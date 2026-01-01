/* lrg-card-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-card-def.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER
#include "../lrg-log.h"

/**
 * LrgCardDefPrivate:
 *
 * Private data for #LrgCardDef.
 */
typedef struct
{
    gchar            *id;
    gchar            *name;
    gchar            *description;
    LrgCardType       card_type;
    LrgCardRarity     rarity;
    gint              base_cost;
    LrgCardTargetType target_type;
    LrgCardKeyword    keywords;
    gboolean          upgradeable;
    gchar            *upgraded_def_id;
    gchar            *icon;
    GPtrArray        *effects;
    GPtrArray        *tags;

    /* Scoring properties (Balatro-style) */
    LrgCardSuit       suit;
    LrgCardRank       rank;
    gint              chip_value;
} LrgCardDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCardDef, lrg_card_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_CARD_TYPE,
    PROP_RARITY,
    PROP_BASE_COST,
    PROP_TARGET_TYPE,
    PROP_KEYWORDS,
    PROP_UPGRADEABLE,
    PROP_UPGRADED_DEF_ID,
    PROP_ICON,
    PROP_SUIT,
    PROP_RANK,
    PROP_CHIP_VALUE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default virtual method implementations */

static gboolean
lrg_card_def_real_on_play (LrgCardDef       *self,
                           LrgCombatContext *ctx,
                           LrgCombatant     *target)
{
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);
    guint i;

    /*
     * Default on_play: Execute all effects in order.
     * In a full implementation, effects would be pushed to the effect stack
     * and resolved. For now, we just iterate and log.
     */
    for (i = 0; i < priv->effects->len; i++)
    {
        LrgCardEffect *effect = g_ptr_array_index (priv->effects, i);

        /*
         * TODO: Push effect to effect stack and resolve.
         * For now, effects are placeholder - actual implementation
         * will call lrg_effect_stack_push() and lrg_effect_stack_resolve_all().
         */
        (void)effect;
    }

    lrg_log_debug ("Card '%s' played with %u effects",
                   priv->id, priv->effects->len);

    return TRUE;
}

static gboolean
lrg_card_def_real_on_discard (LrgCardDef       *self,
                              LrgCombatContext *ctx)
{
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);

    lrg_log_debug ("Card '%s' discarded", priv->id);

    return TRUE;
}

static gboolean
lrg_card_def_real_on_exhaust (LrgCardDef       *self,
                              LrgCombatContext *ctx)
{
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);

    lrg_log_debug ("Card '%s' exhausted", priv->id);

    return TRUE;
}

static gboolean
lrg_card_def_real_on_draw (LrgCardDef       *self,
                           LrgCombatContext *ctx)
{
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);

    lrg_log_debug ("Card '%s' drawn", priv->id);

    return TRUE;
}

static gboolean
lrg_card_def_real_can_play (LrgCardDef       *self,
                            LrgCombatContext *ctx)
{
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);

    /*
     * Default can_play checks:
     * 1. Card is not unplayable
     * 2. Player has enough energy (requires context - skip if NULL)
     * 3. Target requirements are met (requires context)
     */

    /* Check unplayable keyword */
    if (priv->keywords & LRG_CARD_KEYWORD_UNPLAYABLE)
    {
        lrg_log_debug ("Card '%s' is unplayable", priv->id);
        return FALSE;
    }

    /*
     * Energy and target checks require combat context.
     * If context is NULL, assume card can be played (for preview/UI purposes).
     */
    if (ctx == NULL)
        return TRUE;

    /*
     * TODO: Add energy check when LrgCombatContext is implemented:
     * gint cost = lrg_card_def_calculate_cost (self, ctx);
     * if (lrg_combat_context_get_energy (ctx) < cost)
     *     return FALSE;
     *
     * TODO: Add target check:
     * if (priv->target_type == LRG_CARD_TARGET_SINGLE_ENEMY)
     *     if (lrg_combat_context_get_enemy_count (ctx) == 0)
     *         return FALSE;
     */

    return TRUE;
}

static gint
lrg_card_def_real_calculate_cost (LrgCardDef       *self,
                                  LrgCombatContext *ctx)
{
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);
    gint cost;

    /*
     * Default calculate_cost:
     * 1. X-cost cards use all remaining energy
     * 2. Otherwise, use base cost (modified by context if available)
     */

    /* X-cost handling */
    if (priv->keywords & LRG_CARD_KEYWORD_X_COST)
    {
        if (ctx != NULL)
        {
            /*
             * TODO: Return remaining energy when context implemented:
             * return lrg_combat_context_get_energy (ctx);
             */
        }
        return 0;
    }

    cost = priv->base_cost;

    /*
     * Apply cost modifiers from combat context.
     * TODO: When context implemented:
     * if (ctx != NULL)
     *     cost = lrg_combat_context_modify_card_cost (ctx, self, cost);
     */

    return MAX (0, cost);
}

