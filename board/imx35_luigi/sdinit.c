/*
 * Copyright 2008-2009 Freescale Semiconductor, Inc.
 *
 * See file	CREDITS	for	list of	people who contributed to this
 * project.
 *
 * This	program	is free	software; you can redistribute it and/or
 * modify it under the terms of	the	GNU	General	Public License as
 * published by	the	Free Software Foundation; either version 2 of
 * the License,	or (at your	option)	any	later version.
 *
 * This	program	is distributed in the hope that	it will	be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A	PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received	a copy of the GNU General Public License
 * along with this program;	if not,	write to the Free Software
 * Foundation, Inc., 59	Temple Place, Suite	330, Boston,
 * MA 02111-1307 USA
 */

/*!
 * @file sdinit.c
 *
 * @brief initialization code for SD/MMC
 *
 * @ingroup mmc
 */

#include <common.h>

#ifdef CONFIG_MMC

#include <linux/types.h>
#include <asm/io.h>
#include <asm/errno.h>
#include <asm/arch/sdhc.h>
#include <asm/arch/mx35.h>
#include <asm/arch/mx35_pins.h>
#include <asm/arch/iomux.h>

extern volatile u32 esdhc_base_pointer;

#define DBG(fmt,args...)	if (verbose) printf (fmt ,##args)

int sdhc_init(int interface_esdhc)
{
	u32 pad_val = 0;
	int verbose = 0;

	DBG("sdhc_init: begin\n");

	DBG("sdhc_init: rev=%x\n", interface_esdhc);

	if (is_soc_rev(CHIP_REV_2_0) >= 0) {

	  DBG("sdhc_init: >=rev2 chip\n");
			/* IOMUX PROGRAMMING */
		switch (interface_esdhc) {
		case 0:
			DBG("TO2 ESDHC1\n");

			esdhc_base_pointer = \
				(volatile u32 )MMC_SDHC1_BASE_ADDR;

			mxc_request_iomux(MX35_PIN_SD1_CLK,
					MUX_CONFIG_FUNC | MUX_CONFIG_SION);

			pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
					PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_HIGH |
					PAD_CTL_47K_PU | PAD_CTL_SRE_FAST;

			mxc_request_iomux(MX35_PIN_SD1_CMD,
				MUX_CONFIG_FUNC | MUX_CONFIG_SION);
			mxc_iomux_set_pad(MX35_PIN_SD1_CMD, pad_val);

			mxc_request_iomux(MX35_PIN_SD1_DATA0,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
			mxc_iomux_set_pad(MX35_PIN_SD1_DATA0, pad_val);

			mxc_request_iomux(MX35_PIN_SD1_DATA1,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
			mxc_iomux_set_pad(MX35_PIN_SD1_DATA1, pad_val);

			mxc_request_iomux(MX35_PIN_SD1_DATA2,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
			mxc_iomux_set_pad(MX35_PIN_SD1_DATA2, pad_val);

			/* SDHC1 DATA pins 4-7 */
			mxc_request_iomux(MX35_PIN_FEC_TX_CLK,
				  MUX_CONFIG_ALT1);
			mxc_iomux_set_pad(MX35_PIN_FEC_TX_CLK, pad_val);
			mxc_request_iomux(MX35_PIN_FEC_RX_CLK,
				  MUX_CONFIG_ALT1);
			mxc_iomux_set_pad(MX35_PIN_FEC_RX_CLK, pad_val);
			mxc_request_iomux(MX35_PIN_FEC_RX_DV,
				  MUX_CONFIG_ALT1);
			mxc_iomux_set_pad(MX35_PIN_FEC_RX_DV, pad_val);
			mxc_request_iomux(MX35_PIN_FEC_COL,
				  MUX_CONFIG_ALT1);
			mxc_iomux_set_pad(MX35_PIN_FEC_COL, pad_val);

			mxc_iomux_set_input(MUX_IN_ESDHC1_DAT4_IN, INPUT_CTL_PATH1);
			mxc_iomux_set_input(MUX_IN_ESDHC1_DAT5_IN, INPUT_CTL_PATH1);
			mxc_iomux_set_input(MUX_IN_ESDHC1_DAT6_IN, INPUT_CTL_PATH1);
			mxc_iomux_set_input(MUX_IN_ESDHC1_DAT7_IN, INPUT_CTL_PATH1);

			pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
				PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_HIGH |
				PAD_CTL_100K_PU | PAD_CTL_SRE_FAST | PAD_CTL_DRV_1_8V;

			mxc_request_iomux(MX35_PIN_SD1_DATA3,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
			mxc_iomux_set_pad(MX35_PIN_SD1_DATA3, pad_val);

			udelay(100000); /* Settling time */

			break;
		case 1:
			DBG("TO2 ESDHC2\n");

			esdhc_base_pointer = \
				(volatile u32 )MMC_SDHC2_BASE_ADDR;

			mxc_request_iomux(MX35_PIN_SD2_CLK,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
			mxc_request_iomux(MX35_PIN_SD2_CMD,
				  MUX_CONFIG_FUNC | MUX_CONFIG_SION);
			mxc_request_iomux(MX35_PIN_SD2_DATA0,
				  MUX_CONFIG_FUNC);
			mxc_request_iomux(MX35_PIN_SD2_DATA3,
				  MUX_CONFIG_FUNC);

			pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
				PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_MAX |
				PAD_CTL_47K_PU | PAD_CTL_SRE_FAST;
			mxc_iomux_set_pad(MX35_PIN_SD2_CLK, pad_val);

			pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
				PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_MAX |
				PAD_CTL_100K_PU | PAD_CTL_SRE_FAST;
			mxc_iomux_set_pad(MX35_PIN_SD2_CMD, pad_val);
			mxc_iomux_set_pad(MX35_PIN_SD2_DATA0, pad_val);
			mxc_iomux_set_pad(MX35_PIN_SD2_DATA3, pad_val);

			break;
		case 2:
			DBG("TO2 ESDHC3\n");

			esdhc_base_pointer = \
				(volatile u32 )MMC_SDHC3_BASE_ADDR;

			printf("TO2 ESDHC3 not supported!");
			break;
		default:
			break;
		}
	} else {
	  
	  DBG("sdhc_init: rev1 chip\n");
		pad_val = PAD_CTL_PUE_PUD | PAD_CTL_PKE_ENABLE |
			PAD_CTL_HYS_SCHMITZ | PAD_CTL_DRV_MAX |
			PAD_CTL_100K_PU | PAD_CTL_SRE_FAST;

		switch (interface_esdhc) {
		case 0:
			DBG("TO1 ESDHC1\n");

			esdhc_base_pointer = \
				(volatile u32 )MMC_SDHC1_BASE_ADDR;

			mxc_iomux_set_pad(MX35_PIN_SD1_DATA3, pad_val);
			break;
		case 1:
			DBG("TO1 ESDHC2\n");

			esdhc_base_pointer = \
				(volatile u32 )MMC_SDHC2_BASE_ADDR;

			mxc_iomux_set_pad(MX35_PIN_SD2_DATA3, pad_val);
			break;
		case 2:
			DBG("TO1 ESDHC3\n");

			esdhc_base_pointer = \
				(volatile u32 )MMC_SDHC3_BASE_ADDR;

			printf("TO1 ESDHC3 not supported!");
			break;
		default:
			break;
		}
	} 

	return 0;
}

#endif
