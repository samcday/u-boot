// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Bhupesh Sharma <bhupesh.sharma@linaro.org>
 *
 * Based on Linux driver, only handles USB3, DP is disabled
 */

#include <clk.h>
#include <clk-uclass.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <dm/devres.h>
#include <generic-phy.h>
#include <malloc.h>
#include <reset.h>
#include <power/regulator.h>

#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>

#include <dt-bindings/phy/phy-qcom-qmp.h>

#include "phy-qcom-qmp.h"
#include "phy-qcom-qmp-pcs-misc-v3.h"
#include "phy-qcom-qmp-pcs-v6.h"
#include "phy-qcom-qmp-pcs-usb-v6.h"
#include "phy-qcom-qmp-qserdes-com-v6.h"
#include "phy-qcom-qmp-qserdes-txrx-v6.h"

/* QPHY_SW_RESET bit */
#define SW_RESET				BIT(0)
/* QPHY_POWER_DOWN_CONTROL */
#define SW_PWRDN				BIT(0)
#define REFCLK_DRV_DSBL				BIT(1) /* PCIe */

/* QPHY_START_CONTROL bits */
#define SERDES_START				BIT(0)
#define PCS_START				BIT(1)

/* QPHY_PCS_STATUS bit */
#define PHYSTATUS				BIT(6)
#define PHYSTATUS_4_20				BIT(7)

/* QPHY_PCS_AUTONOMOUS_MODE_CTRL register bits */
#define ARCVR_DTCT_EN				BIT(0)
#define ALFPS_DTCT_EN				BIT(1)
#define ARCVR_DTCT_EVENT_SEL			BIT(4)

/* QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR register bits */
#define IRQ_CLEAR				BIT(0)

/* QPHY_PCS_MISC_CLAMP_ENABLE register bits */
#define CLAMP_EN				BIT(0) /* enables i/o clamp_n */

/* QPHY_V3_DP_COM_RESET_OVRD_CTRL register bits */
/* DP PHY soft reset */
#define SW_DPPHY_RESET				BIT(0)
/* mux to select DP PHY reset control, 0:HW control, 1: software reset */
#define SW_DPPHY_RESET_MUX			BIT(1)
/* USB3 PHY soft reset */
#define SW_USB3PHY_RESET			BIT(2)
/* mux to select USB3 PHY reset control, 0:HW control, 1: software reset */
#define SW_USB3PHY_RESET_MUX			BIT(3)

/* QPHY_V3_DP_COM_PHY_MODE_CTRL register bits */
#define USB3_MODE				BIT(0) /* enables USB3 mode */
#define DP_MODE					BIT(1) /* enables DP mode */

/* QPHY_V3_DP_COM_TYPEC_CTRL register bits */
#define SW_PORTSELECT_VAL			BIT(0)
#define SW_PORTSELECT_MUX			BIT(1)

#define PHY_INIT_COMPLETE_TIMEOUT		(200 * 10000)

#define NUM_SUPPLIES	3

struct qmp_combo_init_tbl {
	unsigned int offset;
	unsigned int val;
	/*
	 * mask of lanes for which this register is written
	 * for cases when second lane needs different values
	 */
	u8 lane_mask;
};

#define QMP_PHY_INIT_CFG(o, v)		\
	{				\
		.offset = o,		\
		.val = v,		\
		.lane_mask = 0xff,	\
	}

#define QMP_PHY_INIT_CFG_LANE(o, v, l)	\
	{				\
		.offset = o,		\
		.val = v,		\
		.lane_mask = l,		\
	}

/* set of registers with offsets different per-PHY */
enum qphy_reg_layout {
	/* PCS registers */
	QPHY_SW_RESET,
	QPHY_START_CTRL,
	QPHY_PCS_STATUS,
	QPHY_PCS_AUTONOMOUS_MODE_CTRL,
	QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR,
	QPHY_PCS_POWER_DOWN_CONTROL,

	QPHY_COM_RESETSM_CNTRL,
	QPHY_COM_C_READY_STATUS,
	QPHY_COM_CMN_STATUS,
	QPHY_COM_BIAS_EN_CLKBUFLR_EN,

	QPHY_DP_PHY_STATUS,

