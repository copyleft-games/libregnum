/* lrg-talent-tree.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Classic tiered talent trees: boxed LrgTalentNode plus the final
 * LrgTalentTree container that owns and validates its nodes.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-talent-tree.h"

#include <string.h>

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

/*
 * talent_id_is_valid:
 *
 * Checks that @id is a usable identifier: non-NULL, non-empty, at most
 * LRG_TALENT_ID_MAX_LENGTH bytes and valid UTF-8.
 */
static gboolean
talent_id_is_valid (const gchar *id)
{
    gsize len;

    if (id == NULL)
        return FALSE;

    len = strlen (id);
    if (len == 0 || len > LRG_TALENT_ID_MAX_LENGTH)
        return FALSE;

    return g_utf8_validate (id, (gssize)len, NULL);
}

/* ========================================================================= */
/* LrgTalentNode                                                             */
/* ========================================================================= */

G_DEFINE_BOXED_TYPE (LrgTalentNode, lrg_talent_node,
                     lrg_talent_node_copy,
                     lrg_talent_node_free)

LrgTalentNode *
lrg_talent_node_new (const gchar *id,
                     guint        tier,
                     guint        column,
                     guint        max_rank)
{
    LrgTalentNode *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (max_rank >= 1, NULL);

    self = g_new0 (LrgTalentNode, 1);
    self->id = g_strdup (id);
    self->tier = tier;
    self->column = column;
    self->max_rank = max_rank;
    self->rank_values = g_array_new (FALSE, TRUE, sizeof (gdouble));

    return self;
}

LrgTalentNode *
lrg_talent_node_copy (const LrgTalentNode *self)
{
    LrgTalentNode *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgTalentNode, 1);
    copy->id = g_strdup (self->id);
    copy->name = g_strdup (self->name);
    copy->description = g_strdup (self->description);
    copy->tier = self->tier;
    copy->column = self->column;
    copy->max_rank = self->max_rank;
    copy->prerequisite = g_strdup (self->prerequisite);
    copy->grants_ability = g_strdup (self->grants_ability);
    copy->effect = g_strdup (self->effect);
    copy->rank_values = g_array_new (FALSE, TRUE, sizeof (gdouble));

    /* A caller may have replaced the public array field with NULL */
    if (self->rank_values != NULL && self->rank_values->len > 0)
        g_array_append_vals (copy->rank_values,
                             self->rank_values->data,
                             self->rank_values->len);

    return copy;
}

void
lrg_talent_node_free (LrgTalentNode *self)
{
    if (self == NULL)
        return;

    g_free (self->id);
    g_free (self->name);
    g_free (self->description);
    g_free (self->prerequisite);
    g_free (self->grants_ability);
    g_free (self->effect);
    if (self->rank_values != NULL)
        g_array_unref (self->rank_values);
    g_free (self);
}

void
lrg_talent_node_add_rank_value (LrgTalentNode *self,
                                gdouble        value)
{
    g_return_if_fail (self != NULL);

    if (self->rank_values == NULL)
        self->rank_values = g_array_new (FALSE, TRUE, sizeof (gdouble));

    g_array_append_val (self->rank_values, value);
}

gdouble
lrg_talent_node_get_value (const LrgTalentNode *self,
                           guint                rank)
{
    guint index;

    g_return_val_if_fail (self != NULL, 0.0);

    if (rank == 0 || self->rank_values == NULL || self->rank_values->len == 0)
        return 0.0;

    /* Clamp to the last provided value */
    index = MIN (rank, self->rank_values->len) - 1;
    return g_array_index (self->rank_values, gdouble, index);
}

/*
 * node_replace_string:
 *
 * Replaces an owned string field of a node.
 */
static void
node_replace_string (gchar       **field,
                     const gchar  *value)
{
    gchar *copy;

    copy = g_strdup (value);
    g_free (*field);
    *field = copy;
}

void
lrg_talent_node_set_name (LrgTalentNode *self,
                          const gchar   *name)
{
    g_return_if_fail (self != NULL);
    node_replace_string (&self->name, name);
}

void
lrg_talent_node_set_description (LrgTalentNode *self,
                                 const gchar   *description)
{
    g_return_if_fail (self != NULL);
    node_replace_string (&self->description, description);
}

void
lrg_talent_node_set_prerequisite (LrgTalentNode *self,
                                  const gchar   *prerequisite)
{
    g_return_if_fail (self != NULL);
    node_replace_string (&self->prerequisite, prerequisite);
}

void
lrg_talent_node_set_grants_ability (LrgTalentNode *self,
                                    const gchar   *ability_id)
{
    g_return_if_fail (self != NULL);
    node_replace_string (&self->grants_ability, ability_id);
}

