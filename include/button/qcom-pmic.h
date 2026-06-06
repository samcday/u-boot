/* SPDX-License-Identifier: GPL-2.0+ */

/**
 * Binds the qcom_pwrkey driver to compatible pwrkey/resin child nodes
 * belonging to @parent
 */
int button_qcom_pmic_setup(struct udevice *parent);
