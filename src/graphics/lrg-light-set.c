/* lrg-light-set.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Forward lighting for static geometry. Point lights are plain records
 * in a slot array (removed slots are reused); select() keeps the best N
 * by insertion into a fixed array, which beats sorting for N <= 16. The
 * shader uses raylib's default attribute and matrix names, so DrawMesh()
 * (LrgStaticBatch, LrgTerrain) feeds it model and normal matrices
 * without any extra work; lighting happens in the space the model
 * matrix maps to (origin-relative with a floating origin). Fog is a
 * blend toward one colour by distance from the selected camera; the
 * GLSL and lrg_fog_amount() share one model (lrg-light-set-private.h).
 *
 * Light grid: select() also sorts every light into the XZ cells its
 * radius reaches (a square of cells centred on the camera's cell, at
 * most LRG_LIGHT_SET_GRID_SLOTS per cell, the closest relative to their
 * radius first). apply() uploads the lights as a float texture (two
 * texels per light: position and radius, colour times intensity) and the
 * cell lists as a second one (four light indices per texel, -1 ends a
 * list), binds them to texture units 14 and 15 (above raylib's material
 * maps, which DrawMesh() binds and unbinds per draw) and the shader
 * walks the list of each fragment's cell. sample() reads the same lists
 * so billboards match the geometry.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-light-set.h"
#include "graphics/lrg-light-set-private.h"
#include "graphics/lrg-lod-group.h"
#include "lighting/lrg-shadow-renderer.h"
#include "lighting/lrg-shadow-renderer-private.h"
#include "weather/lrg-ground-overlay-private.h"
#include "core/lrg-prewarm.h"
#include "graphics/lrg-camera.h"
#include "world3d/lrg-world3d.h"
#include "lighting/lrg-environment-glsl-private.h"
#include "graphics/lrg-ssao-glsl-private.h"
#include "graphics/lrg-decal-glsl-private.h"

typedef struct
{
    gboolean used;
    gboolean enabled;
    gdouble  position[3];   /* world */
    gfloat   color[3];      /* 0 .. 1 */
    gfloat   radius;
    gfloat   intensity;
    guint    shadow;        /* shadow priority, 0: none (M14) */
    /* 0.9.0: spot lights, per-light shadow bias and render layers */
    gboolean spot;
    gfloat   direction[3];  /* unit, the way a spot shines */
    gfloat   inner;         /* cone half-angles, degrees */
    gfloat   outer;
    gfloat   shadow_bias;   /* world units, <= 0: the renderer's */
    guint32  layers;        /* render layers the light reaches */
} PointLight;

/* LitLocations:
 * The uniform locations of one program built from the lit library
 * (the light set's own, or a user shader given to
 * lrg_light_set_bind_shader()); -1 where the compiler dropped one. */
typedef struct
{
    gint ambient;
    gint sun_dir;
    gint sun_color;
    gint count;
    gint positions;
    gint colors;
    gint radii;
    gint cutoff;
    gint use_grid;
    gint light_data;
    gint light_grid;
    gint grid_params;
    gint limit;
    gint emission_color;
    gint grid_layers;
    gint light_shadow;
    gint air_params;
    gint air_tint;
    gint air_camera;
    gint air_light_color;
    gint map_camera;
    gint spots;
    gint spot_extra;
    gint draw_layers;
    gint output_linear;
    LrgLightSetFogLocations   fog;
    LrgShadowLocations        shadow;
    LrgGroundOverlayLocations overlay;
} LitLocations;

struct _LrgLightSet
{
    GObject     parent_instance;

    gfloat      ambient[3];
    gboolean    has_directional;
    gfloat      directional_dir[3];
    gfloat      directional_color[3];
    gdouble     origin[3];

    GArray     *lights;          /* PointLight, slot = id */
    guint       n_lights;        /* used slots */
    guint       max_active;
    gfloat      max_distance;
    guint       active[LRG_LIGHT_SET_MAX_ACTIVE];
    guint       n_active;
    gfloat      cutoff;
    gdouble     camera[3];       /* world, last select() */

    gboolean    fog_on;
    LrgFogType  fog_type;
    gfloat      fog_color[4];    /* 0 .. 1, alpha caps the amount */
    gfloat      fog_density;
    gfloat      fog_start;
    gfloat      fog_end;
    gfloat      fog_base;        /* world height */
    gfloat      fog_falloff;

    /* light grid (cells == 0: off) */
    gfloat      grid_cell;
    guint       grid_cells;
    gboolean    grid_built;      /* select() filled the lists */
    gdouble     grid_x0;         /* world corner of cell (0, 0) */
    gdouble     grid_z0;
    guint      *grid_ids;        /* cells * cells * SLOTS light ids */
    gfloat     *grid_keys;
    guint8     *grid_counts;
    guint       tex_data;        /* GL texture ids, 0 = none */
    gint        tex_data_height;
    guint       tex_grid;
    gint        tex_grid_w;
    gint        tex_grid_h;

    /* grid change tracking: build_grid() and upload_grid() redo their
     * work only when an input moved (see grid_inputs_unchanged()) */
    guint       lights_serial;   /* bumped by every point light change */
    guint       grid_serial;     /* bumped by every grid rebuild */
    guint       key_lights_serial;
    gdouble     key_x0;
    gdouble     key_y0;
    gdouble     key_z0;
    gfloat      key_max_distance;
    gdouble     key_camera[3];
    guint       up_grid_serial;  /* the build in the cell texture, 0: none */
    guint      *up_order;        /* light ids of the light texture, first use */
    guint       up_n_data;
    gfloat     *up_data;         /* the light texture as uploaded */
    gfloat     *up_fresh;        /* scratch: this apply's light texture */
    gfloat     *up_cells;        /* scratch: the cell texture */
    gsize       up_cells_len;
    guint      *up_index_of;     /* scratch: light id -> texture row */
    gsize       up_index_len;

    gfloat      light_limit;
    gfloat      emission[3];

    /* M14: clusters (grid layers and slots per cell) */
    guint       grid_slots;
    guint       grid_layers;
    gfloat      grid_layer_height;
    gdouble     grid_y0;         /* world base of layer 0 */

    /* M14: shadows and the volumetric air */
    LrgShadowRenderer *shadows;
    gboolean    air_on;
    gfloat      air_density;
    gfloat      air_g;
    gfloat      air_intensity;
    guint       air_steps;
    gfloat      air_distance;
    gfloat      air_tint[3];

    /* M14 (U14.10): snow and cloud shadows applied in the lit shader */
    LrgGroundOverlay          *overlay;

    /* 0.9.0: the hooks of the other passes, the draw's layers and the
     * output encoding */
    LrgEnvironment *environment;
    LrgSsao        *ssao;
    LrgDecalSet    *decals;
    guint32     draw_layers;
    gboolean    output_linear;

    GrlShader  *shader;
    gboolean    shader_tried;
    LitLocations locs;          /* of the light set's own program */
    GHashTable *foreign;        /* program id -> LitLocations, lrg_light_set_bind_shader() */
};

/* texture units of the grid textures: above raylib's 12 material maps */
#define GRID_DATA_UNIT  (LRG_LIGHT_SET_GRID_DATA_UNIT)
#define GRID_CELLS_UNIT (LRG_LIGHT_SET_GRID_CELLS_UNIT)

/* floats per light in the grid's light texture (four RGBA texels) */
#define TEXEL_FLOATS    (LRG_LIGHT_SET_GRID_TEXELS * 4)

/* the soft light limit passes values up to this share of the limit */
#define LIMIT_KNEE      (0.75f)

static void lrg_light_set_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgLightSet, lrg_light_set, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                lrg_light_set_prewarmable_init))

static void build_grid (LrgLightSet *self,
                        gdouble      camera_x,
                        gdouble      camera_y,
                        gdouble      camera_z);

/* The bound light set (lrg_light_set_bind()); drawing is single
 * threaded, like raylib's own current-shader state. */
static LrgLightSet *bound_light_set = NULL;

/* GLSL 330 with raylib's default attribute and uniform names. Also the
 * vertex source of user lit shaders (lrg_light_set_get_vertex_source()). */
static const gchar light_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec3 fragPosition;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragNormal;\n"
    "void main() {\n"
    "    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    fragNormal = vec3(matNormal * vec4(vertexNormal, 0.0));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

/*
 * The lit shader library (lrg_light_set_get_glsl_library()): every
 * uniform, hook and function of the lit fragment shader, without
 * "#version", varyings and main().  In order:
 *
 * 1. uniforms: the material (texture0, colDiffuse, the map samplers,
 *    mapFlags, pbrParams, emission), the lights (ambient, sun, the
 *    active list with its spot cones, the grid), the light limit, the
 *    eye (mapCamera), the draw's render layers and the output encoding;
 * 2. the shared snippets: cluster lookup, spot cone, fog, shadows, the
 *    volumetric air, the ground overlay;
 * 3. LRG_PI, lrg_to_linear and lrg_to_display (the hooks may use
 *    them), then the hooks of the other passes (environment / IBL, SSAO, decals),
 *    no-op defaults until those objects are bound;
 * 4. the model: lrg_to_linear / lrg_to_display (sRGB as gamma 2.2),
 *    lrg_brdf_direct (GGX D, Smith-Schlick G with Karis' k, Schlick F,
 *    Lambert diffuse; pi folded in so a white light on a white surface
 *    facing it is 1), lrg_light_one (one point or spot light with its
 *    window falloff, cone, layers and shadows), lrg_light_pbr_ex /
 *    lrg_light_pbr (sun + lights + indirect + light limit, linear
 *    radiance), lrg_surface (material sampling), lrg_light_shade
 *    (everything main() does: surface, decals, lighting, emission, snow,
 *    air, fog, output encoding, alpha cutout).
 *
 * Light colours (sunColor, lightColor, the grid's colour texel and
 * ambientColor) arrive in display space as before 0.9.0 (colour x
 * intensity); the falloff and cone are display-space factors too, and
 * the product is linearized once, so a lone light still gives the
 * classic (1 - d / r)^2 curve on screen that lrg_light_set_sample() and
 * the billboards use.
 */
