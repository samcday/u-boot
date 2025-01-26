/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __PM8916_PON_H__
#define __PM8916_PON_H__

#include <linux/errno.h>
#include <sysreset.h>

#if CONFIG_IS_ENABLED(PM8916_PON)
int pm8916_pon_set_reboot_type(enum sysreset_t reset_type);
#else
static inline int pm8916_pon_set_reboot_type(enum sysreset_t reset_type)
{
	return -ENOSYS;
}
#endif

#endif /* __PM8916_PON_H__ */
