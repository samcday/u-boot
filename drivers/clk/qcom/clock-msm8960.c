// SPDX-License-Identifier: GPL-2.0+

#define LOG_DEBUG

#include <clk-uclass.h>
#include <dm.h>
#include <errno.h>
#include <power-domain.h>
#include <asm/io.h>
#include <dt-bindings/clock/qcom,mmcc-msm8960.h>
#include <dt-bindings/reset/qcom,mmcc-msm8960.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <dt-bindings/clock/qcom,gcc-msm8960.h>
#include <dt-bindings/reset/qcom,gcc-msm8960.h>

#include "clock-qcom.h"

#define GPLL8_STATUS        0x3158
#define APCS_GPLL_ENA_VOTE  0x34c0
#define GPLL8_STATUS_ACTIVE BIT(16)

#define SDC1_H_NS    0x2820
#define SDC1_MD      0x2828
#define SDC1_NS      0x282c
#define SDC1_RESET_REG 0x2830

#define USB_HS1_H_NS    0x2900
#define USB_HS1_MD      0x2908
#define USB_HS1_NS      0x290c
#define USB_HS1_RESET_REG 0x2910

#define GSBI5_H_NS    0x2a40
#define GSBI5_UART_MD 0x2a50
#define GSBI5_UART_NS 0x2a54
#define GSBI5_RESET_REG 0x2a5c
#define GSBI5_HALT    0x2fd0

#define GSBI5_UART_HALT_BIT 22
#define GSBI5_H_HALT_BIT    23

#define LEGACY_H_CLK_EN BIT(4)
#define LEGACY_BRANCH_EN BIT(9)
#define LEGACY_SRC_EN BIT(11)

#define GSBI_H_HWC_EN BIT(6)

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

#ifndef MDP_PD
#define MDP_PD			4
#endif

#define MSM8960_GCC_BASE	0x00900000
#define MSM8960_MDP_RATE	128000000
#define MSM8960_PLL8_LOCK_TIMEOUT_US	2000

#define MMCC_AHB_EN_REG		0x0008
#define MMCC_MAXI_EN_REG	0x0018
#define MMCC_MDP_CC_REG		0x00c0
#define MMCC_MDP_MD0_REG	0x00c4
#define MMCC_MDP_MD1_REG	0x00c8
#define MMCC_MDP_NS_REG		0x00d0
#define MMCC_MDP_PD_CTL_REG	0x0190
#define MMCC_MDP_LUT_CC_REG	0x016c
#define MMCC_DBG_BUS_VEC_C_REG	0x01d0
#define MMCC_DBG_BUS_VEC_E_REG	0x01d8
#define MMCC_DBG_BUS_VEC_F_REG	0x01dc
#define MMCC_DBG_BUS_VEC_I_REG	0x01e8
#define MMCC_SW_RESET_AXI_REG	0x0208
#define MMCC_SW_RESET_AHB_REG	0x020c
#define MMCC_SW_RESET_CORE_REG	0x0210

#define MMCC_MDP_AHB_EN	BIT(10)
#define MMCC_MDP_AXI_EN	BIT(23)
#define MMCC_MDP_CLK_BRANCH_EN	BIT(0)
#define MMCC_MDP_CLK_ROOT_EN	BIT(2)
#define MMCC_MDP_LUT_EN	BIT(0)

#define MMCC_MDP_CC_BANK_SEL		BIT(11)
#define MMCC_MDP_CC_BANK0_MND_EN	BIT(8)
#define MMCC_MDP_CC_BANK1_MND_EN	BIT(5)
#define MMCC_MDP_CC_BANK0_MODE_MASK	GENMASK(10, 9)
#define MMCC_MDP_CC_BANK1_MODE_MASK	GENMASK(7, 6)
#define MMCC_MDP_CC_BANK0_MODE_DUAL	(2 << 9)
#define MMCC_MDP_CC_BANK1_MODE_DUAL	(2 << 6)

#define MMCC_MDP_NS_BANK0_RST		BIT(31)
#define MMCC_MDP_NS_BANK1_RST		BIT(30)
#define MMCC_MDP_NS_BANK0_N_MASK	GENMASK(29, 22)
#define MMCC_MDP_NS_BANK1_N_MASK	GENMASK(21, 14)
#define MMCC_MDP_NS_BANK0_SRC_MASK	GENMASK(5, 3)
#define MMCC_MDP_NS_BANK1_SRC_MASK	GENMASK(2, 0)
#define MMCC_MDP_SRC_PLL8_CFG		2

