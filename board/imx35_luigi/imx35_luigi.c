/*
 * Copyright (C) 2007, Guennadi Liakhovetski <lg@denx.de>
 *
 * (C) Copyright 2008-2009 Freescale Semiconductor, Inc.
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
#include <asm/io.h>
#include <asm/arch/mx35.h>
#include <asm/arch/mx35_pins.h>
#include <asm/arch/iomux.h>
#include <i2c.h>
#include <linux/ctype.h>
#include <usb/file_storage.h>

#ifdef CONFIG_HW_WATCHDOG
#include <watchdog.h>
#endif

#ifdef CONFIG_PMIC_13892
#include <pmic_13892.h>
#endif

#ifdef CONFIG_CMD_IDME
#include <idme.h>
#endif

/* watchdog registers */
#define WDOG_WCR __REG16(WDOG_BASE_ADDR + 0x00)
#define WDOG_WSR __REG16(WDOG_BASE_ADDR + 0x02)
#define WDOG_WRSR __REG16(WDOG_BASE_ADDR + 0x04)
#define WDOG_WICR __REG16(WDOG_BASE_ADDR + 0x06)
#define WDOG_WMCR __REG16(WDOG_BASE_ADDR + 0x08)

DECLARE_GLOBAL_DATA_PTR;

static u32 system_rev;

/* board id and serial number. */
static u8 serial_number[16+1];
static u8 board_id[16+1];

u32 get_board_rev(void)
{
	return system_rev;
}

static inline void setup_soc_rev(void)
{
	int reg;
	reg = __REG(IIM_BASE_ADDR + IIM_SREV);
	if (!reg) {
		reg = __REG(ROMPATCH_REV);
		reg <<= 4;
	} else
		reg += CHIP_REV_1_0;
	system_rev = 0x35000 + (reg & 0xFF);
}

static inline void set_board_rev(int rev)
{
	system_rev =  (system_rev & ~(0xF << 8)) | (rev & 0xF) << 8;
}

int is_soc_rev(int rev)
{
	return (system_rev & 0xFF) - rev;
}

int dram_init(void)
{
	gd->bd->bi_dram[0].start = PHYS_SDRAM_1;
	gd->bd->bi_dram[0].size = PHYS_SDRAM_1_SIZE;

	return 0;
}


int board_info_valid (u8 *info)
{
    int i;

    for (i = 0; i < 16; i++) {
	if (!isalnum(info[i]))
	    return 0;
    }

    return 1;
}

/*************************************************************************
 * get_board_serial() - setup to pass kernel serial number information
 *      return: alphanumeric containing the serial number.
 *************************************************************************/
const u8 *get_board_serial(void)
{
    if (!board_info_valid(serial_number))
	return (u8 *) "0000000000000000";
    else
	return serial_number;
}

/*************************************************************************
 * get_board_id16() - setup to pass kernel board revision information
 *      16-byte alphanumeric containing the board revision.
 *************************************************************************/
const u8 *get_board_id16(void)
{
    if (!board_info_valid(board_id))
	return (u8 *) "0000000000000000";
    else
	return board_id;
}


int setup_board_info(void) 
{
#if defined(CONFIG_CMD_IDME)
    if (idme_get_var("boardid", (char *) board_id, 16 + 1)) 
#endif
    {
	/* not found: clean up garbage characters. */
	memset(board_id, 0, 16 + 1);
    }

#if defined(CONFIG_CMD_IDME)
    if (idme_get_var("serial", (char *) serial_number, 16 + 1)) 
#endif
    {
	/* not found: clean up garbage characters. */
	memset(serial_number, 0, 16 + 1);
    }

    return 0;
}

