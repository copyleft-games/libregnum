/* lrg-slider.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Slider widget for selecting numeric values.
 *
 * Painted as three parts: a pill TRACK, an ACCENT FILL from the minimum
 * to the value, and a round THUMB with a soft shadow that swells on
 * hover and while dragged. Arrow keys step the value when focused.
 */

#include "config.h"
#include "lrg-slider.h"
#include <math.h>

/**
 * SECTION:lrg-slider
 * @title: LrgSlider
 * @short_description: Slider widget for numeric input
 *
 * #LrgSlider is an interactive widget that allows users to select
 * a numeric value within a range by dragging a handle along a track.
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * LrgSlider *slider = lrg_slider_new_with_range (0.0, 100.0, 1.0);
 *
 * g_signal_connect (slider, "value-changed",
 *                   G_CALLBACK (on_value_changed), NULL);
 *
 * lrg_slider_set_value (slider, 50.0);
 * ]|
 */

struct _LrgSlider
{
    LrgWidget       parent_instance;

    gdouble         value;
    gdouble         min;
    gdouble         max;
    gdouble         step;
    LrgOrientation  orientation;

    GrlColor        track_color;
    GrlColor        fill_color;
    GrlColor        handle_color;
    gfloat          handle_size;
    gfloat          track_thickness;

    gboolean        is_dragging;

    /* Which legacy appearance properties the caller set explicitly;
     * unset ones follow the theme */
    gboolean        track_set;
    gboolean        fill_set;
    gboolean        handle_set;
    gboolean        handle_size_set;
    gboolean        thickness_set;
};

G_DEFINE_TYPE (LrgSlider, lrg_slider, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_VALUE,
    PROP_MIN,
    PROP_MAX,
    PROP_STEP,
    PROP_ORIENTATION,
    PROP_TRACK_COLOR,
    PROP_FILL_COLOR,
    PROP_HANDLE_COLOR,
    PROP_HANDLE_SIZE,
    PROP_TRACK_THICKNESS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_VALUE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/* How much bigger the thumb grows under the pointer / while dragged */
#define THUMB_HOVER_GROW   (1.18f)
#define THUMB_PRESS_GROW   (1.30f)

/* Opacity of a disabled slider */
#define SLIDER_DISABLED_OPACITY (0.45f)

/*
 * Clamp value to range and snap to step increment.
 */
static gdouble
snap_value (LrgSlider *self,
            gdouble    value)
{
    gdouble range;
    gdouble steps;
    gdouble snapped;

    /* Clamp to range */
    if (value < self->min)
        value = self->min;
    if (value > self->max)
        value = self->max;

    /* Snap to step if step > 0 */
    if (self->step > 0.0)
    {
        range = value - self->min;
        steps = round (range / self->step);
        snapped = self->min + (steps * self->step);

        /* Ensure we don't exceed max due to rounding */
        if (snapped > self->max)
            snapped = self->max;

        value = snapped;
    }

    return value;
}

/*
 * base_handle_size:
 *
 * Resting thumb diameter: the explicit #LrgSlider:handle-size, else
 * the theme's HANDLE_SIZE metric. Layout (track inset, value mapping)
 * always uses this resting size so the value under the pointer does
 * not shift while the thumb grows.
 */
static gfloat
base_handle_size (LrgSlider *self)
{
    if (self->handle_size_set)
        return self->handle_size;
    return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
                                 LRG_THEME_METRIC_HANDLE_SIZE);
}

/*
 * base_track_thickness:
 *
 * The explicit #LrgSlider:track-thickness, else about a quarter of
 * the thumb (never thinner than 4 units).
 */
static gfloat
base_track_thickness (LrgSlider *self)
{
    if (self->thickness_set)
        return self->track_thickness;
    return MAX (4.0f, roundf (base_handle_size (self) * 0.28f));
}

/*
 * Calculate the handle center position based on current value.
 */
