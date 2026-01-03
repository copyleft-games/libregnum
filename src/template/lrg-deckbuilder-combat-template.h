/* lrg-deckbuilder-combat-template.h - Combat-focused deckbuilder template
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgDeckbuilderCombatTemplate is a final template specialized for
 * Slay the Spire-style deckbuilder combat. It extends LrgDeckbuilderTemplate
 * with combat context integration, enemy management, and player health/block.
 *
 * ## Features
 *
 * - **Combat Context**: Integrates with LrgCombatContext for state management
 * - **Enemy Management**: Add, remove, and target enemies
 * - **Player State**: Health, block, and status effects via LrgPlayerCombatant
 * - **Turn Flow**: Player turn -> Enemy turn cycle with proper hooks
 * - **Combat Phases**: Setup, player, enemy, and end phases
 *
 * ## Usage
 *
 * ```c
 * LrgDeckbuilderCombatTemplate *combat = lrg_deckbuilder_combat_template_new ();
 *
 * // Set up player
 * lrg_deckbuilder_combat_template_set_max_health (combat, 80);
 *
 * // Start a combat encounter
 * lrg_deckbuilder_combat_template_start_combat (combat);
 *
 * // Add an enemy
 * LrgEnemyDef *def = lrg_enemy_def_load ("enemies/cultist.yaml");
 * lrg_deckbuilder_combat_template_add_enemy_from_def (combat, def);
 *
 * // Game loop handles turn flow via update
 * ```
 *
 * Since: 1.0
 */

#ifndef LRG_DECKBUILDER_COMBAT_TEMPLATE_H
#define LRG_DECKBUILDER_COMBAT_TEMPLATE_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-deckbuilder-template.h"

G_BEGIN_DECLS

/* Forward declarations */
typedef struct _LrgCombatContext    LrgCombatContext;
typedef struct _LrgPlayerCombatant  LrgPlayerCombatant;
typedef struct _LrgEnemyInstance    LrgEnemyInstance;
typedef struct _LrgEnemyDef         LrgEnemyDef;
typedef struct _LrgCombatRules      LrgCombatRules;

/* LrgCombatResult is defined in lrg-enums.h */

#define LRG_TYPE_DECKBUILDER_COMBAT_TEMPLATE (lrg_deckbuilder_combat_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDeckbuilderCombatTemplate, lrg_deckbuilder_combat_template,
                      LRG, DECKBUILDER_COMBAT_TEMPLATE, LrgDeckbuilderTemplate)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_new:
 *
 * Creates a new combat template with default settings.
 *
 * Returns: (transfer full): a new #LrgDeckbuilderCombatTemplate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDeckbuilderCombatTemplate *
lrg_deckbuilder_combat_template_new (void);

