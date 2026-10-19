/* lrg-photo-look.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-photo-look.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-photo-look
 * @Title: LrgPhotoLook
 * @Short_description: The colour look of a photo
 *
 * #LrgPhotoLook gathers everything that changes how a photo looks after
 * the scene is rendered: exposure, contrast, saturation, white balance,
 * lift and gamma, an optional 3D lookup table, a vignette and film
 * grain.  The grade itself runs in #LrgColorGrade (the look configures
 * one with lrg_photo_look_configure_grade()); the vignette and the grain
 * run in a small shader of their own (lrg_photo_look_draw_finish()).
 * Every step also has a CPU reference, so a photo can be graded without
 * a window (lrg_photo_look_apply_image()) and the tests can check the
 * arithmetic.
 */

/* the vignette and grain shader: the same arithmetic as
 * lrg_photo_look_get_vignette_factor() and lrg_photo_look_get_grain_value() */
static const gchar finish_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec4 vig;\n"
    "uniform vec3 grn;\n"
    "out vec4 finalColor;\n"
    "uint lrg_hash(uint x, uint y, uint s)\n"
    "{\n"
    "    uint h = x * 374761393u + y * 668265263u + s * 2246822519u;\n"
    "    h = (h ^ (h >> 13u)) * 1274126177u;\n"
    "    h = h ^ (h >> 16u);\n"
    "    return h;\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    vec3 c = texel.rgb;\n"
    "    vec2 d = (fragTexCoord - 0.5) * vec2(vig.z, 1.0);\n"
    "    float r = length(d) / (0.5 * length(vec2(vig.z, 1.0)));\n"
    "    float k = clamp((r - vig.y) / (1.0 - vig.y), 0.0, 1.0);\n"
    "    k = k * k * (3.0 - 2.0 * k);\n"
    "    c *= 1.0 - vig.x * k;\n"
    "    if (vig.w > 0.0)\n"
    "    {\n"
    "        uvec2 p = uvec2(fragTexCoord * grn.yz);\n"
    "        float n = float(lrg_hash(p.x, p.y, uint(grn.x)) & 65535u) / 65535.0 - 0.5;\n"
    "        c += n * vig.w * 0.4;\n"
    "    }\n"
    "    finalColor = vec4(clamp(c, 0.0, 1.0), texel.a);\n"
    "}\n";

/* one preset's values */
typedef struct
{
    gfloat exposure;
    gfloat contrast;
    gfloat saturation;
    gfloat temperature;
    gfloat tint;
    gfloat lift;
    gfloat gamma;
    gfloat vignette;
    gfloat grain;
} PresetValues;

/* indexed by LrgPhotoPreset */
static const PresetValues presets[LRG_PHOTO_PRESET_COUNT] =
{
    /* none     */ {  0.00f, 1.00f, 1.00f,  0.00f, 0.0f, 0.00f, 1.00f, 0.00f, 0.00f },
    /* vivid    */ {  0.10f, 1.15f, 1.45f,  0.05f, 0.0f, 0.00f, 1.00f, 0.15f, 0.00f },
    /* noir     */ {  0.00f, 1.45f, 0.00f,  0.00f, 0.0f, 0.00f, 1.00f, 0.45f, 0.35f },
    /* sepia    */ {  0.00f, 1.05f, 0.28f,  0.65f, 0.1f, 0.00f, 1.00f, 0.30f, 0.15f },
    /* faded    */ {  0.15f, 0.80f, 0.70f,  0.05f, 0.0f, 0.07f, 1.10f, 0.10f, 0.10f },
    /* warm     */ {  0.10f, 1.05f, 1.10f,  0.55f, 0.0f, 0.00f, 1.00f, 0.20f, 0.00f },
    /* cool     */ {  0.00f, 1.05f, 0.90f, -0.55f, 0.0f, 0.00f, 1.00f, 0.20f, 0.00f },
    /* dramatic */ { -0.35f, 1.40f, 0.60f, -0.10f, 0.0f, 0.00f, 0.90f, 0.50f, 0.10f }
};

static const gchar * const preset_labels[LRG_PHOTO_PRESET_COUNT] =
{
    "None", "Vivid", "Noir", "Sepia", "Faded", "Warm", "Cool", "Dramatic"
};

struct _LrgPhotoLook
{
    GObject         parent_instance;

