/* lrg-actor-mover.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgActorMover: waypoint following with grid routes from LrgPathfinder,
 * blocked-start/blocked-target handling, timed and on-demand re-pathing,
 * facing with a turn rate, and the walk/idle/sit animation name.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PATHFIND

#include "lrg-actor-mover.h"
#include "lrg-nav-grid.h"
#include "../lrg-log.h"

#include <math.h>

#define MOVER_EPSILON (1e-4f)

typedef struct
{
	gfloat x;
	gfloat y;
	gfloat z;
} Waypoint;

struct _LrgActorMover
{
	GObject             parent_instance;

	LrgActorMoverPlane  plane;
	gfloat              x;
	gfloat              y;
	gfloat              z;
	gfloat              speed;
	gfloat              turn_speed;
	gfloat              facing;
	gfloat              desired_facing;

	gchar              *anim_walk;
	gchar              *anim_idle;
	gchar              *anim_sit;
	LrgActorMoverState  state;

	LrgPathfinder      *pathfinder;
	gfloat              cell_w;
	gfloat              cell_h;
	gfloat              origin_a;
	gfloat              origin_b;

	GArray             *waypoints;   /* Waypoint, next first */
	gboolean            sit_on_arrival;
	gboolean            grid_target; /* the route came from move_to on a grid */
	Waypoint            target;
	gfloat              repath_interval;
	gfloat              repath_timer;
};

G_DEFINE_FINAL_TYPE (LrgActorMover, lrg_actor_mover, G_TYPE_OBJECT)

