/* lrg-editor-node-model.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNodeModel: the node tree of a level as one GListStore of
 * children per parent node, kept in sync incrementally.
 *
 * Bookkeeping: every node the model has seen gets an Entry (a strong
 * reference, the store of its own children once asked for, a "notify"
 * handler and the parent whose store currently shows it). Every change
 * ends in reconcile_store(), which compares a store with the shown
 * children of its parent and applies the smallest splice (common prefix
 * and suffix kept), so views keep their rows, expansion and selection.
 *
 *   node-added        reconcile the parent's store (and the stores of
 *                     the added subtree, which may have changed while it
 *                     was detached)
 *   node-removed      reconcile the store that showed the node
 *   node-changed /    items-changed (pos, 1, 1) on the store showing the
 *   notify            node, so consumers re-read it
 *   editor "changed"  when the process-wide node tree serial moved (a
 *                     reparent, reorder or prefab sync, which the level
 *                     does not signal): reconcile every store
 *   level-replaced    drop everything, new root store
 *
 * While a filter is set, a "shown" set (matches and their ancestors) is
 * recomputed on every structural change or rename and every store is
 * reconciled against it.
 */

#include "config.h"

#include <string.h>

#include "lrg-editor-node-model.h"
#include "lrg-node-private.h"

typedef struct
{
	LrgNode    *node;       /* strong */
	GListStore *children;   /* nullable: created on first get_children() */
	gulong      notify_id;
	LrgNode    *shown_in;   /* parent whose store shows the node (borrowed), or NULL */
	gboolean    in_root;    /* shown in the root store (shown_in is the level root) */
} Entry;

struct _LrgEditorNodeModel
{
	GObject      parent_instance;

	LrgEditor   *editor;       /* weak */
	LrgLevel    *level;        /* strong, nullable */
	GListStore  *root_store;   /* children of the level root */
	GHashTable  *entries;      /* LrgNode* -> Entry* */

	gchar       *filter;       /* as set, NULL when none */
	gchar       *needle;       /* casefolded filter */
	GHashTable  *shown;        /* LrgNode* set: matches and ancestors (filtering only) */
	GHashTable  *matches;      /* LrgNode* set: matches (filtering only) */
	guint        n_nodes;      /* nodes in the level (no filter) */

	guint        serial;       /* lrg_node_get_tree_serial() at the last sync */
	gboolean     reconciling;

	gulong       added_id;
	gulong       removed_id;
	gulong       changed_id;
	gulong       editor_changed_id;
	gulong       replaced_id;
};

