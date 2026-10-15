/* lrg-sky-dome.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Camera-centred sky. The gradient sphere is a small vertex-coloured
 * mesh kept on the CPU (rebuilt when the resolution, radius or colours
 * change) and emitted through rlgl's immediate batch every draw, so
 * there is no GPU state to manage. Depth test and writes are off while
 * the sky draws: it sits behind everything regardless of its radius.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-sky-dome.h"

#define SKY_DEG2RAD       (G_PI / 180.0)
#define SKY_SUN_SEGMENTS  (24)
#define SKY_HALO_SCALE    (4.0f)
#define SKY_HALO_ALPHA    (0.4f)
/* The sun and band sit slightly inside the gradient sphere. */
#define SKY_SUN_DISTANCE  (0.95f)
#define SKY_BAND_DISTANCE (0.9f)

typedef struct
{
    gfloat hours;
    guint8 zenith[4];
    guint8 horizon[4];
    guint8 sun[4];
} SkyKeyframe;

struct _LrgSkyDome
{
    GObject     parent_instance;

    guint       segments;
    guint       rings;
    gfloat      radius;

    guint8      zenith[4];
    guint8      horizon[4];
    guint8      ground[4];
    gboolean    ground_follows;   /* ground colour tracks the horizon */

    GrlTexture *band[2];          /* [1] set only in halves mode */
    gfloat      band_bottom;      /* degrees */
    gfloat      band_top;
    gfloat      band_heading;
    guint8      band_tint[4];

    gfloat      sun_dir[3];
    guint8      sun_color[4];
    gfloat      sun_size;         /* degrees */
    gfloat      sun_tilt;         /* degrees */

    GArray     *keyframes;        /* SkyKeyframe, sorted by hours */
    gfloat      time_of_day;

    /* gradient mesh, rebuilt when geometry_dirty */
    GArray     *positions;        /* gfloat x3 */
    GArray     *colors;           /* guint8 x4 */
    GArray     *indices;          /* guint16 */
    gboolean    geometry_dirty;
};

