// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Linaro Ltd. */

/* Much of this code was adapted from Linux kernel */
/* Copyright (c) 2010-2015,2019 The Linux Foundation.	 All rights reserved.
 */

#include "qcom-scm.h"

#define QCOM_SCM_EBUSY_WAIT_MS 30
#define QCOM_SCM_EBUSY_MAX_RETRY 20

#define SCM_SMC_N_REG_ARGS	4
#define SCM_SMC_FIRST_EXT_IDX	(SCM_SMC_N_REG_ARGS - 1)
#define SCM_SMC_N_EXT_ARGS	(MAX_QCOM_SCM_ARGS - SCM_SMC_N_REG_ARGS + 1)
#define SCM_SMC_FIRST_REG_IDX	2
#define SCM_SMC_LAST_REG_IDX	(SCM_SMC_FIRST_REG_IDX + SCM_SMC_N_REG_ARGS - 1)

/**
 * struct arm_smccc_args
 * @args:	The array of values used in registers in smc instruction
 */
struct arm_smccc_args {
	unsigned long args[8];
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

static int __scm_smc_do(struct arm_smccc_args *smc, struct arm_smccc_res *res,
			bool atomic)
{
	int retry_count = 0;

	do {
		__scm_smc_do_quirk(smc, res);

		if (atomic)
			return 0;

		if (res->a0 == QCOM_SCM_V2_EBUSY) {
			if (retry_count++ > QCOM_SCM_EBUSY_MAX_RETRY)
				break;
			mdelay(QCOM_SCM_EBUSY_WAIT_MS);
		}
	}  while (res->a0 == QCOM_SCM_V2_EBUSY);

	return 0;
}

int qcom_scm_call(const struct qcom_scm_desc *desc,
		  enum qcom_scm_convention qcom_convention,
		  struct qcom_scm_res *res, bool atomic)
{
	int arglen = desc->arginfo & 0xf;
	void *args = NULL;
	int i, ret;
	struct arm_smccc_args smc = {0};
	struct arm_smccc_res smc_res;
	u32 smccc_call_type = atomic ? ARM_SMCCC_FAST_CALL : ARM_SMCCC_STD_CALL;
	u32 qcom_smccc_convention = (qcom_convention == SMC_CONVENTION_ARM_32) ?
				    ARM_SMCCC_SMC_32 : ARM_SMCCC_SMC_64;
	u32 fnid = SCM_SMC_FNID(desc->svc, desc->cmd);

	smc.args[0] = ARM_SMCCC_CALL_VAL(smccc_call_type, qcom_smccc_convention,
					 desc->owner, fnid);
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
		flush_cache((unsigned long)args, SCM_SMC_N_EXT_ARGS * sizeof(u64));
	}

	ret = __scm_smc_do(&smc, &smc_res, atomic);

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

bool qcom_scm_is_call_available(u32 svc_id, u32 cmd_id,
				enum qcom_scm_convention convention)
{
	u32 fnid = SCM_SMC_FNID(svc_id, cmd_id);
	struct qcom_scm_res scm_ret = {0};
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_IS_CALL_AVAIL,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = QCOM_SCM_ARGS(1),
		.args = {
			ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL, convention,
					   ARM_SMCCC_OWNER_SIP, fnid)
		},
	};
	if (qcom_scm_call(&desc, convention, &scm_ret, false))
		return false;
	return scm_ret.result[0];
}
