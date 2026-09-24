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

#include "lrg-version.h"
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
 * Gets objective 0 of the current stage with progress. Use
 * lrg_quest_instance_get_objective() for the other parallel objectives.
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
 * Updates progress for matching objectives. Every incomplete objective of
 * the current stage whose type equals @objective_type and whose target
 * matches (either side %NULL matches anything) is incremented by @amount
 * and #LrgQuestInstance::objective-updated is emitted for it. The stage
 * advances, and the quest completes after the last stage, only once every
 * objective of the stage is complete.
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
 * Advances to the next stage if every objective of the current stage is
 * complete. Returns %FALSE without changes once every stage is done.
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

/**
 * lrg_quest_instance_get_objective_count:
 * @self: an #LrgQuestInstance
 *
 * Gets the number of parallel objectives in the current stage.
 *
 * Returns: the objective count, 0 once every stage is done
 */
LRG_AVAILABLE_IN_ALL
guint              lrg_quest_instance_get_objective_count    (LrgQuestInstance *self);

/**
 * lrg_quest_instance_get_objective:
 * @self: an #LrgQuestInstance
 * @objective_index: objective index within the current stage
 *
 * Gets an objective of the current stage, including its progress.
 *
 * Returns: (transfer none) (nullable): the objective, or %NULL if out of range
 */
LRG_AVAILABLE_IN_ALL
LrgQuestObjective *lrg_quest_instance_get_objective          (LrgQuestInstance *self,
                                                              guint             objective_index);

/**
 * lrg_quest_instance_get_objective_progress:
 * @self: an #LrgQuestInstance
 * @objective_index: objective index within the current stage
 *
 * Gets the current count of an objective in the current stage.
 *
 * Returns: the progress count, 0 if @objective_index is out of range
 */
LRG_AVAILABLE_IN_ALL
guint              lrg_quest_instance_get_objective_progress (LrgQuestInstance *self,
                                                              guint             objective_index);

/**
 * lrg_quest_instance_set_objective_progress:
 * @self: an #LrgQuestInstance
 * @objective_index: objective index within the current stage
 * @count: new progress count; clamped to the objective's target count
 *
 * Sets the progress of an objective in the current stage. Intended for
 * restoring snapshots: no signals are emitted and the stage does not
 * auto-advance. The objective is complete iff the clamped count reaches
 * its target.
 *
 * Returns: %TRUE if the objective exists and was updated
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_quest_instance_set_objective_progress (LrgQuestInstance *self,
                                                              guint             objective_index,
                                                              guint             count);

/**
 * lrg_quest_instance_set_stage:
 * @self: an #LrgQuestInstance
 * @stage: stage index, 0 to the stage count inclusive
 *
 * Moves the instance to @stage and resets the progress of every stage to
 * zero. Intended for restoring snapshots: #LrgQuestInstance::stage-advanced
 * is not emitted and the quest state is not changed. Objective pointers
 * previously returned for this instance become invalid. A @stage equal to
 * the stage count means "every stage done"; the caller decides whether
 * the quest is then marked complete.
 *
 * Returns: %TRUE on success, %FALSE if @stage is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_quest_instance_set_stage              (LrgQuestInstance *self,
                                                              guint             stage);

#pragma GCC visibility pop

G_END_DECLS
