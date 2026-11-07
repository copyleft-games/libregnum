/* lrg-bloom.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-bloom.h"
#include <string.h>
#include <raylib.h>
#include <rlgl.h>
#include "core/lrg-prewarm.h"

#define BLOOM_MAX_LEVELS (8)

/*
 * Bloom as a mip chain (the "progressive downsample / upsample" bloom):
 *
 *  1. prefilter  source -> level 0 (half resolution, quarter at the low
 *                tier): four bilinear taps averaged, then the soft-knee
 *                threshold keeps only what is brighter than @threshold;
 *  2. down       level i-1 -> level i (half again): centre x4 + four
 *                diagonal taps, /8, for each further level;
 *  3. up         level i -> level i-1, added (additive blending) through a
 *                9-tap tent whose radius is blur-size / 4 texels, from the
 *                smallest level back to level 0, so wide and tight glows
 *                add up;
 *  4. composite  source + level0 / levels * intensity * tint, into the
 *                target the processor bound.
 *
 * Levels = iterations, capped by the quality tier (3/4/6/8) and by the
 * image size. Targets are RGBA16F when the driver can render to them.
 */
static const gchar bloom_prefilter_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 texel;\n"
    "uniform vec2 thresh;\n"   /* threshold, soft knee */
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec2 uv = fragTexCoord;\n"
    "    vec3 c = 0.25 * (texture(texture0, uv + vec2(-0.5, -0.5) * texel).rgb +\n"
    "                     texture(texture0, uv + vec2(0.5, -0.5) * texel).rgb +\n"
    "                     texture(texture0, uv + vec2(-0.5, 0.5) * texel).rgb +\n"
    "                     texture(texture0, uv + vec2(0.5, 0.5) * texel).rgb);\n"
    "    float l = max(c.r, max(c.g, c.b));\n"
    "    float knee = max(thresh.x * thresh.y, 0.0001);\n"
    "    float soft = clamp(l - thresh.x + knee, 0.0, 2.0 * knee);\n"
    "    float w;\n"
    "    soft = soft * soft / (4.0 * knee);\n"
    "    w = max(soft, l - thresh.x) / max(l, 0.0001);\n"
    "    finalColor = vec4(c * w, 1.0);\n"
    "}\n";

static const gchar bloom_down_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 texel;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec2 uv = fragTexCoord;\n"
    "    vec3 c = texture(texture0, uv).rgb * 4.0;\n"
    "    c += texture(texture0, uv + vec2(-1.0, -1.0) * texel).rgb;\n"
    "    c += texture(texture0, uv + vec2(1.0, -1.0) * texel).rgb;\n"
    "    c += texture(texture0, uv + vec2(-1.0, 1.0) * texel).rgb;\n"
    "    c += texture(texture0, uv + vec2(1.0, 1.0) * texel).rgb;\n"
    "    finalColor = vec4(c * 0.125, 1.0);\n"
    "}\n";

static const gchar bloom_up_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 texel;\n"
    "uniform float radius;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec2 uv = fragTexCoord;\n"
    "    vec2 d = texel * radius;\n"
    "    vec3 c = texture(texture0, uv).rgb * 4.0;\n"
    "    c += (texture(texture0, uv + vec2(-d.x, 0.0)).rgb + texture(texture0, uv + vec2(d.x, 0.0)).rgb +\n"
    "          texture(texture0, uv + vec2(0.0, -d.y)).rgb + texture(texture0, uv + vec2(0.0, d.y)).rgb) * 2.0;\n"
    "    c += texture(texture0, uv - d).rgb + texture(texture0, uv + d).rgb +\n"
    "         texture(texture0, uv + vec2(-d.x, d.y)).rgb + texture(texture0, uv + vec2(d.x, -d.y)).rgb;\n"
    "    finalColor = vec4(c / 16.0, 1.0);\n"
    "}\n";

static const gchar bloom_composite_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D bloomMap;\n"
    "uniform vec3 tint;\n"     /* already scaled by intensity / levels */
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 c = texture(texture0, fragTexCoord);\n"
    "    vec3 b = texture(bloomMap, fragTexCoord).rgb;\n"
    "    finalColor = vec4(c.rgb + b * tint, c.a);\n"
    "}\n";

