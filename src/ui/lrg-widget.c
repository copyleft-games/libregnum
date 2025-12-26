/* lrg-widget.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for UI widgets.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-widget.h"
#include "lrg-widget-private.h"
#include "lrg-container.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    gfloat        x;
    gfloat        y;
    gfloat        width;
    gfloat        height;
    gboolean      visible;
    gboolean      enabled;
    LrgContainer *parent;  /* Weak reference to parent container */
} LrgWidgetPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgWidget, lrg_widget, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_X,
    PROP_Y,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_VISIBLE,
    PROP_ENABLED,
    PROP_PARENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Internal Functions
 * ========================================================================== */

/*
 * _lrg_widget_set_parent:
 * @self: an #LrgWidget
 * @parent: (nullable): the new parent, or %NULL to unparent
 *
 * Sets the widget's parent container.
 */
void
_lrg_widget_set_parent (LrgWidget    *self,
                        LrgContainer *parent)
{
    LrgWidgetPrivate *priv;

    g_return_if_fail (LRG_IS_WIDGET (self));
    g_return_if_fail (parent == NULL || LRG_IS_CONTAINER (parent));

    priv = lrg_widget_get_instance_private (self);

    if (priv->parent != parent)
    {
        priv->parent = parent;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARENT]);
    }
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_widget_real_draw (LrgWidget *self)
{
    /* Default implementation does nothing - subclasses must override */
}

static void
lrg_widget_real_measure (LrgWidget *self,
                         gfloat    *preferred_width,
                         gfloat    *preferred_height)
{
    LrgWidgetPrivate *priv;

    /* Default implementation returns current size */
    priv = lrg_widget_get_instance_private (self);

    if (preferred_width != NULL)
    {
        *preferred_width = priv->width;
    }
    if (preferred_height != NULL)
    {
        *preferred_height = priv->height;
    }
}

