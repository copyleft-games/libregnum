/* lrg-weather-field.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Fixed steps keep the field independent of how a game splits its
 * time; front formation hashes (seed, step) instead of drawing from a
 * random stream, so a skipped or restored step count reproduces the
 * same fronts.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "weather/lrg-weather-field.h"

struct _LrgWeatherField
{
    GObject  parent_instance;

    guint    width;
    guint    height;
    guint32  seed;
    gdouble  step;            /* seconds */
    gdouble  wind[2];         /* cells per hour */
    gdouble  relax;           /* per hour */
    gdouble  fronts_per_day;
    gdouble  front_radius;
    gdouble  front_strength;

    gfloat  *base;
    gfloat  *moisture;
    gfloat  *storm;
    gfloat  *scratch;

    guint64  steps;
    gdouble  carry;           /* seconds short of the next step */
};

G_DEFINE_FINAL_TYPE (LrgWeatherField, lrg_weather_field, G_TYPE_OBJECT)

static void
lrg_weather_field_finalize (GObject *object)
{
    LrgWeatherField *self = LRG_WEATHER_FIELD (object);

    g_free (self->base);
    g_free (self->moisture);
    g_free (self->storm);
    g_free (self->scratch);
    G_OBJECT_CLASS (lrg_weather_field_parent_class)->finalize (object);
}

static void
lrg_weather_field_class_init (LrgWeatherFieldClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_weather_field_finalize;
}

static void
lrg_weather_field_init (LrgWeatherField *self)
{
    self->step = 3600.0;
    self->relax = 0.04;
    self->fronts_per_day = 1.0;
    self->front_radius = 6.0;
    self->front_strength = 0.6;
}

LrgWeatherField *
lrg_weather_field_new (guint   width,
                       guint   height,
                       guint32 seed)
{
    LrgWeatherField *self;
    gsize            n;
    gsize            i;

    if (width == 0 || height == 0 || width > LRG_WEATHER_FIELD_MAX_SIDE ||
        height > LRG_WEATHER_FIELD_MAX_SIDE)
        return NULL;

    self = g_object_new (LRG_TYPE_WEATHER_FIELD, NULL);
    self->width = width;
    self->height = height;
    self->seed = seed;
    n = (gsize)width * height;
    self->base = g_new (gfloat, n);
    self->moisture = g_new (gfloat, n);
    self->storm = g_new0 (gfloat, n);
    self->scratch = g_new (gfloat, n);
    for (i = 0; i < n; i++)
    {
        self->base[i] = 0.5f;
        self->moisture[i] = 0.5f;
    }
    return self;
}

guint
lrg_weather_field_get_width (LrgWeatherField *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), 0);
    return self->width;
}

guint
lrg_weather_field_get_height (LrgWeatherField *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), 0);
    return self->height;
}

guint32
lrg_weather_field_get_seed (LrgWeatherField *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), 0);
    return self->seed;
}

void
lrg_weather_field_set_step (LrgWeatherField *self,
                            gdouble          seconds)
{
    g_return_if_fail (LRG_IS_WEATHER_FIELD (self));

    if (isfinite (seconds) && seconds > 0.0)
        self->step = seconds;
}

void
lrg_weather_field_set_wind (LrgWeatherField *self,
                            gdouble          x,
                            gdouble          y)
{
    g_return_if_fail (LRG_IS_WEATHER_FIELD (self));

    if (!isfinite (x) || !isfinite (y))
        return;
    self->wind[0] = x;
    self->wind[1] = y;
}

void
lrg_weather_field_get_wind (LrgWeatherField *self,
                            gdouble         *x,
                            gdouble         *y)
{
    g_return_if_fail (LRG_IS_WEATHER_FIELD (self));

    if (x != NULL)
        *x = self->wind[0];
    if (y != NULL)
        *y = self->wind[1];
}

void
lrg_weather_field_set_base (LrgWeatherField *self,
                            guint            x,
                            guint            y,
                            gfloat           moisture)
{
    g_return_if_fail (LRG_IS_WEATHER_FIELD (self));

    if (x >= self->width || y >= self->height || !isfinite (moisture))
        return;
    self->base[(gsize)y * self->width + x] = CLAMP (moisture, 0.0f, 1.0f);
}

