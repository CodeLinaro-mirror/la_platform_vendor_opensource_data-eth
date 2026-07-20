/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <net/ipv6.h>
#include "eth_qos.h"
#include "eth_qos_mgr.h"
#include <linux/msm/ioss.h>
#include "stmmac_api.h"
#include "dwxgmac2.h"

enum eth_qos_filter_type {
	ETH_QOS_FILTER_NONE = 0,
	ETH_QOS_FILTER_PCP,
	ETH_QOS_FILTER_SW_TX_PCP,
	ETH_QOS_FILTER_VLAN,
	ETH_QOS_FILTER_L3L4,
	ETH_QOS_FILTER_CBS,
};

enum eth_qos_q_ch_status {
	ETH_QOS_Q_CH_NONE = 0,
	ETH_QOS_Q_CH_DMA,
	ETH_QOS_Q_CH_PCP,
	ETH_QOS_Q_CH_TX,
};

struct eth_qos_vlan_entry {
	bool used;
	bool hw_path;
	u16 vlanid;
	u32 queue;
	u32 ch;
};

struct eth_qos_pcp_entry {
	bool used;
	bool hw_path;
	u8 pcp;
	u32 queue;
	u32 ch;
};

struct eth_qos_l3l4_entry {
	bool used;
	bool hw_path;
	u32 queue;
	u32 ch;
	u32 sip;
	u32 dip;
	u32 sipv6[4];
	u32 dipv6[4];
	u16 sport;
	u16 dport;
	u8 protocol;
};

struct eth_qos_q_ch_entry {
	struct list_head list;
	enum eth_qos_q_ch_status status;
	bool hw_path;
	u32 queue;
	u32 channel;
};

struct eth_qos_tx_pcp_entry {
	bool used;
	u8 pcp;
	u32 queue;
	u32 ch;
};

struct eth_qos_cbs_entry {
	bool used;
	bool hw_path;
	u32 tc;
	u32 queue;
	u32 ch;
	int idleslope;
	int sendslope;
	int hicredit;
	int locredit;
};

struct eth_qos_hid_entry {
	struct list_head list;
	u32 hid;
	u32 queue;
	u32 tc;
	u32 ch;
	bool hw_path;
	enum eth_qos_filter_type type;
	union {
		struct {
			u8 pcp;
		} pcp;
		struct {
			u16 vlanid;
			u32 idx;
		} vlan;
		struct {
			u32 idx;
		} l3l4;
		struct {
			bool hw_path;
		} cbs;
	} u;
};

struct eth_qos_mgr_ctx {
	struct list_head list;
	struct net_device *ndev;
	struct mutex lock;
	u32 max_rx_queues;
	u32 max_tx_queues;
	struct {
		u32 max_l3l4_filters;
		u32 max_vlan_filters;
		u32 ra_users;
		struct eth_qos_l3l4_entry *l3l4_table;
		struct eth_qos_vlan_entry *vlan_table;
		struct eth_qos_pcp_entry pcp_table[8];
		struct list_head q_ch_status_list;
	} rx;
	struct {
		struct eth_qos_tx_pcp_entry pcp_table[8];
		struct eth_qos_cbs_entry *cbs_table;
		struct list_head q_ch_status_list;
	} tx;
	struct list_head hid_table;
};

static LIST_HEAD(eth_qos_mgr_list);
static DEFINE_MUTEX(eth_qos_mgr_list_lock);

/*
 * Locking rules:
 * - eth_qos_mgr_list_lock protects eth_qos_mgr_list and ctx lifetime.
 * - ctx->lock serializes per-netdev QoS tables and hardware programming.
 * - When both locks are needed, always acquire eth_qos_mgr_list_lock first,
 *   then ctx->lock.
 * - *_ctx_locked() helpers return with ctx->lock held; callers must unlock it.
 *   This closes the find/unlock/use race with eth_qos_mgr_release(), which
 *   removes a ctx from the list while holding both locks before freeing it.
 */

/* Basic helpers and filter/channel type helpers. */
static void eth_qos_mgr_convert_ip_addr(struct sockaddr_storage *addr, u32 *ipv4_addr,
					u32 *ipv6_addr)
{
	if (addr->ss_family == AF_INET && ipv4_addr)
		*ipv4_addr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	else if (addr->ss_family == AF_INET6 && ipv6_addr)
		memcpy(ipv6_addr, &((struct sockaddr_in6 *)addr)->sin6_addr,
		       sizeof(struct in6_addr));
}

static bool eth_qos_mgr_is_dma_type(enum eth_qos_filter_type type)
{
	return type == ETH_QOS_FILTER_VLAN || type == ETH_QOS_FILTER_L3L4;
}

static bool eth_qos_mgr_ipv6_addr_any(const u32 *addr)
{
	struct in6_addr in6;

	memcpy(&in6, addr, sizeof(in6));
	return ipv6_addr_any(&in6);
}

static void eth_qos_mgr_update_ch_entry(struct eth_qos_q_ch_entry *entry,
					enum eth_qos_q_ch_status status,
					bool hw_path, u32 queue, u32 channel)
{
	entry->status = status;
	entry->hw_path = hw_path;
	entry->queue = queue;
	entry->channel = channel;
}

static struct eth_qos_q_ch_entry *eth_qos_mgr_find_q_ch_entry(struct list_head *head,
							  u32 queue, u32 ch)
{
	struct eth_qos_q_ch_entry *entry;

	list_for_each_entry(entry, head, list) {
		if (entry->queue == queue && entry->channel == ch)
			return entry;
	}

	return NULL;
}

static struct eth_qos_q_ch_entry *eth_qos_mgr_get_or_create_q_ch_entry(struct list_head *head,
								     u32 queue, u32 ch)
{
	struct eth_qos_q_ch_entry *entry;

	entry = eth_qos_mgr_find_q_ch_entry(head, queue, ch);
	if (entry)
		return entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	INIT_LIST_HEAD(&entry->list);
	entry->queue = queue;
	entry->channel = ch;
	list_add_tail(&entry->list, head);

	return entry;
}

static void eth_qos_mgr_del_q_ch_entry(struct eth_qos_q_ch_entry *entry)
{
	if (!entry)
		return;

	list_del(&entry->list);
	kfree(entry);
}

static enum eth_qos_q_ch_status
eth_qos_mgr_get_rx_q_ch_status(enum eth_qos_filter_type type)
{
	if (eth_qos_mgr_is_dma_type(type))
		return ETH_QOS_Q_CH_DMA;
	if (type == ETH_QOS_FILTER_PCP)
		return ETH_QOS_Q_CH_PCP;

	return ETH_QOS_Q_CH_NONE;
}

static enum eth_qos_q_ch_status
eth_qos_mgr_get_tx_q_ch_status(enum eth_qos_filter_type type, bool hw_path)
{
	if (type == ETH_QOS_FILTER_SW_TX_PCP || type == ETH_QOS_FILTER_CBS)
		return ETH_QOS_Q_CH_TX;

	return ETH_QOS_Q_CH_NONE;
}

/* Context lookup, allocation, and lifecycle helpers. */
static struct eth_qos_mgr_ctx *eth_qos_mgr_find_ctx(struct net_device *ndev)
{
	struct eth_qos_mgr_ctx *ctx;

	list_for_each_entry(ctx, &eth_qos_mgr_list, list) {
		if (ctx->ndev == ndev)
			return ctx;
	}

	return NULL;
}

static struct eth_qos_hid_entry *eth_qos_mgr_find_hid(struct eth_qos_mgr_ctx *ctx, u32 hid)
{
	struct eth_qos_hid_entry *entry;

	list_for_each_entry(entry, &ctx->hid_table, list) {
		if (entry->hid == hid)
			return entry;
	}

	return NULL;
}

