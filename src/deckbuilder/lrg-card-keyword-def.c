/* lrg-card-keyword-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardKeywordDef - Custom keyword definition implementation.
 */

#include "lrg-card-keyword-def.h"

typedef struct
{
    gchar    *id;
    gchar    *name;
    gchar    *description;
    gchar    *icon;
    gboolean  positive;
    gboolean  negative;
} LrgCardKeywordDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCardKeywordDef, lrg_card_keyword_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_POSITIVE,
    PROP_NEGATIVE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_card_keyword_def_default_on_card_played (LrgCardKeywordDef *self,
                                             gpointer           card,
                                             gpointer           context)
{
    /* Default: do nothing */
}

static void
lrg_card_keyword_def_default_on_card_drawn (LrgCardKeywordDef *self,
                                            gpointer           card,
                                            gpointer           context)
{
    /* Default: do nothing */
}

static void
lrg_card_keyword_def_default_on_card_discarded (LrgCardKeywordDef *self,
                                                gpointer           card,
                                                gpointer           context)
{
    /* Default: do nothing */
}

static void
lrg_card_keyword_def_default_on_turn_start (LrgCardKeywordDef *self,
                                            gpointer           card,
                                            gpointer           context)
{
    /* Default: do nothing */
}

static void
lrg_card_keyword_def_default_on_turn_end (LrgCardKeywordDef *self,
                                          gpointer           card,
                                          gpointer           context)
{
    /* Default: do nothing */
}

static gint
lrg_card_keyword_def_default_modify_cost (LrgCardKeywordDef *self,
                                          gpointer           card,
                                          gpointer           context,
                                          gint               base_cost)
{
    /* Default: no modification */
    return base_cost;
}

static gboolean
lrg_card_keyword_def_default_can_play (LrgCardKeywordDef *self,
                                       gpointer           card,
                                       gpointer           context)
{
    /* Default: can play */
    return TRUE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_card_keyword_def_finalize (GObject *object)
{
    LrgCardKeywordDef *self = LRG_CARD_KEYWORD_DEF (object);
    LrgCardKeywordDefPrivate *priv = lrg_card_keyword_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->icon, g_free);

    G_OBJECT_CLASS (lrg_card_keyword_def_parent_class)->finalize (object);
}

