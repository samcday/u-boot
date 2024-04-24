// SPDX-License-Identifier: GPL-2.0+
/*
 * Common initialisation for Qualcomm Snapdragon boards.
 *
 * Copyright (c) 2023 Linaro Ltd.
 * Author: Caleb Connolly <caleb.connolly@linaro.org>
 */

#define pr_fmt(fmt) "CapsuleUpdate: " fmt

#include <dm/device.h>
#include <dm/uclass.h>
#include <efi.h>
#include <efi_loader.h>
#include <malloc.h>
#include <scsi.h>
#include <part.h>
#include <time.h>

#include "qcom-priv.h"

struct efi_fw_image fw_images[] = {
	{
		.fw_name = u"QUALCOMM-UBOOT",
		.image_index = 1,
	},
};

struct efi_capsule_update_info update_info = {
	/* Filled in by configure_dfu_string() */
	.dfu_string = NULL,
	.num_images = ARRAY_SIZE(fw_images),
	.images = fw_images,
};

struct part_slot_status {
	u16 : 2;
	u16 active : 1;
	u16 : 3;
	u16 successful : 1;
	u16 unbootable : 1;
	u16 tries_remaining : 4;
};

#define SLOT_STATUS(info) ((struct part_slot_status*)&info->type_flags)

static int find_boot_partition(const char *partname, struct blk_desc *blk_dev, char *name)
{
	int ret;
	int partnum;
	struct disk_partition info;
	struct part_slot_status *slot_status;

	for (partnum = 1;; partnum++) {
		ret = part_get_info(blk_dev, partnum, &info);
		if (ret) {
			return ret;
		}
		slot_status = (struct part_slot_status *)&info.type_flags;
		log_io("%s: A: %1d, S: %1d, U: %1d, T: %1d\n", info.name, slot_status->active,
		       slot_status->successful, slot_status->unbootable, slot_status->tries_remaining);
		if (!strncmp(info.name, partname, strlen(partname)) && slot_status->active) {
			log_debug("Found active %s partition!\n", partname);
			strncpy(name, info.name, sizeof(info.name));
			return partnum;
		}
	}

	return -1;
}

static bool u16_strendswith(const u16 *in, const u16 *suffix)
{
	int in_len = u16_strlen(in);
	int suffix_len = u16_strlen(suffix);

	if (in_len < suffix_len)
		return false;

	return !u16_strcmp(in + in_len - suffix_len, suffix);
}

/* Patch the slot status GPT attributes to prevent ABL from switching slots */
static int patch_slot_status(struct blk_desc *desc)
{
	struct gpt_table tbl;
	int ret;
	u16 tmp;

	ret = gpt_table_load(desc, &tbl);
	if (ret) {
		log_err("Failed to load GPT table: %d\n", ret);
		return -1;
	}

	for (int i = 0; i < tbl.header.num_partition_entries; i++) {
		gpt_entry *entry = &tbl.entries[i];
		struct part_slot_status *slot_status;

		/* Check if partition name ends in _a or _b */
		if (!u16_strendswith(entry->partition_name, u"_a") &&
		    !u16_strendswith(entry->partition_name, u"_b"))
			continue;

		log_io("Patching slot status for %ls\n", entry->partition_name);

		/* Patch the slot status */
		tmp = entry->attributes.fields.type_guid_specific;
		slot_status = (struct part_slot_status *)&tmp;

		slot_status->successful = 1;
		slot_status->unbootable = 0;
		slot_status->tries_remaining = 7;

		entry->attributes.fields.type_guid_specific = tmp;
	}

	ret = gpt_table_write(&tbl);
	if (ret) {
		log_err("Failed to write GPT table: %d\n", ret);
		return -1;
	}

	gpt_table_free(&tbl);

	return 0;
}

/**
 * qcom_configure_capsule_updates() - Configure the DFU string for capsule updates
 *
 * U-Boot is flashed to the boot partition on Qualcomm boards. In most cases there
 * are two boot partitions, boot_a and boot_b. As we don't currently support doing
 * full A/B updates, we only support updating the currently active boot partition.
 *
 * So we need to find the current slot suffix and the associated boot partition.
 * We do this by looking for the boot partition that has the 'active' flag set
 * in the GPT partition vendor attribute bits.
 */
void qcom_configure_capsule_updates(void)
{
	struct blk_desc *desc;
	int ret = 0, partnum = -1, devnum;
	static char dfu_string[32] = { 0 };
	char *partname = "boot";
	char name[32]; /* GPT partition name */
	struct udevice *dev = NULL;
	efi_guid_t namespace_guid = QUALCOMM_UBOOT_BOOT_IMAGE_GUID;

	ulong start = get_timer(0);
	ret = efi_capsule_update_info_gen_ids(&namespace_guid,
					      env_get("soc"),
					      ofnode_read_string(ofnode_root(), "model"),
					      ofnode_read_string(ofnode_root(), "compatible"));
	printf("Time taken to generate GUIDs: %lu ms\n", get_timer(start));
	if (ret) {
		log_err("Failed to generate GUIDs: %d\n", ret);
		return;
	}

#ifdef CONFIG_SCSI
	/* Scan for SCSI devices */
	ret = scsi_scan(false);
	if (ret) {
		debug("Failed to scan SCSI devices: %d\n", ret);
		return;
	}
#else
	debug("Qualcomm capsule update running without SCSI support!\n");
#endif

	uclass_foreach_dev_probe(UCLASS_BLK, dev) {
		if (device_get_uclass_id(dev) != UCLASS_BLK)
			continue;

		desc = dev_get_uclass_plat(dev);
		if (!desc || desc->part_type == PART_TYPE_UNKNOWN)
			continue;
		devnum = desc->devnum;
		partnum = find_boot_partition(partname, desc,
					      name);
		if (partnum >= 0)
			break;
	}

	if (partnum < 0) {
		log_err("Failed to find boot partition\n");
		return;
	}

	switch(desc->uclass_id) {
	case UCLASS_SCSI:
		snprintf(dfu_string, 32, "scsi %d=u-boot-bin part %d", devnum, partnum);
		break;
	case UCLASS_MMC:
		snprintf(dfu_string, 32, "mmc part %d:%d=u-boot-bin", devnum, partnum);
		break;
	default:
		debug("Unsupported storage uclass: %d\n", desc->uclass_id);
		return;
	}
	log_debug("U-Boot boot partition is %s\n", name);
	log_debug("DFU string: %s\n", dfu_string);

	update_info.dfu_string = dfu_string;

	// ret = patch_slot_status(desc);
	// if (ret)
	// 	log_err("Failed to patch slot status\n");
}
