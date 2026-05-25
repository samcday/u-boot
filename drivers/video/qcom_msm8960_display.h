/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Shared private bits for the MSM8960-family Fame display bring-up drivers.
 */

#ifndef __QCOM_MSM8960_DISPLAY_H
#define __QCOM_MSM8960_DISPLAY_H

#include <mipi_dsi.h>

#define QCOM_MSM8960_TEISKO_HDISPLAY	480
#define QCOM_MSM8960_TEISKO_VDISPLAY	800
#define QCOM_MSM8960_TEISKO_FB_BPP	4
#define QCOM_MSM8960_TEISKO_FB_STRIDE \
	(QCOM_MSM8960_TEISKO_HDISPLAY * QCOM_MSM8960_TEISKO_FB_BPP)
#define QCOM_MSM8960_TEISKO_FB_SIZE \
	(QCOM_MSM8960_TEISKO_FB_STRIDE * QCOM_MSM8960_TEISKO_VDISPLAY)
#define QCOM_MSM8960_TEISKO_FB_BASE	0x80400000

struct udevice;

struct mipi_dsi_device *qcom_msm8960_dsi_device(struct udevice *dev);
int qcom_msm8960_dsi_prepare(struct udevice *dev);
int qcom_msm8960_dsi_enable_video(struct udevice *dev);

int nokia_teisko_panel_prepare(struct udevice *dev);
int nokia_teisko_panel_enable(struct udevice *dev);

#endif
