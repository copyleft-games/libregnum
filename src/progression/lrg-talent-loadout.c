/* lrg-talent-loadout.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Points spent by one character across the talent trees of its class.
 * Spending is validated exactly (class, node, rank, points, tier gate,
 * prerequisite) and whole loadouts can be re-validated by replaying all
 * spent points, which is what an authoritative server does after loading
 * a persisted snapshot.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-talent-loadout.h"

#include <string.h>

#define LOADOUT_MAX_POINTS_PER_LEVEL (100)

struct _LrgTalentLoadout
{
    GObject     parent_instance;

    gchar      *class_id;
    guint       first_level;
    guint       points_per_level;
    gchar      *spec;

    /* tree id (owned) -> GHashTable (node id (owned) -> GUINT_TO_POINTER (rank)) */
    GHashTable *spent;
    guint       points_spent;
};

G_DEFINE_TYPE (LrgTalentLoadout, lrg_talent_loadout, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CLASS_ID,
    PROP_FIRST_LEVEL,
    PROP_POINTS_PER_LEVEL,
    PROP_SPEC,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

/*
 * loadout_id_is_valid:
 *
 * Checks that @id is non-NULL, non-empty, at most LRG_TALENT_ID_MAX_LENGTH
 * bytes and valid UTF-8.
 */
static gboolean
loadout_id_is_valid (const gchar *id)
{
    gsize len;

    if (id == NULL)
        return FALSE;

    len = strlen (id);
    if (len == 0 || len > LRG_TALENT_ID_MAX_LENGTH)
        return FALSE;

    return g_utf8_validate (id, (gssize)len, NULL);
}

/*
 * loadout_get_tree_ranks:
 *
 * Returns the node -> rank table of @tree_id, creating it when @create.
 */
static GHashTable *
loadout_get_tree_ranks (LrgTalentLoadout *self,
                        const gchar      *tree_id,
                        gboolean          create)
{
    GHashTable *ranks;

    ranks = g_hash_table_lookup (self->spent, tree_id);
    if (ranks == NULL && create)
    {
        ranks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        g_hash_table_insert (self->spent, g_strdup (tree_id), ranks);
    }

    return ranks;
}

/*
 * loadout_rank_of:
 *
 * Current rank of (@tree_id, @node_id), 0 when unranked.
 */
static guint
loadout_rank_of (LrgTalentLoadout *self,
                 const gchar      *tree_id,
                 const gchar      *node_id)
{
    GHashTable *ranks;

    if (tree_id == NULL || node_id == NULL)
        return 0;

    ranks = g_hash_table_lookup (self->spent, tree_id);
    if (ranks == NULL)
        return 0;

    return GPOINTER_TO_UINT (g_hash_table_lookup (ranks, node_id));
}

/*
 * loadout_add_rank:
 *
 * Increments (@tree_id, @node_id) by @delta and the spent total. The
 * caller has already validated the change.
 */
static void
loadout_add_rank (LrgTalentLoadout *self,
                  const gchar      *tree_id,
                  const gchar      *node_id,
                  guint             delta)
{
    GHashTable *ranks;
    guint rank;

    ranks = loadout_get_tree_ranks (self, tree_id, TRUE);
    rank = GPOINTER_TO_UINT (g_hash_table_lookup (ranks, node_id));
    g_hash_table_insert (ranks, g_strdup (node_id), GUINT_TO_POINTER (rank + delta));
    self->points_spent += delta;
}

/*
 * sorted_keys:
 *
 * Returns the keys of @table sorted with g_strcmp0. The strings are
 * borrowed from the table.
 */
static GPtrArray *
sorted_keys (GHashTable *table)
{
    GPtrArray *keys;
    GHashTableIter iter;
    gpointer key;

    keys = g_ptr_array_new ();
    g_hash_table_iter_init (&iter, table);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (keys, key);

    g_ptr_array_sort_values (keys, (GCompareFunc)g_strcmp0);
    return keys;
}

/*
 * compare_node_ptrs:
 *
 * Orders talent nodes by (tier, column, id) for replay.
 */
static gint
compare_node_ptrs (gconstpointer a,
                   gconstpointer b)
{
    const LrgTalentNode *na;
    const LrgTalentNode *nb;

    na = *(const LrgTalentNode * const *)a;
    nb = *(const LrgTalentNode * const *)b;

    if (na->tier != nb->tier)
        return (na->tier < nb->tier) ? -1 : 1;
    if (na->column != nb->column)
        return (na->column < nb->column) ? -1 : 1;
    return g_strcmp0 (na->id, nb->id);
}

/* ========================================================================= */
/* GObject                                                                   */
/* ========================================================================= */

static void
lrg_talent_loadout_finalize (GObject *object)
{
    LrgTalentLoadout *self = LRG_TALENT_LOADOUT (object);

    g_clear_pointer (&self->spent, g_hash_table_unref);
    g_free (self->class_id);
    g_free (self->spec);

    G_OBJECT_CLASS (lrg_talent_loadout_parent_class)->finalize (object);
}

static void
lrg_talent_loadout_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgTalentLoadout *self = LRG_TALENT_LOADOUT (object);

    switch (prop_id)
    {
    case PROP_CLASS_ID:
        g_value_set_string (value, self->class_id);
        break;
    case PROP_FIRST_LEVEL:
        g_value_set_uint (value, self->first_level);
        break;
    case PROP_POINTS_PER_LEVEL:
        g_value_set_uint (value, self->points_per_level);
        break;
    case PROP_SPEC:
        g_value_set_string (value, self->spec);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_talent_loadout_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgTalentLoadout *self = LRG_TALENT_LOADOUT (object);

    switch (prop_id)
    {
    case PROP_CLASS_ID:
        g_free (self->class_id);
        self->class_id = g_value_dup_string (value);
        break;
    case PROP_FIRST_LEVEL:
        lrg_talent_loadout_set_first_level (self, g_value_get_uint (value));
        break;
    case PROP_POINTS_PER_LEVEL:
        lrg_talent_loadout_set_points_per_level (self, g_value_get_uint (value));
        break;
    case PROP_SPEC:
        lrg_talent_loadout_set_spec (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_talent_loadout_class_init (LrgTalentLoadoutClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_talent_loadout_finalize;
    object_class->get_property = lrg_talent_loadout_get_property;
    object_class->set_property = lrg_talent_loadout_set_property;

    /**
     * LrgTalentLoadout:class-id:
     *
     * Class whose trees accept points.
     */
    properties[PROP_CLASS_ID] =
        g_param_spec_string ("class-id", "Class ID", "Owning class id", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentLoadout:first-level:
     *
     * Level granting the first talent point.
     */
    properties[PROP_FIRST_LEVEL] =
        g_param_spec_uint ("first-level", "First Level",
                           "Level granting the first point",
                           1, G_MAXUINT, LRG_TALENT_LOADOUT_DEFAULT_FIRST_LEVEL,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentLoadout:points-per-level:
     *
     * Talent points granted per level from first-level on.
     */
    properties[PROP_POINTS_PER_LEVEL] =
        g_param_spec_uint ("points-per-level", "Points Per Level",
                           "Points granted per level",
                           0, LOADOUT_MAX_POINTS_PER_LEVEL, 1,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentLoadout:spec:
     *
     * Primary tree id (specialisation), or %NULL.
     */
    properties[PROP_SPEC] =
        g_param_spec_string ("spec", "Spec", "Primary tree id", NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_talent_loadout_init (LrgTalentLoadout *self)
{
    self->first_level = LRG_TALENT_LOADOUT_DEFAULT_FIRST_LEVEL;
    self->points_per_level = 1;
    self->spent = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                         (GDestroyNotify)g_hash_table_unref);
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

LrgTalentLoadout *
lrg_talent_loadout_new (const gchar *class_id)
{
    g_return_val_if_fail (class_id != NULL, NULL);

    return g_object_new (LRG_TYPE_TALENT_LOADOUT, "class-id", class_id, NULL);
}

const gchar *
lrg_talent_loadout_get_class_id (LrgTalentLoadout *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), NULL);
    return self->class_id;
}

guint
lrg_talent_loadout_get_first_level (LrgTalentLoadout *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), 0);
    return self->first_level;
}

void
lrg_talent_loadout_set_first_level (LrgTalentLoadout *self,
                                    guint             first_level)
{
    g_return_if_fail (LRG_IS_TALENT_LOADOUT (self));
    g_return_if_fail (first_level >= 1);

    if (self->first_level == first_level)
        return;

    self->first_level = first_level;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIRST_LEVEL]);
}

guint
lrg_talent_loadout_get_points_per_level (LrgTalentLoadout *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), 0);
    return self->points_per_level;
}

void
lrg_talent_loadout_set_points_per_level (LrgTalentLoadout *self,
                                         guint             points_per_level)
{
    g_return_if_fail (LRG_IS_TALENT_LOADOUT (self));
    g_return_if_fail (points_per_level <= LOADOUT_MAX_POINTS_PER_LEVEL);

    if (self->points_per_level == points_per_level)
        return;

    self->points_per_level = points_per_level;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINTS_PER_LEVEL]);
}

