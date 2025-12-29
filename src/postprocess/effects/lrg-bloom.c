/* lrg-bloom.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-bloom.h"

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

    /* Internal render targets (would be actual textures) */
    gpointer       bright_texture;
    gpointer       blur_texture_a;
    gpointer       blur_texture_b;
};

G_DEFINE_TYPE (LrgBloom, lrg_bloom, LRG_TYPE_POST_EFFECT)

enum
{
    PROP_0,
    PROP_THRESHOLD,
    PROP_INTENSITY,
    PROP_BLUR_SIZE,
    PROP_ITERATIONS,
    PROP_SOFT_KNEE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_bloom_real_initialize (LrgPostEffect *effect,
                           guint          width,
                           guint          height,
                           GError       **error)
{
    /*
     * In a real implementation, create:
     * - Brightness extraction render target (half or quarter resolution)
     * - Two ping-pong blur render targets
     * - Load bloom shaders (threshold, blur, combine)
     */
    (void)effect;
    (void)width;
    (void)height;
    (void)error;

    return TRUE;
}

static void
lrg_bloom_real_shutdown (LrgPostEffect *effect)
{
    LrgBloom *self = LRG_BLOOM (effect);

    /* Free render targets */
    g_clear_pointer (&self->bright_texture, g_free);
    g_clear_pointer (&self->blur_texture_a, g_free);
    g_clear_pointer (&self->blur_texture_b, g_free);
}

static void
lrg_bloom_real_apply (LrgPostEffect *effect,
                      guint          source_texture_id,
                      guint          target_texture_id,
                      guint          width,
                      guint          height,
                      gfloat         delta_time)
{
    /*
     * Real implementation would:
     *
     * Pass 1: Brightness extraction
     * - Sample input, convert to luminance
     * - Apply soft threshold: max(0, lum - threshold) * knee_factor
     * - Write to bright_texture
     *
     * Pass 2-N: Gaussian blur (ping-pong)
     * - For each iteration:
     *   - Horizontal blur pass
     *   - Vertical blur pass
     * - Use separable Gaussian kernel for efficiency
     *
     * Pass N+1: Combine
     * - Sample original input
     * - Sample blurred bright areas
     * - output = input + blur * intensity * tint
     *
     * Shader example (threshold):
     * #version 330
     * uniform sampler2D texture0;
     * uniform float threshold;
     * uniform float softKnee;
     *
     * void main() {
     *     vec4 color = texture(texture0, uv);
     *     float brightness = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
     *     float knee = threshold * softKnee;
     *     float soft = brightness - threshold + knee;
     *     soft = clamp(soft / (2.0 * knee + 0.0001), 0.0, 1.0);
     *     soft = soft * soft;
     *     float contrib = max(soft, step(threshold, brightness));
     *     gl_FragColor = vec4(color.rgb * contrib, 1.0);
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
    /*
     * Recreate render targets at new resolution
     * (typically half or quarter size for performance)
     */
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
    self->bright_texture = NULL;
    self->blur_texture_a = NULL;
    self->blur_texture_b = NULL;
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

    self->tint_r = CLAMP (r, 0.0f, 1.0f);
    self->tint_g = CLAMP (g, 0.0f, 1.0f);
    self->tint_b = CLAMP (b, 0.0f, 1.0f);
}