static gfloat
value_to_position (LrgSlider          *self,
                   const GrlRectangle *bounds)
{
    gfloat  track_start;
    gfloat  track_length;
    gfloat  handle = base_handle_size (self);
    gdouble fraction;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        track_start = bounds->x + handle / 2.0f;
        track_length = bounds->width - handle;
    }
    else
    {
        /* Vertical: top is max, bottom is min */
        track_start = bounds->y + bounds->height - handle / 2.0f;
        track_length = -(bounds->height - handle);
    }

    fraction = lrg_slider_get_fraction (self);
    return track_start + (gfloat)(fraction * track_length);
}

/*
 * Calculate value from a position (for dragging).
 */
static gdouble
position_to_value (LrgSlider          *self,
                   gfloat              pos,
                   const GrlRectangle *bounds)
{
    gfloat  track_start;
    gfloat  track_length;
    gfloat  handle = base_handle_size (self);
    gdouble fraction;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        track_start = bounds->x + handle / 2.0f;
        track_length = bounds->width - handle;
    }
    else
    {
        track_start = bounds->y + handle / 2.0f;
        track_length = bounds->height - handle;
    }

    if (track_length <= 0.0f)
        return self->min;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
        fraction = (pos - track_start) / track_length;
    else
        /* Vertical: top is max, bottom is min */
        fraction = 1.0 - ((pos - track_start) / track_length);

    fraction = CLAMP (fraction, 0.0, 1.0);

    return self->min + fraction * (self->max - self->min);
}

/*
 * Helper to get widget bounds as a rectangle.
 */
static GrlRectangle
get_widget_bounds (LrgWidget *widget)
{
    GrlRectangle bounds;

    bounds.x = lrg_widget_get_world_x (widget);
    bounds.y = lrg_widget_get_world_y (widget);
    bounds.width = lrg_widget_get_width (widget);
    bounds.height = lrg_widget_get_height (widget);

    return bounds;
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * lrg_slider_style_defaults:
 *
 * TRACK: the theme's TRACK colour (explicit track-color wins), a pill.
 * FILL: ACCENT, lighter on hover, ACCENT_PRESSED while dragging
 * (explicit fill-color wins), a pill.
 * THUMB: HANDLE colour (explicit handle-color wins) with a soft drop
 * shadow; its @size is the resting diameter, grown on hover and more
 * while pressed so the thumb swells under the finger.
 */
static void
lrg_slider_style_defaults (LrgWidget      *widget,
                           LrgUIPart       part,
                           LrgWidgetState  state,
                           LrgPartLook    *look)
{
    LrgSlider      *self = LRG_SLIDER (widget);
    LrgTheme       *theme = lrg_widget_get_theme (widget);
    gboolean        hovered = (state & LRG_WIDGET_STATE_HOVERED) != 0;
    gboolean        pressed = (state & LRG_WIDGET_STATE_PRESSED) != 0;
    const GrlColor *shadow;
    GrlColor        c;
    gfloat          size;

    switch (part)
    {
    case LRG_UI_PART_TRACK:
        c = self->track_set ? self->track_color
                            : *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);
        lrg_part_look_set_background (look, &c);
        lrg_part_look_set_radius (look, G_MAXFLOAT);
        lrg_part_look_set_size (look, base_track_thickness (self));
        break;

    case LRG_UI_PART_FILL:
        if (self->fill_set)
        {
            if (pressed)
                lrg_paint_color_shade (&self->fill_color, -0.12f, &c);
            else if (hovered)
                lrg_paint_color_shade (&self->fill_color, 0.10f, &c);
            else
                c = self->fill_color;
        }
        else
        {
            c = *lrg_theme_get_color (theme, pressed ? LRG_THEME_COLOR_ACCENT_PRESSED
                                             : hovered ? LRG_THEME_COLOR_ACCENT_HOVER
                                             : LRG_THEME_COLOR_ACCENT);
        }
        lrg_part_look_set_background (look, &c);
        lrg_part_look_set_radius (look, G_MAXFLOAT);
        break;

    case LRG_UI_PART_THUMB:
        c = self->handle_set ? self->handle_color
                             : *lrg_theme_get_color (theme, LRG_THEME_COLOR_HANDLE);
        lrg_part_look_set_background (look, &c);
        lrg_part_look_set_radius (look, G_MAXFLOAT);

        size = base_handle_size (self);
        if (pressed)
            size *= THUMB_PRESS_GROW;
        else if (hovered)
            size *= THUMB_HOVER_GROW;
        lrg_part_look_set_size (look, size);

        /* Soft shadow under the thumb, stronger when lifted by hover */
        shadow = lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW);
        lrg_paint_color_with_alpha (shadow, ((gfloat) shadow->a / 255.0f) * (hovered ? 0.8f : 0.55f),
                                    &c);
        lrg_part_look_set_shadow (look, &c,
                                  MAX (lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) *
                                       (hovered ? 0.45f : 0.3f), 1.0f),
                                  lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y) *
                                  (pressed ? 0.15f : hovered ? 0.45f : 0.3f));

        /* A faint accent halo while hovered / dragged */
        lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT),
                                    pressed ? 0.35f : hovered ? 0.25f : 0.0f, &c);
        lrg_part_look_set_glow (look, &c, size * 0.45f);
        break;

    default:
        LRG_WIDGET_CLASS (lrg_slider_parent_class)->style_defaults (widget, part, state, look);
        return;
    }

    if (state & LRG_WIDGET_STATE_DISABLED)
        lrg_part_look_set_opacity (look, SLIDER_DISABLED_OPACITY);
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

