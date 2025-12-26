/* lrg-dialog-tree.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "dialog/lrg-dialog-tree.h"
#include "lrg-enums.h"

/**
 * LrgDialogTree:
 *
 * Container for interconnected dialog nodes.
 */
struct _LrgDialogTree
{
    GObject     parent_instance;

    gchar      *id;
    gchar      *title;
    gchar      *description;
    gchar      *start_node_id;
    GHashTable *nodes;    /* string -> LrgDialogNode */
};

G_DEFINE_TYPE (LrgDialogTree, lrg_dialog_tree, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_TITLE,
    PROP_DESCRIPTION,
    PROP_START_NODE_ID,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_dialog_tree_finalize (GObject *object)
{
    LrgDialogTree *self = LRG_DIALOG_TREE (object);

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->title, g_free);
    g_clear_pointer (&self->description, g_free);
    g_clear_pointer (&self->start_node_id, g_free);
    g_clear_pointer (&self->nodes, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_dialog_tree_parent_class)->finalize (object);
}

static void
lrg_dialog_tree_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgDialogTree *self = LRG_DIALOG_TREE (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, self->id);
        break;
    case PROP_TITLE:
        g_value_set_string (value, self->title);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, self->description);
        break;
    case PROP_START_NODE_ID:
        g_value_set_string (value, self->start_node_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_dialog_tree_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgDialogTree *self = LRG_DIALOG_TREE (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (self->id);
        self->id = g_value_dup_string (value);
        break;
    case PROP_TITLE:
        g_free (self->title);
        self->title = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        g_free (self->description);
        self->description = g_value_dup_string (value);
        break;
    case PROP_START_NODE_ID:
        g_free (self->start_node_id);
        self->start_node_id = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_dialog_tree_class_init (LrgDialogTreeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_dialog_tree_finalize;
    object_class->get_property = lrg_dialog_tree_get_property;
    object_class->set_property = lrg_dialog_tree_set_property;

    /**
     * LrgDialogTree:id:
     *
     * Unique identifier for this dialog tree.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Tree identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDialogTree:title:
     *
     * Human-readable title for this dialog tree.
     */
    properties[PROP_TITLE] =
        g_param_spec_string ("title",
                             "Title",
                             "Tree title",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDialogTree:description:
     *
     * Description of this dialog tree.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Tree description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDialogTree:start-node-id:
     *
     * ID of the starting node for this dialog.
     */
    properties[PROP_START_NODE_ID] =
        g_param_spec_string ("start-node-id",
                             "Start Node ID",
                             "Starting node identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_dialog_tree_init (LrgDialogTree *self)
{
    self->nodes = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, g_object_unref);
}

/**
 * lrg_dialog_tree_new:
 * @id: unique identifier for the tree
 *
 * Creates a new dialog tree.
 *
 * Returns: (transfer full): A new #LrgDialogTree
 */
LrgDialogTree *
lrg_dialog_tree_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_DIALOG_TREE,
                         "id", id,
                         NULL);
}

/**
 * lrg_dialog_tree_get_id:
 * @self: an #LrgDialogTree
 *
 * Gets the tree identifier.
 *
 * Returns: (transfer none): The tree ID
 */
const gchar *
lrg_dialog_tree_get_id (LrgDialogTree *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), NULL);
    return self->id;
}

/**
 * lrg_dialog_tree_get_start_node_id:
 * @self: an #LrgDialogTree
 *
 * Gets the starting node ID.
 *
 * Returns: (transfer none) (nullable): The start node ID
 */
const gchar *
lrg_dialog_tree_get_start_node_id (LrgDialogTree *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), NULL);
    return self->start_node_id;
}

/**
 * lrg_dialog_tree_set_start_node_id:
 * @self: an #LrgDialogTree
 * @start_node_id: (nullable): start node ID
 *
 * Sets the starting node ID.
 */
void
lrg_dialog_tree_set_start_node_id (LrgDialogTree *self,
                                   const gchar   *start_node_id)
{
    g_return_if_fail (LRG_IS_DIALOG_TREE (self));
    g_object_set (self, "start-node-id", start_node_id, NULL);
}

/**
 * lrg_dialog_tree_add_node:
 * @self: an #LrgDialogTree
 * @node: (transfer full): node to add
 *
 * Adds a node to the tree.
 */
void
lrg_dialog_tree_add_node (LrgDialogTree *self,
                          LrgDialogNode *node)
{
    const gchar *node_id;

    g_return_if_fail (LRG_IS_DIALOG_TREE (self));
    g_return_if_fail (LRG_IS_DIALOG_NODE (node));

    node_id = lrg_dialog_node_get_id (node);
    g_return_if_fail (node_id != NULL);

    /* Takes ownership of node */
    g_hash_table_replace (self->nodes, g_strdup (node_id), node);
}

/**
 * lrg_dialog_tree_get_node:
 * @self: an #LrgDialogTree
 * @node_id: node identifier
 *
 * Gets a node by ID.
 *
 * Returns: (transfer none) (nullable): The node, or %NULL if not found
 */
LrgDialogNode *
lrg_dialog_tree_get_node (LrgDialogTree *self,
                          const gchar   *node_id)
{
    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), NULL);
    g_return_val_if_fail (node_id != NULL, NULL);

    return g_hash_table_lookup (self->nodes, node_id);
}

/**
 * lrg_dialog_tree_get_start_node:
 * @self: an #LrgDialogTree
 *
 * Gets the starting node.
 *
 * Returns: (transfer none) (nullable): The start node, or %NULL
 */
