// SPDX-License-Identifier: GPL-2.0+
/*
 * On MSM8916 devices that lack a PSCI implementation, firing up the secondary
 * cores requires a call to TZ to set the boot address, and some poking of ACPS
 * register block.
 *
 * Copyright (c) 2025 Linaro Ltd.
 */

#include <asm/spin_table.h>
#include <cpu_func.h>
#include <dm/ofnode.h>
#include "qcom-scm.h"

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

int qcom_scm_set_boot_addr_mc(void *entry, unsigned int flags)
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

	return qcom_scm_call(&desc, SMC_CONVENTION_ARM_32, NULL, false);
}

static bool boot_addr_set;

int spin_table_boot_cpu(void *fdt, int cpu_offset)
{
	struct fdtdec_phandle_args acc;
	u32 mpidr_aff, acc_base, reg;
	int ret;

	reg = fdtdec_get_uint(fdt, cpu_offset, "reg", 0);

	if (fdt_node_check_compatible(fdt, cpu_offset, "arm,cortex-a53")) {
		log_warning("CPU%d is not arm,cortex-a53 compatible\n", reg);
		return -EINVAL;
	}

	if (!boot_addr_set) {
		if (!qcom_scm_is_call_available(QCOM_SCM_SVC_BOOT,
						QCOM_SCM_BOOT_SET_ADDR_MC,
						SMC_CONVENTION_ARM_32)) {
			log_warning("BOOT_SET_ADDR_MC unavailable\n");
			return -EPERM;
		}

		debug("Setting CPU boot address to 0x%llx\n",
		      (phys_addr_t)&spin_table_reserve_begin);
		ret = qcom_scm_set_boot_addr_mc(&spin_table_reserve_begin,
						QCOM_SCM_BOOT_MC_FLAG_AARCH64 |
						QCOM_SCM_BOOT_MC_FLAG_COLDBOOT);
		if (ret) {
			log_err("Failed to set CPU boot addr: %d\n", ret);
			return ret;
		}

		boot_addr_set = true;
	}

	mpidr_aff = read_mpidr() & 0xffffff;

	if (reg == mpidr_aff) {
		debug("Skipping boot of current CPU%d\n", reg);
		return 0;
	}

	ret = fdtdec_parse_phandle_with_args(fdt, cpu_offset, "qcom,acc",
					     NULL, 0, 0, &acc);
	if (ret) {
		log_err("Failed to parse qcom,acc phandle: %d\n", reg);
		return ret;
	}

	acc_base = fdtdec_get_addr_size_auto_noparent(fdt, acc.node, "reg", 0,
						      NULL, true);
	if (!acc_base) {
		log_err("Failed to parse qcom,acc regbase\n");
		return -EINVAL;
	}

	log_info("Booting CPU%d @ 0x%x\n", reg, acc_base);
	qcom_boot_cortex_a53(acc_base);
	return 0;
}
