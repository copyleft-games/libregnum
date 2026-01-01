/* lrg-potion-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPotionDef - Base class for potion definitions implementation.
 */

#include "lrg-potion-def.h"
#include "../lrg-log.h"

typedef struct
{
    gchar          *id;
    gchar          *name;
    gchar          *description;
    gchar          *icon;
    LrgPotionRarity rarity;
    LrgPotionTarget target_type;
    gint            potency;
    gboolean        combat_only;
    gint            price;
} LrgPotionDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgPotionDef, lrg_potion_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_RARITY,
    PROP_TARGET_TYPE,
    PROP_POTENCY,
    PROP_COMBAT_ONLY,
    PROP_PRICE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_potion_def_real_can_use (LrgPotionDef *self,
                              gpointer      context)
{
    /* Default: always usable */
    return TRUE;
}

static void
lrg_potion_def_real_on_use (LrgPotionDef *self,
                             gpointer      context,
                             gpointer      target)
{
    /* Default: do nothing - subclasses should override */
    LrgPotionDefPrivate *priv = lrg_potion_def_get_instance_private (self);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Potion '%s' used (potency: %d)",
               priv->id, priv->potency);
}

static gchar *
lrg_potion_def_real_get_tooltip (LrgPotionDef *self,
                                  gpointer      context)
{
    LrgPotionDefPrivate *priv = lrg_potion_def_get_instance_private (self);

    if (priv->description != NULL)
        return g_strdup (priv->description);

    return g_strdup (priv->name);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_potion_def_finalize (GObject *object)
{
    LrgPotionDef *self = LRG_POTION_DEF (object);
    LrgPotionDefPrivate *priv = lrg_potion_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->icon, g_free);

    G_OBJECT_CLASS (lrg_potion_def_parent_class)->finalize (object);
}

