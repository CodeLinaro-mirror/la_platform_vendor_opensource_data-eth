/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/sysfs.h>

#include "ioss_i.h"

#define IDEV_SYSFS_DEV_ATTR_PERMS 0644
#define CH_SYSFS_DEV_ATTR_PERMS 0644

static struct kobject* root_kobj;

int ioss_sysfs_init(struct ioss_device *idev)
{
	const char *name = kobject_name(&idev->net_dev->dev.kobj);

	if (!idev)
		return -EINVAL;

	idev->root_kobj = kobject_create_and_add("ioss", &idev->net_dev->dev.kobj);
	if (!idev->root_kobj) {
		ioss_log_err(NULL, "Failed to create root  sysfs directory for IOSS");
		return -EFAULT;
	}

	name = kobject_name(idev->root_kobj);

	idev->dev_kobj = kobject_create_and_add("devices", idev->root_kobj);
	if (!idev->dev_kobj) {
		ioss_log_err(NULL, "Failed to create devices sysfs directory for IOSS");
		goto fail;
	}
	root_kobj = idev->root_kobj;

	return 0;

fail:
	if(idev->root_kobj){
		kobject_del(idev->root_kobj);
		kobject_put(idev->root_kobj);
        }
	idev->root_kobj = NULL;
	return -EFAULT;
}

void ioss_sysfs_exit(struct ioss_device *idev)
{
	if(idev->dev_kobj){
		kobject_del(idev->dev_kobj);
		kobject_put(idev->dev_kobj);
        }
	if(idev->root_kobj){
		kobject_del(idev->root_kobj);
		kobject_put(idev->root_kobj);
        }

	root_kobj = NULL;
}

static int get_idev_statistics(struct ioss_device *idev,
		struct ioss_device_stats *stats)
{
	int rc;
	struct ioss_channel *ch;
	struct ioss_interface *iface = &idev->interface;
	struct rtnl_link_stats64 netdev_stats;

	memset(stats, 0, sizeof(struct ioss_device_stats));

	/* Fetch EMAC level statistics */
	rc = ioss_dev_op(idev, get_device_statistics, idev, stats);
	if (rc) {
		ioss_dev_err(idev, "Failed to get device statistics");
		return -EFAULT;
	}

	/* Aggregate channel level stats */
	ioss_for_each_channel(ch, iface) {
		struct ioss_channel_stats ch_stats;

		memset(&ch_stats, 0, sizeof(struct ioss_channel_stats));

		if (ioss_dev_op(idev, get_channel_statistics, ch, &ch_stats)) {
			ioss_dev_err(idev, "Failed to get channel statistics");
			return -EFAULT;
		}

		if (ch->direction == IOSS_CH_DIR_RX) {
			stats->hwp_rx_errors += ch_stats.overflow_error +
					ch_stats.underflow_error;
		}

		if (ch->direction == IOSS_CH_DIR_TX) {
			stats->hwp_tx_errors += ch_stats.overflow_error +
					ch_stats.underflow_error;
		}
	}

	/* Fetch Linux netdev stats */
	memset(&netdev_stats, 0, sizeof(struct rtnl_link_stats64));
	dev_get_stats(ioss_iface_to_netdev(iface), &netdev_stats);

	stats->exp_rx_packets += iface->exception_stats.rx_packets;
	stats->exp_rx_bytes += iface->exception_stats.rx_bytes;

	if (stats->emac_rx_packets)
		stats->hwp_rx_packets = stats->emac_rx_packets +
				stats->exp_rx_packets - netdev_stats.rx_packets;
	if (stats->emac_tx_packets)
		stats->hwp_tx_packets = stats->emac_tx_packets +
				stats->exp_tx_packets - netdev_stats.tx_packets;
	if (stats->emac_rx_bytes)
		stats->hwp_rx_bytes = stats->emac_rx_bytes +
				stats->exp_rx_bytes - netdev_stats.rx_bytes;
	if (stats->emac_tx_bytes)
		stats->hwp_tx_bytes = stats->emac_tx_bytes +
				stats->exp_tx_bytes - netdev_stats.tx_bytes;
	stats->hwp_rx_drops = stats->emac_rx_drops;

