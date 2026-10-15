/* lrg-terrain.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrain - chunked heightfield terrain for large streamed worlds.
 * Chunks are addressed by integer (cx, cz) and hold a square grid of
 * heights plus one material (texture) per grid cell with an optional UV
 * orientation, so tile-based worlds can reuse one tile texture in eight
 * orientations. Height queries and mesh generation are headless; only
 * upload() and draw() need a GL context.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TERRAIN (lrg_terrain_get_type ())

/**
 * LRG_TERRAIN_MAX_RESOLUTION:
 *
 * Largest number of cells along one side of a chunk.
 */
#define LRG_TERRAIN_MAX_RESOLUTION (1024)

/**
 * LRG_TERRAIN_MAX_MESH_VERTICES:
 *
 * Largest vertex count of one generated mesh: every index fits in 16
 * bits. A material covering more cells of a chunk is split into several
 * meshes.
 */
#define LRG_TERRAIN_MAX_MESH_VERTICES (65535)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTerrain, lrg_terrain, LRG, TERRAIN, GObject)

/**
 * lrg_terrain_new:
 * @resolution: cells along one side of a chunk, 1 ..
 *   %LRG_TERRAIN_MAX_RESOLUTION
 * @chunk_size: world size of one chunk side (> 0)
 *
 * Creates an empty terrain. Each chunk has (@resolution + 1)² heights
 * and @resolution² cells of size @chunk_size / @resolution. Chunk (cx, cz)
 * covers world X in [cx * @chunk_size, (cx + 1) * @chunk_size] and the
 * same for Z.
 *
 * Returns: (transfer full): a new #LrgTerrain
 */
LRG_AVAILABLE_IN_ALL
LrgTerrain *
lrg_terrain_new (guint  resolution,
                 gfloat chunk_size);

