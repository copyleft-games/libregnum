/* lrg-container.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract container widget that can hold child widgets.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-container.h"
#include "lrg-widget-private.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    GList  *children;  /* List of LrgWidget* (owned references) */
    gfloat  spacing;
    gfloat  padding;
} LrgContainerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgContainer, lrg_container, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_SPACING,
    PROP_PADDING,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_container_real_layout_children (LrgContainer *self)
{
    /* Default implementation does nothing - subclasses must override */
}

static void
lrg_container_real_draw (LrgWidget *widget)
{
    LrgContainer        *self = LRG_CONTAINER (widget);
    LrgContainerPrivate *priv = lrg_container_get_instance_private (self);
    GList               *l;

    /* Draw all visible children */
    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        lrg_widget_draw (child);
    }
}

static void
lrg_container_real_measure (LrgWidget *widget,
                            gfloat    *preferred_width,
                            gfloat    *preferred_height)
{
    LrgContainer        *self = LRG_CONTAINER (widget);
    LrgContainerPrivate *priv = lrg_container_get_instance_private (self);
    gfloat               max_width = 0.0f;
    gfloat               max_height = 0.0f;
    GList               *l;

    /*
     * Default measurement: find the bounding box of all children.
     * Subclasses should override for their specific layout.
     */
    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        gfloat     child_width, child_height;
        gfloat     child_x, child_y;

        if (!lrg_widget_get_visible (child))
        {
            continue;
        }

        lrg_widget_measure (child, &child_width, &child_height);
        child_x = lrg_widget_get_x (child);
        child_y = lrg_widget_get_y (child);

        if (child_x + child_width > max_width)
        {
            max_width = child_x + child_width;
        }
        if (child_y + child_height > max_height)
        {
            max_height = child_y + child_height;
        }
    }

    /* Add padding */
    max_width += priv->padding * 2;
    max_height += priv->padding * 2;

    if (preferred_width != NULL)
    {
        *preferred_width = max_width;
    }
    if (preferred_height != NULL)
    {
        *preferred_height = max_height;
    }
}

