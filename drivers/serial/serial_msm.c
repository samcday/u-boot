// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm UART driver
 *
 * (C) Copyright 2015 Mateusz Kulikowski <mateusz.kulikowski@gmail.com>
 *
 * UART will work in Data Mover mode.
 * Based on Linux driver.
 */

#include <clk.h>
#include <dm.h>
#include <serial.h>
#include <watchdog.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/types.h>

#define MSM_UART_MR1						0x0000

#define MSM_UART_MR1_AUTO_RFR_LEVEL0		0x3F
#define MSM_UART_DM_MR1_AUTO_RFR_LEVEL1		0xFFFFFF00
#define MSM_UART_MR1_RX_RDY_CTL				BIT(7)
#define MSM_UART_MR1_CTS_CTL				BIT(6)

#define MSM_UART_MR2						0x0004
#define MSM_UART_MR2_BITS_PER_CHAR			0x30
#define MSM_UART_MR2_BITS_PER_CHAR_8		(0x3 << 4)
#define MSM_UART_MR2_STOP_BIT_LEN_ONE		(0x1 << 2)
#define MSM_UART_MR2_STOP_BIT_LEN_TWO		(0x3 << 2)
#define MSM_UART_MR2_PARITY_MODE_NONE		0x0
#define MSM_UART_MR2_PARITY_MODE			0x3

#define MSM_UART_CSR						0x0008

#define UARTDM_TF							0x0070

#define MSM_UART_CR							0x0010
#define MSM_UART_CR_CMD_RESET_RX			(1 << 4)
#define MSM_UART_CR_CMD_RESET_TX			(2 << 4)
#define MSM_UART_CR_CMD_RESET_ERR			(3 << 4)
#define MSM_UART_CR_CMD_RESET_BREAK_INT		(4 << 4)
#define MSM_UART_CR_CMD_RESET_CTS			(7 << 4)
#define MSM_UART_CR_CMD_RESET_STALE_INT		(8 << 4)
#define MSM_UART_CR_CMD_RESET_RFR			(14 << 4)
#define MSM_UART_CR_CMD_PROTECTION_EN		(16 << 4)
#define MSM_UART_CR_CMD_STALE_EVENT_ENABLE	(80 << 4)
#define MSM_UART_CR_CMD_FORCE_STALE			(4 << 8)
#define MSM_UART_CR_CMD_RESET_TX_READY		(3 << 8)
#define MSM_UART_CR_TX_ENABLE				BIT(2)
#define MSM_UART_CR_RX_ENABLE				BIT(0)

#define MSM_UART_IMR						0x0014

#define MSM_UART_IPR_STALE_LSB				0x1F
#define MSM_UART_DM_IPR_STALE_TIMEOUT_MSB	0xFFFFFF80

#define MSM_UART_IPR						0x0018
#define MSM_UART_TFWR						0x001C
#define MSM_UART_RFWR						0x0020

#define MSM_UART_SR							0x0008
#define MSM_UART_SR_TX_EMPTY				BIT(3)
#define MSM_UART_SR_TX_READY				BIT(2)
#define MSM_UART_SR_RX_READY				BIT(0)

#define UARTDM_RF							0x0070
#define MSM_UART_ISR						0x0014
#define MSM_UART_ISR_TX_READY				BIT(7)

#define UARTDM_RXFS							0x50
#define UARTDM_RXFS_BUF_SHIFT				0x7
#define UARTDM_RXFS_BUF_MASK				0x7

#define UARTDM_DMEN							0x3C
#define UARTDM_DMRX							0x34
#define UARTDM_NCF_TX						0x40

#define UARTDM_DMRX_DEF_VALUE				0xFFFFFF
#define UARTDM_FIFO_SIZE					64

#define MSM_UARTDM_BAUD						115200
#define MSM_UARTDM_DEFAULT_CLK_RATE			7372800
#define MSM_UARTDM_DEBUG_CLK_RATE \
	CONFIG_IS_ENABLED(DEBUG_UART_MSM, (CONFIG_VAL(DEBUG_UART_CLOCK)), (0))

struct msm_serial_data {
	phys_addr_t base;
	u32 clk_rate;
	u32 slop;
	int count;
};

struct msm_baud_map {
	u16	divisor;
	u8	code;
	u8	rxstale;
};

static void msm_write(struct msm_serial_data *priv, unsigned int val,
		      unsigned int off)
{
	writel_relaxed(val, (void __iomem *)(uintptr_t)(priv->base + off));
}

