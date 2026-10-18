/* lrg-cloud-layer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The deck is a set of horizontal slices through the cloud volume.
 * Each slice is a grid of quads emitted through rlgl's immediate batch
 * with the eye-relative real position (metres) in the texture
 * coordinate; the fragment shader turns that into a noise coordinate,
 * thresholds the noise by the coverage (thinner towards the top of the
 * deck, so slices stack into rounded tops), shades by the density a
 * little towards the sun and fades with distance.  The geometry is
 * scaled about the eye into the sky radius, which keeps its directions
 * (what the eye sees) and keeps it inside the far plane.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "weather/lrg-cloud-layer.h"
#include "weather/lrg-cloud-layer-private.h"
#include "../lrg-log.h"

/* real radius of the drawn deck around the eye, metres */
#define CLOUD_DRAW_RADIUS (12000.0f)

/* quads per side of a slice grid */
#define CLOUD_GRID (24)

/* the deck is scaled to this fraction of the sky radius */
#define CLOUD_SKY_FRACTION (0.8f)

struct _LrgCloudLayer
{
    GObject    parent_instance;

    gfloat     coverage;
    gfloat     altitude;
    gfloat     thickness;
    gfloat     scale;
    guint      slices;
    gfloat     wind[2];
    gdouble    offset[2];
    gfloat     shadow_strength;
    guint8     lit[4];
    guint8     shade[4];
    gdouble    origin[3];

    GrlShader *shader;
    gboolean   shader_failed;
    gint       loc_noise_eye;
    gint       loc_inv_scale;
    gint       loc_coverage;
    gint       loc_sun;
    gint       loc_lit;
    gint       loc_shade;
    gint       loc_radius;
};

G_DEFINE_FINAL_TYPE (LrgCloudLayer, lrg_cloud_layer, G_TYPE_OBJECT)

static const gchar cloud_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "out vec2 fragReal;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragReal = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

/* vertexColor.r carries the slice's height through the deck (0 base,
 * 1 top) and .a the slice alpha */
static const gchar cloud_fs[] =
    "#version 330\n"
    "in vec2 fragReal;\n"
    "in vec4 fragColor;\n"
    "uniform vec2 noiseEye;\n"
    "uniform float invScale;\n"
    "uniform float coverage;\n"
    "uniform vec3 towardsSun;\n"
    "uniform vec4 litColor;\n"
    "uniform vec4 shadeColor;\n"
    "uniform float drawRadius;\n"
    LRG_CLOUD_NOISE_GLSL
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    float h = fragColor.r;\n"
    "    vec2 p = noiseEye + fragReal * invScale;\n"
    "    float cov = coverage * (1.0 - 0.45 * h);\n"
    "    float d = lrg_cloud_density(p, cov);\n"
    "    if (d <= 0.002) discard;\n"
    "    vec2 s = normalize(towardsSun.xz + vec2(1e-5)) * 0.35;\n"
    "    float toward = lrg_cloud_density(p + s, cov);\n"
    "    float light = clamp(1.0 - 0.7 * toward + 0.3 * h, 0.0, 1.0);\n"
    "    float day = clamp(towardsSun.y * 4.0 + 0.4, 0.15, 1.0);\n"
    "    vec3 rgb = mix(shadeColor.rgb, litColor.rgb, light) * day;\n"
    "    float fade = 1.0 - smoothstep(0.45 * drawRadius, drawRadius, length(fragReal));\n"
    "    finalColor = vec4(rgb, d * fragColor.a * litColor.a * fade);\n"
    "}\n";

static void
lrg_cloud_layer_dispose (GObject *object)
{
    LrgCloudLayer *self = LRG_CLOUD_LAYER (object);

    g_clear_object (&self->shader);
    G_OBJECT_CLASS (lrg_cloud_layer_parent_class)->dispose (object);
}

static void
lrg_cloud_layer_class_init (LrgCloudLayerClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_cloud_layer_dispose;
}

