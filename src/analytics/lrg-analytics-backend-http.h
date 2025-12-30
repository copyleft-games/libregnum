/* lrg-analytics-backend-http.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnalyticsBackendHttp - HTTP analytics backend.
 *
 * Sends analytics events to an HTTP endpoint as JSON or YAML payloads.
 * Supports batching, retry on failure, and custom headers.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-analytics-backend.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANALYTICS_BACKEND_HTTP (lrg_analytics_backend_http_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAnalyticsBackendHttp, lrg_analytics_backend_http, LRG, ANALYTICS_BACKEND_HTTP, LrgAnalyticsBackend)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_analytics_backend_http_new:
 * @endpoint_url: the HTTP endpoint URL
 *
 * Creates a new HTTP analytics backend.
 *
 * Returns: (transfer full): a new #LrgAnalyticsBackendHttp
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAnalyticsBackendHttp *
lrg_analytics_backend_http_new (const gchar *endpoint_url);

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_analytics_backend_http_get_endpoint_url:
 * @self: an #LrgAnalyticsBackendHttp
 *
 * Gets the endpoint URL.
 *
 * Returns: (transfer none): the endpoint URL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_analytics_backend_http_get_endpoint_url (LrgAnalyticsBackendHttp *self);

/**
 * lrg_analytics_backend_http_get_api_key:
 * @self: an #LrgAnalyticsBackendHttp
 *
 * Gets the API key used for authentication.
 *
 * Returns: (transfer none) (nullable): the API key
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_analytics_backend_http_get_api_key (LrgAnalyticsBackendHttp *self);

/**
 * lrg_analytics_backend_http_set_api_key:
 * @self: an #LrgAnalyticsBackendHttp
 * @api_key: (nullable): the API key
 *
 * Sets the API key for authentication.
 * The key is sent as an Authorization header.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_backend_http_set_api_key (LrgAnalyticsBackendHttp *self,
                                        const gchar             *api_key);

/**
 * lrg_analytics_backend_http_set_header:
 * @self: an #LrgAnalyticsBackendHttp
 * @name: header name
 * @value: header value
 *
 * Sets a custom HTTP header to include in requests.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_backend_http_set_header (LrgAnalyticsBackendHttp *self,
                                       const gchar             *name,
                                       const gchar             *value);

/**
 * lrg_analytics_backend_http_get_format:
 * @self: an #LrgAnalyticsBackendHttp
 *
 * Gets the payload format (JSON or YAML).
 *
 * Returns: the #LrgAnalyticsFormat
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAnalyticsFormat
lrg_analytics_backend_http_get_format (LrgAnalyticsBackendHttp *self);

/**
 * lrg_analytics_backend_http_set_format:
 * @self: an #LrgAnalyticsBackendHttp
 * @format: the #LrgAnalyticsFormat
 *
 * Sets the payload format.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_backend_http_set_format (LrgAnalyticsBackendHttp *self,
                                       LrgAnalyticsFormat       format);

/* ==========================================================================
 * Batching Configuration
 * ========================================================================== */

/**
 * lrg_analytics_backend_http_get_batch_size:
 * @self: an #LrgAnalyticsBackendHttp
 *
 * Gets the number of events to batch before sending.
 *
 * Returns: the batch size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_analytics_backend_http_get_batch_size (LrgAnalyticsBackendHttp *self);

/**
 * lrg_analytics_backend_http_set_batch_size:
 * @self: an #LrgAnalyticsBackendHttp
 * @batch_size: number of events to batch (1-1000)
 *
 * Sets the batch size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_backend_http_set_batch_size (LrgAnalyticsBackendHttp *self,
                                           guint                    batch_size);

/**
 * lrg_analytics_backend_http_get_flush_interval:
 * @self: an #LrgAnalyticsBackendHttp
 *
 * Gets the automatic flush interval in seconds.
 *
 * Returns: the flush interval
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_analytics_backend_http_get_flush_interval (LrgAnalyticsBackendHttp *self);

/**
 * lrg_analytics_backend_http_set_flush_interval:
 * @self: an #LrgAnalyticsBackendHttp
 * @interval: flush interval in seconds (0 to disable)
 *
 * Sets the automatic flush interval.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_backend_http_set_flush_interval (LrgAnalyticsBackendHttp *self,
                                               guint                    interval);

/**
 * lrg_analytics_backend_http_get_retry_count:
 * @self: an #LrgAnalyticsBackendHttp
 *
 * Gets the number of retry attempts on failure.
 *
 * Returns: the retry count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_analytics_backend_http_get_retry_count (LrgAnalyticsBackendHttp *self);

/**
 * lrg_analytics_backend_http_set_retry_count:
 * @self: an #LrgAnalyticsBackendHttp
 * @count: number of retries (0-10)
 *
 * Sets the retry count.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_analytics_backend_http_set_retry_count (LrgAnalyticsBackendHttp *self,
                                            guint                    count);

/* ==========================================================================
 * Status
 * ========================================================================== */

/**
 * lrg_analytics_backend_http_get_pending_count:
 * @self: an #LrgAnalyticsBackendHttp
 *
 * Gets the number of pending events in the queue.
 *
 * Returns: the pending event count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_analytics_backend_http_get_pending_count (LrgAnalyticsBackendHttp *self);

G_END_DECLS
