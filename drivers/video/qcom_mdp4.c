// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal Qualcomm MDP4 video path for Lumia 520/Fame.
 *
 * This is deliberately Fame-specific for now. It ports the working diagnostic
 * sequence into a video driver shell while keeping hardware start disabled by
 * default so the raw fame_mdp command remains the golden fallback.
 */

#define LOG_CATEGORY UCLASS_VIDEO

#include <clk.h>
#include <cpu_func.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <dm/device_compat.h>
#include <dm/lists.h>
#include <dm/ofnode_graph.h>
#include <dm/read.h>
#include <dm/uclass.h>
#include <errno.h>
#include <mipi_dsi.h>
#include <panel.h>
#include <power-domain.h>
#include <video.h>
#include <asm/io.h>
#include <asm/memory.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/sizes.h>

#include "qcom_msm8960_display.h"

#define FAME_MMCC_BASE			0x04000000
#define MMCC_REG(off)			(FAME_MMCC_BASE + (off))

#define MDP_CC_REG			0x00c0
#define MDP_MD0_REG			0x00c4
#define MDP_MD1_REG			0x00c8
#define MDP_NS_REG			0x00d0

#define MDP_VERSION_REG			0x00000
#define MDP_EXPECTED_VERSION		0x04030705
#define MDP_DIAG_RATE			200000000

#define MDP_CS_CONTROLLER0_REG		0x000c0
#define MDP_CS_CONTROLLER1_REG		0x000c4
#define MDP_DISP_INTF_SEL_REG		0x00038
#define MDP_READ_CNFG_REG		0x0004c
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

#define TEISKO_HDISPLAY		QCOM_MSM8960_TEISKO_HDISPLAY
#define TEISKO_HSYNC_START	525
#define TEISKO_HSYNC_END	529
#define TEISKO_HTOTAL		573
#define TEISKO_VDISPLAY		QCOM_MSM8960_TEISKO_VDISPLAY
#define TEISKO_VSYNC_START	814
#define TEISKO_VSYNC_END	815
#define TEISKO_VTOTAL		829

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
#define FAME_MDP_CC_BANK1_PROG		(FAME_MDP_CC_BRANCH_EN | \
					 FAME_MDP_CC_ROOT_EN | \
					 FAME_MDP_CC_BANK1_MND_EN | \
					 FAME_MDP_CC_BANK1_MODE_DUAL)
#define FAME_MDP_NS_BANK1_RST		BIT(30)

#define MDP4_DMA_DSI_CONFIG		0x0400213f
#define MDP4_DSI_INTF_SEL_MASK		0x000000cb
#define MDP4_DSI_INTF_SEL		0x00000041
#define MDP4_RGB1_BASE_STAGE		BIT(8)
#define MDP4_DMA_P_FETCH_CONFIG	0x43
#define MDP4_FETCH_CONFIG		0x47
#define MDP4_PIPE_SRC_FORMAT_XRGB8888	0x000267ff
#define MDP4_PIPE_SRC_UNPACK_XRGB8888	0x03020001
#define MDP4_RGB1_OP_MODE_FETCH	0x00000010
#define MDP4_PHASE_STEP_DEFAULT	0x20000000

struct qcom_mdp4_priv {
	void __iomem *base;
	struct udevice *dsi;
	struct udevice *panel;
};

static u32 mdp_wh(u32 width, u32 height)
{
	return width | (height << 16);
}

static u32 mdp_pair(u32 low, u32 high)
{
	return low | (high << 16);
}

static void qcom_mdp4_write(struct qcom_mdp4_priv *priv, u32 reg, u32 val)
{
	writel(val, priv->base + reg);
	readl(priv->base + reg);
}

static void qcom_mmcc_write(u32 reg, u32 val)
{
	writel(val, MMCC_REG(reg));
	readl(MMCC_REG(reg));
}