static void
lrg_cloud_layer_init (LrgCloudLayer *self)
{
    self->coverage = 0.0f;
    self->altitude = 1200.0f;
    self->thickness = 300.0f;
    self->scale = 1800.0f;
    self->slices = 3;
    self->shadow_strength = 0.5f;
    self->lit[0] = 250; self->lit[1] = 250; self->lit[2] = 255; self->lit[3] = 255;
    self->shade[0] = 120; self->shade[1] = 126; self->shade[2] = 140; self->shade[3] = 255;
}

LrgCloudLayer *
lrg_cloud_layer_new (guint32 seed)
{
    LrgCloudLayer *self = g_object_new (LRG_TYPE_CLOUD_LAYER, NULL);

    /* the seed picks the starting place in the (periodic) noise */
    self->offset[0] = (gdouble)(lrg_cloud_hash ((gint)seed, 7) % 4096u) / 16.0 * self->scale;
    self->offset[1] = (gdouble)(lrg_cloud_hash (11, (gint)seed) % 4096u) / 16.0 * self->scale;
    return self;
}

void
lrg_cloud_layer_set_coverage (LrgCloudLayer *self,
                              gfloat         coverage)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (!isfinite (coverage))
        return;
    self->coverage = CLAMP (coverage, 0.0f, 1.0f);
}

gfloat
lrg_cloud_layer_get_coverage (LrgCloudLayer *self)
{
    g_return_val_if_fail (LRG_IS_CLOUD_LAYER (self), 0.0f);
    return self->coverage;
}

void
lrg_cloud_layer_set_altitude (LrgCloudLayer *self,
                              gfloat         altitude,
                              gfloat         thickness)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (isfinite (altitude) && altitude > 0.0f)
        self->altitude = altitude;
    if (isfinite (thickness) && thickness >= 0.0f)
        self->thickness = thickness;
}

gfloat
lrg_cloud_layer_get_altitude (LrgCloudLayer *self)
{
    g_return_val_if_fail (LRG_IS_CLOUD_LAYER (self), 0.0f);
    return self->altitude;
}

void
lrg_cloud_layer_set_scale (LrgCloudLayer *self,
                           gfloat         metres)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (isfinite (metres) && metres > 0.0f)
        self->scale = metres;
}

gfloat
lrg_cloud_layer_get_scale (LrgCloudLayer *self)
{
    g_return_val_if_fail (LRG_IS_CLOUD_LAYER (self), 1.0f);
    return self->scale;
}

void
lrg_cloud_layer_set_slices (LrgCloudLayer *self,
                            guint          slices)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));
    self->slices = CLAMP (slices, 1u, (guint)LRG_CLOUD_LAYER_MAX_SLICES);
}

void
lrg_cloud_layer_set_wind (LrgCloudLayer *self,
                          gfloat         vx,
                          gfloat         vz)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (!isfinite (vx) || !isfinite (vz))
        return;
    self->wind[0] = vx;
    self->wind[1] = vz;
}

void
lrg_cloud_layer_get_wind (LrgCloudLayer *self,
                          gfloat        *vx,
                          gfloat        *vz)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (vx != NULL)
        *vx = self->wind[0];
    if (vz != NULL)
        *vz = self->wind[1];
}

void
lrg_cloud_layer_advance (LrgCloudLayer *self,
                         gdouble        seconds)
{
    gdouble period;

    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (!isfinite (seconds) || seconds <= 0.0)
        return;

    /* keep the offset within one noise period so it never loses
     * precision, however long the game runs */
    period = (gdouble)LRG_CLOUD_NOISE_PERIOD * self->scale;
    self->offset[0] = fmod (self->offset[0] + self->wind[0] * seconds, period);
    self->offset[1] = fmod (self->offset[1] + self->wind[1] * seconds, period);
}

void
lrg_cloud_layer_get_offset (LrgCloudLayer *self,
                            gdouble       *x,
                            gdouble       *z)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (x != NULL)
        *x = self->offset[0];
    if (z != NULL)
        *z = self->offset[1];
}

