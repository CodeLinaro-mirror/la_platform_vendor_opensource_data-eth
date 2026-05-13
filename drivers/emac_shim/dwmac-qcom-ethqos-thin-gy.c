// SPDX-License-Identifier: GPL-2.0-only

// Copyright (c) 2021 The Linux Foundation. All rights reserved.
// Copyright (c) 2018-19 Linaro Limited
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <linux/net.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/iommu.h>
#include <linux/if_vlan.h>


#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <net/sock.h>
#include<linux/socket.h>
#include<linux/in.h>
#include<linux/in6.h>
#include<linux/net.h>
#include<linux/tcp.h>
#include<linux/inet.h>
#include<linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>

#include "stmmac_thin.h"
#include "stmmac_platform_thin.h"
#include "dwmac-qcom-ethqos-thin.h"


#define EMAC_HW_v3_0_0 0x30000000

#define SERVER_IP "192.168.1.1"
#define SERVER_PORT 8888
#define MAX_SIZE 2048
#define MAX_RECEIVE_RETRIES 10
static DECLARE_WAIT_QUEUE_HEAD(conn_wait_queue);

struct client_socket
{
	struct socket *conn_socket;
	struct sockaddr_in *server;
	struct qcom_ethqos *ethqos;
	struct kthread_worker kworker;
	struct task_struct *task;
	struct kthread_work init_client;
	struct kthread_worker poll_worker;
	struct kthread_work read_data;
	struct kthread_work poll_work;
	struct task_struct *poll_task;

	int connect_retry_cnt;
	int poll_active;
};
struct client_socket client_sk;
bool pending_data;

void *ipc_emac_thin_log_ctxt_gy;

struct qcom_ethqos *pethqos_gy;

struct vlan_info {
	int vlan_status;
	int vid;
	int gvm_link_state;
};

struct vlan_info client_vlan;

enum {
	HEALTH_MONITOR = 1,
	DEL_GVM_THIN_VLAN = 2,
	ADD_GVM_THIN_VLAN = 3,
	ADD_ALL_GVM_THIN_VLAN = 4,
	GVM_REMOVE = 5
};

struct emac_fe_ev {
	struct list_head list;
	unsigned long ev;
};

enum domain_t {
	POWER_MDIO = 0,
};

enum link_state {
	IDLE,
	UP_SENT,
	DOWN_SENT
};

static enum link_state state = IDLE;
static DEFINE_SPINLOCK(state_lock);

static void qcom_ethqos_client_poll(struct kthread_work *work);
int send_with_retry(struct vlan_info vlan);

static void emac_fe_ev_wq(struct work_struct *work)
{
	struct qcom_ethqos *ethqos = container_of(work, struct qcom_ethqos,
						  emac_fe_work);
	struct stmmac_priv *priv = qcom_ethqos_thin_get_priv_gy(ethqos);
	struct emac_fe_ev *emac_ev;
	struct vlan_info vlan;

	ETHQOSINFO("Enter - cur state [%u]\n", priv->emac_state);
	do {
		unsigned long flags;

		spin_lock_irqsave(&ethqos->lock, flags);
		emac_ev = list_first_entry_or_null(&ethqos->emac_fe_ev_q,
						   struct emac_fe_ev, list);
		if (emac_ev)
			list_del(&emac_ev->list);
		spin_unlock_irqrestore(&ethqos->lock, flags);

		if (!emac_ev)
			break;

		ETHQOSINFO("get ev [%lu]\n", emac_ev->ev);
		switch (emac_ev->ev) {
		case EMAC_HW_UP:
			ETHQOSDBG("HW up ev\n");
			if (priv->emac_state != EMAC_INIT_ST)
				break;

			priv->emac_state = EMAC_HW_UP_ST;
			if (ethqos->suspended &&
			    !stmmac_thin_resume(priv->device)) {
				ETHQOSINFO("resume on HW up\n");
				vlan.vlan_status = ADD_ALL_GVM_THIN_VLAN;
				vlan.vid = 0;
				vlan.gvm_link_state = EMAC_LINK_UP;
				send_with_retry(vlan);
				ethqos->suspended = false;
			} else if (priv->dev_opened &&
				   !priv->dev_inited) {
				ETHQOSINFO("init driver on HW up\n");
				stmmac_dvr_init(priv->dev);
				priv->add_filter(priv->dev);
				vlan.vlan_status = ADD_ALL_GVM_THIN_VLAN;
				vlan.vid = 0;
				vlan.gvm_link_state = EMAC_LINK_UP;
				send_with_retry(vlan);
			}
			break;
		case EMAC_HW_DOWN:
			ETHQOSDBG("HW down ev\n");
			if (priv->emac_state > EMAC_INIT_ST &&
			    priv->dev_inited) {
				if (!stmmac_thin_suspend(priv->device)) {
					ETHQOSINFO("suspended\n");
					ethqos->suspended = true;
				}
			}
			priv->emac_state = EMAC_INIT_ST;
			break;
		case EMAC_LINK_UP:
			ETHQOSDBG("Link up ev\n");
			if (priv->emac_state == EMAC_HW_UP_ST && priv->dev_inited) {
				stmmac_mac_link_up(priv->dev);
				priv->emac_state = EMAC_LINK_UP_ST;
			}
			break;
		case EMAC_LINK_DOWN:
			ETHQOSDBG("Link down ev\n");
			if (priv->emac_state > EMAC_INIT_ST) {
				priv->emac_state = EMAC_HW_UP_ST;
				stmmac_mac_link_down(priv->dev);
			}
			break;
		case EMAC_DMA_INT_STS_AVAIL:
			ETHQOSDBG("CH intr status ev\n");
			if (priv->emac_state > EMAC_INIT_ST &&
			    priv->dev_inited)
				stmmac_ch_status(priv->dev);
			break;
		default:
			ETHQOSERR("Invalid ev passed: %lu\n", emac_ev->ev);
		}
		kfree(emac_ev);
	} while (1);


	ETHQOSINFO("End - cur state [%u]\n", priv->emac_state);
}