static unsigned int msm_read(struct msm_serial_data *priv, unsigned int off)
{
	return readl_relaxed((void __iomem *)(uintptr_t)(priv->base + off));
}

static inline void msm_wait_for_xmitr(struct msm_serial_data *priv)
{
	unsigned int timeout = 500000;

	while (!(msm_read(priv, MSM_UART_SR) & MSM_UART_SR_TX_EMPTY)) {
		if (msm_read(priv, MSM_UART_ISR) & MSM_UART_ISR_TX_READY)
			break;
		udelay(1);
		schedule();
		if (!timeout--)
			break;
	}
	msm_write(priv, MSM_UART_CR_CMD_RESET_TX_READY, MSM_UART_CR);
}

static void msm_reset_dm_count(struct msm_serial_data *priv, int count)
{
	msm_wait_for_xmitr(priv);
	msm_write(priv, count, UARTDM_NCF_TX);
	msm_read(priv, UARTDM_NCF_TX);
}

static void msm_reset(struct msm_serial_data *priv)
{
	unsigned int mr;

	/* reset everything */
	msm_write(priv, MSM_UART_CR_CMD_RESET_RX, MSM_UART_CR);
	msm_write(priv, MSM_UART_CR_CMD_RESET_TX, MSM_UART_CR);
	msm_write(priv, MSM_UART_CR_CMD_RESET_ERR, MSM_UART_CR);
	msm_write(priv, MSM_UART_CR_CMD_RESET_BREAK_INT, MSM_UART_CR);
	msm_write(priv, MSM_UART_CR_CMD_RESET_CTS, MSM_UART_CR);
	msm_write(priv, MSM_UART_CR_CMD_RESET_RFR, MSM_UART_CR);
	mr = msm_read(priv, MSM_UART_MR1);
	mr &= ~MSM_UART_MR1_RX_RDY_CTL;
	msm_write(priv, mr, MSM_UART_MR1);

	/* Disable DM modes */
	msm_write(priv, 0, UARTDM_DMEN);
}

static const struct msm_baud_map *
msm_find_best_baud(struct msm_serial_data *priv, unsigned int baud)
{
	const struct msm_baud_map *entry, *end, *best;
	unsigned long best_diff = ~0UL;
	static const struct msm_baud_map table[] = {
		{    1, 0xff, 31 },
		{    2, 0xee, 16 },
		{    3, 0xdd,  8 },
		{    4, 0xcc,  6 },
		{    6, 0xbb,  6 },
		{    8, 0xaa,  6 },
		{   12, 0x99,  6 },
		{   16, 0x88,  1 },
		{   24, 0x77,  1 },
		{   32, 0x66,  1 },
		{   48, 0x55,  1 },
		{   96, 0x44,  1 },
		{  192, 0x33,  1 },
		{  384, 0x22,  1 },
		{  768, 0x11,  1 },
		{ 1536, 0x00,  1 },
	};

	best = table; /* Default to smallest divider */
	end = table + ARRAY_SIZE(table);
	for (entry = table; entry < end; entry++) {
		unsigned int result = priv->clk_rate / entry->divisor / 16;
		unsigned long diff;

		if (result > baud)
			diff = result - baud;
		else
			diff = baud - result;

		if (diff < best_diff) {
			best_diff = diff;
			best = entry;
		}

		if (!diff)
			break;
	}

	return best;
}

static void msm_set_baud_rate(struct msm_serial_data *priv, unsigned int baud)
{
	unsigned int rxstale, watermark, mask;
	const struct msm_baud_map *entry;

	entry = msm_find_best_baud(priv, baud);
	msm_write(priv, entry->code, MSM_UART_CSR);

	/* RX stale watermark */
	rxstale = entry->rxstale;
	watermark = MSM_UART_IPR_STALE_LSB & rxstale;
	mask = MSM_UART_DM_IPR_STALE_TIMEOUT_MSB;
	watermark |= mask & (rxstale << 2);

	msm_write(priv, watermark, MSM_UART_IPR);

	/* set RX watermark */
	watermark = (UARTDM_FIFO_SIZE * 3) / 4;
	msm_write(priv, watermark, MSM_UART_RFWR);

	/* set TX watermark */
	msm_write(priv, 10, MSM_UART_TFWR);

	msm_write(priv, MSM_UART_CR_CMD_PROTECTION_EN, MSM_UART_CR);
	msm_reset(priv);

	/* Enable RX and TX */
	msm_write(priv, MSM_UART_CR_TX_ENABLE | MSM_UART_CR_RX_ENABLE, MSM_UART_CR);

	msm_write(priv, 0, MSM_UART_IMR);
	msm_write(priv, MSM_UART_CR_CMD_RESET_STALE_INT, MSM_UART_CR);
	msm_write(priv, UARTDM_DMRX_DEF_VALUE, UARTDM_DMRX);
	msm_write(priv, MSM_UART_CR_CMD_STALE_EVENT_ENABLE, MSM_UART_CR);
}

