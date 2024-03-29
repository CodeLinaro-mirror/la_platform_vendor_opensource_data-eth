/* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include "stmmac.h"
#include "ioss/include/linux/msm/ioss.h"
#include "emac_ipa_intf.h"
#include "dwmac-qcom-ethqos.h"
#include "common.h"

void *ipc_stmmac_log_ctxt;

struct stmmac_ioss_device {
	struct ioss_device *idev;
	struct stmmac_priv *_priv;
};

static void *stmmac_ioss_dma_alloc(struct ioss_device *idev,
				   size_t size, dma_addr_t *daddr, gfp_t gfp,
				   struct ioss_mem_allocator *alctr)
{
	return alctr->alloc(idev, size, daddr, gfp, alctr);
}

static void stmmac_ioss_dma_free(struct ioss_device *idev,
				 size_t size, void *buf, dma_addr_t daddr,
				 struct ioss_mem_allocator *alctr)
{
	return alctr->free(idev, size, buf, daddr, alctr);
}

static void *stmmac_ioss_alloc_descs(struct net_device *ndev, size_t size,
				     dma_addr_t *daddr, gfp_t gfp, struct mem_ops *mem_ops,
				     struct channel_info *ch_info)
{
	struct ioss_channel *ch = ch_info->client_ch_priv;
	return stmmac_ioss_dma_alloc(ioss_ch_dev(ch), size, daddr, gfp,
			ch->config.desc_alctr);
}

static void *stmmac_ioss_alloc_buf(struct net_device *ndev, size_t size,
				   dma_addr_t *daddr, gfp_t gfp, struct mem_ops *mem_ops,
				   struct channel_info *ch_info)
{
	struct ioss_channel *ch = ch_info->client_ch_priv;

	return stmmac_ioss_dma_alloc(ioss_ch_dev(ch), size, daddr, gfp,
			ch->config.buff_alctr);
}

static void stmmac_ioss_free_descs(struct net_device *ndev, void *buf, size_t size,
				   dma_addr_t *daddr, struct mem_ops *mem_ops,
				   struct channel_info *ch_info)
{
	struct ioss_channel *ch = ch_info->client_ch_priv;

	return stmmac_ioss_dma_free(ioss_ch_dev(ch), size, buf, *daddr,
			ch->config.desc_alctr);
}

static void stmmac_ioss_free_buf(struct net_device *ndev, void *buf, size_t size,
				 dma_addr_t *daddr, struct mem_ops *mem_ops,
				 struct channel_info *ch_info)
{
	struct ioss_channel *ch = ch_info->client_ch_priv;

	return stmmac_ioss_dma_free(ioss_ch_dev(ch), size, buf, *daddr,
			ch->config.buff_alctr);
}

static int stmmac_ioss_open_device(struct ioss_device *idev)
{
	struct stmmac_ioss_device *stmmac_dev;

	ioss_dev_dbg(idev, "%s", __func__);

	stmmac_dev = kzalloc(sizeof(*stmmac_dev), GFP_KERNEL);
	if (!stmmac_dev)
		return -ENOMEM;

	stmmac_dev->idev = idev;
	stmmac_dev->_priv = netdev_priv(idev->net_dev);

	idev->private = stmmac_dev;

	return 0;
}

static int stmmac_ioss_close_device(struct ioss_device *idev)
{
	struct stmmac_ioss_device *stmmac_dev = idev->private;

	ioss_dev_dbg(idev, "%s", __func__);

	kfree_sensitive(stmmac_dev);

	return 0;
}

static int stmmac_ioss_request_channel(struct ioss_channel *ch)
{
	int i;
	int rc = -EFAULT;
	struct request_channel_input ipa_channel_info;
	enum channel_dir direction =
			(ch->direction == IOSS_CH_DIR_RX) ?
				CH_DIR_RX : CH_DIR_TX;
	struct channel_info *ring;

	ioss_dev_log(ioss_ch_dev(ch), "%s, ring_size=%d, buf_size=%d, dir=%d",
		     __func__, ch->config.ring_size, ch->config.buff_size,
			ch->direction);

	ipa_channel_info.mem_ops = kzalloc(sizeof(*ipa_channel_info.mem_ops), GFP_KERNEL);
	if (!ipa_channel_info.mem_ops)
		return -ENOMEM;

	ipa_channel_info.mem_ops->alloc_descs = stmmac_ioss_alloc_descs;
	ipa_channel_info.mem_ops->alloc_buf = stmmac_ioss_alloc_buf;
	ipa_channel_info.mem_ops->free_descs = stmmac_ioss_free_descs;
	ipa_channel_info.mem_ops->free_buf = stmmac_ioss_free_buf;
	ipa_channel_info.client_ch_priv = ch;

	ipa_channel_info.ndev = ioss_ch_dev(ch)->net_dev;
	ipa_channel_info.desc_cnt = ch->config.ring_size;
	ipa_channel_info.ch_dir = direction;
	ipa_channel_info.buf_size = ch->config.buff_size;
	ipa_channel_info.ch_flags = STMMAC_CONTIG_BUFS;
	ipa_channel_info.flags = GFP_KERNEL;
	ring = request_channel(&ipa_channel_info);

	if (!ring) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to request ring\n");
		kfree_sensitive(ipa_channel_info.mem_ops);
		return -ENOMEM;
	}

	ch->tail_ptr_addr = ipa_channel_info.tail_ptr_addr;

	rc = ioss_channel_add_desc_mem(ch,
				       ring->desc_addr.desc_virt_addrs_base,
				       ring->desc_addr.desc_dma_addrs_base,
				       ring->desc_size * ring->desc_cnt);

	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to add desc mem\n");
		goto release_channel;
	}

	for (i = 0; i < ring->desc_cnt; i++) {
		void *vaddr = (void *)ring->buff_pool_addr.buff_pool_va_addrs_base[0];
		void *addr = vaddr + (ring->buf_size * i);
		dma_addr_t daddr = ring->buff_pool_addr.buff_pool_dma_addrs_base[0] +
			(ring->buf_size * i);

		rc = ioss_channel_add_buff_mem(ch, addr, daddr,
					       ring->buf_size);

		if (rc) {
			ioss_dev_err(ioss_ch_dev(ch), "Failed to add buff mem\n");
			goto release_desc;
		}
	}

	ch->id = ring->channel_num;
	ch->private = ring;

	return 0;

release_desc:
	ioss_channel_del_desc_mem(ch, ring->desc_addr.desc_virt_addrs_base);
	for (i = 0; i < ring->desc_cnt; i++) {
		void *vaddr = (void *)ring->buff_pool_addr.buff_pool_va_addrs_base[0];
		void *addr = vaddr + (ring->buf_size * i);

		ioss_channel_del_buff_mem(ch, addr);
	}
release_channel:
	release_channel(ioss_ch_dev(ch)->net_dev, ring);
	kfree_sensitive(ipa_channel_info.mem_ops);
	return -ENOMEM;
}

static int stmmac_ioss_release_channel(struct ioss_channel *ch)
{
	int i;
	struct channel_info *ring = ch->private;
	struct mem_ops *mem_ops = ring->mem_ops;

	ioss_dev_log(ioss_ch_dev(ch), "Release ring %d\n", ring->channel_num);

	ioss_channel_del_desc_mem(ch, ring->desc_addr.desc_virt_addrs_base);

	for (i = 0; i < ring->desc_cnt; i++) {
		void *vaddr = (void *)ring->buff_pool_addr.buff_pool_va_addrs_base[0];
		void *addr = vaddr + (ring->buf_size * i);

		ioss_channel_del_buff_mem(ch, addr);
	}

	release_channel(ioss_ch_dev(ch)->net_dev, ring);
	kfree_sensitive(mem_ops);

	ch->id = -1;
	ch->private = NULL;

	return 0;
}

static int stmmac_ioss_enable_channel(struct ioss_channel *ch)
{
	struct channel_info *ring = ch->private;

	ioss_dev_dbg(ioss_ch_dev(ch), "%s", __func__);

	return start_channel(ioss_ch_dev(ch)->net_dev, ring);
}

static int stmmac_ioss_disable_channel(struct ioss_channel *ch)
{
	struct channel_info *ring = ch->private;

	ioss_dev_dbg(ioss_ch_dev(ch), "%s", __func__);

	return stop_channel(ioss_ch_dev(ch)->net_dev, ring);
}

static int stmmac_ioss_request_event(struct ioss_channel *ch)
{
	int rc;
	int wdt;
	struct channel_info *ring = ch->private;

	ioss_dev_log(ioss_ch_dev(ch), "Request EVENT: paddr=%pap, DATA: %llu",
		&ch->event.paddr, ch->event.data);

	if (ioss_channel_map_event(ch))
		return -EFAULT;

	rc = request_event(ioss_ch_dev(ch)->net_dev,ring,
						ch->event.daddr, ch->event.data);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to request event\n");
		return rc;
	}

	// call for rx and not tx direction
	if(ch->direction == IOSS_CH_DIR_RX)
	{
		// Each wdt unit is 2048 ns (~2 uS).
		wdt = ch->event.mod_usecs_max / 2;
		ioss_dev_log(ioss_ch_dev(ch),"EVENT: wdt=%d\n", wdt);

		rc = set_event_mod(ioss_ch_dev(ch)->net_dev,ring, wdt);
		if (rc) {
			ioss_dev_err(ioss_ch_dev(ch), "Failed to set interrupt moderation\n");
			release_event(ioss_ch_dev(ch)->net_dev,ring);
			return rc;
		}
	}

	return 0;
}

static int stmmac_ioss_release_event(struct ioss_channel *ch)
{
	struct channel_info *ring = ch->private;

	ioss_dev_dbg(ioss_ch_dev(ch), "%s", __func__);

	ioss_dev_log(ioss_ch_dev(ch), "Release EVENT: daddr=%pad, DATA: %llu",
			&ch->event.daddr, ch->event.data);

	release_event(ioss_ch_dev(ch)->net_dev,ring);
	ioss_channel_unmap_event(ch);

	return 0;
}


enum {
	FLT_TYPE_IP4,
	FLT_TYPE_IP6,
	FLT_TYPE_VLAN,

	/* Must be the last entry */
	FLT_NUM_TYPES,
};