    LrgPhotoPreset  preset;
    gfloat          exposure;
    gfloat          contrast;
    gfloat          saturation;
    gfloat          temperature;
    gfloat          tint;
    gfloat          lift;
    gfloat          gamma;
    gfloat          vignette;
    gfloat          vignette_radius;
    gfloat          grain;
    gfloat          lut_strength;
    LrgColorLut    *lut;

    /* the CPU reference grade, made on first use */
    LrgColorGrade  *cpu_grade;

    /* the finish shader, compiled on first draw (needs a GL context) */
    GrlShader      *shader;
    gboolean        shader_tried;
    gint            loc_vig;
    gint            loc_grn;
};

enum
{
    PROP_0,
    PROP_PRESET,
    PROP_EXPOSURE,
    PROP_CONTRAST,
    PROP_SATURATION,
    PROP_TEMPERATURE,
    PROP_TINT,
    PROP_LIFT,
    PROP_GAMMA,
    PROP_VIGNETTE,
    PROP_VIGNETTE_RADIUS,
    PROP_GRAIN,
    PROP_LUT_STRENGTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgPhotoLook, lrg_photo_look, G_TYPE_OBJECT)

static gfloat
clampf (gfloat value, gfloat lo, gfloat hi)
{
    if (!isfinite (value))
        return lo;
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

gfloat
lrg_photo_look_get_exposure (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 0.0f);

    return self->exposure;
}

void
lrg_photo_look_set_exposure (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, -3.0f, 3.0f);
    if (self->exposure == value)
        return;
    self->exposure = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPOSURE]);
}

gfloat
lrg_photo_look_get_contrast (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 1.0f);

    return self->contrast;
}

void
lrg_photo_look_set_contrast (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, 0.25f, 2.5f);
    if (self->contrast == value)
        return;
    self->contrast = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONTRAST]);
}

gfloat
lrg_photo_look_get_saturation (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 1.0f);

    return self->saturation;
}

void
lrg_photo_look_set_saturation (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, 0.0f, 2.5f);
    if (self->saturation == value)
        return;
    self->saturation = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SATURATION]);
}

gfloat
lrg_photo_look_get_temperature (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 0.0f);

    return self->temperature;
}

void
lrg_photo_look_set_temperature (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, -1.0f, 1.0f);
    if (self->temperature == value)
        return;
    self->temperature = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEMPERATURE]);
}

gfloat
lrg_photo_look_get_tint (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 0.0f);

    return self->tint;
}

void
lrg_photo_look_set_tint (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, -1.0f, 1.0f);
    if (self->tint == value)
        return;
    self->tint = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TINT]);
}

gfloat
lrg_photo_look_get_lift (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 0.0f);

    return self->lift;
}

void
lrg_photo_look_set_lift (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, -0.2f, 0.3f);
    if (self->lift == value)
        return;
    self->lift = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIFT]);
}

gfloat
lrg_photo_look_get_gamma (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 1.0f);

    return self->gamma;
}

void
lrg_photo_look_set_gamma (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, 0.5f, 2.0f);
    if (self->gamma == value)
        return;
    self->gamma = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GAMMA]);
}

gfloat
lrg_photo_look_get_vignette (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 0.0f);

    return self->vignette;
}

void
lrg_photo_look_set_vignette (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, 0.0f, 1.0f);
    if (self->vignette == value)
        return;
    self->vignette = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIGNETTE]);
}

gfloat
lrg_photo_look_get_vignette_radius (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 0.5f);

    return self->vignette_radius;
}

void
lrg_photo_look_set_vignette_radius (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, 0.1f, 1.0f);
    if (self->vignette_radius == value)
        return;
    self->vignette_radius = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIGNETTE_RADIUS]);
}

gfloat
lrg_photo_look_get_grain (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 0.0f);

    return self->grain;
}

void
lrg_photo_look_set_grain (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, 0.0f, 1.0f);
    if (self->grain == value)
        return;
    self->grain = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRAIN]);
}

gfloat
lrg_photo_look_get_lut_strength (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 1.0f);

    return self->lut_strength;
}

void
lrg_photo_look_set_lut_strength (LrgPhotoLook *self,
                         gfloat        value)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    value = clampf (value, 0.0f, 1.0f);
    if (self->lut_strength == value)
        return;
    self->lut_strength = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LUT_STRENGTH]);
}

/* ==========================================================================
 * Presets and state
 * ========================================================================== */

