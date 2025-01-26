// SPDX-License-Identifier: GPL-2.0+

#include <sysreset.h>

struct pm8916_pon_priv {
    struct udevice *pmic;
    phys_addr_t base;
    u32 revision;
};

int pm8916_pon_set_reboot_type(enum sysreset_t reset_type);
