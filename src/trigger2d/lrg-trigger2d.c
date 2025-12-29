/* lrg-trigger2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for 2D triggers.
 */

#include "lrg-trigger2d.h"
#include "lrg-trigger2d-private.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRIGGER2D

/**
 * LrgTrigger2D:
 *
 * Abstract base class for 2D trigger zones.
 *
 * #LrgTrigger2D provides the foundation for creating trigger zones
 * that detect when entities enter, stay within, or exit a defined area.
 * Subclasses implement specific shapes (rectangle, circle, polygon).
 *
 * ## Features
 *
 * - **One-shot triggers**: Fire only once, then disable
 * - **Cooldown**: Minimum time between trigger events
 * - **Collision filtering**: Layer/mask system for selective detection
 * - **Enable/disable**: Temporarily deactivate triggers
 *
 * ## Signal
 *
 * The "triggered" signal is emitted when a trigger event occurs.
 *
 * Since: 1.0
 */

enum
{
    PROP_0,
    PROP_ID,
    PROP_ENABLED,
    PROP_ONE_SHOT,
    PROP_HAS_FIRED,
    PROP_COOLDOWN,
    PROP_COLLISION_LAYER,
    PROP_COLLISION_MASK,
    N_PROPS
};

enum
{
    SIGNAL_TRIGGERED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgTrigger2D, lrg_trigger2d, G_TYPE_OBJECT)

/*
 * GObject virtual methods
 */

static void
lrg_trigger2d_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgTrigger2D *self;
    LrgTrigger2DPrivate *priv;

    self = LRG_TRIGGER2D (object);
    priv = lrg_trigger2d_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;

    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;

    case PROP_ONE_SHOT:
        g_value_set_boolean (value, priv->one_shot);
        break;

    case PROP_HAS_FIRED:
        g_value_set_boolean (value, priv->has_fired);
        break;

    case PROP_COOLDOWN:
        g_value_set_float (value, priv->cooldown);
        break;

    case PROP_COLLISION_LAYER:
        g_value_set_uint (value, priv->collision_layer);
        break;

    case PROP_COLLISION_MASK:
        g_value_set_uint (value, priv->collision_mask);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_trigger2d_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgTrigger2D *self;
    LrgTrigger2DPrivate *priv;

    self = LRG_TRIGGER2D (object);
    priv = lrg_trigger2d_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;

    case PROP_ENABLED:
        priv->enabled = g_value_get_boolean (value);
        break;

    case PROP_ONE_SHOT:
        priv->one_shot = g_value_get_boolean (value);
        break;

    case PROP_COOLDOWN:
        priv->cooldown = g_value_get_float (value);
        break;

    case PROP_COLLISION_LAYER:
        priv->collision_layer = g_value_get_uint (value);
        break;

    case PROP_COLLISION_MASK:
        priv->collision_mask = g_value_get_uint (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_trigger2d_finalize (GObject *object)
{
    LrgTrigger2D *self;
    LrgTrigger2DPrivate *priv;

    self = LRG_TRIGGER2D (object);
    priv = lrg_trigger2d_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);

    if (priv->user_data_destroy != NULL && priv->user_data != NULL)
    {
        priv->user_data_destroy (priv->user_data);
    }

    G_OBJECT_CLASS (lrg_trigger2d_parent_class)->finalize (object);
}

