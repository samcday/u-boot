// SPDX-License-Identifier: GPL-2.0+
/*
 * Nokia Fame MSM8227 MDP4 bring-up diagnostics.
 */

#include <command.h>
#include <clk.h>
#include <cpu_func.h>
#include <dm/device.h>
#include <dm/ofnode.h>
#include <dm/uclass.h>
#include <errno.h>
#include <malloc.h>
#include <mapmem.h>
#include <memalign.h>
#include <mipi_display.h>
#include <power-domain.h>
#include <string.h>
#include <vsprintf.h>
#include <asm/io.h>
#include <asm/memory.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kernel.h>

#define FAME_MMCC_BASE			0x04000000
#define FAME_DSI_BASE			0x04700000
#define FAME_DSI_PLL_BASE		0x04700200
#define FAME_DSI_PHY_BASE		0x04700300
#define FAME_DSI_PHY_REG_BASE		0x04700500
#define FAME_MDP_BASE			0x05100000
#define FAME_TLMM_BASE			0x00800000
#define FAME_RPM_BASE			0x00108000
#define FAME_RPM_IPC_REG		0x02011008
#define FAME_MDP_IOMMU0_BASE		0x07500000
#define FAME_MDP_IOMMU1_BASE		0x07600000

#define MMCC_REG(off)			(FAME_MMCC_BASE + (off))
#define DSI_REG(off)			(FAME_DSI_BASE + (off))
#define DSI_PLL_REG(off)		(FAME_DSI_PLL_BASE + (off))
#define DSI_PHY_REG(off)		(FAME_DSI_PHY_BASE + (off))
#define DSI_PHY_MISC_REG(off)		(FAME_DSI_PHY_REG_BASE + (off))
#define MDP_REG(off)			(FAME_MDP_BASE + (off))
#define RPM_STATUS_REG(idx)		(FAME_RPM_BASE + ((idx) * 4))
#define RPM_CTRL_REG(idx)		(FAME_RPM_BASE + 0x400 + ((idx) * 4))
#define RPM_REQ_REG(idx)		(FAME_RPM_BASE + 0x600 + ((idx) * 4))
#define TLMM_GPIO_CFG_REG(gpio)		(FAME_TLMM_BASE + 0x1000 + \
					 (0x10 * (gpio)))
#define TLMM_GPIO_IN_OUT_REG(gpio)	(TLMM_GPIO_CFG_REG(gpio) + 0x4)

#define AHB_EN_REG			0x0008
#define MAXI_EN_REG			0x0018
#define DSI_CC_REG			0x004c
#define DSI_MD_REG			0x0050
#define DSI_NS_REG			0x0054
#define MDP_VSYNC_CC_REG		0x0058
#define DSI_BYTE_CC_REG		0x0090
#define DSI_BYTE_NS_REG		0x00b0
#define MDP_CC_REG			0x00c0
#define MDP_MD0_REG			0x00c4
#define MDP_MD1_REG			0x00c8
#define DSI_ESC_CC_REG			0x00cc
#define MDP_NS_REG			0x00d0
#define DSI_ESC_NS_REG			0x011c
#define DSI_PIXEL_CC_REG		0x0130
#define DSI_PIXEL_MD_REG		0x0134
#define DSI_PIXEL_NS_REG		0x0138
#define MDP_PD_CTL_REG			0x0190
#define MDP_LUT_CC_REG			0x016c
#define DBG_BUS_VEC_B_REG		0x01cc
#define DBG_BUS_VEC_C_REG		0x01d0
#define DBG_BUS_VEC_E_REG		0x01d8
#define DBG_BUS_VEC_F_REG		0x01dc
#define DBG_BUS_VEC_I_REG		0x01e8
#define SW_RESET_AXI_REG		0x0208
#define SW_RESET_AHB_REG		0x020c
#define SW_RESET_CORE_REG		0x0210
#define MM_PLL1_MODE_REG		0x031c
#define MM_PLL1_L_REG			0x0320
#define MM_PLL1_M_REG			0x0324
#define MM_PLL1_N_REG			0x0328
#define MM_PLL1_CONFIG_REG		0x032c
#define MM_PLL1_STATUS_REG		0x0334

#define DSI_CTRL_REG			0x0000
#define DSI_STATUS0_REG		0x0004
#define DSI_FIFO_STATUS_REG		0x0008
#define DSI_VID_CFG0_REG		0x000c
#define DSI_VID_CFG1_REG		0x001c
#define DSI_ACTIVE_H_REG		0x0020
#define DSI_ACTIVE_V_REG		0x0024
#define DSI_TOTAL_REG			0x0028
#define DSI_ACTIVE_HSYNC_REG		0x002c
#define DSI_ACTIVE_VSYNC_HPOS_REG	0x0030
#define DSI_ACTIVE_VSYNC_VPOS_REG	0x0034
#define DSI_CMD_DMA_CTRL_REG		0x0038
#define DSI_DMA_BASE_REG		0x0044
#define DSI_DMA_LEN_REG		0x0048
#define DSI_ACK_ERR_STATUS_REG		0x0064
#define DSI_TRIG_CTRL_REG		0x0080
#define DSI_TRIG_DMA_REG		0x008c
#define DSI_LANE_STATUS_REG		0x00a4
#define DSI_LANE_CTRL_REG		0x00a8
#define DSI_LANE_SWAP_CTRL_REG		0x00ac
#define DSI_TIMEOUT_STATUS_REG		0x00bc
#define DSI_CLKOUT_TIMING_CTRL_REG	0x00c0
#define DSI_EOT_PACKET_CTRL_REG	0x00c8
#define DSI_ERR_INT_MASK0_REG		0x0108
#define DSI_RESET_REG			0x0114
#define DSI_CLK_CTRL_REG		0x0118
#define DSI_CLK_STATUS_REG		0x011c
#define DSI_PHY_RESET_REG		0x0128
#define DSI_VERSION_REG		0x01f0

#define DSI_PLL_CTRL_0_REG		0x0000
#define DSI_PLL_CTRL_1_REG		0x0004
#define DSI_PLL_CTRL_2_REG		0x0008
#define DSI_PLL_CTRL_3_REG		0x000c
#define DSI_PLL_CTRL_6_REG		0x0018
#define DSI_PLL_CTRL_8_REG		0x0020
#define DSI_PLL_CTRL_9_REG		0x0024
#define DSI_PLL_CTRL_10_REG		0x0028
#define DSI_PLL_RDY_REG		0x0080

#define DSI_PHY_LN_CFG_0(lane)		(0x0000 + (0x40 * (lane)))
#define DSI_PHY_LN_CFG_1(lane)		(0x0004 + (0x40 * (lane)))
#define DSI_PHY_LN_CFG_2(lane)		(0x0008 + (0x40 * (lane)))
#define DSI_PHY_LN_TEST_DATAPATH(lane)	(0x000c + (0x40 * (lane)))
#define DSI_PHY_LN_TEST_STR_0(lane)	(0x0014 + (0x40 * (lane)))
#define DSI_PHY_LN_TEST_STR_1(lane)	(0x0018 + (0x40 * (lane)))
#define DSI_PHY_LNCK_CFG_0_REG		0x0100
#define DSI_PHY_LNCK_CFG_1_REG		0x0104
#define DSI_PHY_LNCK_CFG_2_REG		0x0108
#define DSI_PHY_LNCK_TEST_DATAPATH_REG	0x010c
#define DSI_PHY_LNCK_TEST_STR0_REG	0x0114
#define DSI_PHY_LNCK_TEST_STR1_REG	0x0118
#define DSI_PHY_TIMING_CTRL_0_REG	0x0140
#define DSI_PHY_CTRL_0_REG		0x0170
#define DSI_PHY_CTRL_1_REG		0x0174
#define DSI_PHY_CTRL_2_REG		0x0178
#define DSI_PHY_CTRL_3_REG		0x017c
#define DSI_PHY_STRENGTH_0_REG		0x0180
#define DSI_PHY_STRENGTH_1_REG		0x0184
#define DSI_PHY_STRENGTH_2_REG		0x0188
#define DSI_PHY_BIST_CTRL_0_REG	0x018c
#define DSI_PHY_BIST_CTRL_1_REG	0x0190
#define DSI_PHY_BIST_CTRL_4_REG	0x019c
#define DSI_PHY_LDO_CTRL_REG		0x01b0

#define DSI_PHY_MISC_REGULATOR_CTRL_0_REG	0x0000
#define DSI_PHY_MISC_REGULATOR_CAL_PWR_CFG_REG	0x0018
#define DSI_PHY_MISC_CAL_HW_TRIGGER_REG		0x0028
#define DSI_PHY_MISC_CAL_SW_CFG_2_REG		0x0034
#define DSI_PHY_MISC_CAL_HW_CFG_0_REG		0x0038
#define DSI_PHY_MISC_CAL_HW_CFG_1_REG		0x003c
#define DSI_PHY_MISC_CAL_HW_CFG_3_REG		0x0044
#define DSI_PHY_MISC_CAL_HW_CFG_4_REG		0x0048
#define DSI_PHY_MISC_CAL_STATUS_REG		0x0050

#define MDP_VERSION_REG			0x0000
#define MDP_EXPECTED_VERSION		0x04030705
#define MDP_DIAG_RATE			200000000

#define MDP_CS_CONTROLLER0_REG		0x000c0
#define MDP_CS_CONTROLLER1_REG		0x000c4
#define MDP_DISP_STATUS_REG		0x00018
#define MDP_DISP_INTF_SEL_REG		0x00038
#define MDP_READ_CNFG_REG		0x0004c
#define MDP_INTR_ENABLE_REG		0x00050
#define MDP_INTR_STATUS_REG		0x00054
#define MDP_INTR_CLEAR_REG		0x00058
#define MDP_PORTMAP_MODE_REG		0x00070
#define MDP_LAYERMIXER_UPDATE_REG	0x100fc
#define MDP_LAYERMIXER_IN_CFG_REG	0x10100
#define MDP_OVERLAY_FLUSH_REG		0x18000

#define MDP_OVLP0_CFG_REG		0x10004
#define MDP_OVLP0_SIZE_REG		0x10008
#define MDP_OVLP0_BASE_REG		0x1000c
#define MDP_OVLP0_STRIDE_REG		0x10010
#define MDP_OVLP0_TRANSP_LOW0_REG	0x10180
#define MDP_OVLP0_TRANSP_LOW1_REG	0x10184
#define MDP_OVLP0_TRANSP_HIGH0_REG	0x10188
#define MDP_OVLP0_TRANSP_HIGH1_REG	0x1018c

#define MDP_VG1_FETCH_CONFIG_REG	0x21004
#define MDP_VG1_OP_MODE_REG		0x20058
#define MDP_VG2_FETCH_CONFIG_REG	0x31004
#define MDP_VG2_OP_MODE_REG		0x30058
#define MDP_RGB1_SRC_SIZE_REG		0x40000
#define MDP_RGB1_SRC_XY_REG		0x40004
#define MDP_RGB1_DST_SIZE_REG		0x40008
#define MDP_RGB1_DST_XY_REG		0x4000c
#define MDP_RGB1_SRCP0_BASE_REG	0x40010
#define MDP_RGB1_SRCP1_BASE_REG	0x40014
#define MDP_RGB1_SRCP2_BASE_REG	0x40018
#define MDP_RGB1_SRCP3_BASE_REG	0x4001c
#define MDP_RGB1_SRC_STRIDE_A_REG	0x40040
#define MDP_RGB1_SRC_STRIDE_B_REG	0x40044
#define MDP_RGB1_SRC_FORMAT_REG	0x40050
#define MDP_RGB1_SRC_UNPACK_REG	0x40054
#define MDP_RGB1_OP_MODE_REG		0x40058
#define MDP_RGB1_PHASEX_STEP_REG	0x4005c
#define MDP_RGB1_PHASEY_STEP_REG	0x40060
#define MDP_RGB1_FETCH_CONFIG_REG	0x41004
#define MDP_RGB1_SOLID_COLOR_REG	0x41008
#define MDP_RGB2_FETCH_CONFIG_REG	0x51004

#define MDP_DMA_P_CONFIG_REG		0x90000
#define MDP_DMA_P_SRC_SIZE_REG		0x90004
#define MDP_DMA_P_SRC_BASE_REG		0x90008
#define MDP_DMA_P_SRC_STRIDE_REG	0x9000c
#define MDP_DMA_P_DST_SIZE_REG		0x90010
#define MDP_DMA_P_OP_MODE_REG		0x90070
#define MDP_DMA_P_FETCH_CONFIG_REG	0x91004
#define MDP_DMA_S_OP_MODE_REG		0xa0028
#define MDP_DMA_E_FETCH_CONFIG_REG	0xb1004

#define MDP_DSI_ENABLE_REG		0xe0000
#define MDP_DSI_HSYNC_CTRL_REG		0xe0004
#define MDP_DSI_VSYNC_PERIOD_REG	0xe0008
#define MDP_DSI_VSYNC_LEN_REG		0xe000c
#define MDP_DSI_DISPLAY_HCTRL_REG	0xe0010
#define MDP_DSI_DISPLAY_VSTART_REG	0xe0014
#define MDP_DSI_DISPLAY_VEND_REG	0xe0018
#define MDP_DSI_ACTIVE_HCTL_REG		0xe001c
#define MDP_DSI_ACTIVE_VSTART_REG	0xe0020
#define MDP_DSI_ACTIVE_VEND_REG		0xe0024
#define MDP_DSI_BORDER_CLR_REG		0xe0028
#define MDP_DSI_UNDERFLOW_CLR_REG	0xe002c
#define MDP_DSI_HSYNC_SKEW_REG		0xe0030
#define MDP_DSI_CTRL_POLARITY_REG	0xe0038

