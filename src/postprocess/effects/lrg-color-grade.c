/* lrg-color-grade.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-color-grade.h"
#include <math.h>

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
 */

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
    (void)effect;
}

static void
lrg_color_grade_real_apply (LrgPostEffect *effect,
                            guint          source_texture_id,
                            guint          target_texture_id,
                            guint          width,
                            guint          height,
                            gfloat         delta_time)
{
    /*
     * Color grading shader example:
     *
     * vec3 colorGrade(vec3 color) {
     *     // Exposure
     *     color *= pow(2.0, exposure);
     *
     *     // Temperature/Tint
     *     color.r += temperature * 0.1;
     *     color.b -= temperature * 0.1;
     *     color.g += tint * 0.1;
     *
     *     // Lift/Gamma/Gain
     *     color = pow(max(color + lift, 0.0), 1.0 / gamma) * gain;
     *
     *     // Contrast (around midpoint 0.5)
     *     color = (color - 0.5) * contrast + 0.5;
     *
     *     // Saturation
     *     float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
     *     color = mix(vec3(lum), color, saturation);
     *
     *     return color;
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
lrg_color_grade_class_init (LrgColorGradeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

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

    self->gamma_r = CLAMP (r, -1.0f, 1.0f);
    self->gamma_g = CLAMP (g, -1.0f, 1.0f);
    self->gamma_b = CLAMP (b, -1.0f, 1.0f);
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

    self->gain_r = CLAMP (r, -1.0f, 1.0f);
    self->gain_g = CLAMP (g, -1.0f, 1.0f);
    self->gain_b = CLAMP (b, -1.0f, 1.0f);
}
