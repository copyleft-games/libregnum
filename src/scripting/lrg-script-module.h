/* lrg-script-module.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Authoring contract for per-object scripts.
 *
 * A script attached to an editor node (an #LrgScriptComponent) is expected to
 * provide three lifecycle hooks, called by the component:
 *
 *  - "lrg_script_start"  — once, when the script is attached/started
 *  - "lrg_script_update" — each frame, with the frame delta
 *  - "lrg_script_detach" — once, when the script is detached
 *
 * Interpreted backends (Lua/Python/Gjs) expose these as global functions of the
 * matching names. Compiled backends (Crispy) may use the #LRG_DEFINE_SCRIPT
 * convenience macro to emit suitably-named entry points. The hook names below
 * are the canonical strings the component looks up via
 * lrg_scripting_call_function().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gmodule.h>

G_BEGIN_DECLS

/**
 * LRG_SCRIPT_HOOK_START:
 *
 * Canonical name of the start hook a script provides.
 */
#define LRG_SCRIPT_HOOK_START  "lrg_script_start"

/**
 * LRG_SCRIPT_HOOK_UPDATE:
 *
 * Canonical name of the per-frame update hook a script provides.
 */
#define LRG_SCRIPT_HOOK_UPDATE "lrg_script_update"

/**
 * LRG_SCRIPT_HOOK_DETACH:
 *
 * Canonical name of the detach hook a script provides.
 */
#define LRG_SCRIPT_HOOK_DETACH "lrg_script_detach"

/**
 * LRG_DEFINE_SCRIPT:
 * @start_fn: function to run on start (signature `void (void)`)
 * @update_fn: function to run each frame (signature `void (double delta)`)
 * @detach_fn: function to run on detach (signature `void (void)`)
 *
 * Convenience macro for compiled (Crispy / native) scripts: emits the
 * canonically-named hook entry points, following the #LrgNativeFunction call
 * ABI, that forward to the author's functions.  A compiled module therefore
 * presents the same surface as an interpreted script.  The update hook reads
 * its delta from the first argument (any numeric #GValue).
 */
#define LRG_DEFINE_SCRIPT(start_fn, update_fn, detach_fn)                        \
	G_MODULE_EXPORT gboolean lrg_script_start (struct _LrgScripting *s, guint n, \
	    const GValue *a, GValue *r, GError **e);                                 \
	G_MODULE_EXPORT gboolean lrg_script_update (struct _LrgScripting *s, guint n,\
	    const GValue *a, GValue *r, GError **e);                                 \
	G_MODULE_EXPORT gboolean lrg_script_detach (struct _LrgScripting *s, guint n,\
	    const GValue *a, GValue *r, GError **e);                                 \
	G_MODULE_EXPORT gboolean lrg_script_start (struct _LrgScripting *s, guint n, \
	    const GValue *a, GValue *r, GError **e)                                  \
	{ (void) s; (void) n; (void) a; (void) r; (void) e; start_fn (); return TRUE; } \
	G_MODULE_EXPORT gboolean lrg_script_update (struct _LrgScripting *s, guint n,\
	    const GValue *a, GValue *r, GError **e)                                  \
	{                                                                            \
		GValue d = G_VALUE_INIT;                                                 \
		double delta = 0.0;                                                      \
		(void) s; (void) r; (void) e;                                            \
		if (n > 0 && g_value_type_transformable (G_VALUE_TYPE (&a[0]), G_TYPE_DOUBLE)) \
		{                                                                        \
			g_value_init (&d, G_TYPE_DOUBLE);                                    \
			g_value_transform (&a[0], &d);                                       \
			delta = g_value_get_double (&d);                                     \
		}                                                                        \
		update_fn (delta);                                                       \
		return TRUE;                                                             \
	}                                                                            \
	G_MODULE_EXPORT gboolean lrg_script_detach (struct _LrgScripting *s, guint n,\
	    const GValue *a, GValue *r, GError **e)                                  \
	{ (void) s; (void) n; (void) a; (void) r; (void) e; detach_fn (); return TRUE; }

G_END_DECLS