static int qcom_ethqos_data_ready_notify(struct qcom_ethqos *ethqos, int ev) {
	struct emac_fe_ev *emac_ev;
	unsigned long flags;

	ETHQOSINFO("event [%d]\n", ev);
	if (ev == EMAC_DMA_INT_STS_AVAIL)
		return NOTIFY_DONE;

	emac_ev = kzalloc(sizeof(struct emac_fe_ev), GFP_ATOMIC);
	if (!emac_ev) {
		ETHQOSERR("emac_fe_ev alloc failed\n");
		return -ENOMEM;
	}
	emac_ev->ev = ev;
	spin_lock_irqsave(&ethqos->lock, flags);
	list_add_tail(&emac_ev->list, &ethqos->emac_fe_ev_q);
	spin_unlock_irqrestore(&ethqos->lock, flags);

	queue_work(ethqos->wq, &ethqos->emac_fe_work);
	return 0;
}

void qcom_ethqos_client_sock_cleanup(void) {

	if (!client_sk.conn_socket)
		return;

	ETHQOSINFO("entry\n");
	kthread_cancel_work_sync(&client_sk.read_data);
	kthread_cancel_work_sync(&client_sk.init_client);
	kthread_flush_work(&client_sk.init_client);
	kthread_flush_work(&client_sk.read_data);

	if(client_sk.conn_socket)
	{
		kernel_sock_shutdown(client_sk.conn_socket,SHUT_RDWR);
	}

	if(client_sk.conn_socket)
	{
		sock_release(client_sk.conn_socket);
		client_sk.conn_socket = NULL;
	}

	if(client_sk.server)
	{
		kfree(client_sk.server);
		client_sk.server = NULL;
	}

	ETHQOSINFO("exit\n");
}