G_DEFINE_TYPE (LrgSkyDome, lrg_sky_dome, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static void
color_to_bytes (const GrlColor *color,
                guint8         *out)
{
    out[0] = color->r;
    out[1] = color->g;
    out[2] = color->b;
    out[3] = color->a;
}

static void
bytes_to_color (const guint8 *in,
                GrlColor     *color)
{
    color->r = in[0];
    color->g = in[1];
    color->b = in[2];
    color->a = in[3];
}

static void
set_bytes (guint8 *out,
           guint8  r,
           guint8  g,
           guint8  b,
           guint8  a)
{
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = a;
}

/* lerp_bytes:
 * Per-channel linear interpolation, rounded to nearest. */
static void
lerp_bytes (const guint8 *a,
            const guint8 *b,
            gfloat        t,
            guint8       *out)
{
    guint k;

    t = CLAMP (t, 0.0f, 1.0f);
    for (k = 0; k < 4; k++)
        out[k] = (guint8)floorf ((gfloat)a[k] + ((gfloat)b[k] - (gfloat)a[k]) * t + 0.5f);
}

/* gradient_bytes:
 * The sky colour for a unit direction whose Y component is @sin_elev. */
static void
gradient_bytes (LrgSkyDome *self,
                gfloat      sin_elev,
                guint8     *out)
{
    if (sin_elev >= 0.0f)
        lerp_bytes (self->horizon, self->zenith, sin_elev, out);
    else
        lerp_bytes (self->horizon, self->ground_follows ? self->horizon : self->ground,
                    -sin_elev, out);
}

/* azimuth_dir:
 * Horizontal unit direction for an azimuth (radians): 0 = -Z, pi/2 = +X. */
static void
azimuth_dir (gdouble  azimuth,
             gfloat  *x,
             gfloat  *z)
{
    *x = (gfloat)sin (azimuth);
    *z = (gfloat)-cos (azimuth);
}

static gfloat
wrap_hours (gfloat hours)
{
    gfloat h = fmodf (hours, 24.0f);

    if (h < 0.0f)
        h += 24.0f;
    if (h >= 24.0f)
        h = 0.0f;
    return h;
}

/* ------------------------------------------------------------------------ */
/* Gradient geometry                                                        */
/* ------------------------------------------------------------------------ */

/* build_geometry:
 * 2 * rings + 1 latitude rows from the nadir (-90) to the zenith (+90),
 * row `rings` exactly on the horizon; segments + 1 columns per row with
 * the seam duplicated. Quads between rows r (lower) and r + 1 split as
 * (r,s) (r,s+1) (r+1,s) and (r,s+1) (r+1,s+1) (r+1,s): azimuth runs
 * clockwise seen from above, which makes them counter-clockwise seen
 * from the centre. */
static void
build_geometry (LrgSkyDome *self)
{
    guint rows = 2 * self->rings + 1;
    guint cols = self->segments + 1;
    guint r;
    guint s;

    g_array_set_size (self->positions, 0);
    g_array_set_size (self->colors, 0);
    g_array_set_size (self->indices, 0);

    for (r = 0; r < rows; r++)
    {
        gdouble elev = ((gdouble)r / (gdouble)self->rings - 1.0) * (G_PI / 2.0);
        gfloat  sin_e = (gfloat)sin (elev);
        gfloat  cos_e = (gfloat)cos (elev);
        guint8  color[4];

        if (r == self->rings)
        {
            sin_e = 0.0f;
            cos_e = 1.0f;
        }
        else if (r == 0 || r == rows - 1)
        {
            sin_e = r == 0 ? -1.0f : 1.0f;
            cos_e = 0.0f;
        }
        gradient_bytes (self, sin_e, color);

        for (s = 0; s < cols; s++)
        {
            gdouble az = (gdouble)(s % self->segments) / (gdouble)self->segments * 2.0 * G_PI;
            gfloat  p[3];
            gfloat  hx;
            gfloat  hz;

            azimuth_dir (az, &hx, &hz);
            p[0] = hx * cos_e * self->radius;
            p[1] = sin_e * self->radius;
            p[2] = hz * cos_e * self->radius;
            g_array_append_vals (self->positions, p, 3);
            g_array_append_vals (self->colors, color, 4);
        }
    }

    for (r = 0; r + 1 < rows; r++)
    {
        for (s = 0; s < self->segments; s++)
        {
            guint16 a = (guint16)(r * cols + s);
            guint16 b = (guint16)(r * cols + s + 1);
            guint16 c = (guint16)((r + 1) * cols + s);
            guint16 d = (guint16)((r + 1) * cols + s + 1);
            guint16 tri[6];

            tri[0] = a;
            tri[1] = b;
            tri[2] = c;
            tri[3] = b;
            tri[4] = d;
            tri[5] = c;
            g_array_append_vals (self->indices, tri, 6);
        }
    }
    self->geometry_dirty = FALSE;
}

static void
ensure_geometry (LrgSkyDome *self)
{
    if (self->geometry_dirty)
        build_geometry (self);
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

static void
lrg_sky_dome_finalize (GObject *object)
{
    LrgSkyDome *self = LRG_SKY_DOME (object);

    g_clear_object (&self->band[0]);
    g_clear_object (&self->band[1]);
    g_clear_pointer (&self->keyframes, g_array_unref);
    g_clear_pointer (&self->positions, g_array_unref);
    g_clear_pointer (&self->colors, g_array_unref);
    g_clear_pointer (&self->indices, g_array_unref);

    G_OBJECT_CLASS (lrg_sky_dome_parent_class)->finalize (object);
}

static void
lrg_sky_dome_class_init (LrgSkyDomeClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_sky_dome_finalize;
}

/* add_default_keyframe:
 * Shorthand for the built-in day cycle. */
static void
add_default_keyframe (LrgSkyDome *self,
                      gfloat      hours,
                      guint32     zenith,
                      guint32     horizon,
                      guint32     sun)
{
    GrlColor z = { (zenith >> 24) & 0xFF, (zenith >> 16) & 0xFF, (zenith >> 8) & 0xFF, zenith & 0xFF };
    GrlColor h = { (horizon >> 24) & 0xFF, (horizon >> 16) & 0xFF, (horizon >> 8) & 0xFF, horizon & 0xFF };
    GrlColor s = { (sun >> 24) & 0xFF, (sun >> 16) & 0xFF, (sun >> 8) & 0xFF, sun & 0xFF };

    lrg_sky_dome_add_keyframe (self, hours, &z, &h, &s);
}

static void
lrg_sky_dome_init (LrgSkyDome *self)
{
    self->segments = 32;
    self->rings = 8;
    self->radius = LRG_SKY_DOME_DEFAULT_RADIUS;
    self->ground_follows = TRUE;
    self->band_bottom = -5.0f;
    self->band_top = 40.0f;
    set_bytes (self->band_tint, 255, 255, 255, 255);
    self->sun_size = 2.0f;
    self->sun_tilt = 30.0f;
    self->keyframes = g_array_new (FALSE, FALSE, sizeof (SkyKeyframe));
    self->positions = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->colors = g_array_new (FALSE, FALSE, sizeof (guint8));
    self->indices = g_array_new (FALSE, FALSE, sizeof (guint16));
    self->geometry_dirty = TRUE;

    /* A plain day cycle: night, dawn, noon, dusk (RGBA as 0xRRGGBBAA). */
    add_default_keyframe (self, 0.0f, 0x050A1EFFu, 0x141E37FFu, 0xFFFFFF00u);
    add_default_keyframe (self, 6.0f, 0x46649BFFu, 0xF0965AFFu, 0xFFB46EFFu);
    add_default_keyframe (self, 12.0f, 0x3C78DCFFu, 0xAACDF0FFu, 0xFFFAE6FFu);
    add_default_keyframe (self, 18.0f, 0x3C4B87FFu, 0xE6825AFFu, 0xFF9650FFu);
    lrg_sky_dome_set_time_of_day (self, 12.0f);
}

LrgSkyDome *
lrg_sky_dome_new (void)
{
    return g_object_new (LRG_TYPE_SKY_DOME, NULL);
}

void
lrg_sky_dome_set_resolution (LrgSkyDome *self,
                             guint       segments,
                             guint       rings)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (segments >= 3 && segments <= 256);
    g_return_if_fail (rings >= 1 && rings <= 64);

    self->segments = segments;
    self->rings = rings;
    self->geometry_dirty = TRUE;
}

void
lrg_sky_dome_set_radius (LrgSkyDome *self,
                         gfloat      radius)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (radius > 0.0f && isfinite (radius));

    self->radius = radius;
    self->geometry_dirty = TRUE;
}

