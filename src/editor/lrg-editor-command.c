/* lrg-editor-command.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Undoable editor operations.
 */

#include "lrg-editor-command.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include <string.h>

struct _LrgEditorCommand
{
	GObject parent_instance;

	LrgEditorCommandKind kind;

	LrgNode  *node;       /* subject (ref) */
	LrgLevel *level;      /* add/delete (ref) */
	LrgNode  *parent_a;   /* add/delete parent, reparent old (ref) */
	LrgNode  *parent_b;   /* reparent new (ref) */

	gfloat    before[9];  /* transform: loc xyz, rot xyz, scale xyz */
	gfloat    after[9];

	GObject  *target;     /* set-property target (ref) */
	gchar    *prop_name;
	GValue    before_val;
	GValue    after_val;
};

G_DEFINE_FINAL_TYPE (LrgEditorCommand, lrg_editor_command, G_TYPE_OBJECT)

static void
lrg_editor_command_finalize (GObject *object)
{
	LrgEditorCommand *self = LRG_EDITOR_COMMAND (object);

	g_clear_object (&self->node);
	g_clear_object (&self->level);
	g_clear_object (&self->parent_a);
	g_clear_object (&self->parent_b);
	g_clear_object (&self->target);
	g_clear_pointer (&self->prop_name, g_free);
	if (G_IS_VALUE (&self->before_val))
		g_value_unset (&self->before_val);
	if (G_IS_VALUE (&self->after_val))
		g_value_unset (&self->after_val);

	G_OBJECT_CLASS (lrg_editor_command_parent_class)->finalize (object);
}

static void
lrg_editor_command_class_init (LrgEditorCommandClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_command_finalize;
}

static void
lrg_editor_command_init (LrgEditorCommand *self)
{
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

LrgEditorCommand *
lrg_editor_command_new_transform (LrgNode      *node,
                                  const gfloat *before,
                                  const gfloat *after)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);
	g_return_val_if_fail (before != NULL && after != NULL, NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_TRANSFORM;
	self->node = g_object_ref (node);
	memcpy (self->before, before, sizeof self->before);
	memcpy (self->after, after, sizeof self->after);

	return self;
}

LrgEditorCommand *
lrg_editor_command_new_set_property (GObject      *target,
                                     const gchar  *prop_name,
                                     const GValue *before,
                                     const GValue *after)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (G_IS_OBJECT (target), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);
	g_return_val_if_fail (before != NULL && after != NULL, NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_SET_PROPERTY;
	self->target = g_object_ref (target);
	self->prop_name = g_strdup (prop_name);
	g_value_init (&self->before_val, G_VALUE_TYPE (before));
	g_value_copy (before, &self->before_val);
	g_value_init (&self->after_val, G_VALUE_TYPE (after));
	g_value_copy (after, &self->after_val);

	return self;
}

LrgEditorCommand *
lrg_editor_command_new_add_node (LrgLevel *level,
                                 LrgNode  *node,
                                 LrgNode  *parent)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_ADD_NODE;
	self->level = g_object_ref (level);
	self->node = g_object_ref (node);
	self->parent_a = (parent != NULL) ? g_object_ref (parent) : NULL;

	return self;
}

LrgEditorCommand *
lrg_editor_command_new_delete_node (LrgLevel *level,
                                    LrgNode  *node,
                                    LrgNode  *parent)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_DELETE_NODE;
	self->level = g_object_ref (level);
	self->node = g_object_ref (node);
	self->parent_a = (parent != NULL) ? g_object_ref (parent) : NULL;

	return self;
}

LrgEditorCommand *
lrg_editor_command_new_reparent (LrgNode *node,
                                 LrgNode *old_parent,
                                 LrgNode *new_parent)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);
	g_return_val_if_fail (LRG_IS_NODE (old_parent), NULL);
	g_return_val_if_fail (LRG_IS_NODE (new_parent), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_REPARENT;
	self->node = g_object_ref (node);
	self->parent_a = g_object_ref (old_parent);
	self->parent_b = g_object_ref (new_parent);

	return self;
}

LrgEditorCommand *
lrg_editor_command_new_set_visual_param (LrgNode     *node,
                                         const gchar *param,
                                         gdouble      before,
                                         gdouble      after)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);
	g_return_val_if_fail (param != NULL, NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_SET_VISUAL_PARAM;
	self->node = g_object_ref (node);
	self->prop_name = g_strdup (param);
	g_value_init (&self->before_val, G_TYPE_DOUBLE);
	g_value_set_double (&self->before_val, before);
	g_value_init (&self->after_val, G_TYPE_DOUBLE);
	g_value_set_double (&self->after_val, after);

	return self;
}

static void
apply_visual_param (LrgNode      *node,
                    const gchar  *param,
                    const GValue *value)
{
	LrgNodeVisual *visual = lrg_node_get_visual (node);

	if (visual != NULL)
		lrg_node_visual_set_param_double (visual, param,
		                                  g_value_get_double (value));
}

/* ==========================================================================
 * Apply / Undo
 * ========================================================================== */