/*
 * lrg_slider_paint:
 *
 * Track (a pill across the widget, inset by half a resting thumb),
 * fill from the minimum end to the thumb, then the round thumb
 * centred on the value position. Vertical sliders fill upwards.
 */
static void
lrg_slider_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
    LrgSlider    *self = LRG_SLIDER (widget);
    GrlRectangle  bounds = get_widget_bounds (widget);
    LrgPartLook   track;
    LrgPartLook   fill;
    LrgPartLook   thumb;
    gfloat        handle = base_handle_size (self);
    gfloat        pos = value_to_position (self, &bounds);
    gfloat        t;
    gfloat        d;
    gfloat        tx;
    gfloat        ty;
    gfloat        tw;
    gfloat        th;
    gfloat        cx;
    gfloat        cy;

    lrg_widget_resolve_look (widget, LRG_UI_PART_TRACK, &track);

    /* The canvas drops PRESSED while a captured pointer is outside the
     * widget; a thumb being dragged keeps its pressed look anyway */
    if (self->is_dragging)
    {
        LrgWidgetState held = lrg_widget_get_state_flags (widget) |
                              LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED;

        lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_FILL, held, &fill);
        lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_THUMB, held, &thumb);
    }
    else
    {
        lrg_widget_resolve_look (widget, LRG_UI_PART_FILL, &fill);
        lrg_widget_resolve_look (widget, LRG_UI_PART_THUMB, &thumb);
    }

    t = track.size > 0.0f ? track.size : base_track_thickness (self);
    d = thumb.size > 0.0f ? thumb.size : handle;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        tx = bounds.x + handle / 2.0f;
        ty = bounds.y + (bounds.height - t) / 2.0f;
        tw = bounds.width - handle;
        th = t;
        cx = pos;
        cy = bounds.y + bounds.height / 2.0f;

        lrg_widget_paint_box (widget, list, &track, tx, ty, tw, th);
        /* Fill reaches the thumb centre; a little under it hides the cap */
        lrg_widget_paint_box (widget, list, &fill, tx, ty, MAX (cx - tx, 0.0f), th);
    }
    else
    {
        tx = bounds.x + (bounds.width - t) / 2.0f;
        ty = bounds.y + handle / 2.0f;
        tw = t;
        th = bounds.height - handle;
        cx = bounds.x + bounds.width / 2.0f;
        cy = pos;

        lrg_widget_paint_box (widget, list, &track, tx, ty, tw, th);
        lrg_widget_paint_box (widget, list, &fill, tx, cy, tw, MAX (ty + th - cy, 0.0f));
    }

    /* Thumb */
    lrg_widget_paint_box (widget, list, &thumb, cx - d / 2.0f, cy - d / 2.0f, d, d);
}

