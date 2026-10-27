/* lrg-water-surface.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The water shader and its CPU twin. Patches are kept on the CPU (rest
 * positions and shore factors) and uploaded as raylib meshes on the
 * first draw after a change. The waves (Gerstner, GPU Gems 1 ch. 1)
 * are summed in the vertex shader from uniforms; wave_sum() below is
 * the same arithmetic, the reference the tests check and the answer
 * for anything that floats.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-water-surface.h"
#include "graphics/lrg-light-set-private.h"
#include "graphics/lrg-draw-cull-private.h"
#include "lrg-log.h"
#include "core/lrg-prewarm.h"

/* the material map the scene copy is bound to (raylib binds every map
 * with a texture to its own unit); the albedo map stays the default */
#define SCENE_MAP           (MATERIAL_MAP_METALNESS)
#define GL_COLOR_BIT        (0x00004000)
#define GL_FRAMEBUFFER_BIND (0x8D40)

static const gchar water_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform int waveCount;\n"
    "uniform vec4 waveA[4];\n"
    "uniform vec4 waveB[4];\n"
    "uniform float time;\n"
    "uniform int lit;\n"
    LRG_LIGHT_SAMPLE_GLSL
    "out vec3 fragPosition;\n"
    "out vec2 restXZ;\n"
    "out float shore;\n"
    "out float crest;\n"
    "out vec3 vLight;\n"
    "void main() {\n"
    "    vec3 wp = vec3(matModel * vec4(vertexPosition, 1.0));\n"
    "    vec2 q = wp.xz;\n"
    "    vec3 off = vec3(0.0);\n"
    "    float amp = 0.0;\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        if (i >= waveCount) break;\n"
    "        float th = waveA[i].w * dot(waveA[i].xy, q) + waveB[i].z - waveB[i].x * time;\n"
    "        off.xz += waveB[i].y * waveA[i].z * waveA[i].xy * cos(th);\n"
    "        off.y += waveA[i].z * sin(th);\n"
    "        amp += waveA[i].z;\n"
    "    }\n"
    "    crest = amp > 0.0 ? clamp(off.y / amp, -1.0, 1.0) : 0.0;\n"
    "    fragPosition = wp + off;\n"
    "    restXZ = q;\n"
    "    shore = vertexColor.r;\n"
    "    vLight = lit == 1 ? lrg_light_sample(fragPosition) : vec3(1.0);\n"
    "    gl_Position = mvp * vec4(vertexPosition + off, 1.0);\n"
    "}\n";

