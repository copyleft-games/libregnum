/* lrg-procgen-caves.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural generator: cellular automaton caves.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PROCGEN

#include "lrg-procgen-caves.h"
#include "../lrg-log.h"

#include <string.h>

struct _LrgProcgenCaves
{
	LrgProcgenGenerator parent_instance;

	guint    fill_percent;
	guint    iterations;
	guint    birth_limit;
	guint    survival_limit;
	guint    min_region_size;
	gboolean connect;
	gboolean keep_largest;
};

G_DEFINE_FINAL_TYPE (LrgProcgenCaves, lrg_procgen_caves, LRG_TYPE_PROCGEN_GENERATOR)

enum
{
	PROP_0,
	PROP_FILL_PERCENT,
	PROP_ITERATIONS,
	PROP_BIRTH_LIMIT,
	PROP_SURVIVAL_LIMIT,
	PROP_MIN_REGION_SIZE,
	PROP_CONNECT,
	PROP_KEEP_LARGEST,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_procgen_caves_generate (LrgProcgenGenerator  *generator,
                            LrgProcgenGrid       *grid,
                            LrgRandomStream      *rng,
                            GError              **error)
{
	LrgProcgenCaves *self = LRG_PROCGEN_CAVES (generator);
	g_autofree guint8 *cur = NULL;
	g_autofree guint8 *next = NULL;
	gint w = (gint) lrg_procgen_grid_get_width (grid);
	gint h = (gint) lrg_procgen_grid_get_height (grid);
	gint x;
	gint y;
	guint it;

	if (w < 3 || h < 3)
	{
		g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_TOO_SMALL,
		             "caves: a %dx%d grid is smaller than 3x3", w, h);
		return FALSE;
	}

	/* --- random noise: 1 = wall; the border is always wall --- */
	cur = g_new (guint8, (gsize) w * h);
	next = g_new (guint8, (gsize) w * h);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			gboolean border = x == 0 || y == 0 || x == w - 1 || y == h - 1;

			cur[y * w + x] = border || lrg_random_stream_bounded (rng, 100) < self->fill_percent;
		}

	/* --- automaton steps: a wall survives with >= survival-limit wall
	 * neighbours, a floor turns to wall with >= birth-limit (Moore) --- */
	for (it = 0; it < self->iterations; it++)
	{
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++)
			{
				guint walls = 0;
				gint dx;
				gint dy;

				if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
				{
					next[y * w + x] = 1;
					continue;
				}
				for (dy = -1; dy <= 1; dy++)
					for (dx = -1; dx <= 1; dx++)
						if ((dx != 0 || dy != 0) && cur[(y + dy) * w + x + dx])
							walls++;
				next[y * w + x] = cur[y * w + x] ? walls >= self->survival_limit
				                                 : walls >= self->birth_limit;
			}
		memcpy (cur, next, (gsize) w * h);
	}

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			if (!cur[y * w + x])
				lrg_procgen_grid_set_cell (grid, x, y, LRG_PROCGEN_CELL_FLOOR);

	/* --- clean up: cull small caves, then join or reduce the rest --- */
	if (self->min_region_size > 0)
		lrg_procgen_grid_cull_regions (grid, self->min_region_size, LRG_PROCGEN_CELL_WALL);
	if (self->keep_largest)
		lrg_procgen_grid_keep_largest_region (grid, LRG_PROCGEN_CELL_WALL);
	else if (self->connect)
		lrg_procgen_grid_connect_regions (grid, LRG_PROCGEN_CELL_FLOOR);

	if (lrg_procgen_grid_count_walkable (grid) == 0)
	{
		g_set_error_literal (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_FAILED,
		                     "caves: no open cell left (fill-percent too high?)");
		return FALSE;
	}
	return TRUE;
}