#define LIGHT_LIBRARY \
    "uniform sampler2D texture0;\n" \
    "uniform vec4 colDiffuse;\n" \
    "uniform vec3 ambientColor;\n" \
    "uniform vec3 sunDirection;\n" \
    "uniform vec3 sunColor;\n" \
    "uniform int lightCount;\n" \
    "uniform vec3 lightPosition[16];\n" \
    "uniform vec3 lightColor[16];\n" \
    "uniform float lightRadius[16];\n" \
    "uniform float lightShadow[16];\n" \
    "uniform vec4 lightSpot[16];\n" \
    "uniform vec4 lightSpotExtra[16];\n" \
    "uniform float alphaCutoff;\n" \
    "uniform int useGrid;\n" \
    "uniform sampler2D lightData;\n" \
    "uniform sampler2D lightGrid;\n" \
    LRG_LIGHT_CLUSTER_GLSL \
    LRG_LIGHT_SPOT_GLSL \
    "uniform float lightLimit;\n" \
    "uniform sampler2D emissionMap;\n" \
    "uniform vec3 emissionColor;\n" \
    "uniform vec4 emissionOwn;\n" \
    "uniform sampler2D normalMap;\n" \
    "uniform sampler2D metallicMap;\n" \
    "uniform sampler2D ormMap;\n" \
    "uniform sampler2D heightMap;\n" \
    "uniform int mapFlags;\n" \
    "uniform vec4 pbrParams;\n" \
    "uniform vec3 mapCamera;\n" \
    "uniform float heightScale;\n" \
    "uniform ivec2 drawLayers;\n" \
    "uniform int outputLinear;\n" \
    LRG_FOG_GLSL \
    LRG_SHADOW_GLSL \
    LRG_LIGHT_AIR_GLSL \
    LRG_GROUND_OVERLAY_GLSL \
    "const float LRG_PI = 3.14159265;\n" \
    "vec3 lrg_to_linear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }\n" \
    "vec3 lrg_to_display(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }\n" \
    LRG_ENV_GLSL \
    LRG_SSAO_GLSL \
    LRG_DECAL_GLSL \
    "struct LrgSurface {\n" \
    "    vec4 albedo;\n" \
    "    vec3 normal;\n" \
    "    float metallic;\n" \
    "    float roughness;\n" \
    "    float occlusion;\n" \
    "    vec3 emission;\n" \
    "};\n" \
    "bool lrg_layers_hit(vec4 extra) {\n" \
    "    ivec2 m = ivec2(int(extra.z + 0.5), int(extra.w + 0.5));\n" \
    "    return ((m.x & drawLayers.x) | (m.y & drawLayers.y)) != 0;\n" \
    "}\n" \
    "vec3 lrg_brdf_direct(vec3 n, vec3 v, vec3 l, vec3 albedo, float metal, float rough) {\n" \
    "    float nl = max(dot(n, l), 0.0);\n" \
    "    if (nl <= 0.0) return vec3(0.0);\n" \
    "    float nv = max(dot(n, v), 1e-4);\n" \
    "    vec3 h = normalize(l + v);\n" \
    "    float nh = max(dot(n, h), 0.0);\n" \
    "    float vh = max(dot(v, h), 0.0);\n" \
    "    float r = clamp(rough, 0.045, 1.0);\n" \
    "    float a2 = r * r * r * r;\n" \
    "    float dd = nh * nh * (a2 - 1.0) + 1.0;\n" \
    "    float D = a2 / (LRG_PI * dd * dd);\n" \
    "    float k = (r + 1.0) * (r + 1.0) / 8.0;\n" \
    "    float G = (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k));\n" \
    "    vec3 F0 = mix(vec3(0.04), albedo, metal);\n" \
    "    vec3 F = F0 + (1.0 - F0) * pow(1.0 - vh, 5.0);\n" \
    "    vec3 spec = D * G * F / (4.0 * nv * nl);\n" \
    "    vec3 kd = (1.0 - F) * (1.0 - metal);\n" \
    "    return (kd * albedo + spec * LRG_PI) * nl;\n" \
    "}\n" \
    "vec3 gLrgIrradiance = vec3(0.0);\n" \
    "vec3 lrg_light_one(vec3 p, vec3 ng, vec3 n, vec3 v, vec3 albedo, float metal, float rough,\n" \
    "                   vec3 lp, float r, vec3 c, int pslot, vec4 spot, vec4 extra) {\n" \
    "    if (!lrg_layers_hit(extra)) return vec3(0.0);\n" \
    "    vec3 d = lp - p;\n" \
    "    float dist = length(d);\n" \
    "    float k = clamp(1.0 - dist / r, 0.0, 1.0);\n" \
    "    if (k <= 0.0) return vec3(0.0);\n" \
    "    vec3 l = dist > 1e-4 ? d / dist : n;\n" \
    "    float fd = k * k;\n" \
    "    if (spot.w > -1.5) fd *= lrg_spot_cone(-l, spot, extra.x);\n" \
    "    float nl = max(dot(n, l), 0.0);\n" \
    "    if (fd <= 0.0 || nl <= 0.0) return vec3(0.0);\n" \
    "    float sh = 1.0;\n" \
    "    if (pslot >= 0) sh = lrg_point_shadow(pslot, p, ng);\n" \
    "    int sslot = int(floor(extra.y + 0.5));\n" \
    "    if (sslot >= 0) sh *= lrg_spot_shadow(sslot, p, ng);\n" \
    "    vec3 rad = lrg_to_linear(c * fd) * sh;\n" \
    "    gLrgIrradiance += rad * nl;\n" \
    "    return lrg_brdf_direct(n, v, l, albedo, metal, rough) * rad;\n" \
    "}\n" \
    "vec3 lrg_light_pbr_ex(vec3 p, vec3 ng, vec3 n, vec3 v, vec4 albedo, float metal, float rough, float ao) {\n" \
    "    vec3 alb = albedo.rgb;\n" \
    "    vec3 rad = vec3(0.0);\n" \
    "    gLrgIrradiance = vec3(0.0);\n" \
    "    if (dot(sunColor, sunColor) > 0.0) {\n" \
    "        vec3 l = -sunDirection;\n" \
    "        float nl = max(dot(n, l), 0.0);\n" \
    "        if (nl > 0.0 || dot(ng, l) > 0.0) {\n" \
    "            vec3 sr = lrg_to_linear(sunColor) * lrg_sun_shadow(p, ng);\n" \
    "            gLrgIrradiance += sr * nl;\n" \
    "            rad += lrg_brdf_direct(n, v, l, alb, metal, rough) * sr;\n" \
    "        }\n" \
    "    }\n" \
    "    if (useGrid == 1) {\n" \
    "        ivec2 base;\n" \
    "        if (lrg_cluster_base(p, base)) {\n" \
    "            int per = int(gridLayers.w + 0.5) / 4;\n" \
    "            bool done = false;\n" \
    "            for (int t = 0; t < 8 && !done; t++) {\n" \
    "                if (t >= per) break;\n" \
    "                vec4 ids = texelFetch(lightGrid, base + ivec2(t, 0), 0);\n" \
    "                for (int j = 0; j < 4; j++) {\n" \
    "                    if (ids[j] < 0.0) { done = true; break; }\n" \
    "                    int id = int(ids[j] + 0.5);\n" \
    "                    vec4 pr = texelFetch(lightData, ivec2(0, id), 0);\n" \
    "                    vec4 col = texelFetch(lightData, ivec2(1, id), 0);\n" \
    "                    vec4 sp = texelFetch(lightData, ivec2(2, id), 0);\n" \
    "                    vec4 sx = texelFetch(lightData, ivec2(3, id), 0);\n" \
    "                    rad += lrg_light_one(p, ng, n, v, alb, metal, rough, pr.xyz, pr.w, col.rgb,\n" \
    "                                         int(floor(col.w + 0.5)), sp, sx);\n" \
    "                }\n" \
    "            }\n" \
    "        }\n" \
    "    } else {\n" \
    "        for (int i = 0; i < 16; i++) {\n" \
    "            if (i >= lightCount) break;\n" \
    "            rad += lrg_light_one(p, ng, n, v, alb, metal, rough, lightPosition[i], lightRadius[i],\n" \
    "                                 lightColor[i], int(floor(lightShadow[i] + 0.5)),\n" \
    "                                 lightSpot[i], lightSpotExtra[i]);\n" \
    "        }\n" \
    "    }\n" \
    "    float occ = ao * lrg_ssao();\n" \
    "    vec3 ind;\n" \
    "    if (lrg_env_active()) {\n" \
    "        lrg_env_set_point(p);\n" \
    "        float nv = max(dot(n, v), 1e-4);\n" \
    "        vec2 brdf = lrg_env_brdf(nv, rough);\n" \
    "        vec3 F0 = mix(vec3(0.04), alb, metal);\n" \
    "        vec3 Fr = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - nv, 5.0);\n" \
    "        vec3 kd = (1.0 - Fr) * (1.0 - metal);\n" \
    "        vec3 irr = lrg_env_diffuse(n);\n" \
    "        gLrgIrradiance += irr * occ;\n" \
    "        ind = kd * alb * irr + lrg_env_specular(n, v, rough) * (F0 * brdf.x + brdf.y);\n" \
    "    } else {\n" \
    "        vec3 amb = lrg_to_linear(ambientColor);\n" \
    "        gLrgIrradiance += amb * occ;\n" \
    "        ind = amb * alb;\n" \
    "    }\n" \
    "    rad += ind * occ;\n" \
    "    if (lightLimit > 0.0) {\n" \
    "        vec3 e = lrg_to_display(gLrgIrradiance);\n" \
    "        float knee = lightLimit * 0.75;\n" \
    "        vec3 over = max(e - knee, 0.0);\n" \
    "        vec3 el = min(e, vec3(knee)) + (lightLimit - knee) * (1.0 - exp(-over / (lightLimit - knee)));\n" \
    "        vec3 lin = lrg_to_linear(el);\n" \
    "        for (int c = 0; c < 3; c++)\n" \
    "            if (gLrgIrradiance[c] > 1e-6) rad[c] *= lin[c] / gLrgIrradiance[c];\n" \
    "    }\n" \
    "    return rad;\n" \
    "}\n" \
    "vec3 lrg_light_pbr(vec3 p, vec3 n, vec3 v, vec4 albedo, float metal, float rough, float ao) {\n" \
    "    return lrg_light_pbr_ex(p, n, n, v, albedo, metal, rough, ao);\n" \
    "}\n" \
    "LrgSurface lrg_surface(vec3 p, vec3 ng, vec2 uv, vec4 vcol) {\n" \
    "    LrgSurface s;\n" \
    "    vec3 n = ng;\n" \
    "    float metal = pbrParams.x;\n" \
    "    float rough = pbrParams.y;\n" \
    "    float ao = 1.0;\n" \
    "    if ((mapFlags & 5) != 0) {\n" \
    "        vec3 dp1 = dFdx(p);\n" \
    "        vec3 dp2 = dFdy(p);\n" \
    "        vec2 du1 = dFdx(uv);\n" \
    "        vec2 du2 = dFdy(uv);\n" \
    "        vec3 p2 = cross(dp2, ng);\n" \
    "        vec3 p1 = cross(ng, dp1);\n" \
    "        vec3 tt = p2 * du1.x + p1 * du2.x;\n" \
    "        vec3 bb = p2 * du1.y + p1 * du2.y;\n" \
    "        float inv = inversesqrt(max(max(dot(tt, tt), dot(bb, bb)), 1e-12));\n" \
    "        mat3 tbn = mat3(tt * inv, bb * inv, ng);\n" \
    "        if ((mapFlags & 4) != 0) {\n" \
    "            vec3 vt = normalize(mapCamera - p) * tbn;\n" \
    "            float h = texture(heightMap, uv).r;\n" \
    "            uv -= vt.xy / max(vt.z, 0.25) * (h - 0.5) * heightScale;\n" \
    "        }\n" \
    "        if ((mapFlags & 1) != 0) {\n" \
    "            vec3 nm = texture(normalMap, uv).rgb * 2.0 - 1.0;\n" \
    "            nm.xy *= pbrParams.w;\n" \
    "            n = normalize(tbn * nm);\n" \
    "        }\n" \
    "    }\n" \
    "    if ((mapFlags & 2) != 0) {\n" \
    "        vec4 mg = texture(metallicMap, uv);\n" \
    "        metal = mg.r;\n" \
    "        rough = 1.0 - mg.a;\n" \
    "    }\n" \
    "    if ((mapFlags & 8) != 0) {\n" \
    "        vec3 orm = texture(ormMap, uv).rgb;\n" \
    "        ao = mix(1.0, orm.r, pbrParams.z);\n" \
    "        rough *= orm.g;\n" \
    "        metal *= orm.b;\n" \
    "    }\n" \
    "    vec4 texel = texture(texture0, uv) * colDiffuse * vcol;\n" \
    "    s.albedo = vec4(lrg_to_linear(texel.rgb), texel.a);\n" \
    "    s.normal = n;\n" \
    "    s.metallic = clamp(metal, 0.0, 1.0);\n" \
    "    s.roughness = clamp(rough, 0.0, 1.0);\n" \
    "    s.occlusion = ao;\n" \
    "    s.emission = lrg_to_linear(texture(emissionMap, uv).rgb\n" \
    "                               * (emissionOwn.w > 0.5 ? emissionOwn.rgb : emissionColor));\n" \
    "    return s;\n" \
    "}\n" \
    "vec4 lrg_light_shade(vec3 p, vec3 normal, vec2 uv, vec4 vcol) {\n" \
    "    float nlen = length(normal);\n" \
    "    vec3 ng = nlen > 0.0 ? normal / nlen : vec3(0.0, 1.0, 0.0);\n" \
    "    LrgSurface s = lrg_surface(p, ng, uv, vcol);\n" \
    "    if (s.albedo.a < alphaCutoff) discard;\n" \
    "    vec3 dv = mapCamera - p;\n" \
    "    vec3 v = dot(dv, dv) > 1e-10 ? normalize(dv) : ng;\n" \
    "    vec3 lit;\n" \
    "    if ((mapFlags & 32) != 0) {\n" \
    "        lit = s.albedo.rgb + s.emission;\n" \
    "    } else {\n" \
    "        lrg_decals_apply(p, ng, s.albedo, s.normal, s.roughness, s.metallic);\n" \
    "        lit = lrg_light_pbr_ex(p, ng, s.normal, v, s.albedo, s.metallic, s.roughness, s.occlusion)\n" \
    "              + s.emission;\n" \
    "    }\n" \
    "    vec3 col = lrg_to_display(lit);\n" \
    "    if (ovOn != 0) col = lrg_overlay_apply(p, s.normal, col);\n" \
    "    if (airParams.x > 0.0) {\n" \
    "        vec3 ins;\n" \
    "        float tr = lrg_air(p, ins);\n" \
    "        col = col * tr + ins;\n" \
    "    }\n" \
    "    col = mix(col, fogColor.rgb, lrg_fog_amount(p));\n" \
    "    if (outputLinear != 0) col = lrg_to_linear(col);\n" \
    "    return vec4(col, s.albedo.a);\n" \
    "}\n"

static const gchar light_library[] = LIGHT_LIBRARY;

static const gchar light_fs[] =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragNormal;\n"
    "out vec4 finalColor;\n"
    LIGHT_LIBRARY
    "void main() {\n"
    "    finalColor = lrg_light_shade(fragPosition, fragNormal, fragTexCoord, fragColor);\n"
    "}\n";


/*
 * The fragment source @fs with the LOD cross-fade spliced in: the
 * lrg_lod_get_glsl() snippet (the lodFade uniform and
 * lrg_lod_fade_discard()) before main() and the discard as main()'s
 * first statement, so items fading between LOD levels dither out here
 * exactly like in the other passes (lrg_lod_bind_fade() sets lodFade;
 * 0, the default, keeps every fragment).
 */
static gchar *
light_fs_with_lod (const gchar *fs)
{
    const gchar *main_start = strstr (fs, "void main() {\n");

    if (main_start == NULL)
        return g_strdup (fs);
    return g_strdup_printf ("%.*s%svoid main() {\n    lrg_lod_fade_discard ();\n%s", (gint) (main_start - fs), fs,
                            lrg_lod_get_glsl (), main_start + strlen ("void main() {\n"));
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

static void
lrg_light_set_finalize (GObject *object)
{
    LrgLightSet *self = LRG_LIGHT_SET (object);

    g_clear_object (&self->shader);
    g_clear_object (&self->shadows);
    g_clear_object (&self->overlay);
    g_clear_object (&self->environment);
    g_clear_object (&self->ssao);
    g_clear_object (&self->decals);
    g_clear_pointer (&self->foreign, g_hash_table_unref);
    g_clear_pointer (&self->lights, g_array_unref);
    g_clear_pointer (&self->grid_ids, g_free);
    g_clear_pointer (&self->grid_keys, g_free);
    g_clear_pointer (&self->grid_counts, g_free);
    g_clear_pointer (&self->up_order, g_free);
    g_clear_pointer (&self->up_data, g_free);
    g_clear_pointer (&self->up_fresh, g_free);
    g_clear_pointer (&self->up_cells, g_free);
    g_clear_pointer (&self->up_index_of, g_free);
    /* GPU textures only while the context lives (never at exit) */
    if (IsWindowReady ())
    {
        if (self->tex_data != 0)
            rlUnloadTexture (self->tex_data);
        if (self->tex_grid != 0)
            rlUnloadTexture (self->tex_grid);
    }
    self->tex_data = 0;
    self->tex_grid = 0;

    G_OBJECT_CLASS (lrg_light_set_parent_class)->finalize (object);
}

static void
lrg_light_set_class_init (LrgLightSetClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_light_set_finalize;
}

static void
lrg_light_set_init (LrgLightSet *self)
{
    self->ambient[0] = self->ambient[1] = self->ambient[2] = 64.0f / 255.0f;
    self->lights = g_array_new (FALSE, TRUE, sizeof (PointLight));
    self->max_active = LRG_LIGHT_SET_MAX_ACTIVE;
    self->fog_type = LRG_FOG_TYPE_LINEAR;
    self->fog_falloff = 1.0f;
    self->grid_slots = LRG_LIGHT_SET_GRID_SLOTS;
    self->grid_layers = 1;
    self->grid_layer_height = 4.0f;
    self->air_tint[0] = self->air_tint[1] = self->air_tint[2] = 1.0f;
    self->draw_layers = LRG_RENDER_LAYERS_MASK_ALL;

    /* build the GPU resources behind a loading screen, not on the
     * first draw (see lrg_prewarm_track()) */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgLightSet *
lrg_light_set_new (void)
{
    return g_object_new (LRG_TYPE_LIGHT_SET, NULL);
}

static void
color_to_rgb (const GrlColor *color,
              gfloat         *out)
{
    out[0] = (gfloat)color->r / 255.0f;
    out[1] = (gfloat)color->g / 255.0f;
    out[2] = (gfloat)color->b / 255.0f;
}

/* ------------------------------------------------------------------------ */
/* Global lights                                                            */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_set_ambient (LrgLightSet    *self,
                           const GrlColor *color)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (color != NULL);

    color_to_rgb (color, self->ambient);
}

void
lrg_light_set_set_directional (LrgLightSet      *self,
                               const GrlVector3 *direction,
                               const GrlColor   *color)
{
    gfloat len;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->has_directional = FALSE;
    if (direction == NULL || color == NULL)
        return;

    len = sqrtf (direction->x * direction->x + direction->y * direction->y
                 + direction->z * direction->z);
    if (len <= 0.0f || !isfinite (len))
        return;

    self->directional_dir[0] = direction->x / len;
    self->directional_dir[1] = direction->y / len;
    self->directional_dir[2] = direction->z / len;
    color_to_rgb (color, self->directional_color);
    self->has_directional = TRUE;
}

void
lrg_light_set_set_origin (LrgLightSet *self,
                          gdouble      x,
                          gdouble      y,
                          gdouble      z)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->origin[0] = x;
    self->origin[1] = y;
    self->origin[2] = z;
}

