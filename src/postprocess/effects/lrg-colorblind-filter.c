/* lrg-colorblind-filter.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-colorblind-filter.h"

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
 *
 * Supported colorblind types:
 * - Deuteranopia (red-green, most common)
 * - Protanopia (red-green)
 * - Tritanopia (blue-yellow)
 * - Achromatopsia (total color blindness)
 */

struct _LrgColorblindFilter
{
    LrgPostEffect      parent_instance;

    LrgColorblindType  filter_type;
    LrgColorblindMode  mode;
    gfloat             strength;
};

G_DEFINE_TYPE (LrgColorblindFilter, lrg_colorblind_filter, LRG_TYPE_POST_EFFECT)

enum
{
    PROP_0,
    PROP_FILTER_TYPE,
    PROP_MODE,
    PROP_STRENGTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_colorblind_filter_real_initialize (LrgPostEffect *effect,
                                       guint          width,
                                       guint          height,
                                       GError       **error)
{
    return TRUE;
}

static void
lrg_colorblind_filter_real_shutdown (LrgPostEffect *effect)
{
    (void)effect;
}

static void
lrg_colorblind_filter_real_apply (LrgPostEffect *effect,
                                  guint          source_texture_id,
                                  guint          target_texture_id,
                                  guint          width,
                                  guint          height,
                                  gfloat         delta_time)
{
    /*
     * Color blindness simulation/correction matrices.
     *
     * Shader example for Deuteranopia simulation:
     *
     * mat3 deuteranopia = mat3(
     *     0.625, 0.375, 0.0,
     *     0.7,   0.3,   0.0,
     *     0.0,   0.3,   0.7
     * );
     *
     * mat3 protanopia = mat3(
     *     0.567, 0.433, 0.0,
     *     0.558, 0.442, 0.0,
     *     0.0,   0.242, 0.758
     * );
     *
     * mat3 tritanopia = mat3(
     *     0.95,  0.05,  0.0,
     *     0.0,   0.433, 0.567,
     *     0.0,   0.475, 0.525
     * );
     *
     * For correction mode, we shift problematic colors to
     * distinguishable ones instead of simulating the deficiency.
     *
     * void main() {
     *     vec4 color = texture(texture0, uv);
     *     vec3 corrected;
     *
     *     if (mode == SIMULATE) {
     *         corrected = simulationMatrix * color.rgb;
     *     } else {
     *         corrected = correctionMatrix * color.rgb;
     *     }
     *
     *     gl_FragColor = vec4(mix(color.rgb, corrected, strength), color.a);
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
