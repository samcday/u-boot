// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Ramon Fried <ramon.fried@gmail.com>
 */

#include <dm.h>
#include <generic-phy.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <usb/ehci-ci.h>
#include <usb/ulpi.h>
#include <asm/io.h>

#define QCOM_USB_HS_PHY_INIT_SEQ_MAX	32

/* PHY viewport regs */
#define ULPI_MISC_A_READ		0x96
#define ULPI_MISC_A_SET			0x97
#define ULPI_MISC_A_CLEAR		0x98
#define ULPI_MISC_A_VBUSVLDEXT		BIT(0)
#define ULPI_MISC_A_VBUSVLDEXTSEL	BIT(1)
#define GEN2_SESS_VLD_CTRL_EN		BIT(7)
#define SESS_VLD_CTRL			BIT(25)

struct msm_phy_priv {
	void __iomem *regs;
	struct usb_ehci *ehci; /* Start of IP core*/
	struct ulpi_viewport ulpi_vp; /* ULPI Viewport */
	struct {
		u8 addr;
		u8 val;
	} init_seq[QCOM_USB_HS_PHY_INIT_SEQ_MAX];
	uint init_seq_num;
};

static int msm_phy_power_on(struct phy *phy)
{
	struct msm_phy_priv *priv = dev_get_priv(phy->dev);
	int i, ret;

	for (i = 0; i < priv->init_seq_num; i++) {
		u8 *reg = (u8 *)(EXT_VENDOR_SPEC_OFFSET +
				  priv->init_seq[i].addr);

		ret = ulpi_write(&priv->ulpi_vp,
				 reg, priv->init_seq[i].val);
		if (ret)
			return ret;
	}

	/* Select and enable external configuration with USB PHY */
	ret = ulpi_write(&priv->ulpi_vp, (u8 *)ULPI_MISC_A_SET,
			 ULPI_MISC_A_VBUSVLDEXTSEL | ULPI_MISC_A_VBUSVLDEXT);
	if (ret)
		return ret;

	return 0;
}

static int msm_phy_power_off(struct phy *phy)
{
	struct msm_phy_priv *priv = dev_get_priv(phy->dev);
	int ret;

	/* Disable VBUS mimicing in the controller. */
	ret = ulpi_write(&priv->ulpi_vp, (u8 *)ULPI_MISC_A_CLEAR,
			 ULPI_MISC_A_VBUSVLDEXTSEL | ULPI_MISC_A_VBUSVLDEXT);

	return ret;
}

static int msm_phy_reset(struct phy *phy)
{
	struct msm_phy_priv *p = dev_get_priv(phy->dev);

	/* select ULPI phy */
	writel(PORT_PTS_ULPI, &p->ehci->portsc);

	/* Enable sess_vld */
	setbits_le32(&p->ehci->genconfig2, GEN2_SESS_VLD_CTRL_EN);

	/* Enable external vbus configuration in the LINK */
	setbits_le32(&p->ehci->usbcmd, SESS_VLD_CTRL);

	/* USB_OTG_HS_AHB_BURST */
	writel(0x0, &p->ehci->sbuscfg);

	/* USB_OTG_HS_AHB_MODE: HPROT_MODE */
	/* Bus access related config. */
	writel(0x08, &p->ehci->sbusmode);

	return 0;
}

static int msm_phy_probe(struct udevice *dev)
{
	struct msm_phy_priv *priv = dev_get_priv(dev);
	const u8 *init_seq;
	int size, i;

	priv->regs = dev_remap_addr(dev_get_parent(dev));
	if (!priv->regs)
		return -EINVAL;

	priv->ehci = (struct usb_ehci *)priv->regs;
	priv->ulpi_vp.port_num = 0;

	/* Warning: this will not work if viewport address is > 64 bit due to
	 * ULPI design.
	 */
	priv->ulpi_vp.viewport_addr = (phys_addr_t)&priv->ehci->ulpi_viewpoint;

	size = dev_read_size(dev, "qcom,init-seq");
	if (size > 0) {
		if (size % 2)
			return -EINVAL;
		if (size / 2 > ARRAY_SIZE(priv->init_seq))
			return -EINVAL;

		init_seq = dev_read_u8_array_ptr(dev, "qcom,init-seq", size);
		if (!init_seq)
			return -EINVAL;

		for (i = 0; i < size / 2; i++) {
			priv->init_seq[i].addr = init_seq[i * 2];
			priv->init_seq[i].val = init_seq[i * 2 + 1];
		}
		priv->init_seq_num = size / 2;
	}

	return 0;
}

static struct phy_ops msm_phy_ops = {
	.power_on = msm_phy_power_on,
	.power_off = msm_phy_power_off,
	.reset = msm_phy_reset,
};

static const struct udevice_id msm_phy_ids[] = {
	{ .compatible = "qcom,usb-hs-phy" },
	{ .compatible = "qcom,usb-hs-phy-apq8064" },
	{ .compatible = "qcom,usb-hs-phy-msm8226" },
	{ .compatible = "qcom,usb-hs-phy-msm8660" },
	{ .compatible = "qcom,usb-hs-phy-msm8916" },
	{ .compatible = "qcom,usb-hs-phy-msm8960" },
	{ .compatible = "qcom,usb-hs-phy-msm8974" },
	{ }
};

U_BOOT_DRIVER(msm8916_usbphy) = {
	.name		= "msm8916_usbphy",
	.id		= UCLASS_PHY,
	.of_match	= msm_phy_ids,
	.ops		= &msm_phy_ops,
	.probe		= msm_phy_probe,
	.priv_auto	= sizeof(struct msm_phy_priv),
};
