/* lrg-fracture.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFracture: Voronoi pre-fracture of a closed convex mesh into convex
 * chunks.  Seed points (random, impact biased, jittered grid or given)
 * define 3D Voronoi cells; every cell is the source mesh clipped by the
 * bisector planes to the other seeds, so chunks keep the original
 * surface (materials, texture coordinates) and gain flat interior faces
 * tagged with an interior material id.  Chunks carry exact mass
 * properties and convert to convex hull shapes and render meshes.  The
 * result caches to a `.rfracture` YAML file.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-physics-shape.h"
#include "lrg-dynamics.h"

G_BEGIN_DECLS

/**
 * LRG_FRACTURE_FORMAT_VERSION:
 *
 * Version written to and accepted from `.rfracture` files.
 */
#define LRG_FRACTURE_FORMAT_VERSION (1)

/**
 * LRG_FRACTURE_MAX_CHUNKS:
 *
 * Most seed points (and so chunks) one fracture accepts.
 */
#define LRG_FRACTURE_MAX_CHUNKS (1024u)

/**
 * LRG_FRACTURE_ERROR:
 *
 * Error domain of #LrgFracture.
 */
#define LRG_FRACTURE_ERROR (lrg_fracture_error_quark ())

/**
 * LrgFractureError:
 * @LRG_FRACTURE_ERROR_INVALID: invalid arguments or geometry (too few
 *   vertices, an index out of range, a non-finite value)
 * @LRG_FRACTURE_ERROR_NOT_CLOSED: the source mesh has an edge that is not
 *   shared by exactly two triangles
 * @LRG_FRACTURE_ERROR_NOT_CONVEX: the source mesh is not convex (split it
 *   into convex parts and fracture each)
 * @LRG_FRACTURE_ERROR_NO_SOURCE: no source mesh was set
 * @LRG_FRACTURE_ERROR_PARSE: a `.rfracture` file is malformed or has an
 *   unsupported version
 * @LRG_FRACTURE_ERROR_NO_GPU: a graphics context is required
 *
 * Errors of #LrgFracture.
 */
typedef enum
{
	LRG_FRACTURE_ERROR_INVALID    = 1,
	LRG_FRACTURE_ERROR_NOT_CLOSED = 2,
	LRG_FRACTURE_ERROR_NOT_CONVEX = 3,
	LRG_FRACTURE_ERROR_NO_SOURCE  = 4,
	LRG_FRACTURE_ERROR_PARSE      = 5,
	LRG_FRACTURE_ERROR_NO_GPU     = 6
} LrgFractureError;

/**
 * LrgFractureSeedMode:
 * @LRG_FRACTURE_SEED_RANDOM: uniform random points inside the mesh
 * @LRG_FRACTURE_SEED_IMPACT: random points concentrated round the impact
 *   point (small chunks where it was hit, big ones far away)
 * @LRG_FRACTURE_SEED_GRID: a grid fitted to the bounds, jittered (brick
 *   or tile like pieces)
 * @LRG_FRACTURE_SEED_CUSTOM: the points given to
 *   lrg_fracture_set_seed_points()
 *
 * How lrg_fracture_build() places the Voronoi seed points.
 */
typedef enum
{
	LRG_FRACTURE_SEED_RANDOM = 0,
	LRG_FRACTURE_SEED_IMPACT = 1,
	LRG_FRACTURE_SEED_GRID   = 2,
	LRG_FRACTURE_SEED_CUSTOM = 3
} LrgFractureSeedMode;

