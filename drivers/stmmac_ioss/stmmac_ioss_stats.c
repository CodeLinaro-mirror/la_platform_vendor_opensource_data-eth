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
#include <linux/ethtool.h>
#include <linux/ktime.h>
#include <linux/rtnetlink.h>
#include "stmmac_ioss.h"

static uint stats_refresh_ms = 10;
module_param(stats_refresh_ms, uint, 0644);
MODULE_PARM_DESC(stats_refresh_ms, "Minimum interval in milliseconds between statistics updates");

int stmmac_ioss_stats_init(struct ioss_device *idev)
{
	struct stmmac_ioss_device *stmmac_dev = idev->private;
	const struct ethtool_ops *ops = idev->net_dev->ethtool_ops;

	if (!ops || !ops->get_sset_count || !ops->get_strings || !ops->get_ethtool_stats) {
		stmmac_dev->stats_count = 0;
		ioss_dev_err(idev, "Ethtool ops for stats not available");
		return 0;
	}

	stmmac_dev->stats_count = ops->get_sset_count(idev->net_dev, ETH_SS_STATS);
	if (stmmac_dev->stats_count <= 0) {
		ioss_dev_err(idev, "No ethtool stats available");
		stmmac_dev->stats_count = 0;
		return 0;
	}

	stmmac_dev->stats_data = kcalloc(stmmac_dev->stats_count, sizeof(u64), GFP_KERNEL);
	if (!stmmac_dev->stats_data)
		return -ENOMEM;

	stmmac_dev->stats_strings = kcalloc(stmmac_dev->stats_count, ETH_GSTRING_LEN, GFP_KERNEL);
	if (!stmmac_dev->stats_strings) {
		kfree(stmmac_dev->stats_data);
		return -ENOMEM;
        }

	ops->get_strings(idev->net_dev, ETH_SS_STATS, stmmac_dev->stats_strings);

	return 0;
}

void stmmac_ioss_stats_deinit(struct ioss_device *idev)
{
	struct stmmac_ioss_device *stmmac_dev = idev->private;

	kfree(stmmac_dev->stats_strings);
	kfree(stmmac_dev->stats_data);
}

static u64 get_stats_data(const char *name, u64 data[], const u8 *strings_data, int scount)
{
	int i = 0;
	const char (*strings)[ETH_GSTRING_LEN] = (typeof(strings))strings_data;

	for (i = 0; i < scount; i++) {
		if (strcmp(name, strings[i]) == 0)
			return data[i];
	}

	return 0;
}

static void update_ethtool_stats(struct ioss_device *idev)
{
	struct stmmac_ioss_device *stmmac_dev = idev->private;
	const struct ethtool_ops *ops = idev->net_dev->ethtool_ops;
	ktime_t now, elapsed;
	int stats_count;

	if(!stmmac_dev->stats_count)
		return;

	now = ktime_get();
	elapsed = ktime_sub(now, stmmac_dev->stats_last_update);

	if (ktime_to_ms(elapsed) < stats_refresh_ms)
		return;

	stats_count = ops->get_sset_count(idev->net_dev, ETH_SS_STATS);
	if (stats_count != stmmac_dev->stats_count) {
		ioss_dev_err(idev, "Stats count mismatch: expected %d, got %d",
			     stmmac_dev->stats_count, stats_count);
		return;
	}

	rtnl_lock();
	ops->get_ethtool_stats(idev->net_dev, NULL, stmmac_dev->stats_data);
	rtnl_unlock();

	stmmac_dev->stats_last_update = now;
}