static void eth_qos_mgr_init_q_ch_status_table(struct eth_qos_mgr_ctx *ctx)
{
	if (!ctx)
		return;

	INIT_LIST_HEAD(&ctx->rx.q_ch_status_list);
	INIT_LIST_HEAD(&ctx->tx.q_ch_status_list);
}

static void eth_qos_mgr_free_ctx(struct eth_qos_mgr_ctx *ctx)
{
	struct eth_qos_hid_entry *entry, *tmp;
	struct eth_qos_q_ch_entry *ch_entry, *ch_tmp;

	if (!ctx)
		return;

	list_for_each_entry_safe(entry, tmp, &ctx->hid_table, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	list_for_each_entry_safe(ch_entry, ch_tmp, &ctx->tx.q_ch_status_list, list)
		eth_qos_mgr_del_q_ch_entry(ch_entry);

	list_for_each_entry_safe(ch_entry, ch_tmp, &ctx->rx.q_ch_status_list, list)
		eth_qos_mgr_del_q_ch_entry(ch_entry);

	kfree(ctx->tx.cbs_table);
	kfree(ctx->rx.l3l4_table);
	kfree(ctx->rx.vlan_table);
	kfree(ctx);
}

static struct eth_qos_mgr_ctx *eth_qos_mgr_get_existing_ctx_locked(struct net_device *ndev)
{
	struct eth_qos_mgr_ctx *ctx;

	mutex_lock(&eth_qos_mgr_list_lock);
	ctx = eth_qos_mgr_find_ctx(ndev);
	if (ctx)
		mutex_lock(&ctx->lock);
	mutex_unlock(&eth_qos_mgr_list_lock);

	return ctx;
}

static struct eth_qos_mgr_ctx *eth_qos_mgr_get_or_create_ctx_locked(struct net_device *ndev)
{
	struct stmmac_api_hw_caps caps;
	struct eth_qos_mgr_ctx *ctx;
	int ret;

	mutex_lock(&eth_qos_mgr_list_lock);

	ctx = eth_qos_mgr_find_ctx(ndev);
	if (ctx) {
		mutex_lock(&ctx->lock);
		mutex_unlock(&eth_qos_mgr_list_lock);
		return ctx;
	}

	ret = stmmac_api_get_hw_caps(ndev, &caps);
	if (ret) {
		mutex_unlock(&eth_qos_mgr_list_lock);
		return ERR_PTR(ret);
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		mutex_unlock(&eth_qos_mgr_list_lock);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&ctx->hid_table);

	ctx->rx.l3l4_table = kcalloc(caps.l3l4_filters, sizeof(*ctx->rx.l3l4_table), GFP_KERNEL);
	ctx->rx.vlan_table = kcalloc(caps.vlan_filters, sizeof(*ctx->rx.vlan_table), GFP_KERNEL);
	ctx->tx.cbs_table = kcalloc(caps.tx_queues, sizeof(*ctx->tx.cbs_table), GFP_KERNEL);
	if (!ctx->rx.l3l4_table || !ctx->rx.vlan_table || !ctx->tx.cbs_table) {
		mutex_unlock(&eth_qos_mgr_list_lock);
		eth_qos_mgr_free_ctx(ctx);
		return ERR_PTR(-ENOMEM);
	}

	ctx->ndev = ndev;
	ctx->max_rx_queues = caps.rx_queues;
	ctx->max_tx_queues = caps.tx_queues;
	ctx->rx.max_l3l4_filters = caps.l3l4_filters;
	ctx->rx.max_vlan_filters = caps.vlan_filters;
	mutex_init(&ctx->lock);
	eth_qos_mgr_init_q_ch_status_table(ctx);
	list_add_tail(&ctx->list, &eth_qos_mgr_list);
	mutex_lock(&ctx->lock);
	mutex_unlock(&eth_qos_mgr_list_lock);

	return ctx;
}

static struct eth_qos_mgr_ctx *eth_qos_mgr_detach_ctx(struct net_device *ndev)
{
	struct eth_qos_mgr_ctx *ctx;

	mutex_lock(&eth_qos_mgr_list_lock);
	ctx = eth_qos_mgr_find_ctx(ndev);
	if (ctx) {
		mutex_lock(&ctx->lock);
		list_del(&ctx->list);
	}
	mutex_unlock(&eth_qos_mgr_list_lock);

	return ctx;
}

static struct eth_qos_hid_entry *eth_qos_mgr_alloc_hid(struct eth_qos_mgr_ctx *ctx, u32 hid,
						       enum eth_qos_filter_type type)
{
	struct eth_qos_hid_entry *entry;

	if (eth_qos_mgr_find_hid(ctx, hid))
		return ERR_PTR(-EEXIST);

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	entry->hid = hid;
	entry->type = type;
	list_add_tail(&entry->list, &ctx->hid_table);

	return entry;
}

/* Return the index of an available L3/L4 filter entry */
static int eth_qos_mgr_get_l3l4_idx(struct eth_qos_mgr_ctx *ctx)
{
	u32 idx;

	for (idx = 0; idx < ctx->rx.max_l3l4_filters; idx++) {
		if (!ctx->rx.l3l4_table[idx].used)
			return idx;
	}

	return -ENOSPC;
}

/* Return the index of an available VLAN filter entry */
static int eth_qos_mgr_get_vlan_idx(struct eth_qos_mgr_ctx *ctx)
{
	u32 idx;

	for (idx = 0; idx < ctx->rx.max_vlan_filters; idx++) {
		if (!ctx->rx.vlan_table[idx].used)
			return idx;
	}

	return -ENOSPC;
}

static int eth_qos_mgr_validate_rx_queue(struct eth_qos_mgr_ctx *ctx, u32 queue)
{
	if (queue >= ctx->max_rx_queues)
		return -EINVAL;

	return 0;
}

static int eth_qos_mgr_validate_tx_queue(struct eth_qos_mgr_ctx *ctx, u32 queue)
{
	if (queue >= ctx->max_tx_queues)
		return -EINVAL;

	return 0;
}

static int eth_qos_mgr_validate_rx_q_ch_status(struct eth_qos_mgr_ctx *ctx, u32 ch,
						  enum eth_qos_filter_type type)
{
	if (ch >= ctx->max_rx_queues)
		return -EINVAL;

	return 0;
}

static int eth_qos_mgr_validate_tx_q_ch_status(struct eth_qos_mgr_ctx *ctx, u32 queue,
					       u32 ch, enum eth_qos_filter_type type)
{
	struct eth_qos_q_ch_entry *entry;

	if (ch >= ctx->max_tx_queues)
		return -EINVAL;

	if (type != ETH_QOS_FILTER_CBS)
		return 0;

	entry = eth_qos_mgr_find_q_ch_entry(&ctx->tx.q_ch_status_list, queue, ch);
	if (entry && entry->status == ETH_QOS_Q_CH_TX)
		return -EEXIST;

	return 0;
}

static int eth_qos_mgr_validate_l3l4_input(u16 sport, u16 dport, const char *proto,
					   const char *sip, const char *dip,
					   const char *sipv6, const char *dipv6)
{
	bool have_l3 = sip || dip || sipv6 || dipv6;
	bool have_l4 = sport || dport || proto;

	if (!have_l3 && !have_l4)
		return -EINVAL;

	if (have_l3 && have_l4)
		return -EINVAL;

	return 0;
}

/* RX/TX channel state update helpers. */
static void eth_qos_mgr_set_rx_q_ch_status(struct eth_qos_mgr_ctx *ctx, u32 queue,
					      u32 ch, enum eth_qos_filter_type type,
					      bool hw_path)
{
	struct eth_qos_q_ch_entry *entry;

	if (ch >= ctx->max_rx_queues)
		return;

	entry = eth_qos_mgr_get_or_create_q_ch_entry(&ctx->rx.q_ch_status_list, queue, ch);
	if (!entry)
		return;

	eth_qos_mgr_update_ch_entry(entry, eth_qos_mgr_get_rx_q_ch_status(type),
				    hw_path, queue, ch);
}

static void eth_qos_mgr_refresh_rx_q_ch_status(struct eth_qos_mgr_ctx *ctx, u32 queue, u32 ch)
{
	struct eth_qos_hid_entry *entry;
	struct eth_qos_q_ch_entry *ch_entry;
	enum eth_qos_q_ch_status status = ETH_QOS_Q_CH_NONE;
	bool hw_path = false;

	if (ch >= ctx->max_rx_queues)
		return;

	list_for_each_entry(entry, &ctx->hid_table, list) {
		if (entry->queue != queue || entry->ch != ch)
			continue;

		status = eth_qos_mgr_get_rx_q_ch_status(entry->type);
		hw_path = entry->hw_path;
	}

	ch_entry = eth_qos_mgr_find_q_ch_entry(&ctx->rx.q_ch_status_list, queue, ch);
	if (status == ETH_QOS_Q_CH_NONE) {
		eth_qos_mgr_del_q_ch_entry(ch_entry);
		return;
	}

	if (!ch_entry)
		ch_entry = eth_qos_mgr_get_or_create_q_ch_entry(&ctx->rx.q_ch_status_list,
							      queue, ch);
	if (!ch_entry)
		return;

	eth_qos_mgr_update_ch_entry(ch_entry, status, hw_path, queue, ch);
}

static bool eth_qos_mgr_rx_queue_in_use_by_other_filters(struct eth_qos_mgr_ctx *ctx,
							 u32 queue, u32 hid)
{
	struct eth_qos_hid_entry *entry;

	list_for_each_entry(entry, &ctx->hid_table, list) {
		if (entry->hid == hid)
			continue;

		if (entry->queue != queue)
			continue;

		if (entry->type == ETH_QOS_FILTER_VLAN ||
		    entry->type == ETH_QOS_FILTER_L3L4)
			return true;
	}

	return false;
}

static void eth_qos_mgr_revert_default_rx_mapping(struct net_device *ndev, u32 queue)
{
	stmmac_api_queue_dma_map(ndev, queue, queue);
}

static void eth_qos_mgr_set_tx_q_ch_status(struct eth_qos_mgr_ctx *ctx, u32 queue, u32 ch,
					      enum eth_qos_filter_type type,
					      bool hw_path)
{
	struct eth_qos_q_ch_entry *entry;

	if (ch >= ctx->max_tx_queues)
		return;

	entry = eth_qos_mgr_get_or_create_q_ch_entry(&ctx->tx.q_ch_status_list, queue, ch);
	if (!entry)
		return;

	eth_qos_mgr_update_ch_entry(entry,
				    eth_qos_mgr_get_tx_q_ch_status(type, hw_path),
				    hw_path, queue, ch);
}

static void eth_qos_mgr_refresh_tx_q_ch_status(struct eth_qos_mgr_ctx *ctx, u32 queue, u32 ch)
{
	struct eth_qos_hid_entry *entry;
	struct eth_qos_q_ch_entry *ch_entry;
	enum eth_qos_q_ch_status status = ETH_QOS_Q_CH_NONE;
	bool hw_path = false;

	if (ch >= ctx->max_tx_queues)
		return;

	list_for_each_entry(entry, &ctx->hid_table, list) {
		if (entry->queue != queue || entry->ch != ch)
			continue;

		hw_path = entry->type == ETH_QOS_FILTER_CBS ?
			  entry->u.cbs.hw_path : entry->hw_path;
		status = eth_qos_mgr_get_tx_q_ch_status(entry->type, hw_path);
	}

	ch_entry = eth_qos_mgr_find_q_ch_entry(&ctx->tx.q_ch_status_list, queue, ch);
	if (status == ETH_QOS_Q_CH_NONE) {
		eth_qos_mgr_del_q_ch_entry(ch_entry);
		return;
	}

	if (!ch_entry)
		ch_entry = eth_qos_mgr_get_or_create_q_ch_entry(&ctx->tx.q_ch_status_list,
							      queue, ch);
	if (!ch_entry)
		return;

	eth_qos_mgr_update_ch_entry(ch_entry, status, hw_path, queue, ch);
}

/* Internal state cleanup helpers. */
static void eth_qos_mgr_clear_rx_ctx(struct eth_qos_mgr_ctx *ctx)
{
	struct eth_qos_hid_entry *entry;
	struct eth_qos_hid_entry *tmp;
	u32 i;

	for (i = 0; i < ctx->rx.max_vlan_filters; i++)
		memset(&ctx->rx.vlan_table[i], 0, sizeof(ctx->rx.vlan_table[i]));

	for (i = 0; i < ctx->rx.max_l3l4_filters; i++)
		memset(&ctx->rx.l3l4_table[i], 0, sizeof(ctx->rx.l3l4_table[i]));

	for (i = 0; i < ARRAY_SIZE(ctx->rx.pcp_table); i++)
		memset(&ctx->rx.pcp_table[i], 0, sizeof(ctx->rx.pcp_table[i]));

	while (!list_empty(&ctx->rx.q_ch_status_list))
		eth_qos_mgr_del_q_ch_entry(list_first_entry(&ctx->rx.q_ch_status_list,
						       struct eth_qos_q_ch_entry, list));

	list_for_each_entry_safe(entry, tmp, &ctx->hid_table, list) {
		if (entry->type == ETH_QOS_FILTER_SW_TX_PCP || entry->type == ETH_QOS_FILTER_CBS)
			continue;

		list_del(&entry->list);
		kfree(entry);
	}

	ctx->rx.ra_users = 0;
}

static int eth_qos_mgr_clear_rx_hw(struct eth_qos_mgr_ctx *ctx)
{
	u32 i;
	int ret;

	for (i = 0; i < ctx->rx.max_vlan_filters; i++) {
		if (!ctx->rx.vlan_table[i].used)
			continue;

		ret = stmmac_api_del_hw_vlan_rx_fltr(ctx->ndev, ctx->rx.vlan_table[i].vlanid);
		if (ret) {
			eth_qos_log_err(NULL, "failed to delete VLAN filter for VLAN ID %u: ret=%d",
					ctx->rx.vlan_table[i].vlanid, ret);
			return ret;
		}
		eth_qos_mgr_revert_default_rx_mapping(ctx->ndev, ctx->rx.vlan_table[i].queue);
	}

	for (i = 0; i < ctx->rx.max_l3l4_filters; i++) {
		if (!ctx->rx.l3l4_table[i].used)
			continue;
		ret = stmmac_api_config_l3_filter_with_route_mask(ctx->ndev, i, false,
							    false, false, false,
							    0, NULL, 0, 0);
		if (ret) {
			eth_qos_log_err(NULL, "failed to config L3 filter for index %u: ret=%d", i, ret);
			return ret;
		}

		ret = stmmac_api_config_l4_filter_with_route(ctx->ndev, i, false,
							     false, false, false, false, 0, 0, 0);
		if (ret) {
			eth_qos_log_err(NULL, "failed to config L4 filter for index %u: ret=%d", i, ret);
			return ret;
		}

		eth_qos_mgr_revert_default_rx_mapping(ctx->ndev, ctx->rx.l3l4_table[i].queue);
	}

	for (i = 0; i < ARRAY_SIZE(ctx->rx.pcp_table); i++) {
		if (!ctx->rx.pcp_table[i].used)
			continue;
		ret = stmmac_api_config_pcp(ctx->ndev, BIT(ctx->rx.pcp_table[i].pcp),
					    ctx->rx.pcp_table[i].queue, true);
		if (ret) {
			eth_qos_log_err(NULL, "failed to config PCP for index %u: ret=%d", i, ret);
			return ret;
		}
		eth_qos_mgr_revert_default_rx_mapping(ctx->ndev, ctx->rx.pcp_table[i].queue);
	}

	ctx->rx.ra_users = 0;
	ret = stmmac_api_set_ra_mode(ctx->ndev, false);
	if (ret) {
		eth_qos_log_err(NULL, "failed to set RA mode: ret=%d", ret);
		return ret;
	}

	eth_qos_mgr_clear_rx_ctx(ctx);

	return 0;
}

static void eth_qos_mgr_clear_tx_ctx(struct eth_qos_mgr_ctx *ctx)
{
	struct eth_qos_hid_entry *entry;
	struct eth_qos_hid_entry *tmp;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(ctx->tx.pcp_table); i++)
		memset(&ctx->tx.pcp_table[i], 0, sizeof(ctx->tx.pcp_table[i]));

	for (i = 0; i < ctx->max_tx_queues; i++)
		memset(&ctx->tx.cbs_table[i], 0, sizeof(ctx->tx.cbs_table[i]));

	while (!list_empty(&ctx->tx.q_ch_status_list))
		eth_qos_mgr_del_q_ch_entry(list_first_entry(&ctx->tx.q_ch_status_list,
						       struct eth_qos_q_ch_entry, list));

	list_for_each_entry_safe(entry, tmp, &ctx->hid_table, list) {
		if (entry->type != ETH_QOS_FILTER_SW_TX_PCP && entry->type != ETH_QOS_FILTER_CBS)
			continue;

		list_del(&entry->list);
		kfree(entry);
	}
}

