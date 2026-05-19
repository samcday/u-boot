// SPDX-License-Identifier: GPL-2.0+
/*
 * ARM support for running U-Boot as a UEFI application.
 */

#include <efi.h>
#include <efi_api.h>
#include <init.h>
#include <time.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

phys_addr_t board_get_usable_ram_top(phys_size_t total_size)
{
	return efi_get_ram_base() + gd->ram_size;
}

int dram_init(void)
{
	/* gd->ram_size is set by the EFI app before board init runs. */
	return 0;
}

int dram_init_banksize(void)
{
	gd->bd->bi_dram[0].start = efi_get_ram_base();
	gd->bd->bi_dram[0].size = gd->ram_size;

	return 0;
}

ulong get_tbclk(void)
{
	return CONFIG_SYS_HZ;
}

uint64_t notrace get_ticks(void)
{
	static uint64_t ticks;

	return ticks++;
}

void __udelay(unsigned long usec)
{
	struct efi_boot_services *boot = efi_get_boot();

	if (boot)
		boot->stall(usec);
}
