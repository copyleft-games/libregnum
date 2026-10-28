/* lrg-shadow-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shadow maps for LrgLightSet.
 *
 * Planning (headless): update() splits the camera's view up to the
 * shadow distance into cascades (the practical split scheme), fits each
 * slice's bounding sphere with an orthographic light camera looking
 * along the directional light, and snaps that camera to whole texels so
 * the shadow edges do not crawl while the camera moves. It also picks
 * the point lights that get a cube (priority, then distance) from the
 * light set.
 *
 * Rendering (GL): both atlases are a 32-bit float colour texture plus a
 * depth renderbuffer on a framebuffer of their own. The sun atlas holds
 * the cascades in a 2 x 2 (or 1 x 2, 1 x 1) layout and stores the
 * window depth of the light's orthographic view; the point atlas holds
 * three by two faces per light, one light above the other, and stores
 * the distance to the light over its radius. Face culling is off while
 * casters draw, so walls cast from both sides (Daggerfall's models are
 * one sided). The lookups are LRG_SHADOW_GLSL in the light set's shader.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lighting/lrg-shadow-renderer.h"
#include "lighting/lrg-shadow-renderer-private.h"
#include "graphics/lrg-light-set-private.h"
#include "core/lrg-prewarm.h"

/* how far behind the fitted sphere the light camera starts, so tall
 * casters outside the view still throw their shadow into it */
#define CASTER_REACH_FACTOR (2.0f)
#define CASTER_REACH_MIN    (60.0f)
#define MAX_PASSES          (LRG_SHADOW_MAX_CASCADES + 6 * LRG_SHADOW_MAX_POINT_LIGHTS)

typedef struct
{
    guint   fbo;
    guint   color;
    guint   depth;
    gint    width;
    gint    height;
    gboolean cleared;
} Atlas;

struct _LrgShadowRenderer
{
    GObject  parent_instance;

    /* settings */
    guint    sun_resolution;
    guint    cascades;
    gfloat   distance;
    gfloat   lambda;
    guint    point_resolution;
    guint    max_points;
    gfloat   point_distance;
    gfloat   depth_bias;
    gfloat   normal_offset;
    guint    filter;
    gfloat   strength;

    /* the plan of the last update */
    guint    n_cascades;
    gfloat   split_near[LRG_SHADOW_MAX_CASCADES];
    gfloat   split_far[LRG_SHADOW_MAX_CASCADES];
    gfloat   view[LRG_SHADOW_MAX_CASCADES][16];
    gfloat   proj[LRG_SHADOW_MAX_CASCADES][16];
    gfloat   view_proj[LRG_SHADOW_MAX_CASCADES][16];
    gfloat   texel_world[LRG_SHADOW_MAX_CASCADES];
    gfloat   depth_per_world[LRG_SHADOW_MAX_CASCADES];
    guint    n_points;
    guint    point_ids[LRG_SHADOW_MAX_POINT_LIGHTS];
    gfloat   point_pos[LRG_SHADOW_MAX_POINT_LIGHTS][3];
    gfloat   point_radius[LRG_SHADOW_MAX_POINT_LIGHTS];
    guint64  rendered;      /* bit per pass rendered since the update */

    /* the pass cache: what each pass's tile holds and how old it is */
    guint    refresh;       /* 0: render every pass every frame */
    guint64  current;       /* bit per pass whose tile is still good */
    gboolean cache_valid[MAX_PASSES];
    gfloat   cache_vp[MAX_PASSES][16];
    gfloat   cache_light[MAX_PASSES][4];
    guint    cache_age[MAX_PASSES];

    /* GPU */
    Atlas      sun;
    Atlas      point;
    GrlShader *caster;
    gboolean   caster_tried;
    gint       loc_mode;
    gint       loc_light;
    gboolean   in_pass;
    guint      pass;
    gint       pass_mode;       /* 0 cascade, 1 cube face */
    gfloat     pass_light[4];   /* cube: position, radius */
    gfloat     pass_dir[3];     /* cascade: the way the light travels */
};

static void lrg_shadow_renderer_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgShadowRenderer, lrg_shadow_renderer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                lrg_shadow_renderer_prewarmable_init))

G_DEFINE_ENUM_TYPE (LrgShadowPassKind, lrg_shadow_pass_kind,
                    G_DEFINE_ENUM_VALUE (LRG_SHADOW_PASS_NONE, "none"),
                    G_DEFINE_ENUM_VALUE (LRG_SHADOW_PASS_CASCADE, "cascade"),
                    G_DEFINE_ENUM_VALUE (LRG_SHADOW_PASS_CUBE_FACE, "cube-face"))

/* The caster: raylib's names, so DrawMesh() feeds mvp, matModel and the
 * diffuse map; mode 0 writes the window depth (cascades), mode 1 the
 * distance to the light over its radius (cube faces). */
static const gchar caster_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "out vec2 fragTexCoord;\n"
    "out vec3 fragPosition;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const gchar caster_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec3 fragPosition;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform int casterMode;\n"
    "uniform vec4 casterLight;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    if (texture(texture0, fragTexCoord).a * colDiffuse.a < 0.5) discard;\n"
    "    float d = casterMode == 1 ? length(fragPosition - casterLight.xyz) / casterLight.w\n"
    "                              : gl_FragCoord.z;\n"
    "    finalColor = vec4(d, 0.0, 0.0, 1.0);\n"
    "}\n";

/* ------------------------------------------------------------------------ */
/* Matrix helpers                                                           */
/* ------------------------------------------------------------------------ */

static void
vec_sub (const gfloat *a, const gfloat *b, gfloat *out)
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static void
vec_cross (const gfloat *a, const gfloat *b, gfloat *out)
{
    gfloat r[3];

    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
    memcpy (out, r, sizeof r);
}