static void msm_set_8n1(struct msm_serial_data *priv)
{
	unsigned int mr;

	mr = msm_read(priv, MSM_UART_MR2);
	mr &= ~MSM_UART_MR2_PARITY_MODE;
	mr |= MSM_UART_MR2_PARITY_MODE_NONE;
	mr &= ~MSM_UART_MR2_BITS_PER_CHAR;
	mr |= MSM_UART_MR2_BITS_PER_CHAR_8;
	mr &= ~(MSM_UART_MR2_STOP_BIT_LEN_ONE | MSM_UART_MR2_STOP_BIT_LEN_TWO);
	mr |= MSM_UART_MR2_STOP_BIT_LEN_ONE;
	msm_write(priv, mr, MSM_UART_MR2);
}

static void msm_init_uartdm(struct msm_serial_data *priv)
{
	unsigned int data, rfr_level, mask;

	msm_wait_for_xmitr(priv);

	priv->slop = 0;
	priv->count = 0;

	if (UARTDM_FIFO_SIZE > 12)
		rfr_level = UARTDM_FIFO_SIZE - 12;
	else
		rfr_level = UARTDM_FIFO_SIZE;

	/* set automatic RFR level */
	data = msm_read(priv, MSM_UART_MR1);
	mask = MSM_UART_DM_MR1_AUTO_RFR_LEVEL1;
	data &= ~mask;
	data &= ~MSM_UART_MR1_AUTO_RFR_LEVEL0;
	data |= mask & (rfr_level << 2);
	data |= MSM_UART_MR1_AUTO_RFR_LEVEL0 & rfr_level;
	msm_write(priv, data, MSM_UART_MR1);

	msm_set_baud_rate(priv, MSM_UARTDM_BAUD);
	msm_set_8n1(priv);

	data = msm_read(priv, MSM_UART_MR1);
	data &= ~(MSM_UART_MR1_CTS_CTL | MSM_UART_MR1_RX_RDY_CTL);
	msm_write(priv, data, MSM_UART_MR1);
}

static int msm_poll_get_char_dm(struct msm_serial_data *priv)
{
	int c;
	unsigned char *sp = (unsigned char *)&priv->slop;

	/* Check if a previous read had more than one char */
	if (priv->count) {
		c = sp[sizeof(priv->slop) - priv->count];
		priv->count--;
	/* Or if FIFO is empty */
	} else if (!(msm_read(priv, MSM_UART_SR) & MSM_UART_SR_RX_READY)) {
		/*
		 * If RX packing buffer has less than a word, force stale to
		 * push contents into RX FIFO
		 */
		priv->count = msm_read(priv, UARTDM_RXFS);
		priv->count = (priv->count >> UARTDM_RXFS_BUF_SHIFT) &
			      UARTDM_RXFS_BUF_MASK;
		if (priv->count) {
			msm_write(priv, MSM_UART_CR_CMD_FORCE_STALE, MSM_UART_CR);
			priv->slop = msm_read(priv, UARTDM_RF);
			c = sp[0];
			priv->count--;
			msm_write(priv, MSM_UART_CR_CMD_RESET_STALE_INT, MSM_UART_CR);
			msm_write(priv, UARTDM_DMRX_DEF_VALUE, UARTDM_DMRX);
			msm_write(priv, MSM_UART_CR_CMD_STALE_EVENT_ENABLE,
				  MSM_UART_CR);
		} else {
			c = -EAGAIN;
		}
	/* FIFO has a word */
	} else {
		priv->slop = msm_read(priv, UARTDM_RF);
		c = sp[0];
		priv->count = sizeof(priv->slop) - 1;
	}

	return c;
}

static int msm_poll_get_char(struct msm_serial_data *priv)
{
	u32 imr;
	int c;

	/* Disable all interrupts */
	imr = msm_read(priv, MSM_UART_IMR);
	msm_write(priv, 0, MSM_UART_IMR);

	c = msm_poll_get_char_dm(priv);

	/* Enable interrupts */
	msm_write(priv, imr, MSM_UART_IMR);

	return c;
}