LRG_AVAILABLE_IN_ALL
GQuark lrg_fracture_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_fracture_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FRACTURE_ERROR (lrg_fracture_error_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_fracture_seed_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FRACTURE_SEED_MODE (lrg_fracture_seed_mode_get_type ())

/* ==========================================================================
 * LrgFractureChunk
 * ========================================================================== */

/**
 * LrgFractureChunk:
 *
 * One convex piece of a fractured mesh: an immutable, reference counted
 * boxed record.  Geometry is in the source mesh's frame (not centred);
 * every polygon face is triangulated with its own corners (flat
 * normals).
 */
typedef struct _LrgFractureChunk LrgFractureChunk;

#define LRG_TYPE_FRACTURE_CHUNK (lrg_fracture_chunk_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_fracture_chunk_get_type (void) G_GNUC_CONST;

/**
 * lrg_fracture_chunk_ref:
 * @self: a chunk
 *
 * Returns: (transfer full): @self with one more reference
 */
LRG_AVAILABLE_IN_ALL
LrgFractureChunk * lrg_fracture_chunk_ref (LrgFractureChunk *self);

/**
 * lrg_fracture_chunk_unref:
 * @self: (transfer full): a chunk
 *
 * Drops a reference; the last one frees the chunk.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fracture_chunk_unref (LrgFractureChunk *self);

/**
 * lrg_fracture_chunk_get_index:
 * @self: a chunk
 *
 * Returns: the chunk's index in its fracture
 */
LRG_AVAILABLE_IN_ALL
guint lrg_fracture_chunk_get_index (const LrgFractureChunk *self);

/**
 * lrg_fracture_chunk_get_seed:
 * @self: a chunk
 * @out: (out caller-allocates) (array fixed-size=3): the Voronoi seed
 *   point of the chunk's cell
 */
LRG_AVAILABLE_IN_ALL
void lrg_fracture_chunk_get_seed (const LrgFractureChunk *self,
                                  gfloat                 *out);

/**
 * lrg_fracture_chunk_get_vertex_count:
 * @self: a chunk
 *
 * Returns: the number of render vertices (face corners)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_fracture_chunk_get_vertex_count (const LrgFractureChunk *self);

/**
 * lrg_fracture_chunk_get_positions:
 * @self: a chunk
 * @n_floats: (out) (optional): 3 * vertex count
 *
 * Returns: (array length=n_floats) (transfer none): corner positions in
 *   the source frame
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_fracture_chunk_get_positions (const LrgFractureChunk *self,
                                                 guint                  *n_floats);

/**
 * lrg_fracture_chunk_get_normals:
 * @self: a chunk
 * @n_floats: (out) (optional): 3 * vertex count
 *
 * Returns: (array length=n_floats) (transfer none): flat face normals per
 *   corner
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_fracture_chunk_get_normals (const LrgFractureChunk *self,
                                               guint                  *n_floats);

/**
 * lrg_fracture_chunk_get_texcoords:
 * @self: a chunk
 * @n_floats: (out) (optional): 2 * vertex count
 *
 * Exterior faces interpolate the source's texture coordinates; interior
 * faces get a planar projection of the cut plane (one unit per metre).
 *
 * Returns: (array length=n_floats) (transfer none): texture coordinates
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_fracture_chunk_get_texcoords (const LrgFractureChunk *self,
                                                 guint                  *n_floats);

/**
 * lrg_fracture_chunk_get_indices:
 * @self: a chunk
 * @n_indices: (out) (optional): number of indices
 *
 * Returns: (array length=n_indices) (transfer none): counter-clockwise
 *   (outward facing) triangle list
 */
LRG_AVAILABLE_IN_ALL
const guint32 * lrg_fracture_chunk_get_indices (const LrgFractureChunk *self,
                                                guint                  *n_indices);

/**
 * lrg_fracture_chunk_get_triangle_materials:
 * @self: a chunk
 * @n_triangles: (out) (optional): number of triangles
 *
 * Material id per triangle: the source triangle's material on the
 * original surface, #LrgFracture:interior-material on cut faces.
 *
 * Returns: (array length=n_triangles) (transfer none): material ids
 */
LRG_AVAILABLE_IN_ALL
const guint32 * lrg_fracture_chunk_get_triangle_materials (const LrgFractureChunk *self,
                                                           guint                  *n_triangles);

/**
 * lrg_fracture_chunk_get_interior_triangle_count:
 * @self: a chunk
 * @interior_material: the interior material id to count
 *
 * Returns: how many triangles carry @interior_material
 */
LRG_AVAILABLE_IN_ALL
guint lrg_fracture_chunk_get_interior_triangle_count (const LrgFractureChunk *self,
                                                      guint32                 interior_material);

/**
 * lrg_fracture_chunk_get_hull_points:
 * @self: a chunk
 * @n_floats: (out) (optional): 3 * number of points
 *
 * The chunk's distinct corner points (its convex hull vertices), in the
 * source frame.
 *
 * Returns: (array length=n_floats) (transfer none): points
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_fracture_chunk_get_hull_points (const LrgFractureChunk *self,
                                                   guint                  *n_floats);

/**
 * lrg_fracture_chunk_get_neighbors:
 * @self: a chunk
 * @n_neighbors: (out) (optional): number of neighbours
 *
 * Chunks sharing a cut face with this one (ascending indices): the
 * connectivity a structural / progressive destruction model walks.
 *
 * Returns: (array length=n_neighbors) (transfer none): chunk indices
 */
LRG_AVAILABLE_IN_ALL
const guint32 * lrg_fracture_chunk_get_neighbors (const LrgFractureChunk *self,
                                                  guint                  *n_neighbors);

/**
 * lrg_fracture_chunk_get_volume:
 * @self: a chunk
 *
 * Returns: the enclosed volume in cubic metres
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fracture_chunk_get_volume (const LrgFractureChunk *self);

/**
 * lrg_fracture_chunk_get_center_of_mass:
 * @self: a chunk
 * @out: (out caller-allocates) (array fixed-size=3): centre of mass in
 *   the source frame
 */
LRG_AVAILABLE_IN_ALL
void lrg_fracture_chunk_get_center_of_mass (const LrgFractureChunk *self,
                                            gfloat                 *out);

/**
 * lrg_fracture_chunk_get_mass_properties:
 * @self: a chunk
 * @density: mass per cubic metre (> 0)
 * @mass: (out) (optional): density * volume
 * @com: (out caller-allocates) (array fixed-size=3) (optional): centre of
 *   mass in the source frame
 * @inertia: (out caller-allocates) (array fixed-size=9) (optional):
 *   inertia tensor about the centre of mass, source axes, column-major
 *
 * The same contract as lrg_physics_shape_get_mass_properties(), exact
 * for the chunk's polyhedron.
 *
 * Returns: %FALSE for a non-positive @density
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_chunk_get_mass_properties (const LrgFractureChunk *self,
                                                 gfloat                  density,
                                                 gfloat                 *mass,
                                                 gfloat                 *com,
                                                 gfloat                 *inertia);

/**
 * lrg_fracture_chunk_get_bounds:
 * @self: a chunk
 * @out_min: (out caller-allocates) (array fixed-size=3): lowest corner
 * @out_max: (out caller-allocates) (array fixed-size=3): highest corner
 */
LRG_AVAILABLE_IN_ALL
void lrg_fracture_chunk_get_bounds (const LrgFractureChunk *self,
                                    gfloat                 *out_min,
                                    gfloat                 *out_max);

/**
 * lrg_fracture_chunk_create_shape:
 * @self: a chunk
 * @error: (nullable): return location for an error
 *
 * Builds the chunk's collision shape: a convex hull of its points
 * relative to its centre of mass, so a body placed at
 * lrg_fracture_get_chunk_transform() lines up with the chunk.
 *
 * Returns: (transfer full) (nullable): a convex hull shape
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_fracture_chunk_create_shape (const LrgFractureChunk  *self,
                                                   GError                 **error);

/**
 * lrg_fracture_chunk_create_mesh:
 * @self: a chunk
 * @centered: %TRUE moves the vertices so the centre of mass is the
 *   origin (draw with the body transform), %FALSE keeps the source frame
 * @error: (nullable): return location for an error
 *
 * Builds a #GrlMesh of the chunk (positions, normals, texture
 * coordinates; at most 65535 corners).  Needs a graphics context.
 *
 * Returns: (transfer full) (nullable): a new uploaded mesh
 */
LRG_AVAILABLE_IN_ALL
GrlMesh * lrg_fracture_chunk_create_mesh (const LrgFractureChunk  *self,
                                          gboolean                 centered,
                                          GError                 **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgFractureChunk, lrg_fracture_chunk_unref)

/* ==========================================================================
 * LrgFracture
 * ========================================================================== */

#define LRG_TYPE_FRACTURE (lrg_fracture_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFracture, lrg_fracture, LRG, FRACTURE, GObject)

/**
 * lrg_fracture_new:
 *
 * Creates an empty fracture.  Set a source mesh, then lrg_fracture_build()
 * (or load a `.rfracture` file).
 *
 * Returns: (transfer full): a new #LrgFracture
 */
LRG_AVAILABLE_IN_ALL
LrgFracture * lrg_fracture_new (void);

/**
 * lrg_fracture_new_from_file:
 * @path: a `.rfracture` file
 * @error: (nullable): return location for an error
 *
 * Loads a fracture cache: parameters, seeds and chunks.  The source mesh
 * is not stored in the file, so the result has chunks but no source
 * (lrg_fracture_has_source() is %FALSE) until one is set.
 *
 * Returns: (transfer full) (nullable): the fracture, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgFracture * lrg_fracture_new_from_file (const gchar  *path,
                                          GError      **error);

/**
 * lrg_fracture_set_mesh:
 * @self: an #LrgFracture
 * @n_vertices: number of vertices (4 .. 65535)
 * @positions: (array) (element-type gfloat): 3 floats
 *   per vertex
 * @texcoords: (array) (element-type gfloat) (nullable): 2 floats per vertex
 * @n_indices: number of indices (a multiple of 3, at least 12)
 * @indices: (array length=n_indices): counter-clockwise (outward) triangles
 * @materials: (array) (element-type guint32) (nullable): one material id per
 *   triangle (%NULL: all 0)
 * @error: (nullable): return location for an error
 *
 * Sets the source mesh: closed (every edge shared by exactly two
 * triangles, positions welded) and convex.  Clears the chunks.
 *
 * Returns: %TRUE on success; on failure the fracture is unchanged
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_set_mesh (LrgFracture    *self,
                                guint           n_vertices,
                                const gfloat   *positions,
                                const gfloat   *texcoords,
                                guint           n_indices,
                                const guint32  *indices,
                                const guint32  *materials,
                                GError        **error);

/**
 * lrg_fracture_set_box:
 * @self: an #LrgFracture
 * @half_extents: (array fixed-size=3): half sizes (each > 0)
 * @error: (nullable): return location for an error
 *
 * Sets the source to a box centred on the origin (24 corners, per-face
 * texture coordinates 0..1, material 0): walls, crates, pillars.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_set_box (LrgFracture   *self,
                               const gfloat  *half_extents,
                               GError       **error);

/**
 * lrg_fracture_has_source:
 * @self: an #LrgFracture
 *
 * Returns: %TRUE when a source mesh is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_has_source (LrgFracture *self);

/**
 * lrg_fracture_get_source_volume:
 * @self: an #LrgFracture
 *
 * Returns: the source mesh's volume (0 without a source)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fracture_get_source_volume (LrgFracture *self);

/**
 * lrg_fracture_get_source_bounds:
 * @self: an #LrgFracture
 * @out_min: (out caller-allocates) (array fixed-size=3): lowest corner
 * @out_max: (out caller-allocates) (array fixed-size=3): highest corner
 *
 * Returns: %FALSE without a source
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_get_source_bounds (LrgFracture *self,
                                         gfloat      *out_min,
                                         gfloat      *out_max);

/**
 * lrg_fracture_set_impact_point:
 * @self: an #LrgFracture
 * @point: (array fixed-size=3) (nullable): impact point in the source
 *   frame for %LRG_FRACTURE_SEED_IMPACT, %NULL for the bounds centre
 */
LRG_AVAILABLE_IN_ALL
void lrg_fracture_set_impact_point (LrgFracture  *self,
                                    const gfloat *point);

/**
 * lrg_fracture_get_impact_point:
 * @self: an #LrgFracture
 * @out: (out caller-allocates) (array fixed-size=3): the impact point
 *
 * Returns: %TRUE when one was set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_get_impact_point (LrgFracture *self,
                                        gfloat      *out);

/**
 * lrg_fracture_set_seed_points:
 * @self: an #LrgFracture
 * @points: (array length=n_floats) (nullable): 3 floats per seed
 * @n_floats: number of floats (a multiple of 3, at most
 *   3 * %LRG_FRACTURE_MAX_CHUNKS)
 *
 * Gives explicit seed points and switches #LrgFracture:seed-mode to
 * %LRG_FRACTURE_SEED_CUSTOM (%NULL clears them).  Seeds outside the mesh
 * produce no chunk.
 *
 * Returns: %TRUE when the points were accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_set_seed_points (LrgFracture  *self,
                                       const gfloat *points,
                                       guint         n_floats);

/**
 * lrg_fracture_get_seed_points:
 * @self: an #LrgFracture
 * @n_floats: (out) (optional): 3 * number of seeds
 *
 * The seeds of the last build (or the custom ones).
 *
 * Returns: (array length=n_floats) (transfer none) (nullable): seeds
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_fracture_get_seed_points (LrgFracture *self,
                                             guint       *n_floats);

/**
 * lrg_fracture_build:
 * @self: an #LrgFracture
 * @error: (nullable): return location for an error
 *
 * Places the seeds (#LrgFracture:seed-mode, #LrgFracture:chunk-count,
 * #LrgFracture:seed) and clips one Voronoi cell per seed out of the
 * source mesh.  Deterministic: the same source and parameters give the
 * same chunks on every platform that rounds floats the same way.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_build (LrgFracture  *self,
                             GError      **error);

/**
 * lrg_fracture_is_built:
 * @self: an #LrgFracture
 *
 * Returns: %TRUE when chunks exist (built or loaded)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_is_built (LrgFracture *self);

/**
 * lrg_fracture_get_chunk_count:
 * @self: an #LrgFracture
 *
 * Returns: the number of chunks
 */
LRG_AVAILABLE_IN_ALL
guint lrg_fracture_get_chunk_count (LrgFracture *self);

/**
 * lrg_fracture_get_chunk:
 * @self: an #LrgFracture
 * @index: chunk index
 *
 * Returns: (transfer none) (nullable): the chunk, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
LrgFractureChunk * lrg_fracture_get_chunk (LrgFracture *self,
                                           guint        index);

/**
 * lrg_fracture_get_total_volume:
 * @self: an #LrgFracture
 *
 * Returns: the summed chunk volumes (equal to the source volume up to
 *   rounding)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fracture_get_total_volume (LrgFracture *self);

/**
 * lrg_fracture_get_source_hash:
 * @self: an #LrgFracture
 *
 * A SHA-256 over the source geometry and every build parameter.  A
 * `.rfracture` file whose `source_hash` matches was built from exactly
 * this input.
 *
 * Returns: (transfer full) (nullable): hex digest, %NULL without a source
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_fracture_get_source_hash (LrgFracture *self);

/**
 * lrg_fracture_save_to_data:
 * @self: an #LrgFracture
 * @length: (out) (optional): length of the result
 *
 * Serialises the fracture as `.rfracture` YAML (floats with nine
 * significant digits, so a float survives the round trip exactly).
 *
 * Returns: (transfer full): the document
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_fracture_save_to_data (LrgFracture *self,
                                   gsize       *length);

/**
 * lrg_fracture_save_to_file:
 * @self: an #LrgFracture
 * @path: destination
 * @error: (nullable): return location for an error
 *
 * Writes lrg_fracture_save_to_data() atomically to @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_save_to_file (LrgFracture  *self,
                                    const gchar  *path,
                                    GError      **error);

/**
 * lrg_fracture_load_from_data:
 * @self: an #LrgFracture
 * @data: `.rfracture` YAML
 * @length: length of @data, or -1 when NUL terminated
 * @error: (nullable): return location for an error
 *
 * Replaces parameters, seeds and chunks with the document's; keeps the
 * source mesh.
 *
 * Returns: %TRUE on success; on failure nothing changes
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_load_from_data (LrgFracture  *self,
                                      const gchar  *data,
                                      gssize        length,
                                      GError      **error);

/**
 * lrg_fracture_load_from_file:
 * @self: an #LrgFracture
 * @path: a `.rfracture` file
 * @error: (nullable): return location for an error
 *
 * lrg_fracture_load_from_data() on a file's contents.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_load_from_file (LrgFracture  *self,
                                      const gchar  *path,
                                      GError      **error);

/**
 * lrg_fracture_load_or_build:
 * @self: an #LrgFracture with a source mesh
 * @path: (nullable): the cache file
 * @error: (nullable): return location for an error
 *
 * The cache workflow: loads @path when it exists and its `source_hash`
 * matches lrg_fracture_get_source_hash(), otherwise builds and (with a
 * @path) writes the file.
 *
 * Returns: %TRUE when chunks are available
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_load_or_build (LrgFracture  *self,
                                     const gchar  *path,
                                     GError      **error);

/**
 * lrg_fracture_should_break:
 * @self: an #LrgFracture
 * @impulse: magnitude of a contact impulse in N·s
 *
 * Returns: %TRUE when @impulse reaches #LrgFracture:impulse-threshold
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_should_break (LrgFracture *self,
                                    gfloat       impulse);

/**
 * lrg_fracture_get_chunk_transform:
 * @self: an #LrgFracture
 * @index: chunk index
 * @object_matrix: (array fixed-size=16) (nullable): the intact object's
 *   world matrix (rigid), %NULL for identity
 * @out_matrix: (out caller-allocates) (array fixed-size=16): world
 *   matrix of the chunk's body (its frame sits at the centre of mass)
 *
 * Where to spawn chunk @index so it lines up with the intact object.
 *
 * Returns: %TRUE when @index is a chunk
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_get_chunk_transform (LrgFracture  *self,
                                           guint         index,
                                           const gfloat *object_matrix,
                                           gfloat       *out_matrix);

/**
 * lrg_fracture_get_chunk_velocity:
 * @self: an #LrgFracture
 * @index: chunk index
 * @object_matrix: (array fixed-size=16) (nullable): the object's world
 *   matrix, %NULL for identity
 * @impact_point: (array fixed-size=3) (nullable): world impact point,
 *   %NULL for none
 * @impulse: (array fixed-size=3) (nullable): world impulse that broke
 *   the object, %NULL for none
 * @out_linear: (out caller-allocates) (array fixed-size=3): initial
 *   linear velocity of the chunk
 *
 * A simple break-up model for the swap: every chunk gets the impulse
 * divided by the object's mass, weighted by 2 r / (r + d) where d is the
 * chunk centre's distance to the impact and r the object's bounding
 * radius (chunks at the hit fly twice as fast as the average), plus an
 * outward burst of 10 % of that speed away from the impact point.
 *
 * Returns: %TRUE when @index is a chunk
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fracture_get_chunk_velocity (LrgFracture  *self,
                                          guint         index,
                                          const gfloat *object_matrix,
                                          const gfloat *impact_point,
                                          const gfloat *impulse,
                                          gfloat       *out_linear);

/**
 * lrg_fracture_swap_in:
 * @self: a built #LrgFracture
 * @world: the #LrgDynamicsWorld to add the chunks to
 * @object_matrix: (array fixed-size=16) (nullable): the intact object's
 *   rigid world matrix, %NULL for identity
 * @impact_point: (array fixed-size=3) (nullable): world impact point
 * @impulse: (array fixed-size=3) (nullable): world impulse that broke it
 * @layer: object layer of the chunks (%LRG_PHYSICS_LAYER_DEBRIS is the
 *   usual choice)
 * @error: (nullable): return location for an error
 *
 * Replaces the intact object by its chunks: one dynamic body per chunk
 * with its convex hull shape (lrg_fracture_chunk_create_shape()), mass
 * #LrgFracture:density times its volume, placed with
 * lrg_fracture_get_chunk_transform() and launched with
 * lrg_fracture_get_chunk_velocity().  Removing the intact body is the
 * caller's job.  On failure the bodies already added are removed again.
 *
 * Returns: (transfer full) (element-type guint) (nullable): the new body
 *   ids in chunk order, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GArray * lrg_fracture_swap_in (LrgFracture       *self,
                               LrgDynamicsWorld  *world,
                               const gfloat      *object_matrix,
                               const gfloat      *impact_point,
                               const gfloat      *impulse,
                               guint              layer,
                               GError           **error);

G_END_DECLS
