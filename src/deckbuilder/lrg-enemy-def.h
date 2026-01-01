/* lrg-enemy-def.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_ENEMY_DEF_H
#define LRG_ENEMY_DEF_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-enemy-intent.h"

G_BEGIN_DECLS

#define LRG_TYPE_ENEMY_DEF (lrg_enemy_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgEnemyDef, lrg_enemy_def, LRG, ENEMY_DEF, GObject)

/* Forward declaration */
typedef struct _LrgEnemyInstance LrgEnemyInstance;
typedef struct _LrgCombatContext LrgCombatContext;

/**
 * LrgEnemyDefClass:
 * @parent_class: parent class
 * @decide_intent: decide next intent for an instance
 * @execute_intent: execute the current intent
 * @on_spawn: called when enemy spawns
 * @on_death: called when enemy dies
 *
 * Class structure for enemy definitions.
 * Subclass to create custom enemy behaviors.
 *
 * Since: 1.0
 */
struct _LrgEnemyDefClass
{
    GObjectClass parent_class;

    /* Virtual methods for AI behavior */
    LrgEnemyIntent * (* decide_intent)  (LrgEnemyDef      *self,
                                         LrgEnemyInstance *instance,
                                         LrgCombatContext *context);
    void             (* execute_intent) (LrgEnemyDef      *self,
                                         LrgEnemyInstance *instance,
                                         LrgCombatContext *context);

    /* Lifecycle hooks */
    void             (* on_spawn)       (LrgEnemyDef      *self,
                                         LrgEnemyInstance *instance,
                                         LrgCombatContext *context);
    void             (* on_death)       (LrgEnemyDef      *self,
                                         LrgEnemyInstance *instance,
                                         LrgCombatContext *context);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_enemy_def_new:
 * @id: unique identifier
 * @name: display name
 *
 * Creates a new enemy definition.
 *
 * Returns: (transfer full): a new #LrgEnemyDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyDef *
lrg_enemy_def_new (const gchar *id,
                   const gchar *name);

/* Property accessors */

LRG_AVAILABLE_IN_ALL
const gchar *
lrg_enemy_def_get_id (LrgEnemyDef *self);

LRG_AVAILABLE_IN_ALL
const gchar *
lrg_enemy_def_get_name (LrgEnemyDef *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_set_name (LrgEnemyDef *self,
                        const gchar *name);

LRG_AVAILABLE_IN_ALL
const gchar *
lrg_enemy_def_get_description (LrgEnemyDef *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_set_description (LrgEnemyDef *self,
                               const gchar *description);

LRG_AVAILABLE_IN_ALL
LrgEnemyType
lrg_enemy_def_get_enemy_type (LrgEnemyDef *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_set_enemy_type (LrgEnemyDef  *self,
                              LrgEnemyType  type);

/**
 * lrg_enemy_def_get_base_health:
 * @self: an #LrgEnemyDef
 *
 * Gets the base health for this enemy type.
 * Actual health may vary by ascension level.
 *
 * Returns: base health value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_enemy_def_get_base_health (LrgEnemyDef *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_set_base_health (LrgEnemyDef *self,
                               gint         health);

/**
 * lrg_enemy_def_get_health_variance:
 * @self: an #LrgEnemyDef
 *
 * Gets the health variance for this enemy type.
 * Actual health = base_health ± variance
 *
 * Returns: health variance value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_enemy_def_get_health_variance (LrgEnemyDef *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_set_health_variance (LrgEnemyDef *self,
                                   gint         variance);

LRG_AVAILABLE_IN_ALL
const gchar *
lrg_enemy_def_get_icon (LrgEnemyDef *self);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_set_icon (LrgEnemyDef *self,
                        const gchar *icon);

/* AI Methods */

/**
 * lrg_enemy_def_decide_intent:
 * @self: an #LrgEnemyDef
 * @instance: the enemy instance
 * @context: the combat context
 *
 * Determines what action this enemy will take on their next turn.
 * The returned intent is displayed to the player.
 *
 * Returns: (transfer full): the decided #LrgEnemyIntent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyIntent *
lrg_enemy_def_decide_intent (LrgEnemyDef      *self,
                             LrgEnemyInstance *instance,
                             LrgCombatContext *context);

/**
 * lrg_enemy_def_execute_intent:
 * @self: an #LrgEnemyDef
 * @instance: the enemy instance
 * @context: the combat context
 *
 * Executes the enemy's current intent.
 * Called when it's the enemy's turn to act.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_execute_intent (LrgEnemyDef      *self,
                              LrgEnemyInstance *instance,
                              LrgCombatContext *context);

/* Lifecycle hooks */

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_on_spawn (LrgEnemyDef      *self,
                        LrgEnemyInstance *instance,
                        LrgCombatContext *context);

LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_on_death (LrgEnemyDef      *self,
                        LrgEnemyInstance *instance,
                        LrgCombatContext *context);

/* Intent Pattern Helpers */

/**
 * lrg_enemy_def_add_intent_pattern:
 * @self: an #LrgEnemyDef
 * @intent: (transfer full): intent to add to pattern
 * @weight: selection weight
 *
 * Adds an intent to the weighted selection pool.
 * Used by the default decide_intent implementation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_add_intent_pattern (LrgEnemyDef    *self,
                                  LrgEnemyIntent *intent,
                                  gint            weight);

/**
 * lrg_enemy_def_clear_intent_patterns:
 * @self: an #LrgEnemyDef
 *
 * Clears all intent patterns.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_enemy_def_clear_intent_patterns (LrgEnemyDef *self);

G_END_DECLS

#endif /* LRG_ENEMY_DEF_H */
