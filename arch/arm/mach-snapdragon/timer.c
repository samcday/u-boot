// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal MSM8960 timer setup for 32-bit Snapdragon bring-up.
 */

#include <init.h>
#include <asm/io.h>

#define MSM8960_TMR_BASE		0x0200a000
#define MSM8960_TMR0_DGT_BASE		0x0208a024

#define TIMER_ENABLE			0x0008
#define DGT_CLK_CTL			0x0034

#define DGT_CLK_CTL_DIV_4		3
#define TIMER_ENABLE_EN			1

int timer_init(void)
{
	/* MSM8960 uses PXO / 4 for the debug timer, giving 6.75 MHz. */
	writel(DGT_CLK_CTL_DIV_4, MSM8960_TMR_BASE + DGT_CLK_CTL);
	writel(TIMER_ENABLE_EN, MSM8960_TMR0_DGT_BASE + TIMER_ENABLE);

	return 0;
}
