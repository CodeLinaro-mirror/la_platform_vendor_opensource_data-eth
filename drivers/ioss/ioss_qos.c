/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/inet.h>
#include <linux/of.h>

#include "include/linux/msm/ioss.h"
#include "include/linux/msm/ioss_qos.h"

#include "ioss_i.h"

static struct kobject* qos_kobj;
static struct kobject* qos_tc_params_kobj;

static struct IOSS_QOS_TABLE ioss_qos_table;
static struct IOSS_QOS_NEW_NODES ioss_qos_new_nodes;

static u16 get_num_arguments(char** buff, const char* delim)
{
	u16 len = 0;
	char* token;

	while ((token = strsep(buff, delim)))
		if (strlen(token)) len++;

	return len;
}

static bool is_valid_pcp(u8 pcp)
{
	return (pcp >= PCP_LOWER_LIMIT && pcp <= PCP_UPPER_LIMIT);
}

static bool is_valid_vlan_id(u16 vlan_id)
{
	return (vlan_id >= VLAN_LOWER_LIMIT && vlan_id <= VLAN_UPPER_LIMIT);
}

static int extract_ip_mask(char *src, struct qos_filters *addr)
{
	int i = 0;
	int ret = 0;
	char *token;

	while ( (token = strsep(&src, "/")) ) {
		pr_debug("%s : %s : %s\n", __func__, src, token);
		if (i > 1)
			return -1;
		if (i == 0) {
			ret = inet_pton_with_scope(&init_net, AF_UNSPEC, token, NULL, &(addr->address));
			if (ret) {
				ioss_dev_err(NULL, "[ioss qos] Invalid IP address entered\n");
				return -EINVAL;
			}
		}
		else {
			if (kstrtou8(token, 10, &(addr->mask_length)) < 0)
				return -EINVAL;
		}
		i++;
	}
	pr_debug("extracted address\n");
	addr->address.ss_family = AF_INET;

	return 0;
}

static int extract_proto_port(char *src, struct qos_filters *addr)
{
	char *token;

	/*
	* Currently, src = <Protocol>/<Port>]
	* Remove trailing ] and split into <Protocol> and <Port>
	*/
	src[strlen(src) - 1] = '\0';
	while ( (token = strsep(&src, "/")) ) {
		pr_debug("%s : %s : %s\n", __func__, src, token);
		if (isalpha(token[0])) {
			addr->proto = kstrdup(token, GFP_KERNEL);
		}
		else {
			if (kstrtou32(token, 10, &(addr->port_num)) < 0)
				return -EINVAL;
			if (addr->port_num < 1 || addr->port_num > 65535)
				return -EINVAL;
		}
	}

	return 0;
}

static int extract_qos_filters(char *src, struct qos_filters *addr)
{
	char *token;

	/*
	* Separate <IP>/<Mask>[<Protocol>/<Port>]
	* into <IP>/<Mask>  and  <Protocol>/<Port>]
	*/
	while ( (token = strsep(&src, "[")) ) {
		pr_debug("%s : %s : %s\n", __func__, src, token);
		if (0 == strlen(token)) // Fix for invalid ip
			continue;
		if (token[strlen(token) - 1] == ']') {
			if (extract_proto_port(token, addr))
				return -EINVAL;
		}
		else {
			if (extract_ip_mask(token, addr))
				return -EINVAL;
		}
	}

	return 0;
}

static bool rx_tc_already_exists(u8 prio)
{
	struct list_head *ptr;
    struct qos_routing_rx *entry;

    for (ptr = ioss_qos_table.qos_rx_pending_table.next; ptr != &ioss_qos_table.qos_rx_pending_table; ptr = ptr->next) {
        entry = to_qos_routing_rx(ptr);
        if (entry->tc_prio == prio)
            return true;
    }
    return false;
}

static bool tx_tc_already_exists(u8 prio)
{
	struct list_head *ptr;
    struct qos_routing_tx *entry;

    for (ptr = ioss_qos_table.qos_tx_pending_table.next; ptr != &ioss_qos_table.qos_tx_pending_table; ptr = ptr->next) {
        entry = to_qos_routing_tx(ptr);
        if (entry->tc_prio == prio)
            return true;
    }
    return false;
}