static void
lrg_slider_measure (LrgWidget *widget,
                    gfloat    *preferred_width,
                    gfloat    *preferred_height)
{
    LrgSlider *self = LRG_SLIDER (widget);
    gfloat     cross = ceilf (base_handle_size (self) * THUMB_PRESS_GROW);

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        if (preferred_width != NULL)
            *preferred_width = 200.0f;
        if (preferred_height != NULL)
            *preferred_height = cross;
    }
    else
    {
        if (preferred_width != NULL)
            *preferred_width = cross;
        if (preferred_height != NULL)
            *preferred_height = 200.0f;
    }
}

/*
 * key_step:
 *
 * The value change for a key, or 0 when the key does not move this
 * slider. Without a step, keys move by 1% of the range.
 */
static gdouble
key_step (LrgSlider *self,
          GrlKey     key)
{
    gdouble inc = self->step > 0.0 ? self->step : (self->max - self->min) / 100.0;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        if (key == GRL_KEY_RIGHT)
            return inc;
        if (key == GRL_KEY_LEFT)
            return -inc;
    }
    else
    {
        if (key == GRL_KEY_UP)
            return inc;
        if (key == GRL_KEY_DOWN)
            return -inc;
    }
    return 0.0;
}

/*
 * lrg_slider_handle_event:
 *
 * A left press jumps to the pointer and starts a drag; the canvas
 * keeps routing moves and the release to the slider (pointer capture)
 * even outside its bounds. When focused, the arrow keys along the
 * slider's axis step the value and Home / End jump to the ends; they
 * are consumed so they do not move focus.
 */
