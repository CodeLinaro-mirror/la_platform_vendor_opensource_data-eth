/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/etherdevice.h>
#include <linux/cdev.h>

#include "ioss_i.h"

static struct class *emac_ipa_class;
static dev_t emac_ipa_dev_num;
static struct cdev *emac_ipa_cdev;
static struct device *emac_ipa_dev;


static void ioss_ipa_notify_cb(void *priv,
					enum ipa_dp_evt_type evt,
					unsigned long data)
{
	struct ioss_channel *ch = priv;
	struct sk_buff *skb = (struct sk_buff *)data;
	struct ioss_interface *iface = ch->iface;

	if (evt != IPA_RECEIVE)
		return;

	iface->exception_stats.rx_packets++;
	iface->exception_stats.rx_bytes += skb->len;

	skb->protocol = eth_type_trans(skb, ioss_iface_to_netdev(iface));

	if (netif_rx_ni(skb) == NET_RX_DROP)
		iface->exception_stats.rx_drops++;
}

static int ioss_ipa_fill_pipe_info(struct ioss_channel *ch,
		struct ipa_eth_client_pipe_info *pi)
{
	struct ioss_mem *desc_mem;
	struct ioss_mem *buff_mem;
	struct ipa_eth_buff_smmu_map *sm;
	struct ipa_eth_pipe_setup_info *si = &pi->info;
	struct ioss_device *idev = ioss_ch_dev(ch);

	pi->dir = (ch->direction == IOSS_CH_DIR_TX) ?
			IPA_ETH_PIPE_DIR_TX : IPA_ETH_PIPE_DIR_RX;

	desc_mem = list_first_entry_or_null(
			&ch->desc_mem, typeof(*desc_mem), node);
	if (!desc_mem) {
		ioss_dev_err(idev, "Descriptor memory not found");
		return -EINVAL;
	}

	si->is_transfer_ring_valid = true;
	si->transfer_ring_base = desc_mem->daddr;
	si->transfer_ring_sgt = &desc_mem->sgt;
	si->transfer_ring_size = desc_mem->size;

	si->data_buff_list_size = ch->config.ring_size;
	si->data_buff_list = sm = kcalloc(si->data_buff_list_size,
			sizeof(*si->data_buff_list), GFP_KERNEL);
	if (!sm) {
		ioss_dev_err(idev,
			"Kmalloc failed for data buff list");
		return -EINVAL;
	}
	si->fix_buffer_size = ch->config.buff_size;

	list_for_each_entry(buff_mem, &ch->buff_mem, node) {
		sm->iova = buff_mem->daddr;
		sm->pa = ch->config.buff_alctr->pa(ioss_ch_dev(ch),
				buff_mem->addr, buff_mem->daddr,
				ch->config.buff_alctr);
		sm++;
	}

	if (pi->dir == IPA_ETH_PIPE_DIR_RX) {
		si->notify = ioss_ipa_notify_cb;
		si->priv = ch;
	}

	if (ioss_ipa_hal_fill_si(ch)) {
		ioss_dev_err(idev,
			"Failed to fill IPA pipe setup info");
		return -EINVAL;
	}

	return 0;
}

#if IPA_ETH_API_VER < 2
static void ioss_ipa_fill_eth_hdrs(struct ioss_interface *iface)
{
	struct net_device *net_dev = ioss_iface_to_netdev(iface);

	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;

	union ioss_ipa_eth_hdr *hdr_v4 = &ifp->ipa_hdr_v4;
	union ioss_ipa_eth_hdr *hdr_v6 = &ifp->ipa_hdr_v6;

	struct ipa_eth_hdr_info *hi_v4 = &ii->hdr[0];
	struct ipa_eth_hdr_info *hi_v6 = &ii->hdr[1];

	/* IPv4 */
	memcpy(hdr_v4->l2.h_source, net_dev->dev_addr, ETH_ALEN);
	hdr_v4->l2.h_proto = htons(ETH_P_IP);

	hi_v4->hdr = (u8 *)hdr_v4;
	hi_v4->hdr_len = ETH_HLEN;
	hi_v4->hdr_type = IPA_HDR_L2_ETHERNET_II;

	/* IPv6 */
	memcpy(hdr_v6->l2.h_source, net_dev->dev_addr, ETH_ALEN);
	hdr_v6->l2.h_proto = htons(ETH_P_IPV6);

	hi_v6->hdr = (u8 *)hdr_v6;
	hi_v6->hdr_len = ETH_HLEN;
	hi_v6->hdr_type = IPA_HDR_L2_ETHERNET_II;
}

