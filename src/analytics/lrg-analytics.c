/* lrg-analytics.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnalytics - Singleton analytics manager.
 */

#include "config.h"

#include "lrg-analytics.h"
#include "../lrg-enums.h"
#include "../lrg-log.h"

struct _LrgAnalytics
{
    GObject              parent_instance;

    gboolean             enabled;
    LrgAnalyticsBackend *backend;
    LrgConsent          *consent;

    /* Session tracking */
    gchar               *session_id;
    GDateTime           *session_start;
    gdouble              play_time;
    gboolean             session_active;

    /* User properties */
    GHashTable          *user_properties;

    /* Session counters */
    GHashTable          *counters;
};

enum
{
    PROP_0,
    PROP_ENABLED,
    PROP_SESSION_ID,
    PROP_PLAY_TIME,
    N_PROPS
};

enum
{
    SIGNAL_SESSION_STARTED,
    SIGNAL_SESSION_ENDED,
    SIGNAL_EVENT_TRACKED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static LrgAnalytics *default_analytics = NULL;

G_DEFINE_TYPE (LrgAnalytics, lrg_analytics, G_TYPE_OBJECT)

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static gchar *
generate_session_id (void)
{
    g_autoptr(GDateTime) now = g_date_time_new_now_utc ();
    g_autofree gchar *uuid = g_uuid_string_random ();

    /* Use first 8 chars of UUID plus timestamp */
    return g_strdup_printf ("%.*s-%ld",
                            8, uuid,
                            g_date_time_to_unix (now));
}

static gboolean
should_track (LrgAnalytics *self)
{
    if (!self->enabled)
        return FALSE;

    if (self->consent != NULL)
    {
        if (!lrg_consent_get_analytics_enabled (self->consent))
            return FALSE;
    }

    if (self->backend == NULL)
        return FALSE;

    if (!lrg_analytics_backend_is_enabled (self->backend))
        return FALSE;

    return TRUE;
}

static void
apply_user_properties (LrgAnalytics      *self,
                       LrgAnalyticsEvent *event)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, self->user_properties);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        lrg_analytics_event_set_property_string (event,
                                                 (const gchar *)key,
                                                 (const gchar *)value);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_analytics_finalize (GObject *object)
{
    LrgAnalytics *self = LRG_ANALYTICS (object);

    g_clear_pointer (&self->session_id, g_free);
    g_clear_pointer (&self->session_start, g_date_time_unref);
    g_clear_pointer (&self->user_properties, g_hash_table_unref);
    g_clear_pointer (&self->counters, g_hash_table_unref);
    g_clear_object (&self->backend);
    g_clear_object (&self->consent);

    G_OBJECT_CLASS (lrg_analytics_parent_class)->finalize (object);
}

