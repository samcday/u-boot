/*
 * Mini SPI driver for iMX35
 *
 * Copyright (C) 2008, Lab126, Inc.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


#include <common.h>
#include <asm/arch/mx35.h>
#include <asm/arch/mx35_pins.h>
#include <asm/arch/iomux.h>
#include <asm/arch/minispi.h>

int cspi_logging = 0; /* should be 0 until serial output is configured */

/****** mini cspi driver ******/
#define CSPI_MAXLOOP 1000 /* max number of attempts to read/write FIFO */

#ifdef CONFIG_MX35_CSPI1
/* pins to use */
#define CSPI_MISO MX35_PIN_CSPI1_MISO
#define CSPI_MOSI MX35_PIN_CSPI1_MOSI
#define CSPI_SCLK MX35_PIN_CSPI1_SCLK
#define CSPI_SS0 MX35_PIN_CSPI1_SS0
#define CSPI_SS1 MX35_PIN_CSPI1_SS1
#define CSPI_SPI_RDY MX35_PIN_CSPI1_SPI_RDY

/* base address */
#define CSPI_BASE CSPI1_BASE_ADDR /* CSPI1 */
#endif

#ifdef CONFIG_MX35_CSPI2

/* pins to use */
#define CSPI_MISO MX35_PIN_CSPI2_MISO
#define CSPI_MOSI MX35_PIN_CSPI2_MOSI
#define CSPI_SCLK MX35_PIN_CSPI2_SCLK
#define CSPI_SS0 MX35_PIN_CSPI2_SS0
#define CSPI_SS1 MX35_PIN_CSPI2_SS1
#define CSPI_SPI_RDY MX35_PIN_CSPI2_SPI_RDY

/* base address */
#define CSPI_BASE CSPI1_BASE_ADDR /* CSPI2 */
#endif

/* */
#define CSPI_RXDATA (CSPI_BASE)
#define CSPI_TXDATA (CSPI_BASE+4)
#define CSPI_CONREG (CSPI_BASE+8)
#define CSPI_INTREG (CSPI_BASE+12)
#define CSPI_DMAREG (CSPI_BASE+16)
#define CSPI_STATREG (CSPI_BASE+20)
#define CSPI_PERIODREG (CSPI_BASE+24)
#define CSPI_TESTREG (CSPI_BASE+28)

/** CONREG bits **/
#define CSPI_CONREG_BITCOUNT_32		0x01f00000	/* BITCOUNT = 32 */
#define CSPI_CONREG_DATARATE_4		0x00000000	/* data rate = ipg/4 */
#define CSPI_CONREG_DATARATE_16		0x00020000	/* data rate = ipg/16 */
#define CSPI_CONREG_DATARATE_512	0x00070000	/* data rate = ipg/512 */
#define CSPI_CONREG_SS0			0x00000000	/* chip select SS0 */
#define CSPI_CONREG_SS1			0x00001000	/* chip select SS1 */
#define CSPI_CONREG_SS2			0x00002000	/* chip select SS2 */
#define CSPI_CONREG_SS3			0x00003000	/* chip select SS3 */
#define CSPI_CONREG_DRCTL_DC		0x00000000	/* data ready don't care */
#define CSPI_CONREG_DRCTL_EDGE		0x00000100	/* data ready edge trigger */
#define CSPI_CONREG_DRCTL_LEVEL		0x00000200	/* data ready level trigger */
#define CSPI_CONREG_SSPOL_HIGH		0x00000080	/* active high */
#define CSPI_CONREG_SSCTL_NEGSS		0x00000040	/* negate SS between bursts */
#define CSPI_CONREG_PHA_PHASE1		0x00000020	/* Phase 1 operation */
#define CSPI_CONREG_POL_LOW		0x00000010	/* active low clock polarity */
#define CSPI_CONREG_SMC_IMMEDIATE	0x00000008	/* start when write to TXFIFO */
#define CSPI_CONREG_XCH			0x00000004	/* initiate exchange */
#define CSPI_CONREG_MODE_MASTER		0x00000002	/* master mode */
#define CSPI_CONREG_EN			0x00000001	/* enable bit */

#define CSPI_STATREG_TC			0x00000100	/* transfer complete */
#define CSPI_STATREG_BO			0x00000080	/* bit counter overflow */
#define CSPI_STATREG_RO			0x00000040	/* RXFIFO overflow */
#define CSPI_STATREG_RF			0x00000020	/* RXFIFO full */
#define CSPI_STATREG_RH			0x00000010	/* RXFIFO half full */
#define CSPI_STATREG_RR			0x00000008	/* RXFIFO ready */
#define CSPI_STATREG_TF			0x00000004	/* TXFIFO full */
#define CSPI_STATREG_TH			0x00000002	/* TXFIFO half empty */
#define CSPI_STATREG_TE			0x00000001	/* TXFIFO empty */


#ifdef CSPI_DEBUG

