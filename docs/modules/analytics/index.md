# Analytics Module

The Analytics module provides a privacy-first, GDPR-compliant analytics and telemetry system for tracking game events and player behavior.

## Overview

The module consists of five main components:

1. **LrgAnalyticsEvent** - Event data container with typed properties
2. **LrgConsent** - GDPR consent management with persistence
3. **LrgAnalyticsBackend** - Abstract base class for custom backends
4. **LrgAnalyticsBackendHttp** - HTTP backend implementation (JSON/YAML)
5. **LrgAnalytics** - Singleton manager with session tracking

## Key Features

- **Privacy-first design**: Analytics disabled by default, requires explicit consent
- **Consent management**: GDPR-compliant consent tracking with version control
- **Extensible backends**: Abstract interface allows custom implementations
- **HTTP backend**: Built-in support for sending events via HTTP/HTTPS
- **Session tracking**: Automatic session management with play time tracking
- **Event properties**: Typed properties (string, int, double, boolean)
- **Counters**: Session-scoped counters for aggregated metrics
- **User properties**: Persistent properties attached to all events
- **Serialization**: JSON and YAML output formats

## Quick Start

### Basic Setup

```c
/* Get the analytics singleton */
LrgAnalytics *analytics = lrg_analytics_get_default ();

/* Create and configure HTTP backend */
LrgAnalyticsBackendHttp *http = lrg_analytics_backend_http_new ("https://analytics.example.com/events");
lrg_analytics_backend_http_set_api_key (http, "your-api-key");
lrg_analytics_backend_http_set_format (http, LRG_ANALYTICS_FORMAT_JSON);

/* Set the backend */
lrg_analytics_set_backend (analytics, LRG_ANALYTICS_BACKEND (http));

/* Enable analytics (only after user consent!) */
lrg_analytics_set_enabled (analytics, TRUE);

/* Start a session */
lrg_analytics_start_session (analytics);
```

### Tracking Events

```c
/* Track a simple event */
lrg_analytics_track_simple (analytics, "button_clicked");

/* Track a screen view */
lrg_analytics_track_screen_view (analytics, "main_menu");

/* Track game lifecycle events */
lrg_analytics_track_game_start (analytics);
lrg_analytics_track_level_start (analytics, "level_1");
lrg_analytics_track_level_end (analytics, "level_1", TRUE);  /* completed */
lrg_analytics_track_game_end (analytics, "quit");

/* Track custom event with properties */
g_autoptr(LrgAnalyticsEvent) event = lrg_analytics_event_new ("purchase");
lrg_analytics_event_set_property_string (event, "item_id", "sword_01");
lrg_analytics_event_set_property_int (event, "quantity", 1);
lrg_analytics_event_set_property_double (event, "price", 9.99);
lrg_analytics_track_event (analytics, event);
```

### Session Counters

```c
/* Increment counters during gameplay */
lrg_analytics_increment_counter (analytics, "enemies_killed", 1);
lrg_analytics_increment_counter (analytics, "coins_collected", 50);

/* Read counter values */
gint64 kills = lrg_analytics_get_counter (analytics, "enemies_killed");
```

### User Properties

```c
/* Set properties that are attached to all events */
lrg_analytics_set_user_property (analytics, "user_type", "premium");
lrg_analytics_set_user_property (analytics, "platform", "linux");
```

### Session Management

```c
/* Update play time each frame */
lrg_analytics_update (analytics, delta_time);

/* Get session information */
const gchar *session_id = lrg_analytics_get_session_id (analytics);
GDateTime *start = lrg_analytics_get_session_start (analytics);
gdouble play_time = lrg_analytics_get_play_time (analytics);

/* End session (flushes events) */
lrg_analytics_end_session (analytics);
```

## Consent Management

### Using LrgConsent

```c
LrgConsent *consent = lrg_consent_get_default ();

/* Check if consent prompt is needed */
if (lrg_consent_requires_prompt (consent))
{
    /* Show consent dialog to user */
    show_consent_dialog ();
}

/* Grant or revoke consent */
lrg_consent_set_analytics_enabled (consent, user_agreed_to_analytics);
lrg_consent_set_crash_reporting_enabled (consent, user_agreed_to_crash);

/* Or grant/revoke all at once */
lrg_consent_grant_all (consent);
lrg_consent_revoke_all (consent);

/* Save consent to disk */
g_autoptr(GError) error = NULL;
if (!lrg_consent_save (consent, &error))
{
    g_warning ("Failed to save consent: %s", error->message);
}
```

### Consent Signals

```c
/* Listen for consent changes */
g_signal_connect (consent, "consent-changed",
                  G_CALLBACK (on_consent_changed), NULL);

static void
on_consent_changed (LrgConsent *consent,
                    gpointer    user_data)
{
    /* Update analytics enabled state based on consent */
    LrgAnalytics *analytics = lrg_analytics_get_default ();
    lrg_analytics_set_enabled (analytics,
                               lrg_consent_get_analytics_enabled (consent));
}
```

## Custom Backends

### Creating a Custom Backend