gfloat
lrg_sky_dome_get_radius (LrgSkyDome *self)
{
    g_return_val_if_fail (LRG_IS_SKY_DOME (self), 0.0f);
    return self->radius;
}

/* ------------------------------------------------------------------------ */
/* Colours                                                                  */
/* ------------------------------------------------------------------------ */

void
lrg_sky_dome_set_zenith_color (LrgSkyDome     *self,
                               const GrlColor *color)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (color != NULL);

    color_to_bytes (color, self->zenith);
    self->geometry_dirty = TRUE;
}

void
lrg_sky_dome_set_horizon_color (LrgSkyDome     *self,
                                const GrlColor *color)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (color != NULL);

    color_to_bytes (color, self->horizon);
    self->geometry_dirty = TRUE;
}

void
lrg_sky_dome_set_ground_color (LrgSkyDome     *self,
                               const GrlColor *color)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));

    self->ground_follows = color == NULL;
    if (color != NULL)
        color_to_bytes (color, self->ground);
    self->geometry_dirty = TRUE;
}

void
lrg_sky_dome_get_zenith_color (LrgSkyDome *self,
                               GrlColor   *color)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (color != NULL);
    bytes_to_color (self->zenith, color);
}

void
lrg_sky_dome_get_horizon_color (LrgSkyDome *self,
                                GrlColor   *color)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (color != NULL);
    bytes_to_color (self->horizon, color);
}

void
lrg_sky_dome_get_ground_color (LrgSkyDome *self,
                               GrlColor   *color)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (color != NULL);
    bytes_to_color (self->ground_follows ? self->horizon : self->ground, color);
}

void
lrg_sky_dome_get_color_at (LrgSkyDome       *self,
                           const GrlVector3 *direction,
                           GrlColor         *color)
{
    gfloat len;
    guint8 out[4];

    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (direction != NULL);
    g_return_if_fail (color != NULL);

    len = sqrtf (direction->x * direction->x + direction->y * direction->y
                 + direction->z * direction->z);
    gradient_bytes (self, len > 0.0f ? direction->y / len : 0.0f, out);
    bytes_to_color (out, color);
}

/* ------------------------------------------------------------------------ */
/* Band                                                                     */
/* ------------------------------------------------------------------------ */