gfloat
lrg_weather_field_get_base (LrgWeatherField *self,
                            guint            x,
                            guint            y)
{
    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), 0.0f);

    if (x >= self->width || y >= self->height)
        return 0.0f;
    return self->base[(gsize)y * self->width + x];
}

void
lrg_weather_field_set_relaxation (LrgWeatherField *self,
                                  gdouble          per_hour)
{
    g_return_if_fail (LRG_IS_WEATHER_FIELD (self));

    if (isfinite (per_hour))
        self->relax = CLAMP (per_hour, 0.0, 1.0);
}

void
lrg_weather_field_set_fronts (LrgWeatherField *self,
                              gdouble          per_day,
                              gdouble          radius,
                              gdouble          strength)
{
    g_return_if_fail (LRG_IS_WEATHER_FIELD (self));

    if (isfinite (per_day) && per_day >= 0.0)
        self->fronts_per_day = per_day;
    if (isfinite (radius) && radius > 0.0)
        self->front_radius = radius;
    if (isfinite (strength))
        self->front_strength = CLAMP (strength, 0.0, 1.0);
}

void
lrg_weather_field_reset (LrgWeatherField *self)
{
    gsize n;

    g_return_if_fail (LRG_IS_WEATHER_FIELD (self));

    n = (gsize)self->width * self->height;
    memcpy (self->moisture, self->base, n * sizeof (gfloat));
    memset (self->storm, 0, n * sizeof (gfloat));
    self->steps = 0;
    self->carry = 0.0;
}

/* hash01:
 * The k-th uniform number of a step, [0, 1). */
static gdouble
hash01 (guint32 seed,
        guint64 step,
        guint   k)
{
    guint32 x = seed ^ 0x85ebca6bU;

    x ^= (guint32)(step & 0xffffffffu) * 0x9e3779b1U;
    x ^= (guint32)(step >> 32) * 0xc2b2ae35U;
    x ^= (guint32)k * 0x27d4eb2fU;
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return (gdouble)x / 4294967296.0;
}

/* sample:
 * Bilinear sample of a grid at field coordinates (cell centres at
 * i + 0.5), clamped to the edge cells. */
static gfloat
sample (LrgWeatherField *self,
        const gfloat    *grid,
        gdouble          x,
        gdouble          y)
{
    gdouble fx;
    gdouble fy;
    gint    x0;
    gint    y0;
    gint    x1;
    gint    y1;
    gint    w = (gint)self->width;
    gint    h = (gint)self->height;
    gdouble a;
    gdouble b;

    x -= 0.5;
    y -= 0.5;
    x = CLAMP (x, 0.0, (gdouble)(w - 1));
    y = CLAMP (y, 0.0, (gdouble)(h - 1));
    x0 = (gint)floor (x);
    y0 = (gint)floor (y);
    x1 = MIN (x0 + 1, w - 1);
    y1 = MIN (y0 + 1, h - 1);
    fx = x - x0;
    fy = y - y0;
    a = grid[y0 * w + x0] + (grid[y0 * w + x1] - grid[y0 * w + x0]) * fx;
    b = grid[y1 * w + x0] + (grid[y1 * w + x1] - grid[y1 * w + x0]) * fx;
    return (gfloat)(a + (b - a) * fy);
}

/* advect:
 * Semi-Lagrangian: every cell takes the value from where its air was
 * one step ago. */
static void
advect (LrgWeatherField *self,
        gfloat          *grid,
        gdouble          hours)
{
    guint x;
    guint y;

    for (y = 0; y < self->height; y++)
        for (x = 0; x < self->width; x++)
            self->scratch[(gsize)y * self->width + x] =
                sample (self, grid, x + 0.5 - self->wind[0] * hours, y + 0.5 - self->wind[1] * hours);
    memcpy (grid, self->scratch, (gsize)self->width * self->height * sizeof (gfloat));
}

/* add_front:
 * A Gaussian blob of moisture (and, for some fronts, storm). */