static void ioss_ipa_fill_vlan_hdrs(struct ioss_interface *iface)
{
	struct net_device *net_dev = ioss_iface_to_netdev(iface);

	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;

	union ioss_ipa_eth_hdr *hdr_v4 = &ifp->ipa_hdr_v4;
	union ioss_ipa_eth_hdr *hdr_v6 = &ifp->ipa_hdr_v6;

	struct ipa_eth_hdr_info *hi_v4 = &ii->hdr[0];
	struct ipa_eth_hdr_info *hi_v6 = &ii->hdr[1];

	/* IPv4 */
	memcpy(hdr_v4->vlan.h_source, net_dev->dev_addr, ETH_ALEN);
	hdr_v4->vlan.h_vlan_proto = htons(ETH_P_8021Q);
	hdr_v4->vlan.h_vlan_encapsulated_proto = htons(ETH_P_IP);

	hi_v4->hdr = (u8 *)hdr_v4;
	hi_v4->hdr_len = VLAN_ETH_HLEN;
	hi_v4->hdr_type = IPA_HDR_L2_802_1Q;

	/* IPv6 */
	memcpy(hdr_v6->vlan.h_source, net_dev->dev_addr, ETH_ALEN);
	hdr_v6->vlan.h_vlan_proto = htons(ETH_P_8021Q);
	hdr_v6->vlan.h_vlan_encapsulated_proto = htons(ETH_P_IPV6);

	hi_v6->hdr = (u8 *)hdr_v6;
	hi_v6->hdr_len = VLAN_ETH_HLEN;
	hi_v6->hdr_type = IPA_HDR_L2_802_1Q;
}

static int ioss_ipa_fill_hdrs(struct ioss_interface *iface)
{
	bool ipa_vlan_mode = false;
	struct ioss_device *idev = ioss_iface_dev(iface);

	if (ipa_is_vlan_mode(IPA_VLAN_IF_EMAC, &ipa_vlan_mode)) {
		ioss_dev_err(idev, "Failed to get IPA vlan mode");
		return -EINVAL;
	}

	if (ipa_vlan_mode)
		ioss_ipa_fill_vlan_hdrs(iface);
	else
		ioss_ipa_fill_eth_hdrs(iface);

	return 0;
}
#endif

static int ioss_ipa_vote_bw(struct ioss_interface *iface)
{
	struct ipa_eth_perf_profile profile;
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ioss_device *idev = ioss_iface_dev(iface);

	ioss_dev_dbg(idev,
		"Voting for IPA bandwidth of %u Mbps", iface->link_speed);

	memset(&profile, 0, sizeof(profile));
	profile.max_supported_bw_mbps = iface->link_speed;

	if (ipa_eth_client_set_perf_profile(ec, &profile)) {
		ioss_dev_err(idev,
				"Failed to set IPA perf profile");
		return -EINVAL;
	}

	ioss_dev_cfg(idev,
		"Voted for IPA bandwidth of %u Mbps", iface->link_speed);

	return 0;
}

#if IPA_ETH_API_VER < 2
static int __ioss_ipa_msg(struct net_device *net_dev, bool connect)
{
	struct ipa_ecm_msg msg;

	memset(&msg, 0, sizeof(msg));
	snprintf(msg.name, sizeof(msg.name), "%s", net_dev->name);
	msg.ifindex = net_dev->ifindex;

	return connect ?
		ipa_eth_client_conn_evt(&msg) :
		ipa_eth_client_disconn_evt(&msg);
}