void
lrg_sky_dome_set_band_texture (LrgSkyDome *self,
                               GrlTexture *texture)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

    g_set_object (&self->band[0], texture);
    g_clear_object (&self->band[1]);
}

void
lrg_sky_dome_set_band_halves (LrgSkyDome *self,
                              GrlTexture *first,
                              GrlTexture *second)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (GRL_IS_TEXTURE (first));
    g_return_if_fail (GRL_IS_TEXTURE (second));

    g_set_object (&self->band[0], first);
    g_set_object (&self->band[1], second);
}

void
lrg_sky_dome_set_band_range (LrgSkyDome *self,
                             gfloat      bottom_degrees,
                             gfloat      top_degrees)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (bottom_degrees > -89.0f && top_degrees < 89.0f);
    g_return_if_fail (top_degrees > bottom_degrees);

    self->band_bottom = bottom_degrees;
    self->band_top = top_degrees;
}

void
lrg_sky_dome_set_band_heading (LrgSkyDome *self,
                               gfloat      degrees)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    self->band_heading = degrees;
}

void
lrg_sky_dome_set_band_tint (LrgSkyDome     *self,
                            const GrlColor *tint)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));

    if (tint == NULL)
        set_bytes (self->band_tint, 255, 255, 255, 255);
    else
        color_to_bytes (tint, self->band_tint);
}

/* band_u:
 * Texture index and u for an azimuth in degrees, relative to the band
 * heading. Shared by get_band_uv() and drawing. */
static void
band_u (LrgSkyDome *self,
        gdouble     azimuth_degrees,
        guint      *index,
        gfloat     *u)
{
    gdouble rel = fmod (azimuth_degrees - (gdouble)self->band_heading, 360.0);

    if (rel < 0.0)
        rel += 360.0;
    if (rel >= 360.0)
        rel = 0.0;

    if (self->band[1] == NULL)
    {
        *index = 0;
        *u = (gfloat)(rel / 360.0);
    }
    else
    {
        *index = rel < 180.0 ? 0 : 1;
        *u = (gfloat)(fmod (rel, 180.0) / 180.0);
    }
}

gboolean
lrg_sky_dome_get_band_uv (LrgSkyDome *self,
                          gfloat      azimuth_degrees,
                          guint      *texture_index,
                          gfloat     *u)
{
    guint  index;
    gfloat uu;

    g_return_val_if_fail (LRG_IS_SKY_DOME (self), FALSE);

    if (self->band[0] == NULL)
        return FALSE;
    band_u (self, azimuth_degrees, &index, &uu);
    if (texture_index != NULL)
        *texture_index = index;
    if (u != NULL)
        *u = uu;
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Sun                                                                      */
/* ------------------------------------------------------------------------ */

static void
set_sun_dir (LrgSkyDome *self,
             gfloat      x,
             gfloat      y,
             gfloat      z)
{
    gfloat len = sqrtf (x * x + y * y + z * z);

    if (len <= 0.0f || !isfinite (len))
        return;
    self->sun_dir[0] = x / len;
    self->sun_dir[1] = y / len;
    self->sun_dir[2] = z / len;
}

void
lrg_sky_dome_set_sun_direction (LrgSkyDome       *self,
                                const GrlVector3 *direction)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (direction != NULL);

    set_sun_dir (self, direction->x, direction->y, direction->z);
}

void
lrg_sky_dome_get_sun_direction (LrgSkyDome *self,
                                GrlVector3 *direction)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (direction != NULL);

    direction->x = self->sun_dir[0];
    direction->y = self->sun_dir[1];
    direction->z = self->sun_dir[2];
}

void
lrg_sky_dome_set_sun_color (LrgSkyDome     *self,
                            const GrlColor *color)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (color != NULL);
    color_to_bytes (color, self->sun_color);
}

void
lrg_sky_dome_get_sun_color (LrgSkyDome *self,
                            GrlColor   *color)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (color != NULL);
    bytes_to_color (self->sun_color, color);
}

void
lrg_sky_dome_set_sun_size (LrgSkyDome *self,
                           gfloat      degrees)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (degrees >= 0.0f && degrees < 20.0f);
    self->sun_size = degrees;
}

void
lrg_sky_dome_set_sun_tilt (LrgSkyDome *self,
                           gfloat      degrees)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    self->sun_tilt = degrees;
}

