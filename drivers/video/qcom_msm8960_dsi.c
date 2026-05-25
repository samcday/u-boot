// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal Qualcomm MSM8960-family DSI host support for Nokia Fame.
 *
 * This is intentionally narrow: it mirrors the known-good raw-APPSBL Fame
 * register sequence and provides enough MIPI DSI host plumbing for the Teisko
 * panel driver.
 */

#define LOG_CATEGORY UCLASS_DSI_HOST

#include <cpu_func.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <dsi_host.h>
#include <errno.h>
#include <log.h>
#include <memalign.h>
#include <mipi_dsi.h>
#include <power/regulator.h>
#include <string.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kernel.h>

#include "qcom_msm8960_display.h"

#define FAME_MMCC_BASE				0x04000000
#define FAME_DSI_BASE				0x04700000
#define FAME_DSI_PLL_BASE			0x04700200
#define FAME_DSI_PHY_BASE			0x04700300
#define FAME_DSI_PHY_REG_BASE			0x04700500
#define FAME_RPM_BASE				0x00108000
#define FAME_RPM_IPC_REG			0x02011008

#define MMCC_REG(off)				(FAME_MMCC_BASE + (off))
#define RPM_STATUS_REG(idx)			(FAME_RPM_BASE + ((idx) * 4))
#define RPM_CTRL_REG(idx)			(FAME_RPM_BASE + 0x400 + ((idx) * 4))
#define RPM_REQ_REG(idx)			(FAME_RPM_BASE + 0x600 + ((idx) * 4))

#define AHB_EN_REG				0x0008
#define DSI_CC_REG				0x004c
#define DSI_MD_REG				0x0050
#define DSI_NS_REG				0x0054
#define DSI_BYTE_CC_REG			0x0090
#define DSI_BYTE_NS_REG			0x00b0
#define DSI_ESC_CC_REG				0x00cc
#define DSI_ESC_NS_REG				0x011c
#define DSI_PIXEL_CC_REG			0x0130
#define DSI_PIXEL_MD_REG			0x0134
#define DSI_PIXEL_NS_REG			0x0138
#define DBG_BUS_VEC_B_REG			0x01cc
#define DBG_BUS_VEC_C_REG			0x01d0
#define DBG_BUS_VEC_F_REG			0x01dc
#define DBG_BUS_VEC_I_REG			0x01e8
#define SW_RESET_AHB_REG			0x020c
#define SW_RESET_CORE_REG			0x0210

#define DSI_CTRL_REG				0x0000
#define DSI_STATUS0_REG			0x0004
#define DSI_VID_CFG0_REG			0x000c
#define DSI_VID_CFG1_REG			0x001c
#define DSI_ACTIVE_H_REG			0x0020
#define DSI_ACTIVE_V_REG			0x0024
#define DSI_TOTAL_REG				0x0028
#define DSI_ACTIVE_HSYNC_REG			0x002c
#define DSI_ACTIVE_VSYNC_HPOS_REG		0x0030
#define DSI_ACTIVE_VSYNC_VPOS_REG		0x0034
#define DSI_CMD_DMA_CTRL_REG			0x0038
#define DSI_DMA_BASE_REG			0x0044
#define DSI_DMA_LEN_REG			0x0048
#define DSI_ACK_ERR_STATUS_REG			0x0064
#define DSI_TRIG_CTRL_REG			0x0080
#define DSI_TRIG_DMA_REG			0x008c
#define DSI_LANE_STATUS_REG			0x00a4
#define DSI_LANE_SWAP_CTRL_REG			0x00ac
#define DSI_TIMEOUT_STATUS_REG			0x00bc
#define DSI_CLKOUT_TIMING_CTRL_REG		0x00c0
#define DSI_EOT_PACKET_CTRL_REG		0x00c8
#define DSI_ERR_INT_MASK0_REG			0x0108
#define DSI_RESET_REG				0x0114
#define DSI_CLK_CTRL_REG			0x0118
#define DSI_CLK_STATUS_REG			0x011c
#define DSI_PHY_RESET_REG			0x0128

#define DSI_PLL_CTRL_0_REG			0x0000
#define DSI_PLL_CTRL_1_REG			0x0004
#define DSI_PLL_CTRL_2_REG			0x0008
#define DSI_PLL_CTRL_3_REG			0x000c
#define DSI_PLL_CTRL_6_REG			0x0018
#define DSI_PLL_CTRL_8_REG			0x0020
#define DSI_PLL_CTRL_9_REG			0x0024
#define DSI_PLL_CTRL_10_REG			0x0028
#define DSI_PLL_RDY_REG			0x0080

