// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm Embedded USB Debugger command.
 */

#include <command.h>
#include <qcom_eud.h>
#include <linux/string.h>

static int do_eud(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct udevice *dev;
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = qcom_eud_get(&dev);
	if (ret) {
		printf("EUD device not found: %d\n", ret);
		return CMD_RET_FAILURE;
	}

	if (!strcmp(argv[1], "enable")) {
		if (argc != 2)
			return CMD_RET_USAGE;
		ret = qcom_eud_enable(dev);
		if (ret) {
			printf("EUD enable failed: %d\n", ret);
			return CMD_RET_FAILURE;
		}
		puts("EUD enabled\n");
		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "disable")) {
		if (argc != 2)
			return CMD_RET_USAGE;
		ret = qcom_eud_disable(dev);
		if (ret) {
			printf("EUD disable failed: %d\n", ret);
			return CMD_RET_FAILURE;
		}
		puts("EUD disabled\n");
		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "status")) {
		if (argc != 2)
			return CMD_RET_USAGE;
		printf("EUD is %s\n", qcom_eud_is_enabled(dev) ?
		       "enabled" : "disabled");
		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "write")) {
		if (argc != 3)
			return CMD_RET_USAGE;

		ret = qcom_eud_enable(dev);
		if (ret) {
			printf("EUD enable failed: %d\n", ret);
			return CMD_RET_FAILURE;
		}

		ret = qcom_eud_com_write(dev, QCOM_EUD_COM_APPS_ID, argv[2],
					 strlen(argv[2]));
		if (ret) {
			printf("EUD COM write failed: %d\n", ret);
			return CMD_RET_FAILURE;
		}
		return CMD_RET_SUCCESS;
	}

	return CMD_RET_USAGE;
}

U_BOOT_CMD(eud, 3, 0, do_eud,
	   "control Qualcomm Embedded USB Debugger",
	   "enable - enable EUD and prepare the USB device controller\n"
	   "eud disable - disable EUD and tear down the USB device controller\n"
	   "eud status - show whether EUD is enabled\n"
	   "eud write <text> - enable EUD and write text to COM"
);