static void qcom_ethqos_client_receive(struct kthread_work *work) {
	struct msghdr msg;
	struct kvec vec;
	int max_size = MAX_SIZE;
	void *buf;
	int received_int = -1;
	int len, ret;
	int retries = 0;
	int offset = 0;

	ETHQOSDBG("client_thread start\n");

	if (!client_sk.conn_socket)
		return;

	buf = kzalloc(max_size, GFP_ATOMIC);
	if (!buf) {
		ETHQOSERR("buffer alloc failed\n");
		return;
	}

	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	vec.iov_len = max_size;
	vec.iov_base = buf;
	msg.msg_flags = MSG_DONTWAIT;

	do {
		len = kernel_recvmsg(client_sk.conn_socket, &msg, &vec, 1, max_size, msg.msg_flags);
		if (len <= 0) {
			ETHQOSERR("packet receive failed with error: %d\n",len);
			wait_event_timeout(conn_wait_queue, 0, msecs_to_jiffies(10));
			retries++;
			if (retries == MAX_RECEIVE_RETRIES) {
				ETHQOSERR("Exhaust receive retries...\n");
				kfree(buf);
				return;
			}
		}
		else
			break;
	} while (retries < MAX_RECEIVE_RETRIES);

	/* Process each 4-byte chunk as a separate state */
	while (offset + sizeof(int) <= len) {
		memcpy(&received_int, buf + offset, sizeof(int));
		ETHQOSDBG("Received msg length: %d | current offset: %d| received event : %d\n",
			    len, offset, received_int);

		ret = qcom_ethqos_data_ready_notify(client_sk.ethqos,received_int);
		if (ret < 0) {
			ETHQOSERR("thin emac notify failed : %d\n",ret);
		}
		offset += sizeof(int);
	}

	kfree(buf);
	return;
}

static void qcom_ethqos_client_data_ready(struct sock *sk) {
	ETHQOSDBG("kernel queue work called\n");
	kthread_queue_work(&client_sk.kworker, &client_sk.read_data);
}

void qcom_ethqos_client_data_recieved(struct sock *sk) {
	ETHQOSDBG("Recieved before connect\n");
	pending_data = true;
}

void qcom_ethqos_client_start(struct kthread_work *work)
{
	struct socket *sockt;
	struct sockaddr_in *server = NULL;
	int acc,cn;

	ETHQOSINFO("start\n");

	server=(struct sockaddr_in*) kmalloc(sizeof(struct sockaddr_in),GFP_KERNEL);
	acc=sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sockt);
	if (acc < 0) {
		ETHQOSERR("Could not create socket: %d \n",acc);
		goto release;
	}

	memset(server,0,sizeof(struct sockaddr_in));
	server->sin_family = AF_INET;
	in4_pton(SERVER_IP, -1, (u8 *)&server->sin_addr.s_addr, '\0', NULL);
	server->sin_port = htons(SERVER_PORT);

	sockt->sk->sk_data_ready = qcom_ethqos_client_data_recieved;

connect:
	/* socket connect start*/
	cn=kernel_connect(sockt, (struct sockaddr*) server,sizeof(struct sockaddr_in),O_RDWR);
	ETHQOSDBG("kernel sock connection :%d\n",cn);
	if(cn == 0) {

		client_sk.conn_socket = sockt;
		client_sk.server = server;

		kthread_init_work(&client_sk.read_data, qcom_ethqos_client_receive);
		client_sk.conn_socket->sk->sk_data_ready = qcom_ethqos_client_data_ready;

		if(pending_data)
			qcom_ethqos_client_data_ready(sockt->sk);
		ETHQOSDBG("qcom_ethqos_client_receive worker init\n");
	} else {
		/*retry mechanish here*/
		if (client_sk.connect_retry_cnt > 0)
		{
			ETHQOSINFO("connect failed(cn=%d), retry=%d, wait 50ms\n",
				   cn, 11 - client_sk.connect_retry_cnt);
			wait_event_timeout(conn_wait_queue, 0, msecs_to_jiffies(50));
			 --client_sk.connect_retry_cnt;
			 goto connect;
		}
		ETHQOSERR("kernel connection client failed code::%d\n",cn);

		goto release;
	}
	ETHQOSINFO("exit\n");
	return;

release:
	if(server)
		kfree(server);
	return;
}

int qcom_ethqos_client_connect(void *prv) {
	struct qcom_ethqos *ethqos = prv;

	ETHQOSINFO("Client module start\n");

	client_sk.ethqos = ethqos;
	client_sk.connect_retry_cnt = 10;

	/* eth_adapt_rx kthread is created once in probe; just queue the work. */
	kthread_queue_work(&client_sk.kworker, &client_sk.init_client);

	ETHQOSDBG("Client module exit\n");
	return 0;
}

