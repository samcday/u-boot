// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm SDM845 Embedded USB Debugger test command.
 */

#include <command.h>
#include <console.h>
#include <g_dnl.h>
#include <time.h>
#include <vsprintf.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <u-boot/schedule.h>
#include <usb.h>
#include <asm/io.h>

#define EUD_BASE			0x088e0000UL

#define EUD_REG_COM_TX_ID	0x0000
#define EUD_REG_COM_TX_LEN	0x0004
#define EUD_REG_COM_TX_DAT	0x0008
#define EUD_REG_CSR_EUD_EN	0x1014

#define EUD_UART_ID		0x90
#define EUD_MAX_FIFO_SIZE	14
#define EUD_LOOP_DELAY_US	1000
#define EUD_COM_GRACE_MS	2000
#define EUD_COM_PERIOD_MS	100

enum eud_tx_len_mode {
	EUD_TX_LEN_BEFORE,
	EUD_TX_LEN_AFTER,
	EUD_TX_LEN_NONE,
};

static void __iomem *eud_reg(u32 offset)
{
	return (void __iomem *)(EUD_BASE + offset);
}

static void eud_writel(u32 val, u32 offset)
{
	writel(val, eud_reg(offset));
}

static u32 eud_readl(u32 offset)
{
	return readl(eud_reg(offset));
}

static void eud_enable(void)
{
	eud_writel(BIT(0), EUD_REG_CSR_EUD_EN);
}

static void eud_disable(void)
{
	eud_writel(0, EUD_REG_CSR_EUD_EN);
}

static const char *eud_tx_len_mode_name(enum eud_tx_len_mode mode)
{
	switch (mode) {
	case EUD_TX_LEN_BEFORE:
		return "len-before";
	case EUD_TX_LEN_AFTER:
		return "len-after";
	case EUD_TX_LEN_NONE:
		return "no-len";
	}

	return "unknown";
}

static int eud_parse_tx_len_mode(const char *arg, enum eud_tx_len_mode *mode)
{
	if (!strcmp(arg, "len-before") || !strcmp(arg, "before")) {
		*mode = EUD_TX_LEN_BEFORE;
		return 0;
	}
	if (!strcmp(arg, "len-after") || !strcmp(arg, "after")) {
		*mode = EUD_TX_LEN_AFTER;
		return 0;
	}
	if (!strcmp(arg, "no-len") || !strcmp(arg, "none")) {
		*mode = EUD_TX_LEN_NONE;
		return 0;
	}

	return -EINVAL;
}

static unsigned int eud_com_write(const char *buf, unsigned int len, u8 tx_id,
				  enum eud_tx_len_mode tx_len_mode)
{
	static bool warned_id_readback;
	unsigned int i;
	unsigned int written = 0;
	unsigned int todo;
	u32 reg;

	while (len) {
		todo = len;
		if (todo > EUD_MAX_FIFO_SIZE)
			todo = EUD_MAX_FIFO_SIZE;

		eud_writel(tx_id, EUD_REG_COM_TX_ID);
		reg = eud_readl(EUD_REG_COM_TX_ID);
		if (reg != tx_id && !warned_id_readback) {
			printf("EUD COM_TX_ID readback 0x%08x, expected 0x%02x; continuing.\n",
			       reg, tx_id);
			warned_id_readback = true;
		}

		if (tx_len_mode == EUD_TX_LEN_BEFORE)
			eud_writel(todo, EUD_REG_COM_TX_LEN);

		for (i = 0; i < todo; i++)
			eud_writel((u8)buf[i], EUD_REG_COM_TX_DAT);

		if (tx_len_mode == EUD_TX_LEN_AFTER)
			eud_writel(todo, EUD_REG_COM_TX_LEN);

		buf += todo;
		len -= todo;
		written += todo;
		udelay(1000);
	}

	return written;
}

static int do_eud(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	static const char heartbeat[] = "eud ping\n";
	enum eud_tx_len_mode tx_len_mode = EUD_TX_LEN_AFTER;
	unsigned int tx_count;
	ulong start;
	ulong last_com;
	struct udevice *udc;
	unsigned long tx_id_arg;
	char *endp;
	u8 tx_id = EUD_UART_ID;
	int ret;

	if (argc > 3)
		return CMD_RET_USAGE;
	if (argc >= 2) {
		ret = eud_parse_tx_len_mode(argv[1], &tx_len_mode);
		if (ret) {
			tx_id_arg = hextoul(argv[1], &endp);
			if (!*argv[1] || *endp || tx_id_arg > 0xff) {
				puts("TX ID must fit in one byte, or pass a TX length mode.\n");
				return CMD_RET_USAGE;
			}
			tx_id = tx_id_arg;
		}
	}
	if (argc >= 3 && eud_parse_tx_len_mode(argv[2], &tx_len_mode))
		return CMD_RET_USAGE;

	printf("Enabling SDM845 EUD at 0x%08lx.\n", EUD_BASE);
	puts("Starting ACM gadget to trigger EUD enumeration.\n");
	printf("Using COM TX ID 0x%02x, TX length mode %s.\n",
	       tx_id, eud_tx_len_mode_name(tx_len_mode));
	puts("Run `eudctl com-on` on the host, then press any key here to stop.\n");

	eud_enable();

	ret = udc_device_get_by_index(0, &udc);
	if (ret) {
		printf("USB init failed: %d\n", ret);
		goto disable_eud;
	}

	g_dnl_clear_detach();
	ret = g_dnl_register("usb_serial_acm");
	if (ret) {
		printf("ACM gadget register failed: %d\n", ret);
		goto put_udc;
	}

	console_flush_stdin();
	start = get_timer(0);
	last_com = start;

	while (1) {
		if (tstc()) {
			getchar();
			puts("\rEUD stopped.\n");
			break;
		}
		if (g_dnl_detach()) {
			puts("\rUSB detach requested.\n");
			break;
		}

		dm_usb_gadget_handle_interrupts(udc);

		if (get_timer(start) >= EUD_COM_GRACE_MS &&
		    get_timer(last_com) >= EUD_COM_PERIOD_MS) {
			tx_count = eud_com_write(heartbeat, sizeof(heartbeat) - 1,
						 tx_id, tx_len_mode);
			if (!tx_count)
				puts("EUD COM heartbeat was not queued.\n");
			puts("\rping.\n");
			last_com = get_timer(0);
		}

		udelay(EUD_LOOP_DELAY_US);
		schedule();
	}

	g_dnl_unregister();
	g_dnl_clear_detach();
put_udc:
	udc_device_put(udc);
disable_eud:
	eud_disable();

	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(eud, 3, 0, do_eud,
	   "enable Qualcomm SDM845 EUD COM test loop",
	   "[tx-id] [len-before|len-after|no-len] - enable EUD, write a COM heartbeat, and stop on any key"
);
