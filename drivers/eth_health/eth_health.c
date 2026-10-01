// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/kernfs.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>

#include <stmmac/stmmac_api.h>
#include "eth_health.h"

#define ETH_HEALTH_DRV_NAME	"eth_health"

/* Module parameter: comma-separated interface names to monitor. */
static char *interfaces;
module_param(interfaces, charp, 0444);
MODULE_PARM_DESC(interfaces, "Comma-separated interface names (e.g. eth0,eth1)");

/*
 * gid to assign to the recovery/reinit_{tx,rx}_chan sysfs nodes, so that
 * eth_health_agent (which runs as radio:radio, not root) can open them for
 * writing. Defaults to the 'radio' group's fixed gid in this distro (see
 * poky/meta-qti-distro/conf/distro/include/user_permissions.inc); overridable
 * at insmod/modprobe time like eth_qos's qos_id parameter.
 * insmod eth_health.ko interfaces=eth0,eth1,eth2 recovery_gid=1002
 */
#define ETH_HEALTH_RADIO_GID	1001

static int recovery_gid = ETH_HEALTH_RADIO_GID;
module_param(recovery_gid, int, 0444);
MODULE_PARM_DESC(recovery_gid, "gid to assign to recovery sysfs nodes (default: radio)");

static LIST_HEAD(eth_health_interfaces);
static DEFINE_MUTEX(eth_health_lock);

static struct net_device *recovery_dev_to_ndev(struct device *dev)
{
	struct kobject *netdev_kobj;

	netdev_kobj = dev->kobj.parent->parent;
	if (!netdev_kobj)
		return NULL;

	return to_net_dev(kobj_to_dev(netdev_kobj));
}

/*
 * eth_health_recover_channel - reinit a DMA channel via stmmac public API.
 *
 * Calls stmmac_api_request_channel() then stmmac_api_release_channel() with
 * ndev/rx/ch_num set and all ring fields zero.
 *
 * With ring_size = 0:
 *   - setup_rx/tx_channel's descriptor init loop is skipped (no NULL deref)
 *   - DMA is briefly programmed with addr 0 / UINT_MAX ring len, but DMA is
 *     stopped throughout so these values are harmless
 *
 * release_channel -> restore -> stmmac_enable_rx/tx_queue then:
 *   - __alloc_dma_rx/tx_desc_resources: allocates fresh DMA coherent ring,
 *     page_pool, buf_pool (for TX also frees pending SKBs via dma_free_tx_skbufs
 *     inside disable_tx_queue during acquire)
 *   - __init_dma_rx/tx_desc_rings: initialises the new ring
 *   - stmmac_reset_rx/tx_queue: resets cur_rx/dirty_rx or cur_tx/dirty_tx
 *   - stmmac_start_rx/tx_dma: restarts DMA
 *   - napi_enable / enable_irq: restores SW processing
 */
static int eth_health_recover_channel(struct net_device *ndev, bool is_rx,
				      u32 chan)
{
	struct stmmac_api_channel ch = {
		.ndev   = ndev,
		.rx     = is_rx,
		.ch_num = (int)chan,
	};
	int ret;

	pr_info(ETH_HEALTH_DRV_NAME ": [%s] %s channel %u: request\n",
		ndev->name, is_rx ? "RX" : "TX", chan);

	ret = stmmac_api_request_channel(&ch);
	if (ret) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] %s channel %u: request failed %d\n",
		       ndev->name, is_rx ? "RX" : "TX", chan, ret);
		return ret;
	}

	pr_info(ETH_HEALTH_DRV_NAME ": [%s] %s channel %u: release\n",
		ndev->name, is_rx ? "RX" : "TX", chan);

	stmmac_api_release_channel(&ch);
	return 0;
}

/* -------------------------------------------------------------------------
 * Sysfs: recovery/tx
 * -------------------------------------------------------------------------
 */
static ssize_t eth_health_show_tx(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = recovery_dev_to_ndev(dev);
	struct stmmac_api_hw_caps caps = {};

	if (!ndev)
		return -EINVAL;

	if (stmmac_api_get_hw_caps(ndev, &caps))
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE,
			 "Write TX channel number (0-%u) to trigger recovery\n"
			 "(request_channel + release_channel)\n",
			 caps.tx_queues - 1);
}

static ssize_t eth_health_store_tx(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct net_device *ndev = recovery_dev_to_ndev(dev);
	struct stmmac_api_hw_caps caps = {};
	int val, ret;

	if (!ndev) {
		pr_err(ETH_HEALTH_DRV_NAME ": TX store: cannot resolve netdev\n");
		return -EINVAL;
	}

	if (stmmac_api_get_hw_caps(ndev, &caps)) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] TX store: get_hw_caps failed\n",
		       ndev->name);
		return -EINVAL;
	}

	ret = kstrtoint(buf, 0, &val);
	if (ret || val < 0 || (u32)val >= caps.tx_queues) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] TX: write channel 0-%u\n",
		       ndev->name, caps.tx_queues - 1);
		return ret ? ret : -EINVAL;
	}

	if (!netif_running(ndev)) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] TX: netif not running\n",
		       ndev->name);
		return -EINVAL;
	}

	if (!netif_carrier_ok(ndev)) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] TX: link down\n", ndev->name);
		return -EINVAL;
	}

	ret = eth_health_recover_channel(ndev, false, (u32)val);
	return ret ? ret : size;
}

