/* lrg-light2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base 2D light class implementation.
 */

#include "lrg-light2d.h"
#include "../lrg-log.h"

typedef struct
{
    gfloat   x, y;
    guint8   color_r, color_g, color_b;
    gfloat   intensity;
    gboolean enabled;
    gboolean casts_shadows;
    LrgShadowMethod shadow_method;
    gfloat   shadow_softness;
    LrgLightFalloff falloff;
    LrgLightBlendMode blend_mode;
    gint     layer;
} LrgLight2DPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgLight2D, lrg_light2d, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_X,
    PROP_Y,
    PROP_INTENSITY,
    PROP_ENABLED,
    PROP_CASTS_SHADOWS,
    PROP_SHADOW_METHOD,
    PROP_SHADOW_SOFTNESS,
    PROP_FALLOFF,
    PROP_BLEND_MODE,
    PROP_LAYER,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_light2d_real_render (LrgLight2D *self,
                         guint       target_id,
                         guint       width,
                         guint       height)
{
    /* Base implementation does nothing - subclasses override */
    (void)self;
    (void)target_id;
    (void)width;
    (void)height;
}

static gboolean
lrg_light2d_real_is_visible (LrgLight2D *self,
                             gfloat      viewport_x,
                             gfloat      viewport_y,
                             gfloat      viewport_width,
                             gfloat      viewport_height)
{
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (self);

    /* Default: always visible (subclasses should override with proper bounds check) */
    (void)viewport_x;
    (void)viewport_y;
    (void)viewport_width;
    (void)viewport_height;

    return priv->enabled;
}

static void
lrg_light2d_real_update (LrgLight2D *self,
                         gfloat      delta_time)
{
    /* Base implementation does nothing */
    (void)self;
    (void)delta_time;
}

static void
lrg_light2d_real_calculate_shadows (LrgLight2D *self,
                                    GPtrArray  *casters)
{
    /* Base implementation does nothing - subclasses override */
    (void)self;
    (void)casters;
}

static void
lrg_light2d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgLight2D *self = LRG_LIGHT2D (object);
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_X:
        g_value_set_float (value, priv->x);
        break;
    case PROP_Y:
        g_value_set_float (value, priv->y);
        break;
    case PROP_INTENSITY:
        g_value_set_float (value, priv->intensity);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    case PROP_CASTS_SHADOWS:
        g_value_set_boolean (value, priv->casts_shadows);
        break;
    case PROP_SHADOW_METHOD:
        g_value_set_enum (value, priv->shadow_method);
        break;
    case PROP_SHADOW_SOFTNESS:
        g_value_set_float (value, priv->shadow_softness);
        break;
    case PROP_FALLOFF:
        g_value_set_enum (value, priv->falloff);
        break;
    case PROP_BLEND_MODE:
        g_value_set_enum (value, priv->blend_mode);
        break;
    case PROP_LAYER:
        g_value_set_int (value, priv->layer);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_light2d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgLight2D *self = LRG_LIGHT2D (object);
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_X:
        priv->x = g_value_get_float (value);
        break;
    case PROP_Y:
        priv->y = g_value_get_float (value);
        break;
    case PROP_INTENSITY:
        priv->intensity = g_value_get_float (value);
        break;
    case PROP_ENABLED:
        priv->enabled = g_value_get_boolean (value);
        break;
    case PROP_CASTS_SHADOWS:
        priv->casts_shadows = g_value_get_boolean (value);
        break;
    case PROP_SHADOW_METHOD:
        priv->shadow_method = g_value_get_enum (value);
        break;
    case PROP_SHADOW_SOFTNESS:
        priv->shadow_softness = CLAMP (g_value_get_float (value), 0.0f, 1.0f);
        break;
    case PROP_FALLOFF:
        priv->falloff = g_value_get_enum (value);
        break;
    case PROP_BLEND_MODE:
        priv->blend_mode = g_value_get_enum (value);
        break;
    case PROP_LAYER:
        priv->layer = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_light2d_class_init (LrgLight2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_light2d_get_property;
    object_class->set_property = lrg_light2d_set_property;

    klass->render = lrg_light2d_real_render;
    klass->is_visible = lrg_light2d_real_is_visible;
    klass->update = lrg_light2d_real_update;
    klass->calculate_shadows = lrg_light2d_real_calculate_shadows;

    properties[PROP_X] =
        g_param_spec_float ("x", "X", "X position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_Y] =
        g_param_spec_float ("y", "Y", "Y position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity", "Intensity", "Light intensity",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled", "Enabled", "Whether light is enabled",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CASTS_SHADOWS] =
        g_param_spec_boolean ("casts-shadows", "Casts Shadows", "Whether light casts shadows",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHADOW_METHOD] =
        g_param_spec_enum ("shadow-method", "Shadow Method", "Shadow calculation method",
                           LRG_TYPE_SHADOW_METHOD, LRG_SHADOW_METHOD_GEOMETRY,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHADOW_SOFTNESS] =
        g_param_spec_float ("shadow-softness", "Shadow Softness", "Shadow edge softness",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FALLOFF] =
        g_param_spec_enum ("falloff", "Falloff", "Light falloff type",
                           LRG_TYPE_LIGHT_FALLOFF, LRG_LIGHT_FALLOFF_QUADRATIC,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BLEND_MODE] =
        g_param_spec_enum ("blend-mode", "Blend Mode", "Light blending mode",
                           LRG_TYPE_LIGHT_BLEND_MODE, LRG_LIGHT_BLEND_MULTIPLY,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LAYER] =
        g_param_spec_int ("layer", "Layer", "Render layer",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_light2d_init (LrgLight2D *self)
{
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (self);

    priv->x = 0.0f;
    priv->y = 0.0f;
    priv->color_r = 255;
    priv->color_g = 255;
    priv->color_b = 255;
    priv->intensity = 1.0f;
    priv->enabled = TRUE;
    priv->casts_shadows = TRUE;
    priv->shadow_method = LRG_SHADOW_METHOD_GEOMETRY;
    priv->shadow_softness = 0.0f;
    priv->falloff = LRG_LIGHT_FALLOFF_QUADRATIC;
    priv->blend_mode = LRG_LIGHT_BLEND_MULTIPLY;
    priv->layer = 0;
}

/* Public API */

void
lrg_light2d_get_position (LrgLight2D *self,
                          gfloat     *x,
                          gfloat     *y)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);

    if (x) *x = priv->x;
    if (y) *y = priv->y;
}

void
lrg_light2d_set_position (LrgLight2D *self,
                          gfloat      x,
                          gfloat      y)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->x = x;
    priv->y = y;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Y]);
}

