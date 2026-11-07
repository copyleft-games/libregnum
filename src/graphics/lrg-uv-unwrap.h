/* lrg-uv-unwrap.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUvMesh and LrgUvUnwrap - a headless CPU triangle mesh and the
 * lightmap chart unwrapper that gives it a second UV set (uv2, glTF
 * TEXCOORD_1, raylib texcoords2).
 *
 * The unwrapper cuts a mesh into charts (flood fill across shared edges
 * while each triangle stays within #LrgUvUnwrap:max-chart-angle of the
 * chart's seed normal and does not overlap the chart in projection),
 * projects every chart onto its seed plane, turns it to its minimum-area
 * bounding rectangle, and packs the charts with a skyline packer into
 * the unit square at the largest uniform scale that fits, leaving
 * #LrgUvUnwrap:padding texels (of an #LrgUvUnwrap:resolution sized map)
 * between charts and around the border.  All texel densities inside one
 * mesh are equal.  The output is a new mesh whose vertices are split
 * along chart seams (lrg_uv_unwrap_get_xrefs() maps them back).
 *
 * The concepts (chart growing by normal deviation, planar projection,
 * minimum-area rotation, padded packing) follow xatlas (MIT) and
 * Thekla; no code is taken from either.
 *
 * The result is deterministic: the same mesh and settings give the same
 * uv2 bit for bit, which is what lets a renderer regenerate the uv2 of a
 * built-in primitive at load time and match a lightmap baked earlier
 * (lrg_uv_unwrap_new_lightmap_primitive()).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_UV_UNWRAP_DEFAULT_RESOLUTION:
 *
 * Default #LrgUvUnwrap:resolution: the map size, in texels, the padding
 * is measured in.  The lightmap baker gives meshes it unwrapped at
 * least #LRG_UV_UNWRAP_DEFAULT_RESOLUTION / #LRG_UV_UNWRAP_DEFAULT_PADDING
 * texels so the gaps between charts stay at least 2 texels wide.
 */
#define LRG_UV_UNWRAP_DEFAULT_RESOLUTION (64)

/**
 * LRG_UV_UNWRAP_DEFAULT_PADDING:
 *
 * Default #LrgUvUnwrap:padding in texels.
 */
#define LRG_UV_UNWRAP_DEFAULT_PADDING (2)

/**
 * LRG_UV_UNWRAP_DEFAULT_MAX_CHART_ANGLE:
 *
 * Default #LrgUvUnwrap:max-chart-angle in degrees.
 */
#define LRG_UV_UNWRAP_DEFAULT_MAX_CHART_ANGLE (50.0f)

/**
 * LrgUvUnwrapError:
 * @LRG_UV_UNWRAP_ERROR_EMPTY: the mesh has no triangles
 * @LRG_UV_UNWRAP_ERROR_INVALID: an index is out of range or a value is
 *   not finite
 * @LRG_UV_UNWRAP_ERROR_NO_FIT: the charts do not fit the unit square
 *   even at the smallest scale (too many charts for the resolution and
 *   padding)
 *
 * Errors of lrg_uv_unwrap_run().
 */
typedef enum
{
	LRG_UV_UNWRAP_ERROR_EMPTY,
	LRG_UV_UNWRAP_ERROR_INVALID,
	LRG_UV_UNWRAP_ERROR_NO_FIT
} LrgUvUnwrapError;

LRG_AVAILABLE_IN_ALL
GType lrg_uv_unwrap_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_UV_UNWRAP_ERROR (lrg_uv_unwrap_error_get_type ())

/**
 * LRG_UV_UNWRAP_ERROR:
 *
 * Error domain of #LrgUvUnwrapError.
 */
#define LRG_UV_UNWRAP_ERROR (lrg_uv_unwrap_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_uv_unwrap_error_quark (void);

/* ==========================================================================
 * LrgUvMesh
 * ========================================================================== */

