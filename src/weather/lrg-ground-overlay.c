/* lrg-ground-overlay.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One alpha-blended shader pass.  With snow alpha a_s (flat ground x
 * level, less the tracks) and shadow s, layering "snow, then shadow"
 * over the lit ground is dst' = snow a_s (1 - s) + dst (1 - a_s)(1 - s),
 * which is a single SRC_ALPHA / ONE_MINUS_SRC_ALPHA blend with
 * a = 1 - (1 - a_s)(1 - s) and rgb = snow a_s (1 - s) / a.
 *
 * Over terrain the vertices are in draw space; over objects (buildings,
 * models, props) drawn under a matrix stack of their own the caller hands
 * that matrix in (matStack), so the clouds and the snow are sampled where
 * the surface really is and roofs (normals facing up) take the snow.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "weather/lrg-ground-overlay.h"
#include "weather/lrg-cloud-layer-private.h"
#include "../lrg-log.h"

struct _LrgGroundOverlay
{
    GObject        parent_instance;

    LrgSnowCover  *snow;
    LrgCloudLayer *clouds;
    gfloat         sun[3];
    gfloat         snow_color[4];

    GrlShader     *shader;
    gboolean       shader_failed;
    gint           loc_noise_origin;
    gint           loc_inv_scale;
    gint           loc_coverage;
    gint           loc_sun;
    gint           loc_altitude;
    gint           loc_shadow;
    gint           loc_snow_level;
    gint           loc_snow_color;
    gint           loc_track_count;
    gint           loc_tracks;
    gint           loc_stack;
    gint           loc_cut;
};

G_DEFINE_FINAL_TYPE (LrgGroundOverlay, lrg_ground_overlay, G_TYPE_OBJECT)

static const gchar overlay_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "in vec2 vertexTexCoord;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matStack;\n"
    "out vec3 fragLocal;\n"
    "out vec3 fragNormal;\n"
    "out vec2 fragUV;\n"
    "void main() {\n"
    "    fragUV = vertexTexCoord;\n"
    "    mat4 world = matStack * matModel;\n"
    "    fragLocal = (world * vec4(vertexPosition, 1.0)).xyz;\n"
    "    vec3 n = mat3(world) * vertexNormal;\n"
    "    float nl = length(n);\n"
    "    fragNormal = nl > 1e-6 ? n / nl : vec3(0.0);\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const gchar overlay_fs[] =
    "#version 330\n"
    "in vec3 fragLocal;\n"
    "in vec3 fragNormal;\n"
    "in vec2 fragUV;\n"
    "uniform sampler2D texture0;\n"
    "uniform int cutAlpha;\n"
    "uniform vec2 noiseOrigin;\n"
    "uniform float invScale;\n"
    "uniform float coverage;\n"
    "uniform vec3 towardsSun;\n"
    "uniform float altitude;\n"
    "uniform float shadowStrength;\n"
    "uniform float snowLevel;\n"
    "uniform vec4 snowColor;\n"
    "uniform int trackCount;\n"
    "uniform vec4 tracks[64];\n"
    LRG_CLOUD_NOISE_GLSL
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    if (cutAlpha != 0 && texture(texture0, fragUV).a < 0.35) discard;\n"
    "    float shadow = 0.0;\n"
    "    if (coverage > 0.0 && towardsSun.y > 0.05) {\n"
    "        float rise = max(altitude - fragLocal.y, 0.0);\n"
    "        vec2 q = fragLocal.xz + towardsSun.xz / towardsSun.y * rise;\n"
    "        float fade = smoothstep(0.05, 0.25, towardsSun.y);\n"
    "        shadow = lrg_cloud_density(noiseOrigin + q * invScale, coverage) * shadowStrength * fade;\n"
    "    }\n"
    "    float snow = 0.0;\n"
    "    float dent = 0.0;\n"
    "    if (snowLevel > 0.0) {\n"
    "        float flatness = smoothstep(0.55, 0.85, fragNormal.y);\n"
    "        for (int i = 0; i < trackCount; i++) {\n"
    "            float d = length(fragLocal.xz - tracks[i].xy);\n"
    "            float r = tracks[i].z;\n"
    "            dent = max(dent, tracks[i].w * (1.0 - smoothstep(0.5 * r, r, d)));\n"
    "        }\n"
    "        float patches = lrg_cloud_noise(fragLocal.xz * 0.11);\n"
    "        float cover = clamp(snowLevel * 1.5 - (1.0 - snowLevel) * patches * 0.8, 0.0, 1.0);\n"
    "        snow = flatness * cover * (1.0 - dent) * snowColor.a;\n"
    "        shadow = 1.0 - (1.0 - shadow) * (1.0 - 0.3 * dent * snowLevel);\n"
    "    }\n"
    "    float a = 1.0 - (1.0 - snow) * (1.0 - shadow);\n"
    "    if (a <= 0.002) discard;\n"
    "    finalColor = vec4(snowColor.rgb * snow * (1.0 - shadow) / a, a);\n"
    "}\n";

static void
lrg_ground_overlay_dispose (GObject *object)
{
    LrgGroundOverlay *self = LRG_GROUND_OVERLAY (object);

    g_clear_object (&self->snow);
    g_clear_object (&self->clouds);
    g_clear_object (&self->shader);
    G_OBJECT_CLASS (lrg_ground_overlay_parent_class)->dispose (object);
}

static void
lrg_ground_overlay_class_init (LrgGroundOverlayClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_ground_overlay_dispose;
}

static void
lrg_ground_overlay_init (LrgGroundOverlay *self)
{
    self->sun[1] = 1.0f;
    self->snow_color[0] = 0.92f;
    self->snow_color[1] = 0.94f;
    self->snow_color[2] = 0.98f;
    self->snow_color[3] = 1.0f;
}

LrgGroundOverlay *
lrg_ground_overlay_new (void)
{
    return g_object_new (LRG_TYPE_GROUND_OVERLAY, NULL);
}

void
lrg_ground_overlay_set_snow_cover (LrgGroundOverlay *self,
                                   LrgSnowCover     *snow)
{
    g_return_if_fail (LRG_IS_GROUND_OVERLAY (self));
    g_return_if_fail (snow == NULL || LRG_IS_SNOW_COVER (snow));

    g_set_object (&self->snow, snow);
}

void
lrg_ground_overlay_set_cloud_layer (LrgGroundOverlay *self,
                                    LrgCloudLayer    *clouds)
{
    g_return_if_fail (LRG_IS_GROUND_OVERLAY (self));
    g_return_if_fail (clouds == NULL || LRG_IS_CLOUD_LAYER (clouds));

    g_set_object (&self->clouds, clouds);
}

void
lrg_ground_overlay_set_sun_direction (LrgGroundOverlay *self,
                                      const gfloat     *towards_sun)
{
    g_return_if_fail (LRG_IS_GROUND_OVERLAY (self));
    g_return_if_fail (towards_sun != NULL);

    memcpy (self->sun, towards_sun, sizeof self->sun);
}

void
lrg_ground_overlay_set_snow_color (LrgGroundOverlay *self,
                                   const GrlColor   *color)
{
    g_return_if_fail (LRG_IS_GROUND_OVERLAY (self));
    g_return_if_fail (color != NULL);

    self->snow_color[0] = (gfloat)color->r / 255.0f;
    self->snow_color[1] = (gfloat)color->g / 255.0f;
    self->snow_color[2] = (gfloat)color->b / 255.0f;
    self->snow_color[3] = (gfloat)color->a / 255.0f;
}

gboolean
lrg_ground_overlay_is_visible (LrgGroundOverlay *self)
{
    gboolean snow;
    gboolean shadow;

    g_return_val_if_fail (LRG_IS_GROUND_OVERLAY (self), FALSE);

    snow = self->snow != NULL && lrg_snow_cover_get_level (self->snow) > 0.0;
    shadow = self->clouds != NULL && lrg_cloud_layer_get_coverage (self->clouds) > 0.0f &&
             lrg_cloud_layer_get_shadow_strength (self->clouds) > 0.0f && self->sun[1] > 0.05f;
    return snow || shadow;
}

static gboolean
ensure_shader (LrgGroundOverlay *self)
{
    if (self->shader != NULL)
        return TRUE;
    if (self->shader_failed)
        return FALSE;

    self->shader = grl_shader_new_from_memory (overlay_vs, overlay_fs, NULL);
    if (self->shader == NULL || !grl_shader_is_valid (self->shader))
    {
        lrg_warning (LRG_LOG_DOMAIN_WEATHER, "ground overlay: the shader did not compile; no "
                     "snow or cloud shadows on the ground");
        g_clear_object (&self->shader);
        self->shader_failed = TRUE;
        return FALSE;
    }
    self->loc_noise_origin = grl_shader_get_location (self->shader, "noiseOrigin");
    self->loc_inv_scale = grl_shader_get_location (self->shader, "invScale");
    self->loc_coverage = grl_shader_get_location (self->shader, "coverage");
    self->loc_sun = grl_shader_get_location (self->shader, "towardsSun");
    self->loc_altitude = grl_shader_get_location (self->shader, "altitude");
    self->loc_shadow = grl_shader_get_location (self->shader, "shadowStrength");
    self->loc_snow_level = grl_shader_get_location (self->shader, "snowLevel");
    self->loc_snow_color = grl_shader_get_location (self->shader, "snowColor");
    self->loc_track_count = grl_shader_get_location (self->shader, "trackCount");
    self->loc_tracks = grl_shader_get_location (self->shader, "tracks");
    self->loc_stack = grl_shader_get_location (self->shader, "matStack");
    self->loc_cut = grl_shader_get_location (self->shader, "cutAlpha");
    return TRUE;
}

/*
 * prepare:
 * Everything both passes share: the shader, its uniforms for the draw
 * space whose origin in the world is @origin, and the blend state.
 */
