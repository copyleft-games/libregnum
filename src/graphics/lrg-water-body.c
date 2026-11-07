/* lrg-water-body.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaterBody: the `.rwater` water body (plane, lake polygon, river
 * spline) and the LrgWaterSurface geometry built from it.  See
 * lrg-water-body.h and docs/modules/graphics/water-body.org.
 *
 * Geometry: the outline's local bounding box is cut into cells of
 * `cell_size` and then into tiles of at most 250 x 250 cells (an
 * LrgWaterSurface patch holds 65535 vertices).  Each tile becomes one
 * lrg_water_surface_add_heightfield() patch whose wet cells are those
 * whose centre lies inside the outline; vertex heights are the level
 * (plane, lake) or the river spline's height at the nearest point.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-water-body.h"
#include "lrg-terrain-asset-private.h"
#include "../core/lrg-data-loader.h"

/* cells per tile side: (250 + 1)² vertices fit in 16-bit indices */
#define TILE_CELLS (250)

/* polyline samples per river segment */
#define RIVER_STEPS (16)

G_DEFINE_QUARK (lrg-water-body-error-quark, lrg_water_body_error)

GType
lrg_water_body_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_WATER_BODY_ERROR_PARSE, "LRG_WATER_BODY_ERROR_PARSE", "parse" },
			{ LRG_WATER_BODY_ERROR_INVALID, "LRG_WATER_BODY_ERROR_INVALID", "invalid" },
			{ LRG_WATER_BODY_ERROR_IO, "LRG_WATER_BODY_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgWaterBodyError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_water_body_shape_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_WATER_BODY_PLANE, "LRG_WATER_BODY_PLANE", "plane" },
			{ LRG_WATER_BODY_LAKE, "LRG_WATER_BODY_LAKE", "lake" },
			{ LRG_WATER_BODY_RIVER, "LRG_WATER_BODY_RIVER", "river" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgWaterBodyShape"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

/* A river polyline sample: position, height and width. */
typedef struct
{
	gfloat x;
	gfloat y;
	gfloat z;
	gfloat w;
} RiverSample;

struct _LrgWaterBody
{
	GObject            parent_instance;

	gchar             *name;
	LrgWaterBodyShape  shape;
	gfloat             level;
	gfloat             size[2];
	GArray            *points;        /* gfloat[4]: x, y, z, width */
	gfloat             cell_size;
	gfloat             foam_width;
	gfloat             foam_depth;
	guint              wave_count;
	LrgWaterWave       waves[LRG_WATER_SURFACE_MAX_WAVES];
	gfloat             wave_scale;
	GrlColor           shallow;
	GrlColor           deep;
	GrlColor           foam;
	gfloat             optics[4];
	YamlMapping       *extra;

	GArray            *river;         /* RiverSample, built lazily */
	gboolean           river_dirty;
};

G_DEFINE_FINAL_TYPE (LrgWaterBody, lrg_water_body, G_TYPE_OBJECT)

static void
lrg_water_body_finalize (GObject *object)
{
	LrgWaterBody *self = LRG_WATER_BODY (object);

	g_free (self->name);
	g_array_unref (self->points);
	g_array_unref (self->river);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);
	G_OBJECT_CLASS (lrg_water_body_parent_class)->finalize (object);
}

static void
lrg_water_body_class_init (LrgWaterBodyClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_water_body_finalize;
}

static void
color_set (GrlColor *c,
           gfloat    r,
           gfloat    g,
           gfloat    b)
{
	c->r = (guint8) lrintf (r * 255.0f);
	c->g = (guint8) lrintf (g * 255.0f);
	c->b = (guint8) lrintf (b * 255.0f);
	c->a = 255;
}

static void
lrg_water_body_init (LrgWaterBody *self)
{
	g_autoptr(LrgWaterSurface) defaults = lrg_water_surface_new ();
	guint                      i;

	self->points = g_array_new (FALSE, FALSE, sizeof (gfloat) * 4);
	self->river = g_array_new (FALSE, FALSE, sizeof (RiverSample));
	self->river_dirty = TRUE;
	self->size[0] = self->size[1] = 32.0f;
	self->cell_size = 1.0f;
	self->foam_width = 3.0f;
	self->foam_depth = 0.6f;
	self->wave_scale = 1.0f;

	/* the surface's own defaults: waves, colours, optics */
	self->wave_count = lrg_water_surface_get_wave_count (defaults);
	for (i = 0; i < self->wave_count; i++)
		lrg_water_surface_get_wave (defaults, i, &self->waves[i]);
	color_set (&self->shallow, 0.22f, 0.42f, 0.40f);
	color_set (&self->deep, 0.05f, 0.16f, 0.22f);
	color_set (&self->foam, 0.92f, 0.95f, 0.96f);
	self->optics[0] = 0.35f;
	self->optics[1] = 0.8f;
	self->optics[2] = 0.03f;
	self->optics[3] = 25.0f;
}

LrgWaterBody *
lrg_water_body_new (LrgWaterBodyShape shape)
{
	LrgWaterBody *self = (LrgWaterBody *) g_object_new (LRG_TYPE_WATER_BODY, NULL);

	self->shape = shape;
	return self;
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

const gchar *
lrg_water_body_get_name (LrgWaterBody *self)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), NULL);
	return self->name;
}

void
lrg_water_body_set_name (LrgWaterBody *self,
                         const gchar  *name)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	g_free (self->name);
	self->name = g_strdup (name);
}

LrgWaterBodyShape
lrg_water_body_get_shape (LrgWaterBody *self)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), LRG_WATER_BODY_PLANE);
	return self->shape;
}

