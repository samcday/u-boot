// SPDX-License-Identifier: GPL-2.0+

#include <dm.h>
#include <dm/device_compat.h>
#include <dm/read.h>
#include <fdtdec.h>
#include <power/pmic.h>
#include <reboot-mode/reboot-mode.h>

#define PON_SOFT_RB_SPARE 0x8f

#define GEN1_REASON_SHIFT 2

struct pm8916_pon_reboot_mode_priv {
	struct udevice *pmic;
	phys_addr_t base;
};

static int pm8916_pon_reboot_mode_get(struct udevice *dev, u32 *rebootmode)
{
	struct pm8916_pon_reboot_mode_priv *priv = dev_get_priv(dev);
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
	struct pm8916_pon_reboot_mode_priv *priv = dev_get_priv(dev);
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
	struct pm8916_pon_reboot_mode_priv *priv = dev_get_priv(dev);
	struct udevice *pon = dev_get_parent(dev);
	fdt_addr_t base;

	/* this driver only works as a child of pm8916_pon */
	if (!pon || !pon->driver || strcmp(pon->driver->name, "pm8916_pon"))
		return -EINVAL;

	priv->pmic = dev_get_parent(pon);
	if (!priv->pmic) {
		dev_err(dev, "PMIC driver not found\n");
		return -EINVAL;
	}

	base = dev_read_addr(pon);
	if (base == FDT_ADDR_T_NONE) {
		dev_err(dev, "missing PON reg base\n");
		return -EINVAL;
	}
	priv->base = base;

	return 0;
}

U_BOOT_DRIVER(pm8916_pon_reboot_mode) = {
	.name = "pm8916_pon_reboot_mode",
	.id = UCLASS_REBOOT_MODE,
	.probe = pm8916_pon_reboot_mode_probe,
	.ops = &pm8916_pon_reboot_mode_ops,
	.priv_auto = sizeof(struct pm8916_pon_reboot_mode_priv),
};
