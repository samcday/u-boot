/*
 * inventory.c 
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

#include <asm/arch/iomux.h>
#include <common.h>
#include <post.h>
#include "mx50_i2c.h"
#ifdef CONFIG_PMIC
#include <pmic.h>
#endif

// Taken from platform/include/boardid.h
#define BOARD_ID_N              3
#define BOARD_ID_REV_N          5
#define BOARD_ID_REV_LEN        BOARD_ID_REV_N - BOARD_ID_N

#define BOARD_ID_YOSHI                "000"
#define BOARD_ID_YOSHI_3              "00003"
#define BOARD_ID_TEQUILA              "003"
#define BOARD_ID_TEQUILA_EVT1         "00301"
#define BOARD_ID_FINKLE               "004"
#define BOARD_ID_WHITNEY              "005"
#define BOARD_ID_WHITNEY_EVT1         "00512"
#define BOARD_ID_WHITNEY_WFO          "006"
#define BOARD_ID_WHITNEY_WFO_EVT1     "00601"

#define BOARD_IS_(id, b, n)	(strncmp((id), (b), (n)) == 0)
#define BOARD_REV_GRT_(id, b) (strncmp((id+BOARD_ID_N), (b+BOARD_ID_N), BOARD_ID_REV_LEN) > 0)
#define BOARD_REV_GRT_EQ_(id, b) (strncmp((id+BOARD_ID_N), (b+BOARD_ID_N), BOARD_ID_REV_LEN) >= 0)

#define BOARD_REV_GREATER(id, b) (BOARD_IS_((id), (b), BOARD_ID_N) && BOARD_REV_GRT_((id), (b)))
#define BOARD_REV_GREATER_EQ(id, b) (BOARD_IS_((id), (b), BOARD_ID_N) && BOARD_REV_GRT_EQ_((id), (b)))

#define BOARD_IS_YOSHI(id)         BOARD_IS_((id), BOARD_ID_YOSHI,         BOARD_ID_N)
#define BOARD_IS_TEQUILA(id)       BOARD_IS_((id), BOARD_ID_TEQUILA,       BOARD_ID_N)
#define BOARD_IS_FINKLE(id)        BOARD_IS_((id), BOARD_ID_FINKLE,        BOARD_ID_N)
#define BOARD_IS_WHITNEY(id)       BOARD_IS_((id), BOARD_ID_WHITNEY,       BOARD_ID_N)
#define BOARD_IS_WHITNEY_WFO(id)   BOARD_IS_((id), BOARD_ID_WHITNEY_WFO,   BOARD_ID_N)

#define I2C_ADDR_ACCELEROMTER 0x1C // bus 0
#define I2C_ADDR_PROXIMITY    0x0D // bus 0
#define I2C_ADDR_BATTERY      0x55 // bus 1
#define I2C_ADDR_CODEC        0x1A // bus 1
#define I2C_ADDR_USBMUX       0x35 // bus 1
#define I2C_ADDR_PAPYRUS      0x48 // bus 1
#define I2C_ADDR_NEONODE      0x50 // bus 2

extern const u8 *get_board_id16(void);

#if CONFIG_POST

int check_battery (void)
{
	uchar reg = 0;
	int ret, bus;

	printf("Battery ");
	bus = i2c_get_bus_num();
	i2c_set_bus_num(1);
	ret = i2c_read(I2C_ADDR_BATTERY, 0x7E, 1, &reg, 1);	//0x6c, yoshi 1.0
	if (ret)
	{
		printf("is missing\n");
	}
	else
	{
		printf("ID = 0x%x\n", reg);
	}
	i2c_set_bus_num(bus);
	return ret;
}

int check_codec (void)
{
	unsigned int reg = 0;
	int ret, bus;

	printf("Codec ");
	bus = i2c_get_bus_num();
	i2c_set_bus_num(1);
	ret = i2c_read(I2C_ADDR_CODEC, 0x01, 2, (uchar*)&reg, 2);	//0x049f, wm8962
	if (ret)
	{
		printf("is missing\n");
	}
	else
	{
		printf("ID = 0x%x\n", reg);
	}
	i2c_set_bus_num(bus);
	return ret;
}

int check_usbmux (void)
{
	unsigned int reg = 0;
	int ret, bus;

	printf("USB Mux ");
	bus = i2c_get_bus_num();
	i2c_set_bus_num(1);
	ret = i2c_read(I2C_ADDR_USBMUX, 0x00, 1, (uchar*)&reg, 1); //Chip ID and Rev, AL32
	if (ret)
	{
		printf("is missing\n");
	}
	else
	{
		printf("ID = 0x%x\n", reg);
	}
	i2c_set_bus_num(bus);
	return ret;
}

int check_papyrus (void)
{
	uchar reg = 0;
	int ret, bus;

	printf("Papyrus ");

	/* Set papyrus wakeup bit */
	mxc_request_iomux(MX50_PIN_EPDC_PWRCTRL0, IOMUX_CONFIG_GPIO);
	mxc_iomux_set_pad(MX50_PIN_EPDC_PWRCTRL0,
		PAD_CTL_PKE_NONE |
		PAD_CTL_ODE_OPENDRAIN_NONE |
		PAD_CTL_DRV_HIGH);
	mx50_gpio_direction(IOMUX_TO_GPIO(MX50_PIN_EPDC_PWRCTRL0), MX50_GPIO_DIRECTION_OUT);
	mx50_gpio_set(IOMUX_TO_GPIO(MX50_PIN_EPDC_PWRCTRL0), 1);

	udelay(2500); // wait for it to wake up

	bus = i2c_get_bus_num();
	i2c_set_bus_num(1);
	ret = i2c_read(I2C_ADDR_PAPYRUS, 0x10, 1, &reg, 1);
	if (ret)
	{
		printf("is missing\n");
	}
	else
	{
		printf("ID = 0x%x\n", reg);
	}
	i2c_set_bus_num(bus);

	mx50_gpio_set(IOMUX_TO_GPIO(MX50_PIN_EPDC_PWRCTRL0), 0);

	return ret;
}