	QPHY_TX_TX_POL_INV,
	QPHY_TX_TX_DRV_LVL,
	QPHY_TX_TX_EMP_POST1_LVL,
	QPHY_TX_HIGHZ_DRVR_EN,
	QPHY_TX_TRANSCEIVER_BIAS_EN,

	/* Keep last to ensure regs_layout arrays are properly initialized */
	QPHY_LAYOUT_SIZE
};

static const unsigned int qmp_v6_usb3phy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_SW_RESET]			= QPHY_V6_PCS_SW_RESET,
	[QPHY_START_CTRL]		= QPHY_V6_PCS_START_CONTROL,
	[QPHY_PCS_STATUS]		= QPHY_V6_PCS_PCS_STATUS1,
	[QPHY_PCS_POWER_DOWN_CONTROL]	= QPHY_V6_PCS_POWER_DOWN_CONTROL,

	/* In PCS_USB */
	[QPHY_PCS_AUTONOMOUS_MODE_CTRL]	= QPHY_V6_PCS_USB3_AUTONOMOUS_MODE_CTRL,
	[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR] = QPHY_V6_PCS_USB3_LFPS_RXTERM_IRQ_CLEAR,

	[QPHY_COM_RESETSM_CNTRL]	= QSERDES_V6_COM_RESETSM_CNTRL,
	[QPHY_COM_C_READY_STATUS]	= QSERDES_V6_COM_C_READY_STATUS,
	[QPHY_COM_CMN_STATUS]		= QSERDES_V6_COM_CMN_STATUS,
	[QPHY_COM_BIAS_EN_CLKBUFLR_EN]	= QSERDES_V6_COM_PLL_BIAS_EN_CLK_BUFLR_EN,

	[QPHY_DP_PHY_STATUS]		= QSERDES_V6_DP_PHY_STATUS,

	[QPHY_TX_TX_POL_INV]		= QSERDES_V6_TX_TX_POL_INV,
	[QPHY_TX_TX_DRV_LVL]		= QSERDES_V6_TX_TX_DRV_LVL,
	[QPHY_TX_TX_EMP_POST1_LVL]	= QSERDES_V6_TX_TX_EMP_POST1_LVL,
	[QPHY_TX_HIGHZ_DRVR_EN]		= QSERDES_V6_TX_HIGHZ_DRVR_EN,
	[QPHY_TX_TRANSCEIVER_BIAS_EN]	= QSERDES_V6_TX_TRANSCEIVER_BIAS_EN,
};

static const struct qmp_combo_init_tbl sm8550_usb3_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_SSC_STEP_SIZE1_MODE1, 0xc0),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_SSC_STEP_SIZE2_MODE1, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_CP_CTRL_MODE1, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_PLL_RCTRL_MODE1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_PLL_CCTRL_MODE1, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_CORECLK_DIV_MODE1, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_LOCK_CMP1_MODE1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_LOCK_CMP2_MODE1, 0x41),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DEC_START_MODE1, 0x41),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DEC_START_MSB_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DIV_FRAC_START1_MODE1, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DIV_FRAC_START2_MODE1, 0x75),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DIV_FRAC_START3_MODE1, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_HSCLK_SEL_1, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_VCO_TUNE1_MODE1, 0x25),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_VCO_TUNE2_MODE1, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_BIN_VCOCAL_CMP_CODE1_MODE1, 0x5c),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_BIN_VCOCAL_CMP_CODE2_MODE1, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_BIN_VCOCAL_CMP_CODE1_MODE0, 0x5c),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_BIN_VCOCAL_CMP_CODE2_MODE0, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_SSC_STEP_SIZE1_MODE0, 0xc0),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_SSC_STEP_SIZE2_MODE0, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_CP_CTRL_MODE0, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_PLL_CCTRL_MODE0, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_LOCK_CMP1_MODE0, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_LOCK_CMP2_MODE0, 0x1a),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DEC_START_MODE0, 0x41),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DEC_START_MSB_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DIV_FRAC_START1_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DIV_FRAC_START2_MODE0, 0x75),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_DIV_FRAC_START3_MODE0, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_VCO_TUNE1_MODE0, 0x25),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_VCO_TUNE2_MODE0, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_BG_TIMER, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_SSC_EN_CENTER, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_SSC_PER1, 0x62),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_SSC_PER2, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_SYSCLK_BUF_ENABLE, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_SYSCLK_EN_SEL, 0x1a),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_LOCK_CMP_CFG, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_VCO_TUNE_MAP, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_CORE_CLK_EN, 0x20),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_CMN_CONFIG_1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_AUTO_GAIN_ADJ_CTRL_1, 0xb6),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_AUTO_GAIN_ADJ_CTRL_2, 0x4b),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_AUTO_GAIN_ADJ_CTRL_3, 0x37),
	QMP_PHY_INIT_CFG(QSERDES_V6_COM_ADDITIONAL_MISC, 0x0c),
};

