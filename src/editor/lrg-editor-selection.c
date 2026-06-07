/* lrg-editor-selection.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The set of nodes currently selected in an #LrgEditor.
 */

#include "lrg-editor-selection.h"
#include "lrg-node.h"

struct _LrgEditorSelection
{
	GObject parent_instance;

	GPtrArray *nodes;    /* owned refs; last element is primary */
};

G_DEFINE_FINAL_TYPE (LrgEditorSelection, lrg_editor_selection, G_TYPE_OBJECT)

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_editor_selection_finalize (GObject *object)
{
	LrgEditorSelection *self = LRG_EDITOR_SELECTION (object);

	g_clear_pointer (&self->nodes, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_editor_selection_parent_class)->finalize (object);
}

static void
lrg_editor_selection_class_init (LrgEditorSelectionClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_selection_finalize;

	/**
	 * LrgEditorSelection::changed:
	 * @self: the selection
	 *
	 * Emitted whenever the selected set changes.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
lrg_editor_selection_init (LrgEditorSelection *self)
{
	self->nodes = g_ptr_array_new_with_free_func (g_object_unref);
}

LrgEditorSelection *
lrg_editor_selection_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_SELECTION, NULL);
}

void
lrg_editor_selection_add (LrgEditorSelection *self,
                          LrgNode            *node)
{
	g_return_if_fail (LRG_IS_EDITOR_SELECTION (self));
	g_return_if_fail (LRG_IS_NODE (node));

	/* Remove any existing occurrence so it moves to the primary slot. */
	g_ptr_array_remove (self->nodes, node);
	g_ptr_array_add (self->nodes, g_object_ref (node));

	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_editor_selection_set (LrgEditorSelection *self,
                          LrgNode            *node)
{
	g_return_if_fail (LRG_IS_EDITOR_SELECTION (self));
	g_return_if_fail (node == NULL || LRG_IS_NODE (node));

	if (self->nodes->len > 0)
		g_ptr_array_set_size (self->nodes, 0);

	if (node != NULL)
		g_ptr_array_add (self->nodes, g_object_ref (node));

	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

gboolean
lrg_editor_selection_remove (LrgEditorSelection *self,
                             LrgNode            *node)
{
	gboolean removed;

	g_return_val_if_fail (LRG_IS_EDITOR_SELECTION (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	removed = g_ptr_array_remove (self->nodes, node);
	if (removed)
		g_signal_emit (self, signals[SIGNAL_CHANGED], 0);

	return removed;
}

void
lrg_editor_selection_clear (LrgEditorSelection *self)
{
	g_return_if_fail (LRG_IS_EDITOR_SELECTION (self));

	if (self->nodes->len == 0)
		return;

	g_ptr_array_set_size (self->nodes, 0);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

gboolean
lrg_editor_selection_contains (LrgEditorSelection *self,
                               LrgNode            *node)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_SELECTION (self), FALSE);

	for (i = 0; i < self->nodes->len; i++)
		if (g_ptr_array_index (self->nodes, i) == node)
			return TRUE;

	return FALSE;
}

LrgNode *
lrg_editor_selection_get_primary (LrgEditorSelection *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SELECTION (self), NULL);

	if (self->nodes->len == 0)
		return NULL;

	return g_ptr_array_index (self->nodes, self->nodes->len - 1);
}

GPtrArray *
lrg_editor_selection_get_nodes (LrgEditorSelection *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SELECTION (self), NULL);

	return self->nodes;
}

guint
lrg_editor_selection_get_count (LrgEditorSelection *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SELECTION (self), 0);

	return self->nodes->len;
}
