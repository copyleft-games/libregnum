/* lrg-trigger-listener.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTriggerListener - Interface for objects that respond to game events.
 *
 * Trigger listeners are notified when specific game events occur. They can
 * modify event data (e.g., modify damage amounts) or cancel events entirely.
 * Common implementations include relics, powers, and status effects.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-card-event.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRIGGER_LISTENER (lrg_trigger_listener_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgTriggerListener, lrg_trigger_listener, LRG, TRIGGER_LISTENER, GObject)

/**
 * LrgTriggerListenerInterface:
 * @parent_iface: parent interface
 * @get_trigger_id: returns a unique identifier for this listener
 * @get_priority: returns the priority (higher = earlier execution)
 * @get_event_mask: returns which event types this listener responds to
 * @on_event: called when a matching event occurs
 *
 * The interface for objects that respond to game events.
 *
 * Since: 1.0
 */
struct _LrgTriggerListenerInterface
{
    GTypeInterface parent_iface;

    /**
     * LrgTriggerListenerInterface::get_trigger_id:
     * @self: a #LrgTriggerListener
     *
     * Gets a unique identifier for this listener.
     *
     * Returns: (transfer none): the trigger ID string
     *
     * Since: 1.0
     */
    const gchar * (*get_trigger_id) (LrgTriggerListener *self);

    /**
     * LrgTriggerListenerInterface::get_priority:
     * @self: a #LrgTriggerListener
     *
     * Gets the priority of this listener. Higher priority listeners
     * are notified first.
     *
     * Returns: the priority value
     *
     * Since: 1.0
     */
    gint (*get_priority) (LrgTriggerListener *self);

    /**
     * LrgTriggerListenerInterface::get_event_mask:
     * @self: a #LrgTriggerListener
     *
     * Gets the bitmask of event types this listener responds to.
     * Each bit corresponds to a #LrgCardEventType value.
     *
     * Returns: the event type bitmask
     *
     * Since: 1.0
     */
    guint64 (*get_event_mask) (LrgTriggerListener *self);

    /**
     * LrgTriggerListenerInterface::on_event:
     * @self: a #LrgTriggerListener
     * @event: the event that occurred
     * @context: (nullable): the combat/game context
     *
     * Called when a matching event occurs. The listener may modify
     * the event (e.g., change damage amount) or cancel it.
     *
     * Returns: %TRUE if the event should continue processing,
     *          %FALSE if the event was cancelled
     *
     * Since: 1.0
     */
    gboolean (*on_event) (LrgTriggerListener *self,
                          LrgCardEvent       *event,
                          gpointer            context);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_trigger_listener_get_trigger_id:
 * @self: a #LrgTriggerListener
 *
 * Gets the unique identifier for this listener.
 *
 * Returns: (transfer none): the trigger ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_trigger_listener_get_trigger_id (LrgTriggerListener *self);

/**
 * lrg_trigger_listener_get_priority:
 * @self: a #LrgTriggerListener
 *
 * Gets the priority of this listener. Higher priority listeners
 * are notified first.
 *
 * Returns: the priority value (default: 0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_trigger_listener_get_priority (LrgTriggerListener *self);

/**
 * lrg_trigger_listener_get_event_mask:
 * @self: a #LrgTriggerListener
 *
 * Gets the bitmask of event types this listener responds to.
 *
 * Returns: the event type bitmask
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_trigger_listener_get_event_mask (LrgTriggerListener *self);

/**
 * lrg_trigger_listener_on_event:
 * @self: a #LrgTriggerListener
 * @event: the event that occurred
 * @context: (nullable): the combat/game context
 *
 * Notifies the listener of an event. The listener may modify
 * the event or cancel it.
 *
 * Returns: %TRUE if the event should continue, %FALSE if cancelled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_trigger_listener_on_event (LrgTriggerListener *self,
                                        LrgCardEvent       *event,
                                        gpointer            context);

/* ==========================================================================
 * Convenience Functions
 * ========================================================================== */

/**
 * lrg_trigger_listener_listens_to:
 * @self: a #LrgTriggerListener
 * @event_type: the event type to check
 *
 * Checks if this listener responds to the given event type.
 *
 * Returns: %TRUE if the listener responds to this event type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_trigger_listener_listens_to (LrgTriggerListener *self,
                                          LrgCardEventType    event_type);

/**
 * lrg_trigger_listener_event_type_to_mask:
 * @event_type: the event type
 *
 * Converts an event type to a bitmask value.
 *
 * Returns: the bitmask for this event type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_trigger_listener_event_type_to_mask (LrgCardEventType event_type);

G_END_DECLS
