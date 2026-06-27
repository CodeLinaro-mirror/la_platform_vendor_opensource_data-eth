// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/msm/ioss.h>

#include "ethdbg.h"

/*
 * stmmac_ioss_get_ring_size is no longer needed — vaddr and size are
 * obtained directly from ioss_channel.desc_mem at panic capture time.
 */

static void ethdbg_ioss_capture_rx_queue(struct net_device *ndev,
					 const char *ifname, unsigned int q)
{
	struct ioss_channel *ch;
	struct ioss_device *idev = ioss_real_to_idev(ndev->dev.parent);
	struct ioss_interface *iface;
	unsigned long vaddr = 0;
	unsigned int size = 0;
	char owner[ETHDBG_MD_NAME_LEN];
	int ret;

	if (!netif_running(ndev))
		return;

	if (!idev)
		return;

	iface = &idev->interface;

	ioss_for_each_channel(ch, iface) {
		if (ch->direction == IOSS_CH_DIR_RX && ch->id == q && ch->allocated) {
			struct ioss_mem *imem = list_first_entry_or_null(&ch->desc_mem,
								struct ioss_mem, node);
			if (imem) {
				vaddr = (unsigned long)imem->addr;
				size  = imem->size;
			}
			break;
		}
	}

	if (!vaddr || !size)
		return;

	scnprintf(owner, sizeof(owner), "%s-rx%u", ifname, q);

	ret = ethdbg_va_md_add_region(owner, vaddr, size);
	if (ret)
		pr_err("ethdbg: failed to add VA-MD region %s ret %d\n", owner, ret);
}

static void ethdbg_ioss_capture_tx_queue(struct net_device *ndev,
					 const char *ifname, unsigned int q)
{
	struct ioss_channel *ch;
	struct ioss_device *idev = ioss_real_to_idev(ndev->dev.parent);
	struct ioss_interface *iface;
	unsigned long vaddr = 0;
	unsigned int size = 0;
	char owner[ETHDBG_MD_NAME_LEN];
	int ret;

	if (!netif_running(ndev))
		return;

	if (!idev)
		return;

	iface = &idev->interface;

	ioss_for_each_channel(ch, iface) {
		if (ch->direction == IOSS_CH_DIR_TX && ch->id == q && ch->allocated) {
			struct ioss_mem *imem = list_first_entry_or_null(&ch->desc_mem,
								struct ioss_mem, node);
			if (imem) {
				vaddr = (unsigned long)imem->addr;
				size  = imem->size;
			}
			break;
		}
	}

	if (!vaddr || !size)
		return;

	scnprintf(owner, sizeof(owner), "%s-tx%u", ifname, q);

	ret = ethdbg_va_md_add_region(owner, vaddr, size);
	if (ret)
		pr_err("ethdbg: failed to add VA-MD region %s ret %d\n", owner, ret);
}

static const struct ethdbg_capture_ops ethdbg_ioss_capture_ops = {
	.capture_rx_queue = ethdbg_ioss_capture_rx_queue,
	.capture_tx_queue = ethdbg_ioss_capture_tx_queue,
};

void ethdbg_ioss_init(void)
{
	ethdbg_register_capture_ops(&ethdbg_ioss_capture_ops);
}

void ethdbg_ioss_exit(void)
{
	ethdbg_unregister_capture_ops(&ethdbg_ioss_capture_ops);
}