static void
lrg_potion_def_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgPotionDef *self = LRG_POTION_DEF (object);
    LrgPotionDefPrivate *priv = lrg_potion_def_get_instance_private (self);

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
    case PROP_RARITY:
        g_value_set_int (value, priv->rarity);
        break;
    case PROP_TARGET_TYPE:
        g_value_set_int (value, priv->target_type);
        break;
    case PROP_POTENCY:
        g_value_set_int (value, priv->potency);
        break;
    case PROP_COMBAT_ONLY:
        g_value_set_boolean (value, priv->combat_only);
        break;
    case PROP_PRICE:
        g_value_set_int (value, priv->price);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_potion_def_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgPotionDef *self = LRG_POTION_DEF (object);
    LrgPotionDefPrivate *priv = lrg_potion_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_clear_pointer (&priv->id, g_free);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_clear_pointer (&priv->name, g_free);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        g_clear_pointer (&priv->description, g_free);
        priv->description = g_value_dup_string (value);
        break;
    case PROP_ICON:
        g_clear_pointer (&priv->icon, g_free);
        priv->icon = g_value_dup_string (value);
        break;
    case PROP_RARITY:
        priv->rarity = g_value_get_int (value);
        break;
    case PROP_TARGET_TYPE:
        priv->target_type = g_value_get_int (value);
        break;
    case PROP_POTENCY:
        priv->potency = g_value_get_int (value);
        break;
    case PROP_COMBAT_ONLY:
        priv->combat_only = g_value_get_boolean (value);
        break;
    case PROP_PRICE:
        priv->price = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_potion_def_class_init (LrgPotionDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_potion_def_finalize;
    object_class->get_property = lrg_potion_def_get_property;
    object_class->set_property = lrg_potion_def_set_property;

    /* Set default virtual method implementations */
    klass->can_use = lrg_potion_def_real_can_use;
    klass->on_use = lrg_potion_def_real_on_use;
    klass->get_tooltip = lrg_potion_def_real_get_tooltip;

    /**
     * LrgPotionDef:id:
     *
     * The unique identifier for this potion.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPotionDef:name:
     *
     * The display name for this potion.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPotionDef:description:
     *
     * The description for this potion.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Potion description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPotionDef:icon:
     *
     * The icon path for this potion.
     *
     * Since: 1.0
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon",
                             "Icon",
                             "Icon path",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPotionDef:rarity:
     *
     * The rarity tier for this potion.
     *
     * Since: 1.0
     */
    properties[PROP_RARITY] =
        g_param_spec_int ("rarity",
                          "Rarity",
                          "Rarity tier",
                          LRG_POTION_RARITY_COMMON,
                          LRG_POTION_RARITY_RARE,
                          LRG_POTION_RARITY_COMMON,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPotionDef:target-type:
     *
     * The target type for this potion.
     *
     * Since: 1.0
     */
    properties[PROP_TARGET_TYPE] =
        g_param_spec_int ("target-type",
                          "Target Type",
                          "Target type",
                          LRG_POTION_TARGET_NONE,
                          LRG_POTION_TARGET_ALL_ENEMIES,
                          LRG_POTION_TARGET_NONE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPotionDef:potency:
     *
     * The potency (effect magnitude) for this potion.
     *
     * Since: 1.0
     */
    properties[PROP_POTENCY] =
        g_param_spec_int ("potency",
                          "Potency",
                          "Effect magnitude",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPotionDef:combat-only:
     *
     * Whether this potion can only be used in combat.
     *
     * Since: 1.0
     */
    properties[PROP_COMBAT_ONLY] =
        g_param_spec_boolean ("combat-only",
                              "Combat Only",
                              "Only usable in combat",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPotionDef:price:
     *
     * The base shop price for this potion.
     *
     * Since: 1.0
     */
    properties[PROP_PRICE] =
        g_param_spec_int ("price",
                          "Price",
                          "Base shop price",
                          0, G_MAXINT, 50,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_potion_def_init (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv = lrg_potion_def_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->description = NULL;
    priv->icon = NULL;
    priv->rarity = LRG_POTION_RARITY_COMMON;
    priv->target_type = LRG_POTION_TARGET_NONE;
    priv->potency = 0;
    priv->combat_only = TRUE;
    priv->price = 50;
}

/* ==========================================================================
 * Public API - Constructors
 * ========================================================================== */

/**
 * lrg_potion_def_new:
 * @id: unique identifier
 * @name: display name
 *
 * Creates a new potion definition.
 *
 * Returns: (transfer full): a new #LrgPotionDef
 *
 * Since: 1.0
 */
LrgPotionDef *
lrg_potion_def_new (const gchar *id,
                     const gchar *name)
{
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_POTION_DEF,
                         "id", id,
                         "name", name,
                         NULL);
}

/* ==========================================================================
 * Public API - Properties
 * ========================================================================== */

const gchar *
lrg_potion_def_get_id (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), NULL);

    priv = lrg_potion_def_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_potion_def_get_name (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), NULL);

    priv = lrg_potion_def_get_instance_private (self);
    return priv->name;
}

void
lrg_potion_def_set_name (LrgPotionDef *self,
                          const gchar  *name)
{
    LrgPotionDefPrivate *priv;

    g_return_if_fail (LRG_IS_POTION_DEF (self));

    priv = lrg_potion_def_get_instance_private (self);
    g_clear_pointer (&priv->name, g_free);
    priv->name = g_strdup (name);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

const gchar *
lrg_potion_def_get_description (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), NULL);

    priv = lrg_potion_def_get_instance_private (self);
    return priv->description;
}

void
lrg_potion_def_set_description (LrgPotionDef *self,
                                 const gchar  *description)
{
    LrgPotionDefPrivate *priv;

    g_return_if_fail (LRG_IS_POTION_DEF (self));

    priv = lrg_potion_def_get_instance_private (self);
    g_clear_pointer (&priv->description, g_free);
    priv->description = g_strdup (description);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
}

const gchar *
lrg_potion_def_get_icon (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), NULL);

    priv = lrg_potion_def_get_instance_private (self);
    return priv->icon;
}

void
lrg_potion_def_set_icon (LrgPotionDef *self,
                          const gchar  *icon)
{
    LrgPotionDefPrivate *priv;

    g_return_if_fail (LRG_IS_POTION_DEF (self));

    priv = lrg_potion_def_get_instance_private (self);
    g_clear_pointer (&priv->icon, g_free);
    priv->icon = g_strdup (icon);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
}