/* ------------------------------------------------------------------------ */
/* Point lights                                                             */
/* ------------------------------------------------------------------------ */

static PointLight *
light_at (LrgLightSet *self,
          guint        light)
{
    PointLight *pl;

    if (light >= self->lights->len)
        return NULL;
    pl = &g_array_index (self->lights, PointLight, light);
    return pl->used ? pl : NULL;
}

guint
lrg_light_set_add_point_light (LrgLightSet    *self,
                               gdouble         x,
                               gdouble         y,
                               gdouble         z,
                               const GrlColor *color,
                               gfloat          radius)
{
    PointLight *pl = NULL;
    guint       id;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_LIGHT_SET_INVALID_LIGHT);
    g_return_val_if_fail (color != NULL, LRG_LIGHT_SET_INVALID_LIGHT);
    g_return_val_if_fail (radius > 0.0f, LRG_LIGHT_SET_INVALID_LIGHT);

    /* Reuse the first free slot, else append. */
    for (id = 0; id < self->lights->len; id++)
    {
        if (!g_array_index (self->lights, PointLight, id).used)
        {
            pl = &g_array_index (self->lights, PointLight, id);
            break;
        }
    }
    if (pl == NULL)
    {
        g_array_set_size (self->lights, self->lights->len + 1);
        id = self->lights->len - 1;
        pl = &g_array_index (self->lights, PointLight, id);
    }

    memset (pl, 0, sizeof *pl);
    pl->used = TRUE;
    pl->enabled = TRUE;
    pl->position[0] = x;
    pl->position[1] = y;
    pl->position[2] = z;
    color_to_rgb (color, pl->color);
    pl->radius = radius;
    pl->intensity = 1.0f;
    pl->direction[1] = -1.0f;
    pl->inner = 30.0f;
    pl->outer = 45.0f;
    pl->layers = LRG_RENDER_LAYERS_MASK_ALL;
    self->n_lights++;
    self->lights_serial++;
    return id;
}

gboolean
lrg_light_set_remove_point_light (LrgLightSet *self,
                                  guint        light)
{
    PointLight *pl;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);

    pl = light_at (self, light);
    if (pl == NULL)
        return FALSE;
    pl->used = FALSE;
    self->n_lights--;
    self->lights_serial++;
    /* The last selection may name it; drop the selection. */
    self->n_active = 0;
    self->grid_built = FALSE;
    return TRUE;
}

void
lrg_light_set_clear_point_lights (LrgLightSet *self)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    g_array_set_size (self->lights, 0);
    self->n_lights = 0;
    self->lights_serial++;
    self->n_active = 0;
    self->grid_built = FALSE;
}

guint
lrg_light_set_get_point_light_count (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);
    return self->n_lights;
}

void
lrg_light_set_set_point_light_position (LrgLightSet *self,
                                        guint        light,
                                        gdouble      x,
                                        gdouble      y,
                                        gdouble      z)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->position[0] = x;
    pl->position[1] = y;
    pl->position[2] = z;
    self->lights_serial++;
}

void
lrg_light_set_set_point_light_color (LrgLightSet    *self,
                                     guint           light,
                                     const GrlColor *color)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (color != NULL);

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    color_to_rgb (color, pl->color);
    self->lights_serial++;
}

void
lrg_light_set_set_point_light_radius (LrgLightSet *self,
                                      guint        light,
                                      gfloat       radius)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (radius > 0.0f);

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->radius = radius;
    self->lights_serial++;
}

void
lrg_light_set_set_point_light_intensity (LrgLightSet *self,
                                         guint        light,
                                         gfloat       intensity)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (intensity >= 0.0f);

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->intensity = intensity;
    self->lights_serial++;
}

void
lrg_light_set_set_point_light_enabled (LrgLightSet *self,
                                       guint        light,
                                       gboolean     enabled)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->enabled = enabled;
    self->lights_serial++;
}

void
lrg_light_set_set_max_active (LrgLightSet *self,
                              guint        count)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (count <= LRG_LIGHT_SET_MAX_ACTIVE);

    self->max_active = count;
}

void
lrg_light_set_set_max_distance (LrgLightSet *self,
                                gfloat       distance)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (distance >= 0.0f);

    self->max_distance = distance;
}

/* ------------------------------------------------------------------------ */
/* Selection                                                                */
/* ------------------------------------------------------------------------ */

guint
lrg_light_set_select (LrgLightSet *self,
                      gdouble      camera_x,
                      gdouble      camera_y,
                      gdouble      camera_z)
{
    gdouble keys[LRG_LIGHT_SET_MAX_ACTIVE];
    guint   id;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);

    /* The fog measures distances from this camera too. */
    self->camera[0] = camera_x;
    self->camera[1] = camera_y;
    self->camera[2] = camera_z;

    self->n_active = 0;
    if (self->grid_cells > 0)
        build_grid (self, camera_x, camera_y, camera_z);
    if (self->max_active == 0)
        return 0;

    for (id = 0; id < self->lights->len; id++)
    {
        PointLight *pl = &g_array_index (self->lights, PointLight, id);
        gdouble     dx;
        gdouble     dy;
        gdouble     dz;
        gdouble     key;
        guint       slot;

        if (!pl->used || !pl->enabled || pl->intensity <= 0.0f)
            continue;

        dx = pl->position[0] - camera_x;
        dy = pl->position[1] - camera_y;
        dz = pl->position[2] - camera_z;
        key = sqrt (dx * dx + dy * dy + dz * dz) - (gdouble)pl->radius;
        if (self->max_distance > 0.0f && key > (gdouble)self->max_distance)
            continue;

        /* Insertion into the sorted best-N; ids arrive ascending, so a
         * strict comparison keeps the lower id first on ties. */
        if (self->n_active == self->max_active && key >= keys[self->n_active - 1])
            continue;
        slot = self->n_active < self->max_active ? self->n_active : self->n_active - 1;
        while (slot > 0 && keys[slot - 1] > key)
        {
            keys[slot] = keys[slot - 1];
            self->active[slot] = self->active[slot - 1];
            slot--;
        }
        keys[slot] = key;
        self->active[slot] = id;
        if (self->n_active < self->max_active)
            self->n_active++;
    }
    return self->n_active;
}

/* ------------------------------------------------------------------------ */
/* Light grid                                                               */
/* ------------------------------------------------------------------------ */

/* grid_alloc:
 * (Re)allocates the cell lists for the current cells, layers and slots. */
static void
grid_alloc (LrgLightSet *self)
{
    gsize n;

    g_clear_pointer (&self->grid_ids, g_free);
    g_clear_pointer (&self->grid_keys, g_free);
    g_clear_pointer (&self->grid_counts, g_free);
    self->grid_built = FALSE;
    if (self->grid_cells == 0)
        return;

    n = (gsize)self->grid_cells * self->grid_cells * self->grid_layers;
    self->grid_ids = g_new (guint, n * self->grid_slots);
    self->grid_keys = g_new (gfloat, n * self->grid_slots);
    self->grid_counts = g_new0 (guint8, n);
}

void
lrg_light_set_set_grid (LrgLightSet *self,
                        gfloat       cell_size,
                        guint        cells)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (cells == 0 || (cells >= 2 && cells <= LRG_LIGHT_SET_GRID_MAX_CELLS));
    g_return_if_fail (cells == 0 || (cell_size > 0.0f && isfinite (cell_size)));

    self->grid_cells = cells;
    self->grid_cell = cell_size;
    grid_alloc (self);
}

void
lrg_light_set_set_grid_layers (LrgLightSet *self,
                               gfloat       layer_height,
                               guint        layers)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (layers >= 1 && layers <= LRG_LIGHT_SET_GRID_MAX_LAYERS);
    g_return_if_fail (layer_height > 0.0f && isfinite (layer_height));

    if (self->grid_layers == layers && self->grid_layer_height == layer_height)
        return;
    self->grid_layers = layers;
    self->grid_layer_height = layer_height;
    grid_alloc (self);
}

void
lrg_light_set_set_grid_slots (LrgLightSet *self,
                              guint        slots)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (slots >= 4 && slots <= LRG_LIGHT_SET_GRID_MAX_SLOTS && slots % 4 == 0);

    if (self->grid_slots == slots)
        return;
    self->grid_slots = slots;
    grid_alloc (self);
}

guint
lrg_light_set_get_grid_slots (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);

    return self->grid_slots;
}

/* grid_insert:
 * Puts @id with @key into the sorted list of cell @c; a full list keeps
 * its best (smallest key) entries. Equal keys keep the earlier light. */
static void
grid_insert (LrgLightSet *self,
             gsize        c,
             guint        id,
             gfloat       key)
{
    guint   stride = self->grid_slots;
    guint  *ids = self->grid_ids + c * stride;
    gfloat *keys = self->grid_keys + c * stride;
    guint   count = self->grid_counts[c];
    guint   slot;

    if (count == stride && key >= keys[count - 1])
        return;
    slot = count < stride ? count : count - 1;
    while (slot > 0 && keys[slot - 1] > key)
    {
        keys[slot] = keys[slot - 1];
        ids[slot] = ids[slot - 1];
        slot--;
    }
    keys[slot] = key;
    ids[slot] = id;
    if (count < stride)
        self->grid_counts[c] = (guint8)(count + 1);
}

/* layer_range:
 * The world heights layer @l covers; the outer layers reach on without
 * end (and a one-layer grid covers every height). */
static void
layer_range (LrgLightSet *self,
             guint        l,
             gdouble     *lo,
             gdouble     *hi)
{
    gdouble h = (gdouble)self->grid_layer_height;

    *lo = l == 0 ? -G_MAXDOUBLE : self->grid_y0 + (gdouble)l * h;
    *hi = l + 1 >= self->grid_layers ? G_MAXDOUBLE : self->grid_y0 + (gdouble)(l + 1) * h;
}

/* build_grid:
 * Sorts every lit light into the cells (clusters) its radius reaches,
 * around the camera's cell and layer. A light's key in a cell is the
 * distance from the light to the cell's box over its radius: 0 inside,
 * 1 at the edge of its reach, so strong lights win over weak ones at
 * equal distance. */
static void
build_grid (LrgLightSet *self,
            gdouble      camera_x,
            gdouble      camera_y,
            gdouble      camera_z)
{
    guint   n = self->grid_cells;
    guint   layers = self->grid_layers;
    gdouble cell = (gdouble)self->grid_cell;
    gdouble extent = cell * (gdouble)n;
    gdouble lh = (gdouble)self->grid_layer_height;
    gdouble x0;
    gdouble y0;
    gdouble z0;
    guint   id;

    x0 = (floor (camera_x / cell) - (gdouble)(n / 2)) * cell;
    z0 = (floor (camera_z / cell) - (gdouble)(n / 2)) * cell;
    y0 = (floor (camera_y / lh) - (gdouble)(layers / 2)) * lh;

    /* The lists depend on the lights, the grid's shape (a change drops
     * grid_built), its corner and the distance limit, which measures from
     * the camera itself: when none of them moved since the last build,
     * the lists already hold exactly what a rebuild would write. */
    if (self->grid_built && self->key_lights_serial == self->lights_serial
        && self->key_x0 == x0 && self->key_y0 == y0 && self->key_z0 == z0
        && self->key_max_distance == self->max_distance
        && (self->max_distance <= 0.0f
            || (self->key_camera[0] == camera_x && self->key_camera[1] == camera_y
                && self->key_camera[2] == camera_z)))
        return;
    self->key_lights_serial = self->lights_serial;
    self->key_x0 = x0;
    self->key_y0 = y0;
    self->key_z0 = z0;
    self->key_max_distance = self->max_distance;
    self->key_camera[0] = camera_x;
    self->key_camera[1] = camera_y;
    self->key_camera[2] = camera_z;
    /* never 0, which means "nothing uploaded" */
    if (++self->grid_serial == 0)
        self->grid_serial = 1;

    memset (self->grid_counts, 0, (gsize)n * n * layers);
    self->grid_x0 = x0;
    self->grid_z0 = z0;
    self->grid_y0 = y0;

    for (id = 0; id < self->lights->len; id++)
    {
        PointLight *pl = &g_array_index (self->lights, PointLight, id);
        gdouble     r;
        gdouble     lx;
        gdouble     lz;
        gint        gx0;
        gint        gx1;
        gint        gz0;
        gint        gz1;
        gint        l0;
        gint        l1;
        gint        gx;
        gint        gz;
        gint        l;

        if (!pl->used || !pl->enabled || pl->intensity <= 0.0f)
            continue;
        r = (gdouble)pl->radius;
        if (self->max_distance > 0.0f)
        {
            gdouble dx = pl->position[0] - camera_x;
            gdouble dy = pl->position[1] - camera_y;
            gdouble dz = pl->position[2] - camera_z;

            if (sqrt (dx * dx + dy * dy + dz * dz) - r > (gdouble)self->max_distance)
                continue;
        }

        /* the cells the light's sphere overlaps, clipped to the grid */
        lx = pl->position[0] - self->grid_x0;
        lz = pl->position[2] - self->grid_z0;
        if (lx + r < 0.0 || lz + r < 0.0 || lx - r >= extent || lz - r >= extent)
            continue;
        gx0 = (gint)CLAMP (floor ((lx - r) / cell), 0.0, (gdouble)(n - 1));
        gx1 = (gint)CLAMP (floor ((lx + r) / cell), 0.0, (gdouble)(n - 1));
        gz0 = (gint)CLAMP (floor ((lz - r) / cell), 0.0, (gdouble)(n - 1));
        gz1 = (gint)CLAMP (floor ((lz + r) / cell), 0.0, (gdouble)(n - 1));
        l0 = (gint)CLAMP (floor ((pl->position[1] - r - self->grid_y0) / lh), 0.0,
                          (gdouble)(layers - 1));
        l1 = (gint)CLAMP (floor ((pl->position[1] + r - self->grid_y0) / lh), 0.0,
                          (gdouble)(layers - 1));
        if (layers == 1)
            l0 = l1 = 0;

        for (l = l0; l <= l1; l++)
        {
            gdouble lo;
            gdouble hi;
            gdouble dy;

            layer_range (self, (guint)l, &lo, &hi);
            dy = MAX (MAX (lo - pl->position[1], 0.0), pl->position[1] - hi);
            for (gz = gz0; gz <= gz1; gz++)
            {
                for (gx = gx0; gx <= gx1; gx++)
                {
                    gdouble ax = (gdouble)gx * cell;
                    gdouble az = (gdouble)gz * cell;
                    gdouble dx = MAX (MAX (ax - lx, 0.0), lx - (ax + cell));
                    gdouble dz = MAX (MAX (az - lz, 0.0), lz - (az + cell));
                    gdouble d = sqrt (dx * dx + dy * dy + dz * dz);

                    if (d >= r)
                        continue;
                    grid_insert (self, ((gsize)l * n + (gsize)gz) * n + (gsize)gx, id,
                                 (gfloat)(d / r));
                }
            }
        }
    }
    self->grid_built = TRUE;
}

