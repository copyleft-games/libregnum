/* lrg-progress-bar.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Progress bar widget for displaying completion status.
 *
 * The bar records itself through paint(): a TRACK part (a pill in the
 * theme's TRACK colour) and a FILL part (a gradient from ACCENT to
 * ACCENT_HOVER). The drawn fraction eases towards the value on a
 * spring, so jumps in progress glide instead of snapping. An
 * indeterminate bar shows a shimmering segment sliding across the
 * track. The optional percentage text is the LABEL part. Legacy colour
 * and radius properties still work: once set explicitly they are
 * applied in style_overrides(), after the theme's style rules, so they
 * beat both the default and any matching rule.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-progress-bar.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"
#include <stdio.h>
#include <math.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

/* Seconds for one indeterminate sweep across the track */
#define SWEEP_PERIOD (1.6f)

/* Indeterminate segment length as a fraction of the track */
#define SWEEP_SEGMENT (0.35f)

struct _LrgProgressBar
{
    LrgWidget       parent_instance;

    gdouble         value;
    gdouble         max;
    gboolean        show_text;
    gboolean        indeterminate;
    LrgOrientation  orientation;

    /* Legacy appearance and whether each was set explicitly */
    GrlColor        background_color;
    GrlColor        fill_color;
    GrlColor        text_color;
    gfloat          corner_radius;
    gboolean        background_set;
    gboolean        fill_set;
    gboolean        text_set;
    gboolean        radius_set;

    /* Drawn fraction, eased towards the real one in step() */
    LrgSpring       shown;
    /* Indeterminate sweep position, 0..1 per period */
    gfloat          sweep;
    /* FALSE until the first step(): the bar shows its value directly */
    gboolean        stepped;
};

G_DEFINE_TYPE (LrgProgressBar, lrg_progress_bar, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_VALUE,
    PROP_MAX,
    PROP_SHOW_TEXT,
    PROP_ORIENTATION,
    PROP_BACKGROUND_COLOR,
    PROP_FILL_COLOR,
    PROP_TEXT_COLOR,
    PROP_CORNER_RADIUS,
    PROP_INDETERMINATE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Legacy default colours, reported by the getters until set */
static const GrlColor DEFAULT_BACKGROUND = { 60, 60, 60, 255 };
static const GrlColor DEFAULT_FILL       = { 50, 150, 50, 255 };
static const GrlColor DEFAULT_TEXT       = { 255, 255, 255, 255 };

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/*
 * lrg_progress_bar_step:
 *
 * Steps the bar's own animation (eased fraction and indeterminate
 * sweep) by the canvas frame time. The very first step snaps the fill
 * to the value so a new bar does not animate in.
 *
 * Returns: %TRUE while the fill is easing, and for as long as an
 *          indeterminate bar sweeps (unless motion is reduced)
 */
static gboolean
lrg_progress_bar_step (LrgWidget *widget,
                       gfloat     delta_time)
{
    LrgProgressBar *self = LRG_PROGRESS_BAR (widget);
    gboolean        busy;

    lrg_spring_set_motion (&self->shown, lrg_widget_get_motion (widget));
    lrg_spring_set_target (&self->shown, (gfloat) lrg_progress_bar_get_fraction (self));

    if (!self->stepped)
    {
        lrg_spring_snap (&self->shown, (gfloat) lrg_progress_bar_get_fraction (self));
        self->stepped = TRUE;
        return self->indeterminate && !lrg_motion_get_reduced ();
    }

    busy = lrg_spring_step (&self->shown, delta_time);

    /* The sweep is a continuous animation: it keeps the canvas busy for
     * as long as the bar is indeterminate, and stops entirely when
     * motion is reduced */
    if (self->indeterminate && !lrg_motion_get_reduced ())
    {
        self->sweep += MAX (delta_time, 0.0f) * lrg_motion_get_speed () / SWEEP_PERIOD;
        self->sweep -= floorf (self->sweep);
        busy = TRUE;
    }

    return busy;
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * lrg_progress_bar_style_defaults:
 *
 * TRACK: TRACK colour, pill radius. FILL: linear gradient ACCENT ->
 * ACCENT_HOVER along the bar, with a faint accent glow. LABEL: TEXT.
 * Explicit values are applied later, in style_overrides().
 */
static void
lrg_progress_bar_style_defaults (LrgWidget      *widget,
                                 LrgUIPart       part,
                                 LrgWidgetState  state,
                                 LrgPartLook    *look)
{
    LrgProgressBar   *self = LRG_PROGRESS_BAR (widget);
    LrgTheme         *theme = lrg_widget_get_theme (widget);
    LrgPaintGradient  gradient;
    GrlColor          a;
    GrlColor          b;
    gfloat            angle;

    switch (part)
    {
    case LRG_UI_PART_TRACK:
        lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK));
        lrg_part_look_set_radius (look, G_MAXFLOAT);
        break;

    case LRG_UI_PART_FILL:
        a = *lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
        b = *lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT_HOVER);

        /* Horizontal bars brighten to the right, vertical ones upwards */
        angle = self->orientation == LRG_ORIENTATION_HORIZONTAL ? 0.0f : 270.0f;
        lrg_paint_gradient_init_linear (&gradient, &a, &b, angle);
        lrg_part_look_set_background (look, &a);
        lrg_part_look_set_gradient (look, &gradient);
        lrg_part_look_set_radius (look, G_MAXFLOAT);

        lrg_paint_color_with_alpha (&b, 0.30f, &b);
        lrg_part_look_set_glow (look, &b,
                                lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 0.4f);
        break;

    case LRG_UI_PART_LABEL:
        lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
        break;

    default:
        LRG_WIDGET_CLASS (lrg_progress_bar_parent_class)->style_defaults (widget, part, state, look);
        return;
    }

    if (state & LRG_WIDGET_STATE_DISABLED)
        lrg_part_look_set_opacity (look, 0.45f);
}