LrgDialogNode *
lrg_dialog_tree_get_start_node (LrgDialogTree *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), NULL);

    if (self->start_node_id == NULL)
        return NULL;

    return lrg_dialog_tree_get_node (self, self->start_node_id);
}

/**
 * lrg_dialog_tree_remove_node:
 * @self: an #LrgDialogTree
 * @node_id: node identifier
 *
 * Removes a node from the tree.
 *
 * Returns: %TRUE if the node was removed
 */
gboolean
lrg_dialog_tree_remove_node (LrgDialogTree *self,
                             const gchar   *node_id)
{
    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), FALSE);
    g_return_val_if_fail (node_id != NULL, FALSE);

    return g_hash_table_remove (self->nodes, node_id);
}

/**
 * lrg_dialog_tree_get_node_count:
 * @self: an #LrgDialogTree
 *
 * Gets the number of nodes in the tree.
 *
 * Returns: Node count
 */
guint
lrg_dialog_tree_get_node_count (LrgDialogTree *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), 0);
    return g_hash_table_size (self->nodes);
}

/**
 * lrg_dialog_tree_get_node_ids:
 * @self: an #LrgDialogTree
 *
 * Gets all node IDs in the tree.
 *
 * Returns: (transfer container) (element-type utf8): List of node IDs
 */
GList *
lrg_dialog_tree_get_node_ids (LrgDialogTree *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), NULL);
    return g_hash_table_get_keys (self->nodes);
}

/**
 * lrg_dialog_tree_get_title:
 * @self: an #LrgDialogTree
 *
 * Gets the tree title.
 *
 * Returns: (transfer none) (nullable): The title
 */
const gchar *
lrg_dialog_tree_get_title (LrgDialogTree *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), NULL);
    return self->title;
}

/**
 * lrg_dialog_tree_set_title:
 * @self: an #LrgDialogTree
 * @title: (nullable): tree title
 *
 * Sets the tree title.
 */
void
lrg_dialog_tree_set_title (LrgDialogTree *self,
                           const gchar   *title)
{
    g_return_if_fail (LRG_IS_DIALOG_TREE (self));
    g_object_set (self, "title", title, NULL);
}

/**
 * lrg_dialog_tree_get_description:
 * @self: an #LrgDialogTree
 *
 * Gets the tree description.
 *
 * Returns: (transfer none) (nullable): The description
 */
const gchar *
lrg_dialog_tree_get_description (LrgDialogTree *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), NULL);
    return self->description;
}

/**
 * lrg_dialog_tree_set_description:
 * @self: an #LrgDialogTree
 * @description: (nullable): tree description
 *
 * Sets the tree description.
 */
void
lrg_dialog_tree_set_description (LrgDialogTree *self,
                                 const gchar   *description)
{
    g_return_if_fail (LRG_IS_DIALOG_TREE (self));
    g_object_set (self, "description", description, NULL);
}

/*
 * Helper to check if a node ID exists in the tree.
 */
static gboolean
node_exists (LrgDialogTree *self,
             const gchar   *node_id)
{
    if (node_id == NULL)
        return TRUE;  /* NULL is valid (terminal) */

    return g_hash_table_contains (self->nodes, node_id);
}

/**
 * lrg_dialog_tree_validate:
 * @self: an #LrgDialogTree
 * @error: (nullable): return location for error
 *
 * Validates the dialog tree structure.
 *
 * Returns: %TRUE if the tree is valid
 */
gboolean
lrg_dialog_tree_validate (LrgDialogTree  *self,
                          GError        **error)
{
    GHashTableIter iter;
    gpointer       key, value;

    g_return_val_if_fail (LRG_IS_DIALOG_TREE (self), FALSE);

    /* Check start node exists */
    if (self->start_node_id != NULL && !node_exists (self, self->start_node_id))
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_INVALID_NODE,
                     "Start node '%s' not found in tree '%s'",
                     self->start_node_id, self->id);
        return FALSE;
    }

    /* Check all node references */
    g_hash_table_iter_init (&iter, self->nodes);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        LrgDialogNode *node = LRG_DIALOG_NODE (value);
        const gchar   *next_id;
        GPtrArray     *responses;
        guint          i;

        /* Check next_node_id */
        next_id = lrg_dialog_node_get_next_node_id (node);
        if (next_id != NULL && !node_exists (self, next_id))
        {
            g_set_error (error,
                         LRG_DIALOG_ERROR,
                         LRG_DIALOG_ERROR_INVALID_NODE,
                         "Node '%s' references non-existent next node '%s'",
                         (const gchar *)key, next_id);
            return FALSE;
        }

        /* Check response references */
        responses = lrg_dialog_node_get_responses (node);
        for (i = 0; i < responses->len; i++)
        {
            LrgDialogResponse *resp = g_ptr_array_index (responses, i);
            const gchar       *resp_next;

            resp_next = lrg_dialog_response_get_next_node_id (resp);
            if (resp_next != NULL && !node_exists (self, resp_next))
            {
                g_set_error (error,
                             LRG_DIALOG_ERROR,
                             LRG_DIALOG_ERROR_INVALID_NODE,
                             "Response '%s' in node '%s' references non-existent node '%s'",
                             lrg_dialog_response_get_id (resp),
                             (const gchar *)key, resp_next);
                return FALSE;
            }
        }
    }

    return TRUE;
}
