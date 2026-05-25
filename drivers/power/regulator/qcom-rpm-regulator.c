// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal legacy Qualcomm RPM regulator support for MSM8930/PM8038.
 *
 * This intentionally models only the PM8038 LDOs needed by the Nokia Fame
 * display path. The RPM request format is derived from the working raw
 * APPSBL display bring-up sequence.
 */

#define LOG_CATEGORY UCLASS_REGULATOR

#include <dm.h>
#include <dm/device-internal.h>
#include <dm/device_compat.h>
#include <dm/lists.h>
#include <dm/ofnode.h>
#include <dm/read.h>
#include <errno.h>
#include <power/pmic.h>
#include <power/regulator.h>
#include <string.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kernel.h>

#define QCOM_RPM_STATUS_REG(idx)	((idx) * 4)
#define QCOM_RPM_CTRL_REG(idx)		(0x400 + ((idx) * 4))
#define QCOM_RPM_REQ_REG(idx)		(0x600 + ((idx) * 4))

#define QCOM_RPM_VERSION		3
#define QCOM_RPM_REQ_CTX_OFF		3
#define QCOM_RPM_REQ_SEL_OFF		11
#define QCOM_RPM_ACK_CTX_OFF		15
#define QCOM_RPM_ACK_SEL_OFF		23
#define QCOM_RPM_REQ_SEL_SIZE		4
#define QCOM_RPM_ACK_SEL_SIZE		7
#define QCOM_RPM_ACTIVE_STATE		0
#define QCOM_RPM_ACK_POLL_US		10
#define QCOM_RPM_ACK_TIMEOUT_US		500000
#define QCOM_RPM_NOTIFICATION		BIT(30)
#define QCOM_RPM_REJECTED		BIT(31)
#define QCOM_RPM_LDO_PULL_DOWN		BIT(23)

#define QCOM_RPM_MSM8930_IPC_REG	0x02011008
#define QCOM_RPM_MSM8930_IPC_BIT	2

struct qcom_rpm_priv {
	void __iomem *base;
	void __iomem *ipc_reg;
	u32 ipc_bit;
	bool initialized;
};

struct qcom_rpm_pm8038_ldo_desc {
	const char *name;
	u32 target_id;
	u32 status_id;
	u32 select_id;
};

struct qcom_rpm_regulator_priv {
	const struct qcom_rpm_pm8038_ldo_desc *desc;
	int microvolts;
	bool enabled;
	bool pull_down;
};

static const struct qcom_rpm_pm8038_ldo_desc pm8038_ldos[] = {
	{ "l2", 104, 45, 37 },
	{ "l8", 116, 57, 43 },
	{ "l11", 122, 63, 46 },
};

static u32 qcom_rpm_read(struct qcom_rpm_priv *priv, u32 reg)
{
	return readl(priv->base + reg);
}

static void qcom_rpm_write_reg(struct qcom_rpm_priv *priv, u32 reg, u32 val)
{
	writel(val, priv->base + reg);
	readl(priv->base + reg);
}

static void qcom_rpm_clear_ack(struct qcom_rpm_priv *priv)
{
	int i;

	for (i = 0; i < QCOM_RPM_ACK_SEL_SIZE; i++)
		qcom_rpm_write_reg(priv,
				   QCOM_RPM_CTRL_REG(QCOM_RPM_ACK_SEL_OFF + i),
				   0);
	qcom_rpm_write_reg(priv, QCOM_RPM_CTRL_REG(QCOM_RPM_ACK_CTX_OFF), 0);
}

static int qcom_rpm_init(struct udevice *dev)
{
	struct qcom_rpm_priv *priv = dev_get_priv(dev);
	u32 fw0 = qcom_rpm_read(priv, QCOM_RPM_STATUS_REG(0));
	u32 fw1 = qcom_rpm_read(priv, QCOM_RPM_STATUS_REG(1));
	u32 fw2 = qcom_rpm_read(priv, QCOM_RPM_STATUS_REG(2));

	if (priv->initialized)
		return 0;

	dev_dbg(dev, "RPM firmware %u.%u.%u ctrl_ack=%08x\n",
		fw0, fw1, fw2,
		qcom_rpm_read(priv, QCOM_RPM_CTRL_REG(QCOM_RPM_ACK_CTX_OFF)));
	if (fw0 != QCOM_RPM_VERSION)
		return -EFAULT;

	qcom_rpm_write_reg(priv, QCOM_RPM_CTRL_REG(0), fw0);
	qcom_rpm_write_reg(priv, QCOM_RPM_CTRL_REG(1), fw1);
	qcom_rpm_write_reg(priv, QCOM_RPM_CTRL_REG(2), fw2);
	qcom_rpm_clear_ack(priv);
	priv->initialized = true;

	return 0;
}

