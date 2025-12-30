/* lrg-analytics.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnalytics - Singleton analytics manager.
 *
 * Central manager for analytics event tracking with session management,
 * consent integration, and backend coordination.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-analytics-event.h"
#include "lrg-analytics-backend.h"
#include "lrg-consent.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANALYTICS (lrg_analytics_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAnalytics, lrg_analytics, LRG, ANALYTICS, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_analytics_get_default:
 *
 * Gets the default analytics manager instance.
 *
 * Returns: (transfer none): the default #LrgAnalytics
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAnalytics *
lrg_analytics_get_default (void);

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_analytics_get_enabled:
 * @self: an #LrgAnalytics
 *
 * Gets whether analytics is enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_analytics_get_enabled (LrgAnalytics *self);

/**
 * lrg_analytics_set_enabled:
 * @self: an #LrgAnalytics
 * @enabled: whether to enable analytics
 *
 * Sets whether analytics is enabled.
 * When disabled, events are silently dropped.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_set_enabled (LrgAnalytics *self,
                           gboolean      enabled);

/**
 * lrg_analytics_set_backend:
 * @self: an #LrgAnalytics
 * @backend: (nullable): the #LrgAnalyticsBackend to use
 *
 * Sets the analytics backend.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_set_backend (LrgAnalytics        *self,
                           LrgAnalyticsBackend *backend);

/**
 * lrg_analytics_get_backend:
 * @self: an #LrgAnalytics
 *
 * Gets the analytics backend.
 *
 * Returns: (transfer none) (nullable): the #LrgAnalyticsBackend
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAnalyticsBackend *
lrg_analytics_get_backend (LrgAnalytics *self);

/**
 * lrg_analytics_set_consent:
 * @self: an #LrgAnalytics
 * @consent: (nullable): the #LrgConsent manager
 *
 * Sets the consent manager.
 * If set, analytics will respect consent settings.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_set_consent (LrgAnalytics *self,
                           LrgConsent   *consent);

/**
 * lrg_analytics_get_consent:
 * @self: an #LrgAnalytics
 *
 * Gets the consent manager.
 *
 * Returns: (transfer none) (nullable): the #LrgConsent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgConsent *
lrg_analytics_get_consent (LrgAnalytics *self);

/* ==========================================================================
 * Session Management
 * ========================================================================== */

/**
 * lrg_analytics_get_session_id:
 * @self: an #LrgAnalytics
 *
 * Gets the current session ID.
 *
 * Returns: (transfer none) (nullable): the session ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_analytics_get_session_id (LrgAnalytics *self);

/**
 * lrg_analytics_get_session_start:
 * @self: an #LrgAnalytics
 *
 * Gets the session start time.
 *
 * Returns: (transfer none) (nullable): the session start time
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GDateTime *
lrg_analytics_get_session_start (LrgAnalytics *self);

/**
 * lrg_analytics_get_play_time:
 * @self: an #LrgAnalytics
 *
 * Gets the total play time in seconds since session started.
 *
 * Returns: play time in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_analytics_get_play_time (LrgAnalytics *self);

/**
 * lrg_analytics_start_session:
 * @self: an #LrgAnalytics
 *
 * Starts a new analytics session.
 * Generates a new session ID and emits "session-started".
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_start_session (LrgAnalytics *self);

/**
 * lrg_analytics_end_session:
 * @self: an #LrgAnalytics
 *
 * Ends the current session.
 * Flushes pending events and emits "session-ended".
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_end_session (LrgAnalytics *self);

/**
 * lrg_analytics_update:
 * @self: an #LrgAnalytics
 * @delta: time elapsed in seconds
 *
 * Updates the analytics system.
 * Call each frame to track play time.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_update (LrgAnalytics *self,
                      gfloat        delta);

/* ==========================================================================
 * Event Tracking
 * ========================================================================== */

/**
 * lrg_analytics_track_event:
 * @self: an #LrgAnalytics
 * @event: the #LrgAnalyticsEvent to track
 *
 * Tracks a custom analytics event.
 * The session ID is automatically set on the event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_track_event (LrgAnalytics      *self,
                           LrgAnalyticsEvent *event);

/**
 * lrg_analytics_track_simple:
 * @self: an #LrgAnalytics
 * @event_name: the event name
 *
 * Tracks a simple event with just a name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_track_simple (LrgAnalytics *self,
                            const gchar  *event_name);

/* ==========================================================================
 * Convenience Event Methods
 * ========================================================================== */

/**
 * lrg_analytics_track_screen_view:
 * @self: an #LrgAnalytics
 * @screen_name: the screen/view name
 *
 * Tracks a screen view event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_track_screen_view (LrgAnalytics *self,
                                 const gchar  *screen_name);

/**
 * lrg_analytics_track_game_start:
 * @self: an #LrgAnalytics
 *
 * Tracks a game start event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_track_game_start (LrgAnalytics *self);

/**
 * lrg_analytics_track_game_end:
 * @self: an #LrgAnalytics
 * @reason: (nullable): reason for ending (quit, crash, etc.)
 *
 * Tracks a game end event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_track_game_end (LrgAnalytics *self,
                              const gchar  *reason);

/**
 * lrg_analytics_track_level_start:
 * @self: an #LrgAnalytics
 * @level_name: the level name
 *
 * Tracks a level start event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_track_level_start (LrgAnalytics *self,
                                 const gchar  *level_name);

/**
 * lrg_analytics_track_level_end:
 * @self: an #LrgAnalytics
 * @level_name: the level name
 * @completed: whether the level was completed
 *
 * Tracks a level end event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_track_level_end (LrgAnalytics *self,
                               const gchar  *level_name,
                               gboolean      completed);

/* ==========================================================================
 * User Properties
 * ========================================================================== */

/**
 * lrg_analytics_set_user_property:
 * @self: an #LrgAnalytics
 * @key: property key
 * @value: (nullable): property value
 *
 * Sets a user property that will be included with all events.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_set_user_property (LrgAnalytics *self,
                                 const gchar  *key,
                                 const gchar  *value);

/**
 * lrg_analytics_increment_counter:
 * @self: an #LrgAnalytics
 * @counter_name: the counter name
 * @amount: amount to increment
 *
 * Increments a session counter.
 * Counters are reset when the session ends.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_increment_counter (LrgAnalytics *self,
                                 const gchar  *counter_name,
                                 gint64        amount);

/**
 * lrg_analytics_get_counter:
 * @self: an #LrgAnalytics
 * @counter_name: the counter name
 *
 * Gets the current value of a session counter.
 *
 * Returns: the counter value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_analytics_get_counter (LrgAnalytics *self,
                           const gchar  *counter_name);

/* ==========================================================================
 * Flush
 * ========================================================================== */

/**
 * lrg_analytics_flush:
 * @self: an #LrgAnalytics
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
lrg_analytics_flush (LrgAnalytics  *self,
                     GError       **error);

G_END_DECLS
