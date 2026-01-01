/* lrg-combat-manager.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-combat-manager.h"
#include "lrg-card-def.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-combat-manager
 * @title: LrgCombatManager
 * @short_description: Combat flow controller
 *
 * #LrgCombatManager controls the flow of deckbuilder combat:
 * - Starting/ending combat
 * - Managing turns (player and enemy)
 * - Drawing cards
 * - Playing cards
 * - Checking victory/defeat conditions
 *
 * Subclass to customize combat behavior.
 *
 * Since: 1.0
 */

typedef struct
{
    LrgCombatContext *context;
    gboolean          active;
    LrgCombatResult   result;
} LrgCombatManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCombatManager, lrg_combat_manager, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CONTEXT,
    PROP_ACTIVE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_COMBAT_STARTED,
    SIGNAL_COMBAT_ENDED,
    SIGNAL_TURN_STARTED,
    SIGNAL_TURN_ENDED,
    SIGNAL_CARD_PLAYED,
    SIGNAL_CARD_DRAWN,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Default virtual method implementations */

static void
lrg_combat_manager_real_on_combat_start (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv = lrg_combat_manager_get_instance_private (self);
    LrgCombatContext *ctx = priv->context;
    GPtrArray *enemies;
    guint i;

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER, "Combat started");

    /* Set initial phase */
    lrg_combat_context_set_phase (ctx, LRG_COMBAT_PHASE_SETUP);

    /* Shuffle draw pile */
    lrg_card_pile_shuffle (lrg_combat_context_get_draw_pile (ctx),
                           lrg_combat_context_get_rng (ctx));

    /* Initialize enemy intents */
    enemies = lrg_combat_context_get_enemies (ctx);
    for (i = 0; i < enemies->len; i++)
    {
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i);
        lrg_enemy_instance_decide_intent (enemy, ctx);
    }

    /* Start first player turn */
    lrg_combat_manager_start_player_turn (self);
}

static void
lrg_combat_manager_real_on_turn_start (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv = lrg_combat_manager_get_instance_private (self);
    LrgCombatContext *ctx = priv->context;
    LrgCombatRules *rules;
    LrgPlayerCombatant *player;
    gint energy_per_turn;
    gint cards_per_turn;

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER, "Player turn started");

    rules = lrg_combat_context_get_rules (ctx);
    player = lrg_combat_context_get_player (ctx);

    /* Increment turn counter */
    lrg_combat_context_increment_turn (ctx);

    /* Reset turn counters */
    lrg_combat_context_reset_turn_counters (ctx);

    /* Clear block (unless barricade) */
    if (!lrg_combatant_has_status (LRG_COMBATANT (player), "barricade"))
        lrg_combatant_clear_block (LRG_COMBATANT (player));

    /* Refill energy */
    if (rules != NULL)
        energy_per_turn = lrg_combat_rules_get_energy_per_turn (rules,
                                                                LRG_COMBATANT (player));
    else
        energy_per_turn = 3;

    lrg_combat_context_set_energy (ctx, energy_per_turn);

    /* Draw cards */
    if (rules != NULL)
        cards_per_turn = lrg_combat_rules_get_cards_per_turn (rules,
                                                              LRG_COMBATANT (player));
    else
        cards_per_turn = 5;

    lrg_combat_manager_draw_cards (self, cards_per_turn);

    /* Set phase */
    lrg_combat_context_set_phase (ctx, LRG_COMBAT_PHASE_PLAYER_PLAY);
}

static void
lrg_combat_manager_real_on_turn_end (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv = lrg_combat_manager_get_instance_private (self);
    LrgCombatContext *ctx = priv->context;
    LrgHand *hand;
    LrgCardPile *discard;

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER, "Player turn ended");

    lrg_combat_context_set_phase (ctx, LRG_COMBAT_PHASE_PLAYER_END);

    /* Discard hand (respecting retain) */
    hand = lrg_combat_context_get_hand (ctx);
    discard = lrg_combat_context_get_discard_pile (ctx);
    lrg_hand_discard_all (hand, discard);

    /* Move to enemy turn */
    lrg_combat_context_set_phase (ctx, LRG_COMBAT_PHASE_ENEMY_TURN);
}

static void
lrg_combat_manager_real_on_enemy_turn (LrgCombatManager *self,
                                       LrgEnemyInstance *enemy)
{
    LrgCombatManagerPrivate *priv = lrg_combat_manager_get_instance_private (self);
    LrgCombatContext *ctx = priv->context;

    if (!lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
        return;

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Enemy '%s' taking turn",
                   lrg_combatant_get_id (LRG_COMBATANT (enemy)));

    /* Execute intent */
    lrg_enemy_instance_execute_intent (enemy, ctx);

    /* Decide next intent */
    lrg_enemy_instance_decide_intent (enemy, ctx);

    /* Increment turn counter */
    lrg_enemy_instance_increment_turn (enemy);
}

