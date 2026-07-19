/* SPDX-License-Identifier: GPL-2.0-only*/
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#ifndef _ECHO_REG_DESC_H_
#define _ECHO_REG_DESC_H_

/**
 * Echo Platform Register Descriptors
 *
 * This file contains register layout definitions for the Echo platform.
 * Each array describes the register regions for a specific hardware block.
 */

static const struct ethdbg_reg_desc mac_regs_echo[] = {
	{0x0000, 0x002C}, {0x0050, 0x0058}, {0x0060, 0x0064}, {0x006C, 0x009C},
	{0x00B0, 0x00B8}, {0x00D0, 0x00E0}, {0x0110, 0x012C}, {0x0144, 0x0144},
	{0x0200, 0x0224}, {0x0230, 0x0238}, {0x0240, 0x0248}, {0x0250, 0x0258},
	{0x0260, 0x0268}, {0x0280, 0x0280}, {0x0290, 0x0290}, {0x0300, 0x03FC},
	{0x0700, 0x070C}, {0x0730, 0x0734}, {0x0800, 0x08E4}, {0x0900, 0x09E8},
	{0x09F0, 0x09F4}, {0x0A00, 0x0A14}, {0x0A20, 0x0A34}, {0x0A40, 0x0B40},
	{0x0C00, 0x0C04}, {0x0C10, 0x0C10}, {0x0C80, 0x0C8C}, {0x1000, 0x10F4},
	{0x3000, 0x3084}, {0x7000, 0x707C}, {0x70C0, 0x70D0}, {0x70E0, 0x70E4},
	{0x7500, 0x7534}, {0x8000, 0x857C}, {0x9000, 0x95FC}, {0xA000, 0xA57C},
	{0xB000, 0xB5FC}, {0xC000, 0xC57C}, {0xD000, 0xD5FC}, {0xE000, 0xE57C},
	{0xF000, 0xF5FC}, {0x10000, 0x1057C}, {0x11140, 0x11140}, {0x12008, 0x1200C},
};

static const struct ethdbg_reg_desc qxpcs_regs_echo[] = {
	{0x0000, 0x0024}, {0x0038, 0x003C}, {0x0050, 0x0054}, {0x0060, 0x0064},
	{0x0080, 0x0080}, {0x0088, 0x00A8}, {0x1C20, 0x1C40}, {0x2000, 0x2008},
	{0x2010, 0x2024}, {0x202C, 0x202C}, {0x2040, 0x2044}, {0x2054, 0x2054},
	{0x2060, 0x2064}, {0x207C, 0x207C}, {0x23C0, 0x23D4}, {0x23E0, 0x23E0},
	{0x3000, 0x300C}, {0x3010, 0x301C}, {0x3020, 0x3024}, {0x3038, 0x303C},
	{0x4000, 0x4018}, {0x403C, 0x403C}, {0x5000, 0x5008}, {0x5010, 0x5010},
	{0x5028, 0x5028},
};

static const struct ethdbg_reg_desc rgmii_regs_echo[] = {
	{0x0000, 0x0020}, {0x0028, 0x0028}, {0x0044, 0x0044}, {0x00F4, 0x00F4},
	{0x016C, 0x0174}, {0x01CC, 0x01D0}, {0x01E4, 0x0224}, {0x0228, 0x02A4},
	{0x02A8, 0x0324}, {0x0328, 0x0340}, {0x0344, 0x0390},
};

