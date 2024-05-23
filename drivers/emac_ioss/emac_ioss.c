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
#include "ioss/include/linux/msm/ioss_qos.h"
#include "emac_ipa_intf.h"
#include "dwmac-qcom-ethqos.h"
#include "common.h"
#include "stmmac.h"
#include "dwxgmac2.h"

void *ipc_stmmac_log_ctxt;
struct qos_struct qos_tables;
u32 queue_cnt = 0;

	
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

	ioss_dev_log(ioss_ch_dev(ch), "%s, ring_size=%d, buf_size=%d, dir=%d, id=%d, tc_map=%u",
		     __func__, ch->config.ring_size, ch->config.buff_size,
			ch->direction, ch->channel_num, ch->tc_mapping);

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
	ipa_channel_info.channel_num = ch->channel_num;
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

static int stmmac_get_rx_tc_count(struct list_head *rx_qos, enum data_path path)
{
	int tc_cnt = 0;

	struct qos_routing_rx *temp;
	list_for_each_entry(temp, rx_qos, node) {
		if (path == SW_PATH) {
			if (temp->tc_prio >= 0 && (temp->action == IOSS_QOS_SW_PATH))
				tc_cnt++;
		} else if (path == HW_PATH) {
			if (temp->tc_prio >= 0 && (temp->action == IOSS_QOS_HW_PATH))
				tc_cnt++;
		} else if (path == SW_HW_PATH) {
			tc_cnt++;
		}
	}
	return tc_cnt;
}

static int stmmac_get_tx_tc_count(struct list_head *tx_qos, enum data_path path)
{
	int tc_cnt = 0;

	struct qos_routing_tx *temp;
	list_for_each_entry(temp, tx_qos, node) {
		if (path == SW_PATH) {
			if (temp->tc_prio >= 0 &&  (temp->action == IOSS_QOS_SW_PATH))
				tc_cnt++;
		} else if (path == HW_PATH) {
			if (temp->tc_prio >= 0 && (temp->action == IOSS_QOS_HW_PATH))
				tc_cnt++;
		} else if (path == SW_HW_PATH) {
			tc_cnt++;
		}
	}
	return tc_cnt;
}

static bool compare_ipv6(unsigned char *curr, unsigned char next[16], u8 prefix)
{
	u8 bytes = prefix/8;
	u8 bits = prefix % 8;
	int i = 0;
	u8 bitmask = 0xFF >> bits;

	for (i=0; i < 16 - bytes; i++) {
		if(curr[i] != next[i])
			return false;
	}

	if (bits) {
		if((curr[i] | bitmask) != (next[i] | bitmask))
			return false;
	}

	return true;
}

static bool compare_ipv4(u32 curr, u32 next, u8 mask_len)
{
	u32 ip_bitmask = 0xFFFFFFFF;

	ip_bitmask = ip_bitmask << (32 - mask_len);

	if ((curr ^ next) & ip_bitmask)
		return false;

	return true;
}

static void delete_filter_table(struct list_head *filter_table)
{
	struct dma_filter_table *filter_node, *filter_node_next;

	list_for_each_entry_safe(filter_node, filter_node_next, filter_table, node) {
		list_del(&filter_node->node);
		kfree(filter_node);
	}
}

