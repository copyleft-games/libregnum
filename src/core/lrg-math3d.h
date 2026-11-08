/* lrg-math3d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared, headless 3D math for the runtime and the editor: Euler angles,
 * quaternions, 4x4 matrices, TRS compose/decompose, point/direction
 * transforms, look-at and projection matrices.
 *
 * Conventions (identical to raylib / graylib and to the 0.8 editor math):
 *  - Matrices are 4x4, column-major, stored as 16 floats (m[col * 4 + row]).
 *    This is the memory layout of a raylib Matrix read as m0..m15.
 *  - Points are column vectors: world = M * local.
 *  - Euler rotations are radians applied X, then Y, then Z
 *    (R = Rz * Ry * Rx), like raylib's MatrixRotateXYZ and the authored
 *    "rotation" of an #LrgNode.
 *  - Quaternions are stored (x, y, z, w) like raylib's Quaternion.
 *  - TRS matrices are T * R * S; scale is the length of each basis column
 *    and a mirrored basis is reported as a negative X scale.
 *  - Y is up; a camera looks down its local -Z.
 *
 * Every function is a plain function over float arrays: no GObject, no GL
 * context, no allocation.  Output arrays may alias inputs where noted.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Matrices
 * ========================================================================== */

/**
 * lrg_math3d_identity:
 * @out: (out caller-allocates) (array fixed-size=16): destination matrix
 *
 * Writes the 4x4 identity matrix.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_identity (gfloat *out);

/**
 * lrg_math3d_multiply:
 * @a: (array fixed-size=16): left matrix
 * @b: (array fixed-size=16): right matrix
 * @out: (out caller-allocates) (array fixed-size=16): @a * @b (may alias
 *   @a or @b)
 *
 * Multiplies two column-major matrices.  Applying the result to a point is
 * the same as applying @b first, then @a.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_multiply (const gfloat *a,
                          const gfloat *b,
                          gfloat       *out);

/**
 * lrg_math3d_invert:
 * @m: (array fixed-size=16): matrix to invert
 * @out: (out caller-allocates) (array fixed-size=16): inverse (may alias @m)
 *
 * Inverts a general 4x4 matrix by cofactor expansion.
 *
 * Returns: %FALSE if @m is singular (@out is then the identity)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_math3d_invert (const gfloat *m,
                            gfloat       *out);

/**
 * lrg_math3d_transpose:
 * @m: (array fixed-size=16): matrix
 * @out: (out caller-allocates) (array fixed-size=16): transpose (may alias @m)
 *
 * Transposes a 4x4 matrix (converts between column- and row-major).
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_transpose (const gfloat *m,
                           gfloat       *out);

/**
 * lrg_math3d_transform_point:
 * @m: (array fixed-size=16): matrix
 * @p: (array fixed-size=3): point
 * @out: (out caller-allocates) (array fixed-size=3): M * p with w = 1,
 *   followed by the perspective divide when w is not 0 or 1 (may alias @p)
 *
 * Transforms a point.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_transform_point (const gfloat *m,
                                 const gfloat *p,
                                 gfloat       *out);

/**
 * lrg_math3d_transform_direction:
 * @m: (array fixed-size=16): matrix
 * @d: (array fixed-size=3): direction
 * @out: (out caller-allocates) (array fixed-size=3): M * d with w = 0
 *   (translation ignored; may alias @d)
 *
 * Transforms a direction vector.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_transform_direction (const gfloat *m,
                                     const gfloat *d,
                                     gfloat       *out);

/* ==========================================================================
 * TRS compose / decompose
 * ========================================================================== */

