/* lrg-heat-haze.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Heat haze: every pixel samples its source a little to the side and
 * up or down, by two crossed sine ripples rising with time, weighted
 * by a band about the horizon. haze_offset() is the shader's
 * arithmetic on the CPU (the tests check it); the shader reads the
 * screen height from gl_FragCoord so the band does not depend on how
 * the source is flipped.
 */

#include "config.h"
#include "lrg-heat-haze.h"
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include "core/lrg-prewarm.h"

static const gchar haze_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec4 shape;\n"
    "uniform vec3 params;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    float v = gl_FragCoord.y / params.z;\n"
    "    float d = (v - shape.z) / shape.w;\n"
    "    float w = exp(-d * d * (d > 0.0 ? 2.5 : 0.6));\n"
    "    float u = fragTexCoord.x;\n"
    "    float ph = params.y;\n"
    "    float du = sin(v * shape.x * 6.2831853 * 2.0 - ph + sin(u * shape.x * 3.1)) * w;\n"
    "    float dv = cos(u * shape.x * 6.2831853 + ph * 0.7) * w * 0.5;\n"
    "    vec2 uv = fragTexCoord + vec2(du, dv) * params.x;\n"
    "    finalColor = texture(texture0, uv) * colDiffuse * fragColor;\n"
    "}\n";

struct _LrgHeatHaze
{
    LrgPostEffect  parent_instance;

    gfloat         strength;
    gfloat         scale;
    gfloat         speed;
    gfloat         horizon;
    gfloat         band;
    gfloat         time;

    GrlShader     *shader;
    gboolean       shader_tried;
    gint           loc_shape;
    gint           loc_params;
};

static void lrg_heat_haze_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgHeatHaze, lrg_heat_haze, LRG_TYPE_POST_EFFECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                      lrg_heat_haze_prewarmable_init))

/* the distortion at (u, v) before the intensity: see the shader */
static void
haze_offset (LrgHeatHaze *self,
             gfloat       u,
             gfloat       v,
             gfloat       strength,
             gfloat      *du,
             gfloat      *dv)
{
    gfloat d;
    gfloat w;
    gfloat ph;

    d = (v - self->horizon) / self->band;
    w = expf (-d * d * (d > 0.0f ? 2.5f : 0.6f));
    ph = self->time * self->speed;
    *du = sinf (v * self->scale * 6.2831853f * 2.0f - ph + sinf (u * self->scale * 3.1f)) * w * strength;
    *dv = cosf (u * self->scale * 6.2831853f + ph * 0.7f) * w * 0.5f * strength;
}

static GrlShader *
get_shader (LrgHeatHaze *self)
{
    g_autoptr(GError) error = NULL;
    Shader *handle;

    if (self->shader != NULL || self->shader_tried)
        return self->shader;
    if (!IsWindowReady ())
        return NULL;
    self->shader_tried = TRUE;
    self->shader = grl_shader_new_from_memory (NULL, haze_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("heat haze: %s", error != NULL ? error->message : "no shader");
        return NULL;
    }
    handle = grl_shader_get_handle (self->shader);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
    {
        g_warning ("heat haze: the shader did not compile (GLSL 330 needed)");
        g_clear_object (&self->shader);
        return NULL;
    }
    self->loc_shape = grl_shader_get_location (self->shader, "shape");
    self->loc_params = grl_shader_get_location (self->shader, "params");
    return self->shader;
}

static void
upload (LrgHeatHaze *self,
        gfloat       height)
{
    Shader *handle;
    gfloat shape[4];
    gfloat params[3];

    handle = grl_shader_get_handle (self->shader);
    shape[0] = self->scale;
    shape[1] = self->speed;
    shape[2] = self->horizon;
    shape[3] = self->band;
    params[0] = self->strength * lrg_post_effect_get_intensity (LRG_POST_EFFECT (self));
    params[1] = self->time * self->speed;
    params[2] = MAX (height, 1.0f);
    SetShaderValue (*handle, self->loc_shape, shape, SHADER_UNIFORM_VEC4);
    SetShaderValue (*handle, self->loc_params, params, SHADER_UNIFORM_VEC3);
}

static gboolean
lrg_heat_haze_real_initialize (LrgPostEffect  *effect,
                               guint           width,
                               guint           height,
                               GError        **error)
{
    return TRUE;
}

