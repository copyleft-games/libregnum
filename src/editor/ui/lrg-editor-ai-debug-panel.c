/* lrg-editor-ai-debug-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorAiDebugPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-ai-debug-panel.h"

struct _LrgEditorAiDebugPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorAiDebugPanel, lrg_editor_ai_debug_panel, G_TYPE_OBJECT)

static void
lrg_editor_ai_debug_panel_class_init (LrgEditorAiDebugPanelClass *klass)
{
}

static void
lrg_editor_ai_debug_panel_init (LrgEditorAiDebugPanel *self)
{
}

LrgEditorAiDebugPanel *
lrg_editor_ai_debug_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_AI_DEBUG_PANEL, NULL);
}
