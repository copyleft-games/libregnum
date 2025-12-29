/* lrg-highlight.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Highlight widget for tutorial system.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TUTORIAL

#include "lrg-highlight.h"
#include "../lrg-log.h"

#include <math.h>

struct _LrgHighlight
{
    LrgWidget parent_instance;

    LrgWidget        *target;
    LrgHighlightStyle style;
    GrlColor          color;

    gfloat padding;
    gfloat outline_thickness;
    gfloat corner_radius;

    /* Animation */
    gboolean animated;
    gfloat   pulse_speed;
    gfloat   animation_time;

    /* Manual target rect (when target widget is NULL) */
    gfloat target_x;
    gfloat target_y;
    gfloat target_width;
    gfloat target_height;
    gboolean use_manual_rect;

    /* Screen dimensions for darken/spotlight styles */
    gint screen_width;
    gint screen_height;
};

G_DEFINE_TYPE (LrgHighlight, lrg_highlight, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_TARGET,
    PROP_STYLE,
    PROP_COLOR,
    PROP_PADDING,
    PROP_OUTLINE_THICKNESS,
    PROP_CORNER_RADIUS,
    PROP_ANIMATED,
    PROP_PULSE_SPEED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default yellow-ish highlight color */
static const GrlColor DEFAULT_COLOR = { 255, 220, 50, 255 };

/* Darken overlay color */
static const GrlColor DARKEN_COLOR = { 0, 0, 0, 180 };

static void
get_target_rect (LrgHighlight *self,
                 gfloat       *x,
                 gfloat       *y,
                 gfloat       *width,
                 gfloat       *height)
{
    if (self->use_manual_rect)
    {
        *x = self->target_x;
        *y = self->target_y;
        *width = self->target_width;
        *height = self->target_height;
    }
    else if (self->target != NULL)
    {
        *x = lrg_widget_get_world_x (self->target);
        *y = lrg_widget_get_world_y (self->target);
        *width = lrg_widget_get_width (self->target);
        *height = lrg_widget_get_height (self->target);
    }
    else
    {
        *x = 0;
        *y = 0;
        *width = 0;
        *height = 0;
    }
}

static gfloat
get_pulse_alpha (LrgHighlight *self)
{
    gfloat pulse;

    if (!self->animated)
        return 1.0f;

    /* Sine wave oscillation between 0.5 and 1.0 */
    pulse = (sinf (self->animation_time * self->pulse_speed * 2.0f * G_PI) + 1.0f) / 2.0f;
    return 0.5f + (pulse * 0.5f);
}

static void
lrg_highlight_draw (LrgWidget *widget)
{
    LrgHighlight *self = LRG_HIGHLIGHT (widget);
    gfloat target_x, target_y, target_w, target_h;
    gfloat rect_x, rect_y, rect_w, rect_h;
    GrlColor draw_color;
    gfloat alpha_mult;

    get_target_rect (self, &target_x, &target_y, &target_w, &target_h);

    if (target_w <= 0 || target_h <= 0)
        return;

    /* Apply padding */
    rect_x = target_x - self->padding;
    rect_y = target_y - self->padding;
    rect_w = target_w + (self->padding * 2.0f);
    rect_h = target_h + (self->padding * 2.0f);

    /* Apply animation */
    alpha_mult = get_pulse_alpha (self);
    draw_color = self->color;
    draw_color.a = (guint8)(draw_color.a * alpha_mult);

    switch (self->style)
    {
    case LRG_HIGHLIGHT_STYLE_OUTLINE:
        {
            g_autoptr(GrlRectangle) rect = NULL;

            rect = grl_rectangle_new (rect_x, rect_y, rect_w, rect_h);

            if (self->corner_radius > 0)
            {
                grl_draw_rectangle_rounded_lines_ex (rect, self->corner_radius,
                                                      8, self->outline_thickness,
                                                      &draw_color);
            }
            else
            {
                grl_draw_rectangle_lines_ex (rect, self->outline_thickness, &draw_color);
            }
        }
        break;

    case LRG_HIGHLIGHT_STYLE_GLOW:
        {
            /* Draw multiple expanding outlines with decreasing alpha for glow effect */
            gint i;
            gint glow_layers = 5;

            for (i = glow_layers; i >= 1; i--)
            {
                g_autoptr(GrlRectangle) glow_rect = NULL;
                GrlColor glow_color;
                gfloat expand;

                expand = (gfloat)i * 2.0f;
                glow_rect = grl_rectangle_new (rect_x - expand,
                                               rect_y - expand,
                                               rect_w + (expand * 2.0f),
                                               rect_h + (expand * 2.0f));

                glow_color = draw_color;
                glow_color.a = (guint8)(glow_color.a * (1.0f - ((gfloat)i / (gfloat)(glow_layers + 1))));

                if (self->corner_radius > 0)
                {
                    grl_draw_rectangle_rounded_lines_ex (glow_rect,
                                                          self->corner_radius + expand,
                                                          8, 2.0f, &glow_color);
                }
                else
                {
                    grl_draw_rectangle_lines_ex (glow_rect, 2.0f, &glow_color);
                }
            }
        }
        break;

    case LRG_HIGHLIGHT_STYLE_DARKEN_OTHERS:
        {
            /* Draw dark overlay with cutout for the highlighted area */
            g_autoptr(GrlRectangle) top_rect = NULL;
            g_autoptr(GrlRectangle) bottom_rect = NULL;
            g_autoptr(GrlRectangle) left_rect = NULL;
            g_autoptr(GrlRectangle) right_rect = NULL;
            GrlColor darken = DARKEN_COLOR;

            darken.a = (guint8)(darken.a * alpha_mult);

            /* Get screen dimensions - fall back to reasonable defaults */
            if (self->screen_width == 0 || self->screen_height == 0)
            {
                self->screen_width = 1920;
                self->screen_height = 1080;
            }

            /* Top region */
            if (rect_y > 0)
            {
                top_rect = grl_rectangle_new (0, 0, (gfloat)self->screen_width, rect_y);
                grl_draw_rectangle_rec (top_rect, &darken);
            }

            /* Bottom region */
            if (rect_y + rect_h < self->screen_height)
            {
                bottom_rect = grl_rectangle_new (0, rect_y + rect_h,
                                                  (gfloat)self->screen_width,
                                                  (gfloat)self->screen_height - (rect_y + rect_h));
                grl_draw_rectangle_rec (bottom_rect, &darken);
            }

            /* Left region */
            if (rect_x > 0)
            {
                left_rect = grl_rectangle_new (0, rect_y, rect_x, rect_h);
                grl_draw_rectangle_rec (left_rect, &darken);
            }

            /* Right region */
            if (rect_x + rect_w < self->screen_width)
            {
                right_rect = grl_rectangle_new (rect_x + rect_w, rect_y,
                                                 (gfloat)self->screen_width - (rect_x + rect_w),
                                                 rect_h);
                grl_draw_rectangle_rec (right_rect, &darken);
            }

            /* Draw outline around the cutout */
            {
                g_autoptr(GrlRectangle) outline_rect = NULL;
                outline_rect = grl_rectangle_new (rect_x, rect_y, rect_w, rect_h);

                if (self->corner_radius > 0)
                {
                    grl_draw_rectangle_rounded_lines_ex (outline_rect, self->corner_radius,
                                                          8, 2.0f, &draw_color);
                }
                else
                {
                    grl_draw_rectangle_lines_ex (outline_rect, 2.0f, &draw_color);
                }
            }
        }
        break;

    case LRG_HIGHLIGHT_STYLE_SPOTLIGHT:
        {
            /* Draw dark overlay everywhere */
            g_autoptr(GrlRectangle) full_rect = NULL;
            GrlColor darken = DARKEN_COLOR;

            darken.a = (guint8)(darken.a * alpha_mult);

            if (self->screen_width == 0 || self->screen_height == 0)
            {
                self->screen_width = 1920;
                self->screen_height = 1080;
            }

            full_rect = grl_rectangle_new (0, 0, (gfloat)self->screen_width,
                                           (gfloat)self->screen_height);
            grl_draw_rectangle_rec (full_rect, &darken);

            /* Draw spotlight circle at target center */
            {
                gfloat center_x = rect_x + (rect_w / 2.0f);
                gfloat center_y = rect_y + (rect_h / 2.0f);
                gfloat radius = fmaxf (rect_w, rect_h) / 2.0f + self->padding;

                /* Clear color indicates cutout (not actually rendered, just visual idea) */
                /* In practice we'd need shader for true spotlight effect */
                /* For now, draw a ring highlight */
                {
                    GrlVector2 center_vec = { center_x, center_y };
                    grl_draw_ring (&center_vec,
                                   radius - 2.0f, radius + 2.0f,
                                   0.0f, 360.0f, 32, &draw_color);
                }
            }
        }
        break;

    default:
        break;
    }
}

static void
lrg_highlight_measure (LrgWidget *widget,
                       gfloat    *preferred_width,
                       gfloat    *preferred_height)
{
    LrgHighlight *self = LRG_HIGHLIGHT (widget);
    gfloat target_x, target_y, target_w, target_h;

    get_target_rect (self, &target_x, &target_y, &target_w, &target_h);

    if (preferred_width != NULL)
        *preferred_width = target_w + (self->padding * 2.0f);
    if (preferred_height != NULL)
        *preferred_height = target_h + (self->padding * 2.0f);
}

static void
lrg_highlight_finalize (GObject *object)
{
    LrgHighlight *self = LRG_HIGHLIGHT (object);

    g_clear_object (&self->target);

    G_OBJECT_CLASS (lrg_highlight_parent_class)->finalize (object);
}

static void
lrg_highlight_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgHighlight *self = LRG_HIGHLIGHT (object);

    switch (prop_id)
    {
    case PROP_TARGET:
        g_value_set_object (value, self->target);
        break;
    case PROP_STYLE:
        g_value_set_enum (value, self->style);
        break;
    case PROP_COLOR:
        g_value_set_boxed (value, &self->color);
        break;
    case PROP_PADDING:
        g_value_set_float (value, self->padding);
        break;
    case PROP_OUTLINE_THICKNESS:
        g_value_set_float (value, self->outline_thickness);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_float (value, self->corner_radius);
        break;
    case PROP_ANIMATED:
        g_value_set_boolean (value, self->animated);
        break;
    case PROP_PULSE_SPEED:
        g_value_set_float (value, self->pulse_speed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_highlight_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgHighlight *self = LRG_HIGHLIGHT (object);

    switch (prop_id)
    {
    case PROP_TARGET:
        lrg_highlight_set_target (self, g_value_get_object (value));
        break;
    case PROP_STYLE:
        lrg_highlight_set_style (self, g_value_get_enum (value));
        break;
    case PROP_COLOR:
        lrg_highlight_set_color (self, g_value_get_boxed (value));
        break;
    case PROP_PADDING:
        lrg_highlight_set_padding (self, g_value_get_float (value));
        break;
    case PROP_OUTLINE_THICKNESS:
        lrg_highlight_set_outline_thickness (self, g_value_get_float (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_highlight_set_corner_radius (self, g_value_get_float (value));
        break;
    case PROP_ANIMATED:
        lrg_highlight_set_animated (self, g_value_get_boolean (value));
        break;
    case PROP_PULSE_SPEED:
        lrg_highlight_set_pulse_speed (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_highlight_class_init (LrgHighlightClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_highlight_finalize;
    object_class->get_property = lrg_highlight_get_property;
    object_class->set_property = lrg_highlight_set_property;

    widget_class->draw = lrg_highlight_draw;
    widget_class->measure = lrg_highlight_measure;

    /**
     * LrgHighlight:target:
     *
     * The widget being highlighted.
     *
     * Since: 1.0
     */
    properties[PROP_TARGET] =
        g_param_spec_object ("target", NULL, NULL,
                             LRG_TYPE_WIDGET,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgHighlight:style:
     *
     * The highlight style.
     *
     * Since: 1.0
     */
    properties[PROP_STYLE] =
        g_param_spec_enum ("style", NULL, NULL,
                           LRG_TYPE_HIGHLIGHT_STYLE,
                           LRG_HIGHLIGHT_STYLE_OUTLINE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgHighlight:color:
     *
     * The highlight color.
     *
     * Since: 1.0
     */
    properties[PROP_COLOR] =
        g_param_spec_boxed ("color", NULL, NULL,
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgHighlight:padding:
     *
     * Padding around the target.
     *
     * Since: 1.0
     */
    properties[PROP_PADDING] =
        g_param_spec_float ("padding", NULL, NULL,
                            0.0f, 100.0f, 4.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgHighlight:outline-thickness:
     *
     * Outline thickness for outline style.
     *
     * Since: 1.0
     */
    properties[PROP_OUTLINE_THICKNESS] =
        g_param_spec_float ("outline-thickness", NULL, NULL,
                            1.0f, 20.0f, 3.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgHighlight:corner-radius:
     *
     * Corner radius for rounded highlights.
     *
     * Since: 1.0
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius", NULL, NULL,
                            0.0f, 50.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgHighlight:animated:
     *
     * Whether the highlight is animated.
     *
     * Since: 1.0
     */
    properties[PROP_ANIMATED] =
        g_param_spec_boolean ("animated", NULL, NULL,
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgHighlight:pulse-speed:
     *
     * Animation pulse speed (cycles per second).
     *
     * Since: 1.0
     */
    properties[PROP_PULSE_SPEED] =
        g_param_spec_float ("pulse-speed", NULL, NULL,
                            0.1f, 10.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_highlight_init (LrgHighlight *self)
{
    self->target = NULL;
    self->style = LRG_HIGHLIGHT_STYLE_OUTLINE;
    self->color = DEFAULT_COLOR;
    self->padding = 4.0f;
    self->outline_thickness = 3.0f;
    self->corner_radius = 0.0f;
    self->animated = TRUE;
    self->pulse_speed = 1.0f;
    self->animation_time = 0.0f;
    self->use_manual_rect = FALSE;
    self->screen_width = 0;
    self->screen_height = 0;
}

/**
 * lrg_highlight_new:
 *
 * Creates a new highlight widget.
 *
 * Returns: (transfer full): A new #LrgHighlight
 *
 * Since: 1.0
 */
LrgHighlight *
lrg_highlight_new (void)
{
    return g_object_new (LRG_TYPE_HIGHLIGHT, NULL);
}

/**
 * lrg_highlight_new_with_style:
 * @style: The highlight style to use
 *
 * Creates a new highlight widget with the specified style.
 *
 * Returns: (transfer full): A new #LrgHighlight
 *
 * Since: 1.0
 */
LrgHighlight *
lrg_highlight_new_with_style (LrgHighlightStyle style)
{
    return g_object_new (LRG_TYPE_HIGHLIGHT,
                         "style", style,
                         NULL);
}

/**
 * lrg_highlight_get_target:
 * @self: An #LrgHighlight
 *
 * Gets the target widget being highlighted.
 *
 * Returns: (transfer none) (nullable): The target widget
 *
 * Since: 1.0
 */
LrgWidget *
lrg_highlight_get_target (LrgHighlight *self)
{
    g_return_val_if_fail (LRG_IS_HIGHLIGHT (self), NULL);
    return self->target;
}

/**
 * lrg_highlight_set_target:
 * @self: An #LrgHighlight
 * @target: (nullable): The widget to highlight
 *
 * Sets the target widget to highlight.
 *
 * Since: 1.0
 */
void
lrg_highlight_set_target (LrgHighlight *self,
                          LrgWidget    *target)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));

    if (g_set_object (&self->target, target))
    {
        self->use_manual_rect = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET]);
    }
}

/**
 * lrg_highlight_set_target_rect:
 * @self: An #LrgHighlight
 * @x: Target X position
 * @y: Target Y position
 * @width: Target width
 * @height: Target height
 *
 * Sets a manual target rectangle.
 *
 * Since: 1.0
 */
void
lrg_highlight_set_target_rect (LrgHighlight *self,
                               gfloat        x,
                               gfloat        y,
                               gfloat        width,
                               gfloat        height)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));

    self->target_x = x;
    self->target_y = y;
    self->target_width = width;
    self->target_height = height;
    self->use_manual_rect = TRUE;

    g_clear_object (&self->target);
}

/**
 * lrg_highlight_get_style:
 * @self: An #LrgHighlight
 *
 * Gets the highlight style.
 *
 * Returns: The highlight style
 *
 * Since: 1.0
 */
LrgHighlightStyle
lrg_highlight_get_style (LrgHighlight *self)
{
    g_return_val_if_fail (LRG_IS_HIGHLIGHT (self), LRG_HIGHLIGHT_STYLE_OUTLINE);
    return self->style;
}

/**
 * lrg_highlight_set_style:
 * @self: An #LrgHighlight
 * @style: The highlight style
 *
 * Sets the highlight style.
 *
 * Since: 1.0
 */
void
lrg_highlight_set_style (LrgHighlight     *self,
                         LrgHighlightStyle style)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));

    if (self->style != style)
    {
        self->style = style;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STYLE]);
    }
}

/**
 * lrg_highlight_get_color:
 * @self: An #LrgHighlight
 *
 * Gets the highlight color.
 *
 * Returns: (transfer none): The color
 *
 * Since: 1.0
 */
const GrlColor *
lrg_highlight_get_color (LrgHighlight *self)
{
    g_return_val_if_fail (LRG_IS_HIGHLIGHT (self), &DEFAULT_COLOR);
    return &self->color;
}

/**
 * lrg_highlight_set_color:
 * @self: An #LrgHighlight
 * @color: The highlight color
 *
 * Sets the highlight color.
 *
 * Since: 1.0
 */
void
lrg_highlight_set_color (LrgHighlight   *self,
                         const GrlColor *color)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));
    g_return_if_fail (color != NULL);

    if (self->color.r != color->r ||
        self->color.g != color->g ||
        self->color.b != color->b ||
        self->color.a != color->a)
    {
        self->color = *color;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
    }
}