static void qcom_mdp4_force_vendor_src(struct udevice *dev)
{
	u32 cc;

	qcom_mmcc_write(MDP_NS_REG, FAME_MDP_NS_VENDOR |
			FAME_MDP_NS_BANK1_RST);
	qcom_mmcc_write(MDP_MD0_REG, FAME_MDP_MD0_VENDOR);
	qcom_mmcc_write(MDP_MD1_REG, FAME_MDP_MD1_VENDOR);

	cc = readl(MMCC_REG(MDP_CC_REG));
	cc &= ~(FAME_MDP_CC_BANK1_MND_EN | FAME_MDP_CC_BANK1_MODE_MASK);
	cc |= FAME_MDP_CC_BANK1_PROG;
	qcom_mmcc_write(MDP_CC_REG, cc);

	qcom_mmcc_write(MDP_NS_REG, FAME_MDP_NS_VENDOR);

	cc = readl(MMCC_REG(MDP_CC_REG));
	cc &= ~FAME_MDP_CC_LOW_MASK;
	cc |= FAME_MDP_CC_VENDOR & FAME_MDP_CC_LOW_MASK;
	qcom_mmcc_write(MDP_CC_REG, cc);

	dev_dbg(dev, "vendor MDP source cc=%08x md0=%08x md1=%08x ns=%08x\n",
		readl(MMCC_REG(MDP_CC_REG)), readl(MMCC_REG(MDP_MD0_REG)),
		readl(MMCC_REG(MDP_MD1_REG)), readl(MMCC_REG(MDP_NS_REG)));
}

static int qcom_mdp4_enable_resources(struct udevice *dev)
{
	struct power_domain pd;
	struct clk core, iface, bus, lut;
	ulong rate;
	int ret;

	ret = clk_get_by_name(dev, "core_clk", &core);
	if (ret)
		return ret;
	ret = clk_get_by_name(dev, "iface_clk", &iface);
	if (ret)
		return ret;
	ret = clk_get_by_name(dev, "bus_clk", &bus);
	if (ret)
		return ret;
	ret = clk_get_by_name(dev, "lut_clk", &lut);
	if (ret)
		return ret;

	rate = clk_set_rate(&core, MDP_DIAG_RATE);
	if (rate != MDP_DIAG_RATE) {
		dev_err(dev, "failed to set MDP clock to %u Hz: got %lu\n",
			MDP_DIAG_RATE, rate);
		return -EINVAL;
	}

	ret = clk_enable(&bus);
	if (ret)
		return ret;
	ret = clk_enable(&iface);
	if (ret)
		return ret;

	ret = power_domain_get(dev, &pd);
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

	qcom_mdp4_force_vendor_src(dev);

	return 0;
}

static void qcom_mdp4_program_rgb1(struct qcom_mdp4_priv *priv,
				   phys_addr_t fb_phys)
{
	qcom_mdp4_write(priv, MDP_RGB1_SRC_SIZE_REG,
			mdp_wh(TEISKO_HDISPLAY, TEISKO_VDISPLAY));
	qcom_mdp4_write(priv, MDP_RGB1_SRC_XY_REG, 0);
	qcom_mdp4_write(priv, MDP_RGB1_DST_SIZE_REG,
			mdp_wh(TEISKO_HDISPLAY, TEISKO_VDISPLAY));
	qcom_mdp4_write(priv, MDP_RGB1_DST_XY_REG, 0);
	qcom_mdp4_write(priv, MDP_RGB1_SRCP0_BASE_REG, (u32)fb_phys);
	qcom_mdp4_write(priv, MDP_RGB1_SRCP1_BASE_REG, 0);
	qcom_mdp4_write(priv, MDP_RGB1_SRCP2_BASE_REG, 0);
	qcom_mdp4_write(priv, MDP_RGB1_SRCP3_BASE_REG, 0);
	qcom_mdp4_write(priv, MDP_RGB1_SRC_STRIDE_A_REG,
			QCOM_MSM8960_TEISKO_FB_STRIDE);
	qcom_mdp4_write(priv, MDP_RGB1_SRC_STRIDE_B_REG, 0);
	qcom_mdp4_write(priv, MDP_RGB1_SRC_FORMAT_REG,
			MDP4_PIPE_SRC_FORMAT_XRGB8888);
	qcom_mdp4_write(priv, MDP_RGB1_SRC_UNPACK_REG,
			MDP4_PIPE_SRC_UNPACK_XRGB8888);
	qcom_mdp4_write(priv, MDP_RGB1_SOLID_COLOR_REG, 0);
	qcom_mdp4_write(priv, MDP_RGB1_OP_MODE_REG, MDP4_RGB1_OP_MODE_FETCH);
	qcom_mdp4_write(priv, MDP_RGB1_PHASEX_STEP_REG,
			MDP4_PHASE_STEP_DEFAULT);
	qcom_mdp4_write(priv, MDP_RGB1_PHASEY_STEP_REG,
			MDP4_PHASE_STEP_DEFAULT);
}