#define TEISKO_HDISPLAY			480
#define TEISKO_HSYNC_START		525
#define TEISKO_HSYNC_END		529
#define TEISKO_HTOTAL			573
#define TEISKO_VDISPLAY			800
#define TEISKO_VSYNC_START		814
#define TEISKO_VSYNC_END		815
#define TEISKO_VTOTAL			829
#define TEISKO_FB_BPP			4
#define TEISKO_FB_STRIDE		(TEISKO_HDISPLAY * TEISKO_FB_BPP)
#define TEISKO_FB_SIZE			(TEISKO_FB_STRIDE * TEISKO_VDISPLAY)
#define TEISKO_VENDOR_FB_BASE		0x80400000

#define FAME_MDP_CC_VENDOR		0x80ff08a5
#define FAME_MDP_MD0_VENDOR		0x00000000
#define FAME_MDP_MD1_VENDOR		0x000001fb
#define FAME_MDP_NS_VENDOR		0x003f0001
#define FAME_MDP_CC_BRANCH_EN		BIT(0)
#define FAME_MDP_CC_ROOT_EN		BIT(2)
#define FAME_MDP_CC_BANK1_MND_EN	BIT(5)
#define FAME_MDP_CC_BANK1_MODE_MASK	GENMASK(7, 6)
#define FAME_MDP_CC_BANK1_MODE_DUAL	(2 << 6)
#define FAME_MDP_CC_LOW_MASK		0x00000fff
#define FAME_MDP_CC_BANK_SEL		BIT(11)
#define FAME_MDP_CC_BANK1_PROG		(FAME_MDP_CC_BRANCH_EN | \
					 FAME_MDP_CC_ROOT_EN | \
					 FAME_MDP_CC_BANK1_MND_EN | \
					 FAME_MDP_CC_BANK1_MODE_DUAL)
#define FAME_MDP_NS_BANK1_RST		BIT(30)
#define FAME_MDP_NS_BANK1_N_SRC_MASK	0x003fc007
#define MDP4_DMA_DSI_CONFIG		0x0400213f
#define MDP4_DSI_INTF_SEL_MASK		0x000000cb
#define MDP4_DSI_INTF_SEL		0x00000041
#define MDP4_RGB1_BASE_STAGE		BIT(8)
#define MDP4_DMA_P_FETCH_CONFIG	0x43
#define MDP4_FETCH_CONFIG		0x47
#define MDP4_DSI_UNDERFLOW_RECOVER	BIT(31)
#define MDP4_PIPE_SRC_FORMAT_XRGB8888	0x000267ff
#define MDP4_PIPE_SRC_FORMAT_SOLID_FILL	BIT(22)
#define MDP4_PIPE_SRC_UNPACK_XRGB8888	0x03020001
#define MDP4_RGB1_OP_MODE_FETCH	0x00000010
#define MDP4_PHASE_STEP_DEFAULT	0x20000000
#define MDP4_SOLID_WHITE		0xffffffff

#define FAME_MDP_DEFAULT_READS		8
#define FAME_MDP_MAX_READS		64
#define FAME_MDP_PD			4
#define FAME_PANEL_RESET_GPIO		58
#define FAME_PANEL_RESET_GPIO_CFG_VENDOR	0x000003c1

#define FAME_DSI_PLL_CTRL_1_VENDOR	0x25
#define FAME_DSI_PLL_CTRL_2_VENDOR	0x30
#define FAME_DSI_PLL_CTRL_3_VENDOR	0xc2
#define FAME_DSI_PLL_CTRL_6_VENDOR	0x0c
#define FAME_DSI_PLL_CTRL_8_VENDOR	0x41
#define FAME_DSI_PLL_CTRL_9_VENDOR	0x01
#define FAME_DSI_PLL_CTRL_10_VENDOR	0x01
#define FAME_DSI_NS_VENDOR		0x0000c003
#define FAME_DSI_CC_ROOT_VENDOR	0x00000004
#define FAME_DSI_BYTE_NS_VENDOR	0x00007001
#define FAME_DSI_BYTE_CC_ROOT_VENDOR	0x80ff0004
#define FAME_DSI_ESC_NS_VENDOR		0x00001000
#define FAME_DSI_ESC_CC_ROOT_VENDOR	0x00000004
#define FAME_DSI_PIXEL_NS_VENDOR	0x0000b003
#define FAME_DSI_PIXEL_CC_ROOT_VENDOR	0x80ff0004
#define FAME_DSI_LANE_SWAP		0x1

#define MMCC_SMMU_AHB_EN		BIT(15)
#define MMCC_DSI_AMP_AHB_EN		BIT(24)
#define MMCC_DSI_M_AHB_EN		BIT(9)
#define MMCC_DSI_S_AHB_EN		BIT(18)
#define MMCC_DSI_CLK_BRANCH_EN		BIT(0)
#define MMCC_DSI_CLK_ROOT_EN		BIT(2)
#define MMCC_DSI_MND_EN		BIT(5)
#define MMCC_DSI_MND_MODE_MASK		(BIT(6) | BIT(7))
#define MMCC_DSI_MND_MODE_DUAL		BIT(7)
#define MMCC_DSI_MND_RESET		BIT(7)

#define MMCC_DSI_SRC_SEL_MASK		0x7
#define MMCC_DSI_SRC_PARENT_DSI1PLL	0x3
#define MMCC_DSI_BYTE_PARENT_DSI1PLL	0x1
#define MMCC_DSI_PRE_DIV2_MASK		(0x3 << 14)
#define MMCC_DSI_PRE_DIV4_MASK		(0xf << 12)
#define MMCC_DSI_SRC_N_MASK		(0xff << 24)
#define MMCC_DSI_PIXEL_N_MASK		(0xff << 16)
#define MMCC_DSI_PIXEL_N_VAL		(0xfd << 16)
#define MMCC_DSI_PIXEL_MD_VAL		0x000001fc

#define DSI_CTRL_ENABLE		BIT(0)
#define DSI_CTRL_VID_MODE_EN		BIT(1)
#define DSI_CTRL_CMD_MODE_EN		BIT(2)
#define DSI_CTRL_LANE0			BIT(4)
#define DSI_CTRL_LANE1			BIT(5)
#define DSI_CTRL_CLK_EN		BIT(8)
#define DSI_CTRL_BASE			(DSI_CTRL_ENABLE | DSI_CTRL_CLK_EN | \
					 DSI_CTRL_LANE0 | DSI_CTRL_LANE1)
#define DSI_CTRL_VIDEO			(DSI_CTRL_BASE | DSI_CTRL_VID_MODE_EN | \
					 DSI_CTRL_CMD_MODE_EN)
#define DSI_CLK_CTRL_ENABLE_CLKS	0x0000023f
#define DSI_CMD_DMA_CTRL_LPM_FB		0x14000000
#define DSI_STATUS0_CMD_ENGINE_BUSY	BIT(0)
#define DSI_STATUS0_CMD_DMA_BUSY	BIT(1)
#define DSI_STATUS0_CMD_BUSY		(DSI_STATUS0_CMD_ENGINE_BUSY | \
					 DSI_STATUS0_CMD_DMA_BUSY)
#define DSI_PHY_CAL_BUSY		BIT(4)
#define DSI_PLL_ENABLE			BIT(0)
#define DSI_PLL_READY			BIT(0)
#define DSI_PLL_LOCK_READS		1000
#define DSI_PLL_LOCK_POLL_US		100
#define DSI_DMA_TIMEOUT_US		200000

#define TLMM_FUNC_SEL_MASK		(0xf << 2)
#define TLMM_DRV_STRENGTH_MASK		(0x7 << 6)
#define TLMM_GPIO_PULL_MASK		0x3
#define TLMM_GPIO_OE			BIT(9)
#define TLMM_GPIO_OUT			BIT(1)

#define FAME_RPM_VERSION		3
#define FAME_RPM_REQ_CTX_OFF		3
#define FAME_RPM_REQ_SEL_OFF		11
#define FAME_RPM_ACK_CTX_OFF		15
#define FAME_RPM_ACK_SEL_OFF		23
#define FAME_RPM_REQ_SEL_SIZE		4
#define FAME_RPM_ACK_SEL_SIZE		7
#define FAME_RPM_IPC_BIT		2
#define FAME_RPM_ACTIVE_STATE		0
#define FAME_RPM_ACK_POLL_US		10
#define FAME_RPM_ACK_TIMEOUT_US	500000
#define FAME_RPM_NOTIFICATION		BIT(30)
#define FAME_RPM_REJECTED		BIT(31)
#define FAME_RPM_LDO_PULL_DOWN		BIT(23)

#define FAME_IOMMU_CTX_SHIFT		12
#define FAME_IOMMU_CTX_COUNT		2
#define FAME_IOMMU_SECTION_SIZE		0x00100000
#define FAME_IOMMU_FL_TABLE_SIZE	0x00004000
#define FAME_IOMMU_NUM_FL_PTE		4096

#define FAME_IOMMU_M2VCBR_N		0xff000
#define FAME_IOMMU_CBACR_N		0xff800
#define FAME_IOMMU_TLBRSW		0xffe00
#define FAME_IOMMU_TESTBUSCR		0xffe8c
#define FAME_IOMMU_GLOBAL_TLBIALL	0xfff00
#define FAME_IOMMU_CR			0xfff80
#define FAME_IOMMU_ESR			0xfff88
#define FAME_IOMMU_ESRRESTORE		0xfff8c
#define FAME_IOMMU_REV			0xffff4
#define FAME_IOMMU_IDR			0xffff8
#define FAME_IOMMU_RPU_ACR		0xffffc

#define FAME_IOMMU_SCTLR		0x000
#define FAME_IOMMU_ACTLR		0x004
#define FAME_IOMMU_CONTEXTIDR		0x008
#define FAME_IOMMU_TTBR0		0x010
#define FAME_IOMMU_TTBR1		0x014
#define FAME_IOMMU_TTBCR		0x018
#define FAME_IOMMU_PAR			0x01c
#define FAME_IOMMU_FSR			0x020
#define FAME_IOMMU_FSRRESTORE		0x024
#define FAME_IOMMU_FAR			0x028
#define FAME_IOMMU_FSYNR0		0x02c
#define FAME_IOMMU_FSYNR1		0x030
#define FAME_IOMMU_PRRR		0x034
#define FAME_IOMMU_NMRR		0x038
#define FAME_IOMMU_TLBLCKR		0x03c
#define FAME_IOMMU_TLBFLPTER		0x044
#define FAME_IOMMU_TLBSLPTER		0x048
#define FAME_IOMMU_BFBCR		0x04c
#define FAME_IOMMU_CTX_TLBIALL		0x800

#define FAME_IOMMU_M2VCBR_CBNDX(ctx)	((ctx) << 8)
#define FAME_IOMMU_M2VCBR_NSCFG_NONSEC	(3 << 22)
#define FAME_IOMMU_CR_TLBLKCRWE	BIT(6)
#define FAME_IOMMU_ACTLR_ENABLE	(BIT(1) | BIT(4) | BIT(5) | BIT(6) | \
					 (3 << 12) | BIT(14) | (3 << 16))
#define FAME_IOMMU_SCTLR_ENABLE	(BIT(0) | BIT(1) | BIT(2))
#define FAME_IOMMU_BFBCR_ENABLE	BIT(0)
#define FAME_IOMMU_TTBR0_FLAGS		(BIT(1) | (1 << 3) | BIT(5) | BIT(6))

#define FAME_IOMMU_FL_TYPE_SECT	(2 << 0)
#define FAME_IOMMU_FL_AP0		BIT(10)
#define FAME_IOMMU_FL_AP1		BIT(11)
#define FAME_IOMMU_FL_BUFFERABLE	BIT(2)
#define FAME_IOMMU_FL_CACHEABLE	BIT(3)
#define FAME_IOMMU_FL_TEX0		BIT(12)
#define FAME_IOMMU_FL_SHARED		BIT(16)
#define FAME_IOMMU_FL_NG		BIT(17)

#define FAME_IOMMU_MT_NORMAL		2
#define FAME_IOMMU_CP_NONCACHED	0

struct fame_pm8038_ldo {
	const char *name;
	u32 target_id;
	u32 status_id;
	u32 select_id;
	u32 microvolts;
};

static u8 fame_dsi_cmd_buf[16] __attribute__((aligned(ARCH_DMA_MINALIGN)));
static u32 *fame_mdp_fb;
static u32 *fame_mdp_iommu_fl_table;

static u32 mdp_wh(u32 width, u32 height)
{
	return width | (height << 16);
}

static u32 mdp_pair(u32 low, u32 high)
{
	return low | (high << 16);
}

static void fame_mdp_write(u32 reg, u32 val)
{
	writel(val, MDP_REG(reg));
	readl(MDP_REG(reg));
}

static void fame_mmcc_write(u32 reg, u32 val)
{
	writel(val, MMCC_REG(reg));
	readl(MMCC_REG(reg));
}

static void fame_mmcc_update_bits(u32 reg, u32 mask, u32 val)
{
	u32 old = readl(MMCC_REG(reg));

	fame_mmcc_write(reg, (old & ~mask) | (val & mask));
}