/*
 * lrg_progress_bar_style_overrides:
 *
 * Explicit values, applied after the theme's style rules so they
 * always win. TRACK: background-color and corner-radius. FILL:
 * fill-color gives a gradient from it to a lighter shade (and the glow
 * follows that shade); corner-radius. LABEL: text-color.
 */
static void
lrg_progress_bar_style_overrides (LrgWidget      *widget,
                                  LrgUIPart       part,
                                  LrgWidgetState  state,
                                  LrgPartLook    *look)
{
    LrgProgressBar   *self = LRG_PROGRESS_BAR (widget);
    LrgWidgetClass   *parent = LRG_WIDGET_CLASS (lrg_progress_bar_parent_class);
    LrgPaintGradient  gradient;
    GrlColor          b;
    gfloat            angle;

    if (parent->style_overrides != NULL)
        parent->style_overrides (widget, part, state, look);

    switch (part)
    {
    case LRG_UI_PART_TRACK:
        if (self->background_set)
            lrg_part_look_set_background (look, &self->background_color);
        if (self->radius_set)
            lrg_part_look_set_radius (look, self->corner_radius);
        break;

    case LRG_UI_PART_FILL:
        if (self->fill_set)
        {
            lrg_paint_color_shade (&self->fill_color, 0.18f, &b);
            angle = self->orientation == LRG_ORIENTATION_HORIZONTAL ? 0.0f : 270.0f;
            lrg_paint_gradient_init_linear (&gradient, &self->fill_color, &b, angle);
            lrg_part_look_set_background (look, &self->fill_color);
            lrg_part_look_set_gradient (look, &gradient);
            lrg_paint_color_with_alpha (&b, 0.30f, &b);
            lrg_part_look_set_glow (look, &b, look->glow_size);
        }
        if (self->radius_set)
            lrg_part_look_set_radius (look, self->corner_radius);
        break;

    case LRG_UI_PART_LABEL:
        if (self->text_set)
            lrg_part_look_set_foreground (look, &self->text_color);
        break;

    default:
        break;
    }
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

/*
 * paint_indeterminate:
 *
 * A segment of the FILL look sliding along the track, clipped to it.
 * Its position follows a smoothstep so it accelerates in and slows
 * out, and a soft highlight runs along with it for the shimmer.
 */
static void
paint_indeterminate (LrgProgressBar    *self,
                     LrgPaintList      *list,
                     const LrgPartLook *fill,
                     gfloat             x,
                     gfloat             y,
                     gfloat             w,
                     gfloat             h)
{
    LrgWidget *widget = LRG_WIDGET (self);
    gboolean   horizontal = self->orientation == LRG_ORIENTATION_HORIZONTAL;
    gfloat     length = horizontal ? w : h;
    gfloat     seg = length * SWEEP_SEGMENT;
    gfloat     t = self->sweep;
    gfloat     eased = t * t * (3.0f - 2.0f * t);
    gfloat     start = -seg + eased * (length + seg);
    GrlColor   shine;

    lrg_paint_list_push_clip (list, x, y, w, h);

    if (horizontal)
        lrg_widget_paint_box (widget, list, fill, x + start, y, seg, h);
    else
        lrg_widget_paint_box (widget, list, fill, x, y + length - start - seg, w, seg);

    /* Highlight across the middle of the segment */
    shine = (GrlColor){ 255, 255, 255, 70 };
    if (horizontal)
        lrg_paint_list_rect (list, x + start + seg * 0.3f, y + h * 0.15f, seg * 0.4f, h * 0.35f,
                             G_MAXFLOAT, &shine);
    else
        lrg_paint_list_rect (list, x + w * 0.15f, y + length - start - seg * 0.7f,
                             w * 0.35f, seg * 0.4f, G_MAXFLOAT, &shine);

    lrg_paint_list_pop_clip (list);
}

static void
lrg_progress_bar_paint (LrgWidget    *widget,
                        LrgPaintList *list)
{
    LrgProgressBar *self = LRG_PROGRESS_BAR (widget);
    LrgTheme       *theme = lrg_widget_get_theme (widget);
    LrgPartLook     track;
    LrgPartLook     fill;
    gfloat          x = lrg_widget_get_world_x (widget);
    gfloat          y = lrg_widget_get_world_y (widget);
    gfloat          w = lrg_widget_get_width (widget);
    gfloat          h = lrg_widget_get_height (widget);
    gfloat          f;

    lrg_widget_resolve_look (widget, LRG_UI_PART_TRACK, &track);
    lrg_widget_resolve_look (widget, LRG_UI_PART_FILL, &fill);

    /* 1. Track */
    lrg_widget_paint_box (widget, list, &track, x, y, w, h);

    /* 2. Fill: eased fraction, or the indeterminate sweep */
    if (self->indeterminate)
    {
        paint_indeterminate (self, list, &fill, x, y, w, h);
    }
    else
    {
        f = CLAMP ((gfloat) lrg_progress_bar_get_display_fraction (self), 0.0f, 1.0f);
        if (f > 0.0f)
        {
            if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
                lrg_widget_paint_box (widget, list, &fill, x, y, w * f, h);
            else
                lrg_widget_paint_box (widget, list, &fill, x, y + h * (1.0f - f), w, h * f);
        }
    }

    /* 3. Percentage text, centred */
    if (self->show_text && !self->indeterminate)
    {
        LrgPartLook  label;
        GrlFont     *font = lrg_theme_get_ui_font (theme);
        gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
        gchar        text[16];
        gfloat       tw;
        gfloat       th;

        lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
        g_snprintf (text, sizeof (text), "%d%%",
                    (gint) (lrg_progress_bar_get_fraction (self) * 100.0 + 0.5));
        lrg_paint_measure_text (font, text, size, 1.0f, &tw, &th);
        lrg_paint_color_with_alpha (&label.foreground,
                                    ((gfloat) label.foreground.a / 255.0f) * label.opacity,
                                    &label.foreground);
        lrg_paint_list_text (list, font, text, x + (w - tw) * 0.5f, y + (h - th) * 0.5f,
                             size, 1.0f, &label.foreground);
    }
}

/*
 * lrg_progress_bar_measure:
 *
 * 200 units along the bar; across it a slim 10 units, or room for the
 * small font when the percentage is shown.
 */
static void
lrg_progress_bar_measure (LrgWidget *widget,
                          gfloat    *preferred_width,
                          gfloat    *preferred_height)
{
    LrgProgressBar *self = LRG_PROGRESS_BAR (widget);
    gfloat          thick = 10.0f;

    if (self->show_text)
        thick = lrg_theme_get_metric (lrg_widget_get_theme (widget),
                                      LRG_THEME_METRIC_FONT_SIZE_SMALL) + 8.0f;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        if (preferred_width != NULL)
            *preferred_width = 200.0f;
        if (preferred_height != NULL)
            *preferred_height = thick;
    }
    else
    {
        if (preferred_width != NULL)
            *preferred_width = thick;
        if (preferred_height != NULL)
            *preferred_height = 200.0f;
    }
}

