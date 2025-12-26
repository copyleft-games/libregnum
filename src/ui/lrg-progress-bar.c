/* lrg-progress-bar.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Progress bar widget for displaying completion status.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-progress-bar.h"
#include "../lrg-log.h"
#include <stdio.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgProgressBar
{
    LrgWidget       parent_instance;

    gdouble         value;
    gdouble         max;
    gboolean        show_text;
    LrgOrientation  orientation;
    GrlColor        background_color;
    GrlColor        fill_color;
    GrlColor        text_color;
    gfloat          corner_radius;
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
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default colors */
static const GrlColor DEFAULT_BACKGROUND = { 60, 60, 60, 255 };
static const GrlColor DEFAULT_FILL       = { 50, 150, 50, 255 };
static const GrlColor DEFAULT_TEXT       = { 255, 255, 255, 255 };

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_progress_bar_draw (LrgWidget *widget)
{
    LrgProgressBar *self = LRG_PROGRESS_BAR (widget);
    GrlRectangle    bg_rect, fill_rect;
    gfloat          world_x, world_y, width, height;
    gdouble         fraction;

    world_x = lrg_widget_get_world_x (widget);
    world_y = lrg_widget_get_world_y (widget);
    width = lrg_widget_get_width (widget);
    height = lrg_widget_get_height (widget);

    /* Calculate fraction */
    fraction = (self->max > 0.0) ? (self->value / self->max) : 0.0;
    if (fraction < 0.0)
    {
        fraction = 0.0;
    }
    else if (fraction > 1.0)
    {
        fraction = 1.0;
    }

    /* Background rectangle */
    bg_rect.x = world_x;
    bg_rect.y = world_y;
    bg_rect.width = width;
    bg_rect.height = height;

    /* Fill rectangle based on orientation */
    fill_rect.x = world_x;
    fill_rect.y = world_y;

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        fill_rect.width = (gfloat)(width * fraction);
        fill_rect.height = height;
    }
    else
    {
        /* Vertical: fill from bottom up */
        fill_rect.width = width;
        fill_rect.height = (gfloat)(height * fraction);
        fill_rect.y = world_y + height - fill_rect.height;
    }

    /* Draw background */
    if (self->corner_radius > 0.0f)
    {
        gfloat roundness = self->corner_radius / (width < height ? width : height);
        if (roundness > 1.0f)
        {
            roundness = 1.0f;
        }
        grl_draw_rectangle_rounded (&bg_rect, roundness, 8, &self->background_color);
    }
    else
    {
        grl_draw_rectangle_rec (&bg_rect, &self->background_color);
    }

    /* Draw fill */
    if (fraction > 0.0)
    {
        if (self->corner_radius > 0.0f)
        {
            gfloat roundness = self->corner_radius /
                               (fill_rect.width < fill_rect.height ?
                                fill_rect.width : fill_rect.height);
            if (roundness > 1.0f)
            {
                roundness = 1.0f;
            }
            grl_draw_rectangle_rounded (&fill_rect, roundness, 8, &self->fill_color);
        }
        else
        {
            grl_draw_rectangle_rec (&fill_rect, &self->fill_color);
        }
    }

    /* Draw percentage text if enabled */
    if (self->show_text)
    {
        gchar  text[16];
        gint   percent = (gint)(fraction * 100.0);
        gint   text_x, text_y;
        gfloat font_size = 14.0f;
        gfloat text_width;

        snprintf (text, sizeof(text), "%d%%", percent);

        /* Approximate text width */
        text_width = (gfloat)g_utf8_strlen (text, -1) * (font_size * 0.6f);

        /* Center text */
        text_x = (gint)(world_x + (width - text_width) / 2.0f);
        text_y = (gint)(world_y + (height - font_size) / 2.0f);

        grl_draw_text (text, text_x, text_y, (gint)font_size, &self->text_color);
    }
}

static void
lrg_progress_bar_measure (LrgWidget *widget,
                          gfloat    *preferred_width,
                          gfloat    *preferred_height)
{
    LrgProgressBar *self = LRG_PROGRESS_BAR (widget);

    if (self->orientation == LRG_ORIENTATION_HORIZONTAL)
    {
        if (preferred_width != NULL)
        {
            *preferred_width = 200.0f;
        }
        if (preferred_height != NULL)
        {
            *preferred_height = 24.0f;
        }
    }
    else
    {
        if (preferred_width != NULL)
        {
            *preferred_width = 24.0f;
        }
        if (preferred_height != NULL)
        {
            *preferred_height = 200.0f;
        }
    }
}

static gboolean
lrg_progress_bar_handle_event (LrgWidget        *widget,
                               const LrgUIEvent *event)
{
    /* Progress bar is display-only, no input handling */
    (void)widget;
    (void)event;
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
        lrg_progress_bar_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_FILL_COLOR:
        lrg_progress_bar_set_fill_color (self, g_value_get_boxed (value));
        break;
    case PROP_TEXT_COLOR:
        lrg_progress_bar_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_progress_bar_set_corner_radius (self, g_value_get_float (value));
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

    widget_class->draw = lrg_progress_bar_draw;
    widget_class->measure = lrg_progress_bar_measure;
    widget_class->handle_event = lrg_progress_bar_handle_event;

    /**
     * LrgProgressBar:value:
     *
     * The current progress value.
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
     * The maximum value.
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
     * Whether to display the percentage text.
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
     * The progress bar orientation.
     */
    properties[PROP_ORIENTATION] =
        g_param_spec_enum ("orientation",
                           "Orientation",
                           "The progress bar orientation",
                           LRG_TYPE_ORIENTATION,
                           LRG_ORIENTATION_HORIZONTAL,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "The track background color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_FILL_COLOR] =
        g_param_spec_boxed ("fill-color",
                            "Fill Color",
                            "The progress fill color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

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
     * The corner radius for rounded corners.
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "The corner radius",
                            0.0f, G_MAXFLOAT, 4.0f,
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

    if (value < 0.0)
    {
        value = 0.0;
    }
    else if (value > self->max)
    {
        value = self->max;
    }

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
    gdouble fraction;

    g_return_val_if_fail (LRG_IS_PROGRESS_BAR (self), 0.0);

    fraction = (self->max > 0.0) ? (self->value / self->max) : 0.0;
    if (fraction < 0.0)
    {
        fraction = 0.0;
    }
    else if (fraction > 1.0)
    {
        fraction = 1.0;
    }

    return fraction;
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
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_TEXT]);
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

    if (self->corner_radius != radius)
    {
        self->corner_radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
    }
}