	return 0;
}

static ssize_t read_idev_statistic(struct device *dev, struct device_attribute *attr, char *user_buf)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	struct device *parent = kobj_to_dev(root_kobj->parent);
	struct net_device *netdev=NULL;
	struct ioss_device *idev = NULL;
	struct ioss_interface *iface = NULL;
	struct ioss_device_stats dev_stats;

	netdev = to_net_dev(parent);
	if (!netdev) {
		pr_err("netdev is NULL\n");
		return -EINVAL;
	}

	iface = ioss_netdev_to_iface(netdev);
	if (!iface)
		return -EINVAL;

	idev = ioss_iface_dev(iface);
	if(!idev)
		return -EINVAL;

	if (get_idev_statistics(idev, &dev_stats)) {
		ioss_dev_err(idev, "Failed to get idev statistics");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_rx_packets",
			  dev_stats.hwp_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_tx_packets",
			 dev_stats.hwp_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_rx_bytes",
			 dev_stats.hwp_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_tx_bytes",
			 dev_stats.hwp_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_rx_errors",
			 dev_stats.hwp_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_tx_errors",
			 dev_stats.hwp_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_rx_drops",
			 dev_stats.hwp_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_tx_drops",
			 dev_stats.hwp_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_rx_packets",
			 dev_stats.exp_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_tx_packets",
			 dev_stats.exp_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_rx_bytes",
			 dev_stats.exp_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_tx_bytes",
			 dev_stats.exp_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_rx_errors",
			 dev_stats.exp_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_tx_errors",
			 dev_stats.exp_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_rx_drops",
			 dev_stats.exp_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_tx_drops",
			 dev_stats.exp_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_packets",
			 dev_stats.emac_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_packets",
			 dev_stats.emac_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_bytes",
			 dev_stats.emac_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_bytes",
			 dev_stats.emac_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_errors",
			 dev_stats.emac_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_errors",
			 dev_stats.emac_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_drops",
			 dev_stats.emac_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_drops",
			 dev_stats.emac_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_pause_frames",
			 dev_stats.emac_rx_pause_frames);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_pause_frames",
			 dev_stats.emac_tx_pause_frames);

	memcpy(user_buf, buf, len);
	kfree(buf);

	return len;
}

DEVICE_ATTR(statistics, IDEV_SYSFS_DEV_ATTR_PERMS , read_idev_statistic,  NULL);

static ssize_t read_idev_stats(struct device *dev, struct device_attribute *attr, char *user_buf)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	struct device *parent = kobj_to_dev(root_kobj->parent);
	struct net_device *netdev=NULL;
	struct ioss_device *idev = NULL;
	struct ioss_interface *iface = NULL;
	struct ioss_device_stats dev_stats;

	netdev = to_net_dev(parent);
	if (!netdev) {
		pr_err("netdev is NULL\n");
		return -EINVAL;
	}

	iface = ioss_netdev_to_iface(netdev);
	if (!iface)
		return -EINVAL;

	idev = ioss_iface_dev(iface);
	if(!idev)
		return -EINVAL;

	if (get_idev_statistics(idev, &dev_stats)) {
		ioss_dev_err(idev, "Failed to get idev statistics");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_pause_frames);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu", dev_stats.emac_tx_pause_frames);
	len += scnprintf(buf + len, BUF_LEN - len, "\n");

	memcpy(user_buf, buf, len);
	kfree(buf);

	return len;
}
DEVICE_ATTR(stats, IDEV_SYSFS_DEV_ATTR_PERMS, read_idev_stats,  NULL);

