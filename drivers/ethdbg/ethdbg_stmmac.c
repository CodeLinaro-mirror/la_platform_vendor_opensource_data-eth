// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

/*
 * ethdbg_stmmac.c - Minidump registration for stmmac driver structures.
 *
 * Captures:
 *   - struct net_device          region: <ifname>-ndev
 *   - struct stmmac_priv         region: <ifname>-priv
 *   - struct platform_device     region: <ifname>-dev
 *
 * Also provides indirect register read helpers for QoS VLAN and L3/L4
 * filters, which require stmmac-private register definitions.
 */

#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>

#include "stmmac.h"
#include "dwxgmac2.h"
#include "stmmac_vlan.h"
#include "ethdbg.h"

static void ethdbg_stmmac_capture_rx_queue(struct net_device *ndev,
					   const char *ifname, unsigned int q)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned long vaddr;
	unsigned int size;
	char owner[ETHDBG_MD_NAME_LEN];
	int ret;

	if (!netif_running(ndev))
		return;

	if (priv->plat->rx_queues_cfg[q].api_managed)
		return;

	if (priv->extend_desc) {
		vaddr = (unsigned long)priv->dma_conf.rx_queue[q].dma_erx;
		size  = priv->dma_conf.dma_rx_size * sizeof(struct dma_extended_desc);
	} else {
		vaddr = (unsigned long)priv->dma_conf.rx_queue[q].dma_rx;
		size  = priv->dma_conf.dma_rx_size * sizeof(struct dma_desc);
	}

	if (!vaddr || !size)
		return;

	scnprintf(owner, sizeof(owner), "%s-rx%u", ifname, q);

	ret = ethdbg_va_md_add_region(owner, vaddr, size);
	if (ret)
		pr_err("ethdbg: failed to add VA-MD region %s ret %d\n", owner, ret);
}

static void ethdbg_stmmac_capture_tx_queue(struct net_device *ndev,
					   const char *ifname, unsigned int q)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct stmmac_tx_queue *tx_q = &priv->dma_conf.tx_queue[q];
	unsigned long vaddr;
	unsigned int size;
	char owner[ETHDBG_MD_NAME_LEN];
	int ret;

	if (!netif_running(ndev))
		return;

	if (priv->plat->tx_queues_cfg[q].api_managed)
		return;

	if (priv->extend_desc) {
		vaddr = (unsigned long)tx_q->dma_etx;
		size  = priv->dma_conf.dma_tx_size * sizeof(struct dma_extended_desc);
	} else if (tx_q->tbs & STMMAC_TBS_AVAIL) {
		vaddr = (unsigned long)tx_q->dma_entx;
		size  = priv->dma_conf.dma_tx_size * sizeof(struct dma_edesc);
	} else {
		vaddr = (unsigned long)tx_q->dma_tx;
		size  = priv->dma_conf.dma_tx_size * sizeof(struct dma_desc);
	}

	if (!vaddr || !size)
		return;

	scnprintf(owner, sizeof(owner), "%s-tx%u", ifname, q);

	ret = ethdbg_va_md_add_region(owner, vaddr, size);
	if (ret)
		pr_err("ethdbg: failed to add VA-MD region %s ret %d\n", owner, ret);
}

static const struct ethdbg_capture_ops ethdbg_stmmac_capture_ops = {
	.capture_rx_queue = ethdbg_stmmac_capture_rx_queue,
	.capture_tx_queue = ethdbg_stmmac_capture_tx_queue,
};

void ethdbg_stmmac_register(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev = iface->device;
	const char *interface_name = iface->interface_name;
	struct net_device *net_dev;
	struct stmmac_priv *priv;
	struct device *parent_dev;
	char md_name[ETHDBG_MD_NAME_LEN];
	int ret;

	if (!dev || !dev->net_dev)
		return;

	net_dev = dev->net_dev;

	/* Register net_device region */
	scnprintf(md_name, sizeof(md_name), "%s-ndev", interface_name);
	ret = ethdbg_minidump_add_region(md_name, (uintptr_t)net_dev, sizeof(*net_dev));
	if (!ret)
		pr_info("ethdbg: [%s] registered minidump region '%s'\n", interface_name, md_name);

	/* Register stmmac_priv region */
	priv = netdev_priv(net_dev);
	scnprintf(md_name, sizeof(md_name), "%s-priv", interface_name);
	ret = ethdbg_minidump_add_region(md_name, (uintptr_t)priv, sizeof(*priv));
	if (!ret)
		pr_info("ethdbg: [%s] registered minidump region '%s'\n", interface_name, md_name);

	/* Register platform_device region if the parent is a platform device */
	parent_dev = net_dev->dev.parent;
	if (parent_dev && dev_is_platform(parent_dev)) {
		struct platform_device *pdev = to_platform_device(parent_dev);

		scnprintf(md_name, sizeof(md_name), "%s-dev", interface_name);
		ret = ethdbg_minidump_add_region(md_name, (uintptr_t)pdev, sizeof(*pdev));
		if (!ret)
			pr_info("ethdbg: [%s] registered minidump region '%s'\n",
				interface_name, md_name);
	}
}

void ethdbg_stmmac_init(void)
{
	ethdbg_register_capture_ops(&ethdbg_stmmac_capture_ops);
}

void ethdbg_stmmac_exit(void)
{
	ethdbg_unregister_capture_ops(&ethdbg_stmmac_capture_ops);
}