const gchar *
lrg_photo_preset_get_label (LrgPhotoPreset preset)
{
    if ((gint)preset < 0 || (gint)preset >= LRG_PHOTO_PRESET_COUNT)
        return "None";
    return preset_labels[preset];
}

LrgPhotoPreset
lrg_photo_look_get_preset (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), LRG_PHOTO_PRESET_NONE);

    return self->preset;
}

void
lrg_photo_look_apply_preset (LrgPhotoLook  *self,
                             LrgPhotoPreset preset)
{
    const PresetValues *p;

    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));
    g_return_if_fail ((gint)preset >= 0 && (gint)preset < LRG_PHOTO_PRESET_COUNT);

    /* the preset's numbers replace the adjustments; the table stays */
    p = &presets[preset];
    g_object_freeze_notify (G_OBJECT (self));
    lrg_photo_look_set_exposure (self, p->exposure);
    lrg_photo_look_set_contrast (self, p->contrast);
    lrg_photo_look_set_saturation (self, p->saturation);
    lrg_photo_look_set_temperature (self, p->temperature);
    lrg_photo_look_set_tint (self, p->tint);
    lrg_photo_look_set_lift (self, p->lift);
    lrg_photo_look_set_gamma (self, p->gamma);
    lrg_photo_look_set_vignette (self, p->vignette);
    lrg_photo_look_set_grain (self, p->grain);
    if (self->preset != preset)
    {
        self->preset = preset;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRESET]);
    }
    g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_photo_look_reset (LrgPhotoLook *self)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    g_object_freeze_notify (G_OBJECT (self));
    lrg_photo_look_apply_preset (self, LRG_PHOTO_PRESET_NONE);
    lrg_photo_look_set_vignette_radius (self, 0.5f);
    lrg_photo_look_set_lut (self, NULL);
    lrg_photo_look_set_lut_strength (self, 1.0f);
    g_object_thaw_notify (G_OBJECT (self));
}

LrgColorLut *
lrg_photo_look_get_lut (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), NULL);

    return self->lut;
}

void
lrg_photo_look_set_lut (LrgPhotoLook *self,
                        LrgColorLut  *lut)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    if (self->lut == lut)
        return;
    if (lut != NULL)
        lrg_color_lut_ref (lut);
    g_clear_pointer (&self->lut, lrg_color_lut_unref);
    self->lut = lut;
}

gboolean
lrg_photo_look_has_finish (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), FALSE);

    return self->vignette > 0.0f || self->grain > 0.0f;
}

/* the grade's own steps are neutral (the table is separate) */
static gboolean
grade_is_neutral (LrgPhotoLook *self)
{
    return self->exposure == 0.0f && self->contrast == 1.0f && self->saturation == 1.0f &&
           self->temperature == 0.0f && self->tint == 0.0f && self->lift == 0.0f &&
           self->gamma == 1.0f;
}

gboolean
lrg_photo_look_is_identity (LrgPhotoLook *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), TRUE);

    return grade_is_neutral (self) && !lrg_photo_look_has_finish (self) &&
           (self->lut == NULL || self->lut_strength <= 0.0f);
}

/* ==========================================================================
 * The grade
 * ========================================================================== */

void
lrg_photo_look_configure_grade (LrgPhotoLook  *self,
                                LrgColorGrade *grade)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));
    g_return_if_fail (LRG_IS_COLOR_GRADE (grade));

    lrg_color_grade_set_exposure (grade, self->exposure);
    lrg_color_grade_set_contrast (grade, self->contrast);
    lrg_color_grade_set_saturation (grade, self->saturation);
    lrg_color_grade_set_temperature (grade, self->temperature);
    lrg_color_grade_set_tint (grade, self->tint);
    lrg_color_grade_set_lift (grade, self->lift, self->lift, self->lift);
    lrg_color_grade_set_gamma (grade, self->gamma, self->gamma, self->gamma);
    lrg_color_grade_set_lut (grade, self->lut, self->lut_strength);
}

void
lrg_photo_look_apply_rgb (LrgPhotoLook *self,
                          const gfloat *in,
                          gfloat       *out)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));
    g_return_if_fail (in != NULL && out != NULL);

    if (self->cpu_grade == NULL)
        self->cpu_grade = lrg_color_grade_new ();
    lrg_photo_look_configure_grade (self, self->cpu_grade);
    lrg_color_grade_apply_rgb (self->cpu_grade, in, out);
}

/* ==========================================================================
 * The finish: vignette and grain
 * ========================================================================== */

