/* lrg-dex-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * libdex must be initialized (dex_init ()) before its types exist: until
 * then DexPromise and the other future types are invalid GTypes and the
 * first dex_promise_new () aborts with "cannot create new instance of
 * invalid (non-instantiatable) type". Every libregnum class that hands
 * out DexFutures calls lrg_dex_ensure_init () from its class_init, so an
 * application never has to know libregnum uses libdex. Not installed,
 * not introspected.
 */

#pragma once

#include <glib.h>
#include <libdex.h>

G_BEGIN_DECLS

/*
 * lrg_dex_ensure_init:
 *
 * Calls dex_init () once per process. dex_init () itself may be called
 * again by the application; the once guard only avoids repeating the
 * call from every class that needs it.
 */
static inline void
lrg_dex_ensure_init (void)
{
	static gsize once = 0;

	if (g_once_init_enter (&once))
	{
		dex_init ();
		g_once_init_leave (&once, 1);
	}
}

G_END_DECLS