static const gchar water_fs[] =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec2 restXZ;\n"
    "in float shore;\n"
    "in float crest;\n"
    "in vec3 vLight;\n"
    "uniform int waveCount;\n"
    "uniform vec4 waveA[4];\n"
    "uniform vec4 waveB[4];\n"
    "uniform float time;\n"
    "uniform vec3 viewPos;\n"
    "uniform vec3 shallowColor;\n"
    "uniform vec3 deepColor;\n"
    "uniform vec3 foamColor;\n"
    "uniform vec3 skyHorizon;\n"
    "uniform vec3 skyZenith;\n"
    "uniform vec3 sunDir;\n"
    "uniform vec3 sunColor;\n"
    "uniform vec4 optics;\n"
    "uniform int hasScene;\n"
    "uniform sampler2D sceneTex;\n"
    "uniform vec2 screenSize;\n"
    "uniform mat4 viewProj;\n"
    LRG_FOG_GLSL
    "out vec4 finalColor;\n"
    "float hash12(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "float vnoise(vec2 p) {\n"
    "    vec2 i = floor(p);\n"
    "    vec2 f = fract(p);\n"
    "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),\n"
    "               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);\n"
    "}\n"
    "void main() {\n"
    "    vec3 n = vec3(0.0, 1.0, 0.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        if (i >= waveCount) break;\n"
    "        float th = waveA[i].w * dot(waveA[i].xy, restXZ) + waveB[i].z - waveB[i].x * time;\n"
    "        float wa = waveA[i].w * waveA[i].z;\n"
    "        n.xz -= waveA[i].xy * wa * cos(th);\n"
    "        n.y -= waveB[i].y * wa * sin(th);\n"
    "    }\n"
    "    vec2 r1 = restXZ * 1.7 + vec2(time * 0.31, time * 0.17);\n"
    "    vec2 r2 = restXZ * 2.9 - vec2(time * 0.23, -time * 0.29);\n"
    "    n.xz += 0.06 * vec2(cos(r1.x + sin(r1.y)), cos(r2.y + sin(r2.x)));\n"
    "    n = normalize(n);\n"
    "    vec3 v = normalize(viewPos - fragPosition);\n"
    "    if (dot(v, n) < 0.0 && v.y < 0.0) n = -n;\n"
    "    float cosv = clamp(abs(dot(n, v)), 0.0, 1.0);\n"
    "    float fres = 0.02 + 0.98 * pow(1.0 - cosv, 5.0);\n"
    "    vec3 r = reflect(-v, n);\n"
    "    vec3 sky = mix(skyHorizon, skyZenith, sqrt(clamp(r.y, 0.0, 1.0)));\n"
    "    vec3 refl = sky;\n"
    "    vec2 suv = gl_FragCoord.xy / screenSize;\n"
    "    vec3 body = mix(deepColor, shallowColor, clamp(shore, 0.0, 1.0)) * vLight;\n"
    "    vec3 refr = body;\n"
    "    if (hasScene == 1) {\n"
    "        vec4 clip = viewProj * vec4(fragPosition + r * optics.w, 1.0);\n"
    "        if (clip.w > 0.0) {\n"
    "            vec2 ruv = clip.xy / clip.w * 0.5 + 0.5;\n"
    "            vec2 edge = smoothstep(vec2(0.0), vec2(0.08), ruv) * (1.0 - smoothstep(vec2(0.92), vec2(1.0), ruv));\n"
    "            float w = edge.x * edge.y * step(suv.y, ruv.y) * optics.y;\n"
    "            refl = mix(sky, texture(sceneTex, ruv).rgb, clamp(w, 0.0, 1.0));\n"
    "        }\n"
    "        vec2 tuv = clamp(suv + n.xz * optics.z, vec2(0.001), vec2(0.999));\n"
    "        refr = mix(texture(sceneTex, tuv).rgb, body, 0.45 + 0.35 * (1.0 - shore));\n"
    "    }\n"
    "    vec3 c = mix(refr, refl, fres);\n"
    "    float glint = pow(max(dot(r, -normalize(sunDir)), 0.0), 220.0);\n"
    "    c += sunColor * glint * 1.5;\n"
    "    float grain = vnoise(restXZ * 1.3 + vec2(time * 0.21, -time * 0.13));\n"
    "    float foam = smoothstep(0.35, 0.95, shore + 0.45 * (grain - 0.5) + 0.12 * sin(time * 1.3 + restXZ.x * 0.7));\n"
    "    foam = max(foam, smoothstep(0.75, 1.0, crest) * smoothstep(0.4, 0.9, grain));\n"
    "    c = mix(c, foamColor * vLight, foam * 0.85);\n"
    "    float alpha = hasScene == 1 ? 1.0 : mix(1.0 - optics.x, 1.0, max(fres, foam));\n"
    "    finalColor = vec4(mix(c, fogColor.rgb, lrg_fog_amount(fragPosition)), alpha);\n"
    "}\n";

typedef struct
{
    guint    columns;
    guint    rows;
    gfloat  *positions;   /* (columns + 1) * (rows + 1) * 3 */
    gfloat  *foam;        /* one per vertex */
    guint8  *cells;       /* columns * rows wet flags (heightfields), NULL: all */
    gfloat   min[3];      /* box of the rest positions (patch_bounds()) */
    gfloat   max[3];
    Mesh     mesh;
    gboolean uploaded;
} Patch;

struct _LrgWaterSurface
{
    GObject       parent_instance;

    LrgWaterWave  waves[LRG_WATER_SURFACE_MAX_WAVES];
    guint         n_waves;
    gfloat        wave_scale;
    gdouble       origin[2];

    gfloat        shallow[3];
    gfloat        deep[3];
    gfloat        foam[3];
    gfloat        horizon[3];
    gfloat        zenith[3];
    gfloat        sun_dir[3];
    gfloat        sun_color[3];
    gfloat        optics[4];   /* transparency, reflection, refraction, distance */

    GPtrArray    *patches;     /* Patch * */

    /* GPU */
    gboolean      shader_tried;
    gboolean      shader_ok;
    Shader        shader;
    Material      material;
    gint          loc_wave_count;
    gint          loc_wave_a;
    gint          loc_wave_b;
    gint          loc_time;
    gint          loc_lit;
    gint          loc_view;
    gint          loc_shallow;
    gint          loc_deep;
    gint          loc_foam;
    gint          loc_horizon;
    gint          loc_zenith;
    gint          loc_sun_dir;
    gint          loc_sun_color;
    gint          loc_optics;
    gint          loc_has_scene;
    gint          loc_screen;
    gint          loc_view_proj;
    LrgLightSetSampleLocations light_locs;
    LrgLightSetFogLocations    fog_locs;

    RenderTexture2D scene;
    gboolean        scene_valid;   /* captured for the next draw */
};

static void lrg_water_surface_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgWaterSurface, lrg_water_surface, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                      lrg_water_surface_prewarmable_init))

static void
patch_free (gpointer data)
{
    Patch *patch = data;

    /* a mesh only goes with a live context */
    if (patch->uploaded && IsWindowReady ())
        UnloadMesh (patch->mesh);
    else
    {
        /* UnloadMesh() would free these; without GL free them here */
        MemFree (patch->mesh.vertices);
        MemFree (patch->mesh.texcoords);
        MemFree (patch->mesh.colors);
        MemFree (patch->mesh.indices);
    }
    g_free (patch->positions);
    g_free (patch->foam);
    g_free (patch->cells);
    g_free (patch);
}

static void
lrg_water_surface_finalize (GObject *object)
{
    LrgWaterSurface *self = LRG_WATER_SURFACE (object);

    if (IsWindowReady ())
        lrg_water_surface_release_gpu (self);
    g_clear_pointer (&self->patches, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_water_surface_parent_class)->finalize (object);
}

static void
lrg_water_surface_class_init (LrgWaterSurfaceClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_water_surface_finalize;
}

static void
rgb_set (gfloat *out,
         gfloat  r,
         gfloat  g,
         gfloat  b)
{
    out[0] = r;
    out[1] = g;
    out[2] = b;
}