static void fame_dsi_write(u32 reg, u32 val)
{
	writel(val, DSI_REG(reg));
	readl(DSI_REG(reg));
}

static void fame_dsi_pll_write(u32 reg, u32 val)
{
	writel(val, DSI_PLL_REG(reg));
	readl(DSI_PLL_REG(reg));
}

static void fame_dsi_phy_write(u32 reg, u32 val)
{
	writel(val, DSI_PHY_REG(reg));
	readl(DSI_PHY_REG(reg));
}

static void fame_dsi_phy_misc_write(u32 reg, u32 val)
{
	writel(val, DSI_PHY_MISC_REG(reg));
	readl(DSI_PHY_MISC_REG(reg));
}

static int fame_mmcc_wait_halt(const char *name, u32 reg, u32 bit);

static u32 fame_iommu_ctx_reg(phys_addr_t base, u32 ctx, u32 reg)
{
	return base + reg + (ctx << FAME_IOMMU_CTX_SHIFT);
}

static u32 fame_iommu_m2vcbr_reg(phys_addr_t base, u32 mid)
{
	return base + FAME_IOMMU_M2VCBR_N + (mid * 4);
}

static u32 fame_iommu_cbacr_reg(phys_addr_t base, u32 ctx)
{
	return base + FAME_IOMMU_CBACR_N + (ctx * 4);
}

static void fame_iommu_write(phys_addr_t base, u32 reg, u32 val)
{
	writel(val, base + reg);
	readl(base + reg);
}

static void fame_iommu_ctx_write(phys_addr_t base, u32 ctx, u32 reg, u32 val)
{
	writel(val, fame_iommu_ctx_reg(base, ctx, reg));
	readl(fame_iommu_ctx_reg(base, ctx, reg));
}

static u32 fame_iommu_ctx_read(phys_addr_t base, u32 ctx, u32 reg)
{
	return readl(fame_iommu_ctx_reg(base, ctx, reg));
}

static u32 fame_read_prrr(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c10, c2, 0" : "=r"(val));
	return val;
}

static u32 fame_read_nmrr(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c10, c2, 1" : "=r"(val));
	return val;
}

static u32 fame_read_sctlr(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(val));
	return val;
}

static u32 fame_read_ttbr0(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(val));
	return val;
}

static u32 fame_read_ttbr1(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c2, c0, 1" : "=r"(val));
	return val;
}

static u32 fame_read_ttbcr(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(val));
	return val;
}

static u32 fame_read_dacr(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c3, c0, 0" : "=r"(val));
	return val;
}

static void fame_cpu_dump_state(const char *tag)
{
	printf("%s:\n", tag);
	printf("  cpu: sctlr=%08x ttbr0=%08x ttbr1=%08x ttbcr=%08x dacr=%08x prrr=%08x nmrr=%08x\n",
	       fame_read_sctlr(), fame_read_ttbr0(), fame_read_ttbr1(),
	       fame_read_ttbcr(), fame_read_dacr(), fame_read_prrr(),
	       fame_read_nmrr());
}

static int fame_iommu_get_tex_class(u32 prrr, u32 nmrr, int icp, int ocp,
				    int mt, int nos)
{
	int i;

	for (i = 0; i < 8; i++) {
		int c_icp = (nmrr >> (i * 2)) & 0x3;
		int c_ocp = (nmrr >> ((i * 2) + 16)) & 0x3;
		int c_mt = (prrr >> (i * 2)) & 0x3;
		int c_nos = !!(prrr & BIT(i + 24));

		if (icp == c_icp && ocp == c_ocp && mt == c_mt &&
		    nos == c_nos)
			return i;
	}

	return -ENOENT;
}

static u32 fame_iommu_fl_section_attr(int tex)
{
	u32 attr = FAME_IOMMU_FL_SHARED | FAME_IOMMU_FL_AP0 |
		FAME_IOMMU_FL_AP1;

	if (tex & 0x1)
		attr |= FAME_IOMMU_FL_BUFFERABLE;
	if (tex & 0x2)
		attr |= FAME_IOMMU_FL_CACHEABLE;
	if (tex & 0x4)
		attr |= FAME_IOMMU_FL_TEX0;

	return attr;
}

static int fame_iommu_map_fb(phys_addr_t fb_phys, size_t size, u32 *ttbr0,
			     u32 *prrr, u32 *nmrr)
{
	phys_addr_t pt_phys;
	u32 map_start = (u32)fb_phys & ~(FAME_IOMMU_SECTION_SIZE - 1);
	u32 map_end = ALIGN((u32)fb_phys + size, FAME_IOMMU_SECTION_SIZE);
	u32 attr;
	u32 addr;
	ulong start, end;
	int tex;

	if (!fame_mdp_iommu_fl_table) {
		fame_mdp_iommu_fl_table = memalign(FAME_IOMMU_FL_TABLE_SIZE,
						   FAME_IOMMU_FL_TABLE_SIZE);
		if (!fame_mdp_iommu_fl_table) {
			printf("fame_mdp: failed to allocate MDP IOMMU page table\n");
			return -ENOMEM;
		}
	}

	memset(fame_mdp_iommu_fl_table, 0, FAME_IOMMU_FL_TABLE_SIZE);

	*prrr = fame_read_prrr();
	*nmrr = fame_read_nmrr();
	tex = fame_iommu_get_tex_class(*prrr, *nmrr,
				       FAME_IOMMU_CP_NONCACHED,
				       FAME_IOMMU_CP_NONCACHED,
				       FAME_IOMMU_MT_NORMAL, 1);
	if (tex < 0) {
		printf("fame_mdp: no noncached normal TEX class in prrr=%08x nmrr=%08x; using tex=0\n",
		       *prrr, *nmrr);
		tex = 0;
	}

	attr = fame_iommu_fl_section_attr(tex);
	for (addr = map_start; addr < map_end; addr += FAME_IOMMU_SECTION_SIZE) {
		fame_mdp_iommu_fl_table[addr >> 20] =
			(addr & 0xfff00000) | FAME_IOMMU_FL_NG |
			FAME_IOMMU_FL_TYPE_SECT | FAME_IOMMU_FL_SHARED | attr;
	}

	start = (ulong)fame_mdp_iommu_fl_table;
	end = ALIGN(start + FAME_IOMMU_FL_TABLE_SIZE, ARCH_DMA_MINALIGN);
	flush_dcache_range(start, end);

	pt_phys = virt_to_phys(fame_mdp_iommu_fl_table);
	*ttbr0 = (u32)pt_phys | FAME_IOMMU_TTBR0_FLAGS;

	printf("fame_mdp: MDP IOMMU map fb phys=%08x..%08x table=%08x ttbr0=%08x prrr=%08x nmrr=%08x tex=%d\n",
	       (u32)fb_phys, (u32)fb_phys + size - 1, (u32)pt_phys, *ttbr0,
	       *prrr, *nmrr, tex);

	return 0;
}

static void fame_iommu_reset_context(phys_addr_t base, u32 ctx)
{
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_ACTLR, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_SCTLR, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_FSR, 0x4000000f);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_FSRRESTORE, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_TTBR0, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_TTBR1, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_TTBCR, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_BFBCR, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_PAR, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_FAR, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_CTX_TLBIALL, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_TLBFLPTER, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_TLBSLPTER, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_TLBLCKR, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_CONTEXTIDR, 0);
}

static void fame_iommu_reset_port(phys_addr_t base)
{
	int ctx;

	fame_iommu_write(base, FAME_IOMMU_ESRRESTORE, 0);
	fame_iommu_write(base, FAME_IOMMU_TESTBUSCR, 0);
	fame_iommu_write(base, FAME_IOMMU_TLBRSW, 0);
	fame_iommu_write(base, FAME_IOMMU_GLOBAL_TLBIALL, 0);
	fame_iommu_write(base, FAME_IOMMU_RPU_ACR, 0);
	fame_iommu_write(base, FAME_IOMMU_CR, FAME_IOMMU_CR_TLBLKCRWE);

	for (ctx = 0; ctx < FAME_IOMMU_CTX_COUNT; ctx++) {
		writel(0, fame_iommu_cbacr_reg(base, ctx));
		readl(fame_iommu_cbacr_reg(base, ctx));
		fame_iommu_reset_context(base, ctx);
	}
}

static void fame_iommu_config_mids(phys_addr_t base, u32 ctx,
				   const u8 *mids, int num_mids)
{
	int i;

	writel(0, fame_iommu_cbacr_reg(base, ctx));
	readl(fame_iommu_cbacr_reg(base, ctx));

	for (i = 0; i < num_mids; i++) {
		u32 mid = mids[i];
		u32 val = FAME_IOMMU_M2VCBR_CBNDX(ctx) |
			FAME_IOMMU_M2VCBR_NSCFG_NONSEC;

		writel(val, fame_iommu_m2vcbr_reg(base, mid));
		readl(fame_iommu_m2vcbr_reg(base, mid));
	}
}

static void fame_iommu_program_context(phys_addr_t base, u32 ctx, u32 ttbr0,
				       u32 prrr, u32 nmrr)
{
	fame_iommu_reset_context(base, ctx);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_TTBCR, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_TTBR0, ttbr0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_TTBR1, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_PRRR, prrr);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_NMRR, nmrr);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_CTX_TLBIALL, 0);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_CONTEXTIDR, ctx);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_ACTLR,
			     FAME_IOMMU_ACTLR_ENABLE);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_BFBCR,
			     FAME_IOMMU_BFBCR_ENABLE);
	fame_iommu_ctx_write(base, ctx, FAME_IOMMU_SCTLR,
			     FAME_IOMMU_SCTLR_ENABLE);
}

static void fame_iommu_dump_port(const char *name, phys_addr_t base)
{
	printf("  %s: rev=%08x idr=%08x cr=%08x esr=%08x cbacr0=%08x cbacr1=%08x\n",
	       name, readl(base + FAME_IOMMU_REV), readl(base + FAME_IOMMU_IDR),
	       readl(base + FAME_IOMMU_CR), readl(base + FAME_IOMMU_ESR),
	       readl(fame_iommu_cbacr_reg(base, 0)),
	       readl(fame_iommu_cbacr_reg(base, 1)));
	printf("  %s m2v: 0=%08x 1=%08x 2=%08x 3=%08x 4=%08x 5=%08x\n",
	       name,
	       readl(fame_iommu_m2vcbr_reg(base, 0)),
	       readl(fame_iommu_m2vcbr_reg(base, 1)),
	       readl(fame_iommu_m2vcbr_reg(base, 2)),
	       readl(fame_iommu_m2vcbr_reg(base, 3)),
	       readl(fame_iommu_m2vcbr_reg(base, 4)),
	       readl(fame_iommu_m2vcbr_reg(base, 5)));
	printf("  %s m2v: 6=%08x 7=%08x 8=%08x 9=%08x 10=%08x\n",
	       name,
	       readl(fame_iommu_m2vcbr_reg(base, 6)),
	       readl(fame_iommu_m2vcbr_reg(base, 7)),
	       readl(fame_iommu_m2vcbr_reg(base, 8)),
	       readl(fame_iommu_m2vcbr_reg(base, 9)),
	       readl(fame_iommu_m2vcbr_reg(base, 10)));
	printf("  %s ctx0: sctlr=%08x actlr=%08x ctxidr=%08x ttbr0=%08x ttbcr=%08x prrr=%08x nmrr=%08x bfbcr=%08x\n",
	       name, fame_iommu_ctx_read(base, 0, FAME_IOMMU_SCTLR),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_ACTLR),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_CONTEXTIDR),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_TTBR0),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_TTBCR),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_PRRR),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_NMRR),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_BFBCR));
	printf("  %s ctx0 fault: fsr=%08x far=%08x fsynr=%08x/%08x\n",
	       name,
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_FSR),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_FAR),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_FSYNR0),
	       fame_iommu_ctx_read(base, 0, FAME_IOMMU_FSYNR1));
	printf("  %s ctx1: sctlr=%08x actlr=%08x ctxidr=%08x ttbr0=%08x ttbcr=%08x prrr=%08x nmrr=%08x bfbcr=%08x\n",
	       name, fame_iommu_ctx_read(base, 1, FAME_IOMMU_SCTLR),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_ACTLR),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_CONTEXTIDR),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_TTBR0),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_TTBCR),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_PRRR),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_NMRR),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_BFBCR));
	printf("  %s ctx1 fault: fsr=%08x far=%08x fsynr=%08x/%08x\n",
	       name,
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_FSR),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_FAR),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_FSYNR0),
	       fame_iommu_ctx_read(base, 1, FAME_IOMMU_FSYNR1));
}

static void fame_iommu_dump_state(const char *tag)
{
	printf("%s:\n", tag);
	fame_iommu_dump_port("mdp_iommu0", FAME_MDP_IOMMU0_BASE);
	fame_iommu_dump_port("mdp_iommu1", FAME_MDP_IOMMU1_BASE);
}

static bool fame_iommu_clock_looks_active(void)
{
	u32 ahb = readl(MMCC_REG(AHB_EN_REG));
	u32 halt = readl(MMCC_REG(DBG_BUS_VEC_F_REG));

	return (ahb & MMCC_SMMU_AHB_EN) || !(halt & BIT(22));
}