static const struct ethdbg_reg_desc serdes_regs_echo[] = {
	{0x0058, 0x0058}, {0x0074, 0x0074}, {0x007C, 0x007C}, {0x0084, 0x0084},
	{0x0094, 0x0094}, {0x00AC, 0x00B0}, {0x00BC, 0x00BC}, {0x00CC, 0x00D4},
	{0x0110, 0x0114}, {0x0124, 0x0124}, {0x0140, 0x0140}, {0x0158, 0x015C},
	{0x0168, 0x0168}, {0x0174, 0x0178}, {0x01AC, 0x01B0}, {0x01BC, 0x01BC},
	{0x0424, 0x0428}, {0x043C, 0x0440}, {0x0484, 0x0484}, {0x048C, 0x048C},
	{0x04A4, 0x04A4}, {0x04C0, 0x04C0}, {0x0608, 0x0608}, {0x0614, 0x0614},
	{0x0630, 0x0634}, {0x063C, 0x0648}, {0x0680, 0x0680}, {0x06D8, 0x06DC},
	{0x06E8, 0x06F4}, {0x06F8, 0x0700}, {0x0710, 0x0714}, {0x071C, 0x071C},
	{0x0724, 0x0728}, {0x075C, 0x076C}, {0x0770, 0x0780}, {0x0784, 0x0794},
	{0x07A8, 0x07A8}, {0x0C00, 0x0C0C}, {0x0C20, 0x0C20}, {0x0C28, 0x0C28},
	{0x0C94, 0x0C94}, {0x0CD8, 0x0CDC}, {0x0D18, 0x0D18},
};

/*
 * QoS direct-access registers (XGMAC, shared stmmaceth base).
 *
 * Entries are ordered to match the required output section order so that
 * the printer can walk reg_list[] sequentially without reordering.
 *
 *   0x0000           MAC_CONFIGURATION (low nibble = bus id, used for EMAC line)
 *   0x0008           XGMAC_PACKET_FILTER
 *   0x1030-0x1038    XGMAC_MTL_RXQ_DMA_MAP0/1/2
 *   0x00A8-0x00AC    XGMAC_RXQ_CTRL2/3 (PCP map)
 *   0x0070-0x008C    XGMAC_Qx_TX_FLOW_CTRL (queues 0-7)
 *   0x0090           XGMAC_RX_FLOW_CTRL
 *
 * CBS per queue (stride 0x80, 4 shaper regs, ETS_CONTROL excluded):
 *   0x1118+q*0x80    XGMAC_MTL_TCx_QUANTUM_WEIGHT
 *   0x111C+q*0x80    XGMAC_MTL_TCx_SENDSLOPE
 *   0x1120+q*0x80    XGMAC_MTL_TCx_HICREDIT
 *   0x1124+q*0x80    XGMAC_MTL_TCx_LOCREDIT
 */
static const struct ethdbg_reg_desc qos_direct_regs_echo[] = {
	{0x0000, 0x0000},		/* MAC_CONFIGURATION (EMAC id) */
	{0x0008, 0x0008},		/* PACKET_FILTER */
	{0x1030, 0x1038},		/* DMA MAP 0/1/2 */
	{0x00A8, 0x00AC},		/* PCP MAP (RXQ_CTRL2/3) */
	{0x0070, 0x008C},		/* Tx Flow Control Q0-Q7 */
	{0x0090, 0x0090},		/* Rx Flow Control */
	/* CBS Q0-Q8: QUANTUM_WEIGHT, SENDSLOPE, HICREDIT, LOCREDIT */
	{0x1118, 0x1124},		/* Q0 */
	{0x1198, 0x11A4},		/* Q1 */
	{0x1218, 0x1224},		/* Q2 */
	{0x1298, 0x12A4},		/* Q3 */
	{0x1318, 0x1324},		/* Q4 */
	{0x1398, 0x13A4},		/* Q5 */
	{0x1418, 0x1424},		/* Q6 */
	{0x1498, 0x14A4},		/* Q7 */
	{0x1518, 0x1524},		/* Q8 */
};

/* 32 VLAN filter entries, one register each */
#define VLAN_ENTRY(i)  { ETHDBG_IND_OFFSET(i, 0), ETHDBG_IND_OFFSET(i, 0) }

static const struct ethdbg_reg_desc qos_vlan_regs_echo[] = {
	VLAN_ENTRY(0),  VLAN_ENTRY(1),  VLAN_ENTRY(2),  VLAN_ENTRY(3),
	VLAN_ENTRY(4),  VLAN_ENTRY(5),  VLAN_ENTRY(6),  VLAN_ENTRY(7),
	VLAN_ENTRY(8),  VLAN_ENTRY(9),  VLAN_ENTRY(10), VLAN_ENTRY(11),
	VLAN_ENTRY(12), VLAN_ENTRY(13), VLAN_ENTRY(14), VLAN_ENTRY(15),
	VLAN_ENTRY(16), VLAN_ENTRY(17), VLAN_ENTRY(18), VLAN_ENTRY(19),
	VLAN_ENTRY(20), VLAN_ENTRY(21), VLAN_ENTRY(22), VLAN_ENTRY(23),
	VLAN_ENTRY(24), VLAN_ENTRY(25), VLAN_ENTRY(26), VLAN_ENTRY(27),
	VLAN_ENTRY(28), VLAN_ENTRY(29), VLAN_ENTRY(30), VLAN_ENTRY(31),
};
#undef VLAN_ENTRY