static void
lrg_card_keyword_def_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgCardKeywordDef *self = LRG_CARD_KEYWORD_DEF (object);
    LrgCardKeywordDefPrivate *priv = lrg_card_keyword_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        g_free (priv->description);
        priv->description = g_value_dup_string (value);
        break;
    case PROP_ICON:
        g_free (priv->icon);
        priv->icon = g_value_dup_string (value);
        break;
    case PROP_POSITIVE:
        priv->positive = g_value_get_boolean (value);
        break;
    case PROP_NEGATIVE:
        priv->negative = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_card_keyword_def_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgCardKeywordDef *self = LRG_CARD_KEYWORD_DEF (object);
    LrgCardKeywordDefPrivate *priv = lrg_card_keyword_def_get_instance_private (self);

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
    case PROP_POSITIVE:
        g_value_set_boolean (value, priv->positive);
        break;
    case PROP_NEGATIVE:
        g_value_set_boolean (value, priv->negative);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_card_keyword_def_class_init (LrgCardKeywordDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_card_keyword_def_finalize;
    object_class->set_property = lrg_card_keyword_def_set_property;
    object_class->get_property = lrg_card_keyword_def_get_property;

    /* Default virtual methods */
    klass->on_card_played = lrg_card_keyword_def_default_on_card_played;
    klass->on_card_drawn = lrg_card_keyword_def_default_on_card_drawn;
    klass->on_card_discarded = lrg_card_keyword_def_default_on_card_discarded;
    klass->on_turn_start = lrg_card_keyword_def_default_on_turn_start;
    klass->on_turn_end = lrg_card_keyword_def_default_on_turn_end;
    klass->modify_cost = lrg_card_keyword_def_default_modify_cost;
    klass->can_play = lrg_card_keyword_def_default_can_play;

    /**
     * LrgCardKeywordDef:id:
     *
     * The unique keyword identifier.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique keyword identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardKeywordDef:name:
     *
     * The display name.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardKeywordDef:description:
     *
     * The keyword description.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Keyword description",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardKeywordDef:icon:
     *
     * The icon identifier.
     *
     * Since: 1.0
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon",
                             "Icon",
                             "Icon identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardKeywordDef:positive:
     *
     * Whether the keyword is beneficial.
     *
     * Since: 1.0
     */
    properties[PROP_POSITIVE] =
        g_param_spec_boolean ("positive",
                              "Positive",
                              "Whether the keyword is beneficial",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardKeywordDef:negative:
     *
     * Whether the keyword is detrimental.
     *
     * Since: 1.0
     */
    properties[PROP_NEGATIVE] =
        g_param_spec_boolean ("negative",
                              "Negative",
                              "Whether the keyword is detrimental",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_card_keyword_def_init (LrgCardKeywordDef *self)
{
    LrgCardKeywordDefPrivate *priv = lrg_card_keyword_def_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->description = NULL;
    priv->icon = NULL;
    priv->positive = FALSE;
    priv->negative = FALSE;
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_card_keyword_def_new:
 * @id: unique keyword identifier
 * @name: display name
 * @description: keyword description
 *
 * Creates a new custom keyword definition.
 *
 * Returns: (transfer full): a new #LrgCardKeywordDef
 *
 * Since: 1.0
 */
LrgCardKeywordDef *
lrg_card_keyword_def_new (const gchar *id,
                          const gchar *name,
                          const gchar *description)
{
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_CARD_KEYWORD_DEF,
                         "id", id,
                         "name", name,
                         "description", description,
                         NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_card_keyword_def_get_id:
 * @self: a #LrgCardKeywordDef
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): the keyword ID
 *
 * Since: 1.0
 */
const gchar *
lrg_card_keyword_def_get_id (LrgCardKeywordDef *self)
{
    LrgCardKeywordDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_DEF (self), NULL);

    priv = lrg_card_keyword_def_get_instance_private (self);
    return priv->id;
}

/**
 * lrg_card_keyword_def_get_name:
 * @self: a #LrgCardKeywordDef
 *
 * Gets the display name.
 *
 * Returns: (transfer none): the keyword name
 *
 * Since: 1.0
 */
const gchar *
lrg_card_keyword_def_get_name (LrgCardKeywordDef *self)
{
    LrgCardKeywordDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_DEF (self), NULL);

    priv = lrg_card_keyword_def_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_card_keyword_def_get_description:
 * @self: a #LrgCardKeywordDef
 *
 * Gets the description.
 *
 * Returns: (transfer none): the description
 *
 * Since: 1.0
 */
const gchar *
lrg_card_keyword_def_get_description (LrgCardKeywordDef *self)
{
    LrgCardKeywordDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_DEF (self), NULL);

    priv = lrg_card_keyword_def_get_instance_private (self);
    return priv->description;
}

/**
 * lrg_card_keyword_def_get_icon:
 * @self: a #LrgCardKeywordDef
 *
 * Gets the icon identifier.
 *
 * Returns: (transfer none) (nullable): the icon ID
 *
 * Since: 1.0
 */
const gchar *
lrg_card_keyword_def_get_icon (LrgCardKeywordDef *self)
{
    LrgCardKeywordDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_DEF (self), NULL);

    priv = lrg_card_keyword_def_get_instance_private (self);
    return priv->icon;
}

/**
 * lrg_card_keyword_def_set_icon:
 * @self: a #LrgCardKeywordDef
 * @icon: (nullable): the icon identifier
 *
 * Sets the icon identifier.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_def_set_icon (LrgCardKeywordDef *self,
                               const gchar       *icon)
{
    LrgCardKeywordDefPrivate *priv;

    g_return_if_fail (LRG_IS_CARD_KEYWORD_DEF (self));

    priv = lrg_card_keyword_def_get_instance_private (self);

    if (g_strcmp0 (priv->icon, icon) != 0)
    {
        g_free (priv->icon);
        priv->icon = g_strdup (icon);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
    }
}

/**
 * lrg_card_keyword_def_is_positive:
 * @self: a #LrgCardKeywordDef
 *
 * Checks if the keyword is beneficial.
 *
 * Returns: %TRUE if positive
 *
 * Since: 1.0
 */
gboolean
lrg_card_keyword_def_is_positive (LrgCardKeywordDef *self)
{
    LrgCardKeywordDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_DEF (self), FALSE);

    priv = lrg_card_keyword_def_get_instance_private (self);
    return priv->positive;
}

/**
 * lrg_card_keyword_def_set_positive:
 * @self: a #LrgCardKeywordDef
 * @positive: whether the keyword is positive
 *
 * Sets whether the keyword is beneficial.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_def_set_positive (LrgCardKeywordDef *self,
                                   gboolean           positive)
{
    LrgCardKeywordDefPrivate *priv;

    g_return_if_fail (LRG_IS_CARD_KEYWORD_DEF (self));

    priv = lrg_card_keyword_def_get_instance_private (self);

    if (priv->positive != positive)
    {
        priv->positive = positive;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITIVE]);
    }
}

/**
 * lrg_card_keyword_def_is_negative:
 * @self: a #LrgCardKeywordDef
 *
 * Checks if the keyword is detrimental.
 *
 * Returns: %TRUE if negative
 *
 * Since: 1.0
 */
gboolean
lrg_card_keyword_def_is_negative (LrgCardKeywordDef *self)
{
    LrgCardKeywordDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_DEF (self), FALSE);

    priv = lrg_card_keyword_def_get_instance_private (self);
    return priv->negative;
}

