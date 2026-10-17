/* lrg-color-grade.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-color-grade.h"
#include <math.h>
#include <raylib.h>
#include <rlgl.h>

/**
 * SECTION:lrg-color-grade
 * @Title: LrgColorGrade
 * @Short_description: Color grading effect
 *
 * #LrgColorGrade provides comprehensive color grading controls:
 *
 * - Exposure, contrast, saturation
 * - Temperature and tint (white balance)
 * - Lift/Gamma/Gain (shadows/midtones/highlights)
 *
 * The grade runs as a GLSL 330 fragment shader
 * (lrg_color_grade_get_fragment_shader()) when the effect is applied by
 * a post processor or drawn with lrg_color_grade_draw_texture();
 * lrg_color_grade_apply_rgb() is the same arithmetic on the CPU, the
 * reference the tests check the parameter mapping against.
 */

/* the shader: raylib's default vertex shader feeds fragTexCoord and
 * fragColor; every step mirrors grade_rgb() below */
static const gchar grade_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float exposure;\n"
    "uniform float temperature;\n"
    "uniform float tint;\n"
    "uniform vec3 lift;\n"
    "uniform vec3 gamma;\n"
    "uniform vec3 gain;\n"
    "uniform float contrast;\n"
    "uniform float saturation;\n"
    "uniform float intensity;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    vec3 c = texel.rgb * exp2(exposure);\n"
    "    c *= vec3(1.0 + 0.2 * temperature, 1.0 - 0.2 * tint, 1.0 - 0.2 * temperature);\n"
    "    c = max(c + lift, vec3(0.0));\n"
    "    c = pow(c, vec3(1.0) / gamma) * gain;\n"
    "    c = (c - vec3(0.5)) * contrast + vec3(0.5);\n"
    "    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "    c = mix(vec3(lum), c, saturation);\n"
    "    c = clamp(c, 0.0, 1.0);\n"
    "    finalColor = vec4(mix(texel.rgb, c, intensity), texel.a);\n"
    "}\n";


struct _LrgColorGrade
{
    LrgPostEffect  parent_instance;

    /* Basic adjustments */
    gfloat         exposure;
    gfloat         contrast;
    gfloat         saturation;

    /* White balance */
    gfloat         temperature;
    gfloat         tint;

    /* Lift/Gamma/Gain (RGB) */
    gfloat         lift_r, lift_g, lift_b;
    gfloat         gamma_r, gamma_g, gamma_b;
    gfloat         gain_r, gain_g, gain_b;

    /* the shader, compiled on first use (needs a GL context) */
    GrlShader     *shader;
    gboolean       shader_tried;
    gint           loc_exposure;
    gint           loc_temperature;
    gint           loc_tint;
    gint           loc_lift;
    gint           loc_gamma;
    gint           loc_gain;
    gint           loc_contrast;
    gint           loc_saturation;
    gint           loc_intensity;
};

G_DEFINE_TYPE (LrgColorGrade, lrg_color_grade, LRG_TYPE_POST_EFFECT)

enum
{
    PROP_0,
    PROP_EXPOSURE,
    PROP_CONTRAST,
    PROP_SATURATION,
    PROP_TEMPERATURE,
    PROP_TINT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_color_grade_real_initialize (LrgPostEffect *effect,
                                 guint          width,
                                 guint          height,
                                 GError       **error)
{
    return TRUE;
}

static void
lrg_color_grade_real_shutdown (LrgPostEffect *effect)
{
    LrgColorGrade *self = LRG_COLOR_GRADE (effect);

    /* the shader belongs to the context being shut down */
    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

/* uploads the parameters; the shader must be valid */
static void
upload_uniforms (LrgColorGrade *self)
{
    grl_shader_set_value_float (self->shader, self->loc_exposure, self->exposure);
    grl_shader_set_value_float (self->shader, self->loc_temperature, self->temperature);
    grl_shader_set_value_float (self->shader, self->loc_tint, self->tint);
    grl_shader_set_value_vec3 (self->shader, self->loc_lift, self->lift_r, self->lift_g, self->lift_b);
    grl_shader_set_value_vec3 (self->shader, self->loc_gamma, self->gamma_r, self->gamma_g,
                               self->gamma_b);
    grl_shader_set_value_vec3 (self->shader, self->loc_gain, self->gain_r, self->gain_g, self->gain_b);
    grl_shader_set_value_float (self->shader, self->loc_contrast, self->contrast);
    grl_shader_set_value_float (self->shader, self->loc_saturation, self->saturation);
    grl_shader_set_value_float (self->shader, self->loc_intensity,
                                lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)));
}

/*
 * The processor binds @target (or the screen) before calling: the
 * source texture is drawn over the whole target through the shader,
 * flipped (render textures are stored bottom up).
 */
