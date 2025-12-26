/* lrg-slider.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Slider widget for selecting numeric values.
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
 * Calculate the handle center position based on current value.
 */
static gfloat
value_to_position (LrgSlider          *self,
                   const GrlRectangle *bounds)
{
    gfloat track_start;
    gfloat track_length;
    gdouble fraction;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        track_start = bounds->x + self->handle_size / 2.0f;
        track_length = bounds->width - self->handle_size;
    }
    else
    {
        /* Vertical: top is max, bottom is min */
        track_start = bounds->y + bounds->height - self->handle_size / 2.0f;
        track_length = -(bounds->height - self->handle_size);
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
    gfloat track_start;
    gfloat track_length;
    gdouble fraction;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        track_start = bounds->x + self->handle_size / 2.0f;
        track_length = bounds->width - self->handle_size;
    }
    else
    {
        track_start = bounds->y + self->handle_size / 2.0f;
        track_length = bounds->height - self->handle_size;
    }

    if (track_length <= 0.0f)
        return self->min;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        fraction = (pos - track_start) / track_length;
    }
    else
    {
        /* Vertical: top is max, bottom is min */
        fraction = 1.0 - ((pos - track_start) / track_length);
    }

    /* Clamp fraction to [0, 1] */
    if (fraction < 0.0)
        fraction = 0.0;
    if (fraction > 1.0)
        fraction = 1.0;

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
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_slider_draw (LrgWidget *widget)
{
    LrgSlider *self = LRG_SLIDER (widget);
    GrlRectangle bounds;
    gfloat track_x;
    gfloat track_y;
    gfloat track_w;
    gfloat track_h;
    gfloat fill_w;
    gfloat fill_h;
    gfloat handle_x;
    gfloat handle_y;
    gfloat handle_pos;
    gdouble fraction;

    bounds = get_widget_bounds (widget);
    fraction = lrg_slider_get_fraction (self);
    handle_pos = value_to_position (self, &bounds);

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        /* Horizontal: track runs left-right, centered vertically */
        track_x = bounds.x + self->handle_size / 2.0f;
        track_y = bounds.y + (bounds.height - self->track_thickness) / 2.0f;
        track_w = bounds.width - self->handle_size;
        track_h = self->track_thickness;

        /* Fill from left to handle position */
        fill_w = (gfloat)(fraction * track_w);
        fill_h = track_h;

        /* Handle centered on track at value position */
        handle_x = handle_pos;
        handle_y = bounds.y + bounds.height / 2.0f;
    }
    else
    {
        /* Vertical: track runs top-bottom, centered horizontally */
        track_x = bounds.x + (bounds.width - self->track_thickness) / 2.0f;
        track_y = bounds.y + self->handle_size / 2.0f;
        track_w = self->track_thickness;
        track_h = bounds.height - self->handle_size;

        /* Fill from bottom to handle position (value increases upward) */
        fill_w = track_w;
        fill_h = (gfloat)(fraction * track_h);

        /* Handle centered on track at value position */
        handle_x = bounds.x + bounds.width / 2.0f;
        handle_y = handle_pos;
    }

    /* Draw track background */
    grl_draw_rectangle (track_x, track_y, track_w, track_h, &self->track_color);

    /* Draw fill (progress portion) */
    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        grl_draw_rectangle (track_x, track_y, fill_w, fill_h, &self->fill_color);
    }
    else
    {
        /* Vertical: fill from bottom */
        grl_draw_rectangle (track_x, track_y + track_h - fill_h, fill_w, fill_h, &self->fill_color);
    }

    /* Draw handle */
    grl_draw_circle (handle_x, handle_y, self->handle_size / 2.0f, &self->handle_color);
}

static void
lrg_slider_measure (LrgWidget *widget,
                    gfloat    *preferred_width,
                    gfloat    *preferred_height)
{
    LrgSlider *self = LRG_SLIDER (widget);

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        if (preferred_width != NULL)
            *preferred_width = 200.0f;
        if (preferred_height != NULL)
            *preferred_height = self->handle_size;
    }
    else
    {
        if (preferred_width != NULL)
            *preferred_width = self->handle_size;
        if (preferred_height != NULL)
            *preferred_height = 200.0f;
    }
}

static gboolean
lrg_slider_handle_event (LrgWidget        *widget,
                         const LrgUIEvent *event)
{
    LrgSlider *self = LRG_SLIDER (widget);
    GrlRectangle bounds;
    LrgUIEventType type;
    gdouble new_value;
    gfloat pos;
    gfloat x;
    gfloat y;

    bounds = get_widget_bounds (widget);
    type = lrg_ui_event_get_event_type (event);
    x = lrg_ui_event_get_x (event);
    y = lrg_ui_event_get_y (event);

    switch (type)
    {
    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
        if (lrg_ui_event_get_button (event) == 0)  /* Left button */
        {
            self->is_dragging = TRUE;

            /* Jump to click position */
            if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
                pos = x;
            else
                pos = y;

            new_value = position_to_value (self, pos, &bounds);
            lrg_slider_set_value (self, new_value);

            return TRUE;
        }
        break;

    case LRG_UI_EVENT_MOUSE_BUTTON_UP:
        if (lrg_ui_event_get_button (event) == 0 && self->is_dragging)
        {
            self->is_dragging = FALSE;
            return TRUE;
        }
        break;

    case LRG_UI_EVENT_MOUSE_MOVE:
        if (self->is_dragging)
        {
            if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
                pos = x;
            else
                pos = y;

            new_value = position_to_value (self, pos, &bounds);
            lrg_slider_set_value (self, new_value);

            return TRUE;
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

    widget_class->draw = lrg_slider_draw;
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
     * The track background color.
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
     * The fill color.
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
     * The handle color.
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
     * The handle diameter.
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
     * The track thickness.
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIENTATION]);
}

/* ==========================================================================
 * Public API - Appearance
 * ========================================================================== */

/**
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

/**
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

    if (self->track_color.r == color->r &&
        self->track_color.g == color->g &&
        self->track_color.b == color->b &&
        self->track_color.a == color->a)
        return;

    self->track_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRACK_COLOR]);
}

/**
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

/**
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

    if (self->fill_color.r == color->r &&
        self->fill_color.g == color->g &&
        self->fill_color.b == color->b &&
        self->fill_color.a == color->a)
        return;

    self->fill_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_COLOR]);
}

/**
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

/**
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

    if (self->handle_color.r == color->r &&
        self->handle_color.g == color->g &&
        self->handle_color.b == color->b &&
        self->handle_color.a == color->a)
        return;

    self->handle_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HANDLE_COLOR]);
}

/**
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

/**
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

    if (self->handle_size == size)
        return;

    self->handle_size = size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HANDLE_SIZE]);
}

/**
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

/**
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

    if (self->track_thickness == thickness)
        return;

    self->track_thickness = thickness;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRACK_THICKNESS]);
}
