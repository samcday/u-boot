// SPDX-License-Identifier: GPL-2.0+

struct pm8916_pon_priv {
    struct udevice *pmic;
    phys_addr_t base;
    u32 revision;
};
