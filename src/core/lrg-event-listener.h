/* lrg-event-listener.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEventListener - Generic interface for objects that respond to events.
 *
 * Event listeners are registered with an event bus and notified when
 * matching events occur. Listeners can modify events or cancel them entirely.
 * The event bus dispatches events to listeners in priority order.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-event.h"

G_BEGIN_DECLS

#define LRG_TYPE_EVENT_LISTENER (lrg_event_listener_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgEventListener, lrg_event_listener, LRG, EVENT_LISTENER, GObject)

/**
 * LrgEventListenerInterface:
 * @parent_iface: parent interface
 * @get_id: returns a unique identifier for this listener
 * @get_priority: returns the priority (higher = earlier execution)
 * @get_event_mask: returns which event types this listener responds to
 * @on_event: called when a matching event occurs
 *
 * Interface structure for #LrgEventListener.
 *
 * Implementors must provide all methods. The event bus uses this interface
 * to determine which listeners receive which events and in what order.
 *
 * Since: 1.0
 */
struct _LrgEventListenerInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgEventListenerInterface::get_id:
     * @self: an #LrgEventListener
     *
     * Gets a unique identifier for this listener. This ID is used
     * for unregistering listeners by ID from the event bus.
     *
     * Returns: (transfer none): the listener ID string
     *
     * Since: 1.0
     */
    const gchar * (*get_id) (LrgEventListener *self);

    /**
     * LrgEventListenerInterface::get_priority:
     * @self: an #LrgEventListener
     *
     * Gets the priority of this listener. Higher priority listeners
     * are notified first. Default priority is 0.
     *
     * Returns: the priority value
     *
     * Since: 1.0
     */
    gint (*get_priority) (LrgEventListener *self);

    /**
     * LrgEventListenerInterface::get_event_mask:
     * @self: an #LrgEventListener
     *
     * Gets the bitmask of event types this listener responds to.
     * Each bit corresponds to an event type from #LrgEvent::get_type_mask.
     * Use bitwise OR to subscribe to multiple event types.
     *
     * Returns: the event type bitmask
     *
     * Since: 1.0
     */
    guint64 (*get_event_mask) (LrgEventListener *self);

    /**
     * LrgEventListenerInterface::on_event:
     * @self: an #LrgEventListener
     * @event: (transfer none): the event that occurred
     * @context: (nullable): optional context data
     *
     * Called when a matching event occurs. The listener may modify
     * the event data or cancel it via lrg_event_cancel().
     *
     * Returns: %TRUE if the event should continue processing,
     *          %FALSE if the event was cancelled by this listener
     *
     * Since: 1.0
     */
    gboolean (*on_event) (LrgEventListener *self,
                          LrgEvent         *event,
                          gpointer          context);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_event_listener_get_id:
 * @self: an #LrgEventListener
 *
 * Gets the unique identifier for this listener.
 *
 * Returns: (transfer none): the listener ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_event_listener_get_id (LrgEventListener *self);

/**
 * lrg_event_listener_get_priority:
 * @self: an #LrgEventListener
 *
 * Gets the priority of this listener. Higher priority listeners
 * are notified first.
 *
 * Returns: the priority value (default: 0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_event_listener_get_priority (LrgEventListener *self);

/**
 * lrg_event_listener_get_event_mask:
 * @self: an #LrgEventListener
 *
 * Gets the bitmask of event types this listener responds to.
 *
 * Returns: the event type bitmask
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_event_listener_get_event_mask (LrgEventListener *self);

/**
 * lrg_event_listener_on_event:
 * @self: an #LrgEventListener
 * @event: (transfer none): the event that occurred
 * @context: (nullable): optional context data
 *
 * Notifies the listener of an event. The listener may modify
 * the event or cancel it.
 *
 * Returns: %TRUE if the event should continue, %FALSE if cancelled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_event_listener_on_event (LrgEventListener *self,
                                       LrgEvent         *event,
                                       gpointer          context);

/* ==========================================================================
 * Convenience Functions
 * ========================================================================== */

/**
 * lrg_event_listener_listens_to:
 * @self: an #LrgEventListener
 * @event: an #LrgEvent
 *
 * Checks if this listener responds to the given event type.
 *
 * Returns: %TRUE if the listener responds to this event type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_event_listener_listens_to (LrgEventListener *self,
                                         LrgEvent         *event);

G_END_DECLS
