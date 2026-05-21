/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011 The Chromium OS Authors.
 */

#ifndef __INITCALL_H
#define __INITCALL_H

#include <asm/types.h>
#include <event.h>
#include <hang.h>

_Static_assert(EVT_COUNT < 256, "Can only support 256 event types with 8 bits");

#ifdef CONFIG_INITCALL_DEBUG
#define INITCALL_TRACE(...) printf(__VA_ARGS__)
#else
#define INITCALL_TRACE(...) do { } while (0)
#endif

#define INITCALL(_call) \
	do { \
		INITCALL_TRACE("initcall: %s(): enter %s()\n", __func__, #_call); \
		if (_call()) { \
			printf("%s(): initcall %s() failed\n", __func__, \
			       #_call); \
			hang(); \
		} \
		INITCALL_TRACE("initcall: %s(): leave %s()\n", __func__, #_call); \
	} while (0)

#define INITCALL_EVT(_evt) \
	do { \
		INITCALL_TRACE("initcall: %s(): enter event %d\n", __func__, _evt); \
		if (event_notify_null(_evt)) { \
			printf("%s(): event %d failed\n", __func__, _evt); \
			hang(); \
		} \
		INITCALL_TRACE("initcall: %s(): leave event %d\n", __func__, _evt); \
	} while (0)

#if defined(CONFIG_WATCHDOG) || defined(CONFIG_HW_WATCHDOG)
#define WATCHDOG_INIT() INITCALL(init_func_watchdog_init)
#define WATCHDOG_RESET() INITCALL(init_func_watchdog_reset)
#else
#define WATCHDOG_INIT()
#define WATCHDOG_RESET()
#endif

#endif
