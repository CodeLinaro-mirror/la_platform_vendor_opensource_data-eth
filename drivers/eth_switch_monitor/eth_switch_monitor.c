// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/workqueue.h>
#include <linux/gunyah/gh_dbl.h>

#define MODULE_NAME "eth_switch_monitor"

#define ETH_SWITCH_MON_MAX_DBL  2   /* LA GVM + LV GVM */

struct qcom_eth_switch_mon {
	struct device *dev;
	int detect_irq;

	int dbl_count;
	gh_label_t dbl_labels[ETH_SWITCH_MON_MAX_DBL];
	void *dbl_tx_descs[ETH_SWITCH_MON_MAX_DBL];
	struct work_struct event_work;
};

/**
 * qcom_eth_switch_mon_map_irq - get switch reset detect IRQ from device tree
 * @dev: device associated with the switch monitor driver
 * Description: This function parses the switch-reset-detect-source phandle from
 * the device tree, resolves the referenced interrupt source node, and maps
 * the first interrupt to a Linux IRQ number. It returns 0 when the IRQ
 * source is missing or invalid.
 */
static int qcom_eth_switch_mon_map_irq(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *irq_np;
	int irq;

	irq_np = of_parse_phandle(np, "switch-reset-detect-source", 0);
	if (!irq_np) {
		dev_warn(dev, "switch-reset-detect-source node not found\n");
		return 0;
	}

	if (!of_find_property(irq_np, "interrupts", NULL) &&
	    !of_find_property(irq_np, "interrupts-extended", NULL)) {
		dev_warn(dev, "No interrupts in switch reset detect node\n");
		of_node_put(irq_np);
		return 0;
	}

	irq = irq_of_parse_and_map(irq_np, 0);
	of_node_put(irq_np);

	if (!irq) {
		dev_warn(dev, "Map switch reset detect IRQ failed\n");
		return 0;
	}

	return irq;
}

/**
 * qcom_eth_switch_mon_unmap_irq - release mapped IRQ resource
 * @data: Linux IRQ number encoded as a pointer
 * Description: This function is used as a devres cleanup callback to dispose
 * the IRQ mapping created from the switch reset interrupt source node.
 */
static void qcom_eth_switch_mon_unmap_irq(void *data)
{
	irq_dispose_mapping((unsigned int)(unsigned long)data);
}

/**
 * qcom_eth_switch_mon_event_work - send switch reset doorbell to GVM
 * @work: work item scheduled from the switch reset IRQ handler
 * Description: This function runs in workqueue context and forwards the
 * switch reset event detected in the PVM to the GVM through a Gunyah
 * doorbell. Doorbell transmission is deferred out of interrupt context.
 */
static void qcom_eth_switch_mon_event_work(struct work_struct *work)
{
	struct qcom_eth_switch_mon *priv =
		container_of(work, struct qcom_eth_switch_mon, event_work);
	int i, ret;

	for (i = 0; i < priv->dbl_count; i++) {
		/* bit 0 = Switch Reset event */
		gh_dbl_flags_t flags = BIT(0);

		if (IS_ERR_OR_NULL(priv->dbl_tx_descs[i]))
			continue;

		ret = gh_dbl_send(priv->dbl_tx_descs[i], &flags, GH_DBL_NONBLOCK);
		if (ret)
			dev_err(priv->dev, "gh_dbl_send label=0x%x failed: %d\n", priv->dbl_labels[i], ret);
		else
			dev_info(priv->dev, "Switch doorbell sent (label=0x%x)\n", priv->dbl_labels[i]);
	}
}

/**
 * qcom_eth_switch_mon_cleanup_work - cancel all pending monitor work items
 * @data: pointer to the switch monitor private structure
 * Description: This function is used as a devres cleanup callback. Synchronously
 * cancels cancels any pending event work before driver resources are released.
 */
static void qcom_eth_switch_mon_cleanup_work(void *data)
{
	struct qcom_eth_switch_mon *priv = data;

	cancel_work_sync(&priv->event_work);
}

/**
 * qcom_eth_switch_mon_unregister_dbl - unregister Gunyah doorbell handle
 * @data: pointer to the switch monitor private structure
 * Description: This function is used as a devres cleanup callback to release
 * the Gunyah TX doorbell handle acquired during probe.
 */
static void qcom_eth_switch_mon_unregister_dbl(void *data)
{
	struct qcom_eth_switch_mon *priv = data;
	int i;

	for (i = 0; i < priv->dbl_count; i++) {
		if (!IS_ERR_OR_NULL(priv->dbl_tx_descs[i])) {
			gh_dbl_tx_unregister(priv->dbl_tx_descs[i]);
			priv->dbl_tx_descs[i] = NULL;
		}
	}
}

/**
 * qcom_eth_switch_mon_irq_handler - handle switch reset interrupt
 * @irq: Linux IRQ number for the switch reset source
 * @data: pointer to the switch monitor private structure
 * Description: This is the top-half interrupt handler for the switch reset
 * GPIO event. It does not send the doorbell directly from interrupt context;
 * instead it schedules work to perform the notification asynchronously.
 */
static irqreturn_t qcom_eth_switch_mon_irq_handler(int irq, void *data)
{
	struct qcom_eth_switch_mon *priv = data;

	schedule_work(&priv->event_work);
	return IRQ_HANDLED;
}