#define DSI_PHY_LN_CFG_0(lane)			(0x0000 + (0x40 * (lane)))
#define DSI_PHY_LN_CFG_1(lane)			(0x0004 + (0x40 * (lane)))
#define DSI_PHY_LN_CFG_2(lane)			(0x0008 + (0x40 * (lane)))
#define DSI_PHY_LN_TEST_DATAPATH(lane)		(0x000c + (0x40 * (lane)))
#define DSI_PHY_LN_TEST_STR_0(lane)		(0x0014 + (0x40 * (lane)))
#define DSI_PHY_LN_TEST_STR_1(lane)		(0x0018 + (0x40 * (lane)))
#define DSI_PHY_LNCK_CFG_0_REG			0x0100
#define DSI_PHY_LNCK_CFG_1_REG			0x0104
#define DSI_PHY_LNCK_CFG_2_REG			0x0108
#define DSI_PHY_LNCK_TEST_DATAPATH_REG		0x010c
#define DSI_PHY_LNCK_TEST_STR0_REG		0x0114
#define DSI_PHY_LNCK_TEST_STR1_REG		0x0118
#define DSI_PHY_TIMING_CTRL_0_REG		0x0140
#define DSI_PHY_CTRL_0_REG			0x0170
#define DSI_PHY_CTRL_1_REG			0x0174
#define DSI_PHY_CTRL_2_REG			0x0178
#define DSI_PHY_CTRL_3_REG			0x017c
#define DSI_PHY_STRENGTH_0_REG			0x0180
#define DSI_PHY_STRENGTH_1_REG			0x0184
#define DSI_PHY_STRENGTH_2_REG			0x0188
#define DSI_PHY_BIST_CTRL_0_REG		0x018c
#define DSI_PHY_BIST_CTRL_1_REG		0x0190
#define DSI_PHY_BIST_CTRL_4_REG		0x019c
#define DSI_PHY_LDO_CTRL_REG			0x01b0

#define DSI_PHY_MISC_REGULATOR_CTRL_0_REG	0x0000
#define DSI_PHY_MISC_REGULATOR_CAL_PWR_CFG_REG	0x0018
#define DSI_PHY_MISC_CAL_HW_TRIGGER_REG		0x0028
#define DSI_PHY_MISC_CAL_SW_CFG_2_REG		0x0034
#define DSI_PHY_MISC_CAL_HW_CFG_0_REG		0x0038
#define DSI_PHY_MISC_CAL_HW_CFG_1_REG		0x003c
#define DSI_PHY_MISC_CAL_HW_CFG_3_REG		0x0044
#define DSI_PHY_MISC_CAL_HW_CFG_4_REG		0x0048
#define DSI_PHY_MISC_CAL_STATUS_REG		0x0050

#define FAME_DSI_PLL_CTRL_1_VENDOR		0x25
#define FAME_DSI_PLL_CTRL_2_VENDOR		0x30
#define FAME_DSI_PLL_CTRL_3_VENDOR		0xc2
#define FAME_DSI_PLL_CTRL_6_VENDOR		0x0c
#define FAME_DSI_PLL_CTRL_8_VENDOR		0x41
#define FAME_DSI_PLL_CTRL_9_VENDOR		0x01
#define FAME_DSI_PLL_CTRL_10_VENDOR		0x01
#define FAME_DSI_NS_VENDOR			0x0000c003
#define FAME_DSI_CC_ROOT_VENDOR		0x00000004
#define FAME_DSI_BYTE_NS_VENDOR		0x00007001
#define FAME_DSI_BYTE_CC_ROOT_VENDOR		0x80ff0004
#define FAME_DSI_ESC_NS_VENDOR			0x00001000
#define FAME_DSI_ESC_CC_ROOT_VENDOR		0x00000004
#define FAME_DSI_PIXEL_NS_VENDOR		0x0000b003
#define FAME_DSI_PIXEL_CC_ROOT_VENDOR		0x80ff0004
#define FAME_DSI_LANE_SWAP			0x1

#define MMCC_DSI_AMP_AHB_EN			BIT(24)
#define MMCC_DSI_M_AHB_EN			BIT(9)
#define MMCC_DSI_S_AHB_EN			BIT(18)
#define MMCC_DSI_CLK_BRANCH_EN			BIT(0)
#define MMCC_DSI_MND_RESET			BIT(7)

#define DSI_CTRL_ENABLE			BIT(0)
#define DSI_CTRL_VID_MODE_EN			BIT(1)
#define DSI_CTRL_CMD_MODE_EN			BIT(2)
#define DSI_CTRL_LANE0				BIT(4)
#define DSI_CTRL_LANE1				BIT(5)
#define DSI_CTRL_CLK_EN			BIT(8)
#define DSI_CTRL_BASE				(DSI_CTRL_ENABLE | \
						 DSI_CTRL_CLK_EN | \
						 DSI_CTRL_LANE0 | \
						 DSI_CTRL_LANE1)
#define DSI_CTRL_VIDEO				(DSI_CTRL_BASE | \
						 DSI_CTRL_VID_MODE_EN | \
						 DSI_CTRL_CMD_MODE_EN)
#define DSI_CLK_CTRL_ENABLE_CLKS		0x0000023f
#define DSI_CMD_DMA_CTRL_LPM_FB		0x14000000
#define DSI_STATUS0_CMD_BUSY			(BIT(0) | BIT(1))
#define DSI_PHY_CAL_BUSY			BIT(4)
#define DSI_PLL_ENABLE				BIT(0)
#define DSI_PLL_READY				BIT(0)
#define DSI_PLL_LOCK_READS			1000
#define DSI_PLL_LOCK_POLL_US			100
#define DSI_DMA_TIMEOUT_US			200000