static void
lrg_analytics_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgAnalytics *self = LRG_ANALYTICS (object);

    switch (prop_id)
    {
    case PROP_ENABLED:
        g_value_set_boolean (value, self->enabled);
        break;
    case PROP_SESSION_ID:
        g_value_set_string (value, self->session_id);
        break;
    case PROP_PLAY_TIME:
        g_value_set_double (value, self->play_time);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_analytics_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgAnalytics *self = LRG_ANALYTICS (object);

    switch (prop_id)
    {
    case PROP_ENABLED:
        self->enabled = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_analytics_class_init (LrgAnalyticsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_analytics_finalize;
    object_class->get_property = lrg_analytics_get_property;
    object_class->set_property = lrg_analytics_set_property;

    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether analytics is enabled",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    properties[PROP_SESSION_ID] =
        g_param_spec_string ("session-id",
                             "Session ID",
                             "Current session identifier",
                             NULL,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAY_TIME] =
        g_param_spec_double ("play-time",
                             "Play Time",
                             "Total play time in seconds",
                             0.0, G_MAXDOUBLE, 0.0,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgAnalytics::session-started:
     * @self: the #LrgAnalytics that emitted the signal
     *
     * Emitted when a new session starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_SESSION_STARTED] =
        g_signal_new ("session-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgAnalytics::session-ended:
     * @self: the #LrgAnalytics that emitted the signal
     * @play_time: total session play time in seconds
     *
     * Emitted when a session ends.
     *
     * Since: 1.0
     */
    signals[SIGNAL_SESSION_ENDED] =
        g_signal_new ("session-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_DOUBLE);

    /**
     * LrgAnalytics::event-tracked:
     * @self: the #LrgAnalytics that emitted the signal
     * @event: the #LrgAnalyticsEvent that was tracked
     *
     * Emitted after an event is tracked (for debugging/logging).
     *
     * Since: 1.0
     */
    signals[SIGNAL_EVENT_TRACKED] =
        g_signal_new ("event-tracked",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_ANALYTICS_EVENT);
}

static void
lrg_analytics_init (LrgAnalytics *self)
{
    self->enabled = TRUE;
    self->session_active = FALSE;
    self->play_time = 0.0;

    self->user_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free, g_free);
    self->counters = g_hash_table_new_full (g_str_hash, g_str_equal,
                                             g_free, NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAnalytics *
lrg_analytics_get_default (void)
{
    if (default_analytics == NULL)
    {
        default_analytics = g_object_new (LRG_TYPE_ANALYTICS, NULL);
    }

    return default_analytics;
}

gboolean
lrg_analytics_get_enabled (LrgAnalytics *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS (self), FALSE);

    return self->enabled;
}

void
lrg_analytics_set_enabled (LrgAnalytics *self,
                           gboolean      enabled)
{
    g_return_if_fail (LRG_IS_ANALYTICS (self));

    if (self->enabled != enabled)
    {
        self->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
    }
}

void
lrg_analytics_set_backend (LrgAnalytics        *self,
                           LrgAnalyticsBackend *backend)
{
    g_return_if_fail (LRG_IS_ANALYTICS (self));

    if (g_set_object (&self->backend, backend))
    {
        lrg_debug (LRG_LOG_DOMAIN_ANALYTICS,
                       "Set backend: %s",
                       backend != NULL ? lrg_analytics_backend_get_name (backend) : "none");
    }
}

LrgAnalyticsBackend *
lrg_analytics_get_backend (LrgAnalytics *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS (self), NULL);

    return self->backend;
}

void
lrg_analytics_set_consent (LrgAnalytics *self,
                           LrgConsent   *consent)
{
    g_return_if_fail (LRG_IS_ANALYTICS (self));

    g_set_object (&self->consent, consent);
}

LrgConsent *
lrg_analytics_get_consent (LrgAnalytics *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS (self), NULL);

    return self->consent;
}

const gchar *
lrg_analytics_get_session_id (LrgAnalytics *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS (self), NULL);

    return self->session_id;
}

GDateTime *
lrg_analytics_get_session_start (LrgAnalytics *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS (self), NULL);

    return self->session_start;
}

gdouble
lrg_analytics_get_play_time (LrgAnalytics *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS (self), 0.0);

    return self->play_time;
}

void
lrg_analytics_start_session (LrgAnalytics *self)
{
    g_return_if_fail (LRG_IS_ANALYTICS (self));

    /* End any existing session */
    if (self->session_active)
    {
        lrg_analytics_end_session (self);
    }

    /* Generate new session */
    g_clear_pointer (&self->session_id, g_free);
    g_clear_pointer (&self->session_start, g_date_time_unref);

    self->session_id = generate_session_id ();
    self->session_start = g_date_time_new_now_utc ();
    self->play_time = 0.0;
    self->session_active = TRUE;

    /* Clear session counters */
    g_hash_table_remove_all (self->counters);

    lrg_info (LRG_LOG_DOMAIN_ANALYTICS, "Session started: %s", self->session_id);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION_ID]);
    g_signal_emit (self, signals[SIGNAL_SESSION_STARTED], 0);
}

void
lrg_analytics_end_session (LrgAnalytics *self)
{
    gdouble final_play_time;

    g_return_if_fail (LRG_IS_ANALYTICS (self));

    if (!self->session_active)
        return;

    final_play_time = self->play_time;
    self->session_active = FALSE;

    /* Flush pending events */
    if (self->backend != NULL)
    {
        g_autoptr(GError) error = NULL;

        if (!lrg_analytics_backend_flush (self->backend, &error))
        {
            lrg_warning (LRG_LOG_DOMAIN_ANALYTICS,
                             "Failed to flush on session end: %s",
                             error->message);
        }
    }

    lrg_info (LRG_LOG_DOMAIN_ANALYTICS,
                  "Session ended: %s (%.1f seconds)",
                  self->session_id, final_play_time);

    g_signal_emit (self, signals[SIGNAL_SESSION_ENDED], 0, final_play_time);
}

void
lrg_analytics_update (LrgAnalytics *self,
                      gfloat        delta)
{
    g_return_if_fail (LRG_IS_ANALYTICS (self));

    if (self->session_active)
    {
        self->play_time += (gdouble)delta;
    }
}