enum
{
	SIGNAL_ARRIVED,
	SIGNAL_PATH_FAILED,
	SIGNAL_ANIMATION_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Enum types
 * ========================================================================== */

GType
lrg_actor_mover_state_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ACTOR_MOVER_IDLE, "LRG_ACTOR_MOVER_IDLE", "idle" },
			{ LRG_ACTOR_MOVER_MOVING, "LRG_ACTOR_MOVER_MOVING", "moving" },
			{ LRG_ACTOR_MOVER_SITTING, "LRG_ACTOR_MOVER_SITTING", "sitting" },
			{ LRG_ACTOR_MOVER_BLOCKED, "LRG_ACTOR_MOVER_BLOCKED", "blocked" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgActorMoverState"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_actor_mover_plane_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ACTOR_MOVER_PLANE_XY, "LRG_ACTOR_MOVER_PLANE_XY", "xy" },
			{ LRG_ACTOR_MOVER_PLANE_XZ, "LRG_ACTOR_MOVER_PLANE_XZ", "xz" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgActorMoverPlane"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Plane helpers
 * ========================================================================== */

static void
to_plane (LrgActorMover *self,
          gfloat         x,
          gfloat         y,
          gfloat         z,
          gfloat        *a,
          gfloat        *b)
{
	*a = x;
	*b = (self->plane == LRG_ACTOR_MOVER_PLANE_XZ) ? z : y;
}

/* A grid cell's centre at a given height (the off-plane coordinate). */
static Waypoint
cell_centre (LrgActorMover *self,
             gint           column,
             gint           row,
             gfloat         height)
{
	Waypoint w;
	gfloat a = self->origin_a + ((gfloat) column + 0.5f) * self->cell_w;
	gfloat b = self->origin_b + ((gfloat) row + 0.5f) * self->cell_h;

	w.x = a;
	if (self->plane == LRG_ACTOR_MOVER_PLANE_XZ)
	{
		w.y = height;
		w.z = b;
	}
	else
	{
		w.y = b;
		w.z = height;
	}
	return w;
}

static gfloat
height_of (LrgActorMover *self)
{
	return (self->plane == LRG_ACTOR_MOVER_PLANE_XZ) ? self->y : self->z;
}

static gfloat
wrap_angle (gfloat a)
{
	while (a > (gfloat) G_PI)
		a -= 2.0f * (gfloat) G_PI;
	while (a <= -(gfloat) G_PI)
		a += 2.0f * (gfloat) G_PI;
	return a;
}

/* ==========================================================================
 * State
 * ========================================================================== */

static void
set_state (LrgActorMover      *self,
           LrgActorMoverState  state)
{
	const gchar *before = lrg_actor_mover_get_animation (self);

	self->state = state;
	if (g_strcmp0 (before, lrg_actor_mover_get_animation (self)) != 0)
		g_signal_emit (self, signals[SIGNAL_ANIMATION_CHANGED], 0,
		               lrg_actor_mover_get_animation (self));
}

static void
clear_route (LrgActorMover *self)
{
	g_array_set_size (self->waypoints, 0);
	self->grid_target = FALSE;
	self->repath_timer = 0.0f;
}

static void
start_route (LrgActorMover *self,
             gboolean       sit_on_arrival)
{
	self->sit_on_arrival = sit_on_arrival;
	self->repath_timer = 0.0f;
	if (self->waypoints->len > 0)
		set_state (self, LRG_ACTOR_MOVER_MOVING);
	else
		set_state (self, sit_on_arrival ? LRG_ACTOR_MOVER_SITTING : LRG_ACTOR_MOVER_IDLE);
}

/* ==========================================================================
 * Grid routes
 * ========================================================================== */

/*
 * A blocked cell is left or entered through its walkable neighbour
 * nearest (in cells) to @toward, first in N, E, S, W, NE, SE, SW, NW
 * order on ties, so the choice is deterministic.
 */
static gboolean
walkable_neighbour (LrgNavGrid *grid,
                    gint        column,
                    gint        row,
                    gint        toward_c,
                    gint        toward_r,
                    gint       *out_c,
                    gint       *out_r)
{
	static const gint offsets[8][2] = {
		{ 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
		{ 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 }
	};
	gint best = -1;
	gint best_d = G_MAXINT;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (offsets); i++)
	{
		gint c = column + offsets[i][0];
		gint r = row + offsets[i][1];
		gint d;

		if (!lrg_nav_grid_is_walkable (grid, c, r))
			continue;
		d = (c - toward_c) * (c - toward_c) + (r - toward_r) * (r - toward_r);
		if (d < best_d)
		{
			best_d = d;
			best = (gint) i;
		}
	}
	if (best < 0)
		return FALSE;
	*out_c = column + offsets[best][0];
	*out_r = row + offsets[best][1];
	return TRUE;
}

static gboolean
build_grid_route (LrgActorMover *self,
                  Waypoint       target)
{
	LrgNavGrid *grid = lrg_pathfinder_get_grid (self->pathfinder);
	g_autoptr(LrgPath) path = NULL;
	g_autoptr(GError) error = NULL;
	gint sc;
	gint sr;
	gint ec;
	gint er;
	gint pc;
	gint pr;
	gint from_c;
	gint from_r;
	gint to_c;
	gint to_r;
	gboolean start_moved;
	gboolean end_moved;
	guint n;
	guint i;
	gfloat h = height_of (self);

	lrg_actor_mover_world_to_cell (self, self->x, self->y, self->z, &sc, &sr);
	lrg_actor_mover_world_to_cell (self, target.x, target.y, target.z, &ec, &er);
	if (grid == NULL || !lrg_nav_grid_is_valid (grid, sc, sr) ||
	    !lrg_nav_grid_is_valid (grid, ec, er))
		return FALSE;

	g_array_set_size (self->waypoints, 0);
	if (sc == ec && sr == er)
	{
		g_array_append_val (self->waypoints, target);
		return TRUE;
	}

	from_c = sc;
	from_r = sr;
	to_c = ec;
	to_r = er;
	start_moved = !lrg_nav_grid_is_walkable (grid, sc, sr);
	end_moved = !lrg_nav_grid_is_walkable (grid, ec, er);
	if (start_moved && !walkable_neighbour (grid, sc, sr, ec, er, &from_c, &from_r))
		return FALSE;
	if (end_moved && !walkable_neighbour (grid, ec, er, from_c, from_r, &to_c, &to_r))
		return FALSE;

	path = lrg_pathfinder_find_path (self->pathfinder, from_c, from_r, to_c, to_r, &error);
	if (path == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_PATHFIND, "actor mover: no route (%d,%d) -> (%d,%d): %s",
		           from_c, from_r, to_c, to_r, error != NULL ? error->message : "");
		return FALSE;
	}

	n = lrg_path_get_length (path);
	for (i = 0; i < n; i++)
	{
		Waypoint w;

		if (!lrg_path_get_point (path, i, &pc, &pr))
			continue;
		/* the cell we are standing in needs no waypoint */
		if (pc == sc && pr == sr)
			continue;
		/* the target's own cell is replaced by the exact target */
		if (pc == ec && pr == er)
			continue;
		w = cell_centre (self, pc, pr, h);
		g_array_append_val (self->waypoints, w);
	}
	g_array_append_val (self->waypoints, target);
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_actor_mover_finalize (GObject *object)
{
	LrgActorMover *self = LRG_ACTOR_MOVER (object);

	g_free (self->anim_walk);
	g_free (self->anim_idle);
	g_free (self->anim_sit);
	g_clear_object (&self->pathfinder);
	g_array_unref (self->waypoints);
	G_OBJECT_CLASS (lrg_actor_mover_parent_class)->finalize (object);
}

static void
lrg_actor_mover_class_init (LrgActorMoverClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_actor_mover_finalize;

	/**
	 * LrgActorMover::arrived:
	 * @self: the mover
	 *
	 * Emitted when the mover reaches the end of its route (the state is
	 * already idle or sitting).
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_ARRIVED] = g_signal_new ("arrived", G_TYPE_FROM_CLASS (klass),
	                                        G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                        G_TYPE_NONE, 0);

	/**
	 * LrgActorMover::path-failed:
	 * @self: the mover
	 *
	 * Emitted when lrg_actor_mover_move_to() or a re-path finds no route.
	 * The mover stands where it is with the state
	 * %LRG_ACTOR_MOVER_BLOCKED.
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_PATH_FAILED] = g_signal_new ("path-failed", G_TYPE_FROM_CLASS (klass),
	                                            G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                            G_TYPE_NONE, 0);

	/**
	 * LrgActorMover::animation-changed:
	 * @self: the mover
	 * @name: the new animation name
	 *
	 * Emitted when lrg_actor_mover_get_animation() changes.
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_ANIMATION_CHANGED] = g_signal_new ("animation-changed",
	                                                  G_TYPE_FROM_CLASS (klass),
	                                                  G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                                  G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_actor_mover_init (LrgActorMover *self)
{
	self->speed = 64.0f;
	self->anim_walk = g_strdup ("walk");
	self->anim_idle = g_strdup ("idle");
	self->anim_sit = g_strdup ("sit");
	self->cell_w = 1.0f;
	self->cell_h = 1.0f;
	self->waypoints = g_array_new (FALSE, FALSE, sizeof (Waypoint));
}

LrgActorMover *
lrg_actor_mover_new (void)
{
	return g_object_new (LRG_TYPE_ACTOR_MOVER, NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

void
lrg_actor_mover_set_plane (LrgActorMover      *self,
                           LrgActorMoverPlane  plane)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	self->plane = plane;
}

LrgActorMoverPlane
lrg_actor_mover_get_plane (LrgActorMover *self)
{
	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), LRG_ACTOR_MOVER_PLANE_XY);
	return self->plane;
}

void
lrg_actor_mover_set_position (LrgActorMover *self,
                              gfloat         x,
                              gfloat         y,
                              gfloat         z)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	self->x = x;
	self->y = y;
	self->z = z;
}

void
lrg_actor_mover_get_position (LrgActorMover *self,
                              gfloat        *x,
                              gfloat        *y,
                              gfloat        *z)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	if (x != NULL)
		*x = self->x;
	if (y != NULL)
		*y = self->y;
	if (z != NULL)
		*z = self->z;
}

void
lrg_actor_mover_set_speed (LrgActorMover *self,
                           gfloat         speed)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	self->speed = MAX (speed, 0.0f);
}

gfloat
lrg_actor_mover_get_speed (LrgActorMover *self)
{
	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), 0.0f);
	return self->speed;
}

void
lrg_actor_mover_set_turn_speed (LrgActorMover *self,
                                gfloat         radians_per_second)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	self->turn_speed = MAX (radians_per_second, 0.0f);
}

void
lrg_actor_mover_set_facing (LrgActorMover *self,
                            gfloat         radians)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	self->facing = wrap_angle (radians);
	self->desired_facing = self->facing;
}

gfloat
lrg_actor_mover_get_facing (LrgActorMover *self)
{
	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), 0.0f);
	return self->facing;
}

guint
lrg_actor_mover_get_facing_sector (LrgActorMover *self,
                                   guint          n_sectors)
{
	gfloat sector;
	gfloat a;

	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), 0);
	g_return_val_if_fail (n_sectors > 0, 0);

	sector = 2.0f * (gfloat) G_PI / (gfloat) n_sectors;
	/* shift by half a sector so sector 0 is centred on facing 0 */
	a = self->facing + sector * 0.5f;
	while (a < 0.0f)
		a += 2.0f * (gfloat) G_PI;
	return ((guint) floorf (a / sector)) % n_sectors;
}

void
lrg_actor_mover_set_animation_names (LrgActorMover *self,
                                     const gchar   *walk,
                                     const gchar   *idle,
                                     const gchar   *sit)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));

	if (walk != NULL)
	{
		g_free (self->anim_walk);
		self->anim_walk = g_strdup (walk);
	}
	if (idle != NULL)
	{
		g_free (self->anim_idle);
		self->anim_idle = g_strdup (idle);
	}
	if (sit != NULL)
	{
		g_free (self->anim_sit);
		self->anim_sit = g_strdup (sit);
	}
}

