/*
 * Copyright (C) 2007, Guennadi Liakhovetski <lg@denx.de>
 *
 * (C) Copyright 2008 Freescale Semiconductor, Inc.
 * Fred Fan (r01011@freescale.com)
 *
 * Configuration settings for the MX31ADS Freescale board.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <asm/arch/mx35.h>

 /* High Level Configuration Options */
#define CONFIG_ARM1136		1	/* This is an arm1136 CPU core */
#define CONFIG_MXC		1
#define CONFIG_MX35		1	/* in a mx35 */
#define CONFIG_MX35_HCLK_FREQ	24000000	/* RedBoot says 26MHz */
#define CONFIG_MX35_CLK32	32768

#define CONFIG_DISPLAY_CPUINFO
#define CONFIG_DISPLAY_BOARDINFO

#define BOARD_LATE_INIT

#define CONFIG_PANIC_HANG  /* Do not reboot if a panic occurs */
#define CONFIG_HANG_FEEDBACK /* Flash LEDs before hang */

#define CONFIG_TAGCRC			1
/*
 * Disabled for now due to build problems under Debian and a significant increase
 * in the final file size: 144260 vs. 109536 Bytes.
 */
#if 0
#define CONFIG_OF_LIBFDT		1
#define CONFIG_FIT			1
#define CONFIG_FIT_VERBOSE		1
#endif

#define CONFIG_CMDLINE_TAG		1	/* enable passing of ATAGs */
#define CONFIG_SETUP_MEMORY_TAGS	1
#define CONFIG_INITRD_TAG		1
#define CONFIG_SERIAL16_TAG		1
#define CONFIG_REVISION16_TAG	1
#define CONFIG_POST_TAG			1

#define CONFIG_MX35_GPIO		1

#define CONFIG_MMC			1
#define CONFIG_LEGACY_MMC		1
#define CONFIG_MMC_BOOTFLASH		0
#define CONFIG_MMC_BOOTFLASH_ADDR	0x41000
#define CONFIG_MMC_BOOTFLASH_SIZE	0x340000
#define CONFIG_MMC_USERDATA_ADDR	0x40C00
#define CONFIG_MMC_USERDATA_SIZE	0x400
#define CONFIG_MMC_BIST_ADDR		0x14C00
#define CONFIG_MMC_BIST_SIZE		0x2C000

#define CONFIG_FSL_ESDHC		1
#define CONFIG_FSL_ESDHC_DMA		1
#define CONFIG_SYS_FSL_ESDHC_ADDR	MMC_SDHC1_BASE_ADDR
#define CONFIG_CMD_MMC			1

#define CFG_MMC_BASE            0x0

#define CONFIG_BOOT_HALT_VOLTAGE	3400	/* 3.4V */
#define CONFIG_BOOT_CONTINUE_VOLTAGE	3600	/* 3.6V */	
#define CONFIG_BOOT_AUTOCHG_VOLTAGE	3800	/* 3.8V */	

/*
 * Size of malloc() pool
 */
#define CONFIG_SYS_MALLOC_LEN		(CONFIG_ENV_SIZE + 4 * 1024)
#define CONFIG_SYS_GBL_DATA_SIZE	128/* size in bytes reserved for initial data */

#define CONFIG_SYS_GBL_DATA_OFFSET	(TEXT_BASE - CONFIG_SYS_MALLOC_LEN - CONFIG_SYS_GBL_DATA_SIZE)
#define CONFIG_SYS_POST_WORD_ADDR	(CONFIG_SYS_GBL_DATA_OFFSET - 0x4)
#define CONFIG_SYS_INIT_SP_OFFSET	CONFIG_SYS_POST_WORD_ADDR

/*
 * Hardware drivers
 */

/* for pmic interface */
#define CONFIG_MX35_CSPI1	1
#define CONFIG_PMIC_13892	1

#define CONFIG_MX35_UART	1
#define CFG_MX35_UART1		1

/* allow to overwrite serial and ethaddr */
#define CONFIG_ENV_OVERWRITE
#define CONFIG_CONS_INDEX	1
#define CONFIG_BAUDRATE		115200
#define CONFIG_SYS_BAUDRATE_TABLE	{9600, 19200, 38400, 57600, 115200}

/* enable this if the bootloader should be tagged with version and crc info
 * #define CONFIG_BOARD_VERSION_STRUCTURE "../../board/imx35_luigi/verinfo.inc"
 */

/*-----------------------------------------------------------------------
 * FLASH and environment organization
 */
#define CONFIG_SYS_NO_FLASH		1

#define CONFIG_ENV_IS_NOWHERE	1
#define CONFIG_SYS_ENV_SECT_SIZE	(4 * 1024)
#define CONFIG_ENV_SIZE		CONFIG_SYS_ENV_SECT_SIZE

/***********************************************************
 * Command definition
 ***********************************************************/

#define CONFIG_CMD_RUN		/* run command in env variable	*/
#define CONFIG_CMD_LOG

/* Lab 126 cmds */
#define CONFIG_CMD_BIST		1
#define CONFIG_CMD_PMIC		1
#define CONFIG_CMD_IDME		1

#define CONFIG_BOOTDELAY	3	/* autoboot after 3 seconds	*/

#define CONFIG_LOADADDR		0x87F40400	/* loadaddr env var */
#define CONFIG_BISTADDR		0x87F00000