/* grid_cell_at:
 * The cluster index of world (@x, @y, @z), or -1 outside the built grid. */
static gssize
grid_cell_at (LrgLightSet *self,
              gdouble      x,
              gdouble      y,
              gdouble      z)
{
    gdouble gx;
    gdouble gz;
    gdouble l = 0.0;

    if (self->grid_cells == 0 || !self->grid_built)
        return -1;
    gx = floor ((x - self->grid_x0) / (gdouble)self->grid_cell);
    gz = floor ((z - self->grid_z0) / (gdouble)self->grid_cell);
    if (!(gx >= 0.0 && gz >= 0.0 && gx < (gdouble)self->grid_cells
          && gz < (gdouble)self->grid_cells))
        return -1;
    if (self->grid_layers > 1)
        l = CLAMP (floor ((y - self->grid_y0) / (gdouble)self->grid_layer_height), 0.0,
                   (gdouble)(self->grid_layers - 1));
    return ((gssize)l * (gssize)self->grid_cells + (gssize)gz) * (gssize)self->grid_cells
           + (gssize)gx;
}

guint
lrg_light_set_get_grid_light_count (LrgLightSet *self,
                                    gdouble      x,
                                    gdouble      z)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);

    /* the camera's layer */
    return lrg_light_set_get_cluster_light_count (self, x, self->camera[1], z);
}

guint
lrg_light_set_get_grid_light (LrgLightSet *self,
                              gdouble      x,
                              gdouble      z,
                              guint        index)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_LIGHT_SET_INVALID_LIGHT);

    return lrg_light_set_get_cluster_light (self, x, self->camera[1], z, index);
}

guint
lrg_light_set_get_cluster_light_count (LrgLightSet *self,
                                       gdouble      x,
                                       gdouble      y,
                                       gdouble      z)
{
    gssize c;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);

    c = grid_cell_at (self, x, y, z);
    return c < 0 ? 0 : self->grid_counts[c];
}

guint
lrg_light_set_get_cluster_light (LrgLightSet *self,
                                 gdouble      x,
                                 gdouble      y,
                                 gdouble      z,
                                 guint        index)
{
    gssize c;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_LIGHT_SET_INVALID_LIGHT);

    c = grid_cell_at (self, x, y, z);
    if (c < 0 || index >= self->grid_counts[c])
        return LRG_LIGHT_SET_INVALID_LIGHT;
    return self->grid_ids[(gsize)c * self->grid_slots + index];
}

/* ------------------------------------------------------------------------ */
/* Light limit and emission                                                 */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_set_light_limit (LrgLightSet *self,
                               gfloat       limit)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (limit >= 0.0f);

    self->light_limit = isfinite (limit) ? limit : 0.0f;
}

gfloat
lrg_light_set_get_light_limit (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0.0f);
    return self->light_limit;
}

gfloat
lrg_light_set_limit_light (gfloat limit,
                           gfloat value)
{
    gfloat knee;

    /* Mirrors the shader: identity to the knee, then an exponential
     * roll-off with slope 1 at the knee and @limit as its asymptote. */
    if (limit <= 0.0f || value <= limit * LIMIT_KNEE)
        return value;
    knee = limit * LIMIT_KNEE;
    return knee + (limit - knee) * (1.0f - expf (-(value - knee) / (limit - knee)));
}

void
lrg_light_set_set_emission (LrgLightSet    *self,
                            const GrlColor *color,
                            gfloat          intensity)
{
    guint k;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (intensity >= 0.0f);

    if (color == NULL)
    {
        self->emission[0] = self->emission[1] = self->emission[2] = 0.0f;
        return;
    }
    color_to_rgb (color, self->emission);
    for (k = 0; k < 3; k++)
        self->emission[k] *= intensity;
}

guint
lrg_light_set_get_active_count (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);
    return self->n_active;
}

guint
lrg_light_set_get_active_light (LrgLightSet *self,
                                guint        index)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_LIGHT_SET_INVALID_LIGHT);

    if (index >= self->n_active)
        return LRG_LIGHT_SET_INVALID_LIGHT;
    return self->active[index];
}

void
lrg_light_set_set_alpha_cutoff (LrgLightSet *self,
                                gfloat       cutoff)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    self->cutoff = cutoff;
}

/* ------------------------------------------------------------------------ */
/* Shader                                                                   */
/* ------------------------------------------------------------------------ */

/* lit_locations:
 * Looks up the uniforms of a program built from the lit library and
 * points its samplers at their units (the material maps, the grid, the
 * shadow atlases), sets the material defaults (pbrParams, mapFlags,
 * heightScale) and every render layer.  raylib's material-map locations
 * of @handle are pointed at the library's samplers so DrawMesh() binds
 * the maps where the shader reads them. */
static void
lit_locations (Shader       *handle,
               LitLocations *L)
{
    gint   unit;
    gfloat scale = LRG_LIGHT_SET_HEIGHT_SCALE;
    gint   off = 0;
    gfloat pbr[4] = { LRG_LIGHT_SET_DEFAULT_METALLIC, LRG_LIGHT_SET_DEFAULT_ROUGHNESS, 1.0f, 1.0f };
    gint   layers[2] = { 0xffff, 0xffff };

    L->ambient = GetShaderLocation (*handle, "ambientColor");
    L->sun_dir = GetShaderLocation (*handle, "sunDirection");
    L->sun_color = GetShaderLocation (*handle, "sunColor");
    L->count = GetShaderLocation (*handle, "lightCount");
    L->positions = GetShaderLocation (*handle, "lightPosition");
    L->colors = GetShaderLocation (*handle, "lightColor");
    L->radii = GetShaderLocation (*handle, "lightRadius");
    L->cutoff = GetShaderLocation (*handle, "alphaCutoff");
    L->use_grid = GetShaderLocation (*handle, "useGrid");
    L->light_data = GetShaderLocation (*handle, "lightData");
    L->light_grid = GetShaderLocation (*handle, "lightGrid");
    L->grid_params = GetShaderLocation (*handle, "gridParams");
    L->limit = GetShaderLocation (*handle, "lightLimit");
    L->emission_color = GetShaderLocation (*handle, "emissionColor");
    L->grid_layers = GetShaderLocation (*handle, "gridLayers");
    L->light_shadow = GetShaderLocation (*handle, "lightShadow");
    L->air_params = GetShaderLocation (*handle, "airParams");
    L->air_tint = GetShaderLocation (*handle, "airTint");
    L->air_camera = GetShaderLocation (*handle, "airCamera");
    L->air_light_color = GetShaderLocation (*handle, "airLightColor");
    L->map_camera = GetShaderLocation (*handle, "mapCamera");
    L->spots = GetShaderLocation (*handle, "lightSpot");
    L->spot_extra = GetShaderLocation (*handle, "lightSpotExtra");
    L->draw_layers = GetShaderLocation (*handle, "drawLayers");
    L->output_linear = GetShaderLocation (*handle, "outputLinear");
    lrg_light_set_fog_locations (*handle, &L->fog);
    lrg_shadow_locations (*handle, &L->shadow);
    lrg_ground_overlay_locations (*handle, &L->overlay);

    /* The emission map is raylib's MATERIAL_MAP_EMISSION: DrawMesh()
     * binds a material's map to that unit and sets the sampler through
     * this location. Without a map the unit is empty and reads black;
     * the sampler must not default to unit 0 (the albedo). */
    unit = LRG_LIGHT_SET_EMISSION_UNIT;
    handle->locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation (*handle, "emissionMap");
    SetShaderValue (*handle, handle->locs[SHADER_LOC_MAP_EMISSION], &unit, SHADER_UNIFORM_INT);
    /* material slots (U9.1): normal, metallic-gloss and height maps
     * ride raylib's MATERIAL_MAP_NORMAL / METALNESS / HEIGHT units */
    unit = LRG_LIGHT_SET_NORMAL_UNIT;
    handle->locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation (*handle, "normalMap");
    SetShaderValue (*handle, handle->locs[SHADER_LOC_MAP_NORMAL], &unit, SHADER_UNIFORM_INT);
    unit = LRG_LIGHT_SET_METALLIC_UNIT;
    handle->locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation (*handle, "metallicMap");
    SetShaderValue (*handle, handle->locs[SHADER_LOC_MAP_METALNESS], &unit, SHADER_UNIFORM_INT);
    unit = LRG_LIGHT_SET_HEIGHT_UNIT;
    handle->locs[SHADER_LOC_MAP_HEIGHT] = GetShaderLocation (*handle, "heightMap");
    SetShaderValue (*handle, handle->locs[SHADER_LOC_MAP_HEIGHT], &unit, SHADER_UNIFORM_INT);
    /* 0.9.0: the ORM map rides raylib's MATERIAL_MAP_OCCLUSION unit */
    unit = LRG_LIGHT_SET_ORM_UNIT;
    handle->locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation (*handle, "ormMap");
    SetShaderValue (*handle, handle->locs[SHADER_LOC_MAP_OCCLUSION], &unit, SHADER_UNIFORM_INT);

    /* the material defaults a draw without a material gets */
    SetShaderValue (*handle, GetShaderLocation (*handle, "heightScale"), &scale, SHADER_UNIFORM_FLOAT);
    SetShaderValue (*handle, GetShaderLocation (*handle, "mapFlags"), &off, SHADER_UNIFORM_INT);
    SetShaderValue (*handle, GetShaderLocation (*handle, "pbrParams"), pbr, SHADER_UNIFORM_VEC4);
    SetShaderValue (*handle, L->draw_layers, layers, SHADER_UNIFORM_IVEC2);

    unit = GRID_DATA_UNIT;
    SetShaderValue (*handle, L->light_data, &unit, SHADER_UNIFORM_INT);
    unit = GRID_CELLS_UNIT;
    SetShaderValue (*handle, L->light_grid, &unit, SHADER_UNIFORM_INT);
}

GrlShader *
lrg_light_set_get_shader (LrgLightSet *self)
{
    g_autoptr(GError) error = NULL;
    Shader *handle;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), NULL);

    if (self->shader != NULL || self->shader_tried)
        return self->shader;
    if (!IsWindowReady ())
        return NULL;
    self->shader_tried = TRUE;

    /* On a compile failure raylib hands back its default shader. */
    {
        g_autofree gchar *fs = light_fs_with_lod (light_fs);

        self->shader = grl_shader_new_from_memory (light_vs, fs, &error);
    }
    if (self->shader == NULL)
        return NULL;
    handle = grl_shader_get_handle (self->shader);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
    {
        g_clear_object (&self->shader);
        return NULL;
    }

    lit_locations (handle, &self->locs);
    return self->shader;
}

/* upload_texture:
 * Creates or refreshes a float RGBA texture (point sampled) of @width x
 * @height texels; a size change recreates it. */