static gfloat
vec_dot (const gfloat *a, const gfloat *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* normalizes in place; FALSE (left alone) for a zero or broken vector */
static gboolean
vec_normalize (gfloat *v)
{
    gfloat len = sqrtf (vec_dot (v, v));

    if (!(len > 1e-8f) || !isfinite (len))
        return FALSE;
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
    return TRUE;
}

static void
identity (gfloat *m)
{
    memset (m, 0, 16 * sizeof (gfloat));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void
lrg_shadow_look_at (const gfloat *eye,
                    const gfloat *target,
                    const gfloat *up,
                    gfloat       *out_view)
{
    gfloat vx[3];
    gfloat vy[3];
    gfloat vz[3];

    g_return_if_fail (eye != NULL && target != NULL && up != NULL && out_view != NULL);

    /* raylib's MatrixLookAt(): z points from the target to the eye */
    vec_sub (eye, target, vz);
    if (!vec_normalize (vz))
    {
        vz[0] = 0.0f;
        vz[1] = 0.0f;
        vz[2] = 1.0f;
    }
    vec_cross (up, vz, vx);
    if (!vec_normalize (vx))
    {
        gfloat other[3] = { 1.0f, 0.0f, 0.0f };

        if (fabsf (vz[0]) > 0.9f)
        {
            other[0] = 0.0f;
            other[1] = 0.0f;
            other[2] = 1.0f;
        }
        vec_cross (other, vz, vx);
        vec_normalize (vx);
    }
    vec_cross (vz, vx, vy);

    identity (out_view);
    out_view[0] = vx[0];
    out_view[4] = vx[1];
    out_view[8] = vx[2];
    out_view[1] = vy[0];
    out_view[5] = vy[1];
    out_view[9] = vy[2];
    out_view[2] = vz[0];
    out_view[6] = vz[1];
    out_view[10] = vz[2];
    out_view[12] = -vec_dot (vx, eye);
    out_view[13] = -vec_dot (vy, eye);
    out_view[14] = -vec_dot (vz, eye);
}

void
lrg_shadow_ortho (gfloat  left,
                  gfloat  right,
                  gfloat  bottom,
                  gfloat  top,
                  gfloat  near,
                  gfloat  far,
                  gfloat *out_proj)
{
    g_return_if_fail (out_proj != NULL);

    identity (out_proj);
    if (right == left || top == bottom || far == near)
        return;
    out_proj[0] = 2.0f / (right - left);
    out_proj[5] = 2.0f / (top - bottom);
    out_proj[10] = -2.0f / (far - near);
    out_proj[12] = -(right + left) / (right - left);
    out_proj[13] = -(top + bottom) / (top - bottom);
    out_proj[14] = -(far + near) / (far - near);
}

void
lrg_shadow_perspective (gfloat  fovy_degrees,
                        gfloat  aspect,
                        gfloat  near,
                        gfloat  far,
                        gfloat *out_proj)
{
    gfloat f;

    g_return_if_fail (out_proj != NULL);

    identity (out_proj);
    if (!(near > 0.0f) || !(far > near) || !(aspect > 0.0f))
        return;
    f = 1.0f / tanf (fovy_degrees * (gfloat)G_PI / 360.0f);
    out_proj[0] = f / aspect;
    out_proj[5] = f;
    out_proj[10] = (far + near) / (near - far);
    out_proj[11] = -1.0f;
    out_proj[14] = 2.0f * far * near / (near - far);
    out_proj[15] = 0.0f;
}

void
lrg_shadow_multiply (const gfloat *a,
                     const gfloat *b,
                     gfloat       *out)
{
    gfloat r[16];
    guint  c;
    guint  row;
    guint  k;

    g_return_if_fail (a != NULL && b != NULL && out != NULL);

    for (c = 0; c < 4; c++)
    {
        for (row = 0; row < 4; row++)
        {
            gfloat s = 0.0f;

            for (k = 0; k < 4; k++)
                s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    }
    memcpy (out, r, sizeof r);
}

void
lrg_shadow_transform_point (const gfloat *m,
                            const gfloat *point,
                            gfloat       *out)
{
    gfloat r[4];
    guint  row;

    g_return_if_fail (m != NULL && point != NULL && out != NULL);

    for (row = 0; row < 4; row++)
        r[row] = m[row] * point[0] + m[4 + row] * point[1] + m[8 + row] * point[2] + m[12 + row];
    if (r[3] != 0.0f && isfinite (r[3]))
    {
        r[0] /= r[3];
        r[1] /= r[3];
        r[2] /= r[3];
    }
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
}

gboolean
lrg_shadow_split_distances (gfloat  near,
                            gfloat  far,
                            guint   count,
                            gfloat  lambda,
                            gfloat *out_splits)
{
    guint i;

    g_return_val_if_fail (out_splits != NULL, FALSE);

    if (!(near > 0.0f) || !(far > near) || !isfinite (far) || count == 0
        || count > LRG_SHADOW_MAX_CASCADES)
        return FALSE;
    lambda = CLAMP (isfinite (lambda) ? lambda : 0.5f, 0.0f, 1.0f);

    /* C_i = lambda * n (f/n)^(i/N) + (1 - lambda) (n + (f - n) i/N) */
    for (i = 1; i <= count; i++)
    {
        gfloat t = (gfloat)i / (gfloat)count;
        gfloat log_split = near * powf (far / near, t);
        gfloat uni_split = near + (far - near) * t;

        out_splits[i - 1] = lambda * log_split + (1.0f - lambda) * uni_split;
    }
    out_splits[count - 1] = far;
    return TRUE;
}

/* the cube faces' view direction and up, mirrored by LRG_SHADOW_GLSL */
static const gfloat face_forward[6][3] = {
    { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
};
static const gfloat face_up[6][3] = {
    { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
    { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }
};

gboolean
lrg_shadow_cube_face_matrices (guint         face,
                               const gfloat *light,
                               gfloat        radius,
                               gfloat       *out_view,
                               gfloat       *out_proj)
{
    gfloat target[3];
    guint  k;

    g_return_val_if_fail (light != NULL && out_view != NULL && out_proj != NULL, FALSE);

    if (face > 5)
        return FALSE;
    for (k = 0; k < 3; k++)
        target[k] = light[k] + face_forward[face][k];
    lrg_shadow_look_at (light, target, face_up[face], out_view);
    lrg_shadow_perspective (90.0f, 1.0f, MAX (radius * 0.002f, 0.02f), MAX (radius, 0.1f),
                            out_proj);
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Lifecycle and settings                                                   */
/* ------------------------------------------------------------------------ */

static void
atlas_free (Atlas *atlas)
{
    /* GL objects only while the context lives */
    if (IsWindowReady ())
    {
        if (atlas->fbo != 0)
            rlUnloadFramebuffer (atlas->fbo);
        if (atlas->color != 0)
            rlUnloadTexture (atlas->color);
    }
    memset (atlas, 0, sizeof *atlas);
}

static void
lrg_shadow_renderer_finalize (GObject *object)
{
    LrgShadowRenderer *self = LRG_SHADOW_RENDERER (object);

    atlas_free (&self->sun);
    atlas_free (&self->point);
    g_clear_object (&self->caster);

    G_OBJECT_CLASS (lrg_shadow_renderer_parent_class)->finalize (object);
}

static void
lrg_shadow_renderer_class_init (LrgShadowRendererClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_shadow_renderer_finalize;
}

static void
lrg_shadow_renderer_init (LrgShadowRenderer *self)
{
    self->sun_resolution = 2048;
    self->cascades = 1;
    self->distance = 80.0f;
    self->lambda = 0.6f;
    self->point_resolution = 256;
    self->max_points = 0;
    self->depth_bias = 0.05f;
    self->normal_offset = 1.5f;
    self->filter = 1;
    self->strength = 1.0f;

    /* build the GPU resources behind a loading screen, not on the
     * first draw (see lrg_prewarm_track()) */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgShadowRenderer *
lrg_shadow_renderer_new (void)
{
    return g_object_new (LRG_TYPE_SHADOW_RENDERER, NULL);
}

void
lrg_shadow_renderer_set_sun (LrgShadowRenderer *self,
                             guint              resolution,
                             guint              cascades,
                             gfloat             distance,
                             gfloat             split_lambda)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));
    g_return_if_fail (resolution == 0 || (resolution >= 64 && resolution <= 4096));
    g_return_if_fail (cascades >= 1 && cascades <= LRG_SHADOW_MAX_CASCADES);
    g_return_if_fail (distance > 0.0f && isfinite (distance));

    self->sun_resolution = resolution;
    self->cascades = cascades;
    self->distance = distance;
    self->lambda = CLAMP (isfinite (split_lambda) ? split_lambda : 0.6f, 0.0f, 1.0f);
}

void
lrg_shadow_renderer_get_sun (LrgShadowRenderer *self,
                             guint             *out_resolution,
                             guint             *out_cascades,
                             gfloat            *out_distance)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    if (out_resolution != NULL)
        *out_resolution = self->sun_resolution;
    if (out_cascades != NULL)
        *out_cascades = self->cascades;
    if (out_distance != NULL)
        *out_distance = self->distance;
}

void
lrg_shadow_renderer_set_points (LrgShadowRenderer *self,
                                guint              resolution,
                                guint              max_lights,
                                gfloat             max_distance)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));
    g_return_if_fail (resolution >= 32 && resolution <= 2048);
    g_return_if_fail (max_lights <= LRG_SHADOW_MAX_POINT_LIGHTS);
    g_return_if_fail (max_distance >= 0.0f);

    self->point_resolution = resolution;
    self->max_points = max_lights;
    self->point_distance = isfinite (max_distance) ? max_distance : 0.0f;
}

