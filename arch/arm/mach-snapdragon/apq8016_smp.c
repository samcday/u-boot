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
 * Copyright (C) 2015-2025 Linaro Ltd.
 * Author: Sam Day <me@samcday.com>
 */

#define LOG_DEBUG

#include <asm-generic/unaligned.h>
#include <asm/spin_table.h>
#include <cpu_func.h>
#include <dm/ofnode.h>
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

static void qcom_boot_cortex_a53(phys_addr_t acc_base)
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

static bool is_cpu_spin_table(ofnode cpu) {
	const char *enable_method;
	enable_method = ofnode_read_string(cpu, "enable-method");
	return enable_method && strcmp(enable_method, "spin-table") == 0;
}

void apq8016_smp_setup(void)
{
	ofnode cpus, cpu, acc;
	uint32_t mpidr_aff, acc_base, reg;
	int cpu_count = 0;
	int ret;

	if (!qcom_scm_is_call_available(QCOM_SCM_SVC_BOOT, QCOM_SCM_BOOT_SET_ADDR_MC)) {
		log_warning("qcom SCM call not available, SMP disabled\n");
		return;
	}

	cpus = ofnode_path("/cpus");
	if (!ofnode_valid(cpus))
		return;

	for (cpu = ofnode_by_compatible(cpus, "arm,cortex-a53");
	     ofnode_valid(cpu);
	     cpu = ofnode_by_compatible(cpu, "arm,cortex-a53"))
	{
		reg = ofnode_get_addr_index(cpu, 0);
		if (!is_cpu_spin_table(cpu)) {
			log_warning("CPU%d enable-method is not spin-table\n",
				    reg);
			continue;
		}
		if (ofnode_has_property(cpu, "cpu-release-addr")) {
			log_warning("CPU%d already has release-addr\n",
				    reg);
			continue;
		}
		cpu_count++;
	}

	if (!cpu_count) {
		log_info("No CPUs detected with spin-table enable-method\n");
		return;
	}

	mpidr_aff = read_mpidr() & 0xffffff;

	log_info("Setting CPU boot address to 0x%llx\n",
		 (phys_addr_t)&spin_table_reserve_begin);
	ret = qcom_scm_set_boot_addr_mc(&spin_table_reserve_begin,
					QCOM_SCM_BOOT_MC_FLAG_AARCH64 | QCOM_SCM_BOOT_MC_FLAG_COLDBOOT);
	if (ret) {
		log_err("Failed to set CPU boot addr: %d\n", ret);
		return;
	}

	for (cpu = ofnode_by_compatible(cpus, "arm,cortex-a53"); \
	     ofnode_valid(cpu); \
	     cpu = ofnode_by_compatible(cpu, "arm,cortex-a53"))
	{
		reg = ofnode_get_addr_index(cpu, 0);
		if (reg == mpidr_aff) {
			continue;
		}
		if (!is_cpu_spin_table(cpu) ||
		    ofnode_has_property(cpu, "cpu-release-addr")) {
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