static ssize_t read_ch_statistics(struct device *dev, struct device_attribute *attr, char *user_buf)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	struct ioss_channel_stats ch_stats;
	struct ioss_channel *ch=NULL, *ch_tmp;
	struct device *parent = kobj_to_dev(root_kobj->parent);
	const char *name = kobject_name(&dev->kobj);
	char *ch_name = kstrdup(name, GFP_KERNEL);
	struct net_device *netdev=NULL;
	struct ioss_device *idev = NULL;
	struct ioss_interface *iface = NULL;
	char *dir = strsep(&ch_name, "-");
	int id= 0;

	netdev = to_net_dev(parent);
	if (!netdev) {
		pr_err("netdev is NULL\n");
		return -EINVAL;
	}

	iface = ioss_netdev_to_iface(netdev);
	if (!iface)
		return -EINVAL;

	idev = ioss_iface_dev(iface);
	if(!idev)
		return -EINVAL;

	if(ch_name)
		sscanf(ch_name, "%d", &id);

	ioss_for_each_channel(ch_tmp, iface) {
		char *dir_tmp = (ch_tmp->direction == IOSS_CH_DIR_RX) ? "rx" : "tx";
		if(!strncmp(dir_tmp, dir, 2) && ch_tmp->id == id){
			ch = ch_tmp;
			break;
		}
	}
	if(!ch){
		ioss_dev_err(idev, "Failed to get channel");
		return -EFAULT;
	}

	memset(&ch_stats, 0, sizeof(struct ioss_channel_stats));

	if (ioss_dev_op(idev, get_channel_statistics, ch, &ch_stats)) {
		ioss_dev_err(idev, "Failed to get channel statistics");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "overflow_errors",
			 ch_stats.overflow_error);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "underflow_errors",
			 ch_stats.underflow_error);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "desc_unavail",
			 ch_stats.desc_unavail);

	memcpy(user_buf, buf, len);
	kfree(buf);

	return len;
}
DEVICE_ATTR(ch_statistics, CH_SYSFS_DEV_ATTR_PERMS, read_ch_statistics,  NULL);

static ssize_t read_ch_stats(struct device *dev, struct device_attribute *attr, char *user_buf)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	struct ioss_channel_stats ch_stats;
	struct ioss_channel *ch=NULL, *ch_tmp;
	struct device *parent = kobj_to_dev(root_kobj->parent);
	const char *name = kobject_name(&dev->kobj);
	char *ch_name = kstrdup(name, GFP_KERNEL);
	struct net_device *netdev=NULL;
	struct ioss_device *idev = NULL;
	struct ioss_interface *iface = NULL;
	char *dir = strsep(&ch_name, "-");
	int id= 0;

	netdev = to_net_dev(parent);
	if (!netdev) {
		pr_err("netdev is NULL\n");
		return -EINVAL;
	}

	iface = ioss_netdev_to_iface(netdev);
	if (!iface)
		return -EINVAL;

	idev = ioss_iface_dev(iface);
	if(!idev)
		return -EINVAL;

	if(ch_name)
		sscanf(ch_name, "%d", &id);

	ioss_for_each_channel(ch_tmp, iface) {
	char *dir_tmp = (ch_tmp->direction == IOSS_CH_DIR_RX) ? "rx" : "tx";
		if(!strncmp(dir_tmp, dir, 2) && ch_tmp->id == id){
			ch = ch_tmp;
			break;
		}
	}
	if(!ch){
		ioss_dev_err(idev, "Failed to get channel");
		return -EFAULT;
	}

	memset(&ch_stats, 0, sizeof(struct ioss_channel_stats));

	if (ioss_dev_op(idev, get_channel_statistics, ch, &ch_stats)) {
		ioss_dev_err(idev, "Failed to get channel stats");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_stats.overflow_error);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_stats.underflow_error);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_stats.desc_unavail);
	len += scnprintf(buf + len, BUF_LEN - len, "\n");

	memcpy(user_buf, buf, len);
	kfree(buf);

	return len;
}
DEVICE_ATTR(ch_stats, CH_SYSFS_DEV_ATTR_PERMS, read_ch_stats,  NULL);