void
lrg_water_body_set_shape (LrgWaterBody      *self,
                          LrgWaterBodyShape  shape)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	self->shape = shape;
}

gfloat
lrg_water_body_get_level (LrgWaterBody *self)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), 0.0f);
	return self->level;
}

void
lrg_water_body_set_level (LrgWaterBody *self,
                          gfloat        level)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (isfinite (level))
		self->level = level;
}

void
lrg_water_body_get_size (LrgWaterBody *self,
                         gfloat       *width,
                         gfloat       *length)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (width != NULL)
		*width = self->size[0];
	if (length != NULL)
		*length = self->size[1];
}

void
lrg_water_body_set_size (LrgWaterBody *self,
                         gfloat        width,
                         gfloat        length)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (!(width > 0.0f) || !(length > 0.0f) || !isfinite (width) || !isfinite (length))
		return;
	self->size[0] = width;
	self->size[1] = length;
}

guint
lrg_water_body_get_n_points (LrgWaterBody *self)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), 0);
	return self->points->len;
}

gboolean
lrg_water_body_get_point (LrgWaterBody *self,
                          guint         index,
                          gfloat       *out)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	if (index >= self->points->len)
		return FALSE;
	memcpy (out, &g_array_index (self->points, gfloat, index * 4), sizeof (gfloat) * 4);
	return TRUE;
}

gint
lrg_water_body_add_point (LrgWaterBody *self,
                          gfloat        x,
                          gfloat        y,
                          gfloat        z,
                          gfloat        width)
{
	gfloat p[4];

	g_return_val_if_fail (LRG_IS_WATER_BODY (self), -1);
	if (self->points->len >= LRG_WATER_BODY_MAX_POINTS || !isfinite (x) || !isfinite (y) || !isfinite (z))
		return -1;
	p[0] = x;
	p[1] = y;
	p[2] = z;
	p[3] = (width > 0.0f && isfinite (width)) ? width : 1.0f;
	g_array_append_vals (self->points, p, 1);
	self->river_dirty = TRUE;
	return (gint) self->points->len - 1;
}

gboolean
lrg_water_body_set_point (LrgWaterBody *self,
                          guint         index,
                          gfloat        x,
                          gfloat        y,
                          gfloat        z,
                          gfloat        width)
{
	gfloat *p;

	g_return_val_if_fail (LRG_IS_WATER_BODY (self), FALSE);
	if (index >= self->points->len || !isfinite (x) || !isfinite (y) || !isfinite (z))
		return FALSE;
	p = &g_array_index (self->points, gfloat, index * 4);
	p[0] = x;
	p[1] = y;
	p[2] = z;
	p[3] = (width > 0.0f && isfinite (width)) ? width : 1.0f;
	self->river_dirty = TRUE;
	return TRUE;
}

gboolean
lrg_water_body_remove_point (LrgWaterBody *self,
                             guint         index)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), FALSE);
	if (index >= self->points->len)
		return FALSE;
	g_array_remove_index (self->points, index);
	self->river_dirty = TRUE;
	return TRUE;
}

void
lrg_water_body_clear_points (LrgWaterBody *self)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	g_array_set_size (self->points, 0);
	self->river_dirty = TRUE;
}

gfloat
lrg_water_body_get_cell_size (LrgWaterBody *self)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), 1.0f);
	return self->cell_size;
}

void
lrg_water_body_set_cell_size (LrgWaterBody *self,
                              gfloat        cell_size)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (isfinite (cell_size))
		self->cell_size = CLAMP (cell_size, 0.05f, 64.0f);
}

void
lrg_water_body_get_foam (LrgWaterBody *self,
                         gfloat       *width,
                         gfloat       *depth)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (width != NULL)
		*width = self->foam_width;
	if (depth != NULL)
		*depth = self->foam_depth;
}

void
lrg_water_body_set_foam (LrgWaterBody *self,
                         gfloat        width,
                         gfloat        depth)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (width >= 0.0f && isfinite (width))
		self->foam_width = width;
	if (depth >= 0.0f && isfinite (depth))
		self->foam_depth = depth;
}

guint
lrg_water_body_get_wave_count (LrgWaterBody *self)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), 0);
	return self->wave_count;
}

void
lrg_water_body_set_wave_count (LrgWaterBody *self,
                               guint         count)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	self->wave_count = MIN (count, (guint) LRG_WATER_SURFACE_MAX_WAVES);
}

gboolean
lrg_water_body_get_wave (LrgWaterBody *self,
                         guint         index,
                         LrgWaterWave *out)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	if (index >= self->wave_count)
		return FALSE;
	*out = self->waves[index];
	return TRUE;
}

void
lrg_water_body_set_wave (LrgWaterBody       *self,
                         guint               index,
                         const LrgWaterWave *wave)
{
	LrgWaterWave *w;
	gfloat        len;

	g_return_if_fail (LRG_IS_WATER_BODY (self));
	g_return_if_fail (wave != NULL);
	if (index >= LRG_WATER_SURFACE_MAX_WAVES)
		return;
	w = &self->waves[index];
	*w = *wave;
	len = sqrtf (w->direction[0] * w->direction[0] + w->direction[1] * w->direction[1]);
	if (len > 1e-6f && isfinite (len))
	{
		w->direction[0] /= len;
		w->direction[1] /= len;
	}
	else
	{
		w->direction[0] = 1.0f;
		w->direction[1] = 0.0f;
	}
	w->amplitude = isfinite (w->amplitude) ? MAX (w->amplitude, 0.0f) : 0.0f;
	if (!(w->wavelength > 0.0f) || !isfinite (w->wavelength))
		w->wavelength = 1.0f;
	if (!isfinite (w->speed))
		w->speed = 0.0f;
	w->steepness = isfinite (w->steepness) ? CLAMP (w->steepness, 0.0f, 1.0f) : 0.0f;
	if (index >= self->wave_count)
		self->wave_count = index + 1;
}

