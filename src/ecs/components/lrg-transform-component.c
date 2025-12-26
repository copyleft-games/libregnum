/* lrg-transform-component.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Transform component with parent/child hierarchy.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECS

#include "lrg-transform-component.h"
#include "../lrg-game-object.h"
#include "../../lrg-log.h"

#include <math.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    /* Local transform (relative to parent) */
    gfloat                 local_x;
    gfloat                 local_y;
    gfloat                 local_rotation;  /* Degrees */
    gfloat                 scale_x;
    gfloat                 scale_y;

    /* Hierarchy */
    LrgTransformComponent *parent;          /* Weak reference */
    GList                 *children;        /* List of LrgTransformComponent* (weak refs) */
} LrgTransformComponentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTransformComponent, lrg_transform_component, LRG_TYPE_COMPONENT)

enum
{
    PROP_0,
    PROP_LOCAL_X,
    PROP_LOCAL_Y,
    PROP_LOCAL_ROTATION,
    PROP_SCALE_X,
    PROP_SCALE_Y,
    PROP_PARENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Forward declarations */
static void add_child    (LrgTransformComponent *parent,
                          LrgTransformComponent *child);
static void remove_child (LrgTransformComponent *parent,
                          LrgTransformComponent *child);

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_transform_component_dispose (GObject *object)
{
    LrgTransformComponent        *self = LRG_TRANSFORM_COMPONENT (object);
    LrgTransformComponentPrivate *priv = lrg_transform_component_get_instance_private (self);

    /* Detach from parent */
    if (priv->parent != NULL)
    {
        remove_child (priv->parent, self);
        priv->parent = NULL;
    }

    /* Detach all children */
    lrg_transform_component_detach_children (self);

    G_OBJECT_CLASS (lrg_transform_component_parent_class)->dispose (object);
}

static void
lrg_transform_component_finalize (GObject *object)
{
    LrgTransformComponent        *self = LRG_TRANSFORM_COMPONENT (object);
    LrgTransformComponentPrivate *priv = lrg_transform_component_get_instance_private (self);

    g_list_free (priv->children);

    G_OBJECT_CLASS (lrg_transform_component_parent_class)->finalize (object);
}

static void
lrg_transform_component_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
    LrgTransformComponent        *self = LRG_TRANSFORM_COMPONENT (object);
    LrgTransformComponentPrivate *priv = lrg_transform_component_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_LOCAL_X:
        g_value_set_float (value, priv->local_x);
        break;
    case PROP_LOCAL_Y:
        g_value_set_float (value, priv->local_y);
        break;
    case PROP_LOCAL_ROTATION:
        g_value_set_float (value, priv->local_rotation);
        break;
    case PROP_SCALE_X:
        g_value_set_float (value, priv->scale_x);
        break;
    case PROP_SCALE_Y:
        g_value_set_float (value, priv->scale_y);
        break;
    case PROP_PARENT:
        g_value_set_object (value, priv->parent);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_transform_component_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    LrgTransformComponent *self = LRG_TRANSFORM_COMPONENT (object);

