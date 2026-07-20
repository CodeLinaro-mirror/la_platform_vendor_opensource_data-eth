/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * eth_qos_bus - Lightweight bus-style manager for ETHQOS net_device instances
 *
 * - Provides a simple bus (eth_qos_bus)
 * - Wraps each tracked net_device with a struct device (struct eth_qos_device)
 * - Manages registration/unregistration and iteration over tracked devices
 * - Uses a netdevice notifier to auto-track ETHQOS net_device instances
 */

#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/rtnetlink.h>
#include <linux/suspend.h>

#include "eth_qos_bus.h"
#include "eth_qos.h"
#include "eth_qos_mgr.h"

#define MAX_ETH_QOS_DEVICES 16
#define ETHQOS_DRV_NAME "qcom-ethqos"

/* Known compatible strings for ETHQOS platforms (see dwmac-qcom-ethqos.c) */
static const char * const ethqos_compat_list[] = {
	"qcom,echo-ethqos",
};


static struct eth_qos_device *eth_qos_devices[MAX_ETH_QOS_DEVICES];

static void eth_qos_dev_release(struct device *dev)
{
	struct eth_qos_device *edev = to_eth_qos_device(dev);

	kfree_sensitive(edev);
}

/* Device type for ethlib devices  */
static struct device_type eth_qos_dev_type = {
	.name    = "eth_qos_device",
	.release = eth_qos_dev_release,
};

static int eth_qos_match(struct device *dev, const struct device_driver *drv)
{
	/* Minimal match: we only care about our device type; no real drivers */
	return dev->type == &eth_qos_dev_type;
}

static struct bus_type eth_qos_bus = {
	.name  = "eth_qos",
	.match = eth_qos_match,
};

static bool eth_qos_bus_match_ndev(struct net_device *ndev)
{
	struct device *dev = ndev->dev.parent ? ndev->dev.parent : &ndev->dev;

	if (dev && dev->driver && dev->driver->name &&
	    !strcmp(dev->driver->name, ETHQOS_DRV_NAME))
		return true;

	if (ndev->dev.of_node) {
		size_t i;
		for (i = 0; i < ARRAY_SIZE(ethqos_compat_list); i++) {
			if (of_device_is_compatible(ndev->dev.of_node, ethqos_compat_list[i]))
				return true;
		}
	}

	return false;
}

/* Helpers */
static int eth_index_of(struct eth_qos_device *edev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		if (eth_qos_devices[i] == edev)
			return i;
	}
	return -1;
}

static int eth_index_of_ndev(struct net_device *ndev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		if (eth_qos_devices[i] && eth_qos_devices[i]->ndev == ndev)
			return i;
	}
	return -1;
}

static int eth_find_free_slot(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		if (!eth_qos_devices[i])
			return i;
	}
	return -ENOSPC;
}

static void eth_qos_bus_detach_ndev(struct net_device *ndev);

/* Public API: Bus lifecycle */
int eth_qos_bus_init(void)
{
	int rc;

	memset(eth_qos_devices, 0, sizeof(eth_qos_devices));

	rc = bus_register(&eth_qos_bus);
	if (rc) {
		eth_qos_log_err(NULL, "eth_qos_bus: failed to register bus: %d", rc);
		return rc;
	}

	return 0;
}

void eth_qos_bus_exit(void)
{
	int i;

	/* Ensure all devices are gone */
	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		if (eth_qos_devices[i]) {
			struct eth_qos_device *edev = eth_qos_devices[i];

			eth_qos_log_cfg(NULL, "eth_qos_bus: device %s still registered at exit",
					dev_name(&edev->dev));
			eth_qos_bus_detach_ndev(edev->ndev);
		}
	}

	bus_unregister(&eth_qos_bus);
}

void eth_qos_bus_free(struct eth_qos_device *edev)
{
	int idx;

	if (!edev)
		return;

	idx = eth_index_of(edev);
	if (idx >= 0)
		eth_qos_devices[idx] = NULL;
}

/* Public API: Query helpers */
int eth_qos_bus_get_count(void)
{
	int i, count = 0;

	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		if (eth_qos_devices[i])
			count++;
	}
	return count;
}

struct net_device *eth_qos_bus_get_first(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		if (eth_qos_devices[i] && eth_qos_devices[i]->ndev)
			return eth_qos_devices[i]->ndev;
	}
	return NULL;
}

struct net_device *eth_qos_bus_get_netdev_by_name(const char *name)
{
	int i;

	if (!name || !name[0])
		return NULL;

	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		struct eth_qos_device *edev = eth_qos_devices[i];
		if (!edev || !edev->ndev)
			continue;
		if (!strcmp(edev->ndev->name, name))
			return edev->ndev;
	}
	return NULL;
}

struct net_device *eth_qos_bus_get_netdev_by_ifindex(int ifindex)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		struct eth_qos_device *edev = eth_qos_devices[i];
		if (!edev || !edev->ndev)
			continue;
		if (edev->ndev->ifindex == ifindex)
			return edev->ndev;
	}
	return NULL;
}

int eth_qos_bus_fill_netdev_array(struct net_device **array, int max)
{
	int i, n = 0;

	if (!array || max <= 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		struct eth_qos_device *edev = eth_qos_devices[i];
		if (!edev || !edev->ndev)
			continue;
		if (n >= max)
			break;
		array[n++] = edev->ndev;
	}

	return n;
}

int eth_qos_bus_for_each(int (*cb)(struct net_device *ndev, void *data), void *data)
{
	int i, rc = 0;

	if (!cb)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(eth_qos_devices); i++) {
		struct eth_qos_device *edev = eth_qos_devices[i];
		if (!edev || !edev->ndev)
			continue;
		rc = cb(edev->ndev, data);
		if (rc)
			break;
	}

	return rc;
}