#define FAME_RPM_VERSION			3
#define FAME_RPM_REQ_CTX_OFF			3
#define FAME_RPM_REQ_SEL_OFF			11
#define FAME_RPM_ACK_CTX_OFF			15
#define FAME_RPM_ACK_SEL_OFF			23
#define FAME_RPM_REQ_SEL_SIZE			4
#define FAME_RPM_ACK_SEL_SIZE			7
#define FAME_RPM_IPC_BIT			2
#define FAME_RPM_ACTIVE_STATE			0
#define FAME_RPM_ACK_POLL_US			10
#define FAME_RPM_ACK_TIMEOUT_US		500000
#define FAME_RPM_NOTIFICATION			BIT(30)
#define FAME_RPM_REJECTED			BIT(31)
#define FAME_RPM_LDO_PULL_DOWN			BIT(23)

struct qcom_msm8960_dsi_priv {
	struct mipi_dsi_host host;
	struct mipi_dsi_device device;
	void __iomem *base;
	void __iomem *pll;
	void __iomem *phy;
	void __iomem *phy_misc;
	bool prepared;
};

struct fame_pm8038_ldo {
	const char *name;
	u32 target_id;
	u32 status_id;
	u32 select_id;
	u32 microvolts;
};

static void qcom_dsi_write(void __iomem *base, u32 reg, u32 val)
{
	writel(val, base + reg);
	readl(base + reg);
}

static void qcom_mmcc_write(u32 reg, u32 val)
{
	writel(val, MMCC_REG(reg));
	readl(MMCC_REG(reg));
}

static void qcom_mmcc_update_bits(u32 reg, u32 mask, u32 val)
{
	u32 old = readl(MMCC_REG(reg));

	qcom_mmcc_write(reg, (old & ~mask) | (val & mask));
}

static int qcom_mmcc_wait_halt(struct udevice *dev, const char *name,
			       u32 reg, u32 bit)
{
	u32 val;
	int i;

	for (i = 0; i < 200; i++) {
		val = readl(MMCC_REG(reg));
		if (!(val & BIT(bit))) {
			dev_dbg(dev, "%s active after %d us reg[%#x]=%08x\n",
				name, i, reg, val);
			return 0;
		}
		udelay(1);
	}

	val = readl(MMCC_REG(reg));
	dev_err(dev, "%s halt timeout reg[%#x]=%08x bit=%u\n",
		name, reg, val, bit);

	return -ETIMEDOUT;
}

static void fame_rpm_clear_ack(void)
{
	int i;

	for (i = 0; i < FAME_RPM_ACK_SEL_SIZE; i++)
		writel(0, RPM_CTRL_REG(FAME_RPM_ACK_SEL_OFF + i));
	writel(0, RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF));
	readl(RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF));
}

static int fame_rpm_init(struct udevice *dev)
{
	u32 fw0 = readl(RPM_STATUS_REG(0));
	u32 fw1 = readl(RPM_STATUS_REG(1));
	u32 fw2 = readl(RPM_STATUS_REG(2));

	dev_dbg(dev, "RPM firmware %u.%u.%u ctrl_ack=%08x\n",
		fw0, fw1, fw2, readl(RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF)));
	if (fw0 != FAME_RPM_VERSION)
		return -EFAULT;

	writel(fw0, RPM_CTRL_REG(0));
	writel(fw1, RPM_CTRL_REG(1));
	writel(fw2, RPM_CTRL_REG(2));
	fame_rpm_clear_ack();

	return 0;
}

static int fame_rpm_wait_ack(struct udevice *dev, const char *name)
{
	u32 ack = 0;
	int elapsed;

	for (elapsed = 0; elapsed < FAME_RPM_ACK_TIMEOUT_US;
	     elapsed += FAME_RPM_ACK_POLL_US) {
		ack = readl(RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF));
		if (!ack) {
			udelay(FAME_RPM_ACK_POLL_US);
			continue;
		}

		fame_rpm_clear_ack();
		if (ack & FAME_RPM_NOTIFICATION)
			continue;

		dev_dbg(dev, "RPM %s ack after %d us ack=%08x\n",
			name, elapsed, ack);
		if (ack & FAME_RPM_REJECTED)
			return -EIO;

		return 0;
	}

	dev_err(dev, "RPM %s ack timeout req_ctx=%08x ack_ctx=%08x\n",
		name, readl(RPM_CTRL_REG(FAME_RPM_REQ_CTX_OFF)),
		readl(RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF)));

	return -ETIMEDOUT;
}

static int fame_rpm_write(struct udevice *dev, const char *name, u32 target_id,
			  u32 select_id, const u32 *words, int count)
{
	u32 sel_mask[FAME_RPM_REQ_SEL_SIZE] = { 0 };
	int i;

	if (select_id >= FAME_RPM_REQ_SEL_SIZE * 32)
		return -EINVAL;

	fame_rpm_clear_ack();
	for (i = 0; i < count; i++)
		writel(words[i], RPM_REQ_REG(target_id + i));

	sel_mask[select_id / 32] = BIT(select_id % 32);
	for (i = 0; i < FAME_RPM_REQ_SEL_SIZE; i++)
		writel(sel_mask[i], RPM_CTRL_REG(FAME_RPM_REQ_SEL_OFF + i));

	writel(BIT(FAME_RPM_ACTIVE_STATE), RPM_CTRL_REG(FAME_RPM_REQ_CTX_OFF));
	readl(RPM_CTRL_REG(FAME_RPM_REQ_CTX_OFF));
	writel(BIT(FAME_RPM_IPC_BIT), FAME_RPM_IPC_REG);

	return fame_rpm_wait_ack(dev, name);
}

