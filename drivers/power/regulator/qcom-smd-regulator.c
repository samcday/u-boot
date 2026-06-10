// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm RPM-SMD regulator driver
 *
 * Copyright (c) 2026
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <dm.h>
#include <dm/device_compat.h>
#include <dm/device.h>
#include <dm/device-internal.h>
#include <dm/devres.h>
#include <dm/lists.h>
#include <linux/byteorder/little_endian.h>
#include <linux/kernel.h>
#include <power/regulator.h>
#include <soc/qcom/smd-rpm.h>

#define RPM_KEY_SWEN	0x6e657773
#define RPM_KEY_UV	0x00007675
#define RPM_KEY_MA	0x0000616d

struct rpm_regulator_req {
	__le32 key;
	__le32 nbytes;
	__le32 value;
} __packed;

struct qcom_smd_vreg_data {
	const char *name;
	u32 type;
	u32 id;
};

struct qcom_smd_vreg {
	struct udevice *rpm;
	const struct qcom_smd_vreg_data *data;

	int enabled;
	int uv;
	u32 load_ua;

	bool enabled_updated;
	bool uv_updated;
	bool load_updated;
};

#define RPM_VREG(_name, _type, _id) \
	{ .name = _name, .type = _type, .id = _id }

static const struct qcom_smd_vreg_data pm6125_regulators[] = {
	RPM_VREG("s1", QCOM_SMD_RPM_SMPA, 1),
	RPM_VREG("s2", QCOM_SMD_RPM_SMPA, 2),
	RPM_VREG("s3", QCOM_SMD_RPM_SMPA, 3),
	RPM_VREG("s4", QCOM_SMD_RPM_SMPA, 4),
	RPM_VREG("s5", QCOM_SMD_RPM_SMPA, 5),
	RPM_VREG("s6", QCOM_SMD_RPM_SMPA, 6),
	RPM_VREG("s7", QCOM_SMD_RPM_SMPA, 7),
	RPM_VREG("s8", QCOM_SMD_RPM_SMPA, 8),
	RPM_VREG("l1", QCOM_SMD_RPM_LDOA, 1),
	RPM_VREG("l2", QCOM_SMD_RPM_LDOA, 2),
	RPM_VREG("l3", QCOM_SMD_RPM_LDOA, 3),
	RPM_VREG("l4", QCOM_SMD_RPM_LDOA, 4),
	RPM_VREG("l5", QCOM_SMD_RPM_LDOA, 5),
	RPM_VREG("l6", QCOM_SMD_RPM_LDOA, 6),
	RPM_VREG("l7", QCOM_SMD_RPM_LDOA, 7),
	RPM_VREG("l8", QCOM_SMD_RPM_LDOA, 8),
	RPM_VREG("l9", QCOM_SMD_RPM_LDOA, 9),
	RPM_VREG("l10", QCOM_SMD_RPM_LDOA, 10),
	RPM_VREG("l11", QCOM_SMD_RPM_LDOA, 11),
	RPM_VREG("l12", QCOM_SMD_RPM_LDOA, 12),
	RPM_VREG("l13", QCOM_SMD_RPM_LDOA, 13),
	RPM_VREG("l14", QCOM_SMD_RPM_LDOA, 14),
	RPM_VREG("l15", QCOM_SMD_RPM_LDOA, 15),
	RPM_VREG("l16", QCOM_SMD_RPM_LDOA, 16),
	RPM_VREG("l17", QCOM_SMD_RPM_LDOA, 17),
	RPM_VREG("l18", QCOM_SMD_RPM_LDOA, 18),
	RPM_VREG("l19", QCOM_SMD_RPM_LDOA, 19),
	RPM_VREG("l20", QCOM_SMD_RPM_LDOA, 20),
	RPM_VREG("l21", QCOM_SMD_RPM_LDOA, 21),
	RPM_VREG("l22", QCOM_SMD_RPM_LDOA, 22),
	RPM_VREG("l23", QCOM_SMD_RPM_LDOA, 23),
	RPM_VREG("l24", QCOM_SMD_RPM_LDOA, 24),
	{ }
};

static int qcom_smd_regulator_write_active(struct udevice *dev)
{
	struct qcom_smd_vreg *vreg = dev_get_priv(dev);
	struct rpm_regulator_req req[3];
	int reqlen = 0;
	int ret;

	if (vreg->enabled_updated) {
		req[reqlen].key = cpu_to_le32(RPM_KEY_SWEN);
		req[reqlen].nbytes = cpu_to_le32(sizeof(u32));
		req[reqlen].value = cpu_to_le32(vreg->enabled);
		reqlen++;
	}

	if (vreg->uv_updated && vreg->enabled && vreg->uv > 0) {
		req[reqlen].key = cpu_to_le32(RPM_KEY_UV);
		req[reqlen].nbytes = cpu_to_le32(sizeof(u32));
		req[reqlen].value = cpu_to_le32(vreg->uv);
		reqlen++;
	}

	if (vreg->load_updated && vreg->enabled && vreg->load_ua) {
		req[reqlen].key = cpu_to_le32(RPM_KEY_MA);
		req[reqlen].nbytes = cpu_to_le32(sizeof(u32));
		req[reqlen].value = cpu_to_le32(vreg->load_ua / 1000);
		reqlen++;
	}

	if (!reqlen)
		return 0;

	ret = qcom_rpm_smd_write(vreg->rpm, QCOM_SMD_RPM_ACTIVE_STATE,
				 vreg->data->type, vreg->data->id,
				 req, sizeof(req[0]) * reqlen);
	if (!ret) {
		vreg->enabled_updated = false;
		vreg->uv_updated = false;
		vreg->load_updated = false;
	}

	return ret;
}

