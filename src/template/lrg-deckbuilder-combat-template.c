/* lrg-deckbuilder-combat-template.c
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of LrgDeckbuilderCombatTemplate - a Slay the Spire-style
 * combat template that extends LrgDeckbuilderTemplate with enemy management,
 * player health/block, and combat flow.
 */

#include "config.h"
#include "lrg-deckbuilder-combat-template.h"
#include "../deckbuilder/lrg-combat-context.h"
#include "../deckbuilder/lrg-combat-rules.h"
#include "../deckbuilder/lrg-player-combatant.h"
#include "../deckbuilder/lrg-enemy-instance.h"
#include "../deckbuilder/lrg-enemy-def.h"
#include "../deckbuilder/lrg-combatant.h"
#include "../deckbuilder/lrg-deck-instance.h"
#include "../lrg-log.h"

/* Default values */
#define DEFAULT_MAX_HEALTH 80

struct _LrgDeckbuilderCombatTemplate
{
    LrgDeckbuilderTemplate  parent_instance;

    LrgPlayerCombatant     *player;
    LrgCombatContext       *combat_context;
    LrgCombatRules         *combat_rules;
    LrgEnemyInstance       *selected_target;

    gboolean                in_combat;
    LrgCombatResult         combat_result;
    guint                   current_enemy_index;
    gboolean                enemy_turns_complete;
};