void
lrg_shadow_renderer_get_points (LrgShadowRenderer *self,
                                guint             *out_resolution,
                                guint             *out_max_lights)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    if (out_resolution != NULL)
        *out_resolution = self->point_resolution;
    if (out_max_lights != NULL)
        *out_max_lights = self->max_points;
}

void
lrg_shadow_renderer_set_bias (LrgShadowRenderer *self,
                              gfloat             depth_bias,
                              gfloat             normal_offset)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    self->depth_bias = isfinite (depth_bias) ? MAX (depth_bias, 0.0f) : 0.0f;
    self->normal_offset = isfinite (normal_offset) ? MAX (normal_offset, 0.0f) : 0.0f;
}

void
lrg_shadow_renderer_set_filter (LrgShadowRenderer *self,
                                guint              radius)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    self->filter = MIN (radius, 2u);
}

void
lrg_shadow_renderer_set_strength (LrgShadowRenderer *self,
                                  gfloat             strength)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    self->strength = CLAMP (isfinite (strength) ? strength : 1.0f, 0.0f, 1.0f);
}

gfloat
lrg_shadow_renderer_get_strength (LrgShadowRenderer *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), 0.0f);

    return self->strength;
}

/* ------------------------------------------------------------------------ */
/* Planning                                                                 */
/* ------------------------------------------------------------------------ */

/*
 * fit_cascade:
 * The orthographic light camera of the view slice @near .. @far: the
 * slice's eight corners, their bounding sphere (radius rounded up to
 * 1/16 so it does not breathe with the camera's turn), a camera behind
 * the sphere along @light_dir far enough to catch casters outside it,
 * snapped to whole texels.
 */
static void
fit_cascade (LrgShadowRenderer *self,
             guint              index,
             const gfloat      *camera,
             const gfloat      *forward,
             const gfloat      *right,
             const gfloat      *up,
             gfloat             tan_half,
             gfloat             aspect,
             gfloat             near,
             gfloat             far,
             const gfloat      *light_dir)
{
    gfloat corners[8][3];
    gfloat centre[3] = { 0.0f, 0.0f, 0.0f };
    gfloat radius = 0.0f;
    gfloat reach;
    gfloat eye[3];
    gfloat light_up[3] = { 0.0f, 1.0f, 0.0f };
    gfloat origin[3] = { 0.0f, 0.0f, 0.0f };
    gfloat snapped[3];
    gfloat half_res;
    guint  c;
    guint  k;

    /* the slice's corners */
    for (c = 0; c < 8; c++)
    {
        gfloat d = (c & 4) ? far : near;
        gfloat h = d * tan_half;
        gfloat w = h * aspect;
        gfloat sx = (c & 1) ? 1.0f : -1.0f;
        gfloat sy = (c & 2) ? 1.0f : -1.0f;

        for (k = 0; k < 3; k++)
            corners[c][k] = camera[k] + forward[k] * d + right[k] * w * sx + up[k] * h * sy;
    }
    for (c = 0; c < 8; c++)
        for (k = 0; k < 3; k++)
            centre[k] += corners[c][k] / 8.0f;
    for (c = 0; c < 8; c++)
    {
        gfloat d[3];

        vec_sub (corners[c], centre, d);
        radius = MAX (radius, sqrtf (vec_dot (d, d)));
    }
    radius = ceilf (radius * 16.0f) / 16.0f;
    radius = MAX (radius, 0.5f);

    /* the light camera: behind the sphere, looking along the light */
    reach = MAX (radius * CASTER_REACH_FACTOR, CASTER_REACH_MIN);
    for (k = 0; k < 3; k++)
        eye[k] = centre[k] - light_dir[k] * (radius + reach);
    if (fabsf (light_dir[1]) > 0.99f)
    {
        light_up[0] = 0.0f;
        light_up[1] = 0.0f;
        light_up[2] = 1.0f;
    }
    lrg_shadow_look_at (eye, centre, light_up, self->view[index]);
    lrg_shadow_ortho (-radius, radius, -radius, radius, 0.0f, 2.0f * radius + reach,
                      self->proj[index]);

    /* texel snapping: move the projection so the world origin lands on a
     * texel corner; every point then keeps its texel while the camera
     * moves by whole texels */
    lrg_shadow_multiply (self->proj[index], self->view[index], self->view_proj[index]);
    lrg_shadow_transform_point (self->view_proj[index], origin, snapped);
    half_res = (gfloat)self->sun_resolution * 0.5f;
    self->proj[index][12] += (roundf (snapped[0] * half_res) - snapped[0] * half_res) / half_res;
    self->proj[index][13] += (roundf (snapped[1] * half_res) - snapped[1] * half_res) / half_res;
    lrg_shadow_multiply (self->proj[index], self->view[index], self->view_proj[index]);

    self->split_near[index] = near;
    self->split_far[index] = far;
    self->texel_world[index] = 2.0f * radius / (gfloat)self->sun_resolution;
    self->depth_per_world[index] = 1.0f / (2.0f * radius + reach);
}

