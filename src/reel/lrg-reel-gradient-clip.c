/* lrg-reel-gradient-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-gradient-clip.h"
#include "../graphics/lrg-image-canvas.h"
#include "lrg-reel-context.h"
#include <graylib.h>

struct _LrgReelGradientClip
{
    LrgReelClip      parent_instance;

    /*
     * color_start: start color for linear, inner color for radial.
     * color_end:   end color for linear, outer color for radial.
     */
    GrlColor         color_start;
    GrlColor         color_end;

    gboolean         radial;
    GrlGradientAxis  axis;
};

G_DEFINE_FINAL_TYPE (LrgReelGradientClip, lrg_reel_gradient_clip,
                     LRG_TYPE_REEL_CLIP)

enum
{
    PROP_0,
    PROP_IS_RADIAL,
    PROP_AXIS,
    PROP_START_COLOR,
    PROP_END_COLOR,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Render vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_gradient_clip_render (LrgReelClip    *base,
                                LrgReelContext *ctx,
                                LrgImageCanvas *canvas)
{
    LrgReelGradientClip *self;
    gint                 w;
    gint                 h;
    gint                 radius;

    self = LRG_REEL_GRADIENT_CLIP (base);

    w = lrg_reel_context_get_width (ctx);
    h = lrg_reel_context_get_height (ctx);

    if (self->radial)
    {
        radius = (w > h) ? w : h;
        radius = radius / 2;

        lrg_image_canvas_draw_gradient_radial (canvas,
                                               w / 2,
                                               h / 2,
                                               radius,
                                               &self->color_start,
                                               &self->color_end);
    }
    else
    {
        lrg_image_canvas_draw_gradient_rect (canvas,
                                             0, 0, w, h,
                                             &self->color_start,
                                             &self->color_end,
                                             self->axis);
    }
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_gradient_clip_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
    LrgReelGradientClip *self = LRG_REEL_GRADIENT_CLIP (object);
    GrlColor             tmp;

    switch (prop_id)
    {
    case PROP_IS_RADIAL:
        g_value_set_boolean (value, self->radial);
        break;
    case PROP_AXIS:
        g_value_set_enum (value, (gint) self->axis);
        break;
    case PROP_START_COLOR:
        tmp = self->color_start;
        g_value_set_boxed (value, &tmp);
        break;
    case PROP_END_COLOR:
        tmp = self->color_end;
        g_value_set_boxed (value, &tmp);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_gradient_clip_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    LrgReelGradientClip *self = LRG_REEL_GRADIENT_CLIP (object);

    switch (prop_id)
    {
    case PROP_IS_RADIAL:
        self->radial = g_value_get_boolean (value);
        g_object_notify_by_pspec (object, properties[PROP_IS_RADIAL]);
        break;
    case PROP_AXIS:
        lrg_reel_gradient_clip_set_axis (self,
                                         (GrlGradientAxis) g_value_get_enum (value));
        break;
    case PROP_START_COLOR:
        lrg_reel_gradient_clip_set_start_color (
            self, (const GrlColor *) g_value_get_boxed (value));
        break;
    case PROP_END_COLOR:
        lrg_reel_gradient_clip_set_end_color (
            self, (const GrlColor *) g_value_get_boxed (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_gradient_clip_class_init (LrgReelGradientClipClass *klass)
{
    GObjectClass     *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class   = LRG_REEL_CLIP_CLASS (klass);

    object_class->get_property = lrg_reel_gradient_clip_get_property;
    object_class->set_property = lrg_reel_gradient_clip_set_property;

    clip_class->render = lrg_reel_gradient_clip_render;

    /**
     * LrgReelGradientClip:is-radial:
     *
     * %TRUE if the gradient is radial; %FALSE if it is linear.
     *
     * Since: 1.0
     */
    properties[PROP_IS_RADIAL] =
        g_param_spec_boolean ("is-radial", "Is Radial",
                              "TRUE if the gradient is radial",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelGradientClip:axis:
     *
     * The gradient axis for linear gradients.  Ignored for radial gradients.
     *
     * Since: 1.0
     */
    properties[PROP_AXIS] =
        g_param_spec_enum ("axis", "Axis",
                           "Gradient axis (linear only)",
                           GRL_TYPE_GRADIENT_AXIS,
                           GRL_GRADIENT_AXIS_VERTICAL,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelGradientClip:start-color:
     *
     * The start color (top or left for linear; center for radial).
     *
     * Since: 1.0
     */
    properties[PROP_START_COLOR] =
        g_param_spec_boxed ("start-color", "Start Color",
                            "Start or inner gradient color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelGradientClip:end-color:
     *
     * The end color (bottom or right for linear; outer edge for radial).
     *
     * Since: 1.0
     */
    properties[PROP_END_COLOR] =
        g_param_spec_boxed ("end-color", "End Color",
                            "End or outer gradient color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_gradient_clip_init (LrgReelGradientClip *self)
{
    self->color_start.r = 0;
    self->color_start.g = 0;
    self->color_start.b = 0;
    self->color_start.a = 255;

    self->color_end.r = 0;
    self->color_end.g = 0;
    self->color_end.b = 0;
    self->color_end.a = 255;

    self->radial = FALSE;
    self->axis   = GRL_GRADIENT_AXIS_VERTICAL;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_gradient_clip_new_linear:
 * @start: the gradient color at the start edge.
 * @end: the gradient color at the end edge.
 * @axis: %GRL_GRADIENT_AXIS_HORIZONTAL or %GRL_GRADIENT_AXIS_VERTICAL.
 *
 * Creates a new #LrgReelGradientClip with a linear gradient.
 *
 * Returns: (transfer full): a new #LrgReelGradientClip.
 *
 * Since: 1.0
 */
LrgReelGradientClip *
lrg_reel_gradient_clip_new_linear (const GrlColor  *start,
                                   const GrlColor  *end,
                                   GrlGradientAxis  axis)
{
    LrgReelGradientClip *self;

    g_return_val_if_fail (start != NULL, NULL);
    g_return_val_if_fail (end != NULL, NULL);

    self              = g_object_new (LRG_TYPE_REEL_GRADIENT_CLIP, NULL);
    self->color_start = *start;
    self->color_end   = *end;
    self->radial      = FALSE;
    self->axis        = axis;

    return self;
}

/**
 * lrg_reel_gradient_clip_new_radial:
 * @inner: the gradient color at the center.
 * @outer: the gradient color at the edge.
 *
 * Creates a new #LrgReelGradientClip with a radial gradient.
 *
 * Returns: (transfer full): a new #LrgReelGradientClip.
 *
 * Since: 1.0
 */
LrgReelGradientClip *
lrg_reel_gradient_clip_new_radial (const GrlColor *inner,
                                   const GrlColor *outer)
{
    LrgReelGradientClip *self;

    g_return_val_if_fail (inner != NULL, NULL);
    g_return_val_if_fail (outer != NULL, NULL);

    self              = g_object_new (LRG_TYPE_REEL_GRADIENT_CLIP, NULL);
    self->color_start = *inner;
    self->color_end   = *outer;
    self->radial      = TRUE;

    return self;
}

/**
 * lrg_reel_gradient_clip_get_is_radial:
 * @self: an #LrgReelGradientClip.
 *
 * Returns: %TRUE if the gradient is radial.
 *
 * Since: 1.0
 */
gboolean
lrg_reel_gradient_clip_get_is_radial (LrgReelGradientClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_GRADIENT_CLIP (self), FALSE);

    return self->radial;
}

/**
 * lrg_reel_gradient_clip_get_axis:
 * @self: an #LrgReelGradientClip.
 *
 * Returns: the #GrlGradientAxis in use.
 *
 * Since: 1.0
 */
GrlGradientAxis
lrg_reel_gradient_clip_get_axis (LrgReelGradientClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_GRADIENT_CLIP (self),
                          GRL_GRADIENT_AXIS_VERTICAL);

    return self->axis;
}

/**
 * lrg_reel_gradient_clip_set_axis:
 * @self: an #LrgReelGradientClip.
 * @axis: the new #GrlGradientAxis.
 *
 * Changes the axis for a linear gradient.
 *
 * Since: 1.0
 */
void
lrg_reel_gradient_clip_set_axis (LrgReelGradientClip *self,
                                  GrlGradientAxis      axis)
{
    g_return_if_fail (LRG_IS_REEL_GRADIENT_CLIP (self));

    if (self->axis == axis)
        return;

    self->axis = axis;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AXIS]);
}

/**
 * lrg_reel_gradient_clip_get_start_color:
 * @self: an #LrgReelGradientClip.
 * @out_color: (out caller-allocates): return location for the color.
 *
 * Copies the start (or inner) color into @out_color.
 *
 * Since: 1.0
 */
void
lrg_reel_gradient_clip_get_start_color (LrgReelGradientClip *self,
                                         GrlColor            *out_color)
{
    g_return_if_fail (LRG_IS_REEL_GRADIENT_CLIP (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->color_start;
}

/**
 * lrg_reel_gradient_clip_set_start_color:
 * @self: an #LrgReelGradientClip.
 * @color: the new start (or inner) color.
 *
 * Sets the start color for linear gradients or the inner color for radial
 * gradients.
 *
 * Since: 1.0
 */
void
lrg_reel_gradient_clip_set_start_color (LrgReelGradientClip *self,
                                         const GrlColor      *color)
{
    g_return_if_fail (LRG_IS_REEL_GRADIENT_CLIP (self));
    g_return_if_fail (color != NULL);

    self->color_start = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START_COLOR]);
}

/**
 * lrg_reel_gradient_clip_get_end_color:
 * @self: an #LrgReelGradientClip.
 * @out_color: (out caller-allocates): return location for the color.
 *
 * Copies the end (or outer) color into @out_color.
 *
 * Since: 1.0
 */
void
lrg_reel_gradient_clip_get_end_color (LrgReelGradientClip *self,
                                       GrlColor            *out_color)
{
    g_return_if_fail (LRG_IS_REEL_GRADIENT_CLIP (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->color_end;
}

/**
 * lrg_reel_gradient_clip_set_end_color:
 * @self: an #LrgReelGradientClip.
 * @color: the new end (or outer) color.
 *
 * Sets the end color for linear gradients or the outer color for radial
 * gradients.
 *
 * Since: 1.0
 */
void
lrg_reel_gradient_clip_set_end_color (LrgReelGradientClip *self,
                                       const GrlColor      *color)
{
    g_return_if_fail (LRG_IS_REEL_GRADIENT_CLIP (self));
    g_return_if_fail (color != NULL);

    self->color_end = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_END_COLOR]);
}
