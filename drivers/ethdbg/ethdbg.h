/* SPDX-License-Identifier: GPL-2.0-only*/
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#ifndef _ETHDBG_H_
#define _ETHDBG_H_

#include <linux/list.h>
#include <linux/uio_driver.h>
#include <linux/netdevice.h>
#include <linux/if.h>
#include <linux/types.h>

extern struct list_head ethdbg_interfaces;

/**
 * struct ethdbg_reg_entry - Captured register data
 * @offset: Register offset
 * @value: Register value
 *
 * Stores a single register's offset and captured value.
 */
struct ethdbg_reg_entry {
	u32 offset;
	u32 value;
};

/**
 * struct ethdbg_map - Memory region mapping information
 * @list: List head for linking multiple memory regions
 * @name: Name of the memory region (e.g., "mac", "phy", "pcs")
 * @base_addr: Physical base address of the memory region
 * @size: Size of the memory region in bytes
 *
 * Represents a single memory-mapped region that will be exposed
 * through UIO for userspace access.
 */
struct ethdbg_map {
	struct list_head list;
	const char *name;
	unsigned long base_addr;
	unsigned long size;
};

/**
 * struct ethdbg_hw_block - Runtime hardware block data
 * @base: Virtual address of mapped register block
 * @reg_list: Array of captured register data
 * @num_registers: Total number of registers in this block
 * @num_captured: Number of registers actually captured
 * @num_skipped: Number of registers skipped due to out-of-bounds access
 * @map: Pointer to the original ethdbg_map structure
 *
 * Represents a single hardware block (STMMAC, PCS, RGMII,
 * or SERDES) for panic register capture. During panic,
 * registers are read via base and stored in reg_list.
 */
struct ethdbg_hw_block {
	void __iomem *base;
	unsigned int num_registers;
	unsigned int num_captured;
	unsigned int num_skipped;
	struct ethdbg_map *map;
	struct ethdbg_reg_entry *reg_list;
};

/**
 * struct ethdbg_panic_data - Panic capture data for an interface
 * @blocks: Dynamically allocated array of hardware blocks
 * @num_blocks: Number of initialized blocks
 *
 * Contains all captured register data for panic analysis.
 */
struct ethdbg_dump_data {
	struct ethdbg_hw_block *blocks;
	unsigned int num_blocks;
};

/**
 * struct ethdbg_device - Ethernet debug device information
 * @net_dev: Pointer to the associated network device
 * @map_regions: List head for memory regions to expose via UIO
 * @uio_info: UIO device information structure
 * @panic_data: Panic capture data
 *
 * Created when a network device is detected and contains all
 * information needed to expose hardware registers via UIO.
 */
struct ethdbg_device {
	struct net_device *net_dev;
	void *netdev_priv;
	struct list_head map_regions;

	/* UIO structure */
	struct uio_info uio_info;

	/* Panic structure for data capture */
	struct ethdbg_dump_data dump_data;
};

/**
 * struct ethdbg_interface - User-specified interface configuration
 * @list: List head for linking multiple interfaces
 * @interface_name: Name of the network interface (e.g., "eth0")
 * @device: Pointer to ethdbg_device (NULL until netdevice is detected)
 *
 * Represents a user-configured interface name. Created when the user
 * specifies an interface via module parameter. The device pointer is
 * populated when the corresponding network device is detected.
 */
struct ethdbg_interface {
	struct list_head list;
	char interface_name[IFNAMSIZ];
	struct ethdbg_device *device;
};


int ethdbg_uio_add(struct ethdbg_interface *iface);
void ethdbg_uio_del(struct ethdbg_interface *iface);

int ethdbg_dump_register(struct ethdbg_device *dev, const char *interface_name);
void ethdbg_dump_unregister(struct ethdbg_device *dev);
int ethdbg_panic_init(void);
void ethdbg_panic_deinit(void);

#endif /* _ETHDBG_H_ */