/* Property IDs */
enum
{
    PROP_0,
    PROP_IN_COMBAT,
    PROP_COMBAT_RESULT,
    PROP_PLAYER_HEALTH,
    PROP_PLAYER_MAX_HEALTH,
    PROP_PLAYER_BLOCK,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Signals */
enum
{
    SIGNAL_COMBAT_STARTED,
    SIGNAL_COMBAT_ENDED,
    SIGNAL_ENEMY_ADDED,
    SIGNAL_ENEMY_REMOVED,
    SIGNAL_ENEMY_DIED,
    SIGNAL_PLAYER_DAMAGED,
    SIGNAL_PLAYER_HEALED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgDeckbuilderCombatTemplate, lrg_deckbuilder_combat_template,
                     LRG_TYPE_DECKBUILDER_TEMPLATE)

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static void
remove_dead_enemies (LrgDeckbuilderCombatTemplate *self)
{
    GPtrArray *enemies;
    guint i;

    if (self->combat_context == NULL)
        return;

    enemies = lrg_combat_context_get_enemies (self->combat_context);
    if (enemies == NULL)
        return;

    /* Iterate backwards to safely remove */
    for (i = enemies->len; i > 0; i--)
    {
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i - 1);
        if (!lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
        {
            g_signal_emit (self, signals[SIGNAL_ENEMY_DIED], 0, enemy);
            lrg_combat_context_remove_enemy (self->combat_context, enemy);
        }
    }
}

/* ==========================================================================
 * LrgDeckbuilderTemplate Virtual Method Overrides
 * ========================================================================== */

static void
lrg_deckbuilder_combat_template_real_start_turn (LrgDeckbuilderTemplate *template,
                                                  guint                   turn_number)
{
    LrgDeckbuilderCombatTemplate *self = LRG_DECKBUILDER_COMBAT_TEMPLATE (template);

    /* Clear player block at start of turn */
    if (self->player != NULL)
        lrg_combatant_clear_block (LRG_COMBATANT (self->player));

    /* Chain up for energy reset and card draw */
    LRG_DECKBUILDER_TEMPLATE_CLASS (lrg_deckbuilder_combat_template_parent_class)->start_turn (template, turn_number);
}

static void
lrg_deckbuilder_combat_template_real_end_turn (LrgDeckbuilderTemplate *template,
                                                guint                   turn_number)
{
    /* Chain up for hand discard */
    LRG_DECKBUILDER_TEMPLATE_CLASS (lrg_deckbuilder_combat_template_parent_class)->end_turn (template, turn_number);
}

static gboolean
lrg_deckbuilder_combat_template_real_on_card_played (LrgDeckbuilderTemplate *template,
                                                      LrgCardInstance        *card,
                                                      gpointer                target)
{
    LrgDeckbuilderCombatTemplate *self = LRG_DECKBUILDER_COMBAT_TEMPLATE (template);

    /* Increment cards played counter in combat context */
    if (self->combat_context != NULL)
        lrg_combat_context_increment_cards_played (self->combat_context);

    /* Card effects should be handled by game-specific logic */
    return TRUE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_deckbuilder_combat_template_set_property (GObject      *object,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
    LrgDeckbuilderCombatTemplate *self = LRG_DECKBUILDER_COMBAT_TEMPLATE (object);

    switch (prop_id)
    {
    case PROP_PLAYER_MAX_HEALTH:
        lrg_deckbuilder_combat_template_set_player_max_health (self, g_value_get_int (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_deckbuilder_combat_template_get_property (GObject    *object,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
    LrgDeckbuilderCombatTemplate *self = LRG_DECKBUILDER_COMBAT_TEMPLATE (object);

    switch (prop_id)
    {
    case PROP_IN_COMBAT:
        g_value_set_boolean (value, self->in_combat);
        break;

    case PROP_COMBAT_RESULT:
        g_value_set_int (value, self->combat_result);
        break;

    case PROP_PLAYER_HEALTH:
        g_value_set_int (value, lrg_deckbuilder_combat_template_get_player_health (self));
        break;

    case PROP_PLAYER_MAX_HEALTH:
        g_value_set_int (value, lrg_deckbuilder_combat_template_get_player_max_health (self));
        break;

    case PROP_PLAYER_BLOCK:
        g_value_set_int (value, lrg_deckbuilder_combat_template_get_player_block (self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_deckbuilder_combat_template_dispose (GObject *object)
{
    LrgDeckbuilderCombatTemplate *self = LRG_DECKBUILDER_COMBAT_TEMPLATE (object);

    g_clear_object (&self->player);
    g_clear_object (&self->combat_context);
    g_clear_object (&self->combat_rules);
    self->selected_target = NULL;

    G_OBJECT_CLASS (lrg_deckbuilder_combat_template_parent_class)->dispose (object);
}

static void
lrg_deckbuilder_combat_template_init (LrgDeckbuilderCombatTemplate *self)
{
    self->player = lrg_player_combatant_new ("player", "Player", DEFAULT_MAX_HEALTH);
    self->combat_context = NULL;
    self->combat_rules = NULL;
    self->selected_target = NULL;
    self->in_combat = FALSE;
    self->combat_result = LRG_COMBAT_RESULT_IN_PROGRESS;
    self->current_enemy_index = 0;
    self->enemy_turns_complete = TRUE;
}

static void
lrg_deckbuilder_combat_template_class_init (LrgDeckbuilderCombatTemplateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgDeckbuilderTemplateClass *db_class = LRG_DECKBUILDER_TEMPLATE_CLASS (klass);

    /* GObject methods */
    object_class->set_property = lrg_deckbuilder_combat_template_set_property;
    object_class->get_property = lrg_deckbuilder_combat_template_get_property;
    object_class->dispose = lrg_deckbuilder_combat_template_dispose;

    /* Override parent virtual methods */
    db_class->start_turn = lrg_deckbuilder_combat_template_real_start_turn;
    db_class->end_turn = lrg_deckbuilder_combat_template_real_end_turn;
    db_class->on_card_played = lrg_deckbuilder_combat_template_real_on_card_played;

    /* Properties */
    properties[PROP_IN_COMBAT] =
        g_param_spec_boolean ("in-combat",
                              "In Combat",
                              "Whether currently in combat",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_COMBAT_RESULT] =
        g_param_spec_int ("combat-result",
                          "Combat Result",
                          "The result of combat",
                          LRG_COMBAT_RESULT_IN_PROGRESS, LRG_COMBAT_RESULT_ESCAPE,
                          LRG_COMBAT_RESULT_IN_PROGRESS,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYER_HEALTH] =
        g_param_spec_int ("player-health",
                          "Player Health",
                          "The player's current health",
                          0, G_MAXINT, DEFAULT_MAX_HEALTH,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYER_MAX_HEALTH] =
        g_param_spec_int ("player-max-health",
                          "Player Max Health",
                          "The player's maximum health",
                          1, G_MAXINT, DEFAULT_MAX_HEALTH,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYER_BLOCK] =
        g_param_spec_int ("player-block",
                          "Player Block",
                          "The player's current block",
                          0, G_MAXINT, 0,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */
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
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_ENEMY_ADDED] =
        g_signal_new ("enemy-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_OBJECT);

    signals[SIGNAL_ENEMY_REMOVED] =
        g_signal_new ("enemy-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_OBJECT);

    signals[SIGNAL_ENEMY_DIED] =
        g_signal_new ("enemy-died",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_OBJECT);

    signals[SIGNAL_PLAYER_DAMAGED] =
        g_signal_new ("player-damaged",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_PLAYER_HEALED] =
        g_signal_new ("player-healed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_new:
 *
 * Creates a new combat template with default settings.
 *
 * Returns: (transfer full): a new #LrgDeckbuilderCombatTemplate
 */
LrgDeckbuilderCombatTemplate *
lrg_deckbuilder_combat_template_new (void)
{
    return g_object_new (LRG_TYPE_DECKBUILDER_COMBAT_TEMPLATE, NULL);
}

/* ==========================================================================
 * Public API - Combat Context
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_get_combat_context:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the current combat context.
 *
 * Returns: (transfer none) (nullable): the #LrgCombatContext
 */
LrgCombatContext *
lrg_deckbuilder_combat_template_get_combat_context (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), NULL);

    return self->combat_context;
}

/**
 * lrg_deckbuilder_combat_template_get_combat_rules:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the combat rules.
 *
 * Returns: (transfer none) (nullable): the #LrgCombatRules
 */
LrgCombatRules *
lrg_deckbuilder_combat_template_get_combat_rules (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), NULL);

    return self->combat_rules;
}

/**
 * lrg_deckbuilder_combat_template_set_combat_rules:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @rules: (transfer none): the combat rules
 *
 * Sets the combat rules to use for encounters.
 */
void
lrg_deckbuilder_combat_template_set_combat_rules (LrgDeckbuilderCombatTemplate *self,
                                                   LrgCombatRules               *rules)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self));

    if (self->combat_rules != rules)
    {
        g_clear_object (&self->combat_rules);
        if (rules != NULL)
            self->combat_rules = g_object_ref (rules);
    }
}

/* ==========================================================================
 * Public API - Player State
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_get_player:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the player combatant.
 *
 * Returns: (transfer none): the #LrgPlayerCombatant
 */
LrgPlayerCombatant *
lrg_deckbuilder_combat_template_get_player (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), NULL);

    return self->player;
}

/**
 * lrg_deckbuilder_combat_template_get_player_health:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the player's current health.
 *
 * Returns: current health
 */
gint
lrg_deckbuilder_combat_template_get_player_health (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);

    if (self->player == NULL)
        return 0;

    return lrg_combatant_get_current_health (LRG_COMBATANT (self->player));
}

/**
 * lrg_deckbuilder_combat_template_get_player_max_health:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the player's maximum health.
 *
 * Returns: max health
 */
gint
lrg_deckbuilder_combat_template_get_player_max_health (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);

    if (self->player == NULL)
        return 0;

    return lrg_combatant_get_max_health (LRG_COMBATANT (self->player));
}

/**
 * lrg_deckbuilder_combat_template_set_player_max_health:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @max_health: new maximum health
 *
 * Sets the player's maximum health.
 */
void
lrg_deckbuilder_combat_template_set_player_max_health (LrgDeckbuilderCombatTemplate *self,
                                                        gint                          max_health)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self));
    g_return_if_fail (max_health > 0);

