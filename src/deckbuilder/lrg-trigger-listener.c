/* lrg-trigger-listener.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTriggerListener - Interface implementation.
 */

#include "lrg-trigger-listener.h"

G_DEFINE_INTERFACE (LrgTriggerListener, lrg_trigger_listener, G_TYPE_OBJECT)

/* ==========================================================================
 * Default Implementations
 * ========================================================================== */

static const gchar *
lrg_trigger_listener_default_get_trigger_id (LrgTriggerListener *self)
{
    return "unknown";
}

static gint
lrg_trigger_listener_default_get_priority (LrgTriggerListener *self)
{
    return 0;
}

static guint64
lrg_trigger_listener_default_get_event_mask (LrgTriggerListener *self)
{
    return 0;
}

static gboolean
lrg_trigger_listener_default_on_event (LrgTriggerListener *self,
                                       LrgCardEvent       *event,
                                       gpointer            context)
{
    return TRUE;
}

static void
lrg_trigger_listener_default_init (LrgTriggerListenerInterface *iface)
{
    iface->get_trigger_id = lrg_trigger_listener_default_get_trigger_id;
    iface->get_priority = lrg_trigger_listener_default_get_priority;
    iface->get_event_mask = lrg_trigger_listener_default_get_event_mask;
    iface->on_event = lrg_trigger_listener_default_on_event;
}

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
const gchar *
lrg_trigger_listener_get_trigger_id (LrgTriggerListener *self)
{
    LrgTriggerListenerInterface *iface;

    g_return_val_if_fail (LRG_IS_TRIGGER_LISTENER (self), NULL);

    iface = LRG_TRIGGER_LISTENER_GET_IFACE (self);
    g_return_val_if_fail (iface->get_trigger_id != NULL, NULL);

    return iface->get_trigger_id (self);
}

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
gint
lrg_trigger_listener_get_priority (LrgTriggerListener *self)
{
    LrgTriggerListenerInterface *iface;

    g_return_val_if_fail (LRG_IS_TRIGGER_LISTENER (self), 0);

    iface = LRG_TRIGGER_LISTENER_GET_IFACE (self);
    g_return_val_if_fail (iface->get_priority != NULL, 0);

    return iface->get_priority (self);
}

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
guint64
lrg_trigger_listener_get_event_mask (LrgTriggerListener *self)
{
    LrgTriggerListenerInterface *iface;

    g_return_val_if_fail (LRG_IS_TRIGGER_LISTENER (self), 0);

    iface = LRG_TRIGGER_LISTENER_GET_IFACE (self);
    g_return_val_if_fail (iface->get_event_mask != NULL, 0);

    return iface->get_event_mask (self);
}

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
gboolean
lrg_trigger_listener_on_event (LrgTriggerListener *self,
                               LrgCardEvent       *event,
                               gpointer            context)
{
    LrgTriggerListenerInterface *iface;

    g_return_val_if_fail (LRG_IS_TRIGGER_LISTENER (self), TRUE);
    g_return_val_if_fail (event != NULL, TRUE);

    iface = LRG_TRIGGER_LISTENER_GET_IFACE (self);
    g_return_val_if_fail (iface->on_event != NULL, TRUE);

    return iface->on_event (self, event, context);
}

/* ==========================================================================
 * Convenience Functions
 * ========================================================================== */

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
guint64
lrg_trigger_listener_event_type_to_mask (LrgCardEventType event_type)
{
    return ((guint64)1) << event_type;
}

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
gboolean
lrg_trigger_listener_listens_to (LrgTriggerListener *self,
                                 LrgCardEventType    event_type)
{
    guint64 mask;
    guint64 event_mask;

    g_return_val_if_fail (LRG_IS_TRIGGER_LISTENER (self), FALSE);

    mask = lrg_trigger_listener_get_event_mask (self);
    event_mask = lrg_trigger_listener_event_type_to_mask (event_type);

    return (mask & event_mask) != 0;
}
