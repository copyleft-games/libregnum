/* lrg-editor-input-map-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorInputMapEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_INPUT_MAP_EDITOR (lrg_editor_input_map_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorInputMapEditor, lrg_editor_input_map_editor, LRG, EDITOR_INPUT_MAP_EDITOR, LrgContainer)

/**
 * lrg_editor_input_map_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorInputMapEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorInputMapEditor * lrg_editor_input_map_editor_new (void);

G_END_DECLS
