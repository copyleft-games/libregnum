/* lrg-editor-node-model.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNodeModel: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_NODE_MODEL (lrg_editor_node_model_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorNodeModel, lrg_editor_node_model, LRG, EDITOR_NODE_MODEL, GObject)

/**
 * lrg_editor_node_model_new:
 *
 * Returns: (transfer full): a new #LrgEditorNodeModel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNodeModel * lrg_editor_node_model_new (void);

G_END_DECLS