static void fame_iommu_dump_state_if_safe(const char *tag, bool force)
{
	if (force || fame_iommu_clock_looks_active()) {
		fame_iommu_dump_state(tag);
		return;
	}

	printf("%s:\n", tag);
	printf("  skipped: smmu_ahb_clk does not look active (ahb=%08x halt_f=%08x); use `fame_mdp dump all` to force MMIO reads\n",
	       readl(MMCC_REG(AHB_EN_REG)),
	       readl(MMCC_REG(DBG_BUS_VEC_F_REG)));
}

static __maybe_unused int fame_mdp_enable_iommu(phys_addr_t fb_phys,
					       size_t size)
{
	static const u8 cb0_mids[] = { 0, 2 };
	static const u8 cb1_mids[] = { 1, 3, 4, 5, 6, 7, 8, 9, 10 };
	u32 ttbr0, prrr, nmrr;
	int ret;

	printf("fame_mdp: enable SMMU AHB clock for MDP IOMMU\n");
	fame_mmcc_update_bits(AHB_EN_REG, MMCC_SMMU_AHB_EN, MMCC_SMMU_AHB_EN);
	ret = fame_mmcc_wait_halt("smmu_ahb_clk", DBG_BUS_VEC_F_REG, 22);
	if (ret)
		return ret;

	ret = fame_iommu_map_fb(fb_phys, size, &ttbr0, &prrr, &nmrr);
	if (ret)
		return ret;

	fame_iommu_dump_state("before MDP IOMMU setup");

	fame_iommu_reset_port(FAME_MDP_IOMMU0_BASE);
	fame_iommu_reset_port(FAME_MDP_IOMMU1_BASE);

	fame_iommu_config_mids(FAME_MDP_IOMMU0_BASE, 0, cb0_mids,
			       ARRAY_SIZE(cb0_mids));
	fame_iommu_config_mids(FAME_MDP_IOMMU0_BASE, 1, cb1_mids,
			       ARRAY_SIZE(cb1_mids));
	fame_iommu_config_mids(FAME_MDP_IOMMU1_BASE, 0, cb0_mids,
			       ARRAY_SIZE(cb0_mids));
	fame_iommu_config_mids(FAME_MDP_IOMMU1_BASE, 1, cb1_mids,
			       ARRAY_SIZE(cb1_mids));

	fame_iommu_program_context(FAME_MDP_IOMMU0_BASE, 0, ttbr0, prrr, nmrr);
	fame_iommu_program_context(FAME_MDP_IOMMU0_BASE, 1, ttbr0, prrr, nmrr);
	fame_iommu_program_context(FAME_MDP_IOMMU1_BASE, 0, ttbr0, prrr, nmrr);
	fame_iommu_program_context(FAME_MDP_IOMMU1_BASE, 1, ttbr0, prrr, nmrr);

	fame_iommu_dump_state("after MDP IOMMU setup");

	return 0;
}

static void fame_rpm_clear_ack(void)
{
	int i;

	for (i = 0; i < FAME_RPM_ACK_SEL_SIZE; i++)
		writel(0, RPM_CTRL_REG(FAME_RPM_ACK_SEL_OFF + i));
	writel(0, RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF));
	readl(RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF));
}

static int fame_rpm_init(void)
{
	u32 fw0 = readl(RPM_STATUS_REG(0));
	u32 fw1 = readl(RPM_STATUS_REG(1));
	u32 fw2 = readl(RPM_STATUS_REG(2));

	printf("fame_mdp: RPM firmware %u.%u.%u ctrl_ack=%08x\n",
	       fw0, fw1, fw2, readl(RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF)));
	if (fw0 != FAME_RPM_VERSION) {
		printf("fame_mdp: RPM firmware major %u, expected %u\n",
		       fw0, FAME_RPM_VERSION);
		return -EFAULT;
	}

	writel(fw0, RPM_CTRL_REG(0));
	writel(fw1, RPM_CTRL_REG(1));
	writel(fw2, RPM_CTRL_REG(2));
	fame_rpm_clear_ack();

	return 0;
}

static int fame_rpm_wait_ack(const char *name)
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
		if (ack & FAME_RPM_NOTIFICATION) {
			printf("fame_mdp: RPM %s notification after %d us ack=%08x\n",
			       name, elapsed, ack);
			continue;
		}

		printf("fame_mdp: RPM %s ack after %d us ack=%08x\n",
		       name, elapsed, ack);
		if (ack & FAME_RPM_REJECTED)
			return -EIO;

		return 0;
	}

	printf("fame_mdp: RPM %s ack timeout req_ctx=%08x ack_ctx=%08x\n",
	       name, readl(RPM_CTRL_REG(FAME_RPM_REQ_CTX_OFF)),
	       readl(RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF)));

	return -ETIMEDOUT;
}

static int fame_rpm_write(const char *name, u32 target_id, u32 select_id,
			  const u32 *words, int count)
{
	u32 sel_mask[FAME_RPM_REQ_SEL_SIZE] = { 0 };
	int i;

	if (select_id >= FAME_RPM_REQ_SEL_SIZE * 32)
		return -EINVAL;

	printf("fame_mdp: RPM request %-10s target=%u select=%u words=%08x %08x\n",
	       name, target_id, select_id, words[0], count > 1 ? words[1] : 0);

	fame_rpm_clear_ack();
	for (i = 0; i < count; i++)
		writel(words[i], RPM_REQ_REG(target_id + i));

	sel_mask[select_id / 32] = BIT(select_id % 32);
	for (i = 0; i < FAME_RPM_REQ_SEL_SIZE; i++)
		writel(sel_mask[i], RPM_CTRL_REG(FAME_RPM_REQ_SEL_OFF + i));

	writel(BIT(FAME_RPM_ACTIVE_STATE), RPM_CTRL_REG(FAME_RPM_REQ_CTX_OFF));
	readl(RPM_CTRL_REG(FAME_RPM_REQ_CTX_OFF));

	writel(BIT(FAME_RPM_IPC_BIT), FAME_RPM_IPC_REG);

	return fame_rpm_wait_ack(name);
}

static int fame_pm8038_enable_ldo(const struct fame_pm8038_ldo *ldo)
{
	u32 words[2] = {
		ldo->microvolts | FAME_RPM_LDO_PULL_DOWN,
		0,
	};
	int ret;

	ret = fame_rpm_write(ldo->name, ldo->target_id, ldo->select_id,
			     words, ARRAY_SIZE(words));
	printf("fame_mdp: PM8038 %-3s status[%u]=%08x status[%u]=%08x ret=%d\n",
	       ldo->name, ldo->status_id, readl(RPM_STATUS_REG(ldo->status_id)),
	       ldo->status_id + 1, readl(RPM_STATUS_REG(ldo->status_id + 1)),
	       ret);

	return ret;
}

static int fame_pm8038_enable_display_rails(void)
{
	static const struct fame_pm8038_ldo rails[] = {
		{ "L2", 104, 45, 37, 1200000 },
		{ "L8", 116, 57, 43, 2800000 },
		{ "L11", 122, 63, 46, 1800000 },
	};
	int ret;
	int i;

	printf("fame_mdp: enable PM8038 display rails L2/L8/L11 via RPM\n");
	ret = fame_rpm_init();
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(rails); i++) {
		ret = fame_pm8038_enable_ldo(&rails[i]);
		if (ret)
			return ret;
	}

	printf("fame_mdp: PM8038 display rails requested; WLED/backlight is not managed yet\n");
	mdelay(5);

	return 0;
}

static void fame_pm8038_dump_display_rails(const char *tag)
{
	printf("%s:\n", tag);
	printf("  rpm: fw=%08x.%08x.%08x req_ctx=%08x ack_ctx=%08x ipc=%08x\n",
	       readl(RPM_STATUS_REG(0)), readl(RPM_STATUS_REG(1)),
	       readl(RPM_STATUS_REG(2)),
	       readl(RPM_CTRL_REG(FAME_RPM_REQ_CTX_OFF)),
	       readl(RPM_CTRL_REG(FAME_RPM_ACK_CTX_OFF)),
	       readl(FAME_RPM_IPC_REG));
	printf("  ldo: L2[%u/%u]=%08x/%08x L8[%u/%u]=%08x/%08x L11[%u/%u]=%08x/%08x\n",
	       45, 46, readl(RPM_STATUS_REG(45)), readl(RPM_STATUS_REG(46)),
	       57, 58, readl(RPM_STATUS_REG(57)), readl(RPM_STATUS_REG(58)),
	       63, 64, readl(RPM_STATUS_REG(63)), readl(RPM_STATUS_REG(64)));
}

static int fame_mmcc_wait_halt(const char *name, u32 reg, u32 bit)
{
	u32 val = 0;
	int i;

	for (i = 0; i < 5000; i++) {
		val = readl(MMCC_REG(reg));
		if (!(val & BIT(bit))) {
			printf("fame_mdp: %s active after %d us reg[0x%x]=%08x\n",
			       name, i, reg, val);
			return 0;
		}
		udelay(1);
	}

	printf("fame_mdp: %s halt timeout reg[0x%x]=%08x bit=%u\n",
	       name, reg, val, bit);
	return -ETIMEDOUT;
}

static void fame_mdp_dump_state(const char *tag)
{
	printf("%s:\n", tag);
	printf("  pll2: mode=%08x l=%08x m=%08x n=%08x config=%08x status=%08x\n",
	       readl(MMCC_REG(MM_PLL1_MODE_REG)),
	       readl(MMCC_REG(MM_PLL1_L_REG)),
	       readl(MMCC_REG(MM_PLL1_M_REG)),
	       readl(MMCC_REG(MM_PLL1_N_REG)),
	       readl(MMCC_REG(MM_PLL1_CONFIG_REG)),
	       readl(MMCC_REG(MM_PLL1_STATUS_REG)));
	printf("  clk:  mdp_cc=%08x md0=%08x md1=%08x ns=%08x lut_cc=%08x vsync_cc=%08x\n",
	       readl(MMCC_REG(MDP_CC_REG)), readl(MMCC_REG(MDP_MD0_REG)),
	       readl(MMCC_REG(MDP_MD1_REG)), readl(MMCC_REG(MDP_NS_REG)),
	       readl(MMCC_REG(MDP_LUT_CC_REG)),
	       readl(MMCC_REG(MDP_VSYNC_CC_REG)));
	printf("  en:   ahb=%08x axi=%08x gfs=%08x\n",
	       readl(MMCC_REG(AHB_EN_REG)), readl(MMCC_REG(MAXI_EN_REG)),
	       readl(MMCC_REG(MDP_PD_CTL_REG)));
	printf("  halt: b=%08x c=%08x e=%08x f=%08x i=%08x\n",
	       readl(MMCC_REG(DBG_BUS_VEC_B_REG)),
	       readl(MMCC_REG(DBG_BUS_VEC_C_REG)),
	       readl(MMCC_REG(DBG_BUS_VEC_E_REG)),
	       readl(MMCC_REG(DBG_BUS_VEC_F_REG)),
	       readl(MMCC_REG(DBG_BUS_VEC_I_REG)));
	printf("  rst:  axi=%08x ahb=%08x core=%08x\n",
	       readl(MMCC_REG(SW_RESET_AXI_REG)),
	       readl(MMCC_REG(SW_RESET_AHB_REG)),
	       readl(MMCC_REG(SW_RESET_CORE_REG)));
}

static void fame_mdp_force_vendor_src(void)
{
	u32 cc;

	printf("fame_mdp: force MDP source to vendor bank1 PLL2 state\n");

	fame_mmcc_write(MDP_NS_REG, FAME_MDP_NS_VENDOR | FAME_MDP_NS_BANK1_RST);
	fame_mmcc_write(MDP_MD0_REG, FAME_MDP_MD0_VENDOR);
	fame_mmcc_write(MDP_MD1_REG, FAME_MDP_MD1_VENDOR);

	cc = readl(MMCC_REG(MDP_CC_REG));
	cc &= ~(FAME_MDP_CC_BANK1_MND_EN | FAME_MDP_CC_BANK1_MODE_MASK);
	cc |= FAME_MDP_CC_BANK1_PROG;
	fame_mmcc_write(MDP_CC_REG, cc);

	fame_mmcc_write(MDP_NS_REG, FAME_MDP_NS_VENDOR);

	cc = readl(MMCC_REG(MDP_CC_REG));
	cc &= ~FAME_MDP_CC_LOW_MASK;
	cc |= FAME_MDP_CC_VENDOR & FAME_MDP_CC_LOW_MASK;
	fame_mmcc_write(MDP_CC_REG, cc);

	printf("fame_mdp: vendor MDP source cc=%08x md0=%08x md1=%08x ns=%08x\n",
	       readl(MMCC_REG(MDP_CC_REG)), readl(MMCC_REG(MDP_MD0_REG)),
	       readl(MMCC_REG(MDP_MD1_REG)), readl(MMCC_REG(MDP_NS_REG)));
}