gfloat
lrg_water_body_get_wave_scale (LrgWaterBody *self)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), 1.0f);
	return self->wave_scale;
}

void
lrg_water_body_set_wave_scale (LrgWaterBody *self,
                               gfloat        scale)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (scale >= 0.0f && isfinite (scale))
		self->wave_scale = scale;
}

void
lrg_water_body_get_colors (LrgWaterBody *self,
                           GrlColor     *shallow,
                           GrlColor     *deep,
                           GrlColor     *foam)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (shallow != NULL)
		*shallow = self->shallow;
	if (deep != NULL)
		*deep = self->deep;
	if (foam != NULL)
		*foam = self->foam;
}

void
lrg_water_body_set_colors (LrgWaterBody   *self,
                           const GrlColor *shallow,
                           const GrlColor *deep,
                           const GrlColor *foam)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (shallow != NULL)
		self->shallow = *shallow;
	if (deep != NULL)
		self->deep = *deep;
	if (foam != NULL)
		self->foam = *foam;
}

void
lrg_water_body_get_optics (LrgWaterBody *self,
                           gfloat       *transparency,
                           gfloat       *reflection,
                           gfloat       *refraction,
                           gfloat       *reflect_distance)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (transparency != NULL)
		*transparency = self->optics[0];
	if (reflection != NULL)
		*reflection = self->optics[1];
	if (refraction != NULL)
		*refraction = self->optics[2];
	if (reflect_distance != NULL)
		*reflect_distance = self->optics[3];
}

void
lrg_water_body_set_optics (LrgWaterBody *self,
                           gfloat        transparency,
                           gfloat        reflection,
                           gfloat        refraction,
                           gfloat        reflect_distance)
{
	g_return_if_fail (LRG_IS_WATER_BODY (self));
	if (isfinite (transparency))
		self->optics[0] = CLAMP (transparency, 0.0f, 1.0f);
	if (isfinite (reflection))
		self->optics[1] = CLAMP (reflection, 0.0f, 1.0f);
	if (refraction >= 0.0f && isfinite (refraction))
		self->optics[2] = refraction;
	if (reflect_distance > 0.0f && isfinite (reflect_distance))
		self->optics[3] = reflect_distance;
}

/* ==========================================================================
 * Shape queries
 * ========================================================================== */

static const gfloat *
point_at (LrgWaterBody *self,
          guint         i)
{
	return &g_array_index (self->points, gfloat, i * 4);
}

