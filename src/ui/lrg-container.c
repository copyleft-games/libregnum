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
    gboolean disposing;
    gboolean overflow_hit_test;
} LrgContainerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgContainer, lrg_container, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_SPACING,
    PROP_PADDING,
    PROP_OVERFLOW_HIT_TEST,
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

void
_lrg_container_default_draw (LrgWidget *widget)
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

/*
 * _lrg_container_default_paint:
 *
 * Paints the children in order (later children on top).
 */
void
_lrg_container_default_paint (LrgWidget    *widget,
                              LrgPaintList *list)
{
    LrgContainerPrivate *priv = lrg_container_get_instance_private (LRG_CONTAINER (widget));
    GList               *l;

    for (l = priv->children; l != NULL; l = l->next)
        lrg_widget_paint (LRG_WIDGET (l->data), list);
}

/*
 * lrg_container_real_arrange:
 *
 * A container's arrange step is its layout_children() method.
 */
static void
lrg_container_real_arrange (LrgWidget *widget)
{
    lrg_container_layout_children (LRG_CONTAINER (widget));
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

    /* Bubbling events come up from a child that already saw them;
     * forwarding them back down would deliver them twice */
    if (lrg_ui_event_get_bubbling (event))
        return FALSE;

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
    LrgContainerPrivate *priv = lrg_container_get_instance_private (self);

    priv->disposing = TRUE;
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
    case PROP_OVERFLOW_HIT_TEST:
        g_value_set_boolean (value, priv->overflow_hit_test);
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
    case PROP_OVERFLOW_HIT_TEST:
        lrg_container_set_overflow_hit_test (self, g_value_get_boolean (value));
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
    widget_class->draw = _lrg_container_default_draw;
    widget_class->paint = _lrg_container_default_paint;
    widget_class->arrange = lrg_container_real_arrange;
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

    /**
     * LrgContainer:overflow-hit-test:
     *
     * Whether pointer hit-testing reaches children placed outside the
     * container's rectangle (see lrg_container_set_overflow_hit_test()).
     */
    properties[PROP_OVERFLOW_HIT_TEST] =
        g_param_spec_boolean ("overflow-hit-test", NULL, NULL, FALSE,
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

/*
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

    /* Re-layout now (legacy callers expect it) and mark for the canvas */
    lrg_container_layout_children (self);
    lrg_widget_queue_layout (LRG_WIDGET (self));
}

void
lrg_container_insert_child_at (LrgContainer *self,
                               LrgWidget    *child,
                               guint         index)
{
    LrgContainerPrivate *priv;

    g_return_if_fail (LRG_IS_CONTAINER (self));
    g_return_if_fail (LRG_IS_WIDGET (child));
    g_return_if_fail (lrg_widget_get_parent (child) == NULL);

    priv = lrg_container_get_instance_private (self);

    /* g_list_insert appends for positions past the end */
    priv->children = g_list_insert (priv->children, g_object_ref (child),
                                    index > (guint) G_MAXINT ? -1 : (gint) index);
    _lrg_widget_set_parent (child, self);

    lrg_container_layout_children (self);
    lrg_widget_queue_layout (LRG_WIDGET (self));
}

void
lrg_container_reorder_child (LrgContainer *self,
                             LrgWidget    *child,
                             guint         index)
{
    LrgContainerPrivate *priv;
    GList               *link;

    g_return_if_fail (LRG_IS_CONTAINER (self));
    g_return_if_fail (LRG_IS_WIDGET (child));

    priv = lrg_container_get_instance_private (self);
    link = g_list_find (priv->children, child);
    g_return_if_fail (link != NULL);

    /* Unlink and reinsert the same reference: the child never loses
     * its parent, so focus, capture and state survive the move */
    if ((guint) g_list_position (priv->children, link) == index)
        return;
    priv->children = g_list_remove_link (priv->children, link);
    g_list_free_1 (link);
    priv->children = g_list_insert (priv->children, child,
                                    index > (guint) G_MAXINT ? -1 : (gint) index);

    lrg_container_layout_children (self);
    lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_container_get_overflow_hit_test (LrgContainer *self)
{
    LrgContainerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONTAINER (self), FALSE);
    priv = lrg_container_get_instance_private (self);
    return priv->overflow_hit_test;
}

void
lrg_container_set_overflow_hit_test (LrgContainer *self,
                                     gboolean      overflow)
{
    LrgContainerPrivate *priv;

    g_return_if_fail (LRG_IS_CONTAINER (self));
    priv = lrg_container_get_instance_private (self);

    overflow = !!overflow;
    if (priv->overflow_hit_test == overflow)
        return;
    priv->overflow_hit_test = overflow;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OVERFLOW_HIT_TEST]);
}