#define MMCC_MDP_PD_DELAY_MASK		GENMASK(4, 0)
#define MMCC_MDP_PD_DELAY_VAL		31
#define MMCC_MDP_PD_CLAMP		BIT(5)
#define MMCC_MDP_PD_ENABLE		BIT(8)
#define MMCC_MDP_PD_RETENTION		BIT(9)

#define MMCC_MDP_AXI_RESET		BIT(13)
#define MMCC_MDP_AHB_RESET		BIT(3)
#define MMCC_MDP_RESET			BIT(21)

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

static const struct freq_tbl gsbi_uart_freqs[] = {
	{  1843200, MSM8960_SRC_PLL8, 2,  6, 625 },
	{  3686400, MSM8960_SRC_PLL8, 2, 12, 625 },
	{  7372800, MSM8960_SRC_PLL8, 2, 24, 625 },
	{ 14745600, MSM8960_SRC_PLL8, 2, 48, 625 },
	{ }
};

static const struct msm8960_rcg_rate sdc1_freqs[] = {
	{   400000, 0x0001000f, 0x0010005b, true },
	{ 48000000, 0x000100fd, 0x00fe005b, true },
	{ 96000000, 0x000000ff, 0x0000001b, false },
};

static const struct msm8960_rcg_rate usb_hs1_freqs[] = {
	{ 60000000, 0x000500df, 0x00e40043, true },
};

static const struct gate_clk msm8960_clks[] = {
	GATE_CLK(SDC1_H_CLK, SDC1_H_NS, LEGACY_H_CLK_EN),
	GATE_CLK(SDC1_CLK, SDC1_NS, LEGACY_BRANCH_EN),
	GATE_CLK(USB_HS1_H_CLK, USB_HS1_H_NS, LEGACY_H_CLK_EN),
	GATE_CLK(USB_HS1_XCVR_CLK, USB_HS1_NS, LEGACY_BRANCH_EN),
	GATE_CLK(GSBI5_H_CLK, GSBI5_H_NS, LEGACY_H_CLK_EN),
	GATE_CLK(GSBI5_UART_CLK, GSBI5_UART_NS, LEGACY_BRANCH_EN),
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
	setbits_le32(base + GSBI5_H_NS, LEGACY_H_CLK_EN);

	if (readl(base + GSBI5_H_NS) & GSBI_H_HWC_EN)
		return 0;

	return msm8960_branch_wait(base, GSBI5_H_HALT_BIT);
}

static void msm8960_flush_write(phys_addr_t base, uint reg, u32 val)
{
	writel(val, base + reg);
	readl(base + reg);
}

static int msm8960_mmcc_enable_pll8_vote(void)
{
	phys_addr_t base = MSM8960_GCC_BASE;
	u32 status, vote;
	int i;

	status = readl(base + pll8_vote_clk.status);
	vote = readl(base + pll8_vote_clk.ena_vote);
	debug("%s: enter status=%08x vote=%08x\n", __func__, status, vote);

	if (status & pll8_vote_clk.status_bit) {
		debug("%s: already locked\n", __func__);
		return 0;
	}

	setbits_le32(base + pll8_vote_clk.ena_vote, pll8_vote_clk.vote_bit);

	for (i = 0; i < MSM8960_PLL8_LOCK_TIMEOUT_US; i++) {
		status = readl(base + pll8_vote_clk.status);
		if (status & pll8_vote_clk.status_bit) {
			debug("%s: locked after %d us\n", __func__, i);
			return 0;
		}
		udelay(1);
	}

	vote = readl(base + pll8_vote_clk.ena_vote);
	log_err("%s: PLL8 vote timed out status=%08x vote=%08x\n",
		__func__, status, vote);

	return -ETIMEDOUT;
}

static int msm8960_mmcc_wait_halt(phys_addr_t base, const char *name,
				  uint reg, uint bit)
{
	u32 val;
	int i;

	for (i = 0; i < 200; i++) {
		val = readl(base + reg);
		if (!(val & BIT(bit))) {
			debug("%s: %s active after %d us reg[%#x]=%08x\n",
			      __func__, name, i, reg, val);
			return 0;
		}
		udelay(1);
	}

	val = readl(base + reg);
	log_err("%s still halted: reg[%#x]=%08x bit=%u\n",
		name, reg, val, bit);

	return -ETIMEDOUT;
}