enum
{
    BLOOM_PASS_PREFILTER,
    BLOOM_PASS_DOWN,
    BLOOM_PASS_UP,
    BLOOM_PASS_COMPOSITE,
    N_BLOOM_PASSES
};

/**
 * SECTION:lrg-bloom
 * @Title: LrgBloom
 * @Short_description: Bloom post-processing effect
 *
 * #LrgBloom creates a glow effect around bright areas of the image.
 * This is achieved through:
 *
 * 1. Brightness thresholding to extract bright pixels
 * 2. Gaussian blur to create the glow
 * 3. Additive blending with the original image
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgBloom *bloom = lrg_bloom_new ();
 * lrg_bloom_set_threshold (bloom, 0.8f);
 * lrg_bloom_set_intensity (bloom, 1.5f);
 * lrg_bloom_set_blur_size (bloom, 5.0f);
 * lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (bloom));
 * ]|
 */

struct _LrgBloom
{
    LrgPostEffect  parent_instance;

    /* Threshold settings */
    gfloat         threshold;
    gfloat         soft_knee;

    /* Blur settings */
    gfloat         blur_size;
    guint          iterations;

    /* Output settings */
    gfloat         intensity;
    gfloat         tint_r;
    gfloat         tint_g;
    gfloat         tint_b;

    /* GPU: one shader per pass, the mip chain */
    GrlShader      *shaders[N_BLOOM_PASSES];
    gboolean        shaders_tried;
    gint            loc_texel[N_BLOOM_PASSES];
    gint            loc_thresh;
    gint            loc_radius;
    gint            loc_tint;
    gint            loc_bloom_map;
    RenderTexture2D mips[BLOOM_MAX_LEVELS];
    guint           mip_count;
    guint           chain_width;     /* source size the chain was built for */
    guint           chain_height;
    LrgPostQuality  chain_quality;
    guint           chain_levels;
};

static void lrg_bloom_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgBloom, lrg_bloom, LRG_TYPE_POST_EFFECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                lrg_bloom_prewarmable_init))

