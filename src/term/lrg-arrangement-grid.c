/* lrg-arrangement-grid.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <math.h>
#include "lrg-arrangement-grid.h"
#include "lrg-scene-arrangement.h"
#include "lrg-scene-panel.h"

/* Cell height in world units, and the gap between cells. */
#define LRG_GRID_HEIGHT 2.6f
#define LRG_GRID_GAP    0.3f

struct _LrgArrangementGrid
{
	GObject parent_instance;

	guint  columns;  /* 0 = automatic */
	gfloat curve;    /* bend radius, 0 = flat */
};

static void lrg_arrangement_grid_iface_init (LrgSceneArrangementInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgArrangementGrid, lrg_arrangement_grid, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_SCENE_ARRANGEMENT,
                                                      lrg_arrangement_grid_iface_init))

static const gchar *
grid_get_id (LrgSceneArrangement *self)
{
	(void) self;
	return "grid";
}

static gfloat
panel_aspect (LrgScenePanel *p)
{
	gint sw, sh;

	lrg_scene_panel_get_source_rect (p, NULL, NULL, &sw, &sh);
	return (sw > 0 && sh > 0) ? (gfloat) sw / (gfloat) sh : 1.6f;
}

/*
 * 1. Sort a copy of the panels by slot (stable).
 * 2. Pick the column count: the configured one, or ceil(sqrt(n)).
 * 3. Assign (row, column) in reading order; a change of slot group starts a
 *    new row, so groups form bands.
 * 4. Cells are as wide as the widest panel, so columns line up; every panel
 *    keeps its own aspect inside its cell.
 * 5. The wall is centred on the origin.  With a curve radius R, a column at
 *    flat offset x moves onto the circle around (0, 0, R): angle a = x / R,
 *    position (R sin a, y, R (1 - cos a)), turned by -a to face the viewer.
 */
static void
grid_layout (LrgSceneArrangement *arrangement,
             GPtrArray           *panels,
             gint                 frame_width,
             gint                 frame_height,
             gfloat               scale)
{
	LrgArrangementGrid *self = LRG_ARRANGEMENT_GRID (arrangement);
	g_autoptr (GPtrArray) sorted = NULL;
	g_autofree guint *row = NULL;
	g_autofree guint *col = NULL;
	gfloat max_aspect = 0.0f, cell_w, step_x, step_y;
	guint n, i, cols, rows, r, c;

	(void) frame_width;
	(void) frame_height;
	(void) scale;

	n = panels->len;
	if (n == 0)
		return;

	/* 1. */
	/* A borrowed copy: g_ptr_array_copy() would inherit the caller's element
	   free func and drop references it never took. */
	sorted = g_ptr_array_sized_new (n);
	for (i = 0; i < n; i++)
		g_ptr_array_add (sorted, g_ptr_array_index (panels, i));
	g_ptr_array_sort_values (sorted, lrg_scene_panel_compare_slot);

	/* 2. */
	cols = self->columns > 0 ? self->columns
	                         : (guint) ceilf (sqrtf ((gfloat) n));
	cols = MAX (cols, 1);

	/* 3. */
	row = g_new0 (guint, n);
	col = g_new0 (guint, n);
	r = 0;
	c = 0;
	for (i = 0; i < n; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (sorted, i);

		if (i > 0)
		{
			gboolean new_group =
				lrg_scene_panel_get_group (p)
				!= lrg_scene_panel_get_group (g_ptr_array_index (sorted, i - 1));

			c++;
			if (c >= cols || new_group)
			{
				r++;
				c = 0;
			}
		}
		row[i] = r;
		col[i] = c;
		max_aspect = MAX (max_aspect, panel_aspect (p));
	}
	rows = r + 1;

	/* 4. */
	cell_w = LRG_GRID_HEIGHT * max_aspect;
	step_x = cell_w + LRG_GRID_GAP;
	step_y = LRG_GRID_HEIGHT + LRG_GRID_GAP;

	/* 5. */
	for (i = 0; i < n; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (sorted, i);
		gfloat x = ((gfloat) col[i] - 0.5f * (gfloat) (cols - 1)) * step_x;
		gfloat y = (0.5f * (gfloat) (rows - 1) - (gfloat) row[i]) * step_y;
		gfloat px = x, pz = 0.0f, yaw = 0.0f;

		if (self->curve > 0.0f)
		{
			gfloat a = x / self->curve;

			px = self->curve * sinf (a);
			pz = self->curve * (1.0f - cosf (a));
			yaw = -a * (gfloat) (180.0 / G_PI);
		}

		lrg_scene_panel_set_target (p, px, y, pz, yaw,
		                            LRG_GRID_HEIGHT * panel_aspect (p),
		                            LRG_GRID_HEIGHT);
	}
}

static void
lrg_arrangement_grid_iface_init (LrgSceneArrangementInterface *iface)
{
	iface->get_id = grid_get_id;
	iface->layout = grid_layout;
	iface->wants_continuous = NULL;
}

static void
lrg_arrangement_grid_class_init (LrgArrangementGridClass *klass)
{
	(void) klass;
}

static void
lrg_arrangement_grid_init (LrgArrangementGrid *self)
{
	self->columns = 0;
	self->curve = 14.0f;
}

LrgArrangementGrid *
lrg_arrangement_grid_new (void)
{
	return g_object_new (LRG_TYPE_ARRANGEMENT_GRID, NULL);
}

void
lrg_arrangement_grid_set_columns (LrgArrangementGrid *self,
                                  guint               columns)
{
	g_return_if_fail (LRG_IS_ARRANGEMENT_GRID (self));
	self->columns = columns;
}

guint
lrg_arrangement_grid_get_columns (LrgArrangementGrid *self)
{
	g_return_val_if_fail (LRG_IS_ARRANGEMENT_GRID (self), 0);
	return self->columns;
}

void
lrg_arrangement_grid_set_curve (LrgArrangementGrid *self,
                                gfloat              radius)
{
	g_return_if_fail (LRG_IS_ARRANGEMENT_GRID (self));
	self->curve = MAX (radius, 0.0f);
}

gfloat
lrg_arrangement_grid_get_curve (LrgArrangementGrid *self)
{
	g_return_val_if_fail (LRG_IS_ARRANGEMENT_GRID (self), 0.0f);
	return self->curve;
}