/**
 * lrg_highlight_get_padding:
 * @self: An #LrgHighlight
 *
 * Gets the padding around the target.
 *
 * Returns: The padding in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_highlight_get_padding (LrgHighlight *self)
{
    g_return_val_if_fail (LRG_IS_HIGHLIGHT (self), 0.0f);
    return self->padding;
}

/**
 * lrg_highlight_set_padding:
 * @self: An #LrgHighlight
 * @padding: The padding in pixels
 *
 * Sets the padding around the target.
 *
 * Since: 1.0
 */
void
lrg_highlight_set_padding (LrgHighlight *self,
                           gfloat        padding)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));

    if (self->padding != padding)
    {
        self->padding = padding;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING]);
    }
}

/**
 * lrg_highlight_get_outline_thickness:
 * @self: An #LrgHighlight
 *
 * Gets the outline thickness.
 *
 * Returns: The thickness in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_highlight_get_outline_thickness (LrgHighlight *self)
{
    g_return_val_if_fail (LRG_IS_HIGHLIGHT (self), 3.0f);
    return self->outline_thickness;
}

/**
 * lrg_highlight_set_outline_thickness:
 * @self: An #LrgHighlight
 * @thickness: The thickness in pixels
 *
 * Sets the outline thickness.
 *
 * Since: 1.0
 */