static void
add_front (LrgWeatherField *self,
           gdouble          cx,
           gdouble          cy,
           gdouble          storminess)
{
    gdouble r = self->front_radius;
    gint    x0 = MAX ((gint)floor (cx - 3.0 * r), 0);
    gint    x1 = MIN ((gint)ceil (cx + 3.0 * r), (gint)self->width - 1);
    gint    y0 = MAX ((gint)floor (cy - 3.0 * r), 0);
    gint    y1 = MIN ((gint)ceil (cy + 3.0 * r), (gint)self->height - 1);
    gint    x;
    gint    y;

    for (y = y0; y <= y1; y++)
    {
        for (x = x0; x <= x1; x++)
        {
            gdouble dx = x + 0.5 - cx;
            gdouble dy = y + 0.5 - cy;
            gdouble g = exp (-(dx * dx + dy * dy) / (2.0 * r * r));
            gsize   i = (gsize)y * self->width + (gsize)x;

            self->moisture[i] = (gfloat)MIN (1.0, self->moisture[i] + self->front_strength * g);
            self->storm[i] = (gfloat)MIN (1.0, self->storm[i] + storminess * g);
        }
    }
}

/* run_step:
 * One fixed step: advect, relax, decay storms, maybe a front. */
static void
run_step (LrgWeatherField *self)
{
    gdouble hours = self->step / 3600.0;
    gdouble keep = pow (1.0 - self->relax, hours);
    gdouble storm_keep = keep * keep;
    gdouble chance;
    gsize   n = (gsize)self->width * self->height;
    gsize   i;

    advect (self, self->moisture, hours);
    advect (self, self->storm, hours);
    for (i = 0; i < n; i++)
    {
        self->moisture[i] = (gfloat)(self->base[i] + (self->moisture[i] - self->base[i]) * keep);
        self->storm[i] = (gfloat)(self->storm[i] * storm_keep);
    }

    /* fronts form like a Poisson process sampled once a step */
    chance = self->fronts_per_day * self->step / 86400.0;
    if (hash01 (self->seed, self->steps, 0) < chance)
    {
        gdouble cx = hash01 (self->seed, self->steps, 1) * self->width;
        gdouble cy = hash01 (self->seed, self->steps, 2) * self->height;
        gdouble s = hash01 (self->seed, self->steps, 3);

        /* about a third of the fronts are storms */
        add_front (self, cx, cy, s > 0.66 ? (s - 0.66) / 0.34 : 0.0);
    }
    self->steps++;
}

guint
lrg_weather_field_advance (LrgWeatherField *self,
                           gdouble          seconds)
{
    guint ran = 0;

    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), 0);

    if (!isfinite (seconds) || seconds <= 0.0)
        return 0;

    self->carry += seconds;
    while (self->carry >= self->step)
    {
        self->carry -= self->step;
        run_step (self);
        ran++;
    }
    return ran;
}

guint64
lrg_weather_field_get_step_count (LrgWeatherField *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), 0);
    return self->steps;
}

gfloat
lrg_weather_field_get_moisture (LrgWeatherField *self,
                                gdouble          x,
                                gdouble          y)
{
    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), 0.0f);

    if (!isfinite (x) || !isfinite (y))
        return 0.0f;
    return sample (self, self->moisture, x, y);
}

gfloat
lrg_weather_field_get_storm (LrgWeatherField *self,
                             gdouble          x,
                             gdouble          y)
{
    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), 0.0f);

    if (!isfinite (x) || !isfinite (y))
        return 0.0f;
    return sample (self, self->storm, x, y);
}

void
lrg_weather_field_set_cell (LrgWeatherField *self,
                            guint            x,
                            guint            y,
                            gfloat           moisture,
                            gfloat           storm)
{
    gsize i;

    g_return_if_fail (LRG_IS_WEATHER_FIELD (self));

    if (x >= self->width || y >= self->height || !isfinite (moisture) || !isfinite (storm))
        return;
    i = (gsize)y * self->width + x;
    self->moisture[i] = CLAMP (moisture, 0.0f, 1.0f);
    self->storm[i] = CLAMP (storm, 0.0f, 1.0f);
}

static GVariant *
grid_variant (const gfloat *grid,
              gsize         n)
{
    g_autofree gdouble *values = g_new (gdouble, n);
    gsize               i;

    for (i = 0; i < n; i++)
        values[i] = grid[i];
    return g_variant_new_fixed_array (G_VARIANT_TYPE_DOUBLE, values, n, sizeof (gdouble));
}