static LrgShadowPassKind
pass_kind (LrgShadowRenderer *self,
           guint              pass,
           guint             *index,
           guint             *face);

/* the light a pass renders for (the cube's position and radius; zero
 * for a cascade), part of the cache key */
static void
pass_light (LrgShadowRenderer *self,
            guint              pass,
            gfloat            *out)
{
    guint index;
    guint face;

    memset (out, 0, 4 * sizeof (gfloat));
    if (pass_kind (self, pass, &index, &face) == LRG_SHADOW_PASS_CUBE_FACE)
    {
        memcpy (out, self->point_pos[index], 3 * sizeof (gfloat));
        out[3] = self->point_radius[index];
    }
}

guint
lrg_shadow_renderer_update (LrgShadowRenderer *self,
                            LrgLightSet       *lights,
                            const gfloat      *camera,
                            const gfloat      *forward,
                            const gfloat      *up,
                            gfloat             fovy_degrees,
                            gfloat             aspect,
                            gfloat             near)
{
    LrgLightSetShadowCandidate picked[LRG_SHADOW_MAX_POINT_LIGHTS];
    gdouble origin[3];
    gdouble camera_world[3];
    gfloat  cam[3];
    gfloat  fwd[3];
    gfloat  right[3];
    gfloat  cup[3];
    gfloat  light_dir[3];
    gfloat  splits[LRG_SHADOW_MAX_CASCADES];
    guint   i;
    guint   k;

    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), 0);
    g_return_val_if_fail (LRG_IS_LIGHT_SET (lights), 0);
    g_return_val_if_fail (camera != NULL && forward != NULL && up != NULL, 0);

    self->n_cascades = 0;
    self->n_points = 0;
    self->rendered = 0;
    self->current = 0;

    lrg_light_set_get_origin (lights, &origin[0], &origin[1], &origin[2]);
    for (k = 0; k < 3; k++)
    {
        camera_world[k] = (gdouble)camera[k];
        cam[k] = (gfloat)((gdouble)camera[k] - origin[k]);
        fwd[k] = forward[k];
        cup[k] = up[k];
    }

    /* 1. the cascades of the directional light, in draw space */
    if (self->sun_resolution > 0 && lrg_light_set_get_directional (lights, light_dir, NULL)
        && vec_normalize (fwd) && near > 0.0f && aspect > 0.0f
        && fovy_degrees > 0.0f && fovy_degrees < 180.0f
        && near < self->distance)
    {
        vec_cross (fwd, cup, right);
        if (!vec_normalize (right))
        {
            gfloat other[3] = { 0.0f, 0.0f, 1.0f };

            vec_cross (fwd, other, right);
            vec_normalize (right);
        }
        vec_cross (right, fwd, cup);
        vec_normalize (cup);

        if (lrg_shadow_split_distances (near, self->distance, self->cascades, self->lambda, splits))
        {
            gfloat tan_half = tanf (fovy_degrees * (gfloat)G_PI / 360.0f);
            gfloat slice_near = near;

            for (i = 0; i < self->cascades; i++)
            {
                fit_cascade (self, i, cam, fwd, right, cup, tan_half, aspect, slice_near,
                             splits[i], light_dir);
                slice_near = splits[i];
            }
            self->n_cascades = self->cascades;
        }
    }

    /* 2. the point lights with a cube */
    if (self->max_points > 0)
    {
        self->n_points = lrg_light_set_pick_shadow_lights (lights, camera_world,
                                                           self->point_distance,
                                                           self->max_points, picked);
        for (i = 0; i < self->n_points; i++)
        {
            self->point_ids[i] = picked[i].id;
            memcpy (self->point_pos[i], picked[i].position, sizeof (self->point_pos[i]));
            self->point_radius[i] = picked[i].radius;
        }
    }
    /* 3. the cache: a pass whose camera and light are what its tile was
     * rendered with, younger than the refresh, need not be drawn again.
     * A pass that is only stale by age (nothing moved) is staggered: the
     * passes of a still view all come due on the same frame (the moon's
     * cascades and the torch's six faces at midnight), so at most
     * ceil (passes / refresh) of them render per frame, oldest first, and
     * the rest keep their tile for up to twice the refresh. */
    {
        guint n = lrg_shadow_renderer_get_pass_count (self);
        guint pass;
        guint due[MAX_PASSES];
        guint n_due = 0;
        guint budget;
        guint k;

        for (pass = 0; pass < n && pass < MAX_PASSES; pass++)
        {
            gfloat vp[16];
            gfloat light[4];

            pass_light (self, pass, light);
            lrg_shadow_renderer_get_pass (self, pass, NULL, NULL, vp);
            if (self->cache_valid[pass] && self->cache_age[pass] < G_MAXUINT)
                self->cache_age[pass]++;
            if (self->refresh > 0 && self->cache_valid[pass]
                && memcmp (vp, self->cache_vp[pass], sizeof vp) == 0
                && memcmp (light, self->cache_light[pass], sizeof light) == 0)
            {
                /* (64-bit: twice the refresh of LRG_SHADOW_REFRESH_NEVER
                 * would wrap) */
                if (self->refresh == LRG_SHADOW_REFRESH_NEVER
                    || self->cache_age[pass] < self->refresh)
                    self->current |= G_GUINT64_CONSTANT (1) << pass;
                else if ((guint64)self->cache_age[pass] < 2 * (guint64)self->refresh)
                    due[n_due++] = pass;
            }
        }
        /* the oldest first (a stable insertion sort of a few entries) */
        for (k = 1; k < n_due; k++)
        {
            guint v = due[k];
            guint j = k;

            while (j > 0 && self->cache_age[due[j - 1]] < self->cache_age[v])
            {
                due[j] = due[j - 1];
                j--;
            }
            due[j] = v;
        }
        budget = self->refresh > 0
                 ? (guint)MAX ((guint64)1, ((guint64)n + self->refresh - 1) / self->refresh)
                 : 0;
        for (k = budget; k < n_due; k++)
            self->current |= G_GUINT64_CONSTANT (1) << due[k];
    }
    return lrg_shadow_renderer_get_pass_count (self);
}

