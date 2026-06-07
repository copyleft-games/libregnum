/* lrg-editor.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Host-agnostic editor runtime for an #LrgLevel.
 */

#include "lrg-editor.h"
#include "lrg-editor-command.h"
#include "lrg-editor-selection.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-level-serializer.h"
#include <graylib.h>

struct _LrgEditor
{
	GObject parent_instance;

	LrgLevel           *level;
	LrgEditorSelection *selection;
	GPtrArray          *undo_stack;   /* owned LrgEditorCommand* */
	GPtrArray          *redo_stack;   /* owned LrgEditorCommand* */

	LrgEditorTool       tool;
	gdouble             snap_translate;
	gdouble             snap_rotate;
	gdouble             snap_scale;
};

G_DEFINE_FINAL_TYPE (LrgEditor, lrg_editor, G_TYPE_OBJECT)

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_finalize (GObject *object)
{
	LrgEditor *self = LRG_EDITOR (object);

	g_clear_object (&self->level);
	g_clear_object (&self->selection);
	g_clear_pointer (&self->undo_stack, g_ptr_array_unref);
	g_clear_pointer (&self->redo_stack, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_editor_parent_class)->finalize (object);
}

static void
lrg_editor_class_init (LrgEditorClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_finalize;

	/**
	 * LrgEditor::changed:
	 * @self: the editor
	 *
	 * Emitted after any operation that mutates the document or history, so a
	 * host can refresh derived views.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
lrg_editor_init (LrgEditor *self)
{
	self->level      = lrg_level_new (NULL);
	self->selection  = lrg_editor_selection_new ();
	self->undo_stack = g_ptr_array_new_with_free_func (g_object_unref);
	self->redo_stack = g_ptr_array_new_with_free_func (g_object_unref);
	self->tool       = LRG_EDITOR_TOOL_SELECT;
}

LrgEditor *
lrg_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR, NULL);
}

/* ==========================================================================
 * Internal
 * ========================================================================== */

static void
push_command (LrgEditor        *self,
              LrgEditorCommand *cmd)
{
	lrg_editor_command_apply (cmd);
	g_ptr_array_set_size (self->redo_stack, 0);
	g_ptr_array_add (self->undo_stack, cmd);  /* transfer full */
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* ==========================================================================
 * Document
 * ========================================================================== */

LrgLevel *
lrg_editor_get_level (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	return self->level;
}

void
lrg_editor_set_level (LrgEditor *self,
                      LrgLevel  *level)
{
	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_LEVEL (level));

	lrg_editor_selection_clear (self->selection);
	g_ptr_array_set_size (self->undo_stack, 0);
	g_ptr_array_set_size (self->redo_stack, 0);
	g_set_object (&self->level, level);

	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

gboolean
lrg_editor_load_level (LrgEditor    *self,
                       const gchar  *path,
                       GError      **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(LrgLevel)           loaded = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	ser = lrg_level_serializer_new ();
	loaded = lrg_level_serializer_load_from_file (ser, path, error);
	if (loaded == NULL)
		return FALSE;

	lrg_editor_set_level (self, loaded);
	return TRUE;
}

gboolean
lrg_editor_save_level (LrgEditor    *self,
                       const gchar  *path,
                       GError      **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	ser = lrg_level_serializer_new ();
	return lrg_level_serializer_save_to_file (ser, self->level, path, error);
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

LrgEditorSelection *
lrg_editor_get_selection (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	return self->selection;
}

void
lrg_editor_select (LrgEditor *self,
                   LrgNode   *node,
                   gboolean   additive)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	if (node == NULL)
		lrg_editor_selection_clear (self->selection);
	else if (additive)
		lrg_editor_selection_add (self->selection, node);
	else
		lrg_editor_selection_set (self->selection, node);
}

/* ==========================================================================
 * Mutating operations
 * ========================================================================== */

void
lrg_editor_add_node (LrgEditor *self,
                     LrgNode   *node,
                     LrgNode   *parent)
{
	LrgEditorCommand *cmd;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));

	cmd = lrg_editor_command_new_add_node (self->level, node, parent);
	push_command (self, cmd);
	lrg_editor_selection_set (self->selection, node);
}

gboolean
lrg_editor_delete_node (LrgEditor *self,
                        LrgNode   *node)
{
	LrgNode          *parent;
	LrgEditorCommand *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	parent = lrg_node_get_parent (node);
	if (parent == NULL)
		return FALSE;

	lrg_editor_selection_remove (self->selection, node);

	cmd = lrg_editor_command_new_delete_node (self->level, node, parent);
	push_command (self, cmd);
	return TRUE;
}