static int eth_qos_mgr_clear_tx_hw(struct eth_qos_mgr_ctx *ctx)
{
	u32 i;
	int ret;

	/* Queue 0 is not AVB capable */
	for (i = 1; i < ctx->max_tx_queues; i++) {
		ret = stmmac_api_config_cbs(ctx->ndev, false, 0, 0, 0, 0, i);
		if (ret) {
			eth_qos_log_err(NULL, "failed to config CBS for index %u: ret=%d", i, ret);
			return ret;
		}
	}

	eth_qos_mgr_clear_tx_ctx(ctx);

	return 0;
}

/* Debug/dump formatting helpers. */
static const char *eth_qos_mgr_filter_type_str(enum eth_qos_filter_type type)
{
	switch (type) {
	case ETH_QOS_FILTER_PCP:
		return "pcp";
	case ETH_QOS_FILTER_SW_TX_PCP:
		return "tx_pcp";
	case ETH_QOS_FILTER_VLAN:
		return "vlan";
	case ETH_QOS_FILTER_L3L4:
		return "l3l4";
	case ETH_QOS_FILTER_CBS:
		return "cbs";
	default:
		return "none";
	}
}

static const char *eth_qos_mgr_q_ch_status_str(enum eth_qos_q_ch_status status)
{
	switch (status) {
	case ETH_QOS_Q_CH_DMA:
		return "dma";
	case ETH_QOS_Q_CH_PCP:
		return "pcp";
	case ETH_QOS_Q_CH_TX:
		return "tx";
	default:
		return "none";
	}
}