static void
upload_texture (guint        *id,
                gint         *have_w,
                gint         *have_h,
                gint          width,
                gint          height,
                const gfloat *texels)
{
    if (*id != 0 && (*have_w != width || *have_h != height))
    {
        rlUnloadTexture (*id);
        *id = 0;
    }
    if (*id == 0)
    {
        *id = rlLoadTexture (texels, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
        rlTextureParameters (*id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
        rlTextureParameters (*id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
        *have_w = width;
        *have_h = height;
    }
    else
    {
        rlUpdateTexture (*id, 0, 0, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, texels);
    }
}

/* grid_layer_uniform:
 * gridLayers of LRG_LIGHT_CLUSTER_GLSL: layer 0's base in draw space,
 * the layer height, the layers and the slots per cell. */
static void
grid_layer_uniform (LrgLightSet *self,
                    gfloat      *out)
{
    out[0] = (gfloat)(self->grid_y0 - self->origin[1]);
    out[1] = self->grid_layer_height;
    out[2] = (gfloat)self->grid_layers;
    out[3] = (gfloat)self->grid_slots;
}

/* spot_texels:
 * The third and fourth texel of a light (and its lightSpot /
 * lightSpotExtra uniforms): (spot direction, cos outer) with cos outer
 * -2 for a point light, (cos inner, spot shadow slot or -1, layers low
 * 16 bits, layers high 16 bits). */
static void
spot_texels (LrgLightSet *self,
             PointLight  *pl,
             guint        id,
             gboolean     shadows_ready,
             gfloat      *spot,
             gfloat      *extra)
{
    if (pl->spot)
    {
        spot[0] = pl->direction[0];
        spot[1] = pl->direction[1];
        spot[2] = pl->direction[2];
        spot[3] = cosf (pl->outer * (gfloat)G_PI / 180.0f);
        extra[0] = cosf (pl->inner * (gfloat)G_PI / 180.0f);
        extra[1] = shadows_ready
                   ? (gfloat)lrg_shadow_renderer_get_spot_slot (self->shadows, id)
                   : -1.0f;
    }
    else
    {
        spot[0] = spot[1] = spot[2] = 0.0f;
        spot[3] = -2.0f;
        extra[0] = 1.0f;
        extra[1] = -1.0f;
    }
    extra[2] = (gfloat)(pl->layers & 0xffffu);
    extra[3] = (gfloat)(pl->layers >> 16);
}

/* upload_grid:
 * The lights of the built grid (two texels each, in first-use order)
 * and the cell lists (four indices per texel, -1 terminated), bound to
 * GRID_DATA_UNIT / GRID_CELLS_UNIT, plus the grid uniforms.
 *
 * The cell texture and the order of the lights only change with a
 * rebuild (grid_serial), so they are redone and sent only then. The
 * light texture also follows the origin and the shadow slots, which
 * move without a rebuild: it is refilled from the kept order each time
 * (a few texels per light) and sent only when it differs from what the
 * GPU holds. The buffers persist between applies. */
static void
upload_grid (LrgLightSet        *self,
             Shader             *handle,
             const LitLocations *L)
{
    guint   n = self->grid_cells;
    gsize   c;
    gint    grid_w;
    gint    unused_w;
    gint    use_grid = 1;
    gfloat  params[4];
    gfloat  layers[4];
    guint   stride;
    gsize   n_cells;
    guint   i;
    gboolean shadows_ready;

    /* raylib leaves the last material slot active after DrawMesh();
     * uploads must not bind into a material unit */
    rlActiveTextureSlot (0);
    stride = self->grid_slots;
    n_cells = (gsize)n * n * self->grid_layers;
    grid_w = (gint)(n * (stride / 4));
    if (self->up_data == NULL)
    {
        self->up_order = g_new (guint, LRG_LIGHT_SET_GRID_MAX_LIGHTS);
        self->up_data = g_new0 (gfloat, (gsize)LRG_LIGHT_SET_GRID_MAX_LIGHTS * TEXEL_FLOATS);
        self->up_fresh = g_new0 (gfloat, (gsize)LRG_LIGHT_SET_GRID_MAX_LIGHTS * TEXEL_FLOATS);
    }

    /* the cell lists and the lights' order, after a rebuild (or when the
     * cell texture is gone or the wrong size) */
    if (self->up_grid_serial != self->grid_serial || self->tex_grid == 0
        || self->tex_grid_w != grid_w || self->tex_grid_h != (gint)(n * self->grid_layers))
    {
        gsize n_index = MAX (self->lights->len, 1);

        if (self->up_cells_len < n_cells * stride)
        {
            g_free (self->up_cells);
            self->up_cells = g_new (gfloat, n_cells * stride);
            self->up_cells_len = n_cells * stride;
        }
        if (self->up_index_len < n_index)
        {
            g_free (self->up_index_of);
            self->up_index_of = g_new (guint, n_index);
            self->up_index_len = n_index;
        }
        memset (self->up_index_of, 0xff, sizeof (guint) * n_index);
        self->up_n_data = 0;
        for (c = 0; c < n_cells; c++)
        {
            guint written = 0;
            guint s;

            for (s = 0; s < self->grid_counts[c]; s++)
            {
                guint id = self->grid_ids[c * stride + s];

                if (self->up_index_of[id] == G_MAXUINT)
                {
                    if (self->up_n_data == LRG_LIGHT_SET_GRID_MAX_LIGHTS)
                        continue;
                    self->up_index_of[id] = self->up_n_data;
                    self->up_order[self->up_n_data++] = id;
                }
                self->up_cells[c * stride + written++] = (gfloat)self->up_index_of[id];
            }
            for (; written < stride; written++)
                self->up_cells[c * stride + written] = -1.0f;
        }

        /* the cell texture is (n * slots / 4) x (n * layers): cluster (x, z,
         * layer) at texels x * slots / 4 onwards of row layer * n + z */
        {
            gint have_w = self->tex_grid_w;
            gint have_h = self->tex_grid_h;

            upload_texture (&self->tex_grid, &have_w, &have_h, grid_w,
                            (gint)(n * self->grid_layers), self->up_cells);
            self->tex_grid_w = grid_w;
            self->tex_grid_h = (gint)(n * self->grid_layers);
        }
        self->up_grid_serial = self->grid_serial;
    }

    /* the light texture, 4 x lights: position and radius, colour and
     * point shadow slot, spot direction and cos outer, cos inner, spot
     * shadow slot and layers (see LRG_LIGHT_SAMPLE_GLSL) */
    shadows_ready = self->shadows != NULL && lrg_shadow_renderer_is_ready (self->shadows);
    for (i = 0; i < self->up_n_data; i++)
    {
        guint       id = self->up_order[i];
        PointLight *pl = &g_array_index (self->lights, PointLight, id);
        gfloat     *t = self->up_fresh + (gsize)i * TEXEL_FLOATS;
        guint       k;

        for (k = 0; k < 3; k++)
        {
            t[k] = (gfloat)(pl->position[k] - self->origin[k]);
            t[4 + k] = pl->color[k] * pl->intensity;
        }
        t[3] = pl->radius;
        /* M14: the light's shadow slot, -1 for none */
        t[7] = shadows_ready
               ? (gfloat)lrg_shadow_renderer_get_light_slot (self->shadows, id)
               : -1.0f;
        spot_texels (self, pl, id, shadows_ready, t + 8, t + 12);
    }
    if (self->up_n_data == 0)
        memset (self->up_fresh, 0, TEXEL_FLOATS * sizeof (gfloat));
    if (self->tex_data == 0 || self->tex_data_height != (gint)MAX (self->up_n_data, 1u)
        || memcmp (self->up_fresh, self->up_data,
                   (gsize)MAX (self->up_n_data, 1u) * TEXEL_FLOATS * sizeof (gfloat)) != 0)
    {
        unused_w = LRG_LIGHT_SET_GRID_TEXELS;
        upload_texture (&self->tex_data, &unused_w, &self->tex_data_height,
                        LRG_LIGHT_SET_GRID_TEXELS, (gint)MAX (self->up_n_data, 1u), self->up_fresh);
        memcpy (self->up_data, self->up_fresh,
                (gsize)MAX (self->up_n_data, 1u) * TEXEL_FLOATS * sizeof (gfloat));
    }

    rlActiveTextureSlot (GRID_DATA_UNIT);
    rlEnableTexture (self->tex_data);
    rlActiveTextureSlot (GRID_CELLS_UNIT);
    rlEnableTexture (self->tex_grid);
    rlActiveTextureSlot (0);

    params[0] = (gfloat)(self->grid_x0 - self->origin[0]);
    params[1] = (gfloat)(self->grid_z0 - self->origin[2]);
    params[2] = self->grid_cell;
    params[3] = (gfloat)n;
    grid_layer_uniform (self, layers);
    SetShaderValue (*handle, L->use_grid, &use_grid, SHADER_UNIFORM_INT);
    SetShaderValue (*handle, L->grid_params, params, SHADER_UNIFORM_VEC4);
    SetShaderValue (*handle, L->grid_layers, layers, SHADER_UNIFORM_VEC4);
}

/* upload_air:
 * The air uniforms of LRG_LIGHT_AIR_GLSL: density 0 turns it off. */
static void
upload_air (LrgLightSet        *self,
            Shader             *handle,
            const LitLocations *L)
{
    gfloat params[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    gfloat tint[4];
    gfloat camera[3];
    gfloat colors[LRG_SHADOW_MAX_POINT_LIGHTS * 3];
    guint  n_points = 0;
    guint  i;
    guint  k;

    if (self->air_on)
    {
        params[0] = self->air_density;
        params[1] = self->air_g;
        params[2] = self->air_intensity;
        params[3] = (gfloat)self->air_steps;
    }
    for (k = 0; k < 3; k++)
    {
        tint[k] = self->air_tint[k];
        camera[k] = (gfloat)(self->camera[k] - self->origin[k]);
    }
    tint[3] = self->air_distance;
    SetShaderValue (*handle, L->air_params, params, SHADER_UNIFORM_VEC4);
    if (!self->air_on)
        return;
    SetShaderValue (*handle, L->air_tint, tint, SHADER_UNIFORM_VEC4);
    SetShaderValue (*handle, L->air_camera, camera, SHADER_UNIFORM_VEC3);

    /* the colours of the shadowed point lights the air scatters */
    if (self->shadows != NULL && lrg_shadow_renderer_is_ready (self->shadows))
        n_points = lrg_shadow_renderer_get_point_count (self->shadows);
    for (i = 0; i < n_points; i++)
    {
        PointLight *pl = light_at (self, lrg_shadow_renderer_get_point_light (self->shadows, i));

        for (k = 0; k < 3; k++)
            colors[i * 3 + k] = pl != NULL ? pl->color[k] * pl->intensity : 0.0f;
    }
    if (n_points > 0)
        SetShaderValueV (*handle, L->air_light_color, colors, SHADER_UNIFORM_VEC3,
                         (gint)n_points);
}

/* ------------------------------------------------------------------------ */
/* 0.9.0: the hooks of the other passes                                     */
/* ------------------------------------------------------------------------ */

/* the first hook unit and the end of the hook window, -1 until queried
 * with a context (see lrg_light_set_hook_unit_usable()) */
static gint hook_first = -1;
static gint hook_end = -1;

typedef void (*GetIntegervFn) (guint pname, gint *data);

/* GL_MAX_TEXTURE_IMAGE_UNITS */
#define GL_MAX_TEXTURE_IMAGE_UNITS_ENUM (0x8872)

/* hook_window:
 * Queries the driver's fragment texture units once: more than 16 gives
 * the hooks LRG_LIGHT_SET_HOOK_UNIT .. MIN (units, 32); the 16 GL 3.3
 * guarantees give them raylib's unused material slots 7 .. 10. */
static void
hook_window (void)
{
    GetIntegervFn get_integerv;
    gint          units = 16;

    if (hook_first >= 0 || !IsWindowReady ())
        return;
    get_integerv = (GetIntegervFn)rlGetProcAddress ("glGetIntegerv");
    if (get_integerv != NULL)
        get_integerv (GL_MAX_TEXTURE_IMAGE_UNITS_ENUM, &units);
    if (units > LRG_LIGHT_SET_HOOK_UNIT)
    {
        hook_first = LRG_LIGHT_SET_HOOK_UNIT;
        hook_end = MIN (units, 32);
    }
    else
    {
        hook_first = 7;
        hook_end = LRG_SHADOW_SPOT_UNIT;
    }
}

gboolean
lrg_light_set_hook_unit_usable (gint unit)
{
    hook_window ();
    return hook_first >= 0 && unit >= hook_first && unit < hook_end;
}

gint
lrg_light_set_hook_unit_first (void)
{
    hook_window ();
    return hook_first >= 0 ? hook_first : LRG_LIGHT_SET_HOOK_UNIT;
}

/* bind_hooks:
 * Lets the environment, SSAO and decal binders set their uniforms and
 * textures (or their inactive state for a NULL object) on the lit
 * program, handing out texture units from the hook window upward. */
static void
bind_hooks (LrgLightSet *self,
            guint        shader_id)
{
    gint unit = lrg_light_set_hook_unit_first ();

    rlEnableShader (shader_id);
    lrg_environment_bind_private (self->environment, shader_id, &unit);
    lrg_ssao_bind_private (self->ssao, shader_id, &unit);
    lrg_decal_set_bind_private (self->decals, shader_id, &unit);
    rlActiveTextureSlot (0);
}

/* upload_program:
 * Uploads the lights of the last selection (and the grid, shadows,
 * air, overlay, fog, layers, output encoding) to @handle, a program
 * built from the lit library whose locations are @L, and lets the hook
 * binders bind their state.  The eye is the camera of the last
 * selection. */
static void
upload_program (LrgLightSet        *self,
                Shader             *handle,
                const LitLocations *L)
{
    gfloat  positions[LRG_LIGHT_SET_MAX_ACTIVE * 3];
    gfloat  colors[LRG_LIGHT_SET_MAX_ACTIVE * 3];
    gfloat  radii[LRG_LIGHT_SET_MAX_ACTIVE];
    gfloat  shadow_slots[LRG_LIGHT_SET_MAX_ACTIVE];
    gfloat  spots[LRG_LIGHT_SET_MAX_ACTIVE * 4];
    gfloat  spot_extra[LRG_LIGHT_SET_MAX_ACTIVE * 4];
    gfloat  sun_dir[3] = { 0.0f, -1.0f, 0.0f };
    gfloat  sun_color[3] = { 0.0f, 0.0f, 0.0f };
    gboolean shadows_ready;
    gint    count;
    guint   i;

    shadows_ready = self->shadows != NULL && lrg_shadow_renderer_is_ready (self->shadows);

    /* Positions relative to the floating origin, colours pre-scaled. */
    for (i = 0; i < self->n_active; i++)
    {
        PointLight *pl = &g_array_index (self->lights, PointLight, self->active[i]);
        guint       k;

        for (k = 0; k < 3; k++)
        {
            positions[i * 3 + k] = (gfloat)(pl->position[k] - self->origin[k]);
            colors[i * 3 + k] = pl->color[k] * pl->intensity;
        }
        radii[i] = pl->radius;
        shadow_slots[i] = shadows_ready
                          ? (gfloat)lrg_shadow_renderer_get_light_slot (self->shadows,
                                                                        self->active[i])
                          : -1.0f;
        spot_texels (self, pl, self->active[i], shadows_ready, spots + i * 4, spot_extra + i * 4);
    }
    if (self->has_directional)
    {
        memcpy (sun_dir, self->directional_dir, sizeof sun_dir);
        memcpy (sun_color, self->directional_color, sizeof sun_color);
    }
    count = (gint)self->n_active;

    SetShaderValue (*handle, L->ambient, self->ambient, SHADER_UNIFORM_VEC3);
    SetShaderValue (*handle, L->sun_dir, sun_dir, SHADER_UNIFORM_VEC3);
    SetShaderValue (*handle, L->sun_color, sun_color, SHADER_UNIFORM_VEC3);
    SetShaderValue (*handle, L->count, &count, SHADER_UNIFORM_INT);
    SetShaderValue (*handle, L->cutoff, &self->cutoff, SHADER_UNIFORM_FLOAT);
    if (count > 0)
    {
        SetShaderValueV (*handle, L->positions, positions, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV (*handle, L->colors, colors, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV (*handle, L->radii, radii, SHADER_UNIFORM_FLOAT, count);
        SetShaderValueV (*handle, L->light_shadow, shadow_slots, SHADER_UNIFORM_FLOAT, count);
        SetShaderValueV (*handle, L->spots, spots, SHADER_UNIFORM_VEC4, count);
        SetShaderValueV (*handle, L->spot_extra, spot_extra, SHADER_UNIFORM_VEC4, count);
    }
    SetShaderValue (*handle, L->limit, &self->light_limit, SHADER_UNIFORM_FLOAT);
    SetShaderValue (*handle, L->emission_color, self->emission, SHADER_UNIFORM_VEC3);
    {
        /* the camera, origin-relative, for the material slots' view vector */
        gfloat cam[3];

        cam[0] = (gfloat)(self->camera[0] - self->origin[0]);
        cam[1] = (gfloat)(self->camera[1] - self->origin[1]);
        cam[2] = (gfloat)(self->camera[2] - self->origin[2]);
        SetShaderValue (*handle, L->map_camera, cam, SHADER_UNIFORM_VEC3);
    }
    /* M14: the shadow atlases and the air */
    lrg_shadow_renderer_upload (shadows_ready ? self->shadows : NULL, *handle,
                                &L->shadow);
    upload_air (self, handle, L);
    /* snow and cloud shadows: placed by the overlay's own origin of the
     * draw space (lrg_ground_overlay_set_origin()) */
    lrg_ground_overlay_upload (self->overlay, *handle, &L->overlay, NULL);
    if (self->grid_cells > 0 && self->grid_built)
    {
        upload_grid (self, handle, L);
    }
    else
    {
        gint use_grid = 0;

        SetShaderValue (*handle, L->use_grid, &use_grid, SHADER_UNIFORM_INT);
    }
    lrg_light_set_upload_fog (self, *handle, &L->fog);
    {
        gint layers[2];
        gint linear = self->output_linear ? 1 : 0;

        layers[0] = (gint)(self->draw_layers & 0xffffu);
        layers[1] = (gint)(self->draw_layers >> 16);
        SetShaderValue (*handle, L->draw_layers, layers, SHADER_UNIFORM_IVEC2);
        SetShaderValue (*handle, L->output_linear, &linear, SHADER_UNIFORM_INT);
    }
    bind_hooks (self, handle->id);
}

gboolean
lrg_light_set_apply (LrgLightSet *self,
                     gdouble      camera_x,
                     gdouble      camera_y,
                     gdouble      camera_z)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);

    lrg_light_set_select (self, camera_x, camera_y, camera_z);
    if (lrg_light_set_get_shader (self) == NULL)
        return FALSE;
    upload_program (self, grl_shader_get_handle (self->shader), &self->locs);
    return TRUE;
}

gboolean
lrg_light_set_bind_shader (LrgLightSet *self,
                           GrlShader   *shader)
{
    LitLocations *L;
    Shader       *handle;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);
    g_return_val_if_fail (GRL_IS_SHADER (shader), FALSE);

    if (!IsWindowReady ())
        return FALSE;
    handle = grl_shader_get_handle (shader);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
        return FALSE;
    if (self->shader != NULL && ((Shader *)grl_shader_get_handle (self->shader))->id == handle->id)
    {
        upload_program (self, handle, &self->locs);
        return TRUE;
    }

    /* a user program: its locations once per program id (and its
     * samplers pointed at their units then) */
    if (self->foreign == NULL)
        self->foreign = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
    L = g_hash_table_lookup (self->foreign, GUINT_TO_POINTER (handle->id));
    if (L == NULL)
    {
        L = g_new0 (LitLocations, 1);
        lit_locations (handle, L);
        g_hash_table_insert (self->foreign, GUINT_TO_POINTER (handle->id), L);
    }
    upload_program (self, handle, L);
    return TRUE;
}

void
lrg_light_set_forget_shader (LrgLightSet *self,
                             GrlShader   *shader)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (GRL_IS_SHADER (shader));

    if (self->foreign != NULL)
        g_hash_table_remove (self->foreign,
                             GUINT_TO_POINTER (((Shader *)grl_shader_get_handle (shader))->id));
}

/* ------------------------------------------------------------------------ */
/* Fog                                                                      */
/* ------------------------------------------------------------------------ */

gfloat
lrg_fog_amount (LrgFogType type,
                gfloat     density,
                gfloat     start,
                gfloat     end,
                gfloat     base_height,
                gfloat     falloff,
                gfloat     distance,
                gfloat     height)
{
    gfloat f;

    /* Mirrors lrg_fog_amount() in LRG_FOG_GLSL line by line. */
    if (!isfinite (distance) || distance < 0.0f)
        distance = 0.0f;
    switch (type)
    {
    case LRG_FOG_TYPE_UNIFORM:
        f = density;
        break;
    case LRG_FOG_TYPE_LINEAR:
        if (end > start)
            f = (distance - start) / (end - start);
        else
            f = distance >= end ? 1.0f : 0.0f;
        break;
    case LRG_FOG_TYPE_HEIGHT:
        f = (1.0f - expf (-density * distance))
            * expf (-falloff * MAX (height - base_height, 0.0f));
        break;
    case LRG_FOG_TYPE_EXPONENTIAL:
    default:
        f = 1.0f - expf (-density * distance);
        break;
    }
    if (!isfinite (f))
        return 0.0f;
    return CLAMP (f, 0.0f, 1.0f);
}

void
lrg_light_set_fog_locations (Shader                   shader,
                             LrgLightSetFogLocations *out_locs)
{
    out_locs->type = GetShaderLocation (shader, "fogType");
    out_locs->color = GetShaderLocation (shader, "fogColor");
    out_locs->params = GetShaderLocation (shader, "fogParams");
    out_locs->base = GetShaderLocation (shader, "fogBase");
    out_locs->camera = GetShaderLocation (shader, "fogCamera");
}

void
lrg_light_set_upload_fog (LrgLightSet                   *self,
                          Shader                         shader,
                          const LrgLightSetFogLocations *locs)
{
    gint   type = -1;
    gfloat params[4];
    gfloat camera[3];
    gfloat base;
    guint  k;

    if (self == NULL || !self->fog_on)
    {
        SetShaderValue (shader, locs->type, &type, SHADER_UNIFORM_INT);
        return;
    }

    /* Everything in draw space: the eye and the height base move with
     * the floating origin, like the point lights. */
    type = (gint)self->fog_type;
    params[0] = self->fog_density;
    params[1] = self->fog_start;
    params[2] = self->fog_end;
    params[3] = self->fog_falloff;
    for (k = 0; k < 3; k++)
        camera[k] = (gfloat)(self->camera[k] - self->origin[k]);
    base = (gfloat)((gdouble)self->fog_base - self->origin[1]);

    SetShaderValue (shader, locs->type, &type, SHADER_UNIFORM_INT);
    SetShaderValue (shader, locs->color, self->fog_color, SHADER_UNIFORM_VEC4);
    SetShaderValue (shader, locs->params, params, SHADER_UNIFORM_VEC4);
    SetShaderValue (shader, locs->base, &base, SHADER_UNIFORM_FLOAT);
    SetShaderValue (shader, locs->camera, camera, SHADER_UNIFORM_VEC3);
}

void
lrg_light_set_set_fog (LrgLightSet    *self,
                       LrgFogType      type,
                       const GrlColor *color,
                       gfloat          density,
                       gfloat          start,
                       gfloat          end)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (color != NULL);

    /* Non-finite numbers would poison every fragment; treat them as 0. */
    self->fog_on = TRUE;
    self->fog_type = type;
    color_to_rgb (color, self->fog_color);
    self->fog_color[3] = (gfloat)color->a / 255.0f;
    self->fog_density = isfinite (density) ? MAX (density, 0.0f) : 0.0f;
    self->fog_start = isfinite (start) ? start : 0.0f;
    self->fog_end = isfinite (end) ? end : 0.0f;
}

void
lrg_light_set_set_fog_height (LrgLightSet *self,
                              gfloat       base_height,
                              gfloat       falloff)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->fog_base = isfinite (base_height) ? base_height : 0.0f;
    self->fog_falloff = isfinite (falloff) ? MAX (falloff, 0.0f) : 0.0f;
}

