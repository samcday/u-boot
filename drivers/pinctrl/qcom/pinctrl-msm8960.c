// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm MSM8960 TLMM shim
 */

#include <dm.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <errno.h>

#define MSM8960_PS_HOLD_OFFSET	0x820

static int msm8960_pinctrl_bind(struct udevice *dev)
{
	struct driver *drv;

	if (!IS_ENABLED(CONFIG_SYSRESET_QCOM_PSHOLD))
		return 0;

	drv = lists_driver_lookup_name("qcom_pshold");
	if (!drv)
		return -ENOENT;

	return device_bind_with_driver_data(dev, drv, "msm8960-pshold",
					       MSM8960_PS_HOLD_OFFSET,
					       dev_ofnode(dev), NULL);
}

static const struct udevice_id msm8960_pinctrl_ids[] = {
	{ .compatible = "qcom,msm8960-pinctrl" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(pinctrl_msm8960) = {
	.name		= "pinctrl_msm8960",
	.id		= UCLASS_NOP,
	.of_match	= msm8960_pinctrl_ids,
	.bind		= msm8960_pinctrl_bind,
};
