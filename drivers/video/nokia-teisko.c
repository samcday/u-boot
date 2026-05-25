// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal Nokia Teisko DSI panel support for Lumia 520/Fame.
 *
 * The ordering mirrors the known-good raw-APPSBL bring-up path and the
 * matching Linux panel split: reset and prepare DCS before video, then send
 * display-on after MDP4 has started feeding the DSI interface.
 */

#define LOG_CATEGORY UCLASS_PANEL

#include <dm.h>
#include <dm/device_compat.h>
#include <errno.h>
#include <mipi_dsi.h>
#include <panel.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#include "qcom_msm8960_display.h"

#define FAME_TLMM_BASE				0x00800000
#define FAME_GPIO_CFG(gpio)			(FAME_TLMM_BASE + 0x1000 + \
						 (gpio) * 0x10)
#define FAME_GPIO_INOUT(gpio)			(FAME_GPIO_CFG(gpio) + 4)
#define FAME_TEISKO_RESET_GPIO			58
#define FAME_TEISKO_RESET_GPIO_CFG_VENDOR	0x000003c1
#define FAME_TEISKO_RESET_OUTPUT_HIGH		BIT(1)

#define TEISKO_PIXEL_CLOCK_HZ			28654000
#define TEISKO_DEFAULT_BRIGHTNESS		0x80
#define TEISKO_CONTROL_DISPLAY_ON		0x24

struct nokia_teisko_priv {
	bool prepared;
	bool enabled;
};

static void teisko_timing_entry(struct timing_entry *entry, u32 value)
{
	entry->min = value;
	entry->typ = value;
	entry->max = value;
}

static void teisko_gpio_set(bool high)
{
	writel(FAME_TEISKO_RESET_GPIO_CFG_VENDOR,
	       FAME_GPIO_CFG(FAME_TEISKO_RESET_GPIO));
	readl(FAME_GPIO_CFG(FAME_TEISKO_RESET_GPIO));

	writel(high ? FAME_TEISKO_RESET_OUTPUT_HIGH : 0,
	       FAME_GPIO_INOUT(FAME_TEISKO_RESET_GPIO));
	readl(FAME_GPIO_INOUT(FAME_TEISKO_RESET_GPIO));
}

static void teisko_reset(void)
{
	teisko_gpio_set(true);
	mdelay(2);
	teisko_gpio_set(false);
	mdelay(2);
	teisko_gpio_set(true);
	mdelay(20);
}

static struct mipi_dsi_device *teisko_dsi_device(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);

	if (!plat->device)
		return NULL;

	return plat->device;
}

static int teisko_dsi_write(struct udevice *dev, const char *name, ssize_t ret)
{
	if (ret < 0) {
		dev_err(dev, "%s failed: %zd\n", name, ret);
		return ret;
	}

	dev_dbg(dev, "%s ok: %zd\n", name, ret);

	return 0;
}

static int teisko_write_control_display(struct udevice *dev,
					struct mipi_dsi_device *dsi, u8 val)
{
	return teisko_dsi_write(dev, "dcs write control display",
				mipi_dsi_dcs_write(dsi,
						   MIPI_DCS_WRITE_CONTROL_DISPLAY,
						   &val, sizeof(val)));
}

static int teisko_write_brightness(struct udevice *dev,
				   struct mipi_dsi_device *dsi,
				   u8 brightness)
{
	int ret;

	ret = teisko_dsi_write(dev, "dcs set display brightness",
			       mipi_dsi_dcs_write(dsi,
						  MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
						  &brightness,
						  sizeof(brightness)));
	if (ret)
		return ret;

	return teisko_write_control_display(dev, dsi,
					    brightness ? TEISKO_CONTROL_DISPLAY_ON :
					    0);
}