static int msm8960_mmcc_wait_mdp_clocks(phys_addr_t base)
{
	int ret;

	ret = msm8960_mmcc_wait_halt(base, "mdp_axi_clk",
				     MMCC_DBG_BUS_VEC_E_REG, 8);
	if (ret)
		return ret;

	ret = msm8960_mmcc_wait_halt(base, "mdp_ahb_clk",
				     MMCC_DBG_BUS_VEC_F_REG, 11);
	if (ret)
		return ret;

	ret = msm8960_mmcc_wait_halt(base, "mdp_clk",
				     MMCC_DBG_BUS_VEC_C_REG, 10);
	if (ret)
		return ret;

	return msm8960_mmcc_wait_halt(base, "mdp_lut_clk",
				      MMCC_DBG_BUS_VEC_I_REG, 13);
}

static bool msm8960_mmcc_mdp_src_is_128mhz(phys_addr_t base, bool bank)
{
	u32 ns = readl(base + MMCC_MDP_NS_REG);
	u32 md, n_mask, src_mask;
	uint n_shift, src_shift, md_reg;

	if (bank) {
		n_mask = MMCC_MDP_NS_BANK1_N_MASK;
		src_mask = MMCC_MDP_NS_BANK1_SRC_MASK;
		n_shift = 14;
		src_shift = 0;
		md_reg = MMCC_MDP_MD1_REG;
	} else {
		n_mask = MMCC_MDP_NS_BANK0_N_MASK;
		src_mask = MMCC_MDP_NS_BANK0_SRC_MASK;
		n_shift = 22;
		src_shift = 3;
		md_reg = MMCC_MDP_MD0_REG;
	}

	md = readl(base + md_reg);

	return (md & 0x1ff) == 0x1fc &&
	       ((ns & n_mask) >> n_shift) == ((~(3 - 1)) & 0xff) &&
	       ((ns & src_mask) >> src_shift) == MMCC_MDP_SRC_PLL8_CFG;
}

