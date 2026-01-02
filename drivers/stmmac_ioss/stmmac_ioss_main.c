/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include "stmmac_ioss.h"

#define STMMAC_IOSS_DESC_SIZE	16

static int stmmac_ioss_open_device(struct ioss_device *idev)
{
	struct stmmac_ioss_device *stmmac_dev;

	stmmac_dev = kzalloc(sizeof(*stmmac_dev), GFP_KERNEL);
	if (!stmmac_dev)
		return -ENOMEM;

	stmmac_dev->idev = idev;
	idev->private = stmmac_dev;

	if (stmmac_ioss_stats_init(idev))
		goto err_stats;

	return 0;

err_stats:
	kfree(stmmac_dev);
	return -ENOMEM;
}

static int stmmac_ioss_close_device(struct ioss_device *idev)
{
	struct stmmac_ioss_device *stmmac_dev = idev->private;

	stmmac_ioss_stats_deinit(idev);
	kfree(stmmac_dev);

	return 0;
}

static int stmmac_ioss_alloc_channel_memory(struct ioss_channel *ch,
					    struct stmmac_api_channel *stmmac_ch,
					    size_t desc_size)
{
	void *desc_virt, *buff_virt;
	dma_addr_t desc_dma_addr, buff_dma_addr;
	size_t desc_total_size = desc_size * ch->config.ring_size;
	size_t buff_size = ch->config.buff_size * ch->config.ring_size;

	desc_virt = ch->config.desc_alctr->alloc(ioss_ch_dev(ch), desc_total_size, &desc_dma_addr,
						 GFP_KERNEL, ch->config.desc_alctr);
	if (!desc_virt) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to allocate descriptor memory");
		return -ENOMEM;
	}

	ioss_dev_log(ioss_ch_dev(ch), "Allocated descriptor: virt=%p dma=%pad size=%zu",
		     desc_virt, &desc_dma_addr, desc_total_size);

	buff_virt = ch->config.buff_alctr->alloc(ioss_ch_dev(ch), buff_size, &buff_dma_addr,
						 GFP_KERNEL, ch->config.buff_alctr);
	if (!buff_virt) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to allocate buffer memory");
		ch->config.desc_alctr->free(ioss_ch_dev(ch), desc_total_size,
					    desc_virt, desc_dma_addr, ch->config.desc_alctr);
		return -ENOMEM;
	}

	ioss_dev_log(ioss_ch_dev(ch), "Allocated buffer: virt=%p dma=%pad size=%zu buff_size=%d",
		     buff_virt, &buff_dma_addr, buff_size, ch->config.buff_size);

	stmmac_ch->ring_base = desc_virt;
	stmmac_ch->ring_base_dma_addr = desc_dma_addr;
	stmmac_ch->buff_base = buff_virt;
	stmmac_ch->buff_base_dma_addr = buff_dma_addr;
	stmmac_ch->buff_total_size = buff_size;

	return 0;
}

static void stmmac_ioss_dealloc_channel_memory(struct ioss_channel *ch,
					       struct stmmac_api_channel *stmmac_ch,
					       size_t desc_size)
{
	size_t desc_total_size = desc_size * ch->config.ring_size;

	ch->config.buff_alctr->free(ioss_ch_dev(ch), stmmac_ch->buff_total_size,
				    stmmac_ch->buff_base, stmmac_ch->buff_base_dma_addr,
				    ch->config.buff_alctr);

	ch->config.desc_alctr->free(ioss_ch_dev(ch), desc_total_size,
				    stmmac_ch->ring_base, stmmac_ch->ring_base_dma_addr,
				    ch->config.desc_alctr);
}

