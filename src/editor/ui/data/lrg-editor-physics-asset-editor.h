/* lrg-editor-physics-asset-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPhysicsAssetEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_PHYSICS_ASSET_EDITOR (lrg_editor_physics_asset_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorPhysicsAssetEditor, lrg_editor_physics_asset_editor, LRG, EDITOR_PHYSICS_ASSET_EDITOR, GObject)

/**
 * lrg_editor_physics_asset_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorPhysicsAssetEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPhysicsAssetEditor * lrg_editor_physics_asset_editor_new (void);

G_END_DECLS
