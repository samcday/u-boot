// SPDX-License-Identifier: GPL-2.0-only
/*
 * APQ8016 devices lack a PSCI implementation to bring up secondary cores.
 * Instead, a call is made to qcom TZ impl to set the boot address, and some
 * APCS registers are poked to power the core up.
 *
 * Since the Linux kernel has a strict policy of only allowing spin-table and
 * PSCI enablement methods on arm64, we handle the enablement here. Ironically,
 * 90% of the code here was adapted from the kernel's armv7 qcom drivers.
 *
 * Copyright (c) 2010,2015,2019 The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 * Copyright (c) 2024 Linaro Ltd.
 * Author: Sam Day <me@samcday.com>
 */

#define LOG_DEBUG

#include <errno.h>
#include <linux/arm-smccc.h>
#include <memalign.h>
#include <asm-generic/unaligned.h>
#include <linux/delay.h>
#include <asm/spin_table.h>
#include <cpu_func.h>
#include <dm/ofnode.h>
#include <asm/io.h>
#include "qcom_scm.h"

#define APCS_CPU_PWR_CTL	0x04
#define CORE_PWRD_UP		BIT(7)
#define COREPOR_RST		BIT(5)
#define CORE_RST		BIT(4)
#define CORE_MEM_HS		BIT(3)
#define CORE_MEM_CLAMP		BIT(1)
#define CLAMP			BIT(0)

#define APC_PWR_GATE_CTL	0x14
#define GDHS_CNT_SHIFT		24
#define GDHS_EN			BIT(0)

#define APCS_SAW2_VCTL		0x14
#define APCS_SAW2_2_VCTL	0x1c

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

static int __scm_smc_do(struct arm_smccc_args *smc,
			struct arm_smccc_res *res)
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

static int qcom_scm_call(const struct qcom_scm_desc *desc,
			 struct qcom_scm_res *res)
{
	int arglen = desc->arginfo & 0xf;
	__le32 *args = NULL;
	int i, ret;
	struct arm_smccc_args smc = {0};
	struct arm_smccc_res smc_res;

	smc.args[0] = ARM_SMCCC_CALL_VAL(
		ARM_SMCCC_STD_CALL,
		ARM_SMCCC_SMC_32,
		desc->owner,
		SCM_SMC_FNID(desc->svc, desc->cmd));
	smc.args[1] = desc->arginfo;
	for (i = 0; i < SCM_SMC_N_REG_ARGS; i++)
		smc.args[i + SCM_SMC_FIRST_REG_IDX] = desc->args[i];

	if (unlikely(arglen > SCM_SMC_N_REG_ARGS)) {
		args = malloc_cache_aligned(SCM_SMC_N_EXT_ARGS * sizeof(u64));
		if (!args)
			return -ENOMEM;

		for (i = 0; i < SCM_SMC_N_EXT_ARGS; i++)
			args[i] = cpu_to_le32(desc->args[i +
							 SCM_SMC_FIRST_EXT_IDX]);
		smc.args[SCM_SMC_LAST_REG_IDX] = (unsigned long)args;

		flush_cache((unsigned long)args, SCM_SMC_N_EXT_ARGS * sizeof(u64));
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

static int qcom_scm_set_boot_addr_mc(void *entry, unsigned int flags)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SET_ADDR_MC,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = QCOM_SCM_ARGS(6),
		.args = {
			(u64)entry,
			/* Apply to all CPUs in all affinity levels */
			~0ULL, ~0ULL, ~0ULL, ~0ULL,
			flags,
		},
	};

	return qcom_scm_call(&desc, NULL);
}

static bool qcom_scm_is_call_available(u32 svc_id, u32 cmd_id)
{
	int ret;
	struct qcom_scm_res scm_ret = {0};
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_IS_CALL_AVAIL,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = QCOM_SCM_ARGS(1),
		.args = {
			ARM_SMCCC_CALL_VAL(
				ARM_SMCCC_STD_CALL,
				ARM_SMCCC_SMC_32,
				ARM_SMCCC_OWNER_SIP,
				SCM_SMC_FNID(svc_id, cmd_id))
		},
	};
	if (qcom_scm_call(&desc, &scm_ret))
		return false;
	return scm_ret.result[0];
}

static void qcom_boot_cortex_a53(uint32_t acc_base)
{
	u32 reg_val;

	/* Put the CPU into reset. */
	reg_val = CORE_RST | COREPOR_RST | CLAMP | CORE_MEM_CLAMP;
	writel(reg_val, acc_base + APCS_CPU_PWR_CTL);

	/* Turn on the GDHS and set the GDHS_CNT to 16 XO clock cycles */
	writel(GDHS_EN | (0x10 << GDHS_CNT_SHIFT), acc_base + APC_PWR_GATE_CTL);
	/* Wait for the GDHS to settle */
	udelay(2);

	reg_val &= ~CORE_MEM_CLAMP;
	writel(reg_val, acc_base + APCS_CPU_PWR_CTL);
	reg_val |= CORE_MEM_HS;
	writel(reg_val, acc_base + APCS_CPU_PWR_CTL);
	udelay(2);

	reg_val &= ~CLAMP;
	writel(reg_val, acc_base + APCS_CPU_PWR_CTL);
	udelay(2);

	/* Release CPU out of reset and bring it to life. */
	reg_val &= ~(CORE_RST | COREPOR_RST);
	writel(reg_val, acc_base + APCS_CPU_PWR_CTL);
	reg_val |= CORE_PWRD_UP;
	writel(reg_val, acc_base + APCS_CPU_PWR_CTL);
}

void apq8016_smp_setup(void)
{
	ofnode cpus, cpu, acc;
	uint32_t mpidr_aff, acc_base, reg;
	int ret;

	cpus = ofnode_path("/cpus");
	if (!ofnode_valid(cpus))
		return;

	mpidr_aff = read_mpidr() & 0xffffff;

	// TODO: make sure all cpus are spin-table and cortex-a53

	log_info("Setting CPU boot address to 0x%llx\n", &spin_table_reserve_begin);
	ret = qcom_scm_set_boot_addr_mc(&spin_table_reserve_begin,
					QCOM_SCM_BOOT_MC_FLAG_AARCH64 | QCOM_SCM_BOOT_MC_FLAG_COLDBOOT);

	if (ret) {
		log_warning("Failed to set CPU boot addr: %d\n", ret);
		return;
	}

	ofnode_for_each_subnode(cpu, cpus) {
		if (!ofnode_device_is_compatible(cpu, "arm,cortex-a53"))
			continue;

		reg = ofnode_get_addr_index(cpu, 0);
		if (reg == mpidr_aff) {
			log_info("Skipping booting current CPU%d\n", reg);
			continue;
		}
		acc = ofnode_parse_phandle(cpu, "qcom,acc", 0);
		acc_base = 0;
		if (ofnode_valid(acc))
			acc_base = ofnode_get_addr(acc);
		if (!acc_base) {
			log_err("CPU%d is missing ACC node, cannot enable\n", reg);
			continue;
		}
		log_info("Booting CPU%d @ 0x%x\n", reg, acc_base);
		qcom_boot_cortex_a53(acc_base);
	}
}
