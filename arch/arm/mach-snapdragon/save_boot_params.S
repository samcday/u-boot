/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Save ARM32 previous bootloader arguments before U-Boot clobbers them.
 */

#include <config.h>
#include <linux/linkage.h>

ENTRY(save_boot_params)
	adr	ip, qcom_prev_bl_args_offset
	ldr	lr, [ip]
	add	ip, ip, lr
	stmia	ip, {r0-r3}
	b	save_boot_params_ret
ENDPROC(save_boot_params)

qcom_prev_bl_args_offset:
	.long	qcom_prev_bl_args - .

	.pushsection .data
	.align 2
	.global qcom_prev_bl_args
qcom_prev_bl_args:
	.space 16
	.popsection
