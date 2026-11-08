/* lrg-vignette.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-vignette.h"
#include <raylib.h>
#include <rlgl.h>
#include "core/lrg-prewarm.h"

/*
 * One pass: darken towards @color with distance from the centre. The
 * distance is scaled so 1.0 is a corner of a square screen; @roundness
 * blends between following the screen rectangle (0) and a true circle (1).
 * The same arithmetic as the editor preview pass in LrgWorld3DRenderer.
 */
static const gchar vignette_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 resolution;\n"
    "uniform vec4 params;\n"   /* intensity, radius, smoothness, roundness */
    "uniform vec3 color;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 c = texture(texture0, fragTexCoord);\n"
    "    vec2 d = fragTexCoord - 0.5;\n"
    "    float aspect = resolution.x / max(resolution.y, 1.0);\n"
    "    float dist;\n"
    "    float v;\n"
    "    d.x *= mix(1.0, aspect, params.w);\n"
    "    dist = length(d) * 1.41421;\n"
    "    v = smoothstep(params.y, params.y + max(params.z, 0.001), dist) * params.x;\n"
    "    finalColor = vec4(mix(c.rgb, color, v), c.a);\n"
    "}\n";

/**
 * SECTION:lrg-vignette
 * @Title: LrgVignette
 * @Short_description: Vignette post-processing effect
 *
 * #LrgVignette creates a radial darkening effect around the edges
 * of the screen. This effect is commonly used to:
 *
 * - Draw focus to the center of the screen
 * - Create a cinematic look
 * - Simulate camera lens effects
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgVignette *vignette = lrg_vignette_new ();
 * lrg_vignette_set_intensity (vignette, 0.5f);
 * lrg_vignette_set_radius (vignette, 0.7f);
 * lrg_vignette_set_smoothness (vignette, 0.3f);
 * lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (vignette));
 * ]|
 */

struct _LrgVignette
{
    LrgPostEffect  parent_instance;

    gfloat         intensity;
    gfloat         radius;
    gfloat         smoothness;
    gfloat         roundness;
    gfloat         color_r;
    gfloat         color_g;
    gfloat         color_b;

    GrlShader     *shader;
    gboolean       shader_tried;
    gint           loc_resolution;
    gint           loc_params;
    gint           loc_color;
};

static void lrg_vignette_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgVignette, lrg_vignette, LRG_TYPE_POST_EFFECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                lrg_vignette_prewarmable_init))

