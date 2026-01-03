/* lrg-event-listener.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the LrgEventListener interface.
 */

#include "lrg-event-listener.h"
#include "../lrg-log.h"

G_DEFINE_INTERFACE (LrgEventListener, lrg_event_listener, G_TYPE_OBJECT)

static void
lrg_event_listener_default_init (LrgEventListenerInterface *iface)
{
    /* No default implementations - all methods must be provided */
}

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
const gchar *
lrg_event_listener_get_id (LrgEventListener *self)
{
    LrgEventListenerInterface *iface;

    g_return_val_if_fail (LRG_IS_EVENT_LISTENER (self), NULL);

    iface = LRG_EVENT_LISTENER_GET_IFACE (self);

    g_return_val_if_fail (iface->get_id != NULL, NULL);

    return iface->get_id (self);
}

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
gint
lrg_event_listener_get_priority (LrgEventListener *self)
{
    LrgEventListenerInterface *iface;

    g_return_val_if_fail (LRG_IS_EVENT_LISTENER (self), 0);

    iface = LRG_EVENT_LISTENER_GET_IFACE (self);

    g_return_val_if_fail (iface->get_priority != NULL, 0);

    return iface->get_priority (self);
}

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
guint64
lrg_event_listener_get_event_mask (LrgEventListener *self)
{
    LrgEventListenerInterface *iface;

    g_return_val_if_fail (LRG_IS_EVENT_LISTENER (self), 0);

    iface = LRG_EVENT_LISTENER_GET_IFACE (self);

    g_return_val_if_fail (iface->get_event_mask != NULL, 0);

    return iface->get_event_mask (self);
}

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
gboolean
lrg_event_listener_on_event (LrgEventListener *self,
                              LrgEvent         *event,
                              gpointer          context)
{
    LrgEventListenerInterface *iface;

    g_return_val_if_fail (LRG_IS_EVENT_LISTENER (self), FALSE);
    g_return_val_if_fail (LRG_IS_EVENT (event), FALSE);

    iface = LRG_EVENT_LISTENER_GET_IFACE (self);

    g_return_val_if_fail (iface->on_event != NULL, FALSE);

    return iface->on_event (self, event, context);
}

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
gboolean
lrg_event_listener_listens_to (LrgEventListener *self,
                                LrgEvent         *event)
{
    guint64 listener_mask;
    guint64 event_mask;

    g_return_val_if_fail (LRG_IS_EVENT_LISTENER (self), FALSE);
    g_return_val_if_fail (LRG_IS_EVENT (event), FALSE);

    listener_mask = lrg_event_listener_get_event_mask (self);
    event_mask = lrg_event_get_type_mask (event);

    return (listener_mask & event_mask) != 0;
}