    if (self->player != NULL)
    {
        lrg_player_combatant_set_max_health (self->player, max_health);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYER_MAX_HEALTH]);
    }
}

/**
 * lrg_deckbuilder_combat_template_get_player_block:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the player's current block.
 *
 * Returns: current block
 */
gint
lrg_deckbuilder_combat_template_get_player_block (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);

    if (self->player == NULL)
        return 0;

    return lrg_combatant_get_block (LRG_COMBATANT (self->player));
}

/**
 * lrg_deckbuilder_combat_template_add_player_block:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @amount: block to add
 *
 * Adds block to the player.
 *
 * Returns: actual block gained
 */
gint
lrg_deckbuilder_combat_template_add_player_block (LrgDeckbuilderCombatTemplate *self,
                                                   gint                          amount)
{
    gint gained;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);

    if (self->player == NULL)
        return 0;

    gained = lrg_combatant_add_block (LRG_COMBATANT (self->player), amount);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYER_BLOCK]);

    return gained;
}

/**
 * lrg_deckbuilder_combat_template_heal_player:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @amount: amount to heal
 *
 * Heals the player.
 *
 * Returns: actual amount healed
 */
gint
lrg_deckbuilder_combat_template_heal_player (LrgDeckbuilderCombatTemplate *self,
                                              gint                          amount)
{
    gint healed;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);

    if (self->player == NULL)
        return 0;

    healed = lrg_combatant_heal (LRG_COMBATANT (self->player), amount);
    if (healed > 0)
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYER_HEALTH]);
        g_signal_emit (self, signals[SIGNAL_PLAYER_HEALED], 0, healed);
    }

    return healed;
}

