/* lrg-shape3d-mesh-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared GPU meshes for the immediate-mode 3D shapes. Not installed, not
 * introspected.
 *
 * raylib's DrawSphereEx() and DrawCylinder() tessellate on every call and
 * push each vertex through rlgl's immediate batch, which transforms it on
 * the CPU while a matrix is pushed. A shape drawn every frame redoes all
 * of that every frame. These helpers build the very same triangle list
 * once per parameter set (the same vertices in the same order, computed
 * with the same float expressions, in the shape's local space), upload it
 * once, share it between every shape with those parameters, and draw it
 * with DrawMesh() under the transform the shape pushed, so the GPU does
 * the transform.
 *
 * The colour reaches the default shader as colDiffuse instead of a vertex
 * colour, which the default shader multiplies the same way. Drawing flushes
 * rlgl's batch first so a mesh lands after everything queued before it, as
 * the immediate triangles did.
 */

#pragma once

#include <glib.h>
#include <graylib.h>

G_BEGIN_DECLS

typedef struct _LrgShape3DMesh LrgShape3DMesh;

/*
 * lrg_shape3d_mesh_acquire_sphere:
 * @rings: DrawSphereEx()'s rings
 * @slices: DrawSphereEx()'s slices
 *
 * The unit sphere DrawSphereEx() draws at the origin (scale it by the
 * radius when drawing).
 *
 * Returns: (transfer full) (nullable): a reference on the shared mesh, or
 *   %NULL without a graphics context
 */
LrgShape3DMesh *
lrg_shape3d_mesh_acquire_sphere (gint rings,
                                 gint slices);

/*
 * lrg_shape3d_mesh_acquire_cylinder:
 * @radius_top: DrawCylinder()'s top radius (0 or less: a cone)
 * @radius_bottom: DrawCylinder()'s bottom radius
 * @height: DrawCylinder()'s height
 * @sides: DrawCylinder()'s sides (fewer than 3 draw 3)
 *
 * The triangles DrawCylinder() draws at the origin.
 *
 * Returns: (transfer full) (nullable): a reference on the shared mesh, or
 *   %NULL without a graphics context
 */
LrgShape3DMesh *
lrg_shape3d_mesh_acquire_cylinder (gfloat radius_top,
                                   gfloat radius_bottom,
                                   gfloat height,
                                   gint   sides);

/*
 * lrg_shape3d_mesh_release:
 * @mesh: (transfer full) (nullable): a reference from an acquire call
 *
 * Drops the reference; the last one unloads the mesh.
 */
void
lrg_shape3d_mesh_release (LrgShape3DMesh *mesh);

/*
 * lrg_shape3d_mesh_prepare:
 * @mesh: a shared mesh
 *
 * Makes sure @mesh's buffers live in the current context (re-uploading
 * them after the window was recreated), as its draw would. Used to
 * prewarm the shapes.
 *
 * Returns: %FALSE without a window or when the upload failed
 */
gboolean
lrg_shape3d_mesh_prepare (LrgShape3DMesh *mesh);

/*
 * lrg_shape3d_mesh_draw:
 * @mesh: a mesh
 * @color: the shape's colour
 * @scale: uniform scale applied before the current rlgl transform (the
 *   sphere's radius; 1 for a cylinder)
 *
 * Draws @mesh with raylib's default shader and texture under the current
 * rlgl transform (the shape's rlPushMatrix() translate, rotate, scale).
 *
 * Returns: %FALSE when there is no graphics context or the mesh could
 *   not be made in it (a mesh from a closed context is remade in the
 *   current one); the caller then draws the immediate way
 */
gboolean
lrg_shape3d_mesh_draw (LrgShape3DMesh *mesh,
                       const GrlColor *color,
                       gfloat          scale);

G_END_DECLS
