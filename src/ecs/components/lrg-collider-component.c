/* lrg-collider-component.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Collision bounds component.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECS

#include "lrg-collider-component.h"
#include "../lrg-game-object.h"
#include "../../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgColliderComponent
{
    LrgComponent   parent_instance;

    GrlRectangle  *bounds;             /* Collision bounds relative to entity */
    gboolean       collision_enabled;  /* Whether collision checks are active */
    guint32        layer;              /* Which layers this collider is on */
    guint32        mask;               /* Which layers this collider can hit */
};

G_DEFINE_TYPE (LrgColliderComponent, lrg_collider_component, LRG_TYPE_COMPONENT)

enum
{
    PROP_0,
    PROP_COLLISION_ENABLED,
    PROP_LAYER,
    PROP_MASK,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_COLLISION,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_collider_component_finalize (GObject *object)
{
    LrgColliderComponent *self = LRG_COLLIDER_COMPONENT (object);

    g_clear_pointer (&self->bounds, grl_rectangle_free);

    G_OBJECT_CLASS (lrg_collider_component_parent_class)->finalize (object);
}

static void
lrg_collider_component_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgColliderComponent *self = LRG_COLLIDER_COMPONENT (object);

    switch (prop_id)
    {
    case PROP_COLLISION_ENABLED:
        g_value_set_boolean (value, self->collision_enabled);
        break;
    case PROP_LAYER:
        g_value_set_uint (value, self->layer);
        break;
    case PROP_MASK:
        g_value_set_uint (value, self->mask);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_collider_component_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgColliderComponent *self = LRG_COLLIDER_COMPONENT (object);

    switch (prop_id)
    {
    case PROP_COLLISION_ENABLED:
        lrg_collider_component_set_collision_enabled (self,
                                                      g_value_get_boolean (value));
        break;
    case PROP_LAYER:
        lrg_collider_component_set_layer (self, g_value_get_uint (value));
        break;
    case PROP_MASK:
        lrg_collider_component_set_mask (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_collider_component_class_init (LrgColliderComponentClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_collider_component_finalize;
    object_class->get_property = lrg_collider_component_get_property;
    object_class->set_property = lrg_collider_component_set_property;

    /**
     * LrgColliderComponent:collision-enabled:
     *
     * Whether collision checking is enabled for this collider.
     */
    properties[PROP_COLLISION_ENABLED] =
        g_param_spec_boolean ("collision-enabled",
                              "Collision Enabled",
                              "Whether collision checks are active",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgColliderComponent:layer:
     *
     * The collision layer bitmask this collider belongs to.
     */
    properties[PROP_LAYER] =
        g_param_spec_uint ("layer",
                           "Layer",
                           "Collision layer bitmask",
                           0, G_MAXUINT32, 1,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgColliderComponent:mask:
     *
     * The collision mask bitmask (which layers this can collide with).
     */
    properties[PROP_MASK] =
        g_param_spec_uint ("mask",
                           "Mask",
                           "Collision mask bitmask",
                           0, G_MAXUINT32, G_MAXUINT32,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgColliderComponent::collision:
     * @self: the collider that detected collision
     * @other: the other collider involved in the collision
     *
     * Emitted when this collider collides with another.
     */
    signals[SIGNAL_COLLISION] =
        g_signal_new ("collision",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_COLLIDER_COMPONENT);
}

static void
lrg_collider_component_init (LrgColliderComponent *self)
{
    self->bounds = grl_rectangle_new (0, 0, 0, 0);
    self->collision_enabled = TRUE;
    self->layer = 1;          /* Default: layer 0 (bit 0) */
    self->mask = G_MAXUINT32; /* Default: collide with all layers */
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_collider_component_new:
 *
 * Creates a new collider component with default bounds.
 *
 * Returns: (transfer full): A new #LrgColliderComponent
 */
LrgColliderComponent *
lrg_collider_component_new (void)
{
    return g_object_new (LRG_TYPE_COLLIDER_COMPONENT, NULL);
}

/**
 * lrg_collider_component_new_with_bounds:
 * @x: X offset from entity position
 * @y: Y offset from entity position
 * @width: collision width
 * @height: collision height
 *
 * Creates a new collider component with the specified bounds.
 *
 * Returns: (transfer full): A new #LrgColliderComponent
 */
LrgColliderComponent *
lrg_collider_component_new_with_bounds (gfloat x,
                                        gfloat y,
                                        gfloat width,
                                        gfloat height)
{
    LrgColliderComponent *self;

    self = g_object_new (LRG_TYPE_COLLIDER_COMPONENT, NULL);
    lrg_collider_component_set_bounds (self, x, y, width, height);

    return self;
}

/* ==========================================================================
 * Public API - Bounds
 * ========================================================================== */

/**
 * lrg_collider_component_set_bounds:
 * @self: an #LrgColliderComponent
 * @x: X offset
 * @y: Y offset
 * @width: collision width
 * @height: collision height
 *
 * Sets the collision bounds relative to the entity's position.
 */
void
lrg_collider_component_set_bounds (LrgColliderComponent *self,
                                   gfloat                x,
                                   gfloat                y,
                                   gfloat                width,
                                   gfloat                height)
{
    g_return_if_fail (LRG_IS_COLLIDER_COMPONENT (self));

    g_clear_pointer (&self->bounds, grl_rectangle_free);
    self->bounds = grl_rectangle_new (x, y, width, height);
}

/**
 * lrg_collider_component_get_bounds:
 * @self: an #LrgColliderComponent
 *
 * Gets the collision bounds relative to the entity's position.
 *
 * Returns: (transfer full): The bounds rectangle
 */
GrlRectangle *
lrg_collider_component_get_bounds (LrgColliderComponent *self)
{
    g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT (self), NULL);

    return grl_rectangle_copy (self->bounds);
}

/**
 * lrg_collider_component_get_world_bounds:
 * @self: an #LrgColliderComponent
 *
 * Gets the collision bounds in world coordinates.
 *
 * Returns: (transfer full) (nullable): The world bounds, or %NULL if no owner
 */
GrlRectangle *
lrg_collider_component_get_world_bounds (LrgColliderComponent *self)
{
    LrgGameObject *owner;
    gfloat         entity_x;
    gfloat         entity_y;

    g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT (self), NULL);

    owner = lrg_component_get_owner (LRG_COMPONENT (self));
    if (owner == NULL)
    {
        return NULL;
    }

    entity_x = grl_entity_get_x (GRL_ENTITY (owner));
    entity_y = grl_entity_get_y (GRL_ENTITY (owner));

    return grl_rectangle_new (entity_x + self->bounds->x,
                              entity_y + self->bounds->y,
                              self->bounds->width,
                              self->bounds->height);
}

/* ==========================================================================
 * Public API - Collision Enable/Disable
 * ========================================================================== */

/**
 * lrg_collider_component_set_collision_enabled:
 * @self: an #LrgColliderComponent
 * @enabled: whether collision checking is enabled
 *
 * Sets whether collision checking is enabled for this collider.
 */
void
lrg_collider_component_set_collision_enabled (LrgColliderComponent *self,
                                              gboolean              enabled)
{
    g_return_if_fail (LRG_IS_COLLIDER_COMPONENT (self));

    enabled = !!enabled;

    if (self->collision_enabled != enabled)
    {
        self->collision_enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLISION_ENABLED]);
    }
}

/**
 * lrg_collider_component_get_collision_enabled:
 * @self: an #LrgColliderComponent
 *
 * Gets whether collision checking is enabled.
 *
 * Returns: %TRUE if collision checking is enabled
 */
gboolean
lrg_collider_component_get_collision_enabled (LrgColliderComponent *self)
{
    g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT (self), FALSE);

    return self->collision_enabled;
}

/* ==========================================================================
 * Public API - Collision Layers
 * ========================================================================== */

/**
 * lrg_collider_component_set_layer:
 * @self: an #LrgColliderComponent
 * @layer: the collision layer bitmask
 *
 * Sets the collision layer(s) this collider belongs to.
 */
void
lrg_collider_component_set_layer (LrgColliderComponent *self,
                                  guint32               layer)
{
    g_return_if_fail (LRG_IS_COLLIDER_COMPONENT (self));

    if (self->layer != layer)
    {
        self->layer = layer;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
    }
}

/**
 * lrg_collider_component_get_layer:
 * @self: an #LrgColliderComponent
 *
 * Gets the collision layer bitmask.
 *
 * Returns: The layer bitmask
 */
guint32
lrg_collider_component_get_layer (LrgColliderComponent *self)
{
    g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT (self), 0);

    return self->layer;
}

/**
 * lrg_collider_component_set_mask:
 * @self: an #LrgColliderComponent
 * @mask: the collision mask bitmask
 *
 * Sets which layers this collider can collide with.
 */
void
lrg_collider_component_set_mask (LrgColliderComponent *self,
                                 guint32               mask)
{
    g_return_if_fail (LRG_IS_COLLIDER_COMPONENT (self));

    if (self->mask != mask)
    {
        self->mask = mask;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MASK]);
    }
}