static void
lrg_procgen_caves_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgProcgenCaves *self = LRG_PROCGEN_CAVES (object);

	switch (prop_id)
	{
	case PROP_FILL_PERCENT:
		g_value_set_uint (value, self->fill_percent);
		break;
	case PROP_ITERATIONS:
		g_value_set_uint (value, self->iterations);
		break;
	case PROP_BIRTH_LIMIT:
		g_value_set_uint (value, self->birth_limit);
		break;
	case PROP_SURVIVAL_LIMIT:
		g_value_set_uint (value, self->survival_limit);
		break;
	case PROP_MIN_REGION_SIZE:
		g_value_set_uint (value, self->min_region_size);
		break;
	case PROP_CONNECT:
		g_value_set_boolean (value, self->connect);
		break;
	case PROP_KEEP_LARGEST:
		g_value_set_boolean (value, self->keep_largest);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_caves_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgProcgenCaves *self = LRG_PROCGEN_CAVES (object);

	switch (prop_id)
	{
	case PROP_FILL_PERCENT:
		lrg_procgen_caves_set_fill_percent (self, g_value_get_uint (value));
		break;
	case PROP_ITERATIONS:
		lrg_procgen_caves_set_iterations (self, g_value_get_uint (value));
		break;
	case PROP_BIRTH_LIMIT:
		lrg_procgen_caves_set_birth_limit (self, g_value_get_uint (value));
		break;
	case PROP_SURVIVAL_LIMIT:
		lrg_procgen_caves_set_survival_limit (self, g_value_get_uint (value));
		break;
	case PROP_MIN_REGION_SIZE:
		lrg_procgen_caves_set_min_region_size (self, g_value_get_uint (value));
		break;
	case PROP_CONNECT:
		lrg_procgen_caves_set_connect (self, g_value_get_boolean (value));
		break;
	case PROP_KEEP_LARGEST:
		lrg_procgen_caves_set_keep_largest (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_caves_class_init (LrgProcgenCavesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgProcgenGeneratorClass *generator_class = LRG_PROCGEN_GENERATOR_CLASS (klass);

	object_class->get_property = lrg_procgen_caves_get_property;
	object_class->set_property = lrg_procgen_caves_set_property;
	generator_class->generate = lrg_procgen_caves_generate;

	/**
	 * LrgProcgenCaves:fill-percent:
	 *
	 * Chance, in percent, that a cell starts as wall.
	 */
	properties[PROP_FILL_PERCENT] =
		g_param_spec_uint ("fill-percent", NULL, NULL,
		                   0, 100, 45,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenCaves:iterations:
	 *
	 * Automaton steps.
	 */
	properties[PROP_ITERATIONS] =
		g_param_spec_uint ("iterations", NULL, NULL,
		                   0, 100, 5,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenCaves:birth-limit:
	 *
	 * A floor cell with at least this many wall neighbours becomes wall.
	 */
	properties[PROP_BIRTH_LIMIT] =
		g_param_spec_uint ("birth-limit", NULL, NULL,
		                   0, 9, 5,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenCaves:survival-limit:
	 *
	 * A wall cell with at least this many wall neighbours stays wall.
	 */
	properties[PROP_SURVIVAL_LIMIT] =
		g_param_spec_uint ("survival-limit", NULL, NULL,
		                   0, 9, 4,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenCaves:min-region-size:
	 *
	 * Caves with fewer cells are filled in (0 keeps all).
	 */
	properties[PROP_MIN_REGION_SIZE] =
		g_param_spec_uint ("min-region-size", NULL, NULL,
		                   0, G_MAXINT32, 16,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenCaves:connect:
	 *
	 * Whether to join the remaining caves with tunnels.
	 */
	properties[PROP_CONNECT] =
		g_param_spec_boolean ("connect", NULL, NULL,
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenCaves:keep-largest:
	 *
	 * Whether to keep only the largest cave (overrides connect).
	 */
	properties[PROP_KEEP_LARGEST] =
		g_param_spec_boolean ("keep-largest", NULL, NULL,
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_procgen_caves_init (LrgProcgenCaves *self)
{
	self->fill_percent = 45;
	self->iterations = 5;
	self->birth_limit = 5;
	self->survival_limit = 4;
	self->min_region_size = 16;
	self->connect = TRUE;
	self->keep_largest = FALSE;
}

LrgProcgenCaves *
lrg_procgen_caves_new (guint width,
                       guint height)
{
	return g_object_new (LRG_TYPE_PROCGEN_CAVES, "width", width, "height", height, NULL);
}

guint
lrg_procgen_caves_get_fill_percent (LrgProcgenCaves *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_CAVES (self), 0);
	return self->fill_percent;
}

void
lrg_procgen_caves_set_fill_percent (LrgProcgenCaves *self,
                                    guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_CAVES (self));
	g_return_if_fail (value <= 100);
	if (self->fill_percent == value)
		return;
	self->fill_percent = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_PERCENT]);
}

guint
lrg_procgen_caves_get_iterations (LrgProcgenCaves *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_CAVES (self), 0);
	return self->iterations;
}

void
lrg_procgen_caves_set_iterations (LrgProcgenCaves *self,
                                  guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_CAVES (self));
	g_return_if_fail (value <= 100);
	if (self->iterations == value)
		return;
	self->iterations = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ITERATIONS]);
}

guint
lrg_procgen_caves_get_birth_limit (LrgProcgenCaves *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_CAVES (self), 0);
	return self->birth_limit;
}

void
lrg_procgen_caves_set_birth_limit (LrgProcgenCaves *self,
                                   guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_CAVES (self));
	g_return_if_fail (value <= 9);
	if (self->birth_limit == value)
		return;
	self->birth_limit = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BIRTH_LIMIT]);
}

guint
lrg_procgen_caves_get_survival_limit (LrgProcgenCaves *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_CAVES (self), 0);
	return self->survival_limit;
}

void
lrg_procgen_caves_set_survival_limit (LrgProcgenCaves *self,
                                      guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_CAVES (self));
	g_return_if_fail (value <= 9);
	if (self->survival_limit == value)
		return;
	self->survival_limit = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SURVIVAL_LIMIT]);
}

guint
lrg_procgen_caves_get_min_region_size (LrgProcgenCaves *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_CAVES (self), 0);
	return self->min_region_size;
}

void
lrg_procgen_caves_set_min_region_size (LrgProcgenCaves *self,
                                       guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_CAVES (self));
	g_return_if_fail (value <= G_MAXINT32);
	if (self->min_region_size == value)
		return;
	self->min_region_size = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_REGION_SIZE]);
}

gboolean
lrg_procgen_caves_get_connect (LrgProcgenCaves *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_CAVES (self), FALSE);
	return self->connect;
}

void
lrg_procgen_caves_set_connect (LrgProcgenCaves *self,
                               gboolean value)
{
	g_return_if_fail (LRG_IS_PROCGEN_CAVES (self));
	if (self->connect == (value ? TRUE : FALSE))
		return;
	self->connect = (value ? TRUE : FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONNECT]);
}

gboolean
lrg_procgen_caves_get_keep_largest (LrgProcgenCaves *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_CAVES (self), FALSE);
	return self->keep_largest;
}

void
lrg_procgen_caves_set_keep_largest (LrgProcgenCaves *self,
                                    gboolean value)
{
	g_return_if_fail (LRG_IS_PROCGEN_CAVES (self));
	if (self->keep_largest == (value ? TRUE : FALSE))
		return;
	self->keep_largest = (value ? TRUE : FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KEEP_LARGEST]);
}