/* ------------------------------------------------------------------------ */
/* Time of day                                                              */
/* ------------------------------------------------------------------------ */

void
lrg_sky_dome_clear_keyframes (LrgSkyDome *self)
{
    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_array_set_size (self->keyframes, 0);
}

void
lrg_sky_dome_add_keyframe (LrgSkyDome     *self,
                           gfloat          hours,
                           const GrlColor *zenith,
                           const GrlColor *horizon,
                           const GrlColor *sun)
{
    SkyKeyframe key;
    guint       i;

    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (hours >= 0.0f && hours < 24.0f);
    g_return_if_fail (zenith != NULL && horizon != NULL && sun != NULL);

    key.hours = hours;
    color_to_bytes (zenith, key.zenith);
    color_to_bytes (horizon, key.horizon);
    color_to_bytes (sun, key.sun);

    /* Keep the array sorted; equal times replace. */
    for (i = 0; i < self->keyframes->len; i++)
    {
        SkyKeyframe *k = &g_array_index (self->keyframes, SkyKeyframe, i);

        if (k->hours == hours)
        {
            *k = key;
            return;
        }
        if (k->hours > hours)
            break;
    }
    g_array_insert_val (self->keyframes, i, key);
}

guint
lrg_sky_dome_get_keyframe_count (LrgSkyDome *self)
{
    g_return_val_if_fail (LRG_IS_SKY_DOME (self), 0);
    return self->keyframes->len;
}

void
lrg_sky_dome_set_time_of_day (LrgSkyDome *self,
                              gfloat      hours)
{
    guint   n;
    gdouble theta;
    gdouble tilt;

    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (isfinite (hours));

    hours = wrap_hours (hours);
    self->time_of_day = hours;

    /* Colours: the keyframes before and after @hours, cyclically. */
    n = self->keyframes->len;
    if (n == 1)
    {
        SkyKeyframe *k = &g_array_index (self->keyframes, SkyKeyframe, 0);

        memcpy (self->zenith, k->zenith, 4);
        memcpy (self->horizon, k->horizon, 4);
        memcpy (self->sun_color, k->sun, 4);
        self->geometry_dirty = TRUE;
    }
    else if (n > 1)
    {
        SkyKeyframe *a;
        SkyKeyframe *b;
        gfloat       span;
        gfloat       t;
        guint        next = 0;

        while (next < n && g_array_index (self->keyframes, SkyKeyframe, next).hours <= hours)
            next++;
        /* next == 0 or n: between the last keyframe and the first */
        b = &g_array_index (self->keyframes, SkyKeyframe, next % n);
        a = &g_array_index (self->keyframes, SkyKeyframe, (next + n - 1) % n);
        span = wrap_hours (b->hours - a->hours);
        if (span == 0.0f)
            span = 24.0f;
        t = wrap_hours (hours - a->hours) / span;

        lerp_bytes (a->zenith, b->zenith, t, self->zenith);
        lerp_bytes (a->horizon, b->horizon, t, self->horizon);
        lerp_bytes (a->sun, b->sun, t, self->sun_color);
        self->geometry_dirty = TRUE;
    }

    /* Sun arc: angle 0 at 6 (+X), pi/2 at noon, pi at 18 (-X). */
    theta = ((gdouble)hours - 6.0) / 24.0 * 2.0 * G_PI;
    tilt = (gdouble)self->sun_tilt * SKY_DEG2RAD;
    set_sun_dir (self,
                 (gfloat)cos (theta),
                 (gfloat)(sin (theta) * cos (tilt)),
                 (gfloat)(sin (theta) * sin (tilt)));
}

gfloat
lrg_sky_dome_get_time_of_day (LrgSkyDome *self)
{
    g_return_val_if_fail (LRG_IS_SKY_DOME (self), 0.0f);
    return self->time_of_day;
}

/* ------------------------------------------------------------------------ */
/* Geometry access                                                          */
/* ------------------------------------------------------------------------ */

const gfloat *
lrg_sky_dome_get_positions (LrgSkyDome *self,
                            guint      *n_floats)
{
    g_return_val_if_fail (LRG_IS_SKY_DOME (self), NULL);

    ensure_geometry (self);
    if (n_floats != NULL)
        *n_floats = self->positions->len;
    return (const gfloat *)self->positions->data;
}

