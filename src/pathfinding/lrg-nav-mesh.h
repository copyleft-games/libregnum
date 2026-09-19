/* Triangle navigation mesh. SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <glib-object.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
#define LRG_TYPE_NAV_MESH (lrg_nav_mesh_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavMesh, lrg_nav_mesh, LRG, NAV_MESH, GObject)

/**
 * lrg_nav_mesh_new:
 *
 * Creates an empty navigation mesh. Operations are confined to one thread.
 *
 * Returns: (transfer full): a navigation mesh
 */
LRG_AVAILABLE_IN_ALL
LrgNavMesh *
lrg_nav_mesh_new (void);

/**
 * lrg_nav_mesh_bake:
 * @self: a navigation mesh
 * @vertices: (array length=n_coordinates): world-space XYZ coordinates, Y up
 * @n_coordinates: coordinate count, a positive multiple of three
 * @indices: (array length=n_indices): triangle vertex indices
 * @n_indices: index count, a positive multiple of three
 * @max_slope: walkable slope in degrees, 0 to less than 90
 * @error: (nullable): error return location
 *
 * Replaces the mesh atomically. Discards degenerate and steep triangles and
 * connects triangles sharing exact complete edges, including duplicated vertex
 * positions. Winding is ignored. Non-manifold edges are rejected. Input must
 * already describe unobstructed walkable surfaces, with clearance baked in for
 * finite-radius agents. This is not voxelization or obstacle subtraction.
 * Errors use G_IO_ERROR. Failed bakes preserve the previous mesh and flags.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_mesh_bake (LrgNavMesh *self,
                   const gdouble *vertices,
                   guint n_coordinates,
                   const guint *indices,
                   guint n_indices,
                   gdouble max_slope,
                   GError **error);

/**
 * lrg_nav_mesh_get_polygon_count:
 * @self: a navigation mesh
 *
 * Returns: the number of retained walkable triangles
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_mesh_get_polygon_count (LrgNavMesh *self);

/**
 * lrg_nav_mesh_set_enabled:
 * @self: a navigation mesh
 * @polygon: polygon index in the most recent successful bake
 * @enabled: whether this polygon can be projected onto or traversed
 *
 * Blocks or restores a polygon for subsequent queries without rebaking.
 *
 * Returns: %TRUE if the polygon exists
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_mesh_set_enabled (LrgNavMesh *self,
                          guint polygon,
                          gboolean enabled);

/**
 * lrg_nav_mesh_project:
 * @self: a navigation mesh
 * @point: (array fixed-size=3): query XYZ
 * @max_distance: finite nonnegative maximum 3D projection distance
 * @projected: (out caller-allocates) (array fixed-size=3): nearest enabled surface point
 * @polygon: (out) (optional): polygon index
 *
 * Projects onto the closest triangle, including edges. Ties prefer bake order.
 *
 * Returns: %TRUE when an enabled surface lies within the distance limit
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_mesh_project (LrgNavMesh *self,
                      const gdouble *point,
                      gdouble max_distance,
                      gdouble *projected,
                      guint *polygon);

/**
 * lrg_nav_mesh_find_path:
 * @self: a navigation mesh
 * @start: (array fixed-size=3): start XYZ
 * @goal: (array fixed-size=3): goal XYZ
 * @max_distance: maximum endpoint projection distance
 * @n_coordinates: (out): number of output coordinates
 * @error: (nullable): error return location
 *
 * Projects endpoints and runs A* over enabled triangles. Returns projected start,
 * shared-edge midpoints, and projected goal. Segments stay inside the selected
 * triangle corridor. The centroid-cost corridor is not a globally shortest
 * geometric path. G_IO_ERROR_NOT_FOUND means projection or connectivity failed.
 *
 * Returns: (transfer full) (array length=n_coordinates) (nullable): XYZ waypoints
 */
LRG_AVAILABLE_IN_ALL
gdouble *
lrg_nav_mesh_find_path (LrgNavMesh *self,
                        const gdouble *start,
                        const gdouble *goal,
                        gdouble max_distance,
                        guint *n_coordinates,
                        GError **error);
G_END_DECLS
