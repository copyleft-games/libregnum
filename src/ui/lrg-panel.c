/* lrg-panel.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Container widget with styled background.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-panel.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgPanel
{
    LrgContainer  parent_instance;

    GrlColor      background_color;
    GrlColor      border_color;
    gboolean      has_border;
    gfloat        border_width;
    gfloat        corner_radius;
};

G_DEFINE_TYPE (LrgPanel, lrg_panel, LRG_TYPE_CONTAINER)

enum
{
    PROP_0,
    PROP_BACKGROUND_COLOR,
    PROP_BORDER_COLOR,
    PROP_BORDER_WIDTH,
    PROP_CORNER_RADIUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default colors */
static const GrlColor DEFAULT_BACKGROUND = { 50, 50, 50, 200 };

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_panel_draw (LrgWidget *widget)
{
    LrgPanel     *self = LRG_PANEL (widget);
    GrlRectangle  rect;

    rect.x = lrg_widget_get_world_x (widget);
    rect.y = lrg_widget_get_world_y (widget);
    rect.width = lrg_widget_get_width (widget);
    rect.height = lrg_widget_get_height (widget);

    /* Draw background */
    if (self->corner_radius > 0.0f)
    {
        gfloat roundness;

        /* Calculate roundness as ratio (0.0-1.0) */
        roundness = self->corner_radius / (rect.width < rect.height ?
                                           rect.width : rect.height);
        if (roundness > 1.0f)
        {
            roundness = 1.0f;
        }

        grl_draw_rectangle_rounded (&rect, roundness, 8, &self->background_color);

        if (self->has_border && self->border_width > 0.0f)
        {
            grl_draw_rectangle_rounded_lines_ex (&rect, roundness, 8,
                                                 self->border_width,
                                                 &self->border_color);
        }
    }
    else
    {
        grl_draw_rectangle_rec (&rect, &self->background_color);

        if (self->has_border && self->border_width > 0.0f)
        {
            grl_draw_rectangle_lines_ex (&rect, self->border_width,
                                         &self->border_color);
        }
    }

    /* Draw children (call parent implementation) */
    LRG_WIDGET_CLASS (lrg_panel_parent_class)->draw (widget);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_panel_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    LrgPanel *self = LRG_PANEL (object);

    switch (prop_id)
    {
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, &self->background_color);
        break;
    case PROP_BORDER_COLOR:
        g_value_set_boxed (value, self->has_border ? &self->border_color : NULL);
        break;
    case PROP_BORDER_WIDTH:
        g_value_set_float (value, self->border_width);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_float (value, self->corner_radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_panel_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    LrgPanel *self = LRG_PANEL (object);

    switch (prop_id)
    {
    case PROP_BACKGROUND_COLOR:
        lrg_panel_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_BORDER_COLOR:
        lrg_panel_set_border_color (self, g_value_get_boxed (value));
        break;
    case PROP_BORDER_WIDTH:
        lrg_panel_set_border_width (self, g_value_get_float (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_panel_set_corner_radius (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_panel_class_init (LrgPanelClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->get_property = lrg_panel_get_property;
    object_class->set_property = lrg_panel_set_property;

    widget_class->draw = lrg_panel_draw;

    /**
     * LrgPanel:background-color:
     *
     * The panel's background color.
     */
    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "The background fill color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPanel:border-color:
     *
     * The panel's border color.
     * Set to %NULL for no border.
     */
    properties[PROP_BORDER_COLOR] =
        g_param_spec_boxed ("border-color",
                            "Border Color",
                            "The border color, or NULL for no border",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPanel:border-width:
     *
     * The panel's border width in pixels.
     */
    properties[PROP_BORDER_WIDTH] =
        g_param_spec_float ("border-width",
                            "Border Width",
                            "The border thickness in pixels",
                            0.0f, 100.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPanel:corner-radius:
     *
     * The panel's corner radius for rounded corners.
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "The rounded corner radius in pixels",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_panel_init (LrgPanel *self)
{
    self->background_color = DEFAULT_BACKGROUND;
    self->has_border = FALSE;
    self->border_width = 1.0f;
    self->corner_radius = 0.0f;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_panel_new:
 *
 * Creates a new panel widget.
 *
 * Returns: (transfer full): A new #LrgPanel
 */
LrgPanel *
lrg_panel_new (void)
{
    return g_object_new (LRG_TYPE_PANEL, NULL);
}

/* ==========================================================================
 * Public API - Background
 * ========================================================================== */

/**
 * lrg_panel_get_background_color:
 * @self: an #LrgPanel
 *
 * Gets the panel's background color.
 *
 * Returns: (transfer none): The background color
 */
const GrlColor *
lrg_panel_get_background_color (LrgPanel *self)
{
    g_return_val_if_fail (LRG_IS_PANEL (self), &DEFAULT_BACKGROUND);

    return &self->background_color;
}

/**
 * lrg_panel_set_background_color:
 * @self: an #LrgPanel
 * @color: the background color
 *
 * Sets the panel's background color.
 */
void
lrg_panel_set_background_color (LrgPanel       *self,
                                const GrlColor *color)
{
    g_return_if_fail (LRG_IS_PANEL (self));
    g_return_if_fail (color != NULL);

    if (self->background_color.r != color->r ||
        self->background_color.g != color->g ||
        self->background_color.b != color->b ||
        self->background_color.a != color->a)
    {
        self->background_color = *color;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
    }
}

/* ==========================================================================
 * Public API - Border
 * ========================================================================== */

/**
 * lrg_panel_get_border_color:
 * @self: an #LrgPanel
 *
 * Gets the panel's border color.
 *
 * Returns: (transfer none) (nullable): The border color, or %NULL if no border
 */
const GrlColor *
lrg_panel_get_border_color (LrgPanel *self)
{
    g_return_val_if_fail (LRG_IS_PANEL (self), NULL);

    return self->has_border ? &self->border_color : NULL;
}

/**
 * lrg_panel_set_border_color:
 * @self: an #LrgPanel
 * @color: (nullable): the border color, or %NULL for no border
 *
 * Sets the panel's border color.
 */
void
lrg_panel_set_border_color (LrgPanel       *self,
                            const GrlColor *color)
{
    g_return_if_fail (LRG_IS_PANEL (self));

    if (color == NULL)
    {
        if (self->has_border)
        {
            self->has_border = FALSE;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_COLOR]);
        }
    }
    else
    {
        if (!self->has_border ||
            self->border_color.r != color->r ||
            self->border_color.g != color->g ||
            self->border_color.b != color->b ||
            self->border_color.a != color->a)
        {
            self->has_border = TRUE;
            self->border_color = *color;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_COLOR]);
        }
    }
}

/**
 * lrg_panel_get_border_width:
 * @self: an #LrgPanel
 *
 * Gets the panel's border width.
 *
 * Returns: The border width in pixels
 */
gfloat
lrg_panel_get_border_width (LrgPanel *self)
{
    g_return_val_if_fail (LRG_IS_PANEL (self), 1.0f);

    return self->border_width;
}

/**
 * lrg_panel_set_border_width:
 * @self: an #LrgPanel
 * @width: the border width in pixels
 *
 * Sets the panel's border width.
 */
void
lrg_panel_set_border_width (LrgPanel *self,
                            gfloat    width)
{
    g_return_if_fail (LRG_IS_PANEL (self));
    g_return_if_fail (width >= 0.0f);

    if (self->border_width != width)
    {
        self->border_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_WIDTH]);
    }
}

/* ==========================================================================
 * Public API - Corner Radius
 * ========================================================================== */

/**
 * lrg_panel_get_corner_radius:
 * @self: an #LrgPanel
 *
 * Gets the panel's corner radius.
 *
 * Returns: The corner radius in pixels
 */
gfloat
lrg_panel_get_corner_radius (LrgPanel *self)
{
    g_return_val_if_fail (LRG_IS_PANEL (self), 0.0f);

    return self->corner_radius;
}

/**
 * lrg_panel_set_corner_radius:
 * @self: an #LrgPanel
 * @radius: the corner radius in pixels
 *
 * Sets the panel's corner radius.
 */
void
lrg_panel_set_corner_radius (LrgPanel *self,
                             gfloat    radius)
{
    g_return_if_fail (LRG_IS_PANEL (self));
    g_return_if_fail (radius >= 0.0f);

    if (self->corner_radius != radius)
    {
        self->corner_radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
    }
}