static const struct qmp_combo_init_tbl sm8550_usb3_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V6_TX_RES_CODE_LANE_TX, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V6_TX_RES_CODE_LANE_RX, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V6_TX_RES_CODE_LANE_OFFSET_TX, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_V6_TX_RES_CODE_LANE_OFFSET_RX, 0x09),
	QMP_PHY_INIT_CFG(QSERDES_V6_TX_LANE_MODE_1, 0xf5),
	QMP_PHY_INIT_CFG(QSERDES_V6_TX_LANE_MODE_3, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_V6_TX_LANE_MODE_4, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_V6_TX_LANE_MODE_5, 0x5f),
	QMP_PHY_INIT_CFG(QSERDES_V6_TX_RCV_DETECT_LVL_2, 0x12),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_TX_PI_QEC_CTRL, 0x21, 1),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_TX_PI_QEC_CTRL, 0x05, 2),
};

static const struct qmp_combo_init_tbl sm8550_usb3_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_FO_GAIN, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_SO_GAIN, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_FASTLOCK_FO_GAIN, 0x2f),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x7f),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_FASTLOCK_COUNT_LOW, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_FASTLOCK_COUNT_HIGH, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_PI_CONTROLS, 0x99),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_SB2_THRESH1, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_SB2_THRESH2, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_SB2_GAIN1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_UCDR_SB2_GAIN2, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_AUX_DATA_TCOARSE_TFINE, 0xa0),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_VGA_CAL_CNTRL1, 0x54),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_VGA_CAL_CNTRL2, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_GM_CAL, 0x13),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_EQU_ADAPTOR_CNTRL2, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_EQU_ADAPTOR_CNTRL3, 0x4a),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_EQU_ADAPTOR_CNTRL4, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_IDAC_TSETTLE_LOW, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_IDAC_TSETTLE_HIGH, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x47),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_SIGDET_CNTRL, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_SIGDET_DEGLITCH_CNTRL, 0x0e),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_MODE_01_LOW, 0xdc),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_MODE_01_HIGH, 0x5c),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_MODE_01_HIGH2, 0x9c),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_MODE_01_HIGH3, 0x1d),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_RX_MODE_01_HIGH4, 0x09),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_DFE_EN_TIMER, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_DFE_CTLE_POST_CAL_OFFSET, 0x38),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_DCC_CTRL1, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_VTH_CODE, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_SIGDET_CAL_CTRL1, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_V6_RX_SIGDET_CAL_TRIM, 0x08),

	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_LOW, 0x3f, 1),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_HIGH, 0xbf, 1),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_HIGH2, 0xff, 1),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_HIGH3, 0xdf, 1),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_HIGH4, 0xed, 1),

	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_LOW, 0xbf, 2),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_HIGH, 0xbf, 2),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_HIGH2, 0xbf, 2),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_HIGH3, 0xdf, 2),
	QMP_PHY_INIT_CFG_LANE(QSERDES_V6_RX_RX_MODE_00_HIGH4, 0xfd, 2),
};