static int ioss_ipa_msg_connect(struct net_device *net_dev)
{
	return __ioss_ipa_msg(net_dev, true);
}

static int ioss_ipa_msg_disconnect(struct net_device *net_dev)
{
	return __ioss_ipa_msg(net_dev, false);
}
#endif

int ioss_ipa_register(struct ioss_interface *iface)
{
	int i;
	int ret;
#if IPA_ETH_API_VER < 2
	int ch_count;
#endif
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;
	struct ioss_channel *ch;
#if IPA_ETH_API_VER < 2
	struct net_device *net_dev = ioss_iface_to_netdev(iface);
#endif
	struct ioss_device *idev = ioss_iface_dev(iface);

	ec->priv = iface;
	ec->inst_id = iface->instance_id;
	ec->traffic_type = IPA_ETH_PIPE_BEST_EFFORT;
	ec->client_type = ioss_ipa_hal_get_ctype(idev);

	if (ec->client_type == IPA_ETH_CLIENT_MAX) {
		ioss_dev_err(idev, "Failed to determine client type");
		return -EFAULT;
	}

	INIT_LIST_HEAD(&ec->pipe_list);

#if IPA_ETH_API_VER < 2
	ch_count = 0;
#endif
	ioss_for_each_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;

		cp->ipa_pi.client_info = ec;

		if (ioss_ipa_fill_pipe_info(ch, &cp->ipa_pi)) {
			ioss_dev_err(idev, "Failed to fill pipe info");
			return -EFAULT;
		}

		list_add_tail(&cp->ipa_pi.link, &ec->pipe_list);
#if IPA_ETH_API_VER < 2
		ch_count++;
#endif
	}
#if IPA_ETH_API_VER < 2
	ii->pipe_hdl_list_size = ch_count;

	ii->pipe_hdl_list = kcalloc(ii->pipe_hdl_list_size,
				sizeof(*ii->pipe_hdl_list), GFP_KERNEL);
	if (!ii->pipe_hdl_list) {
		ioss_dev_err(idev, "Failed to alloc pipe hdl list");
		return -EINVAL;
	}

	ii->netdev_name = net_dev->name;

	if (ioss_ipa_fill_hdrs(iface)) {
		ioss_dev_err(idev, "Failed to fill partial headers");
		return -EINVAL;
	}
#endif

	/* connect pipes */
#if IPA_ETH_API_VER >= 2
	ec->net_dev = ioss_iface_to_netdev(iface);
#endif
	if (ipa_eth_client_conn_pipes(ec)) {
		ioss_dev_err(idev, "Failed to connect pipes");
		return -EINVAL;
	}

	/* vote for bw */
	if (ioss_ipa_vote_bw(iface)) {
		ioss_dev_err(idev, "Failed to vote for bandwidth");
		return -EINVAL;
	}

	/* Retrieve output from conn_pipes call */
	i = 0;
	ioss_for_each_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;
		struct ipa_eth_client_pipe_info *pi = &cp->ipa_pi;
		struct ipa_eth_pipe_setup_info *si = &pi->info;

		ch->event.paddr = si->db_pa;
		ch->event.data = si->db_val;

#if IPA_ETH_API_VER < 2
		ii->pipe_hdl_list[i++] = pi->pipe_hdl;
#endif
	}

	/* register interface */
#if IPA_ETH_API_VER >= 2
	ii->client = ec;
	ii->is_conn_evt = true;
#endif

	if (ipa_eth_client_reg_intf(ii)) {
		ioss_dev_err(idev, "Failed to register interface");
		return -EINVAL;
	}

#if IPA_ETH_API_VER < 2
	/* send ecm msg */
	if (ioss_ipa_msg_connect(net_dev)) {
		ioss_dev_err(idev, "Failed to send connect message");
		return -EINVAL;
	}