static ssize_t eth_qos_mgr_dump_hid_table(struct eth_qos_mgr_ctx *ctx, char *buf,
					  size_t buf_sz, ssize_t len)
{
	struct eth_qos_hid_entry *entry;

	len += scnprintf(buf + len, buf_sz - len,
			 "hid_table:\n"
			 "hid\ttype\ttc\tqueue\tch\tdetails\n");

	list_for_each_entry(entry, &ctx->hid_table, list) {
		if (len >= buf_sz)
			break;

		len += scnprintf(buf + len, buf_sz - len,
				 "%u\t%s\t%u\t%u\t%u\t",
				 entry->hid,
				 eth_qos_mgr_filter_type_str(entry->type),
				 entry->tc, entry->queue, entry->ch);

		switch (entry->type) {
		case ETH_QOS_FILTER_PCP:
		case ETH_QOS_FILTER_SW_TX_PCP:
			len += scnprintf(buf + len, buf_sz - len, "pcp=%u path=%s\n",
					 entry->u.pcp.pcp,
					 entry->hw_path ? "hw" : "sw");
			break;
		case ETH_QOS_FILTER_VLAN:
			len += scnprintf(buf + len, buf_sz - len,
					 "vlanid=%u idx=%u path=%s\n",
					 entry->u.vlan.vlanid, entry->u.vlan.idx,
					 entry->hw_path ? "hw" : "sw");
			break;
		case ETH_QOS_FILTER_L3L4:
			len += scnprintf(buf + len, buf_sz - len, "idx=%u path=%s\n",
					 entry->u.l3l4.idx,
					 entry->hw_path ? "hw" : "sw");
			break;
		case ETH_QOS_FILTER_CBS:
			len += scnprintf(buf + len, buf_sz - len,
					 "hw_path=%u\n", entry->u.cbs.hw_path);
			break;
		default:
			len += scnprintf(buf + len, buf_sz - len, "-\n");
			break;
		}
	}
	len += scnprintf(buf + len, buf_sz - len, "\n");

	return min_t(ssize_t, len, buf_sz);
}

static ssize_t eth_qos_mgr_dump_rx_tables(struct eth_qos_mgr_ctx *ctx, char *buf,
					  size_t buf_sz, ssize_t len)
{
	u32 i;

	len += scnprintf(buf + len, buf_sz - len,
			 "rx_pcp_table:\n"
			 "pcp\tused\tqueue\tch\tpath\n");
	for (i = 0; i < ARRAY_SIZE(ctx->rx.pcp_table) && len < buf_sz; i++) {
		struct eth_qos_pcp_entry *entry = &ctx->rx.pcp_table[i];

		if (!entry->used)
			continue;

		len += scnprintf(buf + len, buf_sz - len,
				 "%u\t%u\t%u\t%u\t%s\n",
				 i, entry->used, entry->queue, entry->ch,
				 entry->hw_path ? "hw" : "sw");
	}
	len += scnprintf(buf + len, buf_sz - len, "\n");

	len += scnprintf(buf + len, buf_sz - len,
			 "rx_vlan_table:\n"
			 "idx\tused\tvlanid\tqueue\tch\tpath\n");
	for (i = 0; i < ctx->rx.max_vlan_filters && len < buf_sz; i++) {
		struct eth_qos_vlan_entry *entry = &ctx->rx.vlan_table[i];

		if (!entry->used)
			continue;

		len += scnprintf(buf + len, buf_sz - len,
				 "%u\t%u\t%u\t%u\t%u\t%s\n",
				 i, entry->used, entry->vlanid, entry->queue,
				 entry->ch, entry->hw_path ? "hw" : "sw");
	}
	len += scnprintf(buf + len, buf_sz - len, "\n");

	len += scnprintf(buf + len, buf_sz - len,
			 "rx_l3l4_table:\n"
			 "idx\tused\tqueue\tch\tpath\tsip\tdip\tsport\tdport\tproto\n");
	for (i = 0; i < ctx->rx.max_l3l4_filters && len < buf_sz; i++) {
		struct eth_qos_l3l4_entry *entry = &ctx->rx.l3l4_table[i];

		if (!entry->used)
			continue;

		len += scnprintf(buf + len, buf_sz - len,
				 "%u\t%u\t%u\t%u\t%s\t%pI4h\t%pI4h\t%u\t%u\t%u\n",
				 i, entry->used, entry->queue, entry->ch,
				 entry->hw_path ? "hw" : "sw",
				 &entry->sip, &entry->dip,
				 entry->sport, entry->dport, entry->protocol);
		if (!eth_qos_mgr_ipv6_addr_any(entry->sipv6))
			len += scnprintf(buf + len, buf_sz - len,
					 "\tsipv6=%pI6c\n", entry->sipv6);
		if (!eth_qos_mgr_ipv6_addr_any(entry->dipv6))
			len += scnprintf(buf + len, buf_sz - len,
					 "\tdipv6=%pI6c\n", entry->dipv6);
	}
	len += scnprintf(buf + len, buf_sz - len, "\n");

	struct eth_qos_q_ch_entry *entry;

	len += scnprintf(buf + len, buf_sz - len,
				"rx_q_ch_status_table:\n"
				"status\tpath\tqueue\tchannel\n");
	list_for_each_entry(entry, &ctx->rx.q_ch_status_list, list) {
		if (len >= buf_sz)
			break;
		len += scnprintf(buf + len, buf_sz - len,
					"%s\t%s\t%u\t%u\n",
					eth_qos_mgr_q_ch_status_str(entry->status),
					entry->hw_path ? "hw" : "sw",
					entry->queue, entry->channel);
	}
	len += scnprintf(buf + len, buf_sz - len, "\n");

	return min_t(ssize_t, len, buf_sz);
}