static int stmmac_ioss_enable_event(struct ioss_channel *ch)
{
	int rc;
	struct channel_info *ring = ch->private;

	ioss_dev_dbg(ioss_ch_dev(ch), "%s", __func__);

	rc = enable_event(ioss_ch_dev(ch)->net_dev, ring);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to enable event\n");
		return rc;
	}

	return 0;
}

static int stmmac_ioss_disable_event(struct ioss_channel *ch)
{
	int rc;
	struct channel_info *ring = ch->private;

	ioss_log_msg(NULL, "%s ", __func__);

	rc = disable_event(ioss_ch_dev(ch)->net_dev, ring);
	if (rc) {
		ioss_dev_err(ioss_ch_dev(ch), "Failed to disable event\n");
		return rc;
	}

	return 0;
}

static u64 __get_stats_data(const char *name, u64 data[], const u8 *strings_data, int scount)
{
	int i = 0;
	const char (*strings)[ETH_GSTRING_LEN] = (typeof(strings))strings_data;

	ioss_log_msg(NULL, "%s", __func__);

	/* iterate through strings[], find matchging index */
	for (i = 0; i < scount; i++) {
		if (strcmp(name, strings[i]) == 0)
			return data[i];
	}

	return 0;
}

static int stmmac_ioss_device_statistics(struct ioss_device *idev,
					 struct ioss_device_stats *statistics)
{
	u64 *data;
	u8 *strings;
	int strings_count = 0;
	struct ethtool_stats stats;
	const struct ethtool_ops *ops = idev->net_dev->ethtool_ops;