guint
lrg_shadow_renderer_get_cascade_count (LrgShadowRenderer *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), 0);

    return self->n_cascades;
}

gboolean
lrg_shadow_renderer_get_cascade (LrgShadowRenderer *self,
                                 guint              index,
                                 gfloat            *out_near,
                                 gfloat            *out_far,
                                 gfloat            *out_matrix)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), FALSE);

    if (index >= self->n_cascades)
        return FALSE;
    if (out_near != NULL)
        *out_near = self->split_near[index];
    if (out_far != NULL)
        *out_far = self->split_far[index];
    if (out_matrix != NULL)
        memcpy (out_matrix, self->view_proj[index], 16 * sizeof (gfloat));
    return TRUE;
}

guint
lrg_shadow_renderer_get_point_count (LrgShadowRenderer *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), 0);

    return self->n_points;
}

guint
lrg_shadow_renderer_get_point_light (LrgShadowRenderer *self,
                                     guint              slot)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), G_MAXUINT);

    return slot < self->n_points ? self->point_ids[slot] : G_MAXUINT;
}

gint
lrg_shadow_renderer_get_light_slot (LrgShadowRenderer *self,
                                    guint              light)
{
    guint i;

    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), LRG_SHADOW_NO_SLOT);

    for (i = 0; i < self->n_points; i++)
        if (self->point_ids[i] == light)
            return (gint)i;
    return LRG_SHADOW_NO_SLOT;
}

guint
lrg_shadow_renderer_get_pass_count (LrgShadowRenderer *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), 0);

    return self->n_cascades + 6 * self->n_points;
}

/* the matrices of point pass @slot / @face */
static void
point_pass_matrices (LrgShadowRenderer *self,
                     guint              slot,
                     guint              face,
                     gfloat            *view,
                     gfloat            *proj)
{
    lrg_shadow_cube_face_matrices (face, self->point_pos[slot], self->point_radius[slot],
                                   view, proj);
}

static LrgShadowPassKind
pass_kind (LrgShadowRenderer *self,
           guint              pass,
           guint             *index,
           guint             *face)
{
    if (pass < self->n_cascades)
    {
        *index = pass;
        *face = 0;
        return LRG_SHADOW_PASS_CASCADE;
    }
    pass -= self->n_cascades;
    if (pass < 6 * self->n_points)
    {
        *index = pass / 6;
        *face = pass % 6;
        return LRG_SHADOW_PASS_CUBE_FACE;
    }
    return LRG_SHADOW_PASS_NONE;
}

LrgShadowPassKind
lrg_shadow_renderer_get_pass (LrgShadowRenderer *self,
                              guint              pass,
                              guint             *out_index,
                              guint             *out_face,
                              gfloat            *out_view_proj)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), LRG_SHADOW_PASS_NONE);

    if (pass < self->n_cascades)
    {
        if (out_index != NULL)
            *out_index = pass;
        if (out_face != NULL)
            *out_face = 0;
        if (out_view_proj != NULL)
            memcpy (out_view_proj, self->view_proj[pass], 16 * sizeof (gfloat));
        return LRG_SHADOW_PASS_CASCADE;
    }
    pass -= self->n_cascades;
    if (pass < 6 * self->n_points)
    {
        guint slot = pass / 6;
        guint face = pass % 6;

        if (out_index != NULL)
            *out_index = slot;
        if (out_face != NULL)
            *out_face = face;
        if (out_view_proj != NULL)
        {
            gfloat view[16];
            gfloat proj[16];

            point_pass_matrices (self, slot, face, view, proj);
            lrg_shadow_multiply (proj, view, out_view_proj);
        }
        return LRG_SHADOW_PASS_CUBE_FACE;
    }
    return LRG_SHADOW_PASS_NONE;
}

/* ------------------------------------------------------------------------ */
/* GPU                                                                      */
/* ------------------------------------------------------------------------ */

GrlShader *
lrg_shadow_renderer_get_caster_shader (LrgShadowRenderer *self)
{
    g_autoptr(GError) error = NULL;
    Shader *handle;

    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), NULL);

    if (self->caster != NULL || self->caster_tried)
        return self->caster;
    if (!IsWindowReady ())
        return NULL;
    self->caster_tried = TRUE;

    self->caster = grl_shader_new_from_memory (caster_vs, caster_fs, &error);
    if (self->caster == NULL)
        return NULL;
    handle = grl_shader_get_handle (self->caster);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
    {
        g_warning ("shadow renderer: the caster shader did not compile (GLSL 330 needed)");
        g_clear_object (&self->caster);
        return NULL;
    }
    self->loc_mode = GetShaderLocation (*handle, "casterMode");
    self->loc_light = GetShaderLocation (*handle, "casterLight");
    return self->caster;
}

/*
 * atlas_ensure:
 * A framebuffer of @width x @height with a float colour texture (point
 * sampled, clamped) and a depth renderbuffer; a size change makes a new
 * one. FALSE when the framebuffer is incomplete (left freed).
 */
