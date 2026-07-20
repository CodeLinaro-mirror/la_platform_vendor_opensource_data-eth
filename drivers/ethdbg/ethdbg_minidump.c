// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/kernel.h>
#include <linux/string.h>
#include <soc/qcom/minidump.h>
#include "ethdbg.h"

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

void ethdbg_minidump_register(struct ethdbg_device *dev,
			      const char *interface_name)
{
	struct ethdbg_dump_data *dump_data = &dev->dump_data;
	char md_name[MAX_NAME_LENGTH + 1];
	unsigned int phy_idx = 0;
	unsigned int i;

	for (i = 0; i < dump_data->num_blocks; i++) {
		struct ethdbg_hw_block *block = &dump_data->blocks[i];
		int ret;

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
				continue;
			}
		}

		block->minidump_registered = true;
	}
}

void ethdbg_minidump_unregister(struct ethdbg_device *dev,
				const char *interface_name)
{
	struct ethdbg_dump_data *dump_data = &dev->dump_data;
	char md_name[MAX_NAME_LENGTH + 1];
	unsigned int phy_idx = 0;
	unsigned int i;

	for (i = 0; i < dump_data->num_blocks; i++) {
		struct ethdbg_hw_block *block = &dump_data->blocks[i];
		int ret;

		if (!block->minidump_registered)
			continue;

		if (block->is_phy) {
			scnprintf(md_name, sizeof(md_name), "%s-phyreg%u", interface_name, phy_idx);
			ret = ethdbg_minidump_remove_region(md_name, (uintptr_t)block->reg_list,
							    block->num_registers * sizeof(struct ethdbg_reg_entry));
			if (ret < 0)
				continue;

			phy_idx++;
		} else {
			/* Remove the map descriptor first, then the register data */
			scnprintf(md_name, sizeof(md_name), "%s-map%u", interface_name, block->map->index);
			ethdbg_minidump_remove_region(md_name, (uintptr_t)block->map,
						      sizeof(struct ethdbg_map));

			scnprintf(md_name, sizeof(md_name), "%s-reg%u", interface_name, block->map->index);
			ret = ethdbg_minidump_remove_region(md_name, (uintptr_t)block->reg_list,
							    block->num_registers * sizeof(struct ethdbg_reg_entry));
			if (ret < 0)
				continue;
		}

		block->minidump_registered = false;
	}
}