const gchar *
lrg_actor_mover_get_animation (LrgActorMover *self)
{
	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), NULL);

	switch (self->state)
	{
	case LRG_ACTOR_MOVER_MOVING:
		return self->anim_walk;
	case LRG_ACTOR_MOVER_SITTING:
		return self->anim_sit;
	case LRG_ACTOR_MOVER_IDLE:
	case LRG_ACTOR_MOVER_BLOCKED:
	default:
		return self->anim_idle;
	}
}

LrgActorMoverState
lrg_actor_mover_get_state (LrgActorMover *self)
{
	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), LRG_ACTOR_MOVER_IDLE);
	return self->state;
}

void
lrg_actor_mover_set_grid (LrgActorMover *self,
                          LrgPathfinder *pathfinder,
                          gfloat         cell_width,
                          gfloat         cell_height,
                          gfloat         origin_a,
                          gfloat         origin_b)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	g_return_if_fail (pathfinder == NULL || LRG_IS_PATHFINDER (pathfinder));
	g_return_if_fail (cell_width > 0.0f && cell_height > 0.0f);

	g_set_object (&self->pathfinder, pathfinder);
	self->cell_w = cell_width;
	self->cell_h = cell_height;
	self->origin_a = origin_a;
	self->origin_b = origin_b;
}