G_DEFINE_FINAL_TYPE (LrgEditorNodeModel, lrg_editor_node_model, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_EDITOR,
	PROP_LEVEL,
	PROP_ROOT_MODEL,
	PROP_FILTER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void reconcile_store (LrgEditorNodeModel *self, LrgNode *parent, GListStore *store);
static void refilter (LrgEditorNodeModel *self);

/* ==========================================================================
 * Entries
 * ========================================================================== */

static void
entry_free (gpointer data)
{
	Entry *e = data;

	g_clear_signal_handler (&e->notify_id, e->node);
	g_clear_object (&e->children);
	g_clear_object (&e->node);
	g_free (e);
}

/* The level root, or NULL without a level. */
static LrgNode *
level_root (LrgEditorNodeModel *self)
{
	return self->level != NULL ? lrg_level_get_root (self->level) : NULL;
}

/* The store showing the children of @parent (the root store for the
 * level root), or NULL when nobody asked for it yet. */
static GListStore *
store_of (LrgEditorNodeModel *self,
          LrgNode            *parent)
{
	Entry *e;

	if (parent == NULL || parent == level_root (self))
		return self->root_store;
	e = g_hash_table_lookup (self->entries, parent);
	return e != NULL ? e->children : NULL;
}

/* Emits items-changed (pos, 1, 1) for @node in the store showing it. */
static void
touch_node (LrgEditorNodeModel *self,
            LrgNode            *node)
{
	GListModel *store;
	guint       pos;

	store = lrg_editor_node_model_get_position (self, node, &pos);
	if (store != NULL)
		g_list_model_items_changed (store, pos, 1, 1);
}

static void
on_node_notify (GObject    *object,
                GParamSpec *pspec,
                gpointer    user_data)
{
	static const gchar * const shown_props[] = {
		"name", "guid", "visible", "locked", "is-2d", "visual", "prefab-link", NULL
	};
	LrgEditorNodeModel *self = LRG_EDITOR_NODE_MODEL (user_data);
	const gchar        *name = g_param_spec_get_name (pspec);

	/* Only what a row shows: transforms change every frame of a drag */
	if (!g_strv_contains (shown_props, name))
		return;

	/* A rename can change which nodes match the filter */
	if (self->needle != NULL && (g_strcmp0 (name, "name") == 0 || g_strcmp0 (name, "guid") == 0))
	{
		refilter (self);
		lrg_editor_node_model_sync (self);
	}
	touch_node (self, LRG_NODE (object));
}

/* The Entry of @node, created (with its notify handler) on demand. */
static Entry *
entry_get (LrgEditorNodeModel *self,
           LrgNode            *node)
{
	Entry *e = g_hash_table_lookup (self->entries, node);

	if (e == NULL)
	{
		e = g_new0 (Entry, 1);
		e->node = g_object_ref (node);
		e->notify_id = g_signal_connect (node, "notify", G_CALLBACK (on_node_notify), self);
		g_hash_table_insert (self->entries, node, e);
	}
	return e;
}

/* ==========================================================================
 * Filter
 * ========================================================================== */

/* Does @node itself match the casefolded needle? */
static gboolean
node_matches (LrgEditorNodeModel *self,
              LrgNode            *node)
{
	const gchar      *name = lrg_node_get_name (node);
	const gchar      *guid = lrg_node_get_guid (node);
	g_autofree gchar *folded = (name != NULL) ? g_utf8_casefold (name, -1) : NULL;
	g_autofree gchar *gfold = (guid != NULL) ? g_utf8_casefold (guid, -1) : NULL;

	return (folded != NULL && strstr (folded, self->needle) != NULL) ||
	       (gfold != NULL && strstr (gfold, self->needle) != NULL);
}

/* Marks @node shown (and returns TRUE) if it or a descendant matches. */
static gboolean
mark_shown (LrgEditorNodeModel *self,
            LrgNode            *node)
{
	GPtrArray *children = lrg_node_get_children (node);
	gboolean   any = FALSE;
	guint      i;

	for (i = 0; i < children->len; i++)
		if (mark_shown (self, g_ptr_array_index (children, i)))
			any = TRUE;

	if (node_matches (self, node))
	{
		g_hash_table_add (self->matches, node);
		any = TRUE;
	}
	if (any)
		g_hash_table_add (self->shown, node);
	return any;
}

/* Recomputes the shown / match sets for the current filter. */
static void
refilter (LrgEditorNodeModel *self)
{
	LrgNode   *root = level_root (self);
	GPtrArray *top;
	guint      i;

	g_hash_table_remove_all (self->shown);
	g_hash_table_remove_all (self->matches);
	self->n_nodes = (self->level != NULL) ? lrg_level_count_nodes (self->level) : 0;
	if (self->needle == NULL || root == NULL)
		return;

	/* The level root itself never matches: it is not a row */
	top = lrg_node_get_children (root);
	for (i = 0; i < top->len; i++)
		mark_shown (self, g_ptr_array_index (top, i));
}

/* ==========================================================================
 * Reconciling
 * ========================================================================== */

/* The children of @parent that the filter lets through, in order. */
static GPtrArray *
wanted_children (LrgEditorNodeModel *self,
                 LrgNode            *parent)
{
	GPtrArray *children;
	GPtrArray *want = g_ptr_array_new ();
	guint      i;

	if (parent == NULL)
		return want;
	children = lrg_node_get_children (parent);
	for (i = 0; i < children->len; i++)
	{
		LrgNode *c = g_ptr_array_index (children, i);

		if (self->needle == NULL || g_hash_table_contains (self->shown, c))
			g_ptr_array_add (want, c);
	}
	return want;
}

/*
 * reconcile_store:
 *
 * Makes @store hold exactly the wanted children of @parent with one
 * g_list_store_splice() over the differing middle part (common prefix
 * and suffix are kept, so an insertion, a removal or a move within the
 * store is a minimal change). Updates which parent shows each node.
 */
static void
reconcile_store (LrgEditorNodeModel *self,
                 LrgNode            *parent,
                 GListStore         *store)
{
	g_autoptr(GPtrArray) want = wanted_children (self, parent);
	guint                n = g_list_model_get_n_items (G_LIST_MODEL (store));
	gboolean             is_root = (store == self->root_store);
	guint                pre = 0;
	guint                suf = 0;
	guint                removed;
	guint                added;
	guint                i;

	/* Common prefix and suffix (items are compared by identity) */
	while (pre < n && pre < want->len)
	{
		g_autoptr(GObject) item = g_list_model_get_item (G_LIST_MODEL (store), pre);

		if (item != g_ptr_array_index (want, pre))
			break;
		pre++;
	}
	while (suf < n - pre && suf < want->len - pre)
	{
		g_autoptr(GObject) item = g_list_model_get_item (G_LIST_MODEL (store), n - 1 - suf);

		if (item != g_ptr_array_index (want, want->len - 1 - suf))
			break;
		suf++;
	}
	removed = n - pre - suf;
	added = want->len - pre - suf;

	/* Nodes leaving the store no longer show here (unless they stay,
	 * moved within it: the loop over the wanted ones re-marks them) */
	for (i = pre; i < pre + removed; i++)
	{
		g_autoptr(GObject) item = g_list_model_get_item (G_LIST_MODEL (store), i);
		Entry             *e = g_hash_table_lookup (self->entries, item);

		if (e != NULL && e->shown_in == parent && e->in_root == is_root)
		{
			e->shown_in = NULL;
			e->in_root = FALSE;
		}
	}
	for (i = 0; i < want->len; i++)
	{
		Entry *e = entry_get (self, g_ptr_array_index (want, i));

		e->shown_in = parent;
		e->in_root = is_root;
	}

	if (removed > 0 || added > 0)
		g_list_store_splice (store, pre, removed, want->pdata + pre, added);
}

/* Reconciles the stores of @node and of every descendant that has one. */
static void
reconcile_subtree (LrgEditorNodeModel *self,
                   LrgNode            *node)
{
	GPtrArray  *children = lrg_node_get_children (node);
	GListStore *store = store_of (self, node);
	guint       i;

	if (store != NULL)
		reconcile_store (self, node, store);
	for (i = 0; i < children->len; i++)
		reconcile_subtree (self, g_ptr_array_index (children, i));
}

void
lrg_editor_node_model_sync (LrgEditorNodeModel *self)
{
	GHashTableIter iter;
	gpointer       value;

	g_return_if_fail (LRG_IS_EDITOR_NODE_MODEL (self));

	if (self->reconciling)
		return;
	self->reconciling = TRUE;
	self->serial = lrg_node_get_tree_serial ();

	if (self->needle != NULL)
		refilter (self);
	else if (self->level != NULL)
		self->n_nodes = lrg_level_count_nodes (self->level);

	if (level_root (self) != NULL)
		reconcile_store (self, level_root (self), self->root_store);

	/* Every other store, attached or not (a detached subtree keeps its
	 * stores current for when an undo puts it back). Iterate a snapshot:
	 * reconciling creates entries. */
	{
		g_autoptr(GPtrArray) stores = g_ptr_array_new ();
		guint                i;

		g_hash_table_iter_init (&iter, self->entries);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			if (((Entry *) value)->children != NULL)
				g_ptr_array_add (stores, value);
		for (i = 0; i < stores->len; i++)
		{
			Entry *e = g_ptr_array_index (stores, i);

			reconcile_store (self, e->node, e->children);
		}
	}
	self->reconciling = FALSE;
}

/* ==========================================================================
 * Level and editor signals
 * ========================================================================== */

static void
on_node_added (LrgLevel *level,
               LrgNode  *node,
               gpointer  user_data)
{
	LrgEditorNodeModel *self = LRG_EDITOR_NODE_MODEL (user_data);
	LrgNode            *parent = lrg_node_get_parent (node);
	GListStore         *store;

	if (self->reconciling)
		return;
	if (self->needle != NULL)
	{
		lrg_editor_node_model_sync (self);
		return;
	}
	self->n_nodes = lrg_level_count_nodes (level);
	store = store_of (self, parent);
	if (store != NULL)
		reconcile_store (self, parent, store);
	reconcile_subtree (self, node);
}

static void
on_node_removed (LrgLevel *level,
                 LrgNode  *node,
                 gpointer  user_data)
{
	LrgEditorNodeModel *self = LRG_EDITOR_NODE_MODEL (user_data);
	Entry              *e = g_hash_table_lookup (self->entries, node);
	LrgNode            *was_in;
	GListStore         *store;

	if (self->reconciling)
		return;
	if (self->needle != NULL)
	{
		lrg_editor_node_model_sync (self);
		return;
	}
	self->n_nodes = lrg_level_count_nodes (level);
	if (e == NULL || e->shown_in == NULL)
		return;
	was_in = e->shown_in;
	store = e->in_root ? self->root_store : store_of (self, was_in);
	if (store != NULL)
		reconcile_store (self, e->in_root ? level_root (self) : was_in, store);
}

static void
on_node_changed (LrgLevel *level,
                 LrgNode  *node,
                 gpointer  user_data)
{
	LrgEditorNodeModel *self = LRG_EDITOR_NODE_MODEL (user_data);

	if (self->needle != NULL)
	{
		refilter (self);
		lrg_editor_node_model_sync (self);
	}
	touch_node (self, node);
}

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	LrgEditorNodeModel *self = LRG_EDITOR_NODE_MODEL (user_data);

	if (self->serial != lrg_node_get_tree_serial ())
		lrg_editor_node_model_sync (self);
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	lrg_editor_node_model_set_level (LRG_EDITOR_NODE_MODEL (user_data), level);
}

