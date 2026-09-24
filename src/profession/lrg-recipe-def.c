/* lrg-recipe-def.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Crafting recipe definitions and the reagent/product item box.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "profession/lrg-recipe-def.h"

#include <math.h>

/* ==========================================================================
 * LrgRecipeItem (boxed)
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgRecipeItem, lrg_recipe_item,
                     lrg_recipe_item_copy,
                     lrg_recipe_item_free)

LrgRecipeItem *
lrg_recipe_item_new (const gchar *item_id,
                     guint        count,
                     gdouble      chance)
{
    LrgRecipeItem *self;

    g_return_val_if_fail (item_id != NULL && item_id[0] != '\0', NULL);
    g_return_val_if_fail (count > 0, NULL);
    g_return_val_if_fail (isfinite (chance) && chance > 0.0 && chance <= 1.0, NULL);

    self = g_new0 (LrgRecipeItem, 1);
    self->item_id = g_strdup (item_id);
    self->count = count;
    self->chance = chance;
    return self;
}

LrgRecipeItem *
lrg_recipe_item_copy (const LrgRecipeItem *self)
{
    LrgRecipeItem *copy;

    g_return_val_if_fail (self != NULL, NULL);

    /* Copy field by field so even hand-edited structs copy faithfully */
    copy = g_new0 (LrgRecipeItem, 1);
    copy->item_id = g_strdup (self->item_id);
    copy->count = self->count;
    copy->chance = self->chance;
    return copy;
}

void
lrg_recipe_item_free (LrgRecipeItem *self)
{
    if (self == NULL)
        return;
    g_free (self->item_id);
    g_free (self);
}

/* ==========================================================================
 * LrgRecipeDef
 * ========================================================================== */

typedef struct
{
    gchar           *id;
    gchar           *name;
    gchar           *description;
    gchar           *profession_id;
    guint            required_skill;
    gdouble          craft_time;
    LrgRecipeSource  source;
    guint            trainer_cost;
    gchar           *station;
    gchar           *tool;
    guint            required_level;
    LrgSkillBand    *band;          /* explicit band, nullable */
    LrgSkillBand    *default_band;  /* derived from required_skill */
    GPtrArray       *reagents;      /* LrgRecipeItem */
    GPtrArray       *products;      /* LrgRecipeItem */
} LrgRecipeDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgRecipeDef, lrg_recipe_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_PROFESSION_ID,
    PROP_REQUIRED_SKILL,
    PROP_CRAFT_TIME,
    PROP_SOURCE,
    PROP_TRAINER_COST,
    PROP_STATION,
    PROP_TOOL,
    PROP_REQUIRED_LEVEL,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

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
 * replace_uint:
 * Stores @value in *slot and notifies @pspec when it changed.
 */
static void
replace_uint (GObject    *object,
              guint      *slot,
              guint       value,
              GParamSpec *pspec)
{
    if (*slot == value)
        return;
    *slot = value;
    g_object_notify_by_pspec (object, pspec);
}

/*
 * refresh_default_band:
 * Rebuilds the implicit band so its orange threshold tracks required-skill.
 */
static void
refresh_default_band (LrgRecipeDefPrivate *priv)
{
    g_clear_pointer (&priv->default_band, lrg_skill_band_free);
    priv->default_band = lrg_skill_band_new_default (priv->required_skill);
}

static void
lrg_recipe_def_finalize (GObject *object)
{
    LrgRecipeDefPrivate *priv;

    priv = lrg_recipe_def_get_instance_private (LRG_RECIPE_DEF (object));
    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->profession_id, g_free);
    g_clear_pointer (&priv->station, g_free);
    g_clear_pointer (&priv->tool, g_free);
    g_clear_pointer (&priv->band, lrg_skill_band_free);
    g_clear_pointer (&priv->default_band, lrg_skill_band_free);
    g_clear_pointer (&priv->reagents, g_ptr_array_unref);
    g_clear_pointer (&priv->products, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_recipe_def_parent_class)->finalize (object);
}