static ssize_t eth_qos_mgr_dump_tx_tables(struct eth_qos_mgr_ctx *ctx, char *buf,
					  size_t buf_sz, ssize_t len)
{
	u32 i;

	len += scnprintf(buf + len, buf_sz - len,
			 "tx_pcp_table:\n"
			 "pcp\tused\tqueue\tch\n");
	for (i = 0; i < ARRAY_SIZE(ctx->tx.pcp_table) && len < buf_sz; i++) {
		struct eth_qos_tx_pcp_entry *entry = &ctx->tx.pcp_table[i];

		if (!entry->used)
			continue;

		len += scnprintf(buf + len, buf_sz - len,
				 "%u\t%u\t%u\t%u\n",
				 i, entry->used, entry->queue, entry->ch);
	}
	len += scnprintf(buf + len, buf_sz - len, "\n");

	len += scnprintf(buf + len, buf_sz - len,
			 "tx_cbs_table:\n"
			 "queue\tused\thw\ttc\tch\tidleslope\tsendslope\thicredit\tlocredit\n");
	for (i = 0; i < ctx->max_tx_queues && len < buf_sz; i++) {
		struct eth_qos_cbs_entry *entry = &ctx->tx.cbs_table[i];

		if (!entry->used)
			continue;

		len += scnprintf(buf + len, buf_sz - len,
				 "%u\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%d\n",
				 i, entry->used, entry->hw_path, entry->tc,
				 entry->ch, entry->idleslope, entry->sendslope,
				 entry->hicredit, entry->locredit);
	}
	len += scnprintf(buf + len, buf_sz - len, "\n");

	struct eth_qos_q_ch_entry *entry;

	len += scnprintf(buf + len, buf_sz - len,
				"tx_q_ch_status_table:\n"
				"status\tpath\tqueue\tchannel\n");
	list_for_each_entry(entry, &ctx->tx.q_ch_status_list, list) {
		if (len >= buf_sz)
			break;
		len += scnprintf(buf + len, buf_sz - len,
					"%s\t%s\t%u\t%u\n",
					eth_qos_mgr_q_ch_status_str(entry->status),
					entry->hw_path ? "hw" : "sw",
					entry->queue, entry->channel);
	}

	return min_t(ssize_t, len, buf_sz);
}

/* Public QoS programming APIs. */
int eth_qos_mgr_add_vlan(struct net_device *ndev, u32 hid, u32 queue, u32 ch,
			 bool hw_path, u16 vlanid)
{
	struct eth_qos_mgr_ctx *ctx;
	struct eth_qos_hid_entry *entry;
	int idx;
	int ret;

	ctx = eth_qos_mgr_get_or_create_ctx_locked(ndev);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	/* Validate software-visible RX resources before touching hardware state. */
	ret = eth_qos_mgr_validate_rx_queue(ctx, queue);
	if (ret)
		goto out_unlock;

	ret = eth_qos_mgr_validate_rx_q_ch_status(ctx, ch, ETH_QOS_FILTER_VLAN);
	if (ret)
		goto out_unlock;

	entry = eth_qos_mgr_alloc_hid(ctx, hid, ETH_QOS_FILTER_VLAN);
	if (IS_ERR(entry)) {
		ret = PTR_ERR(entry);
		goto out_unlock;
	}

	idx = eth_qos_mgr_get_vlan_idx(ctx);
	if (idx < 0) {
		ret = idx;
		goto err_free_entry;
	}

	/*
	 * Program the stmmac VLAN routing path and ensure the target queue
	 * is dynamically mapped before the filter is enabled.
	 */
	if (!ctx->rx.ra_users) {
		ret = stmmac_api_set_ra_mode(ndev, true);
		if (ret)
			goto err_free_entry;
	}
	stmmac_api_queue_dma_map_dynamic(ndev, queue);
	ret = stmmac_api_add_hw_vlan_rx_routing_fltr(ndev, vlanid, ch);
	if (ret)
		goto err_disable_ra;

	/* Update the manager tables only after hardware programming succeeds. */
	ctx->rx.vlan_table[idx].used = true;
	ctx->rx.vlan_table[idx].hw_path = hw_path;
	ctx->rx.vlan_table[idx].vlanid = vlanid;
	ctx->rx.vlan_table[idx].queue = queue;
	ctx->rx.vlan_table[idx].ch = ch;

	ctx->rx.ra_users++;
	entry->queue = queue;
	entry->ch = ch;
	entry->hw_path = hw_path;
	entry->u.vlan.vlanid = vlanid;
	entry->u.vlan.idx = idx;
	eth_qos_mgr_set_rx_q_ch_status(ctx, queue, ch, entry->type, hw_path);
	ret = 0;
	goto out_unlock;

err_disable_ra:
	if (!ctx->rx.ra_users)
		stmmac_api_set_ra_mode(ndev, false);
err_free_entry:
	list_del(&entry->list);
	kfree(entry);
out_unlock:
	mutex_unlock(&ctx->lock);
	return ret;
}