static gchar *
lrg_card_def_real_get_tooltip (LrgCardDef       *self,
                               LrgCombatContext *ctx)
{
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);

    /*
     * Default tooltip: Return description with variable substitution.
     * TODO: Implement variable substitution (e.g., {damage} -> "6")
     * when effect system is complete.
     */

    if (priv->description == NULL)
        return NULL;

    return g_strdup (priv->description);
}

static void
lrg_card_def_finalize (GObject *object)
{
    LrgCardDef *self = LRG_CARD_DEF (object);
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->upgraded_def_id, g_free);
    g_clear_pointer (&priv->icon, g_free);
    g_clear_pointer (&priv->effects, g_ptr_array_unref);
    g_clear_pointer (&priv->tags, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_card_def_parent_class)->finalize (object);
}

static void
lrg_card_def_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgCardDef *self = LRG_CARD_DEF (object);
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);

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
    case PROP_CARD_TYPE:
        g_value_set_enum (value, priv->card_type);
        break;
    case PROP_RARITY:
        g_value_set_enum (value, priv->rarity);
        break;
    case PROP_BASE_COST:
        g_value_set_int (value, priv->base_cost);
        break;
    case PROP_TARGET_TYPE:
        g_value_set_enum (value, priv->target_type);
        break;
    case PROP_KEYWORDS:
        g_value_set_flags (value, priv->keywords);
        break;
    case PROP_UPGRADEABLE:
        g_value_set_boolean (value, priv->upgradeable);
        break;
    case PROP_UPGRADED_DEF_ID:
        g_value_set_string (value, priv->upgraded_def_id);
        break;
    case PROP_ICON:
        g_value_set_string (value, priv->icon);
        break;
    case PROP_SUIT:
        g_value_set_enum (value, priv->suit);
        break;
    case PROP_RANK:
        g_value_set_enum (value, priv->rank);
        break;
    case PROP_CHIP_VALUE:
        g_value_set_int (value, priv->chip_value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_card_def_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgCardDef *self = LRG_CARD_DEF (object);
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);

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
    case PROP_CARD_TYPE:
        priv->card_type = g_value_get_enum (value);
        break;
    case PROP_RARITY:
        priv->rarity = g_value_get_enum (value);
        break;
    case PROP_BASE_COST:
        priv->base_cost = g_value_get_int (value);
        break;
    case PROP_TARGET_TYPE:
        priv->target_type = g_value_get_enum (value);
        break;
    case PROP_KEYWORDS:
        priv->keywords = g_value_get_flags (value);
        break;
    case PROP_UPGRADEABLE:
        priv->upgradeable = g_value_get_boolean (value);
        break;
    case PROP_UPGRADED_DEF_ID:
        g_clear_pointer (&priv->upgraded_def_id, g_free);
        priv->upgraded_def_id = g_value_dup_string (value);
        break;
    case PROP_ICON:
        g_clear_pointer (&priv->icon, g_free);
        priv->icon = g_value_dup_string (value);
        break;
    case PROP_SUIT:
        priv->suit = g_value_get_enum (value);
        break;
    case PROP_RANK:
        priv->rank = g_value_get_enum (value);
        break;
    case PROP_CHIP_VALUE:
        priv->chip_value = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_card_def_class_init (LrgCardDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_card_def_finalize;
    object_class->get_property = lrg_card_def_get_property;
    object_class->set_property = lrg_card_def_set_property;

    /* Virtual method defaults */
    klass->on_play = lrg_card_def_real_on_play;
    klass->on_discard = lrg_card_def_real_on_discard;
    klass->on_exhaust = lrg_card_def_real_on_exhaust;
    klass->on_draw = lrg_card_def_real_on_draw;
    klass->can_play = lrg_card_def_real_can_play;
    klass->calculate_cost = lrg_card_def_real_calculate_cost;
    klass->get_tooltip = lrg_card_def_real_get_tooltip;

    /**
     * LrgCardDef:id:
     *
     * Unique identifier for this card type.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier for this card type",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:name:
     *
     * Display name for the card.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name for the card",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:description:
     *
     * Card description with variable placeholders.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Card description with variable placeholders",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:card-type:
     *
     * The type of card (attack, skill, power, etc.).
     *
     * Since: 1.0
     */
    properties[PROP_CARD_TYPE] =
        g_param_spec_enum ("card-type",
                           "Card Type",
                           "The type of card",
                           LRG_TYPE_CARD_TYPE,
                           LRG_CARD_TYPE_ATTACK,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:rarity:
     *
     * The rarity of the card.
     *
     * Since: 1.0
     */
    properties[PROP_RARITY] =
        g_param_spec_enum ("rarity",
                           "Rarity",
                           "The rarity of the card",
                           LRG_TYPE_CARD_RARITY,
                           LRG_CARD_RARITY_COMMON,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:base-cost:
     *
     * Base energy cost to play this card.
     *
     * Since: 1.0
     */
    properties[PROP_BASE_COST] =
        g_param_spec_int ("base-cost",
                          "Base Cost",
                          "Base energy cost to play this card",
                          -1, G_MAXINT, 1,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:target-type:
     *
     * The targeting mode for this card.
     *
     * Since: 1.0
     */
    properties[PROP_TARGET_TYPE] =
        g_param_spec_enum ("target-type",
                           "Target Type",
                           "The targeting mode for this card",
                           LRG_TYPE_CARD_TARGET_TYPE,
                           LRG_CARD_TARGET_NONE,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:keywords:
     *
     * Keyword flags for this card.
     *
     * Since: 1.0
     */
    properties[PROP_KEYWORDS] =
        g_param_spec_flags ("keywords",
                            "Keywords",
                            "Keyword flags for this card",
                            LRG_TYPE_CARD_KEYWORD,
                            LRG_CARD_KEYWORD_NONE,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:upgradeable:
     *
     * Whether this card can be upgraded.
     *
     * Since: 1.0
     */
    properties[PROP_UPGRADEABLE] =
        g_param_spec_boolean ("upgradeable",
                              "Upgradeable",
                              "Whether this card can be upgraded",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:upgraded-def-id:
     *
     * ID of the upgraded version of this card.
     *
     * Since: 1.0
     */
    properties[PROP_UPGRADED_DEF_ID] =
        g_param_spec_string ("upgraded-def-id",
                             "Upgraded Def ID",
                             "ID of the upgraded version of this card",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:icon:
     *
     * Path to the card icon.
     *
     * Since: 1.0
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon",
                             "Icon",
                             "Path to the card icon",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:suit:
     *
     * Playing card suit for scoring deckbuilders.
     *
     * Since: 1.0
     */
    properties[PROP_SUIT] =
        g_param_spec_enum ("suit",
                           "Suit",
                           "Playing card suit for scoring",
                           LRG_TYPE_CARD_SUIT,
                           LRG_CARD_SUIT_NONE,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:rank:
     *
     * Playing card rank for scoring deckbuilders.
     *
     * Since: 1.0
     */
    properties[PROP_RANK] =
        g_param_spec_enum ("rank",
                           "Rank",
                           "Playing card rank for scoring",
                           LRG_TYPE_CARD_RANK,
                           LRG_CARD_RANK_NONE,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCardDef:chip-value:
     *
     * Chip value for scoring deckbuilders.
     *
     * Since: 1.0
     */
    properties[PROP_CHIP_VALUE] =
        g_param_spec_int ("chip-value",
                          "Chip Value",
                          "Chip value for scoring",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_card_def_init (LrgCardDef *self)
{
    LrgCardDefPrivate *priv = lrg_card_def_get_instance_private (self);

    priv->effects = g_ptr_array_new_with_free_func (g_object_unref);
    priv->tags = g_ptr_array_new_with_free_func (g_free);
    priv->card_type = LRG_CARD_TYPE_ATTACK;
    priv->rarity = LRG_CARD_RARITY_COMMON;
    priv->base_cost = 0;
    priv->target_type = LRG_CARD_TARGET_NONE;
    priv->keywords = LRG_CARD_KEYWORD_NONE;
    priv->upgradeable = FALSE;
}

/* Public API */

LrgCardDef *
lrg_card_def_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_CARD_DEF,
                         "id", id,
                         NULL);
}

const gchar *
lrg_card_def_get_id (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), NULL);

    priv = lrg_card_def_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_card_def_get_name (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), NULL);

    priv = lrg_card_def_get_instance_private (self);
    return priv->name;
}

void
lrg_card_def_set_name (LrgCardDef  *self,
                       const gchar *name)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "name", name, NULL);
}

const gchar *
lrg_card_def_get_description (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), NULL);

    priv = lrg_card_def_get_instance_private (self);
    return priv->description;
}

void
lrg_card_def_set_description (LrgCardDef  *self,
                              const gchar *description)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "description", description, NULL);
}

