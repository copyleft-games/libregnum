/* lrg-colorblind-filter.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-colorblind-filter.h"
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include "core/lrg-prewarm.h"

/**
 * SECTION:lrg-colorblind-filter
 * @Title: LrgColorblindFilter
 * @Short_description: Colorblind accessibility filter
 *
 * #LrgColorblindFilter provides accessibility support for colorblind
 * players. It can operate in two modes:
 *
 * - Simulate: Shows what colorblind players see (for testing)
 * - Correct: Adjusts colors to improve visibility for colorblind players
 *   (daltonization)
 *
 * Supported colorblind types:
 * - Deuteranopia (red-green, most common)
 * - Protanopia (red-green)
 * - Tritanopia (blue-yellow)
 * - Achromatopsia (total color blindness; simulation only)
 *
 * The filter is one 3x3 matrix in linear light
 * (lrg_colorblind_filter_get_matrix()).  It runs as a GLSL 330 fragment
 * shader when applied by a post processor or drawn with
 * lrg_colorblind_filter_draw_texture(); lrg_colorblind_filter_apply_rgb()
 * is the same arithmetic on the CPU, the reference the tests check.
 */

/* the shader mirrors filter_rgb() below: linear light, the matrix,
 * back to sRGB, mixed by the intensity */
static const gchar colorblind_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 row0;\n"
    "uniform vec3 row1;\n"
    "uniform vec3 row2;\n"
    "uniform float intensity;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    vec3 lin = pow(max(texel.rgb, vec3(0.0)), vec3(2.2));\n"
    "    vec3 m = vec3(dot(row0, lin), dot(row1, lin), dot(row2, lin));\n"
    "    vec3 c = pow(clamp(m, 0.0, 1.0), vec3(1.0 / 2.2));\n"
    "    finalColor = vec4(mix(texel.rgb, c, intensity), texel.a);\n"
    "}\n";

/* Machado, Oliveira and Fernandes (2009), severity 1.0, linear RGB */
static const gfloat sim_protanopia[9] = {
     0.152286f,  1.052583f, -0.204868f,
     0.114503f,  0.786281f,  0.099216f,
    -0.003882f, -0.048116f,  1.051998f
};
static const gfloat sim_deuteranopia[9] = {
     0.367322f,  0.860646f, -0.227968f,
     0.280085f,  0.672501f,  0.047413f,
    -0.011820f,  0.042940f,  0.968881f
};
static const gfloat sim_tritanopia[9] = {
     1.255528f, -0.076749f, -0.178779f,
    -0.078411f,  0.930809f,  0.147602f,
     0.004733f,  0.691367f,  0.303900f
};
/* Rec. 709 luminance on every row */
static const gfloat sim_achromatopsia[9] = {
    0.2126f, 0.7152f, 0.0722f,
    0.2126f, 0.7152f, 0.0722f,
    0.2126f, 0.7152f, 0.0722f
};

/* daltonization: where the lost error goes (Fidaner et al.) */
static const gfloat shift_red_green[9] = {
    0.0f, 0.0f, 0.0f,
    0.7f, 1.0f, 0.0f,
    0.7f, 0.0f, 1.0f
};
static const gfloat shift_blue_yellow[9] = {
    1.0f, 0.0f, 0.7f,
    0.0f, 1.0f, 0.7f,
    0.0f, 0.0f, 0.0f
};

static const gfloat identity3[9] = {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f
};

struct _LrgColorblindFilter
{
    LrgPostEffect      parent_instance;

    LrgColorblindType  filter_type;
    LrgColorblindMode  mode;
    gfloat             strength;

    /* the shader, compiled on first use (needs a GL context) */
    GrlShader         *shader;
    gboolean           shader_tried;
    gint               loc_row0;
    gint               loc_row1;
    gint               loc_row2;
    gint               loc_intensity;
};

static void lrg_colorblind_filter_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgColorblindFilter, lrg_colorblind_filter, LRG_TYPE_POST_EFFECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                lrg_colorblind_filter_prewarmable_init))