static ulong msm8960_mmcc_set_mdp_src_rate(phys_addr_t base, ulong rate)
{
	u32 cc = readl(base + MMCC_MDP_CC_REG);
	u32 ns = readl(base + MMCC_MDP_NS_REG);
	u32 rst_mask, n_mask, src_mask, mode_mask, mode_dual, mnd_en;
	uint n_shift, src_shift, md_reg;
	bool bank, new_bank, enabled;
	u32 n_val;
	int ret;

	debug("%s: base=%#llx rate=%lu cc=%08x ns=%08x\n", __func__,
	      (unsigned long long)base, rate, cc, ns);

	if (rate && rate > MSM8960_MDP_RATE) {
		debug("%s: rejecting rate=%lu\n", __func__, rate);
		return 0;
	}

	ret = msm8960_mmcc_enable_pll8_vote();
	if (ret)
		return 0;

	enabled = cc & MMCC_MDP_CLK_ROOT_EN;
	bank = cc & MMCC_MDP_CC_BANK_SEL;
	if (enabled && msm8960_mmcc_mdp_src_is_128mhz(base, bank)) {
		debug("%s: already at %u Hz on bank %u\n", __func__,
		      MSM8960_MDP_RATE, bank);
		return MSM8960_MDP_RATE;
	}

	new_bank = enabled ? !bank : bank;
	debug("%s: programming bank %u enabled=%u old_bank=%u\n", __func__,
	      new_bank, enabled, bank);

	if (new_bank) {
		rst_mask = MMCC_MDP_NS_BANK1_RST;
		n_mask = MMCC_MDP_NS_BANK1_N_MASK;
		src_mask = MMCC_MDP_NS_BANK1_SRC_MASK;
		mode_mask = MMCC_MDP_CC_BANK1_MODE_MASK;
		mode_dual = MMCC_MDP_CC_BANK1_MODE_DUAL;
		mnd_en = MMCC_MDP_CC_BANK1_MND_EN;
		n_shift = 14;
		src_shift = 0;
		md_reg = MMCC_MDP_MD1_REG;
	} else {
		rst_mask = MMCC_MDP_NS_BANK0_RST;
		n_mask = MMCC_MDP_NS_BANK0_N_MASK;
		src_mask = MMCC_MDP_NS_BANK0_SRC_MASK;
		mode_mask = MMCC_MDP_CC_BANK0_MODE_MASK;
		mode_dual = MMCC_MDP_CC_BANK0_MODE_DUAL;
		mnd_en = MMCC_MDP_CC_BANK0_MND_EN;
		n_shift = 22;
		src_shift = 3;
		md_reg = MMCC_MDP_MD0_REG;
	}

	/*
	 * 384 MHz PLL8 * 1 / 3 = 128 MHz. The legacy M/N/D encoding stores
	 * NOT(N) in MD[7:0] and NOT(N-M) in NS.
	 */
	msm8960_flush_write(base, MMCC_MDP_NS_REG, ns | rst_mask);
	msm8960_flush_write(base, md_reg, 0x000001fc);

	ns = readl(base + MMCC_MDP_NS_REG);
	ns &= ~(n_mask | src_mask);
	n_val = (~(3 - 1)) & 0xff;
	ns |= n_val << n_shift;
	ns |= MMCC_MDP_SRC_PLL8_CFG << src_shift;
	msm8960_flush_write(base, MMCC_MDP_NS_REG, ns);

	cc = readl(base + MMCC_MDP_CC_REG);
	cc &= ~(mode_mask | mnd_en);
	cc |= mode_dual | mnd_en | MMCC_MDP_CLK_ROOT_EN;
	msm8960_flush_write(base, MMCC_MDP_CC_REG, cc);

	ns = readl(base + MMCC_MDP_NS_REG);
	ns &= ~rst_mask;
	msm8960_flush_write(base, MMCC_MDP_NS_REG, ns);

	if (enabled) {
		cc = readl(base + MMCC_MDP_CC_REG);
		cc ^= MMCC_MDP_CC_BANK_SEL;
		cc |= MMCC_MDP_CLK_ROOT_EN;
		msm8960_flush_write(base, MMCC_MDP_CC_REG, cc);
	}

	debug("%s: done cc=%08x ns=%08x md0=%08x md1=%08x\n", __func__,
	      readl(base + MMCC_MDP_CC_REG), readl(base + MMCC_MDP_NS_REG),
	      readl(base + MMCC_MDP_MD0_REG), readl(base + MMCC_MDP_MD1_REG));

	return MSM8960_MDP_RATE;
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

static uint msm8960_set_rate_raw(phys_addr_t base, uint md_reg, uint ns_reg,
					 const struct msm8960_rcg_rate *rates,
					 size_t count, uint rate)
{
	const struct msm8960_rcg_rate *cfg;
	u32 ns;

	cfg = msm8960_find_rate(rates, count, rate);
	ns = cfg->ns | LEGACY_SRC_EN;
	if (cfg->mnd_enable)
		ns |= RCG_MN_EN;

	clk_enable_gpll0(base, &pll8_vote_clk);

	/* Hold the M/N counter in reset while programming the legacy NS/MD RCG. */
	writel(ns | RCG_MN_RESET, base + ns_reg);
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

	setbits_le32(priv->base + GSBI5_UART_NS, LEGACY_SRC_EN |
		      LEGACY_BRANCH_EN);
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
		return msm8960_set_rate_raw(priv->base, SDC1_MD, SDC1_NS,
						    sdc1_freqs, ARRAY_SIZE(sdc1_freqs), rate);
	case USB_HS1_XCVR_CLK:
		qcom_gate_clk_en(priv, USB_HS1_H_CLK);
		clrbits_le32(priv->base + USB_HS1_RESET_REG, BIT(0));
		return msm8960_set_rate_raw(priv->base, USB_HS1_MD, USB_HS1_NS,
						    usb_hs1_freqs, ARRAY_SIZE(usb_hs1_freqs), rate);
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
		setbits_le32(priv->base + GSBI5_UART_NS, LEGACY_SRC_EN);
		return 0;
	case GSBI5_UART_CLK:
		setbits_le32(priv->base + GSBI5_UART_NS, LEGACY_BRANCH_EN);
		return msm8960_branch_wait(priv->base, GSBI5_UART_HALT_BIT);
	default:
		return qcom_gate_clk_en(priv, clk->id);
	}
}