guint
lrg_talent_loadout_get_points_total (LrgTalentLoadout *self,
                                     guint             level)
{
    guint64 total;

    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), 0);

    if (level < self->first_level)
        return 0;

    /* 64-bit product of two 32-bit values cannot overflow; saturate to guint */
    total = ((guint64)level - self->first_level + 1) * self->points_per_level;
    return (guint)MIN (total, (guint64)G_MAXUINT);
}

guint
lrg_talent_loadout_get_points_spent (LrgTalentLoadout *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), 0);
    return self->points_spent;
}

guint
lrg_talent_loadout_get_points_available (LrgTalentLoadout *self,
                                         guint             level)
{
    guint total;

    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), 0);

    total = lrg_talent_loadout_get_points_total (self, level);
    return (total > self->points_spent) ? total - self->points_spent : 0;
}

guint
lrg_talent_loadout_get_points_in_tree (LrgTalentLoadout *self,
                                       const gchar      *tree_id)
{
    GHashTable *ranks;
    GHashTableIter iter;
    gpointer value;
    guint total;

    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), 0);

    if (tree_id == NULL)
        return 0;

    ranks = g_hash_table_lookup (self->spent, tree_id);
    if (ranks == NULL)
        return 0;

    total = 0;
    g_hash_table_iter_init (&iter, ranks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        total += GPOINTER_TO_UINT (value);

    return total;
}

