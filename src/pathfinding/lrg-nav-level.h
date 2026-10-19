/* lrg-nav-level.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavLevel - the walk and sight queries of one level for a headless
 * simulation (a game server running rooms nobody draws): an
 * #LrgNavPolyMesh baked from the level's triangle soup for walking, an
 * #LrgCollisionMesh of the same triangles for line of sight, and doors
 * that are both at once: a tagged area of the navigation mesh and a box
 * that blocks sight while the door is not open.  Each path query says
 * which doors its agent passes: open ones only, closed ones too (it opens
 * them), or locked ones as well (it picks or breaks them).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "../physics/lrg-collision-mesh.h"
#include "lrg-nav-poly-mesh.h"

G_BEGIN_DECLS

#define LRG_TYPE_NAV_LEVEL (lrg_nav_level_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavLevel, lrg_nav_level, LRG, NAV_LEVEL, GObject)

/**
 * lrg_nav_level_new:
 *
 * Creates an empty level: no triangles, no doors, not baked (every walk
 * query fails and every line of sight is clear until a bake succeeds).
 * The agent the paths are made for is the navigation mesh's
 * (lrg_nav_level_get_nav_mesh(), its properties).  Single-threaded,
 * except that lrg_nav_level_bake_async() runs the bake on a worker.
 *
 * Returns: (transfer full): a new #LrgNavLevel
 */
LRG_AVAILABLE_IN_ALL
LrgNavLevel *
lrg_nav_level_new (void);

/**
 * lrg_nav_level_get_nav_mesh:
 * @self: an #LrgNavLevel
 *
 * The walk mesh: set its agent properties (radius, height, climb,
 * slope, cell size) before the bake; advanced queries (nearest points,
 * random points) may use it directly once lrg_nav_level_is_baked().
 *
 * Returns: (transfer none): the navigation mesh
 */
LRG_AVAILABLE_IN_ALL
LrgNavPolyMesh *
lrg_nav_level_get_nav_mesh (LrgNavLevel *self);

/**
 * lrg_nav_level_get_collision:
 * @self: an #LrgNavLevel
 *
 * The sight mesh: the level's triangles plus one box instance per door
 * that is not open (instance id: the door's tag).  Rebuilt by each bake.
 *
 * Returns: (transfer none): the collision mesh
 */
LRG_AVAILABLE_IN_ALL
LrgCollisionMesh *
lrg_nav_level_get_collision (LrgNavLevel *self);

/**
 * lrg_nav_level_add_triangles:
 * @self: an #LrgNavLevel
 * @vertices: (array length=n_floats): nine floats per triangle (x y z
 *   of three corners, Y up), the layout of lrg_collision_mesh_set_triangles()
 * @n_floats: number of floats, a multiple of nine
 *
 * Stages the level's static triangles (copied) for the next bake:
 * floors, walls, ceilings.  Leave the doors' own geometry out: a door is
 * its box (lrg_nav_level_add_door()).  Triangles with a corner that is
 * not finite are dropped.
 *
 * Returns: the number of triangles staged
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_level_add_triangles (LrgNavLevel  *self,
                             const gfloat *vertices,
                             guint         n_floats);

/**
 * lrg_nav_level_add_door:
 * @self: an #LrgNavLevel
 * @tag: the door's nonzero id
 * @min: (array fixed-size=3): minimum corner of the door's box (the
 *   doorway it fills, from the floor up)
 * @max: (array fixed-size=3): maximum corner
 * @state: its state now
 *
 * Adds a door (or replaces the door with @tag): the walkable floor under
 * the box becomes the tagged area @tag of the navigation mesh (the box
 * reaches the mesh's max-climb below @min so the floor is inside it, and
 * one cell further on each side so a thin door slab covers cells),
 * and the box blocks sight while the door is not
 * %LRG_NAV_DOOR_STATE_OPEN.  A door added after a bake takes effect at
 * the next bake.  Boxes thinner than a centimetre are widened to one.
 *
 * Returns: %FALSE when @tag is 0, %LRG_NAV_LEVEL_LADDER_TAG or
 *   %LRG_NAV_LEVEL_WATER_TAG, or the box is empty or not finite
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_add_door (LrgNavLevel      *self,
                        guint32           tag,
                        const gfloat     *min,
                        const gfloat     *max,
                        LrgNavDoorState   state);

/**
 * lrg_nav_level_set_door_state:
 * @self: an #LrgNavLevel
 * @tag: a door's tag
 * @state: its new state
 *
 * Opens, closes or locks a door: takes effect at once for paths, walk
 * rays and sight (no rebake).  Safe to call while an asynchronous bake
 * runs (the state is applied when it finishes).
 *
 * Returns: %FALSE when there is no door @tag
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_set_door_state (LrgNavLevel     *self,
                              guint32          tag,
                              LrgNavDoorState  state);

/**
 * lrg_nav_level_get_door_state:
 * @self: an #LrgNavLevel
 * @tag: a door's tag
 *
 * Returns: the door's state, %LRG_NAV_DOOR_STATE_OPEN when there is no
 *   door @tag (nothing blocks)
 */