static const struct gate_clk msm8960_mmcc_clks[] = {
	GATE_CLK(MDP_AHB_CLK, MMCC_AHB_EN_REG, MMCC_MDP_AHB_EN),
	GATE_CLK(MDP_AXI_CLK, MMCC_MAXI_EN_REG, MMCC_MDP_AXI_EN),
	GATE_CLK(MDP_SRC, MMCC_MDP_CC_REG, MMCC_MDP_CLK_ROOT_EN),
	GATE_CLK(MDP_CLK, MMCC_MDP_CC_REG, MMCC_MDP_CLK_BRANCH_EN),
	GATE_CLK(MDP_LUT_CLK, MMCC_MDP_LUT_CC_REG, MMCC_MDP_LUT_EN),
};

static const struct qcom_reset_map msm8960_mmcc_resets[] = {
	[MPD_AXI_RESET]	= { MMCC_SW_RESET_AXI_REG, 13 },
	[MDP_AHB_RESET]	= { MMCC_SW_RESET_AHB_REG, 3 },
	[MDP_RESET]	= { MMCC_SW_RESET_CORE_REG, 21 },
};

static const struct qcom_power_map msm8960_mmcc_power_domains[] = {
	[MDP_PD]	= { MMCC_MDP_PD_CTL_REG },
};

static ulong msm8960_mmcc_clk_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	debug("%s: id=%lu rate=%lu\n", __func__, clk->id, rate);

	switch (clk->id) {
	case MDP_SRC:
	case MDP_CLK:
	case MDP_LUT_CLK:
		return msm8960_mmcc_set_mdp_src_rate(priv->base, rate);
	default:
		return 0;
	}
}

static ulong msm8960_mmcc_clk_get_rate(struct clk *clk)
{
	switch (clk->id) {
	case MDP_SRC:
	case MDP_CLK:
	case MDP_LUT_CLK:
		return MSM8960_MDP_RATE;
	default:
		return 0;
	}
}

static int msm8960_mmcc_clk_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);
	phys_addr_t base = priv->base;
	ulong rate;

	debug("%s: id=%lu base=%#llx\n", __func__, clk->id,
	      (unsigned long long)base);

	switch (clk->id) {
	case MDP_SRC:
		rate = msm8960_mmcc_set_mdp_src_rate(base, MSM8960_MDP_RATE);
		return rate ? 0 : -EINVAL;
	case MDP_CLK:
		rate = msm8960_mmcc_set_mdp_src_rate(base, MSM8960_MDP_RATE);
		if (!rate)
			return -EINVAL;
		setbits_le32(base + MMCC_MDP_CC_REG,
			     MMCC_MDP_CLK_ROOT_EN | MMCC_MDP_CLK_BRANCH_EN);
		return msm8960_mmcc_wait_halt(base, "mdp_clk",
					      MMCC_DBG_BUS_VEC_C_REG, 10);
	case MDP_LUT_CLK:
		rate = msm8960_mmcc_set_mdp_src_rate(base, MSM8960_MDP_RATE);
		if (!rate)
			return -EINVAL;
		setbits_le32(base + MMCC_MDP_LUT_CC_REG, MMCC_MDP_LUT_EN);
		return msm8960_mmcc_wait_halt(base, "mdp_lut_clk",
					      MMCC_DBG_BUS_VEC_I_REG, 13);
	case MDP_AXI_CLK:
		setbits_le32(base + MMCC_MAXI_EN_REG, MMCC_MDP_AXI_EN);
		return msm8960_mmcc_wait_halt(base, "mdp_axi_clk",
					      MMCC_DBG_BUS_VEC_E_REG, 8);
	case MDP_AHB_CLK:
		setbits_le32(base + MMCC_AHB_EN_REG, MMCC_MDP_AHB_EN);
		return msm8960_mmcc_wait_halt(base, "mdp_ahb_clk",
					      MMCC_DBG_BUS_VEC_F_REG, 11);
	default:
		return qcom_gate_clk_en(priv, clk->id);
	}
}

