// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/kernel.h>
#include <linux/panic_notifier.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include "ethdbg.h"
#include "ethdbg_regs.h"

#define REG_SIZE 4

static int ethdbg_panic_notifier(struct notifier_block *nb,
				 unsigned long event, void *ptr);

static struct notifier_block ethdbg_panic_nb = {
	.notifier_call = ethdbg_panic_notifier,
	.priority = INT_MAX,
};

static const struct ethdbg_hw_desc *ethdbg_find_hw_desc(const char *name)
{
	int i;

	for (i = 0; echo_hw_descs[i].name != NULL; i++) {
		if (strcmp(echo_hw_descs[i].name, name) == 0)
			return &echo_hw_descs[i];
	}
	return NULL;
}

static void ethdbg_capture_regs(struct ethdbg_hw_block *block,
				const struct ethdbg_hw_desc *hw_desc,
				const char *interface_name)
{
	size_t reg_desc_idx;

	block->num_captured = 0;
	block->num_skipped = 0;

	for (reg_desc_idx = 0; reg_desc_idx < hw_desc->num_reg_desc; reg_desc_idx++) {
		const struct ethdbg_reg_desc *reg_desc = &hw_desc->reg_desc[reg_desc_idx];

		for (u32 reg_offset = reg_desc->start_offset;
		     reg_offset <= reg_desc->end_offset; reg_offset += REG_SIZE) {
			if (block->num_captured == block->num_registers)
				break;

			if (reg_offset + REG_SIZE > block->map->size) {
				block->num_skipped++;
				continue;
			}

			struct ethdbg_reg_entry *reg_entry = &block->reg_list[block->num_captured];
			reg_entry->offset = reg_offset;
			reg_entry->value = readl(block->base + reg_offset);
			block->num_captured++;
		}
	}
}

static void ethdbg_capture_hw_block(struct ethdbg_hw_block *block,
				    const char *interface_name)
{
	const struct ethdbg_hw_desc *hw_desc;

	hw_desc = ethdbg_find_hw_desc(block->map->name);
	ethdbg_capture_regs(block, hw_desc, interface_name);
}

void ethdbg_dump_device(struct ethdbg_device *dev)
{
	struct ethdbg_dump_data *dump_data;
	int regs_captured = 0, regs_skipped = 0;

	if (!dev)
		return;

	dump_data = &dev->dump_data;

	for (int i = 0; i < dump_data->num_blocks; i++) {
		ethdbg_capture_hw_block(&dump_data->blocks[i], dev->net_dev->name);
		regs_captured += dump_data->blocks[i].num_captured;
		regs_skipped += dump_data->blocks[i].num_skipped;
	}

	pr_emerg("ETHDBG: [%s] ethdbg_device=0x%px blocks=%u captured regs=%u skipped "
		 "regs=%u\n", dev->net_dev->name, dev, dump_data->num_blocks,
		 regs_captured, regs_skipped);
}

static int ethdbg_panic_notifier(struct notifier_block *nb,
				 unsigned long event, void *ptr)
{
	struct ethdbg_interface *iface;

	list_for_each_entry(iface, &ethdbg_interfaces, list) {
		ethdbg_dump_device(iface->device);
	}

	return NOTIFY_DONE;
}

static int ethdbg_count_hw_blocks(struct ethdbg_device *dev)
{
	struct ethdbg_map *map;
	int count = 0;

	list_for_each_entry(map, &dev->map_regions, list) {
		const struct ethdbg_hw_desc *hw_desc = ethdbg_find_hw_desc(map->name);
		if (hw_desc && hw_desc->reg_desc && hw_desc->num_reg_desc > 0)
			count++;
	}
	return count;
}

static int ethdbg_count_reg(const struct ethdbg_hw_desc *hw_desc)
{
	int total_regs = 0;
	size_t reg_desc_idx;

	for (reg_desc_idx = 0; reg_desc_idx < hw_desc->num_reg_desc; reg_desc_idx++) {
		const struct ethdbg_reg_desc *reg_desc = &hw_desc->reg_desc[reg_desc_idx];

		if (reg_desc->start_offset % REG_SIZE != 0 ||
		    (reg_desc->end_offset + REG_SIZE) % REG_SIZE != 0) {
			pr_warn("Register range [0x%x-0x%x] not aligned to %d bytes\n",
				reg_desc->start_offset, reg_desc->end_offset, REG_SIZE);
		}

		u32 region_size = reg_desc->end_offset - reg_desc->start_offset + REG_SIZE;
		total_regs += (region_size / REG_SIZE);
	}
	return total_regs;
}

