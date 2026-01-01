/* lrg-enemy-intent.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_ENEMY_INTENT_H
#define LRG_ENEMY_INTENT_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_ENEMY_INTENT (lrg_enemy_intent_get_type ())

/**
 * LrgEnemyIntent:
 *
 * Represents an enemy's intended action for their turn.
 * This is displayed to the player to allow strategic planning.
 *
 * Intents can represent:
 * - Attacks (damage amount shown)
 * - Multi-attacks (damage × times)
 * - Defending (block amount)
 * - Buffs/debuffs (status effects)
 * - Special actions (escape, summon, etc.)
 *
 * Since: 1.0
 */
typedef struct _LrgEnemyIntent LrgEnemyIntent;

LRG_AVAILABLE_IN_ALL
GType
lrg_enemy_intent_get_type (void) G_GNUC_CONST;

/**
 * lrg_enemy_intent_new:
 * @type: the intent type
 *
 * Creates a new enemy intent.
 *
 * Returns: (transfer full): a new #LrgEnemyIntent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyIntent *
lrg_enemy_intent_new (LrgIntentType type);

/**
 * lrg_enemy_intent_new_attack:
 * @damage: base damage amount
 * @times: number of attacks (1 for single hit)
 *
 * Creates an attack intent.
 *
 * Returns: (transfer full): a new attack #LrgEnemyIntent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyIntent *
lrg_enemy_intent_new_attack (gint damage,
                             gint times);

/**
 * lrg_enemy_intent_new_defend:
 * @block: block amount
 *
 * Creates a defend intent.
 *
 * Returns: (transfer full): a new defend #LrgEnemyIntent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyIntent *
lrg_enemy_intent_new_defend (gint block);

/**
 * lrg_enemy_intent_new_buff:
 * @status_id: (nullable): status effect to apply
 * @stacks: number of stacks
 *
 * Creates a buff intent (enemy buffing self).
 *
 * Returns: (transfer full): a new buff #LrgEnemyIntent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyIntent *
lrg_enemy_intent_new_buff (const gchar *status_id,
                           gint         stacks);

/**
 * lrg_enemy_intent_new_debuff:
 * @status_id: (nullable): status effect to apply
 * @stacks: number of stacks
 *
 * Creates a debuff intent (enemy debuffing player).
 *
 * Returns: (transfer full): a new debuff #LrgEnemyIntent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyIntent *
lrg_enemy_intent_new_debuff (const gchar *status_id,
                             gint         stacks);

LRG_AVAILABLE_IN_ALL
LrgEnemyIntent *
lrg_enemy_intent_copy (const LrgEnemyIntent *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_intent_free (LrgEnemyIntent *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgEnemyIntent, lrg_enemy_intent_free)

/* Accessors */

LRG_AVAILABLE_IN_ALL
LrgIntentType
lrg_enemy_intent_get_intent_type (const LrgEnemyIntent *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_intent_set_intent_type (LrgEnemyIntent *self,
                                  LrgIntentType   type);

/**
 * lrg_enemy_intent_get_damage:
 * @self: an #LrgEnemyIntent
 *
 * Gets the base damage for attack intents.
 *
 * Returns: base damage amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_enemy_intent_get_damage (const LrgEnemyIntent *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_intent_set_damage (LrgEnemyIntent *self,
                             gint            damage);

/**
 * lrg_enemy_intent_get_times:
 * @self: an #LrgEnemyIntent
 *
 * Gets the number of times an attack hits.
 *
 * Returns: number of hits (1 = single attack)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_enemy_intent_get_times (const LrgEnemyIntent *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_intent_set_times (LrgEnemyIntent *self,
                            gint            times);

/**
 * lrg_enemy_intent_get_block:
 * @self: an #LrgEnemyIntent
 *
 * Gets the block amount for defend intents.
 *
 * Returns: block amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_enemy_intent_get_block (const LrgEnemyIntent *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_intent_set_block (LrgEnemyIntent *self,
                            gint            block);

/**
 * lrg_enemy_intent_get_status_id:
 * @self: an #LrgEnemyIntent
 *
 * Gets the status effect ID for buff/debuff intents.
 *
 * Returns: (transfer none) (nullable): status effect ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_enemy_intent_get_status_id (const LrgEnemyIntent *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_intent_set_status_id (LrgEnemyIntent *self,
                                const gchar    *status_id);

/**
 * lrg_enemy_intent_get_stacks:
 * @self: an #LrgEnemyIntent
 *
 * Gets the number of stacks for buff/debuff intents.
 *
 * Returns: number of stacks
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_enemy_intent_get_stacks (const LrgEnemyIntent *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_intent_set_stacks (LrgEnemyIntent *self,
                             gint            stacks);

/**
 * lrg_enemy_intent_is_attack:
 * @self: an #LrgEnemyIntent
 *
 * Checks if this intent involves attacking.
 *
 * Returns: %TRUE if intent type is attack or attack combo
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_enemy_intent_is_attack (const LrgEnemyIntent *self);

/**
 * lrg_enemy_intent_get_total_damage:
 * @self: an #LrgEnemyIntent
 *
 * Gets the total damage for multi-hit attacks.
 *
 * Returns: damage × times, or 0 for non-attacks
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_enemy_intent_get_total_damage (const LrgEnemyIntent *self);

G_END_DECLS

#endif /* LRG_ENEMY_INTENT_H */