const guint8 *
lrg_sky_dome_get_colors (LrgSkyDome *self,
                         guint      *n_bytes)
{
    g_return_val_if_fail (LRG_IS_SKY_DOME (self), NULL);

    ensure_geometry (self);
    if (n_bytes != NULL)
        *n_bytes = self->colors->len;
    return (const guint8 *)self->colors->data;
}

const guint16 *
lrg_sky_dome_get_indices (LrgSkyDome *self,
                          guint      *n_indices)
{
    g_return_val_if_fail (LRG_IS_SKY_DOME (self), NULL);

    ensure_geometry (self);
    if (n_indices != NULL)
        *n_indices = self->indices->len;
    return (const guint16 *)self->indices->data;
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

static void
draw_gradient (LrgSkyDome   *self,
               const gfloat *c)
{
    const gfloat  *p = (const gfloat *)self->positions->data;
    const guint8  *col = (const guint8 *)self->colors->data;
    const guint16 *idx = (const guint16 *)self->indices->data;
    guint          k;

    rlSetTexture (0);
    rlBegin (RL_TRIANGLES);
    for (k = 0; k < self->indices->len; k++)
    {
        guint v = idx[k];

        rlColor4ub (col[v * 4], col[v * 4 + 1], col[v * 4 + 2], col[v * 4 + 3]);
        rlVertex3f (c[0] + p[v * 3], c[1] + p[v * 3 + 1], c[2] + p[v * 3 + 2]);
    }
    rlEnd ();
}

/* sun_fan:
 * A disc of angular radius @degrees around the sun direction in the sun
 * colour, its alpha scaled by @inner_alpha at the centre and @rim_alpha
 * at the rim (a rim of 0 gives a soft halo). */
static void
sun_fan (LrgSkyDome   *self,
         const gfloat *c,
         gfloat        degrees,
         gfloat        inner_alpha,
         gfloat        rim_alpha)
{
    gfloat dist = self->radius * SKY_SUN_DISTANCE;
    gfloat ext = dist * tanf ((gfloat)(degrees * SKY_DEG2RAD));
    gfloat centre[3];
    gfloat right[3];
    gfloat up[3];
    gfloat len;
    guint8 inner = (guint8)floorf ((gfloat)self->sun_color[3] * inner_alpha + 0.5f);
    guint8 rim = (guint8)floorf ((gfloat)self->sun_color[3] * rim_alpha + 0.5f);
    guint  k;

    /* Basis perpendicular to the sun direction. */
    if (fabsf (self->sun_dir[1]) < 0.99f)
    {
        right[0] = -self->sun_dir[2];
        right[1] = 0.0f;
        right[2] = self->sun_dir[0];
    }
    else
    {
        right[0] = 1.0f;
        right[1] = 0.0f;
        right[2] = 0.0f;
    }
    len = sqrtf (right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    for (k = 0; k < 3; k++)
        right[k] /= len;
    up[0] = self->sun_dir[1] * right[2] - self->sun_dir[2] * right[1];
    up[1] = self->sun_dir[2] * right[0] - self->sun_dir[0] * right[2];
    up[2] = self->sun_dir[0] * right[1] - self->sun_dir[1] * right[0];
    for (k = 0; k < 3; k++)
        centre[k] = c[k] + self->sun_dir[k] * dist;

    rlBegin (RL_TRIANGLES);
    for (k = 0; k < SKY_SUN_SEGMENTS; k++)
    {
        gfloat a0 = (gfloat)k / SKY_SUN_SEGMENTS * 2.0f * (gfloat)G_PI;
        gfloat a1 = (gfloat)(k + 1) / SKY_SUN_SEGMENTS * 2.0f * (gfloat)G_PI;
        gfloat c0 = cosf (a0) * ext;
        gfloat s0 = sinf (a0) * ext;
        gfloat c1 = cosf (a1) * ext;
        gfloat s1 = sinf (a1) * ext;

        rlColor4ub (self->sun_color[0], self->sun_color[1], self->sun_color[2], inner);
        rlVertex3f (centre[0], centre[1], centre[2]);
        rlColor4ub (self->sun_color[0], self->sun_color[1], self->sun_color[2], rim);
        rlVertex3f (centre[0] + right[0] * c1 + up[0] * s1,
                    centre[1] + right[1] * c1 + up[1] * s1,
                    centre[2] + right[2] * c1 + up[2] * s1);
        rlVertex3f (centre[0] + right[0] * c0 + up[0] * s0,
                    centre[1] + right[1] * c0 + up[1] * s0,
                    centre[2] + right[2] * c0 + up[2] * s0);
    }
    rlEnd ();
}

/* draw_band:
 * A vertical cylinder of segments columns between the band elevations.
 * Columns are split where the texture changes (halves mode) so each
 * quad maps one texture with u increasing with azimuth. */
static void
draw_band (LrgSkyDome   *self,
           const gfloat *c)
{
    gfloat dist = self->radius * SKY_BAND_DISTANCE;
    gfloat y0 = dist * tanf ((gfloat)(self->band_bottom * SKY_DEG2RAD));
    gfloat y1 = dist * tanf ((gfloat)(self->band_top * SKY_DEG2RAD));
    guint  halves = self->band[1] != NULL ? 2 : 1;
    guint  per = MAX (self->segments / halves, 4u);
    guint  h;
    guint  s;

    for (h = 0; h < halves; h++)
    {
        Texture2D *tex = grl_texture_get_handle (self->band[h]);
        gdouble    span = 360.0 / (gdouble)halves;
        gdouble    start = (gdouble)self->band_heading + span * (gdouble)h;

        rlSetTexture (tex->id);
        rlBegin (RL_QUADS);
        rlColor4ub (self->band_tint[0], self->band_tint[1], self->band_tint[2], self->band_tint[3]);
        for (s = 0; s < per; s++)
        {
            gdouble az0 = (start + span * (gdouble)s / (gdouble)per) * SKY_DEG2RAD;
            gdouble az1 = (start + span * (gdouble)(s + 1) / (gdouble)per) * SKY_DEG2RAD;
            gfloat  u0 = (gfloat)s / (gfloat)per;
            gfloat  u1 = (gfloat)(s + 1) / (gfloat)per;
            gfloat  x0;
            gfloat  z0;
            gfloat  x1;
            gfloat  z1;

            azimuth_dir (az0, &x0, &z0);
            azimuth_dir (az1, &x1, &z1);

            /* counter-clockwise seen from the centre */
            rlTexCoord2f (u0, 1.0f);
            rlVertex3f (c[0] + x0 * dist, c[1] + y0, c[2] + z0 * dist);
            rlTexCoord2f (u1, 1.0f);
            rlVertex3f (c[0] + x1 * dist, c[1] + y0, c[2] + z1 * dist);
            rlTexCoord2f (u1, 0.0f);
            rlVertex3f (c[0] + x1 * dist, c[1] + y1, c[2] + z1 * dist);
            rlTexCoord2f (u0, 0.0f);
            rlVertex3f (c[0] + x0 * dist, c[1] + y1, c[2] + z0 * dist);
        }
        rlEnd ();
    }
    rlSetTexture (0);
}

void
lrg_sky_dome_draw (LrgSkyDome       *self,
                   const GrlVector3 *camera_position)
{
    gfloat c[3];

    g_return_if_fail (LRG_IS_SKY_DOME (self));
    g_return_if_fail (camera_position != NULL);

    ensure_geometry (self);
    c[0] = camera_position->x;
    c[1] = camera_position->y;
    c[2] = camera_position->z;

    /* State changes apply to the batch at flush time: flush around them. */
    rlDrawRenderBatchActive ();
    rlDisableDepthTest ();
    rlDisableDepthMask ();
    rlDisableBackfaceCulling ();

    draw_gradient (self, c);
    /* The sun is skipped once even its halo is below the horizon. */
    if (self->sun_size > 0.0f && self->sun_color[3] > 0 &&
        self->sun_dir[1] > -sinf ((gfloat)(self->sun_size * SKY_HALO_SCALE * SKY_DEG2RAD)))
    {
        rlSetTexture (0);
        sun_fan (self, c, self->sun_size * SKY_HALO_SCALE, SKY_HALO_ALPHA, 0.0f);
        sun_fan (self, c, self->sun_size, 1.0f, 1.0f);
    }
    if (self->band[0] != NULL)
        draw_band (self, c);

    rlDrawRenderBatchActive ();
    rlEnableBackfaceCulling ();
    rlEnableDepthMask ();
    rlEnableDepthTest ();
}
