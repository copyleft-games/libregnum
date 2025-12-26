/* lrg-quest-instance.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Active quest instance tracking player progress.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "lrg-enums.h"
#include "quest/lrg-quest-def.h"
#include "quest/lrg-quest-objective.h"

G_BEGIN_DECLS

#define LRG_TYPE_QUEST_INSTANCE (lrg_quest_instance_get_type ())

#pragma GCC visibility push(default)

G_DECLARE_FINAL_TYPE (LrgQuestInstance, lrg_quest_instance, LRG, QUEST_INSTANCE, GObject)

/**
 * lrg_quest_instance_new:
 * @quest_def: the quest definition
 *
 * Creates a new quest instance from a definition.
 *
 * Returns: (transfer full): A new #LrgQuestInstance
 */
LrgQuestInstance  *lrg_quest_instance_new               (LrgQuestDef *quest_def);

/**
 * lrg_quest_instance_get_quest_def:
 * @self: an #LrgQuestInstance
 *
 * Gets the quest definition.
 *
 * Returns: (transfer none): The quest definition
 */
LrgQuestDef       *lrg_quest_instance_get_quest_def     (LrgQuestInstance *self);

/**
 * lrg_quest_instance_get_state:
 * @self: an #LrgQuestInstance
 *
 * Gets the current quest state.
 *
 * Returns: The quest state
 */
LrgQuestState      lrg_quest_instance_get_state         (LrgQuestInstance *self);

/**
 * lrg_quest_instance_set_state:
 * @self: an #LrgQuestInstance
 * @state: new state
 *
 * Sets the quest state.
 */
void               lrg_quest_instance_set_state         (LrgQuestInstance *self,
                                                         LrgQuestState     state);

/**
 * lrg_quest_instance_get_current_stage:
 * @self: an #LrgQuestInstance
 *
 * Gets the current stage index.
 *
 * Returns: Current stage index
 */
guint              lrg_quest_instance_get_current_stage (LrgQuestInstance *self);

/**
 * lrg_quest_instance_get_current_objective:
 * @self: an #LrgQuestInstance
 *
 * Gets the current stage objective with progress.
 *
 * Returns: (transfer none) (nullable): Current objective, or %NULL if complete
 */
LrgQuestObjective *lrg_quest_instance_get_current_objective (LrgQuestInstance *self);

/**
 * lrg_quest_instance_update_progress:
 * @self: an #LrgQuestInstance
 * @objective_type: type of objective to update
 * @target_id: (nullable): target entity/item ID
 * @amount: amount to add
 *
 * Updates progress for matching objectives.
 *
 * Returns: %TRUE if progress was updated
 */
gboolean           lrg_quest_instance_update_progress   (LrgQuestInstance      *self,
                                                         LrgQuestObjectiveType  objective_type,
                                                         const gchar           *target_id,
                                                         guint                  amount);

/**
 * lrg_quest_instance_advance_stage:
 * @self: an #LrgQuestInstance
 *
 * Advances to the next stage if current objective is complete.
 *
 * Returns: %TRUE if advanced
 */
gboolean           lrg_quest_instance_advance_stage     (LrgQuestInstance *self);

/**
 * lrg_quest_instance_complete:
 * @self: an #LrgQuestInstance
 *
 * Marks the quest as complete.
 */
void               lrg_quest_instance_complete          (LrgQuestInstance *self);

/**
 * lrg_quest_instance_fail:
 * @self: an #LrgQuestInstance
 *
 * Marks the quest as failed.
 */
void               lrg_quest_instance_fail              (LrgQuestInstance *self);

/**
 * lrg_quest_instance_is_complete:
 * @self: an #LrgQuestInstance
 *
 * Checks if the quest is complete.
 *
 * Returns: %TRUE if complete
 */
gboolean           lrg_quest_instance_is_complete       (LrgQuestInstance *self);

/**
 * lrg_quest_instance_get_progress:
 * @self: an #LrgQuestInstance
 *
 * Gets overall quest progress (0.0 to 1.0).
 *
 * Returns: Progress fraction
 */
gdouble            lrg_quest_instance_get_progress      (LrgQuestInstance *self);

#pragma GCC visibility pop

G_END_DECLS
