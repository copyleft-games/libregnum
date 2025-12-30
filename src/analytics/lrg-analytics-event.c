/* lrg-analytics-event.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnalyticsEvent - Analytics event data container.
 */

#include "config.h"

#include "lrg-analytics-event.h"
#include "../lrg-log.h"

#include <json-glib/json-glib.h>

struct _LrgAnalyticsEvent
{
    GObject     parent_instance;

    gchar      *name;
    GDateTime  *timestamp;
    gchar      *session_id;
    GHashTable *properties;
};

enum
{
    PROP_0,
    PROP_NAME,
    PROP_TIMESTAMP,
    PROP_SESSION_ID,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgAnalyticsEvent, lrg_analytics_event, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_analytics_event_finalize (GObject *object)
{
    LrgAnalyticsEvent *self = LRG_ANALYTICS_EVENT (object);

    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->timestamp, g_date_time_unref);
    g_clear_pointer (&self->session_id, g_free);
    g_clear_pointer (&self->properties, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_analytics_event_parent_class)->finalize (object);
}

static void
lrg_analytics_event_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgAnalyticsEvent *self = LRG_ANALYTICS_EVENT (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_TIMESTAMP:
        g_value_set_boxed (value, self->timestamp);
        break;
    case PROP_SESSION_ID:
        g_value_set_string (value, self->session_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_analytics_event_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgAnalyticsEvent *self = LRG_ANALYTICS_EVENT (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_clear_pointer (&self->name, g_free);
        self->name = g_value_dup_string (value);
        break;
    case PROP_SESSION_ID:
        g_clear_pointer (&self->session_id, g_free);
        self->session_id = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_analytics_event_class_init (LrgAnalyticsEventClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_analytics_event_finalize;
    object_class->get_property = lrg_analytics_event_get_property;
    object_class->set_property = lrg_analytics_event_set_property;

    /**
     * LrgAnalyticsEvent:name:
     *
     * The event name/type identifier.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Event name/type identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgAnalyticsEvent:timestamp:
     *
     * The event timestamp.
     *
     * Since: 1.0
     */
    properties[PROP_TIMESTAMP] =
        g_param_spec_boxed ("timestamp",
                            "Timestamp",
                            "When the event occurred",
                            G_TYPE_DATE_TIME,
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgAnalyticsEvent:session-id:
     *
     * The session ID associated with this event.
     *
     * Since: 1.0
     */
    properties[PROP_SESSION_ID] =
        g_param_spec_string ("session-id",
                             "Session ID",
                             "Session identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_analytics_event_init (LrgAnalyticsEvent *self)
{
    self->timestamp = g_date_time_new_now_utc ();
    self->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, (GDestroyNotify)g_variant_unref);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAnalyticsEvent *
lrg_analytics_event_new (const gchar *name)
{
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_ANALYTICS_EVENT,
                         "name", name,
                         NULL);
}

const gchar *
lrg_analytics_event_get_name (LrgAnalyticsEvent *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_EVENT (self), NULL);

    return self->name;
}

GDateTime *
lrg_analytics_event_get_timestamp (LrgAnalyticsEvent *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_EVENT (self), NULL);

    return self->timestamp;
}

const gchar *
lrg_analytics_event_get_session_id (LrgAnalyticsEvent *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_EVENT (self), NULL);

    return self->session_id;
}

void
lrg_analytics_event_set_session_id (LrgAnalyticsEvent *self,
                                    const gchar       *session_id)
{
    g_return_if_fail (LRG_IS_ANALYTICS_EVENT (self));

    g_clear_pointer (&self->session_id, g_free);
    self->session_id = g_strdup (session_id);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION_ID]);
}

void
lrg_analytics_event_set_property_string (LrgAnalyticsEvent *self,
                                         const gchar       *key,
                                         const gchar       *value)
{
    g_return_if_fail (LRG_IS_ANALYTICS_EVENT (self));
    g_return_if_fail (key != NULL);

    g_hash_table_insert (self->properties,
                         g_strdup (key),
                         g_variant_ref_sink (g_variant_new_string (value ? value : "")));
}

void
lrg_analytics_event_set_property_int (LrgAnalyticsEvent *self,
                                      const gchar       *key,
                                      gint64             value)
{
    g_return_if_fail (LRG_IS_ANALYTICS_EVENT (self));
    g_return_if_fail (key != NULL);

    g_hash_table_insert (self->properties,
                         g_strdup (key),
                         g_variant_ref_sink (g_variant_new_int64 (value)));
}

void
lrg_analytics_event_set_property_double (LrgAnalyticsEvent *self,
                                         const gchar       *key,
                                         gdouble            value)
{
    g_return_if_fail (LRG_IS_ANALYTICS_EVENT (self));
    g_return_if_fail (key != NULL);

    g_hash_table_insert (self->properties,
                         g_strdup (key),
                         g_variant_ref_sink (g_variant_new_double (value)));
}

