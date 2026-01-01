/* lrg-event-bus.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEventBus - Central event dispatch system.
 *
 * The event bus manages trigger listeners and dispatches game events
 * to all registered listeners in priority order. Listeners can modify
 * events or cancel them entirely.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-card-event.h"
#include "lrg-trigger-listener.h"

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
 * Creates a new event bus. Use this for isolated combat contexts
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
 * Registers a trigger listener with the event bus. The listener
 * will be notified of matching events.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_bus_register (LrgEventBus        *self,
                             LrgTriggerListener *listener);

/**
 * lrg_event_bus_unregister:
 * @self: an #LrgEventBus
 * @listener: the listener to unregister
 *
 * Unregisters a trigger listener from the event bus.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_bus_unregister (LrgEventBus        *self,
                               LrgTriggerListener *listener);

/**
 * lrg_event_bus_unregister_by_id:
 * @self: an #LrgEventBus
 * @trigger_id: the trigger ID to unregister
 *
 * Unregisters all listeners with the given trigger ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_event_bus_unregister_by_id (LrgEventBus *self,
                                     const gchar *trigger_id);

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
 * @event: (transfer full): the event to emit
 * @context: (nullable): the combat/game context
 *
 * Emits an event to all registered listeners. Listeners are notified
 * in priority order (highest first). If a listener cancels the event,
 * subsequent listeners are not notified.
 *
 * The event bus takes ownership of the event and frees it after dispatch.
 *
 * Returns: %TRUE if the event completed (not cancelled), %FALSE if cancelled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_event_bus_emit (LrgEventBus  *self,
                             LrgCardEvent *event,
                             gpointer      context);

/**
 * lrg_event_bus_emit_copy:
 * @self: an #LrgEventBus
 * @event: (transfer none): the event to emit (copied)
 * @context: (nullable): the combat/game context
 *
 * Emits a copy of an event to all registered listeners. The original
 * event is not modified.
 *
 * Returns: %TRUE if the event completed (not cancelled), %FALSE if cancelled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_event_bus_emit_copy (LrgEventBus  *self,
                                  LrgCardEvent *event,
                                  gpointer      context);

G_END_DECLS
