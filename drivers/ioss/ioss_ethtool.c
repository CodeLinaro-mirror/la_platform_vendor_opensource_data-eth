/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2007-2009 STMicroelectronics Ltd
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "ioss_i.h"

static bool enable_ioss_ethtool_ops;
module_param(enable_ioss_ethtool_ops, bool, 0444);
MODULE_PARM_DESC(enable_ioss_ethtool_ops,
		 "Enable IOSS ethtool operations (default: false)");

struct ioss_stat_desc {
	char name[ETH_GSTRING_LEN];
	int idx;
};

#define IOSS_STAT(_name, _field) \
{ \
	.name = #_name, \
	.idx = offsetof(struct ioss_ch_stats, _field) / sizeof(u64), \
}

static const struct ioss_stat_desc ioss_tx_ch_stats[] = {
	IOSS_STAT(ioss_tx,               tx_xmit),
	IOSS_STAT(ioss_tx_ipa,           tx_ipa),
	IOSS_STAT(ioss_tx_ipa_cb,        tx_ipa_cb),
	IOSS_STAT(ioss_tx_wm_stop,       tx_wm_stop),
	IOSS_STAT(ioss_tx_wm_wake,       tx_wm_wake),
	IOSS_STAT(ioss_tx_gso_tcp,       tx_gso_tcp),
	IOSS_STAT(ioss_tx_gso_udp,       tx_gso_udp),
	IOSS_STAT(ioss_tx_ipa_err,       tx_ipa_err),
	IOSS_STAT(ioss_tx_ipa_nospace,   tx_ipa_nospace),
	IOSS_STAT(ioss_tx_ipa_is_ready,  tx_ipa_is_ready),
	IOSS_STAT(ioss_tx_wm_space_used, tx_wm_space_used),
	IOSS_STAT(ioss_tx_wm_space_free, tx_wm_space_free),
	IOSS_STAT(ioss_tx_net_lock_bh,   tx_net_lock_bh),
};

static const struct ioss_stat_desc ioss_rx_ch_stats[] = {
	IOSS_STAT(ioss_rx_ipa_cb,  rx_ipa_cb),
	IOSS_STAT(ioss_rx_net_err, rx_net_err),
};

static void ioss_fill_strings(u8 **p, const struct ioss_stat_desc *descs,
			      int n, int queue)
{
	int i;

	for (i = 0; i < n; i++, *p += ETH_GSTRING_LEN)
		snprintf(*p, ETH_GSTRING_LEN, "%s[%d]", descs[i].name, queue);
}

static void ioss_fill_stats(u64 **p, const struct ioss_ch_stats *stats,
			    const struct ioss_stat_desc *descs, int n)
{
	const u64 *base = (const u64 *)stats;
	int i;

	for (i = 0; i < n; i++)
		*(*p)++ = base[descs[i].idx];
}

static int ioss_ethtool_get_sset_count(struct net_device *ndev, int sset)
{
	struct ioss_interface *iface = ioss_netdev_to_iface(ndev);
	struct ioss_device *idev = ioss_iface_dev(iface);
	const struct ethtool_ops *real_ops;
	struct ioss_channel *ch;
	int ioss_count = 0;
	int real_count = 0;

	real_ops = iface->ethtool_ops_real;

	if (sset == ETH_SS_STATS) {
		mutex_lock(&idev->refresh_lock);
		ioss_for_each_valid_channel(ch, iface) {
			if (ch->direction == IOSS_CH_DIR_TX)
				ioss_count += ARRAY_SIZE(ioss_tx_ch_stats);
			else if (ch->direction == IOSS_CH_DIR_RX)
				ioss_count += ARRAY_SIZE(ioss_rx_ch_stats);
		}
		iface->ethtool_stat_count = ioss_count;
		mutex_unlock(&idev->refresh_lock);
	}

	if (real_ops && real_ops->get_sset_count) {
		real_count = real_ops->get_sset_count(ndev, sset);
		if (real_count < 0)
			real_count = 0;
	}

	return ioss_count + real_count;
}

