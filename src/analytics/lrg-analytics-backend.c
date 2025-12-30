/* lrg-analytics-backend.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnalyticsBackend - Abstract base class for analytics backends.
 */

#include "config.h"

#include "lrg-analytics-backend.h"
#include "../lrg-log.h"

typedef struct
{
    gchar    *name;
    gboolean  enabled;
} LrgAnalyticsBackendPrivate;

enum
{
    PROP_0,
    PROP_NAME,
    PROP_ENABLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE_WITH_PRIVATE (LrgAnalyticsBackend, lrg_analytics_backend, G_TYPE_OBJECT)

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_analytics_backend_real_send_event (LrgAnalyticsBackend  *self,
                                       LrgAnalyticsEvent    *event,
                                       GError              **error)
{
    /* Default implementation does nothing */
    (void)self;
    (void)event;
    (void)error;

    lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "send_event not implemented by subclass");

    return TRUE;
}

static gboolean
lrg_analytics_backend_real_flush (LrgAnalyticsBackend  *self,
                                  GError              **error)
{
    /* Default implementation does nothing */
    (void)self;
    (void)error;

    return TRUE;
}

static gboolean
lrg_analytics_backend_real_is_enabled (LrgAnalyticsBackend *self)
{
    LrgAnalyticsBackendPrivate *priv = lrg_analytics_backend_get_instance_private (self);

    return priv->enabled;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_analytics_backend_finalize (GObject *object)
{
    LrgAnalyticsBackend *self = LRG_ANALYTICS_BACKEND (object);
    LrgAnalyticsBackendPrivate *priv = lrg_analytics_backend_get_instance_private (self);

    g_clear_pointer (&priv->name, g_free);

    G_OBJECT_CLASS (lrg_analytics_backend_parent_class)->finalize (object);
}

static void
lrg_analytics_backend_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgAnalyticsBackend *self = LRG_ANALYTICS_BACKEND (object);
    LrgAnalyticsBackendPrivate *priv = lrg_analytics_backend_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_analytics_backend_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgAnalyticsBackend *self = LRG_ANALYTICS_BACKEND (object);
    LrgAnalyticsBackendPrivate *priv = lrg_analytics_backend_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_clear_pointer (&priv->name, g_free);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_ENABLED:
        priv->enabled = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_analytics_backend_class_init (LrgAnalyticsBackendClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_analytics_backend_finalize;
    object_class->get_property = lrg_analytics_backend_get_property;
    object_class->set_property = lrg_analytics_backend_set_property;

    /* Default virtual method implementations */
    klass->send_event = lrg_analytics_backend_real_send_event;
    klass->flush = lrg_analytics_backend_real_flush;
    klass->is_enabled = lrg_analytics_backend_real_is_enabled;

    /**
     * LrgAnalyticsBackend:name:
     *
     * The backend name/identifier.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Backend name/identifier",
                             "unknown",
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgAnalyticsBackend:enabled:
     *
     * Whether the backend is enabled.
     *
     * Since: 1.0
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether the backend is enabled",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_analytics_backend_init (LrgAnalyticsBackend *self)
{
    LrgAnalyticsBackendPrivate *priv = lrg_analytics_backend_get_instance_private (self);

    priv->name = g_strdup ("unknown");
    priv->enabled = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

gboolean
lrg_analytics_backend_get_enabled (LrgAnalyticsBackend *self)
{
    LrgAnalyticsBackendPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND (self), FALSE);

    priv = lrg_analytics_backend_get_instance_private (self);

    return priv->enabled;
}

void
lrg_analytics_backend_set_enabled (LrgAnalyticsBackend *self,
                                   gboolean             enabled)
{
    LrgAnalyticsBackendPrivate *priv;

    g_return_if_fail (LRG_IS_ANALYTICS_BACKEND (self));

    priv = lrg_analytics_backend_get_instance_private (self);

    if (priv->enabled != enabled)
    {
        priv->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
    }
}

const gchar *
lrg_analytics_backend_get_name (LrgAnalyticsBackend *self)
{
    LrgAnalyticsBackendPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND (self), NULL);

    priv = lrg_analytics_backend_get_instance_private (self);

    return priv->name;
}

gboolean
lrg_analytics_backend_send_event (LrgAnalyticsBackend  *self,
                                  LrgAnalyticsEvent    *event,
                                  GError              **error)
{
    LrgAnalyticsBackendClass *klass;

    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND (self), FALSE);
    g_return_val_if_fail (LRG_IS_ANALYTICS_EVENT (event), FALSE);

    klass = LRG_ANALYTICS_BACKEND_GET_CLASS (self);

    if (klass->send_event != NULL)
        return klass->send_event (self, event, error);

    return TRUE;
}

gboolean
lrg_analytics_backend_flush (LrgAnalyticsBackend  *self,
                             GError              **error)
{
    LrgAnalyticsBackendClass *klass;

    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND (self), FALSE);

    klass = LRG_ANALYTICS_BACKEND_GET_CLASS (self);

    if (klass->flush != NULL)
        return klass->flush (self, error);

    return TRUE;
}

gboolean
lrg_analytics_backend_is_enabled (LrgAnalyticsBackend *self)
{
    LrgAnalyticsBackendClass *klass;

    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND (self), FALSE);

    klass = LRG_ANALYTICS_BACKEND_GET_CLASS (self);

    if (klass->is_enabled != NULL)
        return klass->is_enabled (self);

    return FALSE;
}