LrgCardType
lrg_card_def_get_card_type (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), LRG_CARD_TYPE_ATTACK);

    priv = lrg_card_def_get_instance_private (self);
    return priv->card_type;
}

void
lrg_card_def_set_card_type (LrgCardDef  *self,
                            LrgCardType  card_type)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "card-type", card_type, NULL);
}

LrgCardRarity
lrg_card_def_get_rarity (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), LRG_CARD_RARITY_COMMON);

    priv = lrg_card_def_get_instance_private (self);
    return priv->rarity;
}

void
lrg_card_def_set_rarity (LrgCardDef    *self,
                         LrgCardRarity  rarity)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "rarity", rarity, NULL);
}

gint
lrg_card_def_get_base_cost (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), 0);

    priv = lrg_card_def_get_instance_private (self);
    return priv->base_cost;
}

void
lrg_card_def_set_base_cost (LrgCardDef *self,
                            gint        cost)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "base-cost", cost, NULL);
}

LrgCardTargetType
lrg_card_def_get_target_type (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), LRG_CARD_TARGET_NONE);

    priv = lrg_card_def_get_instance_private (self);
    return priv->target_type;
}

void
lrg_card_def_set_target_type (LrgCardDef        *self,
                              LrgCardTargetType  target_type)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "target-type", target_type, NULL);
}

