/* lrg-joker-def.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-joker-def.h"
#include "lrg-joker-instance.h"
#include "lrg-scoring-context.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/**
 * LrgJokerDef:
 *
 * Definition for a joker in a Balatro-style scoring system.
 *
 * Jokers are persistent effects that modify scoring. They can:
 * - Add chips (+Chips)
 * - Add mult (+Mult)
 * - Multiply mult (X Mult)
 * - Have conditional triggers (specific hands, suits, etc.)
 *
 * Subclass to create complex jokers with custom logic.
 *
 * Since: 1.0
 */

typedef struct
{
    /* Identification */
    gchar          *id;
    gchar          *name;
    gchar          *description;

    /* Rarity and cost */
    LrgJokerRarity  rarity;
    gint            cost;
    gint            sell_value;

    /* Simple effect values */
    gint64          plus_chips;
    gint64          plus_mult;
    gdouble         x_mult;

    /* Conditions */
    LrgHandType     required_hand;
    LrgCardSuit     required_suit;
} LrgJokerDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgJokerDef, lrg_joker_def, G_TYPE_OBJECT)

static void
lrg_joker_def_finalize (GObject *object)
{
    LrgJokerDef *self = LRG_JOKER_DEF (object);
    LrgJokerDefPrivate *priv = lrg_joker_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);

    G_OBJECT_CLASS (lrg_joker_def_parent_class)->finalize (object);
}

static gboolean
lrg_joker_def_real_can_trigger (LrgJokerDef       *self,
                                LrgScoringContext *ctx,
                                LrgJokerInstance  *instance)
{
    LrgJokerDefPrivate *priv = lrg_joker_def_get_instance_private (self);
    LrgHandType hand_type;

    /* Check required hand type */
    if (priv->required_hand != LRG_HAND_TYPE_NONE)
    {
        hand_type = lrg_scoring_context_get_hand_type (ctx);
        if (hand_type != priv->required_hand)
            return FALSE;
    }

    /* Check required suit on scoring cards.
     * If the joker requires a specific suit, at least one scoring card
     * must match that suit for the joker to trigger.
     */
    if (priv->required_suit != LRG_CARD_SUIT_NONE)
    {
        GPtrArray *scoring_cards;
        gboolean suit_found = FALSE;
        guint i;

        scoring_cards = lrg_scoring_context_get_scoring_cards (ctx);
        if (scoring_cards != NULL)
        {
            for (i = 0; i < scoring_cards->len; i++)
            {
                LrgCardInstance *card = g_ptr_array_index (scoring_cards, i);
                LrgCardDef *def = lrg_card_instance_get_def (card);
                if (lrg_card_def_get_suit (def) == priv->required_suit)
                {
                    suit_found = TRUE;
                    break;
                }
            }
        }

        if (!suit_found)
            return FALSE;
    }

    return TRUE;
}

static void
lrg_joker_def_real_apply_effect (LrgJokerDef       *self,
                                 LrgScoringContext *ctx,
                                 LrgJokerInstance  *instance)
{
    LrgJokerDefPrivate *priv = lrg_joker_def_get_instance_private (self);

    /* Apply simple effects */
    if (priv->plus_chips > 0)
    {
        lrg_scoring_context_add_chips (ctx, priv->plus_chips);
        lrg_debug (LRG_LOG_DOMAIN,
                   "Joker '%s' added +%ld chips",
                   priv->name, priv->plus_chips);
    }

    if (priv->plus_mult > 0)
    {
        lrg_scoring_context_add_mult (ctx, priv->plus_mult);
        lrg_debug (LRG_LOG_DOMAIN,
                   "Joker '%s' added +%ld mult",
                   priv->name, priv->plus_mult);
    }

    if (priv->x_mult > 1.0)
    {
        lrg_scoring_context_apply_x_mult (ctx, priv->x_mult);
        lrg_debug (LRG_LOG_DOMAIN,
                   "Joker '%s' applied x%.2f mult",
                   priv->name, priv->x_mult);
    }
}

static const gchar *
lrg_joker_def_real_get_description (LrgJokerDef      *self,
                                    LrgJokerInstance *instance)
{
    LrgJokerDefPrivate *priv = lrg_joker_def_get_instance_private (self);

    return priv->description;
}

static void
lrg_joker_def_class_init (LrgJokerDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_joker_def_finalize;

    klass->can_trigger = lrg_joker_def_real_can_trigger;
    klass->apply_effect = lrg_joker_def_real_apply_effect;
    klass->get_description = lrg_joker_def_real_get_description;
}

static void
lrg_joker_def_init (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv = lrg_joker_def_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->description = NULL;
    priv->rarity = LRG_JOKER_RARITY_COMMON;
    priv->cost = 4;
    priv->sell_value = 2;
    priv->plus_chips = 0;
    priv->plus_mult = 0;
    priv->x_mult = 1.0;
    priv->required_hand = LRG_HAND_TYPE_NONE;
    priv->required_suit = LRG_CARD_SUIT_NONE;
}