#define LRG_TYPE_UV_MESH (lrg_uv_mesh_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUvMesh, lrg_uv_mesh, LRG, UV_MESH, GObject)

/**
 * lrg_uv_mesh_new:
 * @positions: (array length=n_floats): vertex positions, 3 floats each
 * @n_floats: number of floats in @positions (3 x the vertex count)
 * @indices: (array length=n_indices) (nullable): triangle list, counter-
 *   clockwise front faces; %NULL for sequential triangles
 * @n_indices: number of indices (a multiple of 3), ignored without
 *   @indices
 *
 * Creates a CPU mesh from copies of the arrays.  Normals, uvs and uv2
 * start empty.
 *
 * Returns: (transfer full) (nullable): the mesh, or %NULL when an index
 *   is out of range or @n_indices is not a multiple of 3
 */
LRG_AVAILABLE_IN_ALL
LrgUvMesh * lrg_uv_mesh_new (const gfloat  *positions,
                             gsize          n_floats,
                             const guint32 *indices,
                             gsize          n_indices);

/**
 * lrg_uv_mesh_new_primitive:
 * @primitive: a solid #LrgPrimitiveType
 * @params: (array fixed-size=4) (nullable): the render item parameters
 *   of the primitive (#LrgRenderItem.params: cube x/y/z size, sphere /
 *   disc radius, cylinder and cone radius and height, torus major
 *   radius, plane x/z size, 2D rectangle width/height); 0 or %NULL for
 *   the unit size
 *
 * Builds the primitive the 3D renderer draws for a mesh renderer
 * component, already scaled by @params, in object space: a box centred
 * on the origin, a 16 x 24 UV sphere, a 1 x 1 plane on XZ facing +Y,
 * cylinders and cones of 24 sides centred on the origin, a 32-sided
 * disc facing +Y, a 32 x 12 torus around Y, 2D shapes facing +Z.
 * Vertices carry normals and uvs.  Faces are split at hard edges.
 *
 * Returns: (transfer full) (nullable): the mesh, or %NULL for a
 *   primitive without a surface (grid, custom mesh)
 */
LRG_AVAILABLE_IN_ALL
LrgUvMesh * lrg_uv_mesh_new_primitive (LrgPrimitiveType  primitive,
                                       const gfloat     *params);

/**
 * lrg_uv_mesh_new_from_raylib:
 * @mesh: (not nullable): a raylib `Mesh *` with CPU data (vertices, and
 *   optionally normals, texcoords, texcoords2 and indices)
 *
 * Copies a raylib mesh's CPU arrays.  Animated vertices are ignored.
 *
 * Returns: (transfer full) (nullable): the mesh, or %NULL when @mesh has
 *   no CPU vertices
 */
LRG_AVAILABLE_IN_ALL
LrgUvMesh * lrg_uv_mesh_new_from_raylib (gconstpointer mesh);

/**
 * lrg_uv_mesh_to_raylib:
 * @self: an #LrgUvMesh
 * @out_mesh: (not nullable): a raylib `Mesh *` to fill
 *
 * Fills @out_mesh (zeroed first) with newly allocated raylib CPU arrays
 * (vertices, normals, texcoords, texcoords2 when present, indices) that
 * UnloadMesh() frees; the caller uploads it with UploadMesh().  raylib
 * indices are 16-bit, so a mesh of more than 65535 vertices is written
 * without indices (one vertex per corner).
 */
LRG_AVAILABLE_IN_ALL
void lrg_uv_mesh_to_raylib (LrgUvMesh *self,
                            gpointer   out_mesh);

