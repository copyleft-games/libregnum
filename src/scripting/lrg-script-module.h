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

#include <glib.h>

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
 * canonically-named hook entry points that forward to the author's functions,
 * so a compiled module presents the same surface as an interpreted script.
 */
#define LRG_DEFINE_SCRIPT(start_fn, update_fn, detach_fn)            \
	G_MODULE_EXPORT void lrg_script_start (void);               \
	G_MODULE_EXPORT void lrg_script_update (double delta);      \
	G_MODULE_EXPORT void lrg_script_detach (void);             \
	G_MODULE_EXPORT void lrg_script_start (void)  { start_fn (); } \
	G_MODULE_EXPORT void lrg_script_update (double delta) { update_fn (delta); } \
	G_MODULE_EXPORT void lrg_script_detach (void) { detach_fn (); }

G_END_DECLS
