/* lrg-event.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEvent - Generic interface for events that can be dispatched via event bus.
 *
 * This interface provides a common contract for all event types in the engine.
 * Events carry information about occurrences and can be cancelled by listeners.
 * The event bus uses this interface to dispatch events to registered listeners.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_EVENT (lrg_event_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgEvent, lrg_event, LRG, EVENT, GObject)

/**
 * LrgEventInterface:
 * @parent_iface: parent interface
 * @get_type_mask: returns the event type as a bitmask for filtering
 * @is_cancelled: checks if the event has been cancelled
 * @cancel: cancels the event
 *
 * Interface structure for #LrgEvent.
 *
 * Implementors must provide all three methods. The type_mask is used
 * by the event bus to efficiently filter which listeners receive events.
 *
 * Since: 1.0
 */
struct _LrgEventInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgEventInterface::get_type_mask:
     * @self: an #LrgEvent
     *
     * Gets the event type as a bitmask value. This is used by the
     * event bus to match events to listeners based on their event masks.
     *
     * Each event type should return a unique power-of-two value,
     * allowing listeners to subscribe to multiple event types via OR.
     *
     * Returns: the event type bitmask
     *
     * Since: 1.0
     */
    guint64 (*get_type_mask) (LrgEvent *self);

    /**
     * LrgEventInterface::is_cancelled:
     * @self: an #LrgEvent
     *
     * Checks if the event has been cancelled by a listener.
     *
     * Returns: %TRUE if cancelled, %FALSE otherwise
     *
     * Since: 1.0
     */
    gboolean (*is_cancelled) (LrgEvent *self);

    /**
     * LrgEventInterface::cancel:
     * @self: an #LrgEvent
     *
     * Cancels the event. Cancelled events will stop propagating
     * to subsequent listeners. The event bus will emit the
     * "event-cancelled" signal when an event is cancelled.
     *
     * Since: 1.0
     */
    void (*cancel) (LrgEvent *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_event_get_type_mask:
 * @self: an #LrgEvent
 *
 * Gets the event type as a bitmask value for listener matching.
 *
 * Returns: the event type bitmask
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_event_get_type_mask (LrgEvent *self);

/**
 * lrg_event_is_cancelled:
 * @self: an #LrgEvent
 *
 * Checks if the event has been cancelled.
 *
 * Returns: %TRUE if cancelled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_event_is_cancelled (LrgEvent *self);

/**
 * lrg_event_cancel:
 * @self: an #LrgEvent
 *
 * Cancels the event. This prevents further propagation to listeners.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_cancel (LrgEvent *self);

G_END_DECLS