void
lrg_talent_node_set_effect (LrgTalentNode *self,
                            const gchar   *effect)
{
    g_return_if_fail (self != NULL);
    node_replace_string (&self->effect, effect);
}

/* ========================================================================= */
/* LrgTalentTree                                                             */
/* ========================================================================= */

struct _LrgTalentTree
{
    GObject     parent_instance;

    gchar      *id;
    gchar      *name;
    gchar      *description;
    gchar      *icon;
    gchar      *class_id;
    gchar      *role;
    guint       tier_gate;

    GPtrArray  *nodes;      /* owns LrgTalentNode*, sorted (tier, column, id) */
    GHashTable *node_index; /* id (borrowed from node) -> LrgTalentNode* */
};

G_DEFINE_TYPE (LrgTalentTree, lrg_talent_tree, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_CLASS_ID,
    PROP_ROLE,
    PROP_TIER_GATE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * compare_nodes:
 *
 * GPtrArray sort function ordering nodes by (tier, column, id).
 */
static gint
compare_nodes (gconstpointer a,
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

/*
 * tree_set_string:
 *
 * Replaces a string field and notifies @pspec only when the value changed.
 */
static void
tree_set_string (LrgTalentTree  *self,
                 gchar         **field,
                 const gchar    *value,
                 GParamSpec     *pspec)
{
    if (g_strcmp0 (*field, value) == 0)
        return;

    g_free (*field);
    *field = g_strdup (value);
    g_object_notify_by_pspec (G_OBJECT (self), pspec);
}

static void
lrg_talent_tree_finalize (GObject *object)
{
    LrgTalentTree *self = LRG_TALENT_TREE (object);

    g_clear_pointer (&self->node_index, g_hash_table_unref);
    g_clear_pointer (&self->nodes, g_ptr_array_unref);
    g_free (self->id);
    g_free (self->name);
    g_free (self->description);
    g_free (self->icon);
    g_free (self->class_id);
    g_free (self->role);

    G_OBJECT_CLASS (lrg_talent_tree_parent_class)->finalize (object);
}

static void
lrg_talent_tree_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgTalentTree *self = LRG_TALENT_TREE (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, self->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, self->description);
        break;
    case PROP_ICON:
        g_value_set_string (value, self->icon);
        break;
    case PROP_CLASS_ID:
        g_value_set_string (value, self->class_id);
        break;
    case PROP_ROLE:
        g_value_set_string (value, self->role);
        break;
    case PROP_TIER_GATE:
        g_value_set_uint (value, self->tier_gate);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_talent_tree_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgTalentTree *self = LRG_TALENT_TREE (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (self->id);
        self->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        lrg_talent_tree_set_name (self, g_value_get_string (value));
        break;
    case PROP_DESCRIPTION:
        lrg_talent_tree_set_description (self, g_value_get_string (value));
        break;
    case PROP_ICON:
        lrg_talent_tree_set_icon (self, g_value_get_string (value));
        break;
    case PROP_CLASS_ID:
        lrg_talent_tree_set_class_id (self, g_value_get_string (value));
        break;
    case PROP_ROLE:
        lrg_talent_tree_set_role (self, g_value_get_string (value));
        break;
    case PROP_TIER_GATE:
        lrg_talent_tree_set_tier_gate (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_talent_tree_class_init (LrgTalentTreeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_talent_tree_finalize;
    object_class->get_property = lrg_talent_tree_get_property;
    object_class->set_property = lrg_talent_tree_set_property;

    /**
     * LrgTalentTree:id:
     *
     * Unique tree id.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id", "ID", "Unique tree id", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentTree:name:
     *
     * Display name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Display name", NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentTree:description:
     *
     * Display description.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", "Description", "Display description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentTree:icon:
     *
     * Icon key.
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon", "Icon", "Icon key", NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentTree:class-id:
     *
     * Owning class id; loadouts only spend in trees of their own class.
     */
    properties[PROP_CLASS_ID] =
        g_param_spec_string ("class-id", "Class ID", "Owning class id", NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentTree:role:
     *
     * Role hint such as "tank", "healer" or "damage".
     */
    properties[PROP_ROLE] =
        g_param_spec_string ("role", "Role", "Role hint", NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTalentTree:tier-gate:
     *
     * Points required in this tree per tier: tier t needs t * tier-gate.
     */
    properties[PROP_TIER_GATE] =
        g_param_spec_uint ("tier-gate", "Tier Gate", "Points per tier",
                           0, LRG_TALENT_TREE_MAX_TIER_GATE,
                           LRG_TALENT_TREE_DEFAULT_TIER_GATE,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_talent_tree_init (LrgTalentTree *self)
{
    self->tier_gate = LRG_TALENT_TREE_DEFAULT_TIER_GATE;
    self->nodes = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_talent_node_free);
    self->node_index = g_hash_table_new (g_str_hash, g_str_equal);
}

LrgTalentTree *
lrg_talent_tree_new (const gchar *id,
                     const gchar *class_id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_TALENT_TREE,
                         "id", id,
                         "class-id", class_id,
                         NULL);
}

const gchar *
lrg_talent_tree_get_id (LrgTalentTree *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), NULL);
    return self->id;
}

const gchar *
lrg_talent_tree_get_name (LrgTalentTree *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), NULL);
    return self->name;
}

void
lrg_talent_tree_set_name (LrgTalentTree *self,
                          const gchar   *name)
{
    g_return_if_fail (LRG_IS_TALENT_TREE (self));
    tree_set_string (self, &self->name, name, properties[PROP_NAME]);
}

const gchar *
lrg_talent_tree_get_description (LrgTalentTree *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), NULL);
    return self->description;
}