GVariant *
lrg_weather_field_save_state (LrgWeatherField *self)
{
    GVariantBuilder b;
    gsize           n;

    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), NULL);

    n = (gsize)self->width * self->height;
    g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "width", g_variant_new_uint32 (self->width));
    g_variant_builder_add (&b, "{sv}", "height", g_variant_new_uint32 (self->height));
    g_variant_builder_add (&b, "{sv}", "seed", g_variant_new_uint32 (self->seed));
    g_variant_builder_add (&b, "{sv}", "steps", g_variant_new_uint64 (self->steps));
    g_variant_builder_add (&b, "{sv}", "carry", g_variant_new_double (self->carry));
    g_variant_builder_add (&b, "{sv}", "wind",
                           g_variant_new ("(dd)", self->wind[0], self->wind[1]));
    g_variant_builder_add (&b, "{sv}", "moisture", grid_variant (self->moisture, n));
    g_variant_builder_add (&b, "{sv}", "storm", grid_variant (self->storm, n));
    return g_variant_ref_sink (g_variant_builder_end (&b));
}

gboolean
lrg_weather_field_restore_state (LrgWeatherField  *self,
                                 GVariant         *state,
                                 GError          **error)
{
    g_autoptr(GVariant) moisture = NULL;
    g_autoptr(GVariant) storm = NULL;
    const gdouble      *m;
    const gdouble      *s;
    gsize               n_m = 0;
    gsize               n_s = 0;
    gsize               n;
    gsize               i;
    guint32             width;
    guint32             height;
    guint32             seed;
    guint64             steps;
    gdouble             carry;
    gdouble             wx;
    gdouble             wy;

    g_return_val_if_fail (LRG_IS_WEATHER_FIELD (self), FALSE);
    g_return_val_if_fail (state != NULL, FALSE);

    if (!g_variant_is_of_type (state, G_VARIANT_TYPE_VARDICT) ||
        !g_variant_lookup (state, "width", "u", &width) ||
        !g_variant_lookup (state, "height", "u", &height) ||
        !g_variant_lookup (state, "seed", "u", &seed) ||
        !g_variant_lookup (state, "steps", "t", &steps) ||
        !g_variant_lookup (state, "carry", "d", &carry) ||
        !g_variant_lookup (state, "wind", "(dd)", &wx, &wy))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "weather field state: missing or mistyped keys");
        return FALSE;
    }
    if (width != self->width || height != self->height)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "weather field state: size %ux%u, the field is %ux%u",
                     width, height, self->width, self->height);
        return FALSE;
    }
    moisture = g_variant_lookup_value (state, "moisture", G_VARIANT_TYPE ("ad"));
    storm = g_variant_lookup_value (state, "storm", G_VARIANT_TYPE ("ad"));
    n = (gsize)width * height;
    if (moisture == NULL || storm == NULL)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "weather field state: no grids");
        return FALSE;
    }
    m = g_variant_get_fixed_array (moisture, &n_m, sizeof (gdouble));
    s = g_variant_get_fixed_array (storm, &n_s, sizeof (gdouble));
    if (n_m != n || n_s != n || !isfinite (carry) || carry < 0.0 || !isfinite (wx) ||
        !isfinite (wy))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "weather field state: grid size or values out of range");
        return FALSE;
    }
    for (i = 0; i < n; i++)
    {
        if (!isfinite (m[i]) || m[i] < 0.0 || m[i] > 1.0 || !isfinite (s[i]) || s[i] < 0.0 ||
            s[i] > 1.0)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "weather field state: cell %" G_GSIZE_FORMAT " out of range", i);
            return FALSE;
        }
    }

    /* validated: apply */
    for (i = 0; i < n; i++)
    {
        self->moisture[i] = (gfloat)m[i];
        self->storm[i] = (gfloat)s[i];
    }
    self->seed = seed;
    self->steps = steps;
    self->carry = MIN (carry, self->step);
    self->wind[0] = wx;
    self->wind[1] = wy;
    return TRUE;
}
