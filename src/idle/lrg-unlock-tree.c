/* lrg-unlock-tree.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-unlock-tree.h"

/* ========================================================================= */
/* LrgUnlockNode - Boxed type for unlock tree nodes                         */
/* ========================================================================= */

/* Forward declarations for G_DEFINE_BOXED_TYPE */
LrgUnlockNode *lrg_unlock_node_copy (const LrgUnlockNode *self);
void lrg_unlock_node_free (LrgUnlockNode *self);

G_DEFINE_BOXED_TYPE (LrgUnlockNode, lrg_unlock_node,
                     lrg_unlock_node_copy,
                     lrg_unlock_node_free)

LrgUnlockNode *
lrg_unlock_node_new (const gchar *id,
                     const gchar *name)
{
    LrgUnlockNode *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    self = g_slice_new0 (LrgUnlockNode);
    self->id = g_strdup (id);
    self->name = g_strdup (name);
    self->description = NULL;
    self->icon = NULL;
    self->cost = lrg_big_number_new_zero ();
    self->unlocked = FALSE;
    self->unlock_time = 0;
    self->tier = 0;

    return self;
}

LrgUnlockNode *
lrg_unlock_node_copy (const LrgUnlockNode *self)
{
    LrgUnlockNode *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new0 (LrgUnlockNode);
    copy->id = g_strdup (self->id);
    copy->name = g_strdup (self->name);
    copy->description = g_strdup (self->description);
    copy->icon = g_strdup (self->icon);
    copy->cost = lrg_big_number_copy (self->cost);
    copy->unlocked = self->unlocked;
    copy->unlock_time = self->unlock_time;
    copy->tier = self->tier;

    return copy;
}

void
lrg_unlock_node_free (LrgUnlockNode *self)
{
    if (self == NULL)
        return;

    g_free (self->id);
    g_free (self->name);
    g_free (self->description);
    g_free (self->icon);
    lrg_big_number_free (self->cost);
    g_slice_free (LrgUnlockNode, self);
}

const gchar *
lrg_unlock_node_get_id (const LrgUnlockNode *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

const gchar *
lrg_unlock_node_get_name (const LrgUnlockNode *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->name;
}

const gchar *
lrg_unlock_node_get_description (const LrgUnlockNode *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->description;
}

void
lrg_unlock_node_set_description (LrgUnlockNode *self,
                                 const gchar   *description)
{
    g_return_if_fail (self != NULL);

    g_free (self->description);
    self->description = g_strdup (description);
}

const gchar *
lrg_unlock_node_get_icon (const LrgUnlockNode *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->icon;
}

void
lrg_unlock_node_set_icon (LrgUnlockNode *self,
                          const gchar   *icon)
{
    g_return_if_fail (self != NULL);

    g_free (self->icon);
    self->icon = g_strdup (icon);
}

const LrgBigNumber *
lrg_unlock_node_get_cost (const LrgUnlockNode *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->cost;
}

void
lrg_unlock_node_set_cost (LrgUnlockNode      *self,
                          const LrgBigNumber *cost)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (cost != NULL);

    lrg_big_number_free (self->cost);
    self->cost = lrg_big_number_copy (cost);
}

void
lrg_unlock_node_set_cost_simple (LrgUnlockNode *self,
                                 gdouble        cost)
{
    g_autoptr(LrgBigNumber) bn = NULL;

    bn = lrg_big_number_new (cost);
    lrg_unlock_node_set_cost (self, bn);
}

gboolean
lrg_unlock_node_is_unlocked (const LrgUnlockNode *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->unlocked;
}

gint64
lrg_unlock_node_get_unlock_time (const LrgUnlockNode *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->unlock_time;
}

gint
lrg_unlock_node_get_tier (const LrgUnlockNode *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->tier;
}

void
lrg_unlock_node_set_tier (LrgUnlockNode *self,
                          gint           tier)
{
    g_return_if_fail (self != NULL);
    self->tier = tier;
}

/* ========================================================================= */
/* LrgUnlockTree - GObject managing the unlock graph                        */
/* ========================================================================= */

