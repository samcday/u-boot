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

#define IOMUXC_SW_MUX_CTL_PAD_SCKR			(IOMUXC_BASE_ADDR + 0x140)
#define IOMUXC_SW_MUX_CTL_PAD_FSR			(IOMUXC_BASE_ADDR + 0x144)
#define IOMUXC_SW_MUX_CTL_PAD_TX5_RX0		(IOMUXC_BASE_ADDR + 0x158)
#define IOMUXC_SW_MUX_CTL_PAD_ATA_DATA14		(IOMUXC_BASE_ADDR + 0x2b4)
#define IOMUXC_SW_MUX_CTL_PAD_ATA_DATA15		(IOMUXC_BASE_ADDR + 0x2b8)
#define IOMUXC_SW_MUX_CTL_PAD_ATA_INTRQ		(IOMUXC_BASE_ADDR + 0x2bc)
#define IOMUXC_SW_MUX_CTL_PAD_ATA_BUFF_EN		(IOMUXC_BASE_ADDR + 0x2c0)
#define IOMUXC_SW_MUX_CTL_PAD_ATA_DMARQ		(IOMUXC_BASE_ADDR + 0x2c4)

#define IOMUXC_SW_PAD_CTL_PAD_SCKR			(IOMUXC_BASE_ADDR + 0x584)
#define IOMUXC_SW_PAD_CTL_PAD_FSR			(IOMUXC_BASE_ADDR + 0x588)
#define IOMUXC_SW_PAD_CTL_PAD_TX5_RX0		(IOMUXC_BASE_ADDR + 0x59c)
#define IOMUXC_SW_PAD_CTL_PAD_ATA_DATA14		(IOMUXC_BASE_ADDR + 0x718)
#define IOMUXC_SW_PAD_CTL_PAD_ATA_DATA15		(IOMUXC_BASE_ADDR + 0x71c)
#define IOMUXC_SW_PAD_CTL_PAD_ATA_INTRQ		(IOMUXC_BASE_ADDR + 0x720)
#define IOMUXC_SW_PAD_CTL_PAD_ATA_BUFF_EN		(IOMUXC_BASE_ADDR + 0x724)
#define IOMUXC_SW_PAD_CTL_PAD_ATA_DMARQ		(IOMUXC_BASE_ADDR + 0x728)

#define GPIO1_IO_MASK 		0x00000430	//GPIO1_4,5,7 direction
#define GPIO2_IO_MASK 		0xd8000000	//GPIO2_27,28,29,30 direction

#define GPIO_BUTTON_VOULUME_UP	0x00000010
#define GPIO_BUTTON_VOULUME_DOWN	0x00000020
#define GPIO_BUTTON_FIVEWAY_UP	0x08000000
#define GPIO_BUTTON_FIVEWAY_DOWN	0x10000000
#define GPIO_BUTTON_FIVEWAY_LEFT	0x00000400
#define GPIO_BUTTON_FIVEWAY_RIGHT	0x40000000
#define GPIO_BUTTON_FIVEWAY_SELECT	0x80000000

static void mx_io_mux_gpio(void)
{
	__REG(IOMUXC_SW_MUX_CTL_PAD_SCKR) = 0x0005;		//k3, gpio1_4
	__REG(IOMUXC_SW_PAD_CTL_PAD_SCKR) = 0x01E0;
	__REG(IOMUXC_BASE_ADDR + 0x0850) |= 0x0001;		//IOMUXC_GPIO1_IPP_IND_G_IN_4_SELECT_INPUT
	__REG(IOMUXC_SW_MUX_CTL_PAD_FSR) = 0x0005;		//k5, gpio1_5
	__REG(IOMUXC_SW_PAD_CTL_PAD_FSR) = 0x01E0;
	__REG(IOMUXC_BASE_ADDR + 0x0854) |= 0x0001;		//IOMUXC_GPIO1_IPP_IND_G_IN5_SELECT_INPUT
	__REG(IOMUXC_SW_MUX_CTL_PAD_TX5_RX0) = 0x0005;		//j3, gpio1_10
	__REG(IOMUXC_SW_PAD_CTL_PAD_TX5_RX0) = 0x01E0;
	__REG(IOMUXC_BASE_ADDR + 0x0830) &= ~0x0001;		//IOMUXC_GPIO1_IPP_IND_G_IN_10_SELECT_INPUT
	__REG(IOMUXC_SW_MUX_CTL_PAD_ATA_DATA14) = 0x0005;	//w1, gpio2_27
	__REG(IOMUXC_SW_PAD_CTL_PAD_ATA_DATA14) = 0x01E0;
	__REG(IOMUXC_BASE_ADDR + 0x08b4) |= 0x0001;		//IOMUXC_GPIO2_IPP_IND_G_IN_27_SELECT_INPUT
	__REG(IOMUXC_SW_MUX_CTL_PAD_ATA_DATA15) = 0x0005;	//t4, gpio2_28
	__REG(IOMUXC_SW_PAD_CTL_PAD_ATA_DATA15) = 0x01E0;
	__REG(IOMUXC_BASE_ADDR + 0x08b8) |= 0x0001;		//IOMUXC_GPIO2_IPP_IND_G_IN_28_SELECT_INPUT
	__REG(IOMUXC_SW_MUX_CTL_PAD_ATA_BUFF_EN) = 0x0005;	//t5, gpio2_30
	__REG(IOMUXC_SW_PAD_CTL_PAD_ATA_BUFF_EN) = 0x01E0;
	__REG(IOMUXC_BASE_ADDR + 0x08c4) |= 0x0001;		//IOMUXC_GPIO2_IPP_IND_G_IN_30_SELECT_INPUT
	__REG(IOMUXC_SW_MUX_CTL_PAD_ATA_DMARQ) = 0x0005;		//t3, gpio2_31
	__REG(IOMUXC_SW_PAD_CTL_PAD_ATA_DMARQ) = 0x01E0;
	__REG(IOMUXC_BASE_ADDR + 0x08c8) |= 0x0001;		//IOMUXC_GPIO2_IPP_IND_G_IN_31_SELECT_INPUT
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
	    		case GPIO_BUTTON_FIVEWAY_LEFT:
				printf("Left\n");
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
