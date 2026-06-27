// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/string.h>
#include <soc/qcom/minidump.h>
#include "ethdbg.h"

/* Defined in ethdbg_stmmac.c */
void ethdbg_stmmac_register(struct ethdbg_interface *iface);
void ethdbg_stmmac_unregister(struct ethdbg_interface *iface);

#define ETHDBG_MAX_CAPTURE_OPS	4

static const struct ethdbg_capture_ops *capture_ops[ETHDBG_MAX_CAPTURE_OPS];
static unsigned int num_capture_ops;

void ethdbg_register_capture_ops(const struct ethdbg_capture_ops *ops)
{
	if (WARN_ON(num_capture_ops >= ETHDBG_MAX_CAPTURE_OPS))
		return;
	capture_ops[num_capture_ops++] = ops;
}

void ethdbg_unregister_capture_ops(const struct ethdbg_capture_ops *ops)
{
	unsigned int i;

	for (i = 0; i < num_capture_ops; i++) {
		if (capture_ops[i] == ops) {
			capture_ops[i] = capture_ops[--num_capture_ops];
			capture_ops[num_capture_ops] = NULL;
			return;
		}
	}
}

int ethdbg_minidump_add_region(const char *name, uintptr_t vaddr, size_t size)
{
	struct md_region md_entry;
	int ret;

	if (!msm_minidump_enabled())
		return -ENODEV;

	scnprintf(md_entry.name, sizeof(md_entry.name), "%s", name);
	md_entry.virt_addr = vaddr;
	md_entry.phys_addr = virt_to_phys((void *)vaddr);
	md_entry.size = size;

	ret = msm_minidump_add_region(&md_entry);
	if (ret < 0)
		pr_err("ethdbg: failed to register minidump region %s ret %d\n",
		       name, ret);
	return ret;
}

int ethdbg_minidump_remove_region(const char *name, uintptr_t vaddr, size_t size)
{
	struct md_region md_entry;
	int ret;

	if (!msm_minidump_enabled())
		return -ENODEV;

	scnprintf(md_entry.name, sizeof(md_entry.name), "%s", name);
	md_entry.virt_addr = vaddr;
	md_entry.phys_addr = virt_to_phys((void *)vaddr);
	md_entry.size = size;

	ret = msm_minidump_remove_region(&md_entry);
	if (ret < 0)
		pr_err("ethdbg: failed to remove minidump region %s ret %d\n",
		       name, ret);
	return ret;
}

int ethdbg_va_md_add_region(const char *owner, unsigned long vaddr, unsigned int size)
{
	struct va_md_entry entry;

	entry.vaddr = vaddr;
	entry.size = size;
	entry.cb = NULL;
	scnprintf(entry.owner, sizeof(entry.owner), "%s", owner);

	return qcom_va_md_add_region(&entry);
}

static int ethdbg_va_md_notify(struct notifier_block *nb, unsigned long event,
			       void *ptr)
{
	struct ethdbg_interface *iface;
	unsigned int i;

	list_for_each_entry(iface, &ethdbg_interfaces, list) {
		struct ethdbg_device *dev = iface->device;
		struct net_device *ndev;
		unsigned int q;

		if (!dev || !dev->net_dev)
			continue;

		ndev = dev->net_dev;

		for (q = 0; q < ndev->num_rx_queues; q++) {
			for (i = 0; i < num_capture_ops; i++) {
				if (capture_ops[i]->capture_rx_queue)
					capture_ops[i]->capture_rx_queue(ndev, iface->interface_name, q);
			}
		}

		for (q = 0; q < ndev->num_tx_queues; q++) {
			for (i = 0; i < num_capture_ops; i++) {
				if (capture_ops[i]->capture_tx_queue)
					capture_ops[i]->capture_tx_queue(ndev, iface->interface_name, q);
			}
		}
	}

	return NOTIFY_DONE;
}

static struct notifier_block ethdbg_va_md_nb = {
	.notifier_call = ethdbg_va_md_notify,
	.priority = INT_MAX,
};

void ethdbg_va_minidump_init(void)
{
	int ret;

	if (!qcom_va_md_enabled())
		return;

	ret = qcom_va_md_register("ethdbg", &ethdbg_va_md_nb);
	if (ret)
		pr_err("ethdbg: failed to register VA-minidump notifier, err: %d\n", ret);
}