static gboolean
atlas_ensure (Atlas *atlas,
              gint   width,
              gint   height)
{
    if (atlas->fbo != 0 && atlas->width == width && atlas->height == height)
        return TRUE;
    atlas_free (atlas);

    atlas->fbo = rlLoadFramebuffer ();
    if (atlas->fbo == 0)
        return FALSE;
    atlas->color = rlLoadTexture (NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    atlas->depth = rlLoadTextureDepth (width, height, true);
    if (atlas->color == 0 || atlas->depth == 0)
    {
        atlas_free (atlas);
        return FALSE;
    }
    rlTextureParameters (atlas->color, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters (atlas->color, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters (atlas->color, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
    rlTextureParameters (atlas->color, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    rlFramebufferAttach (atlas->fbo, atlas->color, RL_ATTACHMENT_COLOR_CHANNEL0,
                         RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach (atlas->fbo, atlas->depth, RL_ATTACHMENT_DEPTH,
                         RL_ATTACHMENT_RENDERBUFFER, 0);
    if (!rlFramebufferComplete (atlas->fbo))
    {
        g_warning ("shadow renderer: a %d x %d shadow atlas is not renderable", width, height);
        atlas_free (atlas);
        return FALSE;
    }
    rlDisableFramebuffer ();
    atlas->width = width;
    atlas->height = height;
    atlas->cleared = FALSE;
    return TRUE;
}

/* forgets every cached tile (a new atlas holds nothing) */
static void
drop_cache (LrgShadowRenderer *self)
{
    memset (self->cache_valid, 0, sizeof self->cache_valid);
    self->current = 0;
}

/* the sun atlas: two cascades side by side, a second row for 3 and 4 */
static void
sun_layout (LrgShadowRenderer *self,
            guint             *out_cols,
            guint             *out_rows)
{
    *out_cols = MIN (self->cascades, 2u);
    *out_rows = (self->cascades + 1) / 2;
}

static Matrix
to_matrix (const gfloat *m)
{
    Matrix r;

    r.m0 = m[0]; r.m1 = m[1]; r.m2 = m[2]; r.m3 = m[3];
    r.m4 = m[4]; r.m5 = m[5]; r.m6 = m[6]; r.m7 = m[7];
    r.m8 = m[8]; r.m9 = m[9]; r.m10 = m[10]; r.m11 = m[11];
    r.m12 = m[12]; r.m13 = m[13]; r.m14 = m[14]; r.m15 = m[15];
    return r;
}

gboolean
lrg_shadow_renderer_begin_pass (LrgShadowRenderer *self,
                                guint              pass)
{
    LrgShadowPassKind kind;
    Shader *handle;
    Atlas  *atlas;
    gfloat  view[16];
    gfloat  proj[16];
    gfloat  light[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    guint   index;
    guint   face;
    gint    mode;
    gint    x;
    gint    y;
    gint    res;

    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), FALSE);

    if (self->in_pass)
        lrg_shadow_renderer_end_pass (self);
    kind = lrg_shadow_renderer_get_pass (self, pass, &index, &face, NULL);
    if (kind == LRG_SHADOW_PASS_NONE || lrg_shadow_renderer_get_caster_shader (self) == NULL)
        return FALSE;

    if (kind == LRG_SHADOW_PASS_CASCADE)
    {
        guint cols;
        guint rows;

        sun_layout (self, &cols, &rows);
        res = (gint)self->sun_resolution;
        atlas = &self->sun;
        if (atlas->fbo == 0 || atlas->width != res * (gint)cols || atlas->height != res * (gint)rows)
            drop_cache (self);
        if (!atlas_ensure (atlas, res * (gint)cols, res * (gint)rows))
            return FALSE;
        x = (gint)(index % 2) * res;
        y = (gint)(index / 2) * res;
        memcpy (view, self->view[index], sizeof view);
        memcpy (proj, self->proj[index], sizeof proj);
        mode = 0;
    }
    else
    {
        res = (gint)self->point_resolution;
        atlas = &self->point;
        if (atlas->fbo == 0 || atlas->width != res * 3
            || atlas->height != res * 2 * (gint)self->max_points)
            drop_cache (self);
        if (!atlas_ensure (atlas, res * 3, res * 2 * (gint)self->max_points))
            return FALSE;
        x = (gint)(face % 3) * res;
        y = (gint)(index * 2 + face / 3) * res;
        point_pass_matrices (self, index, face, view, proj);
        memcpy (light, self->point_pos[index], 3 * sizeof (gfloat));
        light[3] = self->point_radius[index];
        mode = 1;
    }

    /* nothing may sample the atlases while they are drawn into */
    rlDrawRenderBatchActive ();
    rlActiveTextureSlot (LRG_SHADOW_SUN_UNIT);
    rlDisableTexture ();
    rlActiveTextureSlot (LRG_SHADOW_POINT_UNIT);
    rlDisableTexture ();
    rlActiveTextureSlot (0);

    rlEnableFramebuffer (atlas->fbo);
    rlClearColor (255, 255, 255, 255);
    if (!atlas->cleared)
    {
        /* a new atlas: far everywhere (1 is the farthest depth and a
         * full radius) */
        rlViewport (0, 0, atlas->width, atlas->height);
        rlClearScreenBuffers ();
        atlas->cleared = TRUE;
    }
    /* this pass's tile only: the others may hold cached passes */
    rlViewport (x, y, res, res);
    rlEnableScissorTest ();
    rlScissor (x, y, res, res);
    rlClearScreenBuffers ();
    rlDisableScissorTest ();
    rlSetMatrixProjection (to_matrix (proj));
    rlSetMatrixModelview (to_matrix (view));
    rlEnableDepthTest ();
    rlEnableDepthMask ();
    rlDisableBackfaceCulling ();

    /* what a caster outside the caster shader (a billboard batch's own
     * program) needs to write the same numbers */
    self->pass_mode = mode;
    memcpy (self->pass_light, light, sizeof self->pass_light);
    self->pass_dir[0] = -view[2];
    self->pass_dir[1] = -view[6];
    self->pass_dir[2] = -view[10];
    handle = grl_shader_get_handle (self->caster);
    SetShaderValue (*handle, self->loc_mode, &mode, SHADER_UNIFORM_INT);
    SetShaderValue (*handle, self->loc_light, light, SHADER_UNIFORM_VEC4);
    self->in_pass = TRUE;
    self->pass = pass;
    return TRUE;
}

gboolean
lrg_shadow_renderer_get_pass_state (LrgShadowRenderer *self,
                                    gint              *out_mode,
                                    gfloat            *out_light,
                                    gfloat            *out_direction)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), FALSE);

    if (!self->in_pass)
        return FALSE;
    if (out_mode != NULL)
        *out_mode = self->pass_mode;
    if (out_light != NULL)
        memcpy (out_light, self->pass_light, sizeof self->pass_light);
    if (out_direction != NULL)
        memcpy (out_direction, self->pass_dir, sizeof self->pass_dir);
    return TRUE;
}

void
lrg_shadow_renderer_end_pass (LrgShadowRenderer *self)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    if (!self->in_pass)
        return;
    rlDrawRenderBatchActive ();
    rlEnableBackfaceCulling ();
    rlDisableDepthTest ();
    /* the screen, its viewport and 2D matrices, as after a texture mode */
    EndTextureMode ();
    self->in_pass = FALSE;
    lrg_shadow_renderer_mark_rendered (self, self->pass);
}

void
lrg_shadow_renderer_mark_rendered (LrgShadowRenderer *self,
                                   guint              pass)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    if (pass >= MAX_PASSES || pass >= lrg_shadow_renderer_get_pass_count (self))
        return;
    self->rendered |= G_GUINT64_CONSTANT (1) << pass;
    /* what the tile holds now */
    self->cache_valid[pass] = TRUE;
    self->cache_age[pass] = 0;
    lrg_shadow_renderer_get_pass (self, pass, NULL, NULL, self->cache_vp[pass]);
    pass_light (self, pass, self->cache_light[pass]);
}