static void
lrg_water_surface_init (LrgWaterSurface *self)
{
    LrgWaterWave wave;

    self->wave_scale = 1.0f;
    self->patches = g_ptr_array_new_with_free_func (patch_free);
    rgb_set (self->shallow, 0.22f, 0.42f, 0.40f);
    rgb_set (self->deep, 0.05f, 0.16f, 0.22f);
    rgb_set (self->foam, 0.92f, 0.95f, 0.96f);
    rgb_set (self->horizon, 0.72f, 0.80f, 0.86f);
    rgb_set (self->zenith, 0.32f, 0.48f, 0.72f);
    rgb_set (self->sun_dir, -0.3f, -0.8f, -0.5f);
    rgb_set (self->sun_color, 1.0f, 0.95f, 0.85f);
    self->optics[0] = 0.35f;
    self->optics[1] = 0.8f;
    self->optics[2] = 0.03f;
    self->optics[3] = 25.0f;

    /* three gentle waves at uneven angles, wavelengths and speeds */
    wave.direction[0] = 1.0f; wave.direction[1] = 0.25f;
    wave.amplitude = 0.12f; wave.wavelength = 9.0f; wave.speed = 1.6f; wave.steepness = 0.5f;
    lrg_water_surface_set_wave (self, 0, &wave);
    wave.direction[0] = 0.4f; wave.direction[1] = 1.0f;
    wave.amplitude = 0.07f; wave.wavelength = 5.0f; wave.speed = 1.2f; wave.steepness = 0.5f;
    lrg_water_surface_set_wave (self, 1, &wave);
    wave.direction[0] = -0.7f; wave.direction[1] = 0.6f;
    wave.amplitude = 0.04f; wave.wavelength = 2.6f; wave.speed = 0.9f; wave.steepness = 0.4f;
    lrg_water_surface_set_wave (self, 2, &wave);

    /* build the GPU resources behind a loading screen, not on the
     * first draw (see lrg_prewarm_track()) */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgWaterSurface *
lrg_water_surface_new (void)
{
    return g_object_new (LRG_TYPE_WATER_SURFACE, NULL);
}

/* --- waves --- */

void
lrg_water_surface_set_wave (LrgWaterSurface    *self,
                            guint               index,
                            const LrgWaterWave *wave)
{
    LrgWaterWave *w;
    gfloat len;

    g_return_if_fail (LRG_IS_WATER_SURFACE (self));
    g_return_if_fail (wave != NULL);
    g_return_if_fail (index < LRG_WATER_SURFACE_MAX_WAVES);

    w = &self->waves[index];
    *w = *wave;
    len = sqrtf (w->direction[0] * w->direction[0] + w->direction[1] * w->direction[1]);
    if (!(len > 1e-6f) || !isfinite (len))
    {
        w->direction[0] = 1.0f;
        w->direction[1] = 0.0f;
    }
    else
    {
        w->direction[0] /= len;
        w->direction[1] /= len;
    }
    w->amplitude = isfinite (w->amplitude) ? MAX (w->amplitude, 0.0f) : 0.0f;
    w->wavelength = isfinite (w->wavelength) ? MAX (w->wavelength, 0.01f) : 1.0f;
    w->speed = isfinite (w->speed) ? w->speed : 0.0f;
    w->steepness = isfinite (w->steepness) ? CLAMP (w->steepness, 0.0f, 1.0f) : 0.0f;
    self->n_waves = MAX (self->n_waves, index + 1);
}

gboolean
lrg_water_surface_get_wave (LrgWaterSurface *self,
                            guint            index,
                            LrgWaterWave    *out_wave)
{
    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), FALSE);
    g_return_val_if_fail (out_wave != NULL, FALSE);

    if (index >= self->n_waves)
        return FALSE;
    *out_wave = self->waves[index];
    return TRUE;
}

void
lrg_water_surface_set_wave_count (LrgWaterSurface *self,
                                  guint            count)
{
    g_return_if_fail (LRG_IS_WATER_SURFACE (self));
    self->n_waves = MIN (count, (guint)LRG_WATER_SURFACE_MAX_WAVES);
}

guint
lrg_water_surface_get_wave_count (LrgWaterSurface *self)
{
    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), 0);
    return self->n_waves;
}

void
lrg_water_surface_set_wave_scale (LrgWaterSurface *self,
                                  gfloat           scale)
{
    g_return_if_fail (LRG_IS_WATER_SURFACE (self));
    self->wave_scale = isfinite (scale) ? MAX (scale, 0.0f) : 0.0f;
}

void
lrg_water_surface_set_origin (LrgWaterSurface *self,
                              gdouble          x,
                              gdouble          z)
{
    g_return_if_fail (LRG_IS_WATER_SURFACE (self));
    self->origin[0] = isfinite (x) ? x : 0.0;
    self->origin[1] = isfinite (z) ? z : 0.0;
}

/*
 * wave_params:
 * The shader's per-wave uniforms: (dir.x, dir.z, amplitude, k) and
 * (omega, Q, phase).  k = 2 pi / L, omega = speed * k, and the steepness is
 * shared out over the waves (Q = s / (k A n)) so the horizontal pull
 * never folds the surface over itself.
 */