enum
{
    PROP_0,
    PROP_INTENSITY,
    PROP_RADIUS,
    PROP_SMOOTHNESS,
    PROP_ROUNDNESS,
    PROP_COLOR_R,
    PROP_COLOR_G,
    PROP_COLOR_B,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_vignette_real_initialize (LrgPostEffect *effect,
                              guint          width,
                              guint          height,
                              GError       **error)
{
    /* No special resources needed for vignette */
    return TRUE;
}

static void
lrg_vignette_real_shutdown (LrgPostEffect *effect)
{
    LrgVignette *self = LRG_VIGNETTE (effect);

    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

/* Compiles the pass shader once (needs a window) */
static GrlShader *
vignette_get_shader (LrgVignette *self)
{
    g_autoptr(GError) error = NULL;

    if (self->shader != NULL || self->shader_tried || !IsWindowReady ())
        return self->shader;
    self->shader_tried = TRUE;
    self->shader = lrg_post_effect_compile_shader (vignette_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("vignette: %s", error->message);
        return NULL;
    }
    self->loc_resolution = grl_shader_get_location (self->shader, "resolution");
    self->loc_params = grl_shader_get_location (self->shader, "params");
    self->loc_color = grl_shader_get_location (self->shader, "color");
    return self->shader;
}

static void
lrg_vignette_real_apply (LrgPostEffect *effect,
                         guint          source_texture_id,
                         guint          target_texture_id,
                         guint          width,
                         guint          height,
                         gfloat         delta_time)
{
    LrgVignette *self = LRG_VIGNETTE (effect);
    GrlShader *shader;
    gfloat strength;

    (void)target_texture_id;
    (void)delta_time;

    /* the effect's own intensity scaled by the base (blend) intensity */
    strength = self->intensity * lrg_post_effect_get_intensity (effect);
    shader = strength > 0.0f ? vignette_get_shader (self) : NULL;
    if (shader != NULL)
    {
        grl_shader_set_value_vec2 (shader, self->loc_resolution, (gfloat)width, (gfloat)height);
        grl_shader_set_value_vec4 (shader, self->loc_params, strength, self->radius,
                                   self->smoothness, self->roundness);
        grl_shader_set_value_vec3 (shader, self->loc_color, self->color_r, self->color_g,
                                   self->color_b);
        grl_shader_begin (shader);
    }
    lrg_post_effect_draw_fullscreen (source_texture_id, width, height);
    if (shader != NULL)
        grl_shader_end (shader);
}

static const gchar *
lrg_vignette_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "vignette";
}

static void
lrg_vignette_real_resize (LrgPostEffect *effect,
                          guint          width,
                          guint          height)
{
    /* No resize handling needed */
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_vignette_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgVignette *self = LRG_VIGNETTE (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        g_value_set_float (value, self->intensity);
        break;
    case PROP_RADIUS:
        g_value_set_float (value, self->radius);
        break;
    case PROP_SMOOTHNESS:
        g_value_set_float (value, self->smoothness);
        break;
    case PROP_ROUNDNESS:
        g_value_set_float (value, self->roundness);
        break;
    case PROP_COLOR_R:
        g_value_set_float (value, self->color_r);
        break;
    case PROP_COLOR_G:
        g_value_set_float (value, self->color_g);
        break;
    case PROP_COLOR_B:
        g_value_set_float (value, self->color_b);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vignette_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgVignette *self = LRG_VIGNETTE (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        lrg_vignette_set_intensity (self, g_value_get_float (value));
        break;
    case PROP_RADIUS:
        lrg_vignette_set_radius (self, g_value_get_float (value));
        break;
    case PROP_SMOOTHNESS:
        lrg_vignette_set_smoothness (self, g_value_get_float (value));
        break;
    case PROP_ROUNDNESS:
        lrg_vignette_set_roundness (self, g_value_get_float (value));
        break;
    case PROP_COLOR_R:
        lrg_vignette_set_color (self, g_value_get_float (value), self->color_g, self->color_b);
        break;
    case PROP_COLOR_G:
        lrg_vignette_set_color (self, self->color_r, g_value_get_float (value), self->color_b);
        break;
    case PROP_COLOR_B:
        lrg_vignette_set_color (self, self->color_r, self->color_g, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vignette_class_init (LrgVignetteClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->get_property = lrg_vignette_get_property;
    object_class->set_property = lrg_vignette_set_property;

    effect_class->initialize = lrg_vignette_real_initialize;
    effect_class->shutdown = lrg_vignette_real_shutdown;
    effect_class->apply = lrg_vignette_real_apply;
    effect_class->resize = lrg_vignette_real_resize;
    effect_class->get_name = lrg_vignette_real_get_name;

    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity",
                            "Intensity",
                            "Vignette intensity",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_RADIUS] =
        g_param_spec_float ("radius",
                            "Radius",
                            "Inner radius where vignette starts",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_SMOOTHNESS] =
        g_param_spec_float ("smoothness",
                            "Smoothness",
                            "Smoothness of the vignette edge",
                            0.0f, 1.0f, 0.3f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_ROUNDNESS] =
        g_param_spec_float ("roundness",
                            "Roundness",
                            "Roundness of the vignette shape",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgVignette:color-r:
     *
     * Red component of the colour the edges fade to (0..1).
     */
    properties[PROP_COLOR_R] =
        g_param_spec_float ("color-r", "Color R", "Edge colour red", 0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgVignette:color-g:
     *
     * Green component of the colour the edges fade to (0..1).
     */
    properties[PROP_COLOR_G] =
        g_param_spec_float ("color-g", "Color G", "Edge colour green", 0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgVignette:color-b:
     *
     * Blue component of the colour the edges fade to (0..1).
     */
    properties[PROP_COLOR_B] =
        g_param_spec_float ("color-b", "Color B", "Edge colour blue", 0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_vignette_init (LrgVignette *self)
{
    self->intensity = 0.5f;
    self->radius = 0.5f;
    self->smoothness = 0.3f;
    self->roundness = 1.0f;
    self->color_r = 0.0f;
    self->color_g = 0.0f;
    self->color_b = 0.0f;

    /* compile ahead of the first frame when a loading screen prewarms */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgVignette *
lrg_vignette_new (void)
{
    return g_object_new (LRG_TYPE_VIGNETTE, NULL);
}

gfloat
lrg_vignette_get_intensity (LrgVignette *self)
{
    g_return_val_if_fail (LRG_IS_VIGNETTE (self), 0.0f);

    return self->intensity;
}

void
lrg_vignette_set_intensity (LrgVignette *self,
                            gfloat       intensity)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    intensity = CLAMP (intensity, 0.0f, 1.0f);

    if (self->intensity == intensity)
        return;

    self->intensity = intensity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

gfloat
lrg_vignette_get_radius (LrgVignette *self)
{
    g_return_val_if_fail (LRG_IS_VIGNETTE (self), 0.0f);

    return self->radius;
}

void
lrg_vignette_set_radius (LrgVignette *self,
                         gfloat       radius)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    radius = CLAMP (radius, 0.0f, 1.0f);

    if (self->radius == radius)
        return;

    self->radius = radius;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
}

gfloat
lrg_vignette_get_smoothness (LrgVignette *self)
{
    g_return_val_if_fail (LRG_IS_VIGNETTE (self), 0.0f);

    return self->smoothness;
}

void
lrg_vignette_set_smoothness (LrgVignette *self,
                             gfloat       smoothness)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    smoothness = CLAMP (smoothness, 0.0f, 1.0f);

    if (self->smoothness == smoothness)
        return;

    self->smoothness = smoothness;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SMOOTHNESS]);
}

gfloat
lrg_vignette_get_roundness (LrgVignette *self)
{
    g_return_val_if_fail (LRG_IS_VIGNETTE (self), 0.0f);

    return self->roundness;
}

void
lrg_vignette_set_roundness (LrgVignette *self,
                            gfloat       roundness)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    roundness = CLAMP (roundness, 0.0f, 1.0f);

    if (self->roundness == roundness)
        return;

    self->roundness = roundness;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROUNDNESS]);
}

void
lrg_vignette_get_color (LrgVignette *self,
                        gfloat      *r,
                        gfloat      *g,
                        gfloat      *b)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    if (r != NULL)
        *r = self->color_r;
    if (g != NULL)
        *g = self->color_g;
    if (b != NULL)
        *b = self->color_b;
}

void
lrg_vignette_set_color (LrgVignette *self,
                        gfloat       r,
                        gfloat       g,
                        gfloat       b)
{
    g_return_if_fail (LRG_IS_VIGNETTE (self));

    r = CLAMP (r, 0.0f, 1.0f);
    g = CLAMP (g, 0.0f, 1.0f);
    b = CLAMP (b, 0.0f, 1.0f);
    g_object_freeze_notify (G_OBJECT (self));
    if (self->color_r != r)
    {
        self->color_r = r;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR_R]);
    }
    if (self->color_g != g)
    {
        self->color_g = g;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR_G]);
    }
    if (self->color_b != b)
    {
        self->color_b = b;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR_B]);
    }
    g_object_thaw_notify (G_OBJECT (self));
}

/* lrg_vignette_prewarm: compiles the pass shader ahead of the first frame */
static gboolean
lrg_vignette_prewarm (LrgPrewarmable *prewarmable)
{
    vignette_get_shader (LRG_VIGNETTE (prewarmable));
    return TRUE;
}

static void
lrg_vignette_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_vignette_prewarm;
}
