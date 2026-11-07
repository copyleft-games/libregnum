/* lrg-script-debugger.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptDebugger: (stub, being written)
 */

#include "config.h"

#include "lrg-script-debugger.h"

struct _LrgScriptDebugger
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgScriptDebugger, lrg_script_debugger, G_TYPE_OBJECT)

static void
lrg_script_debugger_class_init (LrgScriptDebuggerClass *klass)
{
}

static void
lrg_script_debugger_init (LrgScriptDebugger *self)
{
}

LrgScriptDebugger *
lrg_script_debugger_new (void)
{
	return g_object_new (LRG_TYPE_SCRIPT_DEBUGGER, NULL);
}