int check_accelerometer (void)
{
	uchar reg = 0;
	int ret, bus;

	printf("Accelerometer ");
	bus = i2c_get_bus_num();
	i2c_set_bus_num(0);
	ret = i2c_read(I2C_ADDR_ACCELEROMTER, 0x0D, 1, &reg, 1);
	if (ret)
	{
		printf("is missing\n");
	}
	else
	{
		printf("ID = 0x%x\n", reg);
	}
	i2c_set_bus_num(bus);
	return ret;
}

int check_proximity (void)
{
	uchar reg = 0;
	int ret, bus;

	printf("Proximity ");
	bus = i2c_get_bus_num();
	i2c_set_bus_num(0);
	ret = i2c_read(I2C_ADDR_PROXIMITY, 0x40, 1, &reg, 1);
	if (ret)
	{
		printf("is missing\n");
	}
	else
	{
		printf("ID = 0x%x\n", reg);
	}
	i2c_set_bus_num(bus);
	return ret;
}

int check_touch (void)
{
	uchar resp[2];
	int i, ret, bus;

	printf("Touch ");
	memset(resp, 0, sizeof(resp));
	bus = i2c_get_bus_num();
	i2c_set_bus_num(2);
	ret = i2c_read(I2C_ADDR_NEONODE, 0x0a, sizeof(resp), &resp[0], sizeof(resp));
	if (ret)
	{
		printf("is missing\n");
	}
	else
	{
		printf("ID =");
		for (i = 0; i < sizeof(resp); i++) {
			printf(" %02x", resp[i]);
		}
		printf("\n");
	}
	i2c_set_bus_num(bus);
	return ret;
}

