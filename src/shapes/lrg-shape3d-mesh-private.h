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
 *
 * The meshes are indexed: each distinct position is stored once and the
 * triangles (the same ones, in the same order) index them, so a corner
 * shared by six triangles is transformed once instead of six times.
 *
 * Inside a shape batch (lrg_shape3d_begin_batch(), see lrg-shape3d.h) an
 * opaque draw is queued instead: its MVP (the very product DrawMesh()
 * would send) and colour join its mesh's instance list, and a flush draws
 * each list with one instanced call. A translucent draw flushes first.
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
 * lrg_shape3d_mesh_acquire_cube:
 *
 * The unit cube DrawCube() draws at the origin (scale it by the size
 * when drawing). Only batched cubes draw it: one cube alone is cheaper
 * through rlgl's immediate batch.
 *
 * Returns: (transfer full) (nullable): a reference on the shared mesh, or
 *   %NULL without a graphics context
 */
LrgShape3DMesh *
lrg_shape3d_mesh_acquire_cube (void);

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
 * @sx: x scale applied before the current rlgl transform (the sphere's
 *   radius, the cube's width; 1 for a cylinder)
 * @sy: y scale
 * @sz: z scale
 *
 * Draws @mesh with raylib's default shader and texture under the current
 * rlgl transform (the shape's rlPushMatrix() translate, rotate, scale),
 * or, inside a shape batch with an opaque @color, queues it.
 *
 * Returns: %FALSE when there is no graphics context or the mesh could
 *   not be made in it (a mesh from a closed context is remade in the
 *   current one); the caller then draws the immediate way
 */
gboolean
lrg_shape3d_mesh_draw (LrgShape3DMesh *mesh,
                       const GrlColor *color,
                       gfloat          sx,
                       gfloat          sy,
                       gfloat          sz);

/*
 * lrg_shape3d_mesh_push_transform:
 * @position: the render position
 * @rotation: the render rotation (radians, XYZ order)
 * @scale: the render scale
 *
 * rlPushMatrix() and the shape transform: translate, rotate, scale (the
 * rotations by zero and a scale of one skipped: they would multiply by
 * the identity). Pop it with rlPopMatrix().
 */
void
lrg_shape3d_mesh_push_transform (const GrlVector3 *position,
                                 const GrlVector3 *rotation,
                                 const GrlVector3 *scale);

/*
 * lrg_shape3d_mesh_batch_begin:
 *
 * Opens (or nests) a shape batch (lrg_shape3d_begin_batch()).
 */
void
lrg_shape3d_mesh_batch_begin (void);

/*
 * lrg_shape3d_mesh_batch_end:
 *
 * Closes a shape batch; the outermost one draws what is queued.
 */
void
lrg_shape3d_mesh_batch_end (void);

/*
 * lrg_shape3d_mesh_batch_flush:
 *
 * Draws what is queued (one instanced call per mesh, in the order the
 * meshes were first queued) and empties the queue.
 */
void
lrg_shape3d_mesh_batch_flush (void);

/*
 * lrg_shape3d_mesh_batching:
 *
 * Returns: %TRUE inside a shape batch that can queue (a context with
 *   GLSL 330, not in stereo)
 */
gboolean
lrg_shape3d_mesh_batching (void);

G_END_DECLS