/**
 * lrg_card_keyword_def_set_negative:
 * @self: a #LrgCardKeywordDef
 * @negative: whether the keyword is negative
 *
 * Sets whether the keyword is detrimental.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_def_set_negative (LrgCardKeywordDef *self,
                                   gboolean           negative)
{
    LrgCardKeywordDefPrivate *priv;

    g_return_if_fail (LRG_IS_CARD_KEYWORD_DEF (self));

    priv = lrg_card_keyword_def_get_instance_private (self);

    if (priv->negative != negative)
    {
        priv->negative = negative;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NEGATIVE]);
    }
}

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_card_keyword_def_on_card_played:
 * @self: a #LrgCardKeywordDef
 * @card: the card being played
 * @context: the combat context
 *
 * Called when a card with this keyword is played.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_def_on_card_played (LrgCardKeywordDef *self,
                                     gpointer           card,
                                     gpointer           context)
{
    LrgCardKeywordDefClass *klass;

    g_return_if_fail (LRG_IS_CARD_KEYWORD_DEF (self));

    klass = LRG_CARD_KEYWORD_DEF_GET_CLASS (self);
    if (klass->on_card_played != NULL)
        klass->on_card_played (self, card, context);
}

/**
 * lrg_card_keyword_def_on_card_drawn:
 * @self: a #LrgCardKeywordDef
 * @card: the card drawn
 * @context: the combat context
 *
 * Called when a card with this keyword is drawn.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_def_on_card_drawn (LrgCardKeywordDef *self,
                                    gpointer           card,
                                    gpointer           context)
{
    LrgCardKeywordDefClass *klass;

    g_return_if_fail (LRG_IS_CARD_KEYWORD_DEF (self));

    klass = LRG_CARD_KEYWORD_DEF_GET_CLASS (self);
    if (klass->on_card_drawn != NULL)
        klass->on_card_drawn (self, card, context);
}

/**
 * lrg_card_keyword_def_on_card_discarded:
 * @self: a #LrgCardKeywordDef
 * @card: the card discarded
 * @context: the combat context
 *
 * Called when a card with this keyword is discarded.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_def_on_card_discarded (LrgCardKeywordDef *self,
                                        gpointer           card,
                                        gpointer           context)
{
    LrgCardKeywordDefClass *klass;

    g_return_if_fail (LRG_IS_CARD_KEYWORD_DEF (self));

    klass = LRG_CARD_KEYWORD_DEF_GET_CLASS (self);
    if (klass->on_card_discarded != NULL)
        klass->on_card_discarded (self, card, context);
}

/**
 * lrg_card_keyword_def_on_turn_start:
 * @self: a #LrgCardKeywordDef
 * @card: the card
 * @context: the combat context
 *
 * Called at turn start for cards with this keyword in hand.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_def_on_turn_start (LrgCardKeywordDef *self,
                                    gpointer           card,
                                    gpointer           context)
{
    LrgCardKeywordDefClass *klass;

    g_return_if_fail (LRG_IS_CARD_KEYWORD_DEF (self));

    klass = LRG_CARD_KEYWORD_DEF_GET_CLASS (self);
    if (klass->on_turn_start != NULL)
        klass->on_turn_start (self, card, context);
}

/**
 * lrg_card_keyword_def_on_turn_end:
 * @self: a #LrgCardKeywordDef
 * @card: the card
 * @context: the combat context
 *
 * Called at turn end for cards with this keyword in hand.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_def_on_turn_end (LrgCardKeywordDef *self,
                                  gpointer           card,
                                  gpointer           context)
{
    LrgCardKeywordDefClass *klass;

    g_return_if_fail (LRG_IS_CARD_KEYWORD_DEF (self));

    klass = LRG_CARD_KEYWORD_DEF_GET_CLASS (self);
    if (klass->on_turn_end != NULL)
        klass->on_turn_end (self, card, context);
}

/**
 * lrg_card_keyword_def_modify_cost:
 * @self: a #LrgCardKeywordDef
 * @card: the card
 * @context: the combat context
 * @base_cost: the base cost
 *
 * Modifies the card's energy cost.
 *
 * Returns: the modified cost
 *
 * Since: 1.0
 */
gint
lrg_card_keyword_def_modify_cost (LrgCardKeywordDef *self,
                                  gpointer           card,
                                  gpointer           context,
                                  gint               base_cost)
{
    LrgCardKeywordDefClass *klass;

    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_DEF (self), base_cost);

    klass = LRG_CARD_KEYWORD_DEF_GET_CLASS (self);
    if (klass->modify_cost != NULL)
        return klass->modify_cost (self, card, context, base_cost);

    return base_cost;
}

/**
 * lrg_card_keyword_def_can_play:
 * @self: a #LrgCardKeywordDef
 * @card: the card
 * @context: the combat context
 *
 * Checks if a card with this keyword can be played.
 *
 * Returns: %TRUE if the card can be played
 *
 * Since: 1.0
 */
gboolean
lrg_card_keyword_def_can_play (LrgCardKeywordDef *self,
                               gpointer           card,
                               gpointer           context)
{
    LrgCardKeywordDefClass *klass;

    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_DEF (self), TRUE);

    klass = LRG_CARD_KEYWORD_DEF_GET_CLASS (self);
    if (klass->can_play != NULL)
        return klass->can_play (self, card, context);

    return TRUE;
}