/**
 * lrg_deckbuilder_combat_template_damage_player:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @amount: damage to deal
 *
 * Deals damage to the player.
 *
 * Returns: actual damage taken
 */
gint
lrg_deckbuilder_combat_template_damage_player (LrgDeckbuilderCombatTemplate *self,
                                                gint                          amount)
{
    gint damage_taken;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);

    if (self->player == NULL)
        return 0;

    damage_taken = lrg_combatant_take_damage (LRG_COMBATANT (self->player), amount, 0);
    if (damage_taken > 0)
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYER_HEALTH]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYER_BLOCK]);
        g_signal_emit (self, signals[SIGNAL_PLAYER_DAMAGED], 0, damage_taken);
    }

    return damage_taken;
}

/* ==========================================================================
 * Public API - Enemy Management
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_add_enemy:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @enemy: (transfer full): enemy to add
 *
 * Adds an enemy to the current combat.
 */
void
lrg_deckbuilder_combat_template_add_enemy (LrgDeckbuilderCombatTemplate *self,
                                            LrgEnemyInstance             *enemy)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self));
    g_return_if_fail (LRG_IS_ENEMY_INSTANCE (enemy));

    if (self->combat_context == NULL)
        return;

    lrg_combat_context_add_enemy (self->combat_context, enemy);
    g_signal_emit (self, signals[SIGNAL_ENEMY_ADDED], 0, enemy);
}

/**
 * lrg_deckbuilder_combat_template_add_enemy_from_def:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @def: (transfer none): enemy definition
 *
 * Creates and adds an enemy from a definition.
 *
 * Returns: (transfer none): the created enemy
 */
LrgEnemyInstance *
lrg_deckbuilder_combat_template_add_enemy_from_def (LrgDeckbuilderCombatTemplate *self,
                                                     LrgEnemyDef                  *def)
{
    LrgEnemyInstance *enemy;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), NULL);
    g_return_val_if_fail (LRG_IS_ENEMY_DEF (def), NULL);

    if (self->combat_context == NULL)
        return NULL;

    enemy = lrg_enemy_instance_new (def);
    lrg_deckbuilder_combat_template_add_enemy (self, enemy);

    return enemy;
}

/**
 * lrg_deckbuilder_combat_template_remove_enemy:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @enemy: enemy to remove
 *
 * Removes an enemy from combat.
 */
void
lrg_deckbuilder_combat_template_remove_enemy (LrgDeckbuilderCombatTemplate *self,
                                               LrgEnemyInstance             *enemy)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self));
    g_return_if_fail (LRG_IS_ENEMY_INSTANCE (enemy));

    if (self->combat_context == NULL)
        return;

    /* Clear selected target if it's being removed */
    if (self->selected_target == enemy)
        self->selected_target = NULL;

    lrg_combat_context_remove_enemy (self->combat_context, enemy);
    g_signal_emit (self, signals[SIGNAL_ENEMY_REMOVED], 0, enemy);
}

/**
 * lrg_deckbuilder_combat_template_get_enemies:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets all enemies in the current combat.
 *
 * Returns: (transfer none) (element-type LrgEnemyInstance): enemy list
 */
GPtrArray *
lrg_deckbuilder_combat_template_get_enemies (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), NULL);

    if (self->combat_context == NULL)
        return NULL;

    return lrg_combat_context_get_enemies (self->combat_context);
}

/**
 * lrg_deckbuilder_combat_template_get_enemy_count:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the number of enemies in combat.
 *
 * Returns: enemy count
 */
