/* lrg-fxaa.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-fxaa.h"

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
};

G_DEFINE_TYPE (LrgFxaa, lrg_fxaa, LRG_TYPE_POST_EFFECT)

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
    (void)effect;
}

static void
lrg_fxaa_real_apply (LrgPostEffect *effect,
                     guint          source_texture_id,
                     guint          target_texture_id,
                     guint          width,
                     guint          height,
                     gfloat         delta_time)
{
    /*
     * FXAA shader (simplified):
     *
     * #define FXAA_SPAN_MAX 8.0
     * #define FXAA_REDUCE_MIN 1.0/128.0
     * #define FXAA_REDUCE_MUL 1.0/8.0
     *
     * float luma(vec3 color) {
     *     return dot(color, vec3(0.299, 0.587, 0.114));
     * }
     *
     * void main() {
     *     vec2 rcpFrame = 1.0 / resolution;
     *
     *     // Sample luminance of neighbors
     *     float lumaNW = luma(texture(tex, uv + vec2(-1, -1) * rcpFrame).rgb);
     *     float lumaNE = luma(texture(tex, uv + vec2( 1, -1) * rcpFrame).rgb);
     *     float lumaSW = luma(texture(tex, uv + vec2(-1,  1) * rcpFrame).rgb);
     *     float lumaSE = luma(texture(tex, uv + vec2( 1,  1) * rcpFrame).rgb);
     *     float lumaM  = luma(texture(tex, uv).rgb);
     *
     *     // Edge detection
     *     float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
     *     float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
     *     float lumaRange = lumaMax - lumaMin;
     *
     *     if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold)) {
     *         gl_FragColor = texture(tex, uv);
     *         return;
     *     }
     *
     *     // Calculate gradient direction and blend
     *     vec2 dir;
     *     dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
     *     dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
     *
     *     float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) *
     *                           (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
     *     float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
     *
     *     dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * rcpFrame;
     *
     *     // Blend samples
     *     vec3 rgbA = 0.5 * (texture(tex, uv + dir * (1.0/3.0 - 0.5)).rgb +
     *                        texture(tex, uv + dir * (2.0/3.0 - 0.5)).rgb);
     *     vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(tex, uv + dir * -0.5).rgb +
     *                                       texture(tex, uv + dir *  0.5).rgb);
     *
     *     float lumaB = luma(rgbB);
     *     if (lumaB < lumaMin || lumaB > lumaMax) {
     *         gl_FragColor = vec4(rgbA, 1.0);
     *     } else {
     *         gl_FragColor = vec4(rgbB, 1.0);
     *     }
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
