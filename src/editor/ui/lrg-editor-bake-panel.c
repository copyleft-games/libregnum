/* lrg-editor-bake-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBakePanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-bake-panel.h"

struct _LrgEditorBakePanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorBakePanel, lrg_editor_bake_panel, G_TYPE_OBJECT)

static void
lrg_editor_bake_panel_class_init (LrgEditorBakePanelClass *klass)
{
}

static void
lrg_editor_bake_panel_init (LrgEditorBakePanel *self)
{
}

LrgEditorBakePanel *
lrg_editor_bake_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_BAKE_PANEL, NULL);
}
