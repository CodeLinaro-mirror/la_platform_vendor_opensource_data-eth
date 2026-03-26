/* SPDX-License-Identifier: GPL-2.0-only*/
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#ifndef _ETHDBG_REGS_H_
#define _ETHDBG_REGS_H_

#include <linux/types.h>

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
 * @name: Name of the register block (e.g., "stmmaceth", "rgmii")
 * @reg_desc: Array of register offset ranges in this block
 * @num_reg_desc: Number of register offset ranges in the array
 *
 * Describes the layout of registers in a hardware block.
 * This is a static template used to capture registers.
 */
struct ethdbg_hw_desc {
	const char *name;
	const struct ethdbg_reg_desc *reg_desc;
	size_t num_reg_desc;
};

#include "ethdbg_regs_sdx95.h"

#endif /* _ETHDBG_REGS_H_ */
