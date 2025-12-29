/* lrg-film-grain.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-film-grain.h"

/**
 * SECTION:lrg-film-grain
 * @Title: LrgFilmGrain
 * @Short_description: Film grain noise overlay effect
 *
 * #LrgFilmGrain adds animated noise to simulate film grain.
 * This effect is useful for:
 *
 * - Creating a cinematic film look
 * - Hiding banding artifacts
 * - Adding visual texture
 */

struct _LrgFilmGrain
{
    LrgPostEffect  parent_instance;

    gfloat         intensity;
    gfloat         size;
    gfloat         speed;
    gboolean       colored;
    gfloat         luminance_response;
    gfloat         time;
};

G_DEFINE_TYPE (LrgFilmGrain, lrg_film_grain, LRG_TYPE_POST_EFFECT)

enum
{
    PROP_0,
    PROP_INTENSITY,
    PROP_SIZE,
    PROP_SPEED,
    PROP_COLORED,
    PROP_LUMINANCE_RESPONSE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_film_grain_real_initialize (LrgPostEffect *effect,
                                guint          width,
                                guint          height,
                                GError       **error)
{
    return TRUE;
}

static void
lrg_film_grain_real_shutdown (LrgPostEffect *effect)
{
    (void)effect;
}

static void
lrg_film_grain_real_apply (LrgPostEffect *effect,
                           guint          source_texture_id,
                           guint          target_texture_id,
                           guint          width,
                           guint          height,
                           gfloat         delta_time)
{
    /*
     * Shader example:
     *
     * float rand(vec2 co) {
     *     return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
     * }
     *
     * void main() {
     *     vec2 uv = gl_FragCoord.xy / resolution;
     *     vec4 color = texture(texture0, uv);
     *
     *     float lum = dot(color.rgb, vec3(0.299, 0.587, 0.114));
     *     float response = mix(1.0, 1.0 - lum, luminanceResponse);
     *
     *     vec2 grainUv = gl_FragCoord.xy / size;
     *     float noise;
     *     if (colored) {
     *         vec3 noiseRgb = vec3(
     *             rand(grainUv + time),
     *             rand(grainUv + time + 1.0),
     *             rand(grainUv + time + 2.0)
     *         );
     *         color.rgb += (noiseRgb - 0.5) * intensity * response;
     *     } else {
     *         noise = rand(grainUv + time);
     *         color.rgb += (noise - 0.5) * intensity * response;
     *     }
     *
     *     gl_FragColor = color;
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
lrg_film_grain_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "film-grain";
}

static void
lrg_film_grain_real_resize (LrgPostEffect *effect,
                            guint          width,
                            guint          height)
{
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_film_grain_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgFilmGrain *self = LRG_FILM_GRAIN (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        g_value_set_float (value, self->intensity);
        break;
    case PROP_SIZE:
        g_value_set_float (value, self->size);
        break;
    case PROP_SPEED:
        g_value_set_float (value, self->speed);
        break;
    case PROP_COLORED:
        g_value_set_boolean (value, self->colored);
        break;
    case PROP_LUMINANCE_RESPONSE:
        g_value_set_float (value, self->luminance_response);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_film_grain_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgFilmGrain *self = LRG_FILM_GRAIN (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        lrg_film_grain_set_intensity (self, g_value_get_float (value));
        break;
    case PROP_SIZE:
        lrg_film_grain_set_size (self, g_value_get_float (value));
        break;
    case PROP_SPEED:
        lrg_film_grain_set_speed (self, g_value_get_float (value));
        break;
    case PROP_COLORED:
        lrg_film_grain_set_colored (self, g_value_get_boolean (value));
        break;
    case PROP_LUMINANCE_RESPONSE:
        lrg_film_grain_set_luminance_response (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_film_grain_class_init (LrgFilmGrainClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->get_property = lrg_film_grain_get_property;
    object_class->set_property = lrg_film_grain_set_property;

    effect_class->initialize = lrg_film_grain_real_initialize;
    effect_class->shutdown = lrg_film_grain_real_shutdown;
    effect_class->apply = lrg_film_grain_real_apply;
    effect_class->resize = lrg_film_grain_real_resize;
    effect_class->get_name = lrg_film_grain_real_get_name;

    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity",
                            "Intensity",
                            "Grain intensity",
                            0.0f, 1.0f, 0.1f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_SIZE] =
        g_param_spec_float ("size",
                            "Size",
                            "Grain size",
                            1.0f, 5.0f, 1.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_SPEED] =
        g_param_spec_float ("speed",
                            "Speed",
                            "Animation speed",
                            0.0f, 10.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_COLORED] =
        g_param_spec_boolean ("colored",
                              "Colored",
                              "Use colored grain",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_LUMINANCE_RESPONSE] =
        g_param_spec_float ("luminance-response",
                            "Luminance Response",
                            "How much grain is affected by brightness",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_film_grain_init (LrgFilmGrain *self)
{
    self->intensity = 0.1f;
    self->size = 1.5f;
    self->speed = 1.0f;
    self->colored = FALSE;
    self->luminance_response = 0.5f;
    self->time = 0.0f;
}

LrgFilmGrain *
lrg_film_grain_new (void)
{
    return g_object_new (LRG_TYPE_FILM_GRAIN, NULL);
}

gfloat
lrg_film_grain_get_intensity (LrgFilmGrain *self)
{
    g_return_val_if_fail (LRG_IS_FILM_GRAIN (self), 0.0f);
    return self->intensity;
}

void
lrg_film_grain_set_intensity (LrgFilmGrain *self,
                              gfloat        intensity)
{
    g_return_if_fail (LRG_IS_FILM_GRAIN (self));

    intensity = CLAMP (intensity, 0.0f, 1.0f);
    if (self->intensity == intensity)
        return;

    self->intensity = intensity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

gfloat
lrg_film_grain_get_size (LrgFilmGrain *self)
{
    g_return_val_if_fail (LRG_IS_FILM_GRAIN (self), 1.0f);
    return self->size;
}

void
lrg_film_grain_set_size (LrgFilmGrain *self,
                         gfloat        size)
{
    g_return_if_fail (LRG_IS_FILM_GRAIN (self));

    size = CLAMP (size, 1.0f, 5.0f);
    if (self->size == size)
        return;

    self->size = size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SIZE]);
}

gfloat
lrg_film_grain_get_speed (LrgFilmGrain *self)
{
    g_return_val_if_fail (LRG_IS_FILM_GRAIN (self), 1.0f);
    return self->speed;
}

void
lrg_film_grain_set_speed (LrgFilmGrain *self,
                          gfloat        speed)
{
    g_return_if_fail (LRG_IS_FILM_GRAIN (self));

    speed = CLAMP (speed, 0.0f, 10.0f);
    if (self->speed == speed)
        return;

    self->speed = speed;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEED]);
}

gboolean
lrg_film_grain_get_colored (LrgFilmGrain *self)
{
    g_return_val_if_fail (LRG_IS_FILM_GRAIN (self), FALSE);
    return self->colored;
}

void
lrg_film_grain_set_colored (LrgFilmGrain *self,
                            gboolean      colored)
{
    g_return_if_fail (LRG_IS_FILM_GRAIN (self));

    colored = !!colored;
    if (self->colored == colored)
        return;

    self->colored = colored;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLORED]);
}

gfloat
lrg_film_grain_get_luminance_response (LrgFilmGrain *self)
{
    g_return_val_if_fail (LRG_IS_FILM_GRAIN (self), 0.5f);
    return self->luminance_response;
}

void
lrg_film_grain_set_luminance_response (LrgFilmGrain *self,
                                       gfloat        response)
{
    g_return_if_fail (LRG_IS_FILM_GRAIN (self));

    response = CLAMP (response, 0.0f, 1.0f);
    if (self->luminance_response == response)
        return;

    self->luminance_response = response;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LUMINANCE_RESPONSE]);
}

void
lrg_film_grain_update (LrgFilmGrain *self,
                       gfloat        delta_time)
{
    g_return_if_fail (LRG_IS_FILM_GRAIN (self));

    self->time += delta_time * self->speed;
}
