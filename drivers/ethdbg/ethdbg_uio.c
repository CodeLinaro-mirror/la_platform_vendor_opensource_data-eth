// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/uio_driver.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include "ethdbg.h"

#define ETHDBG_UIO_VERSION "1.0.0"

static void set_map_info(struct ethdbg_device *dev)
{
	struct ethdbg_map *map;
	int i = 0;

	list_for_each_entry(map, &dev->map_regions, list) {
		if (i >= MAX_UIO_MAPS) {
			pr_warn("Exceeded MAX_UIO_MAPS limit, some regions will not be mapped\n");
			break;
		}
		dev->uio_info.mem[i].name = map->name;
		dev->uio_info.mem[i].addr = roundup((phys_addr_t)map->base_addr,
							PAGE_SIZE);
		dev->uio_info.mem[i].size = roundup((resource_size_t)map->size,
							PAGE_SIZE);
		dev->uio_info.mem[i].memtype = UIO_MEM_PHYS;
		i++;
	}
}

int ethdbg_uio_add(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev;
	int ret;

	dev = iface->device;

	dev->uio_info.name = dev->net_dev->name;
	dev->uio_info.version = ETHDBG_UIO_VERSION;

	set_map_info(dev);

	ret = uio_register_device(&dev->net_dev->dev, &dev->uio_info);
	if (ret < 0) {
		pr_err("Failed to register uio device\n");
		return -ENODEV;
	}

	dev_info(&dev->uio_info.uio_dev->dev, "Registered UIO device for interface %s\n",
		 iface->interface_name);
	return 0;
}


void ethdbg_uio_del(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev;
	struct ethdbg_map *map, *tmp;

	if (!iface || !iface->device)
		return;

	dev = iface->device;

	if (dev->uio_info.uio_dev) {
		dev_info(&dev->uio_info.uio_dev->dev, "Unregistering UIO device for interface %s\n",
			 iface->interface_name);
		uio_unregister_device(&dev->uio_info);
	}

	/* Free map regions list */
	list_for_each_entry_safe(map, tmp, &dev->map_regions, list) {
		list_del(&map->list);
		kfree(map);
	}
}

MODULE_LICENSE("GPL");