/**
 * lrg_uv_mesh_get_n_vertices:
 * @self: an #LrgUvMesh
 *
 * Returns: the vertex count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_uv_mesh_get_n_vertices (LrgUvMesh *self);

/**
 * lrg_uv_mesh_get_n_triangles:
 * @self: an #LrgUvMesh
 *
 * Returns: the triangle count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_uv_mesh_get_n_triangles (LrgUvMesh *self);

/**
 * lrg_uv_mesh_get_positions:
 * @self: an #LrgUvMesh
 * @n_floats: (out) (optional): 3 x the vertex count
 *
 * Returns: (transfer none) (array length=n_floats): the positions
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_uv_mesh_get_positions (LrgUvMesh *self,
                                          gsize     *n_floats);

/**
 * lrg_uv_mesh_get_indices:
 * @self: an #LrgUvMesh
 * @n_indices: (out) (optional): 3 x the triangle count
 *
 * Returns: (transfer none) (array length=n_indices): the triangle list
 */
LRG_AVAILABLE_IN_ALL
const guint32 * lrg_uv_mesh_get_indices (LrgUvMesh *self,
                                         gsize     *n_indices);

/**
 * lrg_uv_mesh_get_normals:
 * @self: an #LrgUvMesh
 * @n_floats: (out) (optional): 3 x the vertex count, 0 without normals
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): the
 *   normals
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_uv_mesh_get_normals (LrgUvMesh *self,
                                        gsize     *n_floats);

/**
 * lrg_uv_mesh_set_normals:
 * @self: an #LrgUvMesh
 * @normals: (array length=n_floats) (nullable): 3 floats per vertex, or
 *   %NULL to drop them
 * @n_floats: number of floats (must be 3 x the vertex count)
 *
 * Returns: %FALSE when @n_floats does not match the vertex count
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_uv_mesh_set_normals (LrgUvMesh    *self,
                                  const gfloat *normals,
                                  gsize         n_floats);

/**
 * lrg_uv_mesh_compute_normals:
 * @self: an #LrgUvMesh
 *
 * Replaces the normals with area-weighted vertex normals over the
 * triangles that share each vertex index (vertices split at hard edges
 * keep their hard edges).
 */
LRG_AVAILABLE_IN_ALL
void lrg_uv_mesh_compute_normals (LrgUvMesh *self);

/**
 * lrg_uv_mesh_get_uvs:
 * @self: an #LrgUvMesh
 * @n_floats: (out) (optional): 2 x the vertex count, 0 without uvs
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): the
 *   first texture coordinates
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_uv_mesh_get_uvs (LrgUvMesh *self,
                                    gsize     *n_floats);

/**
 * lrg_uv_mesh_set_uvs:
 * @self: an #LrgUvMesh
 * @uvs: (array length=n_floats) (nullable): 2 floats per vertex, or
 *   %NULL to drop them
 * @n_floats: number of floats (must be 2 x the vertex count)
 *
 * Returns: %FALSE when @n_floats does not match the vertex count
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_uv_mesh_set_uvs (LrgUvMesh    *self,
                              const gfloat *uvs,
                              gsize         n_floats);

/**
 * lrg_uv_mesh_get_uv2:
 * @self: an #LrgUvMesh
 * @n_floats: (out) (optional): 2 x the vertex count, 0 without uv2
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): the
 *   second texture coordinates (lightmap UVs, v = 0 at the first atlas
 *   row)
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_uv_mesh_get_uv2 (LrgUvMesh *self,
                                    gsize     *n_floats);

/**
 * lrg_uv_mesh_set_uv2:
 * @self: an #LrgUvMesh
 * @uv2: (array length=n_floats) (nullable): 2 floats per vertex, or
 *   %NULL to drop them
 * @n_floats: number of floats (must be 2 x the vertex count)
 *
 * Returns: %FALSE when @n_floats does not match the vertex count
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_uv_mesh_set_uv2 (LrgUvMesh    *self,
                              const gfloat *uv2,
                              gsize         n_floats);

/**
 * lrg_uv_mesh_get_surface_area:
 * @self: an #LrgUvMesh
 * @matrix: (array fixed-size=16) (nullable): a column-major transform
 *   applied first (see lrg_math3d_transform_point()), %NULL for object
 *   space
 *
 * Returns: the summed triangle area
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_uv_mesh_get_surface_area (LrgUvMesh    *self,
                                      const gfloat *matrix);

/**
 * lrg_uv_mesh_get_uv2_area:
 * @self: an #LrgUvMesh
 *
 * Returns: the summed area of the triangles in uv2 space (the fraction
 *   of the unit square the charts cover), 0 without uv2
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_uv_mesh_get_uv2_area (LrgUvMesh *self);

/* ==========================================================================
 * LrgUvUnwrap
 * ========================================================================== */

