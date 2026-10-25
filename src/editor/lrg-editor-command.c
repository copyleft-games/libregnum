/* lrg-editor-command.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Undoable editor operations.
 *
 * Every command is a discriminated record holding exactly what it needs to
 * apply and undo itself.  Structural commands (insert / delete / move) store
 * child indices so undo restores nodes in their original position, which
 * keeps the outliner order and the serialized file stable across undo/redo.
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
	gchar    *label;      /* optional explicit description */
	guint64   serial;     /* modification serial, see get_serial() */

	LrgNode  *node;       /* subject (ref) */
	LrgLevel *level;      /* add/delete (ref) */
	LrgNode  *parent_a;   /* add/delete parent, reparent old (ref) */
	LrgNode  *parent_b;   /* reparent new (ref) */
	gint      index_a;    /* add/delete index, reparent old index */
	gint      index_b;    /* reparent new index */

	gfloat    before[9];  /* transform: loc xyz, rot xyz, scale xyz */
	gfloat    after[9];

	GObject  *target;     /* set-property target (ref) */
	gchar    *prop_name;
	GValue    before_val;
	GValue    after_val;

	GPtrArray *children;  /* group: owned LrgEditorCommand* */

	LrgNode  *snap_before; /* node-state: detached snapshot nodes (owned) */
	LrgNode  *snap_after;
};

G_DEFINE_FINAL_TYPE (LrgEditorCommand, lrg_editor_command, G_TYPE_OBJECT)

/* Process-wide serial source.  Commands normally live on the thread that
 * owns the editor; the mutex keeps serials unique even when tests create
 * editors on worker threads. */
static guint64 serial_counter = 0;

static guint64
next_serial (void)
{
	static GMutex lock;
	guint64       value;

	g_mutex_lock (&lock);
	value = ++serial_counter;
	g_mutex_unlock (&lock);

	return value;
}

static void
lrg_editor_command_finalize (GObject *object)
{
	LrgEditorCommand *self = LRG_EDITOR_COMMAND (object);

	g_clear_pointer (&self->label, g_free);
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
	g_clear_pointer (&self->children, g_ptr_array_unref);
	g_clear_object (&self->snap_before);
	g_clear_object (&self->snap_after);

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
	self->serial  = next_serial ();
	self->index_a = -1;
	self->index_b = -1;
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
	if (LRG_IS_NODE (target))
		self->node = g_object_ref (LRG_NODE (target));
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
	return lrg_editor_command_new_insert_node (level, node, parent, -1);
}

LrgEditorCommand *
lrg_editor_command_new_insert_node (LrgLevel *level,
                                    LrgNode  *node,
                                    LrgNode  *parent,
                                    gint      index)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_ADD_NODE;
	self->level = g_object_ref (level);
	self->node = g_object_ref (node);
	self->parent_a = (parent != NULL) ? g_object_ref (parent) : NULL;
	self->index_a = index;

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
	self->index_a = (parent != NULL) ? lrg_node_get_child_index (parent, node) : -1;

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
	self->index_a = lrg_node_get_child_index (old_parent, node);
	self->index_b = -1;

	return self;
}

LrgEditorCommand *
lrg_editor_command_new_move (LrgNode *node,
                             LrgNode *new_parent,
                             gint     new_index)
{
	LrgEditorCommand *self;
	LrgNode          *old_parent;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);
	g_return_val_if_fail (LRG_IS_NODE (new_parent), NULL);

	old_parent = lrg_node_get_parent (node);
	if (old_parent == NULL)
		return NULL;

	/* A node cannot become a descendant of itself. */
	if (lrg_node_is_ancestor_of (node, new_parent))
		return NULL;

	self = lrg_editor_command_new_reparent (node, old_parent, new_parent);
	self->index_b = new_index;

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

LrgEditorCommand *
lrg_editor_command_new_group (const gchar *label)
{
	LrgEditorCommand *self;

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_GROUP;
	self->label = g_strdup (label);
	self->children = g_ptr_array_new_with_free_func (g_object_unref);

	return self;
}