static void
detach_level (LrgEditorNodeModel *self)
{
	if (self->level != NULL)
	{
		g_clear_signal_handler (&self->added_id, self->level);
		g_clear_signal_handler (&self->removed_id, self->level);
		g_clear_signal_handler (&self->changed_id, self->level);
		g_clear_object (&self->level);
	}
	if (self->entries != NULL)
		g_hash_table_remove_all (self->entries);
	if (self->shown != NULL)
		g_hash_table_remove_all (self->shown);
	if (self->matches != NULL)
		g_hash_table_remove_all (self->matches);
}

void
lrg_editor_node_model_set_level (LrgEditorNodeModel *self,
                                 LrgLevel           *level)
{
	g_return_if_fail (LRG_IS_EDITOR_NODE_MODEL (self));
	g_return_if_fail (level == NULL || LRG_IS_LEVEL (level));

	detach_level (self);

	/* A new root store: views reset their caches with the new model */
	g_clear_object (&self->root_store);
	self->root_store = g_list_store_new (LRG_TYPE_NODE);

	if (level != NULL)
	{
		self->level = g_object_ref (level);
		self->added_id = g_signal_connect (level, "node-added", G_CALLBACK (on_node_added), self);
		self->removed_id = g_signal_connect (level, "node-removed", G_CALLBACK (on_node_removed), self);
		self->changed_id = g_signal_connect (level, "node-changed", G_CALLBACK (on_node_changed), self);
	}
	lrg_editor_node_model_sync (self);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LEVEL]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROOT_MODEL]);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_node_model_dispose (GObject *object)
{
	LrgEditorNodeModel *self = LRG_EDITOR_NODE_MODEL (object);

	if (self->editor != NULL)
	{
		g_clear_signal_handler (&self->editor_changed_id, self->editor);
		g_clear_signal_handler (&self->replaced_id, self->editor);
		g_clear_weak_pointer (&self->editor);
	}
	detach_level (self);
	g_clear_object (&self->root_store);

	G_OBJECT_CLASS (lrg_editor_node_model_parent_class)->dispose (object);
}