static gboolean
lrg_slider_handle_event (LrgWidget        *widget,
                         const LrgUIEvent *event)
{
    LrgSlider      *self = LRG_SLIDER (widget);
    GrlRectangle    bounds;
    LrgUIEventType  type;
    gfloat          pos;
    gdouble         delta;

    bounds = get_widget_bounds (widget);
    type = lrg_ui_event_get_event_type (event);

    switch (type)
    {
    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
        if (lrg_ui_event_get_button (event) == GRL_MOUSE_BUTTON_LEFT)
        {
            self->is_dragging = TRUE;

            /* Jump to click position */
            pos = self->orientation == LRG_ORIENTATION_HORIZONTAL ? lrg_ui_event_get_x (event)
                                                                  : lrg_ui_event_get_y (event);
            lrg_slider_set_value (self, position_to_value (self, pos, &bounds));
            return TRUE;
        }
        break;

    case LRG_UI_EVENT_MOUSE_BUTTON_UP:
        if (lrg_ui_event_get_button (event) == GRL_MOUSE_BUTTON_LEFT && self->is_dragging)
        {
            self->is_dragging = FALSE;
            return TRUE;
        }
        break;

    case LRG_UI_EVENT_MOUSE_MOVE:
        if (self->is_dragging)
        {
            pos = self->orientation == LRG_ORIENTATION_HORIZONTAL ? lrg_ui_event_get_x (event)
                                                                  : lrg_ui_event_get_y (event);
            lrg_slider_set_value (self, position_to_value (self, pos, &bounds));
            return TRUE;
        }
        break;

    case LRG_UI_EVENT_KEY_DOWN:
        switch (lrg_ui_event_get_key (event))
        {
        case GRL_KEY_HOME:
            lrg_slider_set_value (self, self->min);
            return TRUE;
        case GRL_KEY_END:
            lrg_slider_set_value (self, self->max);
            return TRUE;
        default:
            delta = key_step (self, lrg_ui_event_get_key (event));
            if (delta != 0.0)
            {
                lrg_slider_set_value (self, self->value + delta);
                return TRUE;
            }
            break;
        }
        break;

    default:
        break;
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_slider_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    LrgSlider *self = LRG_SLIDER (object);

    switch (prop_id)
    {
    case PROP_VALUE:
        g_value_set_double (value, self->value);
        break;
    case PROP_MIN:
        g_value_set_double (value, self->min);
        break;
    case PROP_MAX:
        g_value_set_double (value, self->max);
        break;
    case PROP_STEP:
        g_value_set_double (value, self->step);
        break;
    case PROP_ORIENTATION:
        g_value_set_enum (value, self->orientation);
        break;
    case PROP_TRACK_COLOR:
        g_value_set_boxed (value, &self->track_color);
        break;
    case PROP_FILL_COLOR:
        g_value_set_boxed (value, &self->fill_color);
        break;
    case PROP_HANDLE_COLOR:
        g_value_set_boxed (value, &self->handle_color);
        break;
    case PROP_HANDLE_SIZE:
        g_value_set_float (value, self->handle_size);
        break;
    case PROP_TRACK_THICKNESS:
        g_value_set_float (value, self->track_thickness);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_slider_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    LrgSlider *self = LRG_SLIDER (object);

    switch (prop_id)
    {
    case PROP_VALUE:
        lrg_slider_set_value (self, g_value_get_double (value));
        break;
    case PROP_MIN:
        lrg_slider_set_min (self, g_value_get_double (value));
        break;
    case PROP_MAX:
        lrg_slider_set_max (self, g_value_get_double (value));
        break;
    case PROP_STEP:
        lrg_slider_set_step (self, g_value_get_double (value));
        break;
    case PROP_ORIENTATION:
        lrg_slider_set_orientation (self, g_value_get_enum (value));
        break;
    case PROP_TRACK_COLOR:
        lrg_slider_set_track_color (self, g_value_get_boxed (value));
        break;
    case PROP_FILL_COLOR:
        lrg_slider_set_fill_color (self, g_value_get_boxed (value));
        break;
    case PROP_HANDLE_COLOR:
        lrg_slider_set_handle_color (self, g_value_get_boxed (value));
        break;
    case PROP_HANDLE_SIZE:
        lrg_slider_set_handle_size (self, g_value_get_float (value));
        break;
    case PROP_TRACK_THICKNESS:
        lrg_slider_set_track_thickness (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_slider_class_init (LrgSliderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->get_property = lrg_slider_get_property;
    object_class->set_property = lrg_slider_set_property;

    widget_class->paint = lrg_slider_paint;
    widget_class->style_defaults = lrg_slider_style_defaults;
    widget_class->measure = lrg_slider_measure;
    widget_class->handle_event = lrg_slider_handle_event;

    /**
     * LrgSlider:value:
     *
     * The current slider value.
     */
    properties[PROP_VALUE] =
        g_param_spec_double ("value",
                             "Value",
                             "The current slider value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSlider:min:
     *
     * The minimum value.
     */
    properties[PROP_MIN] =
        g_param_spec_double ("min",
                             "Minimum",
                             "The minimum value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSlider:max:
     *
     * The maximum value.
     */
    properties[PROP_MAX] =
        g_param_spec_double ("max",
                             "Maximum",
                             "The maximum value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 100.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSlider:step:
     *
     * The step increment.
     */
    properties[PROP_STEP] =
        g_param_spec_double ("step",
                             "Step",
                             "The step increment",
                             0.0, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSlider:orientation:
     *
     * The slider orientation.
     */
    properties[PROP_ORIENTATION] =
        g_param_spec_enum ("orientation",
                           "Orientation",
                           "The slider orientation",
                           LRG_TYPE_ORIENTATION,
                           LRG_ORIENTATION_HORIZONTAL,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSlider:track-color:
     *
     * The track background colour. Until set, the theme's TRACK colour
     * is used; once set it replaces that default.
     */
    properties[PROP_TRACK_COLOR] =
        g_param_spec_boxed ("track-color",
                            "Track Color",
                            "The track background color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSlider:fill-color:
     *
     * The colour of the filled part. Until set, the theme's ACCENT
     * family is used; once set it replaces that default (hover and
     * press shades are derived from it).
     */
    properties[PROP_FILL_COLOR] =
        g_param_spec_boxed ("fill-color",
                            "Fill Color",
                            "The filled portion color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSlider:handle-color:
     *
     * The thumb colour. Until set, the theme's HANDLE colour is used.
     */
    properties[PROP_HANDLE_COLOR] =
        g_param_spec_boxed ("handle-color",
                            "Handle Color",
                            "The handle color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSlider:handle-size:
     *
     * The resting thumb diameter. The default (20) is only reported:
     * the slider follows the theme's HANDLE_SIZE until a size is set.
     * The thumb grows a little on hover and while dragged.
     */
    properties[PROP_HANDLE_SIZE] =
        g_param_spec_float ("handle-size",
                            "Handle Size",
                            "The handle diameter in pixels",
                            1.0f, 100.0f, 20.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSlider:track-thickness:
     *
     * The track thickness. The default (6) is only reported: until set,
     * the track is about a quarter of the thumb diameter.
     */
    properties[PROP_TRACK_THICKNESS] =
        g_param_spec_float ("track-thickness",
                            "Track Thickness",
                            "The track thickness in pixels",
                            1.0f, 50.0f, 6.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgSlider::value-changed:
     * @self: the slider that emitted the signal
     *
     * Emitted when the slider value changes.
     */
    signals[SIGNAL_VALUE_CHANGED] =
        g_signal_new ("value-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_slider_init (LrgSlider *self)
{
    lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
    self->value = 0.0;
    self->min = 0.0;
    self->max = 100.0;
    self->step = 1.0;
    self->orientation = LRG_ORIENTATION_HORIZONTAL;

    /* Default track color: dark gray */
    self->track_color.r = 80;
    self->track_color.g = 80;
    self->track_color.b = 80;
    self->track_color.a = 255;

    /* Default fill color: blue */
    self->fill_color.r = 66;
    self->fill_color.g = 135;
    self->fill_color.b = 245;
    self->fill_color.a = 255;

    /* Default handle color: white */
    self->handle_color.r = 255;
    self->handle_color.g = 255;
    self->handle_color.b = 255;
    self->handle_color.a = 255;

    self->handle_size = 20.0f;
    self->track_thickness = 6.0f;
    self->is_dragging = FALSE;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/*
 * lrg_slider_new:
 *
 * Creates a new slider widget with default range (0 to 100).
 *
 * Returns: (transfer full): A new #LrgSlider
 */
LrgSlider *
lrg_slider_new (void)
{
    return g_object_new (LRG_TYPE_SLIDER, NULL);
}

/*
 * lrg_slider_new_with_range:
 * @min: minimum value
 * @max: maximum value
 * @step: step increment
 *
 * Creates a new slider widget with the specified range.
 *
 * Returns: (transfer full): A new #LrgSlider
 */
LrgSlider *
lrg_slider_new_with_range (gdouble min,
                           gdouble max,
                           gdouble step)
{
    return g_object_new (LRG_TYPE_SLIDER,
                         "min", min,
                         "max", max,
                         "step", step,
                         NULL);
}

/* ==========================================================================
 * Public API - Value
 * ========================================================================== */

/*
 * lrg_slider_get_value:
 * @self: an #LrgSlider
 *
 * Gets the current slider value.
 *
 * Returns: The current value
 */
gdouble
lrg_slider_get_value (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), 0.0);

    return self->value;
}

/*
 * lrg_slider_set_value:
 * @self: an #LrgSlider
 * @value: the new value
 *
 * Sets the current slider value.
 */
void
lrg_slider_set_value (LrgSlider *self,
                      gdouble    value)
{
    g_return_if_fail (LRG_IS_SLIDER (self));

    value = snap_value (self, value);

    if (self->value == value)
        return;

    self->value = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE]);
    g_signal_emit (self, signals[SIGNAL_VALUE_CHANGED], 0);
}

/* ==========================================================================
 * Public API - Range
 * ========================================================================== */

/*
 * lrg_slider_get_min:
 * @self: an #LrgSlider
 *
 * Gets the minimum value.
 *
 * Returns: The minimum value
 */
gdouble
lrg_slider_get_min (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), 0.0);

    return self->min;
}

/*
 * lrg_slider_set_min:
 * @self: an #LrgSlider
 * @min: the minimum value
 *
 * Sets the minimum value.
 */
void
lrg_slider_set_min (LrgSlider *self,
                    gdouble    min)
{
    g_return_if_fail (LRG_IS_SLIDER (self));

    if (self->min == min)
        return;

    self->min = min;

    /* Re-clamp current value */
    if (self->value < self->min)
        lrg_slider_set_value (self, self->min);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN]);
}

/*
 * lrg_slider_get_max:
 * @self: an #LrgSlider
 *
 * Gets the maximum value.
 *
 * Returns: The maximum value
 */
gdouble
lrg_slider_get_max (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), 100.0);

    return self->max;
}

/*
 * lrg_slider_set_max:
 * @self: an #LrgSlider
 * @max: the maximum value
 *
 * Sets the maximum value.
 */
void
lrg_slider_set_max (LrgSlider *self,
                    gdouble    max)
{
    g_return_if_fail (LRG_IS_SLIDER (self));

    if (self->max == max)
        return;

    self->max = max;

    /* Re-clamp current value */
    if (self->value > self->max)
        lrg_slider_set_value (self, self->max);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX]);
}

/*
 * lrg_slider_get_step:
 * @self: an #LrgSlider
 *
 * Gets the step increment.
 *
 * Returns: The step increment
 */
gdouble
lrg_slider_get_step (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), 1.0);

    return self->step;
}

/*
 * lrg_slider_set_step:
 * @self: an #LrgSlider
 * @step: the step increment
 *
 * Sets the step increment.
 */
void
lrg_slider_set_step (LrgSlider *self,
                     gdouble    step)
{
    g_return_if_fail (LRG_IS_SLIDER (self));
    g_return_if_fail (step >= 0.0);

    if (self->step == step)
        return;

    self->step = step;

    /* Re-snap current value */
    lrg_slider_set_value (self, self->value);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STEP]);
}

/*
 * lrg_slider_set_range:
 * @self: an #LrgSlider
 * @min: the minimum value
 * @max: the maximum value
 *
 * Sets the value range.
 */
void
lrg_slider_set_range (LrgSlider *self,
                      gdouble    min,
                      gdouble    max)
{
    g_return_if_fail (LRG_IS_SLIDER (self));
    g_return_if_fail (min <= max);

    g_object_freeze_notify (G_OBJECT (self));
    lrg_slider_set_min (self, min);
    lrg_slider_set_max (self, max);
    g_object_thaw_notify (G_OBJECT (self));
}

/*
 * lrg_slider_get_fraction:
 * @self: an #LrgSlider
 *
 * Gets the current value as a fraction of the range.
 *
 * Returns: The fraction (0.0 to 1.0)
 */
gdouble
lrg_slider_get_fraction (LrgSlider *self)
{
    gdouble range;

    g_return_val_if_fail (LRG_IS_SLIDER (self), 0.0);

    range = self->max - self->min;
    if (range <= 0.0)
        return 0.0;

    return (self->value - self->min) / range;
}

/* ==========================================================================
 * Public API - Orientation
 * ========================================================================== */

/*
 * lrg_slider_get_orientation:
 * @self: an #LrgSlider
 *
 * Gets the slider orientation.
 *
 * Returns: The orientation
 */
LrgOrientation
lrg_slider_get_orientation (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), LRG_ORIENTATION_HORIZONTAL);

    return self->orientation;
}