    switch (prop_id)
    {
    case PROP_LOCAL_X:
        lrg_transform_component_set_local_x (self, g_value_get_float (value));
        break;
    case PROP_LOCAL_Y:
        lrg_transform_component_set_local_y (self, g_value_get_float (value));
        break;
    case PROP_LOCAL_ROTATION:
        lrg_transform_component_set_local_rotation (self, g_value_get_float (value));
        break;
    case PROP_SCALE_X:
        {
            LrgTransformComponentPrivate *priv = lrg_transform_component_get_instance_private (self);
            gfloat new_value = g_value_get_float (value);
            if (priv->scale_x != new_value)
            {
                priv->scale_x = new_value;
                g_object_notify_by_pspec (object, properties[PROP_SCALE_X]);
            }
        }
        break;
    case PROP_SCALE_Y:
        {
            LrgTransformComponentPrivate *priv = lrg_transform_component_get_instance_private (self);
            gfloat new_value = g_value_get_float (value);
            if (priv->scale_y != new_value)
            {
                priv->scale_y = new_value;
                g_object_notify_by_pspec (object, properties[PROP_SCALE_Y]);
            }
        }
        break;
    case PROP_PARENT:
        lrg_transform_component_set_parent (self, g_value_get_object (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_transform_component_class_init (LrgTransformComponentClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_transform_component_dispose;
    object_class->finalize = lrg_transform_component_finalize;
    object_class->get_property = lrg_transform_component_get_property;
    object_class->set_property = lrg_transform_component_set_property;

    /**
     * LrgTransformComponent:local-x:
     *
     * The local X position relative to parent.
     */
    properties[PROP_LOCAL_X] =
        g_param_spec_float ("local-x",
                            "Local X",
                            "Local X position relative to parent",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTransformComponent:local-y:
     *
     * The local Y position relative to parent.
     */
    properties[PROP_LOCAL_Y] =
        g_param_spec_float ("local-y",
                            "Local Y",
                            "Local Y position relative to parent",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTransformComponent:local-rotation:
     *
     * The local rotation in degrees relative to parent.
     */
    properties[PROP_LOCAL_ROTATION] =
        g_param_spec_float ("local-rotation",
                            "Local Rotation",
                            "Local rotation in degrees",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTransformComponent:scale-x:
     *
     * The X scale factor.
     */
    properties[PROP_SCALE_X] =
        g_param_spec_float ("scale-x",
                            "Scale X",
                            "X scale factor",
                            -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTransformComponent:scale-y:
     *
     * The Y scale factor.
     */
    properties[PROP_SCALE_Y] =
        g_param_spec_float ("scale-y",
                            "Scale Y",
                            "Y scale factor",
                            -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTransformComponent:parent:
     *
     * The parent transform for hierarchical positioning.
     */
    properties[PROP_PARENT] =
        g_param_spec_object ("parent",
                             "Parent",
                             "Parent transform component",
                             LRG_TYPE_TRANSFORM_COMPONENT,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_transform_component_init (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv = lrg_transform_component_get_instance_private (self);

    priv->local_x = 0.0f;
    priv->local_y = 0.0f;
    priv->local_rotation = 0.0f;
    priv->scale_x = 1.0f;
    priv->scale_y = 1.0f;
    priv->parent = NULL;
    priv->children = NULL;
}

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static void
add_child (LrgTransformComponent *parent,
           LrgTransformComponent *child)
{
    LrgTransformComponentPrivate *priv = lrg_transform_component_get_instance_private (parent);

    if (g_list_find (priv->children, child) == NULL)
    {
        priv->children = g_list_append (priv->children, child);
    }
}

static void
remove_child (LrgTransformComponent *parent,
              LrgTransformComponent *child)
{
    LrgTransformComponentPrivate *priv = lrg_transform_component_get_instance_private (parent);

    priv->children = g_list_remove (priv->children, child);
}

/* Convert degrees to radians */
static inline gfloat
deg_to_rad (gfloat degrees)
{
    return degrees * (G_PI / 180.0f);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_transform_component_new:
 *
 * Creates a new transform component at position (0, 0).
 *
 * Returns: (transfer full): A new #LrgTransformComponent
 */
LrgTransformComponent *
lrg_transform_component_new (void)
{
    return g_object_new (LRG_TYPE_TRANSFORM_COMPONENT, NULL);
}

/**
 * lrg_transform_component_new_at:
 * @x: Initial local X position
 * @y: Initial local Y position
 *
 * Creates a new transform component at the specified local position.
 *
 * Returns: (transfer full): A new #LrgTransformComponent
 */
LrgTransformComponent *
lrg_transform_component_new_at (gfloat x,
                                gfloat y)
{
    return g_object_new (LRG_TYPE_TRANSFORM_COMPONENT,
                         "local-x", x,
                         "local-y", y,
                         NULL);
}

/* ==========================================================================
 * Public API - Local Transform
 * ========================================================================== */

/**
 * lrg_transform_component_get_local_position:
 * @self: an #LrgTransformComponent
 *
 * Gets the local position relative to parent.
 *
 * Returns: (transfer full): A new #GrlVector2 with the local position
 */
GrlVector2 *
lrg_transform_component_get_local_position (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), NULL);

    priv = lrg_transform_component_get_instance_private (self);
    return grl_vector2_new (priv->local_x, priv->local_y);
}

/**
 * lrg_transform_component_set_local_position:
 * @self: an #LrgTransformComponent
 * @position: The local position
 *
 * Sets the local position relative to parent.
 */
void
lrg_transform_component_set_local_position (LrgTransformComponent *self,
                                            GrlVector2            *position)
{
    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));
    g_return_if_fail (position != NULL);

    lrg_transform_component_set_local_position_xy (self, position->x, position->y);
}

/**
 * lrg_transform_component_set_local_position_xy:
 * @self: an #LrgTransformComponent
 * @x: Local X position
 * @y: Local Y position
 *
 * Sets the local position using X and Y coordinates.
 */
void
lrg_transform_component_set_local_position_xy (LrgTransformComponent *self,
                                               gfloat                 x,
                                               gfloat                 y)
{
    LrgTransformComponentPrivate *priv;
    gboolean x_changed;
    gboolean y_changed;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));

    priv = lrg_transform_component_get_instance_private (self);

    x_changed = (priv->local_x != x);
    y_changed = (priv->local_y != y);

    if (x_changed)
    {
        priv->local_x = x;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCAL_X]);
    }
    if (y_changed)
    {
        priv->local_y = y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCAL_Y]);
    }
}

/**
 * lrg_transform_component_get_local_x:
 * @self: an #LrgTransformComponent
 *
 * Gets the local X position.
 *
 * Returns: The local X coordinate
 */
gfloat
lrg_transform_component_get_local_x (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), 0.0f);