LRG_AVAILABLE_IN_ALL
LrgNavDoorState
lrg_nav_level_get_door_state (LrgNavLevel *self,
                              guint32      tag);

/**
 * lrg_nav_level_get_door_count:
 * @self: an #LrgNavLevel
 *
 * Returns: the number of doors
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_level_get_door_count (LrgNavLevel *self);

/**
 * lrg_nav_level_get_triangle_count:
 * @self: an #LrgNavLevel
 *
 * Returns: the number of staged level triangles
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_level_get_triangle_count (LrgNavLevel *self);

/**
 * lrg_nav_level_bake:
 * @self: an #LrgNavLevel
 * @error: (nullable): return location for a #GError
 *
 * Bakes the navigation mesh (the triangles and the door areas) and
 * builds the sight mesh (the triangles and the boxes of the doors that
 * are not open).  Errors are the navigation mesh's
 * (lrg_nav_poly_mesh_bake(): %G_IO_ERROR_INVALID_ARGUMENT without
 * triangles, %G_IO_ERROR_NO_SPACE, %G_IO_ERROR_NOT_FOUND) or the
 * collision mesh's; %G_IO_ERROR_PENDING while an asynchronous bake runs.
 * On error the level stays as it was.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_bake (LrgNavLevel  *self,
                    GError      **error);

/**
 * lrg_nav_level_bake_async:
 * @self: an #LrgNavLevel
 * @cancellable: (nullable): a #GCancellable (checked before the bake
 *   starts and when it ends; a cancelled bake leaves the level unbaked)
 * @callback: (scope async): called in the thread-default main context
 *   of the caller when the bake is done
 * @user_data: data for @callback
 *
 * lrg_nav_level_bake() on a worker thread, so a server's tick goes on
 * while a large level bakes.  Until it finishes the level answers as an
 * unbaked one (lrg_nav_level_is_baked() is %FALSE, paths fail, sight is
 * clear) and must not be given triangles or doors; door states may
 * change and are applied at the end.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_nav_level_bake_async (LrgNavLevel         *self,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data);

/**
 * lrg_nav_level_bake_finish:
 * @self: an #LrgNavLevel
 * @result: the #GAsyncResult given to the callback
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE when the bake succeeded
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_bake_finish (LrgNavLevel   *self,
                           GAsyncResult  *result,
                           GError       **error);

/**
 * lrg_nav_level_is_baked:
 * @self: an #LrgNavLevel
 *
 * Returns: %TRUE when a bake succeeded and none is running
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_is_baked (LrgNavLevel *self);

/**
 * lrg_nav_level_find_path:
 * @self: an #LrgNavLevel
 * @start: (array fixed-size=3): start (feet)
 * @goal: (array fixed-size=3): goal (feet)
 * @extents: (array fixed-size=3): half extents for snapping both ends
 *   to the walkable surface
 * @passes: the most closed door the agent gets through:
 *   %LRG_NAV_DOOR_STATE_OPEN (open doors only), %LRG_NAV_DOOR_STATE_CLOSED
 *   (it opens closed doors), %LRG_NAV_DOOR_STATE_LOCKED (it unlocks
 *   them too) or %LRG_NAV_DOOR_STATE_SEALED (nothing stops it)
 * @max_nodes: polygons expanded at most, 0 for no limit
 * @n_floats: (out): number of floats returned, three per point
 * @error: (nullable): return location for a #GError
 *
 * lrg_nav_poly_mesh_find_path() with the doors' states: a door is crossed
 * when its state is at most @passes (open < closed < locked < sealed).
 * Ladders and water are crossed (an agent that climbs and swims;
 * lrg_nav_level_find_path_full() for others).
 * %G_IO_ERROR_NOT_FOUND when there is no way (or the level is not
 * baked).
 *
 * Returns: (transfer full) (array length=n_floats) (nullable): the
 *   points from the snapped start to the snapped goal, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gfloat *
lrg_nav_level_find_path (LrgNavLevel   *self,
                         const gfloat  *start,
                         const gfloat  *goal,
                         const gfloat  *extents,
                         LrgNavDoorState passes,
                         guint          max_nodes,
                         guint         *n_floats,
                         GError       **error);

/**
 * lrg_nav_level_find_doors_on_path:
 * @self: an #LrgNavLevel
 * @points: (array length=n_floats): a path from lrg_nav_level_find_path()
 * @n_floats: its number of floats
 * @n_doors: (out): number of door tags returned
 *
 * The doors a path's legs pass through (their boxes, in the order the
 * path meets them, each once): the doors an agent must open on its way.
 *
 * Returns: (transfer full) (array length=n_doors) (nullable): the door
 *   tags, %NULL when there are none
 */