void
lrg_talent_tree_set_description (LrgTalentTree *self,
                                 const gchar   *description)
{
    g_return_if_fail (LRG_IS_TALENT_TREE (self));
    tree_set_string (self, &self->description, description,
                     properties[PROP_DESCRIPTION]);
}

const gchar *
lrg_talent_tree_get_icon (LrgTalentTree *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), NULL);
    return self->icon;
}

void
lrg_talent_tree_set_icon (LrgTalentTree *self,
                          const gchar   *icon)
{
    g_return_if_fail (LRG_IS_TALENT_TREE (self));
    tree_set_string (self, &self->icon, icon, properties[PROP_ICON]);
}

const gchar *
lrg_talent_tree_get_class_id (LrgTalentTree *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), NULL);
    return self->class_id;
}

void
lrg_talent_tree_set_class_id (LrgTalentTree *self,
                              const gchar   *class_id)
{
    g_return_if_fail (LRG_IS_TALENT_TREE (self));
    tree_set_string (self, &self->class_id, class_id, properties[PROP_CLASS_ID]);
}

const gchar *
lrg_talent_tree_get_role (LrgTalentTree *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), NULL);
    return self->role;
}

void
lrg_talent_tree_set_role (LrgTalentTree *self,
                          const gchar   *role)
{
    g_return_if_fail (LRG_IS_TALENT_TREE (self));
    tree_set_string (self, &self->role, role, properties[PROP_ROLE]);
}

guint
lrg_talent_tree_get_tier_gate (LrgTalentTree *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), 0);
    return self->tier_gate;
}

void
lrg_talent_tree_set_tier_gate (LrgTalentTree *self,
                               guint          tier_gate)
{
    g_return_if_fail (LRG_IS_TALENT_TREE (self));
    g_return_if_fail (tier_gate <= LRG_TALENT_TREE_MAX_TIER_GATE);

    if (self->tier_gate == tier_gate)
        return;

    self->tier_gate = tier_gate;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIER_GATE]);
}

gboolean
lrg_talent_tree_add_node (LrgTalentTree  *self,
                          LrgTalentNode  *node,
                          GError        **error)
{
    g_autoptr(LrgTalentNode) owned = node;

    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), FALSE);
    g_return_val_if_fail (node != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* Per-node sanity: id and rank bounds */
    if (!talent_id_is_valid (owned->id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent node id must be non-empty UTF-8 of at most %d bytes",
                     LRG_TALENT_ID_MAX_LENGTH);
        return FALSE;
    }
    if (owned->max_rank < 1 || owned->max_rank > LRG_TALENT_MAX_RANK)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Talent node '%s' max rank %u outside 1..%d",
                     owned->id, owned->max_rank, LRG_TALENT_MAX_RANK);
        return FALSE;
    }
    if (g_hash_table_contains (self->node_index, owned->id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE,
                     "Talent tree '%s' already has node '%s'",
                     self->id, owned->id);
        return FALSE;
    }

    /* Ensure the public array field is never NULL once owned by the tree */
    if (owned->rank_values == NULL)
        owned->rank_values = g_array_new (FALSE, TRUE, sizeof (gdouble));

    /* Insert and keep the node list sorted by (tier, column, id) */
    g_hash_table_insert (self->node_index, owned->id, owned);
    g_ptr_array_add (self->nodes, g_steal_pointer (&owned));
    g_ptr_array_sort (self->nodes, compare_nodes);

    return TRUE;
}

const LrgTalentNode *
lrg_talent_tree_get_node (LrgTalentTree *self,
                          const gchar   *node_id)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), NULL);

    if (node_id == NULL)
        return NULL;

    return g_hash_table_lookup (self->node_index, node_id);
}