void
lrg_highlight_set_outline_thickness (LrgHighlight *self,
                                     gfloat        thickness)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));

    if (self->outline_thickness != thickness)
    {
        self->outline_thickness = thickness;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OUTLINE_THICKNESS]);
    }
}

/**
 * lrg_highlight_get_corner_radius:
 * @self: An #LrgHighlight
 *
 * Gets the corner radius.
 *
 * Returns: The radius in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_highlight_get_corner_radius (LrgHighlight *self)
{
    g_return_val_if_fail (LRG_IS_HIGHLIGHT (self), 0.0f);
    return self->corner_radius;
}

/**
 * lrg_highlight_set_corner_radius:
 * @self: An #LrgHighlight
 * @radius: The radius in pixels
 *
 * Sets the corner radius.
 *
 * Since: 1.0
 */
void
lrg_highlight_set_corner_radius (LrgHighlight *self,
                                 gfloat        radius)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));

    if (self->corner_radius != radius)
    {
        self->corner_radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
    }
}

/**
 * lrg_highlight_get_animated:
 * @self: An #LrgHighlight
 *
 * Gets whether the highlight is animated.
 *
 * Returns: %TRUE if animated
 *
 * Since: 1.0
 */
gboolean
lrg_highlight_get_animated (LrgHighlight *self)
{
    g_return_val_if_fail (LRG_IS_HIGHLIGHT (self), TRUE);
    return self->animated;
}

