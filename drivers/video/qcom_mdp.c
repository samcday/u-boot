// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm Mobile Display Processor driver
 * This driver is very minimal, it only supports discovering an MDP5 pipe that
 * was already configured by the previous bootloader.
 *
 * This driver was adapted from the work by Stephan Gerhold + Nikita Travkin
 * in the lk2nd project.
 *
 * Future work:
 *  * Support for the DMA0 + VIG0 pipes
 *
 * (C) Copyright 2025 Samuel Day
 */

#include <dm.h>
#include <log.h>
#include <video.h>
#include <dm/device_compat.h>
#include <asm/io.h>

#define CTL0_BASE 0x1000
#define RGB0_BASE 0x14000

#define CTL_FLUSH 0x18

#define PIPE_SRC_IMG_SIZE 	0x4
#define PIPE_SRC0_ADDR 		0x14
#define PIPE_SRC_YSTRIDE 	0x24
#define PIPE_SRC_FORMAT 	0x30
#define PIPE_SRC_UNPACK_PATTERN 0x34

#define SRC_FORMAT_SRC_RGB565 (BIT(9) | BIT(4) | BIT(2) | BIT(1))
#define SRC_FORMAT_UNPACK_RGB (BIT(17) | BIT(13))

static int qcom_mdp_probe(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	phys_addr_t base = 0;
	phys_addr_t rgb0_addr;
	u32 img_size, src_format, width, height, bpp;

	base = dev_read_addr(dev);
	if (base == FDT_ADDR_T_NONE) {
		dev_err(dev, "failed to lookup regs\n");
		return -ENODATA;
	}

	rgb0_addr = readl(base + RGB0_BASE + PIPE_SRC0_ADDR);
	if (!rgb0_addr) {
		dev_info(dev, "RGB0 pipe is not active\n");
		return 0;
	}

	img_size = readl(base + RGB0_BASE + PIPE_SRC_IMG_SIZE);
	width = img_size & 0xFFFF;
	height = img_size >> 16;

	src_format = readl(base + RGB0_BASE + PIPE_SRC_FORMAT);
	bpp = ((src_format & 0x600) >> 9) + 1;

	dev_dbg(dev, "RGB0 pipe is active @0x%llx (%dx%d bpp %d)\n",
		rgb0_addr, width, height, bpp);

	if (bpp == 4) {
		uc_priv->bpix = VIDEO_BPP32;
		uc_priv->format = VIDEO_X8R8G8B8;
	} else if (bpp == 3) {
		/* U-Boot video uclass doesn't support 24bpp, so step down to
		 * 16bit, this way we don't need to re-allocate the buffer */
		dev_warn(dev, "unsupported 24bpp plane, switching to 16bpp\n");

		/* Clear the new 16bpp framebuffer region, otherwise the
		 * previous 24bpp contents will get scanned out and look
		 * decidedly ugly. A potential improvement here would be to
		 * convert the image */
		memset((void *)rgb0_addr, 0, width * height * 2);

		src_format = SRC_FORMAT_SRC_RGB565 | SRC_FORMAT_UNPACK_RGB;
		writel(src_format, base + RGB0_BASE + PIPE_SRC_FORMAT);
		writel(width * 2, base + RGB0_BASE + PIPE_SRC_YSTRIDE);
		writel(BIT(17) | 1, base + RGB0_BASE + PIPE_SRC_UNPACK_PATTERN);
		writel(BIT(3) /* RGB_0 */, base + CTL0_BASE + CTL_FLUSH);

		uc_priv->bpix = VIDEO_BPP16;
	} else if (bpp == 2)
		uc_priv->bpix = VIDEO_BPP16;
	else
		uc_priv->bpix = VIDEO_BPP8;

	video_set_flush_dcache(dev, true);
	plat->base = rgb0_addr;
	plat->size = width * height * bpp;
	uc_priv->xsize = width;
	uc_priv->ysize = height;

	return 0;
}

static const struct udevice_id qcom_mdp_ids[] = {
	{ .compatible = "qcom,mdp5" },
	{ }
};

U_BOOT_DRIVER(qcom_mdp) = {
	.name	= "qcom_mdp",
	.id	= UCLASS_VIDEO,
	.of_match = qcom_mdp_ids,
	.probe	= qcom_mdp_probe,
};

static const struct udevice_id qcom_mdss_ids[] = {
	{ .compatible = "qcom,mdss" },
	{ }
};

U_BOOT_DRIVER(qcom_mdss) = {
	.name	= "qcom_mdss",
	.id	= UCLASS_NOP,
	.of_match = qcom_mdss_ids,
	.bind	= dm_scan_fdt_dev,
};