static void
lrg_color_grade_real_apply (LrgPostEffect *effect,
                            guint          source_texture_id,
                            guint          target_texture_id,
                            guint          width,
                            guint          height,
                            gfloat         delta_time)
{
    LrgColorGrade *self = LRG_COLOR_GRADE (effect);
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

    shader = lrg_color_grade_is_identity (self) ? NULL : lrg_color_grade_get_shader (self);
    if (shader != NULL)
    {
        upload_uniforms (self);
        grl_shader_begin (shader);
    }
    DrawTexturePro (source, src, dst, origin, 0.0f, WHITE);
    if (shader != NULL)
        grl_shader_end (shader);
}

static const gchar *
lrg_color_grade_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "color-grade";
}

static void
lrg_color_grade_real_resize (LrgPostEffect *effect,
                             guint          width,
                             guint          height)
{
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_color_grade_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgColorGrade *self = LRG_COLOR_GRADE (object);

    switch (prop_id)
    {
    case PROP_EXPOSURE:
        g_value_set_float (value, self->exposure);
        break;
    case PROP_CONTRAST:
        g_value_set_float (value, self->contrast);
        break;
    case PROP_SATURATION:
        g_value_set_float (value, self->saturation);
        break;
    case PROP_TEMPERATURE:
        g_value_set_float (value, self->temperature);
        break;
    case PROP_TINT:
        g_value_set_float (value, self->tint);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_color_grade_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgColorGrade *self = LRG_COLOR_GRADE (object);

    switch (prop_id)
    {
    case PROP_EXPOSURE:
        lrg_color_grade_set_exposure (self, g_value_get_float (value));
        break;
    case PROP_CONTRAST:
        lrg_color_grade_set_contrast (self, g_value_get_float (value));
        break;
    case PROP_SATURATION:
        lrg_color_grade_set_saturation (self, g_value_get_float (value));
        break;
    case PROP_TEMPERATURE:
        lrg_color_grade_set_temperature (self, g_value_get_float (value));
        break;
    case PROP_TINT:
        lrg_color_grade_set_tint (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_color_grade_dispose (GObject *object)
{
    LrgColorGrade *self = LRG_COLOR_GRADE (object);

    g_clear_object (&self->shader);
    G_OBJECT_CLASS (lrg_color_grade_parent_class)->dispose (object);
}

static void
lrg_color_grade_class_init (LrgColorGradeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->dispose = lrg_color_grade_dispose;
    object_class->get_property = lrg_color_grade_get_property;
    object_class->set_property = lrg_color_grade_set_property;

    effect_class->initialize = lrg_color_grade_real_initialize;
    effect_class->shutdown = lrg_color_grade_real_shutdown;
    effect_class->apply = lrg_color_grade_real_apply;
    effect_class->resize = lrg_color_grade_real_resize;
    effect_class->get_name = lrg_color_grade_real_get_name;

    properties[PROP_EXPOSURE] =
        g_param_spec_float ("exposure",
                            "Exposure",
                            "Exposure adjustment",
                            -5.0f, 5.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_CONTRAST] =
        g_param_spec_float ("contrast",
                            "Contrast",
                            "Contrast",
                            0.0f, 2.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_SATURATION] =
        g_param_spec_float ("saturation",
                            "Saturation",
                            "Color saturation",
                            0.0f, 2.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_TEMPERATURE] =
        g_param_spec_float ("temperature",
                            "Temperature",
                            "Color temperature",
                            -1.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_TINT] =
        g_param_spec_float ("tint",
                            "Tint",
                            "Green-magenta tint",
                            -1.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_color_grade_init (LrgColorGrade *self)
{
    self->exposure = 0.0f;
    self->contrast = 1.0f;
    self->saturation = 1.0f;
    self->temperature = 0.0f;
    self->tint = 0.0f;
    self->lift_r = self->lift_g = self->lift_b = 0.0f;
    self->gamma_r = self->gamma_g = self->gamma_b = 1.0f;
    self->gain_r = self->gain_g = self->gain_b = 1.0f;
}

LrgColorGrade *
lrg_color_grade_new (void)
{
    return g_object_new (LRG_TYPE_COLOR_GRADE, NULL);
}

gfloat
lrg_color_grade_get_exposure (LrgColorGrade *self)
{
    g_return_val_if_fail (LRG_IS_COLOR_GRADE (self), 0.0f);
    return self->exposure;
}

void
lrg_color_grade_set_exposure (LrgColorGrade *self,
                              gfloat         exposure)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    exposure = CLAMP (exposure, -5.0f, 5.0f);
    if (self->exposure == exposure)
        return;

    self->exposure = exposure;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPOSURE]);
}

gfloat
lrg_color_grade_get_contrast (LrgColorGrade *self)
{
    g_return_val_if_fail (LRG_IS_COLOR_GRADE (self), 1.0f);
    return self->contrast;
}

void
lrg_color_grade_set_contrast (LrgColorGrade *self,
                              gfloat         contrast)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    contrast = CLAMP (contrast, 0.0f, 2.0f);
    if (self->contrast == contrast)
        return;

    self->contrast = contrast;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONTRAST]);
}

