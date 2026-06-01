/* SPDX-License-Identifier: GPL-2.0-only*/
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#ifndef _ETHDBG_H_
#define _ETHDBG_H_
#include <linux/list.h>
#include <linux/uio_driver.h>
#include <linux/netdevice.h>
#include <linux/if.h>
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
 * struct ethdbg_device - Ethernet debug device information
 * @net_dev: Pointer to the associated network device
 * @map_regions: List head for memory regions to expose via UIO
 * @uio_info: UIO device information structure
 *
 * Created when a network device is detected and contains all
 * information needed to expose hardware registers via UIO.
 */
struct ethdbg_device {
	struct net_device *net_dev;
	struct list_head map_regions;
	/* UIO structure */
	struct uio_info uio_info;
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
#endif /* _ETHDBG_H_ */
