/* lrg-combat-context.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-combat-context.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-combat-context
 * @title: LrgCombatContext
 * @short_description: Combat state container
 *
 * #LrgCombatContext holds all state for an active combat:
 * - Player and enemy combatants
 * - Card piles (draw, discard, exhaust)
 * - Current hand
 * - Energy
 * - Turn counter
 * - Combat phase
 *
 * Since: 1.0
 */

struct _LrgCombatContext
{
    GObject             parent_instance;

    LrgPlayerCombatant *player;
    GPtrArray          *enemies;
    LrgCombatRules     *rules;

    LrgCardPile        *draw_pile;
    LrgCardPile        *discard_pile;
    LrgCardPile        *exhaust_pile;
    LrgHand            *hand;

    gint                energy;
    gint                turn;
    LrgCombatPhase      phase;
    gint                cards_played_this_turn;

    GHashTable         *variables;
    GRand              *rng;
};

G_DEFINE_FINAL_TYPE (LrgCombatContext, lrg_combat_context, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_PLAYER,
    PROP_RULES,
    PROP_ENERGY,
    PROP_TURN,
    PROP_PHASE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_PHASE_CHANGED,
    SIGNAL_TURN_STARTED,
    SIGNAL_ENEMY_ADDED,
    SIGNAL_ENEMY_REMOVED,
    SIGNAL_ENERGY_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_combat_context_finalize (GObject *object)
{
    LrgCombatContext *self = LRG_COMBAT_CONTEXT (object);

    g_clear_object (&self->player);
    g_clear_object (&self->rules);
    g_clear_object (&self->draw_pile);
    g_clear_object (&self->discard_pile);
    g_clear_object (&self->exhaust_pile);
    g_clear_object (&self->hand);
    g_ptr_array_unref (self->enemies);
    g_hash_table_unref (self->variables);
    g_rand_free (self->rng);

    G_OBJECT_CLASS (lrg_combat_context_parent_class)->finalize (object);
}

static void
lrg_combat_context_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgCombatContext *self = LRG_COMBAT_CONTEXT (object);

    switch (prop_id)
    {
    case PROP_PLAYER:
        g_value_set_object (value, self->player);
        break;
    case PROP_RULES:
        g_value_set_object (value, self->rules);
        break;
    case PROP_ENERGY:
        g_value_set_int (value, self->energy);
        break;
    case PROP_TURN:
        g_value_set_int (value, self->turn);
        break;
    case PROP_PHASE:
        g_value_set_enum (value, self->phase);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_combat_context_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgCombatContext *self = LRG_COMBAT_CONTEXT (object);

    switch (prop_id)
    {
    case PROP_PLAYER:
        g_clear_object (&self->player);
        self->player = g_value_dup_object (value);
        break;
    case PROP_RULES:
        g_clear_object (&self->rules);
        self->rules = g_value_dup_object (value);
        break;
    case PROP_ENERGY:
        self->energy = MAX (0, g_value_get_int (value));
        break;
    case PROP_TURN:
        self->turn = MAX (0, g_value_get_int (value));
        break;
    case PROP_PHASE:
        lrg_combat_context_set_phase (self, g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_combat_context_class_init (LrgCombatContextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_combat_context_finalize;
    object_class->get_property = lrg_combat_context_get_property;
    object_class->set_property = lrg_combat_context_set_property;

    properties[PROP_PLAYER] =
        g_param_spec_object ("player",
                             "Player",
                             "Player combatant",
                             LRG_TYPE_PLAYER_COMBATANT,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_RULES] =
        g_param_spec_object ("rules",
                             "Rules",
                             "Combat rules",
                             LRG_TYPE_COMBAT_RULES,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_ENERGY] =
        g_param_spec_int ("energy",
                          "Energy",
                          "Current energy",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_TURN] =
        g_param_spec_int ("turn",
                          "Turn",
                          "Current turn number",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_PHASE] =
        g_param_spec_enum ("phase",
                           "Phase",
                           "Current combat phase",
                           LRG_TYPE_COMBAT_PHASE,
                           LRG_COMBAT_PHASE_SETUP,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_PHASE_CHANGED] =
        g_signal_new ("phase-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_COMBAT_PHASE,
                      LRG_TYPE_COMBAT_PHASE);

    signals[SIGNAL_TURN_STARTED] =
        g_signal_new ("turn-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_ENEMY_ADDED] =
        g_signal_new ("enemy-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_ENEMY_INSTANCE);

    signals[SIGNAL_ENEMY_REMOVED] =
        g_signal_new ("enemy-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_ENEMY_INSTANCE);

    signals[SIGNAL_ENERGY_CHANGED] =
        g_signal_new ("energy-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
}

static void
lrg_combat_context_init (LrgCombatContext *self)
{
    self->player = NULL;
    self->rules = NULL;
    self->enemies = g_ptr_array_new_with_free_func (g_object_unref);
    self->draw_pile = lrg_card_pile_new ();
    self->discard_pile = lrg_card_pile_new ();
    self->exhaust_pile = lrg_card_pile_new ();
    self->hand = lrg_hand_new ();
    self->energy = 0;
    self->turn = 0;
    self->phase = LRG_COMBAT_PHASE_SETUP;
    self->cards_played_this_turn = 0;
    self->variables = g_hash_table_new_full (g_str_hash, g_str_equal,
                                             g_free, NULL);
    self->rng = g_rand_new ();
}

/**
 * lrg_combat_context_new:
 * @player: the player combatant
 * @rules: (nullable): combat rules
 *
 * Creates a new combat context.
 *
 * Returns: (transfer full): a new #LrgCombatContext
 *
 * Since: 1.0
 */
LrgCombatContext *
lrg_combat_context_new (LrgPlayerCombatant *player,
                        LrgCombatRules     *rules)
{
    g_return_val_if_fail (LRG_IS_PLAYER_COMBATANT (player), NULL);

    return g_object_new (LRG_TYPE_COMBAT_CONTEXT,
                         "player", player,
                         "rules", rules,
                         NULL);
}

LrgCombatPhase
lrg_combat_context_get_phase (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), LRG_COMBAT_PHASE_SETUP);
    return self->phase;
}

void
lrg_combat_context_set_phase (LrgCombatContext *self,
                              LrgCombatPhase    phase)
{
    LrgCombatPhase old_phase;

    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));

    if (self->phase == phase)
        return;

    old_phase = self->phase;
    self->phase = phase;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PHASE]);
    g_signal_emit (self, signals[SIGNAL_PHASE_CHANGED], 0, old_phase, phase);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Combat phase changed: %d -> %d", old_phase, phase);
}

gint
lrg_combat_context_get_turn (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), 0);
    return self->turn;
}

