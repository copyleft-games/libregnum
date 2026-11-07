/* lrg-math3d.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared headless 3D math.  See lrg-math3d.h for the conventions.
 *
 * The matrix, compose/decompose and camera code moved here from the 0.8
 * editor (src/editor/lrg-editor-math.c) in 0.9.0.  The expressions are kept
 * exactly as they were (same operand order, same temporaries) so that the
 * lrg_editor_math_* wrappers produce bit-identical results; the build uses
 * -ffp-contract=off, so no fused multiply-adds change the rounding.
 * tests/test-math3d.c checks this against a copy of the 0.8 code.
 */

#include "config.h"

#include "lrg-math3d.h"
#include <math.h>
#include <string.h>

/* Element (row, col) of a column-major 4x4 matrix. */
#define M(m, row, col) ((m)[(col) * 4 + (row)])

/* Below this a length or determinant is treated as zero. */
#define LRG_MATH3D_EPSILON_LEN  (1e-12f)
#define LRG_MATH3D_EPSILON_DET  (1e-20f)

/* |sin(pitch)| above which the Euler extraction is at gimbal lock. */
#define LRG_MATH3D_GIMBAL       (0.9999f)

/* Below this |cos(angle)| distance slerp falls back to a normalised lerp. */
#define LRG_MATH3D_SLERP_LINEAR (0.9995f)

/* ==========================================================================
 * Small vector helpers
 * ========================================================================== */

static inline void
v3_set (gfloat *o, gfloat x, gfloat y, gfloat z)
{
	o[0] = x; o[1] = y; o[2] = z;
}

static inline void
v3_sub (const gfloat *a, const gfloat *b, gfloat *o)
{
	o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2];
}

