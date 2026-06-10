/* SPDX-License-Identifier: GPL-2.0+ */

#include <dm/device.h>
#include <linux/errno.h>

#if CONFIG_IS_ENABLED(BUTTON_QCOM_PMIC)
/**
 * Binds the qcom_pwrkey driver to compatible pwrkey/resin child nodes
 * belonging to @parent
 */
int button_qcom_pmic_setup(struct udevice *parent);
#else
static int button_qcom_pmic_setup(struct udevice *parent)
{
	return -ENOSYS;
}
#endif
