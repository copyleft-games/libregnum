/* lrg-procgen-noise.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural generator: noise heightmap terrain.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PROCGEN

#include "lrg-procgen-noise.h"
#include "../lrg-log.h"

struct _LrgProcgenNoise
{
	LrgProcgenGenerator parent_instance;

	guint    octaves;
	guint    scale;
	guint    water_percent;
	guint    wall_percent;
};

G_DEFINE_FINAL_TYPE (LrgProcgenNoise, lrg_procgen_noise, LRG_TYPE_PROCGEN_GENERATOR)

enum
{
	PROP_0,
	PROP_OCTAVES,
	PROP_SCALE,
	PROP_WATER_PERCENT,
	PROP_WALL_PERCENT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* an integer hash of a lattice point (bit-identical everywhere) */
static guint32
lattice_hash (guint32 seed,
              gint32  x,
              gint32  y)
{
	guint32 h = seed ^ ((guint32) x * 0x27d4eb2du) ^ ((guint32) y * 0x165667b1u);

	h ^= h >> 15;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	h *= 0xc2b2ae35u;
	h ^= h >> 16;
	return h;
}

/* smoothstep of a Q16 fraction, in Q16 */
static gint64
smooth_q16 (gint64 t)
{
	return (t * t * (3 * 65536 - 2 * t)) >> 32;
}

/* value noise in Q16 [0, 65535] with a lattice every @period cells */
static gint64
value_noise (guint32 seed,
             gint    x,
             gint    y,
             gint    period)
{
	gint ix = x / period;
	gint iy = y / period;
	gint64 sx = smooth_q16 ((gint64) (x % period) * 65536 / period);
	gint64 sy = smooth_q16 ((gint64) (y % period) * 65536 / period);
	gint64 v00 = lattice_hash (seed, ix, iy) & 0xffff;
	gint64 v10 = lattice_hash (seed, ix + 1, iy) & 0xffff;
	gint64 v01 = lattice_hash (seed, ix, iy + 1) & 0xffff;
	gint64 v11 = lattice_hash (seed, ix + 1, iy + 1) & 0xffff;
	gint64 top = v00 + (((v10 - v00) * sx) >> 16);
	gint64 bottom = v01 + (((v11 - v01) * sx) >> 16);

	return top + (((bottom - top) * sy) >> 16);
}

static gboolean
lrg_procgen_noise_generate (LrgProcgenGenerator  *generator,
                            LrgProcgenGrid       *grid,
                            LrgRandomStream      *rng,
                            GError              **error)
{
	LrgProcgenNoise *self = LRG_PROCGEN_NOISE (generator);
	guint32 seeds[16];
	gint w = (gint) lrg_procgen_grid_get_width (grid);
	gint h = (gint) lrg_procgen_grid_get_height (grid);
	gint x;
	gint y;
	guint o;

	if (self->wall_percent < self->water_percent)
	{
		g_set_error_literal (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG,
		                     "noise: wall-percent must be >= water-percent");
		return FALSE;
	}

	/* one lattice seed per octave */
	for (o = 0; o < self->octaves; o++)
		seeds[o] = lrg_random_stream_next_uint (rng);

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			gint64 acc = 0;
			gint64 norm = 0;
			gint64 amp = 65536;
			gint period = (gint) self->scale;
			gint64 height;
			guint8 tag;

			/* fractal sum: each octave halves the period and the amplitude */
			for (o = 0; o < self->octaves; o++)
			{
				acc += value_noise (seeds[o], x, y, period) * amp;
				norm += amp;
				amp /= 2;
				period = MAX (period / 2, 1);
			}
			height = acc / norm;

			/* thresholds; the border is always wall */
			if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
				tag = LRG_PROCGEN_CELL_WALL;
			else if (height * 100 < (gint64) self->water_percent * 65536)
				tag = LRG_PROCGEN_CELL_WATER;
			else if (height * 100 >= (gint64) self->wall_percent * 65536)
				tag = LRG_PROCGEN_CELL_WALL;
			else
				tag = LRG_PROCGEN_CELL_FLOOR;
			lrg_procgen_grid_set_cell (grid, x, y, tag);
			lrg_procgen_grid_set_height_at (grid, x, y, (gfloat) height / 65535.0f);
		}
	return TRUE;
}

