/* lrg-trigger3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Trigger volume implementation.
 */

#include "config.h"
#include "lrg-trigger3d.h"

struct _LrgTrigger3D
{
    gchar           *id;
    LrgBoundingBox3D bounds;
    LrgTriggerType   trigger_type;
    gchar           *target_id;
    gboolean         enabled;
    gboolean         one_shot;
};

/* Register as a GBoxed type */
G_DEFINE_BOXED_TYPE (LrgTrigger3D, lrg_trigger3d,
                     lrg_trigger3d_copy, lrg_trigger3d_free)

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
LrgTrigger3D *
lrg_trigger3d_new (const gchar            *id,
                   const LrgBoundingBox3D *bounds,
                   LrgTriggerType          trigger_type)
{
    LrgTrigger3D *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (bounds != NULL, NULL);

    self = g_new0 (LrgTrigger3D, 1);
    self->id = g_strdup (id);
    self->bounds = *bounds;
    self->trigger_type = trigger_type;
    self->target_id = NULL;
    self->enabled = TRUE;
    self->one_shot = FALSE;

    return self;
}

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
LrgTrigger3D *
lrg_trigger3d_new_box (const gchar    *id,
                       gfloat          min_x,
                       gfloat          min_y,
                       gfloat          min_z,
                       gfloat          max_x,
                       gfloat          max_y,
                       gfloat          max_z,
                       LrgTriggerType  trigger_type)
{
    LrgTrigger3D *self;

    g_return_val_if_fail (id != NULL, NULL);

    self = g_new0 (LrgTrigger3D, 1);
    self->id = g_strdup (id);
    self->bounds.min.x = min_x;
    self->bounds.min.y = min_y;
    self->bounds.min.z = min_z;
    self->bounds.max.x = max_x;
    self->bounds.max.y = max_y;
    self->bounds.max.z = max_z;
    self->trigger_type = trigger_type;
    self->target_id = NULL;
    self->enabled = TRUE;
    self->one_shot = FALSE;

    return self;
}

/**
 * lrg_trigger3d_copy:
 * @self: (nullable): A #LrgTrigger3D
 *
 * Creates a copy of the trigger.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LrgTrigger3D *
lrg_trigger3d_copy (const LrgTrigger3D *self)
{
    LrgTrigger3D *copy;

    if (self == NULL)
        return NULL;

    copy = lrg_trigger3d_new (self->id, &self->bounds, self->trigger_type);
    copy->enabled = self->enabled;
    copy->one_shot = self->one_shot;

    if (self->target_id != NULL)
        copy->target_id = g_strdup (self->target_id);

    return copy;
}

/**
 * lrg_trigger3d_free:
 * @self: (nullable): A #LrgTrigger3D
 *
 * Frees a trigger.
 */
void
lrg_trigger3d_free (LrgTrigger3D *self)
{
    if (self == NULL)
        return;

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->target_id, g_free);
    g_free (self);
}

/**
 * lrg_trigger3d_get_id:
 * @self: A #LrgTrigger3D
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): The identifier
 */
const gchar *
lrg_trigger3d_get_id (const LrgTrigger3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->id;
}

/**
 * lrg_trigger3d_get_bounds:
 * @self: A #LrgTrigger3D
 *
 * Gets the trigger volume bounds.
 *
 * Returns: (transfer full): The bounds
 */
LrgBoundingBox3D *
lrg_trigger3d_get_bounds (const LrgTrigger3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return lrg_bounding_box3d_copy (&self->bounds);
}

/**
 * lrg_trigger3d_get_trigger_type:
 * @self: A #LrgTrigger3D
 *
 * Gets the trigger type.
 *
 * Returns: The trigger type
 */
LrgTriggerType
lrg_trigger3d_get_trigger_type (const LrgTrigger3D *self)
{
    g_return_val_if_fail (self != NULL, LRG_TRIGGER_TYPE_ENTER);

    return self->trigger_type;
}

/**
 * lrg_trigger3d_get_target_id:
 * @self: A #LrgTrigger3D
 *
 * Gets the target ID that this trigger affects.
 *
 * Returns: (transfer none) (nullable): The target ID, or %NULL if not set
 */
const gchar *
lrg_trigger3d_get_target_id (const LrgTrigger3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->target_id;
}

/**
 * lrg_trigger3d_set_target_id:
 * @self: A #LrgTrigger3D
 * @target_id: (nullable): Target ID to affect when triggered
 *
 * Sets the target ID.
 */
void
lrg_trigger3d_set_target_id (LrgTrigger3D *self,
                             const gchar  *target_id)
{
    g_return_if_fail (self != NULL);

    g_free (self->target_id);
    self->target_id = g_strdup (target_id);
}

/**
 * lrg_trigger3d_is_enabled:
 * @self: A #LrgTrigger3D
 *
 * Checks if the trigger is enabled.
 *
 * Returns: %TRUE if enabled
 */
gboolean
lrg_trigger3d_is_enabled (const LrgTrigger3D *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->enabled;
}

/**
 * lrg_trigger3d_set_enabled:
 * @self: A #LrgTrigger3D
 * @enabled: Whether to enable the trigger
 *
 * Enables or disables the trigger.
 */
void
lrg_trigger3d_set_enabled (LrgTrigger3D *self,
                           gboolean      enabled)
{
    g_return_if_fail (self != NULL);

    self->enabled = enabled;
}

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
gboolean
lrg_trigger3d_test_point (const LrgTrigger3D *self,
                          const GrlVector3   *point)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (point != NULL, FALSE);

    return lrg_trigger3d_test_point_xyz (self, point->x, point->y, point->z);
}

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
gboolean
lrg_trigger3d_test_point_xyz (const LrgTrigger3D *self,
                              gfloat              x,
                              gfloat              y,
                              gfloat              z)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (!self->enabled)
        return FALSE;

    return lrg_bounding_box3d_contains_point_xyz (&self->bounds, x, y, z);
}

/**
 * lrg_trigger3d_is_one_shot:
 * @self: A #LrgTrigger3D
 *
 * Checks if this is a one-shot trigger (triggers once then disables).
 *
 * Returns: %TRUE if one-shot
 */
gboolean
lrg_trigger3d_is_one_shot (const LrgTrigger3D *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->one_shot;
}

/**
 * lrg_trigger3d_set_one_shot:
 * @self: A #LrgTrigger3D
 * @one_shot: Whether this is a one-shot trigger
 *
 * Sets the one-shot flag.
 */
void
lrg_trigger3d_set_one_shot (LrgTrigger3D *self,
                            gboolean      one_shot)
{
    g_return_if_fail (self != NULL);

    self->one_shot = one_shot;
}