    priv = lrg_transform_component_get_instance_private (self);
    return priv->local_x;
}

/**
 * lrg_transform_component_set_local_x:
 * @self: an #LrgTransformComponent
 * @x: The local X coordinate
 *
 * Sets the local X position.
 */
void
lrg_transform_component_set_local_x (LrgTransformComponent *self,
                                     gfloat                 x)
{
    LrgTransformComponentPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));

    priv = lrg_transform_component_get_instance_private (self);

    if (priv->local_x != x)
    {
        priv->local_x = x;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCAL_X]);
    }
}

/**
 * lrg_transform_component_get_local_y:
 * @self: an #LrgTransformComponent
 *
 * Gets the local Y position.
 *
 * Returns: The local Y coordinate
 */
gfloat
lrg_transform_component_get_local_y (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), 0.0f);

    priv = lrg_transform_component_get_instance_private (self);
    return priv->local_y;
}

/**
 * lrg_transform_component_set_local_y:
 * @self: an #LrgTransformComponent
 * @y: The local Y coordinate
 *
 * Sets the local Y position.
 */
void
lrg_transform_component_set_local_y (LrgTransformComponent *self,
                                     gfloat                 y)
{
    LrgTransformComponentPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));

    priv = lrg_transform_component_get_instance_private (self);

    if (priv->local_y != y)
    {
        priv->local_y = y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCAL_Y]);
    }
}

/**
 * lrg_transform_component_get_local_rotation:
 * @self: an #LrgTransformComponent
 *
 * Gets the local rotation in degrees.
 *
 * Returns: The local rotation in degrees
 */
gfloat
lrg_transform_component_get_local_rotation (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), 0.0f);

    priv = lrg_transform_component_get_instance_private (self);
    return priv->local_rotation;
}

/**
 * lrg_transform_component_set_local_rotation:
 * @self: an #LrgTransformComponent
 * @rotation: Local rotation in degrees
 *
 * Sets the local rotation in degrees.
 */