/**
 * qcom_eth_switch_mon_setup_irq - setup switch reset detect IRQ
 * @dev: device associated with the switch monitor driver
 * @priv: pointer to the switch monitor private structure
 * Description: This function retrieves the switch reset detect IRQ from
 * device tree, registers the IRQ mapping cleanup action, and requests the
 * interrupt handler used to monitor switch reset events.
 * Return: 0 on success, or a negative error code on failure.
 */
static int qcom_eth_switch_mon_setup_irq(struct device *dev,
					 struct qcom_eth_switch_mon *priv)
{
	int irq, ret;

	irq = qcom_eth_switch_mon_map_irq(dev);
	if (irq <= 0)
		return -EINVAL;

	ret = devm_add_action_or_reset(dev, qcom_eth_switch_mon_unmap_irq,
				       (void *)(unsigned long)irq);
	if (ret) {
		dev_err(dev, "Failed to register IRQ cleanup: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(dev, irq, qcom_eth_switch_mon_irq_handler,
			       IRQF_TRIGGER_FALLING | IRQF_SHARED,
			       MODULE_NAME, priv);
	if (ret) {
		dev_err(dev, "Failed to request switch reset detect IRQ: %d\n", ret);
		return ret;
	}

	priv->detect_irq = irq;
	dev_info(dev, "Registered switch reset detect IRQ %d\n", irq);

	return 0;
}

/**
 * qcom_eth_switch_mon_probe - initialize the switch monitor driver
 * @pdev: platform device instance for the switch monitor
 * Description: This function initializes the switch monitor context, parses
 * the switch reset interrupt source and doorbell label from device tree,
 * registers cleanup handlers, acquires the Gunyah TX doorbell handle, and
 * registers the switch reset IRQ handler.
 */
static int qcom_eth_switch_mon_probe(struct platform_device *pdev)
{
	struct qcom_eth_switch_mon *priv;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	int i, dbl_count;
	int ret;

	dev_info(dev, "Eth switch monitor probe start\n");

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	dbl_count = of_property_count_u32_elems(np, "qcom,dbl-label");
	if (dbl_count < 0) {
		dev_err(dev, "Missing qcom,dbl-label\n");
		return dbl_count;
	}
	if (dbl_count > ETH_SWITCH_MON_MAX_DBL) {
		dev_warn(dev, "qcom,dbl-label has %d entries, clamping to %d\n",
			 dbl_count, ETH_SWITCH_MON_MAX_DBL);
		dbl_count = ETH_SWITCH_MON_MAX_DBL;
	}
	priv->dbl_count = dbl_count;

	ret = of_property_read_u32_array(np, "qcom,dbl-label",
					 priv->dbl_labels, priv->dbl_count);
	if (ret) {
		dev_err(dev, "Failed to read qcom,dbl-label: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, qcom_eth_switch_mon_unregister_dbl, priv);
	if (ret) {
		dev_err(dev, "Failed to register doorbell cleanup: %d\n", ret);
		return ret;
	}

	INIT_WORK(&priv->event_work, qcom_eth_switch_mon_event_work);
	ret = devm_add_action_or_reset(dev, qcom_eth_switch_mon_cleanup_work, priv);
	if (ret) {
		dev_err(dev, "Failed to register work cleanup: %d\n", ret);
		return ret;
	}

	for (i = 0; i < priv->dbl_count; i++) {
		priv->dbl_tx_descs[i] = gh_dbl_tx_register(priv->dbl_labels[i]);
		if (IS_ERR(priv->dbl_tx_descs[i])) {
			dev_err(dev, "gh_dbl_tx_register label=0x%x failed: %ld\n",
				priv->dbl_labels[i], PTR_ERR(priv->dbl_tx_descs[i]));
			return PTR_ERR(priv->dbl_tx_descs[i]);
		}
		dev_info(dev, "Registered doorbell label=0x%x\n", priv->dbl_labels[i]);
	}

	/* Setup switch reset detect irq */
	ret = qcom_eth_switch_mon_setup_irq(dev, priv);
	if (ret)
		return ret;

	dev_info(dev, "Eth switch monitor probe done\n");
	return 0;
}

/**
 * qcom_eth_switch_mon_remove - remove the switch monitor driver
 * @pdev: platform device instance for the switch monitor
 * Description: This function is called when the switch monitor device is
 * removed. Most resources are released automatically through devres-managed
 * cleanup actions registered during probe.
 */
static int qcom_eth_switch_mon_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "Remove eth switch monitor\n");

	return 0;
}

static const struct of_device_id qcom_eth_switch_mon_of_match[] = {
	{ .compatible = "qcom,eth-switch-monitor" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_eth_switch_mon_of_match);

static struct platform_driver qcom_eth_switch_mon_driver = {
	.probe  = qcom_eth_switch_mon_probe,
	.remove = qcom_eth_switch_mon_remove,
	.driver = {
		.name           = MODULE_NAME,
		.of_match_table = qcom_eth_switch_mon_of_match,
	},
};
module_platform_driver(qcom_eth_switch_mon_driver);

MODULE_DESCRIPTION("PVM Ethernet Switch Monitor Driver");
MODULE_LICENSE("GPL v2");