static int fame_pm8038_enable_display_rails(struct udevice *dev)
{
	static const struct fame_pm8038_ldo rails[] = {
		{ "L2", 104, 45, 37, 1200000 },
		{ "L8", 116, 57, 43, 2800000 },
		{ "L11", 122, 63, 46, 1800000 },
	};
	int ret;
	int i;

	ret = fame_rpm_init(dev);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(rails); i++) {
		u32 words[2] = {
			rails[i].microvolts | FAME_RPM_LDO_PULL_DOWN,
			0,
		};

		ret = fame_rpm_write(dev, rails[i].name, rails[i].target_id,
				     rails[i].select_id, words,
				     ARRAY_SIZE(words));
		if (ret)
			return ret;

		dev_dbg(dev, "PM8038 %s status[%u]=%08x status[%u]=%08x\n",
			rails[i].name, rails[i].status_id,
			readl(RPM_STATUS_REG(rails[i].status_id)),
			rails[i].status_id + 1,
			readl(RPM_STATUS_REG(rails[i].status_id + 1)));
	}

	mdelay(5);

	return 0;
}

static int qcom_msm8960_dsi_enable_supply(struct udevice *dev,
					  const char *supply_name)
{
	struct udevice *supply;
	int microvolts;
	int ret;

	ret = device_get_supply_regulator(dev, supply_name, &supply);
	if (ret)
		return ret;

	microvolts = regulator_get_value(supply);
	if (microvolts > 0) {
		ret = regulator_set_value(supply, microvolts);
		if (ret && ret != -ENOSYS)
			return ret;
	}

	return regulator_set_enable_if_allowed(supply, true);
}

static int qcom_msm8960_dsi_enable_supplies(struct udevice *dev)
{
	static const char * const supplies[] = {
		"vdda-supply",
		"avdd-supply",
		"vddio-supply",
	};
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(supplies); i++) {
		ret = qcom_msm8960_dsi_enable_supply(dev, supplies[i]);
		if (ret) {
			dev_warn(dev, "failed to enable %s via regulators: %d; using raw Fame RPM fallback\n",
				 supplies[i], ret);
			return fame_pm8038_enable_display_rails(dev);
		}
	}

	mdelay(5);

	return 0;
}

static int qcom_msm8960_dsi_program_mmcc(struct udevice *dev)
{
	int ret;

	qcom_mmcc_update_bits(AHB_EN_REG,
			      MMCC_DSI_AMP_AHB_EN | MMCC_DSI_M_AHB_EN |
			      MMCC_DSI_S_AHB_EN,
			      MMCC_DSI_AMP_AHB_EN | MMCC_DSI_M_AHB_EN |
			      MMCC_DSI_S_AHB_EN);

	ret = qcom_mmcc_wait_halt(dev, "amp_ahb_clk", DBG_BUS_VEC_F_REG, 18);
	if (ret)
		return ret;
	ret = qcom_mmcc_wait_halt(dev, "dsi_m_ahb_clk", DBG_BUS_VEC_F_REG, 19);
	if (ret)
		return ret;
	ret = qcom_mmcc_wait_halt(dev, "dsi_s_ahb_clk", DBG_BUS_VEC_F_REG, 21);
	if (ret)
		return ret;

	qcom_mmcc_update_bits(SW_RESET_AHB_REG, BIT(6) | BIT(5),
			      BIT(6) | BIT(5));
	qcom_mmcc_update_bits(SW_RESET_CORE_REG, BIT(20) | BIT(7),
			      BIT(20) | BIT(7));
	udelay(10);
	qcom_mmcc_update_bits(SW_RESET_AHB_REG, BIT(6) | BIT(5), 0);
	qcom_mmcc_update_bits(SW_RESET_CORE_REG, BIT(20) | BIT(7), 0);
	udelay(10);

	return 0;
}

static void qcom_msm8960_dsi_host_phy_reset(struct qcom_msm8960_dsi_priv *priv)
{
	qcom_dsi_write(priv->base, DSI_PHY_RESET_REG, 1);
	udelay(1000);
	qcom_dsi_write(priv->base, DSI_PHY_RESET_REG, 0);
	udelay(100);
}