static const struct qmp_combo_init_tbl sm8550_usb3_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_LOCK_DETECT_CONFIG1, 0xc4),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_LOCK_DETECT_CONFIG2, 0x89),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_LOCK_DETECT_CONFIG3, 0x20),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_LOCK_DETECT_CONFIG6, 0x13),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_REFGEN_REQ_CONFIG1, 0x21),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_RX_SIGDET_LVL, 0x99),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_RCVR_DTCT_DLY_P1U2_L, 0xe7),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_RCVR_DTCT_DLY_P1U2_H, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_CDR_RESET_TIME, 0x0a),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_ALIGN_DETECT_CONFIG1, 0x88),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_ALIGN_DETECT_CONFIG2, 0x13),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_PCS_TX_RX_CONFIG, 0x0c),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_EQ_CONFIG1, 0x4b),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_EQ_CONFIG5, 0x10),
};

static const struct qmp_combo_init_tbl sm8550_usb3_pcs_usb_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_USB3_LFPS_DET_HIGH_COUNT_VAL, 0xf8),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_USB3_RXEQTRAINING_DFE_TIME_S2, 0x07),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_USB3_RCVR_DTCT_DLY_U3_L, 0x40),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_USB3_RCVR_DTCT_DLY_U3_H, 0x00),
	QMP_PHY_INIT_CFG(QPHY_V6_PCS_USB3_POWER_STATE_CONFIG1, 0x68),
};

struct qmp_combo_offsets {
	u16 com;
	u16 txa;
	u16 rxa;
	u16 txb;
	u16 rxb;
	u16 usb3_serdes;
	u16 usb3_pcs_misc;
	u16 usb3_pcs;
	u16 usb3_pcs_usb;
};

/* struct qmp_combo_cfg - per-PHY initialization config */
struct qmp_combo_cfg {
	const struct qmp_combo_offsets *offsets;

	/* Init sequence for PHY blocks - serdes, tx, rx, pcs */
	const struct qmp_combo_init_tbl *serdes_tbl;
	int serdes_tbl_num;
	const struct qmp_combo_init_tbl *tx_tbl;
	int tx_tbl_num;
	const struct qmp_combo_init_tbl *rx_tbl;
	int rx_tbl_num;
	const struct qmp_combo_init_tbl *pcs_tbl;
	int pcs_tbl_num;
	const struct qmp_combo_init_tbl *pcs_usb_tbl;
	int pcs_usb_tbl_num;

	/* regulators to be requested */
	const char * const *vreg_list;
	int num_vregs;
	/* resets to be requested */
	const char * const *reset_list;
	int num_resets;

	/* array of registers with different offsets */
	const unsigned int *regs;
};

struct qmp_combo_priv {
	struct phy *phy;

	void __iomem *com;

	void __iomem *serdes;
	void __iomem *tx;
	void __iomem *rx;
	void __iomem *pcs;
	void __iomem *tx2;
	void __iomem *rx2;
	void __iomem *pcs_misc;
	void __iomem *pcs_usb;

	struct clk *clks;
	unsigned int clk_count;

	struct clk pipe_clk;

	struct reset_ctl *resets;
	unsigned int reset_count;

	struct udevice *vregs[NUM_SUPPLIES];
	unsigned int vreg_count;

	const struct qmp_combo_cfg *cfg;
	struct udevice *dev;
};

static inline void qphy_setbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg |= val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

static inline void qphy_clrbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg &= ~val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

/* list of clocks required by phy */
static const char * const qmp_combophy_clk_l[] = {
	"aux", "cfg_ahb", "ref", "com_aux",
};

/* list of regulators */
static const char * const qmp_phy_vreg_l[] = {
	"vdda-phy-supply", "vdda-pll-supply",
};

/* list of resets */
static const char * const msm8996_usb3phy_reset_l[] = {
	"phy", "common",
};

static const struct qmp_combo_offsets qmp_combo_offsets_v3 = {
	.com		= 0x0000,
	.txa		= 0x1200,
	.rxa		= 0x1400,
	.txb		= 0x1600,
	.rxb		= 0x1800,
	.usb3_serdes	= 0x1000,
	.usb3_pcs_misc	= 0x1a00,
	.usb3_pcs	= 0x1c00,
	.usb3_pcs_usb	= 0x1f00,
};

