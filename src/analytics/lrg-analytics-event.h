/* lrg-analytics-event.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnalyticsEvent - Analytics event data container.
 *
 * Represents a single analytics event with a name, timestamp,
 * session ID, and arbitrary key-value properties.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANALYTICS_EVENT (lrg_analytics_event_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAnalyticsEvent, lrg_analytics_event, LRG, ANALYTICS_EVENT, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_analytics_event_new:
 * @name: event name/type identifier
 *
 * Creates a new analytics event with the given name.
 * The timestamp is set to the current time automatically.
 *
 * Returns: (transfer full): a new #LrgAnalyticsEvent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAnalyticsEvent *
lrg_analytics_event_new (const gchar *name);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_analytics_event_get_name:
 * @self: an #LrgAnalyticsEvent
 *
 * Gets the event name/type.
 *
 * Returns: (transfer none): the event name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_analytics_event_get_name (LrgAnalyticsEvent *self);

/**
 * lrg_analytics_event_get_timestamp:
 * @self: an #LrgAnalyticsEvent
 *
 * Gets the event timestamp.
 *
 * Returns: (transfer none): the timestamp as #GDateTime
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GDateTime *
lrg_analytics_event_get_timestamp (LrgAnalyticsEvent *self);

/**
 * lrg_analytics_event_get_session_id:
 * @self: an #LrgAnalyticsEvent
 *
 * Gets the session ID associated with this event.
 *
 * Returns: (transfer none) (nullable): the session ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_analytics_event_get_session_id (LrgAnalyticsEvent *self);

/**
 * lrg_analytics_event_set_session_id:
 * @self: an #LrgAnalyticsEvent
 * @session_id: (nullable): the session ID
 *
 * Sets the session ID for this event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_event_set_session_id (LrgAnalyticsEvent *self,
                                    const gchar       *session_id);

/* ==========================================================================
 * Custom Properties
 * ========================================================================== */

/**
 * lrg_analytics_event_set_property_string:
 * @self: an #LrgAnalyticsEvent
 * @key: property key
 * @value: property value
 *
 * Sets a string property on the event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_event_set_property_string (LrgAnalyticsEvent *self,
                                         const gchar       *key,
                                         const gchar       *value);

/**
 * lrg_analytics_event_set_property_int:
 * @self: an #LrgAnalyticsEvent
 * @key: property key
 * @value: property value
 *
 * Sets an integer property on the event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_event_set_property_int (LrgAnalyticsEvent *self,
                                      const gchar       *key,
                                      gint64             value);

/**
 * lrg_analytics_event_set_property_double:
 * @self: an #LrgAnalyticsEvent
 * @key: property key
 * @value: property value
 *
 * Sets a floating-point property on the event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_event_set_property_double (LrgAnalyticsEvent *self,
                                         const gchar       *key,
                                         gdouble            value);

/**
 * lrg_analytics_event_set_property_boolean:
 * @self: an #LrgAnalyticsEvent
 * @key: property key
 * @value: property value
 *
 * Sets a boolean property on the event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_event_set_property_boolean (LrgAnalyticsEvent *self,
                                          const gchar       *key,
                                          gboolean           value);

/**
 * lrg_analytics_event_get_property_string:
 * @self: an #LrgAnalyticsEvent
 * @key: property key
 *
 * Gets a string property from the event.
 *
 * Returns: (transfer none) (nullable): the property value, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_analytics_event_get_property_string (LrgAnalyticsEvent *self,
                                         const gchar       *key);

/**
 * lrg_analytics_event_get_property_keys:
 * @self: an #LrgAnalyticsEvent
 *
 * Gets all property keys set on this event.
 *
 * Returns: (transfer container) (element-type utf8): list of keys
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_analytics_event_get_property_keys (LrgAnalyticsEvent *self);

/* ==========================================================================
 * Serialization
 * ========================================================================== */

/**
 * lrg_analytics_event_to_json:
 * @self: an #LrgAnalyticsEvent
 *
 * Serializes the event to JSON format.
 *
 * Returns: (transfer full): JSON string representation
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_analytics_event_to_json (LrgAnalyticsEvent *self);

/**
 * lrg_analytics_event_to_yaml:
 * @self: an #LrgAnalyticsEvent
 *
 * Serializes the event to YAML format.
 *
 * Returns: (transfer full): YAML string representation
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_analytics_event_to_yaml (LrgAnalyticsEvent *self);

G_END_DECLS
