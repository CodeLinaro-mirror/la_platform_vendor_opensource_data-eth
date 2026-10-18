/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/cpumask.h>
#include <linux/etherdevice.h>
#include <linux/string.h>

#include "ioss_i.h"

#if IPA_ETH_API_VER >= 8
bool disable_ipa_tx;
#else
bool disable_ipa_tx = true;
#endif

static int ioss_ipa_set_disable_tx(const char *val, const struct kernel_param *kp)
{
	if (ioss_bus_ipa_connected()) {
		pr_err("ioss: cannot change disable_ipa_tx while IPA is connected\n");
		return -EBUSY;
	}

	return param_set_bool(val, kp);
}

static const struct kernel_param_ops ioss_disable_ipa_tx_ops = {
	.set = ioss_ipa_set_disable_tx,
	.get = param_get_bool,
};

module_param_cb(disable_ipa_tx, &ioss_disable_ipa_tx_ops, &disable_ipa_tx, 0644);
MODULE_PARM_DESC(disable_ipa_tx, "Disable IPA OFFLOAD TX feature globally");

#define to_ipa_dir(ioss_dir) \
	((ioss_dir == IOSS_CH_DIR_TX)? IPA_ETH_PIPE_DIR_TX : IPA_ETH_PIPE_DIR_RX)

#define to_ioss_dir(ipa_dir) \
	((ipa_dir == IPA_ETH_PIPE_DIR_TX) ? IOSS_CH_DIR_TX : IOSS_CH_DIR_RX)

#define IOSS_IPA_RETAIN_FLAGS \
	(NETIF_F_HW_VLAN_CTAG_FILTER | \
	 NETIF_F_HW_VLAN_STAG_FILTER | \
	 NETIF_F_GRO_UDP_FWD)

static enum ipa_eth_pipe_traffic_type to_ipa_traffic_type(enum ioss_traffic_type ioss_type)
{
	static const enum ipa_eth_pipe_traffic_type ipa_map[IOSS_TRAFFIC_TYPE_MAX] = {
		[IOSS_TRAFFIC_BE] = IPA_ETH_PIPE_BEST_EFFORT,
		[IOSS_TRAFFIC_BE_TAGGED] = IPA_ETH_PIPE_BEST_EFFORT_VLAN,
		[IOSS_TRAFFIC_LL] = IPA_ETH_PIPE_LOW_LATENCY,
		[IOSS_TRAFFIC_QOS] = IPA_ETH_PIPE_TRAFFIC_TYPE_QOS,
	};

	return ipa_map[ioss_type];
}

static enum ioss_traffic_type to_ioss_traffic(enum ipa_eth_pipe_traffic_type ipa_type)
{
	static const enum ioss_traffic_type ioss_map[IPA_ETH_PIPE_TRAFFIC_TYPE_MAX] = {
		[IPA_ETH_PIPE_BEST_EFFORT] = IOSS_TRAFFIC_BE,
		[IPA_ETH_PIPE_LOW_LATENCY] = IOSS_TRAFFIC_LL,
		[IPA_ETH_PIPE_BEST_EFFORT_VLAN] = IOSS_TRAFFIC_BE_TAGGED,
		[IPA_ETH_PIPE_TRAFFIC_TYPE_QOS] = IOSS_TRAFFIC_QOS,
	};

	return ioss_map[ipa_type];
}

static void ioss_ipa_rx_callback(void *priv, enum ipa_dp_evt_type evt, unsigned long data)
{
	struct ioss_channel *ch = priv;
	struct sk_buff *skb = (struct sk_buff *)data;
	struct ioss_interface *iface = ch->iface;
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct ioss_driver *idrv = to_ioss_driver(idev->dev.driver);

	if (evt != IPA_RECEIVE)
		return;

	ch->stats.rx_ipa_cb++;

	skb->protocol = eth_type_trans(skb, ioss_iface_to_netdev(iface));
	skb_set_queue_mapping(skb, ch->queue_number);

	if (idrv && idrv->ops->receive_skb) {
		if (ioss_dev_op(idev, receive_skb, ch, skb))
			ch->stats.rx_net_err++;
		return;
	}

	/* Fall back to the Linux networking stack when the driver has no receive_skb hook. */
	iface->exception_stats.rx_packets++;
	iface->exception_stats.rx_bytes += skb->len;
	ioss_dev_op(idev, update_skb, ch, skb);
	if (netif_rx(skb) == NET_RX_DROP) {
		iface->exception_stats.rx_drops++;
		ch->stats.rx_net_err++;
	}
}