void
lrg_actor_mover_world_to_cell (LrgActorMover *self,
                               gfloat         x,
                               gfloat         y,
                               gfloat         z,
                               gint          *column,
                               gint          *row)
{
	gfloat a;
	gfloat b;

	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	g_return_if_fail (column != NULL && row != NULL);

	to_plane (self, x, y, z, &a, &b);
	*column = (gint) floorf ((a - self->origin_a) / self->cell_w);
	*row = (gint) floorf ((b - self->origin_b) / self->cell_h);
}

/* ==========================================================================
 * Routes
 * ========================================================================== */

static void
fail_route (LrgActorMover *self)
{
	clear_route (self);
	set_state (self, LRG_ACTOR_MOVER_BLOCKED);
	g_signal_emit (self, signals[SIGNAL_PATH_FAILED], 0);
}

gboolean
lrg_actor_mover_move_to (LrgActorMover *self,
                         gfloat         x,
                         gfloat         y,
                         gfloat         z,
                         gboolean       sit_on_arrival)
{
	Waypoint target;

	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), FALSE);

	target.x = x;
	target.y = y;
	target.z = z;
	clear_route (self);

	if (self->pathfinder != NULL)
	{
		if (!build_grid_route (self, target))
		{
			fail_route (self);
			return FALSE;
		}
		self->grid_target = TRUE;
		self->target = target;
	}
	else
		g_array_append_val (self->waypoints, target);

	start_route (self, sit_on_arrival);
	return TRUE;
}

