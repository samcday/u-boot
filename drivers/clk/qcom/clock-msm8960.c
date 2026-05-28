// SPDX-License-Identifier: BSD-3-Clause
/*
 * Minimal clock driver for Qualcomm MSM8960-family GCC.
 *
 * This covers the legacy NS/MD clocks needed by early 32-bit Snapdragon
 * targets.
 */

#include <clk-uclass.h>
#include <dm.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/kernel.h>

#include "clock-qcom.h"

#define SDC1_H_CLK		110
#define SDC3_H_CLK		112
#define SDC1_CLK		120
#define SDC3_CLK		122
#define USB_HS1_H_CLK		126
#define USB_HS1_XCVR_CLK	128
#define GSBI5_H_CLK		151
#define GSBI5_UART_CLK		168

#define SDC1_RESET		58
#define SDC3_RESET		60
#define USB_HS1_RESET		64
#define GSBI5_RESET		74

#define BB_PLL8_STATUS_REG	0x3158
#define BB_PLL_ENA_SC0_REG	0x34c0

#define SDC_HCLK_CTL_REG(n)	(0x2820 + 0x20 * ((n) - 1))
#define SDC_APPS_CLK_MD_REG(n)	(0x2828 + 0x20 * ((n) - 1))
#define SDC_APPS_CLK_NS_REG(n)	(0x282c + 0x20 * ((n) - 1))
#define SDC_RESET_REG(n)	(0x2830 + 0x20 * ((n) - 1))

#define USB_HS1_HCLK_CTL_REG		0x2900
#define USB_HS1_XCVR_FS_CLK_MD_REG	0x2908
#define USB_HS1_XCVR_FS_CLK_NS_REG	0x290c
#define USB_HS1_RESET_REG		0x2910

#define GSBI5_HCLK_CTL_REG		0x2a40
#define GSBI5_UART_APPS_MD_REG		0x2a50
#define GSBI5_UART_APPS_NS_REG		0x2a54
#define GSBI5_RESET_REG			0x2a5c

#define LEGACY_BRANCH_ENABLE	BIT(9)
#define LEGACY_MND_ENABLE	BIT(8)
#define LEGACY_MND_RESET	BIT(7)
#define LEGACY_ROOT_ENABLE	BIT(11)

struct msm8960_rcg_rate {
	uint rate;
	u32 md;
	u32 ns;
	bool mnd_enable;
};

static const struct msm8960_rcg_rate msm8960_sdc_rates[] = {
	{   400000, 0x0001000f, 0x0010005b, true },
	{ 48000000, 0x000100fd, 0x00fe005b, true },
	{ 96000000, 0x000000ff, 0x0000001b, false },
};

static const struct msm8960_rcg_rate msm8960_usb_hs1_rates[] = {
	{ 60000000, 0x000500df, 0x00e40043, true },
};

static const struct msm8960_rcg_rate msm8960_gsbi5_uart_rates[] = {
	{ 1843200, 0x0003fd8e, 0xfd910043, true },
	{ 7372800, 0x000cfd8e, 0xfd9a0043, true },
};

static const struct gate_clk msm8960_clks[] = {
	GATE_CLK(SDC1_H_CLK, SDC_HCLK_CTL_REG(1), BIT(4)),
	GATE_CLK(SDC3_H_CLK, SDC_HCLK_CTL_REG(3), BIT(4)),
	GATE_CLK(SDC1_CLK, SDC_APPS_CLK_NS_REG(1), LEGACY_BRANCH_ENABLE),
	GATE_CLK(SDC3_CLK, SDC_APPS_CLK_NS_REG(3), LEGACY_BRANCH_ENABLE),
	GATE_CLK(USB_HS1_H_CLK, USB_HS1_HCLK_CTL_REG, BIT(4)),
	GATE_CLK(USB_HS1_XCVR_CLK, USB_HS1_XCVR_FS_CLK_NS_REG,
		 LEGACY_BRANCH_ENABLE),
	GATE_CLK(GSBI5_H_CLK, GSBI5_HCLK_CTL_REG, BIT(4)),
	GATE_CLK(GSBI5_UART_CLK, GSBI5_UART_APPS_NS_REG,
		 LEGACY_BRANCH_ENABLE),
};

static const struct qcom_reset_map msm8960_gcc_resets[] = {
	[SDC1_RESET]	= { SDC_RESET_REG(1), 0 },
	[SDC3_RESET]	= { SDC_RESET_REG(3), 0 },
	[USB_HS1_RESET]	= { USB_HS1_RESET_REG, 0 },
	[GSBI5_RESET]	= { GSBI5_RESET_REG, 0 },
};

static struct pll_vote_clk pll8_vote_clk = {
	.status = BB_PLL8_STATUS_REG,
	.status_bit = BIT(16),
	.ena_vote = BB_PLL_ENA_SC0_REG,
	.vote_bit = BIT(8),
};