void
lrg_light_set_clear_fog (LrgLightSet *self)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->fog_on = FALSE;
}

gboolean
lrg_light_set_get_fog (LrgLightSet *self,
                       LrgFogType  *out_type,
                       GrlColor    *out_color,
                       gfloat      *out_density,
                       gfloat      *out_start,
                       gfloat      *out_end)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);

    if (!self->fog_on)
        return FALSE;
    if (out_type != NULL)
        *out_type = self->fog_type;
    if (out_color != NULL)
    {
        out_color->r = (guint8)(self->fog_color[0] * 255.0f + 0.5f);
        out_color->g = (guint8)(self->fog_color[1] * 255.0f + 0.5f);
        out_color->b = (guint8)(self->fog_color[2] * 255.0f + 0.5f);
        out_color->a = (guint8)(self->fog_color[3] * 255.0f + 0.5f);
    }
    if (out_density != NULL)
        *out_density = self->fog_density;
    if (out_start != NULL)
        *out_start = self->fog_start;
    if (out_end != NULL)
        *out_end = self->fog_end;
    return TRUE;
}

gfloat
lrg_light_set_sample_fog (LrgLightSet      *self,
                          const GrlVector3 *position)
{
    gdouble d[3];
    gfloat  distance;
    gfloat  height;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0.0f);
    g_return_val_if_fail (position != NULL, 0.0f);

    if (!self->fog_on)
        return 0.0f;

    /* The shader's draw space: eye and base minus the origin. */
    d[0] = (gdouble)position->x - (self->camera[0] - self->origin[0]);
    d[1] = (gdouble)position->y - (self->camera[1] - self->origin[1]);
    d[2] = (gdouble)position->z - (self->camera[2] - self->origin[2]);
    distance = (gfloat)sqrt (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    height = (gfloat)((gdouble)position->y + self->origin[1]);

    return lrg_fog_amount (self->fog_type, self->fog_density, self->fog_start,
                           self->fog_end, self->fog_base, self->fog_falloff,
                           distance, height) * self->fog_color[3];
}

/* ------------------------------------------------------------------------ */
/* Light sampling on the GPU (LRG_LIGHT_SAMPLE_GLSL)                        */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_sample_locations (Shader                      shader,
                                LrgLightSetSampleLocations *out_locs)
{
    gint unit;

    out_locs->ambient = GetShaderLocation (shader, "ambientColor");
    out_locs->sun_color = GetShaderLocation (shader, "sunColor");
    out_locs->count = GetShaderLocation (shader, "lightCount");
    out_locs->positions = GetShaderLocation (shader, "lightPosition");
    out_locs->colors = GetShaderLocation (shader, "lightColor");
    out_locs->radii = GetShaderLocation (shader, "lightRadius");
    out_locs->spots = GetShaderLocation (shader, "lightSpot");
    out_locs->spot_extra = GetShaderLocation (shader, "lightSpotExtra");
    out_locs->use_grid = GetShaderLocation (shader, "useGrid");
    out_locs->light_data = GetShaderLocation (shader, "lightData");
    out_locs->light_grid = GetShaderLocation (shader, "lightGrid");
    out_locs->grid_params = GetShaderLocation (shader, "gridParams");
    out_locs->grid_layers = GetShaderLocation (shader, "gridLayers");
    out_locs->limit = GetShaderLocation (shader, "lightLimit");

    /* the samplers never move: point them at the grid units once */
    unit = GRID_DATA_UNIT;
    SetShaderValue (shader, out_locs->light_data, &unit, SHADER_UNIFORM_INT);
    unit = GRID_CELLS_UNIT;
    SetShaderValue (shader, out_locs->light_grid, &unit, SHADER_UNIFORM_INT);
}

gboolean
lrg_light_set_upload_receive (LrgLightSet                     *self,
                              Shader                           shader,
                              const LrgShadowLocations        *shadow_locs,
                              const LrgGroundOverlayLocations *overlay_locs,
                              gint                             sun_dir_loc)
{
    gfloat   sun_dir[3] = { 0.0f, -1.0f, 0.0f };
    gboolean ready;

    if (self == NULL)
        return FALSE;
    ready = self->shadows != NULL && lrg_shadow_renderer_is_ready (self->shadows);
    if (self->has_directional)
        memcpy (sun_dir, self->directional_dir, sizeof sun_dir);
    SetShaderValue (shader, sun_dir_loc, sun_dir, SHADER_UNIFORM_VEC3);
    lrg_shadow_renderer_upload (ready ? self->shadows : NULL, shader, shadow_locs);
    lrg_ground_overlay_upload (self->overlay, shader, overlay_locs, NULL);
    return ready || (self->overlay != NULL && lrg_ground_overlay_is_visible (self->overlay));
}