gfloat
lrg_photo_look_get_vignette_factor (LrgPhotoLook *self,
                                    gfloat        u,
                                    gfloat        v,
                                    gfloat        aspect)
{
    gfloat dx;
    gfloat dy;
    gfloat r;
    gfloat k;

    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 1.0f);

    if (self->vignette <= 0.0f)
        return 1.0f;
    if (!(aspect > 0.0f))
        aspect = 1.0f;

    /* the distance from the centre as a fraction of the half diagonal */
    dx = (u - 0.5f) * aspect;
    dy = v - 0.5f;
    r = sqrtf (dx * dx + dy * dy) / (0.5f * sqrtf (aspect * aspect + 1.0f));
    k = clampf ((r - self->vignette_radius) / (1.0f - self->vignette_radius), 0.0f, 1.0f);
    k = k * k * (3.0f - 2.0f * k);
    return 1.0f - self->vignette * k;
}

gfloat
lrg_photo_look_get_grain_value (LrgPhotoLook *self,
                                gint          x,
                                gint          y,
                                guint         seed)
{
    guint32 h;
    gfloat n;

    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), 0.0f);

    if (self->grain <= 0.0f)
        return 0.0f;

    /* the integer hash the shader repeats */
    h = (guint32)x * 374761393u + (guint32)y * 668265263u + (guint32)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    n = (gfloat)(h & 65535u) / 65535.0f - 0.5f;
    return n * self->grain * 0.4f;
}