guint
lrg_talent_loadout_get_rank (LrgTalentLoadout *self,
                             const gchar      *tree_id,
                             const gchar      *node_id)
{
    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), 0);
    return loadout_rank_of (self, tree_id, node_id);
}

GPtrArray *
lrg_talent_loadout_get_trees (LrgTalentLoadout *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), NULL);
    return sorted_keys (self->spent);
}

gboolean
lrg_talent_loadout_can_spend (LrgTalentLoadout  *self,
                              LrgTalentTree     *tree,
                              const gchar       *node_id,
                              guint              level,
                              GError           **error)
{
    const LrgTalentNode *node;
    const gchar *tree_id;
    guint rank;
    guint64 needed;
    guint in_tree;

    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), FALSE);
    g_return_val_if_fail (LRG_IS_TALENT_TREE (tree), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    tree_id = lrg_talent_tree_get_id (tree);

    /* 1. The tree must belong to this loadout's class */
    if (g_strcmp0 (lrg_talent_tree_get_class_id (tree), self->class_id) != 0)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent tree '%s' does not belong to class '%s'",
                     tree_id, self->class_id);
        return FALSE;
    }

    /* 2. The node must exist in the tree */
    node = lrg_talent_tree_get_node (tree, node_id);
    if (node == NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent tree '%s' has no node '%s'",
                     tree_id, node_id != NULL ? node_id : "(null)");
        return FALSE;
    }

    /* 3. Max rank */
    rank = loadout_rank_of (self, tree_id, node_id);
    if (rank >= node->max_rank)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "Talent '%s' is already at max rank %u",
                     node_id, node->max_rank);
        return FALSE;
    }

    /* 4. Unspent points at this level */
    if (lrg_talent_loadout_get_points_available (self, level) == 0)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "No talent points available at level %u", level);
        return FALSE;
    }

    /* 5. Tier gate: t * tier-gate points ALREADY spent in this tree */
    in_tree = lrg_talent_loadout_get_points_in_tree (self, tree_id);
    needed = (guint64)node->tier * lrg_talent_tree_get_tier_gate (tree);
    if ((guint64)in_tree < needed)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Tier %u of '%s' requires %" G_GUINT64_FORMAT
                     " points in the tree (have %u)",
                     node->tier, tree_id, needed, in_tree);
        return FALSE;
    }

    /* 6. Prerequisite at max rank */
    if (node->prerequisite != NULL)
    {
        const LrgTalentNode *prereq;

        prereq = lrg_talent_tree_get_node (tree, node->prerequisite);
        if (prereq == NULL ||
            loadout_rank_of (self, tree_id, prereq->id) < prereq->max_rank)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR,
                         LRG_PROGRESSION_ERROR_REQUIREMENT,
                         "Talent '%s' requires '%s' at max rank",
                         node_id, node->prerequisite);
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
lrg_talent_loadout_spend (LrgTalentLoadout  *self,
                          LrgTalentTree     *tree,
                          const gchar       *node_id,
                          guint              level,
                          GError           **error)
{
    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), FALSE);
    g_return_val_if_fail (LRG_IS_TALENT_TREE (tree), FALSE);

    if (!lrg_talent_loadout_can_spend (self, tree, node_id, level, error))
        return FALSE;

    loadout_add_rank (self, lrg_talent_tree_get_id (tree), node_id, 1);
    return TRUE;
}