enum
{
    PROP_0,
    PROP_FILTER_TYPE,
    PROP_MODE,
    PROP_STRENGTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* the full-severity simulation matrix of @type, NULL for none */
static const gfloat *
simulation_matrix (LrgColorblindType type)
{
    switch (type)
    {
    case LRG_COLORBLIND_PROTANOPIA:
        return sim_protanopia;
    case LRG_COLORBLIND_DEUTERANOPIA:
        return sim_deuteranopia;
    case LRG_COLORBLIND_TRITANOPIA:
        return sim_tritanopia;
    case LRG_COLORBLIND_ACHROMATOPSIA:
        return sim_achromatopsia;
    case LRG_COLORBLIND_NONE:
    default:
        return NULL;
    }
}

/* out = a * b, 3x3 row-major (out must not alias) */
static void
mat3_mul (const gfloat *a,
          const gfloat *b,
          gfloat       *out)
{
    guint r;
    guint c;
    guint k;

    for (r = 0; r < 3; r++)
    {
        for (c = 0; c < 3; c++)
        {
            gfloat sum = 0.0f;

            for (k = 0; k < 3; k++)
                sum += a[r * 3 + k] * b[k * 3 + c];
            out[r * 3 + c] = sum;
        }
    }
}

/* the matrix before the strength mix */
static void
full_matrix (LrgColorblindFilter *self,
             gfloat              *out)
{
    const gfloat *sim;
    const gfloat *shift;
    gfloat err[9];
    gfloat moved[9];
    guint i;

    sim = simulation_matrix (self->filter_type);
    if (sim == NULL ||
        (self->mode == LRG_COLORBLIND_MODE_CORRECT &&
         self->filter_type == LRG_COLORBLIND_ACHROMATOPSIA))
    {
        for (i = 0; i < 9; i++)
            out[i] = identity3[i];
        return;
    }
    if (self->mode == LRG_COLORBLIND_MODE_SIMULATE)
    {
        for (i = 0; i < 9; i++)
            out[i] = sim[i];
        return;
    }

    /* correction: I + D (I - S) */
    shift = self->filter_type == LRG_COLORBLIND_TRITANOPIA ? shift_blue_yellow : shift_red_green;
    for (i = 0; i < 9; i++)
        err[i] = identity3[i] - sim[i];
    mat3_mul (shift, err, moved);
    for (i = 0; i < 9; i++)
        out[i] = identity3[i] + moved[i];
}

void
lrg_colorblind_filter_get_matrix (LrgColorblindFilter *self,
                                  gfloat              *matrix)
{
    gfloat full[9];
    guint i;

    g_return_if_fail (LRG_IS_COLORBLIND_FILTER (self));
    g_return_if_fail (matrix != NULL);

    full_matrix (self, full);
    for (i = 0; i < 9; i++)
        matrix[i] = identity3[i] + (full[i] - identity3[i]) * self->strength;
}

gboolean
lrg_colorblind_filter_is_identity (LrgColorblindFilter *self)
{
    g_return_val_if_fail (LRG_IS_COLORBLIND_FILTER (self), TRUE);

    return self->filter_type == LRG_COLORBLIND_NONE || self->strength <= 0.0f ||
           (self->mode == LRG_COLORBLIND_MODE_CORRECT &&
            self->filter_type == LRG_COLORBLIND_ACHROMATOPSIA) ||
           lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)) <= 0.0f;
}

void
lrg_colorblind_filter_apply_rgb (LrgColorblindFilter *self,
                                 const gfloat        *in,
                                 gfloat              *out)
{
    gfloat m[9];
    gfloat lin[3];
    gfloat original[3];
    gfloat intensity;
    guint k;

    g_return_if_fail (LRG_IS_COLORBLIND_FILTER (self));
    g_return_if_fail (in != NULL);
    g_return_if_fail (out != NULL);

    for (k = 0; k < 3; k++)
    {
        original[k] = in[k];
        lin[k] = powf (MAX (in[k], 0.0f), 2.2f);
    }
    lrg_colorblind_filter_get_matrix (self, m);
    intensity = lrg_post_effect_get_intensity (LRG_POST_EFFECT (self));
    for (k = 0; k < 3; k++)
    {
        gfloat v;

        v = m[k * 3] * lin[0] + m[k * 3 + 1] * lin[1] + m[k * 3 + 2] * lin[2];
        v = powf (CLAMP (v, 0.0f, 1.0f), 1.0f / 2.2f);
        out[k] = original[k] + (v - original[k]) * intensity;
    }
}

const gchar *
lrg_colorblind_filter_get_fragment_shader (void)
{
    return colorblind_fs;
}