static void
apply_trs (LrgNode      *node,
           const gfloat *t)
{
	lrg_node_set_location_xyz (node, t[0], t[1], t[2]);
	lrg_node_set_rotation_xyz (node, t[3], t[4], t[5]);
	lrg_node_set_scale_xyz (node, t[6], t[7], t[8]);
}

LrgEditorCommandKind
lrg_editor_command_get_kind (LrgEditorCommand *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), LRG_EDITOR_CMD_TRANSFORM);

	return self->kind;
}

void
lrg_editor_command_apply (LrgEditorCommand *self)
{
	g_return_if_fail (LRG_IS_EDITOR_COMMAND (self));

	switch (self->kind)
	{
	case LRG_EDITOR_CMD_TRANSFORM:
		apply_trs (self->node, self->after);
		break;
	case LRG_EDITOR_CMD_SET_PROPERTY:
		g_object_set_property (self->target, self->prop_name, &self->after_val);
		break;
	case LRG_EDITOR_CMD_ADD_NODE:
		lrg_level_add_node (self->level, self->node, self->parent_a);
		break;
	case LRG_EDITOR_CMD_DELETE_NODE:
		lrg_level_remove_node (self->level, self->node);
		break;
	case LRG_EDITOR_CMD_REPARENT:
		lrg_node_remove_child (self->parent_a, self->node);
		lrg_node_add_child (self->parent_b, self->node);
		break;
	case LRG_EDITOR_CMD_SET_VISUAL_PARAM:
		apply_visual_param (self->node, self->prop_name, &self->after_val);
		break;
	default:
		break;
	}
}

void
lrg_editor_command_undo (LrgEditorCommand *self)
{
	g_return_if_fail (LRG_IS_EDITOR_COMMAND (self));

	switch (self->kind)
	{
	case LRG_EDITOR_CMD_TRANSFORM:
		apply_trs (self->node, self->before);
		break;
	case LRG_EDITOR_CMD_SET_PROPERTY:
		g_object_set_property (self->target, self->prop_name, &self->before_val);
		break;
	case LRG_EDITOR_CMD_ADD_NODE:
		lrg_level_remove_node (self->level, self->node);
		break;
	case LRG_EDITOR_CMD_DELETE_NODE:
		lrg_level_add_node (self->level, self->node, self->parent_a);
		break;
	case LRG_EDITOR_CMD_REPARENT:
		lrg_node_remove_child (self->parent_b, self->node);
		lrg_node_add_child (self->parent_a, self->node);
		break;
	case LRG_EDITOR_CMD_SET_VISUAL_PARAM:
		apply_visual_param (self->node, self->prop_name, &self->before_val);
		break;
	default:
		break;
	}
}

gboolean
lrg_editor_command_merge (LrgEditorCommand *self,
                          LrgEditorCommand *other)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), FALSE);
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (other), FALSE);

	if (self->kind != other->kind)
		return FALSE;

	if (self->kind == LRG_EDITOR_CMD_TRANSFORM && self->node == other->node)
	{
		memcpy (self->after, other->after, sizeof self->after);
		return TRUE;
	}

	if (self->kind == LRG_EDITOR_CMD_SET_VISUAL_PARAM &&
	    self->node == other->node &&
	    g_strcmp0 (self->prop_name, other->prop_name) == 0)
	{
		g_value_copy (&other->after_val, &self->after_val);
		return TRUE;
	}

	if (self->kind == LRG_EDITOR_CMD_SET_PROPERTY &&
	    self->target == other->target &&
	    g_strcmp0 (self->prop_name, other->prop_name) == 0)
	{
		g_value_unset (&self->after_val);
		g_value_init (&self->after_val, G_VALUE_TYPE (&other->after_val));
		g_value_copy (&other->after_val, &self->after_val);
		return TRUE;
	}

	return FALSE;
}

gchar *
lrg_editor_command_describe (LrgEditorCommand *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), NULL);

	switch (self->kind)
	{
	case LRG_EDITOR_CMD_TRANSFORM:
		return g_strdup_printf ("Transform %s",
		                        self->node ? lrg_node_get_name (self->node) : "node");
	case LRG_EDITOR_CMD_SET_PROPERTY:
		return g_strdup_printf ("Set %s", self->prop_name);
	case LRG_EDITOR_CMD_ADD_NODE:
		return g_strdup_printf ("Add %s",
		                        self->node ? lrg_node_get_name (self->node) : "node");
	case LRG_EDITOR_CMD_DELETE_NODE:
		return g_strdup_printf ("Delete %s",
		                        self->node ? lrg_node_get_name (self->node) : "node");
	case LRG_EDITOR_CMD_REPARENT:
		return g_strdup_printf ("Reparent %s",
		                        self->node ? lrg_node_get_name (self->node) : "node");
	case LRG_EDITOR_CMD_SET_VISUAL_PARAM:
		return g_strdup_printf ("Set %s", self->prop_name);
	default:
		return g_strdup ("Command");
	}
}
