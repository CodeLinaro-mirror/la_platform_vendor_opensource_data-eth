// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <soc/qcom/minidump.h>
#include <linux/msm/ioss.h>

#include "stmmac.h"
#include "ethdbg.h"

/*
 * stmmac_ioss_get_ring_size - Look up the IOSS channel ring size
 * @priv:         stmmac private data
 * @queue:        DMA queue / channel index
 * @dir:          IOSS_CH_DIR_RX or IOSS_CH_DIR_TX
 * @default_size: fallback value when no IOSS channel is found
 *
 * Walks the valid_channels list of the IOSS interface associated with
 * @priv->device. Returns the configured ring_size of the first allocated
 * channel that matches @queue and @dir, or @default_size if none is found.
 */
static u32 stmmac_ioss_get_ring_size(struct stmmac_priv *priv, int queue,
				     enum ioss_channel_dir dir,
				     u32 default_size)
{
	struct ioss_device *idev;
	struct ioss_interface *iface;
	struct ioss_channel *ch;

	idev = ioss_real_to_idev(priv->device);
	if (!idev)
		return default_size;

	iface = &idev->interface;

	ioss_for_each_channel(ch, iface) {
		if (ch->direction == dir && ch->id == queue && ch->allocated)
			return ch->config.ring_size;
	}

	return default_size;
}

/*
 * ethdbg_ipa_capture_rx_queue - Capture one IPA/IOSS-managed RX descriptor queue
 * @priv:   stmmac private data
 * @ifname: network interface name, used for region naming
 * @q:      RX queue index
 *
 * Looks up the IOSS ring size for queue @q, resolves the descriptor base
 * address, and registers the region with VA-minidump. Region is named
 * "<ifname>-rx<q>des". Skipped silently if the descriptor base address
 * is NULL.
 */
void ethdbg_ipa_capture_rx_queue(struct stmmac_priv *priv,
				  const char *ifname, unsigned int q)
{
	u32 ring_size = stmmac_ioss_get_ring_size(priv, q, IOSS_CH_DIR_RX,
					      priv->dma_conf.dma_rx_size);
	struct va_md_entry entry;
	int ret;

	if (priv->extend_desc) {
		entry.vaddr = (unsigned long)priv->dma_conf.rx_queue[q].dma_erx;
		entry.size  = ring_size * sizeof(struct dma_extended_desc);
	} else {
		entry.vaddr = (unsigned long)priv->dma_conf.rx_queue[q].dma_rx;
		entry.size  = ring_size * sizeof(struct dma_desc);
	}

	if (!entry.vaddr || !entry.size)
		return;

	scnprintf(entry.owner, sizeof(entry.owner), "%s-rx%u", ifname, q);

	ret = qcom_va_md_add_region(&entry);
	if (ret)
		pr_err("ethdbg: failed to add VA-MD region %s ret %d\n",
		       entry.owner, ret);
}

/*
 * ethdbg_ipa_capture_tx_queue - Capture one IPA/IOSS-managed TX descriptor queue
 * @priv:   stmmac private data
 * @ifname: network interface name, used for region naming
 * @q:      TX queue index
 *
 * Looks up the IOSS ring size for queue @q, resolves the descriptor base
 * address, and registers the region with VA-minidump. Region is named
 * "<ifname>:tx<q>des". Skipped silently if the descriptor base address
 * is NULL.
 */
void ethdbg_ipa_capture_tx_queue(struct stmmac_priv *priv,
				  const char *ifname, unsigned int q)
{
	struct stmmac_tx_queue *tx_q = &priv->dma_conf.tx_queue[q];
	u32 ring_size = stmmac_ioss_get_ring_size(priv, q, IOSS_CH_DIR_TX,
					      priv->dma_conf.dma_tx_size);
	struct va_md_entry entry;
	int ret;

	if (priv->extend_desc) {
		entry.vaddr = (unsigned long)tx_q->dma_etx;
		entry.size  = ring_size * sizeof(struct dma_extended_desc);
	} else if (tx_q->tbs & STMMAC_TBS_AVAIL) {
		entry.vaddr = (unsigned long)tx_q->dma_entx;
		entry.size  = ring_size * sizeof(struct dma_edesc);
	} else {
		entry.vaddr = (unsigned long)tx_q->dma_tx;
		entry.size  = ring_size * sizeof(struct dma_desc);
	}

	if (!entry.vaddr || !entry.size)
		return;

	scnprintf(entry.owner, sizeof(entry.owner), "%s-tx%u", ifname, q);

	ret = qcom_va_md_add_region(&entry);
	if (ret)
		pr_err("ethdbg: failed to add VA-MD region %s ret %d\n",
		       entry.owner, ret);
}