static void ioss_ethtool_get_strings(struct net_device *ndev, u32 sset, u8 *data)
{
	struct ioss_interface *iface = ioss_netdev_to_iface(ndev);
	struct ioss_device *idev = ioss_iface_dev(iface);
	const struct ethtool_ops *real_ops;
	struct ioss_channel *ch;
	u8 *p = data;
	int real_count = 0;
	int remaining = 0;

	real_ops = iface->ethtool_ops_real;

	if (real_ops) {
		if (sset == ETH_SS_STATS && real_ops->get_sset_count) {
			real_count = real_ops->get_sset_count(ndev, ETH_SS_STATS);
			if (real_count < 0)
				real_count = 0;
		}
		if (real_ops->get_strings)
			real_ops->get_strings(ndev, sset, p);
		p += real_count * ETH_GSTRING_LEN;
	}

	if (sset != ETH_SS_STATS)
		return;

	mutex_lock(&idev->refresh_lock);

	remaining = iface->ethtool_stat_count;
	memset(p, 0, remaining * ETH_GSTRING_LEN);

	ioss_for_each_valid_channel(ch, iface) {
		if (ch->direction == IOSS_CH_DIR_TX) {
			if (remaining < (int)ARRAY_SIZE(ioss_tx_ch_stats))
				break;
			ioss_fill_strings(&p, ioss_tx_ch_stats,
					  ARRAY_SIZE(ioss_tx_ch_stats), ch->queue_number);
			remaining -= ARRAY_SIZE(ioss_tx_ch_stats);
		}
	}

	ioss_for_each_valid_channel(ch, iface) {
		if (ch->direction == IOSS_CH_DIR_RX) {
			if (remaining < (int)ARRAY_SIZE(ioss_rx_ch_stats))
				break;
			ioss_fill_strings(&p, ioss_rx_ch_stats,
					  ARRAY_SIZE(ioss_rx_ch_stats), ch->queue_number);
			remaining -= ARRAY_SIZE(ioss_rx_ch_stats);
		}
	}

	mutex_unlock(&idev->refresh_lock);
}

static void ioss_ethtool_get_stats(struct net_device *ndev, struct ethtool_stats *stats, u64 *data)
{
	struct ioss_interface *iface = ioss_netdev_to_iface(ndev);
	struct ioss_device *idev = ioss_iface_dev(iface);
	const struct ethtool_ops *real_ops;
	struct ioss_iface_priv *ifp;
	struct ioss_channel *ch;
	u64 *p = data;
	int real_count = 0;
	int remaining = 0;

	real_ops = iface->ethtool_ops_real;
	ifp = iface->ioss_priv;

	if (real_ops) {
		if (real_ops->get_sset_count) {
			real_count = real_ops->get_sset_count(ndev, ETH_SS_STATS);
			if (real_count < 0)
				real_count = 0;
		}
		if (real_ops->get_ethtool_stats)
			real_ops->get_ethtool_stats(ndev, stats, p);
		p += real_count;
	}

	mutex_lock(&idev->refresh_lock);

	remaining = iface->ethtool_stat_count;
	memset(p, 0, remaining * sizeof(u64));

	ioss_for_each_valid_channel(ch, iface) {
		u64 stop_wm, space_used;

		if (ch->direction != IOSS_CH_DIR_TX)
			continue;

		if (remaining < (int)ARRAY_SIZE(ioss_tx_ch_stats))
			break;

		space_used = ch->stats.tx_ipa - ch->stats.tx_ipa_cb;
		stop_wm = ifp ? ifp->ipa_config.offload_max_pkts : 0;

		ch->stats.tx_wm_space_used = space_used;
		ch->stats.tx_wm_space_free =
			stop_wm > space_used ? stop_wm - space_used : 0;

		ioss_fill_stats(&p, &ch->stats,
				ioss_tx_ch_stats, ARRAY_SIZE(ioss_tx_ch_stats));
		remaining -= ARRAY_SIZE(ioss_tx_ch_stats);
	}

	ioss_for_each_valid_channel(ch, iface) {
		if (ch->direction != IOSS_CH_DIR_RX)
			continue;
		if (remaining < (int)ARRAY_SIZE(ioss_rx_ch_stats))
			break;
		ioss_fill_stats(&p, &ch->stats,
				ioss_rx_ch_stats, ARRAY_SIZE(ioss_rx_ch_stats));
		remaining -= ARRAY_SIZE(ioss_rx_ch_stats);
	}

	mutex_unlock(&idev->refresh_lock);
}

void ioss_ethtool_hijack_stats(struct ioss_interface *iface)
{
	struct net_device *ndev = ioss_iface_to_netdev(iface);

	iface->ethtool_ops_real = ndev->ethtool_ops;
	if (!enable_ioss_ethtool_ops)
		return;

	if (iface->ethtool_ops_real)
		iface->ethtool_ops = *iface->ethtool_ops_real;
	else
		memset(&iface->ethtool_ops, 0, sizeof(iface->ethtool_ops));

	iface->ethtool_ops.get_sset_count    = ioss_ethtool_get_sset_count;
	iface->ethtool_ops.get_strings       = ioss_ethtool_get_strings;
	iface->ethtool_ops.get_ethtool_stats = ioss_ethtool_get_stats;

	ndev->ethtool_ops = &iface->ethtool_ops;
}

void ioss_ethtool_restore_stats(struct ioss_interface *iface)
{
	struct net_device *ndev = ioss_iface_to_netdev(iface);

	ndev->ethtool_ops = iface->ethtool_ops_real;
	iface->ethtool_ops_real = NULL;
}
