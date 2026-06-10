// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm APCS IPC mailbox controller
 *
 * Copyright (c) 2026
 */

#include <asm/io.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <mailbox-uclass.h>
#include <linux/bitops.h>

#define QCOM_APCS_IPC_BITS	32

struct qcom_apcs_ipc {
	void __iomem *base;
	ulong offset;
};

static int qcom_apcs_ipc_request(struct mbox_chan *chan)
{
	if (chan->id >= QCOM_APCS_IPC_BITS)
		return -EINVAL;

	return 0;
}

static int qcom_apcs_ipc_send(struct mbox_chan *chan, const void *data)
{
	struct qcom_apcs_ipc *apcs = dev_get_priv(chan->dev);

	writel(BIT(chan->id), apcs->base + apcs->offset);
	readl(apcs->base + apcs->offset);

	return 0;
}

static const struct mbox_ops qcom_apcs_ipc_ops = {
	.request = qcom_apcs_ipc_request,
	.send = qcom_apcs_ipc_send,
};

static int qcom_apcs_ipc_probe(struct udevice *dev)
{
	struct qcom_apcs_ipc *apcs = dev_get_priv(dev);

	apcs->base = dev_read_addr_ptr(dev);
	if (!apcs->base)
		return -EINVAL;

	apcs->offset = dev_get_driver_data(dev);

	return 0;
}

static const struct udevice_id qcom_apcs_ipc_of_match[] = {
	{ .compatible = "qcom,sm6125-apcs-hmss-global", .data = 0x8 },
	{ .compatible = "qcom,sm6115-apcs-hmss-global", .data = 0x8 },
	{ .compatible = "qcom,sm4250-apcs-hmss-global", .data = 0x8 },
	{ .compatible = "qcom,msm8994-apcs-kpss-global", .data = 0x8 },
	{ }
};

U_BOOT_DRIVER(qcom_apcs_ipc) = {
	.name = "qcom-apcs-ipc",
	.id = UCLASS_MAILBOX,
	.of_match = qcom_apcs_ipc_of_match,
	.probe = qcom_apcs_ipc_probe,
	.priv_auto = sizeof(struct qcom_apcs_ipc),
	.ops = &qcom_apcs_ipc_ops,
};
