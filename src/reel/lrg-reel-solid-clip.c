/* lrg-reel-solid-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-solid-clip.h"
#include "../graphics/lrg-image-canvas.h"
#include "lrg-reel-context.h"
#include <graylib.h>

struct _LrgReelSolidClip
{
    LrgReelClip  parent_instance;

    GrlColor     color;
};

G_DEFINE_FINAL_TYPE (LrgReelSolidClip, lrg_reel_solid_clip, LRG_TYPE_REEL_CLIP)

enum
{
    PROP_0,
    PROP_COLOR,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Render vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_solid_clip_render (LrgReelClip    *base,
                             LrgReelContext *ctx,
                             LrgImageCanvas *canvas)
{
    LrgReelSolidClip *self;

    (void) ctx;

    self = LRG_REEL_SOLID_CLIP (base);

    lrg_image_canvas_clear (canvas, &self->color);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_solid_clip_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgReelSolidClip *self = LRG_REEL_SOLID_CLIP (object);
    GrlColor          tmp;

    switch (prop_id)
    {
    case PROP_COLOR:
        tmp = self->color;
        g_value_set_boxed (value, &tmp);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_solid_clip_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgReelSolidClip *self = LRG_REEL_SOLID_CLIP (object);

    switch (prop_id)
    {
    case PROP_COLOR:
        lrg_reel_solid_clip_set_color (self,
                                       (const GrlColor *) g_value_get_boxed (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_solid_clip_class_init (LrgReelSolidClipClass *klass)
{
    GObjectClass     *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class   = LRG_REEL_CLIP_CLASS (klass);

    object_class->get_property = lrg_reel_solid_clip_get_property;
    object_class->set_property = lrg_reel_solid_clip_set_property;

    clip_class->render = lrg_reel_solid_clip_render;

    /**
     * LrgReelSolidClip:color:
     *
     * The solid fill color used for every rendered frame.
     *
     * Since: 1.0
     */
    properties[PROP_COLOR] =
        g_param_spec_boxed ("color", "Color",
                            "Solid fill color for the frame",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_solid_clip_init (LrgReelSolidClip *self)
{
    self->color.r = 0;
    self->color.g = 0;
    self->color.b = 0;
    self->color.a = 255;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_solid_clip_new:
 * @color: the fill color.
 *
 * Creates a new #LrgReelSolidClip that fills each frame with @color.
 *
 * Returns: (transfer full): a new #LrgReelSolidClip.
 *
 * Since: 1.0
 */
LrgReelSolidClip *
lrg_reel_solid_clip_new (const GrlColor *color)
{
    LrgReelSolidClip *self;

    g_return_val_if_fail (color != NULL, NULL);

    self        = g_object_new (LRG_TYPE_REEL_SOLID_CLIP, NULL);
    self->color = *color;

    return self;
}

/**
 * lrg_reel_solid_clip_get_color:
 * @self: an #LrgReelSolidClip.
 * @out_color: (out caller-allocates): return location for the color.
 *
 * Copies the current fill color into @out_color.
 *
 * Since: 1.0
 */
void
lrg_reel_solid_clip_get_color (LrgReelSolidClip *self,
                                GrlColor         *out_color)
{
    g_return_if_fail (LRG_IS_REEL_SOLID_CLIP (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->color;
}

/**
 * lrg_reel_solid_clip_set_color:
 * @self: an #LrgReelSolidClip.
 * @color: the new fill color.
 *
 * Sets the fill color used when rendering this clip.
 *
 * Since: 1.0
 */
void
lrg_reel_solid_clip_set_color (LrgReelSolidClip *self,
                                const GrlColor   *color)
{
    g_return_if_fail (LRG_IS_REEL_SOLID_CLIP (self));
    g_return_if_fail (color != NULL);

    self->color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
}