static void msm_poll_put_char(struct msm_serial_data *priv, unsigned char c)
{
	u32 imr;

	/* Disable all interrupts */
	imr = msm_read(priv, MSM_UART_IMR);
	msm_write(priv, 0, MSM_UART_IMR);

	msm_reset_dm_count(priv, 1);

	/* Wait until FIFO is empty */
	while (!(msm_read(priv, MSM_UART_SR) & MSM_UART_SR_TX_READY))
		schedule();

	/* Write a character */
	msm_write(priv, c, UARTDM_TF);

	/* Wait until FIFO is empty */
	while (!(msm_read(priv, MSM_UART_SR) & MSM_UART_SR_TX_READY))
		schedule();

	/* Enable interrupts */
	msm_write(priv, imr, MSM_UART_IMR);
}

static int msm_serial_getc(struct udevice *dev)
{
	struct msm_serial_data *priv = dev_get_priv(dev);

	return msm_poll_get_char(priv);
}

static int msm_serial_putc(struct udevice *dev, const char ch)
{
	struct msm_serial_data *priv = dev_get_priv(dev);

	msm_poll_put_char(priv, ch);

	return 0;
}

static int msm_serial_pending(struct udevice *dev, bool input)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	int count;

	if (!input)
		return !(msm_read(priv, MSM_UART_SR) & MSM_UART_SR_TX_EMPTY);

	if (priv->count)
		return priv->count;

	if (msm_read(priv, MSM_UART_SR) & MSM_UART_SR_RX_READY)
		return 1;

	count = msm_read(priv, UARTDM_RXFS);
	count = (count >> UARTDM_RXFS_BUF_SHIFT) & UARTDM_RXFS_BUF_MASK;

	return count;
}

static const struct dm_serial_ops msm_serial_ops = {
	.putc = msm_serial_putc,
	.pending = msm_serial_pending,
	.getc = msm_serial_getc,
};

static long msm_uart_clk_init(struct udevice *dev)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	struct clk clk;
	long rate;
	int ret;

	ret = clk_get_by_name(dev, "core", &clk);
	if (ret < 0) {
		log_debug("%s: Failed to get clock: %d, using %u Hz\n",
			  __func__, ret, priv->clk_rate);
		return priv->clk_rate;
	}

	rate = clk_set_rate(&clk, priv->clk_rate);
	if (rate <= 0) {
		log_debug("%s: Failed to set clock: %ld, using %u Hz\n",
			  __func__, rate, priv->clk_rate);
		return priv->clk_rate;
	}

	return rate;
}

static int msm_serial_probe(struct udevice *dev)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	long rate;

	/* No need to reinitialize the UART after relocation */
	if (gd->flags & GD_FLG_RELOC)
		return 0;

	rate = msm_uart_clk_init(dev);
	if (rate < 0)
		return rate;
	if (!rate) {
		log_err("Got core clock rate of 0... Please fix your clock driver\n");
		return -EINVAL;
	}

	priv->clk_rate = rate;
	msm_init_uartdm(priv);

	return 0;
}

static int msm_serial_of_to_plat(struct udevice *dev)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	int ret;

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	ret = dev_read_u32(dev, "clock-frequency", &priv->clk_rate);
	if (ret < 0) {
		log_debug("No clock frequency specified, using default rate\n");
		/* Keep DM serial at the same rate as the early debug UART. */
		priv->clk_rate = MSM_UARTDM_DEBUG_CLK_RATE;
		if (!priv->clk_rate)
			priv->clk_rate = MSM_UARTDM_DEFAULT_CLK_RATE;
	}

	return 0;
}

static const struct udevice_id msm_serial_ids[] = {
	{ .compatible = "qcom,msm-uartdm" },
	{ }
};

U_BOOT_DRIVER(serial_msm) = {
	.name	= "serial_msm",
	.id	= UCLASS_SERIAL,
	.of_match = msm_serial_ids,
	.of_to_plat = msm_serial_of_to_plat,
	.priv_auto = sizeof(struct msm_serial_data),
	.probe = msm_serial_probe,
	.ops	= &msm_serial_ops,
	.flags = DM_FLAG_PRE_RELOC,
};

#ifdef CONFIG_DEBUG_UART_MSM

static struct msm_serial_data init_serial_data = {
	.base = CONFIG_VAL(DEBUG_UART_BASE),
	.clk_rate = CONFIG_VAL(DEBUG_UART_CLOCK),
};

#include <debug_uart.h>

static inline void _debug_uart_init(void)
{
	msm_init_uartdm(&init_serial_data);
}

static inline void _debug_uart_putc(int ch)
{
	msm_poll_put_char(&init_serial_data, ch);
}

DEBUG_UART_FUNCS

#endif