static void qcom_msm8960_dsi_phy_enable(struct qcom_msm8960_dsi_priv *priv)
{
	static const u32 regulator[] = { 0x02, 0x08, 0x05, 0x00, 0x20 };
	static const u32 timing[] = {
		0x67, 0x16, 0x0d, 0x00, 0x38, 0x3c,
		0x12, 0x19, 0x18, 0x03, 0x04, 0xa0,
	};
	u32 status;
	int i;

	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_REGULATOR_CTRL_0_REG, 0x02);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_REGULATOR_CTRL_0_REG + 4, 1);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_REGULATOR_CTRL_0_REG + 8, 1);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_REGULATOR_CTRL_0_REG + 12, 0);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_REGULATOR_CTRL_0_REG + 16,
		       0x100);

	qcom_dsi_write(priv->phy, DSI_PHY_LDO_CTRL_REG, 0x25);
	qcom_dsi_write(priv->phy, DSI_PHY_STRENGTH_0_REG, 0xff);
	qcom_dsi_write(priv->phy, DSI_PHY_STRENGTH_1_REG, 0x00);
	qcom_dsi_write(priv->phy, DSI_PHY_STRENGTH_2_REG, 0x06);
	qcom_dsi_write(priv->phy, DSI_PHY_CTRL_0_REG, 0x5f);
	qcom_dsi_write(priv->phy, DSI_PHY_CTRL_1_REG, 0x00);
	qcom_dsi_write(priv->phy, DSI_PHY_CTRL_2_REG, 0x00);
	qcom_dsi_write(priv->phy, DSI_PHY_CTRL_3_REG, 0x10);

	for (i = 0; i < ARRAY_SIZE(regulator); i++)
		qcom_dsi_write(priv->phy_misc,
			       DSI_PHY_MISC_REGULATOR_CTRL_0_REG + (i * 4),
			       regulator[i]);

	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_REGULATOR_CAL_PWR_CFG_REG,
		       0x3);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_CAL_SW_CFG_2_REG, 0x0);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_CAL_HW_CFG_1_REG, 0x5a);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_CAL_HW_CFG_3_REG, 0x10);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_CAL_HW_CFG_4_REG, 0x1);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_CAL_HW_CFG_0_REG, 0x1);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_CAL_HW_TRIGGER_REG, 0x1);
	mdelay(5);
	qcom_dsi_write(priv->phy_misc, DSI_PHY_MISC_CAL_HW_TRIGGER_REG, 0x0);

	for (i = 0; i < 5000; i++) {
		status = readl(priv->phy_misc + DSI_PHY_MISC_CAL_STATUS_REG);
		if (!(status & DSI_PHY_CAL_BUSY))
			break;
		udelay(1);
	}

	for (i = 0; i < 4; i++) {
		qcom_dsi_write(priv->phy, DSI_PHY_LN_CFG_0(i), 0x80);
		qcom_dsi_write(priv->phy, DSI_PHY_LN_CFG_1(i), 0x45);
		qcom_dsi_write(priv->phy, DSI_PHY_LN_CFG_2(i), 0x00);
		qcom_dsi_write(priv->phy, DSI_PHY_LN_TEST_DATAPATH(i), 0x00);
		qcom_dsi_write(priv->phy, DSI_PHY_LN_TEST_STR_0(i), 0x01);
		qcom_dsi_write(priv->phy, DSI_PHY_LN_TEST_STR_1(i), 0x66);
	}

	qcom_dsi_write(priv->phy, DSI_PHY_LNCK_CFG_0_REG, 0x40);
	qcom_dsi_write(priv->phy, DSI_PHY_LNCK_CFG_1_REG, 0x67);
	qcom_dsi_write(priv->phy, DSI_PHY_LNCK_CFG_2_REG, 0x00);
	qcom_dsi_write(priv->phy, DSI_PHY_LNCK_TEST_DATAPATH_REG, 0x00);
	qcom_dsi_write(priv->phy, DSI_PHY_LNCK_TEST_STR0_REG, 0x01);
	qcom_dsi_write(priv->phy, DSI_PHY_LNCK_TEST_STR1_REG, 0x88);

	qcom_dsi_write(priv->phy, DSI_PHY_BIST_CTRL_4_REG, 0x0f);
	qcom_dsi_write(priv->phy, DSI_PHY_BIST_CTRL_1_REG, 0x03);
	qcom_dsi_write(priv->phy, DSI_PHY_BIST_CTRL_0_REG, 0x03);
	qcom_dsi_write(priv->phy, DSI_PHY_BIST_CTRL_4_REG, 0x00);

	for (i = 0; i < ARRAY_SIZE(timing); i++)
		qcom_dsi_write(priv->phy, DSI_PHY_TIMING_CTRL_0_REG + (i * 4),
			       timing[i]);
}

