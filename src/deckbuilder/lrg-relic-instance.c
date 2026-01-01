/* lrg-relic-instance.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRelicInstance - Runtime instance of a relic implementation.
 */

#include "lrg-relic-instance.h"
#include "../lrg-log.h"

struct _LrgRelicInstance
{
    GObject      parent_instance;

    LrgRelicDef *def;
    gboolean     enabled;
    gint         counter;
    guint        uses;
    GHashTable  *data;
};

G_DEFINE_FINAL_TYPE (LrgRelicInstance, lrg_relic_instance, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_DEF,
    PROP_ENABLED,
    PROP_COUNTER,
    PROP_USES,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_COUNTER_REACHED,
    SIGNAL_FLASHED,
    SIGNAL_ENABLED_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_relic_instance_finalize (GObject *object)
{
    LrgRelicInstance *self = LRG_RELIC_INSTANCE (object);

    g_clear_object (&self->def);
    g_clear_pointer (&self->data, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_relic_instance_parent_class)->finalize (object);
}

static void
lrg_relic_instance_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgRelicInstance *self = LRG_RELIC_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DEF:
        g_value_set_object (value, self->def);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, self->enabled);
        break;
    case PROP_COUNTER:
        g_value_set_int (value, self->counter);
        break;
    case PROP_USES:
        g_value_set_uint (value, self->uses);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_relic_instance_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgRelicInstance *self = LRG_RELIC_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DEF:
        g_clear_object (&self->def);
        self->def = g_value_dup_object (value);
        break;
    case PROP_ENABLED:
        lrg_relic_instance_set_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_COUNTER:
        self->counter = g_value_get_int (value);
        break;
    case PROP_USES:
        self->uses = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_relic_instance_class_init (LrgRelicInstanceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_relic_instance_finalize;
    object_class->get_property = lrg_relic_instance_get_property;
    object_class->set_property = lrg_relic_instance_set_property;

    /**
     * LrgRelicInstance:def:
     *
     * The relic definition.
     *
     * Since: 1.0
     */
    properties[PROP_DEF] =
        g_param_spec_object ("def",
                             "Definition",
                             "The relic definition",
                             LRG_TYPE_RELIC_DEF,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicInstance:enabled:
     *
     * Whether the relic is enabled.
     *
     * Since: 1.0
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether enabled",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicInstance:counter:
     *
     * The current counter value.
     *
     * Since: 1.0
     */
    properties[PROP_COUNTER] =
        g_param_spec_int ("counter",
                          "Counter",
                          "Current counter value",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRelicInstance:uses:
     *
     * Number of times triggered.
     *
     * Since: 1.0
     */
    properties[PROP_USES] =
        g_param_spec_uint ("uses",
                           "Uses",
                           "Number of times triggered",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgRelicInstance::counter-reached:
     * @self: the #LrgRelicInstance
     *
     * Emitted when the counter reaches max.
     *
     * Since: 1.0
     */
    signals[SIGNAL_COUNTER_REACHED] =
        g_signal_new ("counter-reached",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgRelicInstance::flashed:
     * @self: the #LrgRelicInstance
     *
     * Emitted when the relic should flash for visual feedback.
     *
     * Since: 1.0
     */
    signals[SIGNAL_FLASHED] =
        g_signal_new ("flashed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgRelicInstance::enabled-changed:
     * @self: the #LrgRelicInstance
     * @enabled: new enabled state
     *
     * Emitted when enabled state changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_ENABLED_CHANGED] =
        g_signal_new ("enabled-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_BOOLEAN);
}

static void
lrg_relic_instance_init (LrgRelicInstance *self)
{
    self->def = NULL;
    self->enabled = TRUE;
    self->counter = 0;
    self->uses = 0;
    self->data = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, NULL);
}

/* ==========================================================================
 * Public API - Constructors
 * ========================================================================== */

/**
 * lrg_relic_instance_new:
 * @def: the relic definition
 *
 * Creates a new relic instance from a definition.
 *
 * Returns: (transfer full): a new #LrgRelicInstance
 *
 * Since: 1.0
 */
LrgRelicInstance *
lrg_relic_instance_new (LrgRelicDef *def)
{
    g_return_val_if_fail (LRG_IS_RELIC_DEF (def), NULL);

    return g_object_new (LRG_TYPE_RELIC_INSTANCE,
                         "def", def,
                         NULL);
}

/* ==========================================================================
 * Public API - Properties
 * ========================================================================== */

LrgRelicDef *
lrg_relic_instance_get_def (LrgRelicInstance *self)
{
    g_return_val_if_fail (LRG_IS_RELIC_INSTANCE (self), NULL);

    return self->def;
}

const gchar *
lrg_relic_instance_get_id (LrgRelicInstance *self)
{
    g_return_val_if_fail (LRG_IS_RELIC_INSTANCE (self), NULL);
    g_return_val_if_fail (self->def != NULL, NULL);

    return lrg_relic_def_get_id (self->def);
}

const gchar *
lrg_relic_instance_get_name (LrgRelicInstance *self)
{
    g_return_val_if_fail (LRG_IS_RELIC_INSTANCE (self), NULL);
    g_return_val_if_fail (self->def != NULL, NULL);

    return lrg_relic_def_get_name (self->def);
}

gboolean
lrg_relic_instance_get_enabled (LrgRelicInstance *self)
{
    g_return_val_if_fail (LRG_IS_RELIC_INSTANCE (self), FALSE);

    return self->enabled;
}

void
lrg_relic_instance_set_enabled (LrgRelicInstance *self,
                                 gboolean          enabled)
{
    g_return_if_fail (LRG_IS_RELIC_INSTANCE (self));

    enabled = !!enabled;

    if (self->enabled != enabled)
    {
        self->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
        g_signal_emit (self, signals[SIGNAL_ENABLED_CHANGED], 0, enabled);

        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Relic '%s' %s",
                   lrg_relic_instance_get_id (self),
                   enabled ? "enabled" : "disabled");
    }
}

gint
lrg_relic_instance_get_counter (LrgRelicInstance *self)
{
    g_return_val_if_fail (LRG_IS_RELIC_INSTANCE (self), 0);

    return self->counter;
}

void
lrg_relic_instance_set_counter (LrgRelicInstance *self,
                                 gint              counter)
{
    g_return_if_fail (LRG_IS_RELIC_INSTANCE (self));
    g_return_if_fail (counter >= 0);

    self->counter = counter;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNTER]);
}

gboolean
lrg_relic_instance_increment_counter (LrgRelicInstance *self)
{
    gint max_counter;

    g_return_val_if_fail (LRG_IS_RELIC_INSTANCE (self), FALSE);

    if (self->def == NULL)
        return FALSE;

    max_counter = lrg_relic_def_get_counter_max (self->def);
    if (max_counter <= 0)
        return FALSE;

    self->counter++;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNTER]);

    if (self->counter >= max_counter)
    {
        self->counter = 0;
        g_signal_emit (self, signals[SIGNAL_COUNTER_REACHED], 0);

        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Relic '%s' counter reached max (%d)",
                   lrg_relic_instance_get_id (self),
                   max_counter);

        return TRUE;
    }

    return FALSE;
}

