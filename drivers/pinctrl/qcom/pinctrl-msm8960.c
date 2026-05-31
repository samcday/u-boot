// SPDX-License-Identifier: GPL-2.0+
#include <dm.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <errno.h>

#include "pinctrl-qcom.h"
#include "dm/device_compat.h"

#define MSM8960_PS_HOLD_OFFSET 0x820

#define MAX_PIN_NAME_LEN   32
#define MSM8960_NUM_GPIO   152
#define MSM8960_NUM_PINS   158
#define MSM8960_GPIO_BASE  0x1000
#define MSM8960_GPIO_STRIDE 0x10

static char pin_name[MAX_PIN_NAME_LEN] __section(".data");

static const struct pinctrl_function msm8960_functions[] = {
	{ "gpio", 0 },
	{ "gsbi1", 1 },
	{ "gsbi2", 1 },
	{ "gsbi3", 1 },
	{ "gsbi4", 1 },
	{ "gsbi5", 1 },
	{ "gsbi6", 1 },
	{ "gsbi7", 1 },
	{ "gsbi8", 1 },
	{ "gsbi9", 1 },
	{ "gsbi10", 1 },
	{ "gsbi11", 1 },
	{ "gsbi12", 1 },
};

#define SDC_PINGROUP(pg_name, ctl, pull, drv) \
	{ \
		.name = pg_name, \
		.ctl_reg = ctl, \
		.io_reg = 0, \
		.pull_bit = pull, \
		.drv_bit = drv, \
		.oe_bit = -1, \
		.in_bit = -1, \
		.out_bit = -1, \
	}

static const struct msm_special_pin_data msm8960_special_pins_data[] = {
	SDC_PINGROUP("sdc1_clk", 0x20a0, 13, 6),
	SDC_PINGROUP("sdc1_cmd", 0x20a0, 11, 3),
	SDC_PINGROUP("sdc1_data", 0x20a0, 9, 0),
	SDC_PINGROUP("sdc3_clk", 0x20a4, 14, 6),
	SDC_PINGROUP("sdc3_cmd", 0x20a4, 11, 3),
	SDC_PINGROUP("sdc3_data", 0x20a4, 9, 0),
};

static const char *msm8960_get_function_name(struct udevice *dev,
					     unsigned int selector)
{
	return msm8960_functions[selector].name;
}

static int msm8960_get_function_mux(__maybe_unused unsigned int pin,
				    unsigned int selector)
{
	if (selector >= ARRAY_SIZE(msm8960_functions))
		return -EINVAL;

	return msm8960_functions[selector].val;
}

static const char *msm8960_get_pin_name(struct udevice *dev,
					unsigned int selector)
{
	if (selector < MSM8960_NUM_GPIO)
		snprintf(pin_name, MAX_PIN_NAME_LEN, "gpio%u", selector);
	else
		snprintf(pin_name, MAX_PIN_NAME_LEN, "%s",
			 msm8960_special_pins_data[selector -
						   MSM8960_NUM_GPIO].name);

	return pin_name;
}

static const struct msm_pinctrl_data msm8960_data = {
	.pin_data = {
		.pin_count = MSM8960_NUM_PINS,
		.gpio_base = MSM8960_GPIO_BASE,
		.gpio_stride = MSM8960_GPIO_STRIDE,
		.special_pins_start = MSM8960_NUM_GPIO,
		.special_pins_data = msm8960_special_pins_data,
	},
	.functions_count = ARRAY_SIZE(msm8960_functions),
	.get_function_name = msm8960_get_function_name,
	.get_function_mux = msm8960_get_function_mux,
	.get_pin_name = msm8960_get_pin_name,
};

static int msm8960_bind_pshold(struct udevice *dev)
{
	struct driver *drv;

	drv = lists_driver_lookup_name("qcom_pshold");
	if (!drv)
		return -ENOENT;

	return device_bind_with_driver_data(dev, drv, "msm8960-pshold",
					    MSM8960_PS_HOLD_OFFSET,
					    dev_ofnode(dev), NULL);
}

static int msm8960_pinctrl_bind(struct udevice *dev)
{
	int ret;

	if (IS_ENABLED(CONFIG_SYSRESET_QCOM_PSHOLD)) {
		ret = msm8960_bind_pshold(dev);
		if (ret)
			dev_err(dev, "Failed to bind pshold: %d\n", ret);
	}

	return msm_pinctrl_bind(dev);
}

static const struct udevice_id msm8960_pinctrl_ids[] = {
	{ .compatible = "qcom,msm8960-pinctrl", .data = (ulong)&msm8960_data },
	{ }
};

U_BOOT_DRIVER(pinctrl_msm8960) = {
	.name = "pinctrl_msm8960",
	.id = UCLASS_NOP,
	.of_match = msm8960_pinctrl_ids,
	.bind = msm8960_pinctrl_bind,
};