static const struct qmp_combo_cfg sm8550_usb3dpphy_cfg = {
	.offsets		= &qmp_combo_offsets_v3,

	.serdes_tbl		= sm8550_usb3_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(sm8550_usb3_serdes_tbl),
	.tx_tbl			= sm8550_usb3_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(sm8550_usb3_tx_tbl),
	.rx_tbl			= sm8550_usb3_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(sm8550_usb3_rx_tbl),
	.pcs_tbl		= sm8550_usb3_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(sm8550_usb3_pcs_tbl),
	.pcs_usb_tbl		= sm8550_usb3_pcs_usb_tbl,
	.pcs_usb_tbl_num	= ARRAY_SIZE(sm8550_usb3_pcs_usb_tbl),

	.regs			= qmp_v6_usb3phy_regs_layout,
	.reset_list		= msm8996_usb3phy_reset_l,
	.num_resets		= ARRAY_SIZE(msm8996_usb3phy_reset_l),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
};

static void qmp_combo_configure_lane(void __iomem *base,
					const struct qmp_combo_init_tbl tbl[],
					int num,
					u8 lane_mask)
{
	int i;
	const struct qmp_combo_init_tbl *t = tbl;

	if (!t)
		return;

	for (i = 0; i < num; i++, t++) {
		if (!(t->lane_mask & lane_mask))
			continue;

		writel(t->val, base + t->offset);
	}
}

static void qmp_combo_configure(void __iomem *base,
				   const struct qmp_combo_init_tbl tbl[],
				   int num)
{
	qmp_combo_configure_lane(base, tbl, num, 0xff);
}

static int qmp_combo_do_reset(struct qmp_combo_priv *qmp)
{
	const struct qmp_combo_cfg *cfg = qmp->cfg;
	int i, ret;

	for (i = 0; i < qmp->reset_count; i++) {
		ret = reset_assert(&qmp->resets[i]);
		if (ret)
			return ret;
	}

	udelay(10);

	for (i = 0; i < qmp->reset_count; i++) {
		ret = reset_deassert(&qmp->resets[i]);
		if (ret)
			return ret;
	}

	udelay(50);

	return 0;
}

static int qmp_combo_power_on(struct phy *phy)
{
	struct qmp_combo_priv *qmp = dev_get_priv(phy->dev);
	const struct qmp_combo_cfg *cfg = qmp->cfg;
	void __iomem *status;
	unsigned int mask, val;
	int ret, i;
	
	for (i = 0; i < qmp->vreg_count; i++) {
		ret = regulator_set_enable(qmp->vregs[i], true);
		if (ret && ret != -ENOSYS)
			pr_err("%s: failed to enable regulator %d (ret=%d)\n", __func__, i, ret);
	}
	
	ret = qmp_combo_do_reset(qmp);
	if (ret)
		return ret;

	for (i = 0; i < qmp->clk_count; i++) {
		ret = clk_enable(&qmp->clks[i]);
		if (ret && ret != -ENOSYS) {
			pr_err("%s: failed to enable clock %d\n", __func__, i);
			return ret;
		}
	}

	qphy_setbits(qmp->com, QPHY_V3_DP_COM_POWER_DOWN_CTRL, SW_PWRDN);

	/* override hardware control for reset of qmp phy */
	qphy_setbits(qmp->com, QPHY_V3_DP_COM_RESET_OVRD_CTRL,
			SW_DPPHY_RESET_MUX | SW_DPPHY_RESET |
			SW_USB3PHY_RESET_MUX | SW_USB3PHY_RESET);

	/* Use hardware based port select and switch on typec orientation */
	writel(0, qmp->com + QPHY_V3_DP_COM_TYPEC_CTRL);
	writel(USB3_MODE, qmp->com + QPHY_V3_DP_COM_PHY_MODE_CTRL);

	/* bring QMP USB PCS block out of reset */
	qphy_clrbits(qmp->com, QPHY_V3_DP_COM_RESET_OVRD_CTRL,
			SW_USB3PHY_RESET_MUX | SW_USB3PHY_RESET);

	qphy_clrbits(qmp->com, QPHY_V3_DP_COM_SWI_CTRL, 0x03);
	qphy_clrbits(qmp->com, QPHY_V3_DP_COM_SW_RESET, SW_RESET);

	qphy_setbits(qmp->pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL],
			SW_PWRDN);

	qmp_combo_configure(qmp->serdes, cfg->serdes_tbl, cfg->serdes_tbl_num);	
	
	clk_enable(&qmp->pipe_clk);

	qmp_combo_configure_lane(qmp->tx, cfg->tx_tbl, cfg->tx_tbl_num, 1);
	qmp_combo_configure_lane(qmp->tx2, cfg->tx_tbl, cfg->tx_tbl_num, 2);
	qmp_combo_configure_lane(qmp->rx, cfg->rx_tbl, cfg->rx_tbl_num, 1);
	qmp_combo_configure_lane(qmp->rx2, cfg->rx_tbl, cfg->rx_tbl_num, 2);

	qmp_combo_configure(qmp->pcs, cfg->pcs_tbl, cfg->pcs_tbl_num);

	if (qmp->pcs_usb)
		qmp_combo_configure(qmp->pcs_usb, cfg->pcs_usb_tbl, cfg->pcs_usb_tbl_num);
	
	/* Pull PHY out of reset state */
	qphy_clrbits(qmp->pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* start SerDes */
	qphy_setbits(qmp->pcs, cfg->regs[QPHY_START_CTRL], SERDES_START | PCS_START);

	status = qmp->pcs + cfg->regs[QPHY_PCS_STATUS];
	ret = readl_poll_timeout(status, val, !(val & PHYSTATUS), PHY_INIT_COMPLETE_TIMEOUT);
	if (ret) {
		pr_err("%s: phy initialization timed-out\n", __func__);
		return ret;
	}

	return 0;
}

