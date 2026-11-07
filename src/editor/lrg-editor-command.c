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
#include <glib/gstdio.h>
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-node-tilemap.h"
#include "lrg-timeline.h"
#include "lrg-prefab.h"
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

	GArray   *tiles;       /* tiles: TileChange, layer in index_a */

	LrgTimeline *tl_before; /* timeline: owned copies (nullable) */
	LrgTimeline *tl_after;

	struct _TreeSnap *tree_before; /* prefab-sync: subtree snapshots (owned) */
	struct _TreeSnap *tree_after;

	gchar    *file_before;  /* prefab-file: contents (owned, nullable) */
	gchar    *file_after;
	gboolean  file_create;  /* prefab-file: undo deletes the file (0.9.0) */

	GPtrArray *marks;       /* OverrideMark*: override-set changes (nullable) */
};

/*
 * TreeSnap:
 *
 * One node of a prefab-sync snapshot: the live node (referenced, so a node
 * the sync removed survives for undo), a detached copy of its own state,
 * a copy of its prefab link, and the snapshots of its children in order.
 */
typedef struct _TreeSnap
{
	LrgNode       *node;
	LrgNode       *state;
	LrgPrefabLink *link;
	GPtrArray     *children;  /* TreeSnap* */
} TreeSnap;

/* An override-set change carried by any command (see add_override_mark). */
typedef struct
{
	LrgNode  *owner;
	gchar    *key;
	gboolean  remove;
} OverrideMark;

typedef struct
{
	gint  x;
	gint  y;
	guint before;
	guint after;
} TileChange;

G_DEFINE_FINAL_TYPE (LrgEditorCommand, lrg_editor_command, G_TYPE_OBJECT)

static void
tree_snap_free (gpointer data)
{
	TreeSnap *snap = data;

	if (snap == NULL)
		return;
	g_clear_object (&snap->node);
	g_clear_object (&snap->state);
	g_clear_object (&snap->link);
	g_clear_pointer (&snap->children, g_ptr_array_unref);
	g_free (snap);
}

static void
override_mark_free (gpointer data)
{
	OverrideMark *mark = data;

	g_clear_object (&mark->owner);
	g_free (mark->key);
	g_free (mark);
}

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
	g_clear_pointer (&self->tiles, g_array_unref);
	g_clear_object (&self->tl_before);
	g_clear_object (&self->tl_after);
	g_clear_pointer (&self->tree_before, tree_snap_free);
	g_clear_pointer (&self->tree_after, tree_snap_free);
	g_clear_pointer (&self->file_before, g_free);
	g_clear_pointer (&self->file_after, g_free);
	g_clear_pointer (&self->marks, g_ptr_array_unref);

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

LrgEditorCommand *
lrg_editor_command_new_tiles (LrgNode     *node,
                              guint        layer,
                              const gchar *label)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_TILES;
	self->node = g_object_ref (node);
	self->index_a = (gint) layer;
	self->label = g_strdup (label);
	self->tiles = g_array_new (FALSE, FALSE, sizeof (TileChange));
	return self;
}

void
lrg_editor_command_tiles_add (LrgEditorCommand *self,
                              gint              x,
                              gint              y,
                              guint             before,
                              guint             after)
{
	TileChange c;

	g_return_if_fail (LRG_IS_EDITOR_COMMAND (self));
	g_return_if_fail (self->kind == LRG_EDITOR_CMD_TILES);

	c.x = x;
	c.y = y;
	c.before = before;
	c.after = after;
	g_array_append_val (self->tiles, c);
}

guint
lrg_editor_command_tiles_get_n_changes (LrgEditorCommand *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), 0);

	return (self->tiles != NULL) ? self->tiles->len : 0;
}

