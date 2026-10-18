/* lrg-sky-bodies.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Everything is emitted through rlgl's immediate batch around the eye
 * (no GPU state).  Star directions are generated once per (count, seed)
 * and rotated on the CPU when the rotation changes.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-sky-bodies.h"
#include "weather/lrg-celestial.h"

#define BODIES_DEG2RAD     (G_PI / 180.0)
#define BODIES_SEGMENTS    (32)
#define BODIES_MOON_ROWS   (24)
#define BODIES_HALO_SCALE  (4.0f)
#define BODIES_HALO_ALPHA  (0.35f)
/* angular half size of the brightest star, degrees */
#define BODIES_STAR_SIZE   (0.16f)
/* stars sit a little further out than the moons and the sun */
#define BODIES_STAR_DIST   (0.97f)
#define BODIES_BODY_DIST   (0.93f)

typedef struct
{
    gboolean shown;
    gfloat   dir[3];
    gfloat   size;
    guint8   color[4];
    gdouble  phase;
} Moon;

struct _LrgSkyBodies
{
    GObject  parent_instance;

    gfloat   radius;

    gfloat   sun_dir[3];
    gfloat   sun_size;
    guint8   sun_color[4];

    Moon     moons[LRG_SKY_BODIES_MAX_MOONS];
    guint8   dark[4];

    guint    n_stars;
    guint32  star_seed;
    gfloat  *star_base;     /* 3 x n, fixed to the star sphere */
    gfloat  *star_dir;      /* 3 x n, rotated */
    gfloat  *star_bright;
    gdouble  star_angle;
    gdouble  star_tilt;
    gboolean stars_dirty;
    gfloat   star_visibility;
};

G_DEFINE_FINAL_TYPE (LrgSkyBodies, lrg_sky_bodies, G_TYPE_OBJECT)

static void
lrg_sky_bodies_finalize (GObject *object)
{
    LrgSkyBodies *self = LRG_SKY_BODIES (object);

    g_free (self->star_base);
    g_free (self->star_dir);
    g_free (self->star_bright);
    G_OBJECT_CLASS (lrg_sky_bodies_parent_class)->finalize (object);
}

static void
lrg_sky_bodies_class_init (LrgSkyBodiesClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_sky_bodies_finalize;
}

static void
lrg_sky_bodies_init (LrgSkyBodies *self)
{
    self->radius = 500.0f;
    self->sun_dir[1] = 1.0f;
    self->sun_color[0] = 255; self->sun_color[1] = 244; self->sun_color[2] = 214;
    self->sun_color[3] = 255;
    self->dark[0] = 18; self->dark[1] = 20; self->dark[2] = 30; self->dark[3] = 230;
}

LrgSkyBodies *
lrg_sky_bodies_new (void)
{
    return g_object_new (LRG_TYPE_SKY_BODIES, NULL);
}

void
lrg_sky_bodies_set_radius (LrgSkyBodies *self,
                           gfloat        radius)
{
    g_return_if_fail (LRG_IS_SKY_BODIES (self));

    if (isfinite (radius) && radius > 0.0f)
        self->radius = radius;
}

/* normalize_into:
 * Copies a non-zero, finite vector normalized; FALSE otherwise. */
static gboolean
normalize_into (const GrlVector3 *v,
                gfloat           *out)
{
    gfloat len;

    if (v == NULL || !isfinite (v->x) || !isfinite (v->y) || !isfinite (v->z))
        return FALSE;
    len = sqrtf (v->x * v->x + v->y * v->y + v->z * v->z);
    if (len < 1e-6f)
        return FALSE;
    out[0] = v->x / len;
    out[1] = v->y / len;
    out[2] = v->z / len;
    return TRUE;
}

