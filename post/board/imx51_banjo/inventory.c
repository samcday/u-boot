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

#include <common.h>
#include <post.h>
#include "mx51_i2c.h"
#include <pmic_13892.h>

#if CONFIG_POST & CONFIG_SYS_POST_GPIO

int check_battery (void)
{
	uchar reg = 0;
	int ret, bus;

	printf("Battery ");
	bus = i2c_get_bus_num();
	i2c_set_bus_num(2);
	ret = i2c_read(0x55, 0x7E, 1, &reg, 1);
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
	uchar reg = 0;
	int ret, bus;

	printf("Codec ");
	bus = i2c_get_bus_num();
	i2c_set_bus_num(1);
	ret = i2c_read(0x18, 0x32, 1, &reg, 1);
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

int check_accelerometer (void)
{
	uchar reg = 0;
	int ret, bus;

	printf("Accelerometer ");
	bus = i2c_get_bus_num();
	i2c_set_bus_num(2);
	ret = i2c_read(0x19, 0x0F, 1, &reg, 1);
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

int check_power (void)
{
    unsigned int val = 0;
	int ret;

	printf("PMIC ");
	ret = pmic_read_reg(7, &val);
	if (ret)
	{
		printf("ID = 0x%x\n", val);
		ret = 0;
	}
	else
	{
		printf("is missing\n");
		ret = 1;
	}
	return ret;
}

int inventory_post_test (int flags)
{
	int ret = 0;

	ret |= check_power();
	ret |= check_battery();
	ret |= check_codec();
	ret |= check_accelerometer();

	return ret;
}

#endif /* CONFIG_POST & CONFIG_SYS_POST_GPIO */