#endif

	if (iface->auto_resume_disabled) {
		ioss_dev_cfg(idev, "creating dev char device for auto platform\n");

		ret = alloc_chrdev_region(&emac_ipa_dev_num, 0, 1, "emac_ipa");
		if (ret) {
			ioss_dev_err(idev, "alloc_chrdev_region error for node %s\n",
				  "emac_ipa");
			goto alloc_emac_ipa_chrdev_region_fail;
		}

		emac_ipa_cdev = cdev_alloc();
		if (!emac_ipa_cdev) {
			ret = -ENOMEM;
			ioss_dev_err(idev, "failed to alloc emac_ipa cdev\n");
			goto fail_alloc_emac_ipa_cdev;
		}

		cdev_init(emac_ipa_cdev,NULL);

		ret = cdev_add(emac_ipa_cdev,emac_ipa_dev_num,1);
		if (ret < 0) {
			ioss_dev_err(idev, "emac_ipa cdev_add err=%d\n", -ret);
			goto emac_ipa_cdev_add_fail;
		}

		emac_ipa_class = class_create(THIS_MODULE,"emac_ipa");
		if (!emac_ipa_class) {
			ret = -ENODEV;
			ioss_dev_err(idev, "failed to create emac_ipa class\n");
			goto fail_create_emac_ipa_class;
		}

		emac_ipa_dev = device_create(emac_ipa_class, NULL,
				emac_ipa_dev_num, NULL, "emac_ipa");
		if (!emac_ipa_dev) {
			ret = -EINVAL;
			ioss_dev_err(idev, "failed to create emac_ipa device\n");
			goto fail_create_emac_ipa_device;
		}
	}

	return 0;

fail_create_emac_ipa_device:
	class_destroy(emac_ipa_class);
fail_create_emac_ipa_class:
	cdev_del(emac_ipa_cdev);
emac_ipa_cdev_add_fail:
fail_alloc_emac_ipa_cdev:
	unregister_chrdev_region(emac_ipa_dev_num, 1);
alloc_emac_ipa_chrdev_region_fail:
	return ret;
}

int ioss_ipa_unregister(struct ioss_interface *iface)
{
	int rc;
	struct ioss_channel *ch;
	struct ioss_iface_priv *ifp = iface->ioss_priv;
	struct ipa_eth_client *ec = &ifp->ipa_ec;
	struct ipa_eth_intf_info *ii = &ifp->ipa_ii;
#if IPA_ETH_API_VER < 2
	union ioss_ipa_eth_hdr *hdr_v4 = &ifp->ipa_hdr_v4;
	union ioss_ipa_eth_hdr *hdr_v6 = &ifp->ipa_hdr_v6;
	struct net_device *net_dev = ioss_iface_to_netdev(iface);
#endif
	struct ioss_device *idev = ioss_iface_dev(iface);

	if(emac_ipa_cdev && iface->auto_resume_disabled)
	{
		device_destroy(emac_ipa_class, emac_ipa_dev_num);
		class_destroy(emac_ipa_class);
		cdev_del(emac_ipa_cdev);
		unregister_chrdev_region(emac_ipa_dev_num, 1);
	}

	/* connect pipes */
	rc = ipa_eth_client_disconn_pipes(ec);
	if (rc) {
		ioss_dev_err(idev, "Failed to disconnect pipes");
		return rc;
	}

	/* unregister interface */
	rc = ipa_eth_client_unreg_intf(ii);
	if (rc) {
		ioss_dev_err(idev, "Failed to unregister interface");
		return rc;
	}

#if IPA_ETH_API_VER < 2
	/* send ecm msg */
	rc = ioss_ipa_msg_disconnect(net_dev);
	if (rc) {
		ioss_dev_err(idev, "Failed to send disconnect message");
		return rc;
	}

	memset(hdr_v4, 0, sizeof(*hdr_v4));
	memset(hdr_v6, 0, sizeof(*hdr_v6));

	kfree(ii->pipe_hdl_list);
#endif
	memset(ii, 0, sizeof(*ii));

	ioss_for_each_channel(ch, iface) {
		struct ioss_ch_priv *cp = ch->ioss_priv;

		ch->event.paddr = 0;
		ch->event.data = 0;

		memset(&cp->ipa_pi, 0, sizeof(cp->ipa_pi));
	}

	memset(ec, 0, sizeof(*ec));

	return 0;
}