#if IPA_ETH_API_VER >= 8
static void ioss_ipa_tx_callback(void *priv, enum ipa_dp_evt_type evt, unsigned long data)
{
	struct ioss_channel *ch = priv;
	struct ioss_iface_priv *ifp = ch->iface->ioss_priv;
	struct ioss_driver *idrv = to_ioss_driver(ioss_ch_dev(ch)->dev.driver);
	struct sk_buff *skb = (struct sk_buff *)data;
	int max_pkts = ifp->ipa_config.offload_max_pkts;
	struct net_device *ndev;
	struct netdev_queue *txq;

	if (evt != IPA_WRITE_DONE)
		return;

	ch->stats.tx_ipa_cb++;

	int outstanding = atomic_dec_return(&ch->ipa_tx_outstanding_pkts);
	bool ready = outstanding <= max_pkts / 2;

	/* Only count wake WM on stop-WM exit; outstanding may cross wake_wm
	 * multiple times per stop-WM hit.
	 */
	if (outstanding == max_pkts / 2 && atomic_read(&ch->ipa_tx_wm_fc_active)) {
		ch->stats.tx_wm_wake++;
		atomic_set(&ch->ipa_tx_wm_fc_active, 0);
	}

	ch->stats.tx_ipa_is_ready = ready;

	if (WARN_RATELIMIT(!idrv || !idrv->ops->xmit_done, "IPA TX: xmit_done is NULL"))
		return;

	/* Only serialize with ndo_start_xmit when -ENOSPC was returned; skip
	 * the lock in the common (no congestion) path to reduce contention.
	 */
	if (atomic_read(&ch->ipa_tx_nospace_sync)) {
		ndev = ioss_iface_to_netdev(ch->iface);
		txq = netdev_get_tx_queue(ndev, ch->queue_number);
		ch->stats.tx_net_lock_bh++;
		__netif_tx_lock_bh(txq);
		atomic_set(&ch->ipa_tx_nospace_sync, 0);
		__netif_tx_unlock_bh(txq);
	}
	idrv->ops->xmit_done(ch, skb, 0, ready);
}

int ioss_ipa_xmit(struct ioss_channel *ch, struct sk_buff *skb)
{
	struct ioss_ch_priv *cp = ch->ioss_priv;
	struct ioss_iface_priv *ifp = ch->iface->ioss_priv;
	int max_pkts = ifp->ipa_config.offload_max_pkts;
	int pkts = atomic_inc_return(&ch->ipa_tx_outstanding_pkts);
	int ret;

	ch->stats.tx_xmit++;

	if (pkts > max_pkts) {
		atomic_dec(&ch->ipa_tx_outstanding_pkts);
		ch->stats.tx_ipa_is_ready = 0;
		atomic_set(&ch->ipa_tx_nospace_sync, 1);
		return -ENOSPC;
	}
	if (pkts == max_pkts && !atomic_read(&ch->ipa_tx_wm_fc_active)) {
		ch->stats.tx_wm_stop++;
		atomic_set(&ch->ipa_tx_wm_fc_active, 1);
	}

	if (skb_is_gso(skb)) {
		struct skb_shared_info *shinfo = skb_shinfo(skb);

		if (shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))
			ch->stats.tx_gso_tcp++;
		else if (shinfo->gso_type & SKB_GSO_UDP_L4)
			ch->stats.tx_gso_udp++;
	}

	ret = ipa_eth_offload_tx_dp(&cp->ipa_pi, skb);
	if (ret) {
		atomic_dec(&ch->ipa_tx_outstanding_pkts);
		if (ret == -ENOSPC) {
			ch->stats.tx_ipa_nospace++;
			ch->stats.tx_ipa_is_ready = 0;
			atomic_set(&ch->ipa_tx_nospace_sync, 1);
		} else {
			ch->stats.tx_ipa_err++;
		}
	} else {
		ch->stats.tx_ipa++;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ioss_ipa_xmit);