int send_with_retry(struct vlan_info vlan)
{
	struct msghdr msg;
	struct kvec vec;
	int retries = 0;
	int ret = 0;

	vec.iov_base = &vlan;
	vec.iov_len = sizeof(vlan);
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_DONTWAIT;

	while (retries < MAX_RECEIVE_RETRIES) {
		if (client_sk.conn_socket) {
			ret = kernel_sendmsg(client_sk.conn_socket, &msg, &vec, 1, sizeof(vlan));
			if (ret < 0) {
				if (ret == -EPIPE || ret == -ECONNRESET || ret == -ECONNABORTED) {
					ETHQOSINFO(" lost connection!!!\n");
					qcom_ethqos_client_sock_cleanup();
					qcom_ethqos_client_connect(client_sk.ethqos);
				}
				ETHQOSINFO("send failed, retrying...ret = %d\n", ret);
			} else if (vlan.vlan_status != HEALTH_MONITOR) {
				ETHQOSINFO("gvm sent vlan state %d, vid = %d\n", vlan.vlan_status, vlan.vid);
			return 0;
			}
		}
		retries++;
		wait_event_timeout(conn_wait_queue, 0, msecs_to_jiffies(5));
	}
	return -1;
}

static int netdev_event_listener(struct notifier_block *nb, unsigned long event, void *data)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(data);
	struct stmmac_priv *priv;
	unsigned long flags;
	bool send_flag = false;

	if (!netdev || !netdev->dev.parent || !netdev->dev.parent->driver)
		return NOTIFY_DONE;

	if (netdev->priv_flags & IFF_802_1Q_VLAN) {
		ETHQOSDBG("Skipping VLAN interface: %s\n", netdev->name);
		return NOTIFY_DONE;
	}

	if (strcmp(netdev->dev.parent->driver->name, DRV_NAME_GY) != 0)
		return NOTIFY_DONE;

	priv = netdev_priv(netdev);
	ETHQOSDBG("driver name = %s\n", netdev->dev.parent->driver->name);
	spin_lock_irqsave(&state_lock, flags);
	switch (event) {
		case NETDEV_UP:
			if (state != UP_SENT) {
				ETHQOSINFO("thin driver interface is UP\n");
				client_vlan.gvm_link_state = EMAC_LINK_UP;
				client_vlan.vlan_status = ADD_ALL_GVM_THIN_VLAN;
				client_vlan.vid = 0;
				state = UP_SENT;
				send_flag = true;
			}
			break;
		case NETDEV_DOWN:
			if (state != DOWN_SENT) {
				ETHQOSINFO("thin driver interface is DOWN\n");
				client_vlan.gvm_link_state = EMAC_LINK_DOWN;
				client_vlan.vid = 0;
				client_vlan.vlan_status = DEL_GVM_THIN_VLAN;
				state = DOWN_SENT;
				send_flag = true;
			}
			break;
		default:
			break;
	}
	spin_unlock_irqrestore(&state_lock, flags);

	if (send_flag && priv->emac_state >= EMAC_HW_UP_ST)
		send_with_retry(client_vlan);

	return NOTIFY_OK;
}

static struct notifier_block netdev_notifier = {
	.notifier_call = netdev_event_listener,
};

static int qcom_ethqos_add_filter(struct net_device *ndev)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	enum emac_ctrl_fe_filter_types filter_type;
	union emac_ctrl_fe_filter filter;
	int ret = -EPERM;

	if (priv->emac_state < EMAC_HW_UP_ST) {
		ETHQOSINFO("emac HW is not ready for adding filter\n");
		return ret;
	}

	switch (priv->filter_type) {
	case VLAN_TYPE:
		filter_type = VLAN_FILTER;
		filter.vlan_id = priv->vid;
		client_vlan.vlan_status = ADD_GVM_THIN_VLAN;
		client_vlan.vid = priv->vid;
		client_vlan.gvm_link_state = EMAC_LINK_UP;
		ret = send_with_retry(client_vlan);
		ETHQOSINFO("vlan %d is UP\n", priv->vid);
		break;
	default:
		ETHQOSDBG("No filter configured yet, type=%d\n", priv->filter_type);
		break;
	}
	return ret;
}

static int qcom_ethqos_del_filter(struct net_device *ndev)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	enum emac_ctrl_fe_filter_types filter_type;
	union emac_ctrl_fe_filter filter;
	int ret = -EPERM;

	if (priv->emac_state < EMAC_HW_UP_ST) {
		ETHQOSINFO("emac HW is not ready for deleting filter\n");
		return ret;
	}

	if (priv->filter_type == VLAN_TYPE) {
		filter_type = VLAN_FILTER;
		filter.vlan_id = priv->vid;
		ETHQOSINFO("vlan %d is DOWN\n", priv->vid);
		client_vlan.vlan_status = DEL_GVM_THIN_VLAN;
		client_vlan.vid = priv->vid;
		client_vlan.gvm_link_state = EMAC_LINK_UP;
		return send_with_retry(client_vlan);
	}
	return ret;
}

