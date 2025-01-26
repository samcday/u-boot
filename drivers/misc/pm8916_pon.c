// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm PM8916 PON glue driver
 *
 * The PON device handles pwrkey/resin buttons, as well as setting reboot
 * reason and reboot mode. This glue driver binds the respective drivers.
 *
 * (C) Copyright 2025 Linaro Ltd.
 */

#include <dm.h>
#include <dm/lists.h>
#include <dm/device_compat.h>
#include "button/qcom-pmic.h"

static int pm8916_pon_bind(struct udevice *dev)
{
	int ret;

	ret = button_qcom_pmic_setup(dev);
	if (ret)
		dev_warn(dev, "failed to bind qcom_pwrkey: %d\n", ret);

	return 0;
}

static const struct udevice_id pm8916_pon_ids[] = {
	{ .compatible = "qcom,pm8916-pon" },
	{ },
};

U_BOOT_DRIVER(pm8916_pon) = {
	.name = "pm8916_pon",
	.id = UCLASS_MISC,
	.of_match = pm8916_pon_ids,
	.bind = pm8916_pon_bind,
};
