/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#ifndef __ETH_HEALTH_H__
#define __ETH_HEALTH_H__

#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/netdevice.h>

/**
 * struct eth_health_iface - per-interface state for eth_health
 * @list:           links into eth_health_interfaces
 * @ifname:         interface name supplied via module param
 * @ndev:           associated net_device (set on NETDEV_UP, cleared on DOWN)
 * @hm_kobj:        kobject for health_monitor
 * @hm_owned:       true when this module created health_monitor/ itself
 * @recovery_kobj:  kobject for health_monitor/recovery/ (always owned)
 */
struct eth_health_iface {
	struct list_head list;
	char ifname[IFNAMSIZ];
	struct net_device *ndev;
	struct kobject *hm_kobj;
	struct kobject *recovery_kobj;
};

#endif /* __ETH_HEALTH_H__ */