#define LRG_TYPE_UV_UNWRAP (lrg_uv_unwrap_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUvUnwrap, lrg_uv_unwrap, LRG, UV_UNWRAP, GObject)

/**
 * lrg_uv_unwrap_new:
 *
 * Creates an unwrapper with the default settings
 * (%LRG_UV_UNWRAP_DEFAULT_RESOLUTION, %LRG_UV_UNWRAP_DEFAULT_PADDING,
 * %LRG_UV_UNWRAP_DEFAULT_MAX_CHART_ANGLE).
 *
 * Returns: (transfer full): a new #LrgUvUnwrap
 */
LRG_AVAILABLE_IN_ALL
LrgUvUnwrap * lrg_uv_unwrap_new (void);

/**
 * lrg_uv_unwrap_get_resolution:
 * @self: an #LrgUvUnwrap
 *
 * Returns: the map size the padding is measured in (texels)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_uv_unwrap_get_resolution (LrgUvUnwrap *self);

/**
 * lrg_uv_unwrap_set_resolution:
 * @self: an #LrgUvUnwrap
 * @resolution: map size in texels, 8 .. 8192
 *
 * Sets #LrgUvUnwrap:resolution.
 */
LRG_AVAILABLE_IN_ALL
void lrg_uv_unwrap_set_resolution (LrgUvUnwrap *self,
                                   guint        resolution);

/**
 * lrg_uv_unwrap_get_padding:
 * @self: an #LrgUvUnwrap
 *
 * Returns: the padding in texels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_uv_unwrap_get_padding (LrgUvUnwrap *self);

/**
 * lrg_uv_unwrap_set_padding:
 * @self: an #LrgUvUnwrap
 * @padding: texels left free around every chart, 0 .. 16
 *
 * Sets #LrgUvUnwrap:padding.  Charts end up 2 x @padding texels apart.
 */
LRG_AVAILABLE_IN_ALL
void lrg_uv_unwrap_set_padding (LrgUvUnwrap *self,
                                guint        padding);

/**
 * lrg_uv_unwrap_get_max_chart_angle:
 * @self: an #LrgUvUnwrap
 *
 * Returns: the chart normal deviation limit in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_uv_unwrap_get_max_chart_angle (LrgUvUnwrap *self);

/**
 * lrg_uv_unwrap_set_max_chart_angle:
 * @self: an #LrgUvUnwrap
 * @degrees: largest angle between a triangle's normal and its chart's
 *   seed normal, 1 .. 85
 *
 * Sets #LrgUvUnwrap:max-chart-angle.  Larger angles make fewer, more
 * stretched charts (texel density in a chart varies by up to
 * 1 / cos (@degrees)).
 */
LRG_AVAILABLE_IN_ALL
void lrg_uv_unwrap_set_max_chart_angle (LrgUvUnwrap *self,
                                        gfloat       degrees);

/**
 * lrg_uv_unwrap_run:
 * @self: an #LrgUvUnwrap
 * @mesh: the mesh to unwrap
 * @error: (nullable): return location for a #GError
 *
 * Unwraps @mesh.  The result has the same triangles in the same order;
 * its vertices are those of @mesh duplicated where charts meet, with the
 * normals and uvs carried over and uv2 filled.  Statistics of the run
 * are kept until the next one.
 *
 * Returns: (transfer full) (nullable): the unwrapped mesh, or %NULL on
 *   error
 */