/* Catmull-Rom between p1 and p2 (p0, p3 the neighbours), per component. */
static gfloat
catmull (gfloat p0,
         gfloat p1,
         gfloat p2,
         gfloat p3,
         gfloat t)
{
	gfloat t2 = t * t, t3 = t2 * t;

	return 0.5f * (2.0f * p1 + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
	               (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

/* The river polyline: RIVER_STEPS samples per segment of the spline. */
static void
ensure_river (LrgWaterBody *self)
{
	guint n = self->points->len, i, s;

	if (!self->river_dirty)
		return;
	self->river_dirty = FALSE;
	g_array_set_size (self->river, 0);
	if (n == 0)
		return;
	if (n == 1)
	{
		const gfloat *p = point_at (self, 0);
		RiverSample   r = { p[0], p[1], p[2], p[3] };

		g_array_append_val (self->river, r);
		return;
	}
	for (i = 0; i + 1 < n; i++)
	{
		const gfloat *p0 = point_at (self, i > 0 ? i - 1 : 0);
		const gfloat *p1 = point_at (self, i);
		const gfloat *p2 = point_at (self, i + 1);
		const gfloat *p3 = point_at (self, MIN (i + 2, n - 1));

		for (s = 0; s < RIVER_STEPS; s++)
		{
			gfloat      t = (gfloat) s / (gfloat) RIVER_STEPS;
			RiverSample r;

			r.x = catmull (p0[0], p1[0], p2[0], p3[0], t);
			r.y = catmull (p0[1], p1[1], p2[1], p3[1], t);
			r.z = catmull (p0[2], p1[2], p2[2], p3[2], t);
			r.w = MAX (catmull (p0[3], p1[3], p2[3], p3[3], t), 0.01f);
			g_array_append_val (self->river, r);
		}
	}
	{
		const gfloat *p = point_at (self, n - 1);
		RiverSample   r = { p[0], p[1], p[2], p[3] };

		g_array_append_val (self->river, r);
	}
}

/* Distance from (x, z) to the river polyline; the nearest sample's height
 * and width (interpolated along the segment). */
static gfloat
river_distance (LrgWaterBody *self,
                gfloat        x,
                gfloat        z,
                gfloat       *out_y,
                gfloat       *out_w)
{
	gfloat best = G_MAXFLOAT;
	guint  i;

	ensure_river (self);
	*out_y = self->level;
	*out_w = 0.0f;
	if (self->river->len == 0)
		return best;
	if (self->river->len == 1)
	{
		RiverSample *r = &g_array_index (self->river, RiverSample, 0);

		*out_y = r->y;
		*out_w = r->w;
		return sqrtf ((x - r->x) * (x - r->x) + (z - r->z) * (z - r->z));
	}
	for (i = 0; i + 1 < self->river->len; i++)
	{
		RiverSample *a = &g_array_index (self->river, RiverSample, i);
		RiverSample *b = &g_array_index (self->river, RiverSample, i + 1);
		gfloat       dx = b->x - a->x, dz = b->z - a->z, l2 = dx * dx + dz * dz, t, px, pz, d;

		t = l2 > 1e-12f ? ((x - a->x) * dx + (z - a->z) * dz) / l2 : 0.0f;
		t = CLAMP (t, 0.0f, 1.0f);
		px = a->x + dx * t;
		pz = a->z + dz * t;
		d = sqrtf ((x - px) * (x - px) + (z - pz) * (z - pz));
		if (d < best)
		{
			best = d;
			*out_y = a->y + (b->y - a->y) * t;
			*out_w = a->w + (b->w - a->w) * t;
		}
	}
	return best;
}

/* Even-odd point in polygon on XZ, and the distance to its edges. */
static gboolean
lake_contains (LrgWaterBody *self,
               gfloat        x,
               gfloat        z,
               gfloat       *out_edge)
{
	guint    n = self->points->len, i, j;
	gboolean inside = FALSE;
	gfloat   best = G_MAXFLOAT;

	for (i = 0, j = n - 1; i < n; j = i++)
	{
		const gfloat *a = point_at (self, i), *b = point_at (self, j);
		gfloat        dx = b[0] - a[0], dz = b[2] - a[2], l2 = dx * dx + dz * dz, t, px, pz;

		if (((a[2] > z) != (b[2] > z)) && (x < (b[0] - a[0]) * (z - a[2]) / (b[2] - a[2]) + a[0]))
			inside = !inside;
		t = l2 > 1e-12f ? ((x - a[0]) * dx + (z - a[2]) * dz) / l2 : 0.0f;
		t = CLAMP (t, 0.0f, 1.0f);
		px = a[0] + dx * t;
		pz = a[2] + dz * t;
		best = MIN (best, sqrtf ((x - px) * (x - px) + (z - pz) * (z - pz)));
	}
	if (out_edge != NULL)
		*out_edge = best;
	return n >= 3 && inside;
}

/* Inside test with the rest level and the distance to the outline. */
static gboolean
shape_query (LrgWaterBody *self,
             gfloat        x,
             gfloat        z,
             gfloat       *out_level,
             gfloat       *out_edge)
{
	gfloat level = self->level, edge = 0.0f;
	gboolean inside = FALSE;

	switch (self->shape)
	{
	case LRG_WATER_BODY_PLANE:
	{
		gfloat hx = self->size[0] * 0.5f, hz = self->size[1] * 0.5f;

		inside = fabsf (x) <= hx && fabsf (z) <= hz;
		edge = MIN (hx - fabsf (x), hz - fabsf (z));
		break;
	}
	case LRG_WATER_BODY_LAKE:
		inside = lake_contains (self, x, z, &edge);
		break;
	case LRG_WATER_BODY_RIVER:
	{
		gfloat w, d = river_distance (self, x, z, &level, &w);

		inside = self->points->len >= 2 && d <= w * 0.5f;
		edge = w * 0.5f - d;
		break;
	}
	default:
		break;
	}
	if (out_level != NULL)
		*out_level = level;
	if (out_edge != NULL)
		*out_edge = edge;
	return inside;
}

gboolean
lrg_water_body_contains (LrgWaterBody *self,
                         gfloat        x,
                         gfloat        z,
                         gfloat       *out_level)
{
	g_return_val_if_fail (LRG_IS_WATER_BODY (self), FALSE);
	return shape_query (self, x, z, out_level, NULL);
}

gboolean
lrg_water_body_get_bounds (LrgWaterBody *self,
                           gfloat       *out_min,
                           gfloat       *out_max)
{
	guint i;

	g_return_val_if_fail (LRG_IS_WATER_BODY (self), FALSE);
	g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);
	if (self->shape == LRG_WATER_BODY_PLANE)
	{
		out_min[0] = -self->size[0] * 0.5f;
		out_max[0] = self->size[0] * 0.5f;
		out_min[2] = -self->size[1] * 0.5f;
		out_max[2] = self->size[1] * 0.5f;
		out_min[1] = out_max[1] = self->level;
		return TRUE;
	}
	if ((self->shape == LRG_WATER_BODY_LAKE && self->points->len < 3) ||
	    (self->shape == LRG_WATER_BODY_RIVER && self->points->len < 2))
		return FALSE;
	out_min[0] = out_min[1] = out_min[2] = G_MAXFLOAT;
	out_max[0] = out_max[1] = out_max[2] = -G_MAXFLOAT;
	if (self->shape == LRG_WATER_BODY_LAKE)
	{
		for (i = 0; i < self->points->len; i++)
		{
			const gfloat *p = point_at (self, i);

			out_min[0] = MIN (out_min[0], p[0]);
			out_max[0] = MAX (out_max[0], p[0]);
			out_min[2] = MIN (out_min[2], p[2]);
			out_max[2] = MAX (out_max[2], p[2]);
		}
		out_min[1] = out_max[1] = self->level;
		return TRUE;
	}
	ensure_river (self);
	for (i = 0; i < self->river->len; i++)
	{
		RiverSample *r = &g_array_index (self->river, RiverSample, i);
		gfloat       hw = r->w * 0.5f;

		out_min[0] = MIN (out_min[0], r->x - hw);
		out_max[0] = MAX (out_max[0], r->x + hw);
		out_min[1] = MIN (out_min[1], r->y);
		out_max[1] = MAX (out_max[1], r->y);
		out_min[2] = MIN (out_min[2], r->z - hw);
		out_max[2] = MAX (out_max[2], r->z + hw);
	}
	return TRUE;
}

/* ==========================================================================
 * Surface building
 * ========================================================================== */

void
lrg_water_body_apply_to_surface (LrgWaterBody    *self,
                                 LrgWaterSurface *surface)
{
	guint i;

	g_return_if_fail (LRG_IS_WATER_BODY (self));
	g_return_if_fail (LRG_IS_WATER_SURFACE (surface));
	for (i = 0; i < self->wave_count; i++)
		lrg_water_surface_set_wave (surface, i, &self->waves[i]);
	lrg_water_surface_set_wave_count (surface, self->wave_count);
	lrg_water_surface_set_wave_scale (surface, self->wave_scale);
	lrg_water_surface_set_colors (surface, &self->shallow, &self->deep, &self->foam);
	lrg_water_surface_set_optics (surface, self->optics[0], self->optics[1], self->optics[2], self->optics[3]);
}

typedef struct
{
	LrgWaterBody       *self;
	gfloat              ox;
	gfloat              oz;
	LrgWaterGroundFunc  ground;
	gpointer            ground_data;
} FoamCtx;

/* The shore factor at a draw-space vertex. */
static gfloat
foam_at (gfloat   x,
         gfloat   z,
         gpointer user_data)
{
	FoamCtx *ctx = (FoamCtx *) user_data;
	gfloat   lx = x - ctx->ox, lz = z - ctx->oz, level, edge, band = 0.0f, depth = 0.0f, g;

	shape_query (ctx->self, lx, lz, &level, &edge);
	if (ctx->self->foam_width > 0.0f)
		band = 1.0f - CLAMP (edge / ctx->self->foam_width, 0.0f, 1.0f);
	if (ctx->ground != NULL && ctx->self->foam_depth > 0.0f && ctx->ground (lx, lz, &g, ctx->ground_data))
		depth = 1.0f - CLAMP ((level - g) / ctx->self->foam_depth, 0.0f, 1.0f);
	return MAX (band, depth);
}

gboolean
lrg_water_body_build_surface (LrgWaterBody        *self,
                              LrgWaterSurface     *surface,
                              gfloat               offset_x,
                              gfloat               offset_y,
                              gfloat               offset_z,
                              LrgWaterGroundFunc   ground_func,
                              gpointer             user_data,
                              GError             **error)
{
	gfloat   mn[3], mx[3], cell = self->cell_size;
	guint    cols, rows, tx, tz;
	gboolean any = FALSE;
	FoamCtx  ctx;

	g_return_val_if_fail (LRG_IS_WATER_BODY (self), FALSE);
	g_return_val_if_fail (LRG_IS_WATER_SURFACE (surface), FALSE);

	lrg_water_surface_clear (surface);
	lrg_water_body_apply_to_surface (self, surface);
	if (!lrg_water_body_get_bounds (self, mn, mx))
	{
		g_set_error (error, LRG_WATER_BODY_ERROR, LRG_WATER_BODY_ERROR_INVALID,
		             "a %s needs at least %u points", self->shape == LRG_WATER_BODY_LAKE ? "lake" : "river",
		             self->shape == LRG_WATER_BODY_LAKE ? 3u : 2u);
		return FALSE;
	}
	cols = MAX (1u, (guint) ceilf ((mx[0] - mn[0]) / cell));
	rows = MAX (1u, (guint) ceilf ((mx[2] - mn[2]) / cell));
	ctx.self = self;
	ctx.ox = offset_x;
	ctx.oz = offset_z;
	ctx.ground = ground_func;
	ctx.ground_data = user_data;

	for (tz = 0; tz < rows; tz += TILE_CELLS)
		for (tx = 0; tx < cols; tx += TILE_CELLS)
		{
			guint              tc = MIN ((guint) TILE_CELLS, cols - tx), tr = MIN ((guint) TILE_CELLS, rows - tz);
			g_autofree gfloat *heights = g_new (gfloat, (gsize) (tc + 1) * (tr + 1));
			g_autofree guint8 *wet = g_new0 (guint8, (gsize) tc * tr);
			gfloat             x0 = mn[0] + (gfloat) tx * cell, z0 = mn[2] + (gfloat) tz * cell;
			guint              i, j, n_wet = 0;
			g_autoptr(GError)  local = NULL;

			/* wet cells: centre inside the outline */
			for (j = 0; j < tr; j++)
				for (i = 0; i < tc; i++)
					if (shape_query (self, x0 + ((gfloat) i + 0.5f) * cell, z0 + ((gfloat) j + 0.5f) * cell, NULL,
					                 NULL))
					{
						wet[(gsize) j * tc + i] = 1;
						n_wet++;
					}
			if (n_wet == 0)
				continue;

			/* rest heights per vertex */
			for (j = 0; j <= tr; j++)
				for (i = 0; i <= tc; i++)
				{
					gfloat level;

					shape_query (self, x0 + (gfloat) i * cell, z0 + (gfloat) j * cell, &level, NULL);
					heights[(gsize) j * (tc + 1) + i] = level + offset_y;
				}
			if (!lrg_water_surface_add_heightfield (surface, x0 + offset_x, z0 + offset_z,
			                                        x0 + (gfloat) tc * cell + offset_x,
			                                        z0 + (gfloat) tr * cell + offset_z, tc, tr, heights, wet, foam_at,
			                                        &ctx, &local))
			{
				g_set_error (error, LRG_WATER_BODY_ERROR, LRG_WATER_BODY_ERROR_INVALID, "%s", local->message);
				return FALSE;
			}
			any = TRUE;
		}
	if (!any)
		g_set_error_literal (error, LRG_WATER_BODY_ERROR, LRG_WATER_BODY_ERROR_INVALID,
		                     "the outline holds no cell (smaller than the cell size?)");
	return any;
}

/* ==========================================================================
 * YAML
 * ========================================================================== */

static gboolean
read_color (TerrainReader *rd,
            YamlNode      *node,
            const gchar   *path,
            GrlColor      *out)
{
	gfloat c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	guint  n, k;

	if (!_lrg_terrain_rd_numbers (rd, node, path, 3, 4, c, &n))
		return FALSE;
	for (k = 0; k < 4; k++)
		if (c[k] < 0.0f || c[k] > 1.0f)
			return _lrg_terrain_rd_fail (rd, path, "colour components must lie in 0 .. 1");
	out->r = (guint8) lrintf (c[0] * 255.0f);
	out->g = (guint8) lrintf (c[1] * 255.0f);
	out->b = (guint8) lrintf (c[2] * 255.0f);
	out->a = (guint8) lrintf (c[3] * 255.0f);
	return TRUE;
}

static YamlNode *
color_node (const GrlColor *c)
{
	gfloat v[4];

	v[0] = (gfloat) c->r / 255.0f;
	v[1] = (gfloat) c->g / 255.0f;
	v[2] = (gfloat) c->b / 255.0f;
	v[3] = (gfloat) c->a / 255.0f;
	return _lrg_terrain_float_list (v, 4);
}

static gboolean
read_wave (LrgWaterBody  *self,
           TerrainReader *rd,
           YamlNode      *node,
           guint          index)
{
	g_autofree gchar *base = g_strdup_printf ("/waves[%u]", index);
	YamlMapping      *map;
	LrgWaterWave      w;
	guint             i;

	if (!_lrg_terrain_rd_mapping (rd, node, base, &map))
		return FALSE;
	memset (&w, 0, sizeof w);
	w.direction[0] = 1.0f;
	w.wavelength = 1.0f;
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar      *key = yaml_mapping_get_key (map, i);
		YamlNode         *member = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", base, key);
		gboolean          ok = TRUE;

		if (g_strcmp0 (key, "direction") == 0)
			ok = _lrg_terrain_rd_numbers (rd, member, p, 2, 2, w.direction, NULL);
		else if (g_strcmp0 (key, "amplitude") == 0)
			ok = _lrg_terrain_rd_number (rd, member, p, 0.0, 1000.0, &w.amplitude);
		else if (g_strcmp0 (key, "wavelength") == 0)
			ok = _lrg_terrain_rd_number (rd, member, p, 0.001, 100000.0, &w.wavelength);
		else if (g_strcmp0 (key, "speed") == 0)
			ok = _lrg_terrain_rd_number (rd, member, p, -10000.0, 10000.0, &w.speed);
		else if (g_strcmp0 (key, "steepness") == 0)
			ok = _lrg_terrain_rd_number (rd, member, p, 0.0, 1.0, &w.steepness);
		else
			ok = _lrg_terrain_rd_fail (rd, p, "unknown wave key");
		if (!ok)
			return FALSE;
	}
	lrg_water_body_set_wave (self, index, &w);
	return TRUE;
}

static gboolean
read_document (LrgWaterBody  *self,
               TerrainReader *rd,
               YamlNode      *root)
{
	YamlMapping *map;
	guint        i, j;
	gboolean     have_shape = FALSE;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		return _lrg_terrain_rd_fail (rd, "/", "a water body must be a YAML mapping");
	map = yaml_node_get_mapping (root);
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar      *key = yaml_mapping_get_key (map, i);
		YamlNode         *member = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("/%s", key);
		gint              shape;
		gfloat            v[4];

		if (g_strcmp0 (key, "version") == 0)
		{
			guint ver;

			if (!_lrg_terrain_rd_uint (rd, member, p, 1, G_MAXUINT, &ver))
				return FALSE;
			if (ver > LRG_WATER_BODY_FORMAT_VERSION)
				return _lrg_terrain_rd_fail (rd, p, "version %u is newer than this build reads (%u)", ver,
				                             (guint) LRG_WATER_BODY_FORMAT_VERSION);
		}
		else if (g_strcmp0 (key, "name") == 0)
		{
			const gchar *s;

			if (!_lrg_terrain_rd_string (rd, member, p, &s))
				return FALSE;
			lrg_water_body_set_name (self, s);
		}
		else if (g_strcmp0 (key, "shape") == 0)
		{
			if (!_lrg_terrain_rd_enum (rd, member, p, LRG_TYPE_WATER_BODY_SHAPE, &shape))
				return FALSE;
			self->shape = (LrgWaterBodyShape) shape;
			have_shape = TRUE;
		}
		else if (g_strcmp0 (key, "level") == 0)
		{
			if (!_lrg_terrain_rd_number (rd, member, p, -1.0e7, 1.0e7, &self->level))
				return FALSE;
		}
		else if (g_strcmp0 (key, "size") == 0)
		{
			if (!_lrg_terrain_rd_numbers (rd, member, p, 2, 2, v, NULL))
				return FALSE;
			if (!(v[0] > 0.0f) || !(v[1] > 0.0f))
				return _lrg_terrain_rd_fail (rd, p, "expected two sizes above 0");
			self->size[0] = v[0];
			self->size[1] = v[1];
		}
		else if (g_strcmp0 (key, "points") == 0)
		{
			YamlSequence *seq;

			if (!_lrg_terrain_rd_sequence (rd, member, p, &seq))
				return FALSE;
			if (yaml_sequence_get_length (seq) > LRG_WATER_BODY_MAX_POINTS)
				return _lrg_terrain_rd_fail (rd, p, "at most %u points", (guint) LRG_WATER_BODY_MAX_POINTS);
			for (j = 0; j < yaml_sequence_get_length (seq); j++)
			{
				g_autofree gchar *pp = g_strdup_printf ("%s[%u]", p, j);
				guint             n;

				if (!_lrg_terrain_rd_numbers (rd, yaml_sequence_get_element (seq, j), pp, 2, 4, v, &n))
					return FALSE;
				if (n == 3)
					return _lrg_terrain_rd_fail (rd, pp, "expected [x, z] or [x, y, z, width]");
				if (n == 2)
					lrg_water_body_add_point (self, v[0], 0.0f, v[1], 1.0f);
				else
				{
					if (!(v[3] > 0.0f))
						return _lrg_terrain_rd_fail (rd, pp, "the width must be above 0");
					lrg_water_body_add_point (self, v[0], v[1], v[2], v[3]);
				}
			}
		}
		else if (g_strcmp0 (key, "cell_size") == 0)
		{
			if (!_lrg_terrain_rd_number (rd, member, p, 0.05, 64.0, &self->cell_size))
				return FALSE;
		}
		else if (g_strcmp0 (key, "foam") == 0)
		{
			if (!_lrg_terrain_rd_numbers (rd, member, p, 2, 2, v, NULL))
				return FALSE;
			if (v[0] < 0.0f || v[1] < 0.0f)
				return _lrg_terrain_rd_fail (rd, p, "expected a width and a depth >= 0");
			self->foam_width = v[0];
			self->foam_depth = v[1];
		}
		else if (g_strcmp0 (key, "waves") == 0)
		{
			YamlSequence *seq;

			if (!_lrg_terrain_rd_sequence (rd, member, p, &seq))
				return FALSE;
			if (yaml_sequence_get_length (seq) > LRG_WATER_SURFACE_MAX_WAVES)
				return _lrg_terrain_rd_fail (rd, p, "at most %u waves", (guint) LRG_WATER_SURFACE_MAX_WAVES);
			self->wave_count = 0;
			for (j = 0; j < yaml_sequence_get_length (seq); j++)
				if (!read_wave (self, rd, yaml_sequence_get_element (seq, j), j))
					return FALSE;
			self->wave_count = yaml_sequence_get_length (seq);
		}
		else if (g_strcmp0 (key, "wave_scale") == 0)
		{
			if (!_lrg_terrain_rd_number (rd, member, p, 0.0, 1000.0, &self->wave_scale))
				return FALSE;
		}
		else if (g_strcmp0 (key, "colors") == 0)
		{
			YamlMapping *cm;

			if (!_lrg_terrain_rd_mapping (rd, member, p, &cm))
				return FALSE;
			for (j = 0; j < yaml_mapping_get_size (cm); j++)
			{
				const gchar      *ck = yaml_mapping_get_key (cm, j);
				g_autofree gchar *cp = g_strdup_printf ("%s/%s", p, ck);
				GrlColor         *slot;

				if (g_strcmp0 (ck, "shallow") == 0)
					slot = &self->shallow;
				else if (g_strcmp0 (ck, "deep") == 0)
					slot = &self->deep;
				else if (g_strcmp0 (ck, "foam") == 0)
					slot = &self->foam;
				else
					return _lrg_terrain_rd_fail (rd, cp, "expected shallow, deep or foam");
				if (!read_color (rd, yaml_mapping_get_value (cm, j), cp, slot))
					return FALSE;
			}
		}
		else if (g_strcmp0 (key, "optics") == 0)
		{
			YamlMapping *om;

			if (!_lrg_terrain_rd_mapping (rd, member, p, &om))
				return FALSE;
			for (j = 0; j < yaml_mapping_get_size (om); j++)
			{
				const gchar      *ok = yaml_mapping_get_key (om, j);
				g_autofree gchar *op = g_strdup_printf ("%s/%s", p, ok);
				YamlNode         *ov = yaml_mapping_get_value (om, j);
				gboolean          good;

				if (g_strcmp0 (ok, "transparency") == 0)
					good = _lrg_terrain_rd_number (rd, ov, op, 0.0, 1.0, &self->optics[0]);
				else if (g_strcmp0 (ok, "reflection") == 0)
					good = _lrg_terrain_rd_number (rd, ov, op, 0.0, 1.0, &self->optics[1]);
				else if (g_strcmp0 (ok, "refraction") == 0)
					good = _lrg_terrain_rd_number (rd, ov, op, 0.0, 1000.0, &self->optics[2]);
				else if (g_strcmp0 (ok, "reflect_distance") == 0)
					good = _lrg_terrain_rd_number (rd, ov, op, 0.001, 1.0e6, &self->optics[3]);
				else
					good = _lrg_terrain_rd_fail (rd, op,
					                             "expected transparency, reflection, refraction or reflect_distance");
				if (!good)
					return FALSE;
			}
		}
		else
			_lrg_terrain_keep_extra (&self->extra, key, member);
	}
	if (!have_shape)
		return _lrg_terrain_rd_fail (rd, "/shape", "missing (not a water body)");
	return TRUE;
}

