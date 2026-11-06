/* lrg-editor-dungeon-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDungeonEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_DUNGEON_EDITOR (lrg_editor_dungeon_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorDungeonEditor, lrg_editor_dungeon_editor, LRG, EDITOR_DUNGEON_EDITOR, LrgContainer)

/**
 * lrg_editor_dungeon_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorDungeonEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDungeonEditor * lrg_editor_dungeon_editor_new (void);

G_END_DECLS