void
lrg_actor_mover_follow_path (LrgActorMover *self,
                             const LrgPath *path,
                             gboolean       sit_on_arrival)
{
	gfloat h;
	guint n;
	guint i;

	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	g_return_if_fail (path != NULL);

	clear_route (self);
	h = height_of (self);
	n = lrg_path_get_length (path);
	for (i = 0; i < n; i++)
	{
		Waypoint w;
		gint c;
		gint r;

		if (!lrg_path_get_point (path, i, &c, &r))
			continue;
		w = cell_centre (self, c, r, h);
		g_array_append_val (self->waypoints, w);
	}
	start_route (self, sit_on_arrival);
}

void
lrg_actor_mover_follow_waypoints (LrgActorMover *self,
                                  const gfloat  *xyz,
                                  guint          n_floats,
                                  gboolean       sit_on_arrival)
{
	guint i;

	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	g_return_if_fail (xyz != NULL || n_floats == 0);
	g_return_if_fail (n_floats % 3 == 0);

	clear_route (self);
	for (i = 0; i + 2 < n_floats; i += 3)
	{
		Waypoint w;

		w.x = xyz[i];
		w.y = xyz[i + 1];
		w.z = xyz[i + 2];
		g_array_append_val (self->waypoints, w);
	}
	start_route (self, sit_on_arrival);
}

gboolean
lrg_actor_mover_follow_nav_agent (LrgActorMover *self,
                                  LrgNavAgent   *agent,
                                  gboolean       sit_on_arrival)
{
	const gfloat *points;
	guint n = 0;

	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), FALSE);
	g_return_val_if_fail (LRG_IS_NAV_AGENT (agent), FALSE);

	points = lrg_nav_agent_get_path (agent, &n);
	if (points == NULL || n < 3)
		return FALSE;
	lrg_actor_mover_follow_waypoints (self, points, n - n % 3, sit_on_arrival);
	return TRUE;
}

void
lrg_actor_mover_stop (LrgActorMover *self)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));

	clear_route (self);
	set_state (self, LRG_ACTOR_MOVER_IDLE);
}

void
lrg_actor_mover_sit (LrgActorMover *self)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));

	clear_route (self);
	set_state (self, LRG_ACTOR_MOVER_SITTING);
}

void
lrg_actor_mover_set_repath_interval (LrgActorMover *self,
                                     gfloat         seconds)
{
	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));
	self->repath_interval = MAX (seconds, 0.0f);
}

gboolean
lrg_actor_mover_repath (LrgActorMover *self)
{
	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), FALSE);

	if (!self->grid_target || self->pathfinder == NULL)
		return TRUE;
	self->repath_timer = 0.0f;
	if (!build_grid_route (self, self->target))
	{
		fail_route (self);
		return FALSE;
	}
	return TRUE;
}

/*
 * TRUE when a cell on the grid route ahead became blocked.  Every
 * intermediate waypoint is checked, not only the next, so the route
 * bends as soon as a door closes rather than at the door; the final
 * waypoint is the exact target, which may sit on a blocked cell.
 */
static gboolean
route_blocked (LrgActorMover *self)
{
	LrgNavGrid *grid;
	guint i;

	if (!self->grid_target || self->pathfinder == NULL || self->waypoints->len < 2)
		return FALSE;
	grid = lrg_pathfinder_get_grid (self->pathfinder);
	if (grid == NULL)
		return FALSE;
	for (i = 0; i + 1 < self->waypoints->len; i++)
	{
		Waypoint *w = &g_array_index (self->waypoints, Waypoint, i);
		gint c;
		gint r;

		lrg_actor_mover_world_to_cell (self, w->x, w->y, w->z, &c, &r);
		if (!lrg_nav_grid_is_walkable (grid, c, r))
			return TRUE;
	}
	return FALSE;
}