void
lrg_editor_command_group_append (LrgEditorCommand *self,
                                 LrgEditorCommand *child)
{
	g_return_if_fail (LRG_IS_EDITOR_COMMAND (self));
	g_return_if_fail (self->kind == LRG_EDITOR_CMD_GROUP);
	g_return_if_fail (LRG_IS_EDITOR_COMMAND (child));
	g_return_if_fail (child != self);

	g_ptr_array_add (self->children, child);
	self->serial = next_serial ();
}

guint
lrg_editor_command_group_get_n_children (LrgEditorCommand *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), 0);

	return (self->children != NULL) ? self->children->len : 0;
}

LrgEditorCommand *
lrg_editor_command_group_get_child (LrgEditorCommand *self,
                                    guint             index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), NULL);

	if (self->children == NULL || index >= self->children->len)
		return NULL;

	return g_ptr_array_index (self->children, index);
}

/* A detached copy of a node's own state, used as an immutable snapshot. */
static LrgNode *
copy_snapshot (LrgNode *src)
{
	LrgNode *snap = lrg_node_new (NULL);

	lrg_node_copy_state (snap, src);
	return snap;
}

LrgEditorCommand *
lrg_editor_command_new_node_state (LrgNode     *node,
                                   LrgNode     *before,
                                   LrgNode     *after,
                                   const gchar *label)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);
	g_return_val_if_fail (LRG_IS_NODE (before), NULL);
	g_return_val_if_fail (LRG_IS_NODE (after), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_NODE_STATE;
	self->node = g_object_ref (node);
	self->label = g_strdup (label);
	self->snap_before = copy_snapshot (before);
	self->snap_after = copy_snapshot (after);

	return self;
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

LrgEditorCommandKind
lrg_editor_command_get_kind (LrgEditorCommand *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), LRG_EDITOR_CMD_TRANSFORM);

	return self->kind;
}

LrgNode *
lrg_editor_command_get_node (LrgEditorCommand *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), NULL);

	if (self->node != NULL)
		return self->node;

	if (self->children != NULL)
	{
		for (i = 0; i < self->children->len; i++)
		{
			LrgNode *n = lrg_editor_command_get_node (g_ptr_array_index (self->children, i));

			if (n != NULL)
				return n;
		}
	}

	return NULL;
}

const gchar *
lrg_editor_command_get_label (LrgEditorCommand *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), NULL);

	return self->label;
}

void
lrg_editor_command_set_label (LrgEditorCommand *self,
                              const gchar      *label)
{
	g_return_if_fail (LRG_IS_EDITOR_COMMAND (self));

	g_free (self->label);
	self->label = g_strdup (label);
}

guint64
lrg_editor_command_get_serial (LrgEditorCommand *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), 0);

	return self->serial;
}

/* ==========================================================================
 * Apply / Undo
 * ========================================================================== */

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

static void
apply_trs (LrgNode      *node,
           const gfloat *t)
{
	lrg_node_set_location_xyz (node, t[0], t[1], t[2]);
	lrg_node_set_rotation_xyz (node, t[3], t[4], t[5]);
	lrg_node_set_scale_xyz (node, t[6], t[7], t[8]);
}

/*
 * move_node:
 *
 * Detaches @node from wherever it currently is and inserts it under
 * @parent at @index.  Holding a reference across the detach keeps the node
 * alive when its old parent drops the last array reference.
 */
static void
move_node (LrgNode *node,
           LrgNode *parent,
           gint     index)
{
	LrgNode *cur;

	g_object_ref (node);
	cur = lrg_node_get_parent (node);
	if (cur != NULL)
		lrg_node_remove_child (cur, node);
	lrg_node_insert_child (parent, node, index);
	g_object_unref (node);
}

void
lrg_editor_command_apply (LrgEditorCommand *self)
{
	guint i;

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
		lrg_level_insert_node (self->level, self->node, self->parent_a, self->index_a);
		break;
	case LRG_EDITOR_CMD_DELETE_NODE:
		lrg_level_remove_node (self->level, self->node);
		break;
	case LRG_EDITOR_CMD_REPARENT:
		move_node (self->node, self->parent_b, self->index_b);
		break;
	case LRG_EDITOR_CMD_SET_VISUAL_PARAM:
		apply_visual_param (self->node, self->prop_name, &self->after_val);
		break;
	case LRG_EDITOR_CMD_GROUP:
		for (i = 0; i < self->children->len; i++)
			lrg_editor_command_apply (g_ptr_array_index (self->children, i));
		break;
	case LRG_EDITOR_CMD_NODE_STATE:
		lrg_node_copy_state (self->node, self->snap_after);
		break;
	default:
		break;
	}
}

