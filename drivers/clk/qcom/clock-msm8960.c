// SPDX-License-Identifier: GPL-2.0+

#include <clk-uclass.h>
#include <dm.h>
#include <errno.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <dt-bindings/clock/qcom,gcc-msm8960.h>
#include <dt-bindings/reset/qcom,gcc-msm8960.h>

#include "clock-qcom.h"

#define GPLL8_STATUS        0x3158
#define APCS_GPLL_ENA_VOTE  0x34c0
#define GPLL8_STATUS_ACTIVE BIT(16)

#define SDC1_HCLK_CTL_REG       0x2820
#define SDC1_APPS_CLK_MD_REG    0x2828
#define SDC1_APPS_CLK_NS_REG    0x282c
#define SDC1_RESET_REG          0x2830

#define USB_HS1_HCLK_CTL_REG        0x2900
#define USB_HS1_XCVR_FS_CLK_MD_REG  0x2908
#define USB_HS1_XCVR_FS_CLK_NS_REG  0x290c
#define USB_HS1_RESET_REG           0x2910

#define GSBI5_H_NS        0x2a40
#define GSBI5_UART_MD     0x2a50
#define GSBI5_UART_NS     0x2a54
#define GSBI5_RESET_REG   0x2a5c
#define GSBI5_HALT        0x2fd0

#define GSBI5_UART_HALT_BIT 22
#define GSBI5_H_HALT_BIT    23

#define GSBI_H_CLK_EN    BIT(4)
#define GSBI_H_HWC_EN    BIT(6)
#define GSBI_UART_CLK_EN BIT(9)
#define GSBI_UART_SRC_EN BIT(11)

#define LEGACY_BRANCH_ENABLE BIT(9)
#define LEGACY_MND_RESET     BIT(7)
#define LEGACY_MND_ENABLE    BIT(8)
#define LEGACY_ROOT_ENABLE   BIT(11)

#define RCG_SRC_SEL_MASK  GENMASK(2, 0)
#define RCG_PRE_DIV_MASK  GENMASK(4, 3)
#define RCG_MN_MODE_MASK  GENMASK(6, 5)
#define RCG_MN_MODE_DUAL  (2 << 5)
#define RCG_MN_RESET      BIT(7)
#define RCG_MN_EN         BIT(8)
#define RCG_MND_MASK      GENMASK(15, 0)
#define RCG_N_VAL_SHIFT   16
#define RCG_M_VAL_SHIFT   16
#define RCG_PRE_DIV_SHIFT 3

#define MSM8960_SRC_PLL8 3

struct msm8960_rcg_rate {
	uint rate;
	u32 md;
	u32 ns;
	bool mnd_enable;
};

static const struct pll_vote_clk pll8_vote_clk = {
	.status = GPLL8_STATUS,
	.status_bit = GPLL8_STATUS_ACTIVE,
	.ena_vote = APCS_GPLL_ENA_VOTE,
	.vote_bit = BIT(8),
};

static const struct msm8960_rcg_rate msm8960_sdc1_rates[] = {
	{   400000, 0x0001000f, 0x0010005b, true },
	{ 48000000, 0x000100fd, 0x00fe005b, true },
	{ 96000000, 0x000000ff, 0x0000001b, false },
};

static const struct msm8960_rcg_rate msm8960_usb_hs1_rates[] = {
	{ 60000000, 0x000500df, 0x00e40043, true },
};

static const struct freq_tbl gsbi_uart_freqs[] = {
	{  1843200, MSM8960_SRC_PLL8, 2,  6, 625 },
	{  3686400, MSM8960_SRC_PLL8, 2, 12, 625 },
	{  7372800, MSM8960_SRC_PLL8, 2, 24, 625 },
	{ 14745600, MSM8960_SRC_PLL8, 2, 48, 625 },
	{ }
};

static const struct gate_clk msm8960_clks[] = {
	GATE_CLK(SDC1_H_CLK, SDC1_HCLK_CTL_REG, BIT(4)),
	GATE_CLK(SDC1_CLK, SDC1_APPS_CLK_NS_REG, LEGACY_BRANCH_ENABLE),
	GATE_CLK(USB_HS1_H_CLK, USB_HS1_HCLK_CTL_REG, BIT(4)),
	GATE_CLK(USB_HS1_XCVR_CLK, USB_HS1_XCVR_FS_CLK_NS_REG,
		 LEGACY_BRANCH_ENABLE),
	GATE_CLK(GSBI5_H_CLK, GSBI5_H_NS, GSBI_H_CLK_EN),
	GATE_CLK(GSBI5_UART_CLK, GSBI5_UART_NS, GSBI_UART_CLK_EN),
};

static const struct qcom_reset_map msm8960_gcc_resets[] = {
	[SDC1_RESET] = { SDC1_RESET_REG, 0 },
	[USB_HS1_RESET] = { USB_HS1_RESET_REG, 0 },
	[GSBI5_RESET] = { GSBI5_RESET_REG, 0 },
};

static int msm8960_branch_wait(phys_addr_t base, unsigned int halt_bit)
{
	u32 count;

	for (count = 0; count < 200; count++) {
		if (!(readl(base + GSBI5_HALT) & BIT(halt_bit)))
			return 0;
		udelay(1);
	}

	log_warning("MSM8960 GCC branch clock %u stuck off\n", halt_bit);
	return -EBUSY;
}

static int msm8960_enable_gsbi5_h_clk(phys_addr_t base)
{
	setbits_le32(base + GSBI5_H_NS, GSBI_H_CLK_EN);

	if (readl(base + GSBI5_H_NS) & GSBI_H_HWC_EN)
		return 0;

	return msm8960_branch_wait(base, GSBI5_H_HALT_BIT);
}