/**
 * lrg_terrain_get_resolution:
 * @self: an #LrgTerrain
 *
 * Returns: cells along one side of a chunk
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_terrain_get_resolution (LrgTerrain *self);

/**
 * lrg_terrain_get_chunk_size:
 * @self: an #LrgTerrain
 *
 * Returns: world size of one chunk side
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_terrain_get_chunk_size (LrgTerrain *self);

/**
 * lrg_terrain_set_chunk:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 * @heights: (array length=n_heights): (resolution + 1)² heights, row
 *   major: index = j * (resolution + 1) + i with i along +X, j along +Z
 * @n_heights: number of heights
 * @materials: (array) (nullable): resolution² material ids, one per
 *   cell (index = j * resolution + i); %NULL means material 0 everywhere
 * @variants: (array) (nullable): resolution² #LrgTerrainUvVariant bit
 *   sets, one per cell; %NULL means no rotation or flip
 * @error: (nullable): return location for a #GError
 *
 * Adds or replaces the chunk at (@cx, @cz). The data is copied. The
 * chunk's mesh and the edge normals of its four neighbours are rebuilt
 * lazily; call lrg_terrain_upload() before drawing them.
 *
 * Errors: %G_IO_ERROR_INVALID_ARGUMENT when @n_heights is wrong, a
 * height is not finite or a variant uses bits other than the three
 * #LrgTerrainUvVariant flags. Nothing changes then.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_terrain_set_chunk (LrgTerrain    *self,
                       gint           cx,
                       gint           cz,
                       const gfloat  *heights,
                       guint          n_heights,
                       const guint   *materials,
                       const guint8  *variants,
                       GError       **error);

/**
 * lrg_terrain_remove_chunk:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 *
 * Removes a chunk and releases its GPU meshes.
 *
 * Returns: %TRUE if the chunk existed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_terrain_remove_chunk (LrgTerrain *self,
                          gint        cx,
                          gint        cz);

/**
 * lrg_terrain_has_chunk:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 *
 * Returns: %TRUE if the chunk is loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_terrain_has_chunk (LrgTerrain *self,
                       gint        cx,
                       gint        cz);

/**
 * lrg_terrain_get_chunk_count:
 * @self: an #LrgTerrain
 *
 * Returns: the number of loaded chunks
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_terrain_get_chunk_count (LrgTerrain *self);

/**
 * lrg_terrain_clear:
 * @self: an #LrgTerrain
 *
 * Removes every chunk. Material textures, origin, texture repeat and
 * shader are kept.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_clear (LrgTerrain *self);

/**
 * lrg_terrain_world_to_chunk:
 * @self: an #LrgTerrain
 * @x: world X
 * @z: world Z
 * @cx: (out) (optional): chunk X containing @x
 * @cz: (out) (optional): chunk Z containing @z
 *
 * Converts a world position to the coordinates of the chunk containing
 * it (floor division, so negative positions work).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_world_to_chunk (LrgTerrain *self,
                            gdouble     x,
                            gdouble     z,
                            gint       *cx,
                            gint       *cz);

/**
 * lrg_terrain_height_at:
 * @self: an #LrgTerrain
 * @x: world X
 * @z: world Z
 * @height: (out) (optional): bilinearly interpolated height
 *
 * Samples the heightfield at a world position (world coordinates, not
 * origin-relative). Headless.
 *
 * Returns: %TRUE when the position lies in a loaded chunk
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_terrain_height_at (LrgTerrain *self,
                       gdouble     x,
                       gdouble     z,
                       gfloat     *height);

/**
 * lrg_terrain_get_chunk_bounds:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 * @min_x: (out) (optional): minimum world X
 * @min_y: (out) (optional): minimum height
 * @min_z: (out) (optional): minimum world Z
 * @max_x: (out) (optional): maximum world X
 * @max_y: (out) (optional): maximum height
 * @max_z: (out) (optional): maximum world Z
 *
 * World-space axis-aligned bounds of a chunk, for culling.
 *
 * Returns: %TRUE if the chunk is loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_terrain_get_chunk_bounds (LrgTerrain *self,
                              gint        cx,
                              gint        cz,
                              gdouble    *min_x,
                              gdouble    *min_y,
                              gdouble    *min_z,
                              gdouble    *max_x,
                              gdouble    *max_y,
                              gdouble    *max_z);

/**
 * lrg_terrain_set_texture_repeat:
 * @self: an #LrgTerrain
 * @cells: cells covered by one repeat of a texture (>= 1)
 *
 * With the default of 1 every cell shows a whole texture (a tile). With
 * N, a texture spans N × N cells and continues seamlessly across cell and
 * chunk borders; the cell's #LrgTerrainUvVariant then reorients its part
 * of the texture. Rebuilds every mesh.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_set_texture_repeat (LrgTerrain *self,
                                guint       cells);

/**
 * lrg_terrain_get_texture_repeat:
 * @self: an #LrgTerrain
 *
 * Returns: cells covered by one texture repeat
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_terrain_get_texture_repeat (LrgTerrain *self);

/**
 * lrg_terrain_set_origin:
 * @self: an #LrgTerrain
 * @x: origin X
 * @y: origin Y
 * @z: origin Z
 *
 * Floating origin: drawing subtracts this world position, so a camera
 * placed at (camera - origin) sees the terrain in the right place with
 * full float precision far from world zero. Queries keep using world
 * coordinates. Meshes are chunk-local, so moving the origin re-uploads
 * nothing.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_set_origin (LrgTerrain *self,
                        gdouble     x,
                        gdouble     y,
                        gdouble     z);

/**
 * lrg_terrain_get_origin:
 * @self: an #LrgTerrain
 * @x: (out) (optional): origin X
 * @y: (out) (optional): origin Y
 * @z: (out) (optional): origin Z
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_get_origin (LrgTerrain *self,
                        gdouble    *x,
                        gdouble    *y,
                        gdouble    *z);

/**
 * lrg_terrain_get_chunk_mesh_count:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 *
 * Number of meshes generated for a chunk: one per material used by its
 * cells (more if a material exceeds %LRG_TERRAIN_MAX_MESH_VERTICES),
 * ordered by material id. Builds the meshes if needed. Headless.
 *
 * Returns: the mesh count (0 for an unloaded chunk)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_terrain_get_chunk_mesh_count (LrgTerrain *self,
                                  gint        cx,
                                  gint        cz);

/**
 * lrg_terrain_get_chunk_mesh_material:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 * @mesh: mesh index
 *
 * Returns: the mesh's material id (0 when out of range)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_terrain_get_chunk_mesh_material (LrgTerrain *self,
                                     gint        cx,
                                     gint        cz,
                                     guint       mesh);

/**
 * lrg_terrain_get_chunk_mesh_positions:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 * @mesh: mesh index
 * @n_floats: (out): number of floats (3 per vertex)
 *
 * Positions are chunk-local: relative to the chunk corner
 * (cx * chunk_size, 0, cz * chunk_size). Each cell has four vertices of
 * its own (UVs differ per cell) and two triangles.
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): positions
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_terrain_get_chunk_mesh_positions (LrgTerrain *self,
                                      gint        cx,
                                      gint        cz,
                                      guint       mesh,
                                      guint      *n_floats);

/**
 * lrg_terrain_get_chunk_mesh_normals:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 * @mesh: mesh index
 * @n_floats: (out): number of floats (3 per vertex)
 *
 * Unit normals from central differences of the heights (neighbouring
 * chunks are sampled across edges when loaded).
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): normals
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_terrain_get_chunk_mesh_normals (LrgTerrain *self,
                                    gint        cx,
                                    gint        cz,
                                    guint       mesh,
                                    guint      *n_floats);

/**
 * lrg_terrain_get_chunk_mesh_texcoords:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 * @mesh: mesh index
 * @n_floats: (out): number of floats (2 per vertex)
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): texture
 *   coordinates
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_terrain_get_chunk_mesh_texcoords (LrgTerrain *self,
                                      gint        cx,
                                      gint        cz,
                                      guint       mesh,
                                      guint      *n_floats);

/**
 * lrg_terrain_get_chunk_mesh_indices:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 * @mesh: mesh index
 * @n_indices: (out): number of indices
 *
 * Triangles are counter-clockwise seen from above (+Y), i.e. front
 * facing under raylib's default culling.
 *
 * Returns: (transfer none) (array length=n_indices) (nullable): 16-bit
 *   triangle indices
 */
