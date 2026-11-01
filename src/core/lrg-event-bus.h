/* lrg-event-bus.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEventBus - Central event dispatch system.
 *
 * The event bus manages event listeners and dispatches events to all
 * registered listeners in priority order. Listeners can modify events
 * or cancel them entirely.
 *
 * This is a generic event bus that works with any object implementing
 * the LrgEvent and LrgEventListener interfaces.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-event.h"
#include "lrg-event-listener.h"

G_BEGIN_DECLS

#define LRG_TYPE_EVENT_BUS (lrg_event_bus_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEventBus, lrg_event_bus, LRG, EVENT_BUS, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_event_bus_get_default:
 *
 * Gets the default event bus singleton.
 *
 * Returns: (transfer none): the default #LrgEventBus
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEventBus * lrg_event_bus_get_default (void);

/* ==========================================================================
 * Instance Creation
 * ========================================================================== */

/**
 * lrg_event_bus_new:
 *
 * Creates a new event bus. Use this for isolated contexts
 * rather than the global singleton.
 *
 * Returns: (transfer full): a new #LrgEventBus
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEventBus * lrg_event_bus_new (void);

/* ==========================================================================
 * Listener Management
 * ========================================================================== */

/**
 * lrg_event_bus_register:
 * @self: an #LrgEventBus
 * @listener: (transfer none): the listener to register
 *
 * Registers an event listener with the event bus. The listener
 * will be notified of matching events. Each call adds a separate registration,
 * even for the same listener, and holds a reference until it is removed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_bus_register (LrgEventBus      *self,
                             LrgEventListener *listener);

/**
 * lrg_event_bus_unregister:
 * @self: an #LrgEventBus
 * @listener: the listener to unregister
 *
 * Removes the oldest registration for this listener from the event bus.
 * Other registrations of the same listener remain active.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_bus_unregister (LrgEventBus      *self,
                               LrgEventListener *listener);

/**
 * lrg_event_bus_unregister_by_id:
 * @self: an #LrgEventBus
 * @listener_id: the listener ID to unregister
 *
 * Unregisters all listeners with the given ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_bus_unregister_by_id (LrgEventBus *self,
                                     const gchar *listener_id);

/**
 * lrg_event_bus_clear:
 * @self: an #LrgEventBus
 *
 * Removes all registered listeners.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_bus_clear (LrgEventBus *self);

/**
 * lrg_event_bus_refresh_listener:
 * @self: an #LrgEventBus
 * @listener: the listener whose event mask changed
 *
 * Tells the bus that @listener's event mask changed. Only matters for
 * listeners that opted into mask caching
 * (#LrgEventListenerInterface.has_static_mask): the bus reads their
 * mask again, for every registration of @listener, at the start of the
 * next emission. Such a listener must be refreshed after its mask gains
 * bits; until then it does not receive events of the new types. A mask
 * that only loses bits needs no refresh (the live mask is checked at
 * each listener's turn). For other listeners, and listeners that are
 * not registered, this does nothing: their mask is read on every
 * emission. Safe to call from a listener callback; the change applies
 * from the next emission, nested ones included.
 *
 * Since: 0.6.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_bus_refresh_listener (LrgEventBus      *self,
                                     LrgEventListener *listener);

/**
 * lrg_event_bus_refresh_listeners:
 * @self: an #LrgEventBus
 *
 * Like lrg_event_bus_refresh_listener() for every registered listener:
 * all cached masks are read again at the start of the next emission.
 *
 * Since: 0.6.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_bus_refresh_listeners (LrgEventBus *self);

/**
 * lrg_event_bus_get_listener_count:
 * @self: an #LrgEventBus
 *
 * Gets the number of registered listeners.
 *
 * Returns: the listener count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_event_bus_get_listener_count (LrgEventBus *self);

/* ==========================================================================
 * Event Dispatch
 * ========================================================================== */

/**
 * lrg_event_bus_emit:
 * @self: an #LrgEventBus
 * @event: (transfer none): the event to emit
 * @context: (nullable): optional context data
 *
 * Emits an event to all registered listeners. Listeners are notified
 * in priority order (highest first). If a listener cancels the event,
 * subsequent listeners are not notified.
 *
 * Listeners may unregister during dispatch and remain alive until dispatch
 * finishes. Listeners removed before their turn are skipped. Newly registered
 * listeners participate in subsequent emissions, including nested emissions.
 * Removing and re-registering the same listener does not revive the removed
 * registration in a dispatch already in progress.
 *
 * Event masks are captured at the start of each emission: only listeners
 * whose mask matches the event then take part. A listener whose mask stops
 * matching before its turn is skipped; one whose mask starts matching
 * during the emission waits for the next one.
 *
 * Listeners that opt into mask caching
 * (#LrgEventListenerInterface.has_static_mask returns %TRUE) are not asked
 * on every emission: their mask is read at the first emission after they
 * register and after lrg_event_bus_refresh_listener() or
 * lrg_event_bus_refresh_listeners(), so a mask that gains bits takes effect
 * only after a refresh. Their live mask is still checked at their turn.
 *
 * Priorities are captured at the start of each emission. Equal priorities use
 * registration order. Priority changes during callbacks take effect on the
 * next emission; nested emissions have independent snapshots.
 *
 * The bus, event, and snapshotted listeners remain alive through completion
 * signals even if callbacks release their owners' references. Access must be
 * confined to one thread or externally serialized. Listener accessors should
 * not mutate the bus.
 *
 * Returns: %TRUE if the event completed (not cancelled), %FALSE if cancelled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_event_bus_emit (LrgEventBus *self,
                             LrgEvent    *event,
                             gpointer     context);

G_END_DECLS
