/* lrg-ground-overlay-private.h - the ground overlay's GLSL and uniforms
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared by LrgGroundOverlay's own blended pass and by LrgLightSet's
 * lit shader, which applies the same snow and cloud shadows in the one
 * pass that draws a surface (lrg_light_set_set_ground_overlay()): no
 * second draw of the terrain and the models.  Not installed and not
 * introspected.
 */

#pragma once

#include <glib.h>
#include <raylib.h>

#include "weather/lrg-ground-overlay.h"
#include "weather/lrg-cloud-layer-private.h"

G_BEGIN_DECLS

/*
 * LRG_GROUND_OVERLAY_GLSL:
 * The overlay uniforms (all prefixed "ov") and
 * - void lrg_overlay_eval (vec3 p, vec3 n, out float snow, out float shadow):
 *   the snow's opacity (flat ground x level, less the tracks) and the
 *   cloud shadow (0 clear .. the layer's strength) at draw-space @p with
 *   unit normal @n;
 * - vec3 lrg_overlay_apply (vec3 p, vec3 n, vec3 lit): @lit with the snow
 *   laid over it and the shadow over both, the one blend of
 *   lrg-ground-overlay.c in a shader's own output.
 * ovOn (int) is 0 when there is nothing to apply.
 */
#define LRG_GROUND_OVERLAY_GLSL \
    "uniform int ovOn;\n" \
    "uniform vec2 ovNoiseOrigin;\n" \
    "uniform float ovInvScale;\n" \
    "uniform float ovCoverage;\n" \
    "uniform vec3 ovSun;\n" \
    "uniform float ovAltitude;\n" \
    "uniform float ovShadow;\n" \
    "uniform float ovSnowLevel;\n" \
    "uniform vec4 ovSnowColor;\n" \
    "uniform int ovTrackCount;\n" \
    "uniform vec4 ovTracks[64];\n" \
    LRG_CLOUD_NOISE_GLSL \
    "void lrg_overlay_eval(vec3 p, vec3 n, out float snow, out float shadow) {\n" \
    "    shadow = 0.0;\n" \
    "    if (ovCoverage > 0.0 && ovSun.y > 0.05) {\n" \
    "        float rise = max(ovAltitude - p.y, 0.0);\n" \
    "        vec2 q = p.xz + ovSun.xz / ovSun.y * rise;\n" \
    "        float fade = smoothstep(0.05, 0.25, ovSun.y);\n" \
    "        shadow = lrg_cloud_density(ovNoiseOrigin + q * ovInvScale, ovCoverage) * ovShadow * fade;\n" \
    "    }\n" \
    "    snow = 0.0;\n" \
    "    if (ovSnowLevel > 0.0) {\n" \
    "        float flatness = smoothstep(0.55, 0.85, n.y);\n" \
    "        float dent = 0.0;\n" \
    "        if (flatness > 0.0) {\n" \
    "            for (int i = 0; i < ovTrackCount; i++) {\n" \
    "                float d = length(p.xz - ovTracks[i].xy);\n" \
    "                float r = ovTracks[i].z;\n" \
    "                dent = max(dent, ovTracks[i].w * (1.0 - smoothstep(0.5 * r, r, d)));\n" \
    "            }\n" \
    "            float patches = lrg_cloud_noise(p.xz * 0.11);\n" \
    "            float cover = clamp(ovSnowLevel * 1.5 - (1.0 - ovSnowLevel) * patches * 0.8, 0.0, 1.0);\n" \
    "            snow = flatness * cover * (1.0 - dent) * ovSnowColor.a;\n" \
    "        }\n" \
    "        shadow = 1.0 - (1.0 - shadow) * (1.0 - 0.3 * dent * ovSnowLevel);\n" \
    "    }\n" \
    "}\n" \
    "vec3 lrg_overlay_apply(vec3 p, vec3 n, vec3 lit) {\n" \
    "    float snow;\n" \
    "    float shadow;\n" \
    "    lrg_overlay_eval(p, n, snow, shadow);\n" \
    "    return (ovSnowColor.rgb * snow + lit * (1.0 - snow)) * (1.0 - shadow);\n" \
    "}\n"

/*
 * LrgGroundOverlayLocations:
 * Uniform locations of LRG_GROUND_OVERLAY_GLSL in one shader (-1 when the
 * compiler dropped an unused uniform).
 */
typedef struct
{
    gint on;
    gint noise_origin;
    gint inv_scale;
    gint coverage;
    gint sun;
    gint altitude;
    gint shadow;
    gint snow_level;
    gint snow_color;
    gint track_count;
    gint tracks;
} LrgGroundOverlayLocations;

/*
 * lrg_ground_overlay_locations:
 * Looks up the overlay uniforms of @shader.
 */
G_GNUC_INTERNAL
void
lrg_ground_overlay_locations (Shader                     shader,
                              LrgGroundOverlayLocations *out_locs);

/*
 * lrg_ground_overlay_upload:
 * Uploads @self's uniforms to @shader (the active program) for a draw
 * space whose origin in the world is @origin: the clouds and the newest
 * tracks placed by it, the sun, the snow level and colour.  A %NULL
 * @self, or one with nothing to show (lrg_ground_overlay_is_visible()),
 * uploads ovOn = 0.  A %NULL @origin takes the one lrg_ground_overlay_set_origin()
 * gave.  Returns whether anything is applied.
 */
G_GNUC_INTERNAL
gboolean
lrg_ground_overlay_upload (LrgGroundOverlay                  *self,
                           Shader                             shader,
                           const LrgGroundOverlayLocations   *locs,
                           const gdouble                     *origin);

G_END_DECLS