static gboolean
prepare (LrgGroundOverlay *self,
         const gdouble    *origin,
         gboolean          objects)
{
    gint    cut = objects ? 1 : 0;
    gfloat  tracks[LRG_GROUND_OVERLAY_MAX_TRACKS * 4];
    gfloat  noise_origin[2] = { 0.0f, 0.0f };
    gfloat  inv_scale = 0.0f;
    gfloat  coverage = 0.0f;
    gfloat  altitude = 0.0f;
    gfloat  shadow = 0.0f;
    gfloat  level = 0.0f;
    gint    n_tracks = 0;
    Shader  sh;

    if (!lrg_ground_overlay_is_visible (self) || !IsWindowReady () || !ensure_shader (self))
        return FALSE;

    /* the clouds, sampled at draw-space origin */
    if (self->clouds != NULL)
    {
        lrg_cloud_layer_get_noise_point (self->clouds, origin[0], origin[2], noise_origin);
        inv_scale = 1.0f / lrg_cloud_layer_get_scale (self->clouds);
        coverage = lrg_cloud_layer_get_coverage (self->clouds);
        altitude = lrg_cloud_layer_get_altitude (self->clouds) - (gfloat)origin[1];
        shadow = lrg_cloud_layer_get_shadow_strength (self->clouds);
    }

    /* the snow and the newest tracks, in draw space */
    if (self->snow != NULL)
    {
        guint count = lrg_snow_cover_get_track_count (self->snow);
        guint first = count > LRG_GROUND_OVERLAY_MAX_TRACKS ?
                      count - LRG_GROUND_OVERLAY_MAX_TRACKS : 0;
        guint i;

        level = (gfloat)lrg_snow_cover_get_level (self->snow);
        for (i = first; i < count; i++)
        {
            gdouble x;
            gdouble z;
            gdouble r;
            gdouble d;

            lrg_snow_cover_get_track (self->snow, i, &x, &z, &r, &d);
            tracks[n_tracks * 4 + 0] = (gfloat)(x - origin[0]);
            tracks[n_tracks * 4 + 1] = (gfloat)(z - origin[2]);
            tracks[n_tracks * 4 + 2] = (gfloat)r;
            tracks[n_tracks * 4 + 3] = (gfloat)d;
            n_tracks++;
        }
    }

    sh = *(Shader *)grl_shader_get_handle (self->shader);
    SetShaderValue (sh, self->loc_noise_origin, noise_origin, SHADER_UNIFORM_VEC2);
    SetShaderValue (sh, self->loc_inv_scale, &inv_scale, SHADER_UNIFORM_FLOAT);
    SetShaderValue (sh, self->loc_coverage, &coverage, SHADER_UNIFORM_FLOAT);
    SetShaderValue (sh, self->loc_sun, self->sun, SHADER_UNIFORM_VEC3);
    SetShaderValue (sh, self->loc_altitude, &altitude, SHADER_UNIFORM_FLOAT);
    SetShaderValue (sh, self->loc_shadow, &shadow, SHADER_UNIFORM_FLOAT);
    SetShaderValue (sh, self->loc_snow_level, &level, SHADER_UNIFORM_FLOAT);
    SetShaderValue (sh, self->loc_snow_color, self->snow_color, SHADER_UNIFORM_VEC4);
    SetShaderValue (sh, self->loc_track_count, &n_tracks, SHADER_UNIFORM_INT);
    /* objects carry cut-out textures (fences, grates): their clear texels get nothing */
    SetShaderValue (sh, self->loc_cut, &cut, SHADER_UNIFORM_INT);
    if (n_tracks > 0)
        SetShaderValueV (sh, self->loc_tracks, tracks, SHADER_UNIFORM_VEC4, n_tracks);
    lrg_ground_overlay_set_model_matrix (self, NULL);

    rlDrawRenderBatchActive ();
    rlDisableDepthMask ();
    rlSetBlendMode (RL_BLEND_ALPHA);
    rlEnableColorBlend ();
    return TRUE;
}