static inline gfloat
v3_dot (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
v3_cross (const gfloat *a, const gfloat *b, gfloat *o)
{
	gfloat x = a[1] * b[2] - a[2] * b[1];
	gfloat y = a[2] * b[0] - a[0] * b[2];
	gfloat z = a[0] * b[1] - a[1] * b[0];

	o[0] = x; o[1] = y; o[2] = z;
}

static inline gfloat
v3_length (const gfloat *a)
{
	return sqrtf (v3_dot (a, a));
}

static inline gboolean
v3_normalize (gfloat *a)
{
	gfloat len = v3_length (a);

	if (len < LRG_MATH3D_EPSILON_LEN)
		return FALSE;
	a[0] /= len; a[1] /= len; a[2] /= len;
	return TRUE;
}

/* ==========================================================================
 * Rotation-matrix helpers (3x3, r[row][col])
 * ========================================================================== */

/*
 * euler_from_rotation:
 * @r: an orthonormal rotation, r[row][col]
 * @rotation: (out): Euler XYZ radians
 *
 * Inverts R = Rz * Ry * Rx.  R[2][0] = -sin(y); away from gimbal lock
 * x = atan2 (R21, R22) and z = atan2 (R10, R00).  At gimbal lock only
 * x +- z is observable, so z is folded into x and reported as 0.  This is
 * the exact extraction the 0.8 editor used.
 */
static void
euler_from_rotation (gfloat  r[3][3],
                     gfloat *rotation)
{
	if (r[2][0] < LRG_MATH3D_GIMBAL && r[2][0] > -LRG_MATH3D_GIMBAL)
	{
		rotation[1] = asinf (-r[2][0]);
		rotation[0] = atan2f (r[2][1], r[2][2]);
		rotation[2] = atan2f (r[1][0], r[0][0]);
	}
	else
	{
		/* Gimbal lock: fold Z into X. */
		rotation[1] = (r[2][0] <= -LRG_MATH3D_GIMBAL) ? (gfloat) G_PI_2 : (gfloat) -G_PI_2;
		rotation[0] = atan2f (-r[1][2], r[1][1]);
		rotation[2] = 0.0f;
	}
}

/*
 * quat_from_rotation:
 * @r: an orthonormal rotation, r[row][col]
 * @q: (out): unit quaternion (x, y, z, w), w >= 0
 *
 * Shepperd's method: pick the largest of the four diagonal combinations as
 * the pivot to avoid dividing by a small number, then normalise and force
 * w >= 0 so equal rotations give equal quaternions.
 */
static void
quat_from_rotation (gfloat  r[3][3],
                    gfloat *q)
{
	gfloat trace;
	gfloat s;
	gfloat len;

	trace = r[0][0] + r[1][1] + r[2][2];
	if (trace > 0.0f)
	{
		s = sqrtf (trace + 1.0f) * 2.0f;          /* s = 4w */
		q[3] = 0.25f * s;
		q[0] = (r[2][1] - r[1][2]) / s;
		q[1] = (r[0][2] - r[2][0]) / s;
		q[2] = (r[1][0] - r[0][1]) / s;
	}
	else if (r[0][0] > r[1][1] && r[0][0] > r[2][2])
	{
		s = sqrtf (1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;   /* 4x */
		q[3] = (r[2][1] - r[1][2]) / s;
		q[0] = 0.25f * s;
		q[1] = (r[0][1] + r[1][0]) / s;
		q[2] = (r[0][2] + r[2][0]) / s;
	}
	else if (r[1][1] > r[2][2])
	{
		s = sqrtf (1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;   /* 4y */
		q[3] = (r[0][2] - r[2][0]) / s;
		q[0] = (r[0][1] + r[1][0]) / s;
		q[1] = 0.25f * s;
		q[2] = (r[1][2] + r[2][1]) / s;
	}
	else
	{
		s = sqrtf (1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;   /* 4z */
		q[3] = (r[1][0] - r[0][1]) / s;
		q[0] = (r[0][2] + r[2][0]) / s;
		q[1] = (r[1][2] + r[2][1]) / s;
		q[2] = 0.25f * s;
	}

	len = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (q[3] < 0.0f)
		len = -len;
	q[0] /= len; q[1] /= len; q[2] /= len; q[3] /= len;
}

/*
 * rotation_from_quat:
 * @q: unit quaternion (x, y, z, w)
 * @r: (out): r[row][col]
 *
 * The standard unit-quaternion rotation matrix.
 */
static void
rotation_from_quat (const gfloat *q,
                    gfloat        r[3][3])
{
	gfloat x = q[0], y = q[1], z = q[2], w = q[3];
	gfloat xx = x * x, yy = y * y, zz = z * z;
	gfloat xy = x * y, xz = x * z, yz = y * z;
	gfloat wx = w * x, wy = w * y, wz = w * z;

	r[0][0] = 1.0f - 2.0f * (yy + zz);
	r[0][1] = 2.0f * (xy - wz);
	r[0][2] = 2.0f * (xz + wy);
	r[1][0] = 2.0f * (xy + wz);
	r[1][1] = 1.0f - 2.0f * (xx + zz);
	r[1][2] = 2.0f * (yz - wx);
	r[2][0] = 2.0f * (xz - wy);
	r[2][1] = 2.0f * (yz + wx);
	r[2][2] = 1.0f - 2.0f * (xx + yy);
}

/*
 * split_scale:
 * @m: a TRS matrix
 * @location, @scale: (out)
 * @r: (out): the normalised basis r[row][col]
 *
 * Shared front half of both decompose functions: translation column, scale
 * as basis-column lengths (negated X when the basis is mirrored) and the
 * rotation basis.  Returns %FALSE on a zero-length axis (r untouched).
 */
static gboolean
split_scale (const gfloat *m,
             gfloat       *location,
             gfloat       *scale,
             gfloat        r[3][3])
{
	gfloat det;
	gint   row, col;

	location[0] = M (m, 0, 3);
	location[1] = M (m, 1, 3);
	location[2] = M (m, 2, 3);

	/* Scale is the length of each basis column. */
	for (col = 0; col < 3; col++)
	{
		gfloat c[3] = { M (m, 0, col), M (m, 1, col), M (m, 2, col) };

		scale[col] = v3_length (c);
		if (scale[col] < LRG_MATH3D_EPSILON_LEN)
			return FALSE;
	}

	/* A mirrored basis is represented as a negative X scale. */
	det = M (m, 0, 0) * (M (m, 1, 1) * M (m, 2, 2) - M (m, 2, 1) * M (m, 1, 2)) -
	      M (m, 0, 1) * (M (m, 1, 0) * M (m, 2, 2) - M (m, 2, 0) * M (m, 1, 2)) +
	      M (m, 0, 2) * (M (m, 1, 0) * M (m, 2, 1) - M (m, 2, 0) * M (m, 1, 1));
	if (det < 0.0f)
		scale[0] = -scale[0];

	for (col = 0; col < 3; col++)
		for (row = 0; row < 3; row++)
			r[row][col] = M (m, row, col) / scale[col];

	return TRUE;
}

/*
 * write_trs:
 * @location, @scale: translation and scale
 * @r: rotation r[row][col]
 * @out: (out): T * R * S
 */
static void
write_trs (const gfloat *location,
           gfloat        r[3][3],
           const gfloat *scale,
           gfloat       *out)
{
	gint row, col;

	/* Columns of R scaled by S, then the translation column. */
	for (col = 0; col < 3; col++)
	{
		for (row = 0; row < 3; row++)
			M (out, row, col) = r[row][col] * scale[col];
		M (out, 3, col) = 0.0f;
	}
	M (out, 0, 3) = location[0];
	M (out, 1, 3) = location[1];
	M (out, 2, 3) = location[2];
	M (out, 3, 3) = 1.0f;
}

/* ==========================================================================
 * Matrices
 * ========================================================================== */

void
lrg_math3d_identity (gfloat *out)
{
	g_return_if_fail (out != NULL);

	memset (out, 0, 16 * sizeof (gfloat));
	out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void
lrg_math3d_multiply (const gfloat *a,
                     const gfloat *b,
                     gfloat       *out)
{
	gfloat tmp[16];
	gint   row, col, k;

	g_return_if_fail (a != NULL && b != NULL && out != NULL);

	/* Accumulate into a temporary so @out may alias either input. */
	for (col = 0; col < 4; col++)
		for (row = 0; row < 4; row++)
		{
			gfloat sum = 0.0f;

			for (k = 0; k < 4; k++)
				sum += M (a, row, k) * M (b, k, col);
			M (tmp, row, col) = sum;
		}

	memcpy (out, tmp, sizeof tmp);
}

gboolean
lrg_math3d_invert (const gfloat *m,
                   gfloat       *out)
{
	gfloat inv[16];
	gfloat det;
	gint   i;

	g_return_val_if_fail (m != NULL && out != NULL, FALSE);

	/* Cofactor expansion (MESA gluInvertMatrix), layout-agnostic. */
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
	         m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
	         m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
	         m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
	          m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
	         m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
	         m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
	         m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
	          m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
	         m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
	         m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
	          m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
	          m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
	         m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
	         m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
	          m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
	          m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

	det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (fabsf (det) < LRG_MATH3D_EPSILON_DET)
	{
		lrg_math3d_identity (out);
		return FALSE;
	}

	det = 1.0f / det;
	for (i = 0; i < 16; i++)
		out[i] = inv[i] * det;

	return TRUE;
}

void
lrg_math3d_transpose (const gfloat *m,
                      gfloat       *out)
{
	gfloat tmp[16];
	gint   row, col;

	g_return_if_fail (m != NULL && out != NULL);

	for (col = 0; col < 4; col++)
		for (row = 0; row < 4; row++)
			M (tmp, row, col) = M (m, col, row);
	memcpy (out, tmp, sizeof tmp);
}

void
lrg_math3d_transform_point (const gfloat *m,
                            const gfloat *p,
                            gfloat       *out)
{
	gfloat x, y, z, w;

	g_return_if_fail (m != NULL && p != NULL && out != NULL);

	x = M (m, 0, 0) * p[0] + M (m, 0, 1) * p[1] + M (m, 0, 2) * p[2] + M (m, 0, 3);
	y = M (m, 1, 0) * p[0] + M (m, 1, 1) * p[1] + M (m, 1, 2) * p[2] + M (m, 1, 3);
	z = M (m, 2, 0) * p[0] + M (m, 2, 1) * p[1] + M (m, 2, 2) * p[2] + M (m, 2, 3);
	w = M (m, 3, 0) * p[0] + M (m, 3, 1) * p[1] + M (m, 3, 2) * p[2] + M (m, 3, 3);

	/* Perspective divide only for projective matrices. */
	if (w != 0.0f && w != 1.0f)
	{
		x /= w; y /= w; z /= w;
	}
	v3_set (out, x, y, z);
}

void
lrg_math3d_transform_direction (const gfloat *m,
                                const gfloat *d,
                                gfloat       *out)
{
	gfloat x, y, z;

	g_return_if_fail (m != NULL && d != NULL && out != NULL);

	x = M (m, 0, 0) * d[0] + M (m, 0, 1) * d[1] + M (m, 0, 2) * d[2];
	y = M (m, 1, 0) * d[0] + M (m, 1, 1) * d[1] + M (m, 1, 2) * d[2];
	z = M (m, 2, 0) * d[0] + M (m, 2, 1) * d[1] + M (m, 2, 2) * d[2];

	v3_set (out, x, y, z);
}

/* ==========================================================================
 * TRS compose / decompose
 * ========================================================================== */

void
lrg_math3d_compose (const gfloat *location,
                    const gfloat *rotation,
                    const gfloat *scale,
                    gfloat       *out)
{
	gfloat ca, sa, cb, sb, cc, sc;
	gfloat r[3][3];

	g_return_if_fail (location != NULL && rotation != NULL);
	g_return_if_fail (scale != NULL && out != NULL);

	ca = cosf (rotation[0]); sa = sinf (rotation[0]);
	cb = cosf (rotation[1]); sb = sinf (rotation[1]);
	cc = cosf (rotation[2]); sc = sinf (rotation[2]);

	/* R = Rz(c) * Ry(b) * Rx(a) */
	r[0][0] = cb * cc;
	r[0][1] = sa * sb * cc - ca * sc;
	r[0][2] = ca * sb * cc + sa * sc;
	r[1][0] = cb * sc;
	r[1][1] = sa * sb * sc + ca * cc;
	r[1][2] = ca * sb * sc - sa * cc;
	r[2][0] = -sb;
	r[2][1] = sa * cb;
	r[2][2] = ca * cb;

	write_trs (location, r, scale, out);
}

gboolean
lrg_math3d_decompose (const gfloat *m,
                      gfloat       *location,
                      gfloat       *rotation,
                      gfloat       *scale)
{
	gfloat r[3][3];

	g_return_val_if_fail (m != NULL && location != NULL, FALSE);
	g_return_val_if_fail (rotation != NULL && scale != NULL, FALSE);

	if (!split_scale (m, location, scale, r))
	{
		v3_set (rotation, 0.0f, 0.0f, 0.0f);
		return FALSE;
	}

	euler_from_rotation (r, rotation);
	return TRUE;
}

void
lrg_math3d_compose_quat (const gfloat *location,
                         const gfloat *rotation,
                         const gfloat *scale,
                         gfloat       *out)
{
	gfloat r[3][3];

	g_return_if_fail (location != NULL && rotation != NULL);
	g_return_if_fail (scale != NULL && out != NULL);

	rotation_from_quat (rotation, r);
	write_trs (location, r, scale, out);
}

gboolean
lrg_math3d_decompose_quat (const gfloat *m,
                           gfloat       *location,
                           gfloat       *rotation,
                           gfloat       *scale)
{
	gfloat r[3][3];

	g_return_val_if_fail (m != NULL && location != NULL, FALSE);
	g_return_val_if_fail (rotation != NULL && scale != NULL, FALSE);

	if (!split_scale (m, location, scale, r))
	{
		lrg_math3d_quat_identity (rotation);
		return FALSE;
	}

	quat_from_rotation (r, rotation);
	return TRUE;
}

/* ==========================================================================
 * Euler <-> quaternion <-> matrix
 * ========================================================================== */

void
lrg_math3d_euler_to_quat (const gfloat *euler,
                          gfloat       *out)
{
	gfloat cx, sx, cy, sy, cz, sz;

	g_return_if_fail (euler != NULL && out != NULL);

	/* Half angles of each axis rotation. */
	cx = cosf (euler[0] * 0.5f); sx = sinf (euler[0] * 0.5f);
	cy = cosf (euler[1] * 0.5f); sy = sinf (euler[1] * 0.5f);
	cz = cosf (euler[2] * 0.5f); sz = sinf (euler[2] * 0.5f);

	/* q = qz * qy * qx, expanded. */
	out[0] = sx * cy * cz - cx * sy * sz;
	out[1] = cx * sy * cz + sx * cy * sz;
	out[2] = cx * cy * sz - sx * sy * cz;
	out[3] = cx * cy * cz + sx * sy * sz;
}

void
lrg_math3d_quat_to_euler (const gfloat *q,
                          gfloat       *out)
{
	gfloat n[4];
	gfloat r[3][3];

	g_return_if_fail (q != NULL && out != NULL);

	lrg_math3d_quat_normalize (q, n);
	rotation_from_quat (n, r);
	euler_from_rotation (r, out);
}

void
lrg_math3d_quat_to_matrix (const gfloat *q,
                           gfloat       *out)
{
	gfloat r[3][3];
	gfloat zero[3] = { 0.0f, 0.0f, 0.0f };
	gfloat one[3] = { 1.0f, 1.0f, 1.0f };

	g_return_if_fail (q != NULL && out != NULL);

	rotation_from_quat (q, r);
	write_trs (zero, r, one, out);
}

void
lrg_math3d_matrix_to_quat (const gfloat *m,
                           gfloat       *out)
{
	gfloat r[3][3];
	gint   row, col;

	g_return_if_fail (m != NULL && out != NULL);

	for (col = 0; col < 3; col++)
		for (row = 0; row < 3; row++)
			r[row][col] = M (m, row, col);
	quat_from_rotation (r, out);
}

/* ==========================================================================
 * Quaternions
 * ========================================================================== */

void
lrg_math3d_quat_identity (gfloat *out)
{
	g_return_if_fail (out != NULL);

	out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; out[3] = 1.0f;
}

void
lrg_math3d_quat_from_axis_angle (const gfloat *axis,
                                 gfloat        angle,
                                 gfloat       *out)
{
	gfloat a[3];
	gfloat s;

	g_return_if_fail (axis != NULL && out != NULL);

	memcpy (a, axis, sizeof a);
	if (!v3_normalize (a))
	{
		lrg_math3d_quat_identity (out);
		return;
	}

	s = sinf (angle * 0.5f);
	out[0] = a[0] * s;
	out[1] = a[1] * s;
	out[2] = a[2] * s;
	out[3] = cosf (angle * 0.5f);
}

void
lrg_math3d_quat_multiply (const gfloat *a,
                          const gfloat *b,
                          gfloat       *out)
{
	gfloat x, y, z, w;

	g_return_if_fail (a != NULL && b != NULL && out != NULL);

	/* Hamilton product into temporaries so @out may alias. */
	x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];

	out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}

void
lrg_math3d_quat_normalize (const gfloat *q,
                           gfloat       *out)
{
	gfloat len;

	g_return_if_fail (q != NULL && out != NULL);

	len = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (len < LRG_MATH3D_EPSILON_LEN)
	{
		lrg_math3d_quat_identity (out);
		return;
	}
	out[0] = q[0] / len; out[1] = q[1] / len;
	out[2] = q[2] / len; out[3] = q[3] / len;
}

void
lrg_math3d_quat_invert (const gfloat *q,
                        gfloat       *out)
{
	gfloat len2;

	g_return_if_fail (q != NULL && out != NULL);

	len2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
	if (len2 < LRG_MATH3D_EPSILON_LEN)
	{
		lrg_math3d_quat_identity (out);
		return;
	}
	out[0] = -q[0] / len2; out[1] = -q[1] / len2;
	out[2] = -q[2] / len2; out[3] = q[3] / len2;
}

void
lrg_math3d_quat_rotate_vector (const gfloat *q,
                               const gfloat *v,
                               gfloat       *out)
{
	gfloat u[3];
	gfloat t[3];
	gfloat c[3];
	gfloat w;

	g_return_if_fail (q != NULL && v != NULL && out != NULL);

	/*
	 * v' = v + 2w (u x v) + 2 u x (u x v), with u the vector part: the
	 * expanded q * v * q^-1 for a unit quaternion, without building a
	 * matrix.
	 */
	v3_set (u, q[0], q[1], q[2]);
	w = q[3];
	v3_cross (u, v, t);
	t[0] *= 2.0f; t[1] *= 2.0f; t[2] *= 2.0f;
	v3_cross (u, t, c);
	v3_set (out, v[0] + w * t[0] + c[0],
	             v[1] + w * t[1] + c[1],
	             v[2] + w * t[2] + c[2]);
}

void
lrg_math3d_quat_slerp (const gfloat *a,
                       const gfloat *b,
                       gfloat        t,
                       gfloat       *out)
{
	gfloat bb[4];
	gfloat d;
	gfloat wa, wb;
	gint   i;

	g_return_if_fail (a != NULL && b != NULL && out != NULL);

	/* Take the shorter arc: q and -q are the same rotation. */
	d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	for (i = 0; i < 4; i++)
		bb[i] = (d < 0.0f) ? -b[i] : b[i];
	if (d < 0.0f)
		d = -d;

	if (d > LRG_MATH3D_SLERP_LINEAR)
	{
		/* Nearly parallel: sin(theta) ~ 0, lerp and renormalise. */
		wa = 1.0f - t;
		wb = t;
	}
	else
	{
		gfloat theta = acosf (d);
		gfloat s = sinf (theta);

		wa = sinf ((1.0f - t) * theta) / s;
		wb = sinf (t * theta) / s;
	}

	for (i = 0; i < 4; i++)
		out[i] = wa * a[i] + wb * bb[i];
	lrg_math3d_quat_normalize (out, out);
}

/* ==========================================================================
 * Cameras
 * ========================================================================== */

void
lrg_math3d_look_at (const gfloat *eye,
                    const gfloat *target,
                    const gfloat *up,
                    gfloat       *out)
{
	gfloat f[3], r[3], u[3];
	gfloat world_up[3];

	g_return_if_fail (eye != NULL && target != NULL);
	g_return_if_fail (up != NULL && out != NULL);

	/* Orthonormal camera basis (moved from the editor's view_basis()). */
	v3_sub (target, eye, f);
	if (!v3_normalize (f))
		v3_set (f, 0.0f, 0.0f, -1.0f);

	memcpy (world_up, up, sizeof world_up);
	if (!v3_normalize (world_up))
		v3_set (world_up, 0.0f, 1.0f, 0.0f);

	v3_cross (f, world_up, r);
	if (!v3_normalize (r))
	{
		/* Looking straight down: screen-up is -Z; straight up: +Z. */
		v3_set (world_up, 0.0f, 0.0f, f[1] < 0.0f ? -1.0f : 1.0f);
		v3_cross (f, world_up, r);
		v3_normalize (r);
	}
	v3_cross (r, f, u);

	/* Rows are the camera axes; the last column moves the eye to 0. */
	lrg_math3d_identity (out);
	M (out, 0, 0) = r[0]; M (out, 0, 1) = r[1]; M (out, 0, 2) = r[2];
	M (out, 1, 0) = u[0]; M (out, 1, 1) = u[1]; M (out, 1, 2) = u[2];
	M (out, 2, 0) = -f[0]; M (out, 2, 1) = -f[1]; M (out, 2, 2) = -f[2];
	M (out, 0, 3) = -v3_dot (r, eye);
	M (out, 1, 3) = -v3_dot (u, eye);
	M (out, 2, 3) = v3_dot (f, eye);
}

void
lrg_math3d_perspective (gfloat  fovy,
                        gfloat  aspect,
                        gfloat  near_plane,
                        gfloat  far_plane,
                        gfloat *out)
{
	gfloat t;
	gfloat n = near_plane;
	gfloat f = far_plane;

	g_return_if_fail (out != NULL);

	/* cot(fovy / 2) */
	t = 1.0f / tanf (fovy * 0.5f);

	memset (out, 0, 16 * sizeof (gfloat));
	M (out, 0, 0) = t / aspect;
	M (out, 1, 1) = t;
	M (out, 2, 2) = -(f + n) / (f - n);
	M (out, 2, 3) = -(2.0f * f * n) / (f - n);
	M (out, 3, 2) = -1.0f;
}

void
lrg_math3d_ortho (gfloat  left,
                  gfloat  right,
                  gfloat  bottom,
                  gfloat  top,
                  gfloat  near_plane,
                  gfloat  far_plane,
                  gfloat *out)
{
	gfloat n = near_plane;
	gfloat f = far_plane;

	g_return_if_fail (out != NULL);

	memset (out, 0, 16 * sizeof (gfloat));
	M (out, 0, 0) = 2.0f / (right - left);
	M (out, 1, 1) = 2.0f / (top - bottom);
	M (out, 2, 2) = -2.0f / (f - n);
	/*
	 * Adding +0.0f turns the -0.0f a symmetric volume produces into +0.0f
	 * (IEEE: -0 + +0 = +0); GCC keeps the addition because the build does
	 * not use -fno-signed-zeros.
	 */
	M (out, 0, 3) = -(right + left) / (right - left) + 0.0f;
	M (out, 1, 3) = -(top + bottom) / (top - bottom) + 0.0f;
	M (out, 2, 3) = -(f + n) / (f - n);
	M (out, 3, 3) = 1.0f;
}