/**
 * lrg_joker_def_new:
 * @id: unique identifier for the joker
 * @name: display name
 *
 * Creates a new joker definition with default values.
 *
 * Returns: (transfer full): A new #LrgJokerDef
 *
 * Since: 1.0
 */
LrgJokerDef *
lrg_joker_def_new (const gchar *id,
                   const gchar *name)
{
    LrgJokerDef *self;
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    self = g_object_new (LRG_TYPE_JOKER_DEF, NULL);
    priv = lrg_joker_def_get_instance_private (self);

    priv->id = g_strdup (id);
    priv->name = g_strdup (name);

    return self;
}

/**
 * lrg_joker_def_get_id:
 * @self: a #LrgJokerDef
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
const gchar *
lrg_joker_def_get_id (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), NULL);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->id;
}

/**
 * lrg_joker_def_get_name:
 * @self: a #LrgJokerDef
 *
 * Gets the display name.
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
const gchar *
lrg_joker_def_get_name (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), NULL);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_joker_def_set_description:
 * @self: a #LrgJokerDef
 * @description: the description text
 *
 * Sets the description text.
 *
 * Since: 1.0
 */
void
lrg_joker_def_set_description (LrgJokerDef *self,
                               const gchar *description)
{
    LrgJokerDefPrivate *priv;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));

    priv = lrg_joker_def_get_instance_private (self);

    g_free (priv->description);
    priv->description = g_strdup (description);
}

/**
 * lrg_joker_def_get_description:
 * @self: a #LrgJokerDef
 * @instance: (nullable): optional instance for dynamic values
 *
 * Gets the description, potentially with dynamic values from the instance.
 *
 * Returns: (transfer none): the description
 *
 * Since: 1.0
 */
const gchar *
lrg_joker_def_get_description (LrgJokerDef      *self,
                               LrgJokerInstance *instance)
{
    LrgJokerDefClass *klass;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), NULL);

    klass = LRG_JOKER_DEF_GET_CLASS (self);
    g_return_val_if_fail (klass->get_description != NULL, NULL);

    return klass->get_description (self, instance);
}

/**
 * lrg_joker_def_set_rarity:
 * @self: a #LrgJokerDef
 * @rarity: the #LrgJokerRarity
 *
 * Sets the rarity tier.
 *
 * Since: 1.0
 */
void
lrg_joker_def_set_rarity (LrgJokerDef    *self,
                          LrgJokerRarity  rarity)
{
    LrgJokerDefPrivate *priv;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));

    priv = lrg_joker_def_get_instance_private (self);
    priv->rarity = rarity;
}

/**
 * lrg_joker_def_get_rarity:
 * @self: a #LrgJokerDef
 *
 * Gets the rarity tier.
 *
 * Returns: the #LrgJokerRarity
 *
 * Since: 1.0
 */
LrgJokerRarity
lrg_joker_def_get_rarity (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), LRG_JOKER_RARITY_COMMON);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->rarity;
}

/**
 * lrg_joker_def_set_cost:
 * @self: a #LrgJokerDef
 * @cost: the cost in gold
 *
 * Sets the base shop cost.
 *
 * Since: 1.0
 */
void
lrg_joker_def_set_cost (LrgJokerDef *self,
                        gint         cost)
{
    LrgJokerDefPrivate *priv;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));

    priv = lrg_joker_def_get_instance_private (self);
    priv->cost = cost;
}

/**
 * lrg_joker_def_get_cost:
 * @self: a #LrgJokerDef
 *
 * Gets the base shop cost.
 *
 * Returns: the cost in gold
 *
 * Since: 1.0
 */
gint
lrg_joker_def_get_cost (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), 0);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->cost;
}

/**
 * lrg_joker_def_set_sell_value:
 * @self: a #LrgJokerDef
 * @value: the sell value
 *
 * Sets the sell value (usually cost/2).
 *
 * Since: 1.0
 */
void
lrg_joker_def_set_sell_value (LrgJokerDef *self,
                              gint         value)
{
    LrgJokerDefPrivate *priv;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));

    priv = lrg_joker_def_get_instance_private (self);
    priv->sell_value = value;
}

/**
 * lrg_joker_def_get_sell_value:
 * @self: a #LrgJokerDef
 *
 * Gets the sell value.
 *
 * Returns: the sell value
 *
 * Since: 1.0
 */
gint
lrg_joker_def_get_sell_value (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), 0);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->sell_value;
}

/**
 * lrg_joker_def_set_plus_chips:
 * @self: a #LrgJokerDef
 * @chips: bonus chips to add
 *
 * Sets the +Chips value this joker provides.
 *
 * Since: 1.0
 */
void
lrg_joker_def_set_plus_chips (LrgJokerDef *self,
                              gint64       chips)
{
    LrgJokerDefPrivate *priv;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));

    priv = lrg_joker_def_get_instance_private (self);
    priv->plus_chips = chips;
}

/**
 * lrg_joker_def_get_plus_chips:
 * @self: a #LrgJokerDef
 *
 * Gets the +Chips value.
 *
 * Returns: the bonus chips
 *
 * Since: 1.0
 */