static void
wave_params (LrgWaterSurface *self,
             guint            i,
             gfloat          *a,
             gfloat          *b)
{
    const LrgWaterWave *w = &self->waves[i];
    gfloat amp;
    gfloat k;

    amp = w->amplitude * self->wave_scale;
    k = (gfloat)(2.0 * G_PI) / w->wavelength;
    a[0] = w->direction[0];
    a[1] = w->direction[1];
    a[2] = amp;
    a[3] = k;
    b[0] = w->speed * k;
    b[1] = amp > 0.0f ? w->steepness / (k * amp * (gfloat)MAX (self->n_waves, 1u)) : 0.0f;
    /* the origin enters as a phase, reduced in double on the CPU, so
     * the shader works on small draw-space coordinates only */
    b[2] = (gfloat)fmod ((gdouble)k * ((gdouble)w->direction[0] * self->origin[0] +
                                       (gdouble)w->direction[1] * self->origin[1]),
                         2.0 * G_PI);
    b[3] = 0.0f;
}

void
lrg_water_surface_evaluate (LrgWaterSurface *self,
                            gfloat           x,
                            gfloat           z,
                            gfloat           time,
                            gfloat          *out_offset,
                            gfloat          *out_normal)
{
    gfloat off[3] = { 0.0f, 0.0f, 0.0f };
    gfloat n[3] = { 0.0f, 1.0f, 0.0f };
    gfloat len;
    guint i;

    g_return_if_fail (LRG_IS_WATER_SURFACE (self));

    for (i = 0; i < self->n_waves; i++)
    {
        gfloat a[4];
        gfloat b[4];
        gdouble th;
        gfloat c;
        gfloat s;

        wave_params (self, i, a, b);
        th = (gdouble)a[3] * ((gdouble)a[0] * x + (gdouble)a[1] * z) + (gdouble)b[2] -
             (gdouble)b[0] * time;
        c = (gfloat)cos (th);
        s = (gfloat)sin (th);
        off[0] += b[1] * a[2] * a[0] * c;
        off[2] += b[1] * a[2] * a[1] * c;
        off[1] += a[2] * s;
        n[0] -= a[0] * a[3] * a[2] * c;
        n[2] -= a[1] * a[3] * a[2] * c;
        n[1] -= b[1] * a[3] * a[2] * s;
    }
    if (out_offset != NULL)
        memcpy (out_offset, off, sizeof (off));
    if (out_normal != NULL)
    {
        len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!(len > 1e-9f))
        {
            n[0] = 0.0f;
            n[1] = 1.0f;
            n[2] = 0.0f;
            len = 1.0f;
        }
        out_normal[0] = n[0] / len;
        out_normal[1] = n[1] / len;
        out_normal[2] = n[2] / len;
    }
}

gfloat
lrg_water_surface_get_max_height (LrgWaterSurface *self)
{
    gfloat sum;
    guint i;

    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), 0.0f);

    sum = 0.0f;
    for (i = 0; i < self->n_waves; i++)
        sum += self->waves[i].amplitude * self->wave_scale;
    return sum;
}

/* --- looks --- */

static void
color_rgb (const GrlColor *c,
           gfloat         *out)
{
    out[0] = (gfloat)c->r / 255.0f;
    out[1] = (gfloat)c->g / 255.0f;
    out[2] = (gfloat)c->b / 255.0f;
}

void
lrg_water_surface_set_colors (LrgWaterSurface *self,
                              const GrlColor  *shallow,
                              const GrlColor  *deep,
                              const GrlColor  *foam)
{
    g_return_if_fail (LRG_IS_WATER_SURFACE (self));
    g_return_if_fail (shallow != NULL && deep != NULL && foam != NULL);

    color_rgb (shallow, self->shallow);
    color_rgb (deep, self->deep);
    color_rgb (foam, self->foam);
}

void
lrg_water_surface_set_sky (LrgWaterSurface *self,
                           const GrlColor  *horizon,
                           const GrlColor  *zenith)
{
    g_return_if_fail (LRG_IS_WATER_SURFACE (self));
    g_return_if_fail (horizon != NULL && zenith != NULL);

    color_rgb (horizon, self->horizon);
    color_rgb (zenith, self->zenith);
}

void
lrg_water_surface_set_sun (LrgWaterSurface *self,
                           const gfloat    *direction,
                           const GrlColor  *color)
{
    gfloat len;

    g_return_if_fail (LRG_IS_WATER_SURFACE (self));
    g_return_if_fail (direction != NULL && color != NULL);

    len = sqrtf (direction[0] * direction[0] + direction[1] * direction[1] +
                 direction[2] * direction[2]);
    if (len > 1e-6f && isfinite (len))
        rgb_set (self->sun_dir, direction[0] / len, direction[1] / len, direction[2] / len);
    color_rgb (color, self->sun_color);
}

void
lrg_water_surface_set_optics (LrgWaterSurface *self,
                              gfloat           transparency,
                              gfloat           reflection,
                              gfloat           refraction,
                              gfloat           reflect_distance)
{
    g_return_if_fail (LRG_IS_WATER_SURFACE (self));

    self->optics[0] = isfinite (transparency) ? CLAMP (transparency, 0.0f, 1.0f) : 0.0f;
    self->optics[1] = isfinite (reflection) ? CLAMP (reflection, 0.0f, 1.0f) : 0.0f;
    self->optics[2] = isfinite (refraction) ? CLAMP (refraction, 0.0f, 0.2f) : 0.0f;
    self->optics[3] = isfinite (reflect_distance) && reflect_distance > 0.0f ? reflect_distance : 1.0f;
}

/* --- geometry --- */

void
lrg_water_surface_clear (LrgWaterSurface *self)
{
    g_return_if_fail (LRG_IS_WATER_SURFACE (self));
    g_ptr_array_set_size (self->patches, 0);
}