int board_init(void)
{
#ifdef CONFIG_HW_WATCHDOG
	/* set the timeout to the max number of ticks
	 * and WDZST
	 * leave other settings the same */
	WDOG_WCR = 0xff01 | (WDOG_WCR & 0xff);
	WDOG_WMCR = 0; /* Power Down Counter of WDOG is disabled. */
#endif

	setup_soc_rev();

	/* enable clocks */
	__REG(CCM_BASE_ADDR + CLKCTL_CGR0) |= 0x003F0000;
	__REG(CCM_BASE_ADDR + CLKCTL_CGR1) |= 0x00030FFF;
	__REG(CCM_BASE_ADDR + CLKCTL_CGR2) |= 0x00C00000;
	__REG(CCM_BASE_ADDR + CLKCTL_CGR3) = 0;

	/* setup pins for FEC */
	mxc_request_iomux(MX35_PIN_FEC_TX_CLK, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RX_CLK, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RX_DV, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_COL, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RDATA0, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TDATA0, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TX_EN, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_MDC, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_MDIO, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TX_ERR, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RX_ERR, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_CRS, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RDATA1, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TDATA1, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RDATA2, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TDATA2, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_RDATA3, MUX_CONFIG_FUNC);
	mxc_request_iomux(MX35_PIN_FEC_TDATA3, MUX_CONFIG_FUNC);

#define FEC_PAD_CTL_COMMON (PAD_CTL_DRV_3_3V|PAD_CTL_PUE_PUD| \
			PAD_CTL_ODE_CMOS|PAD_CTL_DRV_NORMAL|PAD_CTL_SRE_SLOW)
	mxc_iomux_set_pad(MX35_PIN_FEC_TX_CLK, FEC_PAD_CTL_COMMON |
			  PAD_CTL_HYS_SCHMITZ | PAD_CTL_PKE_ENABLE |
			  PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RX_CLK,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RX_DV,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_COL,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RDATA0,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TDATA0,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TX_EN,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_MDC,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_MDIO,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_22K_PU);
	mxc_iomux_set_pad(MX35_PIN_FEC_TX_ERR,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RX_ERR,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_CRS,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RDATA1,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TDATA1,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RDATA2,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TDATA2,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_RDATA3,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_SCHMITZ |
			  PAD_CTL_PKE_ENABLE | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX35_PIN_FEC_TDATA3,
			  FEC_PAD_CTL_COMMON | PAD_CTL_HYS_CMOS |
			  PAD_CTL_PKE_NONE | PAD_CTL_100K_PD);
#undef FEC_PAD_CTL_COMMON

	gd->bd->bi_arch_number = MACH_TYPE_MX35_3DS;	/* board id for linux */
	gd->bd->bi_boot_params = 0x80000100;	/* address of boot parameters */

	return 0;
}

#ifdef BOARD_LATE_INIT
static inline int board_detect(void)
{
	set_board_rev(BOARD_REV_2_0);
	return 1;
}

void board_power_off(void) 
{
#ifdef CONFIG_PMIC_13892

    /* Turn off VSD */
    pmic_wor_reg(REG_MODE_1, 0, VSD_EN);

    /* Turn off SW4 in halt/poweroff */
    pmic_wor_reg(REG_SW_5, 0, HALT_MHMODE);

    /* Clear out bit #2 in MEMA */
    pmic_wor_reg(REG_MEM_A, (0 << 2), (1 << 2));

    /* Turn on bit #2 */
    pmic_wor_reg(REG_MEM_A, (1 << 2), (1 << 2));
	
    /*
     * This puts Atlas in USEROFF power cut mode
     */
    pmic_wor_reg(REG_POWER_CTL0, USEROFF_SPI, USEROFF_SPI);

#endif
}

void board_reset(void) 
{
#ifdef CONFIG_PMIC_13892
    /* set to 10 sec for movinand reset */
    if (!pmic_set_alarm(10))
	printf("Couldn't reboot device, halting\n");
#endif

    board_power_off();
}

int board_late_init(void)
{
	if (board_detect()) {
		mxc_request_iomux(MX35_PIN_WATCHDOG_RST, MUX_CONFIG_SION |
					MUX_CONFIG_ALT1);
		printf("i.MX35 CPU board version 2.0\n");

		mxc_request_iomux(MX35_PIN_COMPARE, MUX_CONFIG_GPIO);
		mxc_iomux_set_input(MUX_IN_GPIO1_IN_5, INPUT_CTL_PATH0);
		__REG(GPIO1_BASE_ADDR + 0x04) |= 1 << 5;
		__REG(GPIO1_BASE_ADDR) |= 1 << 5;
	} else
		printf("i.MX35 CPU board version 1.0\n");

#if defined(CONFIG_PMIC_13892) && defined(CONFIG_GADGET_FILE_STORAGE)
	{
	    unsigned short voltage;
	    int ret;

	    pmic_init();

	    ret = pmic_adc_read_voltage(&voltage);
	    if (ret) {
		printf("Battery voltage: %d mV\n", voltage);
	    } else {
		printf("Battery voltage read fail!\n");
	    }
	
	    /* stop boot if battery is too low */
	    while (voltage <= CONFIG_BOOT_HALT_VOLTAGE) {

		printf("Battery voltage too low.  Please plug in a charger\n");
		ret = file_storage_enable(CONFIG_BOOT_CONTINUE_VOLTAGE);
		if (ret) {
		    printf("Can't enable charger.. shutting down\n");
		    board_power_off();
		}

		ret = pmic_adc_read_voltage(&voltage);
		if (ret) {
		    printf("Battery voltage: %d mV\n", voltage);
		} else {
		    printf("Battery voltage read fail!\n");
		}
	    }
	}
#endif
	return 0;
}
#endif

