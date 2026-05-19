/* SPDX-License-Identifier: GPL-2.0+ OR BSD-2-Clause */
/*
 * crt0-efi-arm.S - PE/COFF header for ARM EFI applications
 *
 * Copright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This file is taken and modified from the gnu-efi project.
 */

#include <asm-generic/pe.h>

	.syntax		unified
	.section	.text.head

	/*
	 * Magic "MZ" signature for PE/COFF
	 */
	.globl	image_base
image_base:
	.short	IMAGE_DOS_SIGNATURE		/* 'MZ' */
	.skip	58				/* 'MZ' + pad + offset == 64 */
	.long	pe_header - image_base		/* Offset to the PE header */
pe_header:
	.long	IMAGE_NT_SIGNATURE		/* 'PE' */
coff_header:
	.short	IMAGE_FILE_MACHINE_THUMB	/* Mixed ARM/Thumb */
	.short	3				/* nr_sections */
	.long	0				/* TimeDateStamp */
	.long	0				/* PointerToSymbolTable */
	.long	0				/* NumberOfSymbols */
	.short	section_table - optional_header	/* SizeOfOptionalHeader */
	/* Characteristics */
	.short	(IMAGE_FILE_EXECUTABLE_IMAGE | \
		 IMAGE_FILE_32BIT_MACHINE)
optional_header:
	.short	IMAGE_NT_OPTIONAL_HDR32_MAGIC	/* PE32 format */
	.byte	0x02				/* MajorLinkerVersion */
	.byte	0x14				/* MinorLinkerVersion */
	.long	_etext - _start_raw		/* SizeOfCode */
	.long	_data_size			/* SizeOfInitializedData */
	.long	0				/* SizeOfUninitializedData */
	.long	_start_raw + 1 - image_base	/* AddressOfEntryPoint */
	.long	_start_raw - image_base		/* BaseOfCode */
	.long	0				/* BaseOfData */

extra_header_fields:
	.long	0x400000			/* ImageBase */
	.long	0x1000				/* SectionAlignment */
	.long	0x200				/* FileAlignment */
	.short	1				/* MajorOperatingSystemVersion */
	.short	0				/* MinorOperatingSystemVersion */
	.short	0				/* MajorImageVersion */
	.short	0				/* MinorImageVersion */
	.short	1				/* MajorSubsystemVersion */
	.short	0				/* MinorSubsystemVersion */
	.long	0				/* Win32VersionValue */

	.long	_image_end - image_base		/* SizeOfImage */

	/*
	 * Everything before the kernel image is considered part of the header
	 */
	.long	_start_raw - image_base		/* SizeOfHeaders */
	.long	0				/* CheckSum */
	.short	IMAGE_SUBSYSTEM_EFI_APPLICATION	/* Subsystem */
#if CONFIG_VENDOR_EFI
	.short	0				/* DllCharacteristics */
#else
	.short	(IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | \
		 IMAGE_DLLCHARACTERISTICS_NX_COMPAT | \
		 IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE)
#endif
	.long	0x100000			/* SizeOfStackReserve */
	.long	0x1000				/* SizeOfStackCommit */
	.long	0x100000			/* SizeOfHeapReserve */
	.long	0x1000				/* SizeOfHeapCommit */
	.long	0				/* LoaderFlags */
	.long	0x10				/* NumberOfRvaAndSizes */

	.quad	0				/* ExportTable */
	.quad	0				/* ImportTable */
	.quad	0				/* ResourceTable */
	.quad	0				/* ExceptionTable */
	.quad	0				/* CertificationTable */
	.long	_reloc_start - image_base	/* BaseRelocationTable */
	.long	_reloc_size
	.quad	0				/* Debug */
	.quad	0				/* Architecture */
	.quad	0				/* GlobalPtr */
	.quad	0				/* TLS Table */
	.quad	0				/* Load Config Table */
	.quad	0				/* Bound Import */
	.quad	0				/* Import Address Table */
	.quad	0				/* Delay Import Descriptor */
	.quad	0				/* CLR Runtime Header */
	.quad	0				/* Reserved */

	/* Section table */
section_table:

	.ascii	".text"
	.byte	0
	.byte	0
	.byte	0			/* end of 0 padding of section name */
	.long	_etext - _start_raw	/* VirtualSize */
	.long	_start_raw - image_base	/* VirtualAddress */
	.long	_etext - _start_raw	/* SizeOfRawData */
	.long	_start_raw - image_base	/* PointerToRawData */
	.long	0			/* PointerToRelocations */
	.long	0			/* PointerToLineNumbers */
	.short	0			/* NumberOfRelocations */
	.short	0			/* NumberOfLineNumbers */
	/* Characteristics (section flags) */
	.long	(IMAGE_SCN_MEM_READ | \
		 IMAGE_SCN_MEM_EXECUTE | \
		 IMAGE_SCN_CNT_CODE)

	.ascii	".data"
	.byte	0
	.byte	0
	.byte	0			/* end of 0 padding of section name */
	.long	_data_size		/* VirtualSize */
	.long	_data - image_base	/* VirtualAddress */
	.long	_data_size		/* SizeOfRawData */
	.long	_data - image_base	/* PointerToRawData */
	.long	0			/* PointerToRelocations */
	.long	0			/* PointerToLineNumbers */
	.short	0			/* NumberOfRelocations */
	.short	0			/* NumberOfLineNumbers */
	/* Characteristics (section flags) */
	.long	(IMAGE_SCN_MEM_WRITE | \
		 IMAGE_SCN_MEM_READ | \
		 IMAGE_SCN_CNT_INITIALIZED_DATA)

	.ascii	".reloc"
	.byte	0
	.byte	0			/* end of 0 padding of section name */
	.long	_reloc_size		/* VirtualSize */
	.long	_reloc_start - image_base	/* VirtualAddress */
	.long	_reloc_raw_size		/* SizeOfRawData */
	.long	_reloc_start - image_base	/* PointerToRawData */
	.long	0			/* PointerToRelocations */
	.long	0			/* PointerToLineNumbers */
	.short	0			/* NumberOfRelocations */
	.short	0			/* NumberOfLineNumbers */
	/* Characteristics (section flags) */
	.long	(IMAGE_SCN_MEM_DISCARDABLE | \
		 IMAGE_SCN_MEM_READ | \
		 IMAGE_SCN_CNT_INITIALIZED_DATA)

	.align		12
	.globl		_start_raw
_start_raw:
	.globl		_start
	.thumb
	.thumb_func
_start:
	push		{r0-r2, lr}

	adr.w		r1, .L_DYNAMIC
	ldr		r0, [r1]
	add		r1, r0, r1
	adr.w		r0, .L_BASE
.L_BASE:
	ldr		r2, .L_IMAGE_BASE_OFF
	sub		r0, r0, r2
	bl		_relocate
	cmp		r0, #0
	bne		0f

	ldr		r0, [sp]
	ldr		r1, [sp, #4]
	bl		efi_main

0:	add		sp, sp, #12
	pop		{r3}
	bx		r3

	.align		2
.L_DYNAMIC:
	.word		_DYNAMIC - .
.L_IMAGE_BASE_OFF:
	.word		.L_BASE - image_base