void
lrg_transform_component_set_local_rotation (LrgTransformComponent *self,
                                            gfloat                 rotation)
{
    LrgTransformComponentPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));

    priv = lrg_transform_component_get_instance_private (self);

    if (priv->local_rotation != rotation)
    {
        priv->local_rotation = rotation;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCAL_ROTATION]);
    }
}

/**
 * lrg_transform_component_get_local_scale:
 * @self: an #LrgTransformComponent
 *
 * Gets the local scale.
 *
 * Returns: (transfer full): A new #GrlVector2 with the local scale (x, y)
 */
GrlVector2 *
lrg_transform_component_get_local_scale (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), NULL);

    priv = lrg_transform_component_get_instance_private (self);
    return grl_vector2_new (priv->scale_x, priv->scale_y);
}

/**
 * lrg_transform_component_set_local_scale:
 * @self: an #LrgTransformComponent
 * @scale: The local scale (x, y)
 *
 * Sets the local scale.
 */
void
lrg_transform_component_set_local_scale (LrgTransformComponent *self,
                                         GrlVector2            *scale)
{
    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));
    g_return_if_fail (scale != NULL);

    lrg_transform_component_set_local_scale_xy (self, scale->x, scale->y);
}

/**
 * lrg_transform_component_set_local_scale_xy:
 * @self: an #LrgTransformComponent
 * @scale_x: X scale factor
 * @scale_y: Y scale factor
 *
 * Sets the local scale using separate X and Y factors.
 */
void
lrg_transform_component_set_local_scale_xy (LrgTransformComponent *self,
                                            gfloat                 scale_x,
                                            gfloat                 scale_y)
{
    LrgTransformComponentPrivate *priv;
    gboolean x_changed;
    gboolean y_changed;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));

    priv = lrg_transform_component_get_instance_private (self);

    x_changed = (priv->scale_x != scale_x);
    y_changed = (priv->scale_y != scale_y);

    if (x_changed)
    {
        priv->scale_x = scale_x;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE_X]);
    }
    if (y_changed)
    {
        priv->scale_y = scale_y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE_Y]);
    }
}

/**
 * lrg_transform_component_set_local_scale_uniform:
 * @self: an #LrgTransformComponent
 * @scale: Uniform scale factor
 *
 * Sets uniform scale for both X and Y.
 */
void
lrg_transform_component_set_local_scale_uniform (LrgTransformComponent *self,
                                                 gfloat                 scale)
{
    lrg_transform_component_set_local_scale_xy (self, scale, scale);
}

/* ==========================================================================
 * Public API - World Transform
 * ========================================================================== */

/**
 * lrg_transform_component_get_world_position:
 * @self: an #LrgTransformComponent
 *
 * Gets the world-space position (combining all parent transforms).
 *
 * Returns: (transfer full): A new #GrlVector2 with the world position
 */
GrlVector2 *
lrg_transform_component_get_world_position (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;
    gfloat                        world_x;
    gfloat                        world_y;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), NULL);

    priv = lrg_transform_component_get_instance_private (self);

    if (priv->parent == NULL)
    {
        /* No parent - local is world */
        return grl_vector2_new (priv->local_x, priv->local_y);
    }
    else
    {
        /* Get parent world transform */
        g_autoptr(GrlVector2) parent_pos = lrg_transform_component_get_world_position (priv->parent);
        g_autoptr(GrlVector2) parent_scale = lrg_transform_component_get_world_scale (priv->parent);
        gfloat parent_rot = lrg_transform_component_get_world_rotation (priv->parent);

        /* Transform local position by parent scale and rotation */
        gfloat rad = deg_to_rad (parent_rot);
        gfloat cos_r = cosf (rad);
        gfloat sin_r = sinf (rad);

        /* Scale first, then rotate */
        gfloat scaled_x = priv->local_x * parent_scale->x;
        gfloat scaled_y = priv->local_y * parent_scale->y;

        gfloat rotated_x = scaled_x * cos_r - scaled_y * sin_r;
        gfloat rotated_y = scaled_x * sin_r + scaled_y * cos_r;

        /* Translate by parent position */
        world_x = parent_pos->x + rotated_x;
        world_y = parent_pos->y + rotated_y;

        return grl_vector2_new (world_x, world_y);
    }
}