LrgEditorCommand *
lrg_editor_command_new_timeline (LrgLevel    *level,
                                 LrgTimeline *before,
                                 LrgTimeline *after,
                                 const gchar *label)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);
	g_return_val_if_fail (before == NULL || LRG_IS_TIMELINE (before), NULL);
	g_return_val_if_fail (after == NULL || LRG_IS_TIMELINE (after), NULL);
	g_return_val_if_fail (before != NULL || after != NULL, NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_TIMELINE;
	self->level = g_object_ref (level);
	self->label = g_strdup (label);
	self->tl_before = (before != NULL) ? lrg_timeline_copy (before) : NULL;
	self->tl_after = (after != NULL) ? lrg_timeline_copy (after) : NULL;
	return self;
}

/* --------------------------------------------------------------------------
 * Linked prefabs
 * -------------------------------------------------------------------------- */

LrgEditorCommand *
lrg_editor_command_new_prefab_override (LrgNode     *owner,
                                        const gchar *key,
                                        gboolean     add)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (LRG_IS_NODE (owner), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* The whole effect is the mark; apply/undo handle marks generically. */
	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_PREFAB_OVERRIDE;
	self->node = g_object_ref (owner);
	self->prop_name = g_strdup (key);
	lrg_editor_command_add_override_mark (self, owner, key, !add);
	return self;
}

static TreeSnap *
tree_snap_capture (LrgNode *node)
{
	TreeSnap      *snap = g_new0 (TreeSnap, 1);
	LrgPrefabLink *link = lrg_node_get_prefab_link (node);
	GPtrArray     *children = lrg_node_get_children (node);
	guint          i;

	snap->node = g_object_ref (node);
	snap->state = lrg_node_new (NULL);
	lrg_node_copy_state (snap->state, node);
	snap->link = (link != NULL) ? lrg_prefab_link_copy (link) : NULL;
	snap->children = g_ptr_array_new_with_free_func (tree_snap_free);
	for (i = 0; i < children->len; i++)
		g_ptr_array_add (snap->children, tree_snap_capture (g_ptr_array_index (children, i)));

	return snap;
}

/* TRUE when @node is the node of one of @snap's child snapshots. */
static gboolean
snap_has_child (TreeSnap *snap,
                LrgNode  *node)
{
	guint i;

	for (i = 0; i < snap->children->len; i++)
		if (((TreeSnap *) g_ptr_array_index (snap->children, i))->node == node)
			return TRUE;

	return FALSE;
}

static gpointer
ref_node (gconstpointer src,
          gpointer      data)
{
	return g_object_ref ((gpointer) src);
}

/*
 * tree_snap_restore:
 *
 * Puts the live subtree back into the snapshot's shape: own state and link
 * of every node, then the exact child list (removing extra children and
 * re-inserting missing ones through @level so its signals fire).
 */
static void
tree_snap_restore (LrgLevel *level,
                   TreeSnap *snap)
{
	g_autoptr(GPtrArray) current = NULL;
	guint                i;

	if (!lrg_node_state_equal (snap->node, snap->state))
		lrg_node_copy_state (snap->node, snap->state);
	if (!lrg_prefab_link_equal (lrg_node_get_prefab_link (snap->node), snap->link))
	{
		g_autoptr(LrgPrefabLink) copy = (snap->link != NULL) ? lrg_prefab_link_copy (snap->link) : NULL;

		lrg_node_set_prefab_link (snap->node, copy);
	}

	/* Children that should not be here. g_ptr_array_copy() keeps the
	 * source's free func (unref), so take a reference per element. */
	current = g_ptr_array_copy (lrg_node_get_children (snap->node), ref_node, NULL);
	for (i = 0; i < current->len; i++)
	{
		LrgNode *c = g_ptr_array_index (current, i);

		if (snap_has_child (snap, c))
			continue;
		if (level != NULL)
			lrg_level_remove_node (level, c);
		else
			lrg_node_remove_child (snap->node, c);
	}

	/* Then each snapshot child at its index. */
	for (i = 0; i < snap->children->len; i++)
	{
		TreeSnap *cs = g_ptr_array_index (snap->children, i);
		gint      idx = lrg_node_get_child_index (snap->node, cs->node);

		if (idx == (gint) i)
			continue;

		g_object_ref (cs->node);
		if (idx >= 0)
		{
			lrg_node_remove_child (snap->node, cs->node);
			lrg_node_insert_child (snap->node, cs->node, (gint) i);
		}
		else
		{
			LrgNode *elsewhere = lrg_node_get_parent (cs->node);

			if (elsewhere != NULL)
				lrg_node_remove_child (elsewhere, cs->node);
			if (level != NULL)
				lrg_level_insert_node (level, cs->node, snap->node, (gint) i);
			else
				lrg_node_insert_child (snap->node, cs->node, (gint) i);
		}
		g_object_unref (cs->node);
	}

	for (i = 0; i < snap->children->len; i++)
		tree_snap_restore (level, g_ptr_array_index (snap->children, i));
}