void
lrg_sky_bodies_set_sun (LrgSkyBodies     *self,
                        const GrlVector3 *towards_sun,
                        gfloat            size_degrees,
                        const GrlColor   *color)
{
    g_return_if_fail (LRG_IS_SKY_BODIES (self));

    normalize_into (towards_sun, self->sun_dir);
    if (isfinite (size_degrees) && size_degrees >= 0.0f)
        self->sun_size = size_degrees;
    if (color != NULL)
    {
        self->sun_color[0] = color->r; self->sun_color[1] = color->g;
        self->sun_color[2] = color->b; self->sun_color[3] = color->a;
    }
}

void
lrg_sky_bodies_set_moon (LrgSkyBodies     *self,
                         guint             index,
                         const GrlVector3 *towards_moon,
                         gfloat            size_degrees,
                         const GrlColor   *color,
                         gdouble           phase)
{
    Moon *m;

    g_return_if_fail (LRG_IS_SKY_BODIES (self));
    g_return_if_fail (color != NULL);

    if (index >= LRG_SKY_BODIES_MAX_MOONS)
        return;
    m = &self->moons[index];
    if (!normalize_into (towards_moon, m->dir) || !isfinite (size_degrees) ||
        size_degrees <= 0.0f || !isfinite (phase))
        return;
    m->size = size_degrees;
    m->color[0] = color->r; m->color[1] = color->g; m->color[2] = color->b;
    m->color[3] = color->a;
    m->phase = phase - floor (phase);
    m->shown = TRUE;
}

void
lrg_sky_bodies_hide_moon (LrgSkyBodies *self,
                          guint         index)
{
    g_return_if_fail (LRG_IS_SKY_BODIES (self));

    if (index < LRG_SKY_BODIES_MAX_MOONS)
        self->moons[index].shown = FALSE;
}

gboolean
lrg_sky_bodies_get_moon (LrgSkyBodies *self,
                         guint         index,
                         gdouble      *out_phase,
                         GrlVector3   *out_direction)
{
    const Moon *m;

    g_return_val_if_fail (LRG_IS_SKY_BODIES (self), FALSE);

    if (index >= LRG_SKY_BODIES_MAX_MOONS || !self->moons[index].shown)
        return FALSE;
    m = &self->moons[index];
    if (out_phase != NULL)
        *out_phase = m->phase;
    if (out_direction != NULL)
    {
        out_direction->x = m->dir[0];
        out_direction->y = m->dir[1];
        out_direction->z = m->dir[2];
    }
    return TRUE;
}

void
lrg_sky_bodies_set_moon_dark_color (LrgSkyBodies   *self,
                                    const GrlColor *color)
{
    g_return_if_fail (LRG_IS_SKY_BODIES (self));
    g_return_if_fail (color != NULL);

    self->dark[0] = color->r; self->dark[1] = color->g; self->dark[2] = color->b;
    self->dark[3] = color->a;
}

void
lrg_sky_bodies_set_stars (LrgSkyBodies *self,
                          guint         count,
                          guint32       seed)
{
    g_return_if_fail (LRG_IS_SKY_BODIES (self));

    count = MIN (count, (guint)LRG_SKY_BODIES_MAX_STARS);
    if (count == self->n_stars && seed == self->star_seed && self->star_base != NULL)
        return;

    g_clear_pointer (&self->star_base, g_free);
    g_clear_pointer (&self->star_dir, g_free);
    g_clear_pointer (&self->star_bright, g_free);
    self->n_stars = count;
    self->star_seed = seed;
    if (count == 0)
        return;
    self->star_base = g_new (gfloat, (gsize)count * 3);
    self->star_dir = g_new (gfloat, (gsize)count * 3);
    self->star_bright = g_new (gfloat, count);
    lrg_celestial_generate_stars (seed, count, self->star_base, self->star_bright);
    self->stars_dirty = TRUE;
}

void
lrg_sky_bodies_set_star_rotation (LrgSkyBodies *self,
                                  gdouble       angle,
                                  gdouble       tilt_degrees)
{
    g_return_if_fail (LRG_IS_SKY_BODIES (self));

    if (!isfinite (angle) || !isfinite (tilt_degrees))
        return;
    if (angle != self->star_angle || tilt_degrees != self->star_tilt)
    {
        self->star_angle = angle;
        self->star_tilt = tilt_degrees;
        self->stars_dirty = TRUE;
    }
}

