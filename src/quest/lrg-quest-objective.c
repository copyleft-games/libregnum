/* lrg-quest-objective.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "quest/lrg-quest-objective.h"

/**
 * LrgQuestObjective:
 *
 * Internal structure for quest objectives.
 */
struct _LrgQuestObjective
{
    gchar                 *id;
    gchar                 *description;
    LrgQuestObjectiveType  type;
    gchar                 *target_id;
    guint                  target_count;
    guint                  current_count;
    gchar                 *location;
    gboolean               complete;
};

G_DEFINE_BOXED_TYPE (LrgQuestObjective,
                     lrg_quest_objective,
                     lrg_quest_objective_copy,
                     lrg_quest_objective_free)

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
LrgQuestObjective *
lrg_quest_objective_new (const gchar           *id,
                         const gchar           *description,
                         LrgQuestObjectiveType  type)
{
    LrgQuestObjective *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (description != NULL, NULL);

    self = g_new0 (LrgQuestObjective, 1);
    self->id = g_strdup (id);
    self->description = g_strdup (description);
    self->type = type;
    self->target_count = 1;
    self->current_count = 0;
    self->complete = FALSE;

    return self;
}

/**
 * lrg_quest_objective_copy:
 * @self: an #LrgQuestObjective
 *
 * Creates a deep copy of the objective.
 *
 * Returns: (transfer full): A copy of @self
 */
LrgQuestObjective *
lrg_quest_objective_copy (const LrgQuestObjective *self)
{
    LrgQuestObjective *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgQuestObjective, 1);
    copy->id = g_strdup (self->id);
    copy->description = g_strdup (self->description);
    copy->type = self->type;
    copy->target_id = g_strdup (self->target_id);
    copy->target_count = self->target_count;
    copy->current_count = self->current_count;
    copy->location = g_strdup (self->location);
    copy->complete = self->complete;

    return copy;
}

/**
 * lrg_quest_objective_free:
 * @self: an #LrgQuestObjective
 *
 * Frees the objective.
 */
void
lrg_quest_objective_free (LrgQuestObjective *self)
{
    if (self == NULL)
        return;

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->description, g_free);
    g_clear_pointer (&self->target_id, g_free);
    g_clear_pointer (&self->location, g_free);
    g_free (self);
}

/**
 * lrg_quest_objective_get_id:
 * @self: an #LrgQuestObjective
 *
 * Gets the objective ID.
 *
 * Returns: (transfer none): The objective ID
 */
const gchar *
lrg_quest_objective_get_id (const LrgQuestObjective *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

/**
 * lrg_quest_objective_get_description:
 * @self: an #LrgQuestObjective
 *
 * Gets the objective description.
 *
 * Returns: (transfer none): The description
 */
const gchar *
lrg_quest_objective_get_description (const LrgQuestObjective *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->description;
}

/**
 * lrg_quest_objective_set_description:
 * @self: an #LrgQuestObjective
 * @description: new description
 *
 * Sets the objective description.
 */
void
lrg_quest_objective_set_description (LrgQuestObjective *self,
                                     const gchar       *description)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (description != NULL);

    g_free (self->description);
    self->description = g_strdup (description);
}

/**
 * lrg_quest_objective_get_objective_type:
 * @self: an #LrgQuestObjective
 *
 * Gets the objective type.
 *
 * Returns: The objective type
 */
LrgQuestObjectiveType
lrg_quest_objective_get_objective_type (const LrgQuestObjective *self)
{
    g_return_val_if_fail (self != NULL, LRG_QUEST_OBJECTIVE_CUSTOM);
    return self->type;
}

/**
 * lrg_quest_objective_get_target_id:
 * @self: an #LrgQuestObjective
 *
 * Gets the target entity/item ID.
 *
 * Returns: (transfer none) (nullable): The target ID
 */
const gchar *
lrg_quest_objective_get_target_id (const LrgQuestObjective *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->target_id;
}

/**
 * lrg_quest_objective_set_target_id:
 * @self: an #LrgQuestObjective
 * @target_id: (nullable): target entity/item ID
 *
 * Sets the target ID.
 */