static void qcom_ethqos_client_poll(struct kthread_work *work)
{
	struct vlan_info vlan;
	vlan.vlan_status = HEALTH_MONITOR;
	vlan.vid = 0;
	vlan.gvm_link_state = 0;

	while (client_sk.poll_active) {
		if (client_sk.conn_socket) {
			ETHQOSDBG("Sending health monitor\n");
			send_with_retry(vlan);
		}
		wait_event_timeout(conn_wait_queue, 0, msecs_to_jiffies(1000));
	}
}

void qcom_ethqos_client_poll_worker_start(void)
{
	ETHQOSINFO("Starting poll worker thread\n");
	client_sk.poll_active = 1;
	kthread_init_worker(&client_sk.poll_worker);
	client_sk.poll_task = kthread_run(kthread_worker_fn, &client_sk.poll_worker, "eth_poll_worker");
	if (IS_ERR(client_sk.poll_task)) {
		ETHQOSERR("Failed to create poll worker thread\n");
		return;
	}

	/* Elevate polling thread priority (nice=-5) */
	set_user_nice(client_sk.poll_task, -5);

	kthread_init_work(&client_sk.poll_work, qcom_ethqos_client_poll);
	kthread_queue_work(&client_sk.poll_worker, &client_sk.poll_work);

	return;
}

static int qcom_ethqos_is_genpd_on(struct device *dev)
{
	struct generic_pm_domain *genpd = pd_to_genpd(dev->pm_domain);

	return (genpd->status == GENPD_STATE_ON);
}

static int qcom_ethqos_domain_on(struct qcom_ethqos *ethqos, enum domain_t dom)
{
	struct device *dev = &ethqos->pdev->dev;
	int ret = 0;

	ETHQOSDBG("qcom_ethqos_domain_on start\n");
	if(!qcom_ethqos_is_genpd_on(dev)){
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		dev_err(dev, "poweron(domain=%d) failed.(err=%d)\n", dom, ret);
	}
	return ret;
}

static int qcom_ethqos_domain_off(struct qcom_ethqos *ethqos, enum domain_t dom)
{
	struct device *dev = &ethqos->pdev->dev;
	int ret = 0;

	ETHQOSDBG("qcom_ethqos_domain_off start\n");
	if(qcom_ethqos_is_genpd_on(dev)){
	ret = pm_runtime_put_sync(dev);
	if (ret < 0)
		dev_err(dev, "poweroff(domain=%d) failed.(err=%d)\n", dom, ret);
	}
	return ret;
}

static int qcom_ethqos_domain_transition_d0d1(void *priv, bool high)
{
	struct qcom_ethqos *ethqos = priv;
	int ret = 0;

	if (high) {
		ret = qcom_ethqos_domain_on(ethqos, POWER_MDIO);
		if (ret < 0)
			dev_err(&ethqos->pdev->dev, "Transition from d0 to d1 failed\n");
		else
			dev_info(&ethqos->pdev->dev, "Transition from d0 to d1 done\n");

	} else {
		ret = qcom_ethqos_domain_off(ethqos, POWER_MDIO);
		if (ret < 0)
			dev_err(&ethqos->pdev->dev, "Transition from d1 to d0 failed\n");
		else
			dev_info(&ethqos->pdev->dev, "Transition from d1 to d0 done\n");
	}

	return ret;
}

static inline unsigned int dwmac_qcom_get_eth_type(unsigned char *buf)
{
	return
		((((u16)buf[QTAG_ETH_TYPE_OFFSET] << 8) |
		  buf[QTAG_ETH_TYPE_OFFSET + 1]) == ETH_P_8021Q) ?
		(((u16)buf[QTAG_VLAN_ETH_TYPE_OFFSET] << 8) |
		 buf[QTAG_VLAN_ETH_TYPE_OFFSET + 1]) :
		 (((u16)buf[QTAG_ETH_TYPE_OFFSET] << 8) |
		  buf[QTAG_ETH_TYPE_OFFSET + 1]);
}

