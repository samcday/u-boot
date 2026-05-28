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
#include <errno.h>
#include <malloc.h>
#include <serial.h>
#include <watchdog.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/compiler.h>
#include <dm/pinctrl.h>

/* Serial registers - this driver works in uartdm mode*/

#define UARTDM_MR1				 0x00
#define UARTDM_MR1_RX_RDY_CTL			 BIT(7)
#define UARTDM_MR2				 0x04
#define UARTDM_MR2_8_N_1_MODE			 0x34
/*
 * This is documented on page 1817 of the apq8016e technical reference manual.
 * section 6.2.5.3.26
 *
 * The upper nybble contains the bit clock divider for the RX pin, the lower
 * nybble defines the TX pin. In almost all cases these should be the same value.
 *
 * The baud rate is the core clock frequency divided by the fixed divider value
 * programmed into this register (defined in calc_csr_bitrate()).
 */
#define UARTDM_CSR				 0x08

#define UARTDM_SR                0x08 /* Status register */
#define UARTDM_SR_RX_READY       (1 << 0) /* Receiver FIFO has data */
#define UARTDM_SR_TX_READY       (1 << 2) /* Transmitter FIFO has space */
#define UARTDM_SR_TX_EMPTY       BIT(3) /* Transmitter empty */

#define UARTDM_CR                         0x10 /* Command register */
#define UARTDM_CR_RX_ENABLE               (1 << 0) /* Enable receiver */
#define UARTDM_CR_TX_ENABLE               (1 << 2) /* Enable transmitter */
#define UARTDM_CR_CMD_RESET_RX            (1 << 4) /* Reset receiver */
#define UARTDM_CR_CMD_RESET_TX            (2 << 4) /* Reset transmitter */
#define UARTDM_CR_CMD_RESET_STALE_INT     (8 << 4) /* Reset stale interrupt */
#define UARTDM_CR_CMD_STALE_EVENT_ENABLE  (80 << 4) /* Enable stale event */
#define UARTDM_CR_CMD_RESET_TX_READY      (3 << 8) /* Reset TX ready */

#define UARTDM_ISR              0x14 /* Interrupt status register */
#define UARTDM_ISR_RXSTALE      BIT(3) /* Stale event */
#define UARTDM_IPR              0x18 /* Interrupt programming register */
#define UARTDM_IPR_STALE_LSB    0x1f
#define UARTDM_IPR_STALE_MSB    0xffffff80
#define UARTDM_IPR_STALE_SHIFT  2
#define UARTDM_IPR_STALE_KEEP   (BIT(5) | BIT(6))

#define UARTDM_DMRX             0x34 /* Number of chars to receive */
#define UARTDM_DMRX_DEF_VALUE   0xffffff /* indicates unknown recv length */
#define UARTDM_RX_TOTAL_SNAP    0x38 /* RX transfer byte snapshot */

#define UARTDM_DMEN             0x3C /* DMA/single-character mode */
#define UARTDM_NCF_TX           0x40 /* Number of chars to transmit */
#define UARTDM_TF               0x70 /* UART Transmit FIFO register */
#define UARTDM_RF               0x70 /* UART Receive FIFO register */
#define UARTDM_RF_CHAR          0xff /* higher bits contain error information */

#define UARTDM_RX_FIFO_WORD_SIZE 4
#define UARTDM_TX_FIFO_SIZE     64
#define UARTDM_DEFAULT_CLK_RATE 7372800
#define UARTDM_DEBUG_CLK_RATE \
	CONFIG_IS_ENABLED(DEBUG_UART_MSM, (CONFIG_VAL(DEBUG_UART_CLOCK)), (0))

/* Per section 6.2.5.3.6 of APQ8016E TRM, the unit of STALE_TIMEOUT is
 * "character times", which is defined as 10 times the bitrate. Our
 * bitrate is 115200, so "one character time" is:
 * 1000000/115200*10 = 86.8μs. Thus, our stale timeout choice here of 6
 * results in about a half-millisecond latency before acknowledging
 * single-char inputs. */
#define UARTDM_RX_STALE_TIMEOUT 6

DECLARE_GLOBAL_DATA_PTR;

struct msm_serial_data {
	phys_addr_t base;
	uint32_t clk_rate; /* core clock rate */
	u32 rx_word;
	unsigned int rx_word_count;
	unsigned int rx_transfer_count;
	unsigned int rx_stale_count;
};

static unsigned int msm_serial_stale_timeout(unsigned int timeout)
{
	return (timeout & UARTDM_IPR_STALE_LSB) |
		((timeout << UARTDM_IPR_STALE_SHIFT) & UARTDM_IPR_STALE_MSB);
}

static void msm_serial_start_rx(struct msm_serial_data *priv)
{
	priv->rx_transfer_count = 0;
	priv->rx_stale_count = 0;

	writel(UARTDM_DMRX_DEF_VALUE, priv->base + UARTDM_DMRX);
	writel(UARTDM_CR_CMD_STALE_EVENT_ENABLE, priv->base + UARTDM_CR);
}

