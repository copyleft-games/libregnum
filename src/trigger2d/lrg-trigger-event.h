/* lrg-trigger-event.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Trigger event data structure.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRIGGER_EVENT (lrg_trigger_event_get_type ())

/**
 * LrgTriggerEvent:
 *
 * Event data emitted when a trigger fires.
 *
 * This boxed type contains information about the trigger event,
 * including the event type (enter/stay/exit), the triggering entity,
 * and the position where the event occurred.
 *
 * Since: 1.0
 */
typedef struct _LrgTriggerEvent LrgTriggerEvent;

LRG_AVAILABLE_IN_ALL
GType               lrg_trigger_event_get_type      (void) G_GNUC_CONST;

LRG_AVAILABLE_IN_ALL
void                lrg_trigger_event_free          (LrgTriggerEvent *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgTriggerEvent, lrg_trigger_event_free)

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
LRG_AVAILABLE_IN_ALL
LrgTriggerEvent *   lrg_trigger_event_new           (LrgTrigger2DEventType  event_type,
                                                     gpointer               entity,
                                                     gfloat                 x,
                                                     gfloat                 y);

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
LRG_AVAILABLE_IN_ALL
LrgTriggerEvent *   lrg_trigger_event_copy          (const LrgTriggerEvent *self);

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
LRG_AVAILABLE_IN_ALL
LrgTrigger2DEventType lrg_trigger_event_get_event_type (const LrgTriggerEvent *self);

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
LRG_AVAILABLE_IN_ALL
gpointer            lrg_trigger_event_get_entity    (const LrgTriggerEvent *self);

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
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_event_get_x         (const LrgTriggerEvent *self);

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
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_event_get_y         (const LrgTriggerEvent *self);

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
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_event_get_position  (const LrgTriggerEvent *self,
                                                     gfloat                *out_x,
                                                     gfloat                *out_y);

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger_event_is_enter      (const LrgTriggerEvent *self);

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger_event_is_stay       (const LrgTriggerEvent *self);

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger_event_is_exit       (const LrgTriggerEvent *self);

G_END_DECLS