void
lrg_cloud_layer_set_offset (LrgCloudLayer *self,
                            gdouble        x,
                            gdouble        z)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (!isfinite (x) || !isfinite (z))
        return;
    self->offset[0] = x;
    self->offset[1] = z;
}

/* noise_coord:
 * The (unfolded) noise coordinate of an absolute world point: the
 * clouds drift with the wind, so the point sees noise from upwind. */
static void
noise_coord (LrgCloudLayer *self,
             gdouble        wx,
             gdouble        wz,
             gdouble       *px,
             gdouble       *pz)
{
    *px = (wx - self->offset[0]) / self->scale;
    *pz = (wz - self->offset[1]) / self->scale;
}

void
lrg_cloud_layer_get_noise_point (LrgCloudLayer *self,
                                 gdouble        wx,
                                 gdouble        wz,
                                 gfloat        *out)
{
    gdouble px;
    gdouble pz;
    gdouble period = (gdouble)LRG_CLOUD_NOISE_PERIOD;

    noise_coord (self, wx, wz, &px, &pz);
    px -= floor (px / period) * period;
    pz -= floor (pz / period) * period;
    out[0] = (gfloat)px;
    out[1] = (gfloat)pz;
}

gfloat
lrg_cloud_layer_sample_density (LrgCloudLayer *self,
                                gdouble        x,
                                gdouble        z)
{
    gdouble px;
    gdouble pz;

    g_return_val_if_fail (LRG_IS_CLOUD_LAYER (self), 0.0f);

    if (!isfinite (x) || !isfinite (z))
        return 0.0f;
    noise_coord (self, x, z, &px, &pz);
    return (gfloat)lrg_cloud_density_at (px, pz, self->coverage);
}

void
lrg_cloud_layer_set_shadow_strength (LrgCloudLayer *self,
                                     gfloat         strength)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (isfinite (strength))
        self->shadow_strength = CLAMP (strength, 0.0f, 1.0f);
}

gfloat
lrg_cloud_layer_get_shadow_strength (LrgCloudLayer *self)
{
    g_return_val_if_fail (LRG_IS_CLOUD_LAYER (self), 0.0f);
    return self->shadow_strength;
}

/* sun_fade:
 * Cloud shadows fade out between 15 and 3 degrees of sun height. */
static gfloat
sun_fade (const gfloat *towards_sun)
{
    gfloat t = (towards_sun[1] - 0.05f) / 0.2f;

    t = CLAMP (t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

gfloat
lrg_cloud_layer_sample_shadow (LrgCloudLayer *self,
                               gdouble        x,
                               gdouble        y,
                               gdouble        z,
                               const gfloat  *towards_sun)
{
    gdouble rise;
    gfloat  fade;

    g_return_val_if_fail (LRG_IS_CLOUD_LAYER (self), 0.0f);
    g_return_val_if_fail (towards_sun != NULL, 0.0f);

    fade = sun_fade (towards_sun);
    if (fade <= 0.0f || self->coverage <= 0.0f || !isfinite (y))
        return 0.0f;

    /* follow the ray to the sun up to the cloud base */
    rise = MAX ((gdouble)self->altitude - y, 0.0);
    x += towards_sun[0] / towards_sun[1] * rise;
    z += towards_sun[2] / towards_sun[1] * rise;
    return lrg_cloud_layer_sample_density (self, x, z) * self->shadow_strength * fade;
}

GrlImage *
lrg_cloud_layer_bake_shadow_image (LrgCloudLayer *self,
                                   gdouble        centre_x,
                                   gdouble        centre_z,
                                   gdouble        extent,
                                   guint          size,
                                   const gfloat  *towards_sun)
{
    g_autofree guint8 *pixels = NULL;
    guint              row;
    guint              col;

    g_return_val_if_fail (LRG_IS_CLOUD_LAYER (self), NULL);
    g_return_val_if_fail (towards_sun != NULL, NULL);

    if (size < 8 || size > 1024 || !isfinite (extent) || extent <= 0.0)
        return NULL;

    pixels = g_malloc ((gsize)size * size * 4);
    for (row = 0; row < size; row++)
    {
        for (col = 0; col < size; col++)
        {
            gdouble wx = centre_x - extent * 0.5 + ((gdouble)col + 0.5) / size * extent;
            gdouble wz = centre_z - extent * 0.5 + ((gdouble)row + 0.5) / size * extent;
            gfloat  s = lrg_cloud_layer_sample_shadow (self, wx, 0.0, wz, towards_sun);
            guint8  v = (guint8)floorf ((1.0f - s) * 255.0f + 0.5f);
            guint8 *px = pixels + ((gsize)row * size + col) * 4;

            px[0] = v;
            px[1] = v;
            px[2] = v;
            px[3] = 255;
        }
    }
    return grl_image_new_from_pixels ((gint)size, (gint)size,
                                      GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, pixels);
}

void
lrg_cloud_layer_set_colors (LrgCloudLayer  *self,
                            const GrlColor *lit,
                            const GrlColor *shade)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    if (lit != NULL)
    {
        self->lit[0] = lit->r; self->lit[1] = lit->g; self->lit[2] = lit->b; self->lit[3] = lit->a;
    }
    if (shade != NULL)
    {
        self->shade[0] = shade->r; self->shade[1] = shade->g;
        self->shade[2] = shade->b; self->shade[3] = shade->a;
    }
}

void
lrg_cloud_layer_set_world_origin (LrgCloudLayer *self,
                                  gdouble        x,
                                  gdouble        y,
                                  gdouble        z)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    self->origin[0] = x;
    self->origin[1] = y;
    self->origin[2] = z;
}