static unsigned int dwmac_qcom_get_plat_tx_coal_frames(struct sk_buff *skb)
{
	unsigned int eth_type;
#ifdef CONFIG_PTPSUPPORT_OBJ
	bool is_udp;
#endif

	eth_type = dwmac_qcom_get_eth_type(skb->data);

#ifdef CONFIG_PTPSUPPORT_OBJ
	if (eth_type == ETH_P_1588)
		return PTP_INT_MOD;
#endif

	if (eth_type == ETH_P_TSN)
		return AVB_INT_MOD;
	if (eth_type == ETH_P_IP || eth_type == ETH_P_IPV6) {
#ifdef CONFIG_PTPSUPPORT_OBJ
		is_udp = (((eth_type == ETH_P_IP) &&
				   (ip_hdr(skb)->protocol ==
					IPPROTO_UDP)) ||
				  ((eth_type == ETH_P_IPV6) &&
				   (ipv6_hdr(skb)->nexthdr ==
					IPPROTO_UDP)));

		if (is_udp && ((udp_hdr(skb)->dest ==
			htons(PTP_UDP_EV_PORT)) ||
			(udp_hdr(skb)->dest ==
			  htons(PTP_UDP_GEN_PORT))))
			return PTP_INT_MOD;
#endif
		return IP_PKT_INT_MOD;
	}
	return DEFAULT_INT_MOD;
}

static const struct of_device_id qcom_ethqos_match[] = {
	{ .compatible = "qcom,stmmac-ethqos-emac1-gy", },
	{ }
};

inline void *qcom_ethqos_thin_get_priv_gy(struct qcom_ethqos *ethqos)
{
	struct platform_device *pdev = ethqos->pdev;
	struct net_device *dev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(dev);

	return priv;
}

static int qcom_ethqos_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct plat_stmmacenet_data *plat_dat = NULL;
	struct stmmac_resources stmmac_res;
	struct qcom_ethqos *ethqos = NULL;
	int ret, ret_domain;
	struct net_device *ndev;
	struct stmmac_priv *priv;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(36));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			ETHQOSERR("could not set DMA mask\n");
			return ret;
		}
	}

#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
	place_marker("M - Ethernet probe start");
