/* lrg-fxaa.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-fxaa.h"
#include <raylib.h>
#include <rlgl.h>
#include "core/lrg-prewarm.h"

/*
 * FXAA in one pass (the "console" variant plus a sub-pixel term):
 * - local contrast from the four diagonal neighbours and the centre; below
 *   max(edgeMin, lumaMax * edge) the pixel is left alone;
 * - the edge direction from the luma gradient, clamped to spanMax pixels
 *   (spanMax grows with LrgFxaa:quality), two- and four-tap blends along
 *   it, the four-tap one unless it leaves the local luma range;
 * - sub-pixel aliasing: a contrast-weighted blend towards the local
 *   average, scaled by LrgFxaa:subpixel-quality.
 * The result is mixed with the input by the base intensity.
 */
static const gchar fxaa_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 resolution;\n"
    "uniform vec4 params;\n"   /* edge, edgeMin, spanMax, subpixel */
    "uniform float intensity;\n"
    "out vec4 finalColor;\n"
    "float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    "void main()\n"
    "{\n"
    "    vec2 uv = fragTexCoord;\n"
    "    vec2 px = 1.0 / resolution;\n"
    "    vec4 centre = texture(texture0, uv);\n"
    "    vec3 rgb_nw = texture(texture0, uv + vec2(-1.0, -1.0) * px).rgb;\n"
    "    vec3 rgb_ne = texture(texture0, uv + vec2(1.0, -1.0) * px).rgb;\n"
    "    vec3 rgb_sw = texture(texture0, uv + vec2(-1.0, 1.0) * px).rgb;\n"
    "    vec3 rgb_se = texture(texture0, uv + vec2(1.0, 1.0) * px).rgb;\n"
    "    vec3 rgb_m = centre.rgb;\n"
    "    float l_nw = luma(rgb_nw);\n"
    "    float l_ne = luma(rgb_ne);\n"
    "    float l_sw = luma(rgb_sw);\n"
    "    float l_se = luma(rgb_se);\n"
    "    float l_m = luma(rgb_m);\n"
    "    float l_min = min(l_m, min(min(l_nw, l_ne), min(l_sw, l_se)));\n"
    "    float l_max = max(l_m, max(max(l_nw, l_ne), max(l_sw, l_se)));\n"
    "    float range = l_max - l_min;\n"
    "    vec2 dir;\n"
    "    float reduce;\n"
    "    float rcp;\n"
    "    vec3 a;\n"
    "    vec3 b;\n"
    "    vec3 aa;\n"
    "    vec3 avg;\n"
    "    float sub;\n"
    "    if (range < max(params.y, l_max * params.x))\n"
    "    {\n"
    "        finalColor = centre;\n"
    "        return;\n"
    "    }\n"
    "    dir.x = -((l_nw + l_ne) - (l_sw + l_se));\n"
    "    dir.y = ((l_nw + l_sw) - (l_ne + l_se));\n"
    "    reduce = max((l_nw + l_ne + l_sw + l_se) * 0.03125, 1.0 / 128.0);\n"
    "    rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);\n"
    "    dir = clamp(dir * rcp, vec2(-params.z), vec2(params.z)) * px;\n"
    "    a = 0.5 * (texture(texture0, uv + dir * (1.0 / 3.0 - 0.5)).rgb +\n"
    "               texture(texture0, uv + dir * (2.0 / 3.0 - 0.5)).rgb);\n"
    "    b = a * 0.5 + 0.25 * (texture(texture0, uv - dir * 0.5).rgb +\n"
    "                          texture(texture0, uv + dir * 0.5).rgb);\n"
    "    aa = (luma(b) < l_min || luma(b) > l_max) ? a : b;\n"
    "    avg = (rgb_nw + rgb_ne + rgb_sw + rgb_se + rgb_m * 4.0) * 0.125;\n"
    "    sub = clamp(abs(luma(avg) - l_m) / max(range, 1e-4), 0.0, 1.0);\n"
    "    sub = sub * sub * (3.0 - 2.0 * sub);\n"
    "    aa = mix(aa, avg, sub * sub * params.w * 0.75);\n"
    "    finalColor = vec4(mix(rgb_m, aa, intensity), centre.a);\n"
    "}\n";

/**
 * SECTION:lrg-fxaa
 * @Title: LrgFxaa
 * @Short_description: FXAA anti-aliasing effect
 *
 * #LrgFxaa implements Fast Approximate Anti-Aliasing, a
 * screen-space anti-aliasing technique that works on the
 * final rendered image.
 *
 * FXAA works by detecting edges based on local contrast
 * and blending them to reduce aliasing artifacts. It's
 * fast but can cause some blurring.
 */

struct _LrgFxaa
{
    LrgPostEffect  parent_instance;

    LrgFxaaQuality quality;
    gfloat         subpixel_quality;
    gfloat         edge_threshold;
    gfloat         edge_threshold_min;