static bool is_param_unique(struct list_head *filter_table, enum qos_filter_type filter)
{
	struct list_head *filter_node1, *filter_node2;
	struct list_head *filter_node3, *filter_node4;
	struct dma_filter_table *filter_node_curr, *filter_node_next;
	struct pcp_routing *pcp_node_curr, *pcp_node_next;
	u8 mask_len;
	bool is_last = false;

	/*TODO: add some logic to cnt no. of unique filters to be applied*/
	list_for_each_safe(filter_node1, filter_node2, filter_table) {
		if(filter!=PCP)
			filter_node_curr = to_dma_filter_table(filter_node1);
		else if(filter == PCP)
			pcp_node_curr = to_pcp_routing(filter_node1);
		list_for_each_safe(filter_node3, filter_node4, filter_node1) {
			if (filter != PCP)
				filter_node_next = to_dma_filter_table(filter_node3);
			else
				pcp_node_next = to_pcp_routing(filter_node3);

			if(list_is_last(filter_node3, filter_table)) {
				is_last = true;
			} else {
				is_last = false;
			}
			if(!list_is_last(filter_node1, filter_table)) {
				if (filter == PCP) {
					if ((pcp_node_curr->pcp == pcp_node_next->pcp) && 
                                            (pcp_node_curr->dma_ch == pcp_node_next->dma_ch)) {
						list_del(&pcp_node_next->node);
						if(is_last)
							break;
					} else if ((pcp_node_curr->pcp == pcp_node_next->pcp) &&
                                                   (pcp_node_curr->dma_ch != pcp_node_next->dma_ch))
						return false;				
				} else if (filter == DEST_PORT) {
					if (filter_node_curr->dst_port.proto == 
                                            filter_node_next->dst_port.proto &&
                                            filter_node_curr->dst_port.port_num == 
                                            filter_node_next->dst_port.port_num &&
                                            filter_node_curr->dma_ch != filter_node_next->dma_ch) {
						list_del(&filter_node_next->node);
						if(is_last)
							break;
					} else if (filter_node_curr->dst_port.proto == 
                                                   filter_node_next->dst_port.proto &&
                                                   filter_node_curr->dst_port.port_num == 
                                                   filter_node_next->dst_port.port_num &&
                                                   filter_node_curr->dma_ch != filter_node_next->dma_ch)
						return false;
				} else if (filter == SRC_PORT) {
					if (filter_node_curr->src_port.proto == 
                                            filter_node_next->src_port.proto &&
                                            filter_node_curr->src_port.port_num == 
                                            filter_node_next->src_port.port_num  &&
                                            filter_node_curr->dma_ch == filter_node_next->dma_ch) {
						list_del(&filter_node_next->node);
						if(is_last)
							break;
					} else if (filter_node_curr->src_port.proto == filter_node_next->src_port.proto &&
						filter_node_curr->src_port.port_num == filter_node_next->src_port.port_num  &&
						(filter_node_curr->dma_ch != filter_node_next->dma_ch))
						return false;
				} else if (filter == VLAN_ID) {
					if (filter_node_curr->vlan_id == filter_node_next->vlan_id &&
						(filter_node_curr->dma_ch == filter_node_next->dma_ch)) {
						list_del(&filter_node_next->node);
						if(is_last)
							break;
					} else if (filter_node_curr->vlan_id == filter_node_next->vlan_id &&
						(filter_node_curr->dma_ch != filter_node_next->dma_ch))
						return false;
				} else if (filter == DEST_IP) {
					 mask_len = filter_node_curr->ip_dest.dst_mask_length > filter_node_next->ip_dest.dst_mask_length?
								filter_node_next->ip_dest.dst_mask_length: filter_node_curr->ip_dest.dst_mask_length;
					 if(filter_node_curr->dma_ch != filter_node_next->dma_ch) {
						 if(filter_node_curr->ip_dest.ipv6_dst == filter_node_next->ip_dest.ipv6_dst) {
							 if (filter_node_curr->ip_dest.ipv6_dst) {
								 if(compare_ipv6(filter_node_curr->ip_dest.ipv6_dst_addr,
												filter_node_next->ip_dest.ipv6_dst_addr, 128 - mask_len))
									 return false;
							 } else {
								 if(compare_ipv4(filter_node_curr->ip_dest.ipv4_dst_addr,
									filter_node_next->ip_dest.ipv4_dst_addr, mask_len))
									 return false;
							 }
						 }
					 } else {
						 if(filter_node_curr->ip_dest.ipv6_dst == filter_node_next->ip_dest.ipv6_dst) {
							 if (filter_node_curr->ip_dest.ipv6_dst) {
								 if(compare_ipv6(filter_node_curr->ip_dest.ipv6_dst_addr,
												filter_node_next->ip_dest.ipv6_dst_addr, 128 - mask_len)) {
									 list_del(&filter_node_next->node);
									if(is_last)
										break;
								 }
							 } else {
								 if(compare_ipv4(filter_node_curr->ip_dest.ipv4_dst_addr,
												filter_node_next->ip_dest.ipv4_dst_addr, mask_len)) {
									 list_del(&filter_node_next->node);
									if(is_last)
										break;
								 }
							 }
						 }
					 }

				} else if (filter == SRC_IP) {
					mask_len = filter_node_curr->ip_src.src_mask_length > filter_node_next->ip_src.src_mask_length?
						filter_node_next->ip_src.src_mask_length: filter_node_curr->ip_src.src_mask_length;
					if(filter_node_curr->dma_ch != filter_node_next->dma_ch) {
						if(filter_node_curr->ip_src.ipv6_src == filter_node_next->ip_src.ipv6_src) {
							if (filter_node_curr->ip_src.ipv6_src) {
								if(compare_ipv6(filter_node_curr->ip_src.ipv6_src_addr,
									filter_node_next->ip_src.ipv6_src_addr, 128 - mask_len))
				  					return false;
							} else {
								if(compare_ipv4(filter_node_curr->ip_src.ipv4_src_addr,
									filter_node_next->ip_src.ipv4_src_addr, mask_len))
				  					return false;
							}
						}
					} else {
						if(filter_node_curr->ip_src.ipv6_src == filter_node_next->ip_src.ipv6_src) {
							if (filter_node_curr->ip_src.ipv6_src) {
								if(compare_ipv6(filter_node_curr->ip_src.ipv6_src_addr,
									filter_node_next->ip_src.ipv6_src_addr,128 - mask_len)) {
				  					list_del(&filter_node_next->node);
									if(is_last)
										break;
								}
							} else {
								if(compare_ipv4(filter_node_curr->ip_src.ipv4_src_addr,
									filter_node_next->ip_src.ipv4_src_addr, mask_len)) {
				  					list_del(&filter_node_next->node);
									if(is_last)
										break;
								}
							}
						}
					}
				}
				if (filter_node3) {
					if(list_is_last(filter_node3, filter_table)) {
						break;
					}
				} else {
					break;
				}

			}
		}
		if (filter_node1) {
			if(list_is_last(filter_node1, filter_table)) {
				break;
			}
		} else {
			break;
		}
	}

	return true;
}

void convert_ip_addr_to_str(struct sockaddr_storage *addr, u32 *ipv4_addr, unsigned char *ipv6_addr)
{
	if(addr->ss_family == AF_INET) {
		*ipv4_addr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	} else if(addr->ss_family == AF_INET6) {
		memcpy(ipv6_addr, &((struct sockaddr_in6 *)addr)->sin6_addr, 16);
	}
}

enum protocol get_proto(char *s) {
	if(!s)
		return IOSS_IPPROTO_TCP_UDP;
	else if (!strncmp(s, "udp", 3))
		return IOSS_IPPROTO_UDP;
    else if (!strncmp(s, "tcp", 3))
		return IOSS_IPPROTO_TCP;
	else
		return IOSS_IPPROTO_INVALID_PROTO;
}

static enum qos_filter_type find_filter(struct list_head *qos_rx)
{
	enum qos_filter_type filter = INVALID_FILTER;
	struct qos_routing_rx *temp;
	struct qos_routing_rx *rx_node;
	struct list_head *ptr = NULL;
	struct dma_filter_table *filter_node, *filter_node_temp;
	struct pcp_routing *filter_node_pcp;
	struct filter_map_info *filter_map;
	int count = 0;
	int i=0;
	bool unique = 0;
	bool not_a_filter = false;
	u32 ipv4_addr;
	unsigned char ipv6_addr[16];
	int cnt = 0, cnt1=0;
	struct sockaddr_storage *ss;


	INIT_LIST_HEAD(&qos_tables.dma_filter_table);
	INIT_LIST_HEAD(&qos_tables.pcp_route_table);

	list_for_each_entry(temp, qos_rx, node){
		filter_map = (struct filter_map_info *)(temp->filter_info);
	}
	/*check if PCP is unique in filter table*/
	list_for_each_entry(temp, qos_rx, node) {
		/*for some TC if pcp is not given, we can't use it as unique param*/
		if (temp->pcp.len) {
			for(i=0; i< temp->pcp.len; i++) {
				filter_node_pcp = kzalloc(sizeof(struct dma_filter_table), GFP_KERNEL);
				filter_node_pcp->pcp = temp->pcp.arr[i];
				filter_map = (struct filter_map_info *)(temp->filter_info);
				filter_node_pcp->dma_ch = filter_map->channel;
				filter_node_pcp->queue = filter_map->queue;
				filter_node_pcp->tc_prio = temp->tc_prio;
				INIT_LIST_HEAD(&filter_node_pcp->node);
				list_add_tail(&filter_node_pcp->node, &qos_tables.pcp_route_table);
			}
		} else {
			/* create an array with this incase we need to apply filter when no unique param is found*/
			not_a_filter = true;
		}
	}
	
	if (not_a_filter)
		unique = false;
	else
		unique = is_param_unique(&qos_tables.pcp_route_table, PCP);
	if(unique) {
		filter = PCP;
		goto filter_found;
	}