void ethdbg_stmmac_unregister(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev = iface->device;
	const char *interface_name = iface->interface_name;
	struct net_device *net_dev;
	struct stmmac_priv *priv;
	struct device *parent_dev;
	char md_name[ETHDBG_MD_NAME_LEN];
	int ret;

	if (!dev || !dev->net_dev)
		return;

	net_dev = dev->net_dev;

	/* Unregister platform_device region */
	parent_dev = net_dev->dev.parent;
	if (parent_dev && dev_is_platform(parent_dev)) {
		struct platform_device *pdev = to_platform_device(parent_dev);

		scnprintf(md_name, sizeof(md_name), "%s-dev", interface_name);
		ret = ethdbg_minidump_remove_region(md_name, (uintptr_t)pdev, sizeof(*pdev));
		if (!ret)
			pr_info("ethdbg: [%s] unregistered minidump region '%s'\n",
				interface_name, md_name);
	}

	/* Unregister stmmac_priv region */
	priv = netdev_priv(net_dev);
	scnprintf(md_name, sizeof(md_name), "%s-priv", interface_name);
	ret = ethdbg_minidump_remove_region(md_name, (uintptr_t)priv, sizeof(*priv));
	if (!ret)
		pr_info("ethdbg: [%s] unregistered minidump region '%s'\n",
			interface_name, md_name);

	/* Unregister net_device region */
	scnprintf(md_name, sizeof(md_name), "%s-ndev", interface_name);
	ret = ethdbg_minidump_remove_region(md_name, (uintptr_t)net_dev, sizeof(*net_dev));
	if (!ret)
		pr_info("ethdbg: [%s] unregistered minidump region '%s'\n",
			interface_name, md_name);
}

/* Timing parameters for indirect register polls */
#define ETHDBG_VLAN_POLL_INTERVAL_US	1000U
#define ETHDBG_VLAN_POLL_TIMEOUT_US	500000U
#define ETHDBG_L3L4_POLL_INTERVAL_US	100U
#define ETHDBG_L3L4_POLL_TIMEOUT_US	10000U

/* Sentinel written into reg_list when an indirect read times out */
#define ETHDBG_INDIRECT_TIMEOUT_VAL	0xDEADBEEFU

/*
 * ethdbg_read_vlan_indirect - read one VLAN filter entry.
 *
 * Sequence (mirrors stmmac_vlan.c::vlan_write_filter with CT=1 for read):
 *   1. Clear OFS, CT, and OB in VLAN_TAG, then write (index<<OFS)|OB|CT.
 *   2. Poll until OB clears (hardware done), bounded by timeout.
 *   3. Read result from VLAN_TAG_DATA.
 *
 * Returns the filter data, or ETHDBG_INDIRECT_TIMEOUT_VAL on timeout.
 */
u32 ethdbg_read_vlan_indirect(void __iomem *base, u32 index)
{
	u32 val;
	int ret;

	val  = readl(base + VLAN_TAG);
	val &= ~(VLAN_TAG_CTRL_OFS_MASK | VLAN_TAG_CTRL_CT | VLAN_TAG_CTRL_OB);
	val |= (index << VLAN_TAG_CTRL_OFS_SHIFT) |
	       VLAN_TAG_CTRL_OB | VLAN_TAG_CTRL_CT;
	writel(val, base + VLAN_TAG);

	ret = readl_poll_timeout(base + VLAN_TAG, val,
				 !(val & VLAN_TAG_CTRL_OB),
				 ETHDBG_VLAN_POLL_INTERVAL_US,
				 ETHDBG_VLAN_POLL_TIMEOUT_US);
	if (ret) {
		pr_warn_ratelimited("ethdbg: VLAN filter %u read timeout\n", index);
		return ETHDBG_INDIRECT_TIMEOUT_VAL;
	}

	return readl(base + VLAN_TAG_DATA);
}

/*
 * ethdbg_read_l3l4_indirect - read one L3/L4 filter register.
 *
 * Sequence (mirrors dwxgmac2_core.c::dwxgmac2_filter_read):
 *   1. Poll until XB clears (no previous operation in flight).
 *   2. Write (filter_no, reg_id, TT=read, XB=start) to L3L4_ADDR_CTRL.
 *   3. Poll until XB clears again (read complete).
 *   4. Read result from L3L4_DATA.
 *
 * Returns the register data, or ETHDBG_INDIRECT_TIMEOUT_VAL on timeout.
 */
u32 ethdbg_read_l3l4_indirect(void __iomem *base, u32 filter_no, u32 reg_id)
{
	u32 val;
	int ret;

	ret = readl_poll_timeout(base + XGMAC_L3L4_ADDR_CTRL, val,
				 !(val & XGMAC_XB),
				 ETHDBG_L3L4_POLL_INTERVAL_US,
				 ETHDBG_L3L4_POLL_TIMEOUT_US);
	if (ret) {
		pr_warn_ratelimited("ethdbg: L3L4 filter %u reg %u pre-poll timeout\n",
				    filter_no, reg_id);
		return ETHDBG_INDIRECT_TIMEOUT_VAL;
	}

	val  = ((filter_no << XGMAC_IDDR_FNUM) | reg_id) << XGMAC_IDDR_SHIFT;
	val |= XGMAC_TT | XGMAC_XB;
	writel(val, base + XGMAC_L3L4_ADDR_CTRL);

	ret = readl_poll_timeout(base + XGMAC_L3L4_ADDR_CTRL, val,
				 !(val & XGMAC_XB),
				 ETHDBG_L3L4_POLL_INTERVAL_US,
				 ETHDBG_L3L4_POLL_TIMEOUT_US);
	if (ret) {
		pr_warn_ratelimited("ethdbg: L3L4 filter %u reg %u post-poll timeout\n",
				    filter_no, reg_id);
		return ETHDBG_INDIRECT_TIMEOUT_VAL;
	}

	return readl(base + XGMAC_L3L4_DATA);
}
