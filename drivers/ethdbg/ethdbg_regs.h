/* SPDX-License-Identifier: GPL-2.0-only*/
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#ifndef _ETHDBG_REGS_H_
#define _ETHDBG_REGS_H_

#include <linux/types.h>


/* C45 offset encoding: upper 16 bits = MMD, lower 16 bits = register address */
#define ETHDBG_C45_MMD(offset)		((offset) >> 16)
#define ETHDBG_C45_REG(offset)		((offset) & 0xFFFF)
#define ETHDBG_C45_OFFSET(mmd, reg)	(((mmd) << 16) | (reg))

/**
 * struct ethdbg_reg_desc - Register region descriptor
 * @start_offset: Starting offset of the region (inclusive)
 * @end_offset: Ending offset of the region (inclusive)
 *
 * Describes a contiguous region of registers to capture.
 * Registers are captured from start_offset to end_offset (inclusive)
 * in REG_SIZE increments.
 */
struct ethdbg_reg_desc {
	u32 start_offset;
	u32 end_offset;
};

/**
 * struct ethdbg_hw_desc - Hardware block descriptor
 * @name:        Name of the register block (e.g., "stmmaceth", "rgmii", "RTL8261C")
 * @reg_desc:    Array of register offset ranges in this block
 * @num_reg_desc: Number of register offset ranges in the array
 * @is_phy:      True if this is an MDIO PHY block
 * @is_c45:      True if PHY uses C45 register access (valid when is_phy is true)
 * @phy_id:      Expected PHY ID matched against phydev->drv->phy_id
 * @phy_id_mask: Mask applied before comparing phy_id
 */
struct ethdbg_hw_desc {
	const char                   *name;
	const struct ethdbg_reg_desc *reg_desc;
	size_t                        num_reg_desc;
	bool                          is_phy;
	bool                          is_c45;
	u32                           phy_id;
	u32                           phy_id_mask;
};


#include "ethdbg_regs_echo.h"
#include "ethdbg_phy_regs.h"
#endif /* _ETHDBG_REGS_H_ */