LrgCardKeyword
lrg_card_def_get_keywords (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), LRG_CARD_KEYWORD_NONE);

    priv = lrg_card_def_get_instance_private (self);
    return priv->keywords;
}

void
lrg_card_def_set_keywords (LrgCardDef     *self,
                           LrgCardKeyword  keywords)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "keywords", keywords, NULL);
}

gboolean
lrg_card_def_has_keyword (LrgCardDef     *self,
                          LrgCardKeyword  keyword)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), FALSE);

    priv = lrg_card_def_get_instance_private (self);
    return (priv->keywords & keyword) != 0;
}

void
lrg_card_def_add_keyword (LrgCardDef     *self,
                          LrgCardKeyword  keyword)
{
    LrgCardDefPrivate *priv;

    g_return_if_fail (LRG_IS_CARD_DEF (self));

    priv = lrg_card_def_get_instance_private (self);
    priv->keywords |= keyword;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KEYWORDS]);
}

void
lrg_card_def_remove_keyword (LrgCardDef     *self,
                             LrgCardKeyword  keyword)
{
    LrgCardDefPrivate *priv;

    g_return_if_fail (LRG_IS_CARD_DEF (self));

    priv = lrg_card_def_get_instance_private (self);
    priv->keywords &= ~keyword;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KEYWORDS]);
}

gboolean
lrg_card_def_get_upgradeable (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), FALSE);

    priv = lrg_card_def_get_instance_private (self);
    return priv->upgradeable;
}

void
lrg_card_def_set_upgradeable (LrgCardDef *self,
                              gboolean    upgradeable)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "upgradeable", upgradeable, NULL);
}

const gchar *
lrg_card_def_get_upgraded_def_id (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), NULL);

    priv = lrg_card_def_get_instance_private (self);
    return priv->upgraded_def_id;
}

void
lrg_card_def_set_upgraded_def_id (LrgCardDef  *self,
                                  const gchar *upgraded_id)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "upgraded-def-id", upgraded_id, NULL);
}

const gchar *
lrg_card_def_get_icon (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), NULL);

    priv = lrg_card_def_get_instance_private (self);
    return priv->icon;
}

void
lrg_card_def_set_icon (LrgCardDef  *self,
                       const gchar *icon)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "icon", icon, NULL);
}

void
lrg_card_def_add_effect (LrgCardDef    *self,
                         LrgCardEffect *effect)
{
    LrgCardDefPrivate *priv;

    g_return_if_fail (LRG_IS_CARD_DEF (self));
    g_return_if_fail (effect != NULL);

    priv = lrg_card_def_get_instance_private (self);
    g_ptr_array_add (priv->effects, effect);
}

GPtrArray *
lrg_card_def_get_effects (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), NULL);

    priv = lrg_card_def_get_instance_private (self);
    return priv->effects;
}

void
lrg_card_def_clear_effects (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_if_fail (LRG_IS_CARD_DEF (self));

    priv = lrg_card_def_get_instance_private (self);
    g_ptr_array_set_size (priv->effects, 0);
}