/* ==========================================================================
 * Combat Context
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_get_combat_context:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the current combat context.
 *
 * Returns: (transfer none) (nullable): the #LrgCombatContext, or %NULL if not in combat
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCombatContext *
lrg_deckbuilder_combat_template_get_combat_context (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_get_combat_rules:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the combat rules.
 *
 * Returns: (transfer none) (nullable): the #LrgCombatRules
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCombatRules *
lrg_deckbuilder_combat_template_get_combat_rules (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_set_combat_rules:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @rules: (transfer none): the combat rules
 *
 * Sets the combat rules to use for encounters.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_combat_template_set_combat_rules (LrgDeckbuilderCombatTemplate *self,
                                                   LrgCombatRules               *rules);

/* ==========================================================================
 * Player State
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_get_player:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the player combatant.
 *
 * Returns: (transfer none): the #LrgPlayerCombatant
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlayerCombatant *
lrg_deckbuilder_combat_template_get_player (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_get_player_health:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the player's current health.
 *
 * Returns: current health
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_combat_template_get_player_health (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_get_player_max_health:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the player's maximum health.
 *
 * Returns: max health
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_combat_template_get_player_max_health (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_set_player_max_health:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @max_health: new maximum health
 *
 * Sets the player's maximum health.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_combat_template_set_player_max_health (LrgDeckbuilderCombatTemplate *self,
                                                        gint                          max_health);

/**
 * lrg_deckbuilder_combat_template_get_player_block:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the player's current block.
 *
 * Returns: current block
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_combat_template_get_player_block (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_add_player_block:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @amount: block to add
 *
 * Adds block to the player.
 *
 * Returns: actual block gained
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_combat_template_add_player_block (LrgDeckbuilderCombatTemplate *self,
                                                   gint                          amount);

/**
 * lrg_deckbuilder_combat_template_heal_player:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @amount: amount to heal
 *
 * Heals the player.
 *
 * Returns: actual amount healed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_combat_template_heal_player (LrgDeckbuilderCombatTemplate *self,
                                              gint                          amount);

/**
 * lrg_deckbuilder_combat_template_damage_player:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @amount: damage to deal
 *
 * Deals damage to the player.
 *
 * Returns: actual damage taken
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_combat_template_damage_player (LrgDeckbuilderCombatTemplate *self,
                                                gint                          amount);

/* ==========================================================================
 * Enemy Management
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_add_enemy:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @enemy: (transfer full): enemy to add
 *
 * Adds an enemy to the current combat.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_combat_template_add_enemy (LrgDeckbuilderCombatTemplate *self,
                                            LrgEnemyInstance             *enemy);

/**
 * lrg_deckbuilder_combat_template_add_enemy_from_def:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @def: (transfer none): enemy definition
 *
 * Creates and adds an enemy from a definition.
 *
 * Returns: (transfer none): the created enemy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyInstance *
lrg_deckbuilder_combat_template_add_enemy_from_def (LrgDeckbuilderCombatTemplate *self,
                                                     LrgEnemyDef                  *def);

/**
 * lrg_deckbuilder_combat_template_remove_enemy:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @enemy: enemy to remove
 *
 * Removes an enemy from combat.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_combat_template_remove_enemy (LrgDeckbuilderCombatTemplate *self,
                                               LrgEnemyInstance             *enemy);

/**
 * lrg_deckbuilder_combat_template_get_enemies:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets all enemies in the current combat.
 *
 * Returns: (transfer none) (element-type LrgEnemyInstance): enemy list
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_deckbuilder_combat_template_get_enemies (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_get_enemy_count:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the number of enemies in combat.
 *
 * Returns: enemy count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_combat_template_get_enemy_count (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_get_enemy_at:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @index: enemy index
 *
 * Gets an enemy by index.
 *
 * Returns: (transfer none) (nullable): the enemy, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyInstance *
lrg_deckbuilder_combat_template_get_enemy_at (LrgDeckbuilderCombatTemplate *self,
                                               guint                         index);

/**
 * lrg_deckbuilder_combat_template_get_alive_enemy_count:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the number of living enemies.
 *
 * Returns: number of alive enemies
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_combat_template_get_alive_enemy_count (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_damage_enemy:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @enemy: target enemy
 * @amount: damage to deal
 *
 * Deals damage to an enemy.
 *
 * Returns: actual damage dealt
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_combat_template_damage_enemy (LrgDeckbuilderCombatTemplate *self,
                                               LrgEnemyInstance             *enemy,
                                               gint                          amount);

/**
 * lrg_deckbuilder_combat_template_damage_all_enemies:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @amount: damage to deal to each enemy
 *
 * Deals damage to all enemies.
 *
 * Returns: total damage dealt
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_combat_template_damage_all_enemies (LrgDeckbuilderCombatTemplate *self,
                                                     gint                          amount);

/* ==========================================================================
 * Target Selection
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_get_selected_target:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the currently selected target (for card targeting).
 *
 * Returns: (transfer none) (nullable): the selected enemy, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyInstance *
lrg_deckbuilder_combat_template_get_selected_target (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_set_selected_target:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @target: (nullable): the enemy to select
 *
 * Sets the selected target for card effects.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_combat_template_set_selected_target (LrgDeckbuilderCombatTemplate *self,
                                                      LrgEnemyInstance             *target);

/**
 * lrg_deckbuilder_combat_template_get_random_enemy:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets a random living enemy.
 *
 * Returns: (transfer none) (nullable): a random enemy, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyInstance *
lrg_deckbuilder_combat_template_get_random_enemy (LrgDeckbuilderCombatTemplate *self);

/* ==========================================================================
 * Combat Flow
 * ========================================================================== */

/**
 * lrg_deckbuilder_combat_template_is_in_combat:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Checks if currently in combat.
 *
 * Returns: %TRUE if in combat
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_combat_template_is_in_combat (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_start_combat:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Starts a new combat encounter. Sets up deck, clears enemies.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_combat_template_start_combat (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_end_combat:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @result: the combat result
 *
 * Ends the current combat encounter.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_combat_template_end_combat (LrgDeckbuilderCombatTemplate *self,
                                             LrgCombatResult               result);

/**
 * lrg_deckbuilder_combat_template_get_combat_result:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Gets the current combat result.
 *
 * Returns: the #LrgCombatResult
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCombatResult
lrg_deckbuilder_combat_template_get_combat_result (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_end_player_turn:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Ends the player's turn and starts enemy turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_combat_template_end_player_turn (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_process_enemy_turns:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Processes all enemy actions. Call after end_player_turn.
 * This may be called multiple times for animation/delay purposes.
 *
 * Returns: %TRUE if enemy turns are complete
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_combat_template_process_enemy_turns (LrgDeckbuilderCombatTemplate *self);

/**
 * lrg_deckbuilder_combat_template_check_combat_end:
 * @self: an #LrgDeckbuilderCombatTemplate
 *
 * Checks if combat should end (victory or defeat).
 *
 * Returns: the #LrgCombatResult
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCombatResult
lrg_deckbuilder_combat_template_check_combat_end (LrgDeckbuilderCombatTemplate *self);

/* ==========================================================================
 * Status Effects
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
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_combat_template_apply_status_to_player (LrgDeckbuilderCombatTemplate *self,
                                                         const gchar                  *status_id,
                                                         gint                          stacks);

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
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_combat_template_apply_status_to_enemy (LrgDeckbuilderCombatTemplate *self,
                                                        LrgEnemyInstance             *enemy,
                                                        const gchar                  *status_id,
                                                        gint                          stacks);

/**
 * lrg_deckbuilder_combat_template_apply_status_to_all_enemies:
 * @self: an #LrgDeckbuilderCombatTemplate
 * @status_id: status effect ID
 * @stacks: number of stacks
 *
 * Applies a status effect to all enemies.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_combat_template_apply_status_to_all_enemies (LrgDeckbuilderCombatTemplate *self,
                                                              const gchar                  *status_id,
                                                              gint                          stacks);

G_END_DECLS

#endif /* LRG_DECKBUILDER_COMBAT_TEMPLATE_H */
