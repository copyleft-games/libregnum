/* lrg-panel.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Container widget with a styled background.
 *
 * A panel paints a BOX surface (the theme's PANEL colour, a BORDER
 * hairline and rounded corners, with a drop shadow for elevations
 * 1-3) and then its children on top. Explicitly set legacy colours,
 * border width and radius win over the theme.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-panel.h"
#include "lrg-widget-private.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

/* Highest supported elevation */
#define PANEL_MAX_ELEVATION (3)

struct _LrgPanel
{
    LrgContainer  parent_instance;

    /* Legacy look; each *_set flag records an explicit value, which
     * then wins over the theme default */
    GrlColor      background_color;
    GrlColor      border_color;
    gboolean      has_border;
    gfloat        border_width;
    gfloat        corner_radius;
    gboolean      background_set;
    gboolean      border_set;
    gboolean      border_width_set;
    gboolean      radius_set;

    guint         elevation;
    gboolean      fill_children;
};

G_DEFINE_TYPE (LrgPanel, lrg_panel, LRG_TYPE_CONTAINER)

enum
{
    PROP_0,
    PROP_BACKGROUND_COLOR,
    PROP_BORDER_COLOR,
    PROP_BORDER_WIDTH,
    PROP_CORNER_RADIUS,
    PROP_ELEVATION,
    PROP_FILL_CHILDREN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Legacy default colour, reported by the getter until one is set */
static const GrlColor DEFAULT_BACKGROUND = { 50, 50, 50, 200 };

/* Shadow strength per elevation, as multiples of the theme's
 * SHADOW_BLUR / SHADOW_OFFSET_Y */
static const gfloat ELEVATION_SCALE[PANEL_MAX_ELEVATION + 1] = { 0.0f, 0.5f, 1.0f, 1.6f };

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * lrg_panel_style_defaults:
 *
 * BOX: the theme's PANEL fill with a BORDER hairline and the control
 * corner radius; the shadow grows with the elevation. Explicit legacy
 * values win (a %NULL border colour removes the border).
 */
static void
lrg_panel_style_defaults (LrgWidget      *widget,
                          LrgUIPart       part,
                          LrgWidgetState  state,
                          LrgPartLook    *look)
{
    LrgPanel *self = LRG_PANEL (widget);
    LrgTheme *theme = lrg_widget_get_theme (widget);
    gfloat    scale;

    if (part != LRG_UI_PART_BOX)
    {
        LRG_WIDGET_CLASS (lrg_panel_parent_class)->style_defaults (widget, part, state, look);
        return;
    }

    /* Fill */
    lrg_part_look_set_background (look, self->background_set
                                        ? &self->background_color
                                        : lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));

    /* Border: explicit colour, explicitly none, or the theme's */
    if (!self->border_set)
        lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
                                  self->border_width_set
                                  ? self->border_width
                                  : lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
    else if (self->has_border)
        lrg_part_look_set_border (look, &self->border_color, self->border_width);

    /* Shape */
    lrg_part_look_set_radius (look, self->radius_set
                                    ? self->corner_radius
                                    : lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));

    /* Elevation */
    scale = ELEVATION_SCALE[MIN (self->elevation, PANEL_MAX_ELEVATION)];
    if (scale > 0.0f)
        lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
                                  lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * scale,
                                  lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y) * scale);

    if (state & LRG_WIDGET_STATE_DISABLED)
        lrg_part_look_set_opacity (look, 0.6f);
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

/*
 * lrg_panel_paint:
 *
 * The surface first (shadow, fill, border), then the children on top
 * through the container's default paint.
 */