void
lrg_analytics_event_set_property_boolean (LrgAnalyticsEvent *self,
                                          const gchar       *key,
                                          gboolean           value)
{
    g_return_if_fail (LRG_IS_ANALYTICS_EVENT (self));
    g_return_if_fail (key != NULL);

    g_hash_table_insert (self->properties,
                         g_strdup (key),
                         g_variant_ref_sink (g_variant_new_boolean (value)));
}

const gchar *
lrg_analytics_event_get_property_string (LrgAnalyticsEvent *self,
                                         const gchar       *key)
{
    GVariant *variant;

    g_return_val_if_fail (LRG_IS_ANALYTICS_EVENT (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    variant = g_hash_table_lookup (self->properties, key);
    if (variant == NULL)
        return NULL;

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE_STRING))
        return NULL;

    return g_variant_get_string (variant, NULL);
}

GList *
lrg_analytics_event_get_property_keys (LrgAnalyticsEvent *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_EVENT (self), NULL);

    return g_hash_table_get_keys (self->properties);
}

gchar *
lrg_analytics_event_to_json (LrgAnalyticsEvent *self)
{
    g_autoptr(JsonBuilder) builder = NULL;
    g_autoptr(JsonGenerator) generator = NULL;
    g_autoptr(JsonNode) root = NULL;
    g_autofree gchar *timestamp_str = NULL;
    GHashTableIter iter;
    gpointer key, value;

    g_return_val_if_fail (LRG_IS_ANALYTICS_EVENT (self), NULL);

    builder = json_builder_new ();

    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "name");
    json_builder_add_string_value (builder, self->name ? self->name : "");

    json_builder_set_member_name (builder, "timestamp");
    timestamp_str = g_date_time_format_iso8601 (self->timestamp);
    json_builder_add_string_value (builder, timestamp_str);

    if (self->session_id != NULL)
    {
        json_builder_set_member_name (builder, "session_id");
        json_builder_add_string_value (builder, self->session_id);
    }

    /* Add custom properties */
    json_builder_set_member_name (builder, "properties");
    json_builder_begin_object (builder);

    g_hash_table_iter_init (&iter, self->properties);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        GVariant *variant = (GVariant *)value;
        const gchar *prop_key = (const gchar *)key;

        json_builder_set_member_name (builder, prop_key);

        if (g_variant_is_of_type (variant, G_VARIANT_TYPE_STRING))
        {
            json_builder_add_string_value (builder, g_variant_get_string (variant, NULL));
        }
        else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_INT64))
        {
            json_builder_add_int_value (builder, g_variant_get_int64 (variant));
        }
        else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_DOUBLE))
        {
            json_builder_add_double_value (builder, g_variant_get_double (variant));
        }
        else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_BOOLEAN))
        {
            json_builder_add_boolean_value (builder, g_variant_get_boolean (variant));
        }
    }

    json_builder_end_object (builder);
    json_builder_end_object (builder);

    root = json_builder_get_root (builder);
    generator = json_generator_new ();
    json_generator_set_root (generator, root);
    json_generator_set_pretty (generator, FALSE);

    return json_generator_to_data (generator, NULL);
}

gchar *
lrg_analytics_event_to_yaml (LrgAnalyticsEvent *self)
{
    GString *yaml;
    g_autofree gchar *timestamp_str = NULL;
    GHashTableIter iter;
    gpointer key, value;

    g_return_val_if_fail (LRG_IS_ANALYTICS_EVENT (self), NULL);

    yaml = g_string_new ("");

    g_string_append_printf (yaml, "name: \"%s\"\n", self->name ? self->name : "");

    timestamp_str = g_date_time_format_iso8601 (self->timestamp);
    g_string_append_printf (yaml, "timestamp: \"%s\"\n", timestamp_str);

    if (self->session_id != NULL)
    {
        g_string_append_printf (yaml, "session_id: \"%s\"\n", self->session_id);
    }

    g_string_append (yaml, "properties:\n");

    g_hash_table_iter_init (&iter, self->properties);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        GVariant *variant = (GVariant *)value;
        const gchar *prop_key = (const gchar *)key;

        if (g_variant_is_of_type (variant, G_VARIANT_TYPE_STRING))
        {
            g_string_append_printf (yaml, "  %s: \"%s\"\n",
                                    prop_key,
                                    g_variant_get_string (variant, NULL));
        }
        else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_INT64))
        {
            g_string_append_printf (yaml, "  %s: %" G_GINT64_FORMAT "\n",
                                    prop_key,
                                    g_variant_get_int64 (variant));
        }
        else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_DOUBLE))
        {
            g_string_append_printf (yaml, "  %s: %g\n",
                                    prop_key,
                                    g_variant_get_double (variant));
        }
        else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_BOOLEAN))
        {
            g_string_append_printf (yaml, "  %s: %s\n",
                                    prop_key,
                                    g_variant_get_boolean (variant) ? "true" : "false");
        }
    }

    return g_string_free (yaml, FALSE);
}