#else
int ioss_ipa_xmit(struct ipa_eth_client_pipe_info *pi, struct sk_buff *skb)
{
	return -ENOTSUPP;
}
#endif

static int ioss_ipa_fill_pipe_info(struct ioss_channel *ch,
		struct ipa_eth_client_pipe_info *pi)
{
	struct ioss_mem *desc_mem;
	struct ioss_mem *buff_mem;
	struct ipa_eth_buff_smmu_map *sm;
	struct ipa_eth_pipe_setup_info *si = &pi->info;
	struct ioss_device *idev = ioss_ch_dev(ch);

	pi->dir = to_ipa_dir(ch->direction);
	pi->traffic_type = to_ipa_traffic_type(ch->traffic_type);

	desc_mem = list_first_entry_or_null(
			&ch->desc_mem, typeof(*desc_mem), node);
	if (!desc_mem) {
		ioss_dev_err(idev, "Descriptor memory not found");
		return -EINVAL;
	}

	si->is_transfer_ring_valid = true;
	si->transfer_ring_base = desc_mem->daddr;
	si->transfer_ring_sgt = &desc_mem->sgt;

	if (ch->multi_rx_queues)
		si->transfer_ring_size = desc_mem->size*2;
	else
		si->transfer_ring_size = desc_mem->size;

	si->data_buff_list_size = ch->config.ring_size;
	si->data_buff_list = sm = kcalloc(si->data_buff_list_size,
			sizeof(*si->data_buff_list), GFP_KERNEL);
	if (!sm) {
		ioss_dev_err(idev,
			"Kcalloc failed for data buff list");
		return -EINVAL;
	}
	si->fix_buffer_size = ch->config.buff_size;

	list_for_each_entry(buff_mem, &ch->buff_mem, node) {
		sm->iova = buff_mem->daddr;
		sm->pa = ch->config.buff_alctr->pa(ioss_ch_dev(ch),
				buff_mem->addr, buff_mem->daddr,
				ch->config.buff_alctr);
		sm++;
	}

	if (pi->dir == IPA_ETH_PIPE_DIR_RX) {
		si->notify = ioss_ipa_rx_callback;
		si->priv = ch;
	}

#if IPA_ETH_API_VER >= 8
	if (pi->dir == IPA_ETH_PIPE_DIR_TX) {
		si->notify = ioss_ipa_tx_callback;
		si->priv   = ch;
	}
#endif

	if (ioss_ipa_hal_fill_si(ch)) {
		ioss_dev_err(idev,
			"Failed to fill IPA pipe setup info");
		kfree(si->data_buff_list);
		return -EINVAL;
	}

	return 0;
}

static void ioss_ipa_clear_pipe_info(struct ipa_eth_client_pipe_info *pi) {
	struct ipa_eth_pipe_setup_info *si = &pi->info;
	kfree(si->data_buff_list);
}

static int ioss_ipa_vote_bw(struct ioss_interface *iface)
{
	struct ipa_eth_perf_profile profile;
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ioss_device *idev = ioss_iface_dev(iface);

	ioss_dev_dbg(idev,
		"Voting for IPA bandwidth of %u Mbps", iface->link_speed);

	memset(&profile, 0, sizeof(profile));
	profile.max_supported_bw_mbps = iface->link_speed;

	if (ipa_eth_client_set_perf_profile(ec, &profile)) {
		ioss_dev_err(idev,
				"Failed to set IPA perf profile");
		return -EINVAL;
	}

	ioss_dev_cfg(idev,
		"Voted for IPA bandwidth of %u Mbps", iface->link_speed);

	return 0;
}

#if IPA_ETH_API_VER > 4
int ioss_ipa_enable_pipes(struct ioss_interface *iface)
{
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ioss_device *idev = ioss_iface_dev(iface);

	if (ipa_eth_client_enable_pipes(ec)) {
		ioss_dev_err(idev, "Failed to enable pipes");
		return -EINVAL;
	}
	ioss_dev_dbg(idev, "Enabled IPA pipes");

	return 0;
}