guint
lrg_deckbuilder_combat_template_get_enemy_count (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);

    if (self->combat_context == NULL)
        return 0;

    return lrg_combat_context_get_enemy_count (self->combat_context);
}

/**
 * lrg_deckbuilder_combat_template_get_enemy_at:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @index: enemy index
 *
 * Gets an enemy by index.
 *
 * Returns: (transfer none) (nullable): the enemy, or %NULL
 */
LrgEnemyInstance *
lrg_deckbuilder_combat_template_get_enemy_at (LrgDeckbuilderCombatTemplate *self,
                                               guint                         index)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), NULL);

    if (self->combat_context == NULL)
        return NULL;

    return lrg_combat_context_get_enemy_at (self->combat_context, index);
}

/**
 * lrg_deckbuilder_combat_template_get_alive_enemy_count:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the number of living enemies.
 *
 * Returns: number of alive enemies
 */
guint
lrg_deckbuilder_combat_template_get_alive_enemy_count (LrgDeckbuilderCombatTemplate *self)
{
    GPtrArray *enemies;
    guint alive_count = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);

    enemies = lrg_deckbuilder_combat_template_get_enemies (self);
    if (enemies == NULL)
        return 0;

    for (i = 0; i < enemies->len; i++)
    {
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i);
        if (lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
            alive_count++;
    }

    return alive_count;
}

/**
 * lrg_deckbuilder_combat_template_damage_enemy:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @enemy: target enemy
 * @amount: damage to deal
 *
 * Deals damage to an enemy.
 *
 * Returns: actual damage dealt
 */
gint
lrg_deckbuilder_combat_template_damage_enemy (LrgDeckbuilderCombatTemplate *self,
                                               LrgEnemyInstance             *enemy,
                                               gint                          amount)
{
    gint damage_taken;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);
    g_return_val_if_fail (LRG_IS_ENEMY_INSTANCE (enemy), 0);

    damage_taken = lrg_combatant_take_damage (LRG_COMBATANT (enemy), amount, 0);

    /* Check for death */
    if (!lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
    {
        g_signal_emit (self, signals[SIGNAL_ENEMY_DIED], 0, enemy);
    }

    return damage_taken;
}

/**
 * lrg_deckbuilder_combat_template_damage_all_enemies:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @amount: damage to deal to each enemy
 *
 * Deals damage to all enemies.
 *
 * Returns: total damage dealt
 */
gint
lrg_deckbuilder_combat_template_damage_all_enemies (LrgDeckbuilderCombatTemplate *self,
                                                     gint                          amount)
{
    GPtrArray *enemies;
    gint total_damage = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), 0);

    enemies = lrg_deckbuilder_combat_template_get_enemies (self);
    if (enemies == NULL)
        return 0;

    for (i = 0; i < enemies->len; i++)
    {
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i);
        if (lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
            total_damage += lrg_deckbuilder_combat_template_damage_enemy (self, enemy, amount);
    }

    return total_damage;
}

/* ==========================================================================
 * Public API - Target Selection
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_get_selected_target:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the currently selected target.
 *
 * Returns: (transfer none) (nullable): the selected enemy
 */
LrgEnemyInstance *
lrg_deckbuilder_combat_template_get_selected_target (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), NULL);

    return self->selected_target;
}

/**
 * lrg_deckbuilder_combat_template_set_selected_target:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @target: (nullable): the enemy to select
 *
 * Sets the selected target.
 */
void
lrg_deckbuilder_combat_template_set_selected_target (LrgDeckbuilderCombatTemplate *self,
                                                      LrgEnemyInstance             *target)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self));

    self->selected_target = target;
}

/**
 * lrg_deckbuilder_combat_template_get_random_enemy:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets a random living enemy.
 *
 * Returns: (transfer none) (nullable): a random enemy
 */
LrgEnemyInstance *
lrg_deckbuilder_combat_template_get_random_enemy (LrgDeckbuilderCombatTemplate *self)
{
    GPtrArray *enemies;
    GPtrArray *alive;
    GRand *rng;
    LrgEnemyInstance *result = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), NULL);

    enemies = lrg_deckbuilder_combat_template_get_enemies (self);
    if (enemies == NULL || enemies->len == 0)
        return NULL;

    /* Build list of alive enemies */
    alive = g_ptr_array_new ();
    for (i = 0; i < enemies->len; i++)
    {
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i);
        if (lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
            g_ptr_array_add (alive, enemy);
    }

    if (alive->len > 0)
    {
        rng = lrg_combat_context_get_rng (self->combat_context);
        i = g_rand_int_range (rng, 0, alive->len);
        result = g_ptr_array_index (alive, i);
    }

    g_ptr_array_unref (alive);
    return result;
}

