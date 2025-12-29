/* lrg-trigger-event.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Trigger event data structure implementation.
 */

#include "config.h"

#include "lrg-trigger-event.h"

/**
 * LrgTriggerEvent:
 *
 * Event data emitted when a trigger fires.
 */
struct _LrgTriggerEvent
{
    LrgTrigger2DEventType event_type;
    gpointer              entity;
    gfloat                x;
    gfloat                y;
};

G_DEFINE_BOXED_TYPE (LrgTriggerEvent,
                     lrg_trigger_event,
                     lrg_trigger_event_copy,
                     lrg_trigger_event_free)

/**
 * lrg_trigger_event_new:
 * @event_type: The type of trigger event
 * @entity: (nullable): The entity that triggered the event
 * @x: X coordinate where the event occurred
 * @y: Y coordinate where the event occurred
 *
 * Creates a new trigger event.
 *
 * Returns: (transfer full): A new #LrgTriggerEvent
 *
 * Since: 1.0
 */
LrgTriggerEvent *
lrg_trigger_event_new (LrgTrigger2DEventType  event_type,
                       gpointer               entity,
                       gfloat                 x,
                       gfloat                 y)
{
    LrgTriggerEvent *self;

    self = g_new0 (LrgTriggerEvent, 1);
    self->event_type = event_type;
    self->entity     = entity;
    self->x          = x;
    self->y          = y;

    return self;
}

/**
 * lrg_trigger_event_copy:
 * @self: A #LrgTriggerEvent
 *
 * Creates a copy of a trigger event.
 *
 * Returns: (transfer full): A copy of the event
 *
 * Since: 1.0
 */
LrgTriggerEvent *
lrg_trigger_event_copy (const LrgTriggerEvent *self)
{
    LrgTriggerEvent *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgTriggerEvent, 1);
    copy->event_type = self->event_type;
    copy->entity     = self->entity;
    copy->x          = self->x;
    copy->y          = self->y;

    return copy;
}

/**
 * lrg_trigger_event_free:
 * @self: A #LrgTriggerEvent
 *
 * Frees a trigger event.
 *
 * Since: 1.0
 */
void
lrg_trigger_event_free (LrgTriggerEvent *self)
{
    g_return_if_fail (self != NULL);
    g_free (self);
}

/**
 * lrg_trigger_event_get_event_type:
 * @self: A #LrgTriggerEvent
 *
 * Gets the type of the trigger event.
 *
 * Returns: The #LrgTrigger2DEventType
 *
 * Since: 1.0
 */
LrgTrigger2DEventType
lrg_trigger_event_get_event_type (const LrgTriggerEvent *self)
{
    g_return_val_if_fail (self != NULL, LRG_TRIGGER2D_EVENT_ENTER);
    return self->event_type;
}

/**
 * lrg_trigger_event_get_entity:
 * @self: A #LrgTriggerEvent
 *
 * Gets the entity that triggered the event.
 *
 * Returns: (transfer none) (nullable): The entity pointer
 *
 * Since: 1.0
 */
gpointer
lrg_trigger_event_get_entity (const LrgTriggerEvent *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->entity;
}

/**
 * lrg_trigger_event_get_x:
 * @self: A #LrgTriggerEvent
 *
 * Gets the X coordinate where the event occurred.
 *
 * Returns: The X coordinate
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_event_get_x (const LrgTriggerEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->x;
}

/**
 * lrg_trigger_event_get_y:
 * @self: A #LrgTriggerEvent
 *
 * Gets the Y coordinate where the event occurred.
 *
 * Returns: The Y coordinate
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_event_get_y (const LrgTriggerEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->y;
}

/**
 * lrg_trigger_event_get_position:
 * @self: A #LrgTriggerEvent
 * @out_x: (out) (nullable): Return location for X
 * @out_y: (out) (nullable): Return location for Y
 *
 * Gets the position where the event occurred.
 *
 * Since: 1.0
 */
void
lrg_trigger_event_get_position (const LrgTriggerEvent *self,
                                gfloat                *out_x,
                                gfloat                *out_y)
{
    g_return_if_fail (self != NULL);

    if (out_x != NULL)
        *out_x = self->x;
    if (out_y != NULL)
        *out_y = self->y;
}

/**
 * lrg_trigger_event_is_enter:
 * @self: A #LrgTriggerEvent
 *
 * Checks if this is an enter event.
 *
 * Returns: %TRUE if this is an enter event
 *
 * Since: 1.0
 */
gboolean
lrg_trigger_event_is_enter (const LrgTriggerEvent *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->event_type == LRG_TRIGGER2D_EVENT_ENTER;
}

/**
 * lrg_trigger_event_is_stay:
 * @self: A #LrgTriggerEvent
 *
 * Checks if this is a stay event.
 *
 * Returns: %TRUE if this is a stay event
 *
 * Since: 1.0
 */
gboolean
lrg_trigger_event_is_stay (const LrgTriggerEvent *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->event_type == LRG_TRIGGER2D_EVENT_STAY;
}

/**
 * lrg_trigger_event_is_exit:
 * @self: A #LrgTriggerEvent
 *
 * Checks if this is an exit event.
 *
 * Returns: %TRUE if this is an exit event
 *
 * Since: 1.0
 */
gboolean
lrg_trigger_event_is_exit (const LrgTriggerEvent *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->event_type == LRG_TRIGGER2D_EVENT_EXIT;
}
