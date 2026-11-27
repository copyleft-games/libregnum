/* lrg-blur.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-blur.h"
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>
#include "core/lrg-prewarm.h"
#include "graphics/lrg-glsl-private.h"
#include "postprocess/lrg-post-cpu-private.h"

/*
 * A separable Gaussian in two passes of one shader:
 *
 *  1. horizontal  source -> a private target the source's size
 *                 (RGBA16F when the driver renders to it, else RGBA8):
 *                 the weighted sum of 2r + 1 texels along x;
 *  2. vertical    private target -> the processor's target: the same
 *                 along y, then mixed with the source by the base
 *                 intensity (the first pass mixes with 1).
 *
 * Taps sit on texel centres (offsets are whole texels), so the bilinear
 * filter returns exact texels and the clamp-to-edge wrap repeats the
 * border; the CPU path does exactly that. The weights are computed once
 * on the CPU (blur_kernel()) and uploaded, so both paths share them.
 * Alpha is not blurred: every pass keeps the source's alpha, as the
 * other effects do.
 */
static const gchar blur_fs[] =
    LRG_GLSL_HEADER
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D original;\n"
    "uniform vec2 texel;\n"
    "uniform int radius;\n"
    "uniform float weights[33];\n"
    "uniform float intensity;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec2 uv = fragTexCoord;\n"
    "    vec4 o = texture(original, uv);\n"
    "    vec3 s = texture(texture0, uv).rgb * weights[0];\n"
    "    for (int i = 1; i <= radius; i++)\n"
    "        s += (texture(texture0, uv + texel * float(i)).rgb +\n"
    "              texture(texture0, uv - texel * float(i)).rgb) * weights[i];\n"
    "    finalColor = vec4(mix(o.rgb, s, intensity), o.a);\n"
    "}\n";

/**
 * SECTION:lrg-blur
 * @Title: LrgBlur
 * @Short_description: Separable Gaussian blur effect
 *
 * #LrgBlur blurs the whole image with a Gaussian kernel of
 * #LrgBlur:radius pixels on each side (#LrgBlur:sigma, by default
 * radius / 2), in a horizontal and a vertical pass. The base
 * #LrgPostEffect:intensity mixes the blurred image with the source.
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgBlur) blur = lrg_blur_new ();
 *
 * lrg_blur_set_radius (blur, 6);
 * lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (blur));
 * ]|
 *
 * The effect has a CPU path (lrg_post_effect_apply_cpu()) with the same
 * kernel (lrg_blur_get_kernel()).
 *
 * Since: 0.14
 */

struct _LrgBlur
{
    LrgPostEffect   parent_instance;

    guint           radius;
    gfloat          sigma;          /* 0: radius / 2 */

    /* GPU: the shader and the horizontal pass's target */
    GrlShader      *shader;
    gboolean        shader_tried;
    gint            loc_texel;
    gint            loc_radius;
    gint            loc_weights;
    gint            loc_intensity;
    gint            loc_original;
    RenderTexture2D tmp;
};

static void lrg_blur_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgBlur, lrg_blur, LRG_TYPE_POST_EFFECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                      lrg_blur_prewarmable_init))