static int eth_qos_bus_attach_ndev(struct net_device *ndev)
{
	struct eth_qos_device *edev;
	int idx, ret;

	if (!eth_qos_bus_match_ndev(ndev))
		return 0;

	/* Skip if already present */
	if (eth_index_of_ndev(ndev) >= 0)
		return 0;

	/* Reserve capacity before registering the device / creating sysfs */
	idx = eth_find_free_slot();
	if (idx < 0) {
		eth_qos_log_err(NULL, "eth_qos_bus: no free slots to attach %s", ndev->name);
		return -ENOSPC;
	}

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev) {
		eth_qos_log_err(NULL, "eth_qos_bus: alloc failed for %s", ndev->name);
		return -ENOMEM;
	}
	edev->ndev = ndev;
	edev->dev.parent = ndev->dev.parent ? ndev->dev.parent : &ndev->dev;
	edev->dev.bus    = &eth_qos_bus;
	edev->dev.type   = &eth_qos_dev_type;
	dev_set_name(&edev->dev, "%s-qos", ndev->name);

	ret = device_register(&edev->dev);
	if (ret) {
		eth_qos_log_err(NULL, "eth_qos_bus: device_register failed for %s: %d",
				ndev->name, ret);
		put_device(&edev->dev);
		return ret;
	}

	edev->sysfs_kobj = &ndev->dev.kobj;

	/* Create QoS sysfs under eth */
	ret = eth_qos_create_sysfs(edev);
	if (ret) {
		eth_qos_log_err(NULL, "eth_qos_bus: qos sysfs create failed for %s: %d",
				ndev->name, ret);
		device_unregister(&edev->dev);
		return ret;
	}

	/* Track edev in eth_qos_devices array */
	eth_qos_devices[idx] = edev;

	eth_qos_log_cfg(NULL, "eth_qos_bus: attached %s", ndev->name);

	if (ndev->ethtool_ops && ndev->ethtool_ops->get_drvinfo)
		ndev->ethtool_ops->get_drvinfo(ndev, &edev->drv_info);

	eth_qos_dev_cfg(edev, "addr: %s, driver: %s %s, firmware: %s, erom: %s",
		       edev->drv_info.bus_info,
		       edev->drv_info.driver, edev->drv_info.version,
		       edev->drv_info.fw_version, edev->drv_info.erom_version);
	return 0;
}

static void eth_qos_bus_detach_ndev(struct net_device *ndev)
{
	struct eth_qos_device *edev;
	int idx;

	idx = eth_index_of_ndev(ndev);
	if (idx < 0)
		return;
	edev = eth_qos_devices[idx];

	eth_qos_mgr_release(ndev);

	/* Remove QoS sysfs first */
	eth_qos_remove_sysfs(edev->qos_kobj,
				edev->qos_tc_params_kobj,
				edev->qos_cfg_params_kobj);
	edev->qos_kobj           = NULL;
	edev->qos_tc_params_kobj = NULL;
	edev->qos_cfg_params_kobj = NULL;
	edev->sysfs_kobj          = NULL;

	/* Remove from tracking array before unregistering */
	eth_qos_devices[idx] = NULL;
	device_unregister(&edev->dev);

	eth_qos_log_cfg(NULL, "eth_qos_bus: detached %s", ndev->name);
}

/* Netdevice notifier to auto-attach/detach */
static int eth_netdev_event(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_REGISTER:
		eth_qos_bus_attach_ndev(ndev);
		break;
	case NETDEV_DOWN:
		eth_qos_mgr_clear_rx_sw(ndev);
		break;
	case NETDEV_UNREGISTER:
		eth_qos_bus_detach_ndev(ndev);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block eth_nb = {
	.notifier_call = eth_netdev_event,
};

static int eth_pm_event(struct notifier_block *nb, unsigned long event, void *unused)
{
	struct net_device *ndev;
	int ret;

	switch (event) {
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		rtnl_lock();
		for_each_netdev(&init_net, ndev) {
			if (!eth_qos_bus_match_ndev(ndev))
				continue;
			if (!netif_running(ndev))
				continue;

			ret = eth_qos_mgr_replay(ndev);
			if (ret)
				eth_qos_log_err(NULL, "eth_qos_bus: PM replay failed for %s: %d",
						ndev->name, ret);
		}
		rtnl_unlock();
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block eth_pm_nb = {
	.notifier_call = eth_pm_event,
};

/* Expose init/exit that include notifier registration and initial scan */
int eth_qos_bus_init_with_scan(void)
{
	struct net_device *ndev;
	int rc;

	rc = eth_qos_bus_init();
	if (rc)
		return rc;

	rc = register_netdevice_notifier(&eth_nb);
	if (rc) {
		eth_qos_log_err(NULL, "eth_qos_bus: register_netdevice_notifier failed: %d", rc);
		eth_qos_bus_exit();
		return rc;
	}

	rc = register_pm_notifier(&eth_pm_nb);
	if (rc) {
		eth_qos_log_err(NULL, "eth_qos_bus: register_pm_notifier failed: %d", rc);
		unregister_netdevice_notifier(&eth_nb);
		eth_qos_bus_exit();
		return rc;
	}

	/* Initial scan to catch already-registered devices */
	rtnl_lock();
	for_each_netdev(&init_net, ndev) {
		eth_qos_bus_attach_ndev(ndev);
	}
	rtnl_unlock();

	return 0;
}

void eth_qos_bus_exit_with_notifier(void)
{
	unregister_pm_notifier(&eth_pm_nb);
	unregister_netdevice_notifier(&eth_nb);
	eth_qos_bus_exit();
}
