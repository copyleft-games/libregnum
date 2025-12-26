/* lrg-quest-objective.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Quest objective structure for tracking progress.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_QUEST_OBJECTIVE (lrg_quest_objective_get_type ())

/**
 * LrgQuestObjective:
 *
 * Represents a single objective within a quest.
 *
 * Objectives track progress toward specific goals like killing
 * enemies, collecting items, or reaching locations.
 */
typedef struct _LrgQuestObjective LrgQuestObjective;

#pragma GCC visibility push(default)

GType               lrg_quest_objective_get_type          (void) G_GNUC_CONST;

/**
 * lrg_quest_objective_new:
 * @id: unique identifier for the objective
 * @description: human-readable description
 * @type: objective type
 *
 * Creates a new quest objective.
 *
 * Returns: (transfer full): A new #LrgQuestObjective
 */
LrgQuestObjective  *lrg_quest_objective_new               (const gchar           *id,
                                                           const gchar           *description,
                                                           LrgQuestObjectiveType  type);

/**
 * lrg_quest_objective_copy:
 * @self: an #LrgQuestObjective
 *
 * Creates a deep copy of the objective.
 *
 * Returns: (transfer full): A copy of @self
 */
LrgQuestObjective  *lrg_quest_objective_copy              (const LrgQuestObjective *self);

/**
 * lrg_quest_objective_free:
 * @self: an #LrgQuestObjective
 *
 * Frees the objective.
 */
void                lrg_quest_objective_free              (LrgQuestObjective *self);

/**
 * lrg_quest_objective_get_id:
 * @self: an #LrgQuestObjective
 *
 * Gets the objective ID.
 *
 * Returns: (transfer none): The objective ID
 */
const gchar        *lrg_quest_objective_get_id            (const LrgQuestObjective *self);

/**
 * lrg_quest_objective_get_description:
 * @self: an #LrgQuestObjective
 *
 * Gets the objective description.
 *
 * Returns: (transfer none): The description
 */
const gchar        *lrg_quest_objective_get_description   (const LrgQuestObjective *self);

/**
 * lrg_quest_objective_set_description:
 * @self: an #LrgQuestObjective
 * @description: new description
 *
 * Sets the objective description.
 */
void                lrg_quest_objective_set_description   (LrgQuestObjective *self,
                                                           const gchar       *description);

/**
 * lrg_quest_objective_get_objective_type:
 * @self: an #LrgQuestObjective
 *
 * Gets the objective type.
 *
 * Returns: The objective type
 */
LrgQuestObjectiveType lrg_quest_objective_get_objective_type (const LrgQuestObjective *self);

/**
 * lrg_quest_objective_get_target_id:
 * @self: an #LrgQuestObjective
 *
 * Gets the target entity/item ID.
 *
 * Returns: (transfer none) (nullable): The target ID
 */
const gchar        *lrg_quest_objective_get_target_id     (const LrgQuestObjective *self);

/**
 * lrg_quest_objective_set_target_id:
 * @self: an #LrgQuestObjective
 * @target_id: (nullable): target entity/item ID
 *
 * Sets the target ID.
 */
void                lrg_quest_objective_set_target_id     (LrgQuestObjective *self,
                                                           const gchar       *target_id);

/**
 * lrg_quest_objective_get_target_count:
 * @self: an #LrgQuestObjective
 *
 * Gets the required count to complete the objective.
 *
 * Returns: The target count
 */
guint               lrg_quest_objective_get_target_count  (const LrgQuestObjective *self);

/**
 * lrg_quest_objective_set_target_count:
 * @self: an #LrgQuestObjective
 * @count: required count
 *
 * Sets the target count.
 */
void                lrg_quest_objective_set_target_count  (LrgQuestObjective *self,
                                                           guint              count);

/**
 * lrg_quest_objective_get_current_count:
 * @self: an #LrgQuestObjective
 *
 * Gets the current progress count.
 *
 * Returns: The current count
 */
guint               lrg_quest_objective_get_current_count (const LrgQuestObjective *self);

/**
 * lrg_quest_objective_set_current_count:
 * @self: an #LrgQuestObjective
 * @count: current count
 *
 * Sets the current progress count.
 */
void                lrg_quest_objective_set_current_count (LrgQuestObjective *self,
                                                           guint              count);

/**
 * lrg_quest_objective_increment:
 * @self: an #LrgQuestObjective
 * @amount: amount to add
 *
 * Increments the current count.
 *
 * Returns: The new current count
 */
guint               lrg_quest_objective_increment         (LrgQuestObjective *self,
                                                           guint              amount);

/**
 * lrg_quest_objective_get_location:
 * @self: an #LrgQuestObjective
 *
 * Gets the target location for REACH objectives.
 *
 * Returns: (transfer none) (nullable): The location ID
 */
const gchar        *lrg_quest_objective_get_location      (const LrgQuestObjective *self);

/**
 * lrg_quest_objective_set_location:
 * @self: an #LrgQuestObjective
 * @location: (nullable): location ID
 *
 * Sets the target location.
 */
void                lrg_quest_objective_set_location      (LrgQuestObjective *self,
                                                           const gchar       *location);

/**
 * lrg_quest_objective_is_complete:
 * @self: an #LrgQuestObjective
 *
 * Checks if the objective is complete.
 *
 * Returns: %TRUE if complete
 */
gboolean            lrg_quest_objective_is_complete       (const LrgQuestObjective *self);

/**
 * lrg_quest_objective_set_complete:
 * @self: an #LrgQuestObjective
 * @complete: completion state
 *
 * Manually sets the completion state.
 */
void                lrg_quest_objective_set_complete      (LrgQuestObjective *self,
                                                           gboolean           complete);

/**
 * lrg_quest_objective_get_progress:
 * @self: an #LrgQuestObjective
 *
 * Gets the completion percentage (0.0 to 1.0).
 *
 * Returns: Progress as a fraction
 */
gdouble             lrg_quest_objective_get_progress      (const LrgQuestObjective *self);

#pragma GCC visibility pop

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgQuestObjective, lrg_quest_objective_free)

G_END_DECLS