/* patch_bounds: the box of @patch's rest positions */
static void
patch_bounds (Patch *patch)
{
    gsize n;
    gsize i;
    guint k;

    n = (gsize)(patch->columns + 1) * (patch->rows + 1);
    for (k = 0; k < 3; k++)
    {
        patch->min[k] = G_MAXFLOAT;
        patch->max[k] = -G_MAXFLOAT;
    }
    for (i = 0; i < n; i++)
    {
        for (k = 0; k < 3; k++)
        {
            patch->min[k] = MIN (patch->min[k], patch->positions[i * 3 + k]);
            patch->max[k] = MAX (patch->max[k], patch->positions[i * 3 + k]);
        }
    }
}

gboolean
lrg_water_surface_add_grid (LrgWaterSurface   *self,
                            gfloat             x0,
                            gfloat             z0,
                            gfloat             x1,
                            gfloat             z1,
                            gfloat             y,
                            guint              columns,
                            guint              rows,
                            LrgWaterFoamFunc   foam_func,
                            gpointer           user_data,
                            GError           **error)
{
    Patch *patch;
    guint64 n;
    guint r;
    guint c;

    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    n = (guint64)(columns + 1) * (guint64)(rows + 1);
    if (columns == 0 || rows == 0 || n > LRG_WATER_SURFACE_MAX_PATCH_VERTICES)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "water patch of %u x %u cells: 1 .. %u vertices allowed",
                     columns, rows, (guint)LRG_WATER_SURFACE_MAX_PATCH_VERTICES);
        return FALSE;
    }
    if (!isfinite (x0) || !isfinite (z0) || !isfinite (x1) || !isfinite (z1) || !isfinite (y) ||
        x0 == x1 || z0 == z1)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "water patch has no area or a non-finite corner");
        return FALSE;
    }

    patch = g_new0 (Patch, 1);
    patch->columns = columns;
    patch->rows = rows;
    patch->positions = g_new (gfloat, n * 3);
    patch->foam = g_new (gfloat, n);
    for (r = 0; r <= rows; r++)
    {
        for (c = 0; c <= columns; c++)
        {
            gsize i;
            gfloat x;
            gfloat z;

            i = (gsize)r * (columns + 1) + c;
            x = x0 + (x1 - x0) * (gfloat)c / (gfloat)columns;
            z = z0 + (z1 - z0) * (gfloat)r / (gfloat)rows;
            patch->positions[i * 3 + 0] = x;
            patch->positions[i * 3 + 1] = y;
            patch->positions[i * 3 + 2] = z;
            patch->foam[i] = foam_func != NULL ? CLAMP (foam_func (x, z, user_data), 0.0f, 1.0f) : 0.0f;
        }
    }
    patch_bounds (patch);
    g_ptr_array_add (self->patches, patch);
    return TRUE;
}

/* patch_triangles: the triangles a patch draws (2 per wet cell) */
static guint
patch_triangles (const Patch *p)
{
    guint n;
    guint k;

    if (p->cells == NULL)
        return p->columns * p->rows * 2;
    n = 0;
    for (k = 0; k < p->columns * p->rows; k++)
        n += p->cells[k] != 0 ? 2 : 0;
    return n;
}

gboolean
lrg_water_surface_add_heightfield (LrgWaterSurface   *self,
                                   gfloat             x0,
                                   gfloat             z0,
                                   gfloat             x1,
                                   gfloat             z1,
                                   guint              columns,
                                   guint              rows,
                                   const gfloat      *heights,
                                   const guint8      *cells,
                                   LrgWaterFoamFunc   foam_func,
                                   gpointer           user_data,
                                   GError           **error)
{
    Patch  *patch;
    guint64 n;
    guint   k;
    guint   wet;

    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), FALSE);
    g_return_val_if_fail (heights != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    n = (guint64)(columns + 1) * (guint64)(rows + 1);
    if (columns == 0 || rows == 0 || n > LRG_WATER_SURFACE_MAX_PATCH_VERTICES)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "water patch of %u x %u cells: 1 .. %u vertices allowed",
                     columns, rows, (guint)LRG_WATER_SURFACE_MAX_PATCH_VERTICES);
        return FALSE;
    }
    for (k = 0; k < (guint)n; k++)
    {
        if (!isfinite (heights[k]))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "water heightfield: height %u is not finite", k);
            return FALSE;
        }
    }
    wet = columns * rows;
    if (cells != NULL)
    {
        wet = 0;
        for (k = 0; k < columns * rows; k++)
            wet += cells[k] != 0 ? 1u : 0u;
    }
    if (wet == 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "water heightfield has no wet cell");
        return FALSE;
    }
    /* the grid's corners and checks, then the heights over the plane */
    if (!lrg_water_surface_add_grid (self, x0, z0, x1, z1, 0.0f, columns, rows, foam_func,
                                     user_data, error))
        return FALSE;
    patch = g_ptr_array_index (self->patches, self->patches->len - 1);
    for (k = 0; k < (guint)n; k++)
        patch->positions[k * 3 + 1] = heights[k];
    if (cells != NULL)
        patch->cells = g_memdup2 (cells, (gsize)columns * rows);
    patch_bounds (patch);
    return TRUE;
}

guint
lrg_water_surface_get_patch_count (LrgWaterSurface *self)
{
    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), 0);
    return self->patches->len;
}