/*
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

    /* Re-layout now (legacy callers expect it) and mark for the canvas */
    lrg_container_layout_children (self);
    lrg_widget_queue_layout (LRG_WIDGET (self));
}

/*
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

    /* Same contract as add/remove: lay out now and mark for the canvas
     * (not while disposing: the container is going away) */
    if (!priv->disposing)
    {
        lrg_container_layout_children (self);
        lrg_widget_queue_layout (LRG_WIDGET (self));
    }
}

/*
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

/*
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

/*
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

/*
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

/*
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
        lrg_widget_queue_layout (LRG_WIDGET (self));
    }
}

/*
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

/*
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
        lrg_widget_queue_layout (LRG_WIDGET (self));
    }
}

/* ==========================================================================
 * Public API - Layout
 * ========================================================================== */

/*
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

/* ==========================================================================
 * Shared box layout (VBox / HBox)
 * ========================================================================== */

void
_lrg_box_measure (LrgContainer *self,
                  gboolean      vertical,
                  gboolean      homogeneous,
                  gfloat       *width,
                  gfloat       *height)
{
    gfloat padding = lrg_container_get_padding (self);
    gfloat spacing = lrg_container_get_spacing (self);
    gfloat main_sum = 0.0f;
    gfloat main_max = 0.0f;
    gfloat cross = 0.0f;
    guint  n = 0;
    GList *l;

    for (l = lrg_container_get_children (self); l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        gfloat     w;
        gfloat     h;

        if (!lrg_widget_get_visible (child))
            continue;

        _lrg_widget_outer_size (child, &w, &h);
        main_sum += vertical ? h : w;
        main_max = MAX (main_max, vertical ? h : w);
        cross = MAX (cross, vertical ? w : h);
        n++;
    }

    /* A homogeneous box needs every slot as big as the biggest */
    if (homogeneous)
        main_sum = main_max * (gfloat) n;
    if (n > 1)
        main_sum += spacing * (gfloat) (n - 1);

    if (width != NULL)
        *width = (vertical ? cross : main_sum) + padding * 2.0f;
    if (height != NULL)
        *height = (vertical ? main_sum : cross) + padding * 2.0f;
}

/*
 * box_shrink_lengths:
 *
 * The main-axis length of each visible child, in order, after @deficit
 * has been taken from the flexible ones (fill weight > 0) in proportion
 * to their weight, none below its minimum size plus margins.  Rounds
 * until the deficit is gone or every flexible child is at its minimum;
 * each round either clears the deficit or pins at least one child, so
 * it ends within as many rounds as there are children.
 */
static GArray *
box_shrink_lengths (LrgContainer *self,
                    gboolean      vertical,
                    gfloat        deficit)
{
    GArray   *lengths = g_array_new (FALSE, FALSE, sizeof (gfloat));
    GArray   *floors = g_array_new (FALSE, FALSE, sizeof (gfloat));
    GArray   *weight = g_array_new (FALSE, FALSE, sizeof (gfloat));
    GList    *l;
    guint     i;
    guint     round;

    for (l = lrg_container_get_children (self); l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        gfloat     w;
        gfloat     h;
        gfloat     pw;
        gfloat     ph;
        gfloat     min_w;
        gfloat     min_h;
        gfloat     len;
        gfloat     floor_len;
        gfloat     fw;

        if (!lrg_widget_get_visible (child))
            continue;

        _lrg_widget_outer_size (child, &w, &h);
        lrg_widget_get_preferred_size (child, &pw, &ph);
        lrg_widget_get_min_size (child, &min_w, &min_h);
        len = vertical ? h : w;
        /* the margins are part of the length and never given back */
        floor_len = vertical ? MAX (min_h, 0.0f) + (h - ph)
                             : MAX (min_w, 0.0f) + (w - pw);
        fw = lrg_widget_get_fill_weight (child);

        g_array_append_val (lengths, len);
        floor_len = MIN (floor_len, len);
        g_array_append_val (floors, floor_len);
        g_array_append_val (weight, fw);
    }

    for (round = 0; round <= lengths->len && deficit > 0.01f; round++)
    {
        gfloat active = 0.0f;
        gfloat taken = 0.0f;

        for (i = 0; i < lengths->len; i++)
        {
            if (g_array_index (weight, gfloat, i) > 0.0f &&
                g_array_index (lengths, gfloat, i) > g_array_index (floors, gfloat, i))
                active += g_array_index (weight, gfloat, i);
        }
        if (active <= 0.0f)
            break;

        for (i = 0; i < lengths->len; i++)
        {
            gfloat *len = &g_array_index (lengths, gfloat, i);
            gfloat  fl = g_array_index (floors, gfloat, i);
            gfloat  fw = g_array_index (weight, gfloat, i);
            gfloat  cut;

            if (fw <= 0.0f || *len <= fl)
                continue;

            cut = MIN (deficit * fw / active, *len - fl);
            *len -= cut;
            taken += cut;
        }
        deficit -= taken;
    }

    g_array_unref (floors);
    g_array_unref (weight);
    return lengths;
}