struct _LrgUnlockTree
{
    GObject     parent_instance;

    GHashTable *nodes;        /* id -> LrgUnlockNode* */
    GHashTable *requirements; /* id -> GPtrArray of required IDs */
};

enum
{
    SIGNAL_NODE_UNLOCKED,
    SIGNAL_NODE_LOCKED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (LrgUnlockTree, lrg_unlock_tree, G_TYPE_OBJECT)

static void
lrg_unlock_tree_finalize (GObject *object)
{
    LrgUnlockTree *self = LRG_UNLOCK_TREE (object);

    g_hash_table_unref (self->nodes);
    g_hash_table_unref (self->requirements);

    G_OBJECT_CLASS (lrg_unlock_tree_parent_class)->finalize (object);
}

static void
lrg_unlock_tree_class_init (LrgUnlockTreeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_unlock_tree_finalize;

    /**
     * LrgUnlockTree::node-unlocked:
     * @self: The tree
     * @node_id: ID of unlocked node
     *
     * Emitted when a node is unlocked.
     *
     * Since: 1.0
     */
    signals[SIGNAL_NODE_UNLOCKED] =
        g_signal_new ("node-unlocked",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);

    /**
     * LrgUnlockTree::node-locked:
     * @self: The tree
     * @node_id: ID of locked node
     *
     * Emitted when a node is locked.
     *
     * Since: 1.0
     */
    signals[SIGNAL_NODE_LOCKED] =
        g_signal_new ("node-locked",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

static void
free_requirements_array (gpointer data)
{
    GPtrArray *arr = data;
    g_ptr_array_unref (arr);
}

static void
lrg_unlock_tree_init (LrgUnlockTree *self)
{
    self->nodes = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, (GDestroyNotify)lrg_unlock_node_free);
    self->requirements = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, free_requirements_array);
}

LrgUnlockTree *
lrg_unlock_tree_new (void)
{
    return g_object_new (LRG_TYPE_UNLOCK_TREE, NULL);
}

gboolean
lrg_unlock_tree_add_node (LrgUnlockTree       *self,
                          const LrgUnlockNode *node)
{
    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), FALSE);
    g_return_val_if_fail (node != NULL, FALSE);

    /* Check if already exists */
    if (g_hash_table_contains (self->nodes, node->id))
        return FALSE;

    g_hash_table_insert (self->nodes,
                         g_strdup (node->id),
                         lrg_unlock_node_copy (node));

    /* Initialize empty requirements array */
    g_hash_table_insert (self->requirements,
                         g_strdup (node->id),
                         g_ptr_array_new_with_free_func (g_free));

    return TRUE;
}

LrgUnlockNode *
lrg_unlock_tree_get_node (LrgUnlockTree *self,
                          const gchar   *id)
{
    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->nodes, id);
}

gboolean
lrg_unlock_tree_remove_node (LrgUnlockTree *self,
                             const gchar   *id)
{
    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    g_hash_table_remove (self->requirements, id);
    return g_hash_table_remove (self->nodes, id);
}

GPtrArray *
lrg_unlock_tree_get_all_nodes (LrgUnlockTree *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->nodes);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        g_ptr_array_add (result, value);

    return result;
}

/*
 * Helper to check for cycles using DFS.
 * Returns TRUE if adding node_id -> required_id would create a cycle.
 */
static gboolean
would_create_cycle (LrgUnlockTree *self,
                    const gchar   *node_id,
                    const gchar   *required_id,
                    GHashTable    *visited)
{
    GPtrArray *reqs;
    guint i;

    /* If we've reached the original node, there's a cycle */
    if (g_strcmp0 (required_id, node_id) == 0)
        return TRUE;

    /* Mark as visited */
    if (g_hash_table_contains (visited, required_id))
        return FALSE;
    g_hash_table_add (visited, (gpointer)required_id);

    /* Check all requirements of required_id */
    reqs = g_hash_table_lookup (self->requirements, required_id);
    if (reqs == NULL)
        return FALSE;

    for (i = 0; i < reqs->len; i++)
    {
        const gchar *req_id = g_ptr_array_index (reqs, i);
        if (would_create_cycle (self, node_id, req_id, visited))
            return TRUE;
    }

    return FALSE;
}