void
lrg_talent_loadout_reset (LrgTalentLoadout *self)
{
    g_return_if_fail (LRG_IS_TALENT_LOADOUT (self));

    g_hash_table_remove_all (self->spent);
    self->points_spent = 0;
}

const gchar *
lrg_talent_loadout_get_spec (LrgTalentLoadout *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), NULL);
    return self->spec;
}

void
lrg_talent_loadout_set_spec (LrgTalentLoadout *self,
                             const gchar      *tree_id)
{
    g_return_if_fail (LRG_IS_TALENT_LOADOUT (self));

    if (g_strcmp0 (self->spec, tree_id) == 0)
        return;

    g_free (self->spec);
    self->spec = g_strdup (tree_id);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEC]);
}

GPtrArray *
lrg_talent_loadout_get_granted_abilities (LrgTalentLoadout *self,
                                          GHashTable       *trees)
{
    g_autoptr(GHashTable) seen = NULL;
    GPtrArray *result;
    GHashTableIter tree_iter;
    gpointer tree_key;
    gpointer tree_value;

    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), NULL);
    g_return_val_if_fail (trees != NULL, NULL);

    result = g_ptr_array_new_with_free_func (g_free);
    seen = g_hash_table_new (g_str_hash, g_str_equal);

    /* Walk every ranked node of every known tree */
    g_hash_table_iter_init (&tree_iter, self->spent);
    while (g_hash_table_iter_next (&tree_iter, &tree_key, &tree_value))
    {
        LrgTalentTree *tree;
        GHashTableIter node_iter;
        gpointer node_key;
        gpointer rank;

        tree = g_hash_table_lookup (trees, tree_key);
        if (tree == NULL || !LRG_IS_TALENT_TREE (tree))
            continue;

        g_hash_table_iter_init (&node_iter, tree_value);
        while (g_hash_table_iter_next (&node_iter, &node_key, &rank))
        {
            const LrgTalentNode *node;

            node = lrg_talent_tree_get_node (tree, node_key);
            if (node == NULL || node->grants_ability == NULL ||
                GPOINTER_TO_UINT (rank) == 0)
                continue;

            if (g_hash_table_contains (seen, node->grants_ability))
                continue;

            g_hash_table_add (seen, node->grants_ability);
            g_ptr_array_add (result, g_strdup (node->grants_ability));
        }
    }

    g_ptr_array_sort_values (result, (GCompareFunc)g_strcmp0);
    return result;
}

