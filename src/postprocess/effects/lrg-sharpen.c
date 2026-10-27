/* lrg-sharpen.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A contrast-adaptive sharpening pass written for libregnum from the
 * description of the technique: weight the four neighbours negatively by
 * an amplitude that falls toward zero where the local range already
 * touches black or white, and renormalise.
 */

#include "config.h"

#include <math.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "postprocess/effects/lrg-sharpen.h"
#include "core/lrg-prewarm.h"

/* the shader mirrors lrg_sharpen_pixel() below; one texel is 1 / textureSize */
static const gchar sharpen_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float sharpness;\n"
    "uniform float intensity;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec2 px = 1.0 / vec2(textureSize(texture0, 0));\n"
    "    vec4 c = texture(texture0, fragTexCoord);\n"
    "    vec3 a = texture(texture0, fragTexCoord + vec2(0.0, -px.y)).rgb;\n"
    "    vec3 b = texture(texture0, fragTexCoord + vec2(0.0, px.y)).rgb;\n"
    "    vec3 l = texture(texture0, fragTexCoord + vec2(-px.x, 0.0)).rgb;\n"
    "    vec3 r = texture(texture0, fragTexCoord + vec2(px.x, 0.0)).rgb;\n"
    "    vec3 mn = min(c.rgb, min(min(a, b), min(l, r)));\n"
    "    vec3 mx = max(c.rgb, max(max(a, b), max(l, r)));\n"
    "    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0));\n"
    "    vec3 w = amp * mix(-0.125, -0.2, sharpness);\n"
    "    vec3 s = (c.rgb + (a + b + l + r) * w) / (1.0 + 4.0 * w);\n"
    "    finalColor = vec4(mix(c.rgb, clamp(s, 0.0, 1.0), intensity), c.a) * colDiffuse * fragColor;\n"
    "}\n";

struct _LrgSharpen
{
    LrgPostEffect  parent_instance;

    gfloat         sharpness;
    GrlShader     *shader;
    gboolean       shader_tried;
    gint           loc_sharpness;
    gint           loc_intensity;
};

static void lrg_sharpen_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgSharpen, lrg_sharpen, LRG_TYPE_POST_EFFECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                lrg_sharpen_prewarmable_init))

enum
{
    PROP_0,
    PROP_SHARPNESS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

void
lrg_sharpen_pixel (gfloat        sharpness,
                   const gfloat *center,
                   const gfloat *up,
                   const gfloat *down,
                   const gfloat *left,
                   const gfloat *right,
                   gfloat       *out)
{
    guint k;

    g_return_if_fail (center != NULL && up != NULL && down != NULL);
    g_return_if_fail (left != NULL && right != NULL && out != NULL);

    sharpness = CLAMP (sharpness, 0.0f, 1.0f);
    for (k = 0; k < 3; k++)
    {
        gfloat mn = MIN (center[k], MIN (MIN (up[k], down[k]), MIN (left[k], right[k])));
        gfloat mx = MAX (center[k], MAX (MAX (up[k], down[k]), MAX (left[k], right[k])));
        gfloat amp = sqrtf (CLAMP (MIN (mn, 1.0f - mx) / MAX (mx, 1e-4f), 0.0f, 1.0f));
        gfloat w = amp * (-0.125f + (-0.2f + 0.125f) * sharpness);
        gfloat s = (center[k] + (up[k] + down[k] + left[k] + right[k]) * w) / (1.0f + 4.0f * w);

        out[k] = CLAMP (s, 0.0f, 1.0f);
    }
}

const gchar *
lrg_sharpen_get_fragment_shader (void)
{
    return sharpen_fs;
}

gboolean
lrg_sharpen_is_identity (LrgSharpen *self)
{
    g_return_val_if_fail (LRG_IS_SHARPEN (self), TRUE);

    return lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)) <= 0.0f;
}

static GrlShader *
sharpen_get_shader (LrgSharpen *self)
{
    g_autoptr(GError) error = NULL;
    Shader *handle;

    if (self->shader != NULL || self->shader_tried)
        return self->shader;
    if (!IsWindowReady ())
        return NULL;
    self->shader_tried = TRUE;

    self->shader = grl_shader_new_from_memory (NULL, sharpen_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("sharpen: %s", error != NULL ? error->message : "no shader");
        return NULL;
    }
    handle = grl_shader_get_handle (self->shader);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
    {
        g_warning ("sharpen: the shader did not compile (GLSL 330 needed)");
        g_clear_object (&self->shader);
        return NULL;
    }
    self->loc_sharpness = grl_shader_get_location (self->shader, "sharpness");
    self->loc_intensity = grl_shader_get_location (self->shader, "intensity");
    return self->shader;
}

gboolean
lrg_sharpen_draw_texture (LrgSharpen         *self,
                          GrlTexture         *texture,
                          const GrlRectangle *source,
                          const GrlRectangle *dest)
{
    g_autoptr(GrlColor) white = NULL;
    GrlShader *shader;
    GrlVector2 origin;
    gboolean identity;

    g_return_val_if_fail (LRG_IS_SHARPEN (self), FALSE);
    g_return_val_if_fail (GRL_IS_TEXTURE (texture), FALSE);
    g_return_val_if_fail (source != NULL, FALSE);
    g_return_val_if_fail (dest != NULL, FALSE);

    white = grl_color_new (255, 255, 255, 255);
    origin.x = 0.0f;
    origin.y = 0.0f;
    identity = lrg_sharpen_is_identity (self);
    shader = identity ? NULL : sharpen_get_shader (self);
    if (shader != NULL)
    {
        grl_shader_set_value_float (shader, self->loc_sharpness, self->sharpness);
        grl_shader_set_value_float (shader, self->loc_intensity,
                                    lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)));
        grl_shader_begin (shader);
    }
    grl_draw_texture_pro (texture, source, dest, &origin, 0.0f, white);
    if (shader != NULL)
        grl_shader_end (shader);
    return identity || shader != NULL;
}

