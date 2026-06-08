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
 * qcom_ethqos is intentionally not registered here because its structure
 * definition is private to dwmac-qcom-ethqos.c.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <soc/qcom/minidump.h>

#include "stmmac.h"
#include "ethdbg.h"

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