/* ==========================================================================
 * Public API - Combat Flow
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_is_in_combat:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Checks if currently in combat.
 *
 * Returns: %TRUE if in combat
 */
gboolean
lrg_deckbuilder_combat_template_is_in_combat (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), FALSE);

    return self->in_combat;
}

/**
 * lrg_deckbuilder_combat_template_start_combat:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Starts a new combat encounter.
 */
void
lrg_deckbuilder_combat_template_start_combat (LrgDeckbuilderCombatTemplate *self)
{
    LrgDeckInstance *deck;

    g_return_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self));

    /* Create combat context */
    g_clear_object (&self->combat_context);
    self->combat_context = lrg_combat_context_new (self->player, self->combat_rules);

    /* Set up deck */
    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck != NULL)
        lrg_deck_instance_setup (deck);

    /* Reset state */
    self->in_combat = TRUE;
    self->combat_result = LRG_COMBAT_RESULT_IN_PROGRESS;
    self->selected_target = NULL;
    self->current_enemy_index = 0;
    self->enemy_turns_complete = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IN_COMBAT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMBAT_RESULT]);
    g_signal_emit (self, signals[SIGNAL_COMBAT_STARTED], 0);

    /* Start first turn */
    lrg_deckbuilder_template_start_turn (LRG_DECKBUILDER_TEMPLATE (self));

    lrg_info (LRG_LOG_DOMAIN_TEMPLATE, "Combat started");
}

/**
 * lrg_deckbuilder_combat_template_end_combat:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @result: the combat result
 *
 * Ends the current combat encounter.
 */
void
lrg_deckbuilder_combat_template_end_combat (LrgDeckbuilderCombatTemplate *self,
                                             LrgCombatResult               result)
{
    LrgDeckInstance *deck;

    g_return_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self));

    self->in_combat = FALSE;
    self->combat_result = result;
    self->selected_target = NULL;

    /* Clean up deck */
    deck = lrg_deckbuilder_template_get_deck_instance (LRG_DECKBUILDER_TEMPLATE (self));
    if (deck != NULL)
        lrg_deck_instance_end_combat (deck);

    /* Clear combat context */
    g_clear_object (&self->combat_context);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IN_COMBAT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMBAT_RESULT]);
    g_signal_emit (self, signals[SIGNAL_COMBAT_ENDED], 0, result);

    lrg_info (LRG_LOG_DOMAIN_TEMPLATE, "Combat ended with result %d", result);
}

/**
 * lrg_deckbuilder_combat_template_get_combat_result:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the current combat result.
 *
 * Returns: the #LrgCombatResult
 */
LrgCombatResult
lrg_deckbuilder_combat_template_get_combat_result (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), LRG_COMBAT_RESULT_IN_PROGRESS);

    return self->combat_result;
}

/**
 * lrg_deckbuilder_combat_template_end_player_turn:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Ends the player's turn and prepares for enemy turn.
 */
void
lrg_deckbuilder_combat_template_end_player_turn (LrgDeckbuilderCombatTemplate *self)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self));

    if (!self->in_combat)
        return;

    /* End the current turn */
    lrg_deckbuilder_template_end_turn (LRG_DECKBUILDER_TEMPLATE (self));

    /* Prepare for enemy turns */
    self->current_enemy_index = 0;
    self->enemy_turns_complete = FALSE;
}

/**
 * lrg_deckbuilder_combat_template_process_enemy_turns:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Processes enemy actions.
 *
 * Returns: %TRUE if enemy turns are complete
 */