static gboolean
lrg_sharpen_real_initialize (LrgPostEffect *effect,
                             guint          width,
                             guint          height,
                             GError       **error)
{
    (void)effect;
    (void)width;
    (void)height;
    (void)error;
    return TRUE;
}

static void
lrg_sharpen_real_shutdown (LrgPostEffect *effect)
{
    LrgSharpen *self = LRG_SHARPEN (effect);

    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

static void
lrg_sharpen_real_apply (LrgPostEffect *effect,
                        guint          source_texture_id,
                        guint          target_texture_id,
                        guint          width,
                        guint          height,
                        gfloat         delta_time)
{
    LrgSharpen *self = LRG_SHARPEN (effect);
    Texture2D source;
    Rectangle src;
    Rectangle dst;
    Vector2 origin = { 0.0f, 0.0f };
    GrlShader *shader;

    (void)target_texture_id;
    (void)delta_time;

    if (source_texture_id == 0 || width == 0 || height == 0)
        return;

    source.id = source_texture_id;
    source.width = (int)width;
    source.height = (int)height;
    source.mipmaps = 1;
    source.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    src.x = 0.0f;
    src.y = 0.0f;
    src.width = (float)width;
    src.height = -(float)height;
    dst.x = 0.0f;
    dst.y = 0.0f;
    dst.width = (float)width;
    dst.height = (float)height;

    shader = lrg_sharpen_is_identity (self) ? NULL : sharpen_get_shader (self);
    if (shader != NULL)
    {
        grl_shader_set_value_float (shader, self->loc_sharpness, self->sharpness);
        grl_shader_set_value_float (shader, self->loc_intensity,
                                    lrg_post_effect_get_intensity (effect));
        grl_shader_begin (shader);
    }
    DrawTexturePro (source, src, dst, origin, 0.0f, WHITE);
    if (shader != NULL)
        grl_shader_end (shader);
}

static const gchar *
lrg_sharpen_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "sharpen";
}

static void
lrg_sharpen_real_resize (LrgPostEffect *effect,
                         guint          width,
                         guint          height)
{
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_sharpen_finalize (GObject *object)
{
    LrgSharpen *self = LRG_SHARPEN (object);

    g_clear_object (&self->shader);

    G_OBJECT_CLASS (lrg_sharpen_parent_class)->finalize (object);
}

static void
lrg_sharpen_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgSharpen *self = LRG_SHARPEN (object);

    switch (prop_id)
    {
    case PROP_SHARPNESS:
        g_value_set_float (value, self->sharpness);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_sharpen_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgSharpen *self = LRG_SHARPEN (object);

    switch (prop_id)
    {
    case PROP_SHARPNESS:
        lrg_sharpen_set_sharpness (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_sharpen_class_init (LrgSharpenClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->finalize = lrg_sharpen_finalize;
    object_class->get_property = lrg_sharpen_get_property;
    object_class->set_property = lrg_sharpen_set_property;

    effect_class->initialize = lrg_sharpen_real_initialize;
    effect_class->shutdown = lrg_sharpen_real_shutdown;
    effect_class->apply = lrg_sharpen_real_apply;
    effect_class->resize = lrg_sharpen_real_resize;
    effect_class->get_name = lrg_sharpen_real_get_name;

    properties[PROP_SHARPNESS] =
        g_param_spec_float ("sharpness",
                            "Sharpness",
                            "How hard the centre is pushed from its neighbours",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_sharpen_init (LrgSharpen *self)
{
    self->sharpness = 0.5f;
    self->loc_sharpness = -1;
    self->loc_intensity = -1;

    /* build the GPU resources behind a loading screen, not on the
     * first draw (see lrg_prewarm_track()) */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgSharpen *
lrg_sharpen_new (void)
{
    return g_object_new (LRG_TYPE_SHARPEN, NULL);
}

gfloat
lrg_sharpen_get_sharpness (LrgSharpen *self)
{
    g_return_val_if_fail (LRG_IS_SHARPEN (self), 0.0f);
    return self->sharpness;
}

void
lrg_sharpen_set_sharpness (LrgSharpen *self,
                           gfloat      sharpness)
{
    g_return_if_fail (LRG_IS_SHARPEN (self));

    sharpness = CLAMP (sharpness, 0.0f, 1.0f);
    if (self->sharpness == sharpness)
        return;
    self->sharpness = sharpness;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHARPNESS]);
}

/* ========================================================================== */
/* Prewarm                                                                    */
/* ========================================================================== */

/* lrg_sharpen_prewarm: compiles the sharpen shader now, not on the first
 * frame it sharpens */
static gboolean
lrg_sharpen_prewarm (LrgPrewarmable *prewarmable)
{
    sharpen_get_shader (LRG_SHARPEN (prewarmable));
    return TRUE;
}

static void
lrg_sharpen_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_sharpen_prewarm;
}