static void
lrg_recipe_def_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgRecipeDefPrivate *priv;

    priv = lrg_recipe_def_get_instance_private (LRG_RECIPE_DEF (object));
    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, priv->description);
        break;
    case PROP_PROFESSION_ID:
        g_value_set_string (value, priv->profession_id);
        break;
    case PROP_REQUIRED_SKILL:
        g_value_set_uint (value, priv->required_skill);
        break;
    case PROP_CRAFT_TIME:
        g_value_set_double (value, priv->craft_time);
        break;
    case PROP_SOURCE:
        g_value_set_enum (value, (gint)priv->source);
        break;
    case PROP_TRAINER_COST:
        g_value_set_uint (value, priv->trainer_cost);
        break;
    case PROP_STATION:
        g_value_set_string (value, priv->station);
        break;
    case PROP_TOOL:
        g_value_set_string (value, priv->tool);
        break;
    case PROP_REQUIRED_LEVEL:
        g_value_set_uint (value, priv->required_level);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_recipe_def_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgRecipeDefPrivate *priv;
    gdouble d;
    gint e;

    priv = lrg_recipe_def_get_instance_private (LRG_RECIPE_DEF (object));
    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        replace_string (object, &priv->name, g_value_get_string (value), pspec);
        break;
    case PROP_DESCRIPTION:
        replace_string (object, &priv->description, g_value_get_string (value), pspec);
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
    case PROP_CRAFT_TIME:
        d = g_value_get_double (value);
        if (priv->craft_time != d)
        {
            priv->craft_time = d;
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_SOURCE:
        e = g_value_get_enum (value);
        if ((gint)priv->source != e)
        {
            priv->source = (LrgRecipeSource)e;
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_TRAINER_COST:
        replace_uint (object, &priv->trainer_cost, g_value_get_uint (value), pspec);
        break;
    case PROP_STATION:
        replace_string (object, &priv->station, g_value_get_string (value), pspec);
        break;
    case PROP_TOOL:
        replace_string (object, &priv->tool, g_value_get_string (value), pspec);
        break;
    case PROP_REQUIRED_LEVEL:
        replace_uint (object, &priv->required_level, g_value_get_uint (value), pspec);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_recipe_def_class_init (LrgRecipeDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamFlags flags = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

    object_class->finalize = lrg_recipe_def_finalize;
    object_class->get_property = lrg_recipe_def_get_property;
    object_class->set_property = lrg_recipe_def_set_property;

    /**
     * LrgRecipeDef:id:
     *
     * Unique recipe identifier. Construct-only.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id", "ID", "Recipe identifier", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);
    /**
     * LrgRecipeDef:name:
     *
     * Display name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Display name", NULL, flags);
    /**
     * LrgRecipeDef:description:
     *
     * Description text.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", "Description", "Description", NULL, flags);
    /**
     * LrgRecipeDef:profession-id:
     *
     * Identifier of the profession that owns this recipe.
     */
    properties[PROP_PROFESSION_ID] =
        g_param_spec_string ("profession-id", "Profession ID",
                             "Owning profession identifier", NULL, flags);
    /**
     * LrgRecipeDef:required-skill:
     *
     * Skill required to learn and craft; also the default orange threshold.
     */
    properties[PROP_REQUIRED_SKILL] =
        g_param_spec_uint ("required-skill", "Required Skill",
                           "Skill required to learn and craft",
                           0, LRG_PROFESSION_SKILL_LIMIT, 0, flags);
    /**
     * LrgRecipeDef:craft-time:
     *
     * Craft time in seconds.
     */
    properties[PROP_CRAFT_TIME] =
        g_param_spec_double ("craft-time", "Craft Time", "Craft time in seconds",
                             0.0, 86400.0, 0.0, flags);
    /**
     * LrgRecipeDef:source:
     *
     * Where the recipe is learned.
     */
    properties[PROP_SOURCE] =
        g_param_spec_enum ("source", "Source", "Where the recipe is learned",
                           LRG_TYPE_RECIPE_SOURCE, LRG_RECIPE_SOURCE_TRAINER, flags);
    /**
     * LrgRecipeDef:trainer-cost:
     *
     * Cost when bought from a trainer; charged by the caller.
     */
    properties[PROP_TRAINER_COST] =
        g_param_spec_uint ("trainer-cost", "Trainer Cost", "Trainer cost",
                           0, G_MAXUINT, 0, flags);
    /**
     * LrgRecipeDef:station:
     *
     * Required crafting station key, or %NULL.
     */
    properties[PROP_STATION] =
        g_param_spec_string ("station", "Station", "Required crafting station", NULL, flags);
    /**
     * LrgRecipeDef:tool:
     *
     * Required (not consumed) tool item identifier, or %NULL.
     */
    properties[PROP_TOOL] =
        g_param_spec_string ("tool", "Tool", "Required tool item", NULL, flags);
    /**
     * LrgRecipeDef:required-level:
     *
     * Minimum character level; enforced by the caller.
     */
    properties[PROP_REQUIRED_LEVEL] =
        g_param_spec_uint ("required-level", "Required Level",
                           "Minimum character level", 0, G_MAXUINT, 0, flags);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_recipe_def_init (LrgRecipeDef *self)
{
    LrgRecipeDefPrivate *priv = lrg_recipe_def_get_instance_private (self);

    priv->source = LRG_RECIPE_SOURCE_TRAINER;
    priv->reagents = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_recipe_item_free);
    priv->products = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_recipe_item_free);
    refresh_default_band (priv);
}

LrgRecipeDef *
lrg_recipe_def_new (const gchar *id,
                    const gchar *profession_id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_RECIPE_DEF,
                         "id", id,
                         "profession-id", profession_id,
                         NULL);
}