/*
 * lrg_slider_set_orientation:
 * @self: an #LrgSlider
 * @orientation: the orientation
 *
 * Sets the slider orientation.
 */
void
lrg_slider_set_orientation (LrgSlider      *self,
                            LrgOrientation  orientation)
{
    g_return_if_fail (LRG_IS_SLIDER (self));

    if (self->orientation == orientation)
        return;

    self->orientation = orientation;
    lrg_widget_queue_layout (LRG_WIDGET (self));
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIENTATION]);
}

/* ==========================================================================
 * Public API - Appearance
 * ========================================================================== */

/*
 * lrg_slider_get_track_color:
 * @self: an #LrgSlider
 *
 * Gets the track background color.
 *
 * Returns: (transfer none): The track color
 */
const GrlColor *
lrg_slider_get_track_color (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), NULL);

    return &self->track_color;
}

/*
 * lrg_slider_set_track_color:
 * @self: an #LrgSlider
 * @color: the track color
 *
 * Sets the track background color.
 */
void
lrg_slider_set_track_color (LrgSlider      *self,
                            const GrlColor *color)
{
    g_return_if_fail (LRG_IS_SLIDER (self));
    g_return_if_fail (color != NULL);

    self->track_set = TRUE;
    if (self->track_color.r == color->r &&
        self->track_color.g == color->g &&
        self->track_color.b == color->b &&
        self->track_color.a == color->a)
        return;

    self->track_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRACK_COLOR]);
}

