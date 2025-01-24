// SPDX-License-Identifier: GPL-2.0

#ifndef __QCOM_PRIV_H__
#define __QCOM_PRIV_H__

#if CONFIG_IS_ENABLED(EFI_HAVE_CAPSULE_SUPPORT)
void qcom_configure_capsule_updates(void);
#endif /* EFI_HAVE_CAPSULE_SUPPORT */

#if CONFIG_IS_ENABLED(OF_LIVE)
/**
 * qcom_of_fixup_nodes() - Fixup Qualcomm DT nodes
 *
 * Adjusts nodes in the live tree to improve compatibility with U-Boot.
 */
void qcom_of_fixup_nodes(void);
#endif /* OF_LIVE */

#endif /* __QCOM_PRIV_H__ */
