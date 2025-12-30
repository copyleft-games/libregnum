/* lrg-consent.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgConsent - GDPR-compliant consent management.
 */

#include "config.h"

#include "lrg-consent.h"
#include "../lrg-log.h"

#include <yaml-glib.h>

struct _LrgConsent
{
    GObject    parent_instance;

    gchar     *storage_path;
    gboolean   analytics_enabled;
    gboolean   crash_reporting_enabled;
    GDateTime *consent_date;
    guint      consent_version;
};

enum
{
    PROP_0,
    PROP_STORAGE_PATH,
    PROP_ANALYTICS_ENABLED,
    PROP_CRASH_REPORTING_ENABLED,
    PROP_CONSENT_DATE,
    PROP_CONSENT_VERSION,
    N_PROPS
};

enum
{
    SIGNAL_CONSENT_CHANGED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static LrgConsent *default_consent = NULL;

G_DEFINE_TYPE (LrgConsent, lrg_consent, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_consent_finalize (GObject *object)
{
    LrgConsent *self = LRG_CONSENT (object);

    g_clear_pointer (&self->storage_path, g_free);
    g_clear_pointer (&self->consent_date, g_date_time_unref);

    G_OBJECT_CLASS (lrg_consent_parent_class)->finalize (object);
}

static void
lrg_consent_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgConsent *self = LRG_CONSENT (object);

    switch (prop_id)
    {
    case PROP_STORAGE_PATH:
        g_value_set_string (value, self->storage_path);
        break;
    case PROP_ANALYTICS_ENABLED:
        g_value_set_boolean (value, self->analytics_enabled);
        break;
    case PROP_CRASH_REPORTING_ENABLED:
        g_value_set_boolean (value, self->crash_reporting_enabled);
        break;
    case PROP_CONSENT_DATE:
        g_value_set_boxed (value, self->consent_date);
        break;
    case PROP_CONSENT_VERSION:
        g_value_set_uint (value, self->consent_version);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_consent_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgConsent *self = LRG_CONSENT (object);

    switch (prop_id)
    {
    case PROP_STORAGE_PATH:
        g_clear_pointer (&self->storage_path, g_free);
        self->storage_path = g_value_dup_string (value);
        break;
    case PROP_ANALYTICS_ENABLED:
        lrg_consent_set_analytics_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_CRASH_REPORTING_ENABLED:
        lrg_consent_set_crash_reporting_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_CONSENT_VERSION:
        lrg_consent_set_consent_version (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_consent_class_init (LrgConsentClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_consent_finalize;
    object_class->get_property = lrg_consent_get_property;
    object_class->set_property = lrg_consent_set_property;

    /**
     * LrgConsent:storage-path:
     *
     * Path to persist consent settings.
     *
     * Since: 1.0
     */
    properties[PROP_STORAGE_PATH] =
        g_param_spec_string ("storage-path",
                             "Storage Path",
                             "Path to persist consent settings",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgConsent:analytics-enabled:
     *
     * Whether analytics collection is enabled.
     *
     * Since: 1.0
     */
    properties[PROP_ANALYTICS_ENABLED] =
        g_param_spec_boolean ("analytics-enabled",
                              "Analytics Enabled",
                              "Whether analytics is enabled",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgConsent:crash-reporting-enabled:
     *
     * Whether crash reporting is enabled.
     *
     * Since: 1.0
     */
    properties[PROP_CRASH_REPORTING_ENABLED] =
        g_param_spec_boolean ("crash-reporting-enabled",
                              "Crash Reporting Enabled",
                              "Whether crash reporting is enabled",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgConsent:consent-date:
     *
     * When consent was last given or modified.
     *
     * Since: 1.0
     */
    properties[PROP_CONSENT_DATE] =
        g_param_spec_boxed ("consent-date",
                            "Consent Date",
                            "When consent was given",
                            G_TYPE_DATE_TIME,
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgConsent:consent-version:
     *
     * Version of the consent form shown.
     *
     * Since: 1.0
     */
    properties[PROP_CONSENT_VERSION] =
        g_param_spec_uint ("consent-version",
                           "Consent Version",
                           "Version of consent form",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgConsent::consent-changed:
     * @self: the #LrgConsent that emitted the signal
     *
     * Emitted when any consent setting changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CONSENT_CHANGED] =
        g_signal_new ("consent-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_consent_init (LrgConsent *self)
{
    self->analytics_enabled = FALSE;
    self->crash_reporting_enabled = FALSE;
    self->consent_version = 0;
    self->consent_date = NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgConsent *
lrg_consent_new (const gchar *storage_path)
{
    return g_object_new (LRG_TYPE_CONSENT,
                         "storage-path", storage_path,
                         NULL);
}

LrgConsent *
lrg_consent_get_default (void)
{
    if (default_consent == NULL)
    {
        g_autofree gchar *config_dir = NULL;
        g_autofree gchar *consent_path = NULL;

        config_dir = g_build_filename (g_get_user_config_dir (),
                                       "libregnum",
                                       NULL);
        consent_path = g_build_filename (config_dir, "consent.yaml", NULL);

        default_consent = lrg_consent_new (consent_path);
    }

    return default_consent;
}

gboolean
lrg_consent_get_analytics_enabled (LrgConsent *self)
{
    g_return_val_if_fail (LRG_IS_CONSENT (self), FALSE);

    return self->analytics_enabled;
}

void
lrg_consent_set_analytics_enabled (LrgConsent *self,
                                   gboolean    enabled)
{
    g_return_if_fail (LRG_IS_CONSENT (self));

    if (self->analytics_enabled != enabled)
    {
        self->analytics_enabled = enabled;
        g_clear_pointer (&self->consent_date, g_date_time_unref);
        self->consent_date = g_date_time_new_now_utc ();
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANALYTICS_ENABLED]);
        g_signal_emit (self, signals[SIGNAL_CONSENT_CHANGED], 0);
    }
}

gboolean
lrg_consent_get_crash_reporting_enabled (LrgConsent *self)
{
    g_return_val_if_fail (LRG_IS_CONSENT (self), FALSE);

    return self->crash_reporting_enabled;
}

void
lrg_consent_set_crash_reporting_enabled (LrgConsent *self,
                                         gboolean    enabled)
{
    g_return_if_fail (LRG_IS_CONSENT (self));

    if (self->crash_reporting_enabled != enabled)
    {
        self->crash_reporting_enabled = enabled;
        g_clear_pointer (&self->consent_date, g_date_time_unref);
        self->consent_date = g_date_time_new_now_utc ();
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CRASH_REPORTING_ENABLED]);
        g_signal_emit (self, signals[SIGNAL_CONSENT_CHANGED], 0);
    }
}

GDateTime *
lrg_consent_get_consent_date (LrgConsent *self)
{
    g_return_val_if_fail (LRG_IS_CONSENT (self), NULL);

    return self->consent_date;
}

guint
lrg_consent_get_consent_version (LrgConsent *self)
{
    g_return_val_if_fail (LRG_IS_CONSENT (self), 0);

    return self->consent_version;
}

void
lrg_consent_set_consent_version (LrgConsent *self,
                                 guint       version)
{
    g_return_if_fail (LRG_IS_CONSENT (self));

    if (self->consent_version != version)
    {
        self->consent_version = version;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONSENT_VERSION]);
    }
}

void
lrg_consent_set_all (LrgConsent *self,
                     gboolean    enabled)
{
    g_return_if_fail (LRG_IS_CONSENT (self));

    /* Set without triggering individual signals */
    self->analytics_enabled = enabled;
    self->crash_reporting_enabled = enabled;
    g_clear_pointer (&self->consent_date, g_date_time_unref);
    self->consent_date = g_date_time_new_now_utc ();

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANALYTICS_ENABLED]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CRASH_REPORTING_ENABLED]);
    g_signal_emit (self, signals[SIGNAL_CONSENT_CHANGED], 0);
}

gboolean
lrg_consent_requires_prompt (LrgConsent *self)
{
    g_return_val_if_fail (LRG_IS_CONSENT (self), TRUE);

    /* If no consent date is set, user hasn't consented yet */
    return self->consent_date == NULL;
}

gboolean
lrg_consent_requires_reprompt (LrgConsent *self,
                               guint       current_version)
{
    g_return_val_if_fail (LRG_IS_CONSENT (self), TRUE);

    /* Prompt if never consented or if consent is for older version */
    if (self->consent_date == NULL)
        return TRUE;

    return self->consent_version < current_version;
}

gboolean
lrg_consent_load (LrgConsent  *self,
                  GError     **error)
{
    g_autoptr(YamlParser) parser = NULL;
    YamlNode *root;
    YamlMapping *root_map;
    YamlNode *node;

    g_return_val_if_fail (LRG_IS_CONSENT (self), FALSE);

    if (self->storage_path == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "No storage path set, skipping load");
        return TRUE;
    }

    if (!g_file_test (self->storage_path, G_FILE_TEST_EXISTS))
    {
        lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "Consent file does not exist: %s",
                   self->storage_path);
        return TRUE;
    }

    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, self->storage_path, error))
        return FALSE;

    root = yaml_parser_get_root (parser);
    if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
    {
        lrg_warning (LRG_LOG_DOMAIN_ANALYTICS, "Invalid consent file format");
        return TRUE;
    }

    root_map = yaml_node_get_mapping (root);

    node = yaml_mapping_get_member (root_map, "analytics");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
    {
        self->analytics_enabled = yaml_node_get_boolean (node);
    }

    node = yaml_mapping_get_member (root_map, "crash-reporting");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
    {
        self->crash_reporting_enabled = yaml_node_get_boolean (node);
    }

    node = yaml_mapping_get_member (root_map, "version");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
    {
        self->consent_version = (guint)yaml_node_get_int (node);
    }

    node = yaml_mapping_get_member (root_map, "date");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
    {
        const gchar *date_str = yaml_node_get_string (node);
        if (date_str != NULL)
        {
            g_clear_pointer (&self->consent_date, g_date_time_unref);
            self->consent_date = g_date_time_new_from_iso8601 (date_str, NULL);
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "Loaded consent from %s", self->storage_path);

    return TRUE;
}