static gboolean
lrg_progress_bar_handle_event (LrgWidget        *widget,
                               const LrgUIEvent *event)
{
    /* Progress bar is display-only, no input handling */
    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_progress_bar_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgProgressBar *self = LRG_PROGRESS_BAR (object);

    switch (prop_id)
    {
    case PROP_VALUE:
        g_value_set_double (value, self->value);
        break;
    case PROP_MAX:
        g_value_set_double (value, self->max);
        break;
    case PROP_SHOW_TEXT:
        g_value_set_boolean (value, self->show_text);
        break;
    case PROP_ORIENTATION:
        g_value_set_enum (value, self->orientation);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, &self->background_color);
        break;
    case PROP_FILL_COLOR:
        g_value_set_boxed (value, &self->fill_color);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, &self->text_color);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_float (value, self->corner_radius);
        break;
    case PROP_INDETERMINATE:
        g_value_set_boolean (value, self->indeterminate);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_progress_bar_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgProgressBar *self = LRG_PROGRESS_BAR (object);

    switch (prop_id)
    {
    case PROP_VALUE:
        lrg_progress_bar_set_value (self, g_value_get_double (value));
        break;
    case PROP_MAX:
        lrg_progress_bar_set_max (self, g_value_get_double (value));
        break;
    case PROP_SHOW_TEXT:
        lrg_progress_bar_set_show_text (self, g_value_get_boolean (value));
        break;
    case PROP_ORIENTATION:
        lrg_progress_bar_set_orientation (self, g_value_get_enum (value));
        break;
    case PROP_BACKGROUND_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_progress_bar_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_FILL_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_progress_bar_set_fill_color (self, g_value_get_boxed (value));
        break;
    case PROP_TEXT_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_progress_bar_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_progress_bar_set_corner_radius (self, g_value_get_float (value));
        break;
    case PROP_INDETERMINATE:
        lrg_progress_bar_set_indeterminate (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_progress_bar_class_init (LrgProgressBarClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->get_property = lrg_progress_bar_get_property;
    object_class->set_property = lrg_progress_bar_set_property;

    widget_class->paint = lrg_progress_bar_paint;
    widget_class->step = lrg_progress_bar_step;
    widget_class->style_defaults = lrg_progress_bar_style_defaults;
    widget_class->style_overrides = lrg_progress_bar_style_overrides;
    widget_class->measure = lrg_progress_bar_measure;
    widget_class->handle_event = lrg_progress_bar_handle_event;

    /**
     * LrgProgressBar:value:
     *
     * The current progress value, clamped to 0..#LrgProgressBar:max.
     * The drawn fill eases towards it.
     */
    properties[PROP_VALUE] =
        g_param_spec_double ("value",
                             "Value",
                             "The current progress value",
                             0.0, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProgressBar:max:
     *
     * The value that fills the bar completely.
     */
    properties[PROP_MAX] =
        g_param_spec_double ("max",
                             "Max",
                             "The maximum value",
                             0.001, G_MAXDOUBLE, 100.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProgressBar:show-text:
     *
     * Whether to draw the percentage centred on the bar.
     */
    properties[PROP_SHOW_TEXT] =
        g_param_spec_boolean ("show-text",
                              "Show Text",
                              "Whether to show percentage text",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProgressBar:orientation:
     *
     * Horizontal bars fill left to right, vertical ones bottom to top.
     */
    properties[PROP_ORIENTATION] =
        g_param_spec_enum ("orientation",
                           "Orientation",
                           "The bar orientation",
                           LRG_TYPE_ORIENTATION,
                           LRG_ORIENTATION_HORIZONTAL,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProgressBar:background-color:
     *
     * Track colour. Until set, the theme's TRACK colour is used; once
     * set it replaces that default.
     */
    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "The track color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProgressBar:fill-color:
     *
     * Fill colour. Until set, the fill is a gradient from the theme's
     * ACCENT to ACCENT_HOVER; once set the gradient runs from this
     * colour to a lighter shade of it.
     */
    properties[PROP_FILL_COLOR] =
        g_param_spec_boxed ("fill-color",
                            "Fill Color",
                            "The fill color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProgressBar:text-color:
     *
     * Percentage text colour. Until set, the theme's TEXT colour.
     */
    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color",
                            "Text Color",
                            "The percentage text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProgressBar:corner-radius:
     *
     * Corner radius. The default (4) is only reported: until a radius
     * is set the bar is drawn as a pill.
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "Rounded corner radius",
                            0.0f, G_MAXFLOAT, 4.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProgressBar:indeterminate:
     *
     * When %TRUE the bar ignores its value and shows a shimmering
     * segment sliding across the track, for work of unknown length.
     */
    properties[PROP_INDETERMINATE] =
        g_param_spec_boolean ("indeterminate",
                              "Indeterminate",
                              "Whether progress is unknown",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_progress_bar_init (LrgProgressBar *self)
{
    self->value = 0.0;
    self->max = 100.0;
    self->show_text = FALSE;
    self->orientation = LRG_ORIENTATION_HORIZONTAL;
    self->background_color = DEFAULT_BACKGROUND;
    self->fill_color = DEFAULT_FILL;
    self->text_color = DEFAULT_TEXT;
    self->corner_radius = 4.0f;
    lrg_spring_init (&self->shown, 0.0f, LRG_MOTION_SMOOTH);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgProgressBar *
lrg_progress_bar_new (void)
{
    return g_object_new (LRG_TYPE_PROGRESS_BAR, NULL);
}

gdouble
lrg_progress_bar_get_value (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), 0.0);
    return self->value;
}

void
lrg_progress_bar_set_value (LrgProgressBar *self,
                            gdouble         value)
{
    g_return_if_fail (LRG_IS_PROGRESS_BAR (self));

    value = CLAMP (value, 0.0, self->max);

    if (self->value != value)
    {
        self->value = value;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE]);
    }
}

gdouble
lrg_progress_bar_get_max (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), 100.0);
    return self->max;
}

void
lrg_progress_bar_set_max (LrgProgressBar *self,
                          gdouble         max)
{
    g_return_if_fail (LRG_IS_PROGRESS_BAR (self));
    g_return_if_fail (max > 0.0);

    if (self->max != max)
    {
        self->max = max;
        /* Clamp value if needed */
        if (self->value > max)
        {
            self->value = max;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE]);
        }
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX]);
    }
}