/**
 * lrg_transform_component_get_world_rotation:
 * @self: an #LrgTransformComponent
 *
 * Gets the world-space rotation in degrees (combining all parent rotations).
 *
 * Returns: The world rotation in degrees
 */
gfloat
lrg_transform_component_get_world_rotation (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), 0.0f);

    priv = lrg_transform_component_get_instance_private (self);

    if (priv->parent == NULL)
    {
        return priv->local_rotation;
    }
    else
    {
        return lrg_transform_component_get_world_rotation (priv->parent) + priv->local_rotation;
    }
}

/**
 * lrg_transform_component_get_world_scale:
 * @self: an #LrgTransformComponent
 *
 * Gets the world-space scale (combining all parent scales).
 *
 * Returns: (transfer full): A new #GrlVector2 with the world scale
 */
GrlVector2 *
lrg_transform_component_get_world_scale (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), NULL);

    priv = lrg_transform_component_get_instance_private (self);

    if (priv->parent == NULL)
    {
        return grl_vector2_new (priv->scale_x, priv->scale_y);
    }
    else
    {
        g_autoptr(GrlVector2) parent_scale = lrg_transform_component_get_world_scale (priv->parent);
        return grl_vector2_new (priv->scale_x * parent_scale->x,
                                priv->scale_y * parent_scale->y);
    }
}

/* ==========================================================================
 * Public API - Hierarchy
 * ========================================================================== */

/**
 * lrg_transform_component_get_parent:
 * @self: an #LrgTransformComponent
 *
 * Gets the parent transform.
 *
 * Returns: (transfer none) (nullable): The parent transform, or %NULL
 */
LrgTransformComponent *
lrg_transform_component_get_parent (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), NULL);

    priv = lrg_transform_component_get_instance_private (self);
    return priv->parent;
}

/**
 * lrg_transform_component_set_parent:
 * @self: an #LrgTransformComponent
 * @parent: (nullable): The new parent transform, or %NULL to unparent
 *
 * Sets the parent transform. The local position becomes relative to the parent.
 */
void
lrg_transform_component_set_parent (LrgTransformComponent *self,
                                    LrgTransformComponent *parent)
{
    LrgTransformComponentPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));
    g_return_if_fail (parent == NULL || LRG_IS_TRANSFORM_COMPONENT (parent));
    g_return_if_fail (parent != self);  /* Can't be own parent */

    priv = lrg_transform_component_get_instance_private (self);

    if (priv->parent == parent)
    {
        return;
    }

    /* Remove from old parent */
    if (priv->parent != NULL)
    {
        remove_child (priv->parent, self);
    }

    /* Set new parent */
    priv->parent = parent;

    /* Add to new parent's children list */
    if (parent != NULL)
    {
        add_child (parent, self);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARENT]);
}

/**
 * lrg_transform_component_get_children:
 * @self: an #LrgTransformComponent
 *
 * Gets a list of all child transforms.
 *
 * Returns: (transfer container) (element-type LrgTransformComponent): List of children
 */
GList *
lrg_transform_component_get_children (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), NULL);

    priv = lrg_transform_component_get_instance_private (self);
    return g_list_copy (priv->children);
}

/**
 * lrg_transform_component_get_child_count:
 * @self: an #LrgTransformComponent
 *
 * Gets the number of child transforms.
 *
 * Returns: The child count
 */
guint
lrg_transform_component_get_child_count (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSFORM_COMPONENT (self), 0);

    priv = lrg_transform_component_get_instance_private (self);
    return g_list_length (priv->children);
}

/**
 * lrg_transform_component_detach_children:
 * @self: an #LrgTransformComponent
 *
 * Removes all children from this transform.
 */