static const struct msm8960_rcg_rate *msm8960_find_rate(
		const struct msm8960_rcg_rate *rates, size_t count, uint rate)
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

static void msm8960_rcg_set_rate(phys_addr_t base, const struct freq_tbl *freq)
{
	u32 md;
	u32 ns;

	setbits_le32(base + GSBI5_UART_NS, RCG_MN_RESET);

	md = (freq->m << RCG_M_VAL_SHIFT) | (~freq->n & RCG_MND_MASK);
	writel(md, base + GSBI5_UART_MD);

	ns = readl(base + GSBI5_UART_NS);
	ns &= ~(RCG_SRC_SEL_MASK | RCG_PRE_DIV_MASK | RCG_MN_MODE_MASK |
		RCG_MN_EN | (RCG_MND_MASK << RCG_N_VAL_SHIFT));

	if (freq->n) {
		ns |= RCG_MN_EN | RCG_MN_MODE_DUAL;
		ns |= (~(freq->n - freq->m) & RCG_MND_MASK) << RCG_N_VAL_SHIFT;
	}

	ns |= (freq->pre_div - 1) << RCG_PRE_DIV_SHIFT;
	ns |= freq->src;
	writel(ns, base + GSBI5_UART_NS);

	clrbits_le32(base + GSBI5_UART_NS, RCG_MN_RESET);
}

static ulong msm8960_gsbi5_uart_set_rate(struct msm_clk_priv *priv, ulong rate)
{
	const struct freq_tbl *freq = qcom_find_freq(gsbi_uart_freqs, rate);
	int ret;

	if (!freq || !freq->freq)
		return 0;

	clk_enable_gpll0(priv->base, &pll8_vote_clk);
	clrbits_le32(priv->base + GSBI5_RESET_REG, BIT(0));
	msm8960_rcg_set_rate(priv->base, freq);

	setbits_le32(priv->base + GSBI5_UART_NS, GSBI_UART_SRC_EN |
		      GSBI_UART_CLK_EN);
	ret = msm8960_branch_wait(priv->base, GSBI5_UART_HALT_BIT);
	if (ret)
		return 0;

	ret = msm8960_enable_gsbi5_h_clk(priv->base);
	if (ret)
		return 0;

	return freq->freq;
}

static ulong msm8960_clk_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	switch (clk->id) {
	case SDC1_CLK:
		qcom_gate_clk_en(priv, SDC1_H_CLK);
		clrbits_le32(priv->base + SDC1_RESET_REG, BIT(0));
		return msm8960_set_rate_mnd(priv->base, SDC1_APPS_CLK_MD_REG,
					       SDC1_APPS_CLK_NS_REG,
					       msm8960_sdc1_rates,
					       ARRAY_SIZE(msm8960_sdc1_rates), rate);
	case USB_HS1_XCVR_CLK:
		qcom_gate_clk_en(priv, USB_HS1_H_CLK);
		clrbits_le32(priv->base + USB_HS1_RESET_REG, BIT(0));
		return msm8960_set_rate_mnd(priv->base, USB_HS1_XCVR_FS_CLK_MD_REG,
					       USB_HS1_XCVR_FS_CLK_NS_REG,
					       msm8960_usb_hs1_rates,
					       ARRAY_SIZE(msm8960_usb_hs1_rates), rate);
	case GSBI5_UART_SRC:
	case GSBI5_UART_CLK:
		return msm8960_gsbi5_uart_set_rate(priv, rate);
	default:
		return 0;
	}
}

static ulong msm8960_clk_get_rate(struct clk *clk)
{
	switch (clk->id) {
	case SDC1_CLK:
		return 48000000;
	case USB_HS1_XCVR_CLK:
		return 60000000;
	case GSBI5_UART_SRC:
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
		return qcom_gate_clk_en(priv, clk->id);
	case USB_HS1_XCVR_CLK:
		msm8960_clk_set_rate(clk, 60000000);
		return qcom_gate_clk_en(priv, clk->id);
	case GSBI5_H_CLK:
		return msm8960_enable_gsbi5_h_clk(priv->base);
	case GSBI5_UART_SRC:
		setbits_le32(priv->base + GSBI5_UART_NS, GSBI_UART_SRC_EN);
		return 0;
	case GSBI5_UART_CLK:
		setbits_le32(priv->base + GSBI5_UART_NS, GSBI_UART_CLK_EN);
		return msm8960_branch_wait(priv->base, GSBI5_UART_HALT_BIT);
	default:
		return qcom_gate_clk_en(priv, clk->id);
	}
}

static struct msm_clk_data msm8960_clk_data = {
	.clks = msm8960_clks,
	.num_clks = ARRAY_SIZE(msm8960_clks),
	.resets = msm8960_gcc_resets,
	.num_resets = ARRAY_SIZE(msm8960_gcc_resets),
	.set_rate = msm8960_clk_set_rate,
	.get_rate = msm8960_clk_get_rate,
	.enable = msm8960_clk_enable,
};

static const struct udevice_id gcc_msm8960_of_match[] = {
	{
		.compatible = "qcom,gcc-msm8960",
		.data = (ulong)&msm8960_clk_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_msm8960) = {
	.name = "gcc_msm8960",
	.id = UCLASS_NOP,
	.of_match = gcc_msm8960_of_match,
	.bind = qcom_cc_bind,
	.flags = DM_FLAG_PRE_RELOC,
};
