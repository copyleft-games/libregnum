/* lrg-editor-physics-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPhysicsPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-physics-panel.h"

struct _LrgEditorPhysicsPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorPhysicsPanel, lrg_editor_physics_panel, G_TYPE_OBJECT)

static void
lrg_editor_physics_panel_class_init (LrgEditorPhysicsPanelClass *klass)
{
}

static void
lrg_editor_physics_panel_init (LrgEditorPhysicsPanel *self)
{
}

LrgEditorPhysicsPanel *
lrg_editor_physics_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_PHYSICS_PANEL, NULL);
}