gboolean
lrg_shadow_renderer_is_ready (LrgShadowRenderer *self)
{
    guint   count;
    guint64 all;

    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), FALSE);

    count = lrg_shadow_renderer_get_pass_count (self);
    if (count == 0)
        return FALSE;
    all = count >= 64 ? G_MAXUINT64 : (G_GUINT64_CONSTANT (1) << count) - 1;
    return ((self->rendered | self->current) & all) == all;
}

void
lrg_shadow_renderer_release_gpu (LrgShadowRenderer *self)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    if (self->in_pass)
        lrg_shadow_renderer_end_pass (self);
    atlas_free (&self->sun);
    atlas_free (&self->point);
    g_clear_object (&self->caster);
    self->caster_tried = FALSE;
    self->rendered = 0;
    drop_cache (self);
}

void
lrg_shadow_renderer_set_refresh (LrgShadowRenderer *self,
                                 guint              frames)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    self->refresh = frames;
    if (frames == 0)
        drop_cache (self);
}

guint
lrg_shadow_renderer_get_refresh (LrgShadowRenderer *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), 0);

    return self->refresh;
}

gboolean
lrg_shadow_renderer_pass_is_current (LrgShadowRenderer *self,
                                     guint              pass)
{
    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), FALSE);

    if (pass >= lrg_shadow_renderer_get_pass_count (self) || pass >= MAX_PASSES)
        return FALSE;
    return (self->current & (G_GUINT64_CONSTANT (1) << pass)) != 0;
}

void
lrg_shadow_renderer_invalidate (LrgShadowRenderer *self)
{
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (self));

    drop_cache (self);
}

/* box_outside: whether the box @lo .. @hi lies wholly outside the clip
 * volume of the column-major @view_proj: all eight corners beyond one
 * of its six planes (each plane is linear in the world point, whatever
 * the sign of w, so this holds for the cube faces' perspective too) */
static gboolean
box_outside (const gfloat *view_proj,
             const gfloat *lo,
             const gfloat *hi)
{
    guint outside[6] = { 0, 0, 0, 0, 0, 0 };
    guint c;
    guint plane;

    for (c = 0; c < 8; c++)
    {
        gfloat p[3];
        gfloat clip[4];
        guint  row;

        p[0] = (c & 1) ? hi[0] : lo[0];
        p[1] = (c & 2) ? hi[1] : lo[1];
        p[2] = (c & 4) ? hi[2] : lo[2];
        for (row = 0; row < 4; row++)
            clip[row] = view_proj[row] * p[0] + view_proj[4 + row] * p[1]
                        + view_proj[8 + row] * p[2] + view_proj[12 + row];
        if (clip[0] > clip[3])
            outside[0]++;
        if (clip[0] < -clip[3])
            outside[1]++;
        if (clip[1] > clip[3])
            outside[2]++;
        if (clip[1] < -clip[3])
            outside[3]++;
        if (clip[2] > clip[3])
            outside[4]++;
        if (clip[2] < -clip[3])
            outside[5]++;
    }
    for (plane = 0; plane < 6; plane++)
        if (outside[plane] == 8)
            return TRUE;
    return FALSE;
}

guint
lrg_shadow_renderer_invalidate_box (LrgShadowRenderer *self,
                                    const gfloat      *min,
                                    const gfloat      *max)
{
    guint dropped = 0;
    guint pass;

    g_return_val_if_fail (LRG_IS_SHADOW_RENDERER (self), 0);
    g_return_val_if_fail (min != NULL && max != NULL, 0);

    /* a box with a NaN (or an inverted one) cannot be placed: every
     * tile may hold it */
    if (!(min[0] <= max[0] && min[1] <= max[1] && min[2] <= max[2]))
    {
        for (pass = 0; pass < MAX_PASSES; pass++)
            if (self->cache_valid[pass])
                dropped++;
        drop_cache (self);
        return dropped;
    }

    /* the tiles whose light camera (as rendered) sees part of the box */
    for (pass = 0; pass < MAX_PASSES; pass++)
    {
        if (!self->cache_valid[pass] || box_outside (self->cache_vp[pass], min, max))
            continue;
        self->cache_valid[pass] = FALSE;
        self->current &= ~(G_GUINT64_CONSTANT (1) << pass);
        dropped++;
    }
    return dropped;
}

/* ------------------------------------------------------------------------ */
/* The light shader's side (lrg-shadow-renderer-private.h)                  */
/* ------------------------------------------------------------------------ */

