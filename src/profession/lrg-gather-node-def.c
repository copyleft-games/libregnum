/* lrg-gather-node-def.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gather node definitions.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "profession/lrg-gather-node-def.h"

#include <math.h>

typedef struct
{
    gchar        *id;
    gchar        *name;
    gchar        *profession_id;
    guint         required_skill;
    gdouble       respawn;
    gchar        *required_tool;
    LrgSkillBand *band;          /* explicit band, nullable */
    LrgSkillBand *default_band;  /* derived from required_skill */
    GPtrArray    *yields;        /* LrgRecipeItem */
} LrgGatherNodeDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgGatherNodeDef, lrg_gather_node_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_PROFESSION_ID,
    PROP_REQUIRED_SKILL,
    PROP_RESPAWN,
    PROP_REQUIRED_TOOL,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

#define NODE_PRIV(self) \
    ((LrgGatherNodeDefPrivate *)lrg_gather_node_def_get_instance_private (self))

/*
 * replace_string:
 * Swaps *slot for a copy of @value and notifies @pspec when it changed.
 */
static void
replace_string (GObject     *object,
                gchar      **slot,
                const gchar *value,
                GParamSpec  *pspec)
{
    if (g_strcmp0 (*slot, value) == 0)
        return;
    g_free (*slot);
    *slot = g_strdup (value);
    g_object_notify_by_pspec (object, pspec);
}

/*
 * refresh_default_band:
 * Rebuilds the implicit band so its orange threshold tracks required-skill.
 */
static void
refresh_default_band (LrgGatherNodeDefPrivate *priv)
{
    g_clear_pointer (&priv->default_band, lrg_skill_band_free);
    priv->default_band = lrg_skill_band_new_default (priv->required_skill);
}

static void
lrg_gather_node_def_finalize (GObject *object)
{
    LrgGatherNodeDefPrivate *priv = NODE_PRIV (LRG_GATHER_NODE_DEF (object));

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->profession_id, g_free);
    g_clear_pointer (&priv->required_tool, g_free);
    g_clear_pointer (&priv->band, lrg_skill_band_free);
    g_clear_pointer (&priv->default_band, lrg_skill_band_free);
    g_clear_pointer (&priv->yields, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_gather_node_def_parent_class)->finalize (object);
}

