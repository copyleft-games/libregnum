/* lrg-shape3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for 3D shapes.
 *
 * LrgShape3D extends LrgShape with 3D-specific properties such as
 * position (GrlVector3) and wireframe rendering mode.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-shape.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHAPE3D (lrg_shape3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgShape3D, lrg_shape3d, LRG, SHAPE3D, LrgShape)

/**
 * LrgShape3DClass:
 * @parent_class: The parent class
 *
 * The class structure for #LrgShape3D.
 *
 * Subclasses inherit position and wireframe properties and should
 * implement the draw() virtual method from #LrgShapeClass.
 */
struct _LrgShape3DClass
{
	LrgShapeClass parent_class;

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_shape3d_get_position:
 * @self: an #LrgShape3D
 *
 * Gets the shape's 3D position.
 *
 * Returns: (transfer none): The position vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_shape3d_get_position (LrgShape3D *self);

/**
 * lrg_shape3d_set_position:
 * @self: an #LrgShape3D
 * @position: (transfer none): the position to set
 *
 * Sets the shape's 3D position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_position (LrgShape3D *self,
                               GrlVector3 *position);

/**
 * lrg_shape3d_set_position_xyz:
 * @self: an #LrgShape3D
 * @x: the X coordinate
 * @y: the Y coordinate
 * @z: the Z coordinate
 *
 * Sets the shape's position using X, Y, Z coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_position_xyz (LrgShape3D *self,
                                   gfloat      x,
                                   gfloat      y,
                                   gfloat      z);

/**
 * lrg_shape3d_get_x:
 * @self: an #LrgShape3D
 *
 * Gets the shape's X position.
 *
 * Returns: The X coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_x (LrgShape3D *self);

/**
 * lrg_shape3d_get_y:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Y position.
 *
 * Returns: The Y coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_y (LrgShape3D *self);

/**
 * lrg_shape3d_get_z:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Z position.
 *
 * Returns: The Z coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_z (LrgShape3D *self);

/**
 * lrg_shape3d_get_wireframe:
 * @self: an #LrgShape3D
 *
 * Gets whether the shape is rendered in wireframe mode.
 *
 * Returns: %TRUE if wireframe mode is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shape3d_get_wireframe (LrgShape3D *self);

/**
 * lrg_shape3d_set_wireframe:
 * @self: an #LrgShape3D
 * @wireframe: whether to enable wireframe mode
 *
 * Sets whether the shape is rendered in wireframe mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_wireframe (LrgShape3D *self,
                                gboolean    wireframe);

/* ==========================================================================
 * Rotation Properties
 * ========================================================================== */

/**
 * lrg_shape3d_get_rotation:
 * @self: an #LrgShape3D
 *
 * Gets the shape's rotation as Euler angles in radians.
 *
 * Returns: (transfer none): The rotation vector (x=pitch, y=yaw, z=roll)
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_shape3d_get_rotation (LrgShape3D *self);

/**
 * lrg_shape3d_set_rotation:
 * @self: an #LrgShape3D
 * @rotation: (transfer none): the rotation to set (Euler angles in radians)
 *
 * Sets the shape's rotation using a vector of Euler angles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_rotation (LrgShape3D *self,
                               GrlVector3 *rotation);

/**
 * lrg_shape3d_set_rotation_xyz:
 * @self: an #LrgShape3D
 * @rx: the X rotation (pitch) in radians
 * @ry: the Y rotation (yaw) in radians
 * @rz: the Z rotation (roll) in radians
 *
 * Sets the shape's rotation using individual Euler angles in radians.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_rotation_xyz (LrgShape3D *self,
                                   gfloat      rx,
                                   gfloat      ry,
                                   gfloat      rz);

/**
 * lrg_shape3d_get_rotation_x:
 * @self: an #LrgShape3D
 *
 * Gets the shape's X rotation (pitch) in radians.
 *
 * Returns: The X rotation in radians
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_rotation_x (LrgShape3D *self);

/**
 * lrg_shape3d_get_rotation_y:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Y rotation (yaw) in radians.
 *
 * Returns: The Y rotation in radians
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_rotation_y (LrgShape3D *self);

/**
 * lrg_shape3d_get_rotation_z:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Z rotation (roll) in radians.
 *
 * Returns: The Z rotation in radians
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_rotation_z (LrgShape3D *self);

/* ==========================================================================
 * Scale Properties
 * ========================================================================== */

/**
 * lrg_shape3d_get_scale:
 * @self: an #LrgShape3D
 *
 * Gets the shape's scale factors.
 *
 * Returns: (transfer none): The scale vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_shape3d_get_scale (LrgShape3D *self);

/**
 * lrg_shape3d_set_scale:
 * @self: an #LrgShape3D
 * @scale: (transfer none): the scale to set
 *
 * Sets the shape's scale using a vector.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_scale (LrgShape3D *self,
                            GrlVector3 *scale);

/**
 * lrg_shape3d_set_scale_xyz:
 * @self: an #LrgShape3D
 * @sx: the X scale factor
 * @sy: the Y scale factor
 * @sz: the Z scale factor
 *
 * Sets the shape's scale using individual factors.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_scale_xyz (LrgShape3D *self,
                                gfloat      sx,
                                gfloat      sy,
                                gfloat      sz);

/**
 * lrg_shape3d_set_scale_uniform:
 * @self: an #LrgShape3D
 * @scale: the uniform scale factor
 *
 * Sets the shape's scale uniformly on all axes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_scale_uniform (LrgShape3D *self,
                                    gfloat      scale);

/**
 * lrg_shape3d_get_scale_x:
 * @self: an #LrgShape3D
 *
 * Gets the shape's X scale factor.
 *
 * Returns: The X scale factor
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_scale_x (LrgShape3D *self);

/**
 * lrg_shape3d_get_scale_y:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Y scale factor.
 *
 * Returns: The Y scale factor
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_scale_y (LrgShape3D *self);

/**
 * lrg_shape3d_get_scale_z:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Z scale factor.
 *
 * Returns: The Z scale factor
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_scale_z (LrgShape3D *self);

/* ==========================================================================
 * Render Interpolation
 * ========================================================================== */

/**
 * lrg_shape3d_get_render_transform:
 * @self: an #LrgShape3D
 * @position: (out caller-allocates) (optional): return location for the
 *   position to draw at
 * @rotation: (out caller-allocates) (optional): return location for the
 *   Euler rotation (radians) to draw with
 * @scale: (out caller-allocates) (optional): return location for the
 *   scale to draw with
 *
 * Gets the transform to draw with: while a fixed-timestep
 * #LrgGameTemplate drives render interpolation and the shape changed in
 * the latest fixed step, a blend of its transform at the start of that
 * step and its current one (position and scale linearly, each Euler angle
 * along its shorter arc); otherwise the current transform. The built-in
 * 3D shapes draw with it. The getters keep returning the true transform.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_get_render_transform (LrgShape3D *self,
                                       GrlVector3 *position,
                                       GrlVector3 *rotation,
                                       GrlVector3 *scale);

/**
 * lrg_shape3d_reset_interpolation:
 * @self: an #LrgShape3D
 *
 * Drops the in-flight blend, so the shape is drawn at its current
 * transform (teleports, respawns); further changes in the current fixed
 * step are not blended either.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_reset_interpolation (LrgShape3D *self);

/**
 * lrg_shape3d_get_interpolate:
 * @self: an #LrgShape3D
 *
 * Gets whether the shape is interpolated for drawing.
 *
 * Returns: the value of #LrgShape3D:interpolate
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shape3d_get_interpolate (LrgShape3D *self);

/**
 * lrg_shape3d_set_interpolate:
 * @self: an #LrgShape3D
 * @interpolate: whether to interpolate the shape for drawing
 *
 * Opts the shape in or out of render interpolation; either way its blend
 * is reset.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_interpolate (LrgShape3D *self,
                                  gboolean    interpolate);

/* ==========================================================================
 * Shape batches
 * ========================================================================== */

/**
 * lrg_shape3d_begin_batch:
 *
 * Opens a shape batch: until the matching lrg_shape3d_end_batch(), every
 * opaque (alpha 255), solid #LrgCube3D, #LrgSphere3D, #LrgCylinder3D and
 * #LrgCone3D drawn is not drawn on the spot but queued with the transform
 * and camera of its draw, and the end draws the queue with one instanced
 * call per shared mesh (all the unit cubes, all the spheres of the same
 * rings and slices, all the cylinders of the same size...): thousands of
 * shapes cost a few draw calls instead of one each.
 *
 * What the queue draws is what each shape would have drawn (the same
 * triangles under the same model-view-projection, the same colour), but
 * later and grouped by mesh, which is invisible while the depth test
 * decides what is in front: draw the batch in a 3D mode with the depth
 * test on, with the render target, viewport and blend state it began
 * with. Wireframes and translucent shapes are not queued: a translucent
 * shape (and any #LrgShape with a translucent colour) first draws the
 * queue, so it blends over everything drawn before it. Before drawing
 * anything translucent that is not a shape, or before changing the
 * render target or depth state inside a batch, call
 * lrg_shape3d_flush_batch(). Coplanar overlapping opaque shapes (equal
 * depth) may resolve the other way round than without the batch.
 *
 * Batches nest (only the outermost end draws). Without a graphics
 * context with GLSL 330, or in stereo rendering, shapes draw one by one
 * as without a batch. The batch's program is built here on first use.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_begin_batch (void);

/**
 * lrg_shape3d_end_batch:
 *
 * Closes the shape batch opened by lrg_shape3d_begin_batch(); closing
 * the outermost one draws what it queued.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_end_batch (void);

/**
 * lrg_shape3d_flush_batch:
 *
 * Draws what the open shape batch has queued so far (the batch stays
 * open). Call it before drawing something translucent that is not a
 * shape, or before changing the render target or depth state inside a
 * batch. Does nothing outside a batch.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_flush_batch (void);

/**
 * lrg_shape3d_is_batching:
 *
 * Returns: %TRUE inside a shape batch that queues (a graphics context
 *   with GLSL 330, not in stereo rendering)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shape3d_is_batching (void);

G_END_DECLS