static gboolean
lrg_widget_real_handle_event (LrgWidget        *self,
                              const LrgUIEvent *event)
{
    /* Default implementation does not consume events */
    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_widget_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    LrgWidget        *self = LRG_WIDGET (object);
    LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_X:
        g_value_set_float (value, priv->x);
        break;
    case PROP_Y:
        g_value_set_float (value, priv->y);
        break;
    case PROP_WIDTH:
        g_value_set_float (value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_float (value, priv->height);
        break;
    case PROP_VISIBLE:
        g_value_set_boolean (value, priv->visible);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    case PROP_PARENT:
        g_value_set_object (value, priv->parent);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_widget_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    LrgWidget *self = LRG_WIDGET (object);

    switch (prop_id)
    {
    case PROP_X:
        lrg_widget_set_x (self, g_value_get_float (value));
        break;
    case PROP_Y:
        lrg_widget_set_y (self, g_value_get_float (value));
        break;
    case PROP_WIDTH:
        lrg_widget_set_width (self, g_value_get_float (value));
        break;
    case PROP_HEIGHT:
        lrg_widget_set_height (self, g_value_get_float (value));
        break;
    case PROP_VISIBLE:
        lrg_widget_set_visible (self, g_value_get_boolean (value));
        break;
    case PROP_ENABLED:
        lrg_widget_set_enabled (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_widget_class_init (LrgWidgetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_widget_get_property;
    object_class->set_property = lrg_widget_set_property;

    /* Default virtual method implementations */
    klass->draw = lrg_widget_real_draw;
    klass->measure = lrg_widget_real_measure;
    klass->handle_event = lrg_widget_real_handle_event;

    /**
     * LrgWidget:x:
     *
     * The X position of the widget relative to its parent.
     */
    properties[PROP_X] =
        g_param_spec_float ("x",
                            "X",
                            "X position relative to parent",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgWidget:y:
     *
     * The Y position of the widget relative to its parent.
     */
    properties[PROP_Y] =
        g_param_spec_float ("y",
                            "Y",
                            "Y position relative to parent",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgWidget:width:
     *
     * The width of the widget.
     */
    properties[PROP_WIDTH] =
        g_param_spec_float ("width",
                            "Width",
                            "Widget width",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgWidget:height:
     *
     * The height of the widget.
     */
    properties[PROP_HEIGHT] =
        g_param_spec_float ("height",
                            "Height",
                            "Widget height",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgWidget:visible:
     *
     * Whether the widget is visible.
     * Invisible widgets are not drawn.
     */
    properties[PROP_VISIBLE] =
        g_param_spec_boolean ("visible",
                              "Visible",
                              "Whether the widget is drawn",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgWidget:enabled:
     *
     * Whether the widget is enabled.
     * Disabled widgets do not respond to input events.
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether the widget responds to input",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgWidget:parent:
     *
     * The parent container of this widget.
     * This property is read-only and set automatically.
     */
    properties[PROP_PARENT] =
        g_param_spec_object ("parent",
                             "Parent",
                             "The parent container",
                             LRG_TYPE_CONTAINER,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_widget_init (LrgWidget *self)
{
    LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);

    priv->x = 0.0f;
    priv->y = 0.0f;
    priv->width = 0.0f;
    priv->height = 0.0f;
    priv->visible = TRUE;
    priv->enabled = TRUE;
    priv->parent = NULL;
}

/* ==========================================================================
 * Public API - Position and Size
 * ========================================================================== */

/**
 * lrg_widget_get_x:
 * @self: an #LrgWidget
 *
 * Gets the widget's X position relative to its parent.
 *
 * Returns: The X position
 */
gfloat
lrg_widget_get_x (LrgWidget *self)
{
    LrgWidgetPrivate *priv;

    g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);

    priv = lrg_widget_get_instance_private (self);
    return priv->x;
}

/**
 * lrg_widget_set_x:
 * @self: an #LrgWidget
 * @x: the X position
 *
 * Sets the widget's X position relative to its parent.
 */
void
lrg_widget_set_x (LrgWidget *self,
                  gfloat     x)
{
    LrgWidgetPrivate *priv;

    g_return_if_fail (LRG_IS_WIDGET (self));

    priv = lrg_widget_get_instance_private (self);

    if (priv->x != x)
    {
        priv->x = x;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X]);
    }
}

/**
 * lrg_widget_get_y:
 * @self: an #LrgWidget
 *
 * Gets the widget's Y position relative to its parent.
 *
 * Returns: The Y position
 */
gfloat
lrg_widget_get_y (LrgWidget *self)
{
    LrgWidgetPrivate *priv;

    g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);

    priv = lrg_widget_get_instance_private (self);
    return priv->y;
}

/**
 * lrg_widget_set_y:
 * @self: an #LrgWidget
 * @y: the Y position
 *
 * Sets the widget's Y position relative to its parent.
 */
void
lrg_widget_set_y (LrgWidget *self,
                  gfloat     y)
{
    LrgWidgetPrivate *priv;

    g_return_if_fail (LRG_IS_WIDGET (self));

    priv = lrg_widget_get_instance_private (self);

    if (priv->y != y)
    {
        priv->y = y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Y]);
    }
}

/**
 * lrg_widget_get_width:
 * @self: an #LrgWidget
 *
 * Gets the widget's width.
 *
 * Returns: The width
 */
gfloat
lrg_widget_get_width (LrgWidget *self)
{
    LrgWidgetPrivate *priv;

    g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);

    priv = lrg_widget_get_instance_private (self);
    return priv->width;
}

/**
 * lrg_widget_set_width:
 * @self: an #LrgWidget
 * @width: the width
 *
 * Sets the widget's width.
 */
void
lrg_widget_set_width (LrgWidget *self,
                      gfloat     width)
{
    LrgWidgetPrivate *priv;

    g_return_if_fail (LRG_IS_WIDGET (self));
    g_return_if_fail (width >= 0.0f);

    priv = lrg_widget_get_instance_private (self);

    if (priv->width != width)
    {
        priv->width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
    }
}

/**
 * lrg_widget_get_height:
 * @self: an #LrgWidget
 *
 * Gets the widget's height.
 *
 * Returns: The height
 */
gfloat
lrg_widget_get_height (LrgWidget *self)
{
    LrgWidgetPrivate *priv;

    g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);

    priv = lrg_widget_get_instance_private (self);
    return priv->height;
}

/**
 * lrg_widget_set_height:
 * @self: an #LrgWidget
 * @height: the height
 *
 * Sets the widget's height.
 */
void
lrg_widget_set_height (LrgWidget *self,
                       gfloat     height)
{
    LrgWidgetPrivate *priv;

    g_return_if_fail (LRG_IS_WIDGET (self));
    g_return_if_fail (height >= 0.0f);

    priv = lrg_widget_get_instance_private (self);

    if (priv->height != height)
    {
        priv->height = height;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
    }
}

/**
 * lrg_widget_set_position:
 * @self: an #LrgWidget
 * @x: the X position
 * @y: the Y position
 *
 * Sets both the X and Y position at once.
 */
void
lrg_widget_set_position (LrgWidget *self,
                         gfloat     x,
                         gfloat     y)
{
    g_return_if_fail (LRG_IS_WIDGET (self));

    g_object_freeze_notify (G_OBJECT (self));
    lrg_widget_set_x (self, x);
    lrg_widget_set_y (self, y);
    g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_widget_set_size:
 * @self: an #LrgWidget
 * @width: the width
 * @height: the height
 *
 * Sets both the width and height at once.
 */
void
lrg_widget_set_size (LrgWidget *self,
                     gfloat     width,
                     gfloat     height)
{
    g_return_if_fail (LRG_IS_WIDGET (self));

    g_object_freeze_notify (G_OBJECT (self));
    lrg_widget_set_width (self, width);
    lrg_widget_set_height (self, height);
    g_object_thaw_notify (G_OBJECT (self));
}

/* ==========================================================================
 * Public API - World Coordinates
 * ========================================================================== */

/**
 * lrg_widget_get_world_x:
 * @self: an #LrgWidget
 *
 * Gets the widget's absolute X position in world coordinates.
 *
 * Returns: The absolute X position
 */
gfloat
lrg_widget_get_world_x (LrgWidget *self)
{
    LrgWidgetPrivate *priv;
    gfloat            world_x;

    g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);

    priv = lrg_widget_get_instance_private (self);
    world_x = priv->x;

    /* Add parent's world position if we have a parent */
    if (priv->parent != NULL)
    {
        world_x += lrg_widget_get_world_x (LRG_WIDGET (priv->parent));
    }

    return world_x;
}

/**
 * lrg_widget_get_world_y:
 * @self: an #LrgWidget
 *
 * Gets the widget's absolute Y position in world coordinates.
 *
 * Returns: The absolute Y position
 */
gfloat
lrg_widget_get_world_y (LrgWidget *self)
{
    LrgWidgetPrivate *priv;
    gfloat            world_y;

    g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);

    priv = lrg_widget_get_instance_private (self);
    world_y = priv->y;

    /* Add parent's world position if we have a parent */
    if (priv->parent != NULL)
    {
        world_y += lrg_widget_get_world_y (LRG_WIDGET (priv->parent));
    }

    return world_y;
}

/* ==========================================================================
 * Public API - State
 * ========================================================================== */

/**
 * lrg_widget_get_visible:
 * @self: an #LrgWidget
 *
 * Gets whether the widget is visible.
 *
 * Returns: %TRUE if visible
 */
gboolean
lrg_widget_get_visible (LrgWidget *self)
{
    LrgWidgetPrivate *priv;

    g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);

    priv = lrg_widget_get_instance_private (self);
    return priv->visible;
}