static int qcom_msm8960_dsi_program_pll(struct udevice *dev)
{
	struct qcom_msm8960_dsi_priv *priv = dev_get_priv(dev);
	u32 val;
	int i;

	qcom_dsi_write(priv->pll, DSI_PLL_CTRL_0_REG, 0);
	udelay(1);
	qcom_dsi_write(priv->pll, DSI_PLL_CTRL_1_REG,
		       FAME_DSI_PLL_CTRL_1_VENDOR);
	qcom_dsi_write(priv->pll, DSI_PLL_CTRL_2_REG,
		       FAME_DSI_PLL_CTRL_2_VENDOR);
	qcom_dsi_write(priv->pll, DSI_PLL_CTRL_3_REG,
		       FAME_DSI_PLL_CTRL_3_VENDOR);
	qcom_dsi_write(priv->pll, DSI_PLL_CTRL_6_REG,
		       FAME_DSI_PLL_CTRL_6_VENDOR);
	qcom_dsi_write(priv->pll, DSI_PLL_CTRL_8_REG,
		       FAME_DSI_PLL_CTRL_8_VENDOR);
	qcom_dsi_write(priv->pll, DSI_PLL_CTRL_9_REG,
		       FAME_DSI_PLL_CTRL_9_VENDOR);
	qcom_dsi_write(priv->pll, DSI_PLL_CTRL_10_REG,
		       FAME_DSI_PLL_CTRL_10_VENDOR);
	qcom_dsi_write(priv->pll, DSI_PLL_CTRL_0_REG, DSI_PLL_ENABLE);

	for (i = 0; i < DSI_PLL_LOCK_READS; i++) {
		val = readl(priv->pll + DSI_PLL_RDY_REG);
		if (val & DSI_PLL_READY)
			return 0;
		udelay(DSI_PLL_LOCK_POLL_US);
	}

	val = readl(priv->pll + DSI_PLL_RDY_REG);
	dev_err(dev, "DSI PLL did not lock c0=%08x c1=%08x c2=%08x c3=%08x c6=%08x c8=%08x c9=%08x c10=%08x rdy=%08x\n",
		readl(priv->pll + DSI_PLL_CTRL_0_REG),
		readl(priv->pll + DSI_PLL_CTRL_1_REG),
		readl(priv->pll + DSI_PLL_CTRL_2_REG),
		readl(priv->pll + DSI_PLL_CTRL_3_REG),
		readl(priv->pll + DSI_PLL_CTRL_6_REG),
		readl(priv->pll + DSI_PLL_CTRL_8_REG),
		readl(priv->pll + DSI_PLL_CTRL_9_REG),
		readl(priv->pll + DSI_PLL_CTRL_10_REG), val);

	return -ETIMEDOUT;
}

static void qcom_msm8960_program_dsi_src(void)
{
	qcom_mmcc_update_bits(DSI_NS_REG, MMCC_DSI_MND_RESET,
			      MMCC_DSI_MND_RESET);
	qcom_mmcc_write(DSI_MD_REG, 0);
	qcom_mmcc_write(DSI_NS_REG, FAME_DSI_NS_VENDOR);
	qcom_mmcc_write(DSI_CC_REG, FAME_DSI_CC_ROOT_VENDOR);
	qcom_mmcc_update_bits(DSI_NS_REG, MMCC_DSI_MND_RESET, 0);
}

static void qcom_msm8960_program_dsi_pixel(void)
{
	qcom_mmcc_update_bits(DSI_PIXEL_NS_REG, MMCC_DSI_MND_RESET,
			      MMCC_DSI_MND_RESET);
	qcom_mmcc_write(DSI_PIXEL_MD_REG, 0);
	qcom_mmcc_write(DSI_PIXEL_NS_REG, FAME_DSI_PIXEL_NS_VENDOR);
	qcom_mmcc_write(DSI_PIXEL_CC_REG, FAME_DSI_PIXEL_CC_ROOT_VENDOR);
	qcom_mmcc_update_bits(DSI_PIXEL_NS_REG, MMCC_DSI_MND_RESET, 0);
}

static int qcom_msm8960_dsi_enable_branch(struct udevice *dev,
					  const char *name, u32 cc_reg,
					  u32 halt_reg, u32 halt_bit)
{
	qcom_mmcc_update_bits(cc_reg, MMCC_DSI_CLK_BRANCH_EN,
			      MMCC_DSI_CLK_BRANCH_EN);

	return qcom_mmcc_wait_halt(dev, name, halt_reg, halt_bit);
}

static int qcom_msm8960_dsi_program_link_clocks(struct udevice *dev)
{
	struct qcom_msm8960_dsi_priv *priv = dev_get_priv(dev);
	int ret;
	int first_ret = 0;

	qcom_mmcc_write(DSI_BYTE_NS_REG, FAME_DSI_BYTE_NS_VENDOR);
	qcom_mmcc_write(DSI_BYTE_CC_REG, FAME_DSI_BYTE_CC_ROOT_VENDOR);
	qcom_mmcc_write(DSI_ESC_NS_REG, FAME_DSI_ESC_NS_VENDOR);
	qcom_mmcc_write(DSI_ESC_CC_REG, FAME_DSI_ESC_CC_ROOT_VENDOR);
	qcom_msm8960_program_dsi_src();
	qcom_msm8960_program_dsi_pixel();

	qcom_dsi_write(priv->base, DSI_CLK_CTRL_REG, DSI_CLK_CTRL_ENABLE_CLKS);

	ret = qcom_msm8960_dsi_enable_branch(dev, "dsi1_byte_clk",
					     DSI_BYTE_CC_REG,
					     DBG_BUS_VEC_B_REG, 21);
	if (ret && !first_ret)
		first_ret = ret;
	ret = qcom_msm8960_dsi_enable_branch(dev, "dsi1_esc_clk",
					     DSI_ESC_CC_REG,
					     DBG_BUS_VEC_I_REG, 1);
	if (ret && !first_ret)
		first_ret = ret;
	ret = qcom_msm8960_dsi_enable_branch(dev, "dsi1_clk", DSI_CC_REG,
					     DBG_BUS_VEC_C_REG, 2);
	if (ret && !first_ret)
		first_ret = ret;
	ret = qcom_msm8960_dsi_enable_branch(dev, "dsi1_pixel_clk",
					     DSI_PIXEL_CC_REG,
					     DBG_BUS_VEC_C_REG, 6);
	if (ret && !first_ret)
		first_ret = ret;

	if (first_ret)
		dev_warn(dev, "continuing despite MMCC halt timeout; host clk_status=%08x\n",
			 readl(priv->base + DSI_CLK_STATUS_REG));

	return 0;
}

