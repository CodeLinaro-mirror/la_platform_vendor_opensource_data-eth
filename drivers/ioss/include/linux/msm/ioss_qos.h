/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IOSS_QOS_H_
#define _IOSS_QOS_H_

#include <linux/list.h>
#include <linux/socket.h>
#include <linux/if_ether.h>
#include <linux/ctype.h>

#include "ioss.h"

/**
 *   API
 * Version    Changes
 * ---------------------------------------------------------------------------
 *   1      - Initial version
 *   2      - Optimize IPA connects and improve logging
 */

#define IOSS_QOS_API_VER 2
#define IOSS_QOS_SUBSYS "eth_qos"

#define __ioss_qos_log_msg(ipcbuf, fmt, args...) \
	do { \
		void *__buf = (ipcbuf); \
		if (__buf) \
			ipc_log_string(__buf, " %s:%d " fmt "\n", \
					__func__, __LINE__, ## args); \
	} while (0)

#define ioss_qos_log_err(dev, fmt, args...) \
	do { \
		void *ioss_qos_get_ipclog_buf(void); \
		dev_err(dev, IOSS_QOS_SUBSYS ":ERR:" fmt "\n", ##args); \
		__ioss_qos_log_msg(ioss_qos_get_ipclog_buf(), \
					"ERR:" fmt, ## args); \
	} while (0)

#define ioss_qos_log_msg(dev, fmt, args...) \
	do { \
		void *ioss_qos_get_ipclog_buf(void); \
		dev_dbg(dev, IOSS_QOS_SUBSYS fmt, ##args); \
		__ioss_qos_log_msg(ioss_qos_get_ipclog_buf(), fmt, ## args); \
	} while (0)


#define ioss_qos_dev_err(idev, fmt, args...) \
	do { \
		struct ioss_device *__idev = (idev); \
		struct device *dev = __idev ? &__idev->dev : NULL; \
		ioss_qos_log_err(dev, "(%s) " fmt, ioss_dev_name(idev), ## args); \
	} while (0)


#define ioss_qos_dev_log(idev, fmt, args...) \
	do { \
		struct ioss_device *__idev = (idev); \
		struct device *dev = __idev ? &__idev->dev : NULL; \
		ioss_qos_log_msg(dev, "(%s) " fmt, ioss_dev_name(idev), ## args); \
	} while (0)

struct IOSS_QOS_TABLE {
    struct list_head qos_rx_pending_table;
    struct list_head qos_rx_committed_table;
    struct list_head qos_tx_pending_table;
    struct list_head qos_tx_committed_table;
};

struct IOSS_QOS_NEW_NODES {
    struct qos_routing_rx *rx_node;
    struct qos_routing_tx *tx_node;
};

enum protocol {
    IOSS_IPPROTO_TCP = 0,
    IOSS_IPPROTO_UDP = 1,
    IOSS_IPPROTO_TCP_UDP = 2,
    IOSS_IPPROTO_INVALID_PROTO = 3
};

struct port {
    u32 port_num;
    enum protocol proto;
};

struct tx_cbs_bw {
    u16 low_bw;
    u16 high_bw;
};

struct qos_filters {
    struct sockaddr_storage address;
    u8 mask_length;
    u32 port_num;
    char *proto;
};

struct qos_filters_array {
    struct qos_filters *arr;
    size_t len;
};

struct pcp_array {
    u8 *arr;
    size_t len;
};

struct vlan_id_array {
    u16 *arr;
    size_t len;
};

struct mac_array {
	u16 len;
	u8 (*arr)[ETH_ALEN];
};

enum action {
    NOT_DEFINED = 0,
    IOSS_QOS_SW_PATH = 1,
    IOSS_QOS_HW_PATH = 2
};

struct qos_routing_rx {
    u8 tc_prio;

    bool committed;
    bool skipped;
    enum action action;

    struct pcp_array pcp;
    struct vlan_id_array vlan_ids;

    struct qos_filters_array src;
    struct qos_filters_array dst;

    struct mac_array smac;
    struct mac_array dmac;

    void *filter_info;

    struct list_head node;
};
#define to_qos_routing_rx(ptr) list_entry(ptr, struct qos_routing_rx, node)

struct qos_routing_tx {
    u8 tc_prio;

    bool committed;
    bool skipped;
    enum action action;

    struct tx_cbs_bw cbs_bw;
    u16 bw_allocated;

    void *tx_param_info;

    struct list_head node;
};
#define to_qos_routing_tx(ptr) list_entry(ptr, struct qos_routing_tx, node)

enum ioss_qos_response {
    QOS_COMMIT_SUCCESS = 0,
    QOS_COMMIT_FAIL = 1,
    QOS_COMMIT_EMPTY = 2
};

struct response {
    int err;
    enum ioss_qos_response qos_response_status;
    u8 num_tx_pipes;
    u8 num_rx_pipes;
    struct qos_pipe_mapping qos_pipe_mapping;
};

struct ioss_qos_ops {
	struct response (*prepare_qos)(struct ioss_device *idev, struct list_head *qos_rx, struct list_head *qos_tx);
	int (*request_qos)(struct ioss_device *idev);
	int (*enable_qos)(struct ioss_device *idev);
	int (*clear_qos)(struct ioss_device *idev);
    ssize_t (*show_qos)(struct ioss_device *idev, char *buf, struct list_head *qos_rx, struct list_head *qos_tx);
	int (*clear_qos_cache)(struct ioss_device *idev);
	ssize_t (*show_qos_info)(struct ioss_device *idev, char *buf);
};

#define create_qos_sysfs_node(idev, qos_kobj, qos_node, uid, gid, qos_sysfs_err) \
	do { \
		if (sysfs_create_file(qos_kobj, &dev_attr_##qos_node.attr)) { \
			ioss_dev_err(idev, "unable to create " #qos_node " node"); \
			goto qos_sysfs_err; \
		} \
		if (sysfs_file_change_owner(qos_kobj, #qos_node, KUIDT_INIT(uid), KGIDT_INIT(gid))) { \
			ioss_dev_err(idev, "unable to change owner of " #qos_node " sysfs node"); \
			goto qos_sysfs_err; \
		} \
	} while(0)


#define QOS_TABLE_ROW_BUFFER 16
#define QOS_TABLE_BUFFER 2048

int create_qos_sysfs_nodes(struct device *dev);
void remove_qos_sysfs_nodes(struct device *dev);

static inline int getbitpos(int n)
{
    unsigned i = 1, pos = 1;

    if (!n)
        return 0;

    if (n & (n - 1))
        return -1;

    while ( !(i & n) ) {
        i = i << 1;
        ++pos;
    }

    if (pos > 8)
        return -1;

    return pos;
}

/* Limits Start */
#define PCP_LOWER_LIMIT 0
#define PCP_UPPER_LIMIT 7

#define VLAN_LOWER_LIMIT 1
#define VLAN_UPPER_LIMIT 4095

#define BW_LOWER_LIMIT 0
#define BW_UPPER_LIMIT 10000
/* Limits End */

#endif /* _IOSS_QOS_H_ */