static void fame_dsi_dump_state(const char *tag)
{
	printf("%s:\n", tag);
	printf("  mmcc dsi: cc=%08x md=%08x ns=%08x byte_cc=%08x byte_ns=%08x\n",
	       readl(MMCC_REG(DSI_CC_REG)), readl(MMCC_REG(DSI_MD_REG)),
	       readl(MMCC_REG(DSI_NS_REG)),
	       readl(MMCC_REG(DSI_BYTE_CC_REG)),
	       readl(MMCC_REG(DSI_BYTE_NS_REG)));
	printf("  mmcc dsi: esc_cc=%08x esc_ns=%08x pix_cc=%08x pix_md=%08x pix_ns=%08x\n",
	       readl(MMCC_REG(DSI_ESC_CC_REG)),
	       readl(MMCC_REG(DSI_ESC_NS_REG)),
	       readl(MMCC_REG(DSI_PIXEL_CC_REG)),
	       readl(MMCC_REG(DSI_PIXEL_MD_REG)),
	       readl(MMCC_REG(DSI_PIXEL_NS_REG)));
	printf("  mmcc halt: b=%08x c=%08x f=%08x i=%08x rst_ahb=%08x rst_core=%08x\n",
	       readl(MMCC_REG(DBG_BUS_VEC_B_REG)),
	       readl(MMCC_REG(DBG_BUS_VEC_C_REG)),
	       readl(MMCC_REG(DBG_BUS_VEC_F_REG)),
	       readl(MMCC_REG(DBG_BUS_VEC_I_REG)),
	       readl(MMCC_REG(SW_RESET_AHB_REG)),
	       readl(MMCC_REG(SW_RESET_CORE_REG)));
	printf("  pll: c0=%08x c1=%08x c2=%08x c3=%08x c6=%08x c8=%08x c9=%08x c10=%08x rdy=%08x\n",
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_0_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_1_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_2_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_3_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_6_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_8_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_9_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_10_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_RDY_REG)));
	printf("  host: ver=%08x ctrl=%08x status0=%08x fifo=%08x clk_ctrl=%08x clk_status=%08x lane=%08x\n",
	       readl(DSI_REG(DSI_VERSION_REG)), readl(DSI_REG(DSI_CTRL_REG)),
	       readl(DSI_REG(DSI_STATUS0_REG)),
	       readl(DSI_REG(DSI_FIFO_STATUS_REG)),
	       readl(DSI_REG(DSI_CLK_CTRL_REG)),
	       readl(DSI_REG(DSI_CLK_STATUS_REG)),
	       readl(DSI_REG(DSI_LANE_STATUS_REG)));
	printf("  host lane: ctrl=%08x swap=%08x\n",
	       readl(DSI_REG(DSI_LANE_CTRL_REG)),
	       readl(DSI_REG(DSI_LANE_SWAP_CTRL_REG)));
	printf("  host timing: vid0=%08x active_h=%08x active_v=%08x total=%08x hsync=%08x vsync=%08x\n",
	       readl(DSI_REG(DSI_VID_CFG0_REG)),
	       readl(DSI_REG(DSI_ACTIVE_H_REG)),
	       readl(DSI_REG(DSI_ACTIVE_V_REG)),
	       readl(DSI_REG(DSI_TOTAL_REG)),
	       readl(DSI_REG(DSI_ACTIVE_HSYNC_REG)),
	       readl(DSI_REG(DSI_ACTIVE_VSYNC_VPOS_REG)));
	printf("  host err: ack=%08x timeout=%08x dma_base=%08x dma_len=%08x\n",
	       readl(DSI_REG(DSI_ACK_ERR_STATUS_REG)),
	       readl(DSI_REG(DSI_TIMEOUT_STATUS_REG)),
	       readl(DSI_REG(DSI_DMA_BASE_REG)),
	       readl(DSI_REG(DSI_DMA_LEN_REG)));
	printf("  phy: ldo=%08x ctrl0=%08x timing0=%08x timing1=%08x timing2=%08x timing8=%08x reg0=%08x cal=%08x\n",
	       readl(DSI_PHY_REG(DSI_PHY_LDO_CTRL_REG)),
	       readl(DSI_PHY_REG(DSI_PHY_CTRL_0_REG)),
	       readl(DSI_PHY_REG(DSI_PHY_TIMING_CTRL_0_REG)),
	       readl(DSI_PHY_REG(DSI_PHY_TIMING_CTRL_0_REG + 4)),
	       readl(DSI_PHY_REG(DSI_PHY_TIMING_CTRL_0_REG + 8)),
	       readl(DSI_PHY_REG(DSI_PHY_TIMING_CTRL_0_REG + 32)),
	       readl(DSI_PHY_MISC_REG(DSI_PHY_MISC_REGULATOR_CTRL_0_REG)),
	       readl(DSI_PHY_MISC_REG(DSI_PHY_MISC_CAL_STATUS_REG)));
}

static int fame_mdp_get_clk(ofnode mdp, const char *name, struct clk *clk)
{
	int ret;

	ret = clk_get_by_name_nodev(mdp, name, clk);
	if (ret)
		printf("failed to get MDP clock %s: %d\n", name, ret);

	return ret;
}

static int fame_mdp_get_power_domain(struct power_domain *pd)
{
	struct udevice *pd_dev;
	ofnode mmcc;
	int ret;

	mmcc = ofnode_by_compatible(ofnode_null(), "qcom,mmcc-msm8960");
	if (!ofnode_valid(mmcc)) {
		printf("failed to find qcom,mmcc-msm8960 node\n");
		return -ENODEV;
	}

	ret = uclass_get_device_by_ofnode(UCLASS_POWER_DOMAIN, mmcc, &pd_dev);
	if (ret) {
		printf("failed to get MMCC power-domain provider: %d\n", ret);
		return ret;
	}

	pd->dev = pd_dev;
	pd->id = FAME_MDP_PD;
	pd->priv = NULL;

	return 0;
}

static int fame_mdp_enable_resources(void)
{
	struct power_domain pd;
	struct clk core, iface, bus, lut, vsync;
	bool have_vsync = false;
	ofnode mdp;
	ulong rate;
	int ret;

	mdp = ofnode_by_compatible(ofnode_null(), "qcom,mdp4");
	if (!ofnode_valid(mdp)) {
		printf("failed to find qcom,mdp4 node\n");
		return -ENODEV;
	}

	ret = fame_mdp_get_clk(mdp, "core_clk", &core);
	if (ret)
		return ret;
	ret = fame_mdp_get_clk(mdp, "iface_clk", &iface);
	if (ret)
		return ret;
	ret = fame_mdp_get_clk(mdp, "bus_clk", &bus);
	if (ret)
		return ret;
	ret = fame_mdp_get_clk(mdp, "lut_clk", &lut);
	if (ret)
		return ret;
	ret = clk_get_by_name_nodev(mdp, "vsync_clk", &vsync);
	if (!ret)
		have_vsync = true;
	else
		printf("optional MDP clock vsync_clk unavailable: %d\n", ret);

	rate = clk_set_rate(&core, MDP_DIAG_RATE);
	if (rate != MDP_DIAG_RATE) {
		printf("failed to set mdp_clk to %u Hz: got %lu\n",
		       MDP_DIAG_RATE, rate);
		return -EINVAL;
	}

	ret = clk_enable(&bus);
	if (ret)
		return ret;
	ret = clk_enable(&iface);
	if (ret)
		return ret;

	ret = fame_mdp_get_power_domain(&pd);
	if (ret)
		return ret;
	ret = power_domain_on(&pd);
	if (ret)
		return ret;

	ret = clk_enable(&core);
	if (ret)
		return ret;
	ret = clk_enable(&lut);
	if (ret)
		return ret;
	if (have_vsync) {
		ret = clk_enable(&vsync);
		if (ret)
			return ret;
	}

	fame_mdp_force_vendor_src();

	return 0;
}

static void fame_mdp_dump_teisko_mdp4_state(const char *tag)
{
	printf("%s:\n", tag);
	printf("  mdp4: intf_sel=%08x mixer=%08x dma_cfg=%08x dsi_en=%08x\n",
	       readl(MDP_REG(MDP_DISP_INTF_SEL_REG)),
	       readl(MDP_REG(MDP_LAYERMIXER_IN_CFG_REG)),
	       readl(MDP_REG(MDP_DMA_P_CONFIG_REG)),
	       readl(MDP_REG(MDP_DSI_ENABLE_REG)));
	printf("  mdp4: disp_status=%08x intr_en=%08x intr_status=%08x overlay_flush=%08x\n",
	       readl(MDP_REG(MDP_DISP_STATUS_REG)),
	       readl(MDP_REG(MDP_INTR_ENABLE_REG)),
	       readl(MDP_REG(MDP_INTR_STATUS_REG)),
	       readl(MDP_REG(MDP_OVERLAY_FLUSH_REG)));
	printf("  rgb1: size=%08x dst=%08x base0=%08x stride=%08x\n",
	       readl(MDP_REG(MDP_RGB1_SRC_SIZE_REG)),
	       readl(MDP_REG(MDP_RGB1_DST_SIZE_REG)),
	       readl(MDP_REG(MDP_RGB1_SRCP0_BASE_REG)),
	       readl(MDP_REG(MDP_RGB1_SRC_STRIDE_A_REG)));
	printf("  rgb1: fmt=%08x unpack=%08x op=%08x solid=%08x\n",
	       readl(MDP_REG(MDP_RGB1_SRC_FORMAT_REG)),
	       readl(MDP_REG(MDP_RGB1_SRC_UNPACK_REG)),
	       readl(MDP_REG(MDP_RGB1_OP_MODE_REG)),
	       readl(MDP_REG(MDP_RGB1_SOLID_COLOR_REG)));
	printf("  dsi:  hsync=%08x vperiod=%08x vlen=%08x hctrl=%08x\n",
	       readl(MDP_REG(MDP_DSI_HSYNC_CTRL_REG)),
	       readl(MDP_REG(MDP_DSI_VSYNC_PERIOD_REG)),
	       readl(MDP_REG(MDP_DSI_VSYNC_LEN_REG)),
	       readl(MDP_REG(MDP_DSI_DISPLAY_HCTRL_REG)));
	printf("  dsi:  vstart=%08x vend=%08x pol=%08x underflow=%08x\n",
	       readl(MDP_REG(MDP_DSI_DISPLAY_VSTART_REG)),
	       readl(MDP_REG(MDP_DSI_DISPLAY_VEND_REG)),
	       readl(MDP_REG(MDP_DSI_CTRL_POLARITY_REG)),
	       readl(MDP_REG(MDP_DSI_UNDERFLOW_CLR_REG)));
}

static void fame_mdp_dump_pipe_state(const char *tag)
{
	printf("%s:\n", tag);
	printf("  mdp4: read_cfg=%08x portmap=%08x cs0=%08x cs1=%08x layer_update=%08x\n",
	       readl(MDP_REG(MDP_READ_CNFG_REG)),
	       readl(MDP_REG(MDP_PORTMAP_MODE_REG)),
	       readl(MDP_REG(MDP_CS_CONTROLLER0_REG)),
	       readl(MDP_REG(MDP_CS_CONTROLLER1_REG)),
	       readl(MDP_REG(MDP_LAYERMIXER_UPDATE_REG)));
	printf("  fetch: dma_p=%08x dma_e=%08x vg1=%08x vg2=%08x rgb1=%08x rgb2=%08x\n",
	       readl(MDP_REG(MDP_DMA_P_FETCH_CONFIG_REG)),
	       readl(MDP_REG(MDP_DMA_E_FETCH_CONFIG_REG)),
	       readl(MDP_REG(MDP_VG1_FETCH_CONFIG_REG)),
	       readl(MDP_REG(MDP_VG2_FETCH_CONFIG_REG)),
	       readl(MDP_REG(MDP_RGB1_FETCH_CONFIG_REG)),
	       readl(MDP_REG(MDP_RGB2_FETCH_CONFIG_REG)));
	printf("  dma_p: src_size=%08x base=%08x stride=%08x dst_size=%08x op=%08x\n",
	       readl(MDP_REG(MDP_DMA_P_SRC_SIZE_REG)),
	       readl(MDP_REG(MDP_DMA_P_SRC_BASE_REG)),
	       readl(MDP_REG(MDP_DMA_P_SRC_STRIDE_REG)),
	       readl(MDP_REG(MDP_DMA_P_DST_SIZE_REG)),
	       readl(MDP_REG(MDP_DMA_P_OP_MODE_REG)));
	printf("  ovlp0: cfg=%08x size=%08x base=%08x stride=%08x transp=%08x/%08x/%08x/%08x\n",
	       readl(MDP_REG(MDP_OVLP0_CFG_REG)),
	       readl(MDP_REG(MDP_OVLP0_SIZE_REG)),
	       readl(MDP_REG(MDP_OVLP0_BASE_REG)),
	       readl(MDP_REG(MDP_OVLP0_STRIDE_REG)),
	       readl(MDP_REG(MDP_OVLP0_TRANSP_LOW0_REG)),
	       readl(MDP_REG(MDP_OVLP0_TRANSP_LOW1_REG)),
	       readl(MDP_REG(MDP_OVLP0_TRANSP_HIGH0_REG)),
	       readl(MDP_REG(MDP_OVLP0_TRANSP_HIGH1_REG)));
}

static void fame_tlmm_dump_state(const char *tag)
{
	printf("%s:\n", tag);
	printf("  gpio%d: cfg=%08x inout=%08x\n", FAME_PANEL_RESET_GPIO,
	       readl(TLMM_GPIO_CFG_REG(FAME_PANEL_RESET_GPIO)),
	       readl(TLMM_GPIO_IN_OUT_REG(FAME_PANEL_RESET_GPIO)));
}