void
lrg_relic_instance_reset_counter (LrgRelicInstance *self)
{
    g_return_if_fail (LRG_IS_RELIC_INSTANCE (self));

    if (self->counter != 0)
    {
        self->counter = 0;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNTER]);
    }
}

guint
lrg_relic_instance_get_uses (LrgRelicInstance *self)
{
    g_return_val_if_fail (LRG_IS_RELIC_INSTANCE (self), 0);

    return self->uses;
}

void
lrg_relic_instance_increment_uses (LrgRelicInstance *self)
{
    g_return_if_fail (LRG_IS_RELIC_INSTANCE (self));

    self->uses++;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_USES]);
}

/* ==========================================================================
 * Public API - State
 * ========================================================================== */

gpointer
lrg_relic_instance_get_data (LrgRelicInstance *self,
                              const gchar      *key)
{
    g_return_val_if_fail (LRG_IS_RELIC_INSTANCE (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return g_hash_table_lookup (self->data, key);
}

void
lrg_relic_instance_set_data (LrgRelicInstance *self,
                              const gchar      *key,
                              gpointer          data,
                              GDestroyNotify    destroy)
{
    g_return_if_fail (LRG_IS_RELIC_INSTANCE (self));
    g_return_if_fail (key != NULL);

    /*
     * Note: This simple implementation doesn't track destroy notifiers
     * per key. For a more robust implementation, we'd need a struct
     * containing both the data and destroy notify.
     */
    if (data != NULL)
        g_hash_table_insert (self->data, g_strdup (key), data);
    else
        g_hash_table_remove (self->data, key);
}

gint
lrg_relic_instance_get_int_data (LrgRelicInstance *self,
                                  const gchar      *key,
                                  gint              default_value)
{
    gpointer value;

    g_return_val_if_fail (LRG_IS_RELIC_INSTANCE (self), default_value);
    g_return_val_if_fail (key != NULL, default_value);

    value = g_hash_table_lookup (self->data, key);
    if (value == NULL)
        return default_value;

    return GPOINTER_TO_INT (value);
}

void
lrg_relic_instance_set_int_data (LrgRelicInstance *self,
                                  const gchar      *key,
                                  gint              value)
{
    g_return_if_fail (LRG_IS_RELIC_INSTANCE (self));
    g_return_if_fail (key != NULL);

    g_hash_table_insert (self->data, g_strdup (key), GINT_TO_POINTER (value));
}

/* ==========================================================================
 * Public API - Convenience
 * ========================================================================== */

void
lrg_relic_instance_flash (LrgRelicInstance *self)
{
    g_return_if_fail (LRG_IS_RELIC_INSTANCE (self));

    g_signal_emit (self, signals[SIGNAL_FLASHED], 0);
}