LrgEditorCommand *
lrg_editor_command_new_prefab_sync (LrgLevel    *level,
                                    LrgNode     *root,
                                    const gchar *label)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (level == NULL || LRG_IS_LEVEL (level), NULL);
	g_return_val_if_fail (LRG_IS_NODE (root), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_PREFAB_SYNC;
	self->level = (level != NULL) ? g_object_ref (level) : NULL;
	self->node = g_object_ref (root);
	self->label = g_strdup (label);
	self->tree_before = tree_snap_capture (root);
	return self;
}

void
lrg_editor_command_prefab_sync_capture (LrgEditorCommand *self)
{
	g_return_if_fail (LRG_IS_EDITOR_COMMAND (self));
	g_return_if_fail (self->kind == LRG_EDITOR_CMD_PREFAB_SYNC);

	g_clear_pointer (&self->tree_after, tree_snap_free);
	self->tree_after = tree_snap_capture (self->node);
}

LrgEditorCommand *
lrg_editor_command_new_prefab_file (const gchar *path,
                                    const gchar *before,
                                    const gchar *after)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (after != NULL, NULL);

	self = g_object_new (LRG_TYPE_EDITOR_COMMAND, NULL);
	self->kind = LRG_EDITOR_CMD_PREFAB_FILE;
	self->prop_name = g_strdup (path);
	self->file_before = g_strdup (before);
	self->file_after = g_strdup (after);
	return self;
}

LrgEditorCommand *
lrg_editor_command_new_prefab_create (const gchar *path,
                                      const gchar *contents)
{
	LrgEditorCommand *self;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (contents != NULL, NULL);

	/* A prefab-file command whose "before" is "no file": undo removes the
	 * file it created (Create Variant), redo writes it again. */
	self = lrg_editor_command_new_prefab_file (path, NULL, contents);
	self->file_create = TRUE;
	return self;
}

/* Undo of a created prefab file: remove it (only if it still holds what
 * the command wrote, so a file edited since is never lost). */
static void
remove_created_file (const gchar *path,
                     const gchar *contents)
{
	g_autofree gchar *now = NULL;

	if (!g_file_get_contents (path, &now, NULL, NULL))
		return;
	if (g_strcmp0 (now, contents) != 0)
	{
		g_warning ("Not removing %s on undo: it changed since it was created", path);
		return;
	}
	if (g_remove (path) != 0)
		g_warning ("Could not remove %s on undo", path);
}

static void
write_prefab_file (const gchar *path,
                   const gchar *contents)
{
	g_autoptr(GError) error = NULL;

	if (contents == NULL)
		return;
	if (!g_file_set_contents (path, contents, -1, &error))
		g_warning ("Could not write prefab %s: %s", path, error->message);
}

void
lrg_editor_command_add_override_mark (LrgEditorCommand *self,
                                      LrgNode          *owner,
                                      const gchar      *key,
                                      gboolean          remove)
{
	OverrideMark *mark;

	g_return_if_fail (LRG_IS_EDITOR_COMMAND (self));
	g_return_if_fail (LRG_IS_NODE (owner));
	g_return_if_fail (key != NULL);

	if (self->marks == NULL)
		self->marks = g_ptr_array_new_with_free_func (override_mark_free);

	mark = g_new0 (OverrideMark, 1);
	mark->owner = g_object_ref (owner);
	mark->key = g_strdup (key);
	mark->remove = remove;
	g_ptr_array_add (self->marks, mark);
}