GrlShader *
lrg_colorblind_filter_get_shader (LrgColorblindFilter *self)
{
    g_autoptr(GError) error = NULL;
    Shader *handle;

    g_return_val_if_fail (LRG_IS_COLORBLIND_FILTER (self), NULL);

    if (self->shader != NULL || self->shader_tried)
        return self->shader;
    if (!IsWindowReady ())
        return NULL;
    self->shader_tried = TRUE;

    /* on a compile failure raylib hands back its default shader */
    self->shader = grl_shader_new_from_memory (NULL, colorblind_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("colorblind filter: %s", error != NULL ? error->message : "no shader");
        return NULL;
    }
    handle = grl_shader_get_handle (self->shader);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
    {
        g_warning ("colorblind filter: the shader did not compile (GLSL 330 needed)");
        g_clear_object (&self->shader);
        return NULL;
    }
    self->loc_row0 = grl_shader_get_location (self->shader, "row0");
    self->loc_row1 = grl_shader_get_location (self->shader, "row1");
    self->loc_row2 = grl_shader_get_location (self->shader, "row2");
    self->loc_intensity = grl_shader_get_location (self->shader, "intensity");
    return self->shader;
}

/* uploads the matrix and the intensity; the shader must be valid */
static void
upload_uniforms (LrgColorblindFilter *self)
{
    gfloat m[9];

    lrg_colorblind_filter_get_matrix (self, m);
    grl_shader_set_value_vec3 (self->shader, self->loc_row0, m[0], m[1], m[2]);
    grl_shader_set_value_vec3 (self->shader, self->loc_row1, m[3], m[4], m[5]);
    grl_shader_set_value_vec3 (self->shader, self->loc_row2, m[6], m[7], m[8]);
    grl_shader_set_value_float (self->shader, self->loc_intensity,
                                lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)));
}