static void qcom_msm8960_dsi_sw_reset(struct qcom_msm8960_dsi_priv *priv)
{
	u32 ctrl = readl(priv->base + DSI_CTRL_REG);

	if (ctrl & DSI_CTRL_ENABLE)
		qcom_dsi_write(priv->base, DSI_CTRL_REG,
			       ctrl & ~DSI_CTRL_ENABLE);

	qcom_dsi_write(priv->base, DSI_CLK_CTRL_REG, DSI_CLK_CTRL_ENABLE_CLKS);
	qcom_dsi_write(priv->base, DSI_RESET_REG, 1);
	mdelay(20);
	qcom_dsi_write(priv->base, DSI_RESET_REG, 0);
}

static void qcom_msm8960_dsi_host_setup(struct qcom_msm8960_dsi_priv *priv)
{
	qcom_msm8960_dsi_sw_reset(priv);
	qcom_dsi_write(priv->base, DSI_CTRL_REG, 0);
	qcom_dsi_write(priv->base, DSI_ACTIVE_H_REG, 0x02100030);
	qcom_dsi_write(priv->base, DSI_ACTIVE_V_REG, 0x032f000f);
	qcom_dsi_write(priv->base, DSI_TOTAL_REG, 0x033c023c);
	qcom_dsi_write(priv->base, DSI_ACTIVE_HSYNC_REG, 0x00040000);
	qcom_dsi_write(priv->base, DSI_ACTIVE_VSYNC_HPOS_REG, 0);
	qcom_dsi_write(priv->base, DSI_ACTIVE_VSYNC_VPOS_REG, 0x00010000);
	qcom_dsi_write(priv->base, DSI_VID_CFG0_REG, 0x00009130);
	qcom_dsi_write(priv->base, DSI_VID_CFG1_REG, 0);
	qcom_dsi_write(priv->base, DSI_CMD_DMA_CTRL_REG,
		       DSI_CMD_DMA_CTRL_LPM_FB);
	qcom_dsi_write(priv->base, DSI_TRIG_CTRL_REG, 0x00000004);
	qcom_dsi_write(priv->base, DSI_CLKOUT_TIMING_CTRL_REG, 0x00000318);
	qcom_dsi_write(priv->base, DSI_EOT_PACKET_CTRL_REG, 1);
	qcom_dsi_write(priv->base, DSI_ERR_INT_MASK0_REG, 0x13ff3fe0);
	qcom_dsi_write(priv->base, DSI_CLK_CTRL_REG, DSI_CLK_CTRL_ENABLE_CLKS);
	qcom_dsi_write(priv->base, DSI_LANE_SWAP_CTRL_REG, FAME_DSI_LANE_SWAP);
	qcom_dsi_write(priv->base, DSI_CTRL_REG, DSI_CTRL_BASE);
}

int qcom_msm8960_dsi_prepare(struct udevice *dev)
{
	struct qcom_msm8960_dsi_priv *priv = dev_get_priv(dev);
	int ret;

	if (priv->prepared)
		return 0;

	ret = qcom_msm8960_dsi_enable_supplies(dev);
	if (ret)
		return ret;

	ret = qcom_msm8960_dsi_program_mmcc(dev);
	if (ret)
		return ret;

	qcom_msm8960_dsi_host_phy_reset(priv);
	qcom_msm8960_dsi_phy_enable(priv);

	ret = qcom_msm8960_dsi_program_pll(dev);
	if (ret)
		return ret;

	ret = qcom_msm8960_dsi_program_link_clocks(dev);
	if (ret)
		return ret;

	qcom_msm8960_dsi_host_setup(priv);
	priv->prepared = true;

	return 0;
}

int qcom_msm8960_dsi_enable_video(struct udevice *dev)
{
	struct qcom_msm8960_dsi_priv *priv = dev_get_priv(dev);

	qcom_dsi_write(priv->base, DSI_CTRL_REG, DSI_CTRL_VIDEO);

	return 0;
}

struct mipi_dsi_device *qcom_msm8960_dsi_device(struct udevice *dev)
{
	struct qcom_msm8960_dsi_priv *priv = dev_get_priv(dev);

	return &priv->device;
}

static int qcom_msm8960_dsi_wait_dma_idle(struct udevice *dev,
					  struct qcom_msm8960_dsi_priv *priv,
					  const char *name)
{
	u32 status = 0;
	int i;

	for (i = 0; i < DSI_DMA_TIMEOUT_US; i++) {
		status = readl(priv->base + DSI_STATUS0_REG);
		if (!(status & DSI_STATUS0_CMD_BUSY))
			return 0;
		udelay(1);
	}

	dev_err(dev, "DSI DMA timeout after %s status0=%08x ack=%08x timeout=%08x lane=%08x swap=%08x\n",
		name, status, readl(priv->base + DSI_ACK_ERR_STATUS_REG),
		readl(priv->base + DSI_TIMEOUT_STATUS_REG),
		readl(priv->base + DSI_LANE_STATUS_REG),
		readl(priv->base + DSI_LANE_SWAP_CTRL_REG));

	return -ETIMEDOUT;
}

