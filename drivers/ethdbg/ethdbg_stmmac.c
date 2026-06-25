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

#ifdef CONFIG_QCOM_MINIDUMP

#include <soc/qcom/minidump.h>

#define ETHDBG_VA_MD_NAME	"eth_mini"

/* Defined in ethdbg_ioss.c */
void ethdbg_ipa_capture_rx_queue(struct stmmac_priv *priv,
				  const char *ifname, unsigned int q);
void ethdbg_ipa_capture_tx_queue(struct stmmac_priv *priv,
				  const char *ifname, unsigned int q);

void ethdbg_stmmac_minidump_register(struct ethdbg_device *dev,
				     const char *interface_name)
{
	struct net_device *net_dev;
	struct stmmac_priv *priv;
	struct device *parent_dev;
	char md_name[MAX_NAME_LENGTH + 1];
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

void ethdbg_stmmac_minidump_unregister(struct ethdbg_device *dev,
				       const char *interface_name)
{
	struct net_device *net_dev;
	struct stmmac_priv *priv;
	struct device *parent_dev;
	char md_name[MAX_NAME_LENGTH + 1];
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

/*
 * ethdbg_sw_capture_rx_queue - Capture one SW-managed RX descriptor queue
 * @priv:  stmmac private data
 * @iface: ethdbg interface, used for region naming
 * @q:     RX queue index
 *
 * Registers the SW-path RX descriptor ring for queue @q with VA-minidump.
 * Region is named "<ifname>:rx<q>des". Skipped silently if the descriptor
 * base address is NULL.
 */
static void ethdbg_sw_capture_rx_queue(struct stmmac_priv *priv,
				       struct ethdbg_interface *iface,
				       unsigned int q)
{
	struct va_md_entry entry;
	int ret;

	if (priv->extend_desc) {
		entry.vaddr = (unsigned long)priv->dma_conf.rx_queue[q].dma_erx;
		entry.size  = priv->dma_conf.dma_rx_size *
			      sizeof(struct dma_extended_desc);
	} else {
		entry.vaddr = (unsigned long)priv->dma_conf.rx_queue[q].dma_rx;
		entry.size  = priv->dma_conf.dma_rx_size * sizeof(struct dma_desc);
	}

	if (!entry.vaddr || !entry.size)
		return;

	scnprintf(entry.owner, sizeof(entry.owner), "%s-rx%u",
			  iface->interface_name, q);

	ret = qcom_va_md_add_region(&entry);
	if (ret)
		pr_err("ethdbg: failed to add VA-MD region %s ret %d\n",
		       entry.owner, ret);
}

/*
 * ethdbg_sw_capture_tx_queue - Capture one SW-managed TX descriptor queue
 * @priv:  stmmac private data
 * @iface: ethdbg interface, used for region naming
 * @q:     TX queue index
 *
 * Registers the SW-path TX descriptor ring for queue @q with VA-minidump.
 * Region is named "<ifname>:tx<q>des". Skipped silently if the descriptor
 * base address is NULL.
 */
static void ethdbg_sw_capture_tx_queue(struct stmmac_priv *priv,
				       struct ethdbg_interface *iface,
				       unsigned int q)
{
	struct stmmac_tx_queue *tx_q = &priv->dma_conf.tx_queue[q];
	struct va_md_entry entry;
	int ret;

	if (priv->extend_desc) {
		entry.vaddr = (unsigned long)tx_q->dma_etx;
		entry.size  = priv->dma_conf.dma_tx_size *
			      sizeof(struct dma_extended_desc);
	} else if (tx_q->tbs & STMMAC_TBS_AVAIL) {
		entry.vaddr = (unsigned long)tx_q->dma_entx;
		entry.size  = priv->dma_conf.dma_tx_size * sizeof(struct dma_edesc);
	} else {
		entry.vaddr = (unsigned long)tx_q->dma_tx;
		entry.size  = priv->dma_conf.dma_tx_size * sizeof(struct dma_desc);
	}

	if (!entry.vaddr || !entry.size)
		return;

	scnprintf(entry.owner, sizeof(entry.owner), "%s-tx%u",
			  iface->interface_name, q);

	ret = qcom_va_md_add_region(&entry);
	if (ret)
		pr_err("ethdbg: failed to add VA-MD region %s ret %d\n",
		       entry.owner, ret);
}

static void ethdbg_capture_rx_rings(struct stmmac_priv *priv,
				     struct ethdbg_interface *iface)
{
	unsigned int q;

	for (q = 0; q < priv->plat->rx_queues_to_use; q++) {
		if (priv->plat->rx_queues_cfg[q].api_managed)
			ethdbg_ipa_capture_rx_queue(priv, iface->interface_name, q);
		else
			ethdbg_sw_capture_rx_queue(priv, iface, q);
	}
}

static void ethdbg_capture_tx_rings(struct stmmac_priv *priv,
				     struct ethdbg_interface *iface)
{
	unsigned int q;

	for (q = 0; q < priv->plat->tx_queues_to_use; q++) {
		if (priv->plat->tx_queues_cfg[q].api_managed)
			ethdbg_ipa_capture_tx_queue(priv, iface->interface_name, q);
		else
			ethdbg_sw_capture_tx_queue(priv, iface, q);
	}
}

/*
 * VA-minidump panic notifier for all DMA descriptor rings.
 *
 * Called by the VA-minidump framework at panic time. Walks all active ethdbg
 * interfaces and captures every RX and TX descriptor ring, covering both
 * SW-managed (!api_managed) and IPA/IOSS-managed (api_managed) queues.
 */
static int ethdbg_va_md_notify(struct notifier_block *nb, unsigned long event,
			       void *ptr)
{
	struct ethdbg_interface *iface;

	list_for_each_entry(iface, &ethdbg_interfaces, list) {
		struct ethdbg_device *dev = iface->device;
		struct stmmac_priv *priv;

		if (!dev || !dev->net_dev)
			continue;

		priv = netdev_priv(dev->net_dev);
		if (!priv)
			continue;

		ethdbg_capture_rx_rings(priv, iface);
		ethdbg_capture_tx_rings(priv, iface);
	}

	return NOTIFY_DONE;
}

static struct notifier_block ethdbg_va_md_nb = {
	.notifier_call = ethdbg_va_md_notify,
	.priority = INT_MAX,
};

/**
 * ethdbg_stmmac_va_md_register - Register VA-minidump notifier for DMA rings
 * @dev: ethdbg device (unused, notifier covers all active interfaces)
 * @interface_name: network interface name (used only for log messages)
 *
 * Registers the ethdbg VA-minidump panic notifier on the first NETDEV_UP.
 * Subsequent calls from additional interfaces return -EEXIST from
 * qcom_va_md_register(), which is silently ignored — the single shared
 * notifier already covers all interfaces in ethdbg_interfaces.
 */
void ethdbg_stmmac_va_md_register(struct ethdbg_device *dev,
				   const char *interface_name)
{
	int ret;

	if (!qcom_va_md_enabled())
		return;

	ret = qcom_va_md_register(ETHDBG_VA_MD_NAME, &ethdbg_va_md_nb);
	if (ret && ret != -EEXIST)
		pr_err("ethdbg: [%s] failed to register VA-minidump notifier, err: %d\n",
		       interface_name, ret);
}

/**
 * ethdbg_stmmac_va_md_unregister - Unregister VA-minidump notifier for DMA rings
 * @dev: ethdbg device (unused)
 * @interface_name: network interface name (used only for log messages)
 *
 * Unregisters the ethdbg VA-minidump panic notifier on the last NETDEV_DOWN.
 * If other interfaces are still active the notifier is left registered so
 * their descriptor rings remain covered.
 */
void ethdbg_stmmac_va_md_unregister(struct ethdbg_device *dev,
				      const char *interface_name)
{
	struct ethdbg_interface *iface;
	bool last_iface = true;
	int ret;

	if (!qcom_va_md_enabled())
		return;

	/* Check whether any other interface still has an active device */
	list_for_each_entry(iface, &ethdbg_interfaces, list) {
		if (iface->device && iface->device != dev &&
		    strcmp(iface->interface_name, interface_name) != 0) {
			last_iface = false;
			break;
		}
	}

	if (!last_iface)
		return;

	ret = qcom_va_md_unregister(ETHDBG_VA_MD_NAME, &ethdbg_va_md_nb);
	if (ret)
		pr_err("ethdbg: [%s] failed to unregister VA-minidump notifier, err: %d\n",
		       interface_name, ret);
}

#endif /* CONFIG_QCOM_MINIDUMP */

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