static int fame_mdp_fill_teisko_fb(void)
{
	static const u32 colors[] = {
		0xffffffff, 0xffff0000, 0xff00ff00, 0xff0000ff,
		0xffffff00, 0xffff00ff, 0xff00ffff, 0xff000000,
	};
	ulong start, end;
	u32 *fb;
	int x, y;

	if (!fame_mdp_fb) {
		fame_mdp_fb = map_sysmem(TEISKO_VENDOR_FB_BASE,
					 TEISKO_FB_SIZE);
		if (!fame_mdp_fb) {
			printf("fame_mdp: failed to map vendor framebuffer at %08x\n",
			       TEISKO_VENDOR_FB_BASE);
			return -ENOMEM;
		}
	}

	fb = fame_mdp_fb;
	for (y = 0; y < TEISKO_VDISPLAY; y++) {
		for (x = 0; x < TEISKO_HDISPLAY; x++) {
			u32 bar = (x * ARRAY_SIZE(colors)) / TEISKO_HDISPLAY;

			fb[(y * TEISKO_HDISPLAY) + x] = colors[bar];
		}
	}

	start = (ulong)fb;
	end = ALIGN(start + TEISKO_FB_SIZE, ARCH_DMA_MINALIGN);
	flush_dcache_range(start, end);
	wmb();
	printf("fame_mdp: flushed framebuffer dcache %08lx..%08lx\n",
	       start, end);

	return 0;
}

static int fame_mdp_program_teisko_rgb1_framebuffer(void)
{
	phys_addr_t fb_phys;
	int ret;

	ret = fame_mdp_fill_teisko_fb();
	if (ret)
		return ret;

	fb_phys = map_to_sysmem(fame_mdp_fb);

	printf("fame_mdp: leave MDP IOMMU disabled; vendor UEFI scans out physical FB addresses\n");
	printf("fame_mdp: program RGB1 framebuffer virt=%08lx phys=%08x stride=%u size=%u\n",
	       (ulong)fame_mdp_fb, (u32)fb_phys, TEISKO_FB_STRIDE,
	       TEISKO_FB_SIZE);

	fame_mdp_write(MDP_RGB1_SRC_SIZE_REG,
			       mdp_wh(TEISKO_HDISPLAY, TEISKO_VDISPLAY));
	fame_mdp_write(MDP_RGB1_SRC_XY_REG, 0);
	fame_mdp_write(MDP_RGB1_DST_SIZE_REG,
		       mdp_wh(TEISKO_HDISPLAY, TEISKO_VDISPLAY));
	fame_mdp_write(MDP_RGB1_DST_XY_REG, 0);
	fame_mdp_write(MDP_RGB1_SRCP0_BASE_REG, (u32)fb_phys);
	fame_mdp_write(MDP_RGB1_SRCP1_BASE_REG, 0);
	fame_mdp_write(MDP_RGB1_SRCP2_BASE_REG, 0);
	fame_mdp_write(MDP_RGB1_SRCP3_BASE_REG, 0);
	fame_mdp_write(MDP_RGB1_SRC_STRIDE_A_REG, TEISKO_FB_STRIDE);
	fame_mdp_write(MDP_RGB1_SRC_STRIDE_B_REG, 0);
	fame_mdp_write(MDP_RGB1_SRC_FORMAT_REG, MDP4_PIPE_SRC_FORMAT_XRGB8888);
	fame_mdp_write(MDP_RGB1_SRC_UNPACK_REG,
		       MDP4_PIPE_SRC_UNPACK_XRGB8888);
	fame_mdp_write(MDP_RGB1_SOLID_COLOR_REG, 0);
	fame_mdp_write(MDP_RGB1_OP_MODE_REG, MDP4_RGB1_OP_MODE_FETCH);
	fame_mdp_write(MDP_RGB1_PHASEX_STEP_REG, MDP4_PHASE_STEP_DEFAULT);
	fame_mdp_write(MDP_RGB1_PHASEY_STEP_REG, MDP4_PHASE_STEP_DEFAULT);

	return 0;
}

static int fame_mdp_program_teisko_mdp4(void)
{
	u32 hsync_start_x = TEISKO_HTOTAL - TEISKO_HSYNC_START;
	u32 hsync_end_x = TEISKO_HTOTAL -
		(TEISKO_HSYNC_START - TEISKO_HDISPLAY) - 1;
	u32 vsync_period = TEISKO_VTOTAL * TEISKO_HTOTAL;
	u32 vsync_len = (TEISKO_VSYNC_END - TEISKO_VSYNC_START) *
		TEISKO_HTOTAL;
	u32 display_v_start = (TEISKO_VTOTAL - TEISKO_VSYNC_START) *
		TEISKO_HTOTAL;
	u32 display_v_end = vsync_period -
		((TEISKO_VSYNC_START - TEISKO_VDISPLAY) * TEISKO_HTOTAL) - 1;
	u32 intf_sel;

	fame_mdp_write(MDP_CS_CONTROLLER0_REG, 0);
	fame_mdp_write(MDP_CS_CONTROLLER1_REG, 0);
	fame_mdp_write(MDP_PORTMAP_MODE_REG, 0);
	fame_mdp_write(MDP_READ_CNFG_REG, 0x00003333);

	fame_mdp_write(MDP_DMA_P_FETCH_CONFIG_REG, MDP4_DMA_P_FETCH_CONFIG);
	fame_mdp_write(MDP_DMA_E_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);
	fame_mdp_write(MDP_VG1_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);
	fame_mdp_write(MDP_VG2_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);
	fame_mdp_write(MDP_RGB1_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);
	fame_mdp_write(MDP_RGB2_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);

	if (fame_mdp_program_teisko_rgb1_framebuffer())
		return -ENOMEM;

	fame_mdp_write(MDP_LAYERMIXER_UPDATE_REG, 1);
	fame_mdp_write(MDP_LAYERMIXER_IN_CFG_REG, MDP4_RGB1_BASE_STAGE);
	fame_mdp_write(MDP_VG1_OP_MODE_REG, 0);
	fame_mdp_write(MDP_VG2_OP_MODE_REG, 0);
	fame_mdp_write(MDP_DMA_P_OP_MODE_REG, 0);
	fame_mdp_write(MDP_DMA_S_OP_MODE_REG, 0);

	fame_mdp_write(MDP_DMA_P_SRC_SIZE_REG,
		       mdp_wh(TEISKO_HDISPLAY, TEISKO_VDISPLAY));
	fame_mdp_write(MDP_DMA_P_SRC_BASE_REG, 0);
	fame_mdp_write(MDP_DMA_P_SRC_STRIDE_REG, 0);
	fame_mdp_write(MDP_DMA_P_DST_SIZE_REG, 0);

	fame_mdp_write(MDP_OVLP0_BASE_REG, 0);
	fame_mdp_write(MDP_OVLP0_SIZE_REG,
		       mdp_wh(TEISKO_HDISPLAY, TEISKO_VDISPLAY));
	fame_mdp_write(MDP_OVLP0_STRIDE_REG, 0);
	fame_mdp_write(MDP_OVLP0_TRANSP_LOW0_REG, 0);
	fame_mdp_write(MDP_OVLP0_TRANSP_LOW1_REG, 0);
	fame_mdp_write(MDP_OVLP0_TRANSP_HIGH0_REG, 0);
	fame_mdp_write(MDP_OVLP0_TRANSP_HIGH1_REG, 0);
	fame_mdp_write(MDP_OVLP0_CFG_REG, 3);

	fame_mdp_write(MDP_DSI_ENABLE_REG, 0);
	fame_mdp_write(MDP_DSI_HSYNC_CTRL_REG,
		       mdp_pair(TEISKO_HSYNC_END - TEISKO_HSYNC_START,
				TEISKO_HTOTAL));
	fame_mdp_write(MDP_DSI_VSYNC_PERIOD_REG, vsync_period);
	fame_mdp_write(MDP_DSI_VSYNC_LEN_REG, vsync_len);
	fame_mdp_write(MDP_DSI_DISPLAY_HCTRL_REG,
		       mdp_pair(hsync_start_x, hsync_end_x));
	fame_mdp_write(MDP_DSI_DISPLAY_VSTART_REG, display_v_start);
	fame_mdp_write(MDP_DSI_DISPLAY_VEND_REG, display_v_end);
	fame_mdp_write(MDP_DSI_CTRL_POLARITY_REG, 0);
	fame_mdp_write(MDP_DSI_UNDERFLOW_CLR_REG, 0);
	fame_mdp_write(MDP_DSI_ACTIVE_HCTL_REG, 0);
	fame_mdp_write(MDP_DSI_HSYNC_SKEW_REG, 0);
	fame_mdp_write(MDP_DSI_BORDER_CLR_REG, 0);
	fame_mdp_write(MDP_DSI_ACTIVE_VSTART_REG, 0);
	fame_mdp_write(MDP_DSI_ACTIVE_VEND_REG, 0);

	fame_mdp_write(MDP_DMA_P_CONFIG_REG, MDP4_DMA_DSI_CONFIG);

	intf_sel = readl(MDP_REG(MDP_DISP_INTF_SEL_REG));
	intf_sel &= ~MDP4_DSI_INTF_SEL_MASK;
	intf_sel |= MDP4_DSI_INTF_SEL;
	fame_mdp_write(MDP_DISP_INTF_SEL_REG, intf_sel);

	fame_mdp_write(MDP_DSI_ENABLE_REG, 1);
	fame_mdp_write(MDP_INTR_CLEAR_REG, 0xffffffff);
	fame_mdp_write(MDP_OVERLAY_FLUSH_REG, BIT(0) | BIT(4));

	return 0;
}

static int fame_dsi_program_pll(void)
{
	u32 val;
	int i;

	printf("fame_mdp: program vendor-UEFI DSI PLL c1=%02x c2=%02x c3=%02x c6=%02x c8=%02x c9=%02x c10=%02x\n",
	       FAME_DSI_PLL_CTRL_1_VENDOR, FAME_DSI_PLL_CTRL_2_VENDOR,
	       FAME_DSI_PLL_CTRL_3_VENDOR, FAME_DSI_PLL_CTRL_6_VENDOR,
	       FAME_DSI_PLL_CTRL_8_VENDOR, FAME_DSI_PLL_CTRL_9_VENDOR,
	       FAME_DSI_PLL_CTRL_10_VENDOR);

	fame_dsi_pll_write(DSI_PLL_CTRL_0_REG, 0);
	udelay(1);
	fame_dsi_pll_write(DSI_PLL_CTRL_1_REG,
			   FAME_DSI_PLL_CTRL_1_VENDOR);
	fame_dsi_pll_write(DSI_PLL_CTRL_2_REG,
			   FAME_DSI_PLL_CTRL_2_VENDOR);
	fame_dsi_pll_write(DSI_PLL_CTRL_3_REG,
			   FAME_DSI_PLL_CTRL_3_VENDOR);
	fame_dsi_pll_write(DSI_PLL_CTRL_6_REG,
			   FAME_DSI_PLL_CTRL_6_VENDOR);
	fame_dsi_pll_write(DSI_PLL_CTRL_8_REG,
			   FAME_DSI_PLL_CTRL_8_VENDOR);
	fame_dsi_pll_write(DSI_PLL_CTRL_9_REG,
			   FAME_DSI_PLL_CTRL_9_VENDOR);
	fame_dsi_pll_write(DSI_PLL_CTRL_10_REG,
			   FAME_DSI_PLL_CTRL_10_VENDOR);
	fame_dsi_pll_write(DSI_PLL_CTRL_0_REG, DSI_PLL_ENABLE);

	for (i = 0; i < DSI_PLL_LOCK_READS; i++) {
		val = readl(DSI_PLL_REG(DSI_PLL_RDY_REG));
		if (val & DSI_PLL_READY) {
			printf("fame_mdp: DSI PLL ready after %d us rdy=%08x\n",
			       i * DSI_PLL_LOCK_POLL_US, val);
			return 0;
		}
		udelay(DSI_PLL_LOCK_POLL_US);
	}

	val = readl(DSI_PLL_REG(DSI_PLL_RDY_REG));
	if (val & DSI_PLL_READY) {
		printf("fame_mdp: DSI PLL ready after final read rdy=%08x\n",
		       val);
		return 0;
	}

	printf("fame_mdp: DSI PLL did not lock c0=%08x c1=%08x c2=%08x c3=%08x c6=%08x c8=%08x c9=%08x c10=%08x rdy=%08x\n",
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_0_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_1_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_2_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_3_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_6_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_8_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_9_REG)),
	       readl(DSI_PLL_REG(DSI_PLL_CTRL_10_REG)),
	       val);
	return -ETIMEDOUT;
}

static void fame_mmcc_program_dsi_src(void)
{
	fame_mmcc_update_bits(DSI_NS_REG, MMCC_DSI_MND_RESET,
			      MMCC_DSI_MND_RESET);
	fame_mmcc_write(DSI_MD_REG, 0);
	fame_mmcc_write(DSI_NS_REG, FAME_DSI_NS_VENDOR);
	fame_mmcc_write(DSI_CC_REG, FAME_DSI_CC_ROOT_VENDOR);
	fame_mmcc_update_bits(DSI_NS_REG, MMCC_DSI_MND_RESET, 0);
}

static void fame_mmcc_program_dsi_byte(void)
{
	fame_mmcc_write(DSI_BYTE_NS_REG, FAME_DSI_BYTE_NS_VENDOR);
	fame_mmcc_write(DSI_BYTE_CC_REG, FAME_DSI_BYTE_CC_ROOT_VENDOR);
}

static void fame_mmcc_program_dsi_esc(void)
{
	fame_mmcc_write(DSI_ESC_NS_REG, FAME_DSI_ESC_NS_VENDOR);
	fame_mmcc_write(DSI_ESC_CC_REG, FAME_DSI_ESC_CC_ROOT_VENDOR);
}