/*
 * lrg_slider_get_fill_color:
 * @self: an #LrgSlider
 *
 * Gets the filled portion color.
 *
 * Returns: (transfer none): The fill color
 */
const GrlColor *
lrg_slider_get_fill_color (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), NULL);

    return &self->fill_color;
}

/*
 * lrg_slider_set_fill_color:
 * @self: an #LrgSlider
 * @color: the fill color
 *
 * Sets the filled portion color.
 */
void
lrg_slider_set_fill_color (LrgSlider      *self,
                           const GrlColor *color)
{
    g_return_if_fail (LRG_IS_SLIDER (self));
    g_return_if_fail (color != NULL);

    self->fill_set = TRUE;
    if (self->fill_color.r == color->r &&
        self->fill_color.g == color->g &&
        self->fill_color.b == color->b &&
        self->fill_color.a == color->a)
        return;

    self->fill_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_COLOR]);
}

/*
 * lrg_slider_get_handle_color:
 * @self: an #LrgSlider
 *
 * Gets the handle color.
 *
 * Returns: (transfer none): The handle color
 */
const GrlColor *
lrg_slider_get_handle_color (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), NULL);

    return &self->handle_color;
}

/*
 * lrg_slider_set_handle_color:
 * @self: an #LrgSlider
 * @color: the handle color
 *
 * Sets the handle color.
 */