gdouble
lrg_progress_bar_get_fraction (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), 0.0);

    if (self->max <= 0.0)
        return 0.0;
    return CLAMP (self->value / self->max, 0.0, 1.0);
}

gdouble
lrg_progress_bar_get_display_fraction (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), 0.0);

    /* Before the first step the bar shows the value itself */
    if (!self->stepped)
        return lrg_progress_bar_get_fraction (self);
    return (gdouble) self->shown.value;
}

gboolean
lrg_progress_bar_get_show_text (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), FALSE);
    return self->show_text;
}

void
lrg_progress_bar_set_show_text (LrgProgressBar *self,
                                gboolean        show)
{
    g_return_if_fail (LRG_IS_PROGRESS_BAR (self));

    show = !!show;

    if (self->show_text != show)
    {
        self->show_text = show;
        lrg_widget_queue_layout (LRG_WIDGET (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_TEXT]);
    }
}

gboolean
lrg_progress_bar_get_indeterminate (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), FALSE);
    return self->indeterminate;
}

void
lrg_progress_bar_set_indeterminate (LrgProgressBar *self,
                                    gboolean        indeterminate)
{
    g_return_if_fail (LRG_IS_PROGRESS_BAR (self));

    indeterminate = !!indeterminate;
    if (self->indeterminate != indeterminate)
    {
        self->indeterminate = indeterminate;
        self->sweep = 0.0f;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INDETERMINATE]);
    }
}