void
lrg_transform_component_detach_children (LrgTransformComponent *self)
{
    LrgTransformComponentPrivate *priv;
    GList                        *children_copy;
    GList                        *l;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));

    priv = lrg_transform_component_get_instance_private (self);

    /* Copy list since we're modifying it */
    children_copy = g_list_copy (priv->children);

    for (l = children_copy; l != NULL; l = l->next)
    {
        LrgTransformComponent        *child = l->data;
        LrgTransformComponentPrivate *child_priv = lrg_transform_component_get_instance_private (child);

        child_priv->parent = NULL;
        g_object_notify_by_pspec (G_OBJECT (child), properties[PROP_PARENT]);
    }

    g_list_free (children_copy);
    g_list_free (priv->children);
    priv->children = NULL;
}

/* ==========================================================================
 * Public API - Utility
 * ========================================================================== */

/**
 * lrg_transform_component_translate:
 * @self: an #LrgTransformComponent
 * @offset: Translation offset in local space
 *
 * Translates the transform by the given offset.
 */
void
lrg_transform_component_translate (LrgTransformComponent *self,
                                   GrlVector2            *offset)
{
    LrgTransformComponentPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));
    g_return_if_fail (offset != NULL);

    priv = lrg_transform_component_get_instance_private (self);

    lrg_transform_component_set_local_position_xy (self,
                                                   priv->local_x + offset->x,
                                                   priv->local_y + offset->y);
}

/**
 * lrg_transform_component_rotate:
 * @self: an #LrgTransformComponent
 * @degrees: Rotation amount in degrees
 *
 * Rotates the transform by the given amount.
 */
void
lrg_transform_component_rotate (LrgTransformComponent *self,
                                gfloat                 degrees)
{
    LrgTransformComponentPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));

    priv = lrg_transform_component_get_instance_private (self);

    lrg_transform_component_set_local_rotation (self, priv->local_rotation + degrees);
}

/**
 * lrg_transform_component_look_at:
 * @self: an #LrgTransformComponent
 * @target: World-space target position
 *
 * Rotates the transform to face the target position.
 */
void
lrg_transform_component_look_at (LrgTransformComponent *self,
                                 GrlVector2            *target)
{
    g_autoptr(GrlVector2) world_pos = NULL;
    gfloat                dx;
    gfloat                dy;
    gfloat                angle;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));
    g_return_if_fail (target != NULL);

    world_pos = lrg_transform_component_get_world_position (self);

    dx = target->x - world_pos->x;
    dy = target->y - world_pos->y;

    /* Calculate angle in degrees */
    angle = atan2f (dy, dx) * (180.0f / G_PI);

    /* Convert world rotation to local rotation */
    if (lrg_transform_component_get_parent (self) != NULL)
    {
        gfloat parent_rot = lrg_transform_component_get_world_rotation (
            lrg_transform_component_get_parent (self));
        angle -= parent_rot;
    }

    lrg_transform_component_set_local_rotation (self, angle);
}

/**
 * lrg_transform_component_sync_to_entity:
 * @self: an #LrgTransformComponent
 *
 * Syncs the world transform to the owning game object's entity transform.
 */
void
lrg_transform_component_sync_to_entity (LrgTransformComponent *self)
{
    LrgGameObject         *owner;
    g_autoptr(GrlVector2)  world_pos = NULL;
    g_autoptr(GrlVector2)  world_scale = NULL;
    gfloat                 world_rot;

    g_return_if_fail (LRG_IS_TRANSFORM_COMPONENT (self));

    owner = lrg_component_get_owner (LRG_COMPONENT (self));
    if (owner == NULL)
    {
        return;
    }

    world_pos = lrg_transform_component_get_world_position (self);
    world_rot = lrg_transform_component_get_world_rotation (self);
    world_scale = lrg_transform_component_get_world_scale (self);

    /* Sync to entity */
    grl_entity_set_position (GRL_ENTITY (owner), world_pos);
    grl_entity_set_rotation (GRL_ENTITY (owner), world_rot);

    /* GrlEntity has uniform scale, use average */
    grl_entity_set_scale (GRL_ENTITY (owner), (world_scale->x + world_scale->y) / 2.0f);
}
