/* lrg-profession-def.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Profession definitions and their trainer tiers.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "profession/lrg-profession-def.h"
#include "profession/lrg-skill-band.h"

/* Maximum number of tiers a single profession may define. */
#define LRG_PROFESSION_DEF_MAX_TIERS (64)

/* ==========================================================================
 * LrgProfessionTier (boxed)
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgProfessionTier, lrg_profession_tier,
                     lrg_profession_tier_copy,
                     lrg_profession_tier_free)

LrgProfessionTier *
lrg_profession_tier_new (const gchar *name,
                         guint        skill_cap,
                         guint        required_level,
                         guint        cost)
{
    LrgProfessionTier *self;

    self = g_new0 (LrgProfessionTier, 1);
    self->name = g_strdup (name);
    self->skill_cap = skill_cap;
    self->required_level = required_level;
    self->cost = cost;
    return self;
}

LrgProfessionTier *
lrg_profession_tier_copy (const LrgProfessionTier *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return lrg_profession_tier_new (self->name, self->skill_cap,
                                    self->required_level, self->cost);
}

void
lrg_profession_tier_free (LrgProfessionTier *self)
{
    if (self == NULL)
        return;
    g_free (self->name);
    g_free (self);
}

/* ==========================================================================
 * LrgProfessionDef
 * ========================================================================== */

typedef struct
{
    gchar                 *id;
    gchar                 *name;
    gchar                 *description;
    gchar                 *icon;
    LrgProfessionKind      kind;
    LrgProfessionCategory  category;
    GPtrArray             *tiers;   /* LrgProfessionTier, caps strictly increasing */
} LrgProfessionDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgProfessionDef, lrg_profession_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_KIND,
    PROP_CATEGORY,
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

static void
lrg_profession_def_finalize (GObject *object)
{
    LrgProfessionDefPrivate *priv;

    priv = lrg_profession_def_get_instance_private (LRG_PROFESSION_DEF (object));
    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->icon, g_free);
    g_clear_pointer (&priv->tiers, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_profession_def_parent_class)->finalize (object);
}