static int qmp_combo_power_off(struct phy *phy)
{
	struct qmp_combo_priv *qmp = dev_get_priv(phy->dev);
	const struct qmp_combo_cfg *cfg = qmp->cfg;
	
	clk_disable(&qmp->pipe_clk);

	/* PHY reset */
	qphy_setbits(qmp->pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* stop SerDes and Phy-Coding-Sublayer */
	qphy_clrbits(qmp->pcs, cfg->regs[QPHY_START_CTRL],
			SERDES_START | PCS_START);

	/* Put PHY into POWER DOWN state: active low */
	qphy_clrbits(qmp->pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL],
			SW_PWRDN);

	return 0;
}

static int qmp_combo_vreg_init(struct udevice *dev, struct qmp_combo_priv *qmp)
{
	const struct qmp_combo_cfg *cfg = qmp->cfg;
	unsigned int vreg;
	int ret;

	qmp->vreg_count = cfg->num_vregs;
	
	for (vreg = 0; vreg < NUM_SUPPLIES && vreg < qmp->vreg_count; ++vreg) {
		ret = device_get_supply_regulator(dev, cfg->vreg_list[vreg], &qmp->vregs[vreg]);
		if (ret) {
			dev_err(dev, "failed to ret regulator %d (ret=%d)\n", vreg, ret);
			continue;
		}
	}

	return 0;
}

static int qmp_combo_reset_init(struct udevice *dev, struct qmp_combo_priv *qmp)
{
	const struct qmp_combo_cfg *cfg = qmp->cfg;
	int num = cfg->num_resets;
	int i, ret;

	qmp->reset_count = 0;
	qmp->resets = devm_kcalloc(dev, num, sizeof(*qmp->resets), GFP_KERNEL);
	if (!qmp->resets)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		ret = reset_get_by_name(dev, cfg->reset_list[i], &qmp->resets[i]);
		if (ret < 0) {
			pr_err("%s: failed to get reset %d\n", __func__, i);
			goto reset_get_err;
		}

		++qmp->reset_count;
	}

	return 0;

reset_get_err:
	ret = reset_release_all(qmp->resets, qmp->reset_count);
	if (ret)
		pr_err("%s: failed to disable all resets\n", __func__);

	return ret;
}

static int qmp_combo_clk_init(struct udevice *dev, struct qmp_combo_priv *qmp)
{
	int num = ARRAY_SIZE(qmp_combophy_clk_l);
	int i, ret;

	qmp->clk_count = 0;
	qmp->clks = devm_kcalloc(dev, num, sizeof(*qmp->clks), GFP_KERNEL);
	if (!qmp->clks)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		ret = clk_get_by_name(dev, qmp_combophy_clk_l[i], &qmp->clks[i]);
		if (ret < 0)
			goto clk_get_err;

		++qmp->clk_count;
	}
	
	ret = clk_get_by_name(dev, "usb3_pipe", &qmp->pipe_clk);
	if (ret) {
		pr_err("%s: failed to get pipe clock\n", __func__);
	}

	return 0;

