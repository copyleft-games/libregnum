/* lrg-trigger3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Trigger volume type for 3D levels.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-bounding-box3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRIGGER3D (lrg_trigger3d_get_type ())

/**
 * LrgTrigger3D:
 *
 * A trigger volume in a 3D level.
 *
 * Triggers define volumes that can fire events when entities enter, exit,
 * or interact with them.
 */
typedef struct _LrgTrigger3D LrgTrigger3D;

LRG_AVAILABLE_IN_ALL
GType               lrg_trigger3d_get_type          (void) G_GNUC_CONST;

/**
 * lrg_trigger3d_new:
 * @id: Unique identifier for this trigger
 * @bounds: (transfer none): The trigger volume bounds
 * @trigger_type: Type of trigger event
 *
 * Creates a new trigger.
 *
 * Returns: (transfer full): A newly allocated #LrgTrigger3D
 */
LRG_AVAILABLE_IN_ALL
LrgTrigger3D *      lrg_trigger3d_new               (const gchar            *id,
                                                     const LrgBoundingBox3D *bounds,
                                                     LrgTriggerType          trigger_type);

/**
 * lrg_trigger3d_new_box:
 * @id: Unique identifier for this trigger
 * @min_x: Minimum X coordinate
 * @min_y: Minimum Y coordinate
 * @min_z: Minimum Z coordinate
 * @max_x: Maximum X coordinate
 * @max_y: Maximum Y coordinate
 * @max_z: Maximum Z coordinate
 * @trigger_type: Type of trigger event
 *
 * Creates a new trigger from box coordinates.
 *
 * Returns: (transfer full): A newly allocated #LrgTrigger3D
 */
LRG_AVAILABLE_IN_ALL
LrgTrigger3D *      lrg_trigger3d_new_box           (const gchar            *id,
                                                     gfloat                  min_x,
                                                     gfloat                  min_y,
                                                     gfloat                  min_z,
                                                     gfloat                  max_x,
                                                     gfloat                  max_y,
                                                     gfloat                  max_z,
                                                     LrgTriggerType          trigger_type);

/**
 * lrg_trigger3d_copy:
 * @self: (nullable): A #LrgTrigger3D
 *
 * Creates a copy of the trigger.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgTrigger3D *      lrg_trigger3d_copy              (const LrgTrigger3D     *self);

/**
 * lrg_trigger3d_free:
 * @self: (nullable): A #LrgTrigger3D
 *
 * Frees a trigger.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger3d_free              (LrgTrigger3D           *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgTrigger3D, lrg_trigger3d_free)

/**
 * lrg_trigger3d_get_id:
 * @self: A #LrgTrigger3D
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): The identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_trigger3d_get_id            (const LrgTrigger3D     *self);

/**
 * lrg_trigger3d_get_bounds:
 * @self: A #LrgTrigger3D
 *
 * Gets the trigger volume bounds.
 *
 * Returns: (transfer full): The bounds
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_trigger3d_get_bounds        (const LrgTrigger3D     *self);

/**
 * lrg_trigger3d_get_trigger_type:
 * @self: A #LrgTrigger3D
 *
 * Gets the trigger type.
 *
 * Returns: The trigger type
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerType      lrg_trigger3d_get_trigger_type  (const LrgTrigger3D     *self);

/**
 * lrg_trigger3d_get_target_id:
 * @self: A #LrgTrigger3D
 *
 * Gets the target ID that this trigger affects.
 *
 * Returns: (transfer none) (nullable): The target ID, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_trigger3d_get_target_id     (const LrgTrigger3D     *self);

/**
 * lrg_trigger3d_set_target_id:
 * @self: A #LrgTrigger3D
 * @target_id: (nullable): Target ID to affect when triggered
 *
 * Sets the target ID.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger3d_set_target_id     (LrgTrigger3D           *self,
                                                     const gchar            *target_id);

/**
 * lrg_trigger3d_is_enabled:
 * @self: A #LrgTrigger3D
 *
 * Checks if the trigger is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger3d_is_enabled        (const LrgTrigger3D     *self);

/**
 * lrg_trigger3d_set_enabled:
 * @self: A #LrgTrigger3D
 * @enabled: Whether to enable the trigger
 *
 * Enables or disables the trigger.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger3d_set_enabled       (LrgTrigger3D           *self,
                                                     gboolean                enabled);

/**
 * lrg_trigger3d_test_point:
 * @self: A #LrgTrigger3D
 * @point: (transfer none): Point to test
 *
 * Tests if a point is inside the trigger volume.
 * Only returns %TRUE if the trigger is enabled.
 *
 * Returns: %TRUE if the point is inside and trigger is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger3d_test_point        (const LrgTrigger3D     *self,
                                                     const GrlVector3       *point);

/**
 * lrg_trigger3d_test_point_xyz:
 * @self: A #LrgTrigger3D
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate
 *
 * Tests if a point is inside the trigger volume.
 * Only returns %TRUE if the trigger is enabled.
 *
 * Returns: %TRUE if the point is inside and trigger is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger3d_test_point_xyz    (const LrgTrigger3D     *self,
                                                     gfloat                  x,
                                                     gfloat                  y,
                                                     gfloat                  z);

/**
 * lrg_trigger3d_is_one_shot:
 * @self: A #LrgTrigger3D
 *
 * Checks if this is a one-shot trigger (triggers once then disables).
 *
 * Returns: %TRUE if one-shot
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger3d_is_one_shot       (const LrgTrigger3D     *self);

/**
 * lrg_trigger3d_set_one_shot:
 * @self: A #LrgTrigger3D
 * @one_shot: Whether this is a one-shot trigger
 *
 * Sets the one-shot flag.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger3d_set_one_shot      (LrgTrigger3D           *self,
                                                     gboolean                one_shot);

G_END_DECLS
