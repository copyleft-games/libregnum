/* lrg-mesh-data.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mesh data structure for custom mesh primitives.
 *
 * LrgMeshData stores raw vertex and face data for custom mesh primitives
 * loaded from Blender YAML exports. The data is stored in a format suitable
 * for triangulation and conversion to renderable meshes.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_MESH_DATA (lrg_mesh_data_get_type ())

/**
 * LrgMeshData:
 *
 * Opaque structure containing mesh vertex and face data.
 *
 * The mesh data stores:
 * - Vertices: Flat array of XYZ coordinates
 * - Faces: Variable-length polygon definitions (can be triangles, quads, or n-gons)
 * - Smooth: Whether smooth shading is applied
 *
 * Face data format: [n0, v0, v1, ..., n1, v0, v1, ...]
 * where n is the vertex count for each face, followed by vertex indices.
 */
typedef struct _LrgMeshData LrgMeshData;

LRG_AVAILABLE_IN_ALL
GType lrg_mesh_data_get_type (void) G_GNUC_CONST;

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_mesh_data_new:
 *
 * Creates a new empty #LrgMeshData.
 *
 * Returns: (transfer full): A new #LrgMeshData
 */
LRG_AVAILABLE_IN_ALL
LrgMeshData * lrg_mesh_data_new (void);

/**
 * lrg_mesh_data_copy:
 * @self: an #LrgMeshData
 *
 * Creates a deep copy of the mesh data.
 *
 * Returns: (transfer full): A new #LrgMeshData
 */
LRG_AVAILABLE_IN_ALL
LrgMeshData * lrg_mesh_data_copy (const LrgMeshData *self);

/**
 * lrg_mesh_data_free:
 * @self: an #LrgMeshData
 *
 * Frees the mesh data and all associated memory.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_data_free (LrgMeshData *self);

/* ==========================================================================
 * Vertex Data
 * ========================================================================== */

/**
 * lrg_mesh_data_set_vertices:
 * @self: an #LrgMeshData
 * @vertices: (array): Flat array of vertex coordinates [x0,y0,z0, x1,y1,z1, ...]
 * @n_vertices: Number of vertices (array has n_vertices * 3 floats)
 *
 * Sets the vertex positions. The array is copied.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_data_set_vertices (LrgMeshData  *self,
                                  const gfloat *vertices,
                                  guint         n_vertices);

/**
 * lrg_mesh_data_get_vertices:
 * @self: an #LrgMeshData
 * @n_vertices: (out) (optional): Location for number of vertices
 *
 * Gets the vertex positions as a flat array.
 *
 * Returns: (transfer none) (array) (nullable): The vertex array, or %NULL if empty
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_mesh_data_get_vertices (const LrgMeshData *self,
                                            guint             *n_vertices);

/**
 * lrg_mesh_data_get_n_vertices:
 * @self: an #LrgMeshData
 *
 * Gets the number of vertices.
 *
 * Returns: The vertex count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mesh_data_get_n_vertices (const LrgMeshData *self);

/* ==========================================================================
 * Face Data
 * ========================================================================== */

/**
 * lrg_mesh_data_set_faces:
 * @self: an #LrgMeshData
 * @faces: (array length=total_indices): Face data in format [n0, v0, v1, ..., n1, v0, v1, ...]
 * @n_faces: Number of faces
 * @total_indices: Total length of the faces array
 *
 * Sets the face data. The array is copied.
 * Each face starts with a vertex count followed by that many vertex indices.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_data_set_faces (LrgMeshData *self,
                               const gint  *faces,
                               guint        n_faces,
                               guint        total_indices);

/**
 * lrg_mesh_data_get_faces:
 * @self: an #LrgMeshData
 * @n_faces: (out) (optional): Location for number of faces
 * @total_indices: (out) (optional): Location for total array length
 *
 * Gets the face data.
 *
 * Returns: (transfer none) (array length=total_indices) (nullable): The face array, or %NULL if empty
 */
LRG_AVAILABLE_IN_ALL
const gint * lrg_mesh_data_get_faces (const LrgMeshData *self,
                                       guint             *n_faces,
                                       guint             *total_indices);

/**
 * lrg_mesh_data_get_n_faces:
 * @self: an #LrgMeshData
 *
 * Gets the number of faces.
 *
 * Returns: The face count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mesh_data_get_n_faces (const LrgMeshData *self);

/* ==========================================================================
 * Shading
 * ========================================================================== */

/**
 * lrg_mesh_data_set_smooth:
 * @self: an #LrgMeshData
 * @smooth: Whether to use smooth shading
 *
 * Sets the smooth shading flag.
 * When %TRUE, vertex normals are averaged across faces for smooth appearance.
 * When %FALSE, face normals are used for flat shading.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_data_set_smooth (LrgMeshData *self,
                                gboolean     smooth);

/**
 * lrg_mesh_data_get_smooth:
 * @self: an #LrgMeshData
 *
 * Gets the smooth shading flag.
 *
 * Returns: %TRUE if smooth shading is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_data_get_smooth (const LrgMeshData *self);

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_mesh_data_is_empty:
 * @self: an #LrgMeshData
 *
 * Checks if the mesh data is empty (no vertices or faces).
 *
 * Returns: %TRUE if the mesh has no geometry
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_data_is_empty (const LrgMeshData *self);

/* ==========================================================================
 * Face Winding
 * ========================================================================== */

/**
 * lrg_mesh_data_set_reverse_winding:
 * @self: an #LrgMeshData
 * @reverse: Whether face winding should be reversed during triangulation
 *
 * Sets the reverse winding flag.
 * When %TRUE, triangulation should swap the last two indices of each
 * triangle to correct for mirrored geometry (e.g., Blender Z-up to Y-up).
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_data_set_reverse_winding (LrgMeshData *self,
                                         gboolean     reverse);

/**
 * lrg_mesh_data_get_reverse_winding:
 * @self: an #LrgMeshData
 *
 * Gets the reverse winding flag.
 *
 * Returns: %TRUE if face winding should be reversed during triangulation
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_data_get_reverse_winding (const LrgMeshData *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgMeshData, lrg_mesh_data_free)

G_END_DECLS