void
lrg_analytics_track_event (LrgAnalytics      *self,
                           LrgAnalyticsEvent *event)
{
    g_autoptr(GError) error = NULL;

    g_return_if_fail (LRG_IS_ANALYTICS (self));
    g_return_if_fail (LRG_IS_ANALYTICS_EVENT (event));

    if (!should_track (self))
    {
        lrg_debug (LRG_LOG_DOMAIN_ANALYTICS,
                       "Dropping event '%s' (tracking disabled)",
                       lrg_analytics_event_get_name (event));
        return;
    }

    /* Set session ID */
    if (self->session_id != NULL)
    {
        lrg_analytics_event_set_session_id (event, self->session_id);
    }

    /* Apply user properties */
    apply_user_properties (self, event);

    /* Send to backend */
    if (!lrg_analytics_backend_send_event (self->backend, event, &error))
    {
        lrg_warning (LRG_LOG_DOMAIN_ANALYTICS,
                         "Failed to send event '%s': %s",
                         lrg_analytics_event_get_name (event),
                         error->message);
    }

    g_signal_emit (self, signals[SIGNAL_EVENT_TRACKED], 0, event);
}

void
lrg_analytics_track_simple (LrgAnalytics *self,
                            const gchar  *event_name)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;

    g_return_if_fail (LRG_IS_ANALYTICS (self));
    g_return_if_fail (event_name != NULL);

    event = lrg_analytics_event_new (event_name);
    lrg_analytics_track_event (self, event);
}

void
lrg_analytics_track_screen_view (LrgAnalytics *self,
                                 const gchar  *screen_name)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;

    g_return_if_fail (LRG_IS_ANALYTICS (self));
    g_return_if_fail (screen_name != NULL);

    event = lrg_analytics_event_new ("screen_view");
    lrg_analytics_event_set_property_string (event, "screen_name", screen_name);
    lrg_analytics_track_event (self, event);
}

void
lrg_analytics_track_game_start (LrgAnalytics *self)
{
    g_return_if_fail (LRG_IS_ANALYTICS (self));

    lrg_analytics_track_simple (self, "game_start");
}

void
lrg_analytics_track_game_end (LrgAnalytics *self,
                              const gchar  *reason)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;

    g_return_if_fail (LRG_IS_ANALYTICS (self));

    event = lrg_analytics_event_new ("game_end");
    if (reason != NULL)
    {
        lrg_analytics_event_set_property_string (event, "reason", reason);
    }
    lrg_analytics_event_set_property_double (event, "play_time", self->play_time);
    lrg_analytics_track_event (self, event);
}

void
lrg_analytics_track_level_start (LrgAnalytics *self,
                                 const gchar  *level_name)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;

    g_return_if_fail (LRG_IS_ANALYTICS (self));
    g_return_if_fail (level_name != NULL);

    event = lrg_analytics_event_new ("level_start");
    lrg_analytics_event_set_property_string (event, "level_name", level_name);
    lrg_analytics_track_event (self, event);
}

void
lrg_analytics_track_level_end (LrgAnalytics *self,
                               const gchar  *level_name,
                               gboolean      completed)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;

    g_return_if_fail (LRG_IS_ANALYTICS (self));
    g_return_if_fail (level_name != NULL);

    event = lrg_analytics_event_new ("level_end");
    lrg_analytics_event_set_property_string (event, "level_name", level_name);
    lrg_analytics_event_set_property_boolean (event, "completed", completed);
    lrg_analytics_track_event (self, event);
}

void
lrg_analytics_set_user_property (LrgAnalytics *self,
                                 const gchar  *key,
                                 const gchar  *value)
{
    g_return_if_fail (LRG_IS_ANALYTICS (self));
    g_return_if_fail (key != NULL);

    if (value != NULL)
    {
        g_hash_table_insert (self->user_properties, g_strdup (key), g_strdup (value));
    }
    else
    {
        g_hash_table_remove (self->user_properties, key);
    }
}

void
lrg_analytics_increment_counter (LrgAnalytics *self,
                                 const gchar  *counter_name,
                                 gint64        amount)
{
    gint64 current;

    g_return_if_fail (LRG_IS_ANALYTICS (self));
    g_return_if_fail (counter_name != NULL);

    current = GPOINTER_TO_INT (g_hash_table_lookup (self->counters, counter_name));
    current += amount;
    g_hash_table_insert (self->counters,
                         g_strdup (counter_name),
                         GINT_TO_POINTER (current));
}

gint64
lrg_analytics_get_counter (LrgAnalytics *self,
                           const gchar  *counter_name)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS (self), 0);
    g_return_val_if_fail (counter_name != NULL, 0);

    return GPOINTER_TO_INT (g_hash_table_lookup (self->counters, counter_name));
}

gboolean
lrg_analytics_flush (LrgAnalytics  *self,
                     GError       **error)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS (self), FALSE);

    if (self->backend == NULL)
    {
        g_set_error (error,
                     LRG_ANALYTICS_ERROR,
                     LRG_ANALYTICS_ERROR_BACKEND,
                     "No backend configured");
        return FALSE;
    }

    return lrg_analytics_backend_flush (self->backend, error);
}