LRG_AVAILABLE_IN_ALL
const guint16 *
lrg_terrain_get_chunk_mesh_indices (LrgTerrain *self,
                                    gint        cx,
                                    gint        cz,
                                    guint       mesh,
                                    guint      *n_indices);

/**
 * lrg_terrain_set_material_texture:
 * @self: an #LrgTerrain
 * @material: material id
 * @texture: (nullable): texture for @material, %NULL to unset
 *
 * The terrain keeps a reference. Materials without a texture draw with
 * raylib's white texture.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_set_material_texture (LrgTerrain *self,
                                  guint       material,
                                  GrlTexture *texture);

/**
 * lrg_terrain_set_tint:
 * @self: an #LrgTerrain
 * @tint: (nullable): colour multiplied into every mesh, %NULL for white
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_set_tint (LrgTerrain     *self,
                      const GrlColor *tint);

/**
 * lrg_terrain_set_shader:
 * @self: an #LrgTerrain
 * @shader: (nullable): shader for every mesh (e.g.
 *   lrg_light_set_get_shader()), %NULL for raylib's default
 *
 * The terrain keeps a reference.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_set_shader (LrgTerrain *self,
                        GrlShader  *shader);

/**
 * lrg_terrain_upload:
 * @self: an #LrgTerrain
 * @error: (nullable): return location for a #GError
 *
 * Builds and uploads the meshes of new or changed chunks. CPU data is
 * kept for inspection and re-upload.
 *
 * Errors: %G_IO_ERROR_NOT_INITIALIZED without a graphics context.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_terrain_upload (LrgTerrain  *self,
                    GError     **error);

/**
 * lrg_terrain_is_uploaded:
 * @self: an #LrgTerrain
 *
 * Returns: %TRUE when every chunk's meshes are on the GPU and current
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_terrain_is_uploaded (LrgTerrain *self);

/**
 * lrg_terrain_unload:
 * @self: an #LrgTerrain
 *
 * Releases every GPU mesh, keeping CPU data (call before closing the
 * window if the terrain outlives it).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_unload (LrgTerrain *self);

/**
 * lrg_terrain_draw:
 * @self: an #LrgTerrain
 *
 * Draws every uploaded chunk, translated by (chunk corner - origin).
 * Must be called inside a 3D mode. Chunks not yet uploaded are skipped.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_draw (LrgTerrain *self);

/**
 * lrg_terrain_draw_chunk:
 * @self: an #LrgTerrain
 * @cx: chunk X coordinate
 * @cz: chunk Z coordinate
 *
 * Draws one uploaded chunk (for callers that cull with
 * lrg_terrain_get_chunk_bounds()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_draw_chunk (LrgTerrain *self,
                        gint        cx,
                        gint        cz);

G_END_DECLS