enum
{
    PROP_0,
    PROP_THRESHOLD,
    PROP_INTENSITY,
    PROP_BLUR_SIZE,
    PROP_ITERATIONS,
    PROP_SOFT_KNEE,
    PROP_TINT_R,
    PROP_TINT_G,
    PROP_TINT_B,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_bloom_real_initialize (LrgPostEffect *effect,
                           guint          width,
                           guint          height,
                           GError       **error)
{
    /* GPU resources are built lazily on the first apply */
    (void)effect;
    (void)width;
    (void)height;
    (void)error;
    return TRUE;
}

static void
free_chain (LrgBloom *self)
{
    guint i;

    for (i = 0; i < self->mip_count; i++)
        if (self->mips[i].id != 0 && IsWindowReady ())
            UnloadRenderTexture (self->mips[i]);
    memset (self->mips, 0, sizeof self->mips);
    self->mip_count = 0;
    self->chain_width = 0;
    self->chain_height = 0;
}

/* Levels for the tier, capped by the iterations property */
static guint
bloom_levels (LrgBloom *self)
{
    static const guint caps[] = { 3, 4, 6, 8 };
    LrgPostQuality q = lrg_post_effect_get_quality_tier (LRG_POST_EFFECT (self));

    return MIN (self->iterations, caps[CLAMP ((gint)q, 0, 3)]);
}

static gboolean
load_mip (RenderTexture2D *rt,
          guint            w,
          guint            h)
{
    gint fmt;

    for (fmt = 0; fmt < 2; fmt++)
    {
        *rt = LoadRenderTextureEx ((gint)w, (gint)h,
                                   fmt == 0 ? PIXELFORMAT_UNCOMPRESSED_R16G16B16A16
                                            : PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        if (rt->id != 0 && rt->texture.id != 0 && rlFramebufferComplete (rt->id))
        {
            SetTextureFilter (rt->texture, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap (rt->texture, TEXTURE_WRAP_CLAMP);
            return TRUE;
        }
        if (rt->id != 0 || rt->texture.id != 0)
            UnloadRenderTexture (*rt);
        memset (rt, 0, sizeof *rt);
    }
    return FALSE;
}

/* (Re)builds the chain for a @width x @height source. Creating render
 * textures rebinds framebuffers: the caller rebinds its target after. */
static gboolean
ensure_chain (LrgBloom *self,
              guint     width,
              guint     height)
{
    LrgPostQuality q = lrg_post_effect_get_quality_tier (LRG_POST_EFFECT (self));
    guint levels = bloom_levels (self);
    guint w, h, i;

    if (self->mip_count > 0 && self->chain_width == width && self->chain_height == height &&
        self->chain_quality == q && self->chain_levels == levels)
        return TRUE;

    rlDrawRenderBatchActive ();
    free_chain (self);
    w = width / (q == LRG_POST_QUALITY_LOW ? 4u : 2u);
    h = height / (q == LRG_POST_QUALITY_LOW ? 4u : 2u);
    for (i = 0; i < levels && w >= 2 && h >= 2; i++)
    {
        if (!load_mip (&self->mips[i], w, h))
            break;
        self->mip_count = i + 1;
        w /= 2;
        h /= 2;
    }
    self->chain_width = width;
    self->chain_height = height;
    self->chain_quality = q;
    self->chain_levels = levels;
    return self->mip_count > 0;
}

static gboolean
ensure_shaders (LrgBloom *self)
{
    static const gchar *sources[N_BLOOM_PASSES] = {
        bloom_prefilter_fs, bloom_down_fs, bloom_up_fs, bloom_composite_fs
    };
    gint i;

    if (self->shaders_tried || !IsWindowReady ())
        return self->shaders[BLOOM_PASS_COMPOSITE] != NULL;
    self->shaders_tried = TRUE;
    for (i = 0; i < N_BLOOM_PASSES; i++)
    {
        g_autoptr(GError) error = NULL;

        self->shaders[i] = lrg_post_effect_compile_shader (sources[i], &error);
        if (self->shaders[i] == NULL)
        {
            g_warning ("bloom: pass %d: %s", i, error->message);
            for (i = 0; i < N_BLOOM_PASSES; i++)
                g_clear_object (&self->shaders[i]);
            return FALSE;
        }
        self->loc_texel[i] = grl_shader_get_location (self->shaders[i], "texel");
    }
    self->loc_thresh = grl_shader_get_location (self->shaders[BLOOM_PASS_PREFILTER], "thresh");
    self->loc_radius = grl_shader_get_location (self->shaders[BLOOM_PASS_UP], "radius");
    self->loc_tint = grl_shader_get_location (self->shaders[BLOOM_PASS_COMPOSITE], "tint");
    self->loc_bloom_map = grl_shader_get_location (self->shaders[BLOOM_PASS_COMPOSITE], "bloomMap");
    return TRUE;
}

/* One pass: @src (sized @sw x @sh) through @shader into @dst */
static void
run_pass (GrlShader       *shader,
          gint             loc_texel,
          guint            src,
          guint            sw,
          guint            sh,
          RenderTexture2D *dst)
{
    lrg_post_effect_bind_framebuffer (dst->id, (guint)dst->texture.width, (guint)dst->texture.height);
    grl_shader_set_value_vec2 (shader, loc_texel, 1.0f / (gfloat)MAX (sw, 1u), 1.0f / (gfloat)MAX (sh, 1u));
    grl_shader_begin (shader);
    lrg_post_effect_draw_fullscreen (src, (guint)dst->texture.width, (guint)dst->texture.height);
    grl_shader_end (shader);
}

static void
lrg_bloom_real_shutdown (LrgPostEffect *effect)
{
    LrgBloom *self = LRG_BLOOM (effect);
    gint i;

    free_chain (self);
    for (i = 0; i < N_BLOOM_PASSES; i++)
        g_clear_object (&self->shaders[i]);
    self->shaders_tried = FALSE;
}

static void
lrg_bloom_real_apply (LrgPostEffect *effect,
                      guint          source_texture_id,
                      guint          target_texture_id,
                      guint          width,
                      guint          height,
                      gfloat         delta_time)
{
    LrgBloom *self = LRG_BLOOM (effect);
    GrlShader *composite;
    Shader *handle;
    Texture2D bloom_tex;
    guint target_fbo;
    gfloat k;
    gint i;

    (void)target_texture_id;
    (void)delta_time;

    k = self->intensity * lrg_post_effect_get_intensity (effect);
    if (k <= 0.0f || !ensure_shaders (self))
    {
        lrg_post_effect_draw_fullscreen (source_texture_id, width, height);
        return;
    }

    /* The processor's target, to come back to after the chain */
    rlDrawRenderBatchActive ();
    target_fbo = rlGetActiveFramebuffer ();
    if (!ensure_chain (self, width, height))
    {
        lrg_post_effect_bind_framebuffer (target_fbo, width, height);
        lrg_post_effect_draw_fullscreen (source_texture_id, width, height);
        return;
    }

    /* 1. prefilter, 2. downsample (blending stays off, as on entry) */
    grl_shader_set_value_vec2 (self->shaders[BLOOM_PASS_PREFILTER], self->loc_thresh,
                               self->threshold, self->soft_knee);
    run_pass (self->shaders[BLOOM_PASS_PREFILTER], self->loc_texel[BLOOM_PASS_PREFILTER],
              source_texture_id, width, height, &self->mips[0]);
    for (i = 1; i < (gint)self->mip_count; i++)
        run_pass (self->shaders[BLOOM_PASS_DOWN], self->loc_texel[BLOOM_PASS_DOWN],
                  self->mips[i - 1].texture.id, (guint)self->mips[i - 1].texture.width,
                  (guint)self->mips[i - 1].texture.height, &self->mips[i]);

    /* 3. upsample, added onto each larger level */
    if (self->mip_count > 1)
    {
        grl_shader_set_value_float (self->shaders[BLOOM_PASS_UP], self->loc_radius,
                                    self->blur_size * 0.25f);
        rlDrawRenderBatchActive ();
        rlEnableColorBlend ();
        BeginBlendMode (BLEND_ADDITIVE);
        for (i = (gint)self->mip_count - 1; i >= 1; i--)
            run_pass (self->shaders[BLOOM_PASS_UP], self->loc_texel[BLOOM_PASS_UP],
                      self->mips[i].texture.id, (guint)self->mips[i].texture.width,
                      (guint)self->mips[i].texture.height, &self->mips[i - 1]);
        EndBlendMode ();
        rlDisableColorBlend ();
    }

    /* 4. composite into the processor's target */
    lrg_post_effect_bind_framebuffer (target_fbo, width, height);
    composite = self->shaders[BLOOM_PASS_COMPOSITE];
    handle = grl_shader_get_handle (composite);
    memset (&bloom_tex, 0, sizeof bloom_tex);
    bloom_tex.id = self->mips[0].texture.id;
    bloom_tex.width = self->mips[0].texture.width;
    bloom_tex.height = self->mips[0].texture.height;
    bloom_tex.mipmaps = 1;
    bloom_tex.format = self->mips[0].texture.format;
    k /= (gfloat)self->mip_count;
    grl_shader_set_value_vec3 (composite, self->loc_tint, self->tint_r * k, self->tint_g * k,
                               self->tint_b * k);
    grl_shader_begin (composite);
    SetShaderValueTexture (*handle, self->loc_bloom_map, bloom_tex);
    lrg_post_effect_draw_fullscreen (source_texture_id, width, height);
    grl_shader_end (composite);
}

/* lrg_bloom_prewarm: compiles the four pass shaders ahead of the first frame */
static gboolean
lrg_bloom_prewarm (LrgPrewarmable *prewarmable)
{
    ensure_shaders (LRG_BLOOM (prewarmable));
    return TRUE;
}

static void
lrg_bloom_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_bloom_prewarm;
}

static const gchar *
lrg_bloom_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "bloom";
}

static void
lrg_bloom_real_resize (LrgPostEffect *effect,
                       guint          width,
                       guint          height)
{
    /* the chain follows the source size on the next apply */
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_bloom_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    LrgBloom *self = LRG_BLOOM (object);

    switch (prop_id)
    {
    case PROP_THRESHOLD:
        g_value_set_float (value, self->threshold);
        break;
    case PROP_INTENSITY:
        g_value_set_float (value, self->intensity);
        break;
    case PROP_BLUR_SIZE:
        g_value_set_float (value, self->blur_size);
        break;
    case PROP_ITERATIONS:
        g_value_set_uint (value, self->iterations);
        break;
    case PROP_SOFT_KNEE:
        g_value_set_float (value, self->soft_knee);
        break;
    case PROP_TINT_R:
        g_value_set_float (value, self->tint_r);
        break;
    case PROP_TINT_G:
        g_value_set_float (value, self->tint_g);
        break;
    case PROP_TINT_B:
        g_value_set_float (value, self->tint_b);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_bloom_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    LrgBloom *self = LRG_BLOOM (object);

    switch (prop_id)
    {
    case PROP_THRESHOLD:
        lrg_bloom_set_threshold (self, g_value_get_float (value));
        break;
    case PROP_INTENSITY:
        lrg_bloom_set_intensity (self, g_value_get_float (value));
        break;
    case PROP_BLUR_SIZE:
        lrg_bloom_set_blur_size (self, g_value_get_float (value));
        break;
    case PROP_ITERATIONS:
        lrg_bloom_set_iterations (self, g_value_get_uint (value));
        break;
    case PROP_SOFT_KNEE:
        lrg_bloom_set_soft_knee (self, g_value_get_float (value));
        break;
    case PROP_TINT_R:
        lrg_bloom_set_tint (self, g_value_get_float (value), self->tint_g, self->tint_b);
        break;
    case PROP_TINT_G:
        lrg_bloom_set_tint (self, self->tint_r, g_value_get_float (value), self->tint_b);
        break;
    case PROP_TINT_B:
        lrg_bloom_set_tint (self, self->tint_r, self->tint_g, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_bloom_class_init (LrgBloomClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->get_property = lrg_bloom_get_property;
    object_class->set_property = lrg_bloom_set_property;

    effect_class->initialize = lrg_bloom_real_initialize;
    effect_class->shutdown = lrg_bloom_real_shutdown;
    effect_class->apply = lrg_bloom_real_apply;
    effect_class->resize = lrg_bloom_real_resize;
    effect_class->get_name = lrg_bloom_real_get_name;

    properties[PROP_THRESHOLD] =
        g_param_spec_float ("threshold",
                            "Threshold",
                            "Brightness threshold for bloom extraction",
                            0.0f, 10.0f, 0.8f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity",
                            "Intensity",
                            "Bloom intensity multiplier",
                            0.0f, 5.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_BLUR_SIZE] =
        g_param_spec_float ("blur-size",
                            "Blur Size",
                            "Blur kernel size",
                            1.0f, 20.0f, 4.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_ITERATIONS] =
        g_param_spec_uint ("iterations",
                           "Iterations",
                           "Number of blur passes",
                           1, 8, 3,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_SOFT_KNEE] =
        g_param_spec_float ("soft-knee",
                            "Soft Knee",
                            "Soft transition around threshold",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgBloom:tint-r:
     *
     * Red multiplier of the glow (0..1).
     */
    properties[PROP_TINT_R] =
        g_param_spec_float ("tint-r", "Tint R", "Glow red multiplier", 0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgBloom:tint-g:
     *
     * Green multiplier of the glow (0..1).
     */
    properties[PROP_TINT_G] =
        g_param_spec_float ("tint-g", "Tint G", "Glow green multiplier", 0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgBloom:tint-b:
     *
     * Blue multiplier of the glow (0..1).
     */
    properties[PROP_TINT_B] =
        g_param_spec_float ("tint-b", "Tint B", "Glow blue multiplier", 0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_bloom_init (LrgBloom *self)
{
    self->threshold = 0.8f;
    self->soft_knee = 0.5f;
    self->blur_size = 4.0f;
    self->iterations = 3;
    self->intensity = 1.0f;
    self->tint_r = 1.0f;
    self->tint_g = 1.0f;
    self->tint_b = 1.0f;

    /* compile ahead of the first frame when a loading screen prewarms */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgBloom *
lrg_bloom_new (void)
{
    return g_object_new (LRG_TYPE_BLOOM, NULL);
}

gfloat
lrg_bloom_get_threshold (LrgBloom *self)
{
    g_return_val_if_fail (LRG_IS_BLOOM (self), 0.0f);

    return self->threshold;
}

void
lrg_bloom_set_threshold (LrgBloom *self,
                         gfloat    threshold)
{
    g_return_if_fail (LRG_IS_BLOOM (self));

    threshold = CLAMP (threshold, 0.0f, 10.0f);

    if (self->threshold == threshold)
        return;

    self->threshold = threshold;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THRESHOLD]);
}

gfloat
lrg_bloom_get_intensity (LrgBloom *self)
{
    g_return_val_if_fail (LRG_IS_BLOOM (self), 0.0f);

    return self->intensity;
}

void
lrg_bloom_set_intensity (LrgBloom *self,
                         gfloat    intensity)
{
    g_return_if_fail (LRG_IS_BLOOM (self));

    intensity = CLAMP (intensity, 0.0f, 5.0f);

    if (self->intensity == intensity)
        return;

    self->intensity = intensity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

gfloat
lrg_bloom_get_blur_size (LrgBloom *self)
{
    g_return_val_if_fail (LRG_IS_BLOOM (self), 0.0f);

    return self->blur_size;
}

void
lrg_bloom_set_blur_size (LrgBloom *self,
                         gfloat    blur_size)
{
    g_return_if_fail (LRG_IS_BLOOM (self));

    blur_size = CLAMP (blur_size, 1.0f, 20.0f);

    if (self->blur_size == blur_size)
        return;

    self->blur_size = blur_size;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLUR_SIZE]);
}

guint
lrg_bloom_get_iterations (LrgBloom *self)
{
    g_return_val_if_fail (LRG_IS_BLOOM (self), 0);

    return self->iterations;
}

void
lrg_bloom_set_iterations (LrgBloom *self,
                          guint     iterations)
{
    g_return_if_fail (LRG_IS_BLOOM (self));

    iterations = CLAMP (iterations, 1, 8);

    if (self->iterations == iterations)
        return;

    self->iterations = iterations;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ITERATIONS]);
}

gfloat
lrg_bloom_get_soft_knee (LrgBloom *self)
{
    g_return_val_if_fail (LRG_IS_BLOOM (self), 0.0f);

    return self->soft_knee;
}

void
lrg_bloom_set_soft_knee (LrgBloom *self,
                         gfloat    soft_knee)
{
    g_return_if_fail (LRG_IS_BLOOM (self));

    soft_knee = CLAMP (soft_knee, 0.0f, 1.0f);

    if (self->soft_knee == soft_knee)
        return;

    self->soft_knee = soft_knee;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SOFT_KNEE]);
}

void
lrg_bloom_get_tint (LrgBloom *self,
                    gfloat   *r,
                    gfloat   *g,
                    gfloat   *b)
{
    g_return_if_fail (LRG_IS_BLOOM (self));

    if (r != NULL)
        *r = self->tint_r;
    if (g != NULL)
        *g = self->tint_g;
    if (b != NULL)
        *b = self->tint_b;
}

void
lrg_bloom_set_tint (LrgBloom *self,
                    gfloat    r,
                    gfloat    g,
                    gfloat    b)
{
    g_return_if_fail (LRG_IS_BLOOM (self));

    r = CLAMP (r, 0.0f, 1.0f);
    g = CLAMP (g, 0.0f, 1.0f);
    b = CLAMP (b, 0.0f, 1.0f);
    if (self->tint_r != r)
    {
        self->tint_r = r;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TINT_R]);
    }
    if (self->tint_g != g)
    {
        self->tint_g = g;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TINT_G]);
    }
    if (self->tint_b != b)
    {
        self->tint_b = b;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TINT_B]);
    }
}