static void qcom_mdp4_program_teisko(struct qcom_mdp4_priv *priv,
				     phys_addr_t fb_phys)
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

	qcom_mdp4_write(priv, MDP_CS_CONTROLLER0_REG, 0);
	qcom_mdp4_write(priv, MDP_CS_CONTROLLER1_REG, 0);
	qcom_mdp4_write(priv, MDP_PORTMAP_MODE_REG, 0);
	qcom_mdp4_write(priv, MDP_READ_CNFG_REG, 0x00003333);

	qcom_mdp4_write(priv, MDP_DMA_P_FETCH_CONFIG_REG,
			MDP4_DMA_P_FETCH_CONFIG);
	qcom_mdp4_write(priv, MDP_DMA_E_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);
	qcom_mdp4_write(priv, MDP_VG1_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);
	qcom_mdp4_write(priv, MDP_VG2_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);
	qcom_mdp4_write(priv, MDP_RGB1_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);
	qcom_mdp4_write(priv, MDP_RGB2_FETCH_CONFIG_REG, MDP4_FETCH_CONFIG);

	qcom_mdp4_program_rgb1(priv, fb_phys);

	qcom_mdp4_write(priv, MDP_LAYERMIXER_UPDATE_REG, 1);
	qcom_mdp4_write(priv, MDP_LAYERMIXER_IN_CFG_REG,
			MDP4_RGB1_BASE_STAGE);
	qcom_mdp4_write(priv, MDP_VG1_OP_MODE_REG, 0);
	qcom_mdp4_write(priv, MDP_VG2_OP_MODE_REG, 0);
	qcom_mdp4_write(priv, MDP_DMA_P_OP_MODE_REG, 0);
	qcom_mdp4_write(priv, MDP_DMA_S_OP_MODE_REG, 0);

	qcom_mdp4_write(priv, MDP_DMA_P_SRC_SIZE_REG,
			mdp_wh(TEISKO_HDISPLAY, TEISKO_VDISPLAY));
	qcom_mdp4_write(priv, MDP_DMA_P_SRC_BASE_REG, 0);
	qcom_mdp4_write(priv, MDP_DMA_P_SRC_STRIDE_REG, 0);
	qcom_mdp4_write(priv, MDP_DMA_P_DST_SIZE_REG, 0);

	qcom_mdp4_write(priv, MDP_OVLP0_BASE_REG, 0);
	qcom_mdp4_write(priv, MDP_OVLP0_SIZE_REG,
			mdp_wh(TEISKO_HDISPLAY, TEISKO_VDISPLAY));
	qcom_mdp4_write(priv, MDP_OVLP0_STRIDE_REG, 0);
	qcom_mdp4_write(priv, MDP_OVLP0_TRANSP_LOW0_REG, 0);
	qcom_mdp4_write(priv, MDP_OVLP0_TRANSP_LOW1_REG, 0);
	qcom_mdp4_write(priv, MDP_OVLP0_TRANSP_HIGH0_REG, 0);
	qcom_mdp4_write(priv, MDP_OVLP0_TRANSP_HIGH1_REG, 0);
	qcom_mdp4_write(priv, MDP_OVLP0_CFG_REG, 3);

	qcom_mdp4_write(priv, MDP_DSI_ENABLE_REG, 0);
	qcom_mdp4_write(priv, MDP_DSI_HSYNC_CTRL_REG,
			mdp_pair(TEISKO_HSYNC_END - TEISKO_HSYNC_START,
				 TEISKO_HTOTAL));
	qcom_mdp4_write(priv, MDP_DSI_VSYNC_PERIOD_REG, vsync_period);
	qcom_mdp4_write(priv, MDP_DSI_VSYNC_LEN_REG, vsync_len);
	qcom_mdp4_write(priv, MDP_DSI_DISPLAY_HCTRL_REG,
			mdp_pair(hsync_start_x, hsync_end_x));
	qcom_mdp4_write(priv, MDP_DSI_DISPLAY_VSTART_REG, display_v_start);
	qcom_mdp4_write(priv, MDP_DSI_DISPLAY_VEND_REG, display_v_end);
	qcom_mdp4_write(priv, MDP_DSI_CTRL_POLARITY_REG, 0);
	qcom_mdp4_write(priv, MDP_DSI_UNDERFLOW_CLR_REG, 0);
	qcom_mdp4_write(priv, MDP_DSI_ACTIVE_HCTL_REG, 0);
	qcom_mdp4_write(priv, MDP_DSI_HSYNC_SKEW_REG, 0);
	qcom_mdp4_write(priv, MDP_DSI_BORDER_CLR_REG, 0);
	qcom_mdp4_write(priv, MDP_DSI_ACTIVE_VSTART_REG, 0);
	qcom_mdp4_write(priv, MDP_DSI_ACTIVE_VEND_REG, 0);

	qcom_mdp4_write(priv, MDP_DMA_P_CONFIG_REG, MDP4_DMA_DSI_CONFIG);

	intf_sel = readl(priv->base + MDP_DISP_INTF_SEL_REG);
	intf_sel &= ~MDP4_DSI_INTF_SEL_MASK;
	intf_sel |= MDP4_DSI_INTF_SEL;
	qcom_mdp4_write(priv, MDP_DISP_INTF_SEL_REG, intf_sel);

	qcom_mdp4_write(priv, MDP_DSI_ENABLE_REG, 1);
	qcom_mdp4_write(priv, MDP_INTR_CLEAR_REG, 0xffffffff);
	qcom_mdp4_write(priv, MDP_OVERLAY_FLUSH_REG, BIT(0) | BIT(4));
}