void
lrg_light2d_get_color (LrgLight2D *self,
                       guint8     *r,
                       guint8     *g,
                       guint8     *b)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);

    if (r) *r = priv->color_r;
    if (g) *g = priv->color_g;
    if (b) *b = priv->color_b;
}

void
lrg_light2d_set_color (LrgLight2D *self,
                       guint8      r,
                       guint8      g,
                       guint8      b)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->color_r = r;
    priv->color_g = g;
    priv->color_b = b;
}

gfloat
lrg_light2d_get_intensity (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), 0.0f);

    priv = lrg_light2d_get_instance_private (self);
    return priv->intensity;
}

void
lrg_light2d_set_intensity (LrgLight2D *self,
                           gfloat      intensity)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->intensity = intensity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

gboolean
lrg_light2d_get_enabled (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), FALSE);

    priv = lrg_light2d_get_instance_private (self);
    return priv->enabled;
}

void
lrg_light2d_set_enabled (LrgLight2D *self,
                         gboolean    enabled)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->enabled = enabled;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

gboolean
lrg_light2d_get_casts_shadows (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), FALSE);

    priv = lrg_light2d_get_instance_private (self);
    return priv->casts_shadows;
}

void
lrg_light2d_set_casts_shadows (LrgLight2D *self,
                               gboolean    casts_shadows)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->casts_shadows = casts_shadows;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CASTS_SHADOWS]);
}

LrgShadowMethod
lrg_light2d_get_shadow_method (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), LRG_SHADOW_METHOD_GEOMETRY);

    priv = lrg_light2d_get_instance_private (self);
    return priv->shadow_method;
}

void
lrg_light2d_set_shadow_method (LrgLight2D      *self,
                               LrgShadowMethod  method)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->shadow_method = method;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOW_METHOD]);
}

gfloat
lrg_light2d_get_shadow_softness (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), 0.0f);

    priv = lrg_light2d_get_instance_private (self);
    return priv->shadow_softness;
}

void
lrg_light2d_set_shadow_softness (LrgLight2D *self,
                                 gfloat      softness)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->shadow_softness = CLAMP (softness, 0.0f, 1.0f);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOW_SOFTNESS]);
}

LrgLightFalloff
lrg_light2d_get_falloff (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), LRG_LIGHT_FALLOFF_QUADRATIC);

    priv = lrg_light2d_get_instance_private (self);
    return priv->falloff;
}

void
lrg_light2d_set_falloff (LrgLight2D      *self,
                         LrgLightFalloff  falloff)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->falloff = falloff;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FALLOFF]);
}

LrgLightBlendMode
lrg_light2d_get_blend_mode (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), LRG_LIGHT_BLEND_MULTIPLY);

    priv = lrg_light2d_get_instance_private (self);
    return priv->blend_mode;
}

void
lrg_light2d_set_blend_mode (LrgLight2D        *self,
                            LrgLightBlendMode  mode)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->blend_mode = mode;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLEND_MODE]);
}

gint
lrg_light2d_get_layer (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), 0);

    priv = lrg_light2d_get_instance_private (self);
    return priv->layer;
}

void
lrg_light2d_set_layer (LrgLight2D *self,
                       gint        layer)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->layer = layer;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

/* Virtual method wrappers */

void
lrg_light2d_render (LrgLight2D *self,
                    guint       target_id,
                    guint       width,
                    guint       height)
{
    g_return_if_fail (LRG_IS_LIGHT2D (self));
    LRG_LIGHT2D_GET_CLASS (self)->render (self, target_id, width, height);
}

gboolean
lrg_light2d_is_visible (LrgLight2D *self,
                        gfloat      viewport_x,
                        gfloat      viewport_y,
                        gfloat      viewport_width,
                        gfloat      viewport_height)
{
    g_return_val_if_fail (LRG_IS_LIGHT2D (self), FALSE);
    return LRG_LIGHT2D_GET_CLASS (self)->is_visible (self, viewport_x, viewport_y,
                                                     viewport_width, viewport_height);
}

void
lrg_light2d_update (LrgLight2D *self,
                    gfloat      delta_time)
{
    g_return_if_fail (LRG_IS_LIGHT2D (self));
    LRG_LIGHT2D_GET_CLASS (self)->update (self, delta_time);
}

void
lrg_light2d_calculate_shadows (LrgLight2D *self,
                               GPtrArray  *casters)
{
    g_return_if_fail (LRG_IS_LIGHT2D (self));
    LRG_LIGHT2D_GET_CLASS (self)->calculate_shadows (self, casters);
}