static void
lrg_combat_manager_real_on_combat_end (LrgCombatManager *self,
                                       LrgCombatResult   result)
{
    LrgCombatManagerPrivate *priv = lrg_combat_manager_get_instance_private (self);

    priv->result = result;
    priv->active = FALSE;

    lrg_combat_context_set_phase (priv->context, LRG_COMBAT_PHASE_FINISHED);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Combat ended with result: %d", result);
}

static void
lrg_combat_manager_finalize (GObject *object)
{
    LrgCombatManager *self = LRG_COMBAT_MANAGER (object);
    LrgCombatManagerPrivate *priv = lrg_combat_manager_get_instance_private (self);

    g_clear_object (&priv->context);

    G_OBJECT_CLASS (lrg_combat_manager_parent_class)->finalize (object);
}

static void
lrg_combat_manager_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgCombatManager *self = LRG_COMBAT_MANAGER (object);
    LrgCombatManagerPrivate *priv = lrg_combat_manager_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_CONTEXT:
        g_value_set_object (value, priv->context);
        break;
    case PROP_ACTIVE:
        g_value_set_boolean (value, priv->active);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_combat_manager_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgCombatManager *self = LRG_COMBAT_MANAGER (object);
    LrgCombatManagerPrivate *priv = lrg_combat_manager_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_CONTEXT:
        g_clear_object (&priv->context);
        priv->context = g_value_dup_object (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_combat_manager_class_init (LrgCombatManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_combat_manager_finalize;
    object_class->get_property = lrg_combat_manager_get_property;
    object_class->set_property = lrg_combat_manager_set_property;

    klass->on_combat_start = lrg_combat_manager_real_on_combat_start;
    klass->on_turn_start = lrg_combat_manager_real_on_turn_start;
    klass->on_turn_end = lrg_combat_manager_real_on_turn_end;
    klass->on_enemy_turn = lrg_combat_manager_real_on_enemy_turn;
    klass->on_combat_end = lrg_combat_manager_real_on_combat_end;

    properties[PROP_CONTEXT] =
        g_param_spec_object ("context",
                             "Context",
                             "Combat context",
                             LRG_TYPE_COMBAT_CONTEXT,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_ACTIVE] =
        g_param_spec_boolean ("active",
                              "Active",
                              "Whether combat is active",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_COMBAT_STARTED] =
        g_signal_new ("combat-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_COMBAT_ENDED] =
        g_signal_new ("combat-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_COMBAT_RESULT);

    signals[SIGNAL_TURN_STARTED] =
        g_signal_new ("turn-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_TURN_ENDED] =
        g_signal_new ("turn-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_CARD_PLAYED] =
        g_signal_new ("card-played",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_CARD_INSTANCE,
                      LRG_TYPE_COMBATANT);

    signals[SIGNAL_CARD_DRAWN] =
        g_signal_new ("card-drawn",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_CARD_INSTANCE);
}

static void
lrg_combat_manager_init (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv = lrg_combat_manager_get_instance_private (self);

    priv->context = NULL;
    priv->active = FALSE;
    priv->result = LRG_COMBAT_RESULT_IN_PROGRESS;
}

/**
 * lrg_combat_manager_new:
 *
 * Creates a new combat manager.
 *
 * Returns: (transfer full): a new #LrgCombatManager
 *
 * Since: 1.0
 */
LrgCombatManager *
lrg_combat_manager_new (void)
{
    return g_object_new (LRG_TYPE_COMBAT_MANAGER, NULL);
}

void
lrg_combat_manager_start_combat (LrgCombatManager *self,
                                 LrgCombatContext *context)
{
    LrgCombatManagerPrivate *priv;
    LrgCombatManagerClass *klass;

    g_return_if_fail (LRG_IS_COMBAT_MANAGER (self));
    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (context));

    priv = lrg_combat_manager_get_instance_private (self);

    if (priv->active)
    {
        lrg_warning (LRG_LOG_DOMAIN_DECKBUILDER,
                     "Cannot start combat: already active");
        return;
    }

    g_clear_object (&priv->context);
    priv->context = g_object_ref (context);
    priv->active = TRUE;
    priv->result = LRG_COMBAT_RESULT_IN_PROGRESS;

    klass = LRG_COMBAT_MANAGER_GET_CLASS (self);
    if (klass->on_combat_start != NULL)
        klass->on_combat_start (self);

    g_signal_emit (self, signals[SIGNAL_COMBAT_STARTED], 0);
}

void
lrg_combat_manager_end_combat (LrgCombatManager *self,
                               LrgCombatResult   result)
{
    LrgCombatManagerPrivate *priv;
    LrgCombatManagerClass *klass;

    g_return_if_fail (LRG_IS_COMBAT_MANAGER (self));

    priv = lrg_combat_manager_get_instance_private (self);

    if (!priv->active)
        return;

    klass = LRG_COMBAT_MANAGER_GET_CLASS (self);
    if (klass->on_combat_end != NULL)
        klass->on_combat_end (self, result);

    g_signal_emit (self, signals[SIGNAL_COMBAT_ENDED], 0, result);
}

LrgCombatContext *
lrg_combat_manager_get_context (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMBAT_MANAGER (self), NULL);

    priv = lrg_combat_manager_get_instance_private (self);
    return priv->context;
}

gboolean
lrg_combat_manager_is_active (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMBAT_MANAGER (self), FALSE);

    priv = lrg_combat_manager_get_instance_private (self);
    return priv->active;
}

void
lrg_combat_manager_start_player_turn (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv;
    LrgCombatManagerClass *klass;

    g_return_if_fail (LRG_IS_COMBAT_MANAGER (self));

    priv = lrg_combat_manager_get_instance_private (self);

    if (!priv->active || priv->context == NULL)
        return;

    lrg_combat_context_set_phase (priv->context, LRG_COMBAT_PHASE_PLAYER_START);

    klass = LRG_COMBAT_MANAGER_GET_CLASS (self);
    if (klass->on_turn_start != NULL)
        klass->on_turn_start (self);

    g_signal_emit (self, signals[SIGNAL_TURN_STARTED], 0,
                   lrg_combat_context_get_turn (priv->context));
}

void
lrg_combat_manager_end_player_turn (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv;
    LrgCombatManagerClass *klass;
    GPtrArray *enemies;
    guint i;

    g_return_if_fail (LRG_IS_COMBAT_MANAGER (self));

    priv = lrg_combat_manager_get_instance_private (self);

    if (!priv->active || priv->context == NULL)
        return;

    klass = LRG_COMBAT_MANAGER_GET_CLASS (self);

    /* End player turn */
    if (klass->on_turn_end != NULL)
        klass->on_turn_end (self);

    g_signal_emit (self, signals[SIGNAL_TURN_ENDED], 0,
                   lrg_combat_context_get_turn (priv->context));

    /* Execute all enemy turns */
    enemies = lrg_combat_context_get_enemies (priv->context);
    for (i = 0; i < enemies->len; i++)
    {
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i);

        if (klass->on_enemy_turn != NULL)
            klass->on_enemy_turn (self, enemy);

        /* Check for defeat after each enemy */
        if (lrg_combat_manager_check_defeat (self))
        {
            lrg_combat_manager_end_combat (self, LRG_COMBAT_RESULT_DEFEAT);
            return;
        }
    }

    /* Check for victory (all enemies dead) */
    if (lrg_combat_manager_check_victory (self))
    {
        lrg_combat_manager_end_combat (self, LRG_COMBAT_RESULT_VICTORY);
        return;
    }

    /* Start next player turn */
    lrg_combat_manager_start_player_turn (self);
}

gboolean
lrg_combat_manager_play_card (LrgCombatManager  *self,
                              LrgCardInstance   *card,
                              LrgCombatant      *target,
                              GError           **error)
{
    LrgCombatManagerPrivate *priv;
    LrgCombatContext *ctx;
    LrgCardDef *def;
    gint cost;

    g_return_val_if_fail (LRG_IS_COMBAT_MANAGER (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    priv = lrg_combat_manager_get_instance_private (self);

    if (!priv->active || priv->context == NULL)
    {
        g_set_error (error, LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_COMBAT_NOT_ACTIVE,
                     "Combat is not active");
        return FALSE;
    }

    ctx = priv->context;

    /* Check phase */
    if (lrg_combat_context_get_phase (ctx) != LRG_COMBAT_PHASE_PLAYER_PLAY)
    {
        g_set_error (error, LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_CARD_UNPLAYABLE,
                     "Cannot play cards during this phase");
        return FALSE;
    }

    def = lrg_card_instance_get_def (card);

    /* Check if card can be played */
    if (!lrg_card_def_can_play (def, ctx))
    {
        g_set_error (error, LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_CARD_UNPLAYABLE,
                     "Card cannot be played");
        return FALSE;
    }

    /* Pay energy cost */
    cost = lrg_card_def_calculate_cost (def, ctx);
    if (!lrg_combat_context_spend_energy (ctx, cost))
    {
        g_set_error (error, LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_INSUFFICIENT_ENERGY,
                     "Insufficient energy");
        return FALSE;
    }

    /* Store X value for X-cost cards */
    if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_X_COST))
        lrg_combat_context_set_variable (ctx, "X", cost);

    /* Remove from hand */
    lrg_hand_remove (lrg_combat_context_get_hand (ctx), card);
    lrg_card_instance_set_zone (card, LRG_ZONE_PLAYED);

    /* Execute card effects */
    lrg_card_def_on_play (def, ctx, target);

    /* Track cards played */
    lrg_combat_context_increment_cards_played (ctx);

    /* Move to appropriate pile */
    if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_EXHAUST))
    {
        lrg_card_pile_add (lrg_combat_context_get_exhaust_pile (ctx),
                           card, LRG_PILE_POSITION_TOP);
        lrg_card_instance_set_zone (card, LRG_ZONE_EXHAUST);
        lrg_card_def_on_exhaust (def, ctx);
    }
    else
    {
        lrg_card_pile_add (lrg_combat_context_get_discard_pile (ctx),
                           card, LRG_PILE_POSITION_TOP);
        lrg_card_instance_set_zone (card, LRG_ZONE_DISCARD);
    }

    g_signal_emit (self, signals[SIGNAL_CARD_PLAYED], 0, card, target);

    /* Check for victory/defeat */
    if (lrg_combat_manager_check_victory (self))
        lrg_combat_manager_end_combat (self, LRG_COMBAT_RESULT_VICTORY);
    else if (lrg_combat_manager_check_defeat (self))
        lrg_combat_manager_end_combat (self, LRG_COMBAT_RESULT_DEFEAT);

    return TRUE;
}