gboolean
lrg_deckbuilder_combat_template_process_enemy_turns (LrgDeckbuilderCombatTemplate *self)
{
    GPtrArray *enemies;
    LrgEnemyInstance *enemy;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), TRUE);

    if (!self->in_combat || self->enemy_turns_complete)
        return TRUE;

    enemies = lrg_deckbuilder_combat_template_get_enemies (self);
    if (enemies == NULL || self->current_enemy_index >= enemies->len)
    {
        /* All enemies processed, start next player turn */
        self->enemy_turns_complete = TRUE;
        remove_dead_enemies (self);

        /* Check if combat should end */
        if (lrg_deckbuilder_combat_template_check_combat_end (self) == LRG_COMBAT_RESULT_IN_PROGRESS)
        {
            lrg_deckbuilder_template_start_turn (LRG_DECKBUILDER_TEMPLATE (self));
        }

        return TRUE;
    }

    /* Process current enemy */
    enemy = g_ptr_array_index (enemies, self->current_enemy_index);
    if (lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
    {
        /* Execute the enemy's current intent (attack, buff, debuff, etc.) */
        lrg_enemy_instance_execute_intent (enemy, self->combat_context);
    }

    self->current_enemy_index++;
    return FALSE;
}

/**
 * lrg_deckbuilder_combat_template_check_combat_end:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Checks if combat should end.
 *
 * Returns: the #LrgCombatResult
 */
LrgCombatResult
lrg_deckbuilder_combat_template_check_combat_end (LrgDeckbuilderCombatTemplate *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), LRG_COMBAT_RESULT_IN_PROGRESS);

    if (!self->in_combat)
        return self->combat_result;

    /* Check for player death */
    if (!lrg_combatant_is_alive (LRG_COMBATANT (self->player)))
    {
        lrg_deckbuilder_combat_template_end_combat (self, LRG_COMBAT_RESULT_DEFEAT);
        return LRG_COMBAT_RESULT_DEFEAT;
    }

    /* Check for victory (all enemies dead) */
    if (lrg_deckbuilder_combat_template_get_alive_enemy_count (self) == 0)
    {
        lrg_deckbuilder_combat_template_end_combat (self, LRG_COMBAT_RESULT_VICTORY);
        return LRG_COMBAT_RESULT_VICTORY;
    }

    return LRG_COMBAT_RESULT_IN_PROGRESS;
}

/* ==========================================================================
 * Public API - Status Effects
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_apply_status_to_player:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @status_id: status effect ID
 * @stacks: number of stacks
 *
 * Applies a status effect to the player.
 *
 * Returns: %TRUE if applied
 */
gboolean
lrg_deckbuilder_combat_template_apply_status_to_player (LrgDeckbuilderCombatTemplate *self,
                                                         const gchar                  *status_id,
                                                         gint                          stacks)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), FALSE);
    g_return_val_if_fail (status_id != NULL, FALSE);

    if (self->player == NULL)
        return FALSE;

    return lrg_combatant_apply_status (LRG_COMBATANT (self->player), status_id, stacks);
}

/**
 * lrg_deckbuilder_combat_template_apply_status_to_enemy:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @enemy: target enemy
 * @status_id: status effect ID
 * @stacks: number of stacks
 *
 * Applies a status effect to an enemy.
 *
 * Returns: %TRUE if applied
 */
gboolean
lrg_deckbuilder_combat_template_apply_status_to_enemy (LrgDeckbuilderCombatTemplate *self,
                                                        LrgEnemyInstance             *enemy,
                                                        const gchar                  *status_id,
                                                        gint                          stacks)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_ENEMY_INSTANCE (enemy), FALSE);
    g_return_val_if_fail (status_id != NULL, FALSE);

    return lrg_combatant_apply_status (LRG_COMBATANT (enemy), status_id, stacks);
}

/**
 * lrg_deckbuilder_combat_template_apply_status_to_all_enemies:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @status_id: status effect ID
 * @stacks: number of stacks
 *
 * Applies a status effect to all enemies.
 */
void
lrg_deckbuilder_combat_template_apply_status_to_all_enemies (LrgDeckbuilderCombatTemplate *self,
                                                              const gchar                  *status_id,
                                                              gint                          stacks)
{
    GPtrArray *enemies;
    guint i;

    g_return_if_fail (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (self));
    g_return_if_fail (status_id != NULL);

    enemies = lrg_deckbuilder_combat_template_get_enemies (self);
    if (enemies == NULL)
        return;

    for (i = 0; i < enemies->len; i++)
    {
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i);
        if (lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
            lrg_combatant_apply_status (LRG_COMBATANT (enemy), status_id, stacks);
    }
}