static ssize_t read_ch_status(struct device *dev, struct device_attribute *attr, char *user_buf)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	struct ioss_channel *ch=NULL, *ch_tmp;
	struct device *parent = kobj_to_dev(root_kobj->parent);
	const char *name = kobject_name(&dev->kobj);
	char *ch_name = kstrdup(name, GFP_KERNEL);
	struct net_device *netdev=NULL;
	struct ioss_device *idev = NULL;
	struct ioss_interface *iface = NULL;
	struct ioss_channel_status ch_status;
	char *dir = strsep(&ch_name, "-");
	int id= 0;

	netdev = to_net_dev(parent);
	if (!netdev) {
		pr_err("netdev is NULL\n");
		return -EINVAL;
	}

 	iface = ioss_netdev_to_iface(netdev);
  	if (!iface)
  		return -EINVAL;

  	idev = ioss_iface_dev(iface);
  	if(!idev)
  		return -EINVAL;

	if(ch_name)
		sscanf(ch_name, "%d", &id);

	memset(&ch_status, 0, sizeof(struct ioss_channel_status));

	ioss_for_each_channel(ch_tmp, iface) {
		char *dir_tmp = (ch_tmp->direction == IOSS_CH_DIR_RX) ? "rx" : "tx";
		if(!strncmp(dir_tmp, dir, 2) && ch_tmp->id == id){
			ch = ch_tmp;
			break;
		}
	}
	if(!ch){
		ioss_dev_err(idev, "Failed to get channel");
		return -EFAULT;
	}

	if (ioss_dev_op(idev, get_channel_status, ch, &ch_status)) {
		ioss_dev_err(idev, "Failed to get channel status");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%s: %d\n", "enabled", ch_status.enabled);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "size", ch_status.ring_size);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "interrupt_modc",
			 ch_status.interrupt_modc);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu ns\n", "interrupt_modt",
			 ch_status.interrupt_modt);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "head_ptr", ch_status.head_ptr);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "tail_ptr", ch_status.tail_ptr);

	memcpy(user_buf, buf, len);
	kfree(buf);
	return len;
}
DEVICE_ATTR(ch_status, CH_SYSFS_DEV_ATTR_PERMS, read_ch_status,  NULL);

static ssize_t read_ch_stat(struct device *dev, struct device_attribute *attr, char *user_buf)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	struct ioss_channel_status ch_status;
	struct ioss_channel *ch=NULL, *ch_tmp;
	struct device *parent = kobj_to_dev(root_kobj->parent);
	const char *name = kobject_name(&dev->kobj);
	char *ch_name = kstrdup(name, GFP_KERNEL);
	struct net_device *netdev=NULL;
	struct ioss_device *idev = NULL;
	struct ioss_interface *iface = NULL;
	char *dir = strsep(&ch_name, "-");
	int id= 0;

	netdev = to_net_dev(parent);
	if (!netdev) {
		pr_err("netdev is NULL\n");
		return -EINVAL;
	}

	iface = ioss_netdev_to_iface(netdev);
	if (!iface)
		return -EINVAL;

	idev = ioss_iface_dev(iface);
	if(!idev)
		return -EINVAL;

	if(ch_name)
		sscanf(ch_name, "%d", &id);

	ioss_for_each_channel(ch_tmp, iface) {
	char *dir_tmp = (ch_tmp->direction == IOSS_CH_DIR_RX) ? "rx" : "tx";
		if(!strncmp(dir_tmp, dir, 2) && ch_tmp->id == id){
			ch = ch_tmp;
			break;
		}
	}

	if(!ch){
		ioss_dev_err(idev, "Failed to get channel");
		return -EFAULT;
	}

	memset(&ch_status, 0, sizeof(struct ioss_channel_status));

	if (ioss_dev_op(idev, get_channel_status, ch, &ch_status)) {
		ioss_dev_err(idev, "Failed to get channel status");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%d ", ch_status.enabled);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_status.ring_size);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_status.interrupt_modc);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_status.interrupt_modt);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_status.head_ptr);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu", ch_status.tail_ptr);
	len += scnprintf(buf + len, BUF_LEN - len, "\n");

	memcpy(user_buf, buf, len);
	kfree(buf);

	return len;
}
DEVICE_ATTR(ch_stat, CH_SYSFS_DEV_ATTR_PERMS, read_ch_stat,  NULL);