gboolean
lrg_unlock_tree_add_requirement (LrgUnlockTree *self,
                                 const gchar   *node_id,
                                 const gchar   *required_id)
{
    GPtrArray *reqs;
    g_autoptr(GHashTable) visited = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), FALSE);
    g_return_val_if_fail (node_id != NULL, FALSE);
    g_return_val_if_fail (required_id != NULL, FALSE);

    /* Both nodes must exist */
    if (!g_hash_table_contains (self->nodes, node_id) ||
        !g_hash_table_contains (self->nodes, required_id))
        return FALSE;

    /* Check for cycles */
    visited = g_hash_table_new (g_str_hash, g_str_equal);
    if (would_create_cycle (self, node_id, required_id, visited))
        return FALSE;

    /* Add requirement */
    reqs = g_hash_table_lookup (self->requirements, node_id);
    if (reqs == NULL)
    {
        reqs = g_ptr_array_new_with_free_func (g_free);
        g_hash_table_insert (self->requirements, g_strdup (node_id), reqs);
    }

    /* Check if already required */
    for (i = 0; i < reqs->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (reqs, i), required_id) == 0)
            return TRUE;  /* Already exists */
    }

    g_ptr_array_add (reqs, g_strdup (required_id));
    return TRUE;
}

void
lrg_unlock_tree_remove_requirement (LrgUnlockTree *self,
                                    const gchar   *node_id,
                                    const gchar   *required_id)
{
    GPtrArray *reqs;
    guint i;

    g_return_if_fail (LRG_IS_UNLOCK_TREE (self));
    g_return_if_fail (node_id != NULL);
    g_return_if_fail (required_id != NULL);

    reqs = g_hash_table_lookup (self->requirements, node_id);
    if (reqs == NULL)
        return;

    for (i = 0; i < reqs->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (reqs, i), required_id) == 0)
        {
            g_ptr_array_remove_index (reqs, i);
            return;
        }
    }
}

GPtrArray *
lrg_unlock_tree_get_requirements (LrgUnlockTree *self,
                                  const gchar   *node_id)
{
    GPtrArray *reqs;
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), NULL);
    g_return_val_if_fail (node_id != NULL, NULL);

    reqs = g_hash_table_lookup (self->requirements, node_id);
    result = g_ptr_array_new_with_free_func (g_free);

    if (reqs != NULL)
    {
        for (i = 0; i < reqs->len; i++)
            g_ptr_array_add (result, g_strdup (g_ptr_array_index (reqs, i)));
    }

    return result;
}

GPtrArray *
lrg_unlock_tree_get_dependents (LrgUnlockTree *self,
                                const gchar   *node_id)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer key, value;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), NULL);
    g_return_val_if_fail (node_id != NULL, NULL);

    result = g_ptr_array_new_with_free_func (g_free);

    g_hash_table_iter_init (&iter, self->requirements);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        GPtrArray *reqs = value;
        guint i;

        for (i = 0; i < reqs->len; i++)
        {
            if (g_strcmp0 (g_ptr_array_index (reqs, i), node_id) == 0)
            {
                g_ptr_array_add (result, g_strdup (key));
                break;
            }
        }
    }

    return result;
}

/*
 * Check if all requirements for a node are unlocked.
 */
static gboolean
requirements_met (LrgUnlockTree *self,
                  const gchar   *node_id)
{
    GPtrArray *reqs;
    guint i;

    reqs = g_hash_table_lookup (self->requirements, node_id);
    if (reqs == NULL)
        return TRUE;

    for (i = 0; i < reqs->len; i++)
    {
        const gchar *req_id = g_ptr_array_index (reqs, i);
        LrgUnlockNode *req_node = g_hash_table_lookup (self->nodes, req_id);

        if (req_node == NULL || !req_node->unlocked)
            return FALSE;
    }

    return TRUE;
}

