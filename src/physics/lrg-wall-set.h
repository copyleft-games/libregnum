/* lrg-wall-set.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWallSet - a static, axis-aligned box collision world on the XZ plane
 * for authoritative servers. Agents are squares of half-extent "radius";
 * movement is a swept slab test with wall sliding. A uniform-grid
 * broadphase accelerates queries without changing any result.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_WALL     (lrg_wall_get_type ())
#define LRG_TYPE_WALL_SET (lrg_wall_set_get_type ())

/**
 * LRG_WALL_SET_MAX_RADIUS:
 *
 * Largest agent half-extent accepted by lrg_wall_set_is_clear() and
 * lrg_wall_set_move_slide().
 */
#define LRG_WALL_SET_MAX_RADIUS (5.0)

/**
 * LRG_WALL_SET_DEFAULT_CELL_SIZE:
 *
 * Default broadphase cell edge length in world units (metres).
 */
#define LRG_WALL_SET_DEFAULT_CELL_SIZE (16.0)

/**
 * LRG_WALL_SET_INVALID_INDEX:
 *
 * Returned by lrg_wall_set_add() and lrg_wall_set_add_volume() when the
 * input is rejected.
 */
#define LRG_WALL_SET_INVALID_INDEX (G_MAXUINT)

/**
 * LRG_WALL_SET_NO_HIT:
 *
 * Kind reported by lrg_wall_set_raycast_distance() when nothing was hit.
 */
#define LRG_WALL_SET_NO_HIT (G_MAXUINT)

/**
 * LrgWall:
 * @x: centre X coordinate
 * @z: centre Z coordinate
 * @half_x: half extent along X (>= 0)
 * @half_z: half extent along Z (>= 0)
 * @height: height above the ground; informational for renderers, ignored
 *   by every 2D query
 * @kind: caller-defined category (house, tree, gallery wall, ...)
 * @tag: caller-defined identifier
 *
 * One solid axis-aligned box. Plain value type: copy it freely.
 */
struct _LrgWall
{
    gdouble x;
    gdouble z;
    gdouble half_x;
    gdouble half_z;
    gdouble height;
    guint   kind;
    guint   tag;
};

LRG_AVAILABLE_IN_ALL
GType lrg_wall_get_type (void) G_GNUC_CONST;

/**
 * lrg_wall_new:
 * @x: centre X
 * @z: centre Z
 * @half_x: half extent along X
 * @half_z: half extent along Z
 * @height: height (informational)
 * @kind: caller category
 * @tag: caller identifier
 *
 * Allocates a wall. No validation happens here; lrg_wall_set_add() rejects
 * invalid geometry.
 *
 * Returns: (transfer full): a new #LrgWall
 */
LRG_AVAILABLE_IN_ALL
LrgWall *
lrg_wall_new (gdouble x,
              gdouble z,
              gdouble half_x,
              gdouble half_z,
              gdouble height,
              guint   kind,
              guint   tag);

/**
 * lrg_wall_copy:
 * @self: an #LrgWall
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgWall *
lrg_wall_copy (const LrgWall *self);

/**
 * lrg_wall_free:
 * @self: (nullable): an #LrgWall
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wall_free (LrgWall *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgWall, lrg_wall_free)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWallSet, lrg_wall_set, LRG, WALL_SET, GObject)

/**
 * lrg_wall_set_new:
 *
 * Creates an empty wall set with an unlimited bounds limit
 * (%G_MAXDOUBLE) and a %LRG_WALL_SET_DEFAULT_CELL_SIZE broadphase.
 *
 * Returns: (transfer full): a new #LrgWallSet
 */
LRG_AVAILABLE_IN_ALL
LrgWallSet *
lrg_wall_set_new (void);

