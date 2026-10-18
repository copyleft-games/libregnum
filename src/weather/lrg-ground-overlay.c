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
 * The same evaluation (LRG_GROUND_OVERLAY_GLSL) runs inside LrgLightSet's
 * lit shader when the light set carries the overlay: then a surface gets
 * its snow and cloud shadow in the pass that draws it and this blended
 * second pass is not needed.  Over objects (buildings, models, props)
 * the pass sees them under whatever rlgl matrix stack they are drawn with
 * (raylib puts it into matModel), so roofs (normals facing up) take the
 * snow wherever the model sits.
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
#include "weather/lrg-ground-overlay-private.h"
#include "weather/lrg-cloud-layer-private.h"
#include "../lrg-log.h"

struct _LrgGroundOverlay
{
    GObject        parent_instance;

    LrgSnowCover  *snow;
    LrgCloudLayer *clouds;
    gfloat         sun[3];
    gfloat         snow_color[4];
    gdouble        origin[3];     /* the draw space's origin in the world (lit shader) */

    GrlShader     *shader;
    gboolean       shader_failed;
    LrgGroundOverlayLocations locs;
    gint           loc_cut;
};

G_DEFINE_FINAL_TYPE (LrgGroundOverlay, lrg_ground_overlay, G_TYPE_OBJECT)

/* the blended pass: the vertices in draw space, rlgl's matrix stack
 * already in matModel (raylib multiplies it in), the fragments through
 * the shared evaluation */