LRG_AVAILABLE_IN_ALL
guint32 *
lrg_nav_level_find_doors_on_path (LrgNavLevel  *self,
                                  const gfloat *points,
                                  guint         n_floats,
                                  guint        *n_doors);

/**
 * lrg_nav_level_can_walk:
 * @self: an #LrgNavLevel
 * @from: (array fixed-size=3): start (feet), snapped to the surface
 * @to: (array fixed-size=3): end (feet)
 * @extents: (array fixed-size=3): half extents for snapping @from; the
 *   walk's end must lie within @extents' height of @to
 * @hit_fraction: (out) (optional): where along the segment the walk
 *   stopped, 0 .. 1 (1 when nothing blocked it, or when it ended on
 *   another storey)
 *
 * Whether the straight walk over the surface from @from to @to stays on
 * the mesh through open doors only (lrg_nav_poly_mesh_raycast()) and
 * ends on the floor under @to (not on a storey above or below it).  An
 * unbaked level answers %FALSE with fraction 0.
 *
 * Returns: %TRUE when the whole segment is walkable
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_can_walk (LrgNavLevel  *self,
                        const gfloat *from,
                        const gfloat *to,
                        const gfloat *extents,
                        gfloat       *hit_fraction);

/**
 * lrg_nav_level_has_line_of_sight:
 * @self: an #LrgNavLevel
 * @from: (array fixed-size=3): the eye (or a missile's start)
 * @to: (array fixed-size=3): the point looked at
 * @out_hit: (out caller-allocates) (optional): what blocked the line
 *   (its #LrgCollisionHit.instance_id is the door's tag for a door, 0 for
 *   the level)
 *
 * Whether the segment from @from to @to meets no level triangle and no
 * door that is not open.  An unbaked level sees everything.
 *
 * Returns: %TRUE when nothing is in between
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_has_line_of_sight (LrgNavLevel     *self,
                                 const gfloat    *from,
                                 const gfloat    *to,
                                 LrgCollisionHit *out_hit);

/**
 * LRG_NAV_LEVEL_LADDER_TAG:
 *
 * The tag every ladder link of a level carries (lrg_nav_level_add_ladder());
 * a door may not use it.
 */
#define LRG_NAV_LEVEL_LADDER_TAG (0xfffffffeu)

/**
 * LRG_NAV_LEVEL_WATER_TAG:
 *
 * The area tag of every swim volume of a level (lrg_nav_level_add_water()):
 * the water's surface and the floor under it; a door may not use it.
 */
#define LRG_NAV_LEVEL_WATER_TAG (0xfffffffdu)