static void
lrg_trigger2d_class_init (LrgTrigger2DClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->get_property = lrg_trigger2d_get_property;
    object_class->set_property = lrg_trigger2d_set_property;
    object_class->finalize = lrg_trigger2d_finalize;

    /**
     * LrgTrigger2D:id:
     *
     * Unique identifier for the trigger.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTrigger2D:enabled:
     *
     * Whether the trigger is active.
     *
     * Since: 1.0
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether the trigger is active",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgTrigger2D:one-shot:
     *
     * Whether the trigger fires only once.
     *
     * Since: 1.0
     */
    properties[PROP_ONE_SHOT] =
        g_param_spec_boolean ("one-shot",
                              "One Shot",
                              "Whether the trigger fires only once",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgTrigger2D:has-fired:
     *
     * Whether a one-shot trigger has already fired.
     *
     * Since: 1.0
     */
    properties[PROP_HAS_FIRED] =
        g_param_spec_boolean ("has-fired",
                              "Has Fired",
                              "Whether a one-shot trigger has fired",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgTrigger2D:cooldown:
     *
     * Minimum time between trigger events in seconds.
     *
     * Since: 1.0
     */
    properties[PROP_COOLDOWN] =
        g_param_spec_float ("cooldown",
                            "Cooldown",
                            "Minimum time between events",
                            0.0f,
                            G_MAXFLOAT,
                            0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgTrigger2D:collision-layer:
     *
     * Collision layer this trigger belongs to.
     *
     * Since: 1.0
     */
    properties[PROP_COLLISION_LAYER] =
        g_param_spec_uint ("collision-layer",
                           "Collision Layer",
                           "Collision layer bitmask",
                           0,
                           G_MAXUINT32,
                           1,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTrigger2D:collision-mask:
     *
     * Collision mask for detection.
     *
     * Since: 1.0
     */
    properties[PROP_COLLISION_MASK] =
        g_param_spec_uint ("collision-mask",
                           "Collision Mask",
                           "Collision mask bitmask",
                           0,
                           G_MAXUINT32,
                           G_MAXUINT32,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTrigger2D::triggered:
     * @self: The trigger
     * @event_type: The #LrgTrigger2DEventType
     * @entity: (nullable): The entity that triggered the event
     *
     * Emitted when a trigger event occurs.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TRIGGERED] =
        g_signal_new ("triggered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_TRIGGER2D_EVENT_TYPE,
                      G_TYPE_POINTER);
}

static void
lrg_trigger2d_init (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    priv = lrg_trigger2d_get_instance_private (self);

    priv->id = NULL;
    priv->enabled = TRUE;
    priv->one_shot = FALSE;
    priv->has_fired = FALSE;
    priv->cooldown = 0.0f;
    priv->cooldown_remaining = 0.0f;
    priv->collision_layer = 1;
    priv->collision_mask = G_MAXUINT32;
    priv->user_data = NULL;
    priv->user_data_destroy = NULL;
}

/*
 * Internal function
 */

void
lrg_trigger2d_mark_fired (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    priv = lrg_trigger2d_get_instance_private (self);

    if (priv->one_shot)
    {
        priv->has_fired = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HAS_FIRED]);
    }

    if (priv->cooldown > 0.0f)
    {
        priv->cooldown_remaining = priv->cooldown;
    }
}

/*
 * Public API
 */

/**
 * lrg_trigger2d_test_point:
 * @self: A #LrgTrigger2D
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Tests if a point is inside the trigger zone.
 *
 * Returns: %TRUE if inside
 *
 * Since: 1.0
 */
gboolean
lrg_trigger2d_test_point (LrgTrigger2D *self,
                          gfloat        x,
                          gfloat        y)
{
    LrgTrigger2DClass *klass;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), FALSE);

    klass = LRG_TRIGGER2D_GET_CLASS (self);
    if (klass->test_point != NULL)
    {
        return klass->test_point (self, x, y);
    }

    return FALSE;
}

/**
 * lrg_trigger2d_get_bounds:
 * @self: A #LrgTrigger2D
 * @out_x: (out) (nullable): Return location for X
 * @out_y: (out) (nullable): Return location for Y
 * @out_width: (out) (nullable): Return location for width
 * @out_height: (out) (nullable): Return location for height
 *
 * Gets the axis-aligned bounding box.
 *
 * Since: 1.0
 */
void
lrg_trigger2d_get_bounds (LrgTrigger2D *self,
                          gfloat       *out_x,
                          gfloat       *out_y,
                          gfloat       *out_width,
                          gfloat       *out_height)
{
    LrgTrigger2DClass *klass;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    klass = LRG_TRIGGER2D_GET_CLASS (self);
    if (klass->get_bounds != NULL)
    {
        klass->get_bounds (self, out_x, out_y, out_width, out_height);
    }
    else
    {
        if (out_x != NULL)
            *out_x = 0.0f;
        if (out_y != NULL)
            *out_y = 0.0f;
        if (out_width != NULL)
            *out_width = 0.0f;
        if (out_height != NULL)
            *out_height = 0.0f;
    }
}

/**
 * lrg_trigger2d_get_shape:
 * @self: A #LrgTrigger2D
 *
 * Gets the trigger shape type.
 *
 * Returns: The #LrgTrigger2DShape
 *
 * Since: 1.0
 */