gboolean
lrg_unlock_tree_can_unlock (LrgUnlockTree      *self,
                            const gchar        *node_id,
                            const LrgBigNumber *available_points)
{
    LrgUnlockNode *node;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), FALSE);
    g_return_val_if_fail (node_id != NULL, FALSE);
    g_return_val_if_fail (available_points != NULL, FALSE);

    node = g_hash_table_lookup (self->nodes, node_id);
    if (node == NULL)
        return FALSE;

    /* Already unlocked */
    if (node->unlocked)
        return FALSE;

    /* Check requirements */
    if (!requirements_met (self, node_id))
        return FALSE;

    /* Check cost */
    if (lrg_big_number_compare (available_points, node->cost) < 0)
        return FALSE;

    return TRUE;
}

gboolean
lrg_unlock_tree_unlock (LrgUnlockTree *self,
                        const gchar   *node_id)
{
    LrgUnlockNode *node;
    g_autoptr(GDateTime) now = NULL;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), FALSE);
    g_return_val_if_fail (node_id != NULL, FALSE);

    node = g_hash_table_lookup (self->nodes, node_id);
    if (node == NULL)
        return FALSE;

    /* Already unlocked */
    if (node->unlocked)
        return FALSE;

    node->unlocked = TRUE;
    now = g_date_time_new_now_utc ();
    node->unlock_time = g_date_time_to_unix (now);

    g_signal_emit (self, signals[SIGNAL_NODE_UNLOCKED], 0, node_id);

    return TRUE;
}

gboolean
lrg_unlock_tree_is_unlocked (LrgUnlockTree *self,
                             const gchar   *node_id)
{
    LrgUnlockNode *node;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), FALSE);
    g_return_val_if_fail (node_id != NULL, FALSE);

    node = g_hash_table_lookup (self->nodes, node_id);
    if (node == NULL)
        return FALSE;

    return node->unlocked;
}

void
lrg_unlock_tree_lock (LrgUnlockTree *self,
                      const gchar   *node_id)
{
    LrgUnlockNode *node;

    g_return_if_fail (LRG_IS_UNLOCK_TREE (self));
    g_return_if_fail (node_id != NULL);

    node = g_hash_table_lookup (self->nodes, node_id);
    if (node == NULL)
        return;

    if (node->unlocked)
    {
        node->unlocked = FALSE;
        node->unlock_time = 0;
        g_signal_emit (self, signals[SIGNAL_NODE_LOCKED], 0, node_id);
    }
}

GPtrArray *
lrg_unlock_tree_get_available (LrgUnlockTree      *self,
                               const LrgBigNumber *available_points)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer key, value;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), NULL);
    g_return_val_if_fail (available_points != NULL, NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->nodes);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        if (lrg_unlock_tree_can_unlock (self, key, available_points))
            g_ptr_array_add (result, value);
    }

    return result;
}

GPtrArray *
lrg_unlock_tree_get_unlocked (LrgUnlockTree *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->nodes);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgUnlockNode *node = value;
        if (node->unlocked)
            g_ptr_array_add (result, node);
    }

    return result;
}

GPtrArray *
lrg_unlock_tree_get_locked (LrgUnlockTree *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->nodes);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgUnlockNode *node = value;
        if (!node->unlocked)
            g_ptr_array_add (result, node);
    }

    return result;
}

gdouble
lrg_unlock_tree_get_progress (LrgUnlockTree *self)
{
    guint total;
    guint unlocked = 0;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_UNLOCK_TREE (self), 0.0);

    total = g_hash_table_size (self->nodes);
    if (total == 0)
        return 1.0;

    g_hash_table_iter_init (&iter, self->nodes);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgUnlockNode *node = value;
        if (node->unlocked)
            unlocked++;
    }

    return (gdouble)unlocked / (gdouble)total;
}

void
lrg_unlock_tree_reset (LrgUnlockTree *self)
{
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_UNLOCK_TREE (self));

    g_hash_table_iter_init (&iter, self->nodes);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgUnlockNode *node = value;
        node->unlocked = FALSE;
        node->unlock_time = 0;
    }
}