int checkboard(void)
{
	const char *sn, *rev;

	printf("Board: MX35 Luigi [ ");
	switch (__REG(CCM_BASE_ADDR + CLKCTL_RCSR) & 0x0F) {
	case 0x0000:
		printf("POR");
		break;
	case 0x0002:
		printf("JTAG");
		break;
	case 0x0004:
		printf("RST");
		break;
	case 0x0008:
		printf("WDT");
		break;
	default:
		printf("unknown");
	}
	printf("]\n");
	printf("WDOG_WCR = 0x%hx\n", WDOG_WCR);
	printf("WDOG_WMCR = 0x%hx\n", WDOG_WMCR);

	/* serial number and board id */
	sn = (char *) get_board_serial();
	rev = (char *) get_board_id16();

	if (rev)
	printf ("Board Id: %.*s\n", 16, rev);

	if (sn)
	printf ("S/N: %.*s\n", 16, sn);

	return 0;
}

#ifdef CONFIG_HW_WATCHDOG
void hw_watchdog_reset(void)
{
	/* service the watchdog */
	WDOG_WSR = 0x5555;
	WDOG_WSR = 0xaaaa;
}
#endif

inline int check_post_mode(void) 
{
	char post_mode[20];

#if defined(CONFIG_CMD_IDME)
	if (idme_get_var("postmode", post_mode, 20)) 
#endif
	{
		return -1;
	}

	if (!strncmp(post_mode, "normal", 6)) {
		setenv("post_hotkeys", "0");
	} else if (!strncmp(post_mode, "slow", 4)) {
		setenv("post_hotkeys", "1");
	} else if (!strncmp(post_mode, "factory", 7)) {
		setenv("bootdelay", "-1");
#if defined(CONFIG_CMD_IDME) && defined(CONFIG_FOR_FACTORY)
	} else if (!board_info_valid(board_id)) {
		setenv("bootdelay", "-1");
#endif
	}

	return 0;
}

#if defined(CONFIG_HANG_FEEDBACK)
#if defined(CONFIG_PMIC_13892)
#define led_init()    {pmic_init();}
#define led_2_off()   {pmic_enable_green_led(0);}
#define led_2_on()    {pmic_enable_green_led(1);}
#endif
#define morse_delay(t)	{{unsigned long msec=(t*100); while (msec--) { udelay(1000);}}}
#define short_gap()  {morse_delay(2);}
#define gap()  {led_2_off(); morse_delay(1);}
#define dit()  {led_2_on();  morse_delay(1); gap();}
#define dah()  {led_2_on();  morse_delay(3); gap();}

void hang_feedback (void)
{
    led_init();

    dit(); dit(); dit(); short_gap(); /* Morse Code S */
    dah(); dah(); dah(); short_gap(); /* Morse Code O */
    dit(); dit(); dit(); short_gap(); /* Morse Code S */
    if (!ctrlc()) {
      board_power_off();
    }
}
#endif

#ifdef CONFIG_POST
/*
 * Returns 1 if keys pressed or env variable "post_hotkeys" exists
 * to start the power-on long-running tests
 * Called from board_init_f().
 */
int post_hotkeys_pressed(void)
{
    char *value;
    int ret;

    check_post_mode();

    ret = ctrlc();
    if (!ret) {
        value = getenv("post_hotkeys");
        if (value != NULL)
	    ret = simple_strtoul(value, NULL, 10);
    }
    return ret;
}
#endif

#if defined(CONFIG_POST) || defined(CONFIG_LOGBUFFER)
void post_word_store (ulong a)
{
	volatile ulong *save_addr =
		(volatile ulong *)(CONFIG_SYS_POST_WORD_ADDR);

	*save_addr = a;
}

ulong post_word_load (void)
{
  volatile ulong *save_addr =
		(volatile ulong *)(CONFIG_SYS_POST_WORD_ADDR);

  return *save_addr;
}
#endif

#ifdef CONFIG_LOGBUFFER
unsigned long logbuffer_base(void)
{
  /* OOPS_SAVE_BASE - PAGE_SIZE in linux/include/asm-arm/arch/boot_globals.h */
  return CONFIG_SYS_SDRAM_BASE + CONFIG_SYS_SDRAM_SIZE - (3*4096);
}
#endif
