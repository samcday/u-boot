// SPDX-License-Identifier: GPL-2.0-only
/**
 * Much of this code was adapted from drivers/firmware/qcom/qcom_scm-smc.c
 * in the kernel:
 *  Copyright (c) 2010-2015,2019 The Linux Foundation.	 All rights reserved.
 */

#include <compiler.h>
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <firmware/qcom/scm.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <malloc.h>
#include <memalign.h>

#define QCOM_SCM_EBUSY_WAIT_MS 30
#define QCOM_SCM_EBUSY_MAX_RETRY 20

#define SCM_SMC_N_REG_ARGS	4
#define SCM_SMC_FIRST_EXT_IDX	(SCM_SMC_N_REG_ARGS - 1)
#define SCM_SMC_N_EXT_ARGS	(MAX_QCOM_SCM_ARGS - SCM_SMC_N_REG_ARGS + 1)
#define SCM_SMC_FIRST_REG_IDX	2
#define SCM_SMC_LAST_REG_IDX	(SCM_SMC_FIRST_REG_IDX + SCM_SMC_N_REG_ARGS - 1)

enum qcom_scm_convention {
	SMC_CONVENTION_UNKNOWN,
	SMC_CONVENTION_LEGACY,
	SMC_CONVENTION_ARM_32,
	SMC_CONVENTION_ARM_64,
};

#define SCM_SMC_FNID(s, c)	((((s) & 0xFF) << 8) | ((c) & 0xFF))

#define QCOM_SCM_SVC_INFO		0x06
#define QCOM_SCM_INFO_IS_CALL_AVAIL	0x01

/* common error codes */
#define QCOM_SCM_V2_EBUSY	-12
#define QCOM_SCM_ENOMEM		-5
#define QCOM_SCM_EOPNOTSUPP	-4
#define QCOM_SCM_EINVAL_ADDR	-3
#define QCOM_SCM_EINVAL_ARG	-2
#define QCOM_SCM_ERROR		-1
#define QCOM_SCM_INTERRUPTED	1

static inline int qcom_scm_remap_error(int err)
{
	switch (err) {
	case QCOM_SCM_ERROR:
		return -EIO;
	case QCOM_SCM_EINVAL_ADDR:
	case QCOM_SCM_EINVAL_ARG:
		return -EINVAL;
	case QCOM_SCM_EOPNOTSUPP:
		return -EOPNOTSUPP;
	case QCOM_SCM_ENOMEM:
		return -ENOMEM;
	case QCOM_SCM_V2_EBUSY:
		return -EBUSY;
	}
	return -EINVAL;
}

/**
 * struct arm_smccc_args
 * @args:	The array of values used in registers in smc instruction
 */
struct arm_smccc_args {
	unsigned long args[8];
};

struct qcom_scm {
	enum qcom_scm_convention convention;
};

static void __scm_smc_do_quirk(const struct arm_smccc_args *smc,
			       struct arm_smccc_res *res)
{
	unsigned long a0 = smc->args[0];
	struct arm_smccc_quirk quirk = { .id = ARM_SMCCC_QUIRK_QCOM_A6 };

	quirk.state.a6 = 0;

	do {
		arm_smccc_smc_quirk(a0, smc->args[1], smc->args[2],
				    smc->args[3], smc->args[4], smc->args[5],
				    quirk.state.a6, smc->args[7], res, &quirk);

		if (res->a0 == QCOM_SCM_INTERRUPTED)
			a0 = res->a0;

	} while (res->a0 == QCOM_SCM_INTERRUPTED);
}

static int __scm_smc_do(struct arm_smccc_args *smc, struct arm_smccc_res *res)
{
	int retry_count = 0;

	do {
		__scm_smc_do_quirk(smc, res);

		if (res->a0 == QCOM_SCM_V2_EBUSY) {
			if (retry_count++ > QCOM_SCM_EBUSY_MAX_RETRY)
				break;
			mdelay(QCOM_SCM_EBUSY_WAIT_MS);
		}
	}  while (res->a0 == QCOM_SCM_V2_EBUSY);

	return 0;
}

static int __qcom_scm_call(const struct qcom_scm_desc *desc,
			   enum qcom_scm_convention qcom_convention,
			   struct qcom_scm_res *res)
{
	int arglen = desc->arginfo & 0xf;
	void *args = NULL;
	int i, ret;
	struct arm_smccc_args smc = {0};
	struct arm_smccc_res smc_res;
	u32 qcom_smccc_convention;
	u32 fnid = SCM_SMC_FNID(desc->svc, desc->cmd);

	switch (qcom_convention) {
	case SMC_CONVENTION_ARM_32:
		qcom_smccc_convention = ARM_SMCCC_SMC_32;
		break;
	case SMC_CONVENTION_ARM_64:
		qcom_smccc_convention = ARM_SMCCC_SMC_64;
		break;
	default:
		return -EINVAL;
	}

