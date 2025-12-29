/* lrg-vignette.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-vignette.h"

/**
 * SECTION:lrg-vignette
 * @Title: LrgVignette
 * @Short_description: Vignette post-processing effect
 *
 * #LrgVignette creates a radial darkening effect around the edges
 * of the screen. This effect is commonly used to:
 *
 * - Draw focus to the center of the screen
 * - Create a cinematic look
 * - Simulate camera lens effects
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgVignette *vignette = lrg_vignette_new ();
 * lrg_vignette_set_intensity (vignette, 0.5f);
 * lrg_vignette_set_radius (vignette, 0.7f);
 * lrg_vignette_set_smoothness (vignette, 0.3f);
 * lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (vignette));
 * ]|
 */

struct _LrgVignette
{
    LrgPostEffect  parent_instance;

    gfloat         intensity;
    gfloat         radius;
    gfloat         smoothness;
    gfloat         roundness;
    gfloat         color_r;
    gfloat         color_g;
    gfloat         color_b;
};

G_DEFINE_TYPE (LrgVignette, lrg_vignette, LRG_TYPE_POST_EFFECT)

enum
{
    PROP_0,
    PROP_INTENSITY,
    PROP_RADIUS,
    PROP_SMOOTHNESS,
    PROP_ROUNDNESS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_vignette_real_initialize (LrgPostEffect *effect,
                              guint          width,
                              guint          height,
                              GError       **error)
{
    /* No special resources needed for vignette */
    return TRUE;
}

static void
lrg_vignette_real_shutdown (LrgPostEffect *effect)
{
    /* Nothing to clean up */
}

static void
lrg_vignette_real_apply (LrgPostEffect *effect,
                         guint          source_texture_id,
                         guint          target_texture_id,
                         guint          width,
                         guint          height,
                         gfloat         delta_time)
{
    /*
     * Real implementation would use a shader like:
     *
     * #version 330
     * uniform sampler2D texture0;
     * uniform vec2 resolution;
     * uniform float intensity;
     * uniform float radius;
     * uniform float smoothness;
     * uniform float roundness;
     * uniform vec3 color;
     *
     * void main() {
     *     vec2 uv = gl_FragCoord.xy / resolution;
     *     vec2 center = vec2(0.5, 0.5);
     *     vec2 dist = uv - center;
     *
     *     // Apply roundness (mix between screen-aspect and circular)
     *     float aspect = resolution.x / resolution.y;
     *     dist.x *= mix(aspect, 1.0, roundness);
     *
     *     float d = length(dist);
     *     float vignette = smoothstep(radius, radius - smoothness, d);
     *     vignette = mix(1.0, vignette, intensity);
     *
     *     vec4 texColor = texture(texture0, uv);
     *     gl_FragColor = vec4(mix(color, texColor.rgb, vignette), texColor.a);
     * }
     */
    (void)effect;
    (void)source_texture_id;
    (void)target_texture_id;
    (void)width;
    (void)height;
    (void)delta_time;
}

static const gchar *
lrg_vignette_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "vignette";
}

