/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * eth_qos - QoS sysfs structure under eth
 *
 * Creates QoS sysfs directories/files under:
 *   /sys/class/net/<ifname>/eth/qos
 *   /sys/class/net/<ifname>/eth/qos/add_tc_params
 *
 * This module provides only the sysfs structure; no pending table logic.
 */

#ifndef _ETH_QOS_H_
#define _ETH_QOS_H_

#include <linux/ipc_logging.h>
#include <linux/kobject.h>
#include "eth_qos_bus.h"


#define VLAN_LOWER_LIMIT 1
#define VLAN_UPPER_LIMIT 4095

#define ETH_QOS_SUBSYS "eth_qos"
#define ETH_QOS_IPCLOG_PAGES 50

extern void *eth_qos_ipc_log_ctxt;

#define __eth_qos_log_msg(ipcbuf, fmt, args...) \
	do { \
		void *__buf = (ipcbuf); \
		if (__buf) \
			ipc_log_string(__buf, " %s:%d " fmt "\n", \
				       __func__, __LINE__, ##args); \
	} while (0)

#define eth_qos_log_err(dev, fmt, args...) \
	do { \
		dev_err(dev, ETH_QOS_SUBSYS ":ERR:" fmt "\n", ##args); \
		__eth_qos_log_msg(eth_qos_ipc_log_ctxt, "ERR:" fmt, ##args); \
	} while (0)

#define eth_qos_log_msg(dev, fmt, args...) \
	do { \
		dev_dbg(dev, ETH_QOS_SUBSYS fmt, ##args); \
		__eth_qos_log_msg(eth_qos_ipc_log_ctxt, fmt, ##args); \
	} while (0)

/* QoS logging helpers */
#define eth_qos_log_bug(dev, fmt, args...) \
	do { \
		dev_err(dev, ETH_QOS_SUBSYS ":BUG:" fmt "\n", ##args); \
		__eth_qos_log_msg(eth_qos_ipc_log_ctxt, "BUG:" fmt, ##args); \
		dump_stack(); \
	} while (0)

#define eth_qos_log_dbg(dev, fmt, args...) \
	do { \
		dev_dbg(dev, ETH_QOS_SUBSYS ":dbg:" fmt "\n", ##args); \
		__eth_qos_log_msg(eth_qos_ipc_log_ctxt, "dbg:" fmt, ##args); \
	} while (0)

#define eth_qos_log_cfg(dev, fmt, args...) \
	do { \
		dev_info(dev, ETH_QOS_SUBSYS ":cfg:" fmt "\n", ##args); \
		__eth_qos_log_msg(eth_qos_ipc_log_ctxt, "cfg:" fmt, ##args); \
	} while (0)

/* eth_device-context wrappers */
#define eth_qos_dev_err(edev, fmt, args...) \
	do { \
		struct eth_qos_device *__edev = (edev); \
		struct device *dev = __edev ? &__edev->dev : NULL; \
		eth_qos_log_err(dev, "(%s) " fmt, eth_qos_device_name(__edev), ##args); \
	} while (0)

#define eth_qos_dev_bug(edev, fmt, args...) \
	do { \
		struct eth_qos_device *__edev = (edev); \
		struct device *dev = __edev ? &__edev->dev : NULL; \
		eth_qos_log_bug(dev, "(%s) " fmt, eth_qos_device_name(__edev), ##args); \
	} while (0)

#define eth_qos_dev_dbg(edev, fmt, args...) \
	do { \
		struct eth_qos_device *__edev = (edev); \
		struct device *dev = __edev ? &__edev->dev : NULL; \
		eth_qos_log_dbg(dev, "(%s) " fmt, eth_qos_device_name(__edev), ##args); \
	} while (0)

#define eth_qos_dev_cfg(edev, fmt, args...) \
	do { \
		struct eth_qos_device *__edev = (edev); \
		struct device *dev = __edev ? &__edev->dev : NULL; \
		eth_qos_log_cfg(dev, "(%s) " fmt, eth_qos_device_name(__edev), ##args); \
	} while (0)

#define eth_qos_dev_log(edev, fmt, args...) \
	do { \
		struct eth_qos_device *__edev = (edev); \
		struct device *dev = __edev ? &__edev->dev : NULL; \
		eth_qos_log_msg(dev, "(%s) " fmt, eth_qos_device_name(__edev), ##args); \
	} while (0)

/* Create QoS sysfs directories/files under the eth_device context.
 * On success, populates edev->qos_kobj and edev->qos_tc_params_kobj and returns 0.
 */
int eth_qos_create_sysfs(struct eth_qos_device *edev);

/* Remove QoS sysfs directories (safe to call with NULL pointers). */
void eth_qos_remove_sysfs(struct kobject *qos_kobj,
			     struct kobject *tc_params_kobj,
			     struct kobject *cfg_params_kobj);

#endif /* _ETH_QOS_H_ */