static int stmmac_ioss_add_channel_mem(struct ioss_channel *ch,
				       struct stmmac_api_channel *stmmac_ch,
				       size_t desc_size)
{
	int i, rc;

	rc = ioss_channel_add_desc_mem(ch, stmmac_ch->ring_base,
					stmmac_ch->ring_base_dma_addr,
					desc_size * ch->config.ring_size);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to add desc mem\n");
		return rc;
	}

	for (i = 0; i < ch->config.ring_size; i++) {
		void *addr = stmmac_ch->buff_base + (ch->config.buff_size * i);
		dma_addr_t daddr = stmmac_ch->buff_base_dma_addr + (ch->config.buff_size * i);

		rc = ioss_channel_add_buff_mem(ch, addr, daddr, ch->config.buff_size);
		if (rc) {
			ioss_dev_err(ioss_ch_dev(ch), "Failed to add buff mem at index %d\n", i);
			goto err_add_buff;
		}
	}

	return 0;

err_add_buff:
	while (--i >= 0) {
		void *addr = stmmac_ch->buff_base + (ch->config.buff_size * i);
		ioss_channel_del_buff_mem(ch, addr);
	}
	ioss_channel_del_desc_mem(ch, stmmac_ch->ring_base);
	return rc;
}

static void stmmac_ioss_remove_channel_mem(struct ioss_channel *ch,
					   struct stmmac_api_channel *stmmac_ch)
{
	int i;

	for (i = 0; i < ch->config.ring_size; i++) {
		void *addr = stmmac_ch->buff_base + (ch->config.buff_size * i);
		ioss_channel_del_buff_mem(ch, addr);
	}

	ioss_channel_del_desc_mem(ch, stmmac_ch->ring_base);
}

static int stmmac_ioss_request_channel(struct ioss_channel *ch)
{
	int i;
	int rc = -EFAULT;
	struct stmmac_api_channel *stmmac_ch;
	struct net_device *ndev = ioss_ch_dev(ch)->net_dev;

	ioss_dev_log(ioss_ch_dev(ch), "ring_size=%d, buf_size=%d, dir=%d, ch_num=%d, queue_num=%u",
				 ch->config.ring_size, ch->config.buff_size,
				 ch->direction, ch->channel_num, ch->queue_number);

	stmmac_ch = kzalloc(sizeof(*stmmac_ch), GFP_KERNEL);
	if (!stmmac_ch)
		return -ENOMEM;

	stmmac_ch->ndev = ndev;
	stmmac_ch->rx = (ch->direction == IOSS_CH_DIR_RX);
	stmmac_ch->ch_num = ch->channel_num;
	stmmac_ch->ring_size = ch->config.ring_size;
	stmmac_ch->buff_size = ch->config.buff_size;

	rc = stmmac_ioss_alloc_channel_memory(ch, stmmac_ch, STMMAC_IOSS_DESC_SIZE);
	if (rc)
		goto err_alloc_memory;

	rc = stmmac_api_request_channel(stmmac_ch);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to request channel from stmmac_api\n");
		goto err_request_channel;
	}

	/* Set buffer DMA addresses for each descriptor in the ring */
	for (i = 0; i < ch->config.ring_size; i++) {
		dma_addr_t addr = stmmac_ch->buff_base_dma_addr + (i * ch->config.buff_size);
		stmmac_api_set_desc_addr(stmmac_ch, i, addr);
	}

	ch->tail_ptr_addr = stmmac_ch->tail_ptr;

	rc = stmmac_ioss_add_channel_mem(ch, stmmac_ch, STMMAC_IOSS_DESC_SIZE);
	if (rc)
		goto err_add_channel_mem;

	ch->id = ch->channel_num;
	ch->private = stmmac_ch;

	return 0;

err_add_channel_mem:
	stmmac_api_release_channel(stmmac_ch);
err_request_channel:
	stmmac_ioss_dealloc_channel_memory(ch, stmmac_ch, STMMAC_IOSS_DESC_SIZE);
err_alloc_memory:
	kfree(stmmac_ch);
	return rc;
}

static int stmmac_ioss_release_channel(struct ioss_channel *ch)
{
	struct stmmac_api_channel *stmmac_ch = ch->private;

	ioss_dev_log(ioss_ch_dev(ch), "Release channel %d\n", stmmac_ch->ch_num);

	stmmac_ioss_remove_channel_mem(ch, stmmac_ch);
	stmmac_api_release_channel(stmmac_ch);
	stmmac_ioss_dealloc_channel_memory(ch, stmmac_ch, STMMAC_IOSS_DESC_SIZE);

	kfree(stmmac_ch);
	ch->id = -1;
	ch->private = NULL;

	return 0;
}