	/*check if dest port is unique in filter table*/
	not_a_filter = false;
	count = 0;
	list_for_each_entry(temp, qos_rx, node) {
		/* dest_port */
		if (temp->dst.len) {
			for(i=0; i< temp->dst.len; i++) {
				cnt++;
				if (temp->dst.arr[i].port_num) {
					filter_node = kzalloc(sizeof(struct dma_filter_table), GFP_KERNEL);
					filter_node->dst_port.port_num = temp->dst.arr[i].port_num;
					filter_node->dst_port.proto = get_proto(temp->dst.arr[i].proto);
					if (filter_node->dst_port.proto == IOSS_IPPROTO_INVALID_PROTO) {
						pr_err("invalid proto for tc = %d\n", temp->tc_prio);
						//return INVALID_FILTER;
					}
					filter_map = (struct filter_map_info *)(temp->filter_info);
					filter_node->dma_ch = filter_map->channel;
					filter_node->tc_prio = temp->tc_prio;
					INIT_LIST_HEAD(&filter_node->node);
					list_add_tail(&filter_node->node, &qos_tables.dma_filter_table);
				}
				else {
					cnt1++;
				}

			}
			if(cnt == cnt1) {
				not_a_filter = true;
				break;
			} else {
				not_a_filter = false;
				cnt = 0;
				cnt1 = 0;
			}
		} else {
			not_a_filter = true;
			break;
		}
	}

	if (not_a_filter)
		unique = false;
	else
		unique = is_param_unique(&qos_tables.dma_filter_table, DEST_PORT);
	if(unique) {
		filter = DEST_PORT;
		goto filter_found;
	} else {
		/* do cleanup*/
		delete_filter_table(&qos_tables.dma_filter_table);
	}

	/*check if src port is unique in filter table*/
	cnt = 0;
	cnt1 = 0;
	count = 0;
	not_a_filter = false;
	list_for_each_entry(temp, qos_rx, node) {
		/* src_port */
		if (temp->src.len) {
			for(i=0; i< temp->src.len; i++) {
				cnt++;
				if (temp->src.arr[i].port_num) {
					filter_node = kzalloc(sizeof(struct dma_filter_table), GFP_KERNEL);
					filter_node->src_port.port_num = temp->src.arr[i].port_num;
					filter_node->src_port.proto = get_proto(temp->src.arr[i].proto);
					if (filter_node->src_port.proto == IOSS_IPPROTO_INVALID_PROTO) {
						pr_err("invalid proto for tc = %d\n", temp->tc_prio);
					}
					filter_map = (struct filter_map_info *)(temp->filter_info);
					filter_node->dma_ch = filter_map->channel;
					filter_node->tc_prio = temp->tc_prio;
					INIT_LIST_HEAD(&filter_node->node);
					list_add_tail(&filter_node->node, &qos_tables.dma_filter_table);
				} else{
					cnt1++;
				}
			}
			if(cnt == cnt1) {
				not_a_filter = true;
				break;
			} else {
				not_a_filter = false;
				cnt = 0;
				cnt1 = 0;
			}
		} else {
			not_a_filter = true;	
			break;
		}
	}

	if (not_a_filter)
		unique = false;
	else
		unique = is_param_unique(&qos_tables.dma_filter_table, SRC_PORT);
	if(unique) {
		filter = SRC_PORT;
		goto filter_found;
	} else {
		/* do cleanup*/
		delete_filter_table(&qos_tables.dma_filter_table);
	}

	/*check if vlan is unique in filter table*/
	count = 0;
	not_a_filter = false;
	list_for_each_entry(temp, qos_rx, node) {
		/* src_port */
		if (temp->vlan_ids.len) {
			for(i=0; i< temp->vlan_ids.len; i++) {
				if (temp->vlan_ids.arr[i]) {
					filter_node = kzalloc(sizeof(struct dma_filter_table), GFP_KERNEL);
					filter_node->vlan_id = temp->vlan_ids.arr[i];
					filter_map = (struct filter_map_info *)(temp->filter_info);
					filter_node->dma_ch = filter_map->channel;
					filter_node->tc_prio = temp->tc_prio;
					INIT_LIST_HEAD(&filter_node->node);
					list_add_tail(&filter_node->node, &qos_tables.dma_filter_table);
				}
			}
		} else {
			not_a_filter = true;
			break;
		}
	}

	if (not_a_filter)
		unique = false;
	else
		unique = is_param_unique(&qos_tables.dma_filter_table, VLAN_ID);
	if(unique) {
		filter = VLAN_ID;
		goto filter_found;
	} else {
		/* do cleanup*/
		delete_filter_table(&qos_tables.dma_filter_table);
	}

	/*check if dst ip addr is unique in filter table*/
	cnt = 0;
	cnt1 = 0;
	count = 0;
	not_a_filter = false;
	list_for_each_entry(temp, qos_rx, node) {
		if(temp->dst.len) {
			for(i=0; i< temp->dst.len; i++) {
				cnt++;
				if (temp->dst.arr[i].address.ss_family) {
					filter_node = kzalloc(sizeof(struct dma_filter_table), GFP_KERNEL);
					convert_ip_addr_to_str(&(temp->dst.arr[i].address), &ipv4_addr, ipv6_addr);
					filter_node->ip_dest.dst_mask_length = temp->dst.arr[i].mask_length;
					if(temp->dst.arr[i].address.ss_family == AF_INET6) {
						for (i=0; i<16; i++)
							filter_node->ip_dest.ipv6_dst_addr[i] = ipv6_addr[i];
						filter_node->ip_dest.ipv6_dst = true;
					} else {
						filter_node->ip_dest.ipv4_dst_addr = ipv4_addr;
						filter_node->ip_dest.ipv6_dst = false;
					}
					//filter_node->ip_dest.dst_mask_length = temp->dst.arr[i].mask_length;
					filter_map = (struct filter_map_info *)(temp->filter_info);
					filter_node->dma_ch = filter_map->channel;
					filter_node->tc_prio = temp->tc_prio;
					INIT_LIST_HEAD(&filter_node->node);
					list_add_tail(&filter_node->node, &qos_tables.dma_filter_table);
				} else {
					cnt1++;
				}
			}
			if(cnt == cnt1) {
				not_a_filter = true;
				break;
			} else {
				not_a_filter = false;
				cnt = 0;
				cnt1 = 0;
			}
		} else {
			not_a_filter = true;
			break;
		}
	}

	if (not_a_filter)
		unique = false;
	else
		unique = is_param_unique(&qos_tables.dma_filter_table, DEST_IP);
	if(unique) {
		filter = DEST_IP;
		goto filter_found;
	} else {
		/* do cleanup*/
		delete_filter_table(&qos_tables.dma_filter_table);
	}

