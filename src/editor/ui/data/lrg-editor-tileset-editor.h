/* lrg-editor-tileset-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTilesetEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_TILESET_EDITOR (lrg_editor_tileset_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorTilesetEditor, lrg_editor_tileset_editor, LRG, EDITOR_TILESET_EDITOR, GObject)

/**
 * lrg_editor_tileset_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorTilesetEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTilesetEditor * lrg_editor_tileset_editor_new (void);

G_END_DECLS