static int qcom_mdp4_connect_panel(struct udevice *dev)
{
	struct qcom_mdp4_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_panel_plat *mplat;
	struct mipi_dsi_device *dsi;
	struct udevice *parent;
	ofnode dsi_node;
	int ret;

	ret = uclass_get_device(UCLASS_DSI_HOST, 0, &priv->dsi);
	if (ret == -ENODEV) {
		dsi_node = ofnode_graph_get_remote_node(dev_ofnode(dev), 1, -1);
		if (!ofnode_valid(dsi_node)) {
			dev_err(dev, "failed to find DSI host graph node\n");
			return ret;
		}

		ret = device_get_global_by_ofnode(dsi_node, &priv->dsi);
		if (ret == -ENOENT) {
			ret = device_find_global_by_ofnode(ofnode_get_parent(dsi_node),
							   &parent);
			if (ret) {
				dev_err(dev, "failed to find DSI host parent: %d\n",
					ret);
				return ret;
			}

			ret = lists_bind_fdt(parent, dsi_node, &priv->dsi, NULL,
					     false);
			if (ret) {
				dev_err(dev, "failed to bind DSI host: %d\n", ret);
				return ret;
			}
			if (!priv->dsi) {
				dev_err(dev, "DSI host node had no matching driver\n");
				return -ENODEV;
			}

			ret = device_probe(priv->dsi);
		}
		if (ret) {
			dev_err(dev, "failed to probe DSI host from graph: %d\n",
				ret);
			return ret;
		}
	}
	if (ret) {
		dev_err(dev, "failed to get DSI host: %d\n", ret);
		return ret;
	}

	ret = device_find_first_child_by_uclass(priv->dsi, UCLASS_PANEL,
						&priv->panel);
	if (ret) {
		dev_err(dev, "failed to find DSI panel child: %d\n", ret);
		return ret;
	}

	ret = device_probe(priv->panel);
	if (ret) {
		dev_err(dev, "failed to probe DSI panel child: %d\n", ret);
		return ret;
	}

	mplat = dev_get_plat(priv->panel);
	dsi = qcom_msm8960_dsi_device(priv->dsi);
	dsi->dev = priv->panel;
	dsi->lanes = mplat->lanes;
	dsi->format = mplat->format;
	dsi->mode_flags = mplat->mode_flags;
	mplat->device = dsi;

	return 0;
}

