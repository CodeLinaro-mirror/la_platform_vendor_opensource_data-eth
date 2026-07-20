/* SPDX-License-Identifier: GPL-2.0-only*/
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#ifndef _ETHDBG_PHY_REGS_H_
#define _ETHDBG_PHY_REGS_H_

/**
 * PHY Register Descriptors
 *
 * C22 offsets: lower 16 bits = register address, upper 16 bits = 0
 * C45 offsets: upper 16 bits = mmd, lower 16 bits = register address
 *
 * Ranges follow the same {start_offset, end_offset} convention as
 * MMIO blocks. For individual registers, start_offset == end_offset.
 */

/* Common C22 MII registers - standard IEEE 802.3, applicable to all PHYs */
static const struct ethdbg_reg_desc common_c22_regs[] = {
	{ 0x00, 0x05 },	/* MII Control, Status, PHY ID 1/2, Auto-Neg Advertisement, Link Partner Ability */
	{ 0x09, 0x0a },	/* 1000BASE-T Control, Status */
};

/* Common C45 registers - standard IEEE 802.3, applicable to all C45-capable PHYs */
static const struct ethdbg_reg_desc common_c45_regs[] = {
	/* MMD7 - Auto-Negotiation */
	{ 0x00070000, 0x00070001 },	/* Reg 0x00-0x01: AN Control, Status */
	{ 0x00070010, 0x00070011 },	/* Reg 0x10-0x11: AN Advertisement, LP Base Page */
	{ 0x0007001C, 0x0007001D },	/* Reg 0x1C-0x1D: AN Next Page TX, LP Next Page */
	{ 0x00070020, 0x00070021 },	/* Reg 0x20-0x21: 10GBASE-T AN Control, Status */
	{ 0x00078029, 0x0007802F },	/* Reg 0x8029-0x802F: PHY stats counters */

	/* MMD1 - PMA/PMD */
	{ 0x00010000, 0x00010001 },	/* Reg 0x00-0x01: PMA Control, Status */
	{ 0x00010007, 0x00010008 },	/* Reg 0x07-0x08: PMA Speed capability, 10GBASE-T capability */
	{ 0x00010084, 0x00010084 },	/* Reg 0x84: Test mode control */
	{ 0x00010155, 0x0001015A },	/* Reg 0x155-0x15A: Ingress/egress CRC counters */

	/* MMD3 - PCS */
	{ 0x00030000, 0x00030001 },	/* Reg 0x00-0x01: PCS Control, Status */
	{ 0x00030020, 0x00030021 },	/* Reg 0x20-0x21: 10GBASE-R PCS Status 1, 2 */

	/* MMD7 - EEE */
	{ 0x0007003C, 0x0007003F },	/* Reg 0x3C-0x3F: EEE Control/Advertisement */
};

/* RTL8261C - C45 registers */
static const struct ethdbg_reg_desc rtl8261_c45_regs[] = {
	{ 0x00010000, 0x00010000 },	/* mmd=1  Reg 0x00: PMA Control */
	{ 0x001FA400, 0x001FA400 },	/* mmd=31 Reg 0xA400: FEDCR */
	{ 0x001FA434, 0x001FA434 },	/* mmd=31 Reg 0xA434: ETHERNET_LINK_STATUS */
	{ 0x001E7580, 0x001E7580 },	/* mmd=30 Reg 0x7580: SERDES_CONTROL_3 */
	{ 0x001E758B, 0x001E758B },	/* mmd=30 Reg 0x758B: SERDES_CONTROL_7 */
	{ 0x001E758D, 0x001E758D },	/* mmd=30 Reg 0x758D */
	{ 0x001E7587, 0x001E7587 },	/* mmd=30 Reg 0x7587 */
};

/* QCA81XX - C45 only PHY, PHY-specific registers */
static const struct ethdbg_reg_desc qca81xx_c45_regs[] = {
	/* MMD31 - PHY Identification & Basic Status */
	{ 0x001F0000, 0x001F0003 },	/* Reg 0x00-0x03: PHY Control, Status, ID1, ID2 */
	{ 0x001F0011, 0x001F0011 },	/* Reg 0x11: PHY Link Status (speed, duplex) */
};

static inline unsigned int ethdbg_count_phy_regs(const struct ethdbg_reg_desc *regs,
					      unsigned int num_ranges, bool is_c45)
{
	unsigned int i, count = 0;

	for (i = 0; i < num_ranges; i++) {
		if (is_c45)
			count += ETHDBG_C45_REG(regs[i].end_offset) -
				 ETHDBG_C45_REG(regs[i].start_offset) + 1;
		else
			count += regs[i].end_offset - regs[i].start_offset + 1;
	}

	return count;
}

static const struct ethdbg_hw_desc common_c22_phy_desc = {
	.name         = "common",
	.is_phy       = true,
	.is_c45       = false,
	.reg_desc     = common_c22_regs,
	.num_reg_desc = ARRAY_SIZE(common_c22_regs),
};

static const struct ethdbg_hw_desc common_c45_phy_desc = {
	.name         = "common",
	.is_phy       = true,
	.is_c45       = true,
	.reg_desc     = common_c45_regs,
	.num_reg_desc = ARRAY_SIZE(common_c45_regs),
};

/* phy_desc table - vendor specific PHY descriptors */
static const struct ethdbg_hw_desc phy_descs_tbl[] = {
	{
		.name         = "RTL8261C",
		.phy_id       = 0x001CC898,
		.phy_id_mask  = 0xFFFFFFF0,
		.is_phy       = true,
		.is_c45       = true,
		.reg_desc     = rtl8261_c45_regs,
		.num_reg_desc = ARRAY_SIZE(rtl8261_c45_regs),
	},
	{
		.name         = "QCA8112",
		.phy_id       = 0x004DD1C0,
		.phy_id_mask  = 0xFFFFFFF0,
		.is_phy       = true,
		.is_c45       = true,
		.reg_desc     = qca81xx_c45_regs,
		.num_reg_desc = ARRAY_SIZE(qca81xx_c45_regs),
	},
	{ }
};

static inline const struct ethdbg_hw_desc *ethdbg_find_phy_desc(u32 phy_id)
{
	int i;

	for (i = 0; phy_descs_tbl[i].name != NULL; i++) {
		if ((phy_id & phy_descs_tbl[i].phy_id_mask) ==
		    (phy_descs_tbl[i].phy_id & phy_descs_tbl[i].phy_id_mask))
			return &phy_descs_tbl[i];
	}
	return NULL;
}

#endif /* _ETHDBG_PHY_REGS_H_ */
