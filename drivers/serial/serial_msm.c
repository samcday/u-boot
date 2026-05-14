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

/* Serial registers - this driver works in uartdm mode */

#define UARTDM_DMEN_TXRX_SC_ENABLE	(BIT(4) | BIT(5))

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
#define UARTDM_SR_RX_READY       (1 << 0) /* Receiver FIFO has data */
#define UARTDM_SR_TX_READY       (1 << 2) /* Transmitter FIFO has space */
#define UARTDM_SR_TX_EMPTY       (1 << 3) /* Transmitter underrun */

#define UARTDM_CR_RX_ENABLE               (1 << 0) /* Enable receiver */
#define UARTDM_CR_TX_ENABLE               (1 << 2) /* Enable transmitter */
#define UARTDM_CR_CMD_RESET_RX            (1 << 4) /* Reset receiver */
#define UARTDM_CR_CMD_RESET_TX            (2 << 4) /* Reset transmitter */
#define UARTDM_CR_CMD_RESET_ERR           (3 << 4) /* Reset error status */
#define UARTDM_CR_CMD_RESET_STALE_INT     (8 << 4) /* Reset stale interrupt */
#define UARTDM_CR_CMD_FORCE_STALE         (4 << 8) /* Flush RX packing buffer */
#define UARTDM_CR_CMD_STALE_EVENT_ENABLE  (5 << 8) /* Enable stale event */

#define UARTDM_IPR_STALE_TIMEOUT_LSB      0x0f
#define UARTDM_DMRX_DEF_VALUE             0xffffff
#define UARTDM_RXFS_BUF_SHIFT             7
#define UARTDM_RXFS_BUF_MASK              0x7

#define UARTDM_RF_CHAR          0xff /* higher bits contain error information */

#define GSBI_CTRL_REG_PROTOCOL_SHIFT	4

struct msm_serial_regs {
	u32 csr;
	u32 sr;
	u32 cr;
	u32 tf;
	u32 rf;
	u32 ipr;
	u32 tfwr;
	u32 rfwr;
	u32 dmrx;
	u32 rxfs;
	u32 dmen;
	u32 ncf_tx;
	u32 fixed_csr_115200;
};

static const struct msm_serial_regs uartdm_v13_regs = {
	.csr = 0x08,
	.sr = 0x08,
	.cr = 0x10,
	.tf = 0x70,
	.rf = 0x70,
	.ipr = 0x18,
	.tfwr = 0x1c,
	.rfwr = 0x20,
	.dmrx = 0x34,
	.rxfs = 0x50,
	.dmen = 0x3c,
	.ncf_tx = 0x40,
	.fixed_csr_115200 = 0xff,
};

static const struct msm_serial_regs uartdm_v14_regs = {
	.csr = 0xa0,
	.sr = 0xa4,
	.cr = 0xa8,
	.tf = 0x100,
	.rf = 0x140,
	.dmen = 0x3c,
};

DECLARE_GLOBAL_DATA_PTR;

struct msm_serial_data {
	phys_addr_t base;
	fdt_addr_t gsbi_base;
	const struct msm_serial_regs *regs;
	uint32_t clk_rate; /* core clock rate */
	u32 rx_word;
	u32 rx_count;
};

static void msm_serial_reset_stale(struct msm_serial_data *priv)
{
	writel(UARTDM_CR_CMD_RESET_STALE_INT, priv->base + priv->regs->cr);
	writel(UARTDM_DMRX_DEF_VALUE, priv->base + priv->regs->dmrx);
	writel(UARTDM_CR_CMD_STALE_EVENT_ENABLE, priv->base + priv->regs->cr);
}

static bool msm_serial_flush_rx(struct msm_serial_data *priv)
{
	u32 count;

	if (!priv->regs->rxfs)
		return false;

	count = readl(priv->base + priv->regs->rxfs);
	count = (count >> UARTDM_RXFS_BUF_SHIFT) & UARTDM_RXFS_BUF_MASK;
	if (!count)
		return false;

	writel(UARTDM_CR_CMD_FORCE_STALE, priv->base + priv->regs->cr);
	priv->rx_word = readl(priv->base + priv->regs->rf);
	priv->rx_count = count > sizeof(priv->rx_word) ? sizeof(priv->rx_word) : count;
	msm_serial_reset_stale(priv);

	return true;
}

static int msm_serial_pop_rx(struct msm_serial_data *priv)
{
	int c;

	if (!priv->rx_count)
		return -EAGAIN;

	c = priv->rx_word & UARTDM_RF_CHAR;
	priv->rx_word >>= 8;
	priv->rx_count--;

	return c;
}

static int msm_serial_getc(struct udevice *dev)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	int c;

	c = msm_serial_pop_rx(priv);
	if (c >= 0)
		return c;

	if (!(readl(priv->base + priv->regs->sr) & UARTDM_SR_RX_READY) &&
	    !msm_serial_flush_rx(priv))
		return -EAGAIN;

	if (!priv->rx_count) {
		priv->rx_word = readl(priv->base + priv->regs->rf);
		priv->rx_count = sizeof(priv->rx_word);
	}

	return msm_serial_pop_rx(priv);
}

static int msm_serial_putc(struct udevice *dev, const char ch)
{
	struct msm_serial_data *priv = dev_get_priv(dev);

	if (!(readl(priv->base + priv->regs->sr) & UARTDM_SR_TX_READY))
		return -EAGAIN;

	if (priv->regs->ncf_tx) {
		writel(1, priv->base + priv->regs->ncf_tx);
		readl(priv->base + priv->regs->ncf_tx);
	}

	writel(ch, priv->base + priv->regs->tf);
	return 0;
}