LrgOrientation
lrg_progress_bar_get_orientation (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), LRG_ORIENTATION_HORIZONTAL);
    return self->orientation;
}

void
lrg_progress_bar_set_orientation (LrgProgressBar *self,
                                  LrgOrientation  orientation)
{
    g_return_if_fail (LRG_IS_PROGRESS_BAR (self));

    if (self->orientation != orientation)
    {
        self->orientation = orientation;
        lrg_widget_queue_layout (LRG_WIDGET (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIENTATION]);
    }
}

const GrlColor *
lrg_progress_bar_get_background_color (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), &DEFAULT_BACKGROUND);
    return &self->background_color;
}

void
lrg_progress_bar_set_background_color (LrgProgressBar *self,
                                       const GrlColor *color)
{
    g_return_if_fail (LRG_IS_PROGRESS_BAR (self));
    g_return_if_fail (color != NULL);

    self->background_color = *color;
    self->background_set = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

const GrlColor *
lrg_progress_bar_get_fill_color (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), &DEFAULT_FILL);
    return &self->fill_color;
}

void
lrg_progress_bar_set_fill_color (LrgProgressBar *self,
                                 const GrlColor *color)
{
    g_return_if_fail (LRG_IS_PROGRESS_BAR (self));
    g_return_if_fail (color != NULL);

    self->fill_color = *color;
    self->fill_set = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_COLOR]);
}

const GrlColor *
lrg_progress_bar_get_text_color (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), &DEFAULT_TEXT);
    return &self->text_color;
}

void
lrg_progress_bar_set_text_color (LrgProgressBar *self,
                                 const GrlColor *color)
{
    g_return_if_fail (LRG_IS_PROGRESS_BAR (self));
    g_return_if_fail (color != NULL);

    self->text_color = *color;
    self->text_set = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

gfloat
lrg_progress_bar_get_corner_radius (LrgProgressBar *self)
{
    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), 4.0f);
    return self->corner_radius;
}

void
lrg_progress_bar_set_corner_radius (LrgProgressBar *self,
                                    gfloat          radius)
{
    g_return_if_fail (LRG_IS_PROGRESS_BAR (self));
    g_return_if_fail (radius >= 0.0f);

    self->radius_set = TRUE;
    if (self->corner_radius != radius)
    {
        self->corner_radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
    }
}