guint
lrg_water_surface_get_vertex_count (LrgWaterSurface *self)
{
    guint sum;
    guint i;

    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), 0);

    sum = 0;
    for (i = 0; i < self->patches->len; i++)
    {
        Patch *p = g_ptr_array_index (self->patches, i);

        sum += (p->columns + 1) * (p->rows + 1);
    }
    return sum;
}

guint
lrg_water_surface_get_triangle_count (LrgWaterSurface *self)
{
    guint sum;
    guint i;

    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), 0);

    sum = 0;
    for (i = 0; i < self->patches->len; i++)
    {
        Patch *p = g_ptr_array_index (self->patches, i);

        sum += patch_triangles (p);
    }
    return sum;
}

const gfloat *
lrg_water_surface_get_patch_foam (LrgWaterSurface *self,
                                  guint            patch,
                                  guint           *n_values)
{
    Patch *p;

    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), NULL);

    if (patch >= self->patches->len)
    {
        if (n_values != NULL)
            *n_values = 0;
        return NULL;
    }
    p = g_ptr_array_index (self->patches, patch);
    if (n_values != NULL)
        *n_values = (p->columns + 1) * (p->rows + 1);
    return p->foam;
}

/* --- GPU --- */

static gboolean
ensure_shader (LrgWaterSurface *self)
{
    if (self->shader_tried)
        return self->shader_ok;
    if (!IsWindowReady ())
        return FALSE;
    self->shader_tried = TRUE;

    self->shader = LoadShaderFromMemory (water_vs, water_fs);
    if (self->shader.id == 0 || self->shader.id == rlGetShaderIdDefault ())
    {
        g_warning ("water surface: the shader did not compile (GLSL 330 needed)");
        return FALSE;
    }
    self->loc_wave_count = GetShaderLocation (self->shader, "waveCount");
    self->loc_wave_a = GetShaderLocation (self->shader, "waveA");
    self->loc_wave_b = GetShaderLocation (self->shader, "waveB");
    self->loc_time = GetShaderLocation (self->shader, "time");
    self->loc_lit = GetShaderLocation (self->shader, "lit");
    self->loc_view = GetShaderLocation (self->shader, "viewPos");
    self->loc_shallow = GetShaderLocation (self->shader, "shallowColor");
    self->loc_deep = GetShaderLocation (self->shader, "deepColor");
    self->loc_foam = GetShaderLocation (self->shader, "foamColor");
    self->loc_horizon = GetShaderLocation (self->shader, "skyHorizon");
    self->loc_zenith = GetShaderLocation (self->shader, "skyZenith");
    self->loc_sun_dir = GetShaderLocation (self->shader, "sunDir");
    self->loc_sun_color = GetShaderLocation (self->shader, "sunColor");
    self->loc_optics = GetShaderLocation (self->shader, "optics");
    self->loc_has_scene = GetShaderLocation (self->shader, "hasScene");
    self->loc_screen = GetShaderLocation (self->shader, "screenSize");
    self->loc_view_proj = GetShaderLocation (self->shader, "viewProj");
    self->shader.locs[SHADER_LOC_MAP_DIFFUSE + SCENE_MAP] = GetShaderLocation (self->shader, "sceneTex");
    lrg_light_set_sample_locations (self->shader, &self->light_locs);
    lrg_light_set_fog_locations (self->shader, &self->fog_locs);
    self->material = LoadMaterialDefault ();
    self->material.shader = self->shader;
    self->shader_ok = TRUE;
    return TRUE;
}

/* builds and uploads one patch's raylib mesh */
static void
upload_patch (Patch *patch)
{
    guint n;
    guint r;
    guint c;
    guint t;
    guint i;

    n = (patch->columns + 1) * (patch->rows + 1);
    memset (&patch->mesh, 0, sizeof (patch->mesh));
    patch->mesh.vertexCount = (gint)n;
    patch->mesh.triangleCount = (gint)patch_triangles (patch);
    patch->mesh.vertices = MemAlloc (sizeof (gfloat) * 3 * n);
    patch->mesh.texcoords = MemAlloc (sizeof (gfloat) * 2 * n);
    patch->mesh.colors = MemAlloc (4 * n);
    patch->mesh.indices = MemAlloc (sizeof (gushort) * 3 * (gsize)patch->mesh.triangleCount);
    memcpy (patch->mesh.vertices, patch->positions, sizeof (gfloat) * 3 * n);
    for (i = 0; i < n; i++)
    {
        patch->mesh.texcoords[i * 2 + 0] = patch->positions[i * 3 + 0];
        patch->mesh.texcoords[i * 2 + 1] = patch->positions[i * 3 + 2];
        patch->mesh.colors[i * 4 + 0] = (guchar)(patch->foam[i] * 255.0f + 0.5f);
        patch->mesh.colors[i * 4 + 1] = 0;
        patch->mesh.colors[i * 4 + 2] = 0;
        patch->mesh.colors[i * 4 + 3] = 255;
    }
    t = 0;
    for (r = 0; r < patch->rows; r++)
    {
        for (c = 0; c < patch->columns; c++)
        {
            gushort a = (gushort)(r * (patch->columns + 1) + c);
            gushort b = (gushort)(a + 1);
            gushort d = (gushort)(a + patch->columns + 1);
            gushort e = (gushort)(d + 1);

            /* a heightfield draws its wet cells only */
            if (patch->cells != NULL && patch->cells[r * patch->columns + c] == 0)
                continue;
            patch->mesh.indices[t++] = a;
            patch->mesh.indices[t++] = d;
            patch->mesh.indices[t++] = e;
            patch->mesh.indices[t++] = a;
            patch->mesh.indices[t++] = e;
            patch->mesh.indices[t++] = b;
        }
    }
    UploadMesh (&patch->mesh, FALSE);
    patch->uploaded = TRUE;
}

