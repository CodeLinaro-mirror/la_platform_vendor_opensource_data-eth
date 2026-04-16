/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * eth_qos - QoS sysfs structure under eth (ioss_sysfs style)
 *
 * Sysfs hierarchy:
 *   ndev->dev.kobj                    <- net_device device kobj
 *     └── qos_kobj              "qos" <- standalone kobject
 *           ├── add_params_kobj       <- standalone kobject
 *           │     ├── vlan, l3l4, pcp, tx_pcp, tx_bw
 *
 * Recovering net_device:
 *   DEVICE_ATTR callbacks receive the kobject as 'dev' (struct device *).
 *   Since struct device starts with struct kobject, dev->kobj == *kobj and
 *   dev->kobj.parent == kobj->parent.  Walking up to ndev->dev.kobj then
 *   using to_net_dev() recovers the net_device directly.
 *
 *   qos-level attrs (del, clear_all, print):
 *     depth=1: to_net_dev(kobj_to_dev(dev->kobj.parent))
 *              qos_kobj->parent = ndev->dev.kobj
 *
 *   add_params attrs (vlan, l3l4, pcp, tx_pcp, tx_bw):
 *     depth=2: to_net_dev(kobj_to_dev(dev->kobj.parent->parent))
 *              add_params_kobj->parent->parent = ndev->dev.kobj
 *
 */

#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/of.h>
#include "eth_qos.h"
#include "eth_qos_mgr.h"
#include "eth_qos_bus.h"

/*
 * ndev_from_qos_dev - Recover net_device from a DEVICE_ATTR callback.
 *
 * @dev:   first argument of the DEVICE_ATTR callback (actually a kobject *)
 * @depth: number of ->parent hops to reach ndev->dev.kobj
 *
 * Exactly mirrors ioss_sysfs.c:
 *   idev-level attrs (depth=1):
 *     parent = kobj_to_dev(dev->kobj.parent)   -> &ndev->dev
 *     to_net_dev(parent)                        -> ndev
 *   channel-level attrs (depth=2):
 *     parent = kobj_to_dev(dev->kobj.parent->parent) -> &ndev->dev
 *     to_net_dev(parent)                             -> ndev
 *
 * qos_kobj is created directly under ndev->dev.kobj (no "eth" level),
 * so depth=1 reaches ndev->dev.kobj for qos-level attrs, and depth=2
 * reaches it for add_params attrs.
 */
static struct net_device *ndev_from_qos_dev(struct device *dev, int depth)
{
	struct kobject *k = &dev->kobj;

	while (depth-- > 0 && k)
		k = k->parent;

	if (!k)
		return NULL;

	/* k is ndev->dev.kobj — use to_net_dev (same as ioss_sysfs.c) */
	return to_net_dev(kobj_to_dev(k));
}