static void
lrg_panel_paint (LrgWidget    *widget,
                 LrgPaintList *list)
{
    LrgPartLook look;

    lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
    lrg_widget_paint_box (widget, list, &look,
                          lrg_widget_get_world_x (widget), lrg_widget_get_world_y (widget),
                          lrg_widget_get_width (widget), lrg_widget_get_height (widget));

    LRG_WIDGET_CLASS (lrg_panel_parent_class)->paint (widget, list);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

/*
 * lrg_panel_layout_children:
 *
 * With fill-children on, every visible child is placed in the padded
 * content box (its own margin, alignment and min/max apply), so a
 * panel can frame a box or grid. Off (the default), children keep the
 * positions they were given.
 */
static void
lrg_panel_layout_children (LrgContainer *container)
{
    LrgPanel *self = LRG_PANEL (container);
    gfloat    pad;
    gfloat    w;
    gfloat    h;
    GList    *l;

    if (!self->fill_children)
        return;

    pad = lrg_container_get_padding (container);
    w = MAX (0.0f, lrg_widget_get_width (LRG_WIDGET (self)) - pad * 2.0f);
    h = MAX (0.0f, lrg_widget_get_height (LRG_WIDGET (self)) - pad * 2.0f);

    for (l = lrg_container_get_children (container); l != NULL; l = l->next)
        if (lrg_widget_get_visible (LRG_WIDGET (l->data)))
            lrg_widget_place_in_slot (LRG_WIDGET (l->data), pad, pad, w, h);
}

/*
 * lrg_panel_measure:
 *
 * In fill-children mode the panel wants its largest child (with
 * margins) plus padding; otherwise the container's bounding-box rule.
 */
static void
lrg_panel_measure (LrgWidget *widget,
                   gfloat    *preferred_width,
                   gfloat    *preferred_height)
{
    LrgPanel *self = LRG_PANEL (widget);
    gfloat    pad;
    gfloat    mw = 0.0f;
    gfloat    mh = 0.0f;
    GList    *l;

    if (!self->fill_children)
    {
        LRG_WIDGET_CLASS (lrg_panel_parent_class)->measure (widget, preferred_width,
                                                            preferred_height);
        return;
    }

    pad = lrg_container_get_padding (LRG_CONTAINER (widget));
    for (l = lrg_container_get_children (LRG_CONTAINER (widget)); l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        LrgInsets  m;
        gfloat     cw;
        gfloat     ch;

        if (!lrg_widget_get_visible (child))
            continue;
        lrg_widget_get_preferred_size (child, &cw, &ch);
        lrg_widget_get_margin (child, &m);
        mw = MAX (mw, cw + m.left + m.right);
        mh = MAX (mh, ch + m.top + m.bottom);
    }

    if (preferred_width != NULL)
        *preferred_width = mw + pad * 2.0f;
    if (preferred_height != NULL)
        *preferred_height = mh + pad * 2.0f;
}

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
    case PROP_ELEVATION:
        g_value_set_uint (value, self->elevation);
        break;
    case PROP_FILL_CHILDREN:
        g_value_set_boolean (value, self->fill_children);
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
        if (g_value_get_boxed (value) != NULL)
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
    case PROP_ELEVATION:
        lrg_panel_set_elevation (self, g_value_get_uint (value));
        break;
    case PROP_FILL_CHILDREN:
        lrg_panel_set_fill_children (self, g_value_get_boolean (value));
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

    widget_class->paint = lrg_panel_paint;
    widget_class->measure = lrg_panel_measure;
    LRG_CONTAINER_CLASS (klass)->layout_children = lrg_panel_layout_children;
    widget_class->style_defaults = lrg_panel_style_defaults;

    /* lrg_widget_draw() renders paint() (background and children);
     * the container default would draw the children only */
    widget_class->draw = _lrg_widget_real_draw;

    /**
     * LrgPanel:background-color:
     *
     * The panel's background color. Until it is set the panel uses
     * the theme's PANEL colour; once set it wins.
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
     * The panel's border color. Until it is set the panel draws the
     * theme's BORDER hairline; set it to %NULL for no border.
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
     * The panel's border width in pixels. The default (1) is only
     * reported until set: the theme's BORDER_WIDTH is used.
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
     * The panel's corner radius. The default (0) is only reported
     * until set: the theme's CORNER_RADIUS is used.
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "The rounded corner radius in pixels",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPanel:elevation:
     *
     * How far the panel floats above what is behind it, 0 (flat) to 3.
     * Each step casts a larger, softer shadow based on the theme's
     * SHADOW_BLUR and SHADOW_OFFSET_Y.
     */
    properties[PROP_ELEVATION] =
        g_param_spec_uint ("elevation",
                           "Elevation",
                           "Shadow depth, 0 to 3",
                           0, PANEL_MAX_ELEVATION, 1,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPanel:fill-children:
     *
     * Lay children out inside the padded content box (each child's
     * margin, alignment and min/max size apply) instead of leaving
     * them where they were positioned. Use it to frame a box or grid.
     */
    properties[PROP_FILL_CHILDREN] =
        g_param_spec_boolean ("fill-children",
                              "Fill Children",
                              "Place children in the padded content box",
                              FALSE,
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
    self->elevation = 1;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/*
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

/*
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

/*
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

    self->background_set = TRUE;
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

/*
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

/*
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

    self->border_set = TRUE;
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

/*
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

/*
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

    self->border_width_set = TRUE;

    if (self->border_width != width)
    {
        self->border_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_WIDTH]);
    }
}

/* ==========================================================================
 * Public API - Corner Radius
 * ========================================================================== */

/*
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

/*
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

    self->radius_set = TRUE;

    if (self->corner_radius != radius)
    {
        self->corner_radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
    }
}

/* ==========================================================================
 * Public API - Elevation
 * ========================================================================== */

guint
lrg_panel_get_elevation (LrgPanel *self)
{
    g_return_val_if_fail (LRG_IS_PANEL (self), 0);
    return self->elevation;
}

void
lrg_panel_set_elevation (LrgPanel *self,
                         guint     elevation)
{
    g_return_if_fail (LRG_IS_PANEL (self));

    elevation = MIN (elevation, PANEL_MAX_ELEVATION);
    if (self->elevation != elevation)
    {
        self->elevation = elevation;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ELEVATION]);
    }
}

/* ==========================================================================
 * Public API - Child layout
 * ========================================================================== */

gboolean
lrg_panel_get_fill_children (LrgPanel *self)
{
    g_return_val_if_fail (LRG_IS_PANEL (self), FALSE);
    return self->fill_children;
}

void
lrg_panel_set_fill_children (LrgPanel *self,
                             gboolean  fill_children)
{
    g_return_if_fail (LRG_IS_PANEL (self));

    fill_children = !!fill_children;
    if (self->fill_children != fill_children)
    {
        self->fill_children = fill_children;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_CHILDREN]);
        lrg_widget_queue_layout (LRG_WIDGET (self));
    }
}