gfloat
lrg_color_grade_get_saturation (LrgColorGrade *self)
{
    g_return_val_if_fail (LRG_IS_COLOR_GRADE (self), 1.0f);
    return self->saturation;
}

void
lrg_color_grade_set_saturation (LrgColorGrade *self,
                                gfloat         saturation)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    saturation = CLAMP (saturation, 0.0f, 2.0f);
    if (self->saturation == saturation)
        return;

    self->saturation = saturation;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SATURATION]);
}

gfloat
lrg_color_grade_get_temperature (LrgColorGrade *self)
{
    g_return_val_if_fail (LRG_IS_COLOR_GRADE (self), 0.0f);
    return self->temperature;
}

void
lrg_color_grade_set_temperature (LrgColorGrade *self,
                                 gfloat         temperature)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    temperature = CLAMP (temperature, -1.0f, 1.0f);
    if (self->temperature == temperature)
        return;

    self->temperature = temperature;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEMPERATURE]);
}

gfloat
lrg_color_grade_get_tint (LrgColorGrade *self)
{
    g_return_val_if_fail (LRG_IS_COLOR_GRADE (self), 0.0f);
    return self->tint;
}

void
lrg_color_grade_set_tint (LrgColorGrade *self,
                          gfloat         tint)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    tint = CLAMP (tint, -1.0f, 1.0f);
    if (self->tint == tint)
        return;

    self->tint = tint;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TINT]);
}

void
lrg_color_grade_get_lift (LrgColorGrade *self,
                          gfloat        *r,
                          gfloat        *g,
                          gfloat        *b)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    if (r != NULL) *r = self->lift_r;
    if (g != NULL) *g = self->lift_g;
    if (b != NULL) *b = self->lift_b;
}

void
lrg_color_grade_set_lift (LrgColorGrade *self,
                          gfloat         r,
                          gfloat         g,
                          gfloat         b)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    self->lift_r = CLAMP (r, -1.0f, 1.0f);
    self->lift_g = CLAMP (g, -1.0f, 1.0f);
    self->lift_b = CLAMP (b, -1.0f, 1.0f);
}

void
lrg_color_grade_get_gamma (LrgColorGrade *self,
                           gfloat        *r,
                           gfloat        *g,
                           gfloat        *b)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    if (r != NULL) *r = self->gamma_r;
    if (g != NULL) *g = self->gamma_g;
    if (b != NULL) *b = self->gamma_b;
}

void
lrg_color_grade_set_gamma (LrgColorGrade *self,
                           gfloat         r,
                           gfloat         g,
                           gfloat         b)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    /* gamma is a divisor of the exponent: never 0 or negative */
    self->gamma_r = CLAMP (r, 0.1f, 4.0f);
    self->gamma_g = CLAMP (g, 0.1f, 4.0f);
    self->gamma_b = CLAMP (b, 0.1f, 4.0f);
}

void
lrg_color_grade_get_gain (LrgColorGrade *self,
                          gfloat        *r,
                          gfloat        *g,
                          gfloat        *b)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    if (r != NULL) *r = self->gain_r;
    if (g != NULL) *g = self->gain_g;
    if (b != NULL) *b = self->gain_b;
}

void
lrg_color_grade_set_gain (LrgColorGrade *self,
                          gfloat         r,
                          gfloat         g,
                          gfloat         b)
{
    g_return_if_fail (LRG_IS_COLOR_GRADE (self));

    self->gain_r = CLAMP (r, 0.0f, 4.0f);
    self->gain_g = CLAMP (g, 0.0f, 4.0f);
    self->gain_b = CLAMP (b, 0.0f, 4.0f);
}

gboolean
lrg_color_grade_is_identity (LrgColorGrade *self)
{
    g_return_val_if_fail (LRG_IS_COLOR_GRADE (self), TRUE);

    if (lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)) <= 0.0f)
        return TRUE;
    return self->exposure == 0.0f && self->contrast == 1.0f && self->saturation == 1.0f &&
           self->temperature == 0.0f && self->tint == 0.0f &&
           self->lift_r == 0.0f && self->lift_g == 0.0f && self->lift_b == 0.0f &&
           self->gamma_r == 1.0f && self->gamma_g == 1.0f && self->gamma_b == 1.0f &&
           self->gain_r == 1.0f && self->gain_g == 1.0f && self->gain_b == 1.0f;
}