void
lrg_sky_bodies_set_star_visibility (LrgSkyBodies *self,
                                    gfloat        visibility)
{
    g_return_if_fail (LRG_IS_SKY_BODIES (self));

    if (isfinite (visibility))
        self->star_visibility = CLAMP (visibility, 0.0f, 1.0f);
}

gfloat
lrg_sky_bodies_get_star_visibility (LrgSkyBodies *self)
{
    g_return_val_if_fail (LRG_IS_SKY_BODIES (self), 0.0f);
    return self->star_visibility;
}

guint
lrg_sky_bodies_get_star_count (LrgSkyBodies *self)
{
    g_return_val_if_fail (LRG_IS_SKY_BODIES (self), 0);
    return self->n_stars;
}

/* rotate_stars:
 * Brings the rotated directions up to date. */
static void
rotate_stars (LrgSkyBodies *self)
{
    guint i;

    if (!self->stars_dirty || self->star_base == NULL)
        return;
    for (i = 0; i < self->n_stars; i++)
        lrg_celestial_rotate_sky (&self->star_base[i * 3], self->star_angle, self->star_tilt,
                                  &self->star_dir[i * 3]);
    self->stars_dirty = FALSE;
}

guint
lrg_sky_bodies_count_visible_stars (LrgSkyBodies *self)
{
    guint i;
    guint n = 0;

    g_return_val_if_fail (LRG_IS_SKY_BODIES (self), 0);

    if (self->star_visibility <= 0.0f)
        return 0;
    rotate_stars (self);
    for (i = 0; i < self->n_stars; i++)
        if (self->star_dir[i * 3 + 1] > 0.0f)
            n++;
    return n;
}

gboolean
lrg_sky_bodies_get_star (LrgSkyBodies *self,
                         guint         index,
                         GrlVector3   *out_direction,
                         gfloat       *out_brightness)
{
    g_return_val_if_fail (LRG_IS_SKY_BODIES (self), FALSE);
    g_return_val_if_fail (out_direction != NULL, FALSE);

    if (index >= self->n_stars)
        return FALSE;
    rotate_stars (self);
    out_direction->x = self->star_dir[index * 3 + 0];
    out_direction->y = self->star_dir[index * 3 + 1];
    out_direction->z = self->star_dir[index * 3 + 2];
    if (out_brightness != NULL)
        *out_brightness = self->star_bright[index];
    return TRUE;
}

/* basis:
 * Unit right and up vectors across a direction; right leans towards
 * @lean (the sun, for moons) when given. */
