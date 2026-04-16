/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef _ETH_QOS_MGR_H_
#define _ETH_QOS_MGR_H_

#include <linux/types.h>
#include <linux/netdevice.h>

int eth_qos_mgr_add_vlan(struct net_device *ndev, u32 hid, u32 queue, u32 ch,
			 bool hw_path, u16 vlanid);
int eth_qos_mgr_add_l3l4(struct net_device *ndev, u32 hid, u32 queue, u32 ch,
			 bool hw_path, u16 sport, u16 dport, const char *proto,
			 const char *sip, const char *dip,
			 const char *sipv6, const char *dipv6);
int eth_qos_mgr_add_pcp(struct net_device *ndev, u32 hid, u8 pcp, u32 queue, u32 ch,
			bool hw_path);
int eth_qos_mgr_add_tx_pcp(struct net_device *ndev, u32 hid, u8 pcp, u32 queue, u32 ch);
int eth_qos_mgr_add_cbs(struct net_device *ndev, u32 hid, u32 tc, u32 queue, u32 ch,
			bool hw_path, int sendslope, int idleslope,
			int hicredit, int locredit);
int eth_qos_mgr_del(struct net_device *ndev, u32 hid);
int eth_qos_mgr_clear_rx_sw(struct net_device *ndev);
int eth_qos_mgr_clear_all(struct net_device *ndev);
int eth_qos_mgr_replay(struct net_device *ndev);
void eth_qos_mgr_release(struct net_device *ndev);
ssize_t eth_qos_mgr_dump(struct net_device *ndev, char *buf, size_t buf_sz);

#endif /* _ETH_QOS_MGR_H_ */