gboolean
lrg_light_set_upload_sample (LrgLightSet                      *self,
                             Shader                            shader,
                             const LrgLightSetSampleLocations *locs)
{
    gfloat  positions[LRG_LIGHT_SET_MAX_ACTIVE * 3];
    gfloat  colors[LRG_LIGHT_SET_MAX_ACTIVE * 3];
    gfloat  radii[LRG_LIGHT_SET_MAX_ACTIVE];
    gfloat  spots[LRG_LIGHT_SET_MAX_ACTIVE * 4];
    gfloat  spot_extra[LRG_LIGHT_SET_MAX_ACTIVE * 4];
    gfloat  sun_color[3] = { 0.0f, 0.0f, 0.0f };
    gint    count;
    gint    use_grid;
    guint   i;

    if (self == NULL)
        return FALSE;

    /* the same numbers apply() gives the mesh shader, so a quad gets
     * what lrg_light_set_sample() answers for its centre */
    for (i = 0; i < self->n_active; i++)
    {
        PointLight *pl = &g_array_index (self->lights, PointLight, self->active[i]);
        guint       k;

        for (k = 0; k < 3; k++)
        {
            positions[i * 3 + k] = (gfloat)(pl->position[k] - self->origin[k]);
            colors[i * 3 + k] = pl->color[k] * pl->intensity;
        }
        radii[i] = pl->radius;
        spot_texels (self, pl, self->active[i], FALSE, spots + i * 4, spot_extra + i * 4);
    }
    if (self->has_directional)
        memcpy (sun_color, self->directional_color, sizeof sun_color);
    count = (gint)self->n_active;

    SetShaderValue (shader, locs->ambient, self->ambient, SHADER_UNIFORM_VEC3);
    SetShaderValue (shader, locs->sun_color, sun_color, SHADER_UNIFORM_VEC3);
    SetShaderValue (shader, locs->count, &count, SHADER_UNIFORM_INT);
    if (count > 0)
    {
        SetShaderValueV (shader, locs->positions, positions, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV (shader, locs->colors, colors, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV (shader, locs->radii, radii, SHADER_UNIFORM_FLOAT, count);
        SetShaderValueV (shader, locs->spots, spots, SHADER_UNIFORM_VEC4, count);
        SetShaderValueV (shader, locs->spot_extra, spot_extra, SHADER_UNIFORM_VEC4, count);
    }
    SetShaderValue (shader, locs->limit, &self->light_limit, SHADER_UNIFORM_FLOAT);

    /* the grid as apply() uploaded it; a set whose grid was built but
     * never applied has no textures and lights from its active list */
    use_grid = self->grid_cells > 0 && self->grid_built && self->tex_grid != 0
               && self->tex_data != 0;
    SetShaderValue (shader, locs->use_grid, &use_grid, SHADER_UNIFORM_INT);
    if (use_grid)
    {
        gfloat params[4];
        gfloat layers[4];

        params[0] = (gfloat)(self->grid_x0 - self->origin[0]);
        params[1] = (gfloat)(self->grid_z0 - self->origin[2]);
        params[2] = self->grid_cell;
        params[3] = (gfloat)self->grid_cells;
        SetShaderValue (shader, locs->grid_params, params, SHADER_UNIFORM_VEC4);
        grid_layer_uniform (self, layers);
        SetShaderValue (shader, locs->grid_layers, layers, SHADER_UNIFORM_VEC4);
        rlActiveTextureSlot (GRID_DATA_UNIT);
        rlEnableTexture (self->tex_data);
        rlActiveTextureSlot (GRID_CELLS_UNIT);
        rlEnableTexture (self->tex_grid);
        rlActiveTextureSlot (0);
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* CPU evaluation and binding                                               */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_sample (LrgLightSet      *self,
                      const GrlVector3 *position,
                      const GrlVector3 *normal,
                      gfloat           *out_rgb)
{
    gfloat n[3] = { 0.0f, 0.0f, 0.0f };
    gfloat p[3];
    gboolean has_normal = FALSE;
    gssize cell;
    guint count;
    guint i;
    guint k;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (position != NULL);
    g_return_if_fail (out_rgb != NULL);

    /* The normal is normalized as the fragment shader does; a zero one
     * means "faces every light". */
    if (normal != NULL)
    {
        gfloat len = sqrtf (normal->x * normal->x + normal->y * normal->y
                            + normal->z * normal->z);

        if (len > 0.0f && isfinite (len))
        {
            n[0] = normal->x / len;
            n[1] = normal->y / len;
            n[2] = normal->z / len;
            has_normal = TRUE;
        }
    }

    p[0] = position->x;
    p[1] = position->y;
    p[2] = position->z;
    for (k = 0; k < 3; k++)
        out_rgb[k] = self->ambient[k];

    if (self->has_directional)
    {
        gfloat lambert = 1.0f;

        if (has_normal)
            lambert = MAX (-(n[0] * self->directional_dir[0]
                             + n[1] * self->directional_dir[1]
                             + n[2] * self->directional_dir[2]), 0.0f);
        for (k = 0; k < 3; k++)
            out_rgb[k] += self->directional_color[k] * lambert;
    }

    /* Point lights: the grid cell of @position when there is a grid,
     * else the last selection; origin-relative like the uniforms apply()
     * uploads. */
    cell = grid_cell_at (self, (gdouble)p[0] + self->origin[0], (gdouble)p[1] + self->origin[1],
                         (gdouble)p[2] + self->origin[2]);
    if (self->grid_cells > 0 && self->grid_built)
        count = cell < 0 ? 0 : self->grid_counts[cell];
    else
        count = self->n_active;
    for (i = 0; i < count; i++)
    {
        PointLight *pl;
        gfloat d[3];
        gfloat dist;
        gfloat f;
        gfloat lambert = 1.0f;

        if (self->grid_cells > 0 && self->grid_built)
            pl = &g_array_index (self->lights, PointLight,
                                 self->grid_ids[(gsize)cell * self->grid_slots + i]);
        else
            pl = &g_array_index (self->lights, PointLight, self->active[i]);

        for (k = 0; k < 3; k++)
            d[k] = (gfloat)(pl->position[k] - self->origin[k]) - p[k];
        dist = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        f = CLAMP (1.0f - dist / pl->radius, 0.0f, 1.0f);
        if (f <= 0.0f)
            continue;
        if (has_normal && dist > 0.0001f)
            lambert = MAX ((n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / dist, 0.0f);
        f = f * f;
        /* 0.9.0: a spot light's cone, seen from the point */
        if (pl->spot && dist > 0.0001f)
            f *= lrg_light_set_spot_factor (pl->inner, pl->outer,
                                            -(pl->direction[0] * d[0] + pl->direction[1] * d[1]
                                              + pl->direction[2] * d[2]) / dist);
        for (k = 0; k < 3; k++)
            out_rgb[k] += pl->color[k] * pl->intensity * f * lambert;
    }

    for (k = 0; k < 3; k++)
        out_rgb[k] = lrg_light_set_limit_light (self->light_limit, out_rgb[k]);
}

void
lrg_light_set_bind (LrgLightSet *self)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    g_set_object (&bound_light_set, self);
}

void
lrg_light_set_unbind (void)
{
    g_clear_object (&bound_light_set);
}

LrgLightSet *
lrg_light_set_get_bound (void)
{
    return bound_light_set;
}

/* ------------------------------------------------------------------------ */
/* M14: origin, directional, shadows                                        */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_get_origin (LrgLightSet *self,
                          gdouble     *out_x,
                          gdouble     *out_y,
                          gdouble     *out_z)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    if (out_x != NULL)
        *out_x = self->origin[0];
    if (out_y != NULL)
        *out_y = self->origin[1];
    if (out_z != NULL)
        *out_z = self->origin[2];
}

gboolean
lrg_light_set_get_directional (LrgLightSet *self,
                               gfloat      *out_direction,
                               gfloat      *out_rgb)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);

    if (!self->has_directional)
        return FALSE;
    if (out_direction != NULL)
        memcpy (out_direction, self->directional_dir, sizeof self->directional_dir);
    if (out_rgb != NULL)
        memcpy (out_rgb, self->directional_color, sizeof self->directional_color);
    return TRUE;
}

void
lrg_light_set_set_point_light_shadow (LrgLightSet *self,
                                      guint        light,
                                      guint        priority)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->shadow = priority;
}

guint
lrg_light_set_get_point_light_shadow (LrgLightSet *self,
                                      guint        light)
{
    PointLight *pl;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);

    pl = light_at (self, light);
    return pl != NULL ? pl->shadow : 0;
}

void
lrg_light_set_set_shadow_renderer (LrgLightSet       *self,
                                   LrgShadowRenderer *renderer)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (renderer == NULL || LRG_IS_SHADOW_RENDERER (renderer));

    g_set_object (&self->shadows, renderer);
}

LrgShadowRenderer *
lrg_light_set_get_shadow_renderer (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), NULL);

    return self->shadows;
}

/* candidate_before:
 * The pick order: higher priority, then smaller key, then lower id. */
static gboolean
candidate_before (const LrgLightSetShadowCandidate *a,
                  const LrgLightSetShadowCandidate *b)
{
    if (a->priority != b->priority)
        return a->priority > b->priority;
    if (a->key != b->key)
        return a->key < b->key;
    return a->id < b->id;
}

guint
lrg_light_set_pick_shadow_lights (LrgLightSet                *self,
                                  const gdouble              *camera,
                                  gfloat                      max_distance,
                                  guint                       max,
                                  gboolean                    spots,
                                  LrgLightSetShadowCandidate *out)
{
    guint n = 0;
    guint id;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);
    g_return_val_if_fail (camera != NULL, 0);

    if (max == 0)
        return 0;
    for (id = 0; id < self->lights->len; id++)
    {
        PointLight *pl = &g_array_index (self->lights, PointLight, id);
        LrgLightSetShadowCandidate c;
        gdouble d[3];
        guint   slot;
        guint   k;

        if (!pl->used || !pl->enabled || pl->intensity <= 0.0f || pl->shadow == 0
            || (!pl->spot) != (!spots))
            continue;
        for (k = 0; k < 3; k++)
        {
            d[k] = pl->position[k] - camera[k];
            c.position[k] = (gfloat)(pl->position[k] - self->origin[k]);
        }
        c.id = id;
        c.radius = pl->radius;
        c.priority = pl->shadow;
        c.spot = pl->spot;
        memcpy (c.direction, pl->direction, sizeof c.direction);
        c.outer_degrees = pl->outer;
        c.bias = pl->shadow_bias;
        c.key = (gfloat)(sqrt (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - (gdouble)pl->radius);
        if (max_distance > 0.0f && c.key > max_distance)
            continue;

        /* insertion into the sorted best-max */
        if (n == max && !candidate_before (&c, &out[n - 1]))
            continue;
        slot = n < max ? n : n - 1;
        while (slot > 0 && candidate_before (&c, &out[slot - 1]))
        {
            out[slot] = out[slot - 1];
            slot--;
        }
        out[slot] = c;
        if (n < max)
            n++;
    }
    return n;
}

/* ------------------------------------------------------------------------ */
/* M14 (U14.10): the ground overlay in the lit shader                        */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_set_ground_overlay (LrgLightSet      *self,
                                  LrgGroundOverlay *overlay)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (overlay == NULL || LRG_IS_GROUND_OVERLAY (overlay));

    g_set_object (&self->overlay, overlay);
}

LrgGroundOverlay *
lrg_light_set_get_ground_overlay (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), NULL);

    return self->overlay;
}

/* ------------------------------------------------------------------------ */
/* M14: volumetric air                                                      */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_set_air (LrgLightSet    *self,
                       gfloat          density,
                       gfloat          anisotropy,
                       gfloat          intensity,
                       guint           steps,
                       gfloat          max_distance,
                       const GrlColor *tint)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (density > 0.0f && isfinite (density));
    g_return_if_fail (intensity >= 0.0f && isfinite (intensity));
    g_return_if_fail (steps <= LRG_LIGHT_SET_AIR_MAX_STEPS);
    g_return_if_fail (max_distance > 0.0f && isfinite (max_distance));

    self->air_on = TRUE;
    self->air_density = density;
    self->air_g = CLAMP (isfinite (anisotropy) ? anisotropy : 0.0f, -0.95f, 0.95f);
    self->air_intensity = intensity;
    self->air_steps = steps;
    self->air_distance = max_distance;
    if (tint != NULL)
        color_to_rgb (tint, self->air_tint);
    else
        self->air_tint[0] = self->air_tint[1] = self->air_tint[2] = 1.0f;
}

void
lrg_light_set_clear_air (LrgLightSet *self)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->air_on = FALSE;
}

gboolean
lrg_light_set_get_air (LrgLightSet *self,
                       gfloat      *out_density,
                       gfloat      *out_anisotropy,
                       guint       *out_steps)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);

    if (!self->air_on)
        return FALSE;
    if (out_density != NULL)
        *out_density = self->air_density;
    if (out_anisotropy != NULL)
        *out_anisotropy = self->air_g;
    if (out_steps != NULL)
        *out_steps = self->air_steps;
    return TRUE;
}

gfloat
lrg_light_set_air_phase (gfloat anisotropy,
                         gfloat cos_theta)
{
    gfloat g2 = anisotropy * anisotropy;
    gfloat denom = MAX (1.0f + g2 - 2.0f * anisotropy * cos_theta, 1e-4f);

    /* mirrors lrg_air_phase() in LRG_LIGHT_AIR_GLSL */
    return (1.0f - g2) / (4.0f * (gfloat)G_PI * powf (denom, 1.5f));
}

gfloat
lrg_light_set_sample_air (LrgLightSet      *self,
                          const GrlVector3 *from,
                          const GrlVector3 *to,
                          gfloat           *out_rgb)
{
    gfloat ray[3];
    gfloat dir[3];
    gfloat len;
    gfloat dist;
    gfloat dt;
    gfloat sun_phase = 0.0f;
    guint  n_points = 0;
    guint  i;
    guint  k;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 1.0f);
    g_return_val_if_fail (from != NULL && to != NULL && out_rgb != NULL, 1.0f);

    out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
    if (!self->air_on)
        return 1.0f;

    ray[0] = to->x - from->x;
    ray[1] = to->y - from->y;
    ray[2] = to->z - from->z;
    len = sqrtf (ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2]);
    if (!(len >= 1e-4f) || !isfinite (len))
        return 1.0f;
    for (k = 0; k < 3; k++)
        dir[k] = ray[k] / len;
    dist = MIN (len, self->air_distance);
    /* no steps: the closed form, one source at the middle of the ray */
    dt = dist / (gfloat)MAX (self->air_steps, 1u);
    if (self->has_directional)
        sun_phase = lrg_light_set_air_phase (self->air_g,
                                             -(self->directional_dir[0] * dir[0]
                                               + self->directional_dir[1] * dir[1]
                                               + self->directional_dir[2] * dir[2]));
    if (self->shadows != NULL)
        n_points = lrg_shadow_renderer_get_point_count (self->shadows);

    /* the shader's march, steps at their middles, every point lit */
    for (i = 0; i < MAX (self->air_steps, 1u); i++)
    {
        gfloat t = ((gfloat)i + 0.5f) * dt;
        gfloat x[3];
        gfloat s[3];
        gfloat w;
        guint  slot;

        for (k = 0; k < 3; k++)
        {
            x[k] = (&from->x)[k] + dir[k] * t;
            s[k] = self->ambient[k] * (1.0f / (4.0f * (gfloat)G_PI));
            if (self->has_directional)
                s[k] += self->directional_color[k] * sun_phase;
        }
        for (slot = 0; slot < n_points; slot++)
        {
            PointLight *pl = light_at (self, lrg_shadow_renderer_get_point_light (self->shadows, slot));
            gfloat v[3];
            gfloat d;
            gfloat a;
            gfloat ph;

            if (pl == NULL)
                continue;
            for (k = 0; k < 3; k++)
                v[k] = x[k] - (gfloat)(pl->position[k] - self->origin[k]);
            d = sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (d >= pl->radius || d < 1e-4f)
                continue;
            a = 1.0f - d / pl->radius;
            ph = lrg_light_set_air_phase (self->air_g,
                                          -(v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2]) / d);
            for (k = 0; k < 3; k++)
                s[k] += pl->color[k] * pl->intensity * a * a * ph;
        }
        w = self->air_steps == 0 ? 1.0f - expf (-self->air_density * dist)
                                 : expf (-self->air_density * t) * self->air_density * dt;
        for (k = 0; k < 3; k++)
            out_rgb[k] += s[k] * w;
    }
    for (k = 0; k < 3; k++)
        out_rgb[k] *= self->air_intensity * self->air_tint[k];
    return expf (-self->air_density * dist);
}

