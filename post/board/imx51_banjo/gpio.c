/*
 * gpio.c 
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

#if CONFIG_POST & CONFIG_SYS_POST_GPIO

#define msdelay(t)	{{unsigned long msec=(t*10); while (msec--) { udelay(100);}}}

//#define IOMUXC_BASE_ADDR	0x73FA8000
//#define GPIO1_BASE_ADDR	0x73F84000

#define IOMUXC_SW_MUX_CTL_PAD_EIM_A24  		(IOMUXC_BASE_ADDR + 0x0bc)
#define IOMUXC_SW_MUX_CTL_PAD_EIM_CS2  		(IOMUXC_BASE_ADDR + 0x0e8)
#define IOMUXC_SW_MUX_CTL_PAD_EIM_CS3  		(IOMUXC_BASE_ADDR + 0x0ec)
#define IOMUXC_SW_MUX_CTL_PAD_EIM_CS4  		(IOMUXC_BASE_ADDR + 0x0f0)
#define IOMUXC_SW_MUX_CTL_PAD_EIM_CS5  		(IOMUXC_BASE_ADDR + 0x0f4)
#define IOMUXC_SW_MUX_CTL_PAD_GPIO1_5  		(IOMUXC_BASE_ADDR + 0x3dc)
#define IOMUXC_SW_MUX_CTL_PAD_GPIO1_6  		(IOMUXC_BASE_ADDR + 0x3e0)

#define IOMUXC_SW_PAD_CTL_PAD_EIM_A24  		(IOMUXC_BASE_ADDR + 0x450)
#define IOMUXC_SW_PAD_CTL_PAD_EIM_CS2  		(IOMUXC_BASE_ADDR + 0x47c)
#define IOMUXC_SW_PAD_CTL_PAD_EIM_CS3  		(IOMUXC_BASE_ADDR + 0x480)
#define IOMUXC_SW_PAD_CTL_PAD_EIM_CS4  		(IOMUXC_BASE_ADDR + 0x484)
#define IOMUXC_SW_PAD_CTL_PAD_EIM_CS5  		(IOMUXC_BASE_ADDR + 0x488)
#define IOMUXC_SW_PAD_CTL_PAD_GPIO1_5  		(IOMUXC_BASE_ADDR + 0x808)
#define IOMUXC_SW_PAD_CTL_PAD_GPIO1_6  		(IOMUXC_BASE_ADDR + 0x80c)

#define GPIO1_IO_MASK 		0x00000060	//GPIO1_5,6 direction
#define GPIO2_IO_MASK 		0x78040000	//GPIO2_18,27,28,29,30 direction

#define GPIO_BUTTON_VOULUME_UP	0x00000040
#define GPIO_BUTTON_VOULUME_DOWN	0x00000020
#define GPIO_BUTTON_FIVEWAY_UP	0x08000000
#define GPIO_BUTTON_FIVEWAY_DOWN	0x10000000
#define GPIO_BUTTON_FIVEWAY_LEFT	0x20000000
#define GPIO_BUTTON_FIVEWAY_RIGHT	0x40000000
#define GPIO_BUTTON_FIVEWAY_SELECT	0x00040000

static void mx_io_mux_gpio(void)
{
	__REG(IOMUXC_SW_MUX_CTL_PAD_GPIO1_5) = 0x0000;	//c22, gpio1_5
	__REG(IOMUXC_SW_PAD_CTL_PAD_GPIO1_5) = 0x01E0;
	__REG(IOMUXC_SW_MUX_CTL_PAD_GPIO1_6) = 0x0000;	//b24, gpio1_6
	__REG(IOMUXC_SW_PAD_CTL_PAD_GPIO1_6) = 0x01E0;
	__REG(IOMUXC_SW_MUX_CTL_PAD_EIM_A24) = 0x0001;	//y9, gpio2_18
	__REG(IOMUXC_SW_PAD_CTL_PAD_EIM_A24) = 0x01E0;
	__REG(IOMUXC_SW_MUX_CTL_PAD_EIM_CS2) = 0x0001;	//ae4, gpio2_27
	__REG(IOMUXC_SW_PAD_CTL_PAD_EIM_CS2) = 0x01E0;
	__REG(IOMUXC_SW_MUX_CTL_PAD_EIM_CS3) = 0x0001;	//y8, gpio2_28
	__REG(IOMUXC_SW_PAD_CTL_PAD_EIM_CS3) = 0x01E0;
	__REG(IOMUXC_SW_MUX_CTL_PAD_EIM_CS4) = 0x0001;	//ac7, gpio2_29
	__REG(IOMUXC_SW_PAD_CTL_PAD_EIM_CS4) = 0x01E0;
	__REG(IOMUXC_SW_MUX_CTL_PAD_EIM_CS5) = 0x0001;	//y7, gpio2_30
	__REG(IOMUXC_SW_PAD_CTL_PAD_EIM_CS5) = 0x01E0;
}

static void mx_io_dir_gpio(void)
{
	__REG(GPIO1_BASE_ADDR + 0x0004) &= ~GPIO1_IO_MASK;
	__REG(GPIO2_BASE_ADDR + 0x0004) &= ~GPIO2_IO_MASK;
}

int gpio_post_test(int flags)
{
	mx_io_mux_gpio();
	mx_io_dir_gpio();

	clear_ctrlc();			/* forget any previous Control C */
	int prev = disable_ctrlc(0);	/* disable Control C checking */

	printf ("Start Testing, re-boot to exit !\n");

	u32 default_key1 = __REG(GPIO1_BASE_ADDR) & GPIO1_IO_MASK;
	u32 default_key2 = __REG(GPIO2_BASE_ADDR) & GPIO2_IO_MASK;

	do {
		u32 io_key1 = (__REG(GPIO1_BASE_ADDR) & GPIO1_IO_MASK) ^ default_key1;
		u32 io_key2 = (__REG(GPIO2_BASE_ADDR) & GPIO2_IO_MASK) ^ default_key2;
#if 0
		if ((io_key1 > 0) || (io_key2 > 0))
		{
			printf("[%08x, %08x], [%08x, %08x]\n", default_key1, default_key2, io_key1, io_key2);
		}
#endif
	    	switch (io_key1)
	    	{
	    		case GPIO_BUTTON_VOULUME_UP:
				printf("Voulume Up\n");
				break;
	    		case GPIO_BUTTON_VOULUME_DOWN:
				printf("Voulume Down\n");
				break;
	    	}
	    	switch (io_key2)
	    	{
	    		case GPIO_BUTTON_FIVEWAY_UP:
				printf("Up\n");
				break;
	    		case GPIO_BUTTON_FIVEWAY_DOWN:
				printf("Down\n");
				break;
	    		case GPIO_BUTTON_FIVEWAY_LEFT:
				printf("Left\n");
				break;
	    		case GPIO_BUTTON_FIVEWAY_RIGHT:
				printf("Right\n");
				break;
	    		case GPIO_BUTTON_FIVEWAY_SELECT:
				printf("Select\n");
				break;
	    	}

		if (had_ctrlc())
			break;

		msdelay(10);
	} while (1);

	disable_ctrlc(prev);	/* restore Control C checking */

	return 0;
}

#endif /* CONFIG_POST & CONFIG_SYS_POST_GPIO */