	ioss_dev_dbg(idev, "%s", __func__);

	if (!ops || !ops->get_sset_count ||
	    !ops->get_ethtool_stats || !ops->get_strings)
		return -EOPNOTSUPP;

	strings_count = ops->get_sset_count(idev->net_dev, ETH_SS_STATS);

	data = kcalloc(strings_count, sizeof(u64), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	strings = kcalloc(strings_count, ETH_GSTRING_LEN, GFP_KERNEL);
	if (!strings) {
		kfree_sensitive(data);
		return -ENOMEM;
	}

	memset(&stats, 0, sizeof(stats));
	stats.n_stats = strings_count;

	rtnl_lock();
	ops->get_ethtool_stats(idev->net_dev, &stats, data);
	rtnl_unlock();

	ops->get_strings(idev->net_dev, ETH_SS_STATS, strings);

	statistics->emac_rx_packets =
		__get_stats_data("mmc_rx_framecount_gb", data, strings, strings_count);
	statistics->emac_tx_packets =
		__get_stats_data("mmc_tx_framecount_gb", data, strings, strings_count);
	statistics->emac_rx_bytes =
		__get_stats_data("mmc_rx_octetcount_gb", data, strings, strings_count);
	statistics->emac_tx_bytes =
		__get_stats_data("mmc_tx_octetcount_gb", data, strings, strings_count);
	statistics->emac_rx_drops =
		__get_stats_data("mmc_rx_fifo_overflow", data, strings, strings_count);
	statistics->emac_rx_pause_frames =
		__get_stats_data("mmc_rx_pause_frames", data, strings, strings_count);
	statistics->emac_tx_pause_frames =
		__get_stats_data("mmc_tx_pause_frame", data, strings, strings_count);

	kfree_sensitive(data);
	kfree_sensitive(strings);

	return 0;
}

static int stmmac_ioss_channel_statistics(struct ioss_channel *ch,
		struct ioss_channel_stats *statistics)
{
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct stmmac_priv *priv = netdev_priv(idev->net_dev);

