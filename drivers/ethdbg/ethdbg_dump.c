// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/kernel.h>
#include <linux/panic_notifier.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include "ethdbg.h"
#include "ethdbg_regs.h"
#define REG_SIZE 4

/* Module param to enable capture_phy */
static bool capture_phy_on_panic;
module_param(capture_phy_on_panic, bool, 0644);
MODULE_PARM_DESC(capture_phy_on_panic,
		 "Capture PHY registers during kernel panic (default: off)");

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
	if (!hw_desc)
		return;
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
		/* Skip dump capture for phy regions */
		if (dump_data->blocks[i].is_phy)
			continue;
		ethdbg_capture_hw_block(&dump_data->blocks[i], dev->net_dev->name);
		regs_captured += dump_data->blocks[i].num_captured;
		regs_skipped += dump_data->blocks[i].num_skipped;
	}

	pr_info("ETHDBG: [%s] ethdbg_device=0x%px blocks=%u captured regs=%u skipped "
		 "regs=%u\n", dev->net_dev->name, dev, dump_data->num_blocks,
		 regs_captured, regs_skipped);
}

static int ethdbg_panic_notifier(struct notifier_block *nb,
				 unsigned long event, void *ptr)
{
	struct ethdbg_interface *iface;

	list_for_each_entry(iface, &ethdbg_interfaces, list) {
		if (!iface->device)
			continue;

		ethdbg_dump_device(iface->device);
		ethdbg_dump_device_qos(iface->device, true);
		/* PHY capture during panic is disabled by default;
		 * MDIO accesses may sleep or take locks that are unsafe in panic context.
		 */
		if (capture_phy_on_panic)
			ethdbg_dump_device_phy(iface->device);
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
				"(size: 0x%lx)\n", interface_name, map->name,
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
		kfree(hw_block->reg_list);
		hw_block->reg_list = NULL;
		return -ENXIO;
	}

	hw_block->num_registers = num_registers;
	hw_block->num_captured = 0;
	hw_block->num_skipped = 0;
	hw_block->map = map;

	return 0;
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

int ethdbg_dump_register(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev = iface->device;
	const char *interface_name = iface->interface_name;
	struct ethdbg_dump_data *dump_data;
	int block_count;
	int ret;

	memset(&dev->dump_data, 0, sizeof(dev->dump_data));
	dump_data = &dev->dump_data;

	block_count = ethdbg_count_hw_blocks(dev);
	if (block_count == 0)
		return 0;

	/* Allocate MMIO blocks + 2 PHY blocks (common + vendor) upfront */
	dump_data->blocks = kzalloc((block_count + 2) * sizeof(struct ethdbg_hw_block),
				     GFP_KERNEL);
	if (!dump_data->blocks)
		return -ENOMEM;

	dump_data->num_blocks = ethdbg_setup_dump_blocks(dev, interface_name);

	ret = ethdbg_minidump_register(iface);
	if (ret)
		pr_warn("ethdbg: [%s] minidump registration incomplete, ret=%d\n",
			interface_name, ret);
	return 0;
}

void ethdbg_dump_unregister(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev = iface->device;
	const char *interface_name = iface->interface_name;
	struct ethdbg_dump_data *dump_data;
	int ret;
	int i;

	dump_data = &dev->dump_data;
	if (!dump_data->blocks)
		return;

	ret = ethdbg_minidump_unregister(iface);
	if (ret)
		pr_warn("ethdbg: [%s] minidump unregistration incomplete, ret=%d\n",
			interface_name, ret);

	for (i = 0; i < dump_data->num_blocks; i++) {
		if (!dump_data->blocks[i].is_phy)
			iounmap(dump_data->blocks[i].base);
		kfree(dump_data->blocks[i].reg_list);
	}

	kfree(dump_data->blocks);
	dump_data->blocks = NULL;
	dump_data->num_blocks = 0;
}

static void ethdbg_populate_c45_offsets(struct ethdbg_hw_block *block,
					const struct ethdbg_hw_desc *desc)
{
	unsigned int i, entry_idx = 0;

	for (i = 0; i < desc->num_reg_desc; i++) {
		u32 mmd = ETHDBG_C45_MMD(desc->reg_desc[i].start_offset);
		u32 end = ETHDBG_C45_REG(desc->reg_desc[i].end_offset);
		u32 reg;

		for (reg = ETHDBG_C45_REG(desc->reg_desc[i].start_offset);
		     reg <= end && entry_idx < block->num_registers; reg++, entry_idx++)
			block->reg_list[entry_idx].offset = ETHDBG_C45_OFFSET(mmd, reg);
	}
}

static void ethdbg_populate_c22_offsets(struct ethdbg_hw_block *block,
					const struct ethdbg_hw_desc *desc)
{
	unsigned int i, entry_idx = 0;

	for (i = 0; i < desc->num_reg_desc; i++) {
		u32 reg;

		for (reg = desc->reg_desc[i].start_offset;
		     reg <= desc->reg_desc[i].end_offset && entry_idx < block->num_registers;
		     reg++, entry_idx++)
			block->reg_list[entry_idx].offset = reg;
	}
}

/*
 * ethdbg_populate_indirect_offsets - pre-populate reg_list[].offset for a
 * QoS indirect block at registration time.
 *
 * Each reg_desc entry has start_offset == end_offset == ETHDBG_IND_OFFSET(idx, reg_id).
 * We copy every encoded offset straight into reg_list[], one entry per descriptor
 * row, mirroring how ethdbg_populate_c22_offsets() works for PHY blocks.
 */
static void ethdbg_populate_indirect_offsets(struct ethdbg_hw_block *block,
					     const struct ethdbg_hw_desc *desc)
{
	unsigned int i;

	for (i = 0; i < desc->num_reg_desc && i < block->num_registers; i++)
		block->reg_list[i].offset = desc->reg_desc[i].start_offset;
}

static int ethdbg_alloc_phy_block(struct ethdbg_hw_block *block,
				  const struct ethdbg_hw_desc *desc)
{
	unsigned int num_regs;

	num_regs = ethdbg_count_phy_regs(desc->reg_desc, desc->num_reg_desc, desc->is_c45);

	block->reg_list = kzalloc(PAGE_ALIGN(num_regs * sizeof(struct ethdbg_reg_entry)), GFP_KERNEL);
	if (!block->reg_list)
		return -ENOMEM;

	block->num_registers = num_regs;
	block->is_phy = true;
	block->is_c45 = desc->is_c45;
	block->name = desc->name;
	block->map = NULL;

	/* Pre-populate offsets at registration time; values filled at capture time */
	if (desc->is_c45)
		ethdbg_populate_c45_offsets(block, desc);
	else
		ethdbg_populate_c22_offsets(block, desc);

	return 0;
}

/*
 * ethdbg_capture_phy_block - read registers into @block.
 * Uses block->is_c45 and block->phy_desc set at alloc time.
 */
static void ethdbg_capture_phy_block(struct ethdbg_hw_block *block,
				     struct phy_device *phydev)
{
	unsigned int i;
	int val;

	for (i = 0; i < block->num_registers; i++) {
		u32 offset = block->reg_list[i].offset;

		if (block->is_c45) {
			u32 mmd = ETHDBG_C45_MMD(offset);
			u32 reg = ETHDBG_C45_REG(offset);

			val = phy_read_mmd(phydev, mmd, reg);
		} else {
			val = phy_read(phydev, offset);
		}

		block->reg_list[i].value = (u32)val;
	}
	block->num_captured = block->num_registers;
}

int ethdbg_dump_register_phy(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev = iface->device;
	const char *interface_name = iface->interface_name;
	struct ethdbg_dump_data *dump_data = &dev->dump_data;
	const struct ethdbg_hw_desc *vendor_desc;
	struct phy_device *phydev;
	u32 phy_id;
	int ret;

	if (!dev->net_dev || !dev->net_dev->phydev) {
		pr_info("ethdbg: [%s] phydev not yet attached\n",
			dev->net_dev ? dev->net_dev->name : "?");
		return 0;
	}

	phydev = dev->net_dev->phydev;
	phy_id = phydev->drv ? phydev->drv->phy_id : 0;

	vendor_desc = ethdbg_find_phy_desc(phy_id);
	if (!vendor_desc) {
		pr_info("ethdbg: [%s] no PHY profile for phy_id=0x%08x\n",
			dev->net_dev->name, phy_id);
		return 0;
	}

	const struct ethdbg_hw_desc *common_desc = phydev->is_c45 ? &common_c45_phy_desc
							      : &common_c22_phy_desc;

	ret = ethdbg_alloc_phy_block(&dump_data->blocks[dump_data->num_blocks],
				     common_desc);
	if (ret)
		return ret;
	dump_data->num_blocks++;

	ret = ethdbg_alloc_phy_block(&dump_data->blocks[dump_data->num_blocks],
				     vendor_desc);
	if (ret) {
		kfree(dump_data->blocks[dump_data->num_blocks - 1].reg_list);
		dump_data->blocks[dump_data->num_blocks - 1].reg_list = NULL;
		dump_data->num_blocks--;
		return ret;
	}
	dump_data->num_blocks++;

	ret = ethdbg_minidump_register(iface);
	if (ret)
		pr_warn("ethdbg: [%s] minidump registration incomplete, ret=%d\n",
			interface_name, ret);
	return 0;
}

void ethdbg_dump_device_phy(struct ethdbg_device *dev)
{
	struct ethdbg_dump_data *dump_data = &dev->dump_data;
	struct phy_device *phydev;
	unsigned int i;

	if (!dev->net_dev || !dev->net_dev->phydev)
		return;

	phydev = dev->net_dev->phydev;

	for (i = 0; i < dump_data->num_blocks; i++) {
		struct ethdbg_hw_block *block = &dump_data->blocks[i];

		if (!block->is_phy || !block->reg_list)
			continue;

		ethdbg_capture_phy_block(block, phydev);

		pr_info("ethdbg: [%s] captured %u PHY regs for %s\n",
			dev->net_dev->name, block->num_captured, block->name);
	}
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

/*
 * ethdbg_alloc_indirect_block - allocate and initialise a QoS indirect
 * hw_block.  Mirrors ethdbg_alloc_phy_block() for PHY blocks.
 *
 * @block:      pre-zeroed hw_block slot to initialise
 * @desc:       hw_desc for this indirect block (qos-vlan or qos-l3l4)
 * @base:       ioremap base shared with the direct block (stmmaceth)
 */
static int ethdbg_alloc_indirect_block(struct ethdbg_hw_block *block,
				       const struct ethdbg_hw_desc *desc,
				       void __iomem *base)
{
	unsigned int num_regs = desc->num_reg_desc; /* one entry per descriptor row */

	block->reg_list = kzalloc(PAGE_ALIGN(num_regs * sizeof(struct ethdbg_reg_entry)),
				  GFP_KERNEL);
	if (!block->reg_list)
		return -ENOMEM;

	block->num_registers = num_regs;
	block->base          = base;   /* borrowed — do NOT iounmap this block */
	block->map           = NULL;   /* no ethdbg_map; base is shared */
	block->name          = desc->name;

	/* Pre-populate offsets at registration time; values filled at capture time */
	ethdbg_populate_indirect_offsets(block, desc);

	return 0;
}

/*
 * ethdbg_dump_register_qos - Allocate and initialise all QoS capture blocks:
 *   blocks[0] = direct  (MMIO, existing path)
 *   blocks[1] = VLAN indirect
 *   blocks[2] = L3/L4 indirect
 */
int ethdbg_dump_register_qos(struct ethdbg_device *dev, const char *interface_name)
{
	const struct ethdbg_hw_desc *direct_desc, *vlan_desc, *l3l4_desc;
	struct ethdbg_dump_data *qos_data = &dev->qos_dump_data;
	struct ethdbg_map *map, *stmmac_map = NULL;
	void __iomem *shared_base;
	int ret;

	memset(qos_data, 0, sizeof(*qos_data));

	/* Find the stmmaceth map — all QoS registers share that base address */
	list_for_each_entry(map, &dev->map_regions, list) {
		if (map->name && strcmp(map->name, "stmmaceth") == 0) {
			stmmac_map = map;
			break;
		}
	}
	if (!stmmac_map) {
		pr_warn("ethdbg: [%s] stmmaceth map not found, skipping QoS dump\n",
			interface_name);
		return 0;
	}

	direct_desc = ethdbg_find_hw_desc("qos-stmmac");
	vlan_desc   = ethdbg_find_hw_desc("qos-vlan");
	l3l4_desc   = ethdbg_find_hw_desc("qos-l3l4");
	if (!direct_desc || !vlan_desc || !l3l4_desc) {
		pr_warn("ethdbg: [%s] QoS hw_desc missing, skipping QoS dump\n",
			interface_name);
		return 0;
	}

	/* 3 slots: direct + vlan + l3l4 */
	qos_data->blocks = kzalloc(3 * sizeof(struct ethdbg_hw_block), GFP_KERNEL);
	if (!qos_data->blocks)
		return -ENOMEM;

	/* blocks[0]: direct MMIO — uses ethdbg_init_hw_block (ioremap inside) */
	ret = ethdbg_init_hw_block(&qos_data->blocks[0], stmmac_map,
				   direct_desc, interface_name);
	if (ret)
		goto err_free_blocks;

	/*
	 * Borrow the ioremap base from blocks[0] for the indirect blocks.
	 * Both indirect blocks share the same MMIO window; they must not
	 * iounmap it — that is handled by blocks[0] in unregister.
	 */
	shared_base = qos_data->blocks[0].base;

	/* blocks[1]: VLAN indirect */
	ret = ethdbg_alloc_indirect_block(&qos_data->blocks[1], vlan_desc,
					  shared_base);
	if (ret)
		goto err_free_direct;

	/* blocks[2]: L3/L4 indirect */
	ret = ethdbg_alloc_indirect_block(&qos_data->blocks[2], l3l4_desc,
					  shared_base);
	if (ret)
		goto err_free_vlan;

	qos_data->num_blocks = 3;
	return 0;

err_free_vlan:
	kfree(qos_data->blocks[1].reg_list);
	qos_data->blocks[1].reg_list = NULL;
	qos_data->blocks[1].base = NULL;
err_free_direct:
	iounmap(qos_data->blocks[0].base);
	qos_data->blocks[0].base = NULL;
	kfree(qos_data->blocks[0].reg_list);
	qos_data->blocks[0].reg_list = NULL;
err_free_blocks:
	kfree(qos_data->blocks);
	qos_data->blocks = NULL;
	return ret;
}

/*
 * ethdbg_dump_unregister_qos - Release resources allocated by
 * ethdbg_dump_register_qos().
 *
 * blocks[0] owns the ioremap; blocks[1] and blocks[2] borrow it.
 * Only iounmap blocks[0].base.
 */
void ethdbg_dump_unregister_qos(struct ethdbg_device *dev)
{
	struct ethdbg_dump_data *qos_data = &dev->qos_dump_data;
	int i;

	if (!qos_data->blocks)
		return;

	for (i = 0; i < qos_data->num_blocks; i++) {
		/* Only the direct block (index 0) owns the ioremap base */
		if (i == 0 && qos_data->blocks[0].base)
			iounmap(qos_data->blocks[0].base);
		qos_data->blocks[i].base = NULL;
		kfree(qos_data->blocks[i].reg_list);
		qos_data->blocks[i].reg_list = NULL;
	}

	kfree(qos_data->blocks);
	qos_data->blocks = NULL;
	qos_data->num_blocks = 0;
}

/*
 * ethdbg_dump_device_qos - Capture QoS registers.
 *
 *   blocks[0]: direct MMIO   — always captured (safe in any context)
 *   blocks[1]: VLAN indirect  — skipped when panic_mode=true (requires clocks)
 *   blocks[2]: L3/L4 indirect — skipped when panic_mode=true (requires clocks)
 */
void ethdbg_dump_device_qos(struct ethdbg_device *dev, bool panic_mode)
{
	struct ethdbg_dump_data *qos_data = &dev->qos_dump_data;
	const struct ethdbg_hw_desc *hw_desc;
	struct ethdbg_hw_block *block;
	unsigned int i;

	if (!qos_data->blocks || qos_data->num_blocks < 1)
		return;

	/* blocks[0]: direct MMIO */
	block   = &qos_data->blocks[0];
	hw_desc = ethdbg_find_hw_desc("qos-stmmac");
	if (hw_desc && block->reg_list && block->base)
		ethdbg_capture_regs(block, hw_desc, dev->net_dev->name);

	if (panic_mode)
		return;

	if (qos_data->num_blocks < 3)
		return;

	/* blocks[1]: VLAN indirect */
	block = &qos_data->blocks[1];
	if (block->reg_list) {
		block->num_captured = 0;
		for (i = 0; i < block->num_registers; i++) {
			u32 idx = ETHDBG_IND_IDX(block->reg_list[i].offset);

			block->reg_list[i].value =
				ethdbg_read_vlan_indirect(block->base, idx);
			block->num_captured++;
		}
	}

	/* blocks[2]: L3/L4 indirect */
	block = &qos_data->blocks[2];
	if (block->reg_list) {
		block->num_captured = 0;
		for (i = 0; i < block->num_registers; i++) {
			u32 encoded = block->reg_list[i].offset;
			u32 idx     = ETHDBG_IND_IDX(encoded);
			u32 reg_id  = ETHDBG_IND_REG(encoded);

			block->reg_list[i].value =
				ethdbg_read_l3l4_indirect(block->base, idx, reg_id);
			block->num_captured++;
		}
	}
}