/**
 * lrg_nav_level_add_ladder:
 * @self: an #LrgNavLevel
 * @foot: (array fixed-size=3): the ladder's foot, on the lower floor
 * @top: (array fixed-size=3): its top, on the upper floor (or the
 *   ledge it leads onto)
 *
 * Adds a ladder: an off-mesh link both ways between @foot and @top
 * (lrg_nav_poly_mesh_add_link(), cost twice its height, tag
 * %LRG_NAV_LEVEL_LADDER_TAG), connected at the next bake.  Paths of
 * agents that climb (lrg_nav_level_find_path_full()) may use it; their
 * points carry the foot and the top as a leg of its own
 * (lrg_nav_level_is_ladder_leg()).
 *
 * Returns: the ladder's index (0, 1, ...), or %G_MAXUINT for points
 *   that are not finite or a top not above the foot
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_level_add_ladder (LrgNavLevel  *self,
                          const gfloat *foot,
                          const gfloat *top);

/**
 * lrg_nav_level_get_ladder_count:
 * @self: an #LrgNavLevel
 *
 * Returns: the number of ladders added
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_level_get_ladder_count (LrgNavLevel *self);

/**
 * lrg_nav_level_get_ladder_connected:
 * @self: an #LrgNavLevel
 * @ladder: a ladder index
 *
 * Returns: whether both ends of the ladder found a floor at the last
 *   bake (%FALSE out of range or unbaked)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_get_ladder_connected (LrgNavLevel *self,
                                    guint        ladder);

/**
 * lrg_nav_level_is_ladder_leg:
 * @self: an #LrgNavLevel
 * @from: (array fixed-size=3): a path point
 * @to: (array fixed-size=3): the next one
 *
 * Whether the leg from @from to @to is a ladder's (its foot and its top,
 * either way, each within a centimetre): a walker climbs it instead of
 * walking.
 *
 * Returns: %TRUE for a ladder leg
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_is_ladder_leg (LrgNavLevel  *self,
                             const gfloat *from,
                             const gfloat *to);

/**
 * lrg_nav_level_add_water:
 * @self: an #LrgNavLevel
 * @min: (array fixed-size=3): minimum corner of the water (its bottom
 *   may lie anywhere below the deepest floor it covers)
 * @max: (array fixed-size=3): maximum corner; its height is the
 *   water's surface
 *
 * Adds a swim volume, taking effect at the next bake: its surface
 * becomes walkable for the navigation mesh (a swimmer crosses a pool at
 * the top and climbs out where a bank lies within max-climb of the
 * surface) and everything walkable inside the box, the surface and the
 * floor under it, is the area %LRG_NAV_LEVEL_WATER_TAG, which only paths
 * of agents that swim cross (lrg_nav_level_find_path_full()).  The
 * surface is not part of the sight mesh.  Door boxes win over water
 * where they overlap.
 *
 * Returns: %FALSE when the box is empty or not finite
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_add_water (LrgNavLevel  *self,
                         const gfloat *min,
                         const gfloat *max);

/**
 * lrg_nav_level_get_water_count:
 * @self: an #LrgNavLevel
 *
 * Returns: the number of swim volumes added
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_level_get_water_count (LrgNavLevel *self);

/**
 * lrg_nav_level_in_water:
 * @self: an #LrgNavLevel
 * @point: (array fixed-size=3): a feet position
 * @out_surface: (out) (optional): the surface height of the water it is in
 *
 * Whether @point lies in a swim volume (inside its box, at most at its
 * surface): a walker there swims.
 *
 * Returns: %TRUE in water
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_level_in_water (LrgNavLevel  *self,
                        const gfloat *point,
                        gfloat       *out_surface);

/**
 * lrg_nav_level_find_path_full:
 * @self: an #LrgNavLevel
 * @start: (array fixed-size=3): start (feet)
 * @goal: (array fixed-size=3): goal (feet)
 * @extents: (array fixed-size=3): half extents for snapping both ends
 * @passes: the most closed door the agent gets through (as
 *   lrg_nav_level_find_path())
 * @climbs: whether the agent climbs ladders
 * @swims: whether the agent swims (crosses water areas)
 * @max_nodes: polygons expanded at most, 0 for no limit
 * @n_floats: (out): number of floats returned, three per point
 * @error: (nullable): return location for a #GError
 *
 * lrg_nav_level_find_path() for an agent that may not climb or swim:
 * ladder links are crossed only when @climbs, water areas only when
 * @swims.  lrg_nav_level_find_path() is this call with both %TRUE.
 *
 * Returns: (transfer full) (array length=n_floats) (nullable): the
 *   points, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gfloat *
lrg_nav_level_find_path_full (LrgNavLevel     *self,
                              const gfloat    *start,
                              const gfloat    *goal,
                              const gfloat    *extents,
                              LrgNavDoorState  passes,
                              gboolean         climbs,
                              gboolean         swims,
                              guint            max_nodes,
                              guint           *n_floats,
                              GError         **error);

G_END_DECLS