static void fame_mmcc_program_dsi_pixel(void)
{
	fame_mmcc_update_bits(DSI_PIXEL_NS_REG, MMCC_DSI_MND_RESET,
			      MMCC_DSI_MND_RESET);
	fame_mmcc_write(DSI_PIXEL_MD_REG, 0);
	fame_mmcc_write(DSI_PIXEL_NS_REG, FAME_DSI_PIXEL_NS_VENDOR);
	fame_mmcc_write(DSI_PIXEL_CC_REG, FAME_DSI_PIXEL_CC_ROOT_VENDOR);
	fame_mmcc_update_bits(DSI_PIXEL_NS_REG, MMCC_DSI_MND_RESET, 0);
}

static int fame_mmcc_enable_dsi_branch(const char *name, u32 cc_reg,
				       u32 halt_reg, u32 halt_bit)
{
	fame_mmcc_update_bits(cc_reg, MMCC_DSI_CLK_BRANCH_EN,
			      MMCC_DSI_CLK_BRANCH_EN);

	return fame_mmcc_wait_halt(name, halt_reg, halt_bit);
}

static int fame_dsi_program_mmcc(void)
{
	int ret;

	printf("fame_mdp: enable DSI AHB clocks and pulse DSI resets\n");
	fame_mmcc_update_bits(AHB_EN_REG,
			      MMCC_DSI_AMP_AHB_EN | MMCC_DSI_M_AHB_EN |
			      MMCC_DSI_S_AHB_EN,
			      MMCC_DSI_AMP_AHB_EN | MMCC_DSI_M_AHB_EN |
			      MMCC_DSI_S_AHB_EN);

	ret = fame_mmcc_wait_halt("amp_ahb_clk", DBG_BUS_VEC_F_REG, 18);
	if (ret)
		return ret;
	ret = fame_mmcc_wait_halt("dsi_m_ahb_clk", DBG_BUS_VEC_F_REG, 19);
	if (ret)
		return ret;
	ret = fame_mmcc_wait_halt("dsi_s_ahb_clk", DBG_BUS_VEC_F_REG, 21);
	if (ret)
		return ret;

	fame_mmcc_update_bits(SW_RESET_AHB_REG, BIT(6) | BIT(5),
			      BIT(6) | BIT(5));
	fame_mmcc_update_bits(SW_RESET_CORE_REG, BIT(20) | BIT(7),
			      BIT(20) | BIT(7));
	udelay(10);
	fame_mmcc_update_bits(SW_RESET_AHB_REG, BIT(6) | BIT(5), 0);
	fame_mmcc_update_bits(SW_RESET_CORE_REG, BIT(20) | BIT(7), 0);
	udelay(10);

	return 0;
}

static int fame_dsi_program_link_clocks(void)
{
	int ret;
	int first_ret = 0;

	printf("fame_mdp: program DSI RCGs from dsi1pll/dsi1pllbyte\n");
	fame_mmcc_program_dsi_byte();
	fame_mmcc_program_dsi_esc();
	fame_mmcc_program_dsi_src();
	fame_mmcc_program_dsi_pixel();

	printf("fame_mdp: force DSI host clock gates before MMCC branch polling\n");
	fame_dsi_write(DSI_CLK_CTRL_REG, DSI_CLK_CTRL_ENABLE_CLKS);

	printf("fame_mdp: enable DSI link clocks byte/esc/src/pixel\n");
	ret = fame_mmcc_enable_dsi_branch("dsi1_byte_clk", DSI_BYTE_CC_REG,
					  DBG_BUS_VEC_B_REG, 21);
	if (ret && !first_ret)
		first_ret = ret;
	ret = fame_mmcc_enable_dsi_branch("dsi1_esc_clk", DSI_ESC_CC_REG,
					  DBG_BUS_VEC_I_REG, 1);
	if (ret && !first_ret)
		first_ret = ret;
	ret = fame_mmcc_enable_dsi_branch("dsi1_clk", DSI_CC_REG,
					  DBG_BUS_VEC_C_REG, 2);
	if (ret && !first_ret)
		first_ret = ret;
	ret = fame_mmcc_enable_dsi_branch("dsi1_pixel_clk", DSI_PIXEL_CC_REG,
					  DBG_BUS_VEC_C_REG, 6);
	if (ret && !first_ret)
		first_ret = ret;

	if (first_ret)
		printf("fame_mdp: continuing despite MMCC DSI halt bit timeout; host clk_status=%08x\n",
		       readl(DSI_REG(DSI_CLK_STATUS_REG)));

	return 0;
}

static void fame_dsi_host_phy_reset(void)
{
	printf("fame_mdp: release DSI PHY reset line\n");
	fame_dsi_write(DSI_PHY_RESET_REG, 1);
	udelay(1000);
	fame_dsi_write(DSI_PHY_RESET_REG, 0);
	udelay(100);
}

static void fame_dsi_sw_reset(void)
{
	u32 ctrl = readl(DSI_REG(DSI_CTRL_REG));

	printf("fame_mdp: DSI host software reset\n");
	if (ctrl & DSI_CTRL_ENABLE)
		fame_dsi_write(DSI_CTRL_REG, ctrl & ~DSI_CTRL_ENABLE);

	fame_dsi_write(DSI_CLK_CTRL_REG, DSI_CLK_CTRL_ENABLE_CLKS);
	fame_dsi_write(DSI_RESET_REG, 1);
	mdelay(20);
	fame_dsi_write(DSI_RESET_REG, 0);
}

static void fame_dsi_phy_enable(void)
{
	static const u32 regulator[] = { 0x02, 0x08, 0x05, 0x00, 0x20 };
	static const u32 timing[] = {
		0x67, 0x16, 0x0d, 0x00, 0x38, 0x3c,
		0x12, 0x19, 0x18, 0x03, 0x04, 0xa0,
	};
	u32 status;
	int i;

	printf("fame_mdp: enable MSM8227 28nm DSI PHY\n");
	fame_dsi_phy_misc_write(DSI_PHY_MISC_REGULATOR_CTRL_0_REG, 0x02);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_REGULATOR_CTRL_0_REG + 4, 1);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_REGULATOR_CTRL_0_REG + 8, 1);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_REGULATOR_CTRL_0_REG + 12, 0);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_REGULATOR_CTRL_0_REG + 16,
				0x100);

	fame_dsi_phy_write(DSI_PHY_LDO_CTRL_REG, 0x25);
	fame_dsi_phy_write(DSI_PHY_STRENGTH_0_REG, 0xff);
	fame_dsi_phy_write(DSI_PHY_STRENGTH_1_REG, 0x00);
	fame_dsi_phy_write(DSI_PHY_STRENGTH_2_REG, 0x06);
	fame_dsi_phy_write(DSI_PHY_CTRL_0_REG, 0x5f);
	fame_dsi_phy_write(DSI_PHY_CTRL_1_REG, 0x00);
	fame_dsi_phy_write(DSI_PHY_CTRL_2_REG, 0x00);
	fame_dsi_phy_write(DSI_PHY_CTRL_3_REG, 0x10);

	for (i = 0; i < ARRAY_SIZE(regulator); i++)
		fame_dsi_phy_misc_write(DSI_PHY_MISC_REGULATOR_CTRL_0_REG +
					(i * 4), regulator[i]);

	fame_dsi_phy_misc_write(DSI_PHY_MISC_REGULATOR_CAL_PWR_CFG_REG, 0x3);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_CAL_SW_CFG_2_REG, 0x0);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_CAL_HW_CFG_1_REG, 0x5a);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_CAL_HW_CFG_3_REG, 0x10);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_CAL_HW_CFG_4_REG, 0x1);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_CAL_HW_CFG_0_REG, 0x1);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_CAL_HW_TRIGGER_REG, 0x1);
	mdelay(5);
	fame_dsi_phy_misc_write(DSI_PHY_MISC_CAL_HW_TRIGGER_REG, 0x0);

	for (i = 0; i < 5000; i++) {
		status = readl(DSI_PHY_MISC_REG(DSI_PHY_MISC_CAL_STATUS_REG));
		if (!(status & DSI_PHY_CAL_BUSY))
			break;
		udelay(1);
	}
	printf("fame_mdp: DSI PHY calibration status=%08x after %d us\n",
	       readl(DSI_PHY_MISC_REG(DSI_PHY_MISC_CAL_STATUS_REG)), i);

	for (i = 0; i < 4; i++) {
		fame_dsi_phy_write(DSI_PHY_LN_CFG_0(i), 0x80);
		fame_dsi_phy_write(DSI_PHY_LN_CFG_1(i), 0x45);
		fame_dsi_phy_write(DSI_PHY_LN_CFG_2(i), 0x00);
		fame_dsi_phy_write(DSI_PHY_LN_TEST_DATAPATH(i), 0x00);
		fame_dsi_phy_write(DSI_PHY_LN_TEST_STR_0(i), 0x01);
		fame_dsi_phy_write(DSI_PHY_LN_TEST_STR_1(i), 0x66);
	}

	fame_dsi_phy_write(DSI_PHY_LNCK_CFG_0_REG, 0x40);
	fame_dsi_phy_write(DSI_PHY_LNCK_CFG_1_REG, 0x67);
	fame_dsi_phy_write(DSI_PHY_LNCK_CFG_2_REG, 0x00);
	fame_dsi_phy_write(DSI_PHY_LNCK_TEST_DATAPATH_REG, 0x00);
	fame_dsi_phy_write(DSI_PHY_LNCK_TEST_STR0_REG, 0x01);
	fame_dsi_phy_write(DSI_PHY_LNCK_TEST_STR1_REG, 0x88);

	fame_dsi_phy_write(DSI_PHY_BIST_CTRL_4_REG, 0x0f);
	fame_dsi_phy_write(DSI_PHY_BIST_CTRL_1_REG, 0x03);
	fame_dsi_phy_write(DSI_PHY_BIST_CTRL_0_REG, 0x03);
	fame_dsi_phy_write(DSI_PHY_BIST_CTRL_4_REG, 0x00);

	for (i = 0; i < ARRAY_SIZE(timing); i++)
		fame_dsi_phy_write(DSI_PHY_TIMING_CTRL_0_REG + (i * 4),
				   timing[i]);
}

static void fame_dsi_host_setup(void)
{
	printf("fame_mdp: program DSI host video timing/control\n");
	fame_dsi_sw_reset();
	fame_dsi_write(DSI_CTRL_REG, 0);
	fame_dsi_write(DSI_ACTIVE_H_REG, 0x02100030);
	fame_dsi_write(DSI_ACTIVE_V_REG, 0x032f000f);
	fame_dsi_write(DSI_TOTAL_REG, 0x033c023c);
	fame_dsi_write(DSI_ACTIVE_HSYNC_REG, 0x00040000);
	fame_dsi_write(DSI_ACTIVE_VSYNC_HPOS_REG, 0);
	fame_dsi_write(DSI_ACTIVE_VSYNC_VPOS_REG, 0x00010000);
	fame_dsi_write(DSI_VID_CFG0_REG, 0x00009130);
	fame_dsi_write(DSI_VID_CFG1_REG, 0);
	fame_dsi_write(DSI_CMD_DMA_CTRL_REG, DSI_CMD_DMA_CTRL_LPM_FB);
	fame_dsi_write(DSI_TRIG_CTRL_REG, 0x00000004);
	fame_dsi_write(DSI_CLKOUT_TIMING_CTRL_REG, 0x00000318);
	fame_dsi_write(DSI_EOT_PACKET_CTRL_REG, 1);
	fame_dsi_write(DSI_ERR_INT_MASK0_REG, 0x13ff3fe0);
	fame_dsi_write(DSI_CLK_CTRL_REG, DSI_CLK_CTRL_ENABLE_CLKS);
	fame_dsi_write(DSI_LANE_SWAP_CTRL_REG, FAME_DSI_LANE_SWAP);
	fame_dsi_write(DSI_CTRL_REG, DSI_CTRL_BASE);
}

static void fame_panel_reset_set(bool high)
{
	writel(high ? TLMM_GPIO_OUT : 0, TLMM_GPIO_IN_OUT_REG(FAME_PANEL_RESET_GPIO));
	readl(TLMM_GPIO_IN_OUT_REG(FAME_PANEL_RESET_GPIO));
}

static void fame_panel_reset_pulse(void)
{
	printf("fame_mdp: pulse Teisko reset GPIO%d high/low/high with vendor cfg\n",
	       FAME_PANEL_RESET_GPIO);
	writel(FAME_PANEL_RESET_GPIO_CFG_VENDOR,
	       TLMM_GPIO_CFG_REG(FAME_PANEL_RESET_GPIO));
	readl(TLMM_GPIO_CFG_REG(FAME_PANEL_RESET_GPIO));

	fame_panel_reset_set(true);
	mdelay(2);
	fame_panel_reset_set(false);
	mdelay(2);
	fame_panel_reset_set(true);
	mdelay(20);

	printf("fame_mdp: reset gpio cfg=%08x inout=%08x\n",
	       readl(TLMM_GPIO_CFG_REG(FAME_PANEL_RESET_GPIO)),
	       readl(TLMM_GPIO_IN_OUT_REG(FAME_PANEL_RESET_GPIO)));
}