	if (ch->direction == IOSS_CH_DIR_RX) {
		statistics->desc_unavail = priv->xstats.rxq_stats[ch->id].rx_buf_unav_irq;
		statistics->overflow_error = priv->xstats.overflow_error;
	} else {
		statistics->underflow_error = priv->xstats.tx_underflow;
	}

	return 0;
}

static int stmmac_ioss_channel_status(struct ioss_channel *ch, struct ioss_channel_status *status)
{
	u64 *data;
	int strings_count = 0;
	struct ethtool_stats stats;
	struct ioss_device *idev = ioss_ch_dev(ch);
	const struct ethtool_ops *ops = idev->net_dev->ethtool_ops;
	struct stmmac_priv *priv = netdev_priv(idev->net_dev);

	if (ops == NULL || ops->get_sset_count == NULL ||
		ops->get_ethtool_stats == NULL || ops->get_strings == NULL)
		return -EOPNOTSUPP;

	strings_count = ops->get_sset_count(idev->net_dev, ETH_SS_STATS);

	data = kcalloc(strings_count, sizeof(u64), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memset(&stats, 0, sizeof(stats));
	stats.n_stats = strings_count;

	rtnl_lock();
	ops->get_ethtool_stats(idev->net_dev, &stats, data);
	rtnl_unlock();

	if (ch->direction == IOSS_CH_DIR_RX) {
		status->ring_size = priv->xstats.rxq_stats[ch->id].rxch_desc_ring_len;
		status->head_ptr = priv->xstats.rxq_stats[ch->id].rxch_desc_list_laddr;
		status->tail_ptr = priv->xstats.rxq_stats[ch->id].rxch_desc_tail;
	} else {
		status->ring_size = priv->xstats.txq_stats[ch->id].txch_desc_ring_len;
		status->head_ptr = priv->xstats.txq_stats[ch->id].txch_desc_list_laddr;
		status->tail_ptr = priv->xstats.txq_stats[ch->id].txch_desc_tail;
	}
	kfree_sensitive(data);

	return 0;
}

static int stmmac_ioss_update_skb(struct ioss_channel *ch, struct sk_buff *skb)
{
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct stmmac_priv *priv = netdev_priv(idev->net_dev);

	if (priv->hw->rx_csum)
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

bool stmmac_driver_match(struct device *dev)
{
	bool rc;

	/* Just match the driver name */
	rc = (dev->bus == &platform_bus_type) &&
		!strcmp(to_platform_driver(dev->driver)->driver.name, DRV_NAME);

	ioss_log_dbg(NULL, "%s: MATCH %s, bool = %d, strcmp=%s",
				__func__, dev_name(dev),
				rc,
				to_platform_driver(dev->driver)->driver.name);

	return rc;
}

static struct ioss_driver stmmac_ioss_drv = {
	.name = DRV_NAME "_ioss",
	.match = stmmac_driver_match,
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