/**
 * lrg_widget_set_visible:
 * @self: an #LrgWidget
 * @visible: whether to show the widget
 *
 * Sets whether the widget is visible.
 */
void
lrg_widget_set_visible (LrgWidget *self,
                        gboolean   visible)
{
    LrgWidgetPrivate *priv;

    g_return_if_fail (LRG_IS_WIDGET (self));

    priv = lrg_widget_get_instance_private (self);

    visible = !!visible;  /* Normalize to 0/1 */

    if (priv->visible != visible)
    {
        priv->visible = visible;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
    }
}

/**
 * lrg_widget_get_enabled:
 * @self: an #LrgWidget
 *
 * Gets whether the widget is enabled.
 *
 * Returns: %TRUE if enabled
 */
gboolean
lrg_widget_get_enabled (LrgWidget *self)
{
    LrgWidgetPrivate *priv;

    g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);

    priv = lrg_widget_get_instance_private (self);
    return priv->enabled;
}

/**
 * lrg_widget_set_enabled:
 * @self: an #LrgWidget
 * @enabled: whether to enable the widget
 *
 * Sets whether the widget is enabled.
 */
void
lrg_widget_set_enabled (LrgWidget *self,
                        gboolean   enabled)
{
    LrgWidgetPrivate *priv;

    g_return_if_fail (LRG_IS_WIDGET (self));

    priv = lrg_widget_get_instance_private (self);

    enabled = !!enabled;  /* Normalize to 0/1 */

    if (priv->enabled != enabled)
    {
        priv->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
    }
}