gboolean
lrg_editor_reparent_node (LrgEditor *self,
                          LrgNode   *node,
                          LrgNode   *new_parent)
{
	LrgNode          *old_parent;
	LrgEditorCommand *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (new_parent), FALSE);

	old_parent = lrg_node_get_parent (node);
	if (old_parent == NULL || old_parent == new_parent)
		return FALSE;

	cmd = lrg_editor_command_new_reparent (node, old_parent, new_parent);
	push_command (self, cmd);
	return TRUE;
}

void
lrg_editor_set_node_transform (LrgEditor    *self,
                               LrgNode      *node,
                               const gfloat *location,
                               const gfloat *rotation,
                               const gfloat *scale)
{
	LrgEditorCommand *cmd;
	GrlVector3       *l, *r, *s;
	gfloat            before[9];
	gfloat            after[9];

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (location != NULL && rotation != NULL && scale != NULL);

	l = lrg_node_get_location (node);
	r = lrg_node_get_rotation (node);
	s = lrg_node_get_scale (node);

	before[0] = l->x; before[1] = l->y; before[2] = l->z;
	before[3] = r->x; before[4] = r->y; before[5] = r->z;
	before[6] = s->x; before[7] = s->y; before[8] = s->z;

	after[0] = location[0]; after[1] = location[1]; after[2] = location[2];
	after[3] = rotation[0]; after[4] = rotation[1]; after[5] = rotation[2];
	after[6] = scale[0];    after[7] = scale[1];    after[8] = scale[2];

	cmd = lrg_editor_command_new_transform (node, before, after);
	push_command (self, cmd);
}

void
lrg_editor_set_node_property (LrgEditor    *self,
                              LrgNode      *node,
                              const gchar  *prop_name,
                              const GValue *value)
{
	GParamSpec       *pspec;
	GValue            before = G_VALUE_INIT;
	LrgEditorCommand *cmd;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (value != NULL);

	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (node), prop_name);
	if (pspec == NULL)
		return;

	g_value_init (&before, pspec->value_type);
	g_object_get_property (G_OBJECT (node), prop_name, &before);

	cmd = lrg_editor_command_new_set_property (G_OBJECT (node), prop_name, &before, value);
	g_value_unset (&before);

	push_command (self, cmd);
}

/* ==========================================================================
 * Undo / redo
 * ========================================================================== */

gboolean
lrg_editor_can_undo (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	return self->undo_stack->len > 0;
}

gboolean
lrg_editor_can_redo (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	return self->redo_stack->len > 0;
}

void
lrg_editor_undo (LrgEditor *self)
{
	LrgEditorCommand *cmd;
	guint             last;

	g_return_if_fail (LRG_IS_EDITOR (self));

	if (self->undo_stack->len == 0)
		return;

	last = self->undo_stack->len - 1;
	cmd = g_object_ref (g_ptr_array_index (self->undo_stack, last));
	g_ptr_array_remove_index (self->undo_stack, last);

	lrg_editor_command_undo (cmd);
	g_ptr_array_add (self->redo_stack, cmd);  /* transfer the ref */

	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_editor_redo (LrgEditor *self)
{
	LrgEditorCommand *cmd;
	guint             last;

	g_return_if_fail (LRG_IS_EDITOR (self));

	if (self->redo_stack->len == 0)
		return;

	last = self->redo_stack->len - 1;
	cmd = g_object_ref (g_ptr_array_index (self->redo_stack, last));
	g_ptr_array_remove_index (self->redo_stack, last);

	lrg_editor_command_apply (cmd);
	g_ptr_array_add (self->undo_stack, cmd);  /* transfer the ref */

	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* ==========================================================================
 * Tool / snap
 * ========================================================================== */

LrgEditorTool
lrg_editor_get_tool (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), LRG_EDITOR_TOOL_SELECT);

	return self->tool;
}

void
lrg_editor_set_tool (LrgEditor     *self,
                     LrgEditorTool  tool)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	self->tool = tool;
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_editor_set_snap (LrgEditor *self,
                     gdouble    translate,
                     gdouble    rotate,
                     gdouble    scale)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	self->snap_translate = translate;
	self->snap_rotate = rotate;
	self->snap_scale = scale;
}

void
lrg_editor_get_snap (LrgEditor *self,
                     gdouble   *translate,
                     gdouble   *rotate,
                     gdouble   *scale)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	if (translate != NULL)
		*translate = self->snap_translate;
	if (rotate != NULL)
		*rotate = self->snap_rotate;
	if (scale != NULL)
		*scale = self->snap_scale;
}