#define __create_sysfs(qos_kobj, qos_node, uid, gid, qos_sysfs_err) \
	do { \
		if (sysfs_create_file(qos_kobj, &dev_attr_##qos_node.attr)) { \
			eth_qos_log_err(NULL, "unable to create " #qos_node " node"); \
			goto qos_sysfs_err; \
		} \
		if (sysfs_file_change_owner(qos_kobj, #qos_node, KUIDT_INIT(uid), KGIDT_INIT(gid))) { \
			eth_qos_log_err(NULL, "unable to change owner of " #qos_node " sysfs node"); \
			goto qos_sysfs_err; \
		} \
	} while (0)

/* Generic stubs for attributes */
static ssize_t noop_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "\n");
}

/* Utility functions for argument parsing */
static bool is_valid_vlan_id(u16 vlan_id)
{
	return (vlan_id >= VLAN_LOWER_LIMIT && vlan_id < VLAN_UPPER_LIMIT);
}

/* ---- add_params attrs (depth=2) ---- */

static ssize_t store_vlan_id(struct device *dev, struct device_attribute *attr,
			     const char *user_buf, size_t size)
{
	struct net_device *ndev = ndev_from_qos_dev(dev, 2);
	char *buf, *cur, *tok, *last_key = NULL;
	int hid = -1, queue = -1, ch = -1, vlanid = -1;
	int ret = -EINVAL;
	bool have_path = false, hw_path = false;

	if (!ndev)
		return -ENODEV;

	buf = kstrndup(user_buf, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	strim(buf);
	cur = buf;

	while ((tok = strsep(&cur, " ,\t\n")) != NULL) {
		if (!*tok)
			continue;

		if (!last_key) {
			last_key = tok;
			continue;
		}

		if (!strcmp(last_key, "hid")) {
			if (kstrtoint(tok, 10, &hid))
				hid = -1;
		} else if (!strcmp(last_key, "queue")) {
			if (kstrtoint(tok, 10, &queue))
				queue = -1;
		} else if (!strcmp(last_key, "ch")) {
			if (kstrtoint(tok, 10, &ch))
				ch = -1;
		} else if (!strcmp(last_key, "offload")) {
			if (!strcmp(tok, "1")) {
				hw_path = true;
				have_path = true;
			} else if (!strcmp(tok, "0")) {
				hw_path = false;
				have_path = true;
			}
		} else if (!strcmp(last_key, "vlanid")) {
			if (kstrtoint(tok, 10, &vlanid))
				vlanid = -1;
		}
		last_key = NULL;
	}

	if (hid < 0 || queue < 0 || ch < 0 || !have_path || !is_valid_vlan_id(vlanid)) {
		eth_qos_log_err(NULL, "vlan: hid, queue <qno>, ch <chno>, offload <1|0> and valid vlanid are required. input='%s'",
			    buf);
		goto out;
	}
	eth_qos_log_cfg(NULL, "vlan: hid=%d queue=%d ch=%d path=%s vlanid=%d",
		     hid, queue, ch, hw_path ? "hw" : "sw", vlanid);

	ret = eth_qos_mgr_add_vlan(ndev, hid, queue, ch, hw_path, vlanid);
	if (!ret)
		ret = size;
	else
		eth_qos_log_err(NULL, "vlan: failed to add VLAN routing filter: ret=%d vlanid=%d queue=%d",
			ret, vlanid, queue);

out:
	kfree(buf);
	return ret;
}

/* l3l4: accepts
 *   idx <no>, ch <chno>, sport <port>, dport <port>, proto <protocol>,
 *   sip <IPv4>, dip <IPv4>, sipv6 <IPv6>, dipv6 <IPv6>
 * idx is required and at least one of the remaining must be present.
 */
static ssize_t store_l3l4(struct device *dev, struct device_attribute *attr,
			  const char *user_buf, size_t size)
{
	struct net_device *ndev = ndev_from_qos_dev(dev, 2);
	char *buf, *cur, *tok, *last_key = NULL;
	int hid = -1, queue = -1, ch = -1;
	int ret = -EINVAL;
	bool have_path = false, hw_path = false;
	u16 sport = 0, dport = 0;
	bool have_sport = false, have_dport = false;
	char *proto = NULL, *_sip = NULL, *_dip = NULL, *_sipv6 = NULL, *_dipv6 = NULL;

	if (!ndev)
		return -ENODEV;

	buf = kstrndup(user_buf, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	strim(buf);
	cur = buf;

	while ((tok = strsep(&cur, " ,\t\n")) != NULL) {
		if (!*tok)
			continue;

		if (!last_key) {
			last_key = tok;
			continue;
		}

		if (!strcmp(last_key, "hid")) {
			if (kstrtoint(tok, 10, &hid))
				hid = -1;
		} else if (!strcmp(last_key, "queue")) {
			if (kstrtoint(tok, 10, &queue))
				queue = -1;
		} else if (!strcmp(last_key, "ch")) {
			if (kstrtoint(tok, 10, &ch))
				ch = -1;
		} else if (!strcmp(last_key, "offload")) {
			if (!strcmp(tok, "1")) {
				hw_path = true;
				have_path = true;
			} else if (!strcmp(tok, "0")) {
				hw_path = false;
				have_path = true;
			}
		} else if (!strcmp(last_key, "sport")) {
			unsigned int tmp;
			if (!kstrtouint(tok, 10, &tmp) && tmp >= 1 && tmp <= 65535) {
				sport = (u16)tmp;
				have_sport = true;
			}
		} else if (!strcmp(last_key, "dport")) {
			unsigned int tmp;
			if (!kstrtouint(tok, 10, &tmp) && tmp >= 1 && tmp <= 65535) {
				dport = (u16)tmp;
				have_dport = true;
			}
		} else if (!strcmp(last_key, "proto")) {
			if (!proto)
				proto = kstrdup(tok, GFP_KERNEL);
		} else if (!strcmp(last_key, "sip")) {
			if (!_sip)
				_sip = kstrdup(tok, GFP_KERNEL);
		} else if (!strcmp(last_key, "dip")) {
			if (!_dip)
				_dip = kstrdup(tok, GFP_KERNEL);
		} else if (!strcmp(last_key, "sipv6")) {
			if (!_sipv6)
				_sipv6 = kstrdup(tok, GFP_KERNEL);
		} else if (!strcmp(last_key, "dipv6")) {
			if (!_dipv6)
				_dipv6 = kstrdup(tok, GFP_KERNEL);
		}
		last_key = NULL;
	}

	if (hid < 0 || queue < 0 || ch < 0 || !have_path) {
		eth_qos_log_err(NULL, "l3l4: hid, queue <qno>, ch <chno> and offload <1|0> are required. input='%s'",
			    buf);
		goto out;
	}

	if (!(have_sport || have_dport || proto || _sip || _dip || _sipv6 || _dipv6)) {
		eth_qos_log_err(NULL, "l3l4: at least one of sport/dport/proto/sip/dip/sipv6/dipv6 is required. hid=%d input='%s'",
			hid, buf);
		goto out;
	}

	if ((have_sport || have_dport) && !proto) {
		eth_qos_log_err(NULL, "l3l4: sport/dport require proto. hid=%d input='%s'",
			hid, buf);
		goto out;
	}

	if (proto && strcmp(proto, "udp") && strcmp(proto, "tcp")) {
		eth_qos_log_err(NULL, "l3l4: proto must be 'udp' or 'tcp'. hid=%d input='%s'",
			hid, buf);
		goto out;
	}

	if ((_sip || _dip) && (_sipv6 || _dipv6)) {
		eth_qos_log_err(NULL, "l3l4: IPv4 params sip/dip cannot be combined with IPv6 params sipv6/dipv6. hid=%d input='%s'",
			hid, buf);
		goto out;
	}

	if (_sipv6 && _dipv6) {
		eth_qos_log_err(NULL, "l3l4: only one of sipv6 or dipv6 can be provided. hid=%d input='%s'",
			hid, buf);
		goto out;
	}

	if ((_sip || _dip) && (_sipv6 || _dipv6 || proto)) {
		eth_qos_log_err(NULL, "l3l4: sip/dip cannot be combined with proto or sipv6/dipv6. hid=%d input='%s'",
			hid, buf);
		goto out;
	}

	eth_qos_log_cfg(NULL, "l3l4: hid=%d queue=%d ch=%d path=%s sport=%u dport=%u proto=%s sip=%s dip=%s sipv6=%s dipv6=%s",
		 hid, queue, ch, hw_path ? "hw" : "sw",
		 have_sport ? sport : 0,
		 have_dport ? dport : 0,
		 proto ? proto : "-",
		 _sip ? _sip : "-",
		 _dip ? _dip : "-",
		 _sipv6 ? _sipv6 : "-",
		 _dipv6 ? _dipv6 : "-");

	ret = eth_qos_mgr_add_l3l4(ndev, hid, queue, ch, hw_path, sport, dport, proto,
				   _sip, _dip, _sipv6, _dipv6);
	if (!ret)
		ret = size;

out:
	kfree(proto);
	kfree(_sip);
	kfree(_dip);
	kfree(_sipv6);
	kfree(_dipv6);
	kfree(buf);
	return ret;
}

/* pcp: accepts "pcp_no <val>" (pcp_no required) */
static ssize_t store_pcp(struct device *dev, struct device_attribute *attr,
			 const char *user_buf, size_t size)
{
	struct net_device *ndev = ndev_from_qos_dev(dev, 2);
	char *buf, *cur, *tok, *last_key = NULL;
	bool have_pcp = false;
	int hid = -1, queue = -1, ch = -1;
	int ret = -EINVAL;
	bool have_path = false, hw_path = false;
	u8 pcp = 0;

	if (!ndev)
		return -ENODEV;

	buf = kstrndup(user_buf, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	strim(buf);
	cur = buf;

	while ((tok = strsep(&cur, " ,\t\n")) != NULL) {
		if (!*tok)
			continue;

		if (!last_key) {
			last_key = tok;
			continue;
		}

		if (!strcmp(last_key, "hid")) {
			if (kstrtoint(tok, 10, &hid))
				hid = -1;
		} else if (!strcmp(last_key, "pcp_no")) {
			unsigned int tmp;
			if (!kstrtouint(tok, 10, &tmp) && tmp <= 7) {
				pcp = (u8)tmp;
				have_pcp = true;
			}
		} else if (!strcmp(last_key, "queue")) {
			if (kstrtoint(tok, 10, &queue))
				queue = -1;
		} else if (!strcmp(last_key, "ch")) {
			if (kstrtoint(tok, 10, &ch))
				ch = -1;
		} else if (!strcmp(last_key, "offload")) {
			if (!strcmp(tok, "1")) {
				hw_path = true;
				have_path = true;
			} else if (!strcmp(tok, "0")) {
				hw_path = false;
				have_path = true;
			}
		}
		last_key = NULL;
	}

	if (hid < 0 || !have_pcp || queue < 0 || ch < 0 || !have_path) {
		eth_qos_log_err(NULL, "pcp: hid, pcp_no, queue <qno>, ch <chno> and offload <1|0> are required. input='%s'",
			    buf);
		goto out;
	}

	eth_qos_log_cfg(NULL, "pcp: hid=%d pcp_no=%u queue=%d ch=%d path=%s",
		     hid, pcp, queue, ch, hw_path ? "hw" : "sw");
	ret = eth_qos_mgr_add_pcp(ndev, hid, pcp, queue, ch, hw_path);
	if (!ret)
		ret = size;
	else
		eth_qos_log_err(NULL, "pcp: failed to configure PCP filter pcp_no=%u, queue=%d ret=%d",
			pcp, queue, ret);

out:
	kfree(buf);
	return ret;
}

static ssize_t store_tx_pcp(struct device *dev, struct device_attribute *attr,
			    const char *user_buf, size_t size)
{
	struct net_device *ndev = ndev_from_qos_dev(dev, 2);
	char *buf, *cur, *tok, *last_key = NULL;
	bool have_pcp = false;
	int hid = -1, queue = -1, ch = -1;
	int ret = -EINVAL;
	u8 pcp = 0;

	if (!ndev)
		return -ENODEV;

	buf = kstrndup(user_buf, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	strim(buf);
	cur = buf;

	while ((tok = strsep(&cur, " ,\t\n")) != NULL) {
		if (!*tok)
			continue;

		if (!last_key) {
			last_key = tok;
			continue;
		}

		if (!strcmp(last_key, "hid")) {
			if (kstrtoint(tok, 10, &hid))
				hid = -1;
		} else if (!strcmp(last_key, "pcp_no")) {
			unsigned int tmp;
			if (!kstrtouint(tok, 10, &tmp) && tmp <= 7) {
				pcp = (u8)tmp;
				have_pcp = true;
			}
		} else if (!strcmp(last_key, "queue")) {
			if (kstrtoint(tok, 10, &queue))
				queue = -1;
		} else if (!strcmp(last_key, "ch")) {
			if (kstrtoint(tok, 10, &ch))
				ch = -1;
		}
		last_key = NULL;
	}

	if (hid < 0 || !have_pcp || queue < 0 || ch < 0) {
		eth_qos_log_err(NULL, "tx_pcp: hid, pcp_no, queue <qno> and ch <chno> are required. input='%s'", buf);
		goto out;
	}

	eth_qos_log_cfg(NULL, "tx_pcp: hid=%d pcp_no=%u queue=%d ch=%d", hid, pcp, queue, ch);
	ret = eth_qos_mgr_add_tx_pcp(ndev, hid, pcp, queue, ch);
	if (!ret)
		ret = size;
	else
		eth_qos_log_err(NULL, "tx_pcp: failed to configure TX PCP map pcp_no=%u, queue=%d ret=%d",
			pcp, queue, ret);

out:
	kfree(buf);
	return ret;
}

/* store_tx_bw: accepts
 *   TC <id> offload <1|0> queue <qno> ch <chno> config <cbs>
 *   idleslope <val> sendslope <val> hicredit <val> locredit <val>
 * Notes:
 * - path=hw programs HW CBS via eth_qos_mgr.
 * - path=sw stores SW-path CBS bookkeeping only.
 */
static ssize_t store_tx_bw(struct device *dev, struct device_attribute *attr,
			   const char *user_buf, size_t size)
{
	struct net_device *ndev = ndev_from_qos_dev(dev, 2);
	char *buf, *cur, *tok, *last_key = NULL;
	int hid = -1, tc = -1, queue = -1, ch = -1;
	int ret = -EINVAL;
	char *config = NULL;
	bool have_path = false, hw_path = false;
	bool have_idleslope = false, have_sendslope = false;
	bool have_hicredit = false, have_locredit = false;
	int idleslope = 0, sendslope = 0, hicredit = 0, locredit = 0;

	if (!ndev)
		return -ENODEV;

	buf = kstrndup(user_buf, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	strim(buf);
	cur = buf;

	while ((tok = strsep(&cur, " ,\t\n")) != NULL) {
		if (!*tok)
			continue;

		if (!last_key) {
			last_key = tok;
			continue;
		}

		if (!strcmp(last_key, "hid")) {
			if (kstrtoint(tok, 10, &hid))
				hid = -1;
		} else if (!strcmp(last_key, "TC")) {
			if (kstrtoint(tok, 10, &tc))
				tc = -1;
		} else if (!strcmp(last_key, "offload")) {
			if (!strcmp(tok, "1")) {
				hw_path = true;
				have_path = true;
			} else if (!strcmp(tok, "0")) {
				hw_path = false;
				have_path = true;
			}
		} else if (!strcmp(last_key, "queue")) {
			if (kstrtoint(tok, 10, &queue))
				queue = -1;
		} else if (!strcmp(last_key, "ch")) {
			if (kstrtoint(tok, 10, &ch))
				ch = -1;
		} else if (!strcmp(last_key, "config")) {
			if (!config)
				config = kstrdup(tok, GFP_KERNEL);
		} else if (!strcmp(last_key, "idleslope")) {
			if (!kstrtoint(tok, 0, &idleslope))
				have_idleslope = true;
		} else if (!strcmp(last_key, "sendslope")) {
			if (!kstrtoint(tok, 0, &sendslope))
				have_sendslope = true;
		} else if (!strcmp(last_key, "hicredit")) {
			if (!kstrtoint(tok, 0, &hicredit))
				have_hicredit = true;
		} else if (!strcmp(last_key, "locredit")) {
			if (!kstrtoint(tok, 0, &locredit))
				have_locredit = true;
		}
		last_key = NULL;
	}

	if (hid < 0 || tc < 0 || queue < 0 || ch < 0 || !have_path || !config ||
	    !have_idleslope || !have_sendslope || !have_hicredit || !have_locredit) {
		eth_qos_log_err(NULL, "tx_bw: missing/invalid params. input='%s'", buf);
		goto out;
	}

	eth_qos_log_cfg(NULL, "tx_bw: TC=%d path=%s queue=%d ch=%d config=%s idle=%d send=%d hi=%d lo=%d",
		 tc, hw_path ? "hw" : "sw", queue, ch, config,
		 idleslope, sendslope, hicredit, locredit);

	if (strcmp(config, "cbs")) {
		eth_qos_log_err(NULL, "tx_bw: unsupported config='%s' (only 'cbs' supported)", config);
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = eth_qos_mgr_add_cbs(ndev, hid, tc, queue, ch, hw_path,
				  sendslope, idleslope,
				  hicredit, locredit);
	if (!ret)
		ret = size;

out:
	kfree(config);
	kfree(buf);
	return ret;
}

/* ---- qos-level attrs (depth=1) ---- */
static ssize_t store_del(struct device *dev, struct device_attribute *attr,
			 const char *user_buf, size_t size)
{
	struct net_device *ndev = ndev_from_qos_dev(dev, 1);
	char *buf, *cur, *tok, *last_key = NULL;
	int hid = -1;
	int ret = -EINVAL;

	if (!ndev)
		return -ENODEV;

	buf = kstrndup(user_buf, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	strim(buf);
	cur = buf;

	while ((tok = strsep(&cur, " ,\t\n")) != NULL) {
		if (!*tok)
			continue;

		if (!last_key) {
			last_key = tok;
			continue;
		}

		if (!strcmp(last_key, "hid")) {
			if (kstrtoint(tok, 10, &hid))
				hid = -1;
		}
		last_key = NULL;
	}

	if (hid < 0) {
		eth_qos_log_err(NULL, "del: hid is required. input='%s'", buf);
		goto out;
	}
	
	eth_qos_log_cfg(NULL, "del: hid=%d", hid);

	ret = eth_qos_mgr_del(ndev, hid);
	if (!ret)
		ret = size;
	else
		eth_qos_log_err(NULL, "del: hid=%d ret=%d", hid, ret);

out:
	kfree(buf);
	return ret;
}

/* clear_all: accepts "1" to perform best-effort cleanup of QoS programming. */
static ssize_t store_clear_all(struct device *dev, struct device_attribute *attr,
			       const char *user_buf, size_t size)
{
	struct net_device *ndev = ndev_from_qos_dev(dev, 1);
	char *buf, *cur, *tok;
	int ret = -EINVAL;

	if (!ndev)
		return -ENODEV;

	buf = kstrndup(user_buf, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	strim(buf);

	cur = buf;
	tok = strsep(&cur, " \t\n");
	if (!tok || strcmp(tok, "1")) {
		eth_qos_log_err(NULL, "clear_all: write \"1\" to trigger cleanup. input='%s'", buf);
		goto out;
	}

	ret = eth_qos_mgr_clear_all(ndev);
	if (!ret) {
		eth_qos_log_cfg(NULL, "clear_all: done");
		ret = size;
	}

out:
	kfree(buf);
	return ret;
}

static ssize_t show_print(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = ndev_from_qos_dev(dev, 1);
	ssize_t ret;

	if (!ndev)
		return -ENODEV;

	ret = eth_qos_mgr_dump(ndev, buf, PAGE_SIZE);
	if (ret < 0) {
		eth_qos_log_err(NULL, "print: failed to dump eth_qos_mgr tables: ret=%zd", ret);
		return sysfs_emit(buf, "error: failed to dump eth_qos_mgr tables (%zd)\n", ret);
	}

	return ret;
}

/* ---- Attribute declarations ---- */
static int qos_id;
module_param(qos_id, int, 0);
MODULE_PARM_DESC(qos_id, "gid to assign to qos sysfs nodes");

/* qos-level */
static DEVICE_ATTR(del,           0644, noop_show,          store_del);
static DEVICE_ATTR(clear_all,     0644, noop_show,          store_clear_all);
static DEVICE_ATTR(print,         0444, show_print,         NULL);
/* add_params */
static DEVICE_ATTR(vlan,   0644, noop_show, store_vlan_id);
static DEVICE_ATTR(l3l4,   0644, noop_show, store_l3l4);
static DEVICE_ATTR(pcp,    0644, noop_show, store_pcp);
static DEVICE_ATTR(tx_pcp, 0644, noop_show, store_tx_pcp);
static DEVICE_ATTR(tx_bw,  0644, noop_show, store_tx_bw);
int eth_qos_create_sysfs(struct eth_qos_device *edev)
{
	int ret;
	struct kobject *qos_kobj = NULL;
	struct kobject *tc_param_kobj = NULL;

	if (!edev || !edev->sysfs_kobj)
		return -EINVAL;

	qos_kobj = kobject_create_and_add("qos", edev->sysfs_kobj);
	if (!qos_kobj) {
		eth_qos_log_err(NULL, "unable to create qos kobject");
		ret = -EINVAL;
		goto err_qos_kobj;
	}

	__create_sysfs(qos_kobj, del,           0, qos_id, err_qos_sysfs);
	__create_sysfs(qos_kobj, clear_all,     0, qos_id, err_qos_sysfs);
	__create_sysfs(qos_kobj, print,         0, qos_id, err_qos_sysfs);

	tc_param_kobj = kobject_create_and_add("add_params", qos_kobj);
	if (!tc_param_kobj) {
		eth_qos_log_err(NULL, "unable to create add_params kobject");
		ret = -EINVAL;
		goto err_tc_param_kobj;
	}

	__create_sysfs(tc_param_kobj, vlan,   0, qos_id, err_tc_param_sysfs);
	__create_sysfs(tc_param_kobj, l3l4,   0, qos_id, err_tc_param_sysfs);
	__create_sysfs(tc_param_kobj, pcp,    0, qos_id, err_tc_param_sysfs);
	__create_sysfs(tc_param_kobj, tx_pcp, 0, qos_id, err_tc_param_sysfs);
	__create_sysfs(tc_param_kobj, tx_bw,  0, qos_id, err_tc_param_sysfs);

	edev->qos_kobj = qos_kobj;
	edev->qos_tc_params_kobj = tc_param_kobj;
	edev->qos_cfg_params_kobj = NULL;
	return 0;
err_tc_param_sysfs:
	kobject_del(tc_param_kobj);
err_tc_param_kobj:
	kobject_put(tc_param_kobj);
err_qos_sysfs:
	kobject_del(qos_kobj);
err_qos_kobj:
	kobject_put(qos_kobj);
	return ret ? ret : -EINVAL;
}

void eth_qos_remove_sysfs(struct kobject *qos_kobj,
			  struct kobject *tc_params_kobj,
			  struct kobject *cfg_params_kobj)
{
	if (tc_params_kobj) {
		kobject_del(tc_params_kobj);
		kobject_put(tc_params_kobj);
	}
	if (qos_kobj) {
		kobject_del(qos_kobj);
		kobject_put(qos_kobj);
	}
}