static DEVICE_ATTR(reinit_tx_chan, 0660, eth_health_show_tx, eth_health_store_tx);

/* -------------------------------------------------------------------------
 * Sysfs: recovery/rx
 * -------------------------------------------------------------------------
 */
static ssize_t eth_health_show_rx(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = recovery_dev_to_ndev(dev);
	struct stmmac_api_hw_caps caps = {};

	if (!ndev)
		return -EINVAL;

	if (stmmac_api_get_hw_caps(ndev, &caps))
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE,
			 "Write RX channel number (0-%u) to trigger recovery\n"
			 "(request_channel + release_channel)\n",
			 caps.rx_queues - 1);
}

static ssize_t eth_health_store_rx(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct net_device *ndev = recovery_dev_to_ndev(dev);
	struct stmmac_api_hw_caps caps = {};
	int val, ret;

	if (!ndev) {
		pr_err(ETH_HEALTH_DRV_NAME ": RX store: cannot resolve netdev\n");
		return -EINVAL;
	}

	if (stmmac_api_get_hw_caps(ndev, &caps)) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] RX store: get_hw_caps failed\n",
		       ndev->name);
		return -EINVAL;
	}

	ret = kstrtoint(buf, 0, &val);
	if (ret || val < 0 || (u32)val >= caps.rx_queues) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] RX: write channel 0-%u\n",
		       ndev->name, caps.rx_queues - 1);
		return ret ? ret : -EINVAL;
	}

	if (!netif_running(ndev)) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] RX: netif not running\n",
		       ndev->name);
		return -EINVAL;
	}

	if (!netif_carrier_ok(ndev)) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] RX: link down\n", ndev->name);
		return -EINVAL;
	}

	ret = eth_health_recover_channel(ndev, true, (u32)val);
	return ret ? ret : size;
}

static DEVICE_ATTR(reinit_rx_chan, 0660, eth_health_show_rx, eth_health_store_rx);

/* -------------------------------------------------------------------------
 * Sysfs node lifecycle
 * -------------------------------------------------------------------------
 */
static int eth_health_create_sysfs(struct eth_health_iface *iface,
				   struct net_device *ndev)
{
	struct kobject *hm_kobj, *recovery_kobj;
	int ret;

	hm_kobj = kobject_create_and_add("health", &ndev->dev.kobj);
	if (!hm_kobj) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] failed to create health\n",
		       ndev->name);
		return -ENOMEM;
	}

	recovery_kobj = kobject_create_and_add("recovery", hm_kobj);
	if (!recovery_kobj) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] failed to create recovery\n",
		       ndev->name);
		ret = -ENOMEM;
		goto err_recovery_kobj;
	}

	ret = sysfs_create_file(recovery_kobj, &dev_attr_reinit_tx_chan.attr);
	if (ret) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] failed to create recovery/reinit_tx_chan\n",
		       ndev->name);
		goto err_tx;
	}

	ret = sysfs_file_change_owner(recovery_kobj, "reinit_tx_chan",
				       GLOBAL_ROOT_UID, KGIDT_INIT(recovery_gid));
	if (ret) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] failed to chown recovery/reinit_tx_chan to gid %d\n",
		       ndev->name, recovery_gid);
		goto err_tx_chown;
	}

	ret = sysfs_create_file(recovery_kobj, &dev_attr_reinit_rx_chan.attr);
	if (ret) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] failed to create recovery/reinit_rx_chan\n",
		       ndev->name);
		goto err_tx_chown;
	}

	ret = sysfs_file_change_owner(recovery_kobj, "reinit_rx_chan",
				       GLOBAL_ROOT_UID, KGIDT_INIT(recovery_gid));
	if (ret) {
		pr_err(ETH_HEALTH_DRV_NAME ": [%s] failed to chown recovery/reinit_rx_chan to gid %d\n",
		       ndev->name, recovery_gid);
		goto err_rx;
	}

	iface->ndev          = ndev;
	iface->hm_kobj       = hm_kobj;
	iface->recovery_kobj = recovery_kobj;

	return 0;

err_rx:
	sysfs_remove_file(recovery_kobj, &dev_attr_reinit_rx_chan.attr);
err_tx_chown:
	sysfs_remove_file(recovery_kobj, &dev_attr_reinit_tx_chan.attr);
err_tx:
	kobject_put(recovery_kobj);