static int ethdbg_init_hw_block(struct ethdbg_hw_block *hw_block,
			       struct ethdbg_map *map,
			       const struct ethdbg_hw_desc *hw_desc,
			       const char *interface_name)
{
	int num_registers;
	const struct ethdbg_reg_desc *reg_desc;
	size_t reg_desc_idx;

	num_registers = ethdbg_count_reg(hw_desc);
	if (num_registers == 0) {
		pr_warn("[%s][%s] No registers to capture in this block\n",
			interface_name, map->name);
		return 0;
	}

	for (reg_desc_idx = 0; reg_desc_idx < hw_desc->num_reg_desc; reg_desc_idx++) {
		reg_desc = &hw_desc->reg_desc[reg_desc_idx];

		if (reg_desc->start_offset >= map->size || reg_desc->end_offset >= map->size)
			pr_warn("[%s][%s] Register range [0x%x-0x%x] outside mapped region "
				"(size: 0x%llx)\n", interface_name, map->name,
				reg_desc->start_offset, reg_desc->end_offset, map->size);
	}

	hw_block->reg_list = kzalloc(PAGE_ALIGN(num_registers * sizeof(struct ethdbg_reg_entry)),
								 GFP_KERNEL);
	if (!hw_block->reg_list) {
		pr_err("[%s][%s] Failed to allocate register list\n", interface_name, map->name);
		return -ENOMEM;
	}

	hw_block->base = ioremap(map->base_addr, map->size);
	if (!hw_block->base) {
		pr_err("[%s][%s] Failed to ioremap\n", interface_name, map->name);
		goto err_free_reg_list;
	}

	hw_block->num_registers = num_registers;
	hw_block->num_captured = 0;
	hw_block->num_skipped = 0;
	hw_block->map = map;

	return 0;

err_free_reg_list:
	kfree(hw_block->reg_list);
	hw_block->reg_list = NULL;
	return -ENOMEM;
}

static int ethdbg_setup_dump_blocks(struct ethdbg_device *dev,
				      const char *interface_name)
{
	struct ethdbg_dump_data *dump_data = &dev->dump_data;
	struct ethdbg_map *map;
	const struct ethdbg_hw_desc *hw_desc;
	int hw_block_idx = 0, ret;

	list_for_each_entry(map, &dev->map_regions, list) {
		hw_desc = ethdbg_find_hw_desc(map->name);
		if (!hw_desc || !hw_desc->reg_desc || hw_desc->num_reg_desc == 0)
			continue;

		ret = ethdbg_init_hw_block(&dump_data->blocks[hw_block_idx], map,
					   hw_desc, interface_name);
		if (ret == 0)
			hw_block_idx++;
	}

	return hw_block_idx;
}

int ethdbg_dump_register(struct ethdbg_device *dev, const char *interface_name)
{
	struct ethdbg_dump_data *dump_data;
	int block_count;

	memset(&dev->dump_data, 0, sizeof(dev->dump_data));
	dump_data = &dev->dump_data;

	block_count = ethdbg_count_hw_blocks(dev);
	if (block_count == 0)
		return 0;

	dump_data->blocks = kzalloc(block_count * sizeof(struct ethdbg_hw_block),
				     GFP_KERNEL);
	if (!dump_data->blocks)
		return -ENOMEM;

	dump_data->num_blocks = ethdbg_setup_dump_blocks(dev, interface_name);
	return 0;
}

void ethdbg_dump_unregister(struct ethdbg_device *dev)
{
	struct ethdbg_dump_data *dump_data;
	int i;

	dump_data = &dev->dump_data;
	if (!dump_data->blocks)
		return;

	for (i = 0; i < dump_data->num_blocks; i++) {
		iounmap(dump_data->blocks[i].base);
		kfree(dump_data->blocks[i].reg_list);
	}

	kfree(dump_data->blocks);
	dump_data->blocks = NULL;
	dump_data->num_blocks = 0;
}

int ethdbg_panic_init(void)
{
	int ret;

	ret = atomic_notifier_chain_register(&panic_notifier_list, &ethdbg_panic_nb);
	if (ret) {
		pr_err("Failed to register panic notifier: %d\n", ret);
		return ret;
	}

	return 0;
}

void ethdbg_panic_deinit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &ethdbg_panic_nb);
}