static const gchar overlay_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "in vec2 vertexTexCoord;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "out vec3 fragLocal;\n"
    "out vec3 fragNormal;\n"
    "out vec2 fragUV;\n"
    "void main() {\n"
    "    fragUV = vertexTexCoord;\n"
    "    fragLocal = (matModel * vec4(vertexPosition, 1.0)).xyz;\n"
    "    vec3 n = mat3(matModel) * vertexNormal;\n"
    "    float nl = length(n);\n"
    "    fragNormal = nl > 1e-6 ? n / nl : vec3(0.0);\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const gchar overlay_fs_head[] =
    "#version 330\n"
    "in vec3 fragLocal;\n"
    "in vec3 fragNormal;\n"
    "in vec2 fragUV;\n"
    "uniform sampler2D texture0;\n"
    "uniform int cutAlpha;\n"
    LRG_GROUND_OVERLAY_GLSL
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    if (cutAlpha != 0 && texture(texture0, fragUV).a < 0.35) discard;\n"
    "    float snow;\n"
    "    float shadow;\n"
    "    lrg_overlay_eval(fragLocal, fragNormal, snow, shadow);\n"
    "    float a = 1.0 - (1.0 - snow) * (1.0 - shadow);\n"
    "    if (a <= 0.002) discard;\n"
    "    finalColor = vec4(ovSnowColor.rgb * snow * (1.0 - shadow) / a, a);\n"
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
lrg_ground_overlay_set_origin (LrgGroundOverlay *self,
                               gdouble           x,
                               gdouble           y,
                               gdouble           z)
{
    g_return_if_fail (LRG_IS_GROUND_OVERLAY (self));

    self->origin[0] = x;
    self->origin[1] = y;
    self->origin[2] = z;
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

void
lrg_ground_overlay_locations (Shader                     shader,
                              LrgGroundOverlayLocations *out_locs)
{
    out_locs->on = GetShaderLocation (shader, "ovOn");
    out_locs->noise_origin = GetShaderLocation (shader, "ovNoiseOrigin");
    out_locs->inv_scale = GetShaderLocation (shader, "ovInvScale");
    out_locs->coverage = GetShaderLocation (shader, "ovCoverage");
    out_locs->sun = GetShaderLocation (shader, "ovSun");
    out_locs->altitude = GetShaderLocation (shader, "ovAltitude");
    out_locs->shadow = GetShaderLocation (shader, "ovShadow");
    out_locs->snow_level = GetShaderLocation (shader, "ovSnowLevel");
    out_locs->snow_color = GetShaderLocation (shader, "ovSnowColor");
    out_locs->track_count = GetShaderLocation (shader, "ovTrackCount");
    out_locs->tracks = GetShaderLocation (shader, "ovTracks");
}

gboolean
lrg_ground_overlay_upload (LrgGroundOverlay                *self,
                           Shader                           shader,
                           const LrgGroundOverlayLocations *locs,
                           const gdouble                   *origin)
{
    gfloat  tracks[LRG_GROUND_OVERLAY_MAX_TRACKS * 4];
    gfloat  noise_origin[2] = { 0.0f, 0.0f };
    gfloat  inv_scale = 0.0f;
    gfloat  coverage = 0.0f;
    gfloat  altitude = 0.0f;
    gfloat  shadow = 0.0f;
    gfloat  level = 0.0f;
    gint    n_tracks = 0;
    gint    on;

    on = self != NULL && lrg_ground_overlay_is_visible (self) ? 1 : 0;
    SetShaderValue (shader, locs->on, &on, SHADER_UNIFORM_INT);
    if (!on)
        return FALSE;
    if (origin == NULL)
        origin = self->origin;

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

    SetShaderValue (shader, locs->noise_origin, noise_origin, SHADER_UNIFORM_VEC2);
    SetShaderValue (shader, locs->inv_scale, &inv_scale, SHADER_UNIFORM_FLOAT);
    SetShaderValue (shader, locs->coverage, &coverage, SHADER_UNIFORM_FLOAT);
    SetShaderValue (shader, locs->sun, self->sun, SHADER_UNIFORM_VEC3);
    SetShaderValue (shader, locs->altitude, &altitude, SHADER_UNIFORM_FLOAT);
    SetShaderValue (shader, locs->shadow, &shadow, SHADER_UNIFORM_FLOAT);
    SetShaderValue (shader, locs->snow_level, &level, SHADER_UNIFORM_FLOAT);
    SetShaderValue (shader, locs->snow_color, self->snow_color, SHADER_UNIFORM_VEC4);
    SetShaderValue (shader, locs->track_count, &n_tracks, SHADER_UNIFORM_INT);
    if (n_tracks > 0)
        SetShaderValueV (shader, locs->tracks, tracks, SHADER_UNIFORM_VEC4, n_tracks);
    return TRUE;
}

static gboolean
ensure_shader (LrgGroundOverlay *self)
{
    if (self->shader != NULL)
        return TRUE;
    if (self->shader_failed)
        return FALSE;

    self->shader = grl_shader_new_from_memory (overlay_vs, overlay_fs_head, NULL);
    if (self->shader == NULL || !grl_shader_is_valid (self->shader))
    {
        lrg_warning (LRG_LOG_DOMAIN_WEATHER, "ground overlay: the shader did not compile; no "
                     "snow or cloud shadows on the ground");
        g_clear_object (&self->shader);
        self->shader_failed = TRUE;
        return FALSE;
    }
    lrg_ground_overlay_locations (*(Shader *)grl_shader_get_handle (self->shader), &self->locs);
    self->loc_cut = grl_shader_get_location (self->shader, "cutAlpha");
    return TRUE;
}

/*
 * prepare:
 * Everything both blended passes share: the shader, its uniforms for the
 * draw space whose origin in the world is @origin, and the blend state.
 */
static gboolean
prepare (LrgGroundOverlay *self,
         const gdouble    *origin,
         gboolean          objects)
{
    gint cut = objects ? 1 : 0;

    if (!lrg_ground_overlay_is_visible (self) || !IsWindowReady () || !ensure_shader (self))
        return FALSE;

    lrg_ground_overlay_upload (self, *(Shader *)grl_shader_get_handle (self->shader), &self->locs,
                               origin);
    /* objects carry cut-out textures (fences, grates): their clear texels get nothing */
    SetShaderValue (*(Shader *)grl_shader_get_handle (self->shader), self->loc_cut, &cut,
                    SHADER_UNIFORM_INT);

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