static int qcom_mdp4_autostart(struct udevice *dev)
{
	struct qcom_mdp4_priv *priv = dev_get_priv(dev);
	int ret;

	ret = qcom_mdp4_enable_resources(dev);
	if (ret) {
		dev_err(dev, "failed to enable MDP4 resources: %d\n", ret);
		return ret;
	}

	if (readl(priv->base + MDP_VERSION_REG) != MDP_EXPECTED_VERSION) {
		dev_err(dev, "unexpected MDP version: %08x\n",
			readl(priv->base + MDP_VERSION_REG));
		return -ENODEV;
	}

	ret = qcom_mdp4_connect_panel(dev);
	if (ret) {
		dev_err(dev, "failed to connect DSI panel: %d\n", ret);
		return ret;
	}

	ret = qcom_msm8960_dsi_prepare(priv->dsi);
	if (ret) {
		dev_err(dev, "failed to prepare DSI host: %d\n", ret);
		return ret;
	}

	ret = nokia_teisko_panel_prepare(priv->panel);
	if (ret) {
		dev_err(dev, "failed to prepare Teisko panel: %d\n", ret);
		return ret;
	}

	qcom_mdp4_program_teisko(priv, QCOM_MSM8960_TEISKO_FB_BASE);

	ret = nokia_teisko_panel_enable(priv->panel);
	if (ret) {
		dev_err(dev, "failed to enable Teisko panel: %d\n", ret);
		return ret;
	}

	return qcom_msm8960_dsi_enable_video(priv->dsi);
}

static int qcom_mdp4_bind(struct udevice *dev)
{
	struct video_uc_plat *uc_plat = dev_get_uclass_plat(dev);

	uc_plat->base = QCOM_MSM8960_TEISKO_FB_BASE;
	uc_plat->size = QCOM_MSM8960_TEISKO_FB_SIZE;
	uc_plat->align = SZ_4K;

	return 0;
}

static int qcom_mdp4_probe(struct udevice *dev)
{
	struct qcom_mdp4_priv *priv = dev_get_priv(dev);
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -EINVAL;

	uc_priv->xsize = QCOM_MSM8960_TEISKO_HDISPLAY;
	uc_priv->ysize = QCOM_MSM8960_TEISKO_VDISPLAY;
	uc_priv->bpix = VIDEO_BPP32;
	uc_priv->format = VIDEO_X8R8G8B8;
	uc_priv->line_length = QCOM_MSM8960_TEISKO_FB_STRIDE;
	video_set_flush_dcache(dev, true);

	if (!IS_ENABLED(CONFIG_VIDEO_QCOM_MDP4_FAME_AUTOSTART)) {
		dev_dbg(dev, "Fame MDP4 autostart disabled\n");
		return 0;
	}

	return qcom_mdp4_autostart(dev);
}

static const struct udevice_id qcom_mdp4_ids[] = {
	{ .compatible = "qcom,mdp4" },
	{ }
};

U_BOOT_DRIVER(qcom_mdp4) = {
	.name		= "qcom_mdp4",
	.id		= UCLASS_VIDEO,
	.of_match	= qcom_mdp4_ids,
	.bind		= qcom_mdp4_bind,
	.probe		= qcom_mdp4_probe,
	.priv_auto	= sizeof(struct qcom_mdp4_priv),
};
