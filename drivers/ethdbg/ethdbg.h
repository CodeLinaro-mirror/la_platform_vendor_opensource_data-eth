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
 * @index: Position of this map in the map_regions list, assigned at
 *         parse time. Used as the IDX in minidump region names so that
 *         eth0-reg<IDX> and eth0-map<IDX> can be correlated in a dump.
 *
 * Represents a single memory-mapped region that will be exposed
 * through UIO for userspace access.
 */
struct ethdbg_map {
	struct list_head list;
	const char *name;
	unsigned long base_addr;
	unsigned long size;
	unsigned int index;
};

/**
 * struct ethdbg_hw_block - Runtime hardware block data
 * @base: Virtual address of mapped register block
 * @is_phy: True if this block is accessed via MDIO (PHY), not MMIO
 * @is_c45: True if PHY uses C45 register access
 * @name: Block name from descriptor, used for UIO map naming (PHY blocks only)
 * @reg_list: Array of register data; offsets pre-populated at registration,
 *            values filled at capture time. Page-aligned for direct UIO mmap.
 * @num_registers: Total number of registers in this block
 * @num_captured: Number of registers actually captured
 * @num_skipped: Number of registers skipped due to out-of-bounds access
 * @map: Pointer to the original ethdbg_map structure, NULL for PHY blocks
 * @minidump_registered: (CONFIG_QCOM_MINIDUMP only) True when reg_list is
 *                       registered with minidump
 *
 * Represents a single hardware block (STMMAC, PCS, RGMII,
 * or SERDES) for panic register capture. During panic,
 * registers are read via base and stored in reg_list.
 * MDIO blocks (is_phy == true) are captured on demand via
 * phy_read()/phy_read_mmd() rather than direct MMIO reads.
 */
struct ethdbg_hw_block {
	void __iomem *base;
	bool is_phy;
	bool is_c45;
	const char *name;
	unsigned int num_registers;
	unsigned int num_captured;
	unsigned int num_skipped;
	struct ethdbg_map *map;
	struct ethdbg_reg_entry *reg_list;
#ifdef CONFIG_QCOM_MINIDUMP
	bool minidump_registered;
#endif
};

/**
 * struct ethdbg_dump_data - Capture dump data for an interface
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
 * @uio_info: UIO device for hardware MMIO (read/write path)
 * @dump_uio_info: UIO device for dump buffers
 * @dump_data: Capture dump data
 *
 * Created when a network device is detected and contains all
 * information needed to expose hardware registers via UIO.
 */
struct ethdbg_device {
	struct net_device *net_dev;
	void *netdev_priv;
	struct list_head map_regions;

	/* UIO device for hardware MMIO - UIO_MEM_PHYS slots (read/write path) */
	struct uio_info uio_info;

	/* UIO device for dump buffers - UIO_MEM_LOGICAL slots (dump path) */
	struct uio_info dump_uio_info;

	/* UIO device for PHY dump buffer */
	struct uio_info phy_uio_info;

	/* Dump structure for data capture */
	struct ethdbg_dump_data dump_data;

	/* Guard against double PHY registration */
	bool phy_registered;
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
int ethdbg_uio_add_phy(struct ethdbg_interface *iface);
void ethdbg_uio_del_phy(struct ethdbg_interface *iface);

int ethdbg_dump_register(struct ethdbg_device *dev, const char *interface_name);
int ethdbg_dump_register_phy(struct ethdbg_interface *iface);
void ethdbg_dump_unregister(struct ethdbg_interface *iface);

int ethdbg_panic_init(void);
void ethdbg_panic_deinit(void);
void ethdbg_dump_device(struct ethdbg_device *dev);
void ethdbg_dump_device_phy(struct ethdbg_device *dev);

#ifdef CONFIG_QCOM_MINIDUMP
int ethdbg_minidump_add_region(const char *name, uintptr_t vaddr, size_t size);
int ethdbg_minidump_remove_region(const char *name, uintptr_t vaddr, size_t size);
void ethdbg_minidump_register(struct ethdbg_device *dev,
			      const char *interface_name);
void ethdbg_minidump_unregister(struct ethdbg_device *dev,
				const char *interface_name);
void ethdbg_stmmac_minidump_register(struct ethdbg_device *dev,
				     const char *interface_name);
void ethdbg_stmmac_minidump_unregister(struct ethdbg_device *dev,
				       const char *interface_name);
#else
static inline int ethdbg_minidump_add_region(const char *name,
					     uintptr_t vaddr,
					     size_t size) { return 0; }
static inline int ethdbg_minidump_remove_region(const char *name,
						uintptr_t vaddr,
						size_t size) { return 0; }
static inline void ethdbg_minidump_register(struct ethdbg_device *dev,
					const char *interface_name) {}
static inline void ethdbg_minidump_unregister(struct ethdbg_device *dev,
					  const char *interface_name) {}
static inline void ethdbg_stmmac_minidump_register(struct ethdbg_device *dev,
					   const char *interface_name) {}
static inline void ethdbg_stmmac_minidump_unregister(struct ethdbg_device *dev,
					     const char *interface_name) {}
#endif /* CONFIG_QCOM_MINIDUMP */

#endif /* _ETHDBG_H_ */