/**
 * lrg_math3d_compose:
 * @location: (array fixed-size=3): translation
 * @rotation: (array fixed-size=3): Euler XYZ rotation in radians
 * @scale: (array fixed-size=3): scale
 * @out: (out caller-allocates) (array fixed-size=16): destination matrix
 *
 * Builds T * Rz * Ry * Rx * S, the local matrix of an authored node.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_compose (const gfloat *location,
                         const gfloat *rotation,
                         const gfloat *scale,
                         gfloat       *out);

/**
 * lrg_math3d_decompose:
 * @m: (array fixed-size=16): an affine matrix without shear
 * @location: (out caller-allocates) (array fixed-size=3): translation
 * @rotation: (out caller-allocates) (array fixed-size=3): Euler XYZ radians
 * @scale: (out caller-allocates) (array fixed-size=3): scale (a mirrored
 *   matrix yields a negative X scale)
 *
 * Inverse of lrg_math3d_compose() for matrices it can produce.  At gimbal
 * lock (Y rotation of +-90 degrees) the Z angle is folded into X and
 * reported as 0.
 *
 * Returns: %FALSE if the matrix is degenerate (a zero scale axis)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_math3d_decompose (const gfloat *m,
                               gfloat       *location,
                               gfloat       *rotation,
                               gfloat       *scale);

/**
 * lrg_math3d_compose_quat:
 * @location: (array fixed-size=3): translation
 * @rotation: (array fixed-size=4): unit quaternion (x, y, z, w)
 * @scale: (array fixed-size=3): scale
 * @out: (out caller-allocates) (array fixed-size=16): destination matrix
 *
 * Builds T * R(q) * S.  @rotation is used as given; normalise it first
 * with lrg_math3d_quat_normalize() if it may have drifted.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_compose_quat (const gfloat *location,
                              const gfloat *rotation,
                              const gfloat *scale,
                              gfloat       *out);

/**
 * lrg_math3d_decompose_quat:
 * @m: (array fixed-size=16): an affine matrix without shear
 * @location: (out caller-allocates) (array fixed-size=3): translation
 * @rotation: (out caller-allocates) (array fixed-size=4): unit quaternion
 *   (x, y, z, w) with w >= 0
 * @scale: (out caller-allocates) (array fixed-size=3): scale (a mirrored
 *   matrix yields a negative X scale)
 *
 * Splits a TRS matrix like raylib 6.2's MatrixDecompose: scale is the
 * length of each basis column, the rotation comes from the normalised
 * basis.  Unlike lrg_math3d_decompose() there is no gimbal special case.
 *
 * Returns: %FALSE if the matrix is degenerate (a zero scale axis; the
 *   rotation is then the identity)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_math3d_decompose_quat (const gfloat *m,
                                    gfloat       *location,
                                    gfloat       *rotation,
                                    gfloat       *scale);

/* ==========================================================================
 * Euler <-> quaternion <-> matrix
 * ========================================================================== */

/**
 * lrg_math3d_euler_to_quat:
 * @euler: (array fixed-size=3): Euler XYZ rotation in radians
 * @out: (out caller-allocates) (array fixed-size=4): unit quaternion
 *   (x, y, z, w)
 *
 * Converts an Euler rotation (R = Rz * Ry * Rx) to the quaternion
 * qz * qy * qx.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_euler_to_quat (const gfloat *euler,
                               gfloat       *out);

/**
 * lrg_math3d_quat_to_euler:
 * @q: (array fixed-size=4): quaternion (x, y, z, w); need not be unit
 * @out: (out caller-allocates) (array fixed-size=3): Euler XYZ radians
 *
 * Converts a quaternion to Euler angles with the same branch choices and
 * gimbal handling as lrg_math3d_decompose().
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_quat_to_euler (const gfloat *q,
                               gfloat       *out);

/**
 * lrg_math3d_quat_to_matrix:
 * @q: (array fixed-size=4): unit quaternion (x, y, z, w)
 * @out: (out caller-allocates) (array fixed-size=16): rotation matrix
 *
 * Writes the pure rotation matrix of @q.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_quat_to_matrix (const gfloat *q,
                                gfloat       *out);

/**
 * lrg_math3d_matrix_to_quat:
 * @m: (array fixed-size=16): a matrix whose upper 3x3 is a rotation
 *   (orthonormal, determinant +1)
 * @out: (out caller-allocates) (array fixed-size=4): unit quaternion
 *   (x, y, z, w) with w >= 0
 *
 * Extracts the rotation of an orthonormal matrix (Shepperd's method).  Use
 * lrg_math3d_decompose_quat() for matrices that carry scale.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_matrix_to_quat (const gfloat *m,
                                gfloat       *out);

/* ==========================================================================
 * Quaternions
 * ========================================================================== */