static void
lrg_profession_def_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgProfessionDefPrivate *priv;

    priv = lrg_profession_def_get_instance_private (LRG_PROFESSION_DEF (object));
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
    case PROP_ICON:
        g_value_set_string (value, priv->icon);
        break;
    case PROP_KIND:
        g_value_set_enum (value, (gint)priv->kind);
        break;
    case PROP_CATEGORY:
        g_value_set_enum (value, (gint)priv->category);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_profession_def_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgProfessionDefPrivate *priv;
    gint v;

    priv = lrg_profession_def_get_instance_private (LRG_PROFESSION_DEF (object));
    switch (prop_id)
    {
    case PROP_ID:
        /* Construct-only: set once, no notification needed */
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        replace_string (object, &priv->name, g_value_get_string (value), pspec);
        break;
    case PROP_DESCRIPTION:
        replace_string (object, &priv->description, g_value_get_string (value), pspec);
        break;
    case PROP_ICON:
        replace_string (object, &priv->icon, g_value_get_string (value), pspec);
        break;
    case PROP_KIND:
        v = g_value_get_enum (value);
        if ((gint)priv->kind != v)
        {
            priv->kind = (LrgProfessionKind)v;
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_CATEGORY:
        v = g_value_get_enum (value);
        if ((gint)priv->category != v)
        {
            priv->category = (LrgProfessionCategory)v;
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_profession_def_class_init (LrgProfessionDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_profession_def_finalize;
    object_class->get_property = lrg_profession_def_get_property;
    object_class->set_property = lrg_profession_def_set_property;

    /**
     * LrgProfessionDef:id:
     *
     * Unique profession identifier. Construct-only.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id", "ID", "Profession identifier", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);
    /**
     * LrgProfessionDef:name:
     *
     * Display name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Display name", NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);
    /**
     * LrgProfessionDef:description:
     *
     * Description text.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", "Description", "Description", NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);
    /**
     * LrgProfessionDef:icon:
     *
     * Icon key.
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon", "Icon", "Icon key", NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);
    /**
     * LrgProfessionDef:kind:
     *
     * Primary professions count against #LrgProfessionState:max-primary.
     */
    properties[PROP_KIND] =
        g_param_spec_enum ("kind", "Kind", "Primary or secondary",
                           LRG_TYPE_PROFESSION_KIND, LRG_PROFESSION_KIND_PRIMARY,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);
    /**
     * LrgProfessionDef:category:
     *
     * Gathering, crafting or service.
     */
    properties[PROP_CATEGORY] =
        g_param_spec_enum ("category", "Category", "Profession category",
                           LRG_TYPE_PROFESSION_CATEGORY,
                           LRG_PROFESSION_CATEGORY_CRAFTING,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_profession_def_init (LrgProfessionDef *self)
{
    LrgProfessionDefPrivate *priv = lrg_profession_def_get_instance_private (self);

    priv->kind = LRG_PROFESSION_KIND_PRIMARY;
    priv->category = LRG_PROFESSION_CATEGORY_CRAFTING;
    priv->tiers = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_profession_tier_free);
}

LrgProfessionDef *
lrg_profession_def_new (const gchar           *id,
                        LrgProfessionKind      kind,
                        LrgProfessionCategory  category)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_PROFESSION_DEF,
                         "id", id,
                         "kind", kind,
                         "category", category,
                         NULL);
}

const gchar *
lrg_profession_def_get_id (LrgProfessionDef *self)
{
    LrgProfessionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), NULL);
    priv = lrg_profession_def_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_profession_def_get_name (LrgProfessionDef *self)
{
    LrgProfessionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), NULL);
    priv = lrg_profession_def_get_instance_private (self);
    return priv->name;
}

void
lrg_profession_def_set_name (LrgProfessionDef *self,
                             const gchar      *name)
{
    g_return_if_fail (LRG_IS_PROFESSION_DEF (self));
    g_object_set (self, "name", name, NULL);
}

const gchar *
lrg_profession_def_get_description (LrgProfessionDef *self)
{
    LrgProfessionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), NULL);
    priv = lrg_profession_def_get_instance_private (self);
    return priv->description;
}

void
lrg_profession_def_set_description (LrgProfessionDef *self,
                                    const gchar      *description)
{
    g_return_if_fail (LRG_IS_PROFESSION_DEF (self));
    g_object_set (self, "description", description, NULL);
}

const gchar *
lrg_profession_def_get_icon (LrgProfessionDef *self)
{
    LrgProfessionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), NULL);
    priv = lrg_profession_def_get_instance_private (self);
    return priv->icon;
}

void
lrg_profession_def_set_icon (LrgProfessionDef *self,
                             const gchar      *icon)
{
    g_return_if_fail (LRG_IS_PROFESSION_DEF (self));
    g_object_set (self, "icon", icon, NULL);
}

LrgProfessionKind
lrg_profession_def_get_kind (LrgProfessionDef *self)
{
    LrgProfessionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), LRG_PROFESSION_KIND_PRIMARY);
    priv = lrg_profession_def_get_instance_private (self);
    return priv->kind;
}

void
lrg_profession_def_set_kind (LrgProfessionDef  *self,
                             LrgProfessionKind  kind)
{
    g_return_if_fail (LRG_IS_PROFESSION_DEF (self));
    g_object_set (self, "kind", kind, NULL);
}

LrgProfessionCategory
lrg_profession_def_get_category (LrgProfessionDef *self)
{
    LrgProfessionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), LRG_PROFESSION_CATEGORY_CRAFTING);
    priv = lrg_profession_def_get_instance_private (self);
    return priv->category;
}

