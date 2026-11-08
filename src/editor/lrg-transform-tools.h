/* lrg-transform-tools.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Viewport transform helpers: pivot, align, distribute, grid snap,
 * drop to a floor and ray-versus-box surface snap. The numbers are the
 * same ones the editor's gizmo and align actions apply to node
 * locations. Axis 0 is X, 1 is Y, 2 is Z.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgPivotMode:
 * @LRG_PIVOT_ACTIVE: the active node's origin
 * @LRG_PIVOT_MEDIAN: the average of the points
 * @LRG_PIVOT_BOUNDS: the centre of the axis-aligned bounds
 * @LRG_PIVOT_CURSOR: the cursor point passed by the caller
 */
typedef enum
{
	LRG_PIVOT_ACTIVE,
	LRG_PIVOT_MEDIAN,
	LRG_PIVOT_BOUNDS,
	LRG_PIVOT_CURSOR
} LrgPivotMode;

/**
 * LrgAlignMode:
 * @LRG_TRANSFORM_ALIGN_MIN: move every point to the minimum on the axis
 * @LRG_TRANSFORM_ALIGN_CENTER: move every point to the centre on the axis
 * @LRG_TRANSFORM_ALIGN_MAX: move every point to the maximum on the axis
 */
typedef enum
{
	LRG_TRANSFORM_ALIGN_MIN,
	LRG_TRANSFORM_ALIGN_CENTER,
	LRG_TRANSFORM_ALIGN_MAX
} LrgAlignMode;

/**
 * lrg_transform_pivot:
 * @mode: which pivot
 * @xs: (array length=n): X of each selected point
 * @ys: (array length=n): Y of each selected point
 * @zs: (array length=n): Z of each selected point
 * @n: number of points
 * @active_x: active-node origin
 * @active_y: active-node origin
 * @active_z: active-node origin
 * @cursor_x: cursor position used by %LRG_PIVOT_CURSOR
 * @cursor_y: cursor position used by %LRG_PIVOT_CURSOR
 * @cursor_z: cursor position used by %LRG_PIVOT_CURSOR
 * @out_x: (out): pivot X
 * @out_y: (out): pivot Y
 * @out_z: (out): pivot Z
 *
 * Computes the pivot a gizmo should sit on. @n may be 0; the pivot is
 * then the active origin (or the cursor in cursor mode).
 */
LRG_AVAILABLE_IN_ALL
void lrg_transform_pivot (LrgPivotMode   mode,
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
                          gfloat        *out_z);

/**
 * lrg_transform_align:
 * @mode: min, centre or max
 * @axis: 0, 1 or 2
 * @xs: (array length=n) (inout): X coordinates
 * @ys: (array length=n) (inout): Y coordinates
 * @zs: (array length=n) (inout): Z coordinates
 * @n: number of points
 *
 * Writes the aligned coordinate of @axis. Fewer than two points is a no-op.
 */
LRG_AVAILABLE_IN_ALL
void lrg_transform_align (LrgAlignMode  mode,
                          gint          axis,
                          gfloat       *xs,
                          gfloat       *ys,
                          gfloat       *zs,
                          guint         n);

/**
 * lrg_transform_distribute:
 * @axis: 0, 1 or 2
 * @xs: (array length=n) (inout): X coordinates
 * @ys: (array length=n) (inout): Y coordinates
 * @zs: (array length=n) (inout): Z coordinates
 * @n: number of points
 *
 * Spaces the interior points evenly between the current minimum and
 * maximum on @axis. The two extremes stay put. Fewer than three points
 * is a no-op.
 */
LRG_AVAILABLE_IN_ALL
void lrg_transform_distribute (gint     axis,
                               gfloat  *xs,
                               gfloat  *ys,
                               gfloat  *zs,
                               guint    n);

/**
 * lrg_transform_snap_position:
 * @x: (inout): X
 * @y: (inout): Y
 * @z: (inout): Z
 * @grid: snap spacing; 0 leaves the point alone
 */
LRG_AVAILABLE_IN_ALL
void lrg_transform_snap_position (gfloat *x,
                                  gfloat *y,
                                  gfloat *z,
                                  gfloat  grid);

/**
 * lrg_transform_snap_angle:
 * @degrees: (inout): an angle in degrees
 * @step: snap spacing in degrees; 0 leaves it alone
 */
LRG_AVAILABLE_IN_ALL
void lrg_transform_snap_angle (gfloat *degrees,
                               gfloat  step);

/**
 * lrg_transform_drop:
 * @x: (inout): X
 * @y: (inout): Y
 * @z: (inout): Z
 * @up_axis: the axis that is "up" (0, 1 or 2)
 * @floor: the floor coordinate on @up_axis
 *
 * Sets the up-axis coordinate to @floor. The other two axes stay.
 */
LRG_AVAILABLE_IN_ALL
void lrg_transform_drop (gfloat *x,
                         gfloat *y,
                         gfloat *z,
                         gint    up_axis,
                         gfloat  floor);

/**
 * lrg_transform_ray_aabb:
 * @ox: ray origin X
 * @oy: ray origin Y
 * @oz: ray origin Z
 * @dx: ray direction X (need not be a unit vector)
 * @dy: ray direction Y
 * @dz: ray direction Z
 * @min_x: box minimum
 * @min_y: box minimum
 * @min_z: box minimum
 * @max_x: box maximum
 * @max_y: box maximum
 * @max_z: box maximum
 * @out_t: (out): ray parameter of the entry hit (origin + t * direction)
 *
 * Slab test. A hit behind the origin (t < 0) is a miss. A zero direction
 * component misses unless the origin is inside that slab.
 *
 * Returns: %TRUE when the ray enters the box at @out_t >= 0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_transform_ray_aabb (gfloat  ox,
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
                                 gfloat *out_t);

/**
 * lrg_transform_snap_surface:
 * @ox: ray origin
 * @oy: ray origin
 * @oz: ray origin
 * @dx: ray direction
 * @dy: ray direction
 * @dz: ray direction
 * @min_x: (array length=n): box minima
 * @min_y: (array length=n): box minima
 * @min_z: (array length=n): box minima
 * @max_x: (array length=n): box maxima
 * @max_y: (array length=n): box maxima
 * @max_z: (array length=n): box maxima
 * @n: number of boxes
 * @out_x: (out): hit point
 * @out_y: (out): hit point
 * @out_z: (out): hit point
 *
 * Moves a point onto the nearest box the ray enters. This is the
 * surface-snap / drop-onto-mesh query when the scene is a set of
 * bounds (a collision mesh's triangle query can feed the same point).
 *
 * Returns: %TRUE when some box was hit
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_transform_snap_surface (gfloat        ox,
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
                                     gfloat       *out_z);

G_END_DECLS