/* --- Simple accessors ---------------------------------------------------- */

#define RECIPE_PRIV(self) \
    ((LrgRecipeDefPrivate *)lrg_recipe_def_get_instance_private (self))

const gchar *
lrg_recipe_def_get_id (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), NULL);
    return RECIPE_PRIV (self)->id;
}

const gchar *
lrg_recipe_def_get_name (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), NULL);
    return RECIPE_PRIV (self)->name;
}

void
lrg_recipe_def_set_name (LrgRecipeDef *self,
                         const gchar  *name)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_object_set (self, "name", name, NULL);
}

const gchar *
lrg_recipe_def_get_description (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), NULL);
    return RECIPE_PRIV (self)->description;
}

void
lrg_recipe_def_set_description (LrgRecipeDef *self,
                                const gchar  *description)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_object_set (self, "description", description, NULL);
}

const gchar *
lrg_recipe_def_get_profession_id (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), NULL);
    return RECIPE_PRIV (self)->profession_id;
}

void
lrg_recipe_def_set_profession_id (LrgRecipeDef *self,
                                  const gchar  *profession_id)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_object_set (self, "profession-id", profession_id, NULL);
}

guint
lrg_recipe_def_get_required_skill (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), 0);
    return RECIPE_PRIV (self)->required_skill;
}

void
lrg_recipe_def_set_required_skill (LrgRecipeDef *self,
                                   guint         skill)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_return_if_fail (skill <= LRG_PROFESSION_SKILL_LIMIT);
    g_object_set (self, "required-skill", skill, NULL);
}

gdouble
lrg_recipe_def_get_craft_time (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), 0.0);
    return RECIPE_PRIV (self)->craft_time;
}

void
lrg_recipe_def_set_craft_time (LrgRecipeDef *self,
                               gdouble       seconds)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_return_if_fail (isfinite (seconds) && seconds >= 0.0 && seconds <= 86400.0);
    g_object_set (self, "craft-time", seconds, NULL);
}

LrgRecipeSource
lrg_recipe_def_get_source (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), LRG_RECIPE_SOURCE_TRAINER);
    return RECIPE_PRIV (self)->source;
}

