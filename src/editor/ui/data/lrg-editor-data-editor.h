/* lrg-editor-data-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDataEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_DATA_EDITOR (lrg_editor_data_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorDataEditor, lrg_editor_data_editor, LRG, EDITOR_DATA_EDITOR, LrgContainer)

/**
 * lrg_editor_data_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorDataEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDataEditor * lrg_editor_data_editor_new (void);

G_END_DECLS