/**
 * lrg_highlight_set_animated:
 * @self: An #LrgHighlight
 * @animated: Whether to animate
 *
 * Sets whether the highlight should animate.
 *
 * Since: 1.0
 */
void
lrg_highlight_set_animated (LrgHighlight *self,
                            gboolean      animated)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));

    if (self->animated != animated)
    {
        self->animated = animated;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATED]);
    }
}

/**
 * lrg_highlight_get_pulse_speed:
 * @self: An #LrgHighlight
 *
 * Gets the animation pulse speed.
 *
 * Returns: The pulse speed
 *
 * Since: 1.0
 */
gfloat
lrg_highlight_get_pulse_speed (LrgHighlight *self)
{
    g_return_val_if_fail (LRG_IS_HIGHLIGHT (self), 1.0f);
    return self->pulse_speed;
}

/**
 * lrg_highlight_set_pulse_speed:
 * @self: An #LrgHighlight
 * @speed: The pulse speed
 *
 * Sets the animation pulse speed.
 *
 * Since: 1.0
 */
void
lrg_highlight_set_pulse_speed (LrgHighlight *self,
                               gfloat        speed)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));

    if (self->pulse_speed != speed)
    {
        self->pulse_speed = speed;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PULSE_SPEED]);
    }
}

/**
 * lrg_highlight_update:
 * @self: An #LrgHighlight
 * @delta_time: Time since last update in seconds
 *
 * Updates the highlight animation state.
 *
 * Since: 1.0
 */
void
lrg_highlight_update (LrgHighlight *self,
                      gfloat        delta_time)
{
    g_return_if_fail (LRG_IS_HIGHLIGHT (self));

    if (self->animated)
    {
        self->animation_time += delta_time;

        /* Wrap around to avoid float overflow */
        if (self->animation_time > 1000.0f)
        {
            self->animation_time = 0.0f;
        }
    }
}