    GrlShader     *shader;
    gboolean       shader_tried;
    gint           loc_resolution;
    gint           loc_params;
    gint           loc_intensity;
};

static void lrg_fxaa_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgFxaa, lrg_fxaa, LRG_TYPE_POST_EFFECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                lrg_fxaa_prewarmable_init))

enum
{
    PROP_0,
    PROP_QUALITY,
    PROP_SUBPIXEL_QUALITY,
    PROP_EDGE_THRESHOLD,
    PROP_EDGE_THRESHOLD_MIN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_fxaa_real_initialize (LrgPostEffect *effect,
                          guint          width,
                          guint          height,
                          GError       **error)
{
    return TRUE;
}

static void
lrg_fxaa_real_shutdown (LrgPostEffect *effect)
{
    LrgFxaa *self = LRG_FXAA (effect);

    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

/* Compiles the pass shader once (needs a window) */
static GrlShader *
fxaa_get_shader (LrgFxaa *self)
{
    g_autoptr(GError) error = NULL;

    if (self->shader != NULL || self->shader_tried || !IsWindowReady ())
        return self->shader;
    self->shader_tried = TRUE;
    self->shader = lrg_post_effect_compile_shader (fxaa_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("fxaa: %s", error->message);
        return NULL;
    }
    self->loc_resolution = grl_shader_get_location (self->shader, "resolution");
    self->loc_params = grl_shader_get_location (self->shader, "params");
    self->loc_intensity = grl_shader_get_location (self->shader, "intensity");
    return self->shader;
}

/* How far (pixels) the edge blend may reach for each quality */
static gfloat
fxaa_span_max (LrgFxaaQuality quality)
{
    switch (quality)
    {
    case LRG_FXAA_QUALITY_LOW:
        return 4.0f;
    case LRG_FXAA_QUALITY_MEDIUM:
        return 6.0f;
    case LRG_FXAA_QUALITY_HIGH:
        return 8.0f;
    case LRG_FXAA_QUALITY_ULTRA:
    default:
        return 12.0f;
    }
}

static void
lrg_fxaa_real_apply (LrgPostEffect *effect,
                     guint          source_texture_id,
                     guint          target_texture_id,
                     guint          width,
                     guint          height,
                     gfloat         delta_time)
{
    LrgFxaa *self = LRG_FXAA (effect);
    GrlShader *shader;
    gfloat intensity;

    (void)target_texture_id;
    (void)delta_time;

    intensity = lrg_post_effect_get_intensity (effect);
    shader = intensity > 0.0f ? fxaa_get_shader (self) : NULL;
    if (shader != NULL)
    {
        grl_shader_set_value_vec2 (shader, self->loc_resolution, (gfloat)width, (gfloat)height);
        grl_shader_set_value_vec4 (shader, self->loc_params, self->edge_threshold,
                                   self->edge_threshold_min, fxaa_span_max (self->quality),
                                   self->subpixel_quality);
        grl_shader_set_value_float (shader, self->loc_intensity, intensity);
        grl_shader_begin (shader);
    }
    lrg_post_effect_draw_fullscreen (source_texture_id, width, height);
    if (shader != NULL)
        grl_shader_end (shader);
}

/* lrg_fxaa_prewarm: compiles the pass shader ahead of the first frame */
static gboolean
lrg_fxaa_prewarm (LrgPrewarmable *prewarmable)
{
    fxaa_get_shader (LRG_FXAA (prewarmable));
    return TRUE;
}

static void
lrg_fxaa_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_fxaa_prewarm;
}

static const gchar *
lrg_fxaa_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "fxaa";
}

