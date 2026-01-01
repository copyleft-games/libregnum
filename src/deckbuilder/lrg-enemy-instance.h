/* lrg-enemy-instance.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_ENEMY_INSTANCE_H
#define LRG_ENEMY_INSTANCE_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-combatant.h"
#include "lrg-enemy-def.h"
#include "lrg-enemy-intent.h"

G_BEGIN_DECLS

#define LRG_TYPE_ENEMY_INSTANCE (lrg_enemy_instance_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEnemyInstance, lrg_enemy_instance, LRG, ENEMY_INSTANCE, GObject)

/**
 * lrg_enemy_instance_new:
 * @def: the enemy definition
 *
 * Creates a new enemy instance from a definition.
 * Health is randomized based on base_health ± variance.
 *
 * Returns: (transfer full): a new #LrgEnemyInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyInstance *
lrg_enemy_instance_new (LrgEnemyDef *def);

/**
 * lrg_enemy_instance_new_with_health:
 * @def: the enemy definition
 * @max_health: specific max health value
 *
 * Creates a new enemy instance with specific health.
 *
 * Returns: (transfer full): a new #LrgEnemyInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyInstance *
lrg_enemy_instance_new_with_health (LrgEnemyDef *def,
                                    gint         max_health);

/**
 * lrg_enemy_instance_get_def:
 * @self: an #LrgEnemyInstance
 *
 * Gets the enemy definition for this instance.
 *
 * Returns: (transfer none): the #LrgEnemyDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyDef *
lrg_enemy_instance_get_def (LrgEnemyInstance *self);

/* Intent management */

/**
 * lrg_enemy_instance_get_intent:
 * @self: an #LrgEnemyInstance
 *
 * Gets the current intent for this enemy.
 *
 * Returns: (transfer none) (nullable): current #LrgEnemyIntent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgEnemyIntent *
lrg_enemy_instance_get_intent (LrgEnemyInstance *self);

/**
 * lrg_enemy_instance_set_intent:
 * @self: an #LrgEnemyInstance
 * @intent: (transfer full) (nullable): the new intent
 *
 * Sets the current intent for this enemy.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_enemy_instance_set_intent (LrgEnemyInstance *self,
                               LrgEnemyIntent   *intent);

/**
 * lrg_enemy_instance_decide_intent:
 * @self: an #LrgEnemyInstance
 * @context: (nullable): the combat context
 *
 * Asks the enemy's AI to decide a new intent.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_enemy_instance_decide_intent (LrgEnemyInstance *self,
                                  LrgCombatContext *context);

/**
 * lrg_enemy_instance_execute_intent:
 * @self: an #LrgEnemyInstance
 * @context: (nullable): the combat context
 *
 * Executes the current intent.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_enemy_instance_execute_intent (LrgEnemyInstance *self,
                                   LrgCombatContext *context);

/* Turn tracking */

/**
 * lrg_enemy_instance_get_turn_count:
 * @self: an #LrgEnemyInstance
 *
 * Gets how many turns this enemy has taken.
 *
 * Returns: number of turns
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_enemy_instance_get_turn_count (LrgEnemyInstance *self);

/**
 * lrg_enemy_instance_increment_turn:
 * @self: an #LrgEnemyInstance
 *
 * Increments the turn counter.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_enemy_instance_increment_turn (LrgEnemyInstance *self);

/* Custom data storage */

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_instance_set_data (LrgEnemyInstance *self,
                             const gchar      *key,
                             gpointer          data,
                             GDestroyNotify    destroy);

LRG_AVAILABLE_IN_ALL
gpointer
lrg_enemy_instance_get_data (LrgEnemyInstance *self,
                             const gchar      *key);

G_END_DECLS

#endif /* LRG_ENEMY_INSTANCE_H */