int eth_qos_mgr_add_l3l4(struct net_device *ndev, u32 hid, u32 queue, u32 ch,
			 bool hw_path, u16 sport, u16 dport, const char *proto,
			 const char *sip, const char *dip,
			 const char *sipv6, const char *dipv6)
{
	struct eth_qos_mgr_ctx *ctx;
	struct eth_qos_hid_entry *entry;
	struct sockaddr_storage address;
	u32 ipv6_addr[4];
	u32 sip_addr = 0, dip_addr = 0;
	int idx;
	int ret;

	ctx = eth_qos_mgr_get_or_create_ctx_locked(ndev);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	/* Validate queue/channel ownership and reject mixed L3/L4 input forms. */
	ret = eth_qos_mgr_validate_rx_queue(ctx, queue);
	if (ret)
		goto out_unlock;

	ret = eth_qos_mgr_validate_rx_q_ch_status(ctx, ch, ETH_QOS_FILTER_L3L4);
	if (ret)
		goto out_unlock;

	ret = eth_qos_mgr_validate_l3l4_input(sport, dport, proto, sip, dip,
					      sipv6, dipv6);
	if (ret)
		goto out_unlock;

	entry = eth_qos_mgr_alloc_hid(ctx, hid, ETH_QOS_FILTER_L3L4);
	if (IS_ERR(entry)) {
		ret = PTR_ERR(entry);
		goto out_unlock;
	}

	idx = eth_qos_mgr_get_l3l4_idx(ctx);
	if (idx < 0) {
		ret = idx;
		goto err_free_entry;
	}

	/*
	 * Prepare the stmmac routing path first, then program each requested
	 * L3/L4 match field into the allocated classifier slot.
	 */
	if (!ctx->rx.ra_users) {
		ret = stmmac_api_set_ra_mode(ndev, true);
		if (ret)
			goto err_free_entry;
	}
	stmmac_api_queue_dma_map_dynamic(ndev, queue);

	if (sip) {
		inet_pton_with_scope(&init_net, AF_UNSPEC, sip, NULL, &address);
		eth_qos_mgr_convert_ip_addr(&address, &sip_addr, NULL);
		ret = stmmac_api_config_l3_filter_with_route_mask(ndev, idx, true, false, true, false,
							    sip_addr, NULL, 32, ch);
		if (ret)
			goto err_disable_filters;
	}

	if (dip) {
		inet_pton_with_scope(&init_net, AF_UNSPEC, dip, NULL, &address);
		eth_qos_mgr_convert_ip_addr(&address, &dip_addr, NULL);
		ret = stmmac_api_config_l3_filter_with_route_mask(ndev, idx, true, false, false, false,
							    dip_addr, NULL, 32, ch);
		if (ret)
			goto err_disable_filters;
	}

	if (sipv6) {
		inet_pton_with_scope(&init_net, AF_UNSPEC, sipv6, NULL, &address);
		eth_qos_mgr_convert_ip_addr(&address, NULL, ipv6_addr);
		ret = stmmac_api_config_l3_filter_with_route_mask(ndev, idx, true, true, true, false,
							    0, ipv6_addr, 128, ch);
		if (ret)
			goto err_disable_filters;
	}

	if (dipv6) {
		inet_pton_with_scope(&init_net, AF_UNSPEC, dipv6, NULL, &address);
		eth_qos_mgr_convert_ip_addr(&address, NULL, ipv6_addr);
		ret = stmmac_api_config_l3_filter_with_route_mask(ndev, idx, true, true, false, false,
							    0, ipv6_addr, 128, ch);
		if (ret)
			goto err_disable_filters;
	}

	if ((sport || dport) && proto && (!strcmp(proto, "tcp") || !strcmp(proto, "TCP"))) {
		ret = stmmac_api_config_l4_filter_with_route(ndev, idx, true, false,
							     sport != 0, dport != 0, false,
							     sport, dport, ch);
		if (ret)
			goto err_disable_filters;
	} else if ((sport || dport) && proto && (!strcmp(proto, "udp") || !strcmp(proto, "UDP"))) {
		ret = stmmac_api_config_l4_filter_with_route(ndev, idx, true, true,
							     sport != 0, dport != 0, false,
							     sport, dport, ch);
		if (ret)
			goto err_disable_filters;
	}

	/* Mirror the active filter programming in the software tracking table. */
	ctx->rx.l3l4_table[idx].used = true;
	ctx->rx.l3l4_table[idx].hw_path = hw_path;
	ctx->rx.l3l4_table[idx].queue = queue;
	ctx->rx.l3l4_table[idx].ch = ch;
	ctx->rx.l3l4_table[idx].sip = sip_addr;
	ctx->rx.l3l4_table[idx].dip = dip_addr;
	ctx->rx.l3l4_table[idx].sport = sport;
	ctx->rx.l3l4_table[idx].dport = dport;
	if (sipv6)
		memcpy(ctx->rx.l3l4_table[idx].sipv6, ipv6_addr,
		       sizeof(ctx->rx.l3l4_table[idx].sipv6));
	if (dipv6)
		memcpy(ctx->rx.l3l4_table[idx].dipv6, ipv6_addr,
		       sizeof(ctx->rx.l3l4_table[idx].dipv6));
	if (proto && (!strcmp(proto, "tcp") || !strcmp(proto, "TCP")))
		ctx->rx.l3l4_table[idx].protocol = IPPROTO_TCP;
	else if (proto && (!strcmp(proto, "udp") || !strcmp(proto, "UDP")))
		ctx->rx.l3l4_table[idx].protocol = IPPROTO_UDP;

	ctx->rx.ra_users++;
	entry->queue = queue;
	entry->ch = ch;
	entry->hw_path = hw_path;
	entry->u.l3l4.idx = idx;
	eth_qos_mgr_set_rx_q_ch_status(ctx, queue, ch, entry->type, hw_path);
	ret = 0;
	goto out_unlock;

err_disable_filters:
	stmmac_api_config_l3_filter_with_route_mask(ndev, idx, false, false, false, false, 0, NULL, 0, 0);
	stmmac_api_config_l4_filter_with_route(ndev, idx, false, false, false, false, false, 0, 0, 0);
	if (!ctx->rx.ra_users)
		stmmac_api_set_ra_mode(ndev, false);
err_free_entry:
	list_del(&entry->list);
	kfree(entry);
out_unlock:
	mutex_unlock(&ctx->lock);
	return ret;
}

int eth_qos_mgr_add_pcp(struct net_device *ndev, u32 hid, u8 pcp, u32 queue, u32 ch,
			bool hw_path)
{
	struct eth_qos_mgr_ctx *ctx;
	struct eth_qos_hid_entry *entry;
	int ret;

	ctx = eth_qos_mgr_get_or_create_ctx_locked(ndev);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	/* Validate the requested RX queue/channel before programming PCP routing. */
	ret = eth_qos_mgr_validate_rx_queue(ctx, queue);
	if (ret)
		goto out_unlock;

	ret = eth_qos_mgr_validate_rx_q_ch_status(ctx, ch, ETH_QOS_FILTER_PCP);
	if (ret)
		goto out_unlock;

	entry = eth_qos_mgr_alloc_hid(ctx, hid, ETH_QOS_FILTER_PCP);
	if (IS_ERR(entry)) {
		ret = PTR_ERR(entry);
		goto out_unlock;
	}

	/* Push the PCP-to-queue/channel mapping down to stmmac hardware. */
	if (!ctx->rx.ra_users) {
		ret = stmmac_api_set_ra_mode(ndev, true);
		if (ret)
			goto err_free_entry;
	}
	ret = stmmac_api_config_pcp(ndev, BIT(pcp), queue, false);
	if (ret)
		goto err_disable_ra;
	stmmac_api_queue_dma_map(ndev, queue, ch);

	/* Record the programmed PCP route in the manager tables. */
	ctx->rx.pcp_table[pcp].used = true;
	ctx->rx.pcp_table[pcp].hw_path = hw_path;
	ctx->rx.pcp_table[pcp].pcp = pcp;
	ctx->rx.pcp_table[pcp].queue = queue;
	ctx->rx.pcp_table[pcp].ch = ch;

	ctx->rx.ra_users++;
	entry->queue = queue;
	entry->ch = ch;
	entry->hw_path = hw_path;
	entry->u.pcp.pcp = pcp;
	eth_qos_mgr_set_rx_q_ch_status(ctx, queue, ch, entry->type, hw_path);
	ret = 0;
	goto out_unlock;

err_disable_ra:
	if (!ctx->rx.ra_users)
		stmmac_api_set_ra_mode(ndev, false);
err_free_entry:
	list_del(&entry->list);
	kfree(entry);
out_unlock:
	mutex_unlock(&ctx->lock);
	return ret;
}

int eth_qos_mgr_add_tx_pcp(struct net_device *ndev, u32 hid, u8 pcp, u32 queue, u32 ch)
{
	struct eth_qos_mgr_ctx *ctx;
	struct eth_qos_hid_entry *entry;
	int ret;

	ctx = eth_qos_mgr_get_or_create_ctx_locked(ndev);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	/* Validate the TX queue/channel pair used for egress PCP selection. */
	ret = eth_qos_mgr_validate_tx_queue(ctx, queue);
	if (ret)
		goto out_unlock;

	entry = eth_qos_mgr_alloc_hid(ctx, hid, ETH_QOS_FILTER_SW_TX_PCP);
	if (IS_ERR(entry)) {
		ret = PTR_ERR(entry);
		goto out_unlock;
	}

	ret = eth_qos_mgr_validate_tx_queue(ctx, ch);
	if (ret)
		goto err_free_entry;

	/* Program the PCP mapping in stmmac before exposing it in software. */
	// TODO: stmmac currently lacks a dedicated API for TX PCP routing

	/* Update the TX PCP table and HID tracking after successful programming. */
	ctx->tx.pcp_table[pcp].used = true;
	ctx->tx.pcp_table[pcp].pcp = pcp;
	ctx->tx.pcp_table[pcp].queue = queue;
	ctx->tx.pcp_table[pcp].ch = ch;

	entry->queue = queue;
	entry->ch = ch;
	entry->u.pcp.pcp = pcp;
	eth_qos_mgr_set_tx_q_ch_status(ctx, queue, ch, entry->type, false);
	ret = 0;
	goto out_unlock;

err_free_entry:
	list_del(&entry->list);
	kfree(entry);
out_unlock:
	mutex_unlock(&ctx->lock);
	return ret;
}