void
lrg_shadow_locations (Shader              shader,
                      LrgShadowLocations *out_locs)
{
    gint unit;

    out_locs->cascades = GetShaderLocation (shader, "shadowCascades");
    out_locs->matrices = GetShaderLocation (shader, "shadowMatrix");
    out_locs->tiles = GetShaderLocation (shader, "shadowTile");
    out_locs->cascade_info = GetShaderLocation (shader, "shadowCascadeInfo");
    out_locs->params = GetShaderLocation (shader, "shadowParams");
    out_locs->filter = GetShaderLocation (shader, "shadowFilter");
    out_locs->strength = GetShaderLocation (shader, "shadowStrength");
    out_locs->sun_map = GetShaderLocation (shader, "sunShadowMap");
    out_locs->point_count = GetShaderLocation (shader, "pointShadowCount");
    out_locs->point_lights = GetShaderLocation (shader, "pointShadowLight");
    out_locs->point_params = GetShaderLocation (shader, "pointShadowParams");
    out_locs->point_map = GetShaderLocation (shader, "pointShadowMap");

    unit = LRG_SHADOW_SUN_UNIT;
    SetShaderValue (shader, out_locs->sun_map, &unit, SHADER_UNIFORM_INT);
    unit = LRG_SHADOW_POINT_UNIT;
    SetShaderValue (shader, out_locs->point_map, &unit, SHADER_UNIFORM_INT);
}

void
lrg_shadow_renderer_upload (LrgShadowRenderer        *self,
                            Shader                    shader,
                            const LrgShadowLocations *locs)
{
    gint   cascades = 0;
    gint   points = 0;
    gint   filter;
    guint  i;

    if (self == NULL || !lrg_shadow_renderer_is_ready (self))
    {
        SetShaderValue (shader, locs->cascades, &cascades, SHADER_UNIFORM_INT);
        SetShaderValue (shader, locs->point_count, &points, SHADER_UNIFORM_INT);
        return;
    }

    filter = (gint)self->filter;
    SetShaderValue (shader, locs->filter, &filter, SHADER_UNIFORM_INT);
    SetShaderValue (shader, locs->strength, &self->strength, SHADER_UNIFORM_FLOAT);

    if (self->n_cascades > 0 && self->sun.color != 0)
    {
        gfloat matrices[LRG_SHADOW_MAX_CASCADES * 16];
        Matrix mats[LRG_SHADOW_MAX_CASCADES];
        gfloat tiles[LRG_SHADOW_MAX_CASCADES * 4];
        gfloat info[LRG_SHADOW_MAX_CASCADES * 4];
        gfloat params[4];
        guint  cols;
        guint  rows;

        sun_layout (self, &cols, &rows);
        for (i = 0; i < self->n_cascades; i++)
        {
            memcpy (matrices + i * 16, self->view_proj[i], 16 * sizeof (gfloat));
            tiles[i * 4 + 0] = (gfloat)(i % 2) / (gfloat)cols;
            tiles[i * 4 + 1] = (gfloat)(i / 2) / (gfloat)rows;
            tiles[i * 4 + 2] = 1.0f / (gfloat)cols;
            tiles[i * 4 + 3] = 1.0f / (gfloat)rows;
            info[i * 4 + 0] = self->texel_world[i];
            info[i * 4 + 1] = self->depth_per_world[i];
            info[i * 4 + 2] = 1.0f / (gfloat)self->sun_resolution;
            info[i * 4 + 3] = 0.0f;
        }
        params[0] = 1.0f / (gfloat)self->sun.width;
        params[1] = 1.0f / (gfloat)self->sun.height;
        params[2] = self->depth_bias;
        params[3] = self->normal_offset;
        cascades = (gint)self->n_cascades;
        /* mat4 arrays go through glUniformMatrix4fv; raylib's Matrix
         * names its fields, so each is converted by name */
        for (i = 0; i < self->n_cascades; i++)
            mats[i] = to_matrix (matrices + i * 16);
        rlEnableShader (shader.id);
        rlSetUniformMatrices (locs->matrices, mats, (int)self->n_cascades);
        SetShaderValueV (shader, locs->tiles, tiles, SHADER_UNIFORM_VEC4, cascades);
        SetShaderValueV (shader, locs->cascade_info, info, SHADER_UNIFORM_VEC4, cascades);
        SetShaderValue (shader, locs->params, params, SHADER_UNIFORM_VEC4);
        rlActiveTextureSlot (LRG_SHADOW_SUN_UNIT);
        rlEnableTexture (self->sun.color);
        rlActiveTextureSlot (0);
    }
    else
    {
        gfloat params[4];

        params[0] = 0.0f;
        params[1] = 0.0f;
        params[2] = self->depth_bias;
        params[3] = self->normal_offset;
        SetShaderValue (shader, locs->params, params, SHADER_UNIFORM_VEC4);
    }
    SetShaderValue (shader, locs->cascades, &cascades, SHADER_UNIFORM_INT);

    if (self->n_points > 0 && self->point.color != 0)
    {
        gfloat lights[LRG_SHADOW_MAX_POINT_LIGHTS * 4];
        gfloat params[4];

        for (i = 0; i < self->n_points; i++)
        {
            memcpy (lights + i * 4, self->point_pos[i], 3 * sizeof (gfloat));
            lights[i * 4 + 3] = self->point_radius[i];
        }
        params[0] = (gfloat)self->point_resolution;
        params[1] = (gfloat)self->point.width;
        params[2] = (gfloat)self->point.height;
        params[3] = self->normal_offset;
        points = (gint)self->n_points;
        SetShaderValueV (shader, locs->point_lights, lights, SHADER_UNIFORM_VEC4, points);
        SetShaderValue (shader, locs->point_params, params, SHADER_UNIFORM_VEC4);
        rlActiveTextureSlot (LRG_SHADOW_POINT_UNIT);
        rlEnableTexture (self->point.color);
        rlActiveTextureSlot (0);
    }
    SetShaderValue (shader, locs->point_count, &points, SHADER_UNIFORM_INT);
}

/* ------------------------------------------------------------------------ */
/* Prewarm                                                                  */
/* ------------------------------------------------------------------------ */

/* lrg_shadow_renderer_prewarm: compiles the caster shader now (what the
 * first pass would do). The atlases are left to the first pass: their
 * size follows the lights that cast at the time. */
static gboolean
lrg_shadow_renderer_prewarm (LrgPrewarmable *prewarmable)
{
    lrg_shadow_renderer_get_caster_shader (LRG_SHADOW_RENDERER (prewarmable));
    return TRUE;
}

static void
lrg_shadow_renderer_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_shadow_renderer_prewarm;
}