void
lrg_card_def_add_tag (LrgCardDef  *self,
                      const gchar *tag)
{
    LrgCardDefPrivate *priv;

    g_return_if_fail (LRG_IS_CARD_DEF (self));
    g_return_if_fail (tag != NULL);

    priv = lrg_card_def_get_instance_private (self);
    g_ptr_array_add (priv->tags, g_strdup (tag));
}

gboolean
lrg_card_def_has_tag (LrgCardDef  *self,
                      const gchar *tag)
{
    LrgCardDefPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), FALSE);
    g_return_val_if_fail (tag != NULL, FALSE);

    priv = lrg_card_def_get_instance_private (self);

    for (i = 0; i < priv->tags->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (priv->tags, i), tag) == 0)
            return TRUE;
    }

    return FALSE;
}

GPtrArray *
lrg_card_def_get_tags (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), NULL);

    priv = lrg_card_def_get_instance_private (self);
    return priv->tags;
}

LrgCardSuit
lrg_card_def_get_suit (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), LRG_CARD_SUIT_NONE);

    priv = lrg_card_def_get_instance_private (self);
    return priv->suit;
}

void
lrg_card_def_set_suit (LrgCardDef  *self,
                       LrgCardSuit  suit)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "suit", suit, NULL);
}

LrgCardRank
lrg_card_def_get_rank (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), LRG_CARD_RANK_NONE);

    priv = lrg_card_def_get_instance_private (self);
    return priv->rank;
}

void
lrg_card_def_set_rank (LrgCardDef  *self,
                       LrgCardRank  rank)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "rank", rank, NULL);
}

gint
lrg_card_def_get_chip_value (LrgCardDef *self)
{
    LrgCardDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), 0);

    priv = lrg_card_def_get_instance_private (self);
    return priv->chip_value;
}

void
lrg_card_def_set_chip_value (LrgCardDef *self,
                             gint        chips)
{
    g_return_if_fail (LRG_IS_CARD_DEF (self));

    g_object_set (self, "chip-value", chips, NULL);
}

/* Virtual method wrappers */

gboolean
lrg_card_def_on_play (LrgCardDef       *self,
                      LrgCombatContext *ctx,
                      LrgCombatant     *target)
{
    LrgCardDefClass *klass;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), FALSE);

    klass = LRG_CARD_DEF_GET_CLASS (self);
    if (klass->on_play != NULL)
        return klass->on_play (self, ctx, target);

    return TRUE;
}

gboolean
lrg_card_def_on_discard (LrgCardDef       *self,
                         LrgCombatContext *ctx)
{
    LrgCardDefClass *klass;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), FALSE);

    klass = LRG_CARD_DEF_GET_CLASS (self);
    if (klass->on_discard != NULL)
        return klass->on_discard (self, ctx);

    return TRUE;
}

gboolean
lrg_card_def_on_exhaust (LrgCardDef       *self,
                         LrgCombatContext *ctx)
{
    LrgCardDefClass *klass;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), FALSE);

    klass = LRG_CARD_DEF_GET_CLASS (self);
    if (klass->on_exhaust != NULL)
        return klass->on_exhaust (self, ctx);

    return TRUE;
}

gboolean
lrg_card_def_on_draw (LrgCardDef       *self,
                      LrgCombatContext *ctx)
{
    LrgCardDefClass *klass;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), FALSE);

    klass = LRG_CARD_DEF_GET_CLASS (self);
    if (klass->on_draw != NULL)
        return klass->on_draw (self, ctx);

    return TRUE;
}

gboolean
lrg_card_def_can_play (LrgCardDef       *self,
                       LrgCombatContext *ctx)
{
    LrgCardDefClass *klass;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), FALSE);

    klass = LRG_CARD_DEF_GET_CLASS (self);
    if (klass->can_play != NULL)
        return klass->can_play (self, ctx);

    return TRUE;
}

gint
lrg_card_def_calculate_cost (LrgCardDef       *self,
                             LrgCombatContext *ctx)
{
    LrgCardDefClass *klass;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), 0);

    klass = LRG_CARD_DEF_GET_CLASS (self);
    if (klass->calculate_cost != NULL)
        return klass->calculate_cost (self, ctx);

    return 0;
}

gchar *
lrg_card_def_get_tooltip (LrgCardDef       *self,
                          LrgCombatContext *ctx)
{
    LrgCardDefClass *klass;

    g_return_val_if_fail (LRG_IS_CARD_DEF (self), NULL);

    klass = LRG_CARD_DEF_GET_CLASS (self);
    if (klass->get_tooltip != NULL)
        return klass->get_tooltip (self, ctx);

    return NULL;
}
