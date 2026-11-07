/* lrg-editor-debug-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDebugPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-debug-panel.h"

struct _LrgEditorDebugPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorDebugPanel, lrg_editor_debug_panel, G_TYPE_OBJECT)

static void
lrg_editor_debug_panel_class_init (LrgEditorDebugPanelClass *klass)
{
}

static void
lrg_editor_debug_panel_init (LrgEditorDebugPanel *self)
{
}

LrgEditorDebugPanel *
lrg_editor_debug_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_DEBUG_PANEL, NULL);
}
