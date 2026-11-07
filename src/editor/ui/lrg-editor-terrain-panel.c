/* lrg-editor-terrain-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTerrainPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-terrain-panel.h"

struct _LrgEditorTerrainPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorTerrainPanel, lrg_editor_terrain_panel, G_TYPE_OBJECT)

static void
lrg_editor_terrain_panel_class_init (LrgEditorTerrainPanelClass *klass)
{
}

static void
lrg_editor_terrain_panel_init (LrgEditorTerrainPanel *self)
{
}

LrgEditorTerrainPanel *
lrg_editor_terrain_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_TERRAIN_PANEL, NULL);
}