void
lrg_slider_set_handle_color (LrgSlider      *self,
                             const GrlColor *color)
{
    g_return_if_fail (LRG_IS_SLIDER (self));
    g_return_if_fail (color != NULL);

    self->handle_set = TRUE;
    if (self->handle_color.r == color->r &&
        self->handle_color.g == color->g &&
        self->handle_color.b == color->b &&
        self->handle_color.a == color->a)
        return;

    self->handle_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HANDLE_COLOR]);
}

/*
 * lrg_slider_get_handle_size:
 * @self: an #LrgSlider
 *
 * Gets the handle diameter.
 *
 * Returns: The handle size in pixels
 */
gfloat
lrg_slider_get_handle_size (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), 20.0f);

    return self->handle_size;
}

/*
 * lrg_slider_set_handle_size:
 * @self: an #LrgSlider
 * @size: the handle diameter in pixels
 *
 * Sets the handle diameter.
 */
void
lrg_slider_set_handle_size (LrgSlider *self,
                            gfloat     size)
{
    g_return_if_fail (LRG_IS_SLIDER (self));
    g_return_if_fail (size >= 1.0f);

    self->handle_size_set = TRUE;
    lrg_widget_queue_layout (LRG_WIDGET (self));
    if (self->handle_size == size)
        return;

    self->handle_size = size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HANDLE_SIZE]);
}

/*
 * lrg_slider_get_track_thickness:
 * @self: an #LrgSlider
 *
 * Gets the track thickness.
 *
 * Returns: The track thickness in pixels
 */
gfloat
lrg_slider_get_track_thickness (LrgSlider *self)
{
    g_return_val_if_fail (LRG_IS_SLIDER (self), 6.0f);

    return self->track_thickness;
}

/*
 * lrg_slider_set_track_thickness:
 * @self: an #LrgSlider
 * @thickness: the track thickness in pixels
 *
 * Sets the track thickness.
 */
void
lrg_slider_set_track_thickness (LrgSlider *self,
                                gfloat     thickness)
{
    g_return_if_fail (LRG_IS_SLIDER (self));
    g_return_if_fail (thickness >= 1.0f);

    self->thickness_set = TRUE;
    if (self->track_thickness == thickness)
        return;

    self->track_thickness = thickness;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRACK_THICKNESS]);
}