static void
lrg_editor_node_model_finalize (GObject *object)
{
	LrgEditorNodeModel *self = LRG_EDITOR_NODE_MODEL (object);

	g_clear_pointer (&self->entries, g_hash_table_unref);
	g_clear_pointer (&self->shown, g_hash_table_unref);
	g_clear_pointer (&self->matches, g_hash_table_unref);
	g_free (self->filter);
	g_free (self->needle);

	G_OBJECT_CLASS (lrg_editor_node_model_parent_class)->finalize (object);
}

static void
lrg_editor_node_model_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgEditorNodeModel *self = LRG_EDITOR_NODE_MODEL (object);

	switch (prop_id)
	{
	case PROP_EDITOR:
		g_value_set_object (value, self->editor);
		break;
	case PROP_LEVEL:
		g_value_set_object (value, self->level);
		break;
	case PROP_ROOT_MODEL:
		g_value_set_object (value, self->root_store);
		break;
	case PROP_FILTER:
		g_value_set_string (value, self->filter);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_node_model_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgEditorNodeModel *self = LRG_EDITOR_NODE_MODEL (object);

	switch (prop_id)
	{
	case PROP_LEVEL:
		lrg_editor_node_model_set_level (self, g_value_get_object (value));
		break;
	case PROP_FILTER:
		lrg_editor_node_model_set_filter (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_node_model_class_init (LrgEditorNodeModelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_editor_node_model_dispose;
	object_class->finalize = lrg_editor_node_model_finalize;
	object_class->get_property = lrg_editor_node_model_get_property;
	object_class->set_property = lrg_editor_node_model_set_property;

	/**
	 * LrgEditorNodeModel:editor:
	 *
	 * The editor the model follows (level replacement and structural
	 * edits), or %NULL for a model made over a level alone.
	 */
	properties[PROP_EDITOR] =
		g_param_spec_object ("editor", "Editor", "The editor the model follows",
		                     LRG_TYPE_EDITOR,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorNodeModel:level:
	 *
	 * The level presented. Setting it drops every store and makes a new
	 * #LrgEditorNodeModel:root-model.
	 */
	properties[PROP_LEVEL] =
		g_param_spec_object ("level", "Level", "The level presented",
		                     LRG_TYPE_LEVEL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditorNodeModel:root-model:
	 *
	 * The top-level store (children of the level root). A new object is
	 * made when the level changes; bound views follow it.
	 */
	properties[PROP_ROOT_MODEL] =
		g_param_spec_object ("root-model", "Root model", "The top-level store",
		                     G_TYPE_LIST_MODEL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorNodeModel:filter:
	 *
	 * Case-insensitive substring of node names or GUIDs; only matches and
	 * their ancestors are shown. %NULL (or "") shows everything.
	 */
	properties[PROP_FILTER] =
		g_param_spec_string ("filter", "Filter", "Name or GUID substring to show",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_editor_node_model_init (LrgEditorNodeModel *self)
{
	self->entries = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, entry_free);
	self->shown = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->matches = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->root_store = g_list_store_new (LRG_TYPE_NODE);
}

LrgEditorNodeModel *
lrg_editor_node_model_new (LrgEditor *editor)
{
	LrgEditorNodeModel *self;

	g_return_val_if_fail (editor == NULL || LRG_IS_EDITOR (editor), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_NODE_MODEL, NULL);
	if (editor != NULL)
	{
		g_set_weak_pointer (&self->editor, editor);
		self->editor_changed_id = g_signal_connect (editor, "changed", G_CALLBACK (on_editor_changed), self);
		self->replaced_id = g_signal_connect (editor, "level-replaced", G_CALLBACK (on_level_replaced), self);
		lrg_editor_node_model_set_level (self, lrg_editor_get_level (editor));
	}
	return self;
}

LrgEditorNodeModel *
lrg_editor_node_model_new_for_level (LrgLevel *level)
{
	LrgEditorNodeModel *self;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_NODE_MODEL, NULL);
	lrg_editor_node_model_set_level (self, level);
	return self;
}

LrgEditor *
lrg_editor_node_model_get_editor (LrgEditorNodeModel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), NULL);
	return self->editor;
}

LrgLevel *
lrg_editor_node_model_get_level (LrgEditorNodeModel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), NULL);
	return self->level;
}

GListModel *
lrg_editor_node_model_get_root_model (LrgEditorNodeModel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), NULL);
	return G_LIST_MODEL (self->root_store);
}

