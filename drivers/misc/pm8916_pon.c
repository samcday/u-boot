// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm PM8916 PON glue driver
 *
 * The PON device handles pwrkey/resin buttons, as well as setting reboot
 * type and reboot mode. This glue driver binds the respective drivers.
 *
 * (C) Copyright 2025 Linaro Ltd.
 */

#include <dm.h>
#include <dm/lists.h>
#include <dm/device_compat.h>
#include <power/pmic.h>
#include <linux/delay.h>
#include <sysreset.h>
#include "button/qcom-pmic.h"
#include "pm8916_pon.h"

#define PON_REV2			0x01

#define PON_PS_HOLD_RST_CTL		0x5a
#define PON_PS_HOLD_RST_CTL2		0x5b

#define PON_PS_HOLD_ENABLE		BIT(7)

#define PON_PS_HOLD_TYPE_WARM_RESET	1
#define PON_PS_HOLD_TYPE_SHUTDOWN	4
#define PON_PS_HOLD_TYPE_HARD_RESET	7

struct pm8916_pon_priv {
	struct udevice *pmic;
	phys_addr_t base;
	u32 revision;
};

int pm8916_pon_set_reboot_type(enum sysreset_t reset_type)
{
	struct udevice *dev;
	struct pm8916_pon_priv *priv;
	int ret;
	uint pmic_reset_type;
	uint enable_reg;

	ret = uclass_get_device_by_driver(UCLASS_MISC,
					  DM_DRIVER_GET(pm8916_pon), &dev);
	if (ret) {
		dev_warn(dev, "couldn't find pm8916_pon device: %d\n", ret);
		return ret;
	}
	if (!dev) {
		dev_warn(dev, "couldn't find pm8916_pon device\n");
		return -ENODEV;
	}

	priv = dev_get_priv(dev);

	/* PMICs with revision 0 have the enable bit in same register as ctrl */
	if (priv->revision == 0)
		enable_reg = PON_PS_HOLD_RST_CTL;
	else
		enable_reg = PON_PS_HOLD_RST_CTL2;

	switch (reset_type) {
		case SYSRESET_POWER_OFF:
			pmic_reset_type = PON_PS_HOLD_TYPE_SHUTDOWN;
			break;
		case SYSRESET_COLD:
		case SYSRESET_POWER:
			pmic_reset_type = PON_PS_HOLD_TYPE_HARD_RESET;
			break;
		default:
			pmic_reset_type = PON_PS_HOLD_TYPE_WARM_RESET;
	}

	dev_dbg(dev, "Setting PON reboot type: %x\n", pmic_reset_type);

	ret = pmic_clrsetbits(priv->pmic, priv->base + enable_reg,
			      PON_PS_HOLD_ENABLE, 0);
	if (ret) {
		dev_warn(dev, "clear PON_PS_HOLD_ENABLE failed: %d\n", ret);
		return ret;
	}

	/* Delay needed for disable to kick in. */
	/* Kernel uses usleep_range(100, 1000), lk2nd waits 300usec */
	udelay(300);

	ret = pmic_clrsetbits(priv->pmic, priv->base + PON_PS_HOLD_RST_CTL,
			      0, pmic_reset_type);
	if (ret) {
		dev_warn(dev, "RST_CTL set failed: %d\n", ret);
		return ret;
	}

	ret = pmic_clrsetbits(priv->pmic, priv->base + enable_reg,
			      0, PON_PS_HOLD_ENABLE);
	if (ret) {
		dev_warn(dev, "PON_PS_HOLD_ENABLE failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int pm8916_pon_probe(struct udevice *dev)
{
	int ret;
	struct pm8916_pon_priv *priv = dev_get_priv(dev);

	priv->pmic = dev->parent;
	if (!priv->pmic || !device_is_compatible(priv->pmic, "qcom,pm8916")) {
		dev_err(dev, "parent is not qcom,pm8916 compatible\n");
		return -EINVAL;
	}

	priv->base = dev_read_addr(dev);

	if (!priv->base) {
		dev_err(dev, "missing reg base\n");
		return -EINVAL;
	}

	ret = pmic_reg_read(priv->pmic, priv->base + PON_REV2);
	if (ret < 0) {
		dev_err(dev, "failed to determine PON rev: %d\n", ret);
		return ret;
	}

	priv->revision = ret;
	dev_dbg(dev, "PON rev: %d\n", priv->revision);

	return 0;
}

static int pm8916_pon_bind(struct udevice *dev)
{
	int ret = button_qcom_pmic_setup(dev);
	if (ret)
		dev_warn(dev, "failed to bind qcom_pwrkey: %d\n", ret);

	return 0;
}

static const struct udevice_id pm8916_pon_ids[] = {
	{ .compatible = "qcom,pm8916-pon" },
	{ },
};

U_BOOT_DRIVER(pm8916_pon) = {
	.name 		= "pm8916_pon",
	.id 		= UCLASS_MISC,
	.of_match 	= pm8916_pon_ids,
	.bind 		= pm8916_pon_bind,
	.probe 		= pm8916_pon_probe,
	.priv_auto	= sizeof(struct pm8916_pon_priv),
};