int ioss_sysfs_add_idev(struct ioss_device *idev)
{
	int ret;

	//idev->kobj to access eth0 from ioss/devices for channel instead of net
	idev->kobj = kobject_create_and_add(idev->net_dev->name, idev->dev_kobj);
	if (!idev->kobj) {
		ioss_dev_err(idev, "Failed to create %s sysfs directory", idev->net_dev->name);
		return -EFAULT;
	}

	ret = sysfs_create_file(idev->kobj, &dev_attr_statistics.attr);
	if (ret) {
		ioss_dev_err(idev, "Failed to create sysfs file for %s", idev->net_dev->name);
		goto err_sysfs;
	}
	ret = sysfs_create_file(idev->kobj, &dev_attr_stats.attr);
	if (ret) {
		ioss_dev_err(idev, "Failed to create sysfs file for %s", idev->net_dev->name);
		goto err_sysfs;
	}

	return 0;

err_sysfs:
	if(idev->kobj){
		kobject_del(idev->kobj);
		kobject_put(idev->kobj);
	}
	idev->kobj = NULL;
	return -EFAULT;
}

void ioss_sysfs_remove_idev(struct ioss_device *idev)
{
	if(idev->kobj){
		kobject_del(idev->kobj);
		kobject_put(idev->kobj);
	}
	idev->kobj = NULL;
}

int ioss_sysfs_add_channel(struct ioss_channel *ch)
{
	char dir_name[32];
	struct ioss_device *idev = ioss_ch_dev(ch);
	int ret;

	snprintf(dir_name, sizeof(dir_name), "%s-%d",
		 ((ch->direction == IOSS_CH_DIR_RX) ? "rx" : "tx"), ch->id);

	ch->kobj = kobject_create_and_add(dir_name, idev->kobj);
	if (!ch->kobj) {
		ioss_dev_err(idev, "Failed to create %s sysfs directory", dir_name);
		return -EFAULT;
	}

	ret = sysfs_create_file(ch->kobj, &dev_attr_ch_statistics.attr);
	if (ret) {
		ioss_dev_err(idev, "Failed to create statistics sysfs file for %s", dir_name);
		goto err_sysfs;
	}

	ret = sysfs_create_file(ch->kobj, &dev_attr_ch_stats.attr);
	if (ret) {
		ioss_dev_err(idev, "Failed to create stats sysfs file for %s", dir_name);
		goto err_sysfs;
	}

	ret = sysfs_create_file(ch->kobj, &dev_attr_ch_status.attr);
	if (ret) {
		ioss_dev_err(idev, "Failed to create status sysfs file for %s", dir_name);
		goto err_sysfs;
	}


	ret = sysfs_create_file(ch->kobj, &dev_attr_ch_stat.attr);
	if (ret) {
		ioss_dev_err(idev, "Failed to create stat sysfs file for %s", dir_name);
		goto err_sysfs;
	}

	return 0;

err_sysfs:
	if(ch->kobj){
		kobject_del(ch->kobj);
		kobject_put(ch->kobj);
	}
	ch->kobj = NULL;
	return -EFAULT;
}

void ioss_sysfs_remove_channel(struct ioss_channel *ch)
{
	if(ch->kobj){
		kobject_del(ch->kobj);
		kobject_put(ch->kobj);
	}
	ch->kobj = NULL;
}