int eth_qos_mgr_add_cbs(struct net_device *ndev, u32 hid, u32 tc, u32 queue, u32 ch,
			bool hw_path, int sendslope, int idleslope,
			int hicredit, int locredit)
{
	struct eth_qos_mgr_ctx *ctx;
	struct eth_qos_hid_entry *entry;
	struct ioss_interface *iface;
	struct ioss_device *idev;
	int ret;

	ctx = eth_qos_mgr_get_or_create_ctx_locked(ndev);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	/* Validate the TX queue/channel resources before enabling CBS. */
	ret = eth_qos_mgr_validate_tx_queue(ctx, queue);
	if (ret)
		goto out_unlock;

	ret = eth_qos_mgr_validate_tx_q_ch_status(ctx, queue, ch, ETH_QOS_FILTER_CBS);
	if (ret)
		goto out_unlock;

	entry = eth_qos_mgr_alloc_hid(ctx, hid, ETH_QOS_FILTER_CBS);
	if (IS_ERR(entry)) {
		ret = PTR_ERR(entry);
		goto out_unlock;
	}

	/*
	 * Program the stmmac CBS shaper first; when the flow uses the hardware
	 * path, also mirror the traffic-class mapping into IOSS.
	 */
	ret = stmmac_api_config_cbs(ndev, true, sendslope, idleslope, hicredit, locredit,
				    queue);
	if (ret)
		goto err_free_entry;

	/* Persist the CBS configuration so it can be dumped/replayed later. */
	ctx->tx.cbs_table[queue].used = true;
	ctx->tx.cbs_table[queue].hw_path = hw_path;
	ctx->tx.cbs_table[queue].tc = tc;
	ctx->tx.cbs_table[queue].queue = queue;
	ctx->tx.cbs_table[queue].ch = ch;
	ctx->tx.cbs_table[queue].idleslope = idleslope;
	ctx->tx.cbs_table[queue].sendslope = sendslope;
	ctx->tx.cbs_table[queue].hicredit = hicredit;
	ctx->tx.cbs_table[queue].locredit = locredit;

	entry->queue = queue;
	entry->tc = tc;
	entry->ch = ch;
	entry->hw_path = hw_path;
	entry->u.cbs.hw_path = hw_path;
	eth_qos_mgr_set_tx_q_ch_status(ctx, queue, ch, entry->type, hw_path);
	ret = 0;
	goto out_unlock;

err_free_entry:
	list_del(&entry->list);
	kfree(entry);
out_unlock:
	mutex_unlock(&ctx->lock);
	return ret;
}

