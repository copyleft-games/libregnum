/* lrg-script-debugger.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptDebugger: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SCRIPT_DEBUGGER (lrg_script_debugger_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptDebugger, lrg_script_debugger, LRG, SCRIPT_DEBUGGER, GObject)

/**
 * lrg_script_debugger_new:
 *
 * Returns: (transfer full): a new #LrgScriptDebugger
 */
LRG_AVAILABLE_IN_ALL
LrgScriptDebugger * lrg_script_debugger_new (void);

G_END_DECLS
