/* lrg-editor-node-model.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNodeModel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-node-model.h"

struct _LrgEditorNodeModel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorNodeModel, lrg_editor_node_model, G_TYPE_OBJECT)

static void
lrg_editor_node_model_class_init (LrgEditorNodeModelClass *klass)
{
}

static void
lrg_editor_node_model_init (LrgEditorNodeModel *self)
{
}

LrgEditorNodeModel *
lrg_editor_node_model_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_NODE_MODEL, NULL);
}