void
lrg_quest_objective_set_target_id (LrgQuestObjective *self,
                                   const gchar       *target_id)
{
    g_return_if_fail (self != NULL);

    g_free (self->target_id);
    self->target_id = g_strdup (target_id);
}

/**
 * lrg_quest_objective_get_target_count:
 * @self: an #LrgQuestObjective
 *
 * Gets the required count to complete the objective.
 *
 * Returns: The target count
 */
guint
lrg_quest_objective_get_target_count (const LrgQuestObjective *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->target_count;
}

/**
 * lrg_quest_objective_set_target_count:
 * @self: an #LrgQuestObjective
 * @count: required count
 *
 * Sets the target count.
 */
void
lrg_quest_objective_set_target_count (LrgQuestObjective *self,
                                      guint              count)
{
    g_return_if_fail (self != NULL);
    self->target_count = count;
}

/**
 * lrg_quest_objective_get_current_count:
 * @self: an #LrgQuestObjective
 *
 * Gets the current progress count.
 *
 * Returns: The current count
 */
guint
lrg_quest_objective_get_current_count (const LrgQuestObjective *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->current_count;
}

/**
 * lrg_quest_objective_set_current_count:
 * @self: an #LrgQuestObjective
 * @count: current count
 *
 * Sets the current progress count.
 */
void
lrg_quest_objective_set_current_count (LrgQuestObjective *self,
                                       guint              count)
{
    g_return_if_fail (self != NULL);
    self->current_count = count;

    /* Auto-complete if target reached */
    if (self->current_count >= self->target_count)
        self->complete = TRUE;
}

/**
 * lrg_quest_objective_increment:
 * @self: an #LrgQuestObjective
 * @amount: amount to add
 *
 * Increments the current count.
 *
 * Returns: The new current count
 */
guint
lrg_quest_objective_increment (LrgQuestObjective *self,
                               guint              amount)
{
    g_return_val_if_fail (self != NULL, 0);

    self->current_count += amount;

    /* Auto-complete if target reached */
    if (self->current_count >= self->target_count)
        self->complete = TRUE;

    return self->current_count;
}

/**
 * lrg_quest_objective_get_location:
 * @self: an #LrgQuestObjective
 *
 * Gets the target location for REACH objectives.
 *
 * Returns: (transfer none) (nullable): The location ID
 */
const gchar *
lrg_quest_objective_get_location (const LrgQuestObjective *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->location;
}

/**
 * lrg_quest_objective_set_location:
 * @self: an #LrgQuestObjective
 * @location: (nullable): location ID
 *
 * Sets the target location.
 */
void
lrg_quest_objective_set_location (LrgQuestObjective *self,
                                  const gchar       *location)
{
    g_return_if_fail (self != NULL);

    g_free (self->location);
    self->location = g_strdup (location);
}

/**
 * lrg_quest_objective_is_complete:
 * @self: an #LrgQuestObjective
 *
 * Checks if the objective is complete.
 *
 * Returns: %TRUE if complete
 */
gboolean
lrg_quest_objective_is_complete (const LrgQuestObjective *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->complete;
}

/**
 * lrg_quest_objective_set_complete:
 * @self: an #LrgQuestObjective
 * @complete: completion state
 *
 * Manually sets the completion state.
 */
void
lrg_quest_objective_set_complete (LrgQuestObjective *self,
                                  gboolean           complete)
{
    g_return_if_fail (self != NULL);
    self->complete = complete;
}

/**
 * lrg_quest_objective_get_progress:
 * @self: an #LrgQuestObjective
 *
 * Gets the completion percentage (0.0 to 1.0).
 *
 * Returns: Progress as a fraction
 */
gdouble
lrg_quest_objective_get_progress (const LrgQuestObjective *self)
{
    g_return_val_if_fail (self != NULL, 0.0);

    if (self->complete)
        return 1.0;

    if (self->target_count == 0)
        return 0.0;

    return (gdouble)self->current_count / (gdouble)self->target_count;
}