	/*check if src ip addr is unique in filter table*/
	cnt = 0;
	cnt1 = 0;
	count = 0;
	not_a_filter = false;
	list_for_each_entry(temp, qos_rx, node) {
		if(temp->src.len) {
			for(i=0; i< temp->src.len; i++) {
				cnt++;
				if (temp->src.arr[i].address.ss_family) {
					filter_node = kzalloc(sizeof(struct dma_filter_table), GFP_KERNEL);
					convert_ip_addr_to_str(&(temp->src.arr[i].address), &ipv4_addr, ipv6_addr);
					filter_node->ip_src.src_mask_length = temp->src.arr[i].mask_length;
					if(temp->src.arr[i].address.ss_family == AF_INET6) {
						for (i=0; i<16; i++)
							filter_node->ip_src.ipv6_src_addr[i] = ipv6_addr[i];
						filter_node->ip_src.ipv6_src = true;
					} else {
						filter_node->ip_src.ipv4_src_addr = ipv4_addr;
						filter_node->ip_src.ipv6_src = false;
					}
					filter_map = (struct filter_map_info *)(temp->filter_info);
					filter_node->dma_ch = filter_map->channel;
					filter_node->tc_prio = temp->tc_prio;
					INIT_LIST_HEAD(&filter_node->node);
					list_add_tail(&filter_node->node, &qos_tables.dma_filter_table);
				} else {
					cnt1++; 
				}
			}
			if(cnt1 == cnt) {
				not_a_filter = true;
				break;
			} else {
				not_a_filter = false;
				cnt = 0;
				cnt1 = 0;
			}
		} else {
			not_a_filter = true;
			break;
		}
	}

	if (not_a_filter)
		unique = false;
	else
		unique = is_param_unique(&qos_tables.dma_filter_table, SRC_IP);
	if(unique) {
		filter = SRC_IP;
		goto filter_found;
	} else {
		/* do cleanup*/
		delete_filter_table(&qos_tables.dma_filter_table);
	}

	return filter;

filter_found:
	if (filter != PCP) {
		list_for_each_entry(filter_node_temp, &qos_tables.dma_filter_table, node) {
			qos_tables.filter_cnt++;
		}
	}
	pr_info("No. of filters to be installed = %d\n", qos_tables.filter_cnt);
	return filter;
}

static struct response stmmac_prepare_qos_info(struct ioss_device *idev, struct list_head *qos_rx, struct list_head *qos_tx)
{
	enum qos_filter_type filter;
	struct stmmac_priv *priv = netdev_priv(idev->net_dev);
	struct filter_map_info *filter_info;
	int i=0;
	int num_rx_tc = 0, num_tx_tc = 0;
	int num_rx_sw_tc = 0, num_tx_sw_tc = 0;
	int num_rx_hw_tc = 0, num_tx_hw_tc = 0;
	u8 sw_ch = 0, hw_ch = 0, rx_avail = 0, tx_avail = 0;
	u8 channel = 0;
	struct qos_routing_rx *temp, *temp1;
	struct qos_routing_tx *temp_tx;
	int qos_rx_queues = priv->plat->rx_qos_queues_to_use;
	int qos_tx_queues = priv->plat->tx_qos_queues_to_use;
	u8 pcp_mask = 0, pcp_mask_old = 0;;
	int temp_rx_queues = 0;
	int bw_avail = 1000;
	u32 reg_val = 0;
	struct response map_info;