/* ensure_shader:
 * Compiles the slice shader once; a failure is reported once. */
static gboolean
ensure_shader (LrgCloudLayer *self)
{
    if (self->shader != NULL)
        return TRUE;
    if (self->shader_failed)
        return FALSE;

    self->shader = grl_shader_new_from_memory (cloud_vs, cloud_fs, NULL);
    if (self->shader == NULL || !grl_shader_is_valid (self->shader))
    {
        lrg_warning (LRG_LOG_DOMAIN_WEATHER, "cloud layer: the cloud shader did not compile; "
                     "clouds are not drawn");
        g_clear_object (&self->shader);
        self->shader_failed = TRUE;
        return FALSE;
    }
    self->loc_noise_eye = grl_shader_get_location (self->shader, "noiseEye");
    self->loc_inv_scale = grl_shader_get_location (self->shader, "invScale");
    self->loc_coverage = grl_shader_get_location (self->shader, "coverage");
    self->loc_sun = grl_shader_get_location (self->shader, "towardsSun");
    self->loc_lit = grl_shader_get_location (self->shader, "litColor");
    self->loc_shade = grl_shader_get_location (self->shader, "shadeColor");
    self->loc_radius = grl_shader_get_location (self->shader, "drawRadius");
    return TRUE;
}

void
lrg_cloud_layer_draw (LrgCloudLayer    *self,
                      const GrlVector3 *eye,
                      gfloat            sky_radius,
                      const gfloat     *towards_sun)
{
    gfloat   noise_eye[2];
    gfloat   k;
    guint    s;
    gint     i;
    gint     j;

    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));
    g_return_if_fail (eye != NULL);
    g_return_if_fail (towards_sun != NULL);

    if (self->coverage <= 0.0f || !IsWindowReady () || !isfinite (sky_radius) ||
        sky_radius <= 0.0f || !ensure_shader (self))
        return;

    lrg_cloud_layer_get_noise_point (self, self->origin[0] + eye->x, self->origin[2] + eye->z,
                                     noise_eye);
    /* real metres to draw units: the deck's edge lands inside the sky */
    k = sky_radius * CLOUD_SKY_FRACTION / CLOUD_DRAW_RADIUS;

    rlDrawRenderBatchActive ();
    rlDisableDepthTest ();
    rlDisableDepthMask ();
    rlDisableBackfaceCulling ();
    grl_shader_begin (self->shader);
    {
        gfloat inv = 1.0f / self->scale;
        gfloat lit[4];
        gfloat shade[4];
        gfloat radius = CLOUD_DRAW_RADIUS;
        guint  c;

        for (c = 0; c < 4; c++)
        {
            lit[c] = (gfloat)self->lit[c] / 255.0f;
            shade[c] = (gfloat)self->shade[c] / 255.0f;
        }
        SetShaderValue (*(Shader *)grl_shader_get_handle (self->shader), self->loc_noise_eye,
                        noise_eye, SHADER_UNIFORM_VEC2);
        SetShaderValue (*(Shader *)grl_shader_get_handle (self->shader), self->loc_inv_scale,
                        &inv, SHADER_UNIFORM_FLOAT);
        SetShaderValue (*(Shader *)grl_shader_get_handle (self->shader), self->loc_coverage,
                        &self->coverage, SHADER_UNIFORM_FLOAT);
        SetShaderValue (*(Shader *)grl_shader_get_handle (self->shader), self->loc_sun,
                        towards_sun, SHADER_UNIFORM_VEC3);
        SetShaderValue (*(Shader *)grl_shader_get_handle (self->shader), self->loc_lit, lit,
                        SHADER_UNIFORM_VEC4);
        SetShaderValue (*(Shader *)grl_shader_get_handle (self->shader), self->loc_shade, shade,
                        SHADER_UNIFORM_VEC4);
        SetShaderValue (*(Shader *)grl_shader_get_handle (self->shader), self->loc_radius,
                        &radius, SHADER_UNIFORM_FLOAT);
    }

    /* bottom slice first: the upper ones blend over it */
    for (s = 0; s < self->slices; s++)
    {
        gfloat h = self->slices > 1 ? (gfloat)s / (gfloat)(self->slices - 1) : 0.0f;
        gfloat height = (self->altitude + h * self->thickness) - (gfloat)self->origin[1] - eye->y;
        guint8 alpha = (guint8)MIN (255.0f, 408.0f / (gfloat)self->slices);

        /* a deck below the eye is not drawn (mountain tops) */
        if (height <= 1.0f)
            continue;

        rlBegin (RL_QUADS);
        rlColor4ub ((guint8)(h * 255.0f), 0, 0, alpha);
        for (i = 0; i < CLOUD_GRID; i++)
        {
            for (j = 0; j < CLOUD_GRID; j++)
            {
                gfloat x0 = -CLOUD_DRAW_RADIUS + 2.0f * CLOUD_DRAW_RADIUS * (gfloat)i / CLOUD_GRID;
                gfloat x1 = -CLOUD_DRAW_RADIUS + 2.0f * CLOUD_DRAW_RADIUS * (gfloat)(i + 1) / CLOUD_GRID;
                gfloat z0 = -CLOUD_DRAW_RADIUS + 2.0f * CLOUD_DRAW_RADIUS * (gfloat)j / CLOUD_GRID;
                gfloat z1 = -CLOUD_DRAW_RADIUS + 2.0f * CLOUD_DRAW_RADIUS * (gfloat)(j + 1) / CLOUD_GRID;

                rlTexCoord2f (x0, z0);
                rlVertex3f (eye->x + x0 * k, eye->y + height * k, eye->z + z0 * k);
                rlTexCoord2f (x0, z1);
                rlVertex3f (eye->x + x0 * k, eye->y + height * k, eye->z + z1 * k);
                rlTexCoord2f (x1, z1);
                rlVertex3f (eye->x + x1 * k, eye->y + height * k, eye->z + z1 * k);
                rlTexCoord2f (x1, z0);
                rlVertex3f (eye->x + x1 * k, eye->y + height * k, eye->z + z0 * k);
            }
        }
        rlEnd ();
    }
    grl_shader_end (self->shader);

    rlDrawRenderBatchActive ();
    rlEnableBackfaceCulling ();
    rlEnableDepthMask ();
    rlEnableDepthTest ();
}

void
lrg_cloud_layer_release_gpu (LrgCloudLayer *self)
{
    g_return_if_fail (LRG_IS_CLOUD_LAYER (self));

    g_clear_object (&self->shader);
    self->shader_failed = FALSE;
}