#define CONFIG_BISTCMD_LOCATION 0x87B00000
#define CONFIG_BISTCMD_MAGIC	0xBC 

#define _STRINGIZE(s) #s
#define TOSTRING(s) _STRINGIZE(s)

#define	CONFIG_EXTRA_ENV_SETTINGS \
 "bootcmd=bootm " TOSTRING(CONFIG_MMC_BOOTFLASH_ADDR) "\0"		\
 "bootretry=-1\0" \
 "failbootcmd=panic\0" \
 "post_hotkeys=0\0" \
 "loglevel=5\0"

/*
 * Miscellaneous configurable options
 */
#undef	CONFIG_SYS_LONGHELP		/* undef to save memory */
#define CONFIG_SYS_PROMPT		"uboot> "
#define CONFIG_SYS_CBSIZE		256	/* Console I/O Buffer Size */
/* Print Buffer Size */
#define CONFIG_SYS_PBSIZE		(CONFIG_SYS_CBSIZE + sizeof(CONFIG_SYS_PROMPT) + 16)
#define CONFIG_SYS_MAXARGS		16	/* max number of command args */
#define CONFIG_SYS_BARGSIZE		CONFIG_SYS_CBSIZE	/* Boot Argument Buffer Size */

#define CONFIG_SYS_LOAD_ADDR		CONFIG_LOADADDR

#define CONFIG_SYS_HZ			CONFIG_MX35_CLK32/* use 32kHz clock as source */

#define CONFIG_CMDLINE_EDITING	1

#define CONFIG_HW_WATCHDOG 1

#define CONFIG_NUM_PARTITIONS	5

#ifndef __ASSEMBLY__

#define PARTITION_FILL_SPACE	-1

typedef struct partition_info_t {
    const char *name;
    unsigned int address;
    unsigned int size;
} partition_info_t;

static const struct partition_info_t partition_info[CONFIG_NUM_PARTITIONS] = {
    {
	.name = "dcd",
	.address = 0x400,
	.size = 0x800,  // 2 kB
    },
    {
	.name = "bootloader",
	.address = 0xC00,
	.size = 0x40000, // 256 kB
    },
    {
	.name = "userdata",
	.address = CONFIG_MMC_USERDATA_ADDR,
	.size = CONFIG_MMC_USERDATA_SIZE,  // 1 kB
    },
    {
	.name = "kernel",
	.address = CONFIG_MMC_BOOTFLASH_ADDR,
	.size = CONFIG_MMC_BOOTFLASH_SIZE,  // ~3 MB
    },
    {
	.name = "system",
	.address = 0x3C1000,
	.size = 0x28A00000,  // 650 MB
    }
};

#define CONFIG_NUM_NV_VARS 8
#define CONFIG_SYS_BOARD_ID_OFFSET 0xbf0

typedef struct nvram_t {
    const char *name;
    unsigned int offset;
    unsigned int size;
} nvram_t;

static const struct nvram_t nvram_info[CONFIG_NUM_NV_VARS] = {
    {
	.name = "serial",
	.offset = 0,
	.size = 16,
    },
    {
	.name = "panel",
	.offset = 0x20,
	.size = 16,
    },
    {
	.name = "accel",
	.offset = 0x48,
	.size = 16,
    },
    {
	.name = "mac",
	.offset = 0x58,
	.size = 12,
    },
    {
	.name = "sec",
	.offset = 0x220,
	.size = 20,
    },
    {
	.name = "pcbsn",
	.offset = 0x204,
	.size = 16,
    },
    {
	.name = "config",
	.offset = 0x214,
	.size = 12,
    },
    {
	.name = "postmode",
	.offset = 0x250,
	.size = 16,
    },
};

#endif
    
/*-----------------------------------------------------------------------
 * Stack sizes
 *
 * The stack sizes are set up in start.S using the settings below
 */
#define CONFIG_STACKSIZE	(128 * 1024)	/* regular stack */

/*-----------------------------------------------------------------------
 * Physical Memory Map
 */
#define CONFIG_NR_DRAM_BANKS	1
#define PHYS_SDRAM_1		CSD0_BASE_ADDR
#define PHYS_SDRAM_1_SIZE	(256 * 1024 * 1024)

#define CONFIG_SYS_SDRAM_BASE       PHYS_SDRAM_1
#define CONFIG_SYS_SDRAM_SIZE       PHYS_SDRAM_1_SIZE

#define CONFIG_SYS_ORG_MEMTEST      /* Original (not so) quickie memory test */
#define CONFIG_SYS_ALT_MEMTEST      /* Newer data, address, integrity test */

#define CONFIG_SYS_MEMTEST_SCRATCH  0x10000000      /* Internal RAM */
#define CONFIG_SYS_MEMTEST_START    PHYS_SDRAM_1	/* memtest works on */
#define CONFIG_SYS_MEMTEST_END      (PHYS_SDRAM_1 + PHYS_SDRAM_1_SIZE - 1)

#define CONFIG_LOGBUFFER
#define CONFIG_CMD_DIAG
#define CONFIG_POST         (CONFIG_SYS_POST_MEMORY | \
                             CONFIG_SYS_POST_FAIL)

/* Address and size of Redundant Environment Sector	*/
#define CFG_ENV_OFFSET_REDUND	(CFG_ENV_OFFSET + CONFIG_ENV_SIZE)
#define CFG_ENV_SIZE_REDUND	CONFIG_ENV_SIZE

#endif				/* __CONFIG_H */