static void
lrg_procgen_noise_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgProcgenNoise *self = LRG_PROCGEN_NOISE (object);

	switch (prop_id)
	{
	case PROP_OCTAVES:
		g_value_set_uint (value, self->octaves);
		break;
	case PROP_SCALE:
		g_value_set_uint (value, self->scale);
		break;
	case PROP_WATER_PERCENT:
		g_value_set_uint (value, self->water_percent);
		break;
	case PROP_WALL_PERCENT:
		g_value_set_uint (value, self->wall_percent);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_noise_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgProcgenNoise *self = LRG_PROCGEN_NOISE (object);

	switch (prop_id)
	{
	case PROP_OCTAVES:
		lrg_procgen_noise_set_octaves (self, g_value_get_uint (value));
		break;
	case PROP_SCALE:
		lrg_procgen_noise_set_scale (self, g_value_get_uint (value));
		break;
	case PROP_WATER_PERCENT:
		lrg_procgen_noise_set_water_percent (self, g_value_get_uint (value));
		break;
	case PROP_WALL_PERCENT:
		lrg_procgen_noise_set_wall_percent (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_noise_class_init (LrgProcgenNoiseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgProcgenGeneratorClass *generator_class = LRG_PROCGEN_GENERATOR_CLASS (klass);

	object_class->get_property = lrg_procgen_noise_get_property;
	object_class->set_property = lrg_procgen_noise_set_property;
	generator_class->generate = lrg_procgen_noise_generate;

	/**
	 * LrgProcgenNoise:octaves:
	 *
	 * Noise octaves.
	 */
	properties[PROP_OCTAVES] =
		g_param_spec_uint ("octaves", NULL, NULL,
		                   1, 16, 4,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenNoise:scale:
	 *
	 * Lattice period of the first octave, in cells.
	 */
	properties[PROP_SCALE] =
		g_param_spec_uint ("scale", NULL, NULL,
		                   1, 4096, 16,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenNoise:water-percent:
	 *
	 * Heights below this percentage become water.
	 */
	properties[PROP_WATER_PERCENT] =
		g_param_spec_uint ("water-percent", NULL, NULL,
		                   0, 100, 35,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenNoise:wall-percent:
	 *
	 * Heights at or above this percentage become wall.
	 */
	properties[PROP_WALL_PERCENT] =
		g_param_spec_uint ("wall-percent", NULL, NULL,
		                   0, 100, 70,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_procgen_noise_init (LrgProcgenNoise *self)
{
	self->octaves = 4;
	self->scale = 16;
	self->water_percent = 35;
	self->wall_percent = 70;
}

LrgProcgenNoise *
lrg_procgen_noise_new (guint width,
                       guint height)
{
	return g_object_new (LRG_TYPE_PROCGEN_NOISE, "width", width, "height", height, NULL);
}

guint
lrg_procgen_noise_get_octaves (LrgProcgenNoise *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_NOISE (self), 0);
	return self->octaves;
}

void
lrg_procgen_noise_set_octaves (LrgProcgenNoise *self,
                               guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_NOISE (self));
	g_return_if_fail (value >= 1 && value <= 16);
	if (self->octaves == value)
		return;
	self->octaves = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OCTAVES]);
}

guint
lrg_procgen_noise_get_scale (LrgProcgenNoise *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_NOISE (self), 0);
	return self->scale;
}

void
lrg_procgen_noise_set_scale (LrgProcgenNoise *self,
                             guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_NOISE (self));
	g_return_if_fail (value >= 1 && value <= 4096);
	if (self->scale == value)
		return;
	self->scale = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE]);
}

guint
lrg_procgen_noise_get_water_percent (LrgProcgenNoise *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_NOISE (self), 0);
	return self->water_percent;
}

void
lrg_procgen_noise_set_water_percent (LrgProcgenNoise *self,
                                     guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_NOISE (self));
	g_return_if_fail (value <= 100);
	if (self->water_percent == value)
		return;
	self->water_percent = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WATER_PERCENT]);
}

guint
lrg_procgen_noise_get_wall_percent (LrgProcgenNoise *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_NOISE (self), 0);
	return self->wall_percent;
}

void
lrg_procgen_noise_set_wall_percent (LrgProcgenNoise *self,
                                    guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_NOISE (self));
	g_return_if_fail (value <= 100);
	if (self->wall_percent == value)
		return;
	self->wall_percent = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WALL_PERCENT]);
}
