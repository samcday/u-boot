// SPDX-License-Identifier: BSD-3-Clause
/*
 * reloc_arm.c - position-independent ARM ELF shared object relocator
 *
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 * Copyright (C) 1999 Hewlett-Packard Co.
 * Contributed by David Mosberger <davidm@hpl.hp.com>.
 *
 * All rights reserved.
 *
 * This file is taken and modified from the gnu-efi project.
 */

#include <efi.h>
#include <elf.h>

efi_status_t EFIAPI _relocate(long ldbase, Elf32_Dyn *dyn)
{
	long relsz = 0, relent = 0;
	Elf32_Rel *rel = 0;
	Elf32_Sym *symtab = 0;
	ulong *addr;
	int i;

	for (i = 0; dyn[i].d_tag != DT_NULL; ++i) {
		switch (dyn[i].d_tag) {
		case DT_REL:
			rel = (Elf32_Rel *)((ulong)dyn[i].d_un.d_ptr
					+ ldbase);
			break;
		case DT_RELSZ:
			relsz = dyn[i].d_un.d_val;
			break;
		case DT_RELENT:
			relent = dyn[i].d_un.d_val;
			break;
		case DT_SYMTAB:
			symtab = (Elf32_Sym *)((ulong)dyn[i].d_un.d_ptr + ldbase);
			break;
		default:
			break;
		}
	}

	if (!rel && relent == 0)
		return EFI_SUCCESS;

	if (!rel || relent == 0)
		return EFI_LOAD_ERROR;

	while (relsz > 0) {
		ulong symidx = ELF32_R_SYM(rel->r_info);
		ulong symval = 0;

		if (symidx) {
			if (!symtab)
				return EFI_LOAD_ERROR;
			symval = symtab[symidx].st_value + ldbase;
		}

		/* apply the relocs */
		switch (ELF32_R_TYPE(rel->r_info)) {
		case R_ARM_NONE:
			break;
		case R_ARM_RELATIVE:
			addr = (ulong *)(ldbase + rel->r_offset);
			*addr += ldbase;
			break;
		case R_ARM_ABS32:
			addr = (ulong *)(ldbase + rel->r_offset);
			*addr += symval;
			break;
		case R_ARM_GLOB_DAT:
		case R_ARM_JUMP_SLOT:
			if (!symidx)
				return EFI_LOAD_ERROR;
			addr = (ulong *)(ldbase + rel->r_offset);
			*addr = symval;
			break;
		default:
			return EFI_LOAD_ERROR;
		}
		rel = (Elf32_Rel *)((char *)rel + relent);
		relsz -= relent;
	}

	return EFI_SUCCESS;
}
