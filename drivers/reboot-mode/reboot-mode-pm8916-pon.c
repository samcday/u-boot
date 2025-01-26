// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Linaro Ltd.
 */

#include <dm.h>
#include <power/pmic.h>
#include <reboot-mode/reboot-mode.h>
#include <pm8916_pon.h>
#include <dm/device_compat.h>

#define PON_SOFT_RB_SPARE 0x8f

#define GEN1_REASON_SHIFT 2

static int pm8916_pon_reboot_mode_get(struct udevice *dev, u32 *rebootmode)
{
	struct pm8916_pon_priv *priv = dev_get_priv(dev->parent);
	int reg;
	uint mask = GENMASK(7, GEN1_REASON_SHIFT);

	reg = pmic_reg_read(priv->pmic, priv->base + PON_SOFT_RB_SPARE);
	if (reg < 0) {
		dev_warn(dev, "Failed to read PON_SOFT_RB_SPARE: %d\n", reg);
		return reg;
	}

	*rebootmode = (reg & mask) >> GEN1_REASON_SHIFT;

	return 0;
}

static int pm8916_pon_reboot_mode_set(struct udevice *dev, u32 rebootmode)
{
	struct pm8916_pon_priv *priv = dev_get_priv(dev->parent);
	uint mask = GENMASK(7, GEN1_REASON_SHIFT);
	int ret;

	ret = pmic_clrsetbits(priv->pmic, priv->base + PON_SOFT_RB_SPARE, mask,
			rebootmode << GEN1_REASON_SHIFT);
	if (ret) {
		dev_warn(dev, "Failed to write PON_SOFT_RB_SPARE: %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct reboot_mode_ops pm8916_pon_reboot_mode_ops = {
	.get = pm8916_pon_reboot_mode_get,
	.set = pm8916_pon_reboot_mode_set,
};

static int pm8916_pon_reboot_mode_probe(struct udevice *dev)
{
	/* this driver only works as a child of pm8916_pon */
	if (!dev->parent || !dev->parent->driver ||
	    strcmp(dev->parent->driver->name, "pm8916_pon"))
		return -EINVAL;

	return 0;
}

U_BOOT_DRIVER(pm8916_pon_reboot_mode) = {
	.name 	= "pm8916_pon_reboot_mode",
	.id 	= UCLASS_REBOOT_MODE,
	.probe	= pm8916_pon_reboot_mode_probe,
	.ops	= &pm8916_pon_reboot_mode_ops,
};