#endif

	ETHQOSINFO("Start GY probe\n");

	ipc_emac_thin_log_ctxt_gy = ipc_log_context_create(IPCLOG_STATE_PAGES,
						   "emac_gy", 0);
	if (!ipc_emac_thin_log_ctxt_gy)
		ETHQOSDBG("IPC logging context for emac_gy not available\n");
	else
		ETHQOSDBG("IPC logging has been enabled for emac_gy\n");
	ret = stmmac_thin_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	ethqos = devm_kzalloc(&pdev->dev, sizeof(*ethqos), GFP_KERNEL);
	if (!ethqos)
		return -ENOMEM;

	ethqos->pdev = pdev;

	plat_dat = stmmac_thin_probe_config_dt(pdev,
					  stmmac_res.mac, stmmac_res.ch);
	if (IS_ERR(plat_dat)) {
		dev_err(&pdev->dev, "dt configuration failed\n");
		return PTR_ERR(plat_dat);
	}

	plat_dat->bsp_priv = ethqos;

	if (of_property_read_bool(np, "emac-core-version")) {
		/* Read emac core version value from dtsi */
		ret = of_property_read_u32(np, "emac-core-version",
					   &ethqos->emac_ver);
		if (ret) {
			ETHQOSDBG(": resource emac-hw-ver! not in dtsi\n");
			ethqos->emac_ver = EMAC_HW_v3_0_0;
			WARN_ON(1);
		}
	} else {
		ethqos->emac_ver = EMAC_HW_v3_0_0;
	}
	ETHQOSDBG("emac_core_version = 0x%x\n", ethqos->emac_ver);

	/* Allocate workqueue */
	ethqos->wq = alloc_workqueue("ethqos_wq", WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (!ethqos->wq) {
		ETHQOSERR("Failed to create workqueue\n");
		ret = -ENOMEM;
		goto err_smmu;
	}

	spin_lock_init(&ethqos->lock);
	INIT_WORK((struct work_struct *)&ethqos->emac_fe_work, emac_fe_ev_wq);
	INIT_LIST_HEAD(&ethqos->emac_fe_ev_q);

	ret = stmmac_thin_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret) {
		ETHQOSERR("Failed stmmac_thin_dvr_probe - err = %d\n", ret);
		goto err_reg;
	}

	ndev = dev_get_drvdata(&ethqos->pdev->dev);
	priv = netdev_priv(ndev);

	priv->ethqos_client_connect = qcom_ethqos_client_connect;
	priv->get_plat_tx_coal_frames = dwmac_qcom_get_plat_tx_coal_frames;
	priv->add_filter = qcom_ethqos_add_filter;
	priv->del_filter = qcom_ethqos_del_filter;
	priv->is_gy_en = true;

	if (of_device_is_compatible(np, "qcom,stmmac-ethqos-emac1-gy")) {
		if (!pm_runtime_enabled(&ethqos->pdev->dev)) {
			ret_domain = devm_pm_runtime_enable(&ethqos->pdev->dev);
			if (ret_domain)
				ETHQOSERR("Failed : enable the pm runtime : %d\n",ret_domain);
			else {
				priv->clks_config = qcom_ethqos_domain_transition_d0d1;
				ETHQOSDBG("GVM ETH Power domain loaded successfully.\n");
			}
		}
	}
	priv->emac_state = EMAC_INIT_ST;
	qcom_ethqos_client_poll_worker_start();

	/* Create eth_adapt_rx kthread here (probe context, no rtnl_lock held)
	 * so that client_connect() only needs kthread_queue_work, avoiding a
	 * kthread_create wait_for_completion stall inside rtnl_lock.
	 */
	kthread_init_work(&client_sk.init_client, qcom_ethqos_client_start);
	kthread_init_worker(&client_sk.kworker);
	client_sk.task = kthread_run(kthread_worker_fn,
				     &client_sk.kworker, "eth_adapt_rx");
	if (IS_ERR(client_sk.task)) {
		ETHQOSERR("Error creating eth_adapt_rx kthread\n");
		ret = PTR_ERR(client_sk.task);
		client_sk.task = NULL;
		client_sk.poll_active = 0;
		kthread_cancel_work_sync(&client_sk.poll_work);
		kthread_flush_work(&client_sk.poll_work);
		goto err_kthread;
	}
	set_user_nice(client_sk.task, -10);

	register_netdevice_notifier(&netdev_notifier);

	ret = register_netdev(ndev);
	if (ret) {
		ETHQOSERR("register_netdev failed: %d\n", ret);
		unregister_netdevice_notifier(&netdev_notifier);
		kthread_stop(client_sk.task);
		client_sk.task = NULL;
		client_sk.poll_active = 0;
		kthread_cancel_work_sync(&client_sk.poll_work);
		kthread_flush_work(&client_sk.poll_work);
		goto err_reg_netdev;
	}

#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
	place_marker("M - Ethernet probe end");
#endif
	ETHQOSINFO("End\n");
	return 0;

err_reg_netdev:
err_kthread:
	netif_napi_del(&priv->channel.rx_napi);
	netif_napi_del(&priv->channel.tx_napi);
err_reg:
	destroy_workqueue(ethqos->wq);
err_smmu:
	of_platform_depopulate(&pdev->dev);
	return ret;
}

static void qcom_ethqos_remove(struct platform_device *pdev)
{
	struct qcom_ethqos *ethqos;
	struct vlan_info vlan;
	vlan.vlan_status = GVM_REMOVE;
	vlan.vid = 0;
	vlan.gvm_link_state = EMAC_LINK_DOWN;

	ethqos = get_stmmac_bsp_priv(&pdev->dev);
	if (!ethqos)
		return;

	ETHQOSINFO("Enter\n");
	unregister_netdevice_notifier(&netdev_notifier);
	send_with_retry(vlan);
	client_sk.poll_active = 0;
	kthread_cancel_work_sync(&client_sk.poll_work);
	kthread_flush_work(&client_sk.poll_work);
	qcom_ethqos_client_sock_cleanup();
	if (client_sk.task) {
		kthread_stop(client_sk.task);
		client_sk.task = NULL;
	}
	destroy_workqueue(ethqos->wq);
	stmmac_thin_pltfr_remove(pdev);

	platform_set_drvdata(pdev, NULL);
	of_platform_depopulate(&pdev->dev);

	if (ipc_emac_thin_log_ctxt_gy)
		ipc_log_context_destroy(ipc_emac_thin_log_ctxt_gy);
	ipc_emac_thin_log_ctxt_gy = NULL;
	ETHQOSINFO("Exit\n");

	return;
}

