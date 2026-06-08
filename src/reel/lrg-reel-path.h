/* lrg-reel-path.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Path-motion helpers for reels: arc-length, point-at-t (motion along a path),
 * and evolve (a partial path for stroke-reveal / draw-on animations).  Built on
 * graylib's #GrlPath flattening.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * lrg_reel_path_length:
 * @path: a #GrlPath.
 *
 * Computes the total arc length of @path (sum of flattened segment lengths,
 * not counting jumps between subpaths).
 *
 * Returns: the length in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_path_length (GrlPath *path);

/**
 * lrg_reel_path_point_at:
 * @path: a #GrlPath.
 * @t: normalized position along the path (0..1, clamped).
 * @out_x: (out) (optional): return location for the X coordinate.
 * @out_y: (out) (optional): return location for the Y coordinate.
 * @out_angle: (out) (optional): return location for the tangent angle (radians).
 *
 * Finds the point at normalized arc-length @t along @path, plus the tangent
 * angle there (useful for orienting an object that follows the path).
 *
 * Returns: %TRUE if @path has length and a point was produced
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_path_point_at (GrlPath *path,
                        gdouble  t,
                        gdouble *out_x,
                        gdouble *out_y,
                        gdouble *out_angle);

/**
 * lrg_reel_path_evolve:
 * @path: a #GrlPath.
 * @t: fraction of the path to keep (0..1, clamped).
 *
 * Returns a new path containing the first @t of @path's arc length — a
 * "draw-on" / stroke-reveal effect when @t is animated from 0 to 1.
 *
 * Returns: (transfer full): the partial #GrlPath
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlPath *
lrg_reel_path_evolve (GrlPath *path,
                      gdouble  t);

G_END_DECLS