GListModel *
lrg_editor_node_model_get_children (LrgEditorNodeModel *self,
                                    LrgNode            *node)
{
	Entry *e;

	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), NULL);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), NULL);

	if (node == NULL || node == level_root (self))
		return G_LIST_MODEL (self->root_store);

	e = entry_get (self, node);
	if (e->children == NULL)
	{
		e->children = g_list_store_new (LRG_TYPE_NODE);
		reconcile_store (self, node, e->children);
	}
	return G_LIST_MODEL (e->children);
}

LrgNode *
lrg_editor_node_model_get_parent (LrgEditorNodeModel *self,
                                  LrgNode            *node)
{
	LrgNode *parent;

	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	parent = lrg_node_get_parent (node);
	return (parent == level_root (self)) ? NULL : parent;
}

GListModel *
lrg_editor_node_model_get_position (LrgEditorNodeModel *self,
                                    LrgNode            *node,
                                    guint              *position)
{
	Entry      *e;
	GListStore *store;
	guint       pos = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	e = g_hash_table_lookup (self->entries, node);
	if (e == NULL || (e->shown_in == NULL && !e->in_root))
		return NULL;
	store = e->in_root ? self->root_store : store_of (self, e->shown_in);
	if (store == NULL || !g_list_store_find (store, node, &pos))
		return NULL;
	if (position != NULL)
		*position = pos;
	return G_LIST_MODEL (store);
}