static void
lrg_gather_node_def_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgGatherNodeDefPrivate *priv = NODE_PRIV (LRG_GATHER_NODE_DEF (object));

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_PROFESSION_ID:
        g_value_set_string (value, priv->profession_id);
        break;
    case PROP_REQUIRED_SKILL:
        g_value_set_uint (value, priv->required_skill);
        break;
    case PROP_RESPAWN:
        g_value_set_double (value, priv->respawn);
        break;
    case PROP_REQUIRED_TOOL:
        g_value_set_string (value, priv->required_tool);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_gather_node_def_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgGatherNodeDefPrivate *priv = NODE_PRIV (LRG_GATHER_NODE_DEF (object));
    gdouble d;

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        replace_string (object, &priv->name, g_value_get_string (value), pspec);
        break;
    case PROP_PROFESSION_ID:
        replace_string (object, &priv->profession_id, g_value_get_string (value), pspec);
        break;
    case PROP_REQUIRED_SKILL:
        if (priv->required_skill != g_value_get_uint (value))
        {
            priv->required_skill = g_value_get_uint (value);
            refresh_default_band (priv);
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_RESPAWN:
        d = g_value_get_double (value);
        if (priv->respawn != d)
        {
            priv->respawn = d;
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_REQUIRED_TOOL:
        replace_string (object, &priv->required_tool, g_value_get_string (value), pspec);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_gather_node_def_class_init (LrgGatherNodeDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamFlags flags = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

    object_class->finalize = lrg_gather_node_def_finalize;
    object_class->get_property = lrg_gather_node_def_get_property;
    object_class->set_property = lrg_gather_node_def_set_property;

    /**
     * LrgGatherNodeDef:id:
     *
     * Unique node identifier. Construct-only.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id", "ID", "Node identifier", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);
    /**
     * LrgGatherNodeDef:name:
     *
     * Display name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Display name", NULL, flags);
    /**
     * LrgGatherNodeDef:profession-id:
     *
     * Gathering profession identifier.
     */
    properties[PROP_PROFESSION_ID] =
        g_param_spec_string ("profession-id", "Profession ID",
                             "Gathering profession identifier", NULL, flags);
    /**
     * LrgGatherNodeDef:required-skill:
     *
     * Skill required to gather; also the default orange threshold.
     */
    properties[PROP_REQUIRED_SKILL] =
        g_param_spec_uint ("required-skill", "Required Skill", "Skill required to gather",
                           0, LRG_PROFESSION_SKILL_LIMIT, 0, flags);
    /**
     * LrgGatherNodeDef:respawn:
     *
     * Respawn delay in seconds after depletion.
     */
    properties[PROP_RESPAWN] =
        g_param_spec_double ("respawn", "Respawn", "Respawn delay in seconds",
                             0.0, 86400.0, 0.0, flags);
    /**
     * LrgGatherNodeDef:required-tool:
     *
     * Tool item identifier needed to gather, or %NULL.
     */
    properties[PROP_REQUIRED_TOOL] =
        g_param_spec_string ("required-tool", "Required Tool", "Tool item identifier",
                             NULL, flags);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_gather_node_def_init (LrgGatherNodeDef *self)
{
    LrgGatherNodeDefPrivate *priv = NODE_PRIV (self);

    priv->yields = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_recipe_item_free);
    refresh_default_band (priv);
}

LrgGatherNodeDef *
lrg_gather_node_def_new (const gchar *id,
                         const gchar *profession_id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_GATHER_NODE_DEF,
                         "id", id,
                         "profession-id", profession_id,
                         NULL);
}

const gchar *
lrg_gather_node_def_get_id (LrgGatherNodeDef *self)
{
    g_return_val_if_fail (LRG_IS_GATHER_NODE_DEF (self), NULL);
    return NODE_PRIV (self)->id;
}

const gchar *
lrg_gather_node_def_get_name (LrgGatherNodeDef *self)
{
    g_return_val_if_fail (LRG_IS_GATHER_NODE_DEF (self), NULL);
    return NODE_PRIV (self)->name;
}

void
lrg_gather_node_def_set_name (LrgGatherNodeDef *self,
                              const gchar      *name)
{
    g_return_if_fail (LRG_IS_GATHER_NODE_DEF (self));
    g_object_set (self, "name", name, NULL);
}

const gchar *
lrg_gather_node_def_get_profession_id (LrgGatherNodeDef *self)
{
    g_return_val_if_fail (LRG_IS_GATHER_NODE_DEF (self), NULL);
    return NODE_PRIV (self)->profession_id;
}

void
lrg_gather_node_def_set_profession_id (LrgGatherNodeDef *self,
                                       const gchar      *profession_id)
{
    g_return_if_fail (LRG_IS_GATHER_NODE_DEF (self));
    g_object_set (self, "profession-id", profession_id, NULL);
}

guint
lrg_gather_node_def_get_required_skill (LrgGatherNodeDef *self)
{
    g_return_val_if_fail (LRG_IS_GATHER_NODE_DEF (self), 0);
    return NODE_PRIV (self)->required_skill;
}

void
lrg_gather_node_def_set_required_skill (LrgGatherNodeDef *self,
                                        guint             skill)
{
    g_return_if_fail (LRG_IS_GATHER_NODE_DEF (self));
    g_return_if_fail (skill <= LRG_PROFESSION_SKILL_LIMIT);
    g_object_set (self, "required-skill", skill, NULL);
}

gdouble
lrg_gather_node_def_get_respawn (LrgGatherNodeDef *self)
{
    g_return_val_if_fail (LRG_IS_GATHER_NODE_DEF (self), 0.0);
    return NODE_PRIV (self)->respawn;
}

void
lrg_gather_node_def_set_respawn (LrgGatherNodeDef *self,
                                 gdouble           seconds)
{
    g_return_if_fail (LRG_IS_GATHER_NODE_DEF (self));
    g_return_if_fail (isfinite (seconds) && seconds >= 0.0 && seconds <= 86400.0);
    g_object_set (self, "respawn", seconds, NULL);
}

const gchar *
lrg_gather_node_def_get_required_tool (LrgGatherNodeDef *self)
{
    g_return_val_if_fail (LRG_IS_GATHER_NODE_DEF (self), NULL);
    return NODE_PRIV (self)->required_tool;
}

void
lrg_gather_node_def_set_required_tool (LrgGatherNodeDef *self,
                                       const gchar      *tool)
{
    g_return_if_fail (LRG_IS_GATHER_NODE_DEF (self));
    g_object_set (self, "required-tool", tool, NULL);
}

void
lrg_gather_node_def_set_band (LrgGatherNodeDef   *self,
                              const LrgSkillBand *band)
{
    LrgGatherNodeDefPrivate *priv;

    g_return_if_fail (LRG_IS_GATHER_NODE_DEF (self));
    g_return_if_fail (band == NULL || lrg_skill_band_is_valid (band));

    priv = NODE_PRIV (self);
    g_clear_pointer (&priv->band, lrg_skill_band_free);
    if (band != NULL)
        priv->band = lrg_skill_band_copy (band);
}

const LrgSkillBand *
lrg_gather_node_def_get_band (LrgGatherNodeDef *self)
{
    LrgGatherNodeDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_GATHER_NODE_DEF (self), NULL);
    priv = NODE_PRIV (self);
    return priv->band != NULL ? priv->band : priv->default_band;
}

void
lrg_gather_node_def_add_yield (LrgGatherNodeDef *self,
                               const gchar      *item_id,
                               guint             count,
                               gdouble           chance)
{
    LrgRecipeItem *item;

    g_return_if_fail (LRG_IS_GATHER_NODE_DEF (self));

    item = lrg_recipe_item_new (item_id, count, chance);
    if (item != NULL)
        g_ptr_array_add (NODE_PRIV (self)->yields, item);
}

GPtrArray *
lrg_gather_node_def_get_yields (LrgGatherNodeDef *self)
{
    g_return_val_if_fail (LRG_IS_GATHER_NODE_DEF (self), NULL);
    return NODE_PRIV (self)->yields;
}