/* ==========================================================================
 * Public API - Hierarchy
 * ========================================================================== */

/**
 * lrg_widget_get_parent:
 * @self: an #LrgWidget
 *
 * Gets the parent container of this widget.
 *
 * Returns: (transfer none) (nullable): The parent #LrgContainer, or %NULL
 */
LrgContainer *
lrg_widget_get_parent (LrgWidget *self)
{
    LrgWidgetPrivate *priv;

    g_return_val_if_fail (LRG_IS_WIDGET (self), NULL);

    priv = lrg_widget_get_instance_private (self);
    return priv->parent;
}

/* ==========================================================================
 * Public API - Hit Testing
 * ========================================================================== */

/**
 * lrg_widget_contains_point:
 * @self: an #LrgWidget
 * @x: the X coordinate in world space
 * @y: the Y coordinate in world space
 *
 * Checks if the given point is within the widget's bounds.
 *
 * Returns: %TRUE if the point is inside the widget
 */
gboolean
lrg_widget_contains_point (LrgWidget *self,
                           gfloat     x,
                           gfloat     y)
{
    LrgWidgetPrivate *priv;
    gfloat            world_x;
    gfloat            world_y;

    g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);

    priv = lrg_widget_get_instance_private (self);

    world_x = lrg_widget_get_world_x (self);
    world_y = lrg_widget_get_world_y (self);

    return (x >= world_x && x < world_x + priv->width &&
            y >= world_y && y < world_y + priv->height);
}

/* ==========================================================================
 * Public API - Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_widget_draw:
 * @self: an #LrgWidget
 *
 * Draws the widget by calling the virtual draw() method.
 * Only draws if the widget is visible.
 */
void
lrg_widget_draw (LrgWidget *self)
{
    LrgWidgetPrivate *priv;
    LrgWidgetClass   *klass;

    g_return_if_fail (LRG_IS_WIDGET (self));

    priv = lrg_widget_get_instance_private (self);

    /* Don't draw invisible widgets */
    if (!priv->visible)
    {
        return;
    }

    klass = LRG_WIDGET_GET_CLASS (self);
    if (klass->draw != NULL)
    {
        klass->draw (self);
    }
}

/**
 * lrg_widget_measure:
 * @self: an #LrgWidget
 * @preferred_width: (out): location to store preferred width
 * @preferred_height: (out): location to store preferred height
 *
 * Calculates the widget's preferred size by calling the
 * virtual measure() method.
 */
void
lrg_widget_measure (LrgWidget *self,
                    gfloat    *preferred_width,
                    gfloat    *preferred_height)
{
    LrgWidgetClass *klass;

    g_return_if_fail (LRG_IS_WIDGET (self));

    klass = LRG_WIDGET_GET_CLASS (self);
    if (klass->measure != NULL)
    {
        klass->measure (self, preferred_width, preferred_height);
    }
    else
    {
        /* Fallback if somehow measure is NULL */
        if (preferred_width != NULL)
        {
            *preferred_width = 0.0f;
        }
        if (preferred_height != NULL)
        {
            *preferred_height = 0.0f;
        }
    }
}

/**
 * lrg_widget_handle_event:
 * @self: an #LrgWidget
 * @event: the UI event to handle
 *
 * Handles a UI event by calling the virtual handle_event() method.
 * Only processes events if the widget is visible and enabled.
 *
 * Returns: %TRUE if the event was consumed
 */
gboolean
lrg_widget_handle_event (LrgWidget        *self,
                         const LrgUIEvent *event)
{
    LrgWidgetPrivate *priv;
    LrgWidgetClass   *klass;

    g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
    g_return_val_if_fail (event != NULL, FALSE);

    priv = lrg_widget_get_instance_private (self);

    /* Don't process events for invisible or disabled widgets */
    if (!priv->visible || !priv->enabled)
    {
        return FALSE;
    }

    klass = LRG_WIDGET_GET_CLASS (self);
    if (klass->handle_event != NULL)
    {
        return klass->handle_event (self, event);
    }

    return FALSE;
}