void
lrg_combat_context_increment_turn (LrgCombatContext *self)
{
    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));

    self->turn++;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TURN]);
    g_signal_emit (self, signals[SIGNAL_TURN_STARTED], 0, self->turn);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Turn %d started", self->turn);
}

LrgPlayerCombatant *
lrg_combat_context_get_player (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), NULL);
    return self->player;
}

void
lrg_combat_context_add_enemy (LrgCombatContext *self,
                              LrgEnemyInstance *enemy)
{
    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));
    g_return_if_fail (LRG_IS_ENEMY_INSTANCE (enemy));

    g_ptr_array_add (self->enemies, g_object_ref (enemy));
    g_signal_emit (self, signals[SIGNAL_ENEMY_ADDED], 0, enemy);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Added enemy '%s' to combat",
                   lrg_combatant_get_id (LRG_COMBATANT (enemy)));
}

void
lrg_combat_context_remove_enemy (LrgCombatContext *self,
                                 LrgEnemyInstance *enemy)
{
    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));
    g_return_if_fail (LRG_IS_ENEMY_INSTANCE (enemy));

    if (g_ptr_array_remove (self->enemies, enemy))
    {
        g_signal_emit (self, signals[SIGNAL_ENEMY_REMOVED], 0, enemy);
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                       "Removed enemy '%s' from combat",
                       lrg_combatant_get_id (LRG_COMBATANT (enemy)));
    }
}