int check_power (void)
{
#ifdef CONFIG_PMIC
	unsigned int val = 0;
#endif
	int ret;

	printf("PMIC ");
#ifdef CONFIG_PMIC
	ret = pmic_read_reg(7, &val);
	if (ret)
	{
		printf("ID = 0x%x\n", val);
		ret = 0;
	}
	else
#endif
	{
		printf("is missing\n");
		ret = 1;
	}
	return ret;
}

int inventory_post_test (int flags)
{
	const char *rev;
	int ret = 0;

	/* Set i2c2 enable line */
	mxc_request_iomux(MX50_PIN_SSI_RXC, IOMUX_CONFIG_GPIO);
	mxc_iomux_set_pad(MX50_PIN_SSI_RXC,
		PAD_CTL_PKE_NONE |
		PAD_CTL_ODE_OPENDRAIN_NONE |
		PAD_CTL_DRV_MAX);
	mx50_gpio_direction(IOMUX_TO_GPIO(MX50_PIN_SSI_RXC), MX50_GPIO_DIRECTION_OUT);
	mx50_gpio_set(IOMUX_TO_GPIO(MX50_PIN_SSI_RXC), 1);

	rev = (char *) get_board_id16();
	if (BOARD_IS_YOSHI(rev))  // Only on BOARD_ID_YOSHI boards
	{
		ret  = check_battery();  // Battery may not be installed at SMT station
		ret |= check_power();
		ret |= check_papyrus();
		ret |= check_codec();
		if (BOARD_REV_GREATER(rev, BOARD_ID_YOSHI_3))  // Only on BOARD_ID_YOSHI boards after version 3.x
		{
			ret |= check_accelerometer();
			ret |= check_proximity();
		}
	}

	if (BOARD_IS_TEQUILA(rev))  // Only on BOARD_ID_TEQUILA boards
	{
		ret  = check_battery();  // Battery may not be installed at SMT station
		ret |= check_power();
		ret |= check_papyrus();

		if (BOARD_REV_GREATER(rev, BOARD_ID_TEQUILA_EVT1))  // Only on BOARD_ID_TEQUILA boards after EVT1
		{
			ret |= check_usbmux();
		}
	}

	if (BOARD_IS_FINKLE(rev))  // Only on BOARD_ID_FINKLE boards
	{
		ret  = check_battery();  // Battery may not be installed at SMT station
		ret  = check_power();
		ret |= check_papyrus();
		ret |= check_codec();
		ret |= check_accelerometer();
	}

	if (BOARD_IS_WHITNEY(rev))  // Only on Whitney WAN boards
	{
   		ret  = check_proximity(); // Proximity sensor is still being debated
		ret  = check_touch();       // Neonode are not preprogrammed
		ret  = check_battery();  // Battery may not be installed at SMT station
		ret |= check_power();
		ret |= check_papyrus();
		ret |= check_codec();
		ret |= check_accelerometer();
		if (BOARD_REV_GREATER(rev, BOARD_ID_WHITNEY_EVT1))  // Only on Whitney boards after EVT1
		{
			ret |= check_usbmux();
		}
	}

	if (BOARD_IS_WHITNEY_WFO(rev))  // Only on Whitney WFO boards
	{
   		ret  = check_proximity();   // Proximity sensor is still being debated
		ret  = check_touch();       // Neonode are not preprogrammed
		ret  = check_battery();     // Battery may not be installed at SMT station
		ret |= check_power();
		ret |= check_papyrus();
		ret |= check_codec();
		ret |= check_accelerometer();
		if (BOARD_REV_GREATER(rev, BOARD_ID_WHITNEY_WFO_EVT1))  // Only on Whitney WFO boards after EVT1
		{
			ret |= check_usbmux();
		}
	}

//	mx50_gpio_set(IOMUX_TO_GPIO(MX50_PIN_SSI_RXC), 0);

	return ret;
}

#endif /* CONFIG_POST */
