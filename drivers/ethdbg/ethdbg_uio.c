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

static void eth_uio_module_release(struct device *dev)
{
}

static void free_dump_map_names(struct ethdbg_device *dev)
{
	int i;

	for (i = 0; i < dev->dump_data.num_blocks && i < MAX_UIO_MAPS; i++) {
		kfree(dev->dump_uio_info.mem[i].name);
		dev->dump_uio_info.mem[i].name = NULL;
	}
}

static int set_map_info(struct ethdbg_device *dev)
{
	struct ethdbg_dump_data *dump_data = &dev->dump_data;
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

		dev->dump_uio_info.mem[i].name = kasprintf(GFP_KERNEL, "dump:%s", map->name);
		if (!dev->dump_uio_info.mem[i].name) {
			while (i-- > 0) {
				kfree(dev->dump_uio_info.mem[i].name);
			}
			return -ENOMEM;
		}

		dev->dump_uio_info.mem[i].addr = (phys_addr_t)(uintptr_t)dump_data->blocks[i].reg_list;
		dev->dump_uio_info.mem[i].size = dump_data->blocks[i].num_registers * sizeof(struct ethdbg_reg_entry);
		dev->dump_uio_info.mem[i].memtype = UIO_MEM_LOGICAL;

		i++;
	}

	return 0;
}

/* ethdbg_uio_open */
static int ethdbg_uio_open(struct uio_info *info, struct inode *inode)
{
	struct ethdbg_device *dev = container_of(info, struct ethdbg_device, dump_uio_info);

	ethdbg_dump_device(dev);

	return 0;
}

int ethdbg_uio_add(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev;
	int ret;

	dev = iface->device;

	dev->uio_info.name = dev->net_dev->name;
	dev->uio_info.version = ETHDBG_UIO_VERSION;

	dev->dump_uio_info.name = dev->net_dev->name;
	dev->dump_uio_info.version = ETHDBG_UIO_VERSION;
	dev->dump_uio_info.open = ethdbg_uio_open;

	ret = set_map_info(dev);
	if (ret < 0)
		return ret;

	ret = uio_register_device(&dev->net_dev->dev, &dev->uio_info);
	if (ret < 0) {
		pr_err("Failed to register uio device\n");
		free_dump_map_names(dev);
		return -ENODEV;
	}

	dev_info(&dev->uio_info.uio_dev->dev, "Registered UIO device for interface %s\n",
		 iface->interface_name);

	ret = uio_register_device(&dev->net_dev->dev, &dev->dump_uio_info);
	if (ret < 0) {
		pr_err("ethdbg: failed to register dump UIO device for %s\n",
		       dev->net_dev->name);
		goto err_unregister_uio;
	}

	dev_info(&dev->dump_uio_info.uio_dev->dev,
		 "Registered dump UIO device %s\n", dev->dump_uio_info.name);
	return 0;

err_unregister_uio:
	free_dump_map_names(dev);
	uio_unregister_device(&dev->uio_info);
	return ret;
}


void ethdbg_uio_del(struct ethdbg_interface *iface)
{
	struct ethdbg_device *dev;
	struct ethdbg_map *map, *tmp;

	if (!iface || !iface->device)
		return;

	dev = iface->device;

	if (dev->dump_uio_info.uio_dev) {
		pr_info("ethdbg: [%s] unregistering dump UIO device\n", iface->interface_name);
		uio_unregister_device(&dev->dump_uio_info);
	}

	free_dump_map_names(dev);

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
