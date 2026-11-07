/* lrg-editor-memory-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMemoryPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-memory-panel.h"

struct _LrgEditorMemoryPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorMemoryPanel, lrg_editor_memory_panel, G_TYPE_OBJECT)

static void
lrg_editor_memory_panel_class_init (LrgEditorMemoryPanelClass *klass)
{
}

static void
lrg_editor_memory_panel_init (LrgEditorMemoryPanel *self)
{
}

LrgEditorMemoryPanel *
lrg_editor_memory_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_MEMORY_PANEL, NULL);
}