GPtrArray *
lrg_combat_context_get_enemies (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), NULL);
    return self->enemies;
}

guint
lrg_combat_context_get_enemy_count (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), 0);
    return self->enemies->len;
}

LrgEnemyInstance *
lrg_combat_context_get_enemy_at (LrgCombatContext *self,
                                 guint             index)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), NULL);

    if (index >= self->enemies->len)
        return NULL;

    return g_ptr_array_index (self->enemies, index);
}

LrgCardPile *
lrg_combat_context_get_draw_pile (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), NULL);
    return self->draw_pile;
}

LrgCardPile *
lrg_combat_context_get_discard_pile (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), NULL);
    return self->discard_pile;
}

LrgCardPile *
lrg_combat_context_get_exhaust_pile (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), NULL);
    return self->exhaust_pile;
}

LrgHand *
lrg_combat_context_get_hand (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), NULL);
    return self->hand;
}

gint
lrg_combat_context_get_energy (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), 0);
    return self->energy;
}

void
lrg_combat_context_set_energy (LrgCombatContext *self,
                               gint              energy)
{
    gint old_energy;

    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));

    energy = MAX (0, energy);

    if (self->energy == energy)
        return;

    old_energy = self->energy;
    self->energy = energy;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENERGY]);
    g_signal_emit (self, signals[SIGNAL_ENERGY_CHANGED], 0, old_energy, energy);
}

gboolean
lrg_combat_context_spend_energy (LrgCombatContext *self,
                                 gint              amount)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), FALSE);

    if (amount <= 0)
        return TRUE;

    if (self->energy < amount)
        return FALSE;

    lrg_combat_context_set_energy (self, self->energy - amount);
    return TRUE;
}

void
lrg_combat_context_add_energy (LrgCombatContext *self,
                               gint              amount)
{
    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));

    if (amount > 0)
        lrg_combat_context_set_energy (self, self->energy + amount);
}

gint
lrg_combat_context_get_cards_played_this_turn (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), 0);
    return self->cards_played_this_turn;
}

void
lrg_combat_context_increment_cards_played (LrgCombatContext *self)
{
    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));
    self->cards_played_this_turn++;
}

void
lrg_combat_context_reset_turn_counters (LrgCombatContext *self)
{
    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));
    self->cards_played_this_turn = 0;
}

LrgCombatRules *
lrg_combat_context_get_rules (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), NULL);
    return self->rules;
}

void
lrg_combat_context_set_variable (LrgCombatContext *self,
                                 const gchar      *name,
                                 gint              value)
{
    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));
    g_return_if_fail (name != NULL);

    g_hash_table_insert (self->variables,
                         g_strdup (name),
                         GINT_TO_POINTER (value));
}

gint
lrg_combat_context_get_variable (LrgCombatContext *self,
                                 const gchar      *name)
{
    gpointer value;

    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), 0);
    g_return_val_if_fail (name != NULL, 0);

    if (!g_hash_table_lookup_extended (self->variables, name, NULL, &value))
        return 0;

    return GPOINTER_TO_INT (value);
}

GRand *
lrg_combat_context_get_rng (LrgCombatContext *self)
{
    g_return_val_if_fail (LRG_IS_COMBAT_CONTEXT (self), NULL);
    return self->rng;
}

void
lrg_combat_context_set_seed (LrgCombatContext *self,
                             guint32           seed)
{
    g_return_if_fail (LRG_IS_COMBAT_CONTEXT (self));

    g_rand_set_seed (self->rng, seed);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Combat RNG seed set to %u", seed);
}