static int stmmac_ioss_enable_channel(struct ioss_channel *ch)
{
	int rc;
	struct stmmac_api_channel *stmmac_ch = ch->private;
	struct net_device *ndev = ioss_ch_dev(ch)->net_dev;

	rc = stmmac_api_enable_channel(stmmac_ch);
	if (rc)
		return rc;

	if (ch->direction == IOSS_CH_DIR_RX)
		stmmac_api_queue_dma_map(ndev, ch->queue_number, ch->channel_num);

	return 0;
}

static int stmmac_ioss_disable_channel(struct ioss_channel *ch)
{
	struct stmmac_api_channel *stmmac_ch = ch->private;

	return stmmac_api_disable_channel(stmmac_ch);
}

static int stmmac_ioss_request_event(struct ioss_channel *ch)
{
	int rc;
	struct stmmac_api_channel *stmmac_ch = ch->private;

	stmmac_ch->msi_db_paddr = ch->event.paddr;
	rc = stmmac_api_request_interrupt(stmmac_ch);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to request event\n");
	} else {
		ioss_dev_log(ioss_ch_dev(ch), "MSI configured: paddr=%pap, id=%u, dma_addr=%pad",
			     &stmmac_ch->msi_db_paddr, stmmac_ch->msi_id, &stmmac_ch->msi_db_daddr);
	}

	return rc;
}

static int stmmac_ioss_release_event(struct ioss_channel *ch)
{
	struct stmmac_api_channel *stmmac_ch = ch->private;

	stmmac_api_release_interrupt(stmmac_ch);
	stmmac_ch->msi_db_paddr = 0;
	return 0;
}

static int stmmac_ioss_enable_event(struct ioss_channel *ch)
{
	struct stmmac_api_channel *stmmac_ch = ch->private;

	return stmmac_api_enable_interrupt(stmmac_ch);
}

static int stmmac_ioss_disable_event(struct ioss_channel *ch)
{
	struct stmmac_api_channel *stmmac_ch = ch->private;

	stmmac_api_disable_interrupt(stmmac_ch);

	return 0;
}

static int stmmac_ioss_update_skb(struct ioss_channel *ch, struct sk_buff *skb)
{
	struct net_device *ndev = ioss_ch_dev(ch)->net_dev;

	if ((ndev->features & NETIF_F_RXCSUM) && skb->ip_summed == CHECKSUM_NONE)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	return 0;
}

static struct ioss_driver_ops stmmac_ioss_ops = {
	.open_device = stmmac_ioss_open_device,
	.close_device = stmmac_ioss_close_device,
	.request_channel = stmmac_ioss_request_channel,
	.release_channel = stmmac_ioss_release_channel,
	.enable_channel = stmmac_ioss_enable_channel,
	.disable_channel = stmmac_ioss_disable_channel,
	.request_event = stmmac_ioss_request_event,
	.release_event = stmmac_ioss_release_event,
	.enable_event = stmmac_ioss_enable_event,
	.disable_event = stmmac_ioss_disable_event,
	.get_device_statistics = stmmac_ioss_device_statistics,
	.get_channel_statistics = stmmac_ioss_channel_statistics,
	.get_channel_status = stmmac_ioss_channel_status,
	.update_skb = stmmac_ioss_update_skb,
};

static struct ioss_driver stmmac_ioss_drv = {
	.name = "stmmac_ioss",
	.match = stmmac_api_is_compatible,
	.ops = &stmmac_ioss_ops,
	.filter_types = IOSS_RXF_BE,
};

static int __init stmmac_ioss_init(void)
{
	return ioss_plat_register_driver(&stmmac_ioss_drv, THIS_MODULE);
}
module_init(stmmac_ioss_init);

static void __exit stmmac_ioss_exit(void)
{
	return ioss_plat_unregister_driver(&stmmac_ioss_drv);
}
module_exit(stmmac_ioss_exit);

MODULE_DESCRIPTION("STMMAC IOSS Glue Driver");
MODULE_LICENSE("GPL v2");