int nokia_teisko_panel_prepare(struct udevice *dev)
{
	static const u8 vendor_cmd[] = { 0xff, 0x78 };
	static const u8 address_mode[] = { 0x00 };
	struct nokia_teisko_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_device *dsi = teisko_dsi_device(dev);
	int ret;

	if (priv->prepared)
		return 0;
	if (!dsi)
		return -ENODEV;

	teisko_reset();

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		return ret;

	ret = teisko_dsi_write(dev, "dcs exit sleep mode",
			       mipi_dsi_dcs_exit_sleep_mode(dsi));
	if (ret)
		return ret;
	mdelay(120);

	ret = teisko_dsi_write(dev, "generic vendor command ff 78",
			       mipi_dsi_generic_write(dsi, vendor_cmd,
						      sizeof(vendor_cmd)));
	if (ret)
		return ret;

	ret = teisko_dsi_write(dev, "dcs set address mode",
			       mipi_dsi_dcs_write(dsi,
						  MIPI_DCS_SET_ADDRESS_MODE,
						  address_mode,
						  sizeof(address_mode)));
	if (ret)
		return ret;

	ret = teisko_write_control_display(dev, dsi,
					   TEISKO_CONTROL_DISPLAY_ON);
	if (ret)
		return ret;

	ret = teisko_write_brightness(dev, dsi, TEISKO_DEFAULT_BRIGHTNESS);
	if (ret)
		return ret;

	priv->prepared = true;

	return 0;
}

int nokia_teisko_panel_enable(struct udevice *dev)
{
	struct nokia_teisko_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_device *dsi = teisko_dsi_device(dev);
	int ret;

	if (priv->enabled)
		return 0;
	if (!dsi)
		return -ENODEV;

	ret = teisko_dsi_write(dev, "dcs set display on",
			       mipi_dsi_dcs_set_display_on(dsi));
	if (ret)
		return ret;

	mdelay(20);
	priv->enabled = true;

	return 0;
}

static int nokia_teisko_enable_backlight(struct udevice *dev)
{
	int ret;

	ret = nokia_teisko_panel_prepare(dev);
	if (ret)
		return ret;

	return nokia_teisko_panel_enable(dev);
}

static int nokia_teisko_get_display_timing(struct udevice *dev,
					   struct display_timing *timing)
{
	teisko_timing_entry(&timing->pixelclock, TEISKO_PIXEL_CLOCK_HZ);
	teisko_timing_entry(&timing->hactive, QCOM_MSM8960_TEISKO_HDISPLAY);
	teisko_timing_entry(&timing->hfront_porch, 45);
	teisko_timing_entry(&timing->hback_porch, 44);
	teisko_timing_entry(&timing->hsync_len, 4);
	teisko_timing_entry(&timing->vactive, QCOM_MSM8960_TEISKO_VDISPLAY);
	teisko_timing_entry(&timing->vfront_porch, 14);
	teisko_timing_entry(&timing->vback_porch, 14);
	teisko_timing_entry(&timing->vsync_len, 1);
	timing->flags = 0;
	timing->hdmi_monitor = false;

	return 0;
}

static int nokia_teisko_probe(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);

	plat->lanes = 2;
	plat->format = MIPI_DSI_FMT_RGB888;
	plat->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_HSE;

	return 0;
}

static const struct panel_ops nokia_teisko_ops = {
	.enable_backlight = nokia_teisko_enable_backlight,
	.get_display_timing = nokia_teisko_get_display_timing,
};

static const struct udevice_id nokia_teisko_ids[] = {
	{ .compatible = "nokia,teisko" },
	{ }
};

U_BOOT_DRIVER(nokia_teisko) = {
	.name		= "nokia_teisko",
	.id		= UCLASS_PANEL,
	.of_match	= nokia_teisko_ids,
	.probe		= nokia_teisko_probe,
	.ops		= &nokia_teisko_ops,
	.priv_auto	= sizeof(struct nokia_teisko_priv),
	.plat_auto	= sizeof(struct mipi_dsi_panel_plat),
};
