// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm Embedded USB Debugger support.
 */

#include <dm.h>
#include <misc.h>
#include <qcom_eud.h>
#include <stdio_dev.h>
#include <asm/io.h>
#include <dm/device-internal.h>
#include <dm/read.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/usb/gadget.h>

#define EUD_REG_COM_TX_ID	0x0000
#define EUD_REG_COM_TX_LEN	0x0004
#define EUD_REG_COM_TX_DAT	0x0008
#define EUD_REG_CSR_EUD_EN	0x1014

#define EUD_MAX_FIFO_SIZE	14
#define EUD_PACKET_DELAY_US	1000

struct qcom_eud_priv {
	void __iomem *base;
	struct udevice *udc;
	bool enabled;
};

static int qcom_eud_probe(struct udevice *dev);
static const struct misc_ops qcom_eud_ops;
static const struct udevice_id qcom_eud_ids[];

U_BOOT_DRIVER(qcom_eud) = {
	.name = "qcom_eud",
	.id = UCLASS_MISC,
	.of_match = qcom_eud_ids,
	.probe = qcom_eud_probe,
	.ops = &qcom_eud_ops,
	.priv_auto = sizeof(struct qcom_eud_priv),
};

static void __iomem *qcom_eud_reg(struct qcom_eud_priv *priv, u32 offset)
{
	return (u8 __iomem *)priv->base + offset;
}

static void qcom_eud_writel(struct qcom_eud_priv *priv, u32 val, u32 offset)
{
	writel(val, qcom_eud_reg(priv, offset));
}

static u32 qcom_eud_readl(struct qcom_eud_priv *priv, u32 offset)
{
	return readl(qcom_eud_reg(priv, offset));
}

int qcom_eud_get(struct udevice **devp)
{
	if (!devp)
		return -EINVAL;

	return uclass_get_device_by_driver(UCLASS_MISC, DM_DRIVER_GET(qcom_eud),
					   devp);
}

bool qcom_eud_is_enabled(struct udevice *dev)
{
	struct qcom_eud_priv *priv;

	if (!dev)
		return false;

	priv = dev_get_priv(dev);

	return qcom_eud_readl(priv, EUD_REG_CSR_EUD_EN) & BIT(0);
}

int qcom_eud_enable(struct udevice *dev)
{
	struct qcom_eud_priv *priv;
	int ret;

	if (!dev)
		return -ENODEV;

	priv = dev_get_priv(dev);

	if (priv->enabled)
		return 0;

	qcom_eud_writel(priv, BIT(0), EUD_REG_CSR_EUD_EN);

	ret = udc_device_get_by_index(0, &priv->udc);
	if (ret) {
		qcom_eud_writel(priv, 0, EUD_REG_CSR_EUD_EN);
		priv->udc = NULL;
		return ret;
	}

	priv->enabled = true;

	return 0;
}

int qcom_eud_disable(struct udevice *dev)
{
	struct qcom_eud_priv *priv;
	int ret = 0;

	if (!dev)
		return -ENODEV;

	priv = dev_get_priv(dev);

	qcom_eud_writel(priv, 0, EUD_REG_CSR_EUD_EN);
	priv->enabled = false;

	if (priv->udc) {
		ret = udc_device_put(priv->udc);
		priv->udc = NULL;
	}

	return ret;
}

int qcom_eud_com_write(struct udevice *dev, u8 id, const void *buf, size_t len)
{
	struct qcom_eud_priv *priv;
	const u8 *data = buf;
	size_t todo;
	size_t i;
	u32 reg;

	if (!dev)
		return -ENODEV;
	if (!data && len)
		return -EINVAL;
	if (!qcom_eud_is_enabled(dev))
		return -EIO;

	priv = dev_get_priv(dev);

	while (len) {
		todo = min(len, (size_t)EUD_MAX_FIFO_SIZE);

		qcom_eud_writel(priv, id, EUD_REG_COM_TX_ID);
		reg = qcom_eud_readl(priv, EUD_REG_COM_TX_ID);
		if ((u8)reg != id)
			return -EIO;

		for (i = 0; i < todo; i++)
			qcom_eud_writel(priv, data[i], EUD_REG_COM_TX_DAT);

		qcom_eud_writel(priv, todo, EUD_REG_COM_TX_LEN);

		data += todo;
		len -= todo;
		udelay(EUD_PACKET_DELAY_US);
	}

	return 0;
}

static int qcom_eud_misc_write(struct udevice *dev, int offset,
			       const void *buf, int size)
{
	int ret;

	if (offset < 0 || offset > 0xff || size < 0)
		return -EINVAL;

	ret = qcom_eud_com_write(dev, offset, buf, size);
	if (ret)
		return ret;

	return size;
}

static int qcom_eud_misc_set_enabled(struct udevice *dev, bool val)
{
	bool was_enabled = qcom_eud_is_enabled(dev);
	int ret;

	if (val)
		ret = qcom_eud_enable(dev);
	else
		ret = qcom_eud_disable(dev);
	if (ret)
		return ret;

	return was_enabled;
}

static const struct misc_ops qcom_eud_ops = {
	.write = qcom_eud_misc_write,
	.set_enabled = qcom_eud_misc_set_enabled,
};

#if IS_ENABLED(CONFIG_QCOM_EUD_CONSOLE)
static int qcom_eud_console_start(struct stdio_dev *sdev)
{
	struct udevice *dev;
	int ret;

	if (sdev->priv)
		return 0;

	ret = qcom_eud_get(&dev);
	if (ret)
		return ret;

	ret = qcom_eud_enable(dev);
	if (ret)
		return ret;

	sdev->priv = dev;

	return 0;
}

static int qcom_eud_console_stop(struct stdio_dev *sdev)
{
	sdev->priv = NULL;

	return 0;
}

static struct udevice *qcom_eud_console_get_dev(struct stdio_dev *sdev)
{
	if (!sdev->priv && qcom_eud_console_start(sdev))
		return NULL;

	return sdev->priv;
}

static void qcom_eud_console_putc(struct stdio_dev *sdev, const char c)
{
	struct udevice *dev = qcom_eud_console_get_dev(sdev);

	if (dev)
		qcom_eud_com_write(dev, QCOM_EUD_COM_APPS_ID, &c, 1);
}

static void qcom_eud_console_puts(struct stdio_dev *sdev, const char *str)
{
	struct udevice *dev = qcom_eud_console_get_dev(sdev);

	if (dev)
		qcom_eud_com_write(dev, QCOM_EUD_COM_APPS_ID, str, strlen(str));
}

int drv_qcom_eud_console_init(void)
{
	struct stdio_dev stdio;

	memset(&stdio, 0, sizeof(stdio));
	strcpy(stdio.name, "qcom-eud");
	stdio.flags = DEV_FLAGS_OUTPUT;
	stdio.putc = qcom_eud_console_putc;
	stdio.puts = qcom_eud_console_puts;
	stdio.start = qcom_eud_console_start;
	stdio.stop = qcom_eud_console_stop;

	return stdio_register(&stdio);
}
#endif

static int qcom_eud_probe(struct udevice *dev)
{
	struct qcom_eud_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -EINVAL;

	return 0;
}

static const struct udevice_id qcom_eud_ids[] = {
	{ .compatible = "qcom,sdm845-eud" },
	{ .compatible = "qcom,eud" },
	{ }
};