gboolean
lrg_ground_overlay_begin (LrgGroundOverlay *self,
                          LrgTerrain       *terrain)
{
    gdouble origin[3];

    g_return_val_if_fail (LRG_IS_GROUND_OVERLAY (self), FALSE);
    g_return_val_if_fail (LRG_IS_TERRAIN (terrain), FALSE);

    lrg_terrain_get_origin (terrain, &origin[0], &origin[1], &origin[2]);
    if (!prepare (self, origin, FALSE))
        return FALSE;
    lrg_terrain_set_shader (terrain, self->shader);
    return TRUE;
}

GrlShader *
lrg_ground_overlay_begin_objects (LrgGroundOverlay *self,
                                  const gdouble    *origin)
{
    g_return_val_if_fail (LRG_IS_GROUND_OVERLAY (self), NULL);
    g_return_val_if_fail (origin != NULL, NULL);

    if (!prepare (self, origin, TRUE))
        return NULL;
    return self->shader;
}

void
lrg_ground_overlay_set_model_matrix (LrgGroundOverlay *self,
                                     const gfloat     *matrix)
{
    Matrix m;

    g_return_if_fail (LRG_IS_GROUND_OVERLAY (self));

    if (self->shader == NULL || self->loc_stack < 0)
        return;

    /* the array is OpenGL's column-major order: element i is raylib's "m<i>" */
    if (matrix == NULL)
    {
        m = (Matrix){ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    }
    else
    {
        m.m0 = matrix[0];   m.m1 = matrix[1];   m.m2 = matrix[2];   m.m3 = matrix[3];
        m.m4 = matrix[4];   m.m5 = matrix[5];   m.m6 = matrix[6];   m.m7 = matrix[7];
        m.m8 = matrix[8];   m.m9 = matrix[9];   m.m10 = matrix[10]; m.m11 = matrix[11];
        m.m12 = matrix[12]; m.m13 = matrix[13]; m.m14 = matrix[14]; m.m15 = matrix[15];
    }
    SetShaderValueMatrix (*(Shader *)grl_shader_get_handle (self->shader), self->loc_stack, m);
}

void
lrg_ground_overlay_end_objects (LrgGroundOverlay *self)
{
    g_return_if_fail (LRG_IS_GROUND_OVERLAY (self));

    rlDrawRenderBatchActive ();
    rlEnableDepthMask ();
}

void
lrg_ground_overlay_end (LrgGroundOverlay *self,
                        LrgTerrain       *terrain)
{
    g_return_if_fail (LRG_IS_GROUND_OVERLAY (self));
    g_return_if_fail (LRG_IS_TERRAIN (terrain));

    rlDrawRenderBatchActive ();
    lrg_terrain_set_shader (terrain, NULL);
    rlEnableDepthMask ();
}

void
lrg_ground_overlay_release_gpu (LrgGroundOverlay *self)
{
    g_return_if_fail (LRG_IS_GROUND_OVERLAY (self));

    g_clear_object (&self->shader);
    self->shader_failed = FALSE;
}