gint
lrg_combat_manager_draw_cards (LrgCombatManager *self,
                               gint              count)
{
    LrgCombatManagerPrivate *priv;
    LrgCombatContext *ctx;
    LrgCardPile *draw;
    LrgCardPile *discard;
    LrgHand *hand;
    gint drawn;
    gint i;

    g_return_val_if_fail (LRG_IS_COMBAT_MANAGER (self), 0);

    priv = lrg_combat_manager_get_instance_private (self);

    if (!priv->active || priv->context == NULL || count <= 0)
        return 0;

    ctx = priv->context;
    draw = lrg_combat_context_get_draw_pile (ctx);
    discard = lrg_combat_context_get_discard_pile (ctx);
    hand = lrg_combat_context_get_hand (ctx);

    drawn = 0;

    for (i = 0; i < count; i++)
    {
        LrgCardInstance *card;

        /* Check if draw pile is empty */
        if (lrg_card_pile_get_count (draw) == 0)
        {
            /* Shuffle discard into draw */
            if (lrg_card_pile_get_count (discard) == 0)
                break;

            lrg_card_pile_transfer_all (discard, draw);
            lrg_card_pile_shuffle (draw, lrg_combat_context_get_rng (ctx));

            lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                           "Shuffled discard into draw pile");
        }

        /* Draw card */
        card = lrg_card_pile_draw (draw);
        if (card == NULL)
            break;

        /* Add to hand (may fail if full) */
        if (lrg_hand_add (hand, card))
        {
            lrg_card_instance_set_zone (card, LRG_ZONE_HAND);
            g_signal_emit (self, signals[SIGNAL_CARD_DRAWN], 0, card);

            /* Trigger on_draw */
            lrg_card_def_on_draw (lrg_card_instance_get_def (card), ctx);

            drawn++;
        }
        else
        {
            /* Hand full - discard */
            lrg_card_pile_add (discard, card, LRG_PILE_POSITION_TOP);
            lrg_card_instance_set_zone (card, LRG_ZONE_DISCARD);
        }

        /* Note: Do NOT unref card - ownership was transferred to hand or discard pile */
    }

    return drawn;
}

gboolean
lrg_combat_manager_check_victory (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv;
    GPtrArray *enemies;
    guint i;

    g_return_val_if_fail (LRG_IS_COMBAT_MANAGER (self), FALSE);

    priv = lrg_combat_manager_get_instance_private (self);

    if (priv->context == NULL)
        return FALSE;

    enemies = lrg_combat_context_get_enemies (priv->context);

    for (i = 0; i < enemies->len; i++)
    {
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i);
        if (lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
            return FALSE;
    }

    return TRUE;
}

gboolean
lrg_combat_manager_check_defeat (LrgCombatManager *self)
{
    LrgCombatManagerPrivate *priv;
    LrgPlayerCombatant *player;

    g_return_val_if_fail (LRG_IS_COMBAT_MANAGER (self), FALSE);

    priv = lrg_combat_manager_get_instance_private (self);

    if (priv->context == NULL)
        return FALSE;

    player = lrg_combat_context_get_player (priv->context);

    return !lrg_combatant_is_alive (LRG_COMBATANT (player));
}