LRG_AVAILABLE_IN_ALL
LrgUvMesh * lrg_uv_unwrap_run (LrgUvUnwrap  *self,
                               LrgUvMesh    *mesh,
                               GError      **error);

/**
 * lrg_uv_unwrap_get_n_charts:
 * @self: an #LrgUvUnwrap
 *
 * Returns: the chart count of the last run
 */
LRG_AVAILABLE_IN_ALL
guint lrg_uv_unwrap_get_n_charts (LrgUvUnwrap *self);

/**
 * lrg_uv_unwrap_get_xrefs:
 * @self: an #LrgUvUnwrap
 * @n_vertices: (out) (optional): vertex count of the last result
 *
 * Returns: (transfer none) (array length=n_vertices) (nullable): for each
 *   vertex of the last result, the input vertex it was copied from
 */
LRG_AVAILABLE_IN_ALL
const guint32 * lrg_uv_unwrap_get_xrefs (LrgUvUnwrap *self,
                                         guint       *n_vertices);

/**
 * lrg_uv_unwrap_get_chart_ids:
 * @self: an #LrgUvUnwrap
 * @n_triangles: (out) (optional): triangle count of the last result
 *
 * Returns: (transfer none) (array length=n_triangles) (nullable): the
 *   chart of each triangle of the last result
 */
LRG_AVAILABLE_IN_ALL
const guint32 * lrg_uv_unwrap_get_chart_ids (LrgUvUnwrap *self,
                                             guint       *n_triangles);

/**
 * lrg_uv_unwrap_get_utilization:
 * @self: an #LrgUvUnwrap
 *
 * Returns: the fraction of the unit square the charts of the last run
 *   cover (0 .. 1)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_uv_unwrap_get_utilization (LrgUvUnwrap *self);

/**
 * lrg_uv_unwrap_new_lightmap_primitive:
 * @primitive: a solid #LrgPrimitiveType
 * @params: (array fixed-size=4) (nullable): the render item parameters
 *   (see lrg_uv_mesh_new_primitive())
 *
 * lrg_uv_mesh_new_primitive() unwrapped with the default settings: the
 * exact mesh, uv2 included, the lightmap baker bakes for a primitive
 * mesh renderer.  A renderer that draws a lightmapped primitive uploads
 * this mesh (lrg_uv_mesh_to_raylib() + UploadMesh()) and draws it with
 * the item's world matrix alone (the size is already in the vertices).
 *
 * Returns: (transfer full) (nullable): the mesh, or %NULL for a
 *   primitive without a surface
 */
LRG_AVAILABLE_IN_ALL
LrgUvMesh * lrg_uv_unwrap_new_lightmap_primitive (LrgPrimitiveType  primitive,
                                                  const gfloat     *params);

/**
 * lrg_uv_unwrap_pack_rects:
 * @sizes: (array length=n_sizes): width, height pairs in texels
 * @n_sizes: number of integers in @sizes (2 x the rectangle count)
 * @page_size: page edge in texels
 * @max_pages: most pages to open (0 = no limit)
 * @out_placements: (out caller-allocates) (array): x, y and page index
 *   per rectangle (3 x n_sizes / 2 integers)
 * @out_n_pages: (out) (optional): pages used
 *
 * Skyline bottom-left packing of rectangles into square pages, tallest
 * first (ties keep input order, so the result is deterministic).  The
 * lightmap baker packs its objects with it; it is public for tools.
 *
 * Returns: %FALSE when a rectangle is larger than a page or @max_pages
 *   pages are not enough
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_uv_unwrap_pack_rects (const guint *sizes,
                                   gsize        n_sizes,
                                   guint        page_size,
                                   guint        max_pages,
                                   guint       *out_placements,
                                   guint       *out_n_pages);

G_END_DECLS