	map_info.err = 0;
	if (qos_rx_queues <= 3 || qos_tx_queues <= 3) {
		pr_err("No. of TX/RX queues not sufficient for QOS\n");
		map_info.err = 1;
	} else {
		/*start rx aggr and filter*/
		/* Get no. of qos queues available and aggregation logic */
		num_rx_tc = stmmac_get_rx_tc_count(qos_rx, SW_HW_PATH);
		ioss_dev_dbg(idev, "%s: num_rx_tc = %d\n", __func__, num_rx_tc);
		num_rx_sw_tc = stmmac_get_rx_tc_count(qos_rx, SW_PATH);
		ioss_dev_dbg(idev, "%s: num_rx_sw_tc = %d\n", __func__, num_rx_sw_tc);
		num_rx_hw_tc = stmmac_get_rx_tc_count(qos_rx, HW_PATH);
		ioss_dev_dbg(idev, "%s: num_rx_hw_tc = %d\n", __func__, num_rx_hw_tc);

		rx_avail = qos_rx_queues - 2;
		if (num_rx_sw_tc) {
			sw_ch = rx_avail + 1;
			hw_ch = rx_avail;
		} else {
			hw_ch = rx_avail + 1;
		}
		/*no. of hw tc's > 3*/
		temp_rx_queues = qos_rx_queues - 1;
		list_for_each_entry(temp1, qos_rx, node) {
			filter_info = kzalloc(sizeof(struct filter_map_info), GFP_KERNEL);
			if(temp1->action == IOSS_QOS_SW_PATH) {
				filter_info->channel = sw_ch;
			} else {
				filter_info->channel = hw_ch;
				if(hw_ch > 2)
					hw_ch --;
			}

			qos_tables.pipe_map.pipe_to_tc_mapping_rx[filter_info->channel] |= (1 << temp1->tc_prio);
			/* We are assigning queues from higher to lower starting from higher TC to lower TC
			* if higher TC and lower TC share a PCP value then the lower TC traffic can go on
			* higher TC queue. If multiple pcp's given and only one pcp is common, we can still add
			* pcp routing to make sure lower TC with other pcp goes to lower queues
			*/
			if (temp_rx_queues) {
				pcp_mask_old = pcp_mask;
				if ((pcp_mask != 0xFE) && temp1->pcp.len) {
					for (i=0; i<temp1->pcp.len; i++) {
						/*pcp=0, BMW scenario*/
						if (temp1->pcp.arr[i] != 0) {
							if(pcp_mask & (1 << temp1->pcp.arr[i])) {
								ioss_dev_dbg(idev, "%s: pcp %d already used by other high priority TC\n",
										__func__, temp1->pcp.arr[i]);
								} else {
									pcp_mask |= 1 << temp1->pcp.arr[i];
									filter_info->queue = temp_rx_queues;
								}
							}
						}
						if ((pcp_mask != pcp_mask_old) && (temp_rx_queues > 0))
							temp_rx_queues--;
					}
				}
				qos_tables.rx_channel_info[filter_info->channel] = temp1->action;
				temp1->filter_info = (void *)(filter_info);
			}
			queue_cnt = qos_rx_queues - temp_rx_queues - 1;

		ioss_dev_dbg(idev, "%s: RX aggregation done\n", __func__);

		filter = find_filter(qos_rx);
		if (filter == INVALID_FILTER) {
			ioss_dev_dbg(idev, "%s: No unique param\n", __func__);
			map_info.err = 1;
			goto err_inval_filter;
		} else {
			ioss_dev_dbg(idev, "%s: %d is unique param\n\n", __func__, filter);
			priv->unique_filter = filter;
		}

		/*end of RX aggr and filtering*/

		/*start tx aggr*/
		num_tx_tc = stmmac_get_tx_tc_count(qos_tx, SW_HW_PATH);
		ioss_dev_dbg(idev, "%s: num_tx_tc = %d\n", __func__, num_tx_tc);
		num_tx_sw_tc = stmmac_get_tx_tc_count(qos_tx, SW_PATH);
		ioss_dev_dbg(idev, "%s: num_tx_sw_tc = %d\n", __func__, num_tx_sw_tc);
		num_tx_hw_tc = stmmac_get_tx_tc_count(qos_tx, HW_PATH);
		ioss_dev_dbg(idev, "%s: num_tx_hw_tc = %d\n", __func__, num_tx_hw_tc);
		ioss_dev_dbg(idev, "%s: tx qos queues = %d\n", __func__, qos_tx_queues);

		sw_ch = 0;
		hw_ch = 0;
		tx_avail = qos_tx_queues - 1;
		list_for_each_entry(temp_tx, qos_tx, node) {
			if(tx_avail == 2) {
				if(num_tx_sw_tc && sw_ch == 0 && temp_tx->action == IOSS_QOS_HW_PATH) {
					sw_ch = 2;
					tx_avail --;
				} else if(num_tx_hw_tc && hw_ch == 0 && temp_tx->action == IOSS_QOS_SW_PATH) {
					hw_ch = 2;
					tx_avail --;
				}
			}
			if(tx_avail > 1) {
				channel = tx_avail;
				if(temp_tx->action == IOSS_QOS_SW_PATH)
					sw_ch = tx_avail;
				else if (temp_tx->action == IOSS_QOS_HW_PATH)
					hw_ch = tx_avail;
				tx_avail--;
			} else {
				if(temp_tx->action == IOSS_QOS_SW_PATH)
					channel = sw_ch;
				else if (temp_tx->action == IOSS_QOS_HW_PATH)
					channel = hw_ch;
			}

			qos_tables.pipe_map.pipe_to_tc_mapping_tx[channel] |= (1 << temp_tx->tc_prio);
			qos_tables.tx_routing_info[channel].acc_bw += temp_tx->cbs_bw.high_bw;
			/* As qos_tables is global and we are memsetting to 0 after clear, default mode to use should be MTL_QUEUE_AVB*/
			if (qos_tables.tx_routing_info[channel].acc_bw && (qos_tables.tx_routing_info[channel].mode_to_use != MTL_QUEUE_DCB))
				qos_tables.tx_routing_info[channel].mode_to_use = MTL_QUEUE_AVB;
			else
				qos_tables.tx_routing_info[channel].mode_to_use = MTL_QUEUE_DCB;
			temp_tx->tx_param_info = (void *)(channel);
			qos_tables.tx_channel_info[channel] = temp_tx->action;
		}
		/*CBS claculation*/
		switch(priv->speed) {
			case SPEED_100000:
				bw_avail = 10000;
				break;
			case SPEED_2500:
				bw_avail = 2500;
				break;
			case SPEED_1000:
				bw_avail = 1000;
				break;
			case SPEED_100:
				bw_avail = 100;
				break;
			case SPEED_10:
				bw_avail = 10;
				break;
			default:
				map_info.err = 1;
				ioss_dev_dbg(idev, "%s: Invalid Speed\n", __func__);
				goto err_inval_speed;
		}

		for (i = priv->plat->tx_queues_to_use - 1; i > 1; i--) {
			if(qos_tables.tx_routing_info[i].acc_bw > bw_avail) {
				qos_tables.tx_routing_info[i].acc_bw = bw_avail;
				qos_tables.tx_routing_info[i].mode_to_use = MTL_QUEUE_DCB;
			}

			ioss_dev_dbg(idev, "%s: acc_bw for ch %d = %d\n", __func__, i, qos_tables.tx_routing_info[i].acc_bw);
			if (qos_tables.tx_routing_info[i].mode_to_use == MTL_QUEUE_AVB) {
				switch(priv->plat->interface) {
					case PHY_INTERFACE_MODE_RGMII:
					case PHY_INTERFACE_MODE_RGMII_ID:
					case PHY_INTERFACE_MODE_RGMII_RXID:
					case PHY_INTERFACE_MODE_RGMII_TXID:
					case PHY_INTERFACE_MODE_SGMII:
						qos_tables.tx_routing_info[i].idle_slope = SGMII_INTERFACE_BIT*1024*qos_tables.tx_routing_info[i].acc_bw/priv->speed;
						qos_tables.tx_routing_info[i].send_slope = (priv->speed - qos_tables.tx_routing_info[i].acc_bw)*SGMII_INTERFACE_BIT*1024/priv->speed;
						qos_tables.tx_routing_info[i].hi_credit = MAX_INTERFERENCE_SIZE*8*1024;
						qos_tables.tx_routing_info[i].low_credit = (-1)*MAX_FRAME_SIZE*8*1024;
						break;
					case PHY_INTERFACE_MODE_2500BASEX:
					case PHY_INTERFACE_MODE_USXGMII:
						qos_tables.tx_routing_info[i].idle_slope = SGMII_2500X_BIT*1024*qos_tables.tx_routing_info[i].acc_bw/priv->speed;
						qos_tables.tx_routing_info[i].send_slope = (priv->speed - qos_tables.tx_routing_info[i].acc_bw)*SGMII_2500X_BIT*1024/priv->speed;
						qos_tables.tx_routing_info[i].hi_credit = MAX_INTERFERENCE_SIZE*8*1024;
						qos_tables.tx_routing_info[i].low_credit = (-1)*MAX_FRAME_SIZE*8*1024;
						break;
					default:
						ioss_dev_dbg(idev, "%s: Invalid interface\n", __func__);
						map_info.err = 1;
						goto err_inval_interface;
						break;
					}
				}
			}
		/*end tx aggr*/

		stmmac_backup_pcp(priv, &qos_tables);

		for (i=0; i<priv->plat->tx_queues_to_use; i++) {
			if (i < 2) {
				map_info.qos_pipe_mapping.pipe_to_tc_mapping_tx[0] = 0;
				map_info.qos_pipe_mapping.pipe_to_tc_mapping_tx[1] = 0;
				map_info.qos_pipe_mapping.is_tx_tc_sw[0] = 0;
				map_info.qos_pipe_mapping.is_tx_tc_sw[1] = 1;
			} else {
				map_info.qos_pipe_mapping.pipe_to_tc_mapping_tx[i] = qos_tables.pipe_map.pipe_to_tc_mapping_tx[i];
				map_info.qos_pipe_mapping.is_tx_tc_sw[i] = qos_tables.tx_channel_info[i] == IOSS_QOS_SW_PATH? 1: 0;
				priv->plat->qos_ch_map.ch_to_tc_map_tx[i] = qos_tables.pipe_map.pipe_to_tc_mapping_tx[i];
				priv->plat->qos_ch_map.tc_tx_info[i] = qos_tables.tx_channel_info[i] == IOSS_QOS_SW_PATH? 1: 0;
			}

			ioss_dev_dbg(idev, "%s: tx ch = %d pipe map = %d\n", __func__, i, map_info.qos_pipe_mapping.pipe_to_tc_mapping_tx[i]);
			ioss_dev_dbg(idev, "%s: tx ch = %d\n info = %d\n\n", __func__, i, map_info.qos_pipe_mapping.is_tx_tc_sw[i]);
		}

		for (i=0; i<priv->plat->rx_queues_to_use; i++){
			if (i < 2) {
				map_info.qos_pipe_mapping.pipe_to_tc_mapping_rx[0] = 0;
				map_info.qos_pipe_mapping.pipe_to_tc_mapping_rx[1] = 0;
				map_info.qos_pipe_mapping.is_rx_tc_sw[0] = 0;
				map_info.qos_pipe_mapping.is_rx_tc_sw[1] = 1;
			} else {
				map_info.qos_pipe_mapping.pipe_to_tc_mapping_rx[i] = qos_tables.pipe_map.pipe_to_tc_mapping_rx[i];
				map_info.qos_pipe_mapping.is_rx_tc_sw[i] = qos_tables.rx_channel_info[i] == IOSS_QOS_SW_PATH? 1: 0;
				priv->plat->qos_ch_map.ch_to_tc_map_rx[i] = qos_tables.pipe_map.pipe_to_tc_mapping_rx[i];
				priv->plat->qos_ch_map.tc_rx_info[i] = qos_tables.rx_channel_info[i] == IOSS_QOS_SW_PATH? 1: 0;
			}
			ioss_dev_dbg(idev, "%s: rx ch = %d pipe map = %d\n", __func__, i, map_info.qos_pipe_mapping.pipe_to_tc_mapping_rx[i]);
			ioss_dev_dbg(idev, "%s: rx ch = %d info = %d\n", __func__, i, map_info.qos_pipe_mapping.is_rx_tc_sw[i]);
		}

		map_info.num_tx_pipes = qos_tx_queues;
		map_info.num_rx_pipes = qos_rx_queues;

	}
	return map_info;
err_inval_filter:
	/* check for any other memory leaks*/
err_inval_speed:
err_inval_interface:
	return map_info;
}