/**
 * lrg_math3d_quat_identity:
 * @out: (out caller-allocates) (array fixed-size=4): (0, 0, 0, 1)
 *
 * Writes the identity rotation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_quat_identity (gfloat *out);

/**
 * lrg_math3d_quat_from_axis_angle:
 * @axis: (array fixed-size=3): rotation axis (normalised internally)
 * @angle: angle in radians (right-handed)
 * @out: (out caller-allocates) (array fixed-size=4): unit quaternion
 *
 * Builds a rotation about an axis.  A zero axis yields the identity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_quat_from_axis_angle (const gfloat *axis,
                                      gfloat        angle,
                                      gfloat       *out);

/**
 * lrg_math3d_quat_multiply:
 * @a: (array fixed-size=4): left quaternion
 * @b: (array fixed-size=4): right quaternion
 * @out: (out caller-allocates) (array fixed-size=4): Hamilton product
 *   @a * @b (may alias @a or @b)
 *
 * Composes rotations: rotating by the result equals rotating by @b first,
 * then by @a (the same order as lrg_math3d_multiply()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_quat_multiply (const gfloat *a,
                               const gfloat *b,
                               gfloat       *out);

/**
 * lrg_math3d_quat_normalize:
 * @q: (array fixed-size=4): quaternion
 * @out: (out caller-allocates) (array fixed-size=4): unit quaternion (may
 *   alias @q)
 *
 * Normalises a quaternion; a zero quaternion becomes the identity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_quat_normalize (const gfloat *q,
                                gfloat       *out);

/**
 * lrg_math3d_quat_invert:
 * @q: (array fixed-size=4): quaternion
 * @out: (out caller-allocates) (array fixed-size=4): inverse (conjugate
 *   divided by the squared length; may alias @q)
 *
 * Inverts a quaternion; a zero quaternion becomes the identity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_quat_invert (const gfloat *q,
                             gfloat       *out);

/**
 * lrg_math3d_quat_rotate_vector:
 * @q: (array fixed-size=4): unit quaternion
 * @v: (array fixed-size=3): vector
 * @out: (out caller-allocates) (array fixed-size=3): rotated vector (may
 *   alias @v)
 *
 * Rotates a vector by a quaternion (q * v * q^-1).
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_quat_rotate_vector (const gfloat *q,
                                    const gfloat *v,
                                    gfloat       *out);

/**
 * lrg_math3d_quat_slerp:
 * @a: (array fixed-size=4): start rotation (unit)
 * @b: (array fixed-size=4): end rotation (unit)
 * @t: blend factor, 0 gives @a and 1 gives @b
 * @out: (out caller-allocates) (array fixed-size=4): interpolated unit
 *   quaternion (may alias @a or @b)
 *
 * Spherical linear interpolation along the shorter arc; falls back to a
 * normalised lerp when the rotations are nearly equal.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_quat_slerp (const gfloat *a,
                            const gfloat *b,
                            gfloat        t,
                            gfloat       *out);

/* ==========================================================================
 * Cameras
 * ========================================================================== */

/**
 * lrg_math3d_look_at:
 * @eye: (array fixed-size=3): camera position
 * @target: (array fixed-size=3): point looked at
 * @up: (array fixed-size=3): approximate up direction
 * @out: (out caller-allocates) (array fixed-size=16): view matrix (world to
 *   camera; the camera looks down its -Z)
 *
 * Builds a right-handed view matrix like raylib's MatrixLookAt.  Degenerate
 * inputs are handled the way the editor viewport needs: when @eye equals
 * @target the camera looks down -Z, a zero @up means +Y, and when the view
 * direction is parallel to @up the world -Z (looking down) or +Z (looking
 * up) is used as screen-up, so a top view is "north up".
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_look_at (const gfloat *eye,
                         const gfloat *target,
                         const gfloat *up,
                         gfloat       *out);

/**
 * lrg_math3d_perspective:
 * @fovy: vertical field of view in radians
 * @aspect: width / height
 * @near_plane: near clip distance (> 0)
 * @far_plane: far clip distance (> @near_plane)
 * @out: (out caller-allocates) (array fixed-size=16): projection matrix
 *
 * Builds an OpenGL-style perspective projection (clip z in [-1, 1]), the
 * same matrix as raylib's MatrixPerspective.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_perspective (gfloat  fovy,
                             gfloat  aspect,
                             gfloat  near_plane,
                             gfloat  far_plane,
                             gfloat *out);

/**
 * lrg_math3d_ortho:
 * @left: left plane
 * @right: right plane
 * @bottom: bottom plane
 * @top: top plane
 * @near_plane: near plane
 * @far_plane: far plane
 * @out: (out caller-allocates) (array fixed-size=16): projection matrix
 *
 * Builds an OpenGL-style orthographic projection (clip z in [-1, 1]), the
 * same matrix as raylib's MatrixOrtho.  Symmetric volumes produce exact
 * (positive) zeros in the translation column.
 */
LRG_AVAILABLE_IN_ALL
void lrg_math3d_ortho (gfloat  left,
                       gfloat  right,
                       gfloat  bottom,
                       gfloat  top,
                       gfloat  near_plane,
                       gfloat  far_plane,
                       gfloat *out);

G_END_DECLS
