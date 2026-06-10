/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOC_QCOM_SMD_RPM_H__
#define __SOC_QCOM_SMD_RPM_H__

#include <linux/types.h>

struct udevice;

#define QCOM_SMD_RPM_ACTIVE_STATE	0
#define QCOM_SMD_RPM_SLEEP_STATE	1

#define QCOM_SMD_RPM_LDOA		0x616f646c
#define QCOM_SMD_RPM_SMPA		0x61706d73

int qcom_rpm_smd_write(struct udevice *dev, int state, u32 resource_type,
		       u32 resource_id, const void *buf, size_t count);

#endif /* __SOC_QCOM_SMD_RPM_H__ */
