/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Qualcomm Embedded USB Debugger support.
 */

#ifndef _QCOM_EUD_H_
#define _QCOM_EUD_H_

#include <linux/types.h>

struct udevice;

#define QCOM_EUD_COM_APPS_ID	0x90

int qcom_eud_get(struct udevice **devp);
int qcom_eud_enable(struct udevice *dev);
int qcom_eud_disable(struct udevice *dev);
bool qcom_eud_is_enabled(struct udevice *dev);
int qcom_eud_com_write(struct udevice *dev, u8 id, const void *buf, size_t len);

#endif /* _QCOM_EUD_H_ */