void
lrg_editor_command_undo (LrgEditorCommand *self)
{
	guint i;

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
		lrg_level_insert_node (self->level, self->node, self->parent_a, self->index_a);
		break;
	case LRG_EDITOR_CMD_REPARENT:
		move_node (self->node, self->parent_a, self->index_a);
		break;
	case LRG_EDITOR_CMD_SET_VISUAL_PARAM:
		apply_visual_param (self->node, self->prop_name, &self->before_val);
		break;
	case LRG_EDITOR_CMD_GROUP:
		/* Reverse order: later children may depend on earlier ones (e.g. a
		 * transform of a node the group inserted). */
		for (i = self->children->len; i > 0; i--)
			lrg_editor_command_undo (g_ptr_array_index (self->children, i - 1));
		break;
	case LRG_EDITOR_CMD_NODE_STATE:
		lrg_node_copy_state (self->node, self->snap_before);
		break;
	default:
		break;
	}
}

gboolean
lrg_editor_command_merge (LrgEditorCommand *self,
                          LrgEditorCommand *other)
{
	gboolean merged = FALSE;

	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), FALSE);
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (other), FALSE);

	if (self->kind != other->kind)
		return FALSE;

	if (self->kind == LRG_EDITOR_CMD_TRANSFORM && self->node == other->node)
	{
		memcpy (self->after, other->after, sizeof self->after);
		merged = TRUE;
	}
	else if (self->kind == LRG_EDITOR_CMD_SET_VISUAL_PARAM &&
	         self->node == other->node &&
	         g_strcmp0 (self->prop_name, other->prop_name) == 0)
	{
		g_value_copy (&other->after_val, &self->after_val);
		merged = TRUE;
	}
	else if (self->kind == LRG_EDITOR_CMD_SET_PROPERTY &&
	         self->target == other->target &&
	         g_strcmp0 (self->prop_name, other->prop_name) == 0)
	{
		g_value_unset (&self->after_val);
		g_value_init (&self->after_val, G_VALUE_TYPE (&other->after_val));
		g_value_copy (&other->after_val, &self->after_val);
		merged = TRUE;
	}
	else if (self->kind == LRG_EDITOR_CMD_NODE_STATE &&
	         self->node == other->node &&
	         g_strcmp0 (self->label, other->label) == 0)
	{
		lrg_node_copy_state (self->snap_after, other->snap_after);
		merged = TRUE;
	}

	if (merged)
		self->serial = next_serial ();

	return merged;
}

static const gchar *
node_name (LrgNode *node)
{
	const gchar *name;

	if (node == NULL)
		return "node";
	name = lrg_node_get_name (node);
	return (name != NULL) ? name : "node";
}

gchar *
lrg_editor_command_describe (LrgEditorCommand *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), NULL);

	if (self->label != NULL)
		return g_strdup (self->label);

	switch (self->kind)
	{
	case LRG_EDITOR_CMD_TRANSFORM:
		return g_strdup_printf ("Transform %s", node_name (self->node));
	case LRG_EDITOR_CMD_SET_PROPERTY:
		return g_strdup_printf ("Set %s", self->prop_name);
	case LRG_EDITOR_CMD_ADD_NODE:
		return g_strdup_printf ("Add %s", node_name (self->node));
	case LRG_EDITOR_CMD_DELETE_NODE:
		return g_strdup_printf ("Delete %s", node_name (self->node));
	case LRG_EDITOR_CMD_REPARENT:
		return g_strdup_printf ("Reparent %s", node_name (self->node));
	case LRG_EDITOR_CMD_SET_VISUAL_PARAM:
		return g_strdup_printf ("Set %s", self->prop_name);
	case LRG_EDITOR_CMD_GROUP:
		if (self->children->len == 1)
			return lrg_editor_command_describe (g_ptr_array_index (self->children, 0));
		return g_strdup_printf ("%u changes", self->children->len);
	case LRG_EDITOR_CMD_NODE_STATE:
		return g_strdup_printf ("Edit %s", node_name (self->node));
	default:
		return g_strdup ("Command");
	}
}