static int msm8960_mmcc_mdp_power_on(struct power_domain *pwr)
{
	phys_addr_t base = (phys_addr_t)(uintptr_t)dev_get_priv(pwr->dev);
	int ret;

	debug("%s: dev=%s id=%lu base=%#llx\n", __func__, pwr->dev->name,
	      pwr->id, (unsigned long long)base);

	if (pwr->id != MDP_PD)
		return -ENODEV;

	debug("%s: set mdp src rate\n", __func__);
	if (!msm8960_mmcc_set_mdp_src_rate(base, MSM8960_MDP_RATE))
		return -EINVAL;

	debug("%s: enable mdp clocks\n", __func__);
	setbits_le32(base + MMCC_MAXI_EN_REG, MMCC_MDP_AXI_EN);
	setbits_le32(base + MMCC_AHB_EN_REG, MMCC_MDP_AHB_EN);
	setbits_le32(base + MMCC_MDP_CC_REG,
		     MMCC_MDP_CLK_ROOT_EN | MMCC_MDP_CLK_BRANCH_EN);
	setbits_le32(base + MMCC_MDP_LUT_CC_REG, MMCC_MDP_LUT_EN);

	debug("%s: assert mdp resets\n", __func__);
	setbits_le32(base + MMCC_SW_RESET_AXI_REG, MMCC_MDP_AXI_RESET);
	setbits_le32(base + MMCC_SW_RESET_AHB_REG, MMCC_MDP_AHB_RESET);
	setbits_le32(base + MMCC_SW_RESET_CORE_REG, MMCC_MDP_RESET);
	udelay(1);

	debug("%s: gdsc clamp\n", __func__);
	msm8960_flush_write(base, MMCC_MDP_PD_CTL_REG,
			    MMCC_MDP_PD_DELAY_VAL | MMCC_MDP_PD_CLAMP);
	udelay(1);
	debug("%s: gdsc enable with clamp\n", __func__);
	msm8960_flush_write(base, MMCC_MDP_PD_CTL_REG,
			    MMCC_MDP_PD_DELAY_VAL | MMCC_MDP_PD_ENABLE |
			    MMCC_MDP_PD_CLAMP);
	udelay(1);
	debug("%s: gdsc unclamp\n", __func__);
	msm8960_flush_write(base, MMCC_MDP_PD_CTL_REG,
			    MMCC_MDP_PD_DELAY_VAL | MMCC_MDP_PD_ENABLE);
	udelay(1);

	debug("%s: deassert mdp resets\n", __func__);
	clrbits_le32(base + MMCC_SW_RESET_AXI_REG, MMCC_MDP_AXI_RESET);
	clrbits_le32(base + MMCC_SW_RESET_AHB_REG, MMCC_MDP_AHB_RESET);
	clrbits_le32(base + MMCC_SW_RESET_CORE_REG, MMCC_MDP_RESET);

	debug("%s: wait mdp clocks\n", __func__);
	ret = msm8960_mmcc_wait_mdp_clocks(base);
	if (ret)
		return ret;

	debug("%s: done\n", __func__);
	return 0;
}

static int msm8960_mmcc_mdp_power_off(struct power_domain *pwr)
{
	phys_addr_t base = (phys_addr_t)(uintptr_t)dev_get_priv(pwr->dev);

	debug("%s: dev=%s id=%lu base=%#llx\n", __func__, pwr->dev->name,
	      pwr->id, (unsigned long long)base);

	if (pwr->id != MDP_PD)
		return -ENODEV;

	msm8960_flush_write(base, MMCC_MDP_PD_CTL_REG,
			    MMCC_MDP_PD_DELAY_VAL | MMCC_MDP_PD_CLAMP);
	udelay(1);

	return 0;
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

static struct msm_clk_data msm8960_mmcc_clk_data = {
	.clks = msm8960_mmcc_clks,
	.num_clks = ARRAY_SIZE(msm8960_mmcc_clks),
	.resets = msm8960_mmcc_resets,
	.num_resets = ARRAY_SIZE(msm8960_mmcc_resets),
	.power_domains = msm8960_mmcc_power_domains,
	.num_power_domains = ARRAY_SIZE(msm8960_mmcc_power_domains),
	.enable = msm8960_mmcc_clk_enable,
	.set_rate = msm8960_mmcc_clk_set_rate,
	.get_rate = msm8960_mmcc_clk_get_rate,
	.power_on = msm8960_mmcc_mdp_power_on,
	.power_off = msm8960_mmcc_mdp_power_off,
};

static const struct udevice_id gcc_msm8960_of_match[] = {
	{
		.compatible = "qcom,gcc-msm8960",
		.data = (ulong)&msm8960_clk_data,
	},
	{
		.compatible = "qcom,mmcc-msm8960",
		.data = (ulong)&msm8960_mmcc_clk_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_msm8960) = {
	.name		= "gcc_msm8960",
	.id		= UCLASS_NOP,
	.of_match	= gcc_msm8960_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
