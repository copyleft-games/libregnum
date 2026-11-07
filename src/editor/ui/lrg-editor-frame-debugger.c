/* lrg-editor-frame-debugger.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorFrameDebugger: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-frame-debugger.h"

struct _LrgEditorFrameDebugger
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorFrameDebugger, lrg_editor_frame_debugger, G_TYPE_OBJECT)

static void
lrg_editor_frame_debugger_class_init (LrgEditorFrameDebuggerClass *klass)
{
}

static void
lrg_editor_frame_debugger_init (LrgEditorFrameDebugger *self)
{
}

LrgEditorFrameDebugger *
lrg_editor_frame_debugger_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_FRAME_DEBUGGER, NULL);
}