guint
lrg_editor_command_get_n_override_marks (LrgEditorCommand *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), 0);

	return (self->marks != NULL) ? self->marks->len : 0;
}

/* Applies (or, for @undo, reverts in reverse order) the override marks. */
static void
apply_marks (LrgEditorCommand *self,
             gboolean          undo)
{
	guint i;

	if (self->marks == NULL)
		return;

	for (i = 0; i < self->marks->len; i++)
	{
		OverrideMark  *m = g_ptr_array_index (self->marks, undo ? self->marks->len - 1 - i : i);
		LrgPrefabLink *link = lrg_node_get_prefab_link (m->owner);
		gboolean       add = (m->remove == undo);

		if (link == NULL)
			continue;
		if (add)
			lrg_prefab_link_add_override (link, m->key);
		else
			lrg_prefab_link_remove_override (link, m->key);
	}
}

/* --------------------------------------------------------------------------
 * Automatic override marks
 * -------------------------------------------------------------------------- */

/* Cheap reject: is any ancestor (or @node) a linked instance root? */
static gboolean
in_linked_tree (LrgNode *node)
{
	for (; node != NULL; node = lrg_node_get_parent (node))
		if (lrg_node_get_prefab_link (node) != NULL)
			return TRUE;

	return FALSE;
}

/* Marks @path of @node overridden unless already covered. Root-implicit
 * paths (name, location, rotation of the instance root) are skipped. */
static guint
mark_path (LrgEditorCommand *self,
           LrgNode          *node,
           const gchar      *path)
{
	LrgNode          *owner = lrg_prefab_find_owner (node);
	g_autofree gchar *key = NULL;

	if (owner == NULL)
		return 0;
	if (owner == node &&
	    (g_strcmp0 (path, "name") == 0 ||
	     g_strcmp0 (path, "transform/location") == 0 ||
	     g_strcmp0 (path, "transform/rotation") == 0))
		return 0;

	key = lrg_prefab_make_override_key (owner, node, path);
	if (lrg_prefab_link_has_override (lrg_node_get_prefab_link (owner), key))
		return 0;

	lrg_editor_command_add_override_mark (self, owner, key, FALSE);
	return 1;
}

/* A node leaves @parent: removal of a mirrored node, or un-adding. */
static guint
mark_leave (LrgEditorCommand *self,
            LrgNode          *node,
            LrgNode          *parent)
{
	LrgNode          *owner;
	LrgPrefabLink    *link;
	const gchar      *sg;
	g_autofree gchar *path = NULL;
	g_autofree gchar *key = NULL;

	if (parent == NULL || !in_linked_tree (parent))
		return 0;
	owner = lrg_prefab_find_owner (parent);
	if (owner == NULL)
		return 0;

	link = lrg_node_get_prefab_link (owner);
	sg = lrg_prefab_link_map_lookup (link, lrg_node_get_guid (node));
	if (sg != NULL && node != owner)
		return mark_path (self, parent, (path = g_strconcat ("children/-", sg, NULL)));

	/* An instance-added child going away takes its "+" key with it. */
	path = g_strconcat ("children/+", lrg_node_get_guid (node), NULL);
	key = lrg_prefab_make_override_key (owner, parent, path);
	if (lrg_prefab_link_contains_override (link, key))
	{
		lrg_editor_command_add_override_mark (self, owner, key, TRUE);
		return 1;
	}

	return 0;
}

/* A node arrives under @parent. */
static guint
mark_arrive (LrgEditorCommand *self,
             LrgNode          *node,
             LrgNode          *parent)
{
	g_autofree gchar *path = NULL;

	if (parent == NULL || !in_linked_tree (parent))
		return 0;

	path = g_strconcat ("children/+", lrg_node_get_guid (node), NULL);
	return mark_path (self, parent, path);
}

static const gchar *const trs_paths[3] = {
	"transform/location", "transform/rotation", "transform/scale"
};