static void
lrg_heat_haze_real_shutdown (LrgPostEffect *effect)
{
    LrgHeatHaze *self = LRG_HEAT_HAZE (effect);

    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

static void
lrg_heat_haze_real_apply (LrgPostEffect *effect,
                          guint          source_texture_id,
                          guint          target_texture_id,
                          guint          width,
                          guint          height,
                          gfloat         delta_time)
{
    LrgHeatHaze *self = LRG_HEAT_HAZE (effect);
    Texture2D source;
    Rectangle src;
    Rectangle dst;
    Vector2 origin = { 0.0f, 0.0f };
    GrlShader *shader;

    (void)target_texture_id;
    if (isfinite (delta_time) && delta_time > 0.0f)
        self->time += delta_time;
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
    shader = lrg_heat_haze_is_identity (self) ? NULL : get_shader (self);
    if (shader != NULL)
    {
        upload (self, (gfloat)height);
        grl_shader_begin (shader);
    }
    DrawTexturePro (source, src, dst, origin, 0.0f, WHITE);
    if (shader != NULL)
        grl_shader_end (shader);
}

static const gchar *
lrg_heat_haze_real_get_name (LrgPostEffect *effect)
{
    return "heat-haze";
}

static void
lrg_heat_haze_finalize (GObject *object)
{
    LrgHeatHaze *self = LRG_HEAT_HAZE (object);

    g_clear_object (&self->shader);

    G_OBJECT_CLASS (lrg_heat_haze_parent_class)->finalize (object);
}

static void
lrg_heat_haze_class_init (LrgHeatHazeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->finalize = lrg_heat_haze_finalize;
    effect_class->initialize = lrg_heat_haze_real_initialize;
    effect_class->shutdown = lrg_heat_haze_real_shutdown;
    effect_class->apply = lrg_heat_haze_real_apply;
    effect_class->get_name = lrg_heat_haze_real_get_name;
}

static void
lrg_heat_haze_init (LrgHeatHaze *self)
{
    self->strength = 0.004f;
    self->scale = 9.0f;
    self->speed = 2.2f;
    self->horizon = 0.55f;
    self->band = 0.18f;

    /* build the GPU resources behind a loading screen, not on the
     * first draw (see lrg_prewarm_track()) */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgHeatHaze *
lrg_heat_haze_new (void)
{
    return g_object_new (LRG_TYPE_HEAT_HAZE, NULL);
}

void
lrg_heat_haze_set_strength (LrgHeatHaze *self,
                            gfloat       strength)
{
    g_return_if_fail (LRG_IS_HEAT_HAZE (self));
    self->strength = isfinite (strength) ? CLAMP (strength, 0.0f, 0.05f) : 0.0f;
}

gfloat
lrg_heat_haze_get_strength (LrgHeatHaze *self)
{
    g_return_val_if_fail (LRG_IS_HEAT_HAZE (self), 0.0f);
    return self->strength;
}

void
lrg_heat_haze_set_shape (LrgHeatHaze *self,
                         gfloat       scale,
                         gfloat       speed,
                         gfloat       horizon,
                         gfloat       band)
{
    g_return_if_fail (LRG_IS_HEAT_HAZE (self));

    if (isfinite (scale) && scale > 0.0f)
        self->scale = scale;
    if (isfinite (speed))
        self->speed = speed;
    if (isfinite (horizon))
        self->horizon = CLAMP (horizon, 0.0f, 1.0f);
    if (isfinite (band) && band > 0.0f)
        self->band = band;
}

void
lrg_heat_haze_set_time (LrgHeatHaze *self,
                        gfloat       time)
{
    g_return_if_fail (LRG_IS_HEAT_HAZE (self));
    self->time = isfinite (time) ? time : 0.0f;
}

void
lrg_heat_haze_get_offset (LrgHeatHaze *self,
                          gfloat       u,
                          gfloat       v,
                          gfloat      *out_du,
                          gfloat      *out_dv)
{
    gfloat du;
    gfloat dv;

    g_return_if_fail (LRG_IS_HEAT_HAZE (self));
    g_return_if_fail (out_du != NULL && out_dv != NULL);

    haze_offset (self, u, v,
                 self->strength * lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)), &du, &dv);
    *out_du = du;
    *out_dv = dv;
}

gboolean
lrg_heat_haze_is_identity (LrgHeatHaze *self)
{
    g_return_val_if_fail (LRG_IS_HEAT_HAZE (self), TRUE);
    return self->strength <= 0.0f || lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)) <= 0.0f ||
           !lrg_post_effect_is_enabled (LRG_POST_EFFECT (self));
}

gboolean
lrg_heat_haze_draw_texture (LrgHeatHaze        *self,
                            GrlTexture         *texture,
                            const GrlRectangle *source,
                            const GrlRectangle *dest)
{
    g_autoptr(GrlColor) white = NULL;
    GrlShader *shader;
    GrlVector2 origin;
    gboolean identity;

    g_return_val_if_fail (LRG_IS_HEAT_HAZE (self), FALSE);
    g_return_val_if_fail (GRL_IS_TEXTURE (texture), FALSE);
    g_return_val_if_fail (source != NULL && dest != NULL, FALSE);

    white = grl_color_new (255, 255, 255, 255);
    origin.x = 0.0f;
    origin.y = 0.0f;
    identity = lrg_heat_haze_is_identity (self);
    shader = identity ? NULL : get_shader (self);
    if (shader != NULL)
    {
        upload (self, (gfloat)rlGetFramebufferHeight ());
        grl_shader_begin (shader);
    }
    grl_draw_texture_pro (texture, source, dest, &origin, 0.0f, white);
    if (shader != NULL)
        grl_shader_end (shader);
    return identity || shader != NULL;
}

/* ========================================================================== */
/* Prewarm                                                                    */
/* ========================================================================== */

/* lrg_heat_haze_prewarm: compiles the haze shader now, not on the first
 * frame the haze shows */
static gboolean
lrg_heat_haze_prewarm (LrgPrewarmable *prewarmable)
{
    get_shader (LRG_HEAT_HAZE (prewarmable));
    return TRUE;
}

static void
lrg_heat_haze_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_heat_haze_prewarm;
}