err_recovery_kobj:
	kobject_put(hm_kobj);
	return ret;
}

static void eth_health_remove_sysfs(struct eth_health_iface *iface)
{
	if (!iface->recovery_kobj)
		return;

	sysfs_remove_file(iface->recovery_kobj, &dev_attr_reinit_rx_chan.attr);
	sysfs_remove_file(iface->recovery_kobj, &dev_attr_reinit_tx_chan.attr);
	kobject_put(iface->recovery_kobj);
	iface->recovery_kobj = NULL;

	if (iface->hm_kobj) {
		kobject_put(iface->hm_kobj);
		iface->hm_kobj = NULL;
	}

	iface->ndev = NULL;
}

/* -------------------------------------------------------------------------
 * Netdevice notifier
 * -------------------------------------------------------------------------
 */
static int eth_health_netdev_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct eth_health_iface *iface;

	mutex_lock(&eth_health_lock);

	list_for_each_entry(iface, &eth_health_interfaces, list) {
		if (strcmp(iface->ifname, ndev->name) != 0)
			continue;

		switch (event) {
		case NETDEV_UP:
			if (!iface->recovery_kobj)
				eth_health_create_sysfs(iface, ndev);
			break;

		case NETDEV_UNREGISTER:
			eth_health_remove_sysfs(iface);
			break;

		default:
			break;
		}
		break;
	}

	mutex_unlock(&eth_health_lock);
	return NOTIFY_DONE;
}

static struct notifier_block eth_health_netdev_notifier = {
	.notifier_call = eth_health_netdev_event,
};

/* -------------------------------------------------------------------------
 * Interface list management
 * -------------------------------------------------------------------------
 */
static int eth_health_add_interface(const char *name)
{
	struct eth_health_iface *iface;

	iface = kzalloc(sizeof(*iface), GFP_KERNEL);
	if (!iface)
		return -ENOMEM;

	strscpy(iface->ifname, name, IFNAMSIZ);
	INIT_LIST_HEAD(&iface->list);

	mutex_lock(&eth_health_lock);
	list_add_tail(&iface->list, &eth_health_interfaces);
	mutex_unlock(&eth_health_lock);

	return 0;
}

static void eth_health_remove_all_interfaces(void)
{
	struct eth_health_iface *iface, *tmp;

	mutex_lock(&eth_health_lock);
	list_for_each_entry_safe(iface, tmp, &eth_health_interfaces, list) {
		eth_health_remove_sysfs(iface);
		list_del(&iface->list);
		kfree(iface);
	}
	mutex_unlock(&eth_health_lock);
}

/* -------------------------------------------------------------------------
 * Module init / exit
 * -------------------------------------------------------------------------
 */
static int __init eth_health_init(void)
{
	char *buf, *token, *tmp;
	struct net_device *ndev;
	struct eth_health_iface *iface;
	int ret;

	pr_info(ETH_HEALTH_DRV_NAME ": loading\n");

	if (!interfaces || !*interfaces) {
		pr_err(ETH_HEALTH_DRV_NAME ": no interfaces specified (use interfaces=eth0,...)\n");
		return -EINVAL;
	}

	buf = kstrdup(interfaces, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	tmp = buf;
	while ((token = strsep(&tmp, ",")) != NULL) {
		if (!*token)
			continue;
		ret = eth_health_add_interface(token);
		if (ret) {
			kfree(buf);
			goto err_cleanup;
		}
	}
	kfree(buf);

	ret = register_netdevice_notifier(&eth_health_netdev_notifier);
	if (ret) {
		pr_err(ETH_HEALTH_DRV_NAME ": failed to register netdev notifier\n");
		goto err_cleanup;
	}

	/*
	 * Handle interfaces that are already up when the module loads.
	 * Iterate under rtnl to safely walk the netdev list.
	 */
	rtnl_lock();
	mutex_lock(&eth_health_lock);
	list_for_each_entry(iface, &eth_health_interfaces, list) {
		ndev = dev_get_by_name(&init_net, iface->ifname);
		if (!ndev)
			continue;
		if (netif_running(ndev) && !iface->recovery_kobj)
			eth_health_create_sysfs(iface, ndev);
		dev_put(ndev);
	}
	mutex_unlock(&eth_health_lock);
	rtnl_unlock();

	pr_info(ETH_HEALTH_DRV_NAME ": loaded\n");
	return 0;

err_cleanup:
	eth_health_remove_all_interfaces();
	return ret;
}

static void __exit eth_health_exit(void)
{
	unregister_netdevice_notifier(&eth_health_netdev_notifier);
	eth_health_remove_all_interfaces();
	pr_info(ETH_HEALTH_DRV_NAME ": unloaded\n");
}

module_init(eth_health_init);
module_exit(eth_health_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ethernet DMA channel health recovery sysfs module");
MODULE_AUTHOR("Qualcomm Technologies, Inc.");
