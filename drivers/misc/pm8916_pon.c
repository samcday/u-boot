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
#include "button/qcom-pmic.h"

#define PON_REV2			0x01

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