int eth_qos_mgr_del(struct net_device *ndev, u32 hid)
{
	struct eth_qos_mgr_ctx *ctx;
	struct eth_qos_hid_entry *entry;
	int ret = -ENOENT;
	u32 ch = 0;

	ctx = eth_qos_mgr_get_or_create_ctx_locked(ndev);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	entry = eth_qos_mgr_find_hid(ctx, hid);
	if (!entry)
		goto out_unlock;

	ch = entry->ch;

	switch (entry->type) {
	case ETH_QOS_FILTER_PCP:
		/* Clear the hardware PCP route, then drop the cached RX table entry. */
		ret = stmmac_api_config_pcp(ndev, BIT(entry->u.pcp.pcp),
					    entry->queue, true);
		if (!ret) {
			if (!eth_qos_mgr_rx_queue_in_use_by_other_filters(ctx, entry->queue,
									  entry->hid))
				eth_qos_mgr_revert_default_rx_mapping(ndev, entry->queue);
			memset(&ctx->rx.pcp_table[entry->u.pcp.pcp], 0,
			       sizeof(ctx->rx.pcp_table[entry->u.pcp.pcp]));
			if (ctx->rx.ra_users)
				ctx->rx.ra_users--;
		}
		break;
	case ETH_QOS_FILTER_SW_TX_PCP:
		/*
		 * No dedicated TX PCP clear API exists today, so only the software
		 * bookkeeping is removed and channel status is recomputed below.
		 */
		if (!ret)
			memset(&ctx->tx.pcp_table[entry->u.pcp.pcp], 0,
			       sizeof(ctx->tx.pcp_table[entry->u.pcp.pcp]));
		break;
	case ETH_QOS_FILTER_VLAN:
		ret = stmmac_api_del_hw_vlan_rx_fltr(ndev, entry->u.vlan.vlanid);
		if (!ret) {
			if (!eth_qos_mgr_rx_queue_in_use_by_other_filters(ctx, entry->queue,
									  entry->hid))
				eth_qos_mgr_revert_default_rx_mapping(ndev, entry->queue);
			memset(&ctx->rx.vlan_table[entry->u.vlan.idx], 0,
			       sizeof(ctx->rx.vlan_table[entry->u.vlan.idx]));
			if (ctx->rx.ra_users)
				ctx->rx.ra_users--;
		}
		break;
	case ETH_QOS_FILTER_L3L4:
		/* Disable both L3 and L4 classifier state before clearing the table slot. */
		ret = stmmac_api_config_l3_filter_with_route_mask(ndev, entry->u.l3l4.idx, false,
							    false, false, false, 0, NULL, 0, 0);
		if (!ret)
			ret = stmmac_api_config_l4_filter_with_route(ndev, entry->u.l3l4.idx,
								     false, false, false,
								     false, false, 0, 0, 0);
		if (!ret) {
			if (!eth_qos_mgr_rx_queue_in_use_by_other_filters(ctx, entry->queue,
									  entry->hid))
				eth_qos_mgr_revert_default_rx_mapping(ndev, entry->queue);
			memset(&ctx->rx.l3l4_table[entry->u.l3l4.idx], 0,
			       sizeof(ctx->rx.l3l4_table[entry->u.l3l4.idx]));
			if (ctx->rx.ra_users)
				ctx->rx.ra_users--;
		}
		break;
	case ETH_QOS_FILTER_CBS:
		/* Reset the shaper programming, then clear the cached CBS profile. */
		ret = stmmac_api_config_cbs(ndev, false, 0, 0, 0, 0, entry->queue);
		eth_qos_log_cfg(NULL, "eth_qos_mgr: deleting CBS filter with hid=%u on queue=%u ch=%u ret=%d\n",
			    entry->hid, entry->queue, entry->ch, ret);
		if (!ret)
			memset(&ctx->tx.cbs_table[entry->queue], 0,
			       sizeof(ctx->tx.cbs_table[entry->queue]));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (!ctx->rx.ra_users)
		stmmac_api_set_ra_mode(ndev, false);

	if (ret)
		goto out_unlock;

	list_del(&entry->list);

	if (entry->type == ETH_QOS_FILTER_SW_TX_PCP || entry->type == ETH_QOS_FILTER_CBS)
		eth_qos_mgr_refresh_tx_q_ch_status(ctx, entry->queue, ch);
	else
		eth_qos_mgr_refresh_rx_q_ch_status(ctx, entry->queue, ch);

	kfree(entry);


out_unlock:
	mutex_unlock(&ctx->lock);
	return ret;
}

int eth_qos_mgr_clear_rx_sw(struct net_device *ndev)
{
	struct eth_qos_mgr_ctx *ctx;

	ctx = eth_qos_mgr_get_existing_ctx_locked(ndev);
	if (!ctx)
		return 0;

	eth_qos_mgr_clear_rx_ctx(ctx);
	mutex_unlock(&ctx->lock);

	return 0;
}

int eth_qos_mgr_clear_all(struct net_device *ndev)
{
	struct eth_qos_mgr_ctx *ctx;
	int ret, rx_ret, tx_ret;

	ctx = eth_qos_mgr_get_existing_ctx_locked(ndev);
	if (!ctx)
		return 0;

	rx_ret = eth_qos_mgr_clear_rx_hw(ctx);
	if (rx_ret) {
		eth_qos_log_err(NULL, "eth_qos_mgr: failed to clear RX QoS state for %s: %d",
			    ndev->name, rx_ret);
	}
	tx_ret = eth_qos_mgr_clear_tx_hw(ctx);
	if (tx_ret) {
		eth_qos_log_err(NULL, "eth_qos_mgr: failed to clear TX QoS state for %s: %d",
			    ndev->name, tx_ret);
	}
	mutex_unlock(&ctx->lock);

	ret = rx_ret ? rx_ret : tx_ret;
	return ret;
}

/* Replays UL QoS filters since only UL filters are erased during suspend */
int eth_qos_mgr_replay(struct net_device *ndev)
{
	struct eth_qos_mgr_ctx *ctx;
	struct eth_qos_hid_entry *entry;
	int ret = 0;

	ctx = eth_qos_mgr_get_existing_ctx_locked(ndev);
	if (!ctx)
		return 0;

	ctx->rx.ra_users = 0;

	list_for_each_entry(entry, &ctx->hid_table, list) {
		switch (entry->type) {
		case ETH_QOS_FILTER_PCP:
			if (!ctx->rx.ra_users) {
				ret = stmmac_api_set_ra_mode(ndev, true);
				if (ret)
					goto out_unlock;
			}
			ret = stmmac_api_config_pcp(ndev, BIT(entry->u.pcp.pcp),
						    entry->queue, false);
			if (ret)
				goto out_unlock;
			stmmac_api_queue_dma_map(ndev, entry->queue, entry->ch);
			ctx->rx.ra_users++;
			break;
		case ETH_QOS_FILTER_VLAN:
			if (!ctx->rx.ra_users) {
				ret = stmmac_api_set_ra_mode(ndev, true);
				if (ret)
					goto out_unlock;
			}
			stmmac_api_queue_dma_map_dynamic(ndev, entry->queue);
			ret = stmmac_api_add_hw_vlan_rx_routing_fltr(ndev,
								     entry->u.vlan.vlanid,
								     entry->ch);
			if (ret)
				goto out_unlock;
			ctx->rx.ra_users++;
			break;
		case ETH_QOS_FILTER_L3L4: {
			struct eth_qos_l3l4_entry *l3l4;

			if (entry->u.l3l4.idx >= ctx->rx.max_l3l4_filters) {
				ret = -EINVAL;
				goto out_unlock;
			}
			l3l4 = &ctx->rx.l3l4_table[entry->u.l3l4.idx];

			if (!ctx->rx.ra_users) {
				ret = stmmac_api_set_ra_mode(ndev, true);
				if (ret)
					goto out_unlock;
			}
			stmmac_api_queue_dma_map_dynamic(ndev, entry->queue);

			if (l3l4->sip) {
				ret = stmmac_api_config_l3_filter_with_route_mask(ndev,
									    entry->u.l3l4.idx,
									    true, false,
									    true, false,
									    l3l4->sip, NULL,
									    32, entry->ch);
				if (ret)
					goto out_unlock;
			}

			if (l3l4->dip) {
				ret = stmmac_api_config_l3_filter_with_route_mask(ndev,
									    entry->u.l3l4.idx,
									    true, false,
									    false, false,
									    l3l4->dip, NULL,
									    32, entry->ch);
				if (ret)
					goto out_unlock;
			}

			if (!eth_qos_mgr_ipv6_addr_any(l3l4->sipv6)) {
				ret = stmmac_api_config_l3_filter_with_route_mask(ndev,
									    entry->u.l3l4.idx,
									    true, true,
									    true, false,
									    0, l3l4->sipv6,
									    128, entry->ch);
				if (ret)
					goto out_unlock;
			}

			if (!eth_qos_mgr_ipv6_addr_any(l3l4->dipv6)) {
				ret = stmmac_api_config_l3_filter_with_route_mask(ndev,
									    entry->u.l3l4.idx,
									    true, true,
									    false, false,
									    0, l3l4->dipv6,
									    128, entry->ch);
				if (ret)
					goto out_unlock;
			}

			if ((l3l4->sport || l3l4->dport) && l3l4->protocol == IPPROTO_TCP) {
				ret = stmmac_api_config_l4_filter_with_route(ndev,
									     entry->u.l3l4.idx,
									     true, false,
									     l3l4->sport != 0,
									     l3l4->dport != 0,
									     false,
									     l3l4->sport,
									     l3l4->dport,
									     entry->ch);
				if (ret)
					goto out_unlock;
			} else if ((l3l4->sport || l3l4->dport) &&
				   l3l4->protocol == IPPROTO_UDP) {
				ret = stmmac_api_config_l4_filter_with_route(ndev,
									     entry->u.l3l4.idx,
									     true, true,
									     l3l4->sport != 0,
									     l3l4->dport != 0,
									     false,
									     l3l4->sport,
									     l3l4->dport,
									     entry->ch);
				if (ret)
					goto out_unlock;
			}

			ctx->rx.ra_users++;
			break;
		}
		case ETH_QOS_FILTER_CBS:
		case ETH_QOS_FILTER_SW_TX_PCP:
		case ETH_QOS_FILTER_NONE:
		default:
			break;
		}
	}

out_unlock:
	mutex_unlock(&ctx->lock);
	return ret;
}

void eth_qos_mgr_release(struct net_device *ndev)
{
	struct eth_qos_mgr_ctx *ctx;
	int ret;

	ctx = eth_qos_mgr_detach_ctx(ndev);
	if (!ctx)
		return;

	ret = eth_qos_mgr_clear_rx_hw(ctx);
	if (ret)
		eth_qos_log_err(NULL, "eth_qos_mgr: failed to clear RX QoS state for %s during release: %d",
			    ndev->name, ret);

	ret = eth_qos_mgr_clear_tx_hw(ctx);
	if (ret)
		eth_qos_log_err(NULL, "eth_qos_mgr: failed to clear TX QoS state for %s during release: %d",
			    ndev->name, ret);
	mutex_unlock(&ctx->lock);

	eth_qos_mgr_free_ctx(ctx);
}

ssize_t eth_qos_mgr_dump(struct net_device *ndev, char *buf, size_t buf_sz)
{
	struct eth_qos_mgr_ctx *ctx;
	ssize_t len = 0;

	if (!buf || !buf_sz)
		return -EINVAL;

	ctx = eth_qos_mgr_get_or_create_ctx_locked(ndev);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	len += scnprintf(buf + len, buf_sz - len,
			 "eth_qos_mgr: dev=%s max_rx_queues=%u max_tx_queues=%u max_l3l4_filters=%u max_vlan_filters=%u\n",
			 ndev->name, ctx->max_rx_queues, ctx->max_tx_queues,
			 ctx->rx.max_l3l4_filters, ctx->rx.max_vlan_filters);

	len = eth_qos_mgr_dump_hid_table(ctx, buf, buf_sz, len);
	len = eth_qos_mgr_dump_rx_tables(ctx, buf, buf_sz, len);
	len = eth_qos_mgr_dump_tx_tables(ctx, buf, buf_sz, len);
	len += scnprintf(buf + len, buf_sz - len, "\n");

	mutex_unlock(&ctx->lock);

	return min_t(ssize_t, len, buf_sz);
}