static LrgWaterBody *
load_text (const gchar  *data,
           gssize        length,
           const gchar  *source,
           GError      **error)
{
	g_autoptr(LrgWaterBody) self = lrg_water_body_new (LRG_WATER_BODY_PLANE);
	g_autoptr(YamlParser)   parser = yaml_parser_new ();
	g_autoptr(GError)       local = NULL;
	TerrainReader           rd;

	if (!yaml_parser_load_from_data (parser, data != NULL ? data : "", length, &local))
	{
		g_set_error (error, LRG_WATER_BODY_ERROR, LRG_WATER_BODY_ERROR_PARSE, "%s: %s", source, local->message);
		return NULL;
	}
	rd.source = source;
	rd.domain = LRG_WATER_BODY_ERROR;
	rd.code = LRG_WATER_BODY_ERROR_PARSE;
	rd.error = error;
	if (!read_document (self, &rd, yaml_parser_get_root (parser)))
		return NULL;
	return g_steal_pointer (&self);
}

LrgWaterBody *
lrg_water_body_new_from_data (const gchar  *data,
                              gssize        length,
                              GError      **error)
{
	return load_text (data, length, "<data>", error);
}

LrgWaterBody *
lrg_water_body_new_from_file (const gchar  *path,
                              GError      **error)
{
	g_autofree gchar *contents = NULL;
	g_autoptr(GError) local = NULL;
	gsize             len = 0;

	g_return_val_if_fail (path != NULL, NULL);
	if (!g_file_get_contents (path, &contents, &len, &local))
	{
		g_set_error (error, LRG_WATER_BODY_ERROR, LRG_WATER_BODY_ERROR_IO, "%s", local->message);
		return NULL;
	}
	return load_text (contents, (gssize) len, path, error);
}