int ioss_ipa_disable_pipes(struct ioss_interface *iface)
{
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ioss_device *idev = ioss_iface_dev(iface);

	if (ipa_eth_client_disable_pipes(ec)) {
		ioss_dev_err(idev, "Failed to disable pipes");
		return -EINVAL;
	}
	ioss_dev_dbg(idev, "Disabled IPA pipes");

	return 0;
}
#endif

int ioss_ipa_register(struct ioss_interface *iface)
{
	int i;
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;
	struct ioss_channel *ch;
	struct ioss_device *idev = ioss_iface_dev(iface);

	ec->priv = iface;
	ec->inst_id = iface->instance_id;
	ec->client_type = ioss_ipa_hal_get_ctype(idev);

	if (ec->client_type == IPA_ETH_CLIENT_MAX) {
		ioss_dev_err(idev, "Failed to determine client type");
		return -EFAULT;
	}

	INIT_LIST_HEAD(&ec->pipe_list);

	ioss_for_each_valid_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;

		cp->ipa_pi.client_info = ec;
		cp->ipa_pi.tc_bmap = cp->tc_bmap;

		if (ioss_ipa_fill_pipe_info(ch, &cp->ipa_pi)) {
			ioss_dev_err(idev, "Failed to fill pipe info");
			goto err_ioss_pipe_info;
		}

		list_add_tail(&cp->ipa_pi.link, &ec->pipe_list);
	}

	/* connect pipes */
	ec->net_dev = ioss_iface_to_netdev(iface);

	if (ipa_eth_client_conn_pipes(ec)) {
		ioss_dev_err(idev, "Failed to connect pipes");
		goto err_ipa_conn_pipes;
	}

	/* vote for bw */
	if (ioss_ipa_vote_bw(iface)) {
		ioss_dev_err(idev, "Failed to vote for bandwidth");
		goto err_ipa_vote_bw;
	}

	/* Retrieve output from conn_pipes call */
	i = 0;
	ioss_for_each_valid_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;
		struct ipa_eth_client_pipe_info *pi = &cp->ipa_pi;
		struct ipa_eth_pipe_setup_info *si = &pi->info;

		ch->event.paddr = si->db_pa;
		ch->event.data = si->db_val;

	}

	/* register interface */
	ii->client = ec;
	ii->is_conn_evt = true;

	if (ipa_eth_client_reg_intf(ii)) {
		ioss_dev_err(idev, "Failed to register interface");
		goto err_ipa_reg_intf;
	}

	return 0;

err_ipa_reg_intf:
	ioss_for_each_valid_channel(ch, iface) {
		ch->event.paddr = 0;
		ch->event.data = 0;
	}
err_ipa_vote_bw:
	if (ipa_eth_client_disconn_pipes(ec))
		ioss_dev_err(idev, "Failed to disconnect pipes");
err_ipa_conn_pipes:
	ioss_for_each_valid_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;

		ioss_ipa_clear_pipe_info(&cp->ipa_pi);
		memset(&cp->ipa_pi, 0, sizeof(cp->ipa_pi));
	}
err_ioss_pipe_info:
	memset(ii, 0, sizeof(*ii));
	memset(ec, 0, sizeof(*ec));

	return -EINVAL;
}

int ioss_ipa_unregister(struct ioss_interface *iface)
{
	int rc;
	struct ioss_channel *ch;
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;
	struct ioss_device *idev = ioss_iface_dev(iface);

	/* disconnect pipes */
	rc = ipa_eth_client_disconn_pipes(ec);
	if (rc) {
		ioss_dev_err(idev, "Failed to disconnect pipes");
		return rc;
	}

	/* unregister interface */
	rc = ipa_eth_client_unreg_intf(ii);
	if (rc) {
		ioss_dev_err(idev, "Failed to unregister interface");
		return rc;
	}

	memset(ii, 0, sizeof(*ii));

	ioss_for_each_valid_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;

		ch->event.paddr = 0;
		ch->event.data = 0;
		ioss_ipa_clear_pipe_info(&cp->ipa_pi);

		memset(&cp->ipa_pi, 0, sizeof(cp->ipa_pi));
	}

	memset(ec, 0, sizeof(*ec));

	return 0;
}