LrgPotionRarity
lrg_potion_def_get_rarity (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), LRG_POTION_RARITY_COMMON);

    priv = lrg_potion_def_get_instance_private (self);
    return priv->rarity;
}

void
lrg_potion_def_set_rarity (LrgPotionDef   *self,
                            LrgPotionRarity rarity)
{
    LrgPotionDefPrivate *priv;

    g_return_if_fail (LRG_IS_POTION_DEF (self));

    priv = lrg_potion_def_get_instance_private (self);
    priv->rarity = rarity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RARITY]);
}

LrgPotionTarget
lrg_potion_def_get_target_type (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), LRG_POTION_TARGET_NONE);

    priv = lrg_potion_def_get_instance_private (self);
    return priv->target_type;
}

void
lrg_potion_def_set_target_type (LrgPotionDef   *self,
                                 LrgPotionTarget target_type)
{
    LrgPotionDefPrivate *priv;

    g_return_if_fail (LRG_IS_POTION_DEF (self));

    priv = lrg_potion_def_get_instance_private (self);
    priv->target_type = target_type;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_TYPE]);
}

gint
lrg_potion_def_get_potency (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), 0);

    priv = lrg_potion_def_get_instance_private (self);
    return priv->potency;
}

void
lrg_potion_def_set_potency (LrgPotionDef *self,
                             gint          potency)
{
    LrgPotionDefPrivate *priv;

    g_return_if_fail (LRG_IS_POTION_DEF (self));

    priv = lrg_potion_def_get_instance_private (self);
    priv->potency = potency;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POTENCY]);
}

gboolean
lrg_potion_def_get_combat_only (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), TRUE);

    priv = lrg_potion_def_get_instance_private (self);
    return priv->combat_only;
}

void
lrg_potion_def_set_combat_only (LrgPotionDef *self,
                                 gboolean      combat_only)
{
    LrgPotionDefPrivate *priv;

    g_return_if_fail (LRG_IS_POTION_DEF (self));

    priv = lrg_potion_def_get_instance_private (self);
    priv->combat_only = combat_only;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMBAT_ONLY]);
}

gint
lrg_potion_def_get_price (LrgPotionDef *self)
{
    LrgPotionDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), 0);

    priv = lrg_potion_def_get_instance_private (self);
    return priv->price;
}

void
lrg_potion_def_set_price (LrgPotionDef *self,
                           gint          price)
{
    LrgPotionDefPrivate *priv;

    g_return_if_fail (LRG_IS_POTION_DEF (self));
    g_return_if_fail (price >= 0);

    priv = lrg_potion_def_get_instance_private (self);
    priv->price = price;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRICE]);
}

/* ==========================================================================
 * Public API - Virtual Method Wrappers
 * ========================================================================== */

gboolean
lrg_potion_def_can_use (LrgPotionDef *self,
                         gpointer      context)
{
    LrgPotionDefClass *klass;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), FALSE);

    klass = LRG_POTION_DEF_GET_CLASS (self);
    if (klass->can_use != NULL)
        return klass->can_use (self, context);

    return TRUE;
}

void
lrg_potion_def_on_use (LrgPotionDef *self,
                        gpointer      context,
                        gpointer      target)
{
    LrgPotionDefClass *klass;

    g_return_if_fail (LRG_IS_POTION_DEF (self));

    klass = LRG_POTION_DEF_GET_CLASS (self);
    if (klass->on_use != NULL)
        klass->on_use (self, context, target);
}

gchar *
lrg_potion_def_get_tooltip (LrgPotionDef *self,
                             gpointer      context)
{
    LrgPotionDefClass *klass;

    g_return_val_if_fail (LRG_IS_POTION_DEF (self), NULL);

    klass = LRG_POTION_DEF_GET_CLASS (self);
    if (klass->get_tooltip != NULL)
        return klass->get_tooltip (self, context);

    return g_strdup ("");
}