static void add_rx_tc_by_priority(struct qos_routing_rx *rx_node)
{
	struct list_head *ptr;
    struct qos_routing_rx *entry;

    for (ptr = ioss_qos_table.qos_rx_pending_table.next; ptr != &ioss_qos_table.qos_rx_pending_table; ptr = ptr->next) {
        entry = to_qos_routing_rx(ptr);
        if (entry->tc_prio > rx_node->tc_prio) {
            list_add_tail(&rx_node->node, ptr);
            return;
        }
    }
    list_add_tail(&rx_node->node, &ioss_qos_table.qos_rx_pending_table);
}

static void add_tx_tc_by_priority(struct qos_routing_tx *tx_node)
{
	struct list_head *ptr;
    struct qos_routing_tx *entry;

    for (ptr = ioss_qos_table.qos_tx_pending_table.next; ptr != &ioss_qos_table.qos_tx_pending_table; ptr = ptr->next) {
        entry = to_qos_routing_tx(ptr);
        if (entry->tc_prio > tx_node->tc_prio) {
            list_add_tail(&tx_node->node, ptr);
            return;
        }
    }
    list_add_tail(&tx_node->node, &ioss_qos_table.qos_tx_pending_table);
}

/* Utils End */

static ssize_t show_add_tc(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_add_tc(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	/*
	* Four options
	* 1. rx <prio>
	* 2. tx <prio>
	* 3. rx done
	* 4. tx done
	*/

	char *token;
	u8 prio = 0;
	u16 len = 0;
	size_t i = 0;
	char *tmp = NULL;
	bool is_dir_rx = false;
	bool add_to_list = false;
	char *dup = kstrdup(user_buf, GFP_KERNEL);
	char *buf = kstrdup(user_buf, GFP_KERNEL);

	tmp = dup;
	len = get_num_arguments(&dup, " ");
	kfree(tmp);

	if (len != 2)
		return -EINVAL;

	while ( (token = strsep(&buf, " ")) ) {
		if (0 == strlen(token))
			continue;

		if (i == 0) {
			if (!strncmp(token, "rx", 2))
				is_dir_rx = true;
			else if (!strncmp(token, "tx", 2))
				is_dir_rx = false;
			else
				goto add_err;
		}
		else {
			if (!strncmp(token, "done", 4))
				add_to_list = true;
			else if (kstrtou8(token, 10, &prio) < 0)
				goto add_err;
		}
		i++;
	}

	if (is_dir_rx) {
		if (add_to_list) {
			add_rx_tc_by_priority(ioss_qos_new_nodes.rx_node);
			ioss_qos_new_nodes.rx_node = NULL;
		}
		else {
			// Check if same prio already exists
			if (rx_tc_already_exists(prio)) {
				ioss_dev_err(NULL, "[ioss qos] : rx tc prio already exists");
				goto add_err;
			}
			ioss_qos_new_nodes.rx_node = kzalloc(sizeof(struct qos_routing_rx), GFP_KERNEL);
			ioss_qos_new_nodes.rx_node->tc_prio = prio;
		}
	}
	else {
		if (add_to_list) {
			add_tx_tc_by_priority(ioss_qos_new_nodes.tx_node);
			ioss_qos_new_nodes.tx_node = NULL;
		}
		else {
			// Check if same prio already exists
			if (tx_tc_already_exists(prio)) {
				ioss_dev_err(NULL, "[ioss qos] : tx tc prio already exists");
				goto add_err;
			}
			ioss_qos_new_nodes.tx_node = kzalloc(sizeof(struct qos_routing_tx), GFP_KERNEL);
			ioss_qos_new_nodes.tx_node->tc_prio = prio;
		}
	}

	return size;

add_err:
	ioss_dev_err(NULL, "[ioss qos] add tc failed\n");
	return -EINVAL;
}

static ssize_t show_qos_table(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_qos_table(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	return size;
}

static ssize_t show_del_tc(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_del_tc(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	return size;
}

static ssize_t show_commit(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_commit(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	struct device *parent = NULL;
	struct ioss_device *idev = NULL;
	struct ioss_driver *idrv = NULL;
	struct net_device *net_dev = NULL;
	struct ioss_interface *iface = NULL;

	u8 option;
	int ret = 0;
	struct response res;

	pr_debug("[ioss qos] : inside store_commit\n");
	if (kstrtou8(user_buf, 10, &option) < 0)
		return -EINVAL;
	if (option != 1)
		return -EINVAL;

	pr_debug("[ioss qos] : converting to parent device\n");
	parent = kobj_to_dev(dev->kobj.parent);

	pr_debug("[ioss qos] : checking if parent is not NULL\n");
	if (!parent)
		return -EINVAL;

	pr_debug("[ioss qos] : extracting parent netdev\n");
	net_dev = to_net_dev(parent);
	if (!net_dev)
		return -EINVAL;

	pr_debug("[ioss qos] : extracting iface\n");
	iface = ioss_netdev_to_iface(net_dev);
	if (!iface)
		return -EINVAL;

	pr_debug("[ioss qos] : extracting idev\n");
	idev = ioss_iface_dev(iface);
	if(!idev)
		return -EINVAL;

	pr_debug("[ioss qos] : extracting ioss driver\n");
	idrv = to_ioss_driver(idev->dev.driver);
	if (!idrv)
		return -EINVAL;

	// idev ops
	pr_debug("[ioss qos] : calling glue prepare qos function\n");
	if (idrv->qos_ops->prepare_qos) {
		res = idrv->qos_ops->prepare_qos(idev, &ioss_qos_table.qos_rx_pending_table, &ioss_qos_table.qos_tx_pending_table);
		ioss_dev_log(NULL, "[ioss qos]: glue returned response with err: %d, num_tx_pipes: %u, num_rx_pipes: %u",
					res.err, res.num_tx_pipes, res.num_rx_pipes);
	}

	pr_debug("[ioss qos] : calling glue request qos function\n");
	if (idrv->qos_ops->request_qos) {
		ret = idrv->qos_ops->request_qos(idev);
		ioss_dev_log(NULL, "[ioss qos]: request_qos returned %d", ret);
	}

	pr_debug("[ioss qos] : calling glue enable qos function\n");
	if (idrv->qos_ops->enable_qos) {
		ret = idrv->qos_ops->enable_qos(idev);
		ioss_dev_log(NULL, "[ioss qos]: enable_qos returned %d", ret);
	}

	return size;
}

static ssize_t show_action(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_action(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	char *token;
	u16 len = 0;
	size_t i = 0;
	char *tmp = NULL;
	bool is_dir_rx = false;
	bool is_action_sw = false;
	char *dup = kstrdup(user_buf, GFP_KERNEL);
	char *buf = kstrdup(user_buf, GFP_KERNEL);

	tmp = dup;
	len = get_num_arguments(&dup, " ");
	kfree(tmp);

	if (len != 2)
		return -EINVAL;

	while ( (token = strsep(&buf, " ")) ) {
		if (0 == strlen(token))
			continue;

		if (i == 0) {
			if (!strncmp(token, "rx", 2))
				is_dir_rx = true;
			else if (!strncmp(token, "tx", 2))
				is_dir_rx = false;
			else
				goto action_err;
		}
		else {
			if (!strncmp(token, "sw", 2))
				is_action_sw = true;
			else if (!strncmp(token, "hw", 2))
				is_action_sw = false;
			else
				goto action_err;
		}
		i++;
	}

	if (is_dir_rx && ioss_qos_new_nodes.rx_node) {
		if (is_action_sw)
			ioss_qos_new_nodes.rx_node->action = IOSS_QOS_SW_PATH;
		else
			ioss_qos_new_nodes.rx_node->action = IOSS_QOS_HW_PATH;
	}
	else if (!is_dir_rx && ioss_qos_new_nodes.tx_node) {
		if (is_action_sw)
			ioss_qos_new_nodes.tx_node->action = IOSS_QOS_SW_PATH;
		else
			ioss_qos_new_nodes.tx_node->action = IOSS_QOS_HW_PATH;
	}
	else {
		goto action_err;
	}

	return size;

action_err:
	ioss_dev_err(NULL, "[ioss qos] : invalid direction/action pair entered\n");
	return -EINVAL;
}

static ssize_t show_bw(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_bw(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	char *bw;
	int i = 0;
	u16 bw_val;
	u16 len = 0;
	char *tmp = NULL;
	char *dup = kstrdup(user_buf, GFP_KERNEL);
	char *buf = kstrdup(user_buf, GFP_KERNEL);

	tmp = dup;
	len = get_num_arguments(&dup, " :");
	kfree(tmp);

	if (!ioss_qos_new_nodes.tx_node)
		return -EINVAL;

	if (len != 2)
		return -EINVAL;

	while ( (bw = strsep(&buf, " :")) ) {
		if (0 == strlen(bw))
			continue;
		if (kstrtou16(bw, 10, &bw_val) < 0)
			return -EINVAL;
		if (i == 0)
			ioss_qos_new_nodes.tx_node->cbs_bw.low_bw = bw_val;
		else
			ioss_qos_new_nodes.tx_node->cbs_bw.high_bw = bw_val;
		i++;
	}

	return size;
}

static ssize_t show_pcp(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_pcp(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	char *pcp;
	int i = 0;
	u16 len = 0;
	char *tmp = NULL;
	char *dup = kstrdup(user_buf, GFP_KERNEL);
	char *buf = kstrdup(user_buf, GFP_KERNEL);

	tmp = dup;
	len = get_num_arguments(&dup, " ");
	kfree(tmp);

	if (!ioss_qos_new_nodes.rx_node)
		return -EINVAL;

	if (ioss_qos_new_nodes.rx_node->pcp.arr)
		kfree(ioss_qos_new_nodes.rx_node->pcp.arr);

	ioss_qos_new_nodes.rx_node->pcp.arr = kzalloc(sizeof(u8) * len, GFP_KERNEL);
	ioss_qos_new_nodes.rx_node->pcp.len = len;

	while ( (pcp = strsep(&buf, " ")) ) {
		if (0 == strlen(pcp))
			continue;
		if (kstrtou8(pcp, 10, &ioss_qos_new_nodes.rx_node->pcp.arr[i]) < 0)
			goto pcp_err;
		if (!is_valid_pcp(ioss_qos_new_nodes.rx_node->pcp.arr[i]))
			goto pcp_err;
		i++;
	}

	return size;

pcp_err:
	ioss_dev_err(NULL, "[ioss qos] : invalid pcp value entered\n");
	kfree(ioss_qos_new_nodes.rx_node->pcp.arr);
	return -EINVAL;
}

static ssize_t show_vlan_id(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_vlan_id(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	char *vid;
	int i = 0;
	u16 len = 0;
	char *tmp = NULL;
	char *dup = kstrdup(user_buf, GFP_KERNEL);
	char *buf = kstrdup(user_buf, GFP_KERNEL);

	tmp = dup;
	len = get_num_arguments(&dup, " ");
	kfree(tmp);

	if (!ioss_qos_new_nodes.rx_node)
		return -EINVAL;

	if (ioss_qos_new_nodes.rx_node->vlan_ids.arr)
		kfree(ioss_qos_new_nodes.rx_node->vlan_ids.arr);

	ioss_qos_new_nodes.rx_node->vlan_ids.arr = kzalloc(sizeof(u16) * len, GFP_KERNEL);
	ioss_qos_new_nodes.rx_node->vlan_ids.len = len;

	while ( (vid = strsep(&buf, " ")) ) {
		if (0 == strlen(vid))
			continue;
		if (kstrtou16(vid, 10, &ioss_qos_new_nodes.rx_node->vlan_ids.arr[i]) < 0)
			goto vlan_err;
		if (!is_valid_vlan_id(ioss_qos_new_nodes.rx_node->vlan_ids.arr[i]))
			goto vlan_err;
		i++;
	}

	return size;

vlan_err:
	ioss_dev_err(NULL, "[ioss qos] : invalid vlan id entered \n");
	kfree(ioss_qos_new_nodes.rx_node->vlan_ids.arr);
	return -EINVAL;
}


static ssize_t show_src(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_src(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	char *src;
	int i = 0;
	u16 len = 0;
	char *tmp = NULL;
	char *dup = kstrdup(user_buf, GFP_KERNEL);
	char *buf = kstrdup(user_buf, GFP_KERNEL);
	buf = strim(buf);

	tmp = dup;
	len = get_num_arguments(&dup, " ");
	kfree(tmp);

	if (!ioss_qos_new_nodes.rx_node)
		goto src_err;

	if (ioss_qos_new_nodes.rx_node->src.arr)
		kfree(ioss_qos_new_nodes.rx_node->src.arr);

	ioss_qos_new_nodes.rx_node->src.arr = kzalloc(sizeof(struct qos_filters) * len, GFP_KERNEL);
	ioss_qos_new_nodes.rx_node->src.len = len;

	while ( (src = strsep(&buf, " ")) ) {
		if (0 == strlen(src))
			continue;
		pr_debug("%s : %s", __func__, src);
		if (extract_qos_filters(src, &(ioss_qos_new_nodes.rx_node->src.arr[i])))
			goto src_err;
		i++;
	}

	return size;
src_err:
	if (ioss_qos_new_nodes.rx_node && ioss_qos_new_nodes.rx_node->src.arr)
		kfree(ioss_qos_new_nodes.rx_node->src.arr);
	return -EINVAL;
}

static ssize_t show_dst(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_dst(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	char *src;
	int i = 0;
	u16 len = 0;
	char *tmp = NULL;
	char *dup = kstrdup(user_buf, GFP_KERNEL);
	char *buf = kstrdup(user_buf, GFP_KERNEL);

	buf = strim(buf);

	tmp = dup;
	len = get_num_arguments(&dup, " ");
	kfree(tmp);

	if (!ioss_qos_new_nodes.rx_node)
		goto dst_err;

	if (ioss_qos_new_nodes.rx_node->dst.arr)
		kfree(ioss_qos_new_nodes.rx_node->dst.arr);

	ioss_qos_new_nodes.rx_node->dst.arr = kzalloc(sizeof(struct qos_filters) * len, GFP_KERNEL);
	ioss_qos_new_nodes.rx_node->dst.len = len;

	while ( (src = strsep(&buf, " ")) ) {
		if (0 == strlen(src))
			continue;
		pr_debug("%s : %s", __func__, src);
		if (extract_qos_filters(src, &(ioss_qos_new_nodes.rx_node->dst.arr[i])))
			goto dst_err;
		i++;
	}

	return size;
dst_err:
	if (ioss_qos_new_nodes.rx_node && ioss_qos_new_nodes.rx_node->dst.arr)
		kfree(ioss_qos_new_nodes.rx_node->dst.arr);
	return -EINVAL;
}

static ssize_t show_smac(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_smac(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	char *mac;
	u16 i = 0;
	u16 len = 0;
	char *tmp = NULL;
	char *dup = kstrdup(user_buf, GFP_KERNEL);
	char *buf = kstrdup(user_buf, GFP_KERNEL);

	tmp = dup;
	len = get_num_arguments(&dup, " ");
	kfree(tmp);

	if (!ioss_qos_new_nodes.rx_node)
		goto smac_err;

	if (ioss_qos_new_nodes.rx_node->smac.arr)
		kfree(ioss_qos_new_nodes.rx_node->smac.arr);

	ioss_qos_new_nodes.rx_node->smac.arr = kzalloc(sizeof(u8[ETH_ALEN]) * len, GFP_KERNEL);
	ioss_qos_new_nodes.rx_node->smac.len = len;

	while ( (mac = strsep(&buf, " ")) ) {
		if (0 == strlen(mac))
			continue;
		if (!mac_pton(mac, ioss_qos_new_nodes.rx_node->smac.arr[i])) {
			ioss_dev_err(NULL, "[ioss qos] : invalid smac address entered\n");
			goto smac_err;
		}
		i++;
	}

	return size;

smac_err:
	if (ioss_qos_new_nodes.rx_node && ioss_qos_new_nodes.rx_node->smac.arr)
		kfree(ioss_qos_new_nodes.rx_node->smac.arr);
	return -EINVAL;
}

static ssize_t show_dmac(struct device *dev,
		struct device_attribute *attr, char *user_buf)
{
	return 0;
}

static ssize_t store_dmac(struct device *dev,
		struct device_attribute *attr, const char *user_buf, size_t size)
{
	char *mac;
	u16 i = 0;
	u16 len = 0;
	char *tmp = NULL;
	char *dup = kstrdup(user_buf, GFP_KERNEL);
	char *buf = kstrdup(user_buf, GFP_KERNEL);

	tmp = dup;
	len = get_num_arguments(&dup, " ");
	kfree(tmp);

	if (!ioss_qos_new_nodes.rx_node)
		goto dmac_err;

	if (ioss_qos_new_nodes.rx_node->dmac.arr)
		kfree(ioss_qos_new_nodes.rx_node->dmac.arr);

	ioss_qos_new_nodes.rx_node->dmac.arr = kzalloc(sizeof(u8[ETH_ALEN]) * len, GFP_KERNEL);
	ioss_qos_new_nodes.rx_node->dmac.len = len;

	while ( (mac = strsep(&buf, " ")) ) {
		if (0 == strlen(mac))
			continue;
		if (!mac_pton(mac, ioss_qos_new_nodes.rx_node->dmac.arr[i])) {
			ioss_dev_err(NULL, "[ioss qos] : invalid dmac address entered\n");
			goto dmac_err;
		}
		i++;
	}

	return size;

dmac_err:
	if (ioss_qos_new_nodes.rx_node && ioss_qos_new_nodes.rx_node->dmac.arr)
		kfree(ioss_qos_new_nodes.rx_node->dmac.arr);
	return -EINVAL;
}

static DEVICE_ATTR(add_tc, S_IRWXU | S_IRUGO | S_IRWXG,
		show_add_tc, store_add_tc);
static DEVICE_ATTR(qos_table, S_IRWXU | S_IRUGO | S_IRWXG,
		show_qos_table, store_qos_table);
static DEVICE_ATTR(del_tc, S_IRWXU | S_IRUGO | S_IRWXG,
		show_del_tc, store_del_tc);
static DEVICE_ATTR(commit, S_IRWXU | S_IRUGO | S_IRWXG,
		show_commit, store_commit);

static DEVICE_ATTR(vlan_id, S_IRWXU | S_IRUGO | S_IRWXG,
		show_vlan_id, store_vlan_id);
static DEVICE_ATTR(pcp, S_IRWXU | S_IRUGO | S_IRWXG,
		show_pcp, store_pcp);
static DEVICE_ATTR(src, S_IRWXU | S_IRUGO | S_IRWXG,
		show_src, store_src);
static DEVICE_ATTR(dst, S_IRWXU | S_IRUGO | S_IRWXG,
		show_dst, store_dst);
static DEVICE_ATTR(bw, S_IRWXU | S_IRUGO | S_IRWXG,
		show_bw, store_bw);
static DEVICE_ATTR(action, S_IRWXU | S_IRUGO | S_IRWXG,
		show_action, store_action);
static DEVICE_ATTR(smac, S_IRWXU | S_IRUGO | S_IRWXG,
		show_smac, store_smac);
static DEVICE_ATTR(dmac, S_IRWXU | S_IRUGO | S_IRWXG,
		show_dmac, store_dmac);

int create_qos_sysfs_nodes(struct device *dev) {
	int ret;
	struct ioss_device *idev = to_ioss_device(dev);

	INIT_LIST_HEAD(&ioss_qos_table.qos_rx_pending_table);
	INIT_LIST_HEAD(&ioss_qos_table.qos_rx_committed_table);
	INIT_LIST_HEAD(&ioss_qos_table.qos_tx_pending_table);
	INIT_LIST_HEAD(&ioss_qos_table.qos_tx_committed_table);

	qos_kobj = kobject_create_and_add("qos", &idev->net_dev->dev.kobj);
	if (!qos_kobj) {
		ioss_dev_err(idev, "Unable to create qos kobject");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_kobj, &dev_attr_add_tc.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create add_tc node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_kobj, &dev_attr_qos_table.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create qos-table node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_kobj, &dev_attr_del_tc.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create del_tc node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_kobj, &dev_attr_commit.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create commit node");
		goto err_qos_sysfs;
	}

	qos_tc_params_kobj = kobject_create_and_add("add_tc_params", qos_kobj);
	if (!qos_tc_params_kobj) {
		ioss_dev_err(idev, "Unable to create qos-add_tc_params kobject");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_tc_params_kobj, &dev_attr_vlan_id.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create add_tc_params/vlan_id node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_tc_params_kobj, &dev_attr_pcp.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create add_tc_params/pcp node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_tc_params_kobj, &dev_attr_src.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create add_tc_params/src node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_tc_params_kobj, &dev_attr_dst.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create add_tc_params/dst node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_tc_params_kobj, &dev_attr_bw.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create add_tc_params/bw node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_tc_params_kobj, &dev_attr_action.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create add_tc_params/action node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_tc_params_kobj, &dev_attr_smac.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create add_tc_params/smac node");
		goto err_qos_sysfs;
	}

	ret = sysfs_create_file(qos_tc_params_kobj, &dev_attr_dmac.attr);
	if (ret) {
		ioss_dev_err(idev, "unable to create add_tc_params/dmac node");
		goto err_qos_sysfs;
	}

	return 0;

err_qos_sysfs:
	kobject_put(qos_kobj);
	kobject_put(qos_tc_params_kobj);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(create_qos_sysfs_nodes);


void remove_qos_sysfs_nodes(struct device *dev)
{
	kobject_del(qos_kobj);
	kobject_del(qos_tc_params_kobj);
	kobject_put(qos_kobj);
	kobject_put(qos_tc_params_kobj);
}
EXPORT_SYMBOL_GPL(remove_qos_sysfs_nodes);