void ethdbg_va_minidump_deinit(void)
{
	int ret;

	if (!qcom_va_md_enabled())
		return;

	ret = qcom_va_md_unregister("ethdbg", &ethdbg_va_md_nb);
	if (ret)
		pr_err("ethdbg: failed to unregister VA-minidump notifier, err: %d\n", ret);
}

int ethdbg_minidump_register(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev = iface->device;
	const char *interface_name = iface->interface_name;
	struct ethdbg_dump_data *dump_data = &dev->dump_data;
	char md_name[ETHDBG_MD_NAME_LEN];
	unsigned int phy_idx = 0;
	unsigned int i;
	int ret = 0;
	int err = 0;

	ethdbg_stmmac_register(iface);

	for (i = 0; i < dump_data->num_blocks; i++) {
		struct ethdbg_hw_block *block = &dump_data->blocks[i];

		if (block->minidump_registered)
			continue;

		if (block->is_phy) {
			/* PHY block: eth0-phyreg<phy_idx> */
			scnprintf(md_name, sizeof(md_name), "%s-phyreg%u", interface_name, phy_idx);
			ret = ethdbg_minidump_add_region(md_name, (uintptr_t)block->reg_list,
							 block->num_registers * sizeof(struct ethdbg_reg_entry));
			if (ret < 0) {
				pr_warn("ethdbg: [%s] skipping minidump registration for %s\n",
					interface_name, md_name);
				err = err ?: ret;
				continue;
			}

			phy_idx++;
		} else {
			/* MMIO block: eth0-reg<map->index> */
			scnprintf(md_name, sizeof(md_name), "%s-reg%u", interface_name, block->map->index);
			ret = ethdbg_minidump_add_region(md_name, (uintptr_t)block->reg_list,
							 block->num_registers * sizeof(struct ethdbg_reg_entry));
			if (ret < 0) {
				pr_warn("ethdbg: [%s] skipping minidump registration for %s\n",
					interface_name, md_name);
				err = err ?: ret;
				continue;
			}

			/*
			 * Register the ethdbg_map struct as eth0-map<index> to enable resolving
			 * the index to the region name during postprocessing.
			 */
			scnprintf(md_name, sizeof(md_name), "%s-map%u", interface_name, block->map->index);
			ret = ethdbg_minidump_add_region(md_name, (uintptr_t)block->map, sizeof(struct ethdbg_map));
			if (ret < 0) {
				pr_warn("ethdbg: [%s] skipping minidump registration for %s\n",
					interface_name, md_name);
				err = err ?: ret;
				continue;
			}
		}

		block->minidump_registered = true;
	}

	return err;
}

int ethdbg_minidump_unregister(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev = iface->device;
	const char *interface_name = iface->interface_name;
	struct ethdbg_dump_data *dump_data = &dev->dump_data;
	char md_name[ETHDBG_MD_NAME_LEN];
	unsigned int phy_idx = 0;
	unsigned int i;
	int ret = 0;
	int err = 0;

	ethdbg_stmmac_unregister(iface);

	for (i = 0; i < dump_data->num_blocks; i++) {
		struct ethdbg_hw_block *block = &dump_data->blocks[i];

		if (!block->minidump_registered)
			continue;

		if (block->is_phy) {
			scnprintf(md_name, sizeof(md_name), "%s-phyreg%u", interface_name, phy_idx);
			ret = ethdbg_minidump_remove_region(md_name, (uintptr_t)block->reg_list,
							    block->num_registers * sizeof(struct ethdbg_reg_entry));
			if (ret < 0) {
				err = err ?: ret;
				continue;
			}

			phy_idx++;
		} else {
			/* Remove the map descriptor first, then the register data */
			scnprintf(md_name, sizeof(md_name), "%s-map%u", interface_name, block->map->index);
			ret = ethdbg_minidump_remove_region(md_name, (uintptr_t)block->map,
						      sizeof(struct ethdbg_map));
			if (ret < 0) {
				err = err ?: ret;
				continue;
			}

			scnprintf(md_name, sizeof(md_name), "%s-reg%u", interface_name, block->map->index);
			ret = ethdbg_minidump_remove_region(md_name, (uintptr_t)block->reg_list,
							    block->num_registers * sizeof(struct ethdbg_reg_entry));
			if (ret < 0) {
				err = err ?: ret;
				continue;
			}
		}

		block->minidump_registered = false;
	}

	return err;
}