static void
turn (LrgActorMover *self,
      gfloat         delta)
{
	gfloat diff;
	gfloat step;

	if (self->turn_speed <= 0.0f)
	{
		self->facing = self->desired_facing;
		return;
	}
	diff = wrap_angle (self->desired_facing - self->facing);
	step = self->turn_speed * delta;
	if (fabsf (diff) <= step)
		self->facing = self->desired_facing;
	else
		self->facing = wrap_angle (self->facing + (diff > 0.0f ? step : -step));
}

void
lrg_actor_mover_update (LrgActorMover *self,
                        gfloat         delta)
{
	gfloat budget;

	g_return_if_fail (LRG_IS_ACTOR_MOVER (self));

	if (delta <= 0.0f)
		return;
	if (self->state != LRG_ACTOR_MOVER_MOVING)
	{
		turn (self, delta);
		return;
	}

	/* re-path when due, or when the way ahead closed */
	if (self->grid_target && self->repath_interval > 0.0f)
	{
		self->repath_timer += delta;
		if (self->repath_timer >= self->repath_interval && !lrg_actor_mover_repath (self))
			return;
	}
	if (route_blocked (self) && !lrg_actor_mover_repath (self))
		return;

	budget = self->speed * delta;
	while (budget > 0.0f && self->waypoints->len > 0)
	{
		Waypoint *next = &g_array_index (self->waypoints, Waypoint, 0);
		gfloat dx = next->x - self->x;
		gfloat dy = next->y - self->y;
		gfloat dz = next->z - self->z;
		gfloat dist = sqrtf (dx * dx + dy * dy + dz * dz);
		gfloat da;
		gfloat db;

		to_plane (self, dx, dy, dz, &da, &db);
		if (fabsf (da) > MOVER_EPSILON || fabsf (db) > MOVER_EPSILON)
			self->desired_facing = atan2f (db, da);

		if (dist <= budget + MOVER_EPSILON)
		{
			self->x = next->x;
			self->y = next->y;
			self->z = next->z;
			budget -= dist;
			g_array_remove_index (self->waypoints, 0);
			continue;
		}
		self->x += dx / dist * budget;
		self->y += dy / dist * budget;
		self->z += dz / dist * budget;
		budget = 0.0f;
	}
	turn (self, delta);

	if (self->waypoints->len == 0)
	{
		self->grid_target = FALSE;
		set_state (self, self->sit_on_arrival ? LRG_ACTOR_MOVER_SITTING : LRG_ACTOR_MOVER_IDLE);
		g_signal_emit (self, signals[SIGNAL_ARRIVED], 0);
	}
}

guint
lrg_actor_mover_get_n_waypoints (LrgActorMover *self)
{
	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), 0);
	return self->waypoints->len;
}

gboolean
lrg_actor_mover_get_waypoint (LrgActorMover *self,
                              guint          index,
                              gfloat        *x,
                              gfloat        *y,
                              gfloat        *z)
{
	Waypoint *w;

	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), FALSE);

	if (index >= self->waypoints->len)
		return FALSE;
	w = &g_array_index (self->waypoints, Waypoint, index);
	if (x != NULL)
		*x = w->x;
	if (y != NULL)
		*y = w->y;
	if (z != NULL)
		*z = w->z;
	return TRUE;
}

gfloat
lrg_actor_mover_get_remaining_distance (LrgActorMover *self)
{
	gfloat total = 0.0f;
	gfloat pa;
	gfloat pb;
	guint i;

	g_return_val_if_fail (LRG_IS_ACTOR_MOVER (self), 0.0f);

	to_plane (self, self->x, self->y, self->z, &pa, &pb);
	for (i = 0; i < self->waypoints->len; i++)
	{
		Waypoint *w = &g_array_index (self->waypoints, Waypoint, i);
		gfloat a;
		gfloat b;

		to_plane (self, w->x, w->y, w->z, &a, &b);
		total += sqrtf ((a - pa) * (a - pa) + (b - pb) * (b - pb));
		pa = a;
		pb = b;
	}
	return total;
}
