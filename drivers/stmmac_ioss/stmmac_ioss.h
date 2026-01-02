/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 */

#ifndef __STMMAC_IOSS_H__
#define __STMMAC_IOSS_H__

#include <stmmac/stmmac_api.h>
#include <linux/msm/ioss.h>

#if STMMAC_API_VER  < 1
#error Unsupported STMMAC API VERSION
#endif

struct stmmac_ioss_device {
	struct ioss_device *idev;
	u64 *stats_data;
	u8 *stats_strings;
	int stats_count;
	ktime_t stats_last_update;
};

int stmmac_ioss_stats_init(struct ioss_device *idev);
void stmmac_ioss_stats_deinit(struct ioss_device *idev);
int stmmac_ioss_device_statistics(struct ioss_device *idev,
				  struct ioss_device_stats *statistics);
int stmmac_ioss_channel_statistics(struct ioss_channel *ch,
				   struct ioss_channel_stats *statistics);
int stmmac_ioss_channel_status(struct ioss_channel *ch,
			       struct ioss_channel_status *status);

#endif /* __STMMAC_IOSS_H__ */
