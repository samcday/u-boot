#include <linux/types.h>
#include <asm/global_data.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <log.h>
#include <sort.h>
#include <string.h>

#include "qcom-priv.h"

DECLARE_GLOBAL_DATA_PTR;

extern struct qcom_ddr_bank prevbl_ddr_banks[CONFIG_NR_DRAM_BANKS];

bool qcom_atags_valid(const phys_addr_t p)
{
	const struct tag *tags = (const struct tag *)p;

	return tags && tags->hdr.tag == ATAG_CORE &&
	       tags->hdr.size >= sizeof(struct tag_header) / sizeof(u32);
}

int qcom_parse_atags(const struct tag *tags)
{
	phys_addr_t ram_end = 0;
	const struct tag *t;
	bool atags_end = false;
	u32 words = 0;
	int j = 0;

	memset(prevbl_ddr_banks, 0, sizeof(prevbl_ddr_banks));

	for (t = tags; words < SZ_16K / sizeof(u32); t = tag_next(t)) {
		if (t->hdr.tag == ATAG_NONE) {
			atags_end = true;
			break;
		}
		if (t->hdr.size < sizeof(struct tag_header) / sizeof(u32))
			return -EINVAL;
		if (t->hdr.size > SZ_16K / sizeof(u32) - words)
			return -EINVAL;

		words += t->hdr.size;

		if (t->hdr.tag != ATAG_MEM)
			continue;
		if (t->hdr.size < tag_size(tag_mem32))
			return -EINVAL;
		if (!t->u.mem.size)
			continue;

		prevbl_ddr_banks[j].start = t->u.mem.start;
		prevbl_ddr_banks[j].size = t->u.mem.size;
		ram_end = max(ram_end,
			      (phys_addr_t)t->u.mem.start + t->u.mem.size);
		j++;

		if (j == CONFIG_NR_DRAM_BANKS)
			break;
	}

	if (!atags_end && j < CONFIG_NR_DRAM_BANKS) {
		log_err("Provided more memory banks than we can handle\n");
		return -EINVAL;
	}
	if (!j)
		return -ENODATA;

	qsort(prevbl_ddr_banks, j, sizeof(prevbl_ddr_banks[0]), ddr_bank_cmp);

	gd->ram_base = prevbl_ddr_banks[0].start;
	gd->ram_size = ram_end - gd->ram_base;

	return 0;
}