LrgTrigger2DShape
lrg_trigger2d_get_shape (LrgTrigger2D *self)
{
    LrgTrigger2DClass *klass;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), LRG_TRIGGER2D_SHAPE_RECTANGLE);

    klass = LRG_TRIGGER2D_GET_CLASS (self);
    if (klass->get_shape != NULL)
    {
        return klass->get_shape (self);
    }

    return LRG_TRIGGER2D_SHAPE_RECTANGLE;
}

/**
 * lrg_trigger2d_get_id:
 * @self: A #LrgTrigger2D
 *
 * Gets the trigger ID.
 *
 * Returns: (transfer none) (nullable): The ID
 *
 * Since: 1.0
 */
const gchar *
lrg_trigger2d_get_id (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), NULL);

    priv = lrg_trigger2d_get_instance_private (self);
    return priv->id;
}

/**
 * lrg_trigger2d_set_id:
 * @self: A #LrgTrigger2D
 * @id: (nullable): The ID
 *
 * Sets the trigger ID.
 *
 * Since: 1.0
 */
void
lrg_trigger2d_set_id (LrgTrigger2D *self,
                      const gchar  *id)
{
    LrgTrigger2DPrivate *priv;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    priv = lrg_trigger2d_get_instance_private (self);

    if (g_strcmp0 (priv->id, id) != 0)
    {
        g_free (priv->id);
        priv->id = g_strdup (id);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ID]);
    }
}

/**
 * lrg_trigger2d_is_enabled:
 * @self: A #LrgTrigger2D
 *
 * Checks if enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
gboolean
lrg_trigger2d_is_enabled (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), FALSE);

    priv = lrg_trigger2d_get_instance_private (self);
    return priv->enabled;
}

/**
 * lrg_trigger2d_set_enabled:
 * @self: A #LrgTrigger2D
 * @enabled: Whether enabled
 *
 * Sets whether enabled.
 *
 * Since: 1.0
 */
void
lrg_trigger2d_set_enabled (LrgTrigger2D *self,
                           gboolean      enabled)
{
    LrgTrigger2DPrivate *priv;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    priv = lrg_trigger2d_get_instance_private (self);

    enabled = !!enabled;
    if (priv->enabled != enabled)
    {
        priv->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
    }
}

/**
 * lrg_trigger2d_is_one_shot:
 * @self: A #LrgTrigger2D
 *
 * Checks if one-shot.
 *
 * Returns: %TRUE if one-shot
 *
 * Since: 1.0
 */
gboolean
lrg_trigger2d_is_one_shot (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), FALSE);

    priv = lrg_trigger2d_get_instance_private (self);
    return priv->one_shot;
}

/**
 * lrg_trigger2d_set_one_shot:
 * @self: A #LrgTrigger2D
 * @one_shot: Whether one-shot
 *
 * Sets whether one-shot.
 *
 * Since: 1.0
 */
void
lrg_trigger2d_set_one_shot (LrgTrigger2D *self,
                            gboolean      one_shot)
{
    LrgTrigger2DPrivate *priv;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    priv = lrg_trigger2d_get_instance_private (self);

    one_shot = !!one_shot;
    if (priv->one_shot != one_shot)
    {
        priv->one_shot = one_shot;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ONE_SHOT]);
    }
}

/**
 * lrg_trigger2d_has_fired:
 * @self: A #LrgTrigger2D
 *
 * Checks if has fired.
 *
 * Returns: %TRUE if has fired
 *
 * Since: 1.0
 */
gboolean
lrg_trigger2d_has_fired (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), FALSE);

    priv = lrg_trigger2d_get_instance_private (self);
    return priv->has_fired;
}

/**
 * lrg_trigger2d_reset:
 * @self: A #LrgTrigger2D
 *
 * Resets the trigger.
 *
 * Since: 1.0
 */
void
lrg_trigger2d_reset (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    priv = lrg_trigger2d_get_instance_private (self);

    if (priv->has_fired)
    {
        priv->has_fired = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HAS_FIRED]);
    }

    priv->cooldown_remaining = 0.0f;
}

/**
 * lrg_trigger2d_get_cooldown:
 * @self: A #LrgTrigger2D
 *
 * Gets the cooldown.
 *
 * Returns: Cooldown in seconds
 *
 * Since: 1.0
 */
gfloat
lrg_trigger2d_get_cooldown (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), 0.0f);

    priv = lrg_trigger2d_get_instance_private (self);
    return priv->cooldown;
}

