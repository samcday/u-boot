/* SPDX-License-Identifier: GPL-2.0-only */
/* This header was adapted from linux/drivers/firmware/qcom/qcom_scm.h
 * Copyright (c) 2010-2015,2019 The Linux Foundation. All rights reserved.
 */

#ifndef QCOM_SCM_H
#define QCOM_SCM_H

#include <linux/errno.h>
#include <linux/types.h>

#define QCOM_SCM_SVC_BOOT		0x01
#define QCOM_SCM_BOOT_SET_ADDR_MC	0x11
#define QCOM_SCM_BOOT_MC_FLAG_AARCH64	BIT(0)
#define QCOM_SCM_BOOT_MC_FLAG_COLDBOOT	BIT(1)

#define MAX_QCOM_SCM_ARGS 10
#define MAX_QCOM_SCM_RETS 3

#define QCOM_SCM_ARGS_IMPL(num, a, b, c, d, e, f, g, h, i, j, ...) (\
				   (((a) & 0x3) << 4) | \
				   (((b) & 0x3) << 6) | \
				   (((c) & 0x3) << 8) | \
				   (((d) & 0x3) << 10) | \
				   (((e) & 0x3) << 12) | \
				   (((f) & 0x3) << 14) | \
				   (((g) & 0x3) << 16) | \
				   (((h) & 0x3) << 18) | \
				   (((i) & 0x3) << 20) | \
				   (((j) & 0x3) << 22) | \
				   ((num) & 0xf))
#define QCOM_SCM_ARGS(...) \
	QCOM_SCM_ARGS_IMPL(__VA_ARGS__, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

/**
 * struct qcom_scm_desc
 * @arginfo:	Metadata describing the arguments in args[]
 * @args:	The array of arguments for the secure syscall
 */
struct qcom_scm_desc {
	u32 svc;
	u32 cmd;
	u32 arginfo;
	u64 args[MAX_QCOM_SCM_ARGS];
	u32 owner;
};

/**
 * struct qcom_scm_res
 * @result:	The values returned by the secure syscall
 */
struct qcom_scm_res {
	u64 result[MAX_QCOM_SCM_RETS];
};

#if CONFIG_IS_ENABLED(QCOM_SCM)
bool qcom_scm_is_call_available(u32 svc_id, u32 cmd_id);
int qcom_scm_call(const struct qcom_scm_desc *desc, struct qcom_scm_res *res);
#else
static inline bool qcom_scm_is_call_available(u32 svc_id, u32 cmd_id)
{
	return false;
}

int qcom_scm_call(const struct qcom_scm_desc *desc, struct qcom_scm_res *res)
{
	return -ENOSYS;
}
#endif

#endif