static void qcom_ethqos_shutdown(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	if (!dev)
		return;

	qcom_ethqos_remove(pdev);
}

static int qcom_ethqos_suspend(struct device *dev)
{
	struct qcom_ethqos *ethqos;
	struct net_device *ndev = NULL;
	struct stmmac_priv *priv = NULL;
	int ret;
	struct vlan_info vlan;
	vlan.vlan_status = DEL_GVM_THIN_VLAN;
	vlan.vid = 0;
	vlan.gvm_link_state = EMAC_LINK_DOWN;

	ethqos = get_stmmac_bsp_priv(dev);
	if (!ethqos)
		return -ENODEV;

	ndev = dev_get_drvdata(dev);

	if (!ndev || !netif_running(ndev)) {
		ETHQOSINFO(" Suspend not possible\n");
		return 0;
	}

	priv = netdev_priv(ndev);
	ret = stmmac_thin_suspend(dev);
	if (!ret) {
		ethqos->suspended = true;
		priv->emac_state = EMAC_INIT_ST;
		send_with_retry(vlan);
		qcom_ethqos_client_sock_cleanup();
	}

	priv->boot_kpi = false;
	ETHQOSDBG(" ret = %d\n", ret);
	return ret;
}

static int qcom_ethqos_resume(struct device *dev)
{
	struct net_device *ndev = NULL;
	struct qcom_ethqos *ethqos;
	int ret = 0;

	ETHQOSINFO("Resume Enter\n");

	ethqos = get_stmmac_bsp_priv(dev);

	if (!ethqos)
		return -ENODEV;

	ndev = dev_get_drvdata(dev);

	if (!ndev || !netif_running(ndev)) {
		ETHQOSINFO(" Resume not possible\n");
		return 0;
	}

	if (ethqos->suspended)
		qcom_ethqos_client_connect(client_sk.ethqos);

	ETHQOSDBG("<--Resume Exit\n");
	return ret;
}

MODULE_DEVICE_TABLE(of, qcom_ethqos_match);

static const struct dev_pm_ops qcom_ethqos_pm_ops = {
	.suspend = qcom_ethqos_suspend,
	.resume = qcom_ethqos_resume,
};

static struct platform_driver qcom_ethqos_driver = {
	.probe  = qcom_ethqos_probe,
	.remove = qcom_ethqos_remove,
	.shutdown = qcom_ethqos_shutdown,
	.driver = {
		.name           = DRV_NAME_GY,
		.pm		= &qcom_ethqos_pm_ops,
		.of_match_table = of_match_ptr(qcom_ethqos_match),
	},
};

static int __init qcom_ethqos_init_module_gy(void)
{
	int ret = 0;

	ETHQOSINFO("\n");
	ret = platform_driver_register(&qcom_ethqos_driver);
	if (ret < 0) {
		ETHQOSINFO("qcom-ethqos: Driver registration failed\n");
		return ret;
	}
	ETHQOSINFO("EMAC THIN GY driver registered \n");

	return ret;
}

static void __exit qcom_ethqos_exit_module_gy(void)
{
	ETHQOSINFO("\n");

	platform_driver_unregister(&qcom_ethqos_driver);

	ETHQOSINFO("\n");
}

/*!
 * \brief Macro to register the driver registration function.
 *
 * \details A module always begin with either the init_module or the function
 * you specify with module_init call. This is the entry function for modules;
 * it tells the kernel what functionality the module provides and sets up the
 * kernel to run the module's functions when they're needed. Once it does this,
 * entry function returns and the module does nothing until the kernel wants
 * to do something with the code that the module provides.
 */

module_init(qcom_ethqos_init_module_gy)

/*!
 * \brief Macro to register the driver un-registration function.
 *
 * \details All modules end by calling either cleanup_module or the function
 * you specify with the module_exit call. This is the exit function for modules;
 * it undoes whatever entry function did. It unregisters the functionality
 * that the entry function registered.
 */

module_exit(qcom_ethqos_exit_module_gy)

MODULE_DESCRIPTION("Qualcomm Technologies, Inc.ETHQOS thin driver");
MODULE_LICENSE("GPL v2");