/* ------------------------------------------------------------------------ */
/* Prewarm                                                                  */
/* ------------------------------------------------------------------------ */

/* lrg_light_set_prewarm: compiles the lit shader now (what the first
 * lrg_light_set_get_shader() would do) */
static gboolean
lrg_light_set_prewarm (LrgPrewarmable *prewarmable)
{
    lrg_light_set_get_shader (LRG_LIGHT_SET (prewarmable));
    return TRUE;
}

static void
lrg_light_set_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_light_set_prewarm;
}

const gchar *
lrg_light_set_get_glsl_library (void)
{
    return light_library;
}

const gchar *
lrg_light_set_get_vertex_source (void)
{
    return light_vs;
}

/* ------------------------------------------------------------------------ */
/* 0.9.0 M2: spot lights, per-light shadow bias and layers                  */
/* ------------------------------------------------------------------------ */

/* set_cone:
 * Makes @pl a spot along @direction (normalized here) with the given
 * half-angles (inner 0 .. 89, outer inner .. 89); FALSE for a zero or
 * non-finite direction (nothing changed). */
static gboolean
set_cone (PointLight       *pl,
          const GrlVector3 *direction,
          gfloat            inner_degrees,
          gfloat            outer_degrees)
{
    gfloat len;

    len = sqrtf (direction->x * direction->x + direction->y * direction->y
                 + direction->z * direction->z);
    if (!(len > 0.0f) || !isfinite (len))
        return FALSE;
    inner_degrees = isfinite (inner_degrees) ? CLAMP (inner_degrees, 0.0f, 89.0f) : 30.0f;
    outer_degrees = isfinite (outer_degrees) ? CLAMP (outer_degrees, inner_degrees, 89.0f)
                                             : MAX (inner_degrees, 45.0f);
    pl->spot = TRUE;
    pl->direction[0] = direction->x / len;
    pl->direction[1] = direction->y / len;
    pl->direction[2] = direction->z / len;
    pl->inner = inner_degrees;
    pl->outer = outer_degrees;
    return TRUE;
}

guint
lrg_light_set_add_spot_light (LrgLightSet      *self,
                              gdouble           x,
                              gdouble           y,
                              gdouble           z,
                              const GrlVector3 *direction,
                              const GrlColor   *color,
                              gfloat            radius,
                              gfloat            inner_degrees,
                              gfloat            outer_degrees)
{
    guint id;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_LIGHT_SET_INVALID_LIGHT);
    g_return_val_if_fail (direction != NULL, LRG_LIGHT_SET_INVALID_LIGHT);

    id = lrg_light_set_add_point_light (self, x, y, z, color, radius);
    if (id == LRG_LIGHT_SET_INVALID_LIGHT)
        return id;
    if (!set_cone (light_at (self, id), direction, inner_degrees, outer_degrees))
    {
        g_warning ("lrg_light_set_add_spot_light: zero direction, added a point light");
    }
    self->lights_serial++;
    return id;
}

void
lrg_light_set_set_spot_light (LrgLightSet      *self,
                              guint             light,
                              const GrlVector3 *direction,
                              gfloat            inner_degrees,
                              gfloat            outer_degrees)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    if (direction == NULL)
        pl->spot = FALSE;
    else if (!set_cone (pl, direction, inner_degrees, outer_degrees))
        g_return_if_reached ();
    self->lights_serial++;
}

gboolean
lrg_light_set_get_spot_light (LrgLightSet *self,
                              guint        light,
                              gfloat      *out_direction,
                              gfloat      *out_inner_degrees,
                              gfloat      *out_outer_degrees)
{
    PointLight *pl;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);

    pl = light_at (self, light);
    if (pl == NULL || !pl->spot)
        return FALSE;
    if (out_direction != NULL)
        memcpy (out_direction, pl->direction, sizeof pl->direction);
    if (out_inner_degrees != NULL)
        *out_inner_degrees = pl->inner;
    if (out_outer_degrees != NULL)
        *out_outer_degrees = pl->outer;
    return TRUE;
}

gfloat
lrg_light_set_spot_factor (gfloat inner_degrees,
                           gfloat outer_degrees,
                           gfloat cos_angle)
{
    gfloat c_outer;
    gfloat c_inner;
    gfloat t;

    /* GLSL smoothstep (edge0 = cos outer, edge1 = cos inner) */
    c_outer = cosf (outer_degrees * (gfloat)G_PI / 180.0f);
    c_inner = MAX (cosf (inner_degrees * (gfloat)G_PI / 180.0f), c_outer + 1e-4f);
    t = CLAMP ((cos_angle - c_outer) / (c_inner - c_outer), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void
lrg_light_set_brdf (const gfloat *normal,
                    const gfloat *view,
                    const gfloat *light,
                    const gfloat *albedo,
                    gfloat        metallic,
                    gfloat        roughness,
                    gfloat       *out_rgb)
{
    gfloat h[3];
    gfloat hl;
    gfloat nl;
    gfloat nv;
    gfloat nh;
    gfloat vh;
    gfloat r;
    gfloat a2;
    gfloat dd;
    gfloat d;
    gfloat k;
    gfloat g;
    gfloat fw;
    guint  c;

    g_return_if_fail (normal != NULL && view != NULL && light != NULL);
    g_return_if_fail (albedo != NULL && out_rgb != NULL);

    /* the same steps as lrg_brdf_direct() in the shader */
    nl = MAX (normal[0] * light[0] + normal[1] * light[1] + normal[2] * light[2], 0.0f);
    if (nl <= 0.0f)
    {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
        return;
    }
    nv = MAX (normal[0] * view[0] + normal[1] * view[1] + normal[2] * view[2], 1e-4f);
    for (c = 0; c < 3; c++)
        h[c] = light[c] + view[c];
    hl = sqrtf (h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
    if (hl > 0.0f)
        for (c = 0; c < 3; c++)
            h[c] /= hl;
    nh = MAX (normal[0] * h[0] + normal[1] * h[1] + normal[2] * h[2], 0.0f);
    vh = MAX (view[0] * h[0] + view[1] * h[1] + view[2] * h[2], 0.0f);
    r = CLAMP (roughness, 0.045f, 1.0f);
    a2 = r * r * r * r;
    dd = nh * nh * (a2 - 1.0f) + 1.0f;
    d = a2 / ((gfloat)G_PI * dd * dd);
    k = (r + 1.0f) * (r + 1.0f) / 8.0f;
    g = (nv / (nv * (1.0f - k) + k)) * (nl / (nl * (1.0f - k) + k));
    fw = powf (1.0f - vh, 5.0f);
    metallic = CLAMP (metallic, 0.0f, 1.0f);
    for (c = 0; c < 3; c++)
    {
        gfloat f0 = 0.04f + (albedo[c] - 0.04f) * metallic;
        gfloat f = f0 + (1.0f - f0) * fw;
        gfloat spec = d * g * f / (4.0f * nv * nl);
        gfloat kd = (1.0f - f) * (1.0f - metallic);

        out_rgb[c] = (kd * albedo[c] + spec * (gfloat)G_PI) * nl;
    }
}

void
lrg_light_set_set_light_shadow_bias (LrgLightSet *self,
                                     guint        light,
                                     gfloat       bias)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->shadow_bias = isfinite (bias) ? MAX (bias, 0.0f) : 0.0f;
    self->lights_serial++;
}

gfloat
lrg_light_set_get_light_shadow_bias (LrgLightSet *self,
                                     guint        light)
{
    PointLight *pl;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0.0f);

    pl = light_at (self, light);
    return pl != NULL ? pl->shadow_bias : 0.0f;
}

void
lrg_light_set_set_light_layers (LrgLightSet *self,
                                guint        light,
                                guint32      layers)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->layers = layers;
    self->lights_serial++;
}

guint32
lrg_light_set_get_light_layers (LrgLightSet *self,
                                guint        light)
{
    PointLight *pl;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);

    pl = light_at (self, light);
    return pl != NULL ? pl->layers : 0;
}

void
lrg_light_set_set_draw_layers (LrgLightSet *self,
                               guint32      layers)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->draw_layers = layers;
    /* uploaded now, for renderers that switch layers between draws */
    if (self->shader != NULL && IsWindowReady ())
    {
        Shader *handle = grl_shader_get_handle (self->shader);
        gint    v[2];

        v[0] = (gint)(layers & 0xffffu);
        v[1] = (gint)(layers >> 16);
        rlDrawRenderBatchActive ();
        SetShaderValue (*handle, self->locs.draw_layers, v, SHADER_UNIFORM_IVEC2);
    }
}

guint32
lrg_light_set_get_draw_layers (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_RENDER_LAYERS_MASK_ALL);
    return self->draw_layers;
}

void
lrg_light_set_set_output_linear (LrgLightSet *self,
                                 gboolean     linear)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    self->output_linear = !!linear;
}

gboolean
lrg_light_set_get_output_linear (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);
    return self->output_linear;
}

/* ------------------------------------------------------------------------ */
/* 0.9.0 M2: the hook objects                                               */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_set_environment (LrgLightSet    *self,
                               LrgEnvironment *environment)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (environment == NULL || LRG_IS_ENVIRONMENT (environment));

    g_set_object (&self->environment, environment);
}

LrgEnvironment *
lrg_light_set_get_environment (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), NULL);
    return self->environment;
}

void
lrg_light_set_set_ssao (LrgLightSet *self,
                        LrgSsao     *ssao)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (ssao == NULL || LRG_IS_SSAO (ssao));

    g_set_object (&self->ssao, ssao);
}

LrgSsao *
lrg_light_set_get_ssao (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), NULL);
    return self->ssao;
}

void
lrg_light_set_set_decals (LrgLightSet *self,
                          LrgDecalSet *decals)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (decals == NULL || LRG_IS_DECAL_SET (decals));

    g_set_object (&self->decals, decals);
}

LrgDecalSet *
lrg_light_set_get_decals (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), NULL);
    return self->decals;
}

/* ------------------------------------------------------------------------ */
/* 0.9.0 M2: lights from LrgLight3DDesc                                     */
/* ------------------------------------------------------------------------ */

/* desc_color: the description's colour x @scale as an 8-bit colour,
 * saturating at white */
static GrlColor
desc_color (const LrgLight3DDesc *desc,
            gfloat                scale)
{
    GrlColor c;

    c.r = (guint8)CLAMP (desc->color[0] * scale * 255.0f + 0.5f, 0.0f, 255.0f);
    c.g = (guint8)CLAMP (desc->color[1] * scale * 255.0f + 0.5f, 0.0f, 255.0f);
    c.b = (guint8)CLAMP (desc->color[2] * scale * 255.0f + 0.5f, 0.0f, 255.0f);
    c.a = 255;
    return c;
}

guint
lrg_light_set_add_light_desc (LrgLightSet          *self,
                              const LrgLight3DDesc *desc)
{
    GrlColor   c;
    GrlVector3 dir;
    guint      id;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_LIGHT_SET_INVALID_LIGHT);
    g_return_val_if_fail (desc != NULL, LRG_LIGHT_SET_INVALID_LIGHT);

    if (!desc->enabled)
        return LRG_LIGHT_SET_INVALID_LIGHT;
    dir.x = desc->direction[0];
    dir.y = desc->direction[1];
    dir.z = desc->direction[2];

    /* the directional light: colour x intensity in the colour */
    if (desc->kind == LRG_LIGHT3D_DIRECTIONAL)
    {
        c = desc_color (desc, MAX (desc->intensity, 0.0f));
        lrg_light_set_set_directional (self, &dir, &c);
        return LRG_LIGHT_SET_INVALID_LIGHT;
    }

    /* points and spots: the colour as given, intensity on the light */
    c = desc_color (desc, 1.0f);
    id = lrg_light_set_add_point_light (self, desc->position[0], desc->position[1],
                                        desc->position[2], &c,
                                        desc->range > 0.0f ? desc->range : 10.0f);
    if (id == LRG_LIGHT_SET_INVALID_LIGHT)
        return id;
    lrg_light_set_set_point_light_intensity (self, id, MAX (desc->intensity, 0.0f));
    if (desc->kind == LRG_LIGHT3D_SPOT)
    {
        /* radians in the description, degrees here */
        if (!set_cone (light_at (self, id), &dir, desc->inner_angle * 180.0f / (gfloat)G_PI,
                       desc->outer_angle * 180.0f / (gfloat)G_PI))
            g_debug ("lrg_light_set_add_light_desc: a spot without a direction stays a point light");
    }
    lrg_light_set_set_light_layers (self, id, desc->layer_mask);
    if (desc->cast_shadows)
        lrg_light_set_set_point_light_shadow (self, id, 1);

    /* a light component's own shadow bias */
    if (desc->source != NULL && G_IS_OBJECT (desc->source))
    {
        GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (desc->source),
                                                          "shadow-bias");

        if (pspec != NULL && pspec->value_type == G_TYPE_FLOAT)
        {
            gfloat bias = 0.0f;

            g_object_get (desc->source, "shadow-bias", &bias, NULL);
            lrg_light_set_set_light_shadow_bias (self, id, bias);
        }
    }
    self->lights_serial++;
    return id;
}