void
lrg_color_grade_apply_rgb (LrgColorGrade *self,
                           const gfloat  *in,
                           gfloat        *out)
{
    gfloat c[3];
    gfloat original[3];
    gfloat scale;
    gfloat lum;
    gfloat intensity;
    gfloat lift[3];
    gfloat gamma[3];
    gfloat gain[3];
    guint k;

    g_return_if_fail (LRG_IS_COLOR_GRADE (self));
    g_return_if_fail (in != NULL);
    g_return_if_fail (out != NULL);

    lift[0] = self->lift_r;
    lift[1] = self->lift_g;
    lift[2] = self->lift_b;
    gamma[0] = self->gamma_r;
    gamma[1] = self->gamma_g;
    gamma[2] = self->gamma_b;
    gain[0] = self->gain_r;
    gain[1] = self->gain_g;
    gain[2] = self->gain_b;

    for (k = 0; k < 3; k++)
        original[k] = in[k];

    /* the shader's steps, in its order */
    scale = exp2f (self->exposure);
    for (k = 0; k < 3; k++)
        c[k] = original[k] * scale;
    c[0] *= 1.0f + 0.2f * self->temperature;
    c[1] *= 1.0f - 0.2f * self->tint;
    c[2] *= 1.0f - 0.2f * self->temperature;
    for (k = 0; k < 3; k++)
    {
        c[k] = MAX (c[k] + lift[k], 0.0f);
        c[k] = powf (c[k], 1.0f / gamma[k]) * gain[k];
        c[k] = (c[k] - 0.5f) * self->contrast + 0.5f;
    }
    lum = c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f;
    intensity = lrg_post_effect_get_intensity (LRG_POST_EFFECT (self));
    for (k = 0; k < 3; k++)
    {
        c[k] = lum + (c[k] - lum) * self->saturation;
        c[k] = CLAMP (c[k], 0.0f, 1.0f);
        out[k] = original[k] + (c[k] - original[k]) * intensity;
    }
}

const gchar *
lrg_color_grade_get_fragment_shader (void)
{
    return grade_fs;
}

GrlShader *
lrg_color_grade_get_shader (LrgColorGrade *self)
{
    g_autoptr(GError) error = NULL;
    Shader *handle;

    g_return_val_if_fail (LRG_IS_COLOR_GRADE (self), NULL);

    if (self->shader != NULL || self->shader_tried)
        return self->shader;
    if (!IsWindowReady ())
        return NULL;
    self->shader_tried = TRUE;

    /* on a compile failure raylib hands back its default shader */
    self->shader = grl_shader_new_from_memory (NULL, grade_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("color grade: %s", error != NULL ? error->message : "no shader");
        return NULL;
    }
    handle = grl_shader_get_handle (self->shader);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
    {
        g_warning ("color grade: the shader did not compile (GLSL 330 needed)");
        g_clear_object (&self->shader);
        return NULL;
    }
    self->loc_exposure = grl_shader_get_location (self->shader, "exposure");
    self->loc_temperature = grl_shader_get_location (self->shader, "temperature");
    self->loc_tint = grl_shader_get_location (self->shader, "tint");
    self->loc_lift = grl_shader_get_location (self->shader, "lift");
    self->loc_gamma = grl_shader_get_location (self->shader, "gamma");
    self->loc_gain = grl_shader_get_location (self->shader, "gain");
    self->loc_contrast = grl_shader_get_location (self->shader, "contrast");
    self->loc_saturation = grl_shader_get_location (self->shader, "saturation");
    self->loc_intensity = grl_shader_get_location (self->shader, "intensity");
    return self->shader;
}

gboolean
lrg_color_grade_draw_texture (LrgColorGrade      *self,
                              GrlTexture         *texture,
                              const GrlRectangle *source,
                              const GrlRectangle *dest)
{
    g_autoptr(GrlColor) white = NULL;
    GrlShader *shader;
    GrlVector2 origin;
    gboolean identity;

    g_return_val_if_fail (LRG_IS_COLOR_GRADE (self), FALSE);
    g_return_val_if_fail (GRL_IS_TEXTURE (texture), FALSE);
    g_return_val_if_fail (source != NULL, FALSE);
    g_return_val_if_fail (dest != NULL, FALSE);

    white = grl_color_new (255, 255, 255, 255);
    origin.x = 0.0f;
    origin.y = 0.0f;
    identity = lrg_color_grade_is_identity (self);
    shader = identity ? NULL : lrg_color_grade_get_shader (self);
    if (shader != NULL)
    {
        upload_uniforms (self);
        grl_shader_begin (shader);
    }
    grl_draw_texture_pro (texture, source, dest, &origin, 0.0f, white);
    if (shader != NULL)
        grl_shader_end (shader);
    return identity || shader != NULL;
}