void
lrg_recipe_def_set_source (LrgRecipeDef    *self,
                           LrgRecipeSource  source)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_object_set (self, "source", source, NULL);
}

guint
lrg_recipe_def_get_trainer_cost (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), 0);
    return RECIPE_PRIV (self)->trainer_cost;
}

void
lrg_recipe_def_set_trainer_cost (LrgRecipeDef *self,
                                 guint         cost)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_object_set (self, "trainer-cost", cost, NULL);
}

const gchar *
lrg_recipe_def_get_station (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), NULL);
    return RECIPE_PRIV (self)->station;
}

void
lrg_recipe_def_set_station (LrgRecipeDef *self,
                            const gchar  *station)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_object_set (self, "station", station, NULL);
}

const gchar *
lrg_recipe_def_get_tool (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), NULL);
    return RECIPE_PRIV (self)->tool;
}

void
lrg_recipe_def_set_tool (LrgRecipeDef *self,
                         const gchar  *tool)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_object_set (self, "tool", tool, NULL);
}

guint
lrg_recipe_def_get_required_level (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), 0);
    return RECIPE_PRIV (self)->required_level;
}

void
lrg_recipe_def_set_required_level (LrgRecipeDef *self,
                                   guint         level)
{
    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_object_set (self, "required-level", level, NULL);
}

/* --- Band ---------------------------------------------------------------- */

void
lrg_recipe_def_set_band (LrgRecipeDef       *self,
                         const LrgSkillBand *band)
{
    LrgRecipeDefPrivate *priv;

    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_return_if_fail (band == NULL || lrg_skill_band_is_valid (band));

    priv = RECIPE_PRIV (self);
    g_clear_pointer (&priv->band, lrg_skill_band_free);
    if (band != NULL)
        priv->band = lrg_skill_band_copy (band);
}

const LrgSkillBand *
lrg_recipe_def_get_band (LrgRecipeDef *self)
{
    LrgRecipeDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), NULL);
    priv = RECIPE_PRIV (self);
    return priv->band != NULL ? priv->band : priv->default_band;
}

gboolean
lrg_recipe_def_has_explicit_band (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), FALSE);
    return RECIPE_PRIV (self)->band != NULL;
}

/* --- Reagents and products ----------------------------------------------- */

void
lrg_recipe_def_add_reagent (LrgRecipeDef *self,
                            const gchar  *item_id,
                            guint         count)
{
    LrgRecipeDefPrivate *priv;
    LrgRecipeItem *item;
    guint i;

    g_return_if_fail (LRG_IS_RECIPE_DEF (self));
    g_return_if_fail (item_id != NULL && item_id[0] != '\0');
    g_return_if_fail (count > 0);

    priv = RECIPE_PRIV (self);

    /* Merge repeated reagents so the craft check sees one total per item */
    for (i = 0; i < priv->reagents->len; i++)
    {
        item = g_ptr_array_index (priv->reagents, i);
        if (g_strcmp0 (item->item_id, item_id) == 0)
        {
            item->count = (count > G_MAXUINT - item->count) ? G_MAXUINT : item->count + count;
            return;
        }
    }

    g_ptr_array_add (priv->reagents, lrg_recipe_item_new (item_id, count, 1.0));
}

void
lrg_recipe_def_add_product (LrgRecipeDef *self,
                            const gchar  *item_id,
                            guint         count,
                            gdouble       chance)
{
    LrgRecipeItem *item;

    g_return_if_fail (LRG_IS_RECIPE_DEF (self));

    item = lrg_recipe_item_new (item_id, count, chance);
    if (item != NULL)
        g_ptr_array_add (RECIPE_PRIV (self)->products, item);
}

GPtrArray *
lrg_recipe_def_get_reagents (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), NULL);
    return RECIPE_PRIV (self)->reagents;
}

GPtrArray *
lrg_recipe_def_get_products (LrgRecipeDef *self)
{
    g_return_val_if_fail (LRG_IS_RECIPE_DEF (self), NULL);
    return RECIPE_PRIV (self)->products;
}