void
lrg_profession_def_set_category (LrgProfessionDef      *self,
                                 LrgProfessionCategory  category)
{
    g_return_if_fail (LRG_IS_PROFESSION_DEF (self));
    g_object_set (self, "category", category, NULL);
}

gboolean
lrg_profession_def_add_tier (LrgProfessionDef   *self,
                             LrgProfessionTier  *tier,
                             GError            **error)
{
    LrgProfessionDefPrivate *priv;
    const LrgProfessionTier *last;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), FALSE);
    g_return_val_if_fail (tier != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    priv = lrg_profession_def_get_instance_private (self);

    /* Bound the tier list */
    if (priv->tiers->len >= LRG_PROFESSION_DEF_MAX_TIERS)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "Profession '%s' already has the maximum of %u tiers",
                     priv->id, (guint)LRG_PROFESSION_DEF_MAX_TIERS);
        lrg_profession_tier_free (tier);
        return FALSE;
    }

    /* The cap must be a usable, bounded skill value */
    if (tier->skill_cap == 0 || tier->skill_cap > LRG_PROFESSION_SKILL_LIMIT)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Tier skill cap %u is outside 1..%u",
                     tier->skill_cap, (guint)LRG_PROFESSION_SKILL_LIMIT);
        lrg_profession_tier_free (tier);
        return FALSE;
    }

    /* Caps must strictly increase so every tier is a real upgrade */
    if (priv->tiers->len > 0)
    {
        last = g_ptr_array_index (priv->tiers, priv->tiers->len - 1);
        if (tier->skill_cap <= last->skill_cap)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Tier skill cap %u must exceed the previous cap %u",
                         tier->skill_cap, last->skill_cap);
            lrg_profession_tier_free (tier);
            return FALSE;
        }
    }

    g_ptr_array_add (priv->tiers, tier);
    return TRUE;
}

guint
lrg_profession_def_get_tier_count (LrgProfessionDef *self)
{
    LrgProfessionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), 0);
    priv = lrg_profession_def_get_instance_private (self);
    return priv->tiers->len;
}

const LrgProfessionTier *
lrg_profession_def_get_tier (LrgProfessionDef *self,
                             guint             index)
{
    LrgProfessionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), NULL);
    priv = lrg_profession_def_get_instance_private (self);
    if (index >= priv->tiers->len)
        return NULL;
    return g_ptr_array_index (priv->tiers, index);
}

guint
lrg_profession_def_get_max_skill (LrgProfessionDef *self)
{
    LrgProfessionDefPrivate *priv;
    const LrgProfessionTier *last;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), 0);
    priv = lrg_profession_def_get_instance_private (self);
    if (priv->tiers->len == 0)
        return 0;
    last = g_ptr_array_index (priv->tiers, priv->tiers->len - 1);
    return last->skill_cap;
}

gint
lrg_profession_def_get_tier_for_cap (LrgProfessionDef *self,
                                     guint             skill_cap)
{
    LrgProfessionDefPrivate *priv;
    const LrgProfessionTier *tier;
    guint i;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), -1);
    priv = lrg_profession_def_get_instance_private (self);
    for (i = 0; i < priv->tiers->len; i++)
    {
        tier = g_ptr_array_index (priv->tiers, i);
        if (tier->skill_cap == skill_cap)
            return (gint)i;
    }
    return -1;
}

gint
lrg_profession_def_get_next_tier (LrgProfessionDef *self,
                                  guint             current_cap)
{
    LrgProfessionDefPrivate *priv;
    const LrgProfessionTier *tier;
    guint i;

    g_return_val_if_fail (LRG_IS_PROFESSION_DEF (self), -1);
    priv = lrg_profession_def_get_instance_private (self);

    /* Caps are strictly increasing, so the first larger cap is the next rank */
    for (i = 0; i < priv->tiers->len; i++)
    {
        tier = g_ptr_array_index (priv->tiers, i);
        if (tier->skill_cap > current_cap)
            return (gint)i;
    }
    return -1;
}
