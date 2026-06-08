/* lrg-reel-drop-shadow-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-drop-shadow-effect.h"
#include "lrg-reel-context.h"
#include <string.h>
#include <raylib.h>

struct _LrgReelDropShadowEffect
{
    LrgReelEffect parent_instance;

    gint     offset_x;
    gint     offset_y;
    gint     blur_radius;
    GrlColor shadow_color;
};

G_DEFINE_FINAL_TYPE (LrgReelDropShadowEffect, lrg_reel_drop_shadow_effect,
                     LRG_TYPE_REEL_EFFECT)

enum
{
    PROP_0,
    PROP_OFFSET_X,
    PROP_OFFSET_Y,
    PROP_BLUR_RADIUS,
    PROP_SHADOW_COLOR,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * apply vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_drop_shadow_effect_apply (LrgReelEffect  *base,
                                    GrlImage       *image,
                                    LrgReelContext *ctx)
{
    LrgReelDropShadowEffect *self;
    g_autoptr(GrlImage)      shadow     = NULL;
    g_autoptr(GrlImage)      result     = NULL;
    g_autoptr(GrlColor)      transparent = NULL;
    Image                   *src_img;
    Image                   *dst_img;
    guint8                  *src_data;
    guint8                  *dst_data;
    gint                     width;
    gint                     height;
    gint                     n_bytes;
    gint                     n_pixels;
    gint                     i;

    (void) ctx;

    self = LRG_REEL_DROP_SHADOW_EFFECT (base);

    if (grl_image_get_format (image) != GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        return;

    width  = grl_image_get_width (image);
    height = grl_image_get_height (image);

    /* Step 1: build the shadow silhouette — copy @image, replace opaque pixels
     *         with shadow_color whilst preserving alpha (so blur smooths edges). */
    shadow   = grl_image_copy (image);
    src_img  = (Image *) grl_image_get_handle (shadow);
    src_data = (guint8 *) src_img->data;
    n_pixels = width * height;

    for (i = 0; i < n_pixels; i++)
    {
        guint8 a;

        a = src_data[i * 4 + 3];
        if (a > 0)
        {
            src_data[i * 4 + 0] = self->shadow_color.r;
            src_data[i * 4 + 1] = self->shadow_color.g;
            src_data[i * 4 + 2] = self->shadow_color.b;
            /* Scale shadow alpha by original pixel alpha and shadow alpha. */
            src_data[i * 4 + 3] = (guint8) ((a * (guint32) self->shadow_color.a) / 255u);
        }
    }

    /* Step 2: blur the shadow silhouette. */
    if (self->blur_radius > 0)
        grl_image_blur_box (shadow, self->blur_radius);

    /* Step 3: build result canvas — clear to transparent. */
    transparent = grl_color_new (0, 0, 0, 0);
    result      = grl_image_new_color (width, height, transparent);

    /* Step 4: composite shadow at offset onto result. */
    grl_image_composite (result, shadow,
                          GRL_PORTER_DUFF_SRC_OVER,
                          self->offset_x, self->offset_y);

    /* Step 5: composite the original image over the shadow. */
    grl_image_composite (result, image,
                          GRL_PORTER_DUFF_SRC_OVER,
                          0, 0);

    /* Step 6: copy result back into @image in-place (raw memcpy of pixel data). */
    dst_img  = (Image *) grl_image_get_handle (image);
    dst_data = (guint8 *) dst_img->data;
    n_bytes  = width * height * 4;
    src_img  = (Image *) grl_image_get_handle (result);
    src_data = (guint8 *) src_img->data;
    memcpy (dst_data, src_data, (gsize) n_bytes);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_drop_shadow_effect_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
    LrgReelDropShadowEffect *self = LRG_REEL_DROP_SHADOW_EFFECT (object);
    GrlColor                 tmp;

    switch (prop_id)
    {
    case PROP_OFFSET_X:
        g_value_set_int (value, self->offset_x);
        break;
    case PROP_OFFSET_Y:
        g_value_set_int (value, self->offset_y);
        break;
    case PROP_BLUR_RADIUS:
        g_value_set_int (value, self->blur_radius);
        break;
    case PROP_SHADOW_COLOR:
        tmp = self->shadow_color;
        g_value_set_boxed (value, &tmp);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_drop_shadow_effect_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
    LrgReelDropShadowEffect *self = LRG_REEL_DROP_SHADOW_EFFECT (object);

    switch (prop_id)
    {
    case PROP_OFFSET_X:
        lrg_reel_drop_shadow_effect_set_offset_x (self, g_value_get_int (value));
        break;
    case PROP_OFFSET_Y:
        lrg_reel_drop_shadow_effect_set_offset_y (self, g_value_get_int (value));
        break;
    case PROP_BLUR_RADIUS:
        lrg_reel_drop_shadow_effect_set_blur_radius (self, g_value_get_int (value));
        break;
    case PROP_SHADOW_COLOR:
        lrg_reel_drop_shadow_effect_set_shadow_color (
            self, (const GrlColor *) g_value_get_boxed (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_drop_shadow_effect_class_init (LrgReelDropShadowEffectClass *klass)
{
    GObjectClass       *object_class = G_OBJECT_CLASS (klass);
    LrgReelEffectClass *effect_class = LRG_REEL_EFFECT_CLASS (klass);

    object_class->get_property = lrg_reel_drop_shadow_effect_get_property;
    object_class->set_property = lrg_reel_drop_shadow_effect_set_property;

    effect_class->apply = lrg_reel_drop_shadow_effect_apply;

    /**
     * LrgReelDropShadowEffect:offset-x:
     *
     * Horizontal shadow offset in pixels (positive = right).
     *
     * Since: 1.0
     */
    properties[PROP_OFFSET_X] =
        g_param_spec_int ("offset-x",
                          "Offset X",
                          "Horizontal shadow offset in pixels",
                          G_MININT, G_MAXINT, 4,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelDropShadowEffect:offset-y:
     *
     * Vertical shadow offset in pixels (positive = down).
     *
     * Since: 1.0
     */
    properties[PROP_OFFSET_Y] =
        g_param_spec_int ("offset-y",
                          "Offset Y",
                          "Vertical shadow offset in pixels",
                          G_MININT, G_MAXINT, 4,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelDropShadowEffect:blur-radius:
     *
     * Shadow blur radius (0 = sharp, crisp shadow).
     *
     * Since: 1.0
     */
    properties[PROP_BLUR_RADIUS] =
        g_param_spec_int ("blur-radius",
                          "Blur Radius",
                          "Shadow blur radius in pixels",
                          0, G_MAXINT, 6,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelDropShadowEffect:shadow-color:
     *
     * Shadow color, including alpha for opacity control.
     *
     * Since: 1.0
     */
    properties[PROP_SHADOW_COLOR] =
        g_param_spec_boxed ("shadow-color",
                            "Shadow Color",
                            "Shadow color (alpha controls opacity)",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_drop_shadow_effect_init (LrgReelDropShadowEffect *self)
{
    self->offset_x         = 4;
    self->offset_y         = 4;
    self->blur_radius      = 6;
    /* Semi-transparent black. */
    self->shadow_color.r   = 0;
    self->shadow_color.g   = 0;
    self->shadow_color.b   = 0;
    self->shadow_color.a   = 128;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_drop_shadow_effect_new:
 *
 * Creates a new #LrgReelDropShadowEffect.
 *
 * Returns: (transfer full): a new #LrgReelDropShadowEffect
 *
 * Since: 1.0
 */
LrgReelDropShadowEffect *
lrg_reel_drop_shadow_effect_new (void)
{
    return g_object_new (LRG_TYPE_REEL_DROP_SHADOW_EFFECT, NULL);
}

/**
 * lrg_reel_drop_shadow_effect_get_offset_x:
 * @self: a #LrgReelDropShadowEffect
 *
 * Returns: the horizontal shadow offset
 *
 * Since: 1.0
 */
gint
lrg_reel_drop_shadow_effect_get_offset_x (LrgReelDropShadowEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_DROP_SHADOW_EFFECT (self), 0);

    return self->offset_x;
}

/**
 * lrg_reel_drop_shadow_effect_set_offset_x:
 * @self: a #LrgReelDropShadowEffect
 * @offset_x: horizontal shadow offset in pixels
 *
 * Since: 1.0
 */
void
lrg_reel_drop_shadow_effect_set_offset_x (LrgReelDropShadowEffect *self,
                                           gint                     offset_x)
{
    g_return_if_fail (LRG_IS_REEL_DROP_SHADOW_EFFECT (self));

    if (self->offset_x == offset_x)
        return;

    self->offset_x = offset_x;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OFFSET_X]);
}

/**
 * lrg_reel_drop_shadow_effect_get_offset_y:
 * @self: a #LrgReelDropShadowEffect
 *
 * Returns: the vertical shadow offset
 *
 * Since: 1.0
 */
gint
lrg_reel_drop_shadow_effect_get_offset_y (LrgReelDropShadowEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_DROP_SHADOW_EFFECT (self), 0);

    return self->offset_y;
}

/**
 * lrg_reel_drop_shadow_effect_set_offset_y:
 * @self: a #LrgReelDropShadowEffect
 * @offset_y: vertical shadow offset in pixels
 *
 * Since: 1.0
 */
void
lrg_reel_drop_shadow_effect_set_offset_y (LrgReelDropShadowEffect *self,
                                           gint                     offset_y)
{
    g_return_if_fail (LRG_IS_REEL_DROP_SHADOW_EFFECT (self));

    if (self->offset_y == offset_y)
        return;

    self->offset_y = offset_y;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OFFSET_Y]);
}

/**
 * lrg_reel_drop_shadow_effect_get_blur_radius:
 * @self: a #LrgReelDropShadowEffect
 *
 * Returns: the shadow blur radius
 *
 * Since: 1.0
 */
gint
lrg_reel_drop_shadow_effect_get_blur_radius (LrgReelDropShadowEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_DROP_SHADOW_EFFECT (self), 0);

    return self->blur_radius;
}

/**
 * lrg_reel_drop_shadow_effect_set_blur_radius:
 * @self: a #LrgReelDropShadowEffect
 * @blur_radius: shadow blur radius
 *
 * Since: 1.0
 */
void
lrg_reel_drop_shadow_effect_set_blur_radius (LrgReelDropShadowEffect *self,
                                              gint                     blur_radius)
{
    g_return_if_fail (LRG_IS_REEL_DROP_SHADOW_EFFECT (self));

    if (self->blur_radius == blur_radius)
        return;

    self->blur_radius = blur_radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLUR_RADIUS]);
}

/**
 * lrg_reel_drop_shadow_effect_get_shadow_color:
 * @self: a #LrgReelDropShadowEffect
 * @out_color: (out caller-allocates): return location for the shadow color
 *
 * Since: 1.0
 */
void
lrg_reel_drop_shadow_effect_get_shadow_color (LrgReelDropShadowEffect *self,
                                               GrlColor                *out_color)
{
    g_return_if_fail (LRG_IS_REEL_DROP_SHADOW_EFFECT (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->shadow_color;
}

/**
 * lrg_reel_drop_shadow_effect_set_shadow_color:
 * @self: a #LrgReelDropShadowEffect
 * @color: (not nullable): new shadow color
 *
 * Since: 1.0
 */
void
lrg_reel_drop_shadow_effect_set_shadow_color (LrgReelDropShadowEffect *self,
                                               const GrlColor          *color)
{
    g_return_if_fail (LRG_IS_REEL_DROP_SHADOW_EFFECT (self));
    g_return_if_fail (color != NULL);

    self->shadow_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOW_COLOR]);
}