/* raylib's MatrixMultiply (@a applied first, then @b) without
 * raymath.h (not C89): by element number m0 .. m15 */
static void
mat_elems (const Matrix *m,
           gfloat       *v)
{
    v[0] = m->m0;   v[1] = m->m1;   v[2] = m->m2;   v[3] = m->m3;
    v[4] = m->m4;   v[5] = m->m5;   v[6] = m->m6;   v[7] = m->m7;
    v[8] = m->m8;   v[9] = m->m9;   v[10] = m->m10; v[11] = m->m11;
    v[12] = m->m12; v[13] = m->m13; v[14] = m->m14; v[15] = m->m15;
}

static Matrix
mat_mul (Matrix a,
         Matrix b)
{
    gfloat x[16];
    gfloat y[16];
    gfloat o[16];
    Matrix m;
    guint i;
    guint j;

    mat_elems (&a, x);
    mat_elems (&b, y);
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            o[i * 4 + j] = x[i * 4 + 0] * y[j] + x[i * 4 + 1] * y[4 + j] +
                           x[i * 4 + 2] * y[8 + j] + x[i * 4 + 3] * y[12 + j];
    m.m0 = o[0];   m.m1 = o[1];   m.m2 = o[2];   m.m3 = o[3];
    m.m4 = o[4];   m.m5 = o[5];   m.m6 = o[6];   m.m7 = o[7];
    m.m8 = o[8];   m.m9 = o[9];   m.m10 = o[10]; m.m11 = o[11];
    m.m12 = o[12]; m.m13 = o[13]; m.m14 = o[14]; m.m15 = o[15];
    return m;
}

static Matrix
mat_identity (void)
{
    Matrix m;

    memset (&m, 0, sizeof (m));
    m.m0 = 1.0f;
    m.m5 = 1.0f;
    m.m10 = 1.0f;
    m.m15 = 1.0f;
    return m;
}

gboolean
lrg_water_surface_capture_scene (LrgWaterSurface *self)
{
    guint source;
    gint w;
    gint h;

    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), FALSE);

    if (!IsWindowReady ())
        return FALSE;
    rlDrawRenderBatchActive ();
    w = rlGetFramebufferWidth ();
    h = rlGetFramebufferHeight ();
    if (w <= 0 || h <= 0)
        return FALSE;
    if (self->scene.id != 0 && (self->scene.texture.width != w || self->scene.texture.height != h))
    {
        UnloadRenderTexture (self->scene);
        memset (&self->scene, 0, sizeof (self->scene));
    }
    if (self->scene.id == 0)
    {
        self->scene = LoadRenderTexture (w, h);
        if (self->scene.id == 0)
            return FALSE;
        SetTextureFilter (self->scene.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap (self->scene.texture, TEXTURE_WRAP_CLAMP);
    }
    source = rlGetActiveFramebuffer ();
    rlBindFramebuffer (RL_READ_FRAMEBUFFER, source);
    rlBindFramebuffer (RL_DRAW_FRAMEBUFFER, self->scene.id);
    rlBlitFramebuffer (0, 0, w, h, 0, 0, w, h, GL_COLOR_BIT);
    rlBindFramebuffer (GL_FRAMEBUFFER_BIND, source);
    self->scene_valid = TRUE;
    return TRUE;
}

void
lrg_water_surface_drop_scene (LrgWaterSurface *self)
{
    g_return_if_fail (LRG_IS_WATER_SURFACE (self));
    self->scene_valid = FALSE;
}

