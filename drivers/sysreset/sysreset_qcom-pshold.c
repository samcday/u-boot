// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm PSHOLD reset driver
 *
 * Copyright (c) 2024 Sartura Ltd.
 *
 * Author: Robert Marko <robert.marko@sartura.hr>
 * Based on the Linux msm-poweroff driver.
 *
 */

#include <dm.h>
#include <sysreset.h>
#include <asm/io.h>
#include <linux/delay.h>

#define MSM8960_TLMM_PSHOLD_OFFSET	0x820

struct qcom_pshold_priv {
	phys_addr_t base;
};

static int qcom_pshold_request(struct udevice *dev, enum sysreset_t type)
{
	struct qcom_pshold_priv *priv = dev_get_priv(dev);

	writel(0, priv->base);
	mdelay(10000);

	return 0;
}

static struct sysreset_ops qcom_pshold_ops = {
	.request = qcom_pshold_request,
};

static int qcom_pshold_probe(struct udevice *dev)
{
	struct qcom_pshold_priv *priv = dev_get_priv(dev);
	ulong offset = dev_get_driver_data(dev);

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->base += offset;
	return 0;
}

static const struct udevice_id qcom_pshold_ids[] = {
	{ .compatible = "qcom,pshold", },
	{ .compatible = "qcom,msm8960-pinctrl",
	  .data = MSM8960_TLMM_PSHOLD_OFFSET },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(qcom_pshold) = {
	.name 		= "qcom_pshold",
	.id 		= UCLASS_SYSRESET,
	.of_match 	= qcom_pshold_ids,
	.probe 		= qcom_pshold_probe,
	.priv_auto	= sizeof(struct qcom_pshold_priv),
	.ops 		= &qcom_pshold_ops,
};