int stmmac_ioss_device_statistics(struct ioss_device *idev, struct ioss_device_stats *statistics)
{
	struct stmmac_ioss_device *stmmac_dev = idev->private;

	update_ethtool_stats(idev);

	statistics->emac_rx_packets =
		get_stats_data("mmc_rx_framecount_gb", stmmac_dev->stats_data,
			       stmmac_dev->stats_strings, stmmac_dev->stats_count);
	statistics->emac_tx_packets =
		get_stats_data("mmc_tx_framecount_gb", stmmac_dev->stats_data,
			       stmmac_dev->stats_strings, stmmac_dev->stats_count);
	statistics->emac_rx_bytes =
		get_stats_data("mmc_rx_octetcount_gb", stmmac_dev->stats_data,
			       stmmac_dev->stats_strings, stmmac_dev->stats_count);
	statistics->emac_tx_bytes =
		get_stats_data("mmc_tx_octetcount_gb", stmmac_dev->stats_data,
			       stmmac_dev->stats_strings, stmmac_dev->stats_count);
	statistics->emac_rx_drops =
		get_stats_data("mmc_rx_fifo_overflow", stmmac_dev->stats_data,
			       stmmac_dev->stats_strings, stmmac_dev->stats_count);
	statistics->emac_rx_pause_frames =
		get_stats_data("mmc_rx_pause_frames", stmmac_dev->stats_data,
			       stmmac_dev->stats_strings, stmmac_dev->stats_count);
	statistics->emac_tx_pause_frames =
		get_stats_data("mmc_tx_pause_frame", stmmac_dev->stats_data,
			       stmmac_dev->stats_strings, stmmac_dev->stats_count);

	return 0;
}

int stmmac_ioss_channel_statistics(struct ioss_channel *ch, struct ioss_channel_stats *statistics)
{
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct stmmac_ioss_device *stmmac_dev = idev->private;
	char stat_name[ETH_GSTRING_LEN];

	update_ethtool_stats(idev);

	if (ch->direction == IOSS_CH_DIR_RX) {
		snprintf(stat_name, sizeof(stat_name), "rx_buf_unav_irq[%d]", ch->id);
		statistics->desc_unavail =
			get_stats_data(stat_name, stmmac_dev->stats_data,
				       stmmac_dev->stats_strings, stmmac_dev->stats_count);

		statistics->overflow_error =
			get_stats_data("mmc_rx_fifo_overflow", stmmac_dev->stats_data,
				       stmmac_dev->stats_strings, stmmac_dev->stats_count);
	} else {
		statistics->underflow_error =
			get_stats_data("tx_underflow", stmmac_dev->stats_data,
				       stmmac_dev->stats_strings, stmmac_dev->stats_count);
	}

	return 0;
}

int stmmac_ioss_channel_status(struct ioss_channel *ch, struct ioss_channel_status *status)
{
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct stmmac_ioss_device *stmmac_dev = idev->private;
	struct stmmac_api_channel *stmmac_ch = ch->private;
	char stat_name[ETH_GSTRING_LEN];

	status->ring_size = stmmac_ch->ring_size;

	update_ethtool_stats(idev);

	if (ch->direction == IOSS_CH_DIR_RX) {
		snprintf(stat_name, sizeof(stat_name), "rx_desc_curr_laddr[%d]", ch->id);
		status->head_ptr = get_stats_data(stat_name, stmmac_dev->stats_data,
						  stmmac_dev->stats_strings, stmmac_dev->stats_count);

		snprintf(stat_name, sizeof(stat_name), "rx_desc_tail[%d]", ch->id);
		status->tail_ptr = get_stats_data(stat_name, stmmac_dev->stats_data,
						  stmmac_dev->stats_strings, stmmac_dev->stats_count);
	} else {
		snprintf(stat_name, sizeof(stat_name), "tx_desc_curr_laddr[%d]", ch->id);
		status->head_ptr = get_stats_data(stat_name, stmmac_dev->stats_data,
						  stmmac_dev->stats_strings, stmmac_dev->stats_count);

		snprintf(stat_name, sizeof(stat_name), "tx_desc_tail[%d]", ch->id);
		status->tail_ptr = get_stats_data(stat_name, stmmac_dev->stats_data,
						  stmmac_dev->stats_strings, stmmac_dev->stats_count);
	}

	return 0;
}
