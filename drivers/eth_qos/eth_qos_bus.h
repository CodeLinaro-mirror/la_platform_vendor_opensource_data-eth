/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * eth_qos_bus - Lightweight bus-style manager for ETHQOS net_device instances
 *
 * Provides a simple bus and per-net_device device wrappers so eth can
 * manage multiple net_device instances cleanly (modeled after ioss_bus).
 */

#ifndef _ETH_QOS_BUS_H_
#define _ETH_QOS_BUS_H_

#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/kobject.h>

/* Per-netdev device wrapper owned by eth_qos_bus. */
struct eth_qos_device {
	struct device dev;
	struct net_device *ndev;
	struct ethtool_drvinfo drv_info;
	struct kobject *sysfs_kobj;
	struct kobject *qos_kobj;
	struct kobject *qos_tc_params_kobj;
	struct kobject *qos_cfg_params_kobj;
};

/* Recover eth_device from its embedded struct device */
#define to_eth_qos_device(d) container_of(d, struct eth_qos_device, dev)

#define ETH_QOS_BUS_SUBSYS "eth_qos"

static inline const char *eth_qos_device_name(struct eth_qos_device *edev)
{
	if (!edev)
		return "<noname>";
	if (edev->ndev)
		return edev->ndev->name;
	return dev_name(&edev->dev);
}


/* Bus lifecycle (to be called by eth module init/exit) */
int eth_qos_bus_init(void);
void eth_qos_bus_exit(void);
void eth_qos_bus_free(struct eth_qos_device *edev);
int eth_qos_bus_init_with_scan(void);
void eth_qos_bus_exit_with_notifier(void);

/* Per-netdev device binding is internal; eth_device is bound to netdev
 * via dev_set_drvdata() during NETDEV_REGISTER and cleaned on UNREGISTER.
 * No public per-netdev lifecycle APIs are exposed.
 */

/* Query helpers over tracked ETHQOS net_device instances */
int eth_qos_bus_get_count(void);
struct net_device *eth_qos_bus_get_first(void);
struct net_device *eth_qos_bus_get_netdev_by_name(const char *name);
struct net_device *eth_qos_bus_get_netdev_by_ifindex(int ifindex);
int eth_qos_bus_fill_netdev_array(struct net_device **array, int max);
int eth_qos_bus_for_each(int (*cb)(struct net_device *ndev, void *data), void *data);

#endif /* _ETH_QOS_BUS_H_ */