/**
 * lrg_wall_set_add:
 * @self: an #LrgWallSet
 * @wall: the wall to copy in
 *
 * Appends a copy of @wall. Walls are queried in insertion order, which
 * decides ties in lrg_wall_set_move_slide(). Rejects non-finite values,
 * negative half extents and boxes whose edges overflow. Invalidates the
 * broadphase.
 *
 * Returns: the new wall's index, or %LRG_WALL_SET_INVALID_INDEX
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_wall_set_add (LrgWallSet    *self,
                  const LrgWall *wall);

/**
 * lrg_wall_set_add_box:
 * @self: an #LrgWallSet
 * @x: centre X
 * @z: centre Z
 * @half_x: half extent along X
 * @half_z: half extent along Z
 * @height: height (informational)
 * @kind: caller category
 * @tag: caller identifier
 *
 * Convenience wrapper around lrg_wall_set_add().
 *
 * Returns: the new wall's index, or %LRG_WALL_SET_INVALID_INDEX
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_wall_set_add_box (LrgWallSet *self,
                      gdouble     x,
                      gdouble     z,
                      gdouble     half_x,
                      gdouble     half_z,
                      gdouble     height,
                      guint       kind,
                      guint       tag);

/**
 * lrg_wall_set_get_count:
 * @self: an #LrgWallSet
 *
 * Returns: the number of walls
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_wall_set_get_count (LrgWallSet *self);

/**
 * lrg_wall_set_get:
 * @self: an #LrgWallSet
 * @index: wall index
 * @out: (out caller-allocates): receives a copy of the wall
 *
 * Copies wall @index into @out.
 *
 * Returns: %TRUE if @index was valid; @out is untouched otherwise
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_wall_set_get (LrgWallSet *self,
                  guint       index,
                  LrgWall    *out);

/**
 * lrg_wall_set_clear:
 * @self: an #LrgWallSet
 *
 * Removes every wall and every volume. The bounds limit and cell size are
 * kept.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wall_set_clear (LrgWallSet *self);

/**
 * lrg_wall_set_set_bounds_limit:
 * @self: an #LrgWallSet
 * @limit: positive world coordinate limit
 *
 * Positions with |x| or |z| above @limit are invalid, and
 * lrg_wall_set_move_slide() clamps its target into [-@limit, @limit].
 * Non-positive or NaN values are ignored.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wall_set_set_bounds_limit (LrgWallSet *self,
                               gdouble     limit);

/**
 * lrg_wall_set_get_bounds_limit:
 * @self: an #LrgWallSet
 *
 * Returns: the world coordinate limit (default %G_MAXDOUBLE)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_wall_set_get_bounds_limit (LrgWallSet *self);

/**
 * lrg_wall_set_set_cell_size:
 * @self: an #LrgWallSet
 * @cell_size: broadphase cell edge length; 0 disables the broadphase
 *
 * Sets the uniform-grid cell size. 0 selects the brute-force loop, which
 * produces identical results. Negative or non-finite values are ignored.
 * Invalidates the broadphase.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wall_set_set_cell_size (LrgWallSet *self,
                            gdouble     cell_size);

/**
 * lrg_wall_set_get_cell_size:
 * @self: an #LrgWallSet
 *
 * Returns: the configured cell size (0 = brute force)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_wall_set_get_cell_size (LrgWallSet *self);

/**
 * lrg_wall_set_build_index:
 * @self: an #LrgWallSet
 *
 * Builds the broadphase now instead of on the first query. Queries are
 * read-only once the index exists, so a fully built set may be shared by
 * reader threads as long as nobody adds, clears or reconfigures it.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wall_set_build_index (LrgWallSet *self);

/**
 * lrg_wall_set_is_clear:
 * @self: an #LrgWallSet
 * @x: agent centre X
 * @z: agent centre Z
 * @radius: agent half extent, in [0, %LRG_WALL_SET_MAX_RADIUS]
 *
 * Tests whether a square agent overlaps no wall. A wall blocks when
 * |x - wall.x| < wall.half_x + radius and |z - wall.z| < wall.half_z +
 * radius (touching is allowed).
 *
 * Returns: %TRUE if clear; %FALSE when blocked or on invalid input
 *   (non-finite values, radius out of range, position beyond the bounds
 *   limit)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_wall_set_is_clear (LrgWallSet *self,
                       gdouble     x,
                       gdouble     z,
                       gdouble     radius);

/**
 * lrg_wall_set_move_slide:
 * @self: an #LrgWallSet
 * @radius: agent half extent
 * @x: (inout): agent centre X
 * @z: (inout): agent centre Z
 * @dx: desired X displacement
 * @dz: desired Z displacement
 *
 * Moves a square agent by (@dx, @dz), stopping at the first wall and
 * sliding along it for up to four contact iterations. The target is
 * clamped into the bounds limit first.
 *
 * Returns: %FALSE and leaves @x / @z untouched when the start is not
 *   clear, the displacement is not finite or exceeds twice the bounds
 *   limit; %TRUE otherwise
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_wall_set_move_slide (LrgWallSet *self,
                         gdouble     radius,
                         gdouble    *x,
                         gdouble    *z,
                         gdouble     dx,
                         gdouble     dz);

/**
 * lrg_wall_set_segment_clear:
 * @self: an #LrgWallSet
 * @ax: start X
 * @az: start Z
 * @bx: end X
 * @bz: end Z
 *
 * Line-of-sight test. Both endpoints must be clear points and the segment
 * must not enter any wall.
 *
 * Returns: %TRUE if the segment is unobstructed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_wall_set_segment_clear (LrgWallSet *self,
                            gdouble     ax,
                            gdouble     az,
                            gdouble     bx,
                            gdouble     bz);

/**
 * lrg_wall_set_raycast_distance:
 * @self: an #LrgWallSet
 * @ox: ray origin X
 * @oz: ray origin Z
 * @dx: ray direction X (need not be normalised)
 * @dz: ray direction Z
 * @max_distance: longest distance of interest, > 0
 * @hit_kind: (out) (optional): kind of the wall hit, or
 *   %LRG_WALL_SET_NO_HIT
 *
 * Distance along the normalised direction to the first wall, intended for
 * pulling a third-person camera in front of walls. Heights are ignored.
 * An origin strictly inside a wall reports 0 and that wall. Equal
 * distances report the lowest wall index.
 *
 * Returns: the hit distance, @max_distance when nothing is hit, or 0 on
 *   invalid input (non-finite values, zero direction, non-positive
 *   @max_distance)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_wall_set_raycast_distance (LrgWallSet *self,
                               gdouble     ox,
                               gdouble     oz,
                               gdouble     dx,
                               gdouble     dz,
                               gdouble     max_distance,
                               guint      *hit_kind);

/**
 * lrg_wall_set_add_volume:
 * @self: an #LrgWallSet
 * @x: centre X
 * @z: centre Z
 * @half_x: half extent along X (>= 0)
 * @half_z: half extent along Z (>= 0)
 * @tag: identifier reported by lrg_wall_set_volume_at(), <= %G_MAXINT
 *
 * Adds a named rectangular interior volume (a room, a building interior).
 * Volumes never block movement.
 *
 * Returns: the volume index, or %LRG_WALL_SET_INVALID_INDEX
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_wall_set_add_volume (LrgWallSet *self,
                         gdouble     x,
                         gdouble     z,
                         gdouble     half_x,
                         gdouble     half_z,
                         guint       tag);

/**
 * lrg_wall_set_get_volume_count:
 * @self: an #LrgWallSet
 *
 * Returns: the number of volumes
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_wall_set_get_volume_count (LrgWallSet *self);

/**
 * lrg_wall_set_volume_at:
 * @self: an #LrgWallSet
 * @x: point X
 * @z: point Z
 *
 * Finds the first-added volume containing the point, edges inclusive.
 *
 * Returns: that volume's tag, or -1 when none contains the point or the
 *   point is not finite
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_wall_set_volume_at (LrgWallSet *self,
                        gdouble     x,
                        gdouble     z);

G_END_DECLS
