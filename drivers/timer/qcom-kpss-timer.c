// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm KPSS/SCSS timer driver
 * Adapted from Linux drivers/clocksource/timer-qcom.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2012,2014, The Linux Foundation. All rights reserved.
 */

#include <dm.h>
#include <div64.h>
#include <errno.h>
#include <timer.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/bitops.h>

DECLARE_GLOBAL_DATA_PTR;

#define TIMER_COUNT_VAL			0x04
#define TIMER_ENABLE			0x08
#define TIMER_ENABLE_EN			BIT(0)
#define DGT_CLK_CTL			0x10
#define DGT_CLK_CTL_DIV_4		0x3

#define QCOM_TIMER_DGT_OFFSET		0x24
#define QCOM_TIMER_DGT_RATE_DIV		4

struct qcom_kpss_timer_priv {
	void __iomem *source_base;
	void __iomem *counter;
};

static u64 notrace qcom_kpss_timer_get_count(struct udevice *dev)
{
	struct qcom_kpss_timer_priv *priv = dev_get_priv(dev);

	return timer_conv_64(readl(priv->counter));
}

static int qcom_kpss_timer_probe(struct udevice *dev)
{
	struct timer_dev_priv *uc_priv = dev_get_uclass_priv(dev);
	struct qcom_kpss_timer_priv *priv = dev_get_priv(dev);
	u32 rate;
	int ret;

	ret = dev_read_u32(dev, "clock-frequency", &rate);
	if (ret)
		return ret;

	uc_priv->clock_rate = rate / QCOM_TIMER_DGT_RATE_DIV;

	writel(DGT_CLK_CTL_DIV_4, priv->source_base + DGT_CLK_CTL);
	writel(TIMER_ENABLE_EN, priv->source_base + TIMER_ENABLE);

	return 0;
}

static int qcom_kpss_timer_of_to_plat(struct udevice *dev)
{
	struct qcom_kpss_timer_priv *priv = dev_get_priv(dev);
	phys_addr_t base;
	phys_addr_t source_base;
	u32 cpu_offset;

	base = dev_read_addr(dev);
	if (base == FDT_ADDR_T_NONE)
		return -EINVAL;

	cpu_offset = dev_read_u32_default(dev, "cpu-offset", 0);
	source_base = base + cpu_offset + QCOM_TIMER_DGT_OFFSET;
	priv->source_base = map_physmem(source_base, 0x100, MAP_NOCACHE);
	priv->counter = priv->source_base + TIMER_COUNT_VAL;

	return 0;
}

static const struct timer_ops qcom_kpss_timer_ops = {
	.get_count = qcom_kpss_timer_get_count,
};

static const struct udevice_id qcom_kpss_timer_ids[] = {
	{ .compatible = "qcom,kpss-timer" },
	{ .compatible = "qcom,scss-timer" },
	{ }
};

U_BOOT_DRIVER(qcom_kpss_timer) = {
	.name		= "qcom_kpss_timer",
	.id		= UCLASS_TIMER,
	.of_match	= qcom_kpss_timer_ids,
	.of_to_plat	= qcom_kpss_timer_of_to_plat,
	.probe		= qcom_kpss_timer_probe,
	.ops		= &qcom_kpss_timer_ops,
	.priv_auto	= sizeof(struct qcom_kpss_timer_priv),
	.flags		= DM_FLAG_PRE_RELOC,
};