static int fame_dsi_wait_dma_idle(const char *name)
{
	u32 status = 0;
	int i;

	for (i = 0; i < DSI_DMA_TIMEOUT_US; i++) {
		status = readl(DSI_REG(DSI_STATUS0_REG));
		if (!(status & DSI_STATUS0_CMD_BUSY))
			return 0;
		udelay(1);
	}

	printf("fame_mdp: DSI DMA timeout after %s status0=%08x fifo=%08x ack=%08x timeout=%08x lane=%08x swap=%08x\n",
	       name, status, readl(DSI_REG(DSI_FIFO_STATUS_REG)),
	       readl(DSI_REG(DSI_ACK_ERR_STATUS_REG)),
	       readl(DSI_REG(DSI_TIMEOUT_STATUS_REG)),
	       readl(DSI_REG(DSI_LANE_STATUS_REG)),
	       readl(DSI_REG(DSI_LANE_SWAP_CTRL_REG)));

	return -ETIMEDOUT;
}

static int fame_dsi_tx_short(const char *name, u8 type, u8 data0, u8 data1)
{
	ulong start = (ulong)fame_dsi_cmd_buf;
	ulong end = ALIGN(start + sizeof(fame_dsi_cmd_buf), ARCH_DMA_MINALIGN);
	u32 saved_ctrl;
	int ret;

	ret = fame_dsi_wait_dma_idle("pre-transfer");
	if (ret)
		return ret;

	memset(fame_dsi_cmd_buf, 0xff, sizeof(fame_dsi_cmd_buf));
	fame_dsi_cmd_buf[0] = data0;
	fame_dsi_cmd_buf[1] = data1;
	fame_dsi_cmd_buf[2] = type;
	fame_dsi_cmd_buf[3] = BIT(7);
	flush_dcache_range(start, end);

	saved_ctrl = readl(DSI_REG(DSI_CTRL_REG));
	fame_dsi_write(DSI_CTRL_REG, saved_ctrl | DSI_CTRL_CMD_MODE_EN |
		       DSI_CTRL_ENABLE);
	fame_dsi_write(DSI_DMA_BASE_REG, (u32)start);
	fame_dsi_write(DSI_DMA_LEN_REG, 4);
	fame_dsi_write(DSI_TRIG_DMA_REG, 1);
	readl(DSI_REG(DSI_TRIG_DMA_REG));

	ret = fame_dsi_wait_dma_idle(name);
	fame_dsi_write(DSI_CTRL_REG, saved_ctrl);

	printf("fame_mdp: dsi cmd %-24s type=%02x data=%02x %02x ret=%d status0=%08x fifo=%08x ack=%08x timeout=%08x\n",
	       name, type, data0, data1, ret, readl(DSI_REG(DSI_STATUS0_REG)),
	       readl(DSI_REG(DSI_FIFO_STATUS_REG)),
	       readl(DSI_REG(DSI_ACK_ERR_STATUS_REG)),
	       readl(DSI_REG(DSI_TIMEOUT_STATUS_REG)));

	return ret;
}

static int fame_panel_teisko_prepare(void)
{
	int ret;

	fame_panel_reset_pulse();

	ret = fame_dsi_tx_short("dcs exit sleep", MIPI_DSI_DCS_SHORT_WRITE,
				MIPI_DCS_EXIT_SLEEP_MODE, 0);
	if (ret)
		return ret;
	mdelay(120);

	ret = fame_dsi_tx_short("generic ff 78",
				MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM,
				0xff, 0x78);
	if (ret)
		return ret;
	ret = fame_dsi_tx_short("dcs address mode",
				MIPI_DSI_DCS_SHORT_WRITE_PARAM,
				MIPI_DCS_SET_ADDRESS_MODE, 0x00);
	if (ret)
		return ret;
	ret = fame_dsi_tx_short("dcs control display",
				MIPI_DSI_DCS_SHORT_WRITE_PARAM,
				MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24);
	if (ret)
		return ret;
	ret = fame_dsi_tx_short("dcs brightness",
				MIPI_DSI_DCS_SHORT_WRITE_PARAM,
				MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x80);
	if (ret)
		return ret;
	ret = fame_dsi_tx_short("dcs control display 2",
				MIPI_DSI_DCS_SHORT_WRITE_PARAM,
				MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24);
	if (ret)
		return ret;

	return 0;
}

static int fame_panel_teisko_enable(void)
{
	int ret;

	ret = fame_dsi_tx_short("dcs display on", MIPI_DSI_DCS_SHORT_WRITE,
				MIPI_DCS_SET_DISPLAY_ON, 0);
	if (ret)
		return ret;
	mdelay(20);

	return 0;
}

static int do_fame_mdp_version(struct cmd_tbl *cmdtp, int flag, int argc,
			       char *const argv[])
{
	ulong count = FAME_MDP_DEFAULT_READS;
	u32 version;
	int ret;
	int i;

	if (argc > 2)
		return CMD_RET_USAGE;

	if (argc == 2) {
		count = dectoul(argv[1], NULL);
		if (!count)
			count = 1;
		if (count > FAME_MDP_MAX_READS)
			count = FAME_MDP_MAX_READS;
	}

	if (!of_machine_is_compatible("nokia,fame")) {
		printf("fame_mdp is only intended for nokia,fame\n");
		return CMD_RET_FAILURE;
	}

	fame_mdp_dump_state("before");

	ret = fame_mdp_enable_resources();
	fame_mdp_dump_state("after sequence");
	if (ret) {
		printf("aborting MDP VERSION read; clock/power sequence failed: %d\n",
		       ret);
		return CMD_RET_FAILURE;
	}

	for (i = 0; i < count; i++) {
		version = readl(MDP_REG(MDP_VERSION_REG));
		printf("  version[%d]=%08x%s\n", i, version,
		       version == MDP_EXPECTED_VERSION ? "" : " (unexpected)");
		if (version != MDP_EXPECTED_VERSION)
			ret = -EIO;
	}

	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

static int do_fame_mdp_mode(struct cmd_tbl *cmdtp, int flag, int argc,
			    char *const argv[])
{
	u32 version;
	int ret;

	if (argc != 1)
		return CMD_RET_USAGE;

	if (!of_machine_is_compatible("nokia,fame")) {
		printf("fame_mdp is only intended for nokia,fame\n");
		return CMD_RET_FAILURE;
	}

	fame_mdp_dump_state("before");
	ret = fame_mdp_enable_resources();
	fame_mdp_dump_state("after resource sequence");
	if (ret) {
		printf("aborting MDP4 mode program; resources failed: %d\n",
		       ret);
		return CMD_RET_FAILURE;
	}

	version = readl(MDP_REG(MDP_VERSION_REG));
	printf("  version=%08x%s\n", version,
	       version == MDP_EXPECTED_VERSION ? "" : " (unexpected)");
	if (version != MDP_EXPECTED_VERSION)
		return CMD_RET_FAILURE;

	fame_mdp_dump_teisko_mdp4_state("before MDP4 Teisko mode");
	ret = fame_mdp_program_teisko_mdp4();
	if (ret) {
		printf("aborting MDP4 mode program; MDP4 setup failed: %d\n",
		       ret);
		return CMD_RET_FAILURE;
	}
	fame_mdp_dump_teisko_mdp4_state("after MDP4 Teisko mode");
	printf("MDP4 DSI-side timing programmed; DSI host, PHY, reset GPIO, and panel DCS are not touched here\n");

	return CMD_RET_SUCCESS;
}

static int do_fame_mdp_panel(struct cmd_tbl *cmdtp, int flag, int argc,
			     char *const argv[])
{
	u32 version;
	int ret;

	if (argc != 1)
		return CMD_RET_USAGE;

	if (!of_machine_is_compatible("nokia,fame")) {
		printf("fame_mdp is only intended for nokia,fame\n");
		return CMD_RET_FAILURE;
	}

	fame_mdp_dump_state("before MDP resources");
	ret = fame_mdp_enable_resources();
	fame_mdp_dump_state("after MDP resources");
	if (ret) {
		printf("aborting DSI panel program; MDP resources failed: %d\n",
		       ret);
		return CMD_RET_FAILURE;
	}

	version = readl(MDP_REG(MDP_VERSION_REG));
	printf("  mdp version=%08x%s\n", version,
	       version == MDP_EXPECTED_VERSION ? "" : " (unexpected)");
	if (version != MDP_EXPECTED_VERSION)
		return CMD_RET_FAILURE;

	ret = fame_pm8038_enable_display_rails();
	if (ret) {
		printf("aborting DSI panel program; PM8038 display rails failed: %d\n",
		       ret);
		return CMD_RET_FAILURE;
	}

	fame_dsi_dump_state("before DSI panel sequence");
	ret = fame_dsi_program_mmcc();
	if (ret)
		return CMD_RET_FAILURE;
	fame_dsi_host_phy_reset();
	fame_dsi_phy_enable();
	ret = fame_dsi_program_pll();
	if (ret) {
		fame_dsi_dump_state("after failed DSI PLL lock");
		return CMD_RET_FAILURE;
	}
	ret = fame_dsi_program_link_clocks();
	if (ret) {
		fame_dsi_dump_state("after failed DSI link clock enable");
		return CMD_RET_FAILURE;
	}
	fame_dsi_host_setup();
	fame_dsi_dump_state("after DSI host/PHY setup");

	ret = fame_panel_teisko_prepare();
	fame_dsi_dump_state("after Teisko DCS prepare");
	if (ret) {
		printf("aborting before video enable; Teisko DCS prepare failed: %d\n",
		       ret);
		return CMD_RET_FAILURE;
	}

	fame_mdp_dump_teisko_mdp4_state("before final MDP4/DSI video enable");
	ret = fame_mdp_program_teisko_mdp4();
	if (ret) {
		printf("aborting before video enable; MDP4 setup failed: %d\n",
		       ret);
		return CMD_RET_FAILURE;
	}
	fame_mdp_dump_teisko_mdp4_state("after final MDP4 setup before DSI video");

	ret = fame_panel_teisko_enable();
	fame_dsi_dump_state("after Teisko DCS enable before DSI video");
	if (ret) {
		printf("Teisko DCS enable failed before video enable: %d\n", ret);
		return CMD_RET_FAILURE;
	}

	fame_dsi_write(DSI_CTRL_REG, DSI_CTRL_VIDEO);
	fame_mdp_dump_teisko_mdp4_state("after final DSI video enable");
	fame_iommu_dump_state_if_safe("final MDP IOMMU state", false);
	fame_dsi_dump_state("final DSI state");

	return CMD_RET_SUCCESS;
}

static int do_fame_mdp_dump(struct cmd_tbl *cmdtp, int flag, int argc,
			    char *const argv[])
{
	bool force_iommu = false;

	if (argc > 2)
		return CMD_RET_USAGE;

	if (argc == 2) {
		if (strcmp(argv[1], "all"))
			return CMD_RET_USAGE;
		force_iommu = true;
	}

	if (!of_machine_is_compatible("nokia,fame"))
		printf("warning: fame_mdp dump is intended for nokia,fame\n");

	printf("fame_mdp dump is read-only; no clocks, resets, RPM, GPIOs, or display registers are changed\n");
	fame_cpu_dump_state("CPU/MMU baseline");
	fame_mdp_dump_state("MDP/MMCC baseline");
	fame_dsi_dump_state("DSI baseline");
	fame_mdp_dump_teisko_mdp4_state("MDP4/DSI baseline");
	fame_mdp_dump_pipe_state("MDP4 pipe baseline");
	fame_pm8038_dump_display_rails("RPM display rail baseline");
	fame_tlmm_dump_state("TLMM panel reset baseline");
	fame_iommu_dump_state_if_safe("MDP IOMMU baseline", force_iommu);

	return CMD_RET_SUCCESS;
}

static struct cmd_tbl fame_mdp_sub[] = {
	U_BOOT_CMD_MKENT(dump, 2, 1, do_fame_mdp_dump, "", ""),
	U_BOOT_CMD_MKENT(mode, 1, 1, do_fame_mdp_mode, "", ""),
	U_BOOT_CMD_MKENT(panel, 1, 1, do_fame_mdp_panel, "", ""),
	U_BOOT_CMD_MKENT(version, 2, 1, do_fame_mdp_version, "", ""),
};

static int do_fame_mdp(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	struct cmd_tbl *cmd;

	if (argc < 2)
		return CMD_RET_USAGE;

	argc--;
	argv++;

	cmd = find_cmd_tbl(argv[0], fame_mdp_sub, ARRAY_SIZE(fame_mdp_sub));
	if (!cmd)
		return CMD_RET_USAGE;

	return cmd->cmd(cmdtp, flag, argc, argv);
}

U_BOOT_LONGHELP(fame_mdp,
	"dump [all]      - read-only dump of MDP/MMCC/DSI/RPM/GPIO state;\n"
	"                  `all` forces MDP IOMMU MMIO reads even if the\n"
	"                  SMMU AHB clock does not look active\n"
	"mode            - program MDP4 DSI-side timing for the Teisko panel\n"
	"                  after sequencing MSM8227 MDP clocks/GFS\n"
	"panel           - sequence MDP, DSI host/PHY, reset GPIO, Teisko DCS,\n"
	"                  and enable DSI video mode for bring-up diagnostics\n"
	"version [count] - sequence MSM8227 MDP clocks/GFS and read VERSION\n"
	"                  with mdp_clk sourced from PLL2 at 200 MHz\n");

U_BOOT_CMD(fame_mdp, 3, 1, do_fame_mdp,
	   "Nokia Fame MDP4 diagnostics", fame_mdp_help_text);