gdouble
lrg_talent_loadout_sum_effect (LrgTalentLoadout *self,
                               GHashTable       *trees,
                               const gchar      *effect)
{
    GHashTableIter tree_iter;
    gpointer tree_key;
    gpointer tree_value;
    gdouble total;

    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), 0.0);
    g_return_val_if_fail (trees != NULL, 0.0);

    if (effect == NULL)
        return 0.0;

    total = 0.0;
    g_hash_table_iter_init (&tree_iter, self->spent);
    while (g_hash_table_iter_next (&tree_iter, &tree_key, &tree_value))
    {
        LrgTalentTree *tree;
        GHashTableIter node_iter;
        gpointer node_key;
        gpointer rank;

        tree = g_hash_table_lookup (trees, tree_key);
        if (tree == NULL || !LRG_IS_TALENT_TREE (tree))
            continue;

        g_hash_table_iter_init (&node_iter, tree_value);
        while (g_hash_table_iter_next (&node_iter, &node_key, &rank))
        {
            const LrgTalentNode *node;

            node = lrg_talent_tree_get_node (tree, node_key);
            if (node == NULL || g_strcmp0 (node->effect, effect) != 0)
                continue;

            total += lrg_talent_node_get_value (node, GPOINTER_TO_UINT (rank));
        }
    }

    return total;
}

gboolean
lrg_talent_loadout_validate (LrgTalentLoadout  *self,
                             GHashTable        *trees,
                             guint              level,
                             GError           **error)
{
    g_autoptr(LrgTalentLoadout) replay = NULL;
    g_autoptr(GPtrArray) tree_ids = NULL;
    guint t;

    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), FALSE);
    g_return_val_if_fail (trees != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* Fresh loadout with identical rules; @self is never touched */
    replay = lrg_talent_loadout_new (self->class_id);
    replay->first_level = self->first_level;
    replay->points_per_level = self->points_per_level;

    tree_ids = sorted_keys (self->spent);
    for (t = 0; t < tree_ids->len; t++)
    {
        const gchar *tree_id = g_ptr_array_index (tree_ids, t);
        g_autoptr(GPtrArray) nodes = NULL;
        g_autoptr(GPtrArray) node_ids = NULL;
        LrgTalentTree *tree;
        GHashTable *ranks;
        guint n;

        /* The tree must be known and registered under its own id */
        tree = g_hash_table_lookup (trees, tree_id);
        if (tree == NULL || !LRG_IS_TALENT_TREE (tree) ||
            g_strcmp0 (lrg_talent_tree_get_id (tree), tree_id) != 0)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND,
                         "Unknown talent tree '%s'", tree_id);
            return FALSE;
        }

        /* Resolve every ranked node, then order them (tier, column, id) */
        ranks = g_hash_table_lookup (self->spent, tree_id);
        node_ids = sorted_keys (ranks);
        nodes = g_ptr_array_new ();
        for (n = 0; n < node_ids->len; n++)
        {
            const gchar *node_id = g_ptr_array_index (node_ids, n);
            const LrgTalentNode *node = lrg_talent_tree_get_node (tree, node_id);

            if (node == NULL)
            {
                g_set_error (error, LRG_PROGRESSION_ERROR,
                             LRG_PROGRESSION_ERROR_INVALID,
                             "Talent tree '%s' has no node '%s'", tree_id, node_id);
                return FALSE;
            }
            g_ptr_array_add (nodes, (gpointer)node);
        }
        g_ptr_array_sort (nodes, compare_node_ptrs);

        /* Replay one point at a time with the exact spend rules */
        for (n = 0; n < nodes->len; n++)
        {
            const LrgTalentNode *node = g_ptr_array_index (nodes, n);
            guint rank = GPOINTER_TO_UINT (g_hash_table_lookup (ranks, node->id));
            guint r;

            for (r = 0; r < rank; r++)
            {
                if (!lrg_talent_loadout_can_spend (replay, tree, node->id, level, error))
                {
                    g_prefix_error (error, "Invalid talent loadout: ");
                    return FALSE;
                }
                loadout_add_rank (replay, tree_id, node->id, 1);
            }
        }
    }

    /* The spec must name a known tree of this class */
    if (self->spec != NULL)
    {
        LrgTalentTree *spec_tree = g_hash_table_lookup (trees, self->spec);

        if (spec_tree == NULL || !LRG_IS_TALENT_TREE (spec_tree) ||
            g_strcmp0 (lrg_talent_tree_get_id (spec_tree), self->spec) != 0 ||
            g_strcmp0 (lrg_talent_tree_get_class_id (spec_tree), self->class_id) != 0)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Spec '%s' is not a talent tree of class '%s'",
                         self->spec, self->class_id);
            return FALSE;
        }
    }

    return TRUE;
}