static const struct msm8960_rcg_rate *
msm8960_find_rate(const struct msm8960_rcg_rate *rates, size_t count, uint rate)
{
	int i;

	for (i = 0; i < count; i++)
		if (rate <= rates[i].rate)
			return &rates[i];

	return &rates[count - 1];
}

static uint msm8960_set_rate_mnd(phys_addr_t base, uint md_reg, uint ns_reg,
				 const struct msm8960_rcg_rate *rates,
				 size_t count, uint rate)
{
	const struct msm8960_rcg_rate *cfg;
	u32 ns;

	cfg = msm8960_find_rate(rates, count, rate);
	ns = cfg->ns | LEGACY_ROOT_ENABLE;
	if (cfg->mnd_enable)
		ns |= LEGACY_MND_ENABLE;

	clk_enable_gpll0(base, &pll8_vote_clk);

	/* Hold the M/N counter in reset while programming the legacy NS/MD RCG. */
	writel(ns | LEGACY_MND_RESET, base + ns_reg);
	writel(cfg->md, base + md_reg);
	writel(ns, base + ns_reg);

	return cfg->rate;
}

static ulong msm8960_sdc_set_rate(struct msm_clk_priv *priv, uint index,
				  ulong rate)
{
	qcom_gate_clk_en(priv, index == 1 ? SDC1_H_CLK : SDC3_H_CLK);
	clrbits_le32(priv->base + SDC_RESET_REG(index), BIT(0));

	return msm8960_set_rate_mnd(priv->base, SDC_APPS_CLK_MD_REG(index),
				    SDC_APPS_CLK_NS_REG(index),
				    msm8960_sdc_rates,
				    ARRAY_SIZE(msm8960_sdc_rates), rate);
}

static ulong msm8960_clk_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	switch (clk->id) {
	case SDC1_CLK:
		return msm8960_sdc_set_rate(priv, 1, rate);
	case SDC3_CLK:
		return msm8960_sdc_set_rate(priv, 3, rate);
	case USB_HS1_XCVR_CLK:
		clrbits_le32(priv->base + USB_HS1_RESET_REG, BIT(0));
		return msm8960_set_rate_mnd(priv->base, USB_HS1_XCVR_FS_CLK_MD_REG,
					    USB_HS1_XCVR_FS_CLK_NS_REG,
					    msm8960_usb_hs1_rates,
					    ARRAY_SIZE(msm8960_usb_hs1_rates), rate);
	case GSBI5_UART_CLK:
		qcom_gate_clk_en(priv, GSBI5_H_CLK);
		clrbits_le32(priv->base + GSBI5_RESET_REG, BIT(0));
		rate = msm8960_set_rate_mnd(priv->base, GSBI5_UART_APPS_MD_REG,
					    GSBI5_UART_APPS_NS_REG,
					    msm8960_gsbi5_uart_rates,
					    ARRAY_SIZE(msm8960_gsbi5_uart_rates), rate);
		qcom_gate_clk_en(priv, GSBI5_UART_CLK);
		return rate;
	default:
		return 0;
	}
}

static ulong msm8960_clk_get_rate(struct clk *clk)
{
	switch (clk->id) {
	case SDC1_CLK:
	case SDC3_CLK:
		return 96000000;
	case USB_HS1_XCVR_CLK:
		return 60000000;
	case GSBI5_UART_CLK:
		return 1843200;
	default:
		return 0;
	}
}

static int msm8960_clk_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	switch (clk->id) {
	case SDC1_CLK:
		msm8960_clk_set_rate(clk, 48000000);
		break;
	case SDC3_CLK:
		msm8960_clk_set_rate(clk, 48000000);
		break;
	case USB_HS1_XCVR_CLK:
		msm8960_clk_set_rate(clk, 60000000);
		break;
	case GSBI5_UART_CLK:
		msm8960_clk_set_rate(clk, 1843200);
		break;
	}

	return qcom_gate_clk_en(priv, clk->id);
}

static struct msm_clk_data msm8960_clk_data = {
	.clks = msm8960_clks,
	.num_clks = ARRAY_SIZE(msm8960_clks),
	.resets = msm8960_gcc_resets,
	.num_resets = ARRAY_SIZE(msm8960_gcc_resets),
	.enable = msm8960_clk_enable,
	.set_rate = msm8960_clk_set_rate,
	.get_rate = msm8960_clk_get_rate,
};

static const struct udevice_id gcc_msm8960_of_match[] = {
	{
		.compatible = "qcom,gcc-msm8960",
		.data = (ulong)&msm8960_clk_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_msm8960) = {
	.name		= "gcc_msm8960",
	.id		= UCLASS_NOP,
	.of_match	= gcc_msm8960_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC,
};
