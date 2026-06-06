// SPDX-License-Identifier: GPL-2.0+
/*
 * SoC-specific debug UART initialization (clocks/pins) lives here
 */

#include <asm/io.h>
#include <dt-bindings/clock/qcom,gcc-msm8916.h>

extern int apq8016_clk_init_uart(phys_addr_t gcc_base, unsigned long id);

#define APQ8016_GCC_BASE		((phys_addr_t)0x01800000)

#define APQ8016_TLMM_BASE		((phys_addr_t)0x01000000)
#define APQ8016_TLMM_FUNC_BLSP_UART	(2 << 2)
#define APQ8016_TLMM_DRV_8MA		(3 << 6)
#define APQ8016_TLMM_GPIO_ENABLE	BIT(9)
#define APQ8016_GPIO_CFG(gpio)		(APQ8016_TLMM_BASE + (phys_addr_t)(gpio) * 0x1000)
#define APQ8016_DEBUG_UART_PINCFG	(APQ8016_TLMM_FUNC_BLSP_UART | \
					 APQ8016_TLMM_DRV_8MA | \
					 APQ8016_TLMM_GPIO_ENABLE)

void board_debug_uart_init(void)
{
	/* infer BLSP clock and TX pin by the configured UART base */
	switch (CONFIG_VAL(DEBUG_UART_BASE)) {
	case 0x078af000:
		apq8016_clk_init_uart(APQ8016_GCC_BASE,
				      GCC_BLSP1_UART1_APPS_CLK);
		writel(APQ8016_DEBUG_UART_PINCFG, APQ8016_GPIO_CFG(0));
		break;
	case 0x078b0000:
		apq8016_clk_init_uart(APQ8016_GCC_BASE,
				      GCC_BLSP1_UART2_APPS_CLK);
		writel(APQ8016_DEBUG_UART_PINCFG, APQ8016_GPIO_CFG(4));
		break;
	}
}