static int msm_serial_pending(struct udevice *dev, bool input)
{
	struct msm_serial_data *priv = dev_get_priv(dev);

	if (input) {
		if (priv->rx_count)
			return true;

		return !!(readl(priv->base + priv->regs->sr) & UARTDM_SR_RX_READY) ||
		       msm_serial_flush_rx(priv);
	} else {
		return !(readl(priv->base + priv->regs->sr) & UARTDM_SR_TX_EMPTY);
	}
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
	int ret;
	long rate;

	ret = clk_get_by_name(dev, "core", &clk);
	if (ret < 0) {
		log_debug("%s: Failed to get clock: %d, using %u Hz\n",
			  __func__, ret, priv->clk_rate);
		return priv->clk_rate;
	}

	rate = clk_set_rate(&clk, priv->clk_rate);
	if (rate < 0) {
		log_warning("%s: Failed to set clock: %ld, using %u Hz\n",
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

	if (priv->regs->fixed_csr_115200)
		return priv->regs->fixed_csr_115200;

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
	if (bitrate < 0) {
		log_warning("Couldn't calculate bit clock divider! Using default\n");
		/* This happens to be the value used on MSM8916 for the hardcoded clockrate
		 * in clock-apq8016. It's at least a better guess than a value we *know*
		 * is wrong...
		 */
		bitrate = 0xCC;
	}

	writel(bitrate, priv->base + priv->regs->csr);
	writel(priv->regs->dmrx ? 0 : UARTDM_MR1_RX_RDY_CTL,
	       priv->base + UARTDM_MR1);
	writel(UARTDM_MR2_8_N_1_MODE, priv->base + UARTDM_MR2);

	if (priv->regs->tfwr)
		writel(0, priv->base + priv->regs->tfwr);
	if (priv->regs->rfwr)
		writel(0, priv->base + priv->regs->rfwr);
	if (priv->regs->ipr)
		writel(UARTDM_IPR_STALE_TIMEOUT_LSB, priv->base + priv->regs->ipr);

	writel(priv->regs->dmrx ? 0 : UARTDM_DMEN_TXRX_SC_ENABLE,
	       priv->base + priv->regs->dmen);

	writel(UARTDM_CR_CMD_RESET_RX, priv->base + priv->regs->cr);
	writel(UARTDM_CR_CMD_RESET_TX, priv->base + priv->regs->cr);
	writel(UARTDM_CR_CMD_RESET_ERR, priv->base + priv->regs->cr);
	writel(UARTDM_CR_RX_ENABLE, priv->base + priv->regs->cr);
	writel(UARTDM_CR_TX_ENABLE, priv->base + priv->regs->cr);

	if (priv->regs->dmrx)
		msm_serial_reset_stale(priv);
}

static void msm_serial_configure_gsbi(struct udevice *dev)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	ofnode parent = ofnode_get_parent(dev_ofnode(dev));
	u32 mode;

	if (priv->gsbi_base == FDT_ADDR_T_NONE)
		return;

	if (ofnode_read_u32(parent, "qcom,mode", &mode))
		return;

	writel(mode << GSBI_CTRL_REG_PROTOCOL_SHIFT, priv->gsbi_base);
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

	msm_serial_configure_gsbi(dev);
	uart_dm_init(priv);

	return 0;
}

static int msm_serial_of_to_plat(struct udevice *dev)
{
	struct msm_serial_data *priv = dev_get_priv(dev);
	int ret;

	priv->regs = (const struct msm_serial_regs *)dev_get_driver_data(dev);
	if (!priv->regs)
		priv->regs = &uartdm_v14_regs;

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;
	priv->gsbi_base = dev_read_addr_index(dev, 1);

	ret = dev_read_u32(dev, "clock-frequency", &priv->clk_rate);
	if (ret < 0) {
		log_debug("No clock frequency specified, using default rate\n");
		/* Default for APQ8016 */
		priv->clk_rate = 7372800;
	}

	return 0;
}

static const struct udevice_id msm_serial_ids[] = {
	{ .compatible = "qcom,msm-uartdm-v1.3", .data = (ulong)&uartdm_v13_regs },
	{ .compatible = "qcom,msm-uartdm-v1.4", .data = (ulong)&uartdm_v14_regs },
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
#ifdef CONFIG_ARCH_SNAPDRAGON_ARM32
	.regs = &uartdm_v13_regs,
#else
	.regs = &uartdm_v14_regs,
#endif
};

#include <debug_uart.h>

/* Uncomment to turn on UART clocks when debugging U-Boot as aboot on MSM8916 */
//int apq8016_clk_init_uart(phys_addr_t gcc_base, unsigned long id);

static inline void _debug_uart_init(void)
{
	if (CONFIG_VAL(DEBUG_UART_MSM_GSBI_BASE) &&
	    CONFIG_VAL(DEBUG_UART_MSM_GSBI_MODE))
		writel(CONFIG_VAL(DEBUG_UART_MSM_GSBI_MODE) <<
		       GSBI_CTRL_REG_PROTOCOL_SHIFT,
		       CONFIG_VAL(DEBUG_UART_MSM_GSBI_BASE));

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

	while (!(readl(priv->base + priv->regs->sr) & UARTDM_SR_TX_READY))
		;

	if (priv->regs->ncf_tx) {
		writel(1, priv->base + priv->regs->ncf_tx);
		readl(priv->base + priv->regs->ncf_tx);
	}

	writel(ch, priv->base + priv->regs->tf);
}

DEBUG_UART_FUNCS

#endif
