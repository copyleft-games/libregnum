/* lrg-editor-fsm-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorFsmEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_FSM_EDITOR (lrg_editor_fsm_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorFsmEditor, lrg_editor_fsm_editor, LRG, EDITOR_FSM_EDITOR, GObject)

/**
 * lrg_editor_fsm_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorFsmEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorFsmEditor * lrg_editor_fsm_editor_new (void);

G_END_DECLS