enum
{
    PROP_0,
    PROP_RADIUS,
    PROP_SIGMA,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * The kernel
 * ========================================================================== */

/*
 * blur_kernel:
 * Fills @half with radius + 1 weights, the centre first: w(d) =
 * exp (-d^2 / (2 sigma^2)), normalised so the full kernel (the centre
 * once, every other weight twice) sums to 1.
 */
static void
blur_kernel (LrgBlur *self,
             gfloat  *half)
{
    gfloat sigma;
    gdouble sum;
    guint i;

    sigma = self->sigma > 0.0f ? self->sigma : (gfloat)self->radius * 0.5f;
    if (self->radius == 0 || sigma <= 0.0f)
    {
        half[0] = 1.0f;
        for (i = 1; i <= self->radius; i++)
            half[i] = 0.0f;
        return;
    }

    /* the raw Gaussian, summed in double so large radii stay exact */
    sum = 0.0;
    for (i = 0; i <= self->radius; i++)
    {
        gdouble d = (gdouble)i;
        gdouble w = exp (-(d * d) / (2.0 * (gdouble)sigma * (gdouble)sigma));

        half[i] = (gfloat)w;
        sum += (i == 0) ? w : 2.0 * w;
    }
    for (i = 0; i <= self->radius; i++)
        half[i] = (gfloat)((gdouble)half[i] / sum);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

static GrlShader *
blur_get_shader (LrgBlur *self)
{
    g_autoptr(GError) error = NULL;

    if (self->shader != NULL || self->shader_tried || !IsWindowReady ())
        return self->shader;
    self->shader_tried = TRUE;
    self->shader = lrg_post_effect_compile_shader (blur_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("blur: %s", error->message);
        return NULL;
    }
    self->loc_texel = grl_shader_get_location (self->shader, "texel");
    self->loc_radius = grl_shader_get_location (self->shader, "radius");
    self->loc_weights = grl_shader_get_location (self->shader, "weights");
    self->loc_intensity = grl_shader_get_location (self->shader, "intensity");
    self->loc_original = grl_shader_get_location (self->shader, "original");
    return self->shader;
}

static void
free_tmp (LrgBlur *self)
{
    if (self->tmp.id != 0 && IsWindowReady ())
        UnloadRenderTexture (self->tmp);
    memset (&self->tmp, 0, sizeof self->tmp);
}

/* (Re)creates the horizontal pass's target at @width x @height: RGBA16F,
 * RGBA8 when the driver cannot render to it. Rebinds framebuffers; the
 * caller rebinds its target afterwards. */
static gboolean
ensure_tmp (LrgBlur *self,
            guint    width,
            guint    height)
{
    gint fmt;

    if (self->tmp.id != 0 && (guint)self->tmp.texture.width == width &&
        (guint)self->tmp.texture.height == height)
        return TRUE;

    rlDrawRenderBatchActive ();
    free_tmp (self);
    for (fmt = 0; fmt < 2; fmt++)
    {
        self->tmp = LoadRenderTextureEx ((gint)width, (gint)height,
                                         fmt == 0 ? PIXELFORMAT_UNCOMPRESSED_R16G16B16A16
                                                  : PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        if (self->tmp.id != 0 && self->tmp.texture.id != 0 && rlFramebufferComplete (self->tmp.id))
        {
            SetTextureFilter (self->tmp.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap (self->tmp.texture, TEXTURE_WRAP_CLAMP);
            return TRUE;
        }
        if (self->tmp.id != 0 || self->tmp.texture.id != 0)
            UnloadRenderTexture (self->tmp);
        memset (&self->tmp, 0, sizeof self->tmp);
    }
    return FALSE;
}

/* One pass: @src through the shader into the bound target, stepping by
 * (@tx, @ty) and mixing with @original by @mix */
static void
blur_pass (LrgBlur *self,
           guint    src,
           guint    original,
           guint    width,
           guint    height,
           gfloat   tx,
           gfloat   ty,
           gfloat   mix)
{
    Shader *handle = grl_shader_get_handle (self->shader);
    Texture2D orig;

    memset (&orig, 0, sizeof orig);
    orig.id = original;
    orig.width = (gint)width;
    orig.height = (gint)height;
    orig.mipmaps = 1;
    orig.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    grl_shader_set_value_vec2 (self->shader, self->loc_texel, tx, ty);
    grl_shader_set_value_float (self->shader, self->loc_intensity, mix);
    grl_shader_begin (self->shader);
    SetShaderValueTexture (*handle, self->loc_original, orig);
    lrg_post_effect_draw_fullscreen (src, width, height);
    grl_shader_end (self->shader);
}

static void
lrg_blur_real_shutdown (LrgPostEffect *effect)
{
    LrgBlur *self = LRG_BLUR (effect);

    free_tmp (self);
    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

static void
lrg_blur_real_apply (LrgPostEffect *effect,
                     guint          source_texture_id,
                     guint          target_texture_id,
                     guint          width,
                     guint          height,
                     gfloat         delta_time)
{
    LrgBlur *self = LRG_BLUR (effect);
    gfloat half[LRG_BLUR_MAX_RADIUS + 1];
    guint target_fbo;
    gfloat k;
    Shader *handle;

    (void)target_texture_id;
    (void)delta_time;

    /* identity: radius 0, intensity 0 or no shader draws the source */
    k = lrg_post_effect_get_intensity (effect);
    if (self->radius == 0 || k <= 0.0f || blur_get_shader (self) == NULL)
    {
        lrg_post_effect_draw_fullscreen (source_texture_id, width, height);
        return;
    }

    /* the processor's target, to come back to for the second pass */
    rlDrawRenderBatchActive ();
    target_fbo = rlGetActiveFramebuffer ();
    if (!ensure_tmp (self, width, height))
    {
        lrg_post_effect_bind_framebuffer (target_fbo, width, height);
        lrg_post_effect_draw_fullscreen (source_texture_id, width, height);
        return;
    }

    /* the shared uniforms: the half kernel and its length */
    blur_kernel (self, half);
    handle = grl_shader_get_handle (self->shader);
    grl_shader_set_value_int (self->shader, self->loc_radius, (gint)self->radius);
    SetShaderValueV (*handle, self->loc_weights, half, SHADER_UNIFORM_FLOAT,
                     (gint)self->radius + 1);

    /* 1. horizontal, into the private target (blending stays off) */
    lrg_post_effect_bind_framebuffer (self->tmp.id, width, height);
    blur_pass (self, source_texture_id, source_texture_id, width, height,
               1.0f / (gfloat)width, 0.0f, 1.0f);

    /* 2. vertical, into the processor's target, mixed by the intensity */
    lrg_post_effect_bind_framebuffer (target_fbo, width, height);
    blur_pass (self, self->tmp.texture.id, source_texture_id, width, height,
               0.0f, 1.0f / (gfloat)height, k);
}

/* ==========================================================================
 * CPU
 * ========================================================================== */

/*
 * lrg_blur_real_apply_cpu: the two passes on the float image. The
 * horizontal pass writes a scratch copy, the vertical pass reads it back
 * and mixes with the untouched source by the intensity; taps outside
 * the image repeat the border texel (the GL clamp). Alpha is kept.
 */
static gboolean
lrg_blur_real_apply_cpu (LrgPostEffect *effect,
                         gfloat        *rgba,
                         guint          width,
                         guint          height,
                         gfloat         time)
{
    LrgBlur *self = LRG_BLUR (effect);
    g_autofree gfloat *tmp = NULL;
    gfloat half[LRG_BLUR_MAX_RADIUS + 1];
    gint r;
    gfloat k;
    guint x;
    guint y;

    (void)time;

    k = lrg_post_effect_get_intensity (effect);
    if (self->radius == 0 || k <= 0.0f)
        return TRUE;
    blur_kernel (self, half);
    r = (gint)self->radius;
    tmp = g_new (gfloat, (gsize)width * height * 4);

    /* 1. horizontal: rgba -> tmp */
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            const gfloat *c = lrg_post_cpu_texel (rgba, 4, width, height, (gint)x, (gint)y);
            gfloat *o = tmp + ((gsize)y * width + x) * 4;
            gint i;
            guint j;

            for (j = 0; j < 3; j++)
                o[j] = c[j] * half[0];
            o[3] = c[3];
            for (i = 1; i <= r; i++)
            {
                const gfloat *a = lrg_post_cpu_texel (rgba, 4, width, height, (gint)x + i, (gint)y);
                const gfloat *b = lrg_post_cpu_texel (rgba, 4, width, height, (gint)x - i, (gint)y);

                for (j = 0; j < 3; j++)
                    o[j] += (a[j] + b[j]) * half[i];
            }
        }
    }

    /* 2. vertical: tmp -> rgba, mixed with the source by the intensity */
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            const gfloat *c = lrg_post_cpu_texel (tmp, 4, width, height, (gint)x, (gint)y);
            gfloat *p = rgba + ((gsize)y * width + x) * 4;
            gfloat s[3];
            gint i;
            guint j;

            for (j = 0; j < 3; j++)
                s[j] = c[j] * half[0];
            for (i = 1; i <= r; i++)
            {
                const gfloat *a = lrg_post_cpu_texel (tmp, 4, width, height, (gint)x, (gint)y + i);
                const gfloat *b = lrg_post_cpu_texel (tmp, 4, width, height, (gint)x, (gint)y - i);

                for (j = 0; j < 3; j++)
                    s[j] += (a[j] + b[j]) * half[i];
            }
            for (j = 0; j < 3; j++)
                p[j] += (s[j] - p[j]) * k;
        }
    }
    return TRUE;
}

/* lrg_blur_prewarm: compiles the pass shader ahead of the first frame */
static gboolean
lrg_blur_prewarm (LrgPrewarmable *prewarmable)
{
    blur_get_shader (LRG_BLUR (prewarmable));
    return TRUE;
}

static void
lrg_blur_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_blur_prewarm;
}

