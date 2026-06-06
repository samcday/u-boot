// SPDX-License-Identifier: GPL-2.0+

#include <dm.h>
#include <dm/uclass.h>
#include <errno.h>
#include <fastboot.h>
#include <reboot-mode/reboot-mode.h>
#include <linux/kernel.h>

static int fastboot_set_reboot_mode(const char *mode)
{
	struct udevice *dev;
	int ret = -ENOENT;

	uclass_foreach_dev_probe(UCLASS_REBOOT_MODE, dev) {
		ret = dm_reboot_mode_activate(dev, mode);
		if (!ret || (ret != -ENOENT && ret != -ENOSYS))
			return ret;
	}

	return ret;
}

int fastboot_set_reboot_flag(enum fastboot_reboot_reason reason)
{
	static const char * const reboot_modes[FASTBOOT_REBOOT_REASONS_COUNT][2] = {
		[FASTBOOT_REBOOT_REASON_BOOTLOADER] = { "bootloader", NULL },
		[FASTBOOT_REBOOT_REASON_FASTBOOTD] = { "fastboot", "bootloader" },
		[FASTBOOT_REBOOT_REASON_RECOVERY] = { "recovery", NULL },
	};
	int i, ret;

	if (reason >= FASTBOOT_REBOOT_REASONS_COUNT)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(reboot_modes[reason]); i++) {
		if (!reboot_modes[reason][i])
			continue;

		ret = fastboot_set_reboot_mode(reboot_modes[reason][i]);
		if(!ret || (ret != -ENOENT && ret != -ENOSYS))
			return ret;
	}

	return -ENOENT;
}