static bool channel_match_ipa_config(struct ioss_channel *ch, const char *ipa_config)
{
	int i;

	if (!ipa_config)
		return false;

	/* A channel config (ex. legacy) that does not explicitly list any compatible IPA configs
	 * is assumed to be compatible only with the default IPA config.
	 */
	if (!ch->num_ipa_configs)
		return !strcmp(ipa_config, DEFAULT_IPA_CONFIG);

	for (i = 0; i < ch->num_ipa_configs; i++)
		if (!strcmp(ipa_config, ch->ipa_configs[i]))
			return true;

	return false;
}

static bool validate_channel(struct ioss_channel *ch,
		enum ioss_channel_dir dir, enum ioss_traffic_type traffic)
{
	if (ch->direction != dir)
		return false;

	if (ch->traffic_type != traffic)
		return false;

	return channel_match_ipa_config(ch, ch->iface->ipa_config);
}

/* Recursively select channels as per the channel config list provided by IPA */
static int ioss_ipa_validate_one_channel(struct ioss_interface *iface, struct ipa_eth_dma_ch_config *ch_list, int num_channels)
{
	int ret = -ENOENT;
	enum ioss_channel_dir dir;
	enum ioss_traffic_type traffic;
	struct ioss_channel *ch, *tmp_ch;
	const char *ipa_config = iface->ipa_config;
	struct ioss_device *idev = ioss_iface_dev(iface);

	if (!num_channels)
		return 0;

	dir = to_ioss_dir(ch_list->dir);
	traffic = to_ioss_traffic(ch_list->traffic_type);

	list_for_each_entry_safe(ch, tmp_ch, &iface->invalid_channels, node) {
		struct ioss_ch_priv *ch_priv = ch->ioss_priv;

		if (!validate_channel(ch, dir, traffic))
			continue;

		ioss_dev_log(idev, "Selected channel '%s' for %s '%s' traffic",
			ch->name, ioss_ch_dir_name(dir), ioss_traffic_name(traffic));

		/* Preserve the IPA DMA config order in valid_channels. */
		list_move_tail(&ch->node, &iface->valid_channels);

		ret = ioss_ipa_validate_one_channel(iface, ch_list + 1, num_channels - 1);
		if (!ret)
			ch_priv->ipa_ch_config = ch_list; /* for debug purposes */
		else
			/* Undo in LIFO order */
			list_move(&ch->node, &iface->invalid_channels);

		return ret;
	}

	ioss_dev_err(idev,
		"Failed to find (%u more needed) a matching channel for IPA DMA config: "
		"dir=%u traffic=%u config='%s'",
		num_channels, dir, traffic, ipa_config);

	return ret;
}

int ioss_ipa_validate_channels(struct ioss_interface *iface)
{
	int ret;
	int required_channels = 0;
	struct ioss_device *idev = ioss_iface_dev(iface);
	u32 inst_id = iface->instance_id;
	enum ipa_eth_client_type ct = ioss_ipa_hal_get_ctype(idev);
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_config *ipa_config = &ifp->ipa_config;

	iface->ipa_config = NULL;

	memset(ipa_config, 0, sizeof(*ipa_config));

	ret = ipa_eth_get_config_type(ct, inst_id, ipa_config);
	if (ret) {
		ioss_dev_err(idev, "Failed to get IPA config for %u.%u", ct, inst_id);
		return ret;
	}

	iface->ipa_config = ipa_config->config;

	ioss_dev_log(idev,
			"IPA config for %u.%u is '%s' and uses %u channels",
			ct, inst_id, ipa_config->config, ipa_config->num_dma_channel);

	ioss_ipa_invalidate_channels(iface);

	required_channels = ipa_config->num_dma_channel;

	return ioss_ipa_validate_one_channel(
			iface, ipa_config->dma_config, required_channels);
}

void ioss_ipa_invalidate_channels(struct ioss_interface *iface)
{
	struct ioss_channel *ch, *tmp_ch;

	/* Walk valid_channels in reverse and move each to the head */
	list_for_each_entry_safe_reverse(ch, tmp_ch, &iface->valid_channels, node) {
		ioss_dev_log(ioss_iface_dev(iface), "Ch : %d, id : %d, dir : %d, traffic : %d",
			     ch->channel_num, ch->id, ch->direction, ch->traffic_type);
		list_move(&ch->node, &iface->invalid_channels);
	}
}