gchar *
lrg_water_body_to_data (LrgWaterBody  *self,
                        gsize         *length,
                        GError       **error)
{
	g_autoptr(YamlNode) root = yaml_node_new_mapping (NULL);
	YamlMapping        *map = yaml_node_get_mapping (root);
	YamlNode           *node;
	YamlMapping        *m;
	YamlSequence       *seq;
	gfloat              v[4];
	guint               i;

	g_return_val_if_fail (LRG_IS_WATER_BODY (self), NULL);

	_lrg_terrain_set_member (map, "version", lrg_data_yaml_int (LRG_WATER_BODY_FORMAT_VERSION));
	if (self->name != NULL)
		_lrg_terrain_set_member (map, "name", lrg_data_yaml_string (self->name));
	_lrg_terrain_set_member (map, "shape",
	                         lrg_data_yaml_string (_lrg_terrain_enum_nick (LRG_TYPE_WATER_BODY_SHAPE,
	                                                                       (gint) self->shape)));
	if (self->shape != LRG_WATER_BODY_RIVER)
		_lrg_terrain_set_member (map, "level", lrg_data_yaml_float (self->level));
	if (self->shape == LRG_WATER_BODY_PLANE)
		_lrg_terrain_set_member (map, "size", _lrg_terrain_float_list (self->size, 2));
	if (self->points->len > 0)
	{
		node = yaml_node_new_sequence (NULL);
		seq = yaml_node_get_sequence (node);
		for (i = 0; i < self->points->len; i++)
		{
			const gfloat *p = point_at (self, i);
			YamlNode     *pn;

			if (self->shape == LRG_WATER_BODY_RIVER)
				pn = _lrg_terrain_float_list (p, 4);
			else
			{
				v[0] = p[0];
				v[1] = p[2];
				pn = _lrg_terrain_float_list (v, 2);
			}
			yaml_sequence_add_element (seq, pn);
			yaml_node_unref (pn);
		}
		_lrg_terrain_set_member (map, "points", node);
	}
	_lrg_terrain_set_member (map, "cell_size", lrg_data_yaml_float (self->cell_size));
	v[0] = self->foam_width;
	v[1] = self->foam_depth;
	_lrg_terrain_set_member (map, "foam", _lrg_terrain_float_list (v, 2));

	node = yaml_node_new_sequence (NULL);
	seq = yaml_node_get_sequence (node);
	for (i = 0; i < self->wave_count; i++)
	{
		YamlNode    *wn = yaml_node_new_mapping (NULL);
		YamlMapping *wm = yaml_node_get_mapping (wn);

		_lrg_terrain_set_member (wm, "direction", _lrg_terrain_float_list (self->waves[i].direction, 2));
		_lrg_terrain_set_member (wm, "amplitude", lrg_data_yaml_float (self->waves[i].amplitude));
		_lrg_terrain_set_member (wm, "wavelength", lrg_data_yaml_float (self->waves[i].wavelength));
		_lrg_terrain_set_member (wm, "speed", lrg_data_yaml_float (self->waves[i].speed));
		_lrg_terrain_set_member (wm, "steepness", lrg_data_yaml_float (self->waves[i].steepness));
		yaml_sequence_add_element (seq, wn);
		yaml_node_unref (wn);
	}
	_lrg_terrain_set_member (map, "waves", node);
	_lrg_terrain_set_member (map, "wave_scale", lrg_data_yaml_float (self->wave_scale));

	node = yaml_node_new_mapping (NULL);
	m = yaml_node_get_mapping (node);
	_lrg_terrain_set_member (m, "shallow", color_node (&self->shallow));
	_lrg_terrain_set_member (m, "deep", color_node (&self->deep));
	_lrg_terrain_set_member (m, "foam", color_node (&self->foam));
	_lrg_terrain_set_member (map, "colors", node);

	node = yaml_node_new_mapping (NULL);
	m = yaml_node_get_mapping (node);
	_lrg_terrain_set_member (m, "transparency", lrg_data_yaml_float (self->optics[0]));
	_lrg_terrain_set_member (m, "reflection", lrg_data_yaml_float (self->optics[1]));
	_lrg_terrain_set_member (m, "refraction", lrg_data_yaml_float (self->optics[2]));
	_lrg_terrain_set_member (m, "reflect_distance", lrg_data_yaml_float (self->optics[3]));
	_lrg_terrain_set_member (map, "optics", node);

	_lrg_terrain_write_extra (map, self->extra);
	return lrg_data_yaml_to_data (root, length, error);
}

gboolean
lrg_water_body_save_to_file (LrgWaterBody  *self,
                             const gchar   *path,
                             GError       **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;
	gsize             len = 0;

	g_return_val_if_fail (LRG_IS_WATER_BODY (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	text = lrg_water_body_to_data (self, &len, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, (gssize) len, &local))
	{
		g_set_error (error, LRG_WATER_BODY_ERROR, LRG_WATER_BODY_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return TRUE;
}
