/* lrg-mesh-data.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of LrgMeshData boxed type for mesh vertex/face storage.
 */

#include "config.h"

#include "lrg-mesh-data.h"

struct _LrgMeshData
{
    gfloat   *vertices;          /* Flat array: [x0,y0,z0, x1,y1,z1, ...] */
    guint     n_vertices;        /* Number of vertices */
    gint     *faces;             /* [n0, v0, v1, ..., n1, v0, v1, ...] */
    guint     n_faces;           /* Number of faces */
    guint     total_face_indices; /* Total length of faces array */
    gboolean  smooth;            /* Smooth shading flag */
    gboolean  reverse_winding;   /* Reverse winding during triangulation */
};

G_DEFINE_BOXED_TYPE (LrgMeshData, lrg_mesh_data,
                     lrg_mesh_data_copy, lrg_mesh_data_free)

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
LrgMeshData *
lrg_mesh_data_new (void)
{
    LrgMeshData *self;

    self = g_new0 (LrgMeshData, 1);
    self->vertices = NULL;
    self->n_vertices = 0;
    self->faces = NULL;
    self->n_faces = 0;
    self->total_face_indices = 0;
    self->smooth = FALSE;
    self->reverse_winding = FALSE;

    return self;
}

/**
 * lrg_mesh_data_copy:
 * @self: an #LrgMeshData
 *
 * Creates a deep copy of the mesh data.
 *
 * Returns: (transfer full): A new #LrgMeshData
 */
LrgMeshData *
lrg_mesh_data_copy (const LrgMeshData *self)
{
    LrgMeshData *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = lrg_mesh_data_new ();

    if (self->vertices != NULL && self->n_vertices > 0)
    {
        copy->vertices = g_memdup2 (self->vertices,
                                     self->n_vertices * 3 * sizeof (gfloat));
        copy->n_vertices = self->n_vertices;
    }

    if (self->faces != NULL && self->total_face_indices > 0)
    {
        copy->faces = g_memdup2 (self->faces,
                                  self->total_face_indices * sizeof (gint));
        copy->n_faces = self->n_faces;
        copy->total_face_indices = self->total_face_indices;
    }

    copy->smooth = self->smooth;
    copy->reverse_winding = self->reverse_winding;

    return copy;
}

/**
 * lrg_mesh_data_free:
 * @self: an #LrgMeshData
 *
 * Frees the mesh data and all associated memory.
 */
void
lrg_mesh_data_free (LrgMeshData *self)
{
    if (self == NULL)
        return;

    g_clear_pointer (&self->vertices, g_free);
    g_clear_pointer (&self->faces, g_free);
    g_free (self);
}

/* ==========================================================================
 * Vertex Data
 * ========================================================================== */

/**
 * lrg_mesh_data_set_vertices:
 * @self: an #LrgMeshData
 * @vertices: (array length=n_floats): Flat array of vertex coordinates
 * @n_vertices: Number of vertices (array has n_vertices * 3 floats)
 *
 * Sets the vertex positions. The array is copied.
 */
void
lrg_mesh_data_set_vertices (LrgMeshData  *self,
                             const gfloat *vertices,
                             guint         n_vertices)
{
    g_return_if_fail (self != NULL);

    g_clear_pointer (&self->vertices, g_free);
    self->n_vertices = 0;

    if (vertices != NULL && n_vertices > 0)
    {
        self->vertices = g_memdup2 (vertices, n_vertices * 3 * sizeof (gfloat));
        self->n_vertices = n_vertices;
    }
}

/**
 * lrg_mesh_data_get_vertices:
 * @self: an #LrgMeshData
 * @n_vertices: (out) (optional): Location for number of vertices
 *
 * Gets the vertex positions as a flat array.
 *
 * Returns: (transfer none) (nullable): The vertex array, or %NULL if empty
 */
const gfloat *
lrg_mesh_data_get_vertices (const LrgMeshData *self,
                             guint             *n_vertices)
{
    g_return_val_if_fail (self != NULL, NULL);

    if (n_vertices != NULL)
        *n_vertices = self->n_vertices;

    return self->vertices;
}