guint
lrg_editor_command_mark_prefab_overrides (LrgEditorCommand *self)
{
	guint count = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_COMMAND (self), 0);

	switch (self->kind)
	{
	case LRG_EDITOR_CMD_GROUP:
		for (i = 0; i < self->children->len; i++)
			count += lrg_editor_command_mark_prefab_overrides (g_ptr_array_index (self->children, i));
		return count;
	case LRG_EDITOR_CMD_ADD_NODE:
		return mark_arrive (self, self->node, self->parent_a);
	case LRG_EDITOR_CMD_DELETE_NODE:
		return mark_leave (self, self->node, self->parent_a);
	case LRG_EDITOR_CMD_REPARENT:
		if (self->parent_a == self->parent_b)
			return 0;
		count = mark_leave (self, self->node, self->parent_a);
		return count + mark_arrive (self, self->node, self->parent_b);
	default:
		break;
	}

	if (self->node == NULL || !in_linked_tree (self->node))
		return 0;

	switch (self->kind)
	{
	case LRG_EDITOR_CMD_TRANSFORM:
		for (i = 0; i < 3; i++)
			if (memcmp (self->before + i * 3, self->after + i * 3, 3 * sizeof (gfloat)) != 0)
				count += mark_path (self, self->node, trs_paths[i]);
		break;
	case LRG_EDITOR_CMD_SET_PROPERTY:
		if (g_strcmp0 (self->prop_name, "name") == 0 ||
		    g_strcmp0 (self->prop_name, "visible") == 0 ||
		    g_strcmp0 (self->prop_name, "visual") == 0)
			count += mark_path (self, self->node, self->prop_name);
		else if (g_strcmp0 (self->prop_name, "is-2d") == 0 ||
		         g_strcmp0 (self->prop_name, "is_2d") == 0)
			count += mark_path (self, self->node, "is_2d");
		break;
	case LRG_EDITOR_CMD_SET_VISUAL_PARAM:
		{
			g_autofree gchar *path = g_strconcat ("visual/params/", self->prop_name, NULL);

			count += mark_path (self, self->node, path);
		}
		break;
	case LRG_EDITOR_CMD_TILES:
		count += mark_path (self, self->node, "visual/tilemap");
		break;
	case LRG_EDITOR_CMD_NODE_STATE:
		{
			LrgNode      *owner = lrg_prefab_find_owner (self->node);
			g_auto(GStrv) paths = NULL;

			if (owner == NULL)
				break;
			paths = lrg_prefab_diff_node_state (self->snap_after, self->snap_before,
			                                    owner == self->node);
			for (i = 0; paths != NULL && paths[i] != NULL; i++)
				count += mark_path (self, self->node, paths[i]);
		}
		break;
	default:
		break;
	}

	return count;
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

/* Writes one side of a tile command (after on apply, before on undo,
 * in reverse so repeated cells end at their first state). */
static void
apply_tiles (LrgEditorCommand *self,
             gboolean          undo)
{
	LrgNodeVisual  *visual = lrg_node_get_visual (self->node);
	LrgNodeTilemap *tilemap = (visual != NULL) ? lrg_node_visual_get_tilemap (visual) : NULL;
	guint           i;

	if (tilemap == NULL)
		return;
	for (i = 0; i < self->tiles->len; i++)
	{
		TileChange *c = &g_array_index (self->tiles, TileChange, undo ? self->tiles->len - 1 - i : i);

		lrg_node_tilemap_set_tile (tilemap, (guint) self->index_a, c->x, c->y, undo ? c->before : c->after);
	}
}

/* Replaces the live timeline in state @from by a copy of @to (either may
 * be NULL: add / remove), keeping its position. */
