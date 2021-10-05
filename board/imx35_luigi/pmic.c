/*
 * pmic.c 
 *
 * Copyright 2010 Amazon Technologies, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file pmic.c
 * @brief This file contains mx51 board-specific info to talk to the MC13892 PMIC.
 *
 */

#include <common.h>
#include <pmic_13892.h>
#include <asm/arch/mx35_pins.h>
#include <asm/arch/iomux.h>
#include <asm/arch/mx35_gpio.h>

#define MXC_PMIC_FRAME_MASK		0x00FFFFFF
#define MXC_PMIC_MAX_REG_NUM		0x3F
#define MXC_PMIC_REG_NUM_SHIFT		0x19
#define MXC_PMIC_WRITE_BIT_SHIFT	31

extern void cspi_init(void);
extern int cspi_write(u32 d);
extern int cspi_read(u32 *d);

int board_pmic_init(void) {

    cspi_init();

    mxc_request_iomux(MX35_PIN_CSI_D12, MUX_CONFIG_ALT5);

    return 1;
}

int board_pmic_write_reg(int reg, const unsigned int val) 
{
    int ret;
    unsigned int frame = 0;

    frame |= (1 << MXC_PMIC_WRITE_BIT_SHIFT);
    frame |= reg << MXC_PMIC_REG_NUM_SHIFT;
    frame |= val & MXC_PMIC_FRAME_MASK;

    ret = cspi_write(frame);
    if (!ret)
	return ret;

    /* discard read result */
    cspi_read(&frame);

    return 1;
}

int board_pmic_read_reg(int reg, unsigned int *val) 
{
    int ret;
    unsigned int frame = 0;
    
    *val = 0;

    frame |= reg << MXC_PMIC_REG_NUM_SHIFT;
    ret = cspi_write(frame);
    if (!ret) 
	return ret;

    ret = cspi_read(&frame);
    *val = frame & MXC_PMIC_FRAME_MASK;

    return 1;
}

int board_enable_green_led(int enable) 
{
    int pad_val = PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW | PAD_CTL_DRV_1_8V |
	    PAD_CTL_HYS_CMOS | PAD_CTL_PKE_NONE | PAD_CTL_100K_PU;

    mx35_gpio_direction(IOMUX_TO_GPIO(MX35_PIN_CSI_D12), MX35_GPIO_DIRECTION_OUT);

    /* shasta/luigi controls led w/ gpio */
    if (enable) {
	mx35_gpio_set(IOMUX_TO_GPIO(MX35_PIN_CSI_D12), 1);
    } else {
	mx35_gpio_set(IOMUX_TO_GPIO(MX35_PIN_CSI_D12), 0);
    }

    mxc_iomux_set_pad(MX35_PIN_CSI_D12, pad_val);

    return 1;
}