void
lrg_photo_look_apply_image (LrgPhotoLook *self,
                            GrlImage     *image,
                            guint         seed)
{
    guint8 *pixels;
    gsize size;
    gint width;
    gint height;
    gint x;
    gint y;
    gfloat aspect;
    gboolean grade;
    gboolean finish;

    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));
    g_return_if_fail (GRL_IS_IMAGE (image));

    width = grl_image_get_width (image);
    height = grl_image_get_height (image);
    if (width <= 0 || height <= 0)
        return;
    pixels = grl_image_get_pixels (image, &size);
    if (pixels == NULL || size < (gsize)width * (gsize)height * 4)
        return;

    grade = !grade_is_neutral (self) || (self->lut != NULL && self->lut_strength > 0.0f);
    finish = lrg_photo_look_has_finish (self);
    if (!grade && !finish)
        return;

    aspect = (gfloat)width / (gfloat)height;
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            guint8 *px = pixels + ((gsize)y * (gsize)width + (gsize)x) * 4;
            gfloat c[3];
            gint i;

            for (i = 0; i < 3; i++)
                c[i] = (gfloat)px[i] / 255.0f;
            if (grade)
                lrg_photo_look_apply_rgb (self, c, c);
            if (finish)
            {
                gfloat f;
                gfloat g;

                /* the shader reads the pixel from its texture coordinate */
                f = lrg_photo_look_get_vignette_factor (self, ((gfloat)x + 0.5f) / (gfloat)width,
                                                        ((gfloat)y + 0.5f) / (gfloat)height, aspect);
                g = lrg_photo_look_get_grain_value (self, x, y, seed);
                for (i = 0; i < 3; i++)
                    c[i] = clampf (c[i] * f + g, 0.0f, 1.0f);
            }
            for (i = 0; i < 3; i++)
                px[i] = (guint8)(clampf (c[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

/* the shader, NULL without a window or when it did not compile */
static GrlShader *
get_shader (LrgPhotoLook *self)
{
    g_autoptr(GError) error = NULL;
    Shader *handle;

    if (self->shader != NULL || self->shader_tried)
        return self->shader;
    if (!IsWindowReady ())
        return NULL;
    self->shader_tried = TRUE;

    self->shader = grl_shader_new_from_memory (NULL, finish_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("photo look: %s", error != NULL ? error->message : "no shader");
        return NULL;
    }
    handle = grl_shader_get_handle (self->shader);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
    {
        g_warning ("photo look: the finish shader did not compile (GLSL 330 needed)");
        g_clear_object (&self->shader);
        return NULL;
    }
    self->loc_vig = grl_shader_get_location (self->shader, "vig");
    self->loc_grn = grl_shader_get_location (self->shader, "grn");
    return self->shader;
}

gboolean
lrg_photo_look_draw_finish (LrgPhotoLook       *self,
                            GrlTexture         *texture,
                            const GrlRectangle *source,
                            const GrlRectangle *dest,
                            guint               seed)
{
    g_autoptr(GrlColor) white = NULL;
    GrlShader *shader;
    GrlVector2 origin;
    gboolean needed;
    gfloat aspect;
    gfloat w;
    gfloat h;

    g_return_val_if_fail (LRG_IS_PHOTO_LOOK (self), FALSE);
    g_return_val_if_fail (GRL_IS_TEXTURE (texture), FALSE);
    g_return_val_if_fail (source != NULL && dest != NULL, FALSE);

    white = grl_color_new (255, 255, 255, 255);
    origin.x = 0.0f;
    origin.y = 0.0f;
    needed = lrg_photo_look_has_finish (self);
    shader = needed ? get_shader (self) : NULL;
    if (shader != NULL)
    {
        w = fabsf (source->width);
        h = fabsf (source->height);
        aspect = h > 0.0f ? w / h : 1.0f;
        grl_shader_set_value_vec4 (shader, self->loc_vig, self->vignette, self->vignette_radius,
                                   aspect, self->grain);
        grl_shader_set_value_vec3 (shader, self->loc_grn, (gfloat)(seed & 0xffffffu), w, h);
        grl_shader_begin (shader);
    }
    grl_draw_texture_pro (texture, source, dest, &origin, 0.0f, white);
    if (shader != NULL)
        grl_shader_end (shader);
    return !needed || shader != NULL;
}

void
lrg_photo_look_release_gpu (LrgPhotoLook *self)
{
    g_return_if_fail (LRG_IS_PHOTO_LOOK (self));

    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
    if (self->cpu_grade != NULL)
        g_clear_object (&self->cpu_grade);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_photo_look_finalize (GObject *object)
{
    LrgPhotoLook *self = LRG_PHOTO_LOOK (object);

    g_clear_object (&self->shader);
    g_clear_object (&self->cpu_grade);
    g_clear_pointer (&self->lut, lrg_color_lut_unref);

    G_OBJECT_CLASS (lrg_photo_look_parent_class)->finalize (object);
}

static void
lrg_photo_look_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgPhotoLook *self = LRG_PHOTO_LOOK (object);

    switch (prop_id)
    {
    case PROP_PRESET:
        g_value_set_enum (value, self->preset);
        break;
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
    case PROP_LIFT:
        g_value_set_float (value, self->lift);
        break;
    case PROP_GAMMA:
        g_value_set_float (value, self->gamma);
        break;
    case PROP_VIGNETTE:
        g_value_set_float (value, self->vignette);
        break;
    case PROP_VIGNETTE_RADIUS:
        g_value_set_float (value, self->vignette_radius);
        break;
    case PROP_GRAIN:
        g_value_set_float (value, self->grain);
        break;
    case PROP_LUT_STRENGTH:
        g_value_set_float (value, self->lut_strength);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_photo_look_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgPhotoLook *self = LRG_PHOTO_LOOK (object);

    switch (prop_id)
    {
    case PROP_PRESET:
        lrg_photo_look_apply_preset (self, g_value_get_enum (value));
        break;
    case PROP_EXPOSURE:
        lrg_photo_look_set_exposure (self, g_value_get_float (value));
        break;
    case PROP_CONTRAST:
        lrg_photo_look_set_contrast (self, g_value_get_float (value));
        break;
    case PROP_SATURATION:
        lrg_photo_look_set_saturation (self, g_value_get_float (value));
        break;
    case PROP_TEMPERATURE:
        lrg_photo_look_set_temperature (self, g_value_get_float (value));
        break;
    case PROP_TINT:
        lrg_photo_look_set_tint (self, g_value_get_float (value));
        break;
    case PROP_LIFT:
        lrg_photo_look_set_lift (self, g_value_get_float (value));
        break;
    case PROP_GAMMA:
        lrg_photo_look_set_gamma (self, g_value_get_float (value));
        break;
    case PROP_VIGNETTE:
        lrg_photo_look_set_vignette (self, g_value_get_float (value));
        break;
    case PROP_VIGNETTE_RADIUS:
        lrg_photo_look_set_vignette_radius (self, g_value_get_float (value));
        break;
    case PROP_GRAIN:
        lrg_photo_look_set_grain (self, g_value_get_float (value));
        break;
    case PROP_LUT_STRENGTH:
        lrg_photo_look_set_lut_strength (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_photo_look_class_init (LrgPhotoLookClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_photo_look_finalize;
    object_class->get_property = lrg_photo_look_get_property;
    object_class->set_property = lrg_photo_look_set_property;

    /**
     * LrgPhotoLook:preset:
     *
     * The preset last applied; setting it applies the preset.
     *
     * Since: 1.0
     */
    properties[PROP_PRESET] =
        g_param_spec_enum ("preset", "Preset", "The last applied preset",
                           LRG_TYPE_PHOTO_PRESET, LRG_PHOTO_PRESET_NONE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:exposure:
     *
     * The exposure compensation in stops (EV).
     *
     * Since: 1.0
     */
    properties[PROP_EXPOSURE] =
        g_param_spec_float ("exposure", "exposure", "The exposure compensation in stops (EV)",
                            -3.0f, 3.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:contrast:
     *
     * The contrast around mid grey (1 is neutral).
     *
     * Since: 1.0
     */
    properties[PROP_CONTRAST] =
        g_param_spec_float ("contrast", "contrast", "The contrast around mid grey (1 is neutral)",
                            0.25f, 2.5f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:saturation:
     *
     * The colour saturation (0 is monochrome, 1 neutral).
     *
     * Since: 1.0
     */
    properties[PROP_SATURATION] =
        g_param_spec_float ("saturation", "saturation", "The colour saturation (0 is monochrome, 1 neutral)",
                            0.0f, 2.5f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:temperature:
     *
     * The white balance (negative cool, positive warm).
     *
     * Since: 1.0
     */
    properties[PROP_TEMPERATURE] =
        g_param_spec_float ("temperature", "temperature", "The white balance (negative cool, positive warm)",
                            -1.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:tint:
     *
     * The white balance tint (negative magenta, positive green).
     *
     * Since: 1.0
     */
    properties[PROP_TINT] =
        g_param_spec_float ("tint", "tint", "The white balance tint (negative magenta, positive green)",
                            -1.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:lift:
     *
     * The shadow lift added to every channel.
     *
     * Since: 1.0
     */
    properties[PROP_LIFT] =
        g_param_spec_float ("lift", "lift", "The shadow lift added to every channel",
                            -0.2f, 0.3f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:gamma:
     *
     * The midtone gamma (above 1 brightens the midtones).
     *
     * Since: 1.0
     */
    properties[PROP_GAMMA] =
        g_param_spec_float ("gamma", "gamma", "The midtone gamma (above 1 brightens the midtones)",
                            0.5f, 2.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:vignette:
     *
     * The vignette strength (0 off, 1 black corners).
     *
     * Since: 1.0
     */
    properties[PROP_VIGNETTE] =
        g_param_spec_float ("vignette", "vignette", "The vignette strength (0 off, 1 black corners)",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:vignette-radius:
     *
     * The radius where the vignette starts, as a fraction of the half diagonal.
     *
     * Since: 1.0
     */
    properties[PROP_VIGNETTE_RADIUS] =
        g_param_spec_float ("vignette-radius", "vignette-radius", "The radius where the vignette starts, as a fraction of the half diagonal",
                            0.1f, 1.0f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:grain:
     *
     * The film grain amount (0 off).
     *
     * Since: 1.0
     */
    properties[PROP_GRAIN] =
        g_param_spec_float ("grain", "grain", "The film grain amount (0 off)",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoLook:lut-strength:
     *
     * The how strongly the lookup table is mixed in.
     *
     * Since: 1.0
     */
    properties[PROP_LUT_STRENGTH] =
        g_param_spec_float ("lut-strength", "lut-strength", "The how strongly the lookup table is mixed in",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_photo_look_init (LrgPhotoLook *self)
{
    self->preset = LRG_PHOTO_PRESET_NONE;
    self->exposure = 0.0f;
    self->contrast = 1.0f;
    self->saturation = 1.0f;
    self->temperature = 0.0f;
    self->tint = 0.0f;
    self->lift = 0.0f;
    self->gamma = 1.0f;
    self->vignette = 0.0f;
    self->vignette_radius = 0.5f;
    self->grain = 0.0f;
    self->lut_strength = 1.0f;
}

LrgPhotoLook *
lrg_photo_look_new (void)
{
    return g_object_new (LRG_TYPE_PHOTO_LOOK, NULL);
}

LrgPhotoLook *
lrg_photo_look_new_for_preset (LrgPhotoPreset preset)
{
    LrgPhotoLook *self;

    self = lrg_photo_look_new ();
    lrg_photo_look_apply_preset (self, preset);
    return self;
}