static void
basis (const gfloat *dir,
       const gfloat *lean,
       gfloat       *right,
       gfloat       *up)
{
    gfloat len;
    guint  k;

    right[0] = 0.0f; right[1] = 0.0f; right[2] = 0.0f;
    if (lean != NULL)
    {
        gfloat d = lean[0] * dir[0] + lean[1] * dir[1] + lean[2] * dir[2];

        for (k = 0; k < 3; k++)
            right[k] = lean[k] - dir[k] * d;
    }
    len = sqrtf (right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (len < 1e-4f)
    {
        if (fabsf (dir[1]) < 0.99f)
        {
            right[0] = -dir[2]; right[1] = 0.0f; right[2] = dir[0];
        }
        else
        {
            right[0] = 1.0f; right[1] = 0.0f; right[2] = 0.0f;
        }
        len = sqrtf (right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    }
    for (k = 0; k < 3; k++)
        right[k] /= len;
    up[0] = dir[1] * right[2] - dir[2] * right[1];
    up[1] = dir[2] * right[0] - dir[0] * right[2];
    up[2] = dir[0] * right[1] - dir[1] * right[0];
}

static void
vertex (const gfloat *centre,
        const gfloat *right,
        const gfloat *up,
        gfloat        x,
        gfloat        y)
{
    rlVertex3f (centre[0] + right[0] * x + up[0] * y,
                centre[1] + right[1] * x + up[1] * y,
                centre[2] + right[2] * x + up[2] * y);
}

/* disc:
 * A triangle fan of angular radius @degrees around @dir. */
static void
disc (const gfloat *c,
      gfloat        dist,
      const gfloat *dir,
      gfloat        degrees,
      const guint8 *color,
      gfloat        inner_alpha,
      gfloat        rim_alpha)
{
    gfloat ext = dist * tanf ((gfloat)(degrees * BODIES_DEG2RAD));
    gfloat centre[3];
    gfloat right[3];
    gfloat up[3];
    guint  k;

    basis (dir, NULL, right, up);
    for (k = 0; k < 3; k++)
        centre[k] = c[k] + dir[k] * dist;

    rlBegin (RL_TRIANGLES);
    for (k = 0; k < BODIES_SEGMENTS; k++)
    {
        gfloat a0 = (gfloat)k / BODIES_SEGMENTS * 2.0f * (gfloat)G_PI;
        gfloat a1 = (gfloat)(k + 1) / BODIES_SEGMENTS * 2.0f * (gfloat)G_PI;

        rlColor4ub (color[0], color[1], color[2], (guint8)(color[3] * inner_alpha));
        vertex (centre, right, up, 0.0f, 0.0f);
        rlColor4ub (color[0], color[1], color[2], (guint8)(color[3] * rim_alpha));
        vertex (centre, right, up, cosf (a1) * ext, sinf (a1) * ext);
        vertex (centre, right, up, cosf (a0) * ext, sinf (a0) * ext);
    }
    rlEnd ();
}

/* draw_moon:
 * The dark disc, then the lit rows between limb and terminator, the
 * disc's +x facing the sun. */
static void
draw_moon (LrgSkyBodies *self,
           const Moon   *m,
           const gfloat *c)
{
    gfloat dist = self->radius * BODIES_BODY_DIST;
    gfloat ext = dist * tanf ((gfloat)(m->size * BODIES_DEG2RAD));
    gfloat centre[3];
    gfloat right[3];
    gfloat up[3];
    guint8 dark[4];
    guint  r;
    guint  k;

    /* the dark side, as see-through as the moon is, and only against a
     * dark sky (by day the unlit part is the sky's own colour) */
    memcpy (dark, self->dark, 4);
    dark[3] = (guint8)((gfloat)dark[3] * (gfloat)m->color[3] / 255.0f *
                       lrg_celestial_night_factor (self->sun_dir[1]));
    disc (c, dist * 1.001f, m->dir, m->size, dark, 1.0f, 1.0f);

    basis (m->dir, self->sun_dir, right, up);
    for (k = 0; k < 3; k++)
        centre[k] = c[k] + m->dir[k] * dist;

    rlBegin (RL_QUADS);
    rlColor4ub (m->color[0], m->color[1], m->color[2], m->color[3]);
    for (r = 0; r < BODIES_MOON_ROWS; r++)
    {
        gdouble v0 = -1.0 + 2.0 * (gdouble)r / BODIES_MOON_ROWS;
        gdouble v1 = -1.0 + 2.0 * (gdouble)(r + 1) / BODIES_MOON_ROWS;
        gdouble l0;
        gdouble r0;
        gdouble l1;
        gdouble r1;
        gboolean a;
        gboolean b;

        a = lrg_celestial_moon_disc_span (m->phase, v0, &l0, &r0);
        b = lrg_celestial_moon_disc_span (m->phase, v1, &l1, &r1);
        if (!a && !b)
            continue;
        /* a row past the lit part collapses onto the other row's middle
         * (a horn of the crescent) instead of the disc centre */
        if (!a)
        {
            l0 = (l1 + r1) * 0.5;
            r0 = l0;
        }
        if (!b)
        {
            l1 = (l0 + r0) * 0.5;
            r1 = l1;
        }
        vertex (centre, right, up, (gfloat)l0 * ext, (gfloat)v0 * ext);
        vertex (centre, right, up, (gfloat)r0 * ext, (gfloat)v0 * ext);
        vertex (centre, right, up, (gfloat)r1 * ext, (gfloat)v1 * ext);
        vertex (centre, right, up, (gfloat)l1 * ext, (gfloat)v1 * ext);
    }
    rlEnd ();
}

/* draw_stars:
 * Small additive quads, brighter stars larger. */
static void
draw_stars (LrgSkyBodies *self,
            const gfloat *c)
{
    gfloat dist = self->radius * BODIES_STAR_DIST;
    guint  i;

    rotate_stars (self);
    rlSetBlendMode (RL_BLEND_ADDITIVE);
    rlBegin (RL_QUADS);
    for (i = 0; i < self->n_stars; i++)
    {
        const gfloat *d = &self->star_dir[i * 3];
        gfloat        b = self->star_bright[i];
        gfloat        ext;
        gfloat        centre[3];
        gfloat        right[3];
        gfloat        up[3];
        guint8        level;
        guint         k;

        if (d[1] <= 0.0f)
            continue;
        /* stars low in the sky are dimmed by the thick air */
        level = (guint8)(255.0f * b * self->star_visibility * MIN (1.0f, d[1] * 6.0f + 0.2f));
        if (level == 0)
            continue;
        ext = dist * tanf ((gfloat)(BODIES_STAR_SIZE * (0.4f + 0.6f * b) * BODIES_DEG2RAD));
        basis (d, NULL, right, up);
        for (k = 0; k < 3; k++)
            centre[k] = c[k] + d[k] * dist;
        rlColor4ub (level, level, (guint8)MIN (255, level + 10), 255);
        vertex (centre, right, up, -ext, -ext);
        vertex (centre, right, up, ext, -ext);
        vertex (centre, right, up, ext, ext);
        vertex (centre, right, up, -ext, ext);
    }
    rlEnd ();
    rlDrawRenderBatchActive ();
    rlSetBlendMode (RL_BLEND_ALPHA);
}

void
lrg_sky_bodies_draw (LrgSkyBodies     *self,
                     const GrlVector3 *camera_position)
{
    gfloat c[3];
    guint  i;

    g_return_if_fail (LRG_IS_SKY_BODIES (self));
    g_return_if_fail (camera_position != NULL);

    c[0] = camera_position->x;
    c[1] = camera_position->y;
    c[2] = camera_position->z;

    rlDrawRenderBatchActive ();
    rlDisableDepthTest ();
    rlDisableDepthMask ();
    rlDisableBackfaceCulling ();
    rlSetTexture (0);

    if (self->star_visibility > 0.0f && self->n_stars > 0)
        draw_stars (self, c);

    if (self->sun_size > 0.0f && self->sun_color[3] > 0 &&
        self->sun_dir[1] > -sinf ((gfloat)(self->sun_size * BODIES_HALO_SCALE * BODIES_DEG2RAD)))
    {
        disc (c, self->radius * BODIES_BODY_DIST, self->sun_dir,
              self->sun_size * BODIES_HALO_SCALE, self->sun_color, BODIES_HALO_ALPHA, 0.0f);
        disc (c, self->radius * BODIES_BODY_DIST * 0.999f, self->sun_dir, self->sun_size,
              self->sun_color, 1.0f, 1.0f);
    }

    for (i = 0; i < LRG_SKY_BODIES_MAX_MOONS; i++)
    {
        const Moon *m = &self->moons[i];

        if (!m->shown || m->color[3] == 0 ||
            m->dir[1] < -sinf ((gfloat)(m->size * BODIES_DEG2RAD)))
            continue;
        draw_moon (self, m, c);
    }

    rlDrawRenderBatchActive ();
    rlEnableBackfaceCulling ();
    rlEnableDepthMask ();
    rlEnableDepthTest ();
}