static int msm_serial_pop_rx(struct msm_serial_data *priv)
{
	int c = priv->rx_word & UARTDM_RF_CHAR;

	priv->rx_word >>= 8;
	priv->rx_word_count--;

	return c;
}

static void msm_serial_load_rx_word(struct msm_serial_data *priv,
				    unsigned int count)
{
	priv->rx_word = readl(priv->base + UARTDM_RF);
	priv->rx_word_count = count;
}

static int msm_serial_load_stale_rx(struct msm_serial_data *priv)
{
	unsigned int count = priv->rx_stale_count;

	if (!count)
		return -EAGAIN;
	if (!(readl(priv->base + UARTDM_SR) & UARTDM_SR_RX_READY))
		return -EAGAIN;

	if (count > UARTDM_RX_FIFO_WORD_SIZE)
		count = UARTDM_RX_FIFO_WORD_SIZE;

	priv->rx_stale_count -= count;
	msm_serial_load_rx_word(priv, count);

	if (!priv->rx_stale_count)
		msm_serial_start_rx(priv);

	return msm_serial_pop_rx(priv);
}

static void msm_serial_capture_stale_rx(struct msm_serial_data *priv)
{
	unsigned int snap = readl(priv->base + UARTDM_RX_TOTAL_SNAP);

	if (snap > priv->rx_transfer_count)
		priv->rx_stale_count = snap - priv->rx_transfer_count;
	else
		priv->rx_stale_count = 0;

	writel(UARTDM_CR_CMD_RESET_STALE_INT, priv->base + UARTDM_CR);
	if (!priv->rx_stale_count)
		msm_serial_start_rx(priv);
}

static void msm_serial_wait_tx_empty(struct msm_serial_data *priv)
{
	while (!(readl(priv->base + UARTDM_SR) & UARTDM_SR_TX_EMPTY))
		;
}

static int msm_serial_program_tx_count(struct msm_serial_data *priv,
				       unsigned int count)
{
	if (!(readl(priv->base + UARTDM_SR) & UARTDM_SR_TX_EMPTY))
		return -EAGAIN;

	writel(UARTDM_CR_CMD_RESET_TX_READY, priv->base + UARTDM_CR);
	writel(count, priv->base + UARTDM_NCF_TX);
	readl(priv->base + UARTDM_NCF_TX);

	return 0;
}

static void msm_serial_write(struct msm_serial_data *priv, const char *s,
			     unsigned int count)
{
	unsigned int written = 0;

	while (written < count) {
		unsigned int i;
		unsigned int num_chars = count - written;
		u32 word = 0;

		if (num_chars > sizeof(word))
			num_chars = sizeof(word);

		for (i = 0; i < num_chars; i++)
			word |= (u32)(unsigned char)s[written + i] <<
				(i * 8);

		while (!(readl(priv->base + UARTDM_SR) & UARTDM_SR_TX_READY))
			;

		writel(word, priv->base + UARTDM_TF);
		written += num_chars;
	}
}

static int msm_serial_getc(struct udevice *dev)
{
	struct msm_serial_data *priv = dev_get_priv(dev);

	if (priv->rx_word_count)
		return msm_serial_pop_rx(priv);

	if (priv->rx_stale_count)
		return msm_serial_load_stale_rx(priv);

	if (readl(priv->base + UARTDM_ISR) & UARTDM_ISR_RXSTALE) {
		msm_serial_capture_stale_rx(priv);
		if (priv->rx_stale_count)
			return msm_serial_load_stale_rx(priv);
	}

	if (!(readl(priv->base + UARTDM_SR) & UARTDM_SR_RX_READY))
		return -EAGAIN;

	if (readl(priv->base + UARTDM_ISR) & UARTDM_ISR_RXSTALE) {
		msm_serial_capture_stale_rx(priv);
		if (priv->rx_stale_count)
			return msm_serial_load_stale_rx(priv);
	}

	priv->rx_transfer_count += UARTDM_RX_FIFO_WORD_SIZE;
	msm_serial_load_rx_word(priv, UARTDM_RX_FIFO_WORD_SIZE);

	return msm_serial_pop_rx(priv);
}

static int msm_serial_putc(struct udevice *dev, const char ch)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	int ret;

	ret = msm_serial_program_tx_count(priv, 1);
	if (ret)
		return ret;

	msm_serial_write(priv, &ch, 1);
	return 0;
}

static ssize_t msm_serial_puts(struct udevice *dev, const char *s, size_t len)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	unsigned int count;

	if (!len)
		return 0;

	if (len > UARTDM_TX_FIFO_SIZE)
		count = UARTDM_TX_FIFO_SIZE;
	else
		count = (unsigned int)len;
	if (msm_serial_program_tx_count(priv, count))
		return 0;

	msm_serial_write(priv, s, count);

	return count;
}