static int stmmac_request_qos(struct ioss_device *idev)
{
	struct net_device *ndev = idev->net_dev;
	struct stmmac_priv *priv = netdev_priv(ndev);

	if(priv->plat->rx_qos_queues_to_use > 3)  {
		/*use this flag temporarily to avoid DMA reset*/
		priv->plat->mac_suspended = true;
		stmmac_reconfigure_dma_resources(ndev, queue_cnt, &qos_tables);
	}
	return 0;
}

static int stmmac_enable_qos(struct ioss_device *idev)
{
	int i=0;
	struct net_device *ndev = idev->net_dev;
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct dma_filter_table *dma_filter_node;

	int ret = 0;

	if (priv->plat->rx_qos_queues_to_use > 3) {
		priv->plat->qos_enabled = true;
		stmmac_enable_qos_queue_cfg(priv, &qos_tables);
		if(priv->unique_filter != PCP) {
			stmmac_enable_qos_filtering(ndev, &qos_tables);
		}
		/*cbs routing*/
		stmmac_config_qos_cbs(priv, &qos_tables);
		qos_tables.filter_cnt = 0;
	}
	return 0;
}

static int stmmac_clear_qos(struct ioss_device *idev)
{
	int i=0, ret=0;
	struct net_device *ndev = idev->net_dev;
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct dma_filter_table *dma_filter_node;

	if (priv->plat->qos_enabled)
		priv->plat->qos_enabled = false;
	else
		return 0;

	/*Go to previous pcp routing*/
	if(priv->plat->rx_qos_queues_to_use > 3) {
		stmmac_restore_qos_queue_cfg(priv, &qos_tables);
		if(priv->unique_filter != PCP) {
			stmmac_remove_qos_filtering(ndev, &qos_tables);
		}
		stmmac_restore_dma_config(ndev, priv->plat->rx_queues_to_use - 1, &qos_tables);
		memset(&qos_tables, 0, sizeof(struct qos_struct));
	}

	return 0;
}

static void find_tc_queue_channel(struct qos_struct *qos_table, u8 tc_prio, u8 *queue, u8 *channel, bool dir_rx)
{
	int i;
	struct pcp_routing *ptr;

	if (dir_rx) {
		for (i = 0; i < ARRAY_SIZE(qos_table->pipe_map.pipe_to_tc_mapping_rx); i++) {
			if ((qos_table->pipe_map.pipe_to_tc_mapping_rx[i]) & (1 << tc_prio)) {
				*channel = i;
				break;
			}
		}
		if (tc_prio < ARRAY_SIZE(qos_table->tc_to_queue_map))
			*queue = qos_table->tc_to_queue_map[tc_prio];
		else
			*queue = 0;
	}
	else {
		for (i = 0; i < ARRAY_SIZE(qos_table->pipe_map.pipe_to_tc_mapping_tx); i++) {
			if ((qos_table->pipe_map.pipe_to_tc_mapping_tx[i]) & (1 << tc_prio)) {
				*queue = i;
				*channel = i;
				return;
			}
		}
	}
}

static bool is_vlan_id_filter_applied(struct stmmac_priv *priv, void* filter_value)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->app_filters); i++) {
		if (priv->app_filters[i].vlan_id == *(u16 *)(filter_value))
			return true;
	}

	return false;
}