GPtrArray *
lrg_talent_tree_get_nodes (LrgTalentTree *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), NULL);
    return self->nodes;
}

guint
lrg_talent_tree_get_node_count (LrgTalentTree *self)
{
    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), 0);
    return self->nodes->len;
}

guint
lrg_talent_tree_get_tier_count (LrgTalentTree *self)
{
    const LrgTalentNode *last;

    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), 0);

    if (self->nodes->len == 0)
        return 0;

    /* Sorted by tier, so the last node has the highest tier */
    last = g_ptr_array_index (self->nodes, self->nodes->len - 1);
    return last->tier + 1;
}

guint
lrg_talent_tree_get_max_points (LrgTalentTree *self)
{
    guint64 total;
    guint i;

    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), 0);

    total = 0;
    for (i = 0; i < self->nodes->len; i++)
    {
        const LrgTalentNode *node = g_ptr_array_index (self->nodes, i);
        total += node->max_rank;
    }

    return (guint)MIN (total, (guint64)G_MAXUINT);
}

gboolean
lrg_talent_tree_validate (LrgTalentTree  *self,
                          GError        **error)
{
    guint64 lower_points;
    guint64 tier_points;
    guint current_tier;
    guint i;

    g_return_val_if_fail (LRG_IS_TALENT_TREE (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /*
     * Pass 1: per-node checks that need a lookup of other nodes.
     * Nodes are iterated in sorted order so the first reported error is
     * deterministic.
     */
    for (i = 0; i < self->nodes->len; i++)
    {
        const LrgTalentNode *node = g_ptr_array_index (self->nodes, i);
        const LrgTalentNode *prereq;
        guint n_values;

        /* Public fields may have been edited after add_node() */
        if (node->max_rank < 1 || node->max_rank > LRG_TALENT_MAX_RANK)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Talent node '%s' max rank %u outside 1..%d",
                         node->id, node->max_rank, LRG_TALENT_MAX_RANK);
            return FALSE;
        }

        n_values = (node->rank_values != NULL) ? node->rank_values->len : 0;
        if (n_values != 0 && n_values != node->max_rank)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Talent node '%s' has %u rank values but max rank %u",
                         node->id, n_values, node->max_rank);
            return FALSE;
        }

        if (node->prerequisite != NULL)
        {
            prereq = g_hash_table_lookup (self->node_index, node->prerequisite);
            if (prereq == NULL)
            {
                g_set_error (error, LRG_PROGRESSION_ERROR,
                             LRG_PROGRESSION_ERROR_NOT_FOUND,
                             "Talent node '%s' requires unknown node '%s'",
                             node->id, node->prerequisite);
                return FALSE;
            }
            if (prereq->tier >= node->tier)
            {
                g_set_error (error, LRG_PROGRESSION_ERROR,
                             LRG_PROGRESSION_ERROR_INVALID,
                             "Talent node '%s' (tier %u) requires '%s' which is "
                             "not in a lower tier (tier %u)",
                             node->id, node->tier, prereq->id, prereq->tier);
                return FALSE;
            }
        }

        /* Sorted order puts equal (tier, column) pairs next to each other */
        if (i > 0)
        {
            const LrgTalentNode *prev = g_ptr_array_index (self->nodes, i - 1);
            if (prev->tier == node->tier && prev->column == node->column)
            {
                g_set_error (error, LRG_PROGRESSION_ERROR,
                             LRG_PROGRESSION_ERROR_DUPLICATE,
                             "Talent nodes '%s' and '%s' share tier %u column %u",
                             prev->id, node->id, node->tier, node->column);
                return FALSE;
            }
        }
    }

    /*
     * Pass 2: reachability. Walk tiers in order accumulating the maximum
     * points obtainable in all lower tiers; each populated tier t needs
     * at least t * tier-gate of them.
     */
    lower_points = 0;
    tier_points = 0;
    current_tier = 0;
    for (i = 0; i < self->nodes->len; i++)
    {
        const LrgTalentNode *node = g_ptr_array_index (self->nodes, i);

        if (node->tier != current_tier)
        {
            lower_points += tier_points;
            tier_points = 0;
            current_tier = node->tier;

            if ((guint64)current_tier * self->tier_gate > lower_points)
            {
                g_set_error (error, LRG_PROGRESSION_ERROR,
                             LRG_PROGRESSION_ERROR_REQUIREMENT,
                             "Talent tree '%s' tier %u needs %" G_GUINT64_FORMAT
                             " points but lower tiers only provide %" G_GUINT64_FORMAT,
                             self->id, current_tier,
                             (guint64)current_tier * self->tier_gate, lower_points);
                return FALSE;
            }
        }
        tier_points += node->max_rank;
    }

    return TRUE;
}
