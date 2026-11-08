/* lrg-nav-poly-mesh-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal LrgNavPolyMesh queries for LrgNavAgent / LrgNavCrowd: a path
 * that says where its off-mesh links begin, and allocation-free boundary
 * segment queries.  Not installed, not introspected.
 */

#pragma once

#include "lrg-nav-poly-mesh.h"

G_BEGIN_DECLS

/*
 * _lrg_nav_poly_mesh_find_path_links:
 * As lrg_nav_poly_mesh_find_path(); @link_starts (GArray of guint)
 * receives the index (in points) of each path point where an off-mesh
 * link begins: the agent traverses the link from that point to the next.
 */
gfloat *
_lrg_nav_poly_mesh_find_path_links (LrgNavPolyMesh        *self,
                                    const gfloat          *start,
                                    const gfloat          *goal,
                                    const gfloat          *extents,
                                    guint                  max_nodes,
                                    LrgNavPolyFilterFunc   filter,
                                    gpointer               user_data,
                                    guint                 *n_floats,
                                    GArray                *link_starts,
                                    GError               **error);

/*
 * _lrg_nav_poly_mesh_query_boundary:
 * Fills @out (GArray of guint32) with the boundary segments whose X/Z
 * distance to @center is below @radius and whose height range, widened
 * by @half_height, holds @center's height; sorted by index.
 */
guint
_lrg_nav_poly_mesh_query_boundary (LrgNavPolyMesh *self,
                                   const gfloat   *center,
                                   gfloat          radius,
                                   gfloat          half_height,
                                   GArray         *out);

/*
 * _lrg_nav_poly_mesh_get_boundary_segment:
 * Six floats (a xyz, b xyz) of segment @index (walkable on the right).
 */
const gfloat *
_lrg_nav_poly_mesh_get_boundary_segment (LrgNavPolyMesh *self,
                                         guint           index);

G_END_DECLS
