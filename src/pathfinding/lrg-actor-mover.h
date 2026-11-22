/* lrg-actor-mover.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgActorMover: walks a position along a path at a speed -- an A* path
 * on a grid (LrgPathfinder), a list of waypoints, or an LrgNavAgent's
 * path -- turning to face where it goes and naming the animation that
 * fits (walk, idle, sit).  It re-paths on a timer or when a cell ahead
 * becomes blocked, and says when it has arrived.  Pure logic: no window,
 * no clock but the delta it is given.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-path.h"
#include "lrg-pathfinder.h"
#include "lrg-nav-agent.h"

G_BEGIN_DECLS

/**
 * LrgActorMoverState:
 * @LRG_ACTOR_MOVER_IDLE: standing, with nowhere to go
 * @LRG_ACTOR_MOVER_MOVING: following a path
 * @LRG_ACTOR_MOVER_SITTING: arrived somewhere to sit (or told to sit)
 * @LRG_ACTOR_MOVER_BLOCKED: the last move found no path; standing
 *
 * What an #LrgActorMover is doing.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_ACTOR_MOVER_IDLE,
	LRG_ACTOR_MOVER_MOVING,
	LRG_ACTOR_MOVER_SITTING,
	LRG_ACTOR_MOVER_BLOCKED
} LrgActorMoverState;

LRG_AVAILABLE_IN_ALL
GType lrg_actor_mover_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ACTOR_MOVER_STATE (lrg_actor_mover_state_get_type ())

/**
 * LrgActorMoverPlane:
 * @LRG_ACTOR_MOVER_PLANE_XY: the ground is the x/y plane (2D maps); a
 *   grid cell (column, row) is (x, y)
 * @LRG_ACTOR_MOVER_PLANE_XZ: the ground is the x/z plane (3D worlds, y
 *   up); a grid cell (column, row) is (x, z)
 *
 * Which two coordinates the grid, the facing and distances use.  The
 * third coordinate is carried along waypoints but never pathed.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_ACTOR_MOVER_PLANE_XY,
	LRG_ACTOR_MOVER_PLANE_XZ
} LrgActorMoverPlane;

LRG_AVAILABLE_IN_ALL
GType lrg_actor_mover_plane_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ACTOR_MOVER_PLANE (lrg_actor_mover_plane_get_type ())

#define LRG_TYPE_ACTOR_MOVER (lrg_actor_mover_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgActorMover, lrg_actor_mover, LRG, ACTOR_MOVER, GObject)

/**
 * lrg_actor_mover_new:
 *
 * Creates a mover at the origin, on the x/y plane, moving 64 units a
 * second, turning instantly, with the animation names "walk", "idle"
 * and "sit".
 *
 * Returns: (transfer full): a new #LrgActorMover
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgActorMover * lrg_actor_mover_new (void);

/**
 * lrg_actor_mover_set_plane:
 * @self: an #LrgActorMover
 * @plane: the ground plane
 *
 * Sets which coordinates are the ground.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_set_plane (LrgActorMover      *self,
                                LrgActorMoverPlane  plane);

/**
 * lrg_actor_mover_get_plane:
 * @self: an #LrgActorMover
 *
 * Returns: the ground plane
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgActorMoverPlane lrg_actor_mover_get_plane (LrgActorMover *self);

/**
 * lrg_actor_mover_set_position:
 * @self: an #LrgActorMover
 * @x: x
 * @y: y
 * @z: z
 *
 * Puts the mover somewhere.  A path in progress continues from there.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_set_position (LrgActorMover *self,
                                   gfloat         x,
                                   gfloat         y,
                                   gfloat         z);

/**
 * lrg_actor_mover_get_position:
 * @self: an #LrgActorMover
 * @x: (out) (optional): x
 * @y: (out) (optional): y
 * @z: (out) (optional): z
 *
 * Gets where the mover is.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_get_position (LrgActorMover *self,
                                   gfloat        *x,
                                   gfloat        *y,
                                   gfloat        *z);

/**
 * lrg_actor_mover_set_speed:
 * @self: an #LrgActorMover
 * @speed: units per second (>= 0)
 *
 * Sets the walking speed.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_set_speed (LrgActorMover *self,
                                gfloat         speed);

/**
 * lrg_actor_mover_get_speed:
 * @self: an #LrgActorMover
 *
 * Returns: units per second
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_actor_mover_get_speed (LrgActorMover *self);

/**
 * lrg_actor_mover_set_turn_speed:
 * @self: an #LrgActorMover
 * @radians_per_second: how fast the facing turns; 0 turns instantly
 *
 * Sets how quickly the mover turns to face where it goes.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_set_turn_speed (LrgActorMover *self,
                                     gfloat         radians_per_second);

/**
 * lrg_actor_mover_set_facing:
 * @self: an #LrgActorMover
 * @radians: the facing angle (see lrg_actor_mover_get_facing())
 *
 * Turns the mover at once (for a seat that faces a table).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_set_facing (LrgActorMover *self,
                                 gfloat         radians);

/**
 * lrg_actor_mover_get_facing:
 * @self: an #LrgActorMover
 *
 * The facing is atan2 (db, da) of the direction faced, where (a, b) are
 * the plane's coordinates: (x, y) or (x, z).  0 faces +x; on the x/y
 * plane with y pointing down the screen, pi/2 faces down.
 *
 * Returns: the facing in radians, in (-pi, pi]
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_actor_mover_get_facing (LrgActorMover *self);

/**
 * lrg_actor_mover_get_facing_sector:
 * @self: an #LrgActorMover
 * @n_sectors: how many directions the sprite has (4, 8, ...; > 0)
 *
 * Quantises the facing for a sprite sheet with one animation per
 * direction.  Sector 0 is centred on +a (facing 0) and sectors count
 * with the angle, so with 4 on the x/y plane they are right, down,
 * left, up.
 *
 * Returns: the sector, 0 to @n_sectors - 1
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_actor_mover_get_facing_sector (LrgActorMover *self,
                                         guint          n_sectors);

/**
 * lrg_actor_mover_set_animation_names:
 * @self: an #LrgActorMover
 * @walk: (nullable): the animation while moving (%NULL keeps it)
 * @idle: (nullable): the animation while standing (%NULL keeps it)
 * @sit: (nullable): the animation while sitting (%NULL keeps it)
 *
 * Names the animations, to match a sprite sheet's or a model's clips.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_set_animation_names (LrgActorMover *self,
                                          const gchar   *walk,
                                          const gchar   *idle,
                                          const gchar   *sit);

/**
 * lrg_actor_mover_get_animation:
 * @self: an #LrgActorMover
 *
 * Returns: (transfer none): the animation for the current state: walk
 *   while moving, sit while sitting, idle otherwise
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_actor_mover_get_animation (LrgActorMover *self);

/**
 * lrg_actor_mover_get_state:
 * @self: an #LrgActorMover
 *
 * Returns: what the mover is doing
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgActorMoverState lrg_actor_mover_get_state (LrgActorMover *self);

/**
 * lrg_actor_mover_set_grid:
 * @self: an #LrgActorMover
 * @pathfinder: (nullable): the pathfinder over the walkable grid; %NULL
 *   makes lrg_actor_mover_move_to() go in a straight line
 * @cell_width: width of a cell in world units (> 0)
 * @cell_height: height of a cell in world units (> 0)
 * @origin_a: world coordinate of the grid's left edge (x)
 * @origin_b: world coordinate of the grid's top edge (y, or z on the x/z
 *   plane)
 *
 * Paths over a grid.  Cell (c, r) covers [origin + c * size, origin +
 * (c + 1) * size) and is entered at its centre.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_set_grid (LrgActorMover *self,
                               LrgPathfinder *pathfinder,
                               gfloat         cell_width,
                               gfloat         cell_height,
                               gfloat         origin_a,
                               gfloat         origin_b);

/**
 * lrg_actor_mover_world_to_cell:
 * @self: an #LrgActorMover
 * @x: world x
 * @y: world y
 * @z: world z
 * @column: (out): grid column
 * @row: (out): grid row
 *
 * Finds the grid cell a world point is in (it may be outside the grid).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_world_to_cell (LrgActorMover *self,
                                    gfloat         x,
                                    gfloat         y,
                                    gfloat         z,
                                    gint          *column,
                                    gint          *row);

/**
 * lrg_actor_mover_move_to:
 * @self: an #LrgActorMover
 * @x: target x
 * @y: target y
 * @z: target z
 * @sit_on_arrival: sit (rather than stand) when there
 *
 * Walks to a point.  With a grid the route is an A* path through cell
 * centres, ending exactly at the target; a start or target on a blocked
 * cell (somebody standing up from a desk, walking to a chair) goes
 * through its nearest walkable neighbour.  Without a grid the route is a
 * straight line.  On failure the mover stands where it is, the state is
 * %LRG_ACTOR_MOVER_BLOCKED and #LrgActorMover::path-failed is emitted.
 *
 * Returns: %TRUE when a route was found
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_actor_mover_move_to (LrgActorMover *self,
                                  gfloat         x,
                                  gfloat         y,
                                  gfloat         z,
                                  gboolean       sit_on_arrival);

/**
 * lrg_actor_mover_follow_path:
 * @self: an #LrgActorMover
 * @path: a grid path (cells), for example from lrg_pathfinder_find_path()
 * @sit_on_arrival: sit when there
 *
 * Walks through the centres of a path's cells, at the mover's current
 * height (z on the x/y plane, y on the x/z plane).  Needs a grid.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_follow_path (LrgActorMover *self,
                                  const LrgPath *path,
                                  gboolean       sit_on_arrival);

/**
 * lrg_actor_mover_follow_waypoints:
 * @self: an #LrgActorMover
 * @xyz: (array length=n_floats): waypoints, three floats each
 * @n_floats: number of floats (a multiple of 3)
 * @sit_on_arrival: sit when there
 *
 * Walks through world points in order (a 3D route, a patrol).  Re-pathing
 * does not apply: the route is the caller's.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_follow_waypoints (LrgActorMover *self,
                                       const gfloat  *xyz,
                                       guint          n_floats,
                                       gboolean       sit_on_arrival);

/**
 * lrg_actor_mover_follow_nav_agent:
 * @self: an #LrgActorMover
 * @agent: an #LrgNavAgent with a path
 * @sit_on_arrival: sit when there
 *
 * Walks the corners of a navigation-mesh agent's current path
 * (lrg_nav_agent_get_path()), so a 3D actor gets its route from the
 * navmesh and its walk, facing and animation from the mover.
 *
 * Returns: %FALSE when the agent has no path
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_actor_mover_follow_nav_agent (LrgActorMover *self,
                                           LrgNavAgent   *agent,
                                           gboolean       sit_on_arrival);

/**
 * lrg_actor_mover_stop:
 * @self: an #LrgActorMover
 *
 * Stops where it is and forgets the route; the state becomes idle.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_stop (LrgActorMover *self);

/**
 * lrg_actor_mover_sit:
 * @self: an #LrgActorMover
 *
 * Sits down where it is (stopping first).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_sit (LrgActorMover *self);

/**
 * lrg_actor_mover_set_repath_interval:
 * @self: an #LrgActorMover
 * @seconds: how often a grid route is recomputed while moving; 0 only
 *   re-paths when a cell ahead becomes blocked
 *
 * Grid routes are also recomputed whenever a cell on the route ahead is
 * no longer walkable (a door closed, a chair moved).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_set_repath_interval (LrgActorMover *self,
                                          gfloat         seconds);

/**
 * lrg_actor_mover_repath:
 * @self: an #LrgActorMover
 *
 * Recomputes the grid route to the current target from where the mover
 * is now.  Does nothing without a grid target.
 *
 * Returns: %TRUE when a route was found (or none was needed)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_actor_mover_repath (LrgActorMover *self);

/**
 * lrg_actor_mover_update:
 * @self: an #LrgActorMover
 * @delta: seconds
 *
 * Moves speed * @delta along the route (through as many waypoints as
 * that covers), turns, re-paths when due, and emits
 * #LrgActorMover::arrived on reaching the end.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_actor_mover_update (LrgActorMover *self,
                             gfloat         delta);

/**
 * lrg_actor_mover_get_n_waypoints:
 * @self: an #LrgActorMover
 *
 * Returns: waypoints still ahead, the next first
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_actor_mover_get_n_waypoints (LrgActorMover *self);

/**
 * lrg_actor_mover_get_waypoint:
 * @self: an #LrgActorMover
 * @index: 0 is the next waypoint
 * @x: (out) (optional): x
 * @y: (out) (optional): y
 * @z: (out) (optional): z
 *
 * Reads a waypoint still ahead.
 *
 * Returns: %FALSE when @index is past the end
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_actor_mover_get_waypoint (LrgActorMover *self,
                                       guint          index,
                                       gfloat        *x,
                                       gfloat        *y,
                                       gfloat        *z);

/**
 * lrg_actor_mover_get_remaining_distance:
 * @self: an #LrgActorMover
 *
 * Returns: the length of the route still ahead, in the ground plane
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_actor_mover_get_remaining_distance (LrgActorMover *self);

G_END_DECLS