void
lrg_editor_node_model_set_filter (LrgEditorNodeModel *self,
                                  const gchar        *filter)
{
	g_return_if_fail (LRG_IS_EDITOR_NODE_MODEL (self));

	if (filter != NULL && *filter == '\0')
		filter = NULL;
	if (g_strcmp0 (filter, self->filter) == 0)
		return;

	g_free (self->filter);
	g_free (self->needle);
	self->filter = g_strdup (filter);
	self->needle = (filter != NULL) ? g_utf8_casefold (filter, -1) : NULL;
	lrg_editor_node_model_sync (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTER]);
}

const gchar *
lrg_editor_node_model_get_filter (LrgEditorNodeModel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), NULL);
	return self->filter;
}

gboolean
lrg_editor_node_model_is_match (LrgEditorNodeModel *self,
                                LrgNode            *node)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), FALSE);
	return self->needle == NULL || g_hash_table_contains (self->matches, node);
}

gboolean
lrg_editor_node_model_is_shown (LrgEditorNodeModel *self,
                                LrgNode            *node)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), FALSE);
	return self->needle == NULL || g_hash_table_contains (self->shown, node);
}

guint
lrg_editor_node_model_get_n_matches (LrgEditorNodeModel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_MODEL (self), 0);
	return self->needle != NULL ? g_hash_table_size (self->matches) : self->n_nodes;
}

/* ==========================================================================
 * Tree view binding
 * ========================================================================== */

static GListModel *
tree_children (LrgTreeView *view,
               GObject     *item,
               gpointer     user_data)
{
	return g_object_ref (lrg_editor_node_model_get_children (LRG_EDITOR_NODE_MODEL (user_data), LRG_NODE (item)));
}

static GObject *
tree_parent (LrgTreeView *view,
             GObject     *item,
             gpointer     user_data)
{
	return (GObject *) lrg_editor_node_model_get_parent (LRG_EDITOR_NODE_MODEL (user_data), LRG_NODE (item));
}

static void
on_root_model (GObject    *object,
               GParamSpec *pspec,
               gpointer    user_data)
{
	lrg_tree_view_set_model (LRG_TREE_VIEW (user_data),
	                         lrg_editor_node_model_get_root_model (LRG_EDITOR_NODE_MODEL (object)));
}

void
lrg_editor_node_model_bind_tree_view (LrgEditorNodeModel *self,
                                      LrgTreeView        *view)
{
	g_return_if_fail (LRG_IS_EDITOR_NODE_MODEL (self));
	g_return_if_fail (LRG_IS_TREE_VIEW (view));

	lrg_tree_view_set_children_func (view, tree_children, g_object_ref (self), g_object_unref);
	lrg_tree_view_set_parent_func (view, tree_parent, g_object_ref (self), g_object_unref);
	lrg_tree_view_set_model (view, G_LIST_MODEL (self->root_store));
	g_signal_connect_object (self, "notify::root-model", G_CALLBACK (on_root_model), view, 0);
}