```c
#define MY_TYPE_CUSTOM_BACKEND (my_custom_backend_get_type ())
G_DECLARE_FINAL_TYPE (MyCustomBackend, my_custom_backend, MY, CUSTOM_BACKEND, LrgAnalyticsBackend)

struct _MyCustomBackend
{
    LrgAnalyticsBackend parent_instance;
    /* Your fields here */
};

G_DEFINE_TYPE (MyCustomBackend, my_custom_backend, LRG_TYPE_ANALYTICS_BACKEND)

static gboolean
my_custom_backend_send_event (LrgAnalyticsBackend  *backend,
                              LrgAnalyticsEvent    *event,
                              GError              **error)
{
    MyCustomBackend *self = MY_CUSTOM_BACKEND (backend);

    /* Convert event to your format */
    g_autofree gchar *json = lrg_analytics_event_to_json (event);

    /* Send to your service */
    /* ... */

    return TRUE;
}

static gboolean
my_custom_backend_flush (LrgAnalyticsBackend  *backend,
                         GError              **error)
{
    /* Flush any pending events */
    return TRUE;
}

static void
my_custom_backend_class_init (MyCustomBackendClass *klass)
{
    LrgAnalyticsBackendClass *backend_class = LRG_ANALYTICS_BACKEND_CLASS (klass);

    backend_class->send_event = my_custom_backend_send_event;
    backend_class->flush = my_custom_backend_flush;
}
```

## HTTP Backend Configuration

```c
LrgAnalyticsBackendHttp *http = lrg_analytics_backend_http_new ("https://api.example.com/events");

/* Authentication */
lrg_analytics_backend_http_set_api_key (http, "api-key-here");

/* Custom headers */
lrg_analytics_backend_http_set_header (http, "X-Custom-Header", "value");

/* Payload format */
lrg_analytics_backend_http_set_format (http, LRG_ANALYTICS_FORMAT_JSON);
/* or */
lrg_analytics_backend_http_set_format (http, LRG_ANALYTICS_FORMAT_YAML);

/* Batching configuration */
lrg_analytics_backend_http_set_batch_size (http, 50);      /* Events per batch */
lrg_analytics_backend_http_set_flush_interval (http, 30);  /* Seconds */

/* Retry configuration */
lrg_analytics_backend_http_set_retry_count (http, 3);

/* Check pending events */
guint pending = lrg_analytics_backend_http_get_pending_count (http);
```

## Event Serialization

### JSON Format

```c
g_autofree gchar *json = lrg_analytics_event_to_json (event);
/* Output:
{
  "event": "level_complete",
  "timestamp": "2025-01-15T10:30:00Z",
  "session_id": "abc123",
  "properties": {
    "level_name": "tutorial",
    "score": 1500,
    "time_seconds": 120.5,
    "first_attempt": true
  }
}
*/
```

### YAML Format

```c
g_autofree gchar *yaml = lrg_analytics_event_to_yaml (event);
/* Output:
event: level_complete
timestamp: 2025-01-15T10:30:00Z
session_id: abc123
properties:
  level_name: tutorial
  score: 1500
  time_seconds: 120.5
  first_attempt: true
*/
```

## Type Reference

### LrgAnalyticsEvent

| Property | Type | Description |
|----------|------|-------------|
| name | string | Event name (read-only after creation) |
| timestamp | GDateTime | When the event occurred |
| session-id | string | Associated session ID |

### LrgConsent

| Property | Type | Description |
|----------|------|-------------|
| analytics-enabled | boolean | Analytics consent granted |
| crash-reporting-enabled | boolean | Crash reporting consent granted |
| consent-date | GDateTime | When consent was given/changed |
| consent-version | string | Version of consent policy |

### LrgAnalyticsBackend

| Property | Type | Description |
|----------|------|-------------|
| name | string | Backend identifier |
| enabled | boolean | Whether backend is active |

### LrgAnalyticsBackendHttp

| Property | Type | Description |
|----------|------|-------------|
| endpoint-url | string | HTTP endpoint URL |
| api-key | string | Authentication key |
| format | LrgAnalyticsFormat | Payload format (JSON/YAML) |
| batch-size | uint | Events per batch (1-1000) |
| flush-interval | uint | Auto-flush seconds (0=disabled) |
| retry-count | uint | Retry attempts (0-10) |

### LrgAnalytics

| Property | Type | Description |
|----------|------|-------------|
| enabled | boolean | Master enable switch |
| backend | LrgAnalyticsBackend | Active backend |
| consent | LrgConsent | Consent manager |
| session-id | string | Current session ID |
| play-time | double | Seconds since session start |

## Signals

### LrgConsent Signals

- `consent-changed` - Emitted when any consent setting changes

### LrgAnalytics Signals

- `session-started` - Emitted when a new session begins
- `session-ended` - Emitted when a session ends
- `event-tracked` - Emitted when an event is tracked

## Error Handling

```c
typedef enum {
    LRG_ANALYTICS_ERROR_FAILED,      /* Generic failure */
    LRG_ANALYTICS_ERROR_NETWORK,     /* Connection failed, timeout */
    LRG_ANALYTICS_ERROR_CONSENT,     /* Consent not granted */
    LRG_ANALYTICS_ERROR_DISABLED,    /* Analytics disabled */
    LRG_ANALYTICS_ERROR_BACKEND,     /* Backend error */
    LRG_ANALYTICS_ERROR_SERIALIZE    /* Serialization error */
} LrgAnalyticsError;

/* Example error handling */
g_autoptr(GError) error = NULL;
if (!lrg_analytics_flush (analytics, &error))
{
    if (g_error_matches (error, LRG_ANALYTICS_ERROR, LRG_ANALYTICS_ERROR_NETWORK))
        g_warning ("Network error, events will be retried");
}
```

## Best Practices

1. **Always request consent before enabling analytics**
2. **Use meaningful event names** (snake_case recommended)
3. **Avoid PII** (personally identifiable information) in event properties
4. **Batch events** to reduce network overhead
5. **Handle errors gracefully** - analytics should never break your game
6. **Flush on exit** - call `lrg_analytics_end_session()` before quitting
7. **Update regularly** - call `lrg_analytics_update()` each frame for accurate play time