static int qcom_rpm_wait_ack(struct udevice *dev, const char *name)
{
	struct qcom_rpm_priv *priv = dev_get_priv(dev);
	u32 ack = 0;
	int elapsed;

	for (elapsed = 0; elapsed < QCOM_RPM_ACK_TIMEOUT_US;
	     elapsed += QCOM_RPM_ACK_POLL_US) {
		ack = qcom_rpm_read(priv,
				    QCOM_RPM_CTRL_REG(QCOM_RPM_ACK_CTX_OFF));
		if (!ack) {
			udelay(QCOM_RPM_ACK_POLL_US);
			continue;
		}

		qcom_rpm_clear_ack(priv);
		if (ack & QCOM_RPM_NOTIFICATION)
			continue;

		dev_dbg(dev, "RPM %s ack after %d us ack=%08x\n",
			name, elapsed, ack);
		if (ack & QCOM_RPM_REJECTED)
			return -EIO;

		return 0;
	}

	dev_err(dev, "RPM %s ack timeout req_ctx=%08x ack_ctx=%08x\n",
		name,
		qcom_rpm_read(priv, QCOM_RPM_CTRL_REG(QCOM_RPM_REQ_CTX_OFF)),
		qcom_rpm_read(priv, QCOM_RPM_CTRL_REG(QCOM_RPM_ACK_CTX_OFF)));

	return -ETIMEDOUT;
}

static int qcom_rpm_write(struct udevice *dev, const char *name, u32 target_id,
			  u32 select_id, const u32 *words, int count)
{
	struct qcom_rpm_priv *priv = dev_get_priv(dev);
	u32 sel_mask[QCOM_RPM_REQ_SEL_SIZE] = { 0 };
	int ret;
	int i;

	if (select_id >= QCOM_RPM_REQ_SEL_SIZE * 32)
		return -EINVAL;

	ret = qcom_rpm_init(dev);
	if (ret)
		return ret;

	qcom_rpm_clear_ack(priv);
	for (i = 0; i < count; i++)
		qcom_rpm_write_reg(priv, QCOM_RPM_REQ_REG(target_id + i),
				   words[i]);

	sel_mask[select_id / 32] = BIT(select_id % 32);
	for (i = 0; i < QCOM_RPM_REQ_SEL_SIZE; i++)
		qcom_rpm_write_reg(priv,
				   QCOM_RPM_CTRL_REG(QCOM_RPM_REQ_SEL_OFF + i),
				   sel_mask[i]);

	qcom_rpm_write_reg(priv, QCOM_RPM_CTRL_REG(QCOM_RPM_REQ_CTX_OFF),
			   BIT(QCOM_RPM_ACTIVE_STATE));
	writel(BIT(priv->ipc_bit), priv->ipc_reg);

	return qcom_rpm_wait_ack(dev, name);
}

static const struct qcom_rpm_pm8038_ldo_desc *
qcom_rpm_pm8038_find_ldo(ofnode node)
{
	const char *name = ofnode_get_name(node);
	int i;

	for (i = 0; i < ARRAY_SIZE(pm8038_ldos); i++) {
		if (!strcmp(name, pm8038_ldos[i].name))
			return &pm8038_ldos[i];
	}

	return NULL;
}

static int qcom_rpm_pm8038_regulator_get_value(struct udevice *dev)
{
	struct qcom_rpm_regulator_priv *priv = dev_get_priv(dev);

	return priv->microvolts;
}

static int qcom_rpm_pm8038_regulator_set_value(struct udevice *dev, int uV)
{
	struct qcom_rpm_regulator_priv *priv = dev_get_priv(dev);

	priv->microvolts = uV;

	return 0;
}

static int qcom_rpm_pm8038_regulator_get_enable(struct udevice *dev)
{
	struct qcom_rpm_regulator_priv *priv = dev_get_priv(dev);

	return priv->enabled;
}