/*
 * 32 L3/L4 filters, 6 registers each.
 * reg_id values from dwxgmac2.h: 0x0 (L3L4_CTRL), 0x1 (L4_ADDR),
 * 0x4-0x7 (L3_ADDR0-3). reg_ids 0x2 and 0x3 do not exist in hardware.
 */
#define L3L4_FILTER(i)						\
	{ ETHDBG_IND_OFFSET(i, 0), ETHDBG_IND_OFFSET(i, 0) },	\
	{ ETHDBG_IND_OFFSET(i, 1), ETHDBG_IND_OFFSET(i, 1) },	\
	{ ETHDBG_IND_OFFSET(i, 4), ETHDBG_IND_OFFSET(i, 4) },	\
	{ ETHDBG_IND_OFFSET(i, 5), ETHDBG_IND_OFFSET(i, 5) },	\
	{ ETHDBG_IND_OFFSET(i, 6), ETHDBG_IND_OFFSET(i, 6) },	\
	{ ETHDBG_IND_OFFSET(i, 7), ETHDBG_IND_OFFSET(i, 7) }

static const struct ethdbg_reg_desc qos_l3l4_regs_echo[] = {
	L3L4_FILTER(0),  L3L4_FILTER(1),  L3L4_FILTER(2),  L3L4_FILTER(3),
	L3L4_FILTER(4),  L3L4_FILTER(5),  L3L4_FILTER(6),  L3L4_FILTER(7),
	L3L4_FILTER(8),  L3L4_FILTER(9),  L3L4_FILTER(10), L3L4_FILTER(11),
	L3L4_FILTER(12), L3L4_FILTER(13), L3L4_FILTER(14), L3L4_FILTER(15),
	L3L4_FILTER(16), L3L4_FILTER(17), L3L4_FILTER(18), L3L4_FILTER(19),
	L3L4_FILTER(20), L3L4_FILTER(21), L3L4_FILTER(22), L3L4_FILTER(23),
	L3L4_FILTER(24), L3L4_FILTER(25), L3L4_FILTER(26), L3L4_FILTER(27),
	L3L4_FILTER(28), L3L4_FILTER(29), L3L4_FILTER(30), L3L4_FILTER(31),
};
#undef L3L4_FILTER

static const struct ethdbg_hw_desc echo_hw_descs[] = {
	{
		.name = "stmmaceth",
		.reg_desc = mac_regs_echo,
		.num_reg_desc = ARRAY_SIZE(mac_regs_echo),
	},
	{
		.name = "qxpcs",
		.reg_desc = qxpcs_regs_echo,
		.num_reg_desc = ARRAY_SIZE(qxpcs_regs_echo),
	},
	{
		.name = "rgmii",
		.reg_desc = rgmii_regs_echo,
		.num_reg_desc = ARRAY_SIZE(rgmii_regs_echo),
	},
	{
		.name = "serdes",
		.reg_desc = serdes_regs_echo,
		.num_reg_desc = ARRAY_SIZE(serdes_regs_echo),
	},
	{
		.name = "qos-stmmac",
		.reg_desc = qos_direct_regs_echo,
		.num_reg_desc = ARRAY_SIZE(qos_direct_regs_echo),
	},
	{
		.name = "qos-vlan",
		.reg_desc = qos_vlan_regs_echo,
		.num_reg_desc = ARRAY_SIZE(qos_vlan_regs_echo),
	},
	{
		.name = "qos-l3l4",
		.reg_desc = qos_l3l4_regs_echo,
		.num_reg_desc = ARRAY_SIZE(qos_l3l4_regs_echo),
	},
	{ NULL, NULL, 0 }
};

#endif