static unsigned long ioss_ipa_be_txq_mask(struct ioss_interface *iface)
{
	struct ioss_channel *ch;
	unsigned long mask = 0;

	ioss_for_each_valid_channel(ch, iface) {
		if (ch->direction == IOSS_CH_DIR_TX &&
		    ch->traffic_type == IOSS_TRAFFIC_BE &&
		    ch->queue_number < BITS_PER_LONG)
			set_bit(ch->queue_number, &mask);
	}

	return mask;
}

#if IPA_ETH_API_VER >= 8
static void ioss_ipa_xps_configure_tx_offload(struct ioss_interface *iface)
{
	struct net_device *ndev = ioss_iface_to_netdev(iface);
	struct ioss_device *idev = ioss_iface_dev(iface);
	unsigned long be_txq_mask = ioss_ipa_be_txq_mask(iface);
	unsigned long q;

	/* Clear XPS CPU and RXQ masks on all TXQs */
	for (q = 0; q < ndev->real_num_tx_queues; q++)
		ioss_net_xps_set_cpu_mask(iface, cpu_none_mask, q);

	/* Steer all CPUs to BE TXQs */
	for_each_set_bit(q, &be_txq_mask, BITS_PER_LONG)
		ioss_net_xps_set_cpu_mask(iface, cpu_possible_mask, q);

	ioss_dev_log(idev, "XPS: IPA connected - all CPUs to TXQs 0x%lx", be_txq_mask);
}

static void ioss_ipa_clear_be_xps(struct ioss_interface *iface)
{
	struct ioss_device *idev = ioss_iface_dev(iface);
	unsigned long be_txq_mask = ioss_ipa_be_txq_mask(iface);
	unsigned long q;

	for_each_set_bit(q, &be_txq_mask, BITS_PER_LONG)
		ioss_net_xps_set_cpu_mask(iface, cpu_none_mask, q);

	ioss_dev_log(idev, "XPS: IPA disabled - cleared CPUs from TXQs 0x%lx", be_txq_mask);
}

void ioss_ipa_emb_tx_enable(struct ioss_interface *iface)
{
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_config *ipa_config = &ifp->ipa_config;
	struct ioss_ipa_tx_state *state_save = &iface->ipa_tx_state_saved;
	struct ioss_device *idev = ioss_iface_dev(iface);
	struct net_device *ndev = ioss_iface_to_netdev(iface);

	if (iface->ipa_tx_disabled || disable_ipa_tx) {
		ioss_ipa_clear_be_xps(iface);
		return;
	}

	state_save->hw_features     = ndev->hw_features;
	state_save->features        = ndev->features;
	state_save->wanted_features = ndev->wanted_features;
	state_save->gso_max_size    = ndev->gso_max_size;
	state_save->gso_ipv4_max_size = ndev->gso_ipv4_max_size;

	ndev->hw_features     = (ipa_config->offload_features & ~IOSS_IPA_RETAIN_FLAGS) |
		(state_save->hw_features & IOSS_IPA_RETAIN_FLAGS);
	ndev->wanted_features = (ipa_config->offload_features & ~IOSS_IPA_RETAIN_FLAGS) |
		(state_save->wanted_features & IOSS_IPA_RETAIN_FLAGS);
	ndev->features        = (ipa_config->offload_features & ~IOSS_IPA_RETAIN_FLAGS) |
		(state_save->features & IOSS_IPA_RETAIN_FLAGS);
	WRITE_ONCE(ndev->gso_max_size, ipa_config->offload_gso_max_size);
	WRITE_ONCE(ndev->gso_ipv4_max_size, ipa_config->offload_gso_max_size);
	ndev->needed_headroom += ipa_config->offload_headroom;
	netdev_features_change(ndev);

	ioss_ipa_xps_configure_tx_offload(iface);

	ioss_dev_log(idev,
		     "ULSO enabled: features=0x%llx gso_max=%u headroom=%u",
		     (unsigned long long)ipa_config->offload_features,
		     ipa_config->offload_gso_max_size,
		     ipa_config->offload_headroom);
}

