/* lrg-transform-tools.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-transform-tools.h"

#include <math.h>
#include <string.h>

static gfloat *
axis_array (gint    axis,
            gfloat *xs,
            gfloat *ys,
            gfloat *zs)
{
	if (axis == 0)
		return xs;
	if (axis == 1)
		return ys;
	return zs;
}

static gfloat
snap_one (gfloat value,
          gfloat grid)
{
	if (grid == 0.0f)
		return value;
	if (value >= 0.0f)
		return floorf ((value / grid) + 0.5f) * grid;
	return ceilf ((value / grid) - 0.5f) * grid;
}

void
lrg_transform_pivot (LrgPivotMode   mode,
                     const gfloat  *xs,
                     const gfloat  *ys,
                     const gfloat  *zs,
                     guint          n,
                     gfloat         active_x,
                     gfloat         active_y,
                     gfloat         active_z,
                     gfloat         cursor_x,
                     gfloat         cursor_y,
                     gfloat         cursor_z,
                     gfloat        *out_x,
                     gfloat        *out_y,
                     gfloat        *out_z)
{
	gfloat x;
	gfloat y;
	gfloat z;
	guint  i;

	x = active_x;
	y = active_y;
	z = active_z;
	if (mode == LRG_PIVOT_CURSOR)
	{
		x = cursor_x;
		y = cursor_y;
		z = cursor_z;
	}
	else if (mode == LRG_PIVOT_MEDIAN && n > 0)
	{
		x = 0.0f;
		y = 0.0f;
		z = 0.0f;
		for (i = 0; i < n; i++)
		{
			x += xs[i];
			y += ys[i];
			z += zs[i];
		}
		x /= (gfloat) n;
		y /= (gfloat) n;
		z /= (gfloat) n;
	}
	else if (mode == LRG_PIVOT_BOUNDS && n > 0)
	{
		gfloat min_x = xs[0];
		gfloat max_x = xs[0];
		gfloat min_y = ys[0];
		gfloat max_y = ys[0];
		gfloat min_z = zs[0];
		gfloat max_z = zs[0];

		for (i = 1; i < n; i++)
		{
			if (xs[i] < min_x) min_x = xs[i];
			if (xs[i] > max_x) max_x = xs[i];
			if (ys[i] < min_y) min_y = ys[i];
			if (ys[i] > max_y) max_y = ys[i];
			if (zs[i] < min_z) min_z = zs[i];
			if (zs[i] > max_z) max_z = zs[i];
		}
		x = (min_x + max_x) * 0.5f;
		y = (min_y + max_y) * 0.5f;
		z = (min_z + max_z) * 0.5f;
	}
	if (out_x != NULL)
		*out_x = x;
	if (out_y != NULL)
		*out_y = y;
	if (out_z != NULL)
		*out_z = z;
}

void
lrg_transform_align (LrgAlignMode  mode,
                     gint          axis,
                     gfloat       *xs,
                     gfloat       *ys,
                     gfloat       *zs,
                     guint         n)
{
	gfloat *values;
	gfloat  target;
	guint   i;

	if (n < 2 || axis < 0 || axis > 2)
		return;
	values = axis_array (axis, xs, ys, zs);
	target = values[0];
	for (i = 1; i < n; i++)
	{
		if (mode == LRG_TRANSFORM_ALIGN_MIN && values[i] < target)
			target = values[i];
		else if (mode == LRG_TRANSFORM_ALIGN_MAX && values[i] > target)
			target = values[i];
		else if (mode == LRG_TRANSFORM_ALIGN_CENTER)
			target += values[i];
	}
	if (mode == LRG_TRANSFORM_ALIGN_CENTER)
		target /= (gfloat) n;
	for (i = 0; i < n; i++)
		values[i] = target;
}

typedef struct
{
	gfloat *values;
} DistCtx;

static gint
dist_cmp (gconstpointer a,
          gconstpointer b,
          gpointer      user_data)
{
	const guint *ia = a;
	const guint *ib = b;
	DistCtx     *ctx = user_data;

	if (ctx->values[*ia] < ctx->values[*ib])
		return -1;
	if (ctx->values[*ia] > ctx->values[*ib])
		return 1;
	if (*ia < *ib)
		return -1;
	if (*ia > *ib)
		return 1;
	return 0;
}

void
lrg_transform_distribute (gint     axis,
                          gfloat  *xs,
                          gfloat  *ys,
                          gfloat  *zs,
                          guint    n)
{
	gfloat  *values;
	guint   *order;
	DistCtx  ctx;
	gfloat   first;
	gfloat   last;
	gfloat   step;
	guint    i;

	if (n < 3 || axis < 0 || axis > 2)
		return;
	values = axis_array (axis, xs, ys, zs);
	order = g_new (guint, n);
	for (i = 0; i < n; i++)
		order[i] = i;
	ctx.values = values;
	g_sort_array (order, n, sizeof (guint), dist_cmp, &ctx);
	first = values[order[0]];
	last = values[order[n - 1]];
	step = (last - first) / (gfloat) (n - 1);
	for (i = 1; i < n - 1; i++)
		values[order[i]] = first + step * (gfloat) i;
	g_free (order);
}

void
lrg_transform_snap_position (gfloat *x,
                             gfloat *y,
                             gfloat *z,
                             gfloat  grid)
{
	if (x != NULL)
		*x = snap_one (*x, grid);
	if (y != NULL)
		*y = snap_one (*y, grid);
	if (z != NULL)
		*z = snap_one (*z, grid);
}

void
lrg_transform_snap_angle (gfloat *degrees,
                          gfloat  step)
{
	if (degrees == NULL)
		return;
	*degrees = snap_one (*degrees, step);
}

void
lrg_transform_drop (gfloat *x,
                    gfloat *y,
                    gfloat *z,
                    gint    up_axis,
                    gfloat  floor)
{
	if (up_axis == 0 && x != NULL)
		*x = floor;
	else if (up_axis == 1 && y != NULL)
		*y = floor;
	else if (up_axis == 2 && z != NULL)
		*z = floor;
}

static gboolean
slab (gfloat  origin,
      gfloat  dir,
      gfloat  min_v,
      gfloat  max_v,
      gfloat *tmin,
      gfloat *tmax)
{
	gfloat inv;
	gfloat t1;
	gfloat t2;

	if (dir == 0.0f)
		return origin >= min_v && origin <= max_v;
	inv = 1.0f / dir;
	t1 = (min_v - origin) * inv;
	t2 = (max_v - origin) * inv;
	if (t1 > t2)
	{
		gfloat swap = t1;

		t1 = t2;
		t2 = swap;
	}
	if (t1 > *tmin)
		*tmin = t1;
	if (t2 < *tmax)
		*tmax = t2;
	return *tmin <= *tmax;
}

gboolean
lrg_transform_ray_aabb (gfloat  ox,
                        gfloat  oy,
                        gfloat  oz,
                        gfloat  dx,
                        gfloat  dy,
                        gfloat  dz,
                        gfloat  min_x,
                        gfloat  min_y,
                        gfloat  min_z,
                        gfloat  max_x,
                        gfloat  max_y,
                        gfloat  max_z,
                        gfloat *out_t)
{
	gfloat tmin = 0.0f;
	gfloat tmax = G_MAXFLOAT;

	if (!slab (ox, dx, min_x, max_x, &tmin, &tmax) ||
	    !slab (oy, dy, min_y, max_y, &tmin, &tmax) ||
	    !slab (oz, dz, min_z, max_z, &tmin, &tmax))
		return FALSE;
	if (tmax < 0.0f)
		return FALSE;
	if (out_t != NULL)
		*out_t = tmin;
	return TRUE;
}

gboolean
lrg_transform_snap_surface (gfloat        ox,
                            gfloat        oy,
                            gfloat        oz,
                            gfloat        dx,
                            gfloat        dy,
                            gfloat        dz,
                            const gfloat *min_x,
                            const gfloat *min_y,
                            const gfloat *min_z,
                            const gfloat *max_x,
                            const gfloat *max_y,
                            const gfloat *max_z,
                            guint         n,
                            gfloat       *out_x,
                            gfloat       *out_y,
                            gfloat       *out_z)
{
	gboolean hit = FALSE;
	gfloat   best = 0.0f;
	guint    i;

	for (i = 0; i < n; i++)
	{
		gfloat t = 0.0f;

		if (!lrg_transform_ray_aabb (ox, oy, oz, dx, dy, dz,
		                             min_x[i], min_y[i], min_z[i],
		                             max_x[i], max_y[i], max_z[i], &t))
			continue;
		if (!hit || t < best)
		{
			hit = TRUE;
			best = t;
		}
	}
	if (!hit)
		return FALSE;
	if (out_x != NULL)
		*out_x = ox + best * dx;
	if (out_y != NULL)
		*out_y = oy + best * dy;
	if (out_z != NULL)
		*out_z = oz + best * dz;
	return TRUE;
}