gboolean
lrg_colorblind_filter_draw_texture (LrgColorblindFilter *self,
                                    GrlTexture          *texture,
                                    const GrlRectangle  *source,
                                    const GrlRectangle  *dest)
{
    g_autoptr(GrlColor) white = NULL;
    GrlShader *shader;
    GrlVector2 origin;
    gboolean identity;

    g_return_val_if_fail (LRG_IS_COLORBLIND_FILTER (self), FALSE);
    g_return_val_if_fail (GRL_IS_TEXTURE (texture), FALSE);
    g_return_val_if_fail (source != NULL, FALSE);
    g_return_val_if_fail (dest != NULL, FALSE);

    white = grl_color_new (255, 255, 255, 255);
    origin.x = 0.0f;
    origin.y = 0.0f;
    identity = lrg_colorblind_filter_is_identity (self);
    shader = identity ? NULL : lrg_colorblind_filter_get_shader (self);
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

static gboolean
lrg_colorblind_filter_real_initialize (LrgPostEffect *effect,
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
lrg_colorblind_filter_real_shutdown (LrgPostEffect *effect)
{
    LrgColorblindFilter *self = LRG_COLORBLIND_FILTER (effect);

    /* the shader belongs to the context being shut down */
    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

/*
 * The processor binds @target (or the screen) before calling: the
 * source texture is drawn over the whole target through the shader,
 * flipped (render textures are stored bottom up).
 */
static void
lrg_colorblind_filter_real_apply (LrgPostEffect *effect,
                                  guint          source_texture_id,
                                  guint          target_texture_id,
                                  guint          width,
                                  guint          height,
                                  gfloat         delta_time)
{
    LrgColorblindFilter *self = LRG_COLORBLIND_FILTER (effect);
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

    shader = lrg_colorblind_filter_is_identity (self) ? NULL : lrg_colorblind_filter_get_shader (self);
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
lrg_colorblind_filter_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "colorblind-filter";
}

static void
lrg_colorblind_filter_real_resize (LrgPostEffect *effect,
                                   guint          width,
                                   guint          height)
{
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_colorblind_filter_finalize (GObject *object)
{
    LrgColorblindFilter *self = LRG_COLORBLIND_FILTER (object);

    g_clear_object (&self->shader);

    G_OBJECT_CLASS (lrg_colorblind_filter_parent_class)->finalize (object);
}

static void
lrg_colorblind_filter_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgColorblindFilter *self = LRG_COLORBLIND_FILTER (object);

    switch (prop_id)
    {
    case PROP_FILTER_TYPE:
        g_value_set_enum (value, self->filter_type);
        break;
    case PROP_MODE:
        g_value_set_enum (value, self->mode);
        break;
    case PROP_STRENGTH:
        g_value_set_float (value, self->strength);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_colorblind_filter_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgColorblindFilter *self = LRG_COLORBLIND_FILTER (object);

    switch (prop_id)
    {
    case PROP_FILTER_TYPE:
        lrg_colorblind_filter_set_filter_type (self, g_value_get_enum (value));
        break;
    case PROP_MODE:
        lrg_colorblind_filter_set_mode (self, g_value_get_enum (value));
        break;
    case PROP_STRENGTH:
        lrg_colorblind_filter_set_strength (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_colorblind_filter_class_init (LrgColorblindFilterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->finalize = lrg_colorblind_filter_finalize;
    object_class->get_property = lrg_colorblind_filter_get_property;
    object_class->set_property = lrg_colorblind_filter_set_property;

    effect_class->initialize = lrg_colorblind_filter_real_initialize;
    effect_class->shutdown = lrg_colorblind_filter_real_shutdown;
    effect_class->apply = lrg_colorblind_filter_real_apply;
    effect_class->resize = lrg_colorblind_filter_real_resize;
    effect_class->get_name = lrg_colorblind_filter_real_get_name;

    properties[PROP_FILTER_TYPE] =
        g_param_spec_enum ("filter-type",
                           "Filter Type",
                           "Type of colorblindness",
                           LRG_TYPE_COLORBLIND_TYPE,
                           LRG_COLORBLIND_NONE,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_MODE] =
        g_param_spec_enum ("mode",
                           "Mode",
                           "Filter mode (simulate or correct)",
                           LRG_TYPE_COLORBLIND_MODE,
                           LRG_COLORBLIND_MODE_CORRECT,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_STRENGTH] =
        g_param_spec_float ("strength",
                            "Strength",
                            "Filter strength",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_colorblind_filter_init (LrgColorblindFilter *self)
{
    self->filter_type = LRG_COLORBLIND_NONE;
    self->mode = LRG_COLORBLIND_MODE_CORRECT;
    self->strength = 1.0f;
    self->loc_row0 = -1;
    self->loc_row1 = -1;
    self->loc_row2 = -1;
    self->loc_intensity = -1;

    /* build the GPU resources behind a loading screen, not on the
     * first draw (see lrg_prewarm_track()) */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgColorblindFilter *
lrg_colorblind_filter_new (void)
{
    return g_object_new (LRG_TYPE_COLORBLIND_FILTER, NULL);
}

LrgColorblindFilter *
lrg_colorblind_filter_new_with_type (LrgColorblindType filter_type)
{
    return g_object_new (LRG_TYPE_COLORBLIND_FILTER,
                         "filter-type", filter_type,
                         NULL);
}

LrgColorblindType
lrg_colorblind_filter_get_filter_type (LrgColorblindFilter *self)
{
    g_return_val_if_fail (LRG_IS_COLORBLIND_FILTER (self), LRG_COLORBLIND_NONE);
    return self->filter_type;
}

void
lrg_colorblind_filter_set_filter_type (LrgColorblindFilter *self,
                                       LrgColorblindType    filter_type)
{
    g_return_if_fail (LRG_IS_COLORBLIND_FILTER (self));

    if (self->filter_type == filter_type)
        return;

    self->filter_type = filter_type;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTER_TYPE]);
}

LrgColorblindMode
lrg_colorblind_filter_get_mode (LrgColorblindFilter *self)
{
    g_return_val_if_fail (LRG_IS_COLORBLIND_FILTER (self), LRG_COLORBLIND_MODE_CORRECT);
    return self->mode;
}

void
lrg_colorblind_filter_set_mode (LrgColorblindFilter *self,
                                LrgColorblindMode    mode)
{
    g_return_if_fail (LRG_IS_COLORBLIND_FILTER (self));

    if (self->mode == mode)
        return;

    self->mode = mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

gfloat
lrg_colorblind_filter_get_strength (LrgColorblindFilter *self)
{
    g_return_val_if_fail (LRG_IS_COLORBLIND_FILTER (self), 1.0f);
    return self->strength;
}

void
lrg_colorblind_filter_set_strength (LrgColorblindFilter *self,
                                    gfloat               strength)
{
    g_return_if_fail (LRG_IS_COLORBLIND_FILTER (self));

    strength = CLAMP (strength, 0.0f, 1.0f);
    if (self->strength == strength)
        return;

    self->strength = strength;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STRENGTH]);
}

/* ========================================================================== */
/* Prewarm                                                                    */
/* ========================================================================== */

/* lrg_colorblind_filter_prewarm: compiles the filter shader now, not on
 * the first frame a filter is chosen */
static gboolean
lrg_colorblind_filter_prewarm (LrgPrewarmable *prewarmable)
{
    lrg_colorblind_filter_get_shader (LRG_COLORBLIND_FILTER (prewarmable));
    return TRUE;
}

static void
lrg_colorblind_filter_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_colorblind_filter_prewarm;
}