static const gchar *
lrg_blur_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "blur";
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_blur_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LrgBlur *self = LRG_BLUR (object);

    switch (prop_id)
    {
    case PROP_RADIUS:
        g_value_set_uint (value, self->radius);
        break;
    case PROP_SIGMA:
        g_value_set_float (value, self->sigma);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_blur_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LrgBlur *self = LRG_BLUR (object);

    switch (prop_id)
    {
    case PROP_RADIUS:
        lrg_blur_set_radius (self, g_value_get_uint (value));
        break;
    case PROP_SIGMA:
        lrg_blur_set_sigma (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_blur_class_init (LrgBlurClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->get_property = lrg_blur_get_property;
    object_class->set_property = lrg_blur_set_property;

    effect_class->shutdown = lrg_blur_real_shutdown;
    effect_class->apply = lrg_blur_real_apply;
    effect_class->apply_cpu = lrg_blur_real_apply_cpu;
    effect_class->get_name = lrg_blur_real_get_name;

    /**
     * LrgBlur:radius:
     *
     * How many pixels the kernel reaches on each side (0 is the
     * identity, at most %LRG_BLUR_MAX_RADIUS).
     *
     * Since: 0.14
     */
    properties[PROP_RADIUS] =
        g_param_spec_uint ("radius",
                           "Radius",
                           "Kernel radius in pixels",
                           0, LRG_BLUR_MAX_RADIUS, 4,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgBlur:sigma:
     *
     * The Gaussian's standard deviation in pixels; 0 picks radius / 2.
     *
     * Since: 0.14
     */
    properties[PROP_SIGMA] =
        g_param_spec_float ("sigma",
                            "Sigma",
                            "Gaussian standard deviation (0: radius / 2)",
                            0.0f, (gfloat)LRG_BLUR_MAX_RADIUS, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_blur_init (LrgBlur *self)
{
    self->radius = 4;
    self->sigma = 0.0f;
    self->loc_texel = -1;
    self->loc_radius = -1;
    self->loc_weights = -1;
    self->loc_intensity = -1;
    self->loc_original = -1;

    /* compile ahead of the first frame when a loading screen prewarms */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgBlur *
lrg_blur_new (void)
{
    return g_object_new (LRG_TYPE_BLUR, NULL);
}

guint
lrg_blur_get_radius (LrgBlur *self)
{
    g_return_val_if_fail (LRG_IS_BLUR (self), 0);

    return self->radius;
}

void
lrg_blur_set_radius (LrgBlur *self,
                     guint    radius)
{
    g_return_if_fail (LRG_IS_BLUR (self));

    radius = MIN (radius, (guint)LRG_BLUR_MAX_RADIUS);
    if (self->radius == radius)
        return;
    self->radius = radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
}

gfloat
lrg_blur_get_sigma (LrgBlur *self)
{
    g_return_val_if_fail (LRG_IS_BLUR (self), 0.0f);

    return self->sigma;
}

void
lrg_blur_set_sigma (LrgBlur *self,
                    gfloat   sigma)
{
    g_return_if_fail (LRG_IS_BLUR (self));

    if (!isfinite (sigma))
        sigma = 0.0f;
    sigma = CLAMP (sigma, 0.0f, (gfloat)LRG_BLUR_MAX_RADIUS);
    if (self->sigma == sigma)
        return;
    self->sigma = sigma;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SIGMA]);
}

gfloat *
lrg_blur_get_kernel (LrgBlur *self,
                     guint   *n_weights)
{
    gfloat half[LRG_BLUR_MAX_RADIUS + 1];
    gfloat *out;
    guint r;
    guint i;

    g_return_val_if_fail (LRG_IS_BLUR (self), NULL);
    g_return_val_if_fail (n_weights != NULL, NULL);

    /* mirror the half kernel: offsets -radius .. +radius */
    blur_kernel (self, half);
    r = self->radius;
    out = g_new (gfloat, 2 * r + 1);
    for (i = 0; i <= r; i++)
    {
        out[r + i] = half[i];
        out[r - i] = half[i];
    }
    *n_weights = 2 * r + 1;
    return out;
}

const gchar *
lrg_blur_get_fragment_shader (void)
{
    return blur_fs;
}