gboolean
lrg_consent_save (LrgConsent  *self,
                  GError     **error)
{
    g_autoptr(GString) yaml = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *date_str = NULL;

    g_return_val_if_fail (LRG_IS_CONSENT (self), FALSE);

    if (self->storage_path == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "No storage path set, skipping save");
        return TRUE;
    }

    /* Ensure directory exists */
    dir = g_path_get_dirname (self->storage_path);
    if (g_mkdir_with_parents (dir, 0755) != 0)
    {
        g_set_error (error,
                     G_FILE_ERROR,
                     G_FILE_ERROR_FAILED,
                     "Failed to create directory: %s", dir);
        return FALSE;
    }

    yaml = g_string_new ("# Libregnum Consent Settings\n");
    g_string_append_printf (yaml, "analytics: %s\n",
                            self->analytics_enabled ? "true" : "false");
    g_string_append_printf (yaml, "crash-reporting: %s\n",
                            self->crash_reporting_enabled ? "true" : "false");
    g_string_append_printf (yaml, "version: %u\n", self->consent_version);

    if (self->consent_date != NULL)
    {
        date_str = g_date_time_format_iso8601 (self->consent_date);
        g_string_append_printf (yaml, "date: \"%s\"\n", date_str);
    }

    if (!g_file_set_contents (self->storage_path, yaml->str, yaml->len, error))
    {
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "Saved consent to %s", self->storage_path);

    return TRUE;
}