GVariant *
lrg_talent_loadout_to_variant (LrgTalentLoadout *self)
{
    g_autoptr(GPtrArray) tree_ids = NULL;
    GVariantBuilder rows;
    GVariant *variant;
    guint t;

    g_return_val_if_fail (LRG_IS_TALENT_LOADOUT (self), NULL);

    g_variant_builder_init (&rows, G_VARIANT_TYPE ("a(ssu)"));

    /* Rows sorted by tree id, then node id */
    tree_ids = sorted_keys (self->spent);
    for (t = 0; t < tree_ids->len; t++)
    {
        const gchar *tree_id = g_ptr_array_index (tree_ids, t);
        GHashTable *ranks = g_hash_table_lookup (self->spent, tree_id);
        g_autoptr(GPtrArray) node_ids = sorted_keys (ranks);
        guint n;

        for (n = 0; n < node_ids->len; n++)
        {
            const gchar *node_id = g_ptr_array_index (node_ids, n);
            guint rank = GPOINTER_TO_UINT (g_hash_table_lookup (ranks, node_id));

            g_variant_builder_add (&rows, "(ssu)", tree_id, node_id, rank);
        }
    }

    variant = g_variant_new ("(sms@a(ssu))",
                             self->class_id,
                             self->spec,
                             g_variant_builder_end (&rows));
    return g_variant_ref_sink (variant);
}

LrgTalentLoadout *
lrg_talent_loadout_new_from_variant (GVariant  *variant,
                                     GError   **error)
{
    g_autoptr(LrgTalentLoadout) self = NULL;
    g_autoptr(GVariant) rows = NULL;
    const gchar *class_id;
    const gchar *spec;
    GVariantIter iter;
    const gchar *tree_id;
    const gchar *node_id;
    guint rank;
    gsize n_rows;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Shape checks */
    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_TALENT_LOADOUT_VARIANT_TYPE)))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent loadout variant has type '%s', expected '%s'",
                     g_variant_get_type_string (variant),
                     LRG_TALENT_LOADOUT_VARIANT_TYPE);
        return NULL;
    }
    if (!g_variant_is_normal_form (variant))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "Talent loadout variant is not in normal form");
        return NULL;
    }

    g_variant_get (variant, "(&sm&s@a(ssu))", &class_id, &spec, &rows);

    if (!loadout_id_is_valid (class_id))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "Talent loadout has an invalid class id");
        return NULL;
    }
    if (spec != NULL && !loadout_id_is_valid (spec))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "Talent loadout has an invalid spec id");
        return NULL;
    }

    n_rows = g_variant_n_children (rows);
    if (n_rows > LRG_TALENT_LOADOUT_MAX_ROWS)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent loadout has %" G_GSIZE_FORMAT " rows (max %d)",
                     n_rows, LRG_TALENT_LOADOUT_MAX_ROWS);
        return NULL;
    }

    self = lrg_talent_loadout_new (class_id);
    self->spec = g_strdup (spec);

    /* Rows: valid ids, rank 1..max, no duplicates */
    g_variant_iter_init (&iter, rows);
    while (g_variant_iter_next (&iter, "(&s&su)", &tree_id, &node_id, &rank))
    {
        if (!loadout_id_is_valid (tree_id) || !loadout_id_is_valid (node_id))
        {
            g_set_error_literal (error, LRG_PROGRESSION_ERROR,
                                 LRG_PROGRESSION_ERROR_INVALID,
                                 "Talent loadout row has an invalid tree or node id");
            return NULL;
        }
        if (rank < 1 || rank > LRG_TALENT_MAX_RANK)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Talent '%s' rank %u outside 1..%d",
                         node_id, rank, LRG_TALENT_MAX_RANK);
            return NULL;
        }
        if (loadout_rank_of (self, tree_id, node_id) != 0)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Talent loadout lists '%s/%s' twice", tree_id, node_id);
            return NULL;
        }

        loadout_add_rank (self, tree_id, node_id, rank);
    }

    return g_steal_pointer (&self);
}