gint64
lrg_joker_def_get_plus_chips (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), 0);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->plus_chips;
}

/**
 * lrg_joker_def_set_plus_mult:
 * @self: a #LrgJokerDef
 * @mult: bonus mult to add
 *
 * Sets the +Mult value this joker provides.
 *
 * Since: 1.0
 */
void
lrg_joker_def_set_plus_mult (LrgJokerDef *self,
                             gint64       mult)
{
    LrgJokerDefPrivate *priv;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));

    priv = lrg_joker_def_get_instance_private (self);
    priv->plus_mult = mult;
}

/**
 * lrg_joker_def_get_plus_mult:
 * @self: a #LrgJokerDef
 *
 * Gets the +Mult value.
 *
 * Returns: the bonus mult
 *
 * Since: 1.0
 */
gint64
lrg_joker_def_get_plus_mult (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), 0);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->plus_mult;
}

/**
 * lrg_joker_def_set_x_mult:
 * @self: a #LrgJokerDef
 * @x_mult: the X multiplier
 *
 * Sets the X-Mult value this joker provides.
 *
 * Since: 1.0
 */
void
lrg_joker_def_set_x_mult (LrgJokerDef *self,
                          gdouble      x_mult)
{
    LrgJokerDefPrivate *priv;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));

    priv = lrg_joker_def_get_instance_private (self);
    priv->x_mult = x_mult;
}

/**
 * lrg_joker_def_get_x_mult:
 * @self: a #LrgJokerDef
 *
 * Gets the X-Mult value.
 *
 * Returns: the X multiplier (1.0 = no effect)
 *
 * Since: 1.0
 */
gdouble
lrg_joker_def_get_x_mult (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), 1.0);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->x_mult;
}

/**
 * lrg_joker_def_can_trigger:
 * @self: a #LrgJokerDef
 * @ctx: the scoring context
 * @instance: the joker instance
 *
 * Checks if this joker can trigger in the current context.
 *
 * Returns: %TRUE if the joker should trigger
 *
 * Since: 1.0
 */
gboolean
lrg_joker_def_can_trigger (LrgJokerDef       *self,
                           LrgScoringContext *ctx,
                           LrgJokerInstance  *instance)
{
    LrgJokerDefClass *klass;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), FALSE);
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (ctx), FALSE);

    klass = LRG_JOKER_DEF_GET_CLASS (self);
    g_return_val_if_fail (klass->can_trigger != NULL, FALSE);

    return klass->can_trigger (self, ctx, instance);
}

/**
 * lrg_joker_def_apply_effect:
 * @self: a #LrgJokerDef
 * @ctx: the scoring context to modify
 * @instance: the joker instance
 *
 * Applies this joker's effect to the scoring context.
 *
 * Since: 1.0
 */
void
lrg_joker_def_apply_effect (LrgJokerDef       *self,
                            LrgScoringContext *ctx,
                            LrgJokerInstance  *instance)
{
    LrgJokerDefClass *klass;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (ctx));

    klass = LRG_JOKER_DEF_GET_CLASS (self);
    g_return_if_fail (klass->apply_effect != NULL);

    klass->apply_effect (self, ctx, instance);
}

/**
 * lrg_joker_def_set_required_hand:
 * @self: a #LrgJokerDef
 * @hand_type: the required hand type, or NONE for any
 *
 * Sets a required hand type for triggering.
 *
 * Since: 1.0
 */
void
lrg_joker_def_set_required_hand (LrgJokerDef *self,
                                 LrgHandType  hand_type)
{
    LrgJokerDefPrivate *priv;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));

    priv = lrg_joker_def_get_instance_private (self);
    priv->required_hand = hand_type;
}

/**
 * lrg_joker_def_get_required_hand:
 * @self: a #LrgJokerDef
 *
 * Gets the required hand type.
 *
 * Returns: the required #LrgHandType, or NONE for any
 *
 * Since: 1.0
 */
LrgHandType
lrg_joker_def_get_required_hand (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), LRG_HAND_TYPE_NONE);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->required_hand;
}

/**
 * lrg_joker_def_set_required_suit:
 * @self: a #LrgJokerDef
 * @suit: the required suit, or NONE for any
 *
 * Sets a required suit for triggering (on scoring cards).
 *
 * Since: 1.0
 */
void
lrg_joker_def_set_required_suit (LrgJokerDef *self,
                                 LrgCardSuit  suit)
{
    LrgJokerDefPrivate *priv;

    g_return_if_fail (LRG_IS_JOKER_DEF (self));

    priv = lrg_joker_def_get_instance_private (self);
    priv->required_suit = suit;
}

/**
 * lrg_joker_def_get_required_suit:
 * @self: a #LrgJokerDef
 *
 * Gets the required suit.
 *
 * Returns: the required #LrgCardSuit, or NONE for any
 *
 * Since: 1.0
 */
LrgCardSuit
lrg_joker_def_get_required_suit (LrgJokerDef *self)
{
    LrgJokerDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (self), LRG_CARD_SUIT_NONE);

    priv = lrg_joker_def_get_instance_private (self);
    return priv->required_suit;
}