/**
 * lrg_trigger2d_set_cooldown:
 * @self: A #LrgTrigger2D
 * @cooldown: Cooldown in seconds
 *
 * Sets the cooldown.
 *
 * Since: 1.0
 */
void
lrg_trigger2d_set_cooldown (LrgTrigger2D *self,
                            gfloat        cooldown)
{
    LrgTrigger2DPrivate *priv;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    priv = lrg_trigger2d_get_instance_private (self);

    cooldown = MAX (cooldown, 0.0f);
    if (priv->cooldown != cooldown)
    {
        priv->cooldown = cooldown;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COOLDOWN]);
    }
}

/**
 * lrg_trigger2d_is_on_cooldown:
 * @self: A #LrgTrigger2D
 *
 * Checks if on cooldown.
 *
 * Returns: %TRUE if on cooldown
 *
 * Since: 1.0
 */
gboolean
lrg_trigger2d_is_on_cooldown (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), FALSE);

    priv = lrg_trigger2d_get_instance_private (self);
    return priv->cooldown_remaining > 0.0f;
}

/**
 * lrg_trigger2d_update_cooldown:
 * @self: A #LrgTrigger2D
 * @delta_time: Time elapsed
 *
 * Updates cooldown timer.
 *
 * Since: 1.0
 */
void
lrg_trigger2d_update_cooldown (LrgTrigger2D *self,
                               gfloat        delta_time)
{
    LrgTrigger2DPrivate *priv;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    priv = lrg_trigger2d_get_instance_private (self);

    if (priv->cooldown_remaining > 0.0f)
    {
        priv->cooldown_remaining -= delta_time;
        if (priv->cooldown_remaining < 0.0f)
        {
            priv->cooldown_remaining = 0.0f;
        }
    }
}

/**
 * lrg_trigger2d_get_collision_layer:
 * @self: A #LrgTrigger2D
 *
 * Gets collision layer.
 *
 * Returns: Layer bitmask
 *
 * Since: 1.0
 */
guint32
lrg_trigger2d_get_collision_layer (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), 0);

    priv = lrg_trigger2d_get_instance_private (self);
    return priv->collision_layer;
}

/**
 * lrg_trigger2d_set_collision_layer:
 * @self: A #LrgTrigger2D
 * @layer: Layer bitmask
 *
 * Sets collision layer.
 *
 * Since: 1.0
 */
void
lrg_trigger2d_set_collision_layer (LrgTrigger2D *self,
                                   guint32       layer)
{
    LrgTrigger2DPrivate *priv;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    priv = lrg_trigger2d_get_instance_private (self);

    if (priv->collision_layer != layer)
    {
        priv->collision_layer = layer;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLISION_LAYER]);
    }
}

/**
 * lrg_trigger2d_get_collision_mask:
 * @self: A #LrgTrigger2D
 *
 * Gets collision mask.
 *
 * Returns: Mask bitmask
 *
 * Since: 1.0
 */
guint32
lrg_trigger2d_get_collision_mask (LrgTrigger2D *self)
{
    LrgTrigger2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), 0);

    priv = lrg_trigger2d_get_instance_private (self);
    return priv->collision_mask;
}

/**
 * lrg_trigger2d_set_collision_mask:
 * @self: A #LrgTrigger2D
 * @mask: Mask bitmask
 *
 * Sets collision mask.
 *
 * Since: 1.0
 */
void
lrg_trigger2d_set_collision_mask (LrgTrigger2D *self,
                                  guint32       mask)
{
    LrgTrigger2DPrivate *priv;

    g_return_if_fail (LRG_IS_TRIGGER2D (self));

    priv = lrg_trigger2d_get_instance_private (self);

    if (priv->collision_mask != mask)
    {
        priv->collision_mask = mask;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLISION_MASK]);
    }
}

/**
 * lrg_trigger2d_can_collide_with:
 * @self: A #LrgTrigger2D
 * @other_layer: Other entity's layer
 *
 * Checks if collision is possible.
 *
 * Returns: %TRUE if can collide
 *
 * Since: 1.0
 */
gboolean
lrg_trigger2d_can_collide_with (LrgTrigger2D *self,
                                guint32       other_layer)
{
    LrgTrigger2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRIGGER2D (self), FALSE);

    priv = lrg_trigger2d_get_instance_private (self);

    return (priv->collision_mask & other_layer) != 0;
}
