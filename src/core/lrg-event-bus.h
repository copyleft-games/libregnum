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
 * will be notified of matching events.
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
 * Unregisters an event listener from the event bus.
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
 * Returns: %TRUE if the event completed (not cancelled), %FALSE if cancelled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_event_bus_emit (LrgEventBus *self,
                             LrgEvent    *event,
                             gpointer     context);

G_END_DECLS
