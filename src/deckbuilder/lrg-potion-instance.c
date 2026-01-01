/* lrg-potion-instance.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPotionInstance - Runtime instance of a potion implementation.
 */

#include "lrg-potion-instance.h"
#include "../lrg-log.h"

struct _LrgPotionInstance
{
    GObject       parent_instance;

    LrgPotionDef *def;
    gboolean      consumed;
};

G_DEFINE_FINAL_TYPE (LrgPotionInstance, lrg_potion_instance, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_DEF,
    PROP_CONSUMED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_USED,
    SIGNAL_DISCARDED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_potion_instance_finalize (GObject *object)
{
    LrgPotionInstance *self = LRG_POTION_INSTANCE (object);

    g_clear_object (&self->def);

    G_OBJECT_CLASS (lrg_potion_instance_parent_class)->finalize (object);
}

static void
lrg_potion_instance_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgPotionInstance *self = LRG_POTION_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DEF:
        g_value_set_object (value, self->def);
        break;
    case PROP_CONSUMED:
        g_value_set_boolean (value, self->consumed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_potion_instance_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgPotionInstance *self = LRG_POTION_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_DEF:
        g_clear_object (&self->def);
        self->def = g_value_dup_object (value);
        break;
    case PROP_CONSUMED:
        self->consumed = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_potion_instance_class_init (LrgPotionInstanceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_potion_instance_finalize;
    object_class->get_property = lrg_potion_instance_get_property;
    object_class->set_property = lrg_potion_instance_set_property;

    /**
     * LrgPotionInstance:def:
     *
     * The potion definition.
     *
     * Since: 1.0
     */
    properties[PROP_DEF] =
        g_param_spec_object ("def",
                             "Definition",
                             "The potion definition",
                             LRG_TYPE_POTION_DEF,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPotionInstance:consumed:
     *
     * Whether the potion has been consumed.
     *
     * Since: 1.0
     */
    properties[PROP_CONSUMED] =
        g_param_spec_boolean ("consumed",
                              "Consumed",
                              "Whether consumed",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgPotionInstance::used:
     * @self: the #LrgPotionInstance
     * @target: (nullable): the target
     *
     * Emitted when the potion is used.
     *
     * Since: 1.0
     */
    signals[SIGNAL_USED] =
        g_signal_new ("used",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_POINTER);

    /**
     * LrgPotionInstance::discarded:
     * @self: the #LrgPotionInstance
     *
     * Emitted when the potion is discarded.
     *
     * Since: 1.0
     */
    signals[SIGNAL_DISCARDED] =
        g_signal_new ("discarded",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_potion_instance_init (LrgPotionInstance *self)
{
    self->def = NULL;
    self->consumed = FALSE;
}

/* ==========================================================================
 * Public API - Constructors
 * ========================================================================== */

/**
 * lrg_potion_instance_new:
 * @def: the potion definition
 *
 * Creates a new potion instance from a definition.
 *
 * Returns: (transfer full): a new #LrgPotionInstance
 *
 * Since: 1.0
 */
LrgPotionInstance *
lrg_potion_instance_new (LrgPotionDef *def)
{
    g_return_val_if_fail (LRG_IS_POTION_DEF (def), NULL);

    return g_object_new (LRG_TYPE_POTION_INSTANCE,
                         "def", def,
                         NULL);
}

/* ==========================================================================
 * Public API - Properties
 * ========================================================================== */

LrgPotionDef *
lrg_potion_instance_get_def (LrgPotionInstance *self)
{
    g_return_val_if_fail (LRG_IS_POTION_INSTANCE (self), NULL);

    return self->def;
}

const gchar *
lrg_potion_instance_get_id (LrgPotionInstance *self)
{
    g_return_val_if_fail (LRG_IS_POTION_INSTANCE (self), NULL);
    g_return_val_if_fail (self->def != NULL, NULL);

    return lrg_potion_def_get_id (self->def);
}

const gchar *
lrg_potion_instance_get_name (LrgPotionInstance *self)
{
    g_return_val_if_fail (LRG_IS_POTION_INSTANCE (self), NULL);
    g_return_val_if_fail (self->def != NULL, NULL);

    return lrg_potion_def_get_name (self->def);
}

/* ==========================================================================
 * Public API - Actions
 * ========================================================================== */

gboolean
lrg_potion_instance_can_use (LrgPotionInstance *self,
                              gpointer           context)
{
    g_return_val_if_fail (LRG_IS_POTION_INSTANCE (self), FALSE);

    if (self->consumed)
        return FALSE;

    if (self->def == NULL)
        return FALSE;

    return lrg_potion_def_can_use (self->def, context);
}

gboolean
lrg_potion_instance_use (LrgPotionInstance *self,
                          gpointer           context,
                          gpointer           target)
{
    g_return_val_if_fail (LRG_IS_POTION_INSTANCE (self), FALSE);

    if (!lrg_potion_instance_can_use (self, context))
    {
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Cannot use potion '%s'",
                   lrg_potion_instance_get_id (self));
        return FALSE;
    }

    /* Mark as consumed before use */
    self->consumed = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONSUMED]);

    /* Execute the potion effect */
    lrg_potion_def_on_use (self->def, context, target);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Used potion '%s'",
               lrg_potion_instance_get_id (self));

    g_signal_emit (self, signals[SIGNAL_USED], 0, target);

    return TRUE;
}

void
lrg_potion_instance_discard (LrgPotionInstance *self)
{
    g_return_if_fail (LRG_IS_POTION_INSTANCE (self));

    if (self->consumed)
        return;

    self->consumed = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONSUMED]);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Discarded potion '%s'",
               lrg_potion_instance_get_id (self));

    g_signal_emit (self, signals[SIGNAL_DISCARDED], 0);
}

gboolean
lrg_potion_instance_is_consumed (LrgPotionInstance *self)
{
    g_return_val_if_fail (LRG_IS_POTION_INSTANCE (self), TRUE);

    return self->consumed;
}