static int qcom_msm8960_dsi_attach(struct mipi_dsi_host *host,
				   struct mipi_dsi_device *dsi)
{
	struct qcom_msm8960_dsi_priv *priv =
		container_of(host, struct qcom_msm8960_dsi_priv, host);

	dsi->host = host;
	priv->device = *dsi;

	return 0;
}

static ssize_t qcom_msm8960_dsi_transfer(struct mipi_dsi_host *host,
					 const struct mipi_dsi_msg *msg)
{
	struct qcom_msm8960_dsi_priv *priv =
		container_of(host, struct qcom_msm8960_dsi_priv, host);
	struct udevice *dev = (struct udevice *)host->dev;
	struct mipi_dsi_packet packet;
	ALLOC_CACHE_ALIGN_BUFFER(u8, cmd, 16);
	u32 saved_ctrl;
	ulong start;
	ulong end;
	int ret;

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret)
		return ret;
	if (!mipi_dsi_packet_format_is_short(packet.header[0] & 0x3f))
		return -EOPNOTSUPP;

	ret = qcom_msm8960_dsi_wait_dma_idle(dev, priv, "pre-transfer");
	if (ret)
		return ret;

	memset(cmd, 0xff, 16);
	cmd[0] = packet.header[1];
	cmd[1] = packet.header[2];
	cmd[2] = packet.header[0];
	cmd[3] = BIT(7);

	start = (ulong)cmd;
	end = ALIGN(start + 16, ARCH_DMA_MINALIGN);
	flush_dcache_range(start, end);

	saved_ctrl = readl(priv->base + DSI_CTRL_REG);
	qcom_dsi_write(priv->base, DSI_CTRL_REG, saved_ctrl |
		       DSI_CTRL_CMD_MODE_EN | DSI_CTRL_ENABLE);
	qcom_dsi_write(priv->base, DSI_DMA_BASE_REG, (u32)start);
	qcom_dsi_write(priv->base, DSI_DMA_LEN_REG, 4);
	qcom_dsi_write(priv->base, DSI_TRIG_DMA_REG, 1);
	readl(priv->base + DSI_TRIG_DMA_REG);

	ret = qcom_msm8960_dsi_wait_dma_idle(dev, priv, "transfer");
	qcom_dsi_write(priv->base, DSI_CTRL_REG, saved_ctrl);
	if (ret)
		return ret;

	return msg->tx_len;
}

static const struct mipi_dsi_host_ops qcom_msm8960_dsi_host_ops = {
	.attach = qcom_msm8960_dsi_attach,
	.transfer = qcom_msm8960_dsi_transfer,
};

static int qcom_msm8960_dsi_host_init(struct udevice *dev,
				      struct mipi_dsi_device *device,
				      struct display_timing *timings,
				      unsigned int max_data_lanes,
				      const struct mipi_dsi_phy_ops *phy_ops)
{
	struct qcom_msm8960_dsi_priv *priv = dev_get_priv(dev);

	priv->device = *device;
	priv->device.host = &priv->host;

	return qcom_msm8960_dsi_prepare(dev);
}

static int qcom_msm8960_dsi_host_enable(struct udevice *dev)
{
	return qcom_msm8960_dsi_enable_video(dev);
}

static const struct dsi_host_ops qcom_msm8960_dsi_ops = {
	.init = qcom_msm8960_dsi_host_init,
	.enable = qcom_msm8960_dsi_host_enable,
};

static int qcom_msm8960_dsi_probe(struct udevice *dev)
{
	struct qcom_msm8960_dsi_priv *priv = dev_get_priv(dev);

	priv->base = (void __iomem *)FAME_DSI_BASE;
	priv->pll = (void __iomem *)FAME_DSI_PLL_BASE;
	priv->phy = (void __iomem *)FAME_DSI_PHY_BASE;
	priv->phy_misc = (void __iomem *)FAME_DSI_PHY_REG_BASE;
	priv->host.dev = (struct device *)dev;
	priv->host.ops = &qcom_msm8960_dsi_host_ops;
	priv->device.dev = dev;
	priv->device.host = &priv->host;
	priv->device.channel = 0;
	priv->device.lanes = 2;
	priv->device.format = MIPI_DSI_FMT_RGB888;
	priv->device.mode_flags = MIPI_DSI_MODE_VIDEO |
				  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	return 0;
}

static const struct udevice_id qcom_msm8960_dsi_ids[] = {
	{ .compatible = "qcom,apq8064-dsi-ctrl" },
	{ .compatible = "qcom,mdss-dsi-ctrl" },
	{ }
};

U_BOOT_DRIVER(qcom_msm8960_dsi) = {
	.name		= "qcom_msm8960_dsi",
	.id		= UCLASS_DSI_HOST,
	.of_match	= qcom_msm8960_dsi_ids,
	.probe		= qcom_msm8960_dsi_probe,
	.ops		= &qcom_msm8960_dsi_ops,
	.priv_auto	= sizeof(struct qcom_msm8960_dsi_priv),
};
