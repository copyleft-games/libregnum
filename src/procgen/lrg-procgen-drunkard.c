/* lrg-procgen-drunkard.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural generator: drunkard's walk caves.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PROCGEN

#include "lrg-procgen-drunkard.h"
#include "../lrg-log.h"

struct _LrgProcgenDrunkard
{
	LrgProcgenGenerator parent_instance;

	guint    coverage_percent;
	guint    max_steps;
	guint    walk_length;
};

G_DEFINE_FINAL_TYPE (LrgProcgenDrunkard, lrg_procgen_drunkard, LRG_TYPE_PROCGEN_GENERATOR)

enum
{
	PROP_0,
	PROP_COVERAGE_PERCENT,
	PROP_MAX_STEPS,
	PROP_WALK_LENGTH,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_procgen_drunkard_generate (LrgProcgenGenerator  *generator,
                               LrgProcgenGrid       *grid,
                               LrgRandomStream      *rng,
                               GError              **error)
{
	static const gint dx[4] = { 0, 1, 0, -1 };
	static const gint dy[4] = { -1, 0, 1, 0 };
	LrgProcgenDrunkard *self = LRG_PROCGEN_DRUNKARD (generator);
	g_autoptr(GArray) carved = NULL;
	gint w = (gint) lrg_procgen_grid_get_width (grid);
	gint h = (gint) lrg_procgen_grid_get_height (grid);
	guint64 target;
	guint64 max_steps;
	guint64 steps = 0;
	guint walk = 0;
	gint x;
	gint y;
	guint32 cell;

	if (w < 3 || h < 3)
	{
		g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_TOO_SMALL,
		             "drunkard: a %dx%d grid is smaller than 3x3", w, h);
		return FALSE;
	}

	/* the share of the interior to open, and the step budget */
	target = MAX ((guint64) (w - 2) * (guint64) (h - 2) * self->coverage_percent / 100, 1);
	max_steps = self->max_steps > 0 ? self->max_steps : (guint64) w * (guint64) h * 50;

	/* start in the middle */
	carved = g_array_new (FALSE, FALSE, sizeof (guint32));
	x = w / 2;
	y = h / 2;
	lrg_procgen_grid_set_cell (grid, x, y, LRG_PROCGEN_CELL_FLOOR);
	cell = (guint32) (y * w + x);
	g_array_append_val (carved, cell);

	/* stagger, never onto the border, until enough is open */
	while (carved->len < target && steps < max_steps)
	{
		guint d = lrg_random_stream_bounded (rng, 4);
		gint nx = x + dx[d];
		gint ny = y + dy[d];

		steps++;
		if (nx >= 1 && ny >= 1 && nx <= w - 2 && ny <= h - 2)
		{
			x = nx;
			y = ny;
			if (lrg_procgen_grid_get_cell (grid, x, y) == LRG_PROCGEN_CELL_WALL)
			{
				lrg_procgen_grid_set_cell (grid, x, y, LRG_PROCGEN_CELL_FLOOR);
				cell = (guint32) (y * w + x);
				g_array_append_val (carved, cell);
			}
		}

		/* a new walker from a random open cell */
		if (self->walk_length > 0 && ++walk >= self->walk_length)
		{
			cell = g_array_index (carved, guint32, lrg_random_stream_bounded (rng, carved->len));
			x = (gint) (cell % (guint32) w);
			y = (gint) (cell / (guint32) w);
			walk = 0;
		}
	}
	return TRUE;
}

static void
lrg_procgen_drunkard_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	LrgProcgenDrunkard *self = LRG_PROCGEN_DRUNKARD (object);

	switch (prop_id)
	{
	case PROP_COVERAGE_PERCENT:
		g_value_set_uint (value, self->coverage_percent);
		break;
	case PROP_MAX_STEPS:
		g_value_set_uint (value, self->max_steps);
		break;
	case PROP_WALK_LENGTH:
		g_value_set_uint (value, self->walk_length);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_drunkard_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	LrgProcgenDrunkard *self = LRG_PROCGEN_DRUNKARD (object);

	switch (prop_id)
	{
	case PROP_COVERAGE_PERCENT:
		lrg_procgen_drunkard_set_coverage_percent (self, g_value_get_uint (value));
		break;
	case PROP_MAX_STEPS:
		lrg_procgen_drunkard_set_max_steps (self, g_value_get_uint (value));
		break;
	case PROP_WALK_LENGTH:
		lrg_procgen_drunkard_set_walk_length (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_drunkard_class_init (LrgProcgenDrunkardClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgProcgenGeneratorClass *generator_class = LRG_PROCGEN_GENERATOR_CLASS (klass);

	object_class->get_property = lrg_procgen_drunkard_get_property;
	object_class->set_property = lrg_procgen_drunkard_set_property;
	generator_class->generate = lrg_procgen_drunkard_generate;

	/**
	 * LrgProcgenDrunkard:coverage-percent:
	 *
	 * Share of the interior to open, in percent.
	 */
	properties[PROP_COVERAGE_PERCENT] =
		g_param_spec_uint ("coverage-percent", NULL, NULL,
		                   1, 100, 40,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenDrunkard:max-steps:
	 *
	 * Step budget (0 = 50 per cell).
	 */
	properties[PROP_MAX_STEPS] =
		g_param_spec_uint ("max-steps", NULL, NULL,
		                   0, G_MAXUINT, 0,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenDrunkard:walk-length:
	 *
	 * Steps before a new walker starts from a random open cell (0 = one walker).
	 */
	properties[PROP_WALK_LENGTH] =
		g_param_spec_uint ("walk-length", NULL, NULL,
		                   0, G_MAXUINT, 0,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_procgen_drunkard_init (LrgProcgenDrunkard *self)
{
	self->coverage_percent = 40;
	self->max_steps = 0;
	self->walk_length = 0;
}

LrgProcgenDrunkard *
lrg_procgen_drunkard_new (guint width,
                          guint height)
{
	return g_object_new (LRG_TYPE_PROCGEN_DRUNKARD, "width", width, "height", height, NULL);
}

guint
lrg_procgen_drunkard_get_coverage_percent (LrgProcgenDrunkard *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DRUNKARD (self), 0);
	return self->coverage_percent;
}

void
lrg_procgen_drunkard_set_coverage_percent (LrgProcgenDrunkard *self,
                                           guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_DRUNKARD (self));
	g_return_if_fail (value >= 1 && value <= 100);
	if (self->coverage_percent == value)
		return;
	self->coverage_percent = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COVERAGE_PERCENT]);
}

guint
lrg_procgen_drunkard_get_max_steps (LrgProcgenDrunkard *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DRUNKARD (self), 0);
	return self->max_steps;
}

void
lrg_procgen_drunkard_set_max_steps (LrgProcgenDrunkard *self,
                                    guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_DRUNKARD (self));
	if (self->max_steps == value)
		return;
	self->max_steps = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_STEPS]);
}

guint
lrg_procgen_drunkard_get_walk_length (LrgProcgenDrunkard *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DRUNKARD (self), 0);
	return self->walk_length;
}

void
lrg_procgen_drunkard_set_walk_length (LrgProcgenDrunkard *self,
                                      guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_DRUNKARD (self));
	if (self->walk_length == value)
		return;
	self->walk_length = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WALK_LENGTH]);
}
