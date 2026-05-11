// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <net/net_namespace.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include "ethdbg.h"

LIST_HEAD(ethdbg_interfaces);
static DEFINE_MUTEX(ethdbg_lock);

static char interface_name[IFNAMSIZ] = "";
MODULE_PARM_DESC(interface_name, "Network interface name");

static struct notifier_block ethdbg_netdev_notifier;

static void parse_memory_regions(const struct device_node *np,
				  struct list_head *map_list)
{
	struct resource res;
	struct ethdbg_map *map;
	int i = 0;

	while (of_address_to_resource(np, i, &res) == 0) {
		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (!map) {
			pr_err("Failed to allocate memory for region %d, skipping\n", i);
			i++;
			continue;
		}

		map->base_addr = res.start;
		map->size = resource_size(&res);

		/* Get the corresponding reg-name */
		of_property_read_string_index(np, "reg-names", i, &map->name);

		list_add_tail(&map->list, map_list);

		pr_info("Region %d: %s - Base: 0x%llx, Size: 0x%llx\n",
			i, map->name,
			(unsigned long long)res.start,
			(unsigned long long)resource_size(&res));
		i++;
	}
}

static int parse_maps(struct device *dev,
		      struct list_head *map_list)
{
	struct device_node *np, *phy_np, *pcs_np;

	if (!dev || !dev->of_node) {
		pr_err("No device or device node\n");
		return -ENODEV;
	}

	np = dev->of_node;

	/* emac parsing*/
	parse_memory_regions(np, map_list);

	/* phy parsing */
	phy_np = of_parse_phandle(np, "phys", 0);
	if (phy_np) {
		pr_info("Found phy node: %s\n", phy_np->full_name);
		parse_memory_regions(phy_np, map_list);
	}

	/* pcs parsing */
	pcs_np = of_parse_phandle(np, "qcom-xpcs-handle", 0);
	if (pcs_np) {
		pr_info("Found pcs node: %s\n", pcs_np->full_name);
		parse_memory_regions(pcs_np, map_list);
	}

	return 0;
}

static struct ethdbg_interface *find_interface(const char *iface_name)
{
	struct ethdbg_interface *iface;

	mutex_lock(&ethdbg_lock);
	list_for_each_entry(iface, &ethdbg_interfaces, list) {
		if (strcmp(iface->interface_name, iface_name) == 0) {
			mutex_unlock(&ethdbg_lock);
			return iface;
		}
	}
	mutex_unlock(&ethdbg_lock);
	return NULL;
}

static struct ethdbg_interface *add_interface(const char *iface_name)
{
	struct ethdbg_interface *iface;

	iface = find_interface(iface_name);
	if (iface) {
		pr_info("Interface %s already in list\n", iface_name);
		return iface;
	}

	iface = kzalloc(sizeof(*iface), GFP_KERNEL);
	if (!iface)
		return NULL;

	strscpy(iface->interface_name, iface_name, IFNAMSIZ);
	iface->device = NULL;

	mutex_lock(&ethdbg_lock);
	list_add_tail(&iface->list, &ethdbg_interfaces);
	mutex_unlock(&ethdbg_lock);

	pr_info("Added interface %s to list\n", iface_name);
	return iface;
}

static int register_interface_phy(struct ethdbg_interface *iface)
{
	int ret;

	if (!iface->device)
		return -ENODEV;

	if (iface->device->phy_registered)
		return 0;

	ret = ethdbg_dump_register_phy(iface->device);
	if (ret)
		return ret;

	ret = ethdbg_uio_add_phy(iface);
	if (ret)
		return ret;

	iface->device->phy_registered = true;
	return 0;
}

static void unregister_interface_phy(struct ethdbg_interface *iface)
{
	if (!iface->device || !iface->device->phy_registered)
		return;

	ethdbg_uio_del_phy(iface);
	iface->device->phy_registered = false;
}

static void free_map_regions(struct ethdbg_device *dev)
{
	struct ethdbg_map *map, *tmp;

	list_for_each_entry_safe(map, tmp, &dev->map_regions, list) {
		list_del(&map->list);
		kfree(map);
	}
}

static int register_interface_device(struct ethdbg_interface *iface,
				      struct net_device *net_dev)
{
	struct ethdbg_device *dev;
	struct device *parent_dev = net_dev->dev.parent;
	int ret;

