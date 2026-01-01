/* lrg-unlock-def.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUnlockDef - Base class for unlock condition definitions.
 *
 * Defines conditions that must be met to unlock game content:
 * - Characters
 * - Cards
 * - Relics
 * - Jokers
 * - Cosmetics
 * - Challenge modes
 *
 * This is a derivable type - subclass it to create custom
 * unlock conditions.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_UNLOCK_DEF (lrg_unlock_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgUnlockDef, lrg_unlock_def, LRG, UNLOCK_DEF, GObject)

/**
 * LrgUnlockDefClass:
 * @parent_class: The parent class
 * @check_condition: Check if unlock conditions are met
 * @get_progress: Get progress toward unlock (0.0 - 1.0)
 * @get_requirement_text: Get human-readable requirement description
 * @on_unlocked: Called when the unlock is granted
 *
 * The virtual function table for #LrgUnlockDef.
 *
 * Since: 1.0
 */
struct _LrgUnlockDefClass
{
    GObjectClass parent_class;

    /* Check if conditions are met */
    gboolean   (* check_condition)      (LrgUnlockDef     *self,
                                         LrgPlayerProfile *profile);

    /* Get progress (0.0 = none, 1.0 = complete) */
    gfloat     (* get_progress)         (LrgUnlockDef     *self,
                                         LrgPlayerProfile *profile);

    /* Get human-readable requirement */
    gchar *    (* get_requirement_text) (LrgUnlockDef *self);

    /* Called when unlocked */
    void       (* on_unlocked)          (LrgUnlockDef     *self,
                                         LrgPlayerProfile *profile);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_unlock_def_new:
 * @id: unique identifier
 * @unlock_type: type of content being unlocked
 * @target_id: ID of the content to unlock
 *
 * Creates a new unlock definition.
 *
 * Returns: (transfer full): a new #LrgUnlockDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgUnlockDef * lrg_unlock_def_new (const gchar   *id,
                                    LrgUnlockType  unlock_type,
                                    const gchar   *target_id);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_unlock_def_get_id:
 * @self: a #LrgUnlockDef
 *
 * Gets the unlock's unique identifier.
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_unlock_def_get_id (LrgUnlockDef *self);

/**
 * lrg_unlock_def_get_unlock_type:
 * @self: a #LrgUnlockDef
 *
 * Gets the type of content being unlocked.
 *
 * Returns: the #LrgUnlockType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgUnlockType lrg_unlock_def_get_unlock_type (LrgUnlockDef *self);

/**
 * lrg_unlock_def_get_target_id:
 * @self: a #LrgUnlockDef
 *
 * Gets the ID of the content to unlock.
 *
 * Returns: (transfer none): the target ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_unlock_def_get_target_id (LrgUnlockDef *self);

/**
 * lrg_unlock_def_get_name:
 * @self: a #LrgUnlockDef
 *
 * Gets the unlock's display name.
 *
 * Returns: (transfer none) (nullable): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_unlock_def_get_name (LrgUnlockDef *self);

/**
 * lrg_unlock_def_set_name:
 * @self: a #LrgUnlockDef
 * @name: (nullable): the display name
 *
 * Sets the unlock's display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_unlock_def_set_name (LrgUnlockDef *self,
                               const gchar  *name);

/**
 * lrg_unlock_def_get_description:
 * @self: a #LrgUnlockDef
 *
 * Gets the unlock's description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_unlock_def_get_description (LrgUnlockDef *self);

/**
 * lrg_unlock_def_set_description:
 * @self: a #LrgUnlockDef
 * @description: (nullable): the description
 *
 * Sets the unlock's description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_unlock_def_set_description (LrgUnlockDef *self,
                                      const gchar  *description);

/**
 * lrg_unlock_def_get_hidden:
 * @self: a #LrgUnlockDef
 *
 * Gets whether this unlock is hidden until discovered.
 *
 * Returns: %TRUE if hidden
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_unlock_def_get_hidden (LrgUnlockDef *self);

/**
 * lrg_unlock_def_set_hidden:
 * @self: a #LrgUnlockDef
 * @hidden: whether hidden
 *
 * Sets whether this unlock is hidden until discovered.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_unlock_def_set_hidden (LrgUnlockDef *self,
                                 gboolean      hidden);

/* ==========================================================================
 * Condition Configuration (Simple Conditions)
 * ========================================================================== */

/**
 * lrg_unlock_def_set_win_count:
 * @self: a #LrgUnlockDef
 * @character_id: (nullable): character ID, or NULL for any
 * @count: number of wins required
 *
 * Sets a win count requirement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_unlock_def_set_win_count (LrgUnlockDef *self,
                                    const gchar  *character_id,
                                    gint          count);

/**
 * lrg_unlock_def_set_run_count:
 * @self: a #LrgUnlockDef
 * @character_id: (nullable): character ID, or NULL for any
 * @count: number of runs required
 *
 * Sets a run count requirement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_unlock_def_set_run_count (LrgUnlockDef *self,
                                    const gchar  *character_id,
                                    gint          count);

/**
 * lrg_unlock_def_set_ascension_requirement:
 * @self: a #LrgUnlockDef
 * @character_id: character ID
 * @level: ascension level required
 *
 * Sets an ascension level requirement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_unlock_def_set_ascension_requirement (LrgUnlockDef *self,
                                                const gchar  *character_id,
                                                gint          level);

/**
 * lrg_unlock_def_set_unlock_requirement:
 * @self: a #LrgUnlockDef
 * @required_unlock_id: ID of unlock that must be completed first
 *
 * Sets a prerequisite unlock requirement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_unlock_def_set_unlock_requirement (LrgUnlockDef *self,
                                             const gchar  *required_unlock_id);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_unlock_def_check_condition:
 * @self: a #LrgUnlockDef
 * @profile: player profile to check
 *
 * Checks if unlock conditions are met.
 *
 * Returns: %TRUE if conditions are met
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_unlock_def_check_condition (LrgUnlockDef     *self,
                                          LrgPlayerProfile *profile);

/**
 * lrg_unlock_def_get_progress:
 * @self: a #LrgUnlockDef
 * @profile: player profile to check
 *
 * Gets progress toward unlock.
 *
 * Returns: progress (0.0 - 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_unlock_def_get_progress (LrgUnlockDef     *self,
                                     LrgPlayerProfile *profile);

/**
 * lrg_unlock_def_get_requirement_text:
 * @self: a #LrgUnlockDef
 *
 * Gets human-readable requirement description.
 *
 * Returns: (transfer full): requirement text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_unlock_def_get_requirement_text (LrgUnlockDef *self);

/**
 * lrg_unlock_def_grant:
 * @self: a #LrgUnlockDef
 * @profile: player profile to unlock for
 *
 * Grants the unlock if conditions are met.
 *
 * Returns: %TRUE if unlock was granted
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_unlock_def_grant (LrgUnlockDef     *self,
                                LrgPlayerProfile *profile);

G_END_DECLS