	smc.args[0] = ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL,
					 qcom_smccc_convention, desc->owner, fnid);
	smc.args[1] = desc->arginfo;
	for (i = 0; i < SCM_SMC_N_REG_ARGS; i++)
		smc.args[i + SCM_SMC_FIRST_REG_IDX] = desc->args[i];

	if (unlikely(arglen > SCM_SMC_N_REG_ARGS)) {
		args = malloc_cache_aligned(SCM_SMC_N_EXT_ARGS * sizeof(u64));
		if (!args)
			return -ENOMEM;

		if (qcom_smccc_convention == ARM_SMCCC_SMC_32) {
			__le32 *args32 = args;

			for (i = 0; i < SCM_SMC_N_EXT_ARGS; i++)
				args32[i] = cpu_to_le32(desc->args[i +
							SCM_SMC_FIRST_EXT_IDX]);
		} else {
			__le64 *args64 = args;

			for (i = 0; i < SCM_SMC_N_EXT_ARGS; i++)
				args64[i] = cpu_to_le64(desc->args[i +
							SCM_SMC_FIRST_EXT_IDX]);
		}

		smc.args[SCM_SMC_LAST_REG_IDX] = (phys_addr_t)args;
		flush_cache((unsigned long)args,
			    SCM_SMC_N_EXT_ARGS * sizeof(u64));
	}

	ret = __scm_smc_do(&smc, &smc_res);

	if (args)
		free(args);

	if (ret)
		return ret;

	if (res) {
		res->result[0] = smc_res.a1;
		res->result[1] = smc_res.a2;
		res->result[2] = smc_res.a3;
	}

	return (long)smc_res.a0 ? qcom_scm_remap_error(smc_res.a0) : 0;
}

static enum qcom_scm_convention qcom_scm_detect_convention(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_IS_CALL_AVAIL,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = QCOM_SCM_ARGS(1),
		.args = {
			SCM_SMC_FNID(QCOM_SCM_SVC_INFO,
				     QCOM_SCM_INFO_IS_CALL_AVAIL) |
			(ARM_SMCCC_OWNER_SIP << ARM_SMCCC_OWNER_SHIFT)
		},
	};
	struct qcom_scm_res scm_ret = {0};

	if (IS_ENABLED(CONFIG_ARM64) &&
	    !__qcom_scm_call(&desc, SMC_CONVENTION_ARM_64, &scm_ret) &&
	    scm_ret.result[0] == 1)
		return SMC_CONVENTION_ARM_64;

	if (!__qcom_scm_call(&desc, SMC_CONVENTION_ARM_32, &scm_ret) &&
	    scm_ret.result[0] == 1)
		return SMC_CONVENTION_ARM_32;

	return SMC_CONVENTION_UNKNOWN;
}

static int qcom_scm_get_device(struct udevice **devp)
{
	return uclass_get_device_by_driver(UCLASS_FIRMWARE,
					   DM_DRIVER_GET(qcom_scm), devp);
}

int qcom_scm_call(const struct qcom_scm_desc *desc, struct qcom_scm_res *res)
{
	struct qcom_scm *scm;
	struct udevice *dev;
	int ret;

	ret = qcom_scm_get_device(&dev);
	if (ret)
		return ret;

	scm = dev_get_priv(dev);
	return __qcom_scm_call(desc, scm->convention, res);
}

bool qcom_scm_is_call_available(u32 svc_id, u32 cmd_id)
{
	u32 fnid = SCM_SMC_FNID(svc_id, cmd_id);
	struct qcom_scm_res scm_ret = {0};
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_IS_CALL_AVAIL,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = QCOM_SCM_ARGS(1),
		.args = {
			fnid | (ARM_SMCCC_OWNER_SIP << ARM_SMCCC_OWNER_SHIFT)
		},
	};

	if (qcom_scm_call(&desc, &scm_ret))
		return false;

	return scm_ret.result[0];
}

static int qcom_scm_probe(struct udevice *dev)
{
	struct qcom_scm *scm = dev_get_priv(dev);

	scm->convention = qcom_scm_detect_convention();
	if (scm->convention == SMC_CONVENTION_UNKNOWN)
		return -EOPNOTSUPP;

	return 0;
}

static const struct udevice_id qcom_scm_of_match[] = {
	{ .compatible = "qcom,scm" },
	{ }
};

U_BOOT_DRIVER(qcom_scm) = {
	.name = "qcom_scm",
	.id = UCLASS_FIRMWARE,
	.of_match = qcom_scm_of_match,
	.probe = qcom_scm_probe,
	.priv_auto = sizeof(struct qcom_scm),
};