void ioss_ipa_emb_tx_disable(struct ioss_interface *iface)
{
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_config *ipa_config = &ifp->ipa_config;
	struct ioss_ipa_tx_state *saved_state = &iface->ipa_tx_state_saved;
	struct net_device *ndev = ioss_iface_to_netdev(iface);
	struct ioss_device *idev = ioss_iface_dev(iface);
	struct ioss_channel *ch;

	if (iface->ipa_tx_disabled || disable_ipa_tx)
		return;

	ioss_net_xps_reset(iface);

	ioss_for_each_valid_channel(ch, iface) {
		if (ch->direction == IOSS_CH_DIR_TX)
			atomic_set(&ch->ipa_tx_outstanding_pkts, 0);
	}

	ndev->hw_features     = (saved_state->hw_features & ~IOSS_IPA_RETAIN_FLAGS) |
				(ndev->hw_features & IOSS_IPA_RETAIN_FLAGS);
	ndev->wanted_features = (saved_state->wanted_features & ~IOSS_IPA_RETAIN_FLAGS) |
				(ndev->wanted_features & IOSS_IPA_RETAIN_FLAGS);
	ndev->features        = (saved_state->features & ~IOSS_IPA_RETAIN_FLAGS) |
				(ndev->features & IOSS_IPA_RETAIN_FLAGS);
	WRITE_ONCE(ndev->gso_max_size, saved_state->gso_max_size);
	WRITE_ONCE(ndev->gso_ipv4_max_size, saved_state->gso_ipv4_max_size);
	ndev->needed_headroom -= ipa_config->offload_headroom;
	netdev_update_features(ndev);

	ioss_dev_log(idev, "Restored device feature flags after IPA disconnect");
}
#else
void ioss_ipa_emb_tx_enable(struct ioss_interface *iface)
{
}

void ioss_ipa_emb_tx_disable(struct ioss_interface *iface)
{
}
#endif

#if IPA_ETH_API_VER >= 9
static int ioss_ipa_update_ch_tc_bmap(struct ioss_channel *ch, u32 tc_bmap)
{
	struct ioss_ch_priv *cp = ch->ioss_priv;
	int ret;

	cp->tc_bmap = tc_bmap;

	if (ch->iface->state != IOSS_IF_ST_ONLINE)
		return 0;

	cp->ipa_pi.tc_bmap = tc_bmap;

	ret = ipa_eth_qos_update_tc_mapping(&cp->ipa_pi);
	if (ret)
		ioss_dev_err(ioss_ch_dev(ch),
			     "failed to update tc mapping dir=%d ch=%d bmap=0x%x ret=%d",
			     ch->direction, ch->channel_num, tc_bmap, ret);

	return ret;
}

int ioss_ipa_set_tc_bmap(struct ioss_device *idev, bool is_rx, int ch_num, u32 tc_bmap)
{
	struct ioss_interface *iface;
	struct ioss_channel *ch;
	enum ioss_channel_dir dir;
	bool found = false;
	int ret = 0;

	if (!idev)
		return -EINVAL;

	iface = &idev->interface;
	dir = is_rx ? IOSS_CH_DIR_RX : IOSS_CH_DIR_TX;

	mutex_lock(&idev->refresh_lock);

	ioss_for_each_valid_channel(ch, iface) {
		if (ch->direction != dir || ch->channel_num != ch_num)
			continue;

		found = true;
		ret |= ioss_ipa_update_ch_tc_bmap(ch, tc_bmap);
	}

	ioss_for_each_invalid_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;

		if (ch->direction != dir || ch->channel_num != ch_num)
			continue;

		found = true;
		cp->tc_bmap = tc_bmap;
	}

	mutex_unlock(&idev->refresh_lock);

	if (!found) {
		ioss_dev_err(idev, "failed to find qos channel dir=%d ch=%d", dir, ch_num);
		return -ENODEV;
	}

	return ret;
}
#else
int ioss_ipa_set_tc_bmap(struct ioss_device *idev, bool is_rx, int ch_num, u32 tc_bmap)
{
	if (!idev)
		return -EINVAL;

	ioss_dev_err(idev, "tc mapping is unsupported for IPA_ETH_API_VER=%d",
		     IPA_ETH_API_VER);

	return -EOPNOTSUPP;
}
#endif
EXPORT_SYMBOL_GPL(ioss_ipa_set_tc_bmap);
