/* lrg-milestone.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMilestone - Achievement/milestone definition for idle games.
 *
 * Milestones represent progress checkpoints that can trigger rewards,
 * unlock content, or simply track player achievements.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-big-number.h"

G_BEGIN_DECLS

#define LRG_TYPE_MILESTONE (lrg_milestone_get_type ())

/**
 * LrgMilestone:
 * @id: Unique identifier string
 * @name: Display name
 * @description: Optional description text
 * @icon: Optional icon name
 * @threshold: Value required to achieve
 * @achieved: Whether milestone has been achieved
 * @achieved_time: Unix timestamp when achieved (0 if not achieved)
 * @reward_multiplier: Multiplier applied when achieved (1.0 = no bonus)
 *
 * A boxed type representing a milestone/achievement checkpoint.
 */
typedef struct _LrgMilestone LrgMilestone;

struct _LrgMilestone
{
    gchar        *id;
    gchar        *name;
    gchar        *description;
    gchar        *icon;
    LrgBigNumber *threshold;
    gboolean      achieved;
    gint64        achieved_time;
    gdouble       reward_multiplier;
};

LRG_AVAILABLE_IN_ALL
GType lrg_milestone_get_type (void) G_GNUC_CONST;

/* Construction */

/**
 * lrg_milestone_new:
 * @id: Unique identifier
 * @name: Display name
 * @threshold: Value required to achieve
 *
 * Creates a new milestone.
 *
 * Returns: (transfer full): A new #LrgMilestone
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMilestone *
lrg_milestone_new (const gchar        *id,
                   const gchar        *name,
                   const LrgBigNumber *threshold);

/**
 * lrg_milestone_new_simple:
 * @id: Unique identifier
 * @name: Display name
 * @threshold: Value required to achieve (as double)
 *
 * Creates a new milestone with a simple threshold.
 *
 * Returns: (transfer full): A new #LrgMilestone
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMilestone *
lrg_milestone_new_simple (const gchar *id,
                          const gchar *name,
                          gdouble      threshold);

/**
 * lrg_milestone_copy:
 * @self: an #LrgMilestone
 *
 * Creates a deep copy of a milestone.
 *
 * Returns: (transfer full): A copy of @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMilestone *
lrg_milestone_copy (const LrgMilestone *self);

/**
 * lrg_milestone_free:
 * @self: an #LrgMilestone
 *
 * Frees a milestone.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_milestone_free (LrgMilestone *self);

/* Accessors */

/**
 * lrg_milestone_get_id:
 * @self: an #LrgMilestone
 *
 * Gets the milestone ID.
 *
 * Returns: (transfer none): The ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_milestone_get_id (const LrgMilestone *self);

/**
 * lrg_milestone_get_name:
 * @self: an #LrgMilestone
 *
 * Gets the display name.
 *
 * Returns: (transfer none): The name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_milestone_get_name (const LrgMilestone *self);

/**
 * lrg_milestone_get_description:
 * @self: an #LrgMilestone
 *
 * Gets the description.
 *
 * Returns: (transfer none) (nullable): The description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_milestone_get_description (const LrgMilestone *self);

/**
 * lrg_milestone_set_description:
 * @self: an #LrgMilestone
 * @description: (nullable): New description
 *
 * Sets the description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_milestone_set_description (LrgMilestone *self,
                               const gchar  *description);

/**
 * lrg_milestone_get_threshold:
 * @self: an #LrgMilestone
 *
 * Gets the threshold value.
 *
 * Returns: (transfer none): The threshold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgBigNumber *
lrg_milestone_get_threshold (const LrgMilestone *self);

/**
 * lrg_milestone_get_icon:
 * @self: an #LrgMilestone
 *
 * Gets the icon path.
 *
 * Returns: (transfer none) (nullable): The icon path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_milestone_get_icon (const LrgMilestone *self);

/**
 * lrg_milestone_set_icon:
 * @self: an #LrgMilestone
 * @icon: (nullable): Icon path
 *
 * Sets the icon path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_milestone_set_icon (LrgMilestone *self,
                        const gchar  *icon);

/* State */

/**
 * lrg_milestone_is_achieved:
 * @self: an #LrgMilestone
 *
 * Checks if the milestone has been achieved.
 *
 * Returns: %TRUE if achieved
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_milestone_is_achieved (const LrgMilestone *self);

/**
 * lrg_milestone_get_achieved_time:
 * @self: an #LrgMilestone
 *
 * Gets the timestamp when achieved.
 *
 * Returns: Unix timestamp, or 0 if not achieved
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_milestone_get_achieved_time (const LrgMilestone *self);

/**
 * lrg_milestone_check:
 * @self: an #LrgMilestone
 * @value: Current value to check against threshold
 *
 * Checks if the value meets the threshold and marks as achieved.
 *
 * Returns: %TRUE if newly achieved (was not achieved before)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_milestone_check (LrgMilestone       *self,
                     const LrgBigNumber *value);

/**
 * lrg_milestone_reset:
 * @self: an #LrgMilestone
 *
 * Resets the milestone to unachieved state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_milestone_reset (LrgMilestone *self);

/**
 * lrg_milestone_get_progress:
 * @self: an #LrgMilestone
 * @current: Current value
 *
 * Gets progress towards the milestone (0.0 to 1.0).
 *
 * Returns: Progress percentage
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_milestone_get_progress (const LrgMilestone *self,
                            const LrgBigNumber *current);

/* Reward */

/**
 * lrg_milestone_get_reward_multiplier:
 * @self: an #LrgMilestone
 *
 * Gets the reward multiplier for achieving this milestone.
 *
 * Returns: Reward multiplier (1.0 = no bonus)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_milestone_get_reward_multiplier (const LrgMilestone *self);

/**
 * lrg_milestone_set_reward_multiplier:
 * @self: an #LrgMilestone
 * @multiplier: Reward multiplier
 *
 * Sets the reward multiplier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_milestone_set_reward_multiplier (LrgMilestone *self,
                                     gdouble       multiplier);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgMilestone, lrg_milestone_free)

G_END_DECLS