	/* Check compatibility before allocating */
	if (!parent_dev || !of_device_is_compatible(parent_dev->of_node, "qcom,echo-ethqos")) {
		netdev_err(net_dev, "Unsupported device\n");
		return -EINVAL;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		netdev_err(net_dev, "Failed to allocate ethdbg_device\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&dev->map_regions);
	dev->net_dev = net_dev;
	dev->netdev_priv = netdev_priv(net_dev);
	dev_hold(net_dev);
	iface->device = dev;

	/* Parse device tree and populate memory map regions */
	parse_maps(parent_dev, &dev->map_regions);

	/* Register for dump capture */
	ret = ethdbg_dump_register(dev, iface->interface_name);
	if (ret) {
		netdev_err(net_dev, "Failed to register dump capture: %d\n", ret);
		goto err_free_maps;
	}

	/* Register UIO device and dump UIO device */
	ret = ethdbg_uio_add(iface);
	if (ret)
		goto err_dump_unregister;

	return 0;

err_dump_unregister:
	ethdbg_dump_unregister(dev);
err_free_maps:
	free_map_regions(dev);
	dev_put(net_dev);
	kfree(dev);
	iface->device = NULL;
	return ret;
}

static void unregister_interface(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev;

	if (!iface)
		return;

	dev = iface->device;
	if (dev) {
		unregister_interface_phy(iface);
		ethdbg_dump_unregister(dev);
		ethdbg_uio_del(iface);
		free_map_regions(dev);
		if (dev->net_dev)
			dev_put(dev->net_dev);
		kfree(dev);
		iface->device = NULL;
	}
}

static int ethdbg_netdev_event(struct notifier_block *nb,
			       unsigned long event, void *ptr)
{
	struct net_device *net_dev = netdev_notifier_info_to_dev(ptr);
	struct ethdbg_interface *iface;
	bool found = false;

	mutex_lock(&ethdbg_lock);

	list_for_each_entry(iface, &ethdbg_interfaces, list) {
		if (strcmp(net_dev->name, iface->interface_name) == 0) {
			found = true;
			break;
		}
	}

	mutex_unlock(&ethdbg_lock);

	if (!found)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
		netdev_info(net_dev, "Registering ethdbg device\n");
		register_interface_device(iface, net_dev);
		break;
	case NETDEV_UP:
		/* phydev is not valid at NETDEV_REGISTER.So, register PHY devices after PHY attach */
		register_interface_phy(iface);
		break;
	case NETDEV_UNREGISTER:
		netdev_info(net_dev, "Unregistering ethdbg device\n");
		unregister_interface(iface);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int set_param_interface(const char *param,
			       const struct kernel_param *kp)
{
	struct net_device *net_dev;
	struct ethdbg_interface *iface;
	int ret;

	ret = param_set_copystring(param, kp);
	if (ret) {
		pr_err("param set failed\n");
		return ret;
	}

	/* Add interface to list */
	iface = add_interface(param);
	if (!iface)
		return -ENOMEM;

	/* Check if interface exists in network stack */
	net_dev = netdev_get_by_name(&init_net, param, NULL, GFP_KERNEL);
	if (!net_dev) {
		pr_info("Interface %s not yet created, will register when it comes up\n",
			param);
		return 0;
	}

	/* Interface exists, register it now */
	ret = register_interface_device(iface, net_dev);
	netdev_put(net_dev, NULL);

	return ret;
}

static const struct kernel_param_ops param_mem_ops = {
	.set = set_param_interface,
};

static struct kparam_string kps = {
	.string = interface_name,
	.maxlen = sizeof(interface_name),
};

module_param_cb(interface_name, &param_mem_ops, &kps, 0644);

static int __init ethdbg_init(void)
{
	int ret;

	/* Register panic notifier */
	ret = ethdbg_panic_init();
	if (ret)
		return ret;

	/* Register netdevice notifier */
	ethdbg_netdev_notifier.notifier_call = ethdbg_netdev_event;
	ret = register_netdevice_notifier(&ethdbg_netdev_notifier);
	if (ret) {
		pr_err("Failed to register netdevice notifier: %d\n", ret);
		ethdbg_panic_deinit();
		return ret;
	}

	return 0;
}
module_init(ethdbg_init);


static void __exit ethdbg_exit(void)
{
	struct ethdbg_interface *iface, *tmp;

	unregister_netdevice_notifier(&ethdbg_netdev_notifier);

	/* Clean up interface list */
	mutex_lock(&ethdbg_lock);
	list_for_each_entry_safe(iface, tmp, &ethdbg_interfaces, list) {
		list_del(&iface->list);
		kfree(iface);
	}
	mutex_unlock(&ethdbg_lock);

	/* Unregister panic notifier */
	ethdbg_panic_deinit();
}
module_exit(ethdbg_exit);

MODULE_LICENSE("GPL");