static int qcom_rpm_pm8038_regulator_set_enable(struct udevice *dev,
						bool enable)
{
	struct qcom_rpm_regulator_priv *priv = dev_get_priv(dev);
	u32 words[2] = { 0 };
	int ret;

	if (priv->enabled == enable)
		return -EALREADY;

	if (enable) {
		if (priv->microvolts <= 0)
			return -EINVAL;

		words[0] = priv->microvolts;
	}
	if (priv->pull_down)
		words[0] |= QCOM_RPM_LDO_PULL_DOWN;

	ret = qcom_rpm_write(dev->parent, priv->desc->name, priv->desc->target_id,
			     priv->desc->select_id, words, ARRAY_SIZE(words));
	if (ret)
		return ret;

	priv->enabled = enable;
	dev_dbg(dev, "PM8038 %s status[%u]=%08x status[%u]=%08x\n",
		priv->desc->name, priv->desc->status_id,
		qcom_rpm_read(dev_get_priv(dev->parent),
			      QCOM_RPM_STATUS_REG(priv->desc->status_id)),
		priv->desc->status_id + 1,
		qcom_rpm_read(dev_get_priv(dev->parent),
			      QCOM_RPM_STATUS_REG(priv->desc->status_id + 1)));

	return 0;
}

static int qcom_rpm_pm8038_regulator_probe(struct udevice *dev)
{
	struct qcom_rpm_regulator_priv *priv = dev_get_priv(dev);
	struct dm_regulator_uclass_plat *uc_plat = dev_get_uclass_plat(dev);

	priv->desc = (const struct qcom_rpm_pm8038_ldo_desc *)
		dev_get_driver_data(dev);
	if (!priv->desc)
		return -EINVAL;

	uc_plat->type = REGULATOR_TYPE_LDO;
	if (uc_plat->init_uV > 0)
		priv->microvolts = uc_plat->init_uV;
	else if (uc_plat->min_uV > 0)
		priv->microvolts = uc_plat->min_uV;
	else if (uc_plat->max_uV > 0)
		priv->microvolts = uc_plat->max_uV;

	priv->pull_down = dev_read_bool(dev, "bias-pull-down");

	return 0;
}

static const struct dm_regulator_ops qcom_rpm_pm8038_regulator_ops = {
	.get_value = qcom_rpm_pm8038_regulator_get_value,
	.set_value = qcom_rpm_pm8038_regulator_set_value,
	.get_enable = qcom_rpm_pm8038_regulator_get_enable,
	.set_enable = qcom_rpm_pm8038_regulator_set_enable,
};

U_BOOT_DRIVER(qcom_rpm_pm8038_regulator) = {
	.name		= "qcom_rpm_pm8038_regulator",
	.id		= UCLASS_REGULATOR,
	.ops		= &qcom_rpm_pm8038_regulator_ops,
	.probe		= qcom_rpm_pm8038_regulator_probe,
	.priv_auto	= sizeof(struct qcom_rpm_regulator_priv),
};

static int qcom_rpm_bind_pm8038_regulators(struct udevice *dev,
					   ofnode regulators)
{
	struct driver *drv;
	ofnode node;
	int ret;

	drv = lists_driver_lookup_name("qcom_rpm_pm8038_regulator");
	if (!drv)
		return -ENOENT;

	ofnode_for_each_subnode(node, regulators) {
		const struct qcom_rpm_pm8038_ldo_desc *desc;

		if (!ofnode_is_enabled(node))
			continue;

		desc = qcom_rpm_pm8038_find_ldo(node);
		if (!desc)
			continue;

		ret = device_bind_with_driver_data(dev, drv, desc->name,
						   (ulong)desc, node, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

static int qcom_rpm_bind(struct udevice *dev)
{
	ofnode regulators;

	regulators = dev_read_subnode(dev, "regulators");
	if (!ofnode_valid(regulators))
		return 0;

	if (!ofnode_device_is_compatible(regulators,
					 "qcom,rpm-pm8038-regulators"))
		return 0;

	return qcom_rpm_bind_pm8038_regulators(dev, regulators);
}

static int qcom_rpm_probe(struct udevice *dev)
{
	struct qcom_rpm_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -EINVAL;

	priv->ipc_reg = (void __iomem *)QCOM_RPM_MSM8930_IPC_REG;
	priv->ipc_bit = QCOM_RPM_MSM8930_IPC_BIT;

	return qcom_rpm_init(dev);
}

static const struct udevice_id qcom_rpm_ids[] = {
	{ .compatible = "qcom,rpm-msm8930" },
	{ }
};

U_BOOT_DRIVER(qcom_rpm) = {
	.name		= "qcom_rpm",
	.id		= UCLASS_PMIC,
	.of_match	= qcom_rpm_ids,
	.bind		= qcom_rpm_bind,
	.probe		= qcom_rpm_probe,
	.priv_auto	= sizeof(struct qcom_rpm_priv),
};