/**
 * lrg_collider_component_get_mask:
 * @self: an #LrgColliderComponent
 *
 * Gets the collision mask bitmask.
 *
 * Returns: The mask bitmask
 */
guint32
lrg_collider_component_get_mask (LrgColliderComponent *self)
{
    g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT (self), 0);

    return self->mask;
}

/* ==========================================================================
 * Public API - Collision Testing
 * ========================================================================== */

/**
 * lrg_collider_component_intersects:
 * @self: an #LrgColliderComponent
 * @other: another #LrgColliderComponent
 *
 * Tests whether this collider intersects with another.
 *
 * Returns: %TRUE if the colliders intersect
 */
gboolean
lrg_collider_component_intersects (LrgColliderComponent *self,
                                   LrgColliderComponent *other)
{
    g_autoptr(GrlRectangle) bounds_a = NULL;
    g_autoptr(GrlRectangle) bounds_b = NULL;
    unsigned char           result;

    g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT (self), FALSE);
    g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT (other), FALSE);

    bounds_a = lrg_collider_component_get_world_bounds (self);
    bounds_b = lrg_collider_component_get_world_bounds (other);

    if (bounds_a == NULL || bounds_b == NULL)
    {
        return FALSE;
    }

    /* Use graylib's rectangle collision check */
    result = grl_collision_rects (bounds_a, bounds_b);
    return result != 0;
}

/**
 * lrg_collider_component_can_collide_with:
 * @self: an #LrgColliderComponent
 * @other: another #LrgColliderComponent
 *
 * Tests whether this collider can collide with another based on layers.
 *
 * Returns: %TRUE if collision is possible (layer/mask match)
 */
gboolean
lrg_collider_component_can_collide_with (LrgColliderComponent *self,
                                         LrgColliderComponent *other)
{
    g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT (self), FALSE);
    g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT (other), FALSE);

    /* Check if either collider is disabled */
    if (!self->collision_enabled || !other->collision_enabled)
    {
        return FALSE;
    }

    /* Check layer/mask matching in both directions */
    return (self->layer & other->mask) != 0 &&
           (other->layer & self->mask) != 0;
}
