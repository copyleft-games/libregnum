/* lrg-film-grain.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-film-grain.h"
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include "core/lrg-prewarm.h"

/*
 * Film grain in one pass. The screen is split into cells of @size pixels;
 * each cell gets white noise from an integer hash of (cell, frame), where
 * the frame advances 24 times per second of grain time (time * speed), so
 * grain flickers like film rather than crawling. Monochrome grain adds
 * the same value to r, g and b; coloured grain hashes each channel. The
 * amount is intensity * response, response = mix(1, 1 - luma,
 * luminance-response): bright areas get less grain, as on real stock.
 */
static const gchar grain_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 params;\n"   /* amount, size, frame, luminance response */
    "uniform int colored;\n"
    "out vec4 finalColor;\n"
    "uint hash(uint x)\n"
    "{\n"
    "    x ^= x >> 16u; x *= 0x7feb352du;\n"
    "    x ^= x >> 15u; x *= 0x846ca68bu;\n"
    "    x ^= x >> 16u;\n"
    "    return x;\n"
    "}\n"
    "float noise(uvec2 cell, uint frame, uint channel)\n"
    "{\n"
    "    uint h = hash(cell.x + hash(cell.y + hash(frame * 3u + channel)));\n"
    "    return float(h & 0xffffu) / 65535.0 - 0.5;\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    vec4 c = texture(texture0, fragTexCoord);\n"
    "    uvec2 cell = uvec2(gl_FragCoord.xy / max(params.y, 1.0));\n"
    "    uint frame = uint(params.z);\n"
    "    float lum = clamp(dot(c.rgb, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);\n"
    "    float amount = params.x * mix(1.0, 1.0 - lum, params.w);\n"
    "    vec3 n;\n"
    "    if (colored != 0)\n"
    "        n = vec3(noise(cell, frame, 0u), noise(cell, frame, 1u), noise(cell, frame, 2u));\n"
    "    else\n"
    "        n = vec3(noise(cell, frame, 0u));\n"
    "    finalColor = vec4(max(c.rgb + n * amount, 0.0), c.a);\n"
    "}\n";

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

    GrlShader     *shader;
    gboolean       shader_tried;
    gint           loc_params;
    gint           loc_colored;
};

static void lrg_film_grain_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgFilmGrain, lrg_film_grain, LRG_TYPE_POST_EFFECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                lrg_film_grain_prewarmable_init))

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
    LrgFilmGrain *self = LRG_FILM_GRAIN (effect);

    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

/* Compiles the pass shader once (needs a window) */
static GrlShader *
grain_get_shader (LrgFilmGrain *self)
{
    g_autoptr(GError) error = NULL;

    if (self->shader != NULL || self->shader_tried || !IsWindowReady ())
        return self->shader;
    self->shader_tried = TRUE;
    self->shader = lrg_post_effect_compile_shader (grain_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("film grain: %s", error->message);
        return NULL;
    }
    self->loc_params = grl_shader_get_location (self->shader, "params");
    self->loc_colored = grl_shader_get_location (self->shader, "colored");
    return self->shader;
}

static void
lrg_film_grain_real_apply (LrgPostEffect *effect,
                           guint          source_texture_id,
                           guint          target_texture_id,
                           guint          width,
                           guint          height,
                           gfloat         delta_time)
{
    LrgFilmGrain *self = LRG_FILM_GRAIN (effect);
    GrlShader *shader;
    gfloat amount;

    (void)target_texture_id;

    if (isfinite (delta_time) && delta_time > 0.0f)
        lrg_film_grain_update (self, delta_time);

    amount = self->intensity * lrg_post_effect_get_intensity (effect);
    shader = amount > 0.0f ? grain_get_shader (self) : NULL;
    if (shader != NULL)
    {
        /* 24 grain frames per second; wrap well inside float precision */
        grl_shader_set_value_vec4 (shader, self->loc_params, amount, self->size,
                                   fmodf (floorf (self->time * 24.0f), 65536.0f),
                                   self->luminance_response);
        grl_shader_set_value_int (shader, self->loc_colored, self->colored ? 1 : 0);
        grl_shader_begin (shader);
    }
    lrg_post_effect_draw_fullscreen (source_texture_id, width, height);
    if (shader != NULL)
        grl_shader_end (shader);
}

/* lrg_film_grain_prewarm: compiles the pass shader ahead of the first frame */
static gboolean
lrg_film_grain_prewarm (LrgPrewarmable *prewarmable)
{
    grain_get_shader (LRG_FILM_GRAIN (prewarmable));
    return TRUE;
}

static void
lrg_film_grain_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_film_grain_prewarm;
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

    /* compile ahead of the first frame when a loading screen prewarms */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
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