static bool is_src_ip_filter_applied(struct stmmac_priv *priv, void *filter_value)
{
	int i, j;
	bool equal;
	u32 ipv4_addr;
	struct src_ip filter_node;
	unsigned char ipv6_addr[16];
	struct qos_filters original_node = *(struct qos_filters *)(filter_value);

	convert_ip_addr_to_str(&original_node.address, &ipv4_addr, ipv6_addr);
	filter_node.src_mask_length = original_node.mask_length;
	if(original_node.address.ss_family == AF_INET6) {
		for (i = 0; i < 16; i++)
			filter_node.ipv6_src_addr[i] = ipv6_addr[i];
		filter_node.ipv6_src = true;
	} else {
		filter_node.ipv4_src_addr = ipv4_addr;
		filter_node.ipv6_src = false;
	}

	for (i = 0; i < ARRAY_SIZE(priv->app_filters); i++) {
		equal = true;
		if (priv->app_filters[i].ip_src.ipv6_src != filter_node.ipv6_src)
			equal = false;
		if (priv->app_filters[i].ip_src.src_mask_length != filter_node.src_mask_length)
			equal = false;
		if (priv->app_filters[i].ip_src.ipv6_src) {
			for (j = 0; j < ARRAY_SIZE(filter_node.ipv6_src_addr); j++) {
				if (priv->app_filters[i].ip_src.ipv6_src_addr[j] != filter_node.ipv6_src_addr[j])
					equal = false;
			}
		}
		else {
			if (priv->app_filters[i].ip_src.ipv4_src_addr != filter_node.ipv4_src_addr)
				equal = false;
		}

		if (equal)
			return true;
	}

	return false;
}

static bool is_dest_ip_filter_applied(struct stmmac_priv *priv, void *filter_value)
{
	int i, j;
	bool equal;
	u32 ipv4_addr;
	struct dest_ip filter_node;
	unsigned char ipv6_addr[16];
	struct qos_filters original_node = *(struct qos_filters *)(filter_value);

	convert_ip_addr_to_str(&original_node.address, &ipv4_addr, ipv6_addr);
	filter_node.dst_mask_length = original_node.mask_length;
	if(original_node.address.ss_family == AF_INET6) {
		for (i = 0; i < 16; i++)
			filter_node.ipv6_dst_addr[i] = ipv6_addr[i];
		filter_node.ipv6_dst = true;
	} else {
		filter_node.ipv4_dst_addr = ipv4_addr;
		filter_node.ipv6_dst = false;
	}

	for (i = 0; i < ARRAY_SIZE(priv->app_filters); i++) {
		equal = true;
		if (priv->app_filters[i].ip_dest.ipv6_dst != filter_node.ipv6_dst)
			equal = false;
		if (priv->app_filters[i].ip_dest.dst_mask_length != filter_node.dst_mask_length)
			equal = false;
		if (priv->app_filters[i].ip_dest.ipv6_dst) {
			for (j = 0; j < ARRAY_SIZE(filter_node.ipv6_dst_addr); j++) {
				if (priv->app_filters[i].ip_dest.ipv6_dst_addr[j] != filter_node.ipv6_dst_addr[j])
					equal = false;
			}
		}
		else {
			if (priv->app_filters[i].ip_dest.ipv4_dst_addr != filter_node.ipv4_dst_addr)
				equal = false;
		}

		if (equal)
			return true;
	}

	return false;
}

static bool is_src_port_filter_applied(struct stmmac_priv *priv, void *filter_value)
{
	struct qos_filters original_node = *(struct qos_filters *)(filter_value);
	struct port filter_node;
	int i;

	filter_node.port_num = original_node.port_num;
	filter_node.proto = get_proto(original_node.proto);

	if (filter_node.proto == IOSS_IPPROTO_INVALID_PROTO) {
		return false;
	}

	for (i = 0; i < ARRAY_SIZE(priv->app_filters); i++) {
		if (priv->app_filters[i].src_port.port_num == filter_node.port_num
			&& priv->app_filters[i].src_port.proto == filter_node.proto)
			return true;
	}

	return false;
}


static bool is_dst_port_filter_applied(struct stmmac_priv *priv, void *filter_value)
{
	struct qos_filters original_node = *(struct qos_filters *)(filter_value);
	struct port filter_node;
	int i;

	filter_node.port_num = original_node.port_num;
	filter_node.proto = get_proto(original_node.proto);

	if (filter_node.proto == IOSS_IPPROTO_INVALID_PROTO) {
		return false;
	}

	for (i = 0; i < ARRAY_SIZE(priv->app_filters); i++) {
		if (priv->app_filters[i].dst_port.port_num == filter_node.port_num
			&& priv->app_filters[i].dst_port.proto == filter_node.proto)
			return true;
	}

	return false;
}

static bool is_filter_applied(struct stmmac_priv *priv, enum qos_filter_type filter_type, void *filter_value)
{
	if (priv->unique_filter_new != filter_type)
		return false;

	if (priv->unique_filter_new == PCP)
		return false;

	switch (filter_type) {
		case VLAN_ID:
			if (is_vlan_id_filter_applied(priv, filter_value))
				return true;
			break;

		case SRC_IP:
			if (is_src_ip_filter_applied(priv, filter_value))
				return true;
			break;

		case DEST_IP:
			if (is_dest_ip_filter_applied(priv, filter_value))
				return true;
			break;

		case SRC_PORT:
			if (is_src_port_filter_applied(priv, filter_value))
				return true;
			break;

		case DEST_PORT:
			if (is_dst_port_filter_applied(priv, filter_value))
				return true;
		default:
			return false;
	}

	return false;
}

#define QOS_ROW_PREFIX(priv, filter_type, filter_value)\
	(is_filter_applied(priv, filter_type, filter_value))? "  " : "# "