void
_lrg_box_layout (LrgContainer *self,
                 gboolean      vertical,
                 gboolean      homogeneous)
{
    LrgWidget *widget = LRG_WIDGET (self);
    gfloat     padding = lrg_container_get_padding (self);
    gfloat     spacing = lrg_container_get_spacing (self);
    gfloat     content_w;
    gfloat     content_h;
    gfloat     main_space;
    gfloat     used = 0.0f;
    gfloat     weights = 0.0f;
    gfloat     extra;
    gfloat     pos;
    guint      n = 0;
    guint      index = 0;
    GList     *l;
    g_autoptr(GArray) shrink = NULL;

    content_w = MAX (0.0f, lrg_widget_get_width (widget) - padding * 2.0f);
    content_h = MAX (0.0f, lrg_widget_get_height (widget) - padding * 2.0f);
    main_space = vertical ? content_h : content_w;

    /* Pass 1: preferred lengths along the main axis and weights */
    for (l = lrg_container_get_children (self); l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        gfloat     w;
        gfloat     h;

        if (!lrg_widget_get_visible (child))
            continue;
        _lrg_widget_outer_size (child, &w, &h);
        used += vertical ? h : w;
        weights += lrg_widget_get_fill_weight (child);
        n++;
    }
    if (n == 0)
        return;

    used += spacing * (gfloat) (n - 1);
    extra = main_space - used;

    /*
     * Short of room: take it back from the flexible children.
     *
     * Extra space was handed out by fill weight and a deficit was
     * ignored, so one child that asked for more than the box had -- a
     * one-line label holding a long sentence, a markdown label still at
     * the width it had before the window shrank -- pushed every sibling
     * after it past the box's edge, with nothing to say they had gone.
     * The flexible children give it back in proportion to their weight,
     * none below its own minimum size; one that reaches its minimum
     * drops out and the rest share what is left.  A box with no
     * flexible child overflows as before: shrinking a fixed child would
     * squash a button under its own label.
     */
    shrink = NULL;
    if (!homogeneous && extra < 0.0f && weights > 0.0f)
        shrink = box_shrink_lengths (self, vertical, -extra);

    /* Pass 2: hand out slots and place each child in its slot */
    pos = padding;
    for (l = lrg_container_get_children (self); l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        gfloat     w;
        gfloat     h;
        gfloat     len;

        if (!lrg_widget_get_visible (child))
            continue;

        if (homogeneous)
        {
            len = (main_space - spacing * (gfloat) (n - 1)) / (gfloat) n;
        }
        else
        {
            _lrg_widget_outer_size (child, &w, &h);
            len = vertical ? h : w;
            if (extra > 0.0f && weights > 0.0f)
                len += extra * lrg_widget_get_fill_weight (child) / weights;
            else if (shrink != NULL)
                len = g_array_index (shrink, gfloat, index);
        }
        len = MAX (len, 0.0f);
        index++;

        if (vertical)
            lrg_widget_place_in_slot (child, padding, pos, content_w, len);
        else
            lrg_widget_place_in_slot (child, pos, padding, len, content_h);

        pos += len + spacing;
    }
}