static int msm_serial_pending(struct udevice *dev, bool input)
{
	struct msm_serial_data *priv = dev_get_priv(dev);

	if (input) {
		if (priv->rx_word_count || priv->rx_stale_count)
			return 1;
		if (readl(priv->base + UARTDM_ISR) & UARTDM_ISR_RXSTALE) {
			msm_serial_capture_stale_rx(priv);
			if (priv->rx_stale_count)
				return 1;
		}
		return !!(readl(priv->base + UARTDM_SR) & UARTDM_SR_RX_READY);
	} else {
		return !(readl(priv->base + UARTDM_SR) & UARTDM_SR_TX_EMPTY);
	}
}

static const struct dm_serial_ops msm_serial_ops = {
	.putc = msm_serial_putc,
	.puts = msm_serial_puts,
	.pending = msm_serial_pending,
	.getc = msm_serial_getc,
};

static long msm_uart_clk_init(struct udevice *dev)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	struct clk clk;
	int ret;
	long rate;

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

static int calc_csr_bitrate(struct msm_serial_data *priv)
{
	/* This table is from the TRE. See the definition of UARTDM_CSR */
	unsigned int csr_div_table[] = {24576, 12288, 6144, 3072, 1536, 768, 512, 384,
					256,   192,   128,  96,   64,   48,  32,  16};
	int i = ARRAY_SIZE(csr_div_table) - 1;
	/* Currently we only support one baudrate */
	int baud = 115200;

	for (; i >= 0; i--) {
		int x = priv->clk_rate / csr_div_table[i];

		if (x == baud)
			/* Duplicate the configuration for RX
			 * as the lower nybble only configures TX
			 */
			return i + (i << 4);
	}

	return -EINVAL;
}

static void uart_dm_init(struct msm_serial_data *priv)
{
	int bitrate = calc_csr_bitrate(priv);
	unsigned int ipr;

	if (bitrate < 0) {
		log_warning("Couldn't calculate bit clock divider! Using default\n");
		/* This happens to be the value used on MSM8916 for the hardcoded clockrate
		 * in clock-apq8016. It's at least a better guess than a value we *know*
		 * is wrong...
		 */
		bitrate = 0xCC;
	}

	writel(bitrate, priv->base + UARTDM_CSR);
	/* Enable RS232 flow control to support RS232 db9 connector */
	writel(UARTDM_MR1_RX_RDY_CTL, priv->base + UARTDM_MR1);
	writel(UARTDM_MR2_8_N_1_MODE, priv->base + UARTDM_MR2);

	/*
	 * Disable DMA and single-character modes. TX/RX use packed FIFO.
	 */
	writel(0, priv->base + UARTDM_DMEN);

	priv->rx_word = 0;
	priv->rx_word_count = 0;
	priv->rx_transfer_count = 0;
	priv->rx_stale_count = 0;

	writel(UARTDM_CR_CMD_RESET_RX, priv->base + UARTDM_CR);
	writel(UARTDM_CR_CMD_RESET_TX, priv->base + UARTDM_CR);

	ipr = readl(priv->base + UARTDM_IPR) & UARTDM_IPR_STALE_KEEP;
	ipr |= msm_serial_stale_timeout(UARTDM_RX_STALE_TIMEOUT);
	writel(ipr, priv->base + UARTDM_IPR);

	writel(UARTDM_CR_RX_ENABLE, priv->base + UARTDM_CR);
	writel(UARTDM_CR_TX_ENABLE, priv->base + UARTDM_CR);

	writel(UARTDM_CR_CMD_RESET_STALE_INT, priv->base + UARTDM_CR);
	msm_serial_start_rx(priv);
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

	/* Update the clock rate to the actual programmed rate returned by the
	 * clock driver
	 */
	priv->clk_rate = rate;

	uart_dm_init(priv);

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
		priv->clk_rate = UARTDM_DEBUG_CLK_RATE;
		if (!priv->clk_rate)
			priv->clk_rate = UARTDM_DEFAULT_CLK_RATE;
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
	.priv_auto	= sizeof(struct msm_serial_data),
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

/* Uncomment to turn on UART clocks when debugging U-Boot as aboot on MSM8916 */
//int apq8016_clk_init_uart(phys_addr_t gcc_base, unsigned long id);

static inline void _debug_uart_init(void)
{
	/*
	 * Uncomment to turn on UART clocks when debugging U-Boot as aboot
	 * on MSM8916. Supported debug UART clock IDs:
	 *   - db410c: GCC_BLSP1_UART2_APPS_CLK
	 *   - HMIBSC: GCC_BLSP1_UART1_APPS_CLK
	 */
	//apq8016_clk_init_uart(0x1800000, <uart_clk_id>);
	uart_dm_init(&init_serial_data);
}

static inline void _debug_uart_putc(int ch)
{
	struct msm_serial_data *priv = &init_serial_data;
	char c = ch;

	while (msm_serial_program_tx_count(priv, 1))
		;

	msm_serial_write(priv, &c, 1);
}

DEBUG_UART_FUNCS

#endif
