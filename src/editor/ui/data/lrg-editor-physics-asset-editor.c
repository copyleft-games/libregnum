/* lrg-editor-physics-asset-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPhysicsAssetEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-physics-asset-editor.h"

struct _LrgEditorPhysicsAssetEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorPhysicsAssetEditor, lrg_editor_physics_asset_editor, G_TYPE_OBJECT)

static void
lrg_editor_physics_asset_editor_class_init (LrgEditorPhysicsAssetEditorClass *klass)
{
}

static void
lrg_editor_physics_asset_editor_init (LrgEditorPhysicsAssetEditor *self)
{
}

LrgEditorPhysicsAssetEditor *
lrg_editor_physics_asset_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_PHYSICS_ASSET_EDITOR, NULL);
}
