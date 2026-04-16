/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * eth - STMMAC helper library
 *
 * Module providing helper interfaces between kernel and userspace for STMMAC.
 * Builds into eth_qos.ko
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/rtnetlink.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/device.h>
#include "eth_qos.h"
#include "eth_qos_mgr.h"

void *eth_qos_ipc_log_ctxt;

static int __init eth_qos_init(void)
{
	int rc;

	eth_qos_ipc_log_ctxt = ipc_log_context_create(ETH_QOS_IPCLOG_PAGES,
						      ETH_QOS_SUBSYS, 0);
	if (!eth_qos_ipc_log_ctxt)
		dev_err(NULL, ETH_QOS_SUBSYS ":ERR:Error creating logging context\n");

	eth_qos_log_cfg(NULL, "init");

	rc = eth_qos_bus_init_with_scan();
	if (rc) {
		eth_qos_log_err(NULL, "bus init with scan failed: %d", rc);
		if (eth_qos_ipc_log_ctxt)
			ipc_log_context_destroy(eth_qos_ipc_log_ctxt);
		eth_qos_ipc_log_ctxt = NULL;
	}

	return rc ? rc : 0;
}

static void __exit eth_qos_exit(void)
{
	eth_qos_log_cfg(NULL, "exit");

	eth_qos_bus_exit_with_notifier();

	if (eth_qos_ipc_log_ctxt)
		ipc_log_context_destroy(eth_qos_ipc_log_ctxt);
	eth_qos_ipc_log_ctxt = NULL;
}

module_init(eth_qos_init);
module_exit(eth_qos_exit);

MODULE_AUTHOR("Data-ETH");
MODULE_DESCRIPTION("Ethernet helper library (eth)");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