static void
swap_timeline (LrgLevel    *level,
               LrgTimeline *from,
               LrgTimeline *to)
{
	gint idx = (from != NULL) ? lrg_level_find_timeline (level, lrg_timeline_get_name (from)) : -1;

	if (idx >= 0)
		lrg_level_remove_timeline (level, (guint) idx);
	if (to != NULL)
	{
		g_autoptr(LrgTimeline) copy = lrg_timeline_copy (to);

		lrg_level_insert_timeline (level, copy, idx);
	}
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
	case LRG_EDITOR_CMD_TILES:
		apply_tiles (self, FALSE);
		break;
	case LRG_EDITOR_CMD_TIMELINE:
		swap_timeline (self->level, self->tl_before, self->tl_after);
		break;
	case LRG_EDITOR_CMD_PREFAB_SYNC:
		/* First execution: the subtree already holds the result. */
		if (self->tree_after == NULL)
			self->tree_after = tree_snap_capture (self->node);
		else
			tree_snap_restore (self->level, self->tree_after);
		break;
	case LRG_EDITOR_CMD_PREFAB_FILE:
		write_prefab_file (self->prop_name, self->file_after);
		break;
	default:
		break;
	}

	apply_marks (self, FALSE);
}

void
lrg_editor_command_undo (LrgEditorCommand *self)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_COMMAND (self));

	/* Marks were applied last, so they are undone first. */
	apply_marks (self, TRUE);

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
	case LRG_EDITOR_CMD_TILES:
		apply_tiles (self, TRUE);
		break;
	case LRG_EDITOR_CMD_TIMELINE:
		swap_timeline (self->level, self->tl_after, self->tl_before);
		break;
	case LRG_EDITOR_CMD_PREFAB_SYNC:
		if (self->tree_after == NULL)
			self->tree_after = tree_snap_capture (self->node);
		tree_snap_restore (self->level, self->tree_before);
		break;
	case LRG_EDITOR_CMD_PREFAB_FILE:
		if (self->file_create)
			remove_created_file (self->prop_name, self->file_after);
		else
			write_prefab_file (self->prop_name, self->file_before);
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
	guint    i;

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
	else if (self->kind == LRG_EDITOR_CMD_TILES && self->node == other->node &&
	         self->index_a == other->index_a && g_strcmp0 (self->label, other->label) == 0)
	{
		g_array_append_vals (self->tiles, other->tiles->data, other->tiles->len);
		merged = TRUE;
	}
	else if (self->kind == LRG_EDITOR_CMD_TIMELINE && self->level == other->level &&
	         self->tl_after != NULL && other->tl_before != NULL &&
	         g_strcmp0 (lrg_timeline_get_name (self->tl_after), lrg_timeline_get_name (other->tl_before)) == 0 &&
	         g_strcmp0 (self->label, other->label) == 0)
	{
		/* A drag (key time/value): keep the first before, take the last after. */
		g_clear_object (&self->tl_after);
		self->tl_after = (other->tl_after != NULL) ? lrg_timeline_copy (other->tl_after) : NULL;
		merged = TRUE;
	}

	if (merged)
	{
		self->serial = next_serial ();

		/* Keep the override-set changes of both halves. */
		if (other->marks != NULL)
		{
			for (i = 0; i < other->marks->len; i++)
			{
				OverrideMark *m = g_ptr_array_index (other->marks, i);

				lrg_editor_command_add_override_mark (self, m->owner, m->key, m->remove);
			}
		}
	}

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
	case LRG_EDITOR_CMD_TILES:
		return g_strdup_printf ("Edit tiles of %s", node_name (self->node));
	case LRG_EDITOR_CMD_TIMELINE:
		return g_strdup_printf ("Edit timeline %s",
		                        lrg_timeline_get_name (self->tl_after != NULL ? self->tl_after : self->tl_before));
	case LRG_EDITOR_CMD_PREFAB_OVERRIDE:
		return g_strdup_printf ("%s %s",
		                        (self->marks != NULL && self->marks->len > 0 &&
		                         ((OverrideMark *) g_ptr_array_index (self->marks, 0))->remove)
		                        ? "Clear override" : "Override",
		                        self->prop_name);
	case LRG_EDITOR_CMD_PREFAB_SYNC:
		return g_strdup_printf ("Sync prefab %s", node_name (self->node));
	case LRG_EDITOR_CMD_PREFAB_FILE:
		{
			g_autofree gchar *base = g_path_get_basename (self->prop_name);

			return g_strdup_printf ("%s %s", self->file_create ? "Create" : "Write", base);
		}
	default:
		return g_strdup ("Command");
	}
}