clk_get_err:
	ret = clk_release_all(qmp->clks, qmp->clk_count);
	if (ret)
		pr_err("%s: failed to disable all clocks\n", __func__);

	return ret;
}

static int qmp_combo_parse_dt(struct udevice *dev, struct qmp_combo_priv *qmp)
{
	const struct qmp_combo_offsets *offs = qmp->cfg->offsets;
	struct resource res;
	int ret;

	if (!qmp->cfg->offsets) {
		pr_err("%s: missing PCIE offsets\n", __func__);
		return -EINVAL;
	}

	ret = ofnode_read_resource(dev_ofnode(dev), 0, &res);
	if (ret) {
		dev_err(dev, "can't get reg property\n");
		return ret;
	}

	qmp->com = (void __iomem *)res.start + offs->com;
	qmp->tx = (void __iomem *)res.start + offs->txa;
	qmp->rx = (void __iomem *)res.start + offs->rxa;
	qmp->tx2 = (void __iomem *)res.start + offs->txb;
	qmp->rx2 = (void __iomem *)res.start + offs->rxb;

	qmp->serdes = (void __iomem *)res.start + offs->usb3_serdes;
	qmp->pcs_misc = (void __iomem *)res.start + offs->usb3_pcs_misc;	
	qmp->pcs = (void __iomem *)res.start + offs->usb3_pcs;
	qmp->pcs_usb = (void __iomem *)res.start + offs->usb3_pcs_usb;

	return 0;
}

static int qmp_combo_probe(struct udevice *dev)
{
	struct qmp_combo_priv *qmp = dev_get_priv(dev);
	int ret;

	qmp->serdes = (void __iomem *)dev_read_addr(dev);
	if (IS_ERR(qmp->serdes))
		return PTR_ERR(qmp->serdes);

	qmp->cfg = (const struct qmp_combo_cfg *)dev_get_driver_data(dev);
	if (!qmp->cfg)
		return -EINVAL;

	ret = qmp_combo_clk_init(dev, qmp);
	if (ret) {
		pr_err("%s: failed to get PCIE clks\n", __func__);
		return ret;
	}

	ret = qmp_combo_vreg_init(dev, qmp);
	if (ret) {
		pr_err("%s: failed to get PCIE voltage regulators\n", __func__);
		return ret;
	}

	ret = qmp_combo_reset_init(dev, qmp);
	if (ret) {
		pr_err("%s: failed to get PCIE resets\n", __func__);
		return ret;
	}

	qmp->dev = dev;

	return qmp_combo_parse_dt(dev, qmp);
}

static int qmp_combo_xlate(struct phy *phy, struct ofnode_phandle_args *args)
{
	if (args->args_count != 1) {
		debug("Invalid args_count: %d\n", args->args_count);
		return -EINVAL;
	}

	/* We only support the USB3 phy at slot 0 */
	if (args->args[0] == QMP_USB43DP_DP_PHY)
		return -EINVAL;

	phy->id = QMP_USB43DP_USB3_PHY;

	return 0;
}

static struct phy_ops qmp_combo_ops = {
	.power_on = qmp_combo_power_on,
	.power_off = qmp_combo_power_off,
	.of_xlate = qmp_combo_xlate,
};

static const struct udevice_id qmp_combo_ids[] = {
	{ .compatible = "qcom,sm8550-qmp-usb3-dp-phy", .data = (ulong)&sm8550_usb3dpphy_cfg, },
	{ .compatible = "qcom,sm8650-qmp-usb3-dp-phy", .data = (ulong)&sm8550_usb3dpphy_cfg, },
	{ }
};

U_BOOT_DRIVER(qcom_qmp_combo) = {
	.name		= "qcom-qmp-combo",
	.id		= UCLASS_PHY,
	.of_match	= qmp_combo_ids,
	.ops		= &qmp_combo_ops,
	.probe		= qmp_combo_probe,
	.priv_auto	= sizeof(struct qmp_combo_priv),
};