static ssize_t stmmac_show_qos(struct ioss_device *idev, char* buf, struct list_head *qos_rx, struct list_head *qos_tx)
{
	size_t i;
	u8 tc_queue   = 0;
	u8 tc_channel = 0;
	struct list_head *ptr;
	struct sockaddr_storage *ss;
	const int ROW_BUFFER   =   60;
	const int TABLE_BUFFER = 4095;
	struct qos_routing_rx *rx_node;
	struct qos_routing_tx *tx_node;

	struct net_device *ndev = idev->net_dev;
	struct stmmac_priv *priv = netdev_priv(ndev);

	char *row = kzalloc(sizeof(char) * ROW_BUFFER, GFP_KERNEL);
	char *table = kzalloc(sizeof(char) * TABLE_BUFFER, GFP_KERNEL);

	scnprintf(row, ROW_BUFFER, "UL TCs: \n");
	strlcat(table, row, TABLE_BUFFER);
	list_for_each(ptr, qos_rx) {
		rx_node = to_qos_routing_rx(ptr);
		scnprintf(row, ROW_BUFFER, "  - %u:\n", rx_node->tc_prio);
		strlcat(table, row, TABLE_BUFFER);

		scnprintf(row, ROW_BUFFER, "    action: %s\n", (rx_node->action == IOSS_QOS_HW_PATH)? "hw" : "sw");
		strlcat(table, row, TABLE_BUFFER);

		scnprintf(row, ROW_BUFFER, "    pcp:\n");
		strlcat(table, row, TABLE_BUFFER);
		for (i = 0; i < rx_node->pcp.len; i++) {
			scnprintf(row, ROW_BUFFER, "      - %u\n", rx_node->pcp.arr[i]);
			strlcat(table, row, TABLE_BUFFER);
		}

		scnprintf(row, ROW_BUFFER, "    vlan:\n");
		strlcat(table, row, TABLE_BUFFER);
		for (i = 0; i < rx_node->vlan_ids.len; i++) {
			scnprintf(row, ROW_BUFFER, "    %s- %u\n", QOS_ROW_PREFIX(priv, VLAN_ID, &rx_node->vlan_ids.arr[i]), rx_node->vlan_ids.arr[i]);
			strlcat(table, row, TABLE_BUFFER);
		}

		scnprintf(row, ROW_BUFFER, "    src:\n");
		strlcat(table, row, TABLE_BUFFER);
		for (i = 0; i < rx_node->src.len; i++) {
			ss = &rx_node->src.arr[i].address;
			if (ss->ss_family == AF_INET) {
				scnprintf(row, ROW_BUFFER, "    %s- %pI4/%u\n",
					QOS_ROW_PREFIX(priv, SRC_IP, &rx_node->src.arr[i]),
					&(((struct sockaddr_in *)ss)->sin_addr),
					rx_node->src.arr[i].mask_length);
			}
			else if (ss->ss_family == AF_INET6) {
				scnprintf(row, ROW_BUFFER, "    %s- %pI6/%u\n",
					QOS_ROW_PREFIX(priv, SRC_IP, &rx_node->src.arr[i]),
					&(((struct sockaddr_in6 *)ss)->sin6_addr),
					rx_node->src.arr[i].mask_length);
			}
			else {
				scnprintf(row, ROW_BUFFER, "    %s- [%s/%u]\n",
					QOS_ROW_PREFIX(priv, SRC_PORT, &rx_node->src.arr[i]),
					rx_node->src.arr[i].proto,
					rx_node->src.arr[i].port_num);
			}

			strlcat(table, row, TABLE_BUFFER);
		}

		scnprintf(row, ROW_BUFFER, "    dst:\n");
		strlcat(table, row, TABLE_BUFFER);
		for (i = 0; i < rx_node->dst.len; i++) {
			ss = &rx_node->dst.arr[i].address;
			if (ss->ss_family == AF_INET) {
				scnprintf(row, ROW_BUFFER, "    %s- %pI4/%u\n",
					QOS_ROW_PREFIX(priv, DEST_IP, &rx_node->dst.arr[i]),
					&(((struct sockaddr_in *)ss)->sin_addr),
					rx_node->dst.arr[i].mask_length);
			}
			else if (ss->ss_family == AF_INET6) {
				scnprintf(row, ROW_BUFFER, "    %s- %pI6/%u\n",
					QOS_ROW_PREFIX(priv, DEST_IP, &rx_node->dst.arr[i]),
					&(((struct sockaddr_in6 *)ss)->sin6_addr),
					rx_node->dst.arr[i].mask_length);
			}
			else {
				scnprintf(row, ROW_BUFFER, "    %s- [%s/%u]\n",
					QOS_ROW_PREFIX(priv, DEST_PORT, &rx_node->dst.arr[i]),
					rx_node->dst.arr[i].proto,
					rx_node->dst.arr[i].port_num);
			}

			strlcat(table, row, TABLE_BUFFER);
		}

		scnprintf(row, ROW_BUFFER, "    smac:\n");
		strlcat(table, row, TABLE_BUFFER);
		for (i = 0; i < rx_node->smac.len; i++) {
			scnprintf(row, ROW_BUFFER, "    %s- %02x:%02x:%02x:%02x:%02x:%02x\n",
					QOS_ROW_PREFIX(priv, SRC_MAC, &rx_node->smac.arr[i]),
					rx_node->smac.arr[i][0],
					rx_node->smac.arr[i][1],
					rx_node->smac.arr[i][2],
					rx_node->smac.arr[i][3],
					rx_node->smac.arr[i][4],
					rx_node->smac.arr[i][5]);
			strlcat(table, row, TABLE_BUFFER);
		}

		scnprintf(row, ROW_BUFFER, "    dmac:\n");
		strlcat(table, row, TABLE_BUFFER);
		for (i = 0; i < rx_node->dmac.len; i++) {
			scnprintf(row, ROW_BUFFER, "    %s- %02x:%02x:%02x:%02x:%02x:%02x\n",
					QOS_ROW_PREFIX(priv, DEST_MAC, &rx_node->dmac.arr[i]),
					rx_node->dmac.arr[i][0],
					rx_node->dmac.arr[i][1],
					rx_node->dmac.arr[i][2],
					rx_node->dmac.arr[i][3],
					rx_node->dmac.arr[i][4],
					rx_node->dmac.arr[i][5]);
			strlcat(table, row, TABLE_BUFFER);
		}

		find_tc_queue_channel(&qos_tables, rx_node->tc_prio, &tc_queue, &tc_channel, true);
		scnprintf(row, ROW_BUFFER, "    queue: %u\n    channel: %u\n", tc_queue, tc_channel);
		strlcat(table, row, TABLE_BUFFER);
	}

	scnprintf(row, ROW_BUFFER, "DL TCs: \n");
	strlcat(table, row, TABLE_BUFFER);
	list_for_each(ptr, qos_tx) {
		tx_node = to_qos_routing_tx(ptr);
		scnprintf(row, ROW_BUFFER,"  - %u:\n", tx_node->tc_prio);
		strlcat(table, row, TABLE_BUFFER);

		scnprintf(row, ROW_BUFFER, "    action: %s\n", (tx_node->action == IOSS_QOS_HW_PATH)? "HW" : "SW");
		strlcat(table, row, TABLE_BUFFER);

		scnprintf(row, ROW_BUFFER, "    bw: %u:%u\n", tx_node->cbs_bw.low_bw, tx_node->cbs_bw.high_bw);
		strlcat(table, row, TABLE_BUFFER);

		find_tc_queue_channel(&qos_tables, tx_node->tc_prio, &tc_queue, &tc_channel, false);
		scnprintf(row, ROW_BUFFER, "    queue: %u\n    channel: %u\n", tc_queue, tc_channel);
		strlcat(table, row, TABLE_BUFFER);
	}

	return snprintf(buf, TABLE_BUFFER, "%s\n", table);
}

static struct ioss_qos_ops stmmac_qos_ops = {
	.prepare_qos = stmmac_prepare_qos_info,
	.request_qos = stmmac_request_qos,
	.enable_qos = stmmac_enable_qos,
	.clear_qos = stmmac_clear_qos,
	.show_qos = stmmac_show_qos,
};



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
	.qos_ops = &stmmac_qos_ops,
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

