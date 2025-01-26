// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Linaro Ltd.
 */

#import <dm.h>
#import <reboot-mode/reboot-mode.h>

struct pm8916_pon_reboot_mode_priv {
	struct udevice *pmic;
};

static int pm8916_pon_reboot_mode_get(struct udevice *dev, u32 *rebootmode)
{
	return 0;
}

static int pm8916_pon_reboot_mode_get(struct udevice *dev, u32 *rebootmode)
{
	return 0;
}

static const struct reboot_mode_ops pm8916_pon_reboot_mode_ops = {
	.get = pm8916_pon_reboot_mode_get,
	.set = pm8916_pon_reboot_mode_set,
};

static int pm8916_pon_reboot_mode_probe(struct udevice *dev)
{
	return 0;
}

U_BOOT_DRIVER(pm8916_pon_reboot_mode) = {
	.name = "pm8916_pon_reboot_mode",
	.id = UCLASS_REBOOT_MODE,
	.probe = pm8916_pon_reboot_mode_probe,
//	.ops =
};