static gboolean
lrg_container_real_handle_event (LrgWidget        *widget,
                                 const LrgUIEvent *event)
{
    LrgContainer        *self = LRG_CONTAINER (widget);
    LrgContainerPrivate *priv = lrg_container_get_instance_private (self);
    GList               *l;

    /*
     * Dispatch event to children in reverse order (front to back).
     * Stop if a child consumes the event.
     */
    for (l = g_list_last (priv->children); l != NULL; l = l->prev)
    {
        LrgWidget *child = LRG_WIDGET (l->data);

        if (lrg_widget_handle_event (child, event))
        {
            return TRUE;
        }
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_container_dispose (GObject *object)
{
    LrgContainer *self = LRG_CONTAINER (object);

    /* Remove all children (releases references) */
    lrg_container_remove_all (self);

    G_OBJECT_CLASS (lrg_container_parent_class)->dispose (object);
}

static void
lrg_container_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgContainer        *self = LRG_CONTAINER (object);
    LrgContainerPrivate *priv = lrg_container_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_SPACING:
        g_value_set_float (value, priv->spacing);
        break;
    case PROP_PADDING:
        g_value_set_float (value, priv->padding);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_container_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgContainer *self = LRG_CONTAINER (object);

    switch (prop_id)
    {
    case PROP_SPACING:
        lrg_container_set_spacing (self, g_value_get_float (value));
        break;
    case PROP_PADDING:
        lrg_container_set_padding (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_container_class_init (LrgContainerClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->dispose = lrg_container_dispose;
    object_class->get_property = lrg_container_get_property;
    object_class->set_property = lrg_container_set_property;

    /* Override widget virtual methods */
    widget_class->draw = lrg_container_real_draw;
    widget_class->measure = lrg_container_real_measure;
    widget_class->handle_event = lrg_container_real_handle_event;

    /* Container virtual methods */
    klass->layout_children = lrg_container_real_layout_children;

    /**
     * LrgContainer:spacing:
     *
     * The spacing between child widgets in pixels.
     */
    properties[PROP_SPACING] =
        g_param_spec_float ("spacing",
                            "Spacing",
                            "Space between children in pixels",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgContainer:padding:
     *
     * The padding around the container's content in pixels.
     */
    properties[PROP_PADDING] =
        g_param_spec_float ("padding",
                            "Padding",
                            "Padding around content in pixels",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_container_init (LrgContainer *self)
{
    LrgContainerPrivate *priv = lrg_container_get_instance_private (self);

    priv->children = NULL;
    priv->spacing = 0.0f;
    priv->padding = 0.0f;
}

/* ==========================================================================
 * Public API - Child Management
 * ========================================================================== */

/**
 * lrg_container_add_child:
 * @self: an #LrgContainer
 * @child: the widget to add
 *
 * Adds a child widget to this container.
 */
void
lrg_container_add_child (LrgContainer *self,
                         LrgWidget    *child)
{
    LrgContainerPrivate *priv;

    g_return_if_fail (LRG_IS_CONTAINER (self));
    g_return_if_fail (LRG_IS_WIDGET (child));
    g_return_if_fail (lrg_widget_get_parent (child) == NULL);

    priv = lrg_container_get_instance_private (self);

    /* Take ownership of the child */
    priv->children = g_list_append (priv->children, g_object_ref (child));

    /* Set the child's parent */
    _lrg_widget_set_parent (child, self);

    /* Re-layout */
    lrg_container_layout_children (self);
}

/**
 * lrg_container_remove_child:
 * @self: an #LrgContainer
 * @child: the widget to remove
 *
 * Removes a child widget from this container.
 */
void
lrg_container_remove_child (LrgContainer *self,
                            LrgWidget    *child)
{
    LrgContainerPrivate *priv;
    GList               *link;

    g_return_if_fail (LRG_IS_CONTAINER (self));
    g_return_if_fail (LRG_IS_WIDGET (child));

    priv = lrg_container_get_instance_private (self);

    link = g_list_find (priv->children, child);
    if (link == NULL)
    {
        g_warning ("Child not found in container");
        return;
    }

    /* Clear the child's parent first */
    _lrg_widget_set_parent (child, NULL);

    /* Remove from list and release reference */
    priv->children = g_list_delete_link (priv->children, link);
    g_object_unref (child);

    /* Re-layout */
    lrg_container_layout_children (self);
}

/**
 * lrg_container_remove_all:
 * @self: an #LrgContainer
 *
 * Removes all child widgets from this container.
 */
void
lrg_container_remove_all (LrgContainer *self)
{
    LrgContainerPrivate *priv;
    GList               *l;

    g_return_if_fail (LRG_IS_CONTAINER (self));

    priv = lrg_container_get_instance_private (self);

    /* Clear parent and release all children */
    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        _lrg_widget_set_parent (child, NULL);
        g_object_unref (child);
    }

    g_list_free (priv->children);
    priv->children = NULL;
}

/**
 * lrg_container_get_child_count:
 * @self: an #LrgContainer
 *
 * Gets the number of children in this container.
 *
 * Returns: The child count
 */
guint
lrg_container_get_child_count (LrgContainer *self)
{
    LrgContainerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONTAINER (self), 0);

    priv = lrg_container_get_instance_private (self);
    return g_list_length (priv->children);
}

/**
 * lrg_container_get_child:
 * @self: an #LrgContainer
 * @index: the child index
 *
 * Gets a child widget by index.
 *
 * Returns: (transfer none) (nullable): The child at @index, or %NULL
 */
LrgWidget *
lrg_container_get_child (LrgContainer *self,
                         guint         index)
{
    LrgContainerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONTAINER (self), NULL);

    priv = lrg_container_get_instance_private (self);
    return g_list_nth_data (priv->children, index);
}

/**
 * lrg_container_get_children:
 * @self: an #LrgContainer
 *
 * Gets the list of all children.
 *
 * Returns: (transfer none) (element-type LrgWidget): The children list
 */
GList *
lrg_container_get_children (LrgContainer *self)
{
    LrgContainerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONTAINER (self), NULL);

    priv = lrg_container_get_instance_private (self);
    return priv->children;
}

/* ==========================================================================
 * Public API - Layout Properties
 * ========================================================================== */

/**
 * lrg_container_get_spacing:
 * @self: an #LrgContainer
 *
 * Gets the spacing between child widgets.
 *
 * Returns: The spacing in pixels
 */
gfloat
lrg_container_get_spacing (LrgContainer *self)
{
    LrgContainerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONTAINER (self), 0.0f);

    priv = lrg_container_get_instance_private (self);
    return priv->spacing;
}

/**
 * lrg_container_set_spacing:
 * @self: an #LrgContainer
 * @spacing: the spacing in pixels
 *
 * Sets the spacing between child widgets.
 */
void
lrg_container_set_spacing (LrgContainer *self,
                           gfloat        spacing)
{
    LrgContainerPrivate *priv;

    g_return_if_fail (LRG_IS_CONTAINER (self));
    g_return_if_fail (spacing >= 0.0f);

    priv = lrg_container_get_instance_private (self);

    if (priv->spacing != spacing)
    {
        priv->spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPACING]);
        lrg_container_layout_children (self);
    }
}

/**
 * lrg_container_get_padding:
 * @self: an #LrgContainer
 *
 * Gets the padding around the container's content.
 *
 * Returns: The padding in pixels
 */
gfloat
lrg_container_get_padding (LrgContainer *self)
{
    LrgContainerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONTAINER (self), 0.0f);

    priv = lrg_container_get_instance_private (self);
    return priv->padding;
}

/**
 * lrg_container_set_padding:
 * @self: an #LrgContainer
 * @padding: the padding in pixels
 *
 * Sets the padding around the container's content.
 */
void
lrg_container_set_padding (LrgContainer *self,
                           gfloat        padding)
{
    LrgContainerPrivate *priv;

    g_return_if_fail (LRG_IS_CONTAINER (self));
    g_return_if_fail (padding >= 0.0f);

    priv = lrg_container_get_instance_private (self);

    if (priv->padding != padding)
    {
        priv->padding = padding;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING]);
        lrg_container_layout_children (self);
    }
}

/* ==========================================================================
 * Public API - Layout
 * ========================================================================== */

/**
 * lrg_container_layout_children:
 * @self: an #LrgContainer
 *
 * Triggers the layout_children() virtual method.
 */
void
lrg_container_layout_children (LrgContainer *self)
{
    LrgContainerClass *klass;

    g_return_if_fail (LRG_IS_CONTAINER (self));

    klass = LRG_CONTAINER_GET_CLASS (self);
    if (klass->layout_children != NULL)
    {
        klass->layout_children (self);
    }
}
