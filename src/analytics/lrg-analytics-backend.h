/* lrg-analytics-backend.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnalyticsBackend - Abstract base class for analytics backends.
 *
 * This is a derivable class that defines the interface for sending
 * analytics events. Subclass this to implement custom backends
 * (HTTP, file, Steam, etc.).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-analytics-event.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANALYTICS_BACKEND (lrg_analytics_backend_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAnalyticsBackend, lrg_analytics_backend, LRG, ANALYTICS_BACKEND, GObject)

/**
 * LrgAnalyticsBackendClass:
 * @parent_class: the parent class
 * @send_event: virtual function to send an event
 * @flush: virtual function to flush pending events
 * @is_enabled: virtual function to check if backend is enabled
 *
 * The class structure for #LrgAnalyticsBackend.
 *
 * Since: 1.0
 */
struct _LrgAnalyticsBackendClass
{
    GObjectClass parent_class;

    /**
     * LrgAnalyticsBackendClass::send_event:
     * @self: the analytics backend
     * @event: the event to send
     * @error: (nullable): return location for error
     *
     * Sends an analytics event to the backend.
     * Implementations may queue events for batching.
     *
     * Returns: %TRUE on success
     */
    gboolean (* send_event) (LrgAnalyticsBackend  *self,
                             LrgAnalyticsEvent    *event,
                             GError              **error);

    /**
     * LrgAnalyticsBackendClass::flush:
     * @self: the analytics backend
     * @error: (nullable): return location for error
     *
     * Flushes any pending events to the backend.
     *
     * Returns: %TRUE on success
     */
    gboolean (* flush)      (LrgAnalyticsBackend  *self,
                             GError              **error);

    /**
     * LrgAnalyticsBackendClass::is_enabled:
     * @self: the analytics backend
     *
     * Checks if the backend is enabled and ready to send events.
     *
     * Returns: %TRUE if enabled
     */
    gboolean (* is_enabled) (LrgAnalyticsBackend *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_analytics_backend_get_enabled:
 * @self: an #LrgAnalyticsBackend
 *
 * Gets whether the backend is enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_analytics_backend_get_enabled (LrgAnalyticsBackend *self);

/**
 * lrg_analytics_backend_set_enabled:
 * @self: an #LrgAnalyticsBackend
 * @enabled: whether to enable
 *
 * Sets whether the backend is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_backend_set_enabled (LrgAnalyticsBackend *self,
                                   gboolean             enabled);

/**
 * lrg_analytics_backend_get_name:
 * @self: an #LrgAnalyticsBackend
 *
 * Gets the backend name/identifier.
 *
 * Returns: (transfer none): the backend name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_analytics_backend_get_name (LrgAnalyticsBackend *self);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_analytics_backend_send_event:
 * @self: an #LrgAnalyticsBackend
 * @event: the event to send
 * @error: (nullable): return location for error
 *
 * Sends an analytics event to the backend.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_analytics_backend_send_event (LrgAnalyticsBackend  *self,
                                  LrgAnalyticsEvent    *event,
                                  GError              **error);

/**
 * lrg_analytics_backend_flush:
 * @self: an #LrgAnalyticsBackend
 * @error: (nullable): return location for error
 *
 * Flushes any pending events to the backend.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_analytics_backend_flush (LrgAnalyticsBackend  *self,
                             GError              **error);

/**
 * lrg_analytics_backend_is_enabled:
 * @self: an #LrgAnalyticsBackend
 *
 * Checks if the backend is enabled and ready to send events.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_analytics_backend_is_enabled (LrgAnalyticsBackend *self);

G_END_DECLS