gboolean
lrg_water_surface_draw (LrgWaterSurface *self,
                        const gfloat    *eye,
                        gfloat           time)
{
    LrgLightSet *lights;
    LrgDrawFrustum frustum;
    gboolean cull;
    gfloat pad_y;
    gfloat pad_xz;
    Matrix view_proj;
    gfloat wa[LRG_WATER_SURFACE_MAX_WAVES * 4];
    gfloat wb[LRG_WATER_SURFACE_MAX_WAVES * 4];
    gfloat screen[2];
    gint count;
    gint lit;
    gint has_scene;
    guint i;

    g_return_val_if_fail (LRG_IS_WATER_SURFACE (self), FALSE);
    g_return_val_if_fail (eye != NULL, FALSE);

    if (self->patches->len == 0)
        return TRUE;
    if (!ensure_shader (self))
        return FALSE;

    rlDrawRenderBatchActive ();
    for (i = 0; i < self->patches->len; i++)
    {
        Patch *p = g_ptr_array_index (self->patches, i);

        if (!p->uploaded)
            upload_patch (p);
    }

    memset (wa, 0, sizeof (wa));
    memset (wb, 0, sizeof (wb));
    for (i = 0; i < self->n_waves; i++)
        wave_params (self, i, wa + i * 4, wb + i * 4);
    count = (gint)self->n_waves;
    screen[0] = (gfloat)rlGetFramebufferWidth ();
    screen[1] = (gfloat)rlGetFramebufferHeight ();
    view_proj = mat_mul (rlGetMatrixModelview (), rlGetMatrixProjection ());
    has_scene = self->scene_valid && self->scene.id != 0 ? 1 : 0;
    self->material.maps[SCENE_MAP].texture = has_scene ? self->scene.texture : (Texture2D){ 0 };

    lights = lrg_light_set_get_bound ();
    rlEnableShader (self->shader.id);
    SetShaderValue (self->shader, self->loc_wave_count, &count, SHADER_UNIFORM_INT);
    SetShaderValueV (self->shader, self->loc_wave_a, wa, SHADER_UNIFORM_VEC4, LRG_WATER_SURFACE_MAX_WAVES);
    SetShaderValueV (self->shader, self->loc_wave_b, wb, SHADER_UNIFORM_VEC4, LRG_WATER_SURFACE_MAX_WAVES);
    SetShaderValue (self->shader, self->loc_time, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue (self->shader, self->loc_view, eye, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_shallow, self->shallow, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_deep, self->deep, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_foam, self->foam, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_horizon, self->horizon, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_zenith, self->zenith, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_sun_dir, self->sun_dir, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_sun_color, self->sun_color, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_optics, self->optics, SHADER_UNIFORM_VEC4);
    SetShaderValue (self->shader, self->loc_has_scene, &has_scene, SHADER_UNIFORM_INT);
    SetShaderValue (self->shader, self->loc_screen, screen, SHADER_UNIFORM_VEC2);
    rlSetUniformMatrix (self->loc_view_proj, view_proj);
    lit = lrg_light_set_upload_sample (lights, self->shader, &self->light_locs) ? 1 : 0;
    SetShaderValue (self->shader, self->loc_lit, &lit, SHADER_UNIFORM_INT);
    lrg_light_set_upload_fog (lights, self->shader, &self->fog_locs);

    /* translucent without a scene copy; seen from both sides; never
     * hides what is drawn after it */
    rlSetBlendMode (BLEND_ALPHA);
    rlEnableColorBlend ();
    rlDisableBackfaceCulling ();
    rlDisableDepthMask ();
    /* patches wholly outside the frustum draw nothing; the waves move a
     * vertex at most the sum of the amplitudes up or down and the sum of
     * Q x amplitude sideways, so the boxes grow by that much */
    cull = lrg_draw_cull_frustum_current (&frustum);
    pad_y = 0.0f;
    pad_xz = 0.0f;
    for (i = 0; i < self->n_waves; i++)
    {
        pad_y += fabsf (wa[i * 4 + 2]);
        pad_xz += fabsf (wb[i * 4 + 1] * wa[i * 4 + 2]);
    }
    if (!isfinite (pad_y) || !isfinite (pad_xz))
        cull = FALSE;
    for (i = 0; i < self->patches->len; i++)
    {
        Patch *p = g_ptr_array_index (self->patches, i);

        if (cull)
        {
            gfloat lo[3];
            gfloat hi[3];

            lo[0] = p->min[0] - pad_xz;
            lo[1] = p->min[1] - pad_y;
            lo[2] = p->min[2] - pad_xz;
            hi[0] = p->max[0] + pad_xz;
            hi[1] = p->max[1] + pad_y;
            hi[2] = p->max[2] + pad_xz;
            if (!lrg_draw_cull_frustum_box_visible (&frustum, lo, hi))
                continue;
        }
        DrawMesh (p->mesh, self->material, mat_identity ());
    }
    rlEnableDepthMask ();
    rlEnableBackfaceCulling ();
    self->material.maps[SCENE_MAP].texture = (Texture2D){ 0 };
    /* the copy belongs to the frame it was taken in */
    self->scene_valid = FALSE;
    return TRUE;
}

void
lrg_water_surface_release_gpu (LrgWaterSurface *self)
{
    guint i;

    g_return_if_fail (LRG_IS_WATER_SURFACE (self));

    for (i = 0; i < self->patches->len; i++)
    {
        Patch *p = g_ptr_array_index (self->patches, i);

        if (p->uploaded)
        {
            UnloadMesh (p->mesh);
            memset (&p->mesh, 0, sizeof (p->mesh));
            p->uploaded = FALSE;
        }
    }
    if (self->shader_ok)
    {
        self->material.maps[SCENE_MAP].texture = (Texture2D){ 0 };
        MemFree (self->material.maps);
        self->material.maps = NULL;
        UnloadShader (self->shader);
    }
    memset (&self->shader, 0, sizeof (self->shader));
    self->shader_ok = FALSE;
    self->shader_tried = FALSE;
    if (self->scene.id != 0)
        UnloadRenderTexture (self->scene);
    memset (&self->scene, 0, sizeof (self->scene));
    self->scene_valid = FALSE;
}

/* --- prewarm --- */

/* lrg_water_surface_prewarm: what the first draw builds (the shader and
 * every patch's mesh), built now */
static gboolean
lrg_water_surface_prewarm (LrgPrewarmable *prewarmable)
{
    LrgWaterSurface *self = LRG_WATER_SURFACE (prewarmable);
    guint            i;

    /* the shader even without patches: they may come later */
    if (!ensure_shader (self))
        return TRUE;
    for (i = 0; i < self->patches->len; i++)
    {
        Patch *p = g_ptr_array_index (self->patches, i);

        if (!p->uploaded)
            upload_patch (p);
    }
    return TRUE;
}

static void
lrg_water_surface_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_water_surface_prewarm;
}