static void
lrg_vignette_real_resize (LrgPostEffect *effect,
                          guint          width,
                          guint          height)
{
    /* No resize handling needed */
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_vignette_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgVignette *self = LRG_VIGNETTE (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        g_value_set_float (value, self->intensity);
        break;
    case PROP_RADIUS:
        g_value_set_float (value, self->radius);
        break;
    case PROP_SMOOTHNESS:
        g_value_set_float (value, self->smoothness);
        break;
    case PROP_ROUNDNESS:
        g_value_set_float (value, self->roundness);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vignette_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgVignette *self = LRG_VIGNETTE (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        lrg_vignette_set_intensity (self, g_value_get_float (value));
        break;
    case PROP_RADIUS:
        lrg_vignette_set_radius (self, g_value_get_float (value));
        break;
    case PROP_SMOOTHNESS:
        lrg_vignette_set_smoothness (self, g_value_get_float (value));
        break;
    case PROP_ROUNDNESS:
        lrg_vignette_set_roundness (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vignette_class_init (LrgVignetteClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->get_property = lrg_vignette_get_property;
    object_class->set_property = lrg_vignette_set_property;

    effect_class->initialize = lrg_vignette_real_initialize;
    effect_class->shutdown = lrg_vignette_real_shutdown;
    effect_class->apply = lrg_vignette_real_apply;
    effect_class->resize = lrg_vignette_real_resize;
    effect_class->get_name = lrg_vignette_real_get_name;

    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity",
                            "Intensity",
                            "Vignette intensity",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_RADIUS] =
        g_param_spec_float ("radius",
                            "Radius",
                            "Inner radius where vignette starts",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_SMOOTHNESS] =
        g_param_spec_float ("smoothness",
                            "Smoothness",
                            "Smoothness of the vignette edge",
                            0.0f, 1.0f, 0.3f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_ROUNDNESS] =
        g_param_spec_float ("roundness",
                            "Roundness",
                            "Roundness of the vignette shape",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_vignette_init (LrgVignette *self)
{
    self->intensity = 0.5f;
    self->radius = 0.5f;
    self->smoothness = 0.3f;
    self->roundness = 1.0f;
    self->color_r = 0.0f;
    self->color_g = 0.0f;
    self->color_b = 0.0f;
}

LrgVignette *
lrg_vignette_new (void)
{
    return g_object_new (LRG_TYPE_VIGNETTE, NULL);
}

gfloat
lrg_vignette_get_intensity (LrgVignette *self)
{
    g_return_val_if_fail (LRG_IS_VIGNETTE (self), 0.0f);

    return self->intensity;
}

void
lrg_vignette_set_intensity (LrgVignette *self,
                            gfloat       intensity)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    intensity = CLAMP (intensity, 0.0f, 1.0f);

    if (self->intensity == intensity)
        return;

    self->intensity = intensity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

gfloat
lrg_vignette_get_radius (LrgVignette *self)
{
    g_return_val_if_fail (LRG_IS_VIGNETTE (self), 0.0f);

    return self->radius;
}

void
lrg_vignette_set_radius (LrgVignette *self,
                         gfloat       radius)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    radius = CLAMP (radius, 0.0f, 1.0f);

    if (self->radius == radius)
        return;

    self->radius = radius;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
}

gfloat
lrg_vignette_get_smoothness (LrgVignette *self)
{
    g_return_val_if_fail (LRG_IS_VIGNETTE (self), 0.0f);

    return self->smoothness;
}

void
lrg_vignette_set_smoothness (LrgVignette *self,
                             gfloat       smoothness)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    smoothness = CLAMP (smoothness, 0.0f, 1.0f);

    if (self->smoothness == smoothness)
        return;

    self->smoothness = smoothness;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SMOOTHNESS]);
}

gfloat
lrg_vignette_get_roundness (LrgVignette *self)
{
    g_return_val_if_fail (LRG_IS_VIGNETTE (self), 0.0f);

    return self->roundness;
}

void
lrg_vignette_set_roundness (LrgVignette *self,
                            gfloat       roundness)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    roundness = CLAMP (roundness, 0.0f, 1.0f);

    if (self->roundness == roundness)
        return;

    self->roundness = roundness;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROUNDNESS]);
}

void
lrg_vignette_get_color (LrgVignette *self,
                        gfloat      *r,
                        gfloat      *g,
                        gfloat      *b)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    if (r != NULL)
        *r = self->color_r;
    if (g != NULL)
        *g = self->color_g;
    if (b != NULL)
        *b = self->color_b;
}

void
lrg_vignette_set_color (LrgVignette *self,
                        gfloat       r,
                        gfloat       g,
                        gfloat       b)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    self->color_r = CLAMP (r, 0.0f, 1.0f);
    self->color_g = CLAMP (g, 0.0f, 1.0f);
    self->color_b = CLAMP (b, 0.0f, 1.0f);
}