static void cspi_dump_regs(void) {

    printf("CSPI_RXDATA=0x%08x\n", __REG(CSPI_RXDATA));
    printf("CSPI_TXDATA=0x%08x\n", __REG(CSPI_TXDATA));
    printf("CSPI_CONREG=0x%08x\n", __REG(CSPI_CONREG));
    printf("CSPI_INTREG=0x%08x\n", __REG(CSPI_INTREG));
    printf("CSPI_DMAREG=0x%08x\n", __REG(CSPI_DMAREG));
    printf("CSPI_STATREG=0x%08x\n", __REG(CSPI_STATREG));
    printf("CSPI_PERIODREG=0x%08x\n", __REG(CSPI_PERIODREG));
    printf("CSPI_TESTREG=0x%08x\n", __REG(CSPI_TESTREG));
}

#endif

static void cspi_delay(void) {
	int i=1000000;
	while(i--) ;
}

/* read all remaining data from rxfifo */
static void cspi_rx_flush(void) {
	int maxloop=CSPI_MAXLOOP;
	u32 junk;
	while(maxloop--) {
		if((__REG(CSPI_STATREG)&CSPI_STATREG_RR)) {
			junk=__REG(CSPI_RXDATA);
			if(cspi_logging) printf("cspi flushed %08x\n", junk);
		}
	}
}

/* loop until the tx is empty */
static void cspi_tx_flush(void) {
	int maxloop=CSPI_MAXLOOP;
	while(maxloop-- && !(__REG(CSPI_STATREG)&CSPI_STATREG_TE)) {
		cspi_delay();
	}
}

void cspi_init(void) {
    
#ifdef CSPI_DEBUG
    cspi_logging = 1;
#endif

    mxc_request_iomux(CSPI_MISO, MUX_CONFIG_FUNC);
    mxc_request_iomux(CSPI_MOSI, MUX_CONFIG_FUNC);
    mxc_request_iomux(CSPI_SCLK, MUX_CONFIG_FUNC);
    mxc_request_iomux(CSPI_SS0, MUX_CONFIG_FUNC);
    mxc_request_iomux(CSPI_SS1, MUX_CONFIG_FUNC);
    mxc_request_iomux(CSPI_SPI_RDY, MUX_CONFIG_FUNC);

    mxc_iomux_set_pad(MX35_PIN_CSPI1_MOSI,
		      PAD_CTL_DRV_1_8V | PAD_CTL_HYS_SCHMITZ |
		      PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |
		      PAD_CTL_100K_PD | PAD_CTL_DRV_NORMAL);
    mxc_iomux_set_pad(MX35_PIN_CSPI1_MISO,
		      PAD_CTL_DRV_1_8V | PAD_CTL_HYS_SCHMITZ |
		      PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |
		      PAD_CTL_100K_PD | PAD_CTL_DRV_NORMAL);
    mxc_iomux_set_pad(MX35_PIN_CSPI1_SS0,
		      PAD_CTL_DRV_1_8V | PAD_CTL_HYS_SCHMITZ |
		      PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |
		      PAD_CTL_100K_PU | PAD_CTL_ODE_CMOS |
		      PAD_CTL_DRV_NORMAL);
    mxc_iomux_set_pad(MX35_PIN_CSPI1_SCLK,
		      PAD_CTL_DRV_1_8V | PAD_CTL_HYS_SCHMITZ |
		      PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |
		      PAD_CTL_100K_PD | PAD_CTL_DRV_NORMAL);
    mxc_iomux_set_pad(MX35_PIN_CSPI1_SPI_RDY,
		      PAD_CTL_DRV_1_8V | PAD_CTL_HYS_SCHMITZ |
		      PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD |
		      PAD_CTL_100K_PU | PAD_CTL_DRV_NORMAL);

    __REG(CSPI_CONREG) = CSPI_CONREG_EN
	|CSPI_CONREG_MODE_MASTER
	|CSPI_CONREG_SSPOL_HIGH
	|CSPI_CONREG_DATARATE_16
	|CSPI_CONREG_BITCOUNT_32
	|CSPI_CONREG_SS0;
    __REG(CSPI_INTREG) = 0; /* no interrupts */
    __REG(CSPI_DMAREG) = 0; /* no DMA */
    __REG(CSPI_PERIODREG) = 500; /* 500 waits states - SPI clock source */
    cspi_rx_flush();
}

void cspi_shutdown(void) {
	cspi_tx_flush();
	__REG(CSPI_CONREG) = 0; /* disable the interface */
	/* TODO: disable iomux settings */
}

int cspi_write(u32 d) {
	int maxloop=CSPI_MAXLOOP;
	while(maxloop--) {
		/* loop though until space is available in queue */
		if((__REG(CSPI_STATREG)&CSPI_STATREG_TF)==0) {
			__REG(CSPI_TXDATA)=d;
			__REG(CSPI_CONREG)|=CSPI_CONREG_XCH;
			return 1; /* success */
		}
	}
	if(cspi_logging) printf("cspi write failed\n");
	return 0; /* failure */
}

int cspi_read(u32 *d) {
	int maxloop=CSPI_MAXLOOP;
	while(maxloop--) {
		/* loop though until data is in the queue */
		if((__REG(CSPI_STATREG)&CSPI_STATREG_RR)) {
			*d=__REG(CSPI_RXDATA);
			return 1; /* success */
		}
	}
	if(cspi_logging) printf("cspi read failed\n");
	return 0; /* failure */
}