static int qcom_smd_regulator_set_enable(struct udevice *dev, bool enable)
{
	struct qcom_smd_vreg *vreg = dev_get_priv(dev);
	int old_enabled = vreg->enabled;
	int ret;

	if (vreg->enabled == enable)
		return 0;

	vreg->enabled = enable;
	vreg->enabled_updated = true;

	if (enable && vreg->uv > 0)
		vreg->uv_updated = true;

	ret = qcom_smd_regulator_write_active(dev);
	if (ret)
		vreg->enabled = old_enabled;

	return ret;
}

static int qcom_smd_regulator_get_enable(struct udevice *dev)
{
	struct qcom_smd_vreg *vreg = dev_get_priv(dev);

	return vreg->enabled;
}

static int qcom_smd_regulator_set_value(struct udevice *dev, int uv)
{
	struct qcom_smd_vreg *vreg = dev_get_priv(dev);
	int old_uv = vreg->uv;
	int ret;

	vreg->uv = uv;
	vreg->uv_updated = true;

	ret = qcom_smd_regulator_write_active(dev);
	if (ret)
		vreg->uv = old_uv;

	return ret;
}

static int qcom_smd_regulator_get_value(struct udevice *dev)
{
	struct qcom_smd_vreg *vreg = dev_get_priv(dev);

	return vreg->uv;
}

static int qcom_smd_regulator_set_current(struct udevice *dev, int ua)
{
	struct qcom_smd_vreg *vreg = dev_get_priv(dev);
	u32 old_load = vreg->load_ua;
	int ret;

	vreg->load_ua = ua;
	vreg->load_updated = true;

	ret = qcom_smd_regulator_write_active(dev);
	if (ret)
		vreg->load_ua = old_load;

	return ret;
}

static int qcom_smd_regulator_get_current(struct udevice *dev)
{
	struct qcom_smd_vreg *vreg = dev_get_priv(dev);

	return vreg->load_ua;
}

static const struct dm_regulator_ops qcom_smd_regulator_ops = {
	.get_value = qcom_smd_regulator_get_value,
	.set_value = qcom_smd_regulator_set_value,
	.get_enable = qcom_smd_regulator_get_enable,
	.set_enable = qcom_smd_regulator_set_enable,
	.get_current = qcom_smd_regulator_get_current,
	.set_current = qcom_smd_regulator_set_current,
};

static int qcom_smd_regulator_probe(struct udevice *dev)
{
	const struct qcom_smd_vreg_data *data =
		(const struct qcom_smd_vreg_data *)dev_get_driver_data(dev);
	struct dm_regulator_uclass_plat *uc_pdata = dev_get_uclass_plat(dev);
	struct qcom_smd_vreg *vreg = dev_get_priv(dev);

	vreg->rpm = dev->parent->parent;
	vreg->data = data;
	vreg->enabled = 0;
	vreg->uv = -ENODATA;

	if (uc_pdata->init_uV > 0)
		vreg->uv = uc_pdata->init_uV;
	else if (uc_pdata->max_uV > 0)
		vreg->uv = uc_pdata->max_uV;
	else if (uc_pdata->min_uV > 0)
		vreg->uv = uc_pdata->min_uV;

	uc_pdata->type = REGULATOR_TYPE_LDO;

	return 0;
}

U_BOOT_DRIVER(qcom_smd_regulator) = {
	.name = "qcom_smd_regulator",
	.id = UCLASS_REGULATOR,
	.probe = qcom_smd_regulator_probe,
	.priv_auto = sizeof(struct qcom_smd_vreg),
	.ops = &qcom_smd_regulator_ops,
};

static const struct qcom_smd_vreg_data *
qcom_smd_regulator_get_data(const struct qcom_smd_vreg_data *data,
			    ofnode node)
{
	for (; data->name; data++) {
		if (!strcmp(data->name, ofnode_get_name(node)))
			return data;
	}

	return NULL;
}

static int qcom_smd_regulators_bind(struct udevice *dev)
{
	const struct qcom_smd_vreg_data *init_data, *data;
	struct driver *drv;
	ofnode node;
	int ret;

	init_data = (const struct qcom_smd_vreg_data *)dev_get_driver_data(dev);
	if (!init_data)
		return -EINVAL;

	drv = lists_driver_lookup_name("qcom_smd_regulator");
	if (!drv)
		return -ENOENT;

	dev_for_each_subnode(node, dev) {
		data = qcom_smd_regulator_get_data(init_data, node);
		if (!data)
			continue;

		ret = device_bind_with_driver_data(dev, drv, data->name,
						   (ulong)data, node, NULL);
		if (ret) {
			dev_err(dev, "failed to bind regulator %s: %d\n",
				data->name, ret);
			return ret;
		}
	}

	return 0;
}

static const struct udevice_id qcom_smd_regulators_of_match[] = {
	{
		.compatible = "qcom,rpm-pm6125-regulators",
		.data = (ulong)pm6125_regulators,
	},
	{ }
};

U_BOOT_DRIVER(qcom_smd_regulators) = {
	.name = "qcom_smd_regulators",
	.id = UCLASS_MISC,
	.bind = qcom_smd_regulators_bind,
	.of_match = qcom_smd_regulators_of_match,
};