static void
lrg_fxaa_real_resize (LrgPostEffect *effect,
                      guint          width,
                      guint          height)
{
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_fxaa_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LrgFxaa *self = LRG_FXAA (object);

    switch (prop_id)
    {
    case PROP_QUALITY:
        g_value_set_int (value, self->quality);
        break;
    case PROP_SUBPIXEL_QUALITY:
        g_value_set_float (value, self->subpixel_quality);
        break;
    case PROP_EDGE_THRESHOLD:
        g_value_set_float (value, self->edge_threshold);
        break;
    case PROP_EDGE_THRESHOLD_MIN:
        g_value_set_float (value, self->edge_threshold_min);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_fxaa_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LrgFxaa *self = LRG_FXAA (object);

    switch (prop_id)
    {
    case PROP_QUALITY:
        lrg_fxaa_set_quality (self, g_value_get_int (value));
        break;
    case PROP_SUBPIXEL_QUALITY:
        lrg_fxaa_set_subpixel_quality (self, g_value_get_float (value));
        break;
    case PROP_EDGE_THRESHOLD:
        lrg_fxaa_set_edge_threshold (self, g_value_get_float (value));
        break;
    case PROP_EDGE_THRESHOLD_MIN:
        lrg_fxaa_set_edge_threshold_min (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_fxaa_class_init (LrgFxaaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->get_property = lrg_fxaa_get_property;
    object_class->set_property = lrg_fxaa_set_property;

    effect_class->initialize = lrg_fxaa_real_initialize;
    effect_class->shutdown = lrg_fxaa_real_shutdown;
    effect_class->apply = lrg_fxaa_real_apply;
    effect_class->resize = lrg_fxaa_real_resize;
    effect_class->get_name = lrg_fxaa_real_get_name;

    properties[PROP_QUALITY] =
        g_param_spec_int ("quality",
                          "Quality",
                          "FXAA quality preset",
                          LRG_FXAA_QUALITY_LOW,
                          LRG_FXAA_QUALITY_ULTRA,
                          LRG_FXAA_QUALITY_MEDIUM,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_SUBPIXEL_QUALITY] =
        g_param_spec_float ("subpixel-quality",
                            "Subpixel Quality",
                            "Subpixel anti-aliasing quality",
                            0.0f, 1.0f, 0.75f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_EDGE_THRESHOLD] =
        g_param_spec_float ("edge-threshold",
                            "Edge Threshold",
                            "Edge detection threshold",
                            0.0f, 0.5f, 0.166f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_EDGE_THRESHOLD_MIN] =
        g_param_spec_float ("edge-threshold-min",
                            "Edge Threshold Min",
                            "Minimum edge threshold",
                            0.0f, 0.1f, 0.0833f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_fxaa_init (LrgFxaa *self)
{
    self->quality = LRG_FXAA_QUALITY_MEDIUM;
    self->subpixel_quality = 0.75f;
    self->edge_threshold = 0.166f;
    self->edge_threshold_min = 0.0833f;

    /* compile ahead of the first frame when a loading screen prewarms */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgFxaa *
lrg_fxaa_new (void)
{
    return g_object_new (LRG_TYPE_FXAA, NULL);
}

LrgFxaaQuality
lrg_fxaa_get_quality (LrgFxaa *self)
{
    g_return_val_if_fail (LRG_IS_FXAA (self), LRG_FXAA_QUALITY_MEDIUM);
    return self->quality;
}

void
lrg_fxaa_set_quality (LrgFxaa        *self,
                      LrgFxaaQuality  quality)
{
    g_return_if_fail (LRG_IS_FXAA (self));

    if (self->quality == quality)
        return;

    self->quality = quality;

    /* Update thresholds based on quality preset */
    switch (quality)
    {
    case LRG_FXAA_QUALITY_LOW:
        self->subpixel_quality = 0.5f;
        self->edge_threshold = 0.25f;
        self->edge_threshold_min = 0.0833f;
        break;
    case LRG_FXAA_QUALITY_MEDIUM:
        self->subpixel_quality = 0.75f;
        self->edge_threshold = 0.166f;
        self->edge_threshold_min = 0.0625f;
        break;
    case LRG_FXAA_QUALITY_HIGH:
        self->subpixel_quality = 0.75f;
        self->edge_threshold = 0.125f;
        self->edge_threshold_min = 0.0312f;
        break;
    case LRG_FXAA_QUALITY_ULTRA:
        self->subpixel_quality = 1.0f;
        self->edge_threshold = 0.063f;
        self->edge_threshold_min = 0.0312f;
        break;
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY]);
}

gfloat
lrg_fxaa_get_subpixel_quality (LrgFxaa *self)
{
    g_return_val_if_fail (LRG_IS_FXAA (self), 0.75f);
    return self->subpixel_quality;
}

void
lrg_fxaa_set_subpixel_quality (LrgFxaa *self,
                               gfloat   quality)
{
    g_return_if_fail (LRG_IS_FXAA (self));

    quality = CLAMP (quality, 0.0f, 1.0f);
    if (self->subpixel_quality == quality)
        return;

    self->subpixel_quality = quality;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBPIXEL_QUALITY]);
}

gfloat
lrg_fxaa_get_edge_threshold (LrgFxaa *self)
{
    g_return_val_if_fail (LRG_IS_FXAA (self), 0.166f);
    return self->edge_threshold;
}

void
lrg_fxaa_set_edge_threshold (LrgFxaa *self,
                             gfloat   threshold)
{
    g_return_if_fail (LRG_IS_FXAA (self));

    threshold = CLAMP (threshold, 0.0f, 0.5f);
    if (self->edge_threshold == threshold)
        return;

    self->edge_threshold = threshold;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDGE_THRESHOLD]);
}

gfloat
lrg_fxaa_get_edge_threshold_min (LrgFxaa *self)
{
    g_return_val_if_fail (LRG_IS_FXAA (self), 0.0833f);
    return self->edge_threshold_min;
}

void
lrg_fxaa_set_edge_threshold_min (LrgFxaa *self,
                                 gfloat   threshold)
{
    g_return_if_fail (LRG_IS_FXAA (self));

    threshold = CLAMP (threshold, 0.0f, 0.1f);
    if (self->edge_threshold_min == threshold)
        return;

    self->edge_threshold_min = threshold;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDGE_THRESHOLD_MIN]);
}