/**
 * lrg_mesh_data_get_n_vertices:
 * @self: an #LrgMeshData
 *
 * Gets the number of vertices.
 *
 * Returns: The vertex count
 */
guint
lrg_mesh_data_get_n_vertices (const LrgMeshData *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->n_vertices;
}

/* ==========================================================================
 * Face Data
 * ========================================================================== */

/**
 * lrg_mesh_data_set_faces:
 * @self: an #LrgMeshData
 * @faces: (array length=total_indices): Face data
 * @n_faces: Number of faces
 * @total_indices: Total length of the faces array
 *
 * Sets the face data. The array is copied.
 */
void
lrg_mesh_data_set_faces (LrgMeshData *self,
                          const gint  *faces,
                          guint        n_faces,
                          guint        total_indices)
{
    g_return_if_fail (self != NULL);

    g_clear_pointer (&self->faces, g_free);
    self->n_faces = 0;
    self->total_face_indices = 0;

    if (faces != NULL && total_indices > 0)
    {
        self->faces = g_memdup2 (faces, total_indices * sizeof (gint));
        self->n_faces = n_faces;
        self->total_face_indices = total_indices;
    }
}

/**
 * lrg_mesh_data_get_faces:
 * @self: an #LrgMeshData
 * @n_faces: (out) (optional): Location for number of faces
 * @total_indices: (out) (optional): Location for total array length
 *
 * Gets the face data.
 *
 * Returns: (transfer none) (nullable): The face array, or %NULL if empty
 */
const gint *
lrg_mesh_data_get_faces (const LrgMeshData *self,
                          guint             *n_faces,
                          guint             *total_indices)
{
    g_return_val_if_fail (self != NULL, NULL);

    if (n_faces != NULL)
        *n_faces = self->n_faces;

    if (total_indices != NULL)
        *total_indices = self->total_face_indices;

    return self->faces;
}

/**
 * lrg_mesh_data_get_n_faces:
 * @self: an #LrgMeshData
 *
 * Gets the number of faces.
 *
 * Returns: The face count
 */
guint
lrg_mesh_data_get_n_faces (const LrgMeshData *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->n_faces;
}

/* ==========================================================================
 * Shading
 * ========================================================================== */

/**
 * lrg_mesh_data_set_smooth:
 * @self: an #LrgMeshData
 * @smooth: Whether to use smooth shading
 *
 * Sets the smooth shading flag.
 */
void
lrg_mesh_data_set_smooth (LrgMeshData *self,
                           gboolean     smooth)
{
    g_return_if_fail (self != NULL);

    self->smooth = smooth;
}

/**
 * lrg_mesh_data_get_smooth:
 * @self: an #LrgMeshData
 *
 * Gets the smooth shading flag.
 *
 * Returns: %TRUE if smooth shading is enabled
 */
gboolean
lrg_mesh_data_get_smooth (const LrgMeshData *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->smooth;
}

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
gboolean
lrg_mesh_data_is_empty (const LrgMeshData *self)
{
    g_return_val_if_fail (self != NULL, TRUE);

    return (self->n_vertices == 0 || self->n_faces == 0);
}

/* ==========================================================================
 * Face Winding
 * ========================================================================== */

/**
 * lrg_mesh_data_set_reverse_winding:
 * @self: an #LrgMeshData
 * @reverse: Whether face winding should be reversed
 *
 * Sets the reverse winding flag.
 */
void
lrg_mesh_data_set_reverse_winding (LrgMeshData *self,
                                    gboolean     reverse)
{
    g_return_if_fail (self != NULL);

    self->reverse_winding = reverse;
}

/**
 * lrg_mesh_data_get_reverse_winding:
 * @self: an #LrgMeshData
 *
 * Gets the reverse winding flag.
 *
 * Returns: %TRUE if face winding should be reversed during triangulation
 */
gboolean
lrg_mesh_data_get_reverse_winding (const LrgMeshData *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->reverse_winding;
}
