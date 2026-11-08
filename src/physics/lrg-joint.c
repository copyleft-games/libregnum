/* lrg-joint.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe.
 * The constraint parts (spring, axis, point, rotation-Euler, angle and
 * swing-twist parts), the 6-DOF setup/solve flow and the distance
 * constraint follow Jolt's Constraints/ConstraintPart headers,
 * SixDOFConstraint.cpp and DistanceConstraint.cpp closely, translated to
 * gnu89 C on #LrgSolverBody views.
 *
 * LrgJoint: every joint kind of the dynamics world. All kinds except
 * distance are presets of one 6-DOF joint:
 *
 *  - translation: when all three translation axes are locked, one 3-DOF
 *    point part; otherwise one 1-DOF axis part per locked or limited
 *    axis (with an optional soft-limit spring);
 *  - rotation: when all three rotation axes are locked, one 3-DOF
 *    rotation (Euler) part; otherwise the swing-twist part, which
 *    decomposes the relative rotation into swing (Y, Z) and twist (X)
 *    and activates up to three 1-DOF angle parts when a limit is hit;
 *  - motors and friction: one 1-DOF axis or angle part per axis, solved
 *    before the limits so the limits win.
 *
 * A distance joint is one 1-DOF axis part along the line between the
 * anchors. Every part is a sequential-impulse row with an accumulated
 * impulse (warm started) and a non-linear Gauss-Seidel position step.
 *
 * Sign convention (Jolt's): a row's Jacobian velocity is
 * axis . (v1 - v2) + ..., the impulse is subtracted from body A and
 * added to body B, so the accumulated lambda is the impulse on body B.
 */

#include "config.h"

#include <float.h>
#include <math.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-joint.h"
#include "../lrg-log.h"

#define J_PI (3.14159265358979323846f)
#define J_LOCKED_ANGLE (0.5f * J_PI / 180.0f)
#define J_FREE_ANGLE (179.5f * J_PI / 180.0f)
#define J_DEFAULT_MOTOR_FREQUENCY (2.0f)
#define J_DEFAULT_MOTOR_DAMPING (1.0f)
#define J_N_AXES (6)

/* ==================================================================
 * Small vector, matrix and quaternion helpers (column-major 3x3,
 * quaternions xyzw).
 * ================================================================== */

static inline void
v3_set (gfloat *o, gfloat x, gfloat y, gfloat z)
{
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

static inline void
v3_copy (gfloat *o, const gfloat *a)
{
	o[0] = a[0];
	o[1] = a[1];
	o[2] = a[2];
}

static inline void
v3_add (gfloat *o, const gfloat *a, const gfloat *b)
{
	o[0] = a[0] + b[0];
	o[1] = a[1] + b[1];
	o[2] = a[2] + b[2];
}

static inline void
v3_sub (gfloat *o, const gfloat *a, const gfloat *b)
{
	o[0] = a[0] - b[0];
	o[1] = a[1] - b[1];
	o[2] = a[2] - b[2];
}

static inline void
v3_scale (gfloat *o, const gfloat *a, gfloat s)
{
	o[0] = a[0] * s;
	o[1] = a[1] * s;
	o[2] = a[2] * s;
}

/* o += a * s */
static inline void
v3_madd (gfloat *o, const gfloat *a, gfloat s)
{
	o[0] += a[0] * s;
	o[1] += a[1] * s;
	o[2] += a[2] * s;
}

static inline gfloat
v3_dot (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
v3_cross (gfloat *o, const gfloat *a, const gfloat *b)
{
	gfloat x, y, z;

	x = a[1] * b[2] - a[2] * b[1];
	y = a[2] * b[0] - a[0] * b[2];
	z = a[0] * b[1] - a[1] * b[0];
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

static inline gfloat
v3_length (const gfloat *a)
{
	return sqrtf (v3_dot (a, a));
}

static inline gboolean
v3_is_zero (const gfloat *a)
{
	return a[0] == 0.0f && a[1] == 0.0f && a[2] == 0.0f;
}

static inline gboolean
v3_finite (const gfloat *a)
{
	return isfinite (a[0]) && isfinite (a[1]) && isfinite (a[2]);
}

/* out = m * v for a column-major 3x3 */
static inline void
m3_mul_v (gfloat *o, const gfloat *m, const gfloat *v)
{
	gfloat x, y, z;

	x = m[0] * v[0] + m[3] * v[1] + m[6] * v[2];
	y = m[1] * v[0] + m[4] * v[1] + m[7] * v[2];
	z = m[2] * v[0] + m[5] * v[1] + m[8] * v[2];
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

/*
 * m3_invert:
 * Inverts a column-major 3x3 by the adjugate. Returns %FALSE (and leaves
 * @o untouched) when the determinant is zero.
 */
static gboolean
m3_invert (gfloat *o, const gfloat *m)
{
	gfloat c[9];
	gfloat det;
	gfloat inv;
	guint i;

	/* cofactors, written transposed so c is the adjugate */
	c[0] = m[4] * m[8] - m[7] * m[5];
	c[1] = m[7] * m[2] - m[1] * m[8];
	c[2] = m[1] * m[5] - m[4] * m[2];
	c[3] = m[6] * m[5] - m[3] * m[8];
	c[4] = m[0] * m[8] - m[6] * m[2];
	c[5] = m[3] * m[2] - m[0] * m[5];
	c[6] = m[3] * m[7] - m[6] * m[4];
	c[7] = m[6] * m[1] - m[0] * m[7];
	c[8] = m[0] * m[4] - m[3] * m[1];
	det = m[0] * c[0] + m[3] * c[1] + m[6] * c[2];
	if (det == 0.0f || !isfinite (det))
		return FALSE;
	inv = 1.0f / det;
	for (i = 0; i < 9; i++)
		o[i] = c[i] * inv;
	return TRUE;
}

static inline void
q_identity (gfloat *q)
{
	q[0] = 0.0f;
	q[1] = 0.0f;
	q[2] = 0.0f;
	q[3] = 1.0f;
}

static inline void
q_copy (gfloat *o, const gfloat *q)
{
	o[0] = q[0];
	o[1] = q[1];
	o[2] = q[2];
	o[3] = q[3];
}

static inline void
q_set (gfloat *o, gfloat x, gfloat y, gfloat z, gfloat w)
{
	o[0] = x;
	o[1] = y;
	o[2] = z;
	o[3] = w;
}

static inline void
q_conj (gfloat *o, const gfloat *q)
{
	o[0] = -q[0];
	o[1] = -q[1];
	o[2] = -q[2];
	o[3] = q[3];
}

static inline void
q_neg (gfloat *o, const gfloat *q)
{
	o[0] = -q[0];
	o[1] = -q[1];
	o[2] = -q[2];
	o[3] = -q[3];
}

static inline gfloat
q_dot (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

/* o = a * b (apply b, then a) */
static inline void
q_mul (gfloat *o, const gfloat *a, const gfloat *b)
{
	gfloat x, y, z, w;

	x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	o[0] = x;
	o[1] = y;
	o[2] = z;
	o[3] = w;
}

static void
q_normalize (gfloat *o, const gfloat *q)
{
	gfloat len;

	len = sqrtf (q_dot (q, q));
	if (len < 1e-12f || !isfinite (len))
	{
		q_identity (o);
		return;
	}
	o[0] = q[0] / len;
	o[1] = q[1] / len;
	o[2] = q[2] / len;
	o[3] = q[3] / len;
}

/* o = q v q* */
static inline void
q_rotate (gfloat *o, const gfloat *q, const gfloat *v)
{
	gfloat t[3];
	gfloat u[3];
	gfloat r[3];

	/* t = 2 (q.xyz x v); r = v + w t + q.xyz x t */
	v3_cross (t, q, v);
	v3_scale (t, t, 2.0f);
	v3_cross (u, q, t);
	r[0] = v[0] + q[3] * t[0] + u[0];
	r[1] = v[1] + q[3] * t[1] + u[1];
	r[2] = v[2] + q[3] * t[2] + u[2];
	v3_copy (o, r);
}

/* column i of the rotation matrix of q */
static inline void
q_axis (gfloat *o, const gfloat *q, guint i)
{
	gfloat e[3] = { 0.0f, 0.0f, 0.0f };

	e[i] = 1.0f;
	q_rotate (o, q, e);
}

/*
 * q_from_basis:
 * Quaternion of the rotation whose matrix has columns @x, @y, @z (an
 * orthonormal right-handed basis).
 */
static void
q_from_basis (gfloat *o, const gfloat *x, const gfloat *y, const gfloat *z)
{
	gfloat tr;
	gfloat s;
	gfloat q[4];

	tr = x[0] + y[1] + z[2];
	if (tr > 0.0f)
	{
		s = sqrtf (tr + 1.0f) * 2.0f;
		q[3] = 0.25f * s;
		q[0] = (y[2] - z[1]) / s;
		q[1] = (z[0] - x[2]) / s;
		q[2] = (x[1] - y[0]) / s;
	}
	else if (x[0] > y[1] && x[0] > z[2])
	{
		s = sqrtf (1.0f + x[0] - y[1] - z[2]) * 2.0f;
		q[3] = (y[2] - z[1]) / s;
		q[0] = 0.25f * s;
		q[1] = (y[0] + x[1]) / s;
		q[2] = (z[0] + x[2]) / s;
	}
	else if (y[1] > z[2])
	{
		s = sqrtf (1.0f + y[1] - x[0] - z[2]) * 2.0f;
		q[3] = (z[0] - x[2]) / s;
		q[0] = (y[0] + x[1]) / s;
		q[1] = 0.25f * s;
		q[2] = (z[1] + y[2]) / s;
	}
	else
	{
		s = sqrtf (1.0f + z[2] - x[0] - y[1]) * 2.0f;
		q[3] = (x[1] - y[0]) / s;
		q[0] = (z[0] + x[2]) / s;
		q[1] = (z[1] + y[2]) / s;
		q[2] = 0.25f * s;
	}
	q_normalize (o, q);
}

/*
 * q_swing_twist:
 * Jolt Quat::GetSwingTwist: q = swing * twist, twist about X, swing
 * about an axis in the YZ plane.
 */
static void
q_swing_twist (const gfloat *q, gfloat *swing, gfloat *twist)
{
	gfloat x, y, z, w, s;

	x = q[0];
	y = q[1];
	z = q[2];
	w = q[3];
	s = sqrtf (w * w + x * x);
	if (s != 0.0f)
	{
		q_set (twist, x / s, 0.0f, 0.0f, w / s);
		q_set (swing, 0.0f, (w * y - x * z) / s, (w * z + x * y) / s, s);
	}
	else
	{
		/* a 180 degree rotation about an axis in the YZ plane */
		q_identity (twist);
		q_copy (swing, q);
	}
}

/* Jolt Quat::GetTwist: the part of q that rotates about unit @axis */
static void
q_get_twist (gfloat *o, const gfloat *q, const gfloat *axis)
{
	gfloat d;
	gfloat t[4];
	gfloat len;

	d = v3_dot (q, axis);
	q_set (t, d * axis[0], d * axis[1], d * axis[2], q[3]);
	len = q_dot (t, t);
	if (len != 0.0f)
	{
		len = sqrtf (len);
		q_set (o, t[0] / len, t[1] / len, t[2] / len, t[3] / len);
	}
	else
		q_identity (o);
}

/* q = Rz * Ry * Rx (X applied first): Jolt Quat::sEulerAngles */
static void
q_from_euler (gfloat *o, const gfloat *angles)
{
	gfloat cx, sx, cy, sy, cz, sz;

	cx = cosf (0.5f * angles[0]);
	sx = sinf (0.5f * angles[0]);
	cy = cosf (0.5f * angles[1]);
	sy = sinf (0.5f * angles[1]);
	cz = cosf (0.5f * angles[2]);
	sz = sinf (0.5f * angles[2]);
	o[0] = sx * cy * cz - cx * sy * sz;
	o[1] = cx * sy * cz + sx * cy * sz;
	o[2] = cx * cy * sz - sx * sy * cz;
	o[3] = cx * cy * cz + sx * sy * sz;
}

/* any unit vector perpendicular to unit @n */
static void
v3_perpendicular (gfloat *o, const gfloat *n)
{
	gfloat t[3];

	if (fabsf (n[0]) < 0.57f)
		v3_set (t, 1.0f, 0.0f, 0.0f);
	else
		v3_set (t, 0.0f, 1.0f, 0.0f);
	v3_cross (o, n, t);
	v3_scale (o, o, 1.0f / v3_length (o));
}

/* ==================================================================
 * Solver body access. Velocities are written directly (they do not
 * affect the world inverse inertia); positions only through
 * lrg_solver_body_apply_position_correction().
 * ================================================================== */

/* world point of a body-local anchor measured from the body origin */
static inline void
body_point (const LrgSolverBody *b, const gfloat *local, gfloat *out)
{
	lrg_solver_body_local_to_world (b, local, out);
}

/* world rotation of a joint frame: body orientation * frame */
static inline void
body_frame (const LrgSolverBody *b, const gfloat *frame, gfloat *out)
{
	q_mul (out, b->orientation, frame);
	q_normalize (out, out);
}

/* ==================================================================
 * SpringPart (Jolt SpringPart.h)
 * ================================================================== */

typedef struct
{
	gfloat bias;
	gfloat softness;
} SpringPart;

static inline void
spring_with_bias (SpringPart *s, gfloat bias)
{
	s->softness = 0.0f;
	s->bias = bias;
}

/*
 * spring_with_stiffness:
 * Soft constraint (Erin Catto, "Soft constraints"): softness and bias
 * for stiffness @k and damping @c over @dt. Writes the softened
 * effective mass to @eff.
 */
static void
spring_with_stiffness (SpringPart *s,
                       gfloat      dt,
                       gfloat      inv_eff,
                       gfloat      bias,
                       gfloat      c_error,
                       gfloat      k,
                       gfloat      c,
                       gfloat     *eff)
{
	s->softness = 1.0f / (dt * (c + dt * k));
	s->bias = bias + dt * k * s->softness * c_error;
	*eff = 1.0f / (inv_eff + s->softness);
}

/* frequency (Hz) and damping ratio, relative to the effective mass */
static void
spring_with_frequency (SpringPart *s,
                       gfloat      dt,
                       gfloat      inv_eff,
                       gfloat      bias,
                       gfloat      c_error,
                       gfloat      frequency,
                       gfloat      damping,
                       gfloat     *eff)
{
	gfloat m;
	gfloat omega;

	m = 1.0f / inv_eff;
	omega = 2.0f * J_PI * frequency;
	spring_with_stiffness (s, dt, inv_eff, bias, c_error, m * omega * omega,
	                       2.0f * m * damping * omega, eff);
}

static inline gfloat
spring_get_bias (const SpringPart *s, gfloat total_lambda)
{
	return s->softness * total_lambda + s->bias;
}

/* ==================================================================
 * AxisPart: one linear row along a world axis (Jolt AxisConstraintPart)
 * ================================================================== */

typedef struct
{
	gfloat r1u_x_axis[3];
	gfloat r2_x_axis[3];
	gfloat inv_i1_r1u_x_axis[3];
	gfloat inv_i2_r2_x_axis[3];
	gfloat inv_mass1;
	gfloat inv_mass2;
	gfloat eff_mass;
	SpringPart spring;
	gfloat total_lambda;
} AxisPart;

static inline void
axis_deactivate (AxisPart *p)
{
	p->eff_mass = 0.0f;
	p->total_lambda = 0.0f;
}

static inline gboolean
axis_is_active (const AxisPart *p)
{
	return p->eff_mass != 0.0f;
}

/* r1_plus_u: world point minus body A's centre of mass; r2 likewise for B */
static gfloat
axis_inv_eff (AxisPart            *p,
              const LrgSolverBody *b1,
              const gfloat        *r1_plus_u,
              const LrgSolverBody *b2,
              const gfloat        *r2,
              const gfloat        *axis)
{
	v3_cross (p->r1u_x_axis, r1_plus_u, axis);
	m3_mul_v (p->inv_i1_r1u_x_axis, b1->inv_inertia, p->r1u_x_axis);
	v3_cross (p->r2_x_axis, r2, axis);
	m3_mul_v (p->inv_i2_r2_x_axis, b2->inv_inertia, p->r2_x_axis);
	p->inv_mass1 = b1->inv_mass;
	p->inv_mass2 = b2->inv_mass;
	return b1->inv_mass + v3_dot (p->inv_i1_r1u_x_axis, p->r1u_x_axis) +
	       b2->inv_mass + v3_dot (p->inv_i2_r2_x_axis, p->r2_x_axis);
}

static void
axis_setup (AxisPart            *p,
            const LrgSolverBody *b1,
            const gfloat        *r1_plus_u,
            const LrgSolverBody *b2,
            const gfloat        *r2,
            const gfloat        *axis,
            gfloat               bias)
{
	gfloat inv;

	inv = axis_inv_eff (p, b1, r1_plus_u, b2, r2, axis);
	if (inv < FLT_MIN)
	{
		axis_deactivate (p);
		return;
	}
	p->eff_mass = 1.0f / inv;
	spring_with_bias (&p->spring, bias);
}

/*
 * axis_setup_limit:
 * A limit row: hard when @frequency is zero, otherwise a spring of that
 * frequency pulling the error @c_error to zero.
 */
static void
axis_setup_spring (AxisPart            *p,
                   gfloat               dt,
                   const LrgSolverBody *b1,
                   const gfloat        *r1_plus_u,
                   const LrgSolverBody *b2,
                   const gfloat        *r2,
                   const gfloat        *axis,
                   gfloat               bias,
                   gfloat               c_error,
                   gfloat               frequency,
                   gfloat               damping)
{
	gfloat inv;

	inv = axis_inv_eff (p, b1, r1_plus_u, b2, r2, axis);
	if (inv < FLT_MIN)
	{
		axis_deactivate (p);
		return;
	}
	if (frequency > 0.0f)
		spring_with_frequency (&p->spring, dt, inv, bias, c_error, frequency, damping,
		                       &p->eff_mass);
	else
	{
		p->eff_mass = 1.0f / inv;
		spring_with_bias (&p->spring, bias);
	}
}

static void
axis_apply (const AxisPart *p,
            LrgSolverBody  *b1,
            LrgSolverBody  *b2,
            const gfloat   *axis,
            gfloat          lambda)
{
	if (lambda == 0.0f)
		return;
	v3_madd (b1->linear_velocity, axis, -lambda * p->inv_mass1);
	v3_madd (b1->angular_velocity, p->inv_i1_r1u_x_axis, -lambda);
	v3_madd (b2->linear_velocity, axis, lambda * p->inv_mass2);
	v3_madd (b2->angular_velocity, p->inv_i2_r2_x_axis, lambda);
}

static void
axis_warm_start (AxisPart      *p,
                 LrgSolverBody *b1,
                 LrgSolverBody *b2,
                 const gfloat  *axis,
                 gfloat         ratio)
{
	p->total_lambda *= ratio;
	axis_apply (p, b1, b2, axis, p->total_lambda);
}

static gboolean
axis_solve_velocity (AxisPart      *p,
                     LrgSolverBody *b1,
                     LrgSolverBody *b2,
                     const gfloat  *axis,
                     gfloat         min_lambda,
                     gfloat         max_lambda)
{
	gfloat dv[3];
	gfloat jv;
	gfloat lambda;
	gfloat new_lambda;

	v3_sub (dv, b1->linear_velocity, b2->linear_velocity);
	jv = v3_dot (axis, dv) + v3_dot (p->r1u_x_axis, b1->angular_velocity) -
	     v3_dot (p->r2_x_axis, b2->angular_velocity);
	lambda = p->eff_mass * (jv - spring_get_bias (&p->spring, p->total_lambda));
	new_lambda = CLAMP (p->total_lambda + lambda, min_lambda, max_lambda);
	lambda = new_lambda - p->total_lambda;
	p->total_lambda = new_lambda;
	axis_apply (p, b1, b2, axis, lambda);
	return lambda != 0.0f;
}

/*
 * axis_solve_position:
 * Position step for error @c along @axis: the position impulse acts at
 * the world point @point (Jolt's r1 + u point, the anchor on body B).
 */
static gboolean
axis_solve_position (const AxisPart *p,
                     LrgSolverBody  *b1,
                     LrgSolverBody  *b2,
                     const gfloat   *axis,
                     const gfloat   *point,
                     gfloat          c,
                     gfloat          baumgarte)
{
	gfloat lambda;
	gfloat impulse[3];
	gfloat neg[3];

	if (c == 0.0f || p->spring.softness != 0.0f || p->eff_mass == 0.0f)
		return FALSE;
	lambda = -p->eff_mass * baumgarte * c;
	v3_scale (impulse, axis, lambda);
	v3_scale (neg, impulse, -1.0f);
	lrg_solver_body_apply_position_correction (b1, neg, point, NULL);
	lrg_solver_body_apply_position_correction (b2, impulse, point, NULL);
	return TRUE;
}

/* ==================================================================
 * PointPart: three linear rows locking two points
 * (Jolt PointConstraintPart)
 * ================================================================== */

typedef struct
{
	gfloat r1[3];
	gfloat r2[3];
	gfloat inv_mass1;
	gfloat inv_mass2;
	gfloat inv_i1[9];
	gfloat inv_i2[9];
	gfloat eff_mass[9];
	gboolean active;
	gfloat total_lambda[3];
} PointPart;

static inline void
point_deactivate (PointPart *p)
{
	p->active = FALSE;
	v3_set (p->total_lambda, 0.0f, 0.0f, 0.0f);
}

/* @local1, @local2: anchors measured from each body's origin */
static void
point_setup (PointPart           *p,
             const LrgSolverBody *b1,
             const gfloat        *local1,
             const LrgSolverBody *b2,
             const gfloat        *local2)
{
	gfloat w[3];
	gfloat k[9];
	gfloat e[3];
	gfloat t[3];
	gfloat u[3];
	guint j;

	body_point (b1, local1, w);
	v3_sub (p->r1, w, b1->position);
	body_point (b2, local2, w);
	v3_sub (p->r2, w, b2->position);
	p->inv_mass1 = b1->inv_mass;
	p->inv_mass2 = b2->inv_mass;
	memcpy (p->inv_i1, b1->inv_inertia, sizeof (p->inv_i1));
	memcpy (p->inv_i2, b2->inv_inertia, sizeof (p->inv_i2));

	/* K e_j = (m1 + m2) e_j + r1 x (I1^-1 (e_j x r1)) + r2 x (I2^-1 (e_j x r2)) */
	for (j = 0; j < 3; j++)
	{
		v3_set (e, 0.0f, 0.0f, 0.0f);
		e[j] = 1.0f;
		v3_scale (&k[j * 3], e, b1->inv_mass + b2->inv_mass);
		v3_cross (t, e, p->r1);
		m3_mul_v (u, p->inv_i1, t);
		v3_cross (t, p->r1, u);
		v3_add (&k[j * 3], &k[j * 3], t);
		v3_cross (t, e, p->r2);
		m3_mul_v (u, p->inv_i2, t);
		v3_cross (t, p->r2, u);
		v3_add (&k[j * 3], &k[j * 3], t);
	}
	p->active = m3_invert (p->eff_mass, k);
	if (!p->active)
		v3_set (p->total_lambda, 0.0f, 0.0f, 0.0f);
}

static void
point_apply (const PointPart *p,
             LrgSolverBody   *b1,
             LrgSolverBody   *b2,
             const gfloat    *lambda)
{
	gfloat t[3];
	gfloat u[3];

	if (v3_is_zero (lambda))
		return;
	v3_madd (b1->linear_velocity, lambda, -p->inv_mass1);
	v3_cross (t, p->r1, lambda);
	m3_mul_v (u, p->inv_i1, t);
	v3_madd (b1->angular_velocity, u, -1.0f);
	v3_madd (b2->linear_velocity, lambda, p->inv_mass2);
	v3_cross (t, p->r2, lambda);
	m3_mul_v (u, p->inv_i2, t);
	v3_madd (b2->angular_velocity, u, 1.0f);
}

static void
point_warm_start (PointPart     *p,
                  LrgSolverBody *b1,
                  LrgSolverBody *b2,
                  gfloat         ratio)
{
	v3_scale (p->total_lambda, p->total_lambda, ratio);
	point_apply (p, b1, b2, p->total_lambda);
}

static gboolean
point_solve_velocity (PointPart     *p,
                      LrgSolverBody *b1,
                      LrgSolverBody *b2)
{
	gfloat jv[3];
	gfloat t[3];
	gfloat lambda[3];

	/* v1 + w1 x r1 - v2 - w2 x r2 */
	v3_cross (t, b1->angular_velocity, p->r1);
	v3_add (jv, b1->linear_velocity, t);
	v3_sub (jv, jv, b2->linear_velocity);
	v3_cross (t, b2->angular_velocity, p->r2);
	v3_sub (jv, jv, t);
	m3_mul_v (lambda, p->eff_mass, jv);
	v3_add (p->total_lambda, p->total_lambda, lambda);
	point_apply (p, b1, b2, lambda);
	return !v3_is_zero (lambda);
}

static gboolean
point_solve_position (const PointPart *p,
                      LrgSolverBody   *b1,
                      LrgSolverBody   *b2,
                      gfloat           baumgarte)
{
	gfloat p1[3];
	gfloat p2[3];
	gfloat sep[3];
	gfloat lambda[3];
	gfloat neg[3];

	v3_add (p1, b1->position, p->r1);
	v3_add (p2, b2->position, p->r2);
	v3_sub (sep, p2, p1);
	if (v3_is_zero (sep) || !p->active)
		return FALSE;
	m3_mul_v (lambda, p->eff_mass, sep);
	v3_scale (lambda, lambda, -baumgarte);
	v3_scale (neg, lambda, -1.0f);
	lrg_solver_body_apply_position_correction (b1, neg, p1, NULL);
	lrg_solver_body_apply_position_correction (b2, lambda, p2, NULL);
	return TRUE;
}

/* ==================================================================
 * RotationPart: three angular rows locking relative rotation
 * (Jolt RotationEulerConstraintPart)
 * ================================================================== */

typedef struct
{
	gfloat inv_i1[9];
	gfloat inv_i2[9];
	gfloat eff_mass[9];
	gboolean active;
	gfloat total_lambda[3];
} RotationPart;

static inline void
rotation_deactivate (RotationPart *p)
{
	p->active = FALSE;
	v3_set (p->total_lambda, 0.0f, 0.0f, 0.0f);
}

static void
rotation_setup (RotationPart        *p,
                const LrgSolverBody *b1,
                const LrgSolverBody *b2)
{
	gfloat sum[9];
	guint i;

	memcpy (p->inv_i1, b1->inv_inertia, sizeof (p->inv_i1));
	memcpy (p->inv_i2, b2->inv_inertia, sizeof (p->inv_i2));
	for (i = 0; i < 9; i++)
		sum[i] = p->inv_i1[i] + p->inv_i2[i];
	p->active = m3_invert (p->eff_mass, sum);
	if (!p->active)
	{
		/* a body locked on some rotation axes: give those columns unit
		 * stiffness so the rest still solve (Jolt does the same) */
		for (i = 0; i < 3; i++)
			if (sum[i * 3] == 0.0f && sum[i * 3 + 1] == 0.0f && sum[i * 3 + 2] == 0.0f)
				sum[i * 3 + i] = 1.0f;
		p->active = m3_invert (p->eff_mass, sum);
	}
	if (!p->active)
		v3_set (p->total_lambda, 0.0f, 0.0f, 0.0f);
}

static void
rotation_apply (const RotationPart *p,
                LrgSolverBody      *b1,
                LrgSolverBody      *b2,
                const gfloat       *lambda)
{
	gfloat u[3];

	if (v3_is_zero (lambda))
		return;
	m3_mul_v (u, p->inv_i1, lambda);
	v3_madd (b1->angular_velocity, u, -1.0f);
	m3_mul_v (u, p->inv_i2, lambda);
	v3_madd (b2->angular_velocity, u, 1.0f);
}

static void
rotation_warm_start (RotationPart  *p,
                     LrgSolverBody *b1,
                     LrgSolverBody *b2,
                     gfloat         ratio)
{
	v3_scale (p->total_lambda, p->total_lambda, ratio);
	rotation_apply (p, b1, b2, p->total_lambda);
}

static gboolean
rotation_solve_velocity (RotationPart  *p,
                         LrgSolverBody *b1,
                         LrgSolverBody *b2)
{
	gfloat dw[3];
	gfloat lambda[3];

	v3_sub (dw, b1->angular_velocity, b2->angular_velocity);
	m3_mul_v (lambda, p->eff_mass, dw);
	v3_add (p->total_lambda, p->total_lambda, lambda);
	rotation_apply (p, b1, b2, lambda);
	return !v3_is_zero (lambda);
}

/*
 * rotation_solve_position:
 * @inv_initial: rotation that maps body A's orientation to body B's at
 *   rest: q2 * inv_initial * q1^-1 is identity when solved.
 */
static gboolean
rotation_solve_position (RotationPart  *p,
                         LrgSolverBody *b1,
                         LrgSolverBody *b2,
                         const gfloat  *inv_initial,
                         gfloat         baumgarte)
{
	gfloat diff[4];
	gfloat c1[4];
	gfloat error[3];
	gfloat lambda[3];
	gfloat neg[3];

	rotation_setup (p, b1, b2);
	if (!p->active)
		return FALSE;
	q_conj (c1, b1->orientation);
	q_mul (diff, b2->orientation, inv_initial);
	q_mul (diff, diff, c1);
	if (diff[3] < 0.0f)
		q_neg (diff, diff);
	v3_scale (error, diff, 2.0f);
	if (v3_is_zero (error))
		return FALSE;
	m3_mul_v (lambda, p->eff_mass, error);
	v3_scale (lambda, lambda, -baumgarte);
	v3_scale (neg, lambda, -1.0f);
	lrg_solver_body_apply_position_correction (b1, NULL, NULL, neg);
	lrg_solver_body_apply_position_correction (b2, NULL, NULL, lambda);
	return TRUE;
}

/* ==================================================================
 * AnglePart: one angular row about a world axis
 * (Jolt AngleConstraintPart)
 * ================================================================== */

typedef struct
{
	gfloat inv_i1_axis[3];
	gfloat inv_i2_axis[3];
	gfloat eff_mass;
	SpringPart spring;
	gfloat total_lambda;
} AnglePart;

static inline void
angle_deactivate (AnglePart *p)
{
	p->eff_mass = 0.0f;
	p->total_lambda = 0.0f;
}

static inline gboolean
angle_is_active (const AnglePart *p)
{
	return p->eff_mass != 0.0f;
}

static gfloat
angle_inv_eff (AnglePart           *p,
               const LrgSolverBody *b1,
               const LrgSolverBody *b2,
               const gfloat        *axis)
{
	gfloat s[3];

	m3_mul_v (p->inv_i1_axis, b1->inv_inertia, axis);
	m3_mul_v (p->inv_i2_axis, b2->inv_inertia, axis);
	v3_add (s, p->inv_i1_axis, p->inv_i2_axis);
	return v3_dot (axis, s);
}

static void
angle_setup (AnglePart           *p,
             const LrgSolverBody *b1,
             const LrgSolverBody *b2,
             const gfloat        *axis,
             gfloat               bias)
{
	gfloat inv;

	inv = angle_inv_eff (p, b1, b2, axis);
	if (inv < FLT_MIN)
	{
		angle_deactivate (p);
		return;
	}
	p->eff_mass = 1.0f / inv;
	spring_with_bias (&p->spring, bias);
}

static void
angle_setup_spring (AnglePart           *p,
                    gfloat               dt,
                    const LrgSolverBody *b1,
                    const LrgSolverBody *b2,
                    const gfloat        *axis,
                    gfloat               bias,
                    gfloat               c_error,
                    gfloat               frequency,
                    gfloat               damping)
{
	gfloat inv;

	inv = angle_inv_eff (p, b1, b2, axis);
	if (inv < FLT_MIN)
	{
		angle_deactivate (p);
		return;
	}
	spring_with_frequency (&p->spring, dt, inv, bias, c_error, frequency, damping,
	                       &p->eff_mass);
}

static void
angle_apply (const AnglePart *p,
             LrgSolverBody   *b1,
             LrgSolverBody   *b2,
             gfloat           lambda)
{
	if (lambda == 0.0f)
		return;
	v3_madd (b1->angular_velocity, p->inv_i1_axis, -lambda);
	v3_madd (b2->angular_velocity, p->inv_i2_axis, lambda);
}

static void
angle_warm_start (AnglePart     *p,
                  LrgSolverBody *b1,
                  LrgSolverBody *b2,
                  gfloat         ratio)
{
	p->total_lambda *= ratio;
	angle_apply (p, b1, b2, p->total_lambda);
}

static gboolean
angle_solve_velocity (AnglePart     *p,
                      LrgSolverBody *b1,
                      LrgSolverBody *b2,
                      const gfloat  *axis,
                      gfloat         min_lambda,
                      gfloat         max_lambda)
{
	gfloat dw[3];
	gfloat lambda;
	gfloat new_lambda;

	v3_sub (dw, b1->angular_velocity, b2->angular_velocity);
	lambda = p->eff_mass * (v3_dot (axis, dw) - spring_get_bias (&p->spring, p->total_lambda));
	new_lambda = CLAMP (p->total_lambda + lambda, min_lambda, max_lambda);
	lambda = new_lambda - p->total_lambda;
	p->total_lambda = new_lambda;
	angle_apply (p, b1, b2, lambda);
	return lambda != 0.0f;
}

/* ==================================================================
 * SwingTwistPart (Jolt SwingTwistConstraintPart)
 * ================================================================== */

enum {
	ST_TWIST_LOCKED = 1 << 0,
	ST_SWING_Y_LOCKED = 1 << 1,
	ST_SWING_Z_LOCKED = 1 << 2,
	ST_TWIST_FREE = 1 << 3,
	ST_SWING_Y_FREE = 1 << 4,
	ST_SWING_Z_FREE = 1 << 5,
	ST_SWING_YZ_FREE = ST_SWING_Y_FREE | ST_SWING_Z_FREE
};

enum {
	CLAMP_TWIST_MIN = 1 << 0,
	CLAMP_TWIST_MAX = 1 << 1,
	CLAMP_SWING_Y_MIN = 1 << 2,
	CLAMP_SWING_Y_MAX = 1 << 3,
	CLAMP_SWING_Z_MIN = 1 << 4,
	CLAMP_SWING_Z_MAX = 1 << 5
};

typedef struct
{
	guint flags;
	LrgJointSwingType swing_type;
	gfloat sin_twist_min, sin_twist_max, cos_twist_min, cos_twist_max;
	gfloat swing_y_half_min, swing_y_half_max, swing_z_half_min, swing_z_half_max;
	gfloat sin_swing_y_min, sin_swing_y_max, cos_swing_y_min, cos_swing_y_max;
	gfloat sin_swing_z_min, sin_swing_z_max, cos_swing_z_min, cos_swing_z_max;
	gfloat ws_swing_y_axis[3];
	gfloat ws_swing_z_axis[3];
	gfloat ws_twist_axis[3];
	AnglePart swing_y;
	AnglePart swing_z;
	AnglePart twist;
} SwingTwistPart;

/* sine/cosine of half @min and @max, with the locked/free flags */
static void
st_axis_limits (gfloat  min_angle,
                gfloat  max_angle,
                guint   locked_flag,
                guint   free_flag,
                guint  *flags,
                gfloat *s_min,
                gfloat *s_max,
                gfloat *c_min,
                gfloat *c_max)
{
	if (min_angle > -J_LOCKED_ANGLE && max_angle < J_LOCKED_ANGLE)
	{
		*flags |= locked_flag;
		*s_min = 0.0f;
		*s_max = 0.0f;
		*c_min = 1.0f;
		*c_max = 1.0f;
	}
	else if (min_angle < -J_FREE_ANGLE && max_angle > J_FREE_ANGLE)
	{
		*flags |= free_flag;
		*s_min = -1.0f;
		*s_max = 1.0f;
		*c_min = 0.0f;
		*c_max = 0.0f;
	}
	else
	{
		*s_min = sinf (0.5f * min_angle);
		*s_max = sinf (0.5f * max_angle);
		*c_min = cosf (0.5f * min_angle);
		*c_max = cosf (0.5f * max_angle);
	}
}

static void
st_set_limits (SwingTwistPart *p,
               gfloat          twist_min,
               gfloat          twist_max,
               gfloat          swing_y_min,
               gfloat          swing_y_max,
               gfloat          swing_z_min,
               gfloat          swing_z_max)
{
	p->swing_y_half_min = 0.5f * swing_y_min;
	p->swing_y_half_max = 0.5f * swing_y_max;
	p->swing_z_half_min = 0.5f * swing_z_min;
	p->swing_z_half_max = 0.5f * swing_z_max;
	p->flags = 0;
	st_axis_limits (twist_min, twist_max, ST_TWIST_LOCKED, ST_TWIST_FREE, &p->flags,
	                &p->sin_twist_min, &p->sin_twist_max, &p->cos_twist_min, &p->cos_twist_max);
	st_axis_limits (swing_y_min, swing_y_max, ST_SWING_Y_LOCKED, ST_SWING_Y_FREE, &p->flags,
	                &p->sin_swing_y_min, &p->sin_swing_y_max,
	                &p->cos_swing_y_min, &p->cos_swing_y_max);
	st_axis_limits (swing_z_min, swing_z_max, ST_SWING_Z_LOCKED, ST_SWING_Z_FREE, &p->flags,
	                &p->sin_swing_z_min, &p->sin_swing_z_max,
	                &p->cos_swing_z_min, &p->cos_swing_z_max);
}

/* Jolt sDistanceToMinShorter: which limit is nearer on the half-angle circle */
static gboolean
st_min_shorter (gfloat delta_min, gfloat delta_max)
{
	delta_min = fabsf (delta_min);
	if (delta_min > 1.0f)
		delta_min = 2.0f - delta_min;
	delta_max = fabsf (delta_max);
	if (delta_max > 1.0f)
		delta_max = 2.0f - delta_max;
	return delta_min < delta_max;
}

/*
 * ellipse_closest:
 * Closest point on the ellipse (x/a)^2 + (y/b)^2 = 1 to an outside point
 * (Jolt Ellipse::GetClosestPoint, Newton on the Lagrange parameter;
 * capped at 64 iterations).
 */
static void
ellipse_closest (gfloat a, gfloat b, gfloat px, gfloat py, gfloat *ox, gfloat *oy)
{
	gfloat a_sq, b_sq, t, ta, tb, gt, gd;
	guint i;

	a_sq = a * a;
	b_sq = b * b;
	t = 0.0f;
	for (i = 0; i < 64; i++)
	{
		ta = t + a_sq;
		tb = t + b_sq;
		gt = (a * px / ta) * (a * px / ta) + (b * py / tb) * (b * py / tb) - 1.0f;
		if (fabsf (gt) < 1.0e-6f)
			break;
		gd = -2.0f * (a_sq * px * px / (ta * ta * ta) + b_sq * py * py / (tb * tb * tb));
		if (gd == 0.0f)
			break;
		t -= gt / gd;
	}
	ta = t + a_sq;
	tb = t + b_sq;
	*ox = a_sq * px / ta;
	*oy = b_sq * py / tb;
}

/*
 * st_clamp:
 * Clamps the swing and twist quaternions onto the limits and reports in
 * @clamped which limits were hit (Jolt ClampSwingTwist).
 */
static void
st_clamp (const SwingTwistPart *p, gfloat *swing, gfloat *twist, guint *clamped)
{
	gboolean negate_swing;
	gboolean negate_twist;
	gfloat delta_min;
	gfloat delta_max;
	gfloat v;

	*clamped = 0;
	negate_swing = swing[3] < 0.0f;
	if (negate_swing)
		q_neg (swing, swing);
	negate_twist = twist[3] < 0.0f;
	if (negate_twist)
		q_neg (twist, twist);

	/* twist */
	if (p->flags & ST_TWIST_LOCKED)
	{
		*clamped |= twist[0] != 0.0f ? (CLAMP_TWIST_MIN | CLAMP_TWIST_MAX) : 0;
		q_identity (twist);
	}
	else if ((p->flags & ST_TWIST_FREE) == 0)
	{
		delta_min = p->sin_twist_min - twist[0];
		delta_max = twist[0] - p->sin_twist_max;
		if (delta_min > 0.0f || delta_max > 0.0f)
		{
			if (st_min_shorter (delta_min, delta_max))
			{
				q_set (twist, p->sin_twist_min, 0.0f, 0.0f, p->cos_twist_min);
				*clamped |= CLAMP_TWIST_MIN;
			}
			else
			{
				q_set (twist, p->sin_twist_max, 0.0f, 0.0f, p->cos_twist_max);
				*clamped |= CLAMP_TWIST_MAX;
			}
		}
	}

	/* swing */
	if (p->flags & ST_SWING_Y_LOCKED)
	{
		if (p->flags & ST_SWING_Z_LOCKED)
		{
			*clamped |= swing[1] != 0.0f ? (CLAMP_SWING_Y_MIN | CLAMP_SWING_Y_MAX) : 0;
			*clamped |= swing[2] != 0.0f ? (CLAMP_SWING_Z_MIN | CLAMP_SWING_Z_MAX) : 0;
			q_identity (swing);
		}
		else
		{
			*clamped |= swing[1] != 0.0f ? (CLAMP_SWING_Y_MIN | CLAMP_SWING_Y_MAX) : 0;
			delta_min = p->sin_swing_z_min - swing[2];
			delta_max = swing[2] - p->sin_swing_z_max;
			if (delta_min > 0.0f || delta_max > 0.0f)
			{
				if (st_min_shorter (delta_min, delta_max))
				{
					q_set (swing, 0.0f, 0.0f, p->sin_swing_z_min, p->cos_swing_z_min);
					*clamped |= CLAMP_SWING_Z_MIN;
				}
				else
				{
					q_set (swing, 0.0f, 0.0f, p->sin_swing_z_max, p->cos_swing_z_max);
					*clamped |= CLAMP_SWING_Z_MAX;
				}
			}
			else if ((*clamped & CLAMP_SWING_Y_MIN) != 0)
			{
				v = swing[2];
				q_set (swing, 0.0f, 0.0f, v, sqrtf (MAX (0.0f, 1.0f - v * v)));
			}
		}
	}
	else if (p->flags & ST_SWING_Z_LOCKED)
	{
		*clamped |= swing[2] != 0.0f ? (CLAMP_SWING_Z_MIN | CLAMP_SWING_Z_MAX) : 0;
		delta_min = p->sin_swing_y_min - swing[1];
		delta_max = swing[1] - p->sin_swing_y_max;
		if (delta_min > 0.0f || delta_max > 0.0f)
		{
			if (st_min_shorter (delta_min, delta_max))
			{
				q_set (swing, 0.0f, p->sin_swing_y_min, 0.0f, p->cos_swing_y_min);
				*clamped |= CLAMP_SWING_Y_MIN;
			}
			else
			{
				q_set (swing, 0.0f, p->sin_swing_y_max, 0.0f, p->cos_swing_y_max);
				*clamped |= CLAMP_SWING_Y_MAX;
			}
		}
		else if ((*clamped & CLAMP_SWING_Z_MIN) != 0)
		{
			v = swing[1];
			q_set (swing, 0.0f, v, 0.0f, sqrtf (MAX (0.0f, 1.0f - v * v)));
		}
	}
	else if (p->swing_type == LRG_JOINT_SWING_CONE)
	{
		gfloat a;
		gfloat b;
		gfloat cx;
		gfloat cy;

		/* elliptic cone on (sin half swing y, sin half swing z) */
		a = p->sin_swing_y_max;
		b = p->sin_swing_z_max;
		if ((swing[1] / a) * (swing[1] / a) + (swing[2] / b) * (swing[2] / b) > 1.0f)
		{
			ellipse_closest (a, b, swing[1], swing[2], &cx, &cy);
			q_set (swing, 0.0f, cx, cy, sqrtf (MAX (0.0f, 1.0f - cx * cx - cy * cy)));
			*clamped |= CLAMP_SWING_Y_MIN | CLAMP_SWING_Y_MAX |
			            CLAMP_SWING_Z_MIN | CLAMP_SWING_Z_MAX;
		}
	}
	else
	{
		gfloat hy;
		gfloat hz;
		gfloat cy;
		gfloat cz;

		/* pyramid: clamp the two half angles independently */
		hy = atan2f (swing[1], swing[3]);
		hz = atan2f (swing[2], swing[3]);
		cy = CLAMP (hy, p->swing_y_half_min, p->swing_y_half_max);
		cz = CLAMP (hz, p->swing_z_half_min, p->swing_z_half_max);
		if (cy != hy || cz != hz)
		{
			gfloat q[4];

			q_set (q, 0.0f, sinf (cy) * cosf (cz), cosf (cy) * sinf (cz), cosf (cy) * cosf (cz));
			q_normalize (swing, q);
			*clamped |= CLAMP_SWING_Y_MIN | CLAMP_SWING_Y_MAX |
			            CLAMP_SWING_Z_MIN | CLAMP_SWING_Z_MAX;
		}
	}

	if (negate_swing)
		q_neg (swing, swing);
	if (negate_twist)
		q_neg (twist, twist);
}

static void
st_deactivate (SwingTwistPart *p)
{
	angle_deactivate (&p->swing_y);
	angle_deactivate (&p->swing_z);
	angle_deactivate (&p->twist);
}

/*
 * st_setup:
 * @q: rotation of B's joint frame in A's joint frame
 * @c_to_world: A's joint frame in world space
 */
static void
st_setup (SwingTwistPart      *p,
          const LrgSolverBody *b1,
          const LrgSolverBody *b2,
          const gfloat        *q,
          const gfloat        *c_to_world)
{
	gfloat swing[4];
	gfloat twist[4];
	gfloat cswing[4];
	gfloat ctwist[4];
	gfloat t2w[4];
	gfloat cur[3];
	gfloat des[3];
	gfloat len;
	guint clamped;

	q_swing_twist (q, swing, twist);
	q_copy (cswing, swing);
	q_copy (ctwist, twist);
	st_clamp (p, cswing, ctwist, &clamped);

	if (p->flags & ST_SWING_Y_LOCKED)
	{
		q_mul (t2w, c_to_world, swing);
		q_axis (p->ws_swing_y_axis, t2w, 1);
		q_axis (p->ws_swing_z_axis, t2w, 2);
		if (p->flags & ST_SWING_Z_LOCKED)
		{
			angle_setup (&p->swing_y, b1, b2, p->ws_swing_y_axis, 0.0f);
			angle_setup (&p->swing_z, b1, b2, p->ws_swing_z_axis, 0.0f);
		}
		else
		{
			angle_setup (&p->swing_y, b1, b2, p->ws_swing_y_axis, 0.0f);
			if (clamped & (CLAMP_SWING_Z_MIN | CLAMP_SWING_Z_MAX))
			{
				if (clamped & CLAMP_SWING_Z_MIN)
					v3_scale (p->ws_swing_z_axis, p->ws_swing_z_axis, -1.0f);
				angle_setup (&p->swing_z, b1, b2, p->ws_swing_z_axis, 0.0f);
			}
			else
				angle_deactivate (&p->swing_z);
		}
	}
	else if (p->flags & ST_SWING_Z_LOCKED)
	{
		q_mul (t2w, c_to_world, swing);
		q_axis (p->ws_swing_y_axis, t2w, 1);
		q_axis (p->ws_swing_z_axis, t2w, 2);
		if (clamped & (CLAMP_SWING_Y_MIN | CLAMP_SWING_Y_MAX))
		{
			if (clamped & CLAMP_SWING_Y_MIN)
				v3_scale (p->ws_swing_y_axis, p->ws_swing_y_axis, -1.0f);
			angle_setup (&p->swing_y, b1, b2, p->ws_swing_y_axis, 0.0f);
		}
		else
			angle_deactivate (&p->swing_y);
		angle_setup (&p->swing_z, b1, b2, p->ws_swing_z_axis, 0.0f);
	}
	else if ((p->flags & ST_SWING_YZ_FREE) != ST_SWING_YZ_FREE)
	{
		/* cone or pyramid: one row that pushes the twist axis back */
		if (clamped & (CLAMP_SWING_Y_MIN | CLAMP_SWING_Y_MAX |
		               CLAMP_SWING_Z_MIN | CLAMP_SWING_Z_MAX))
		{
			q_mul (t2w, c_to_world, swing);
			q_axis (cur, t2w, 0);
			q_mul (t2w, c_to_world, cswing);
			q_axis (des, t2w, 0);
			v3_cross (p->ws_swing_y_axis, des, cur);
			len = v3_length (p->ws_swing_y_axis);
			if (len != 0.0f)
			{
				v3_scale (p->ws_swing_y_axis, p->ws_swing_y_axis, 1.0f / len);
				angle_setup (&p->swing_y, b1, b2, p->ws_swing_y_axis, 0.0f);
			}
			else
				angle_deactivate (&p->swing_y);
		}
		else
			angle_deactivate (&p->swing_y);
		angle_deactivate (&p->swing_z);
	}
	else
	{
		angle_deactivate (&p->swing_y);
		angle_deactivate (&p->swing_z);
	}

	if (p->flags & ST_TWIST_LOCKED)
	{
		q_mul (t2w, c_to_world, swing);
		q_axis (p->ws_twist_axis, t2w, 0);
		angle_setup (&p->twist, b1, b2, p->ws_twist_axis, 0.0f);
	}
	else if ((p->flags & ST_TWIST_FREE) == 0)
	{
		if (clamped & (CLAMP_TWIST_MIN | CLAMP_TWIST_MAX))
		{
			q_mul (t2w, c_to_world, swing);
			q_axis (p->ws_twist_axis, t2w, 0);
			if (clamped & CLAMP_TWIST_MIN)
				v3_scale (p->ws_twist_axis, p->ws_twist_axis, -1.0f);
			angle_setup (&p->twist, b1, b2, p->ws_twist_axis, 0.0f);
		}
		else
			angle_deactivate (&p->twist);
	}
	else
		angle_deactivate (&p->twist);
}

static void
st_warm_start (SwingTwistPart *p, LrgSolverBody *b1, LrgSolverBody *b2, gfloat ratio)
{
	angle_warm_start (&p->swing_y, b1, b2, ratio);
	angle_warm_start (&p->swing_z, b1, b2, ratio);
	angle_warm_start (&p->twist, b1, b2, ratio);
}

static gboolean
st_solve_velocity (SwingTwistPart *p, LrgSolverBody *b1, LrgSolverBody *b2)
{
	gboolean impulse;

	impulse = FALSE;
	if (angle_is_active (&p->swing_y))
		impulse |= angle_solve_velocity (&p->swing_y, b1, b2, p->ws_swing_y_axis, -FLT_MAX,
		                                 p->sin_swing_y_min == p->sin_swing_y_max ? FLT_MAX : 0.0f);
	if (angle_is_active (&p->swing_z))
		impulse |= angle_solve_velocity (&p->swing_z, b1, b2, p->ws_swing_z_axis, -FLT_MAX,
		                                 p->sin_swing_z_min == p->sin_swing_z_max ? FLT_MAX : 0.0f);
	if (angle_is_active (&p->twist))
		impulse |= angle_solve_velocity (&p->twist, b1, b2, p->ws_twist_axis, -FLT_MAX,
		                                 p->sin_twist_min == p->sin_twist_max ? FLT_MAX : 0.0f);
	return impulse;
}

static gboolean
st_solve_position (const SwingTwistPart *p,
                   LrgSolverBody        *b1,
                   LrgSolverBody        *b2,
                   const gfloat         *q,
                   const gfloat         *c_to_b1,
                   const gfloat         *c_to_b2,
                   gfloat                baumgarte)
{
	gfloat swing[4];
	gfloat twist[4];
	gfloat t[4];
	gfloat inv_initial[4];
	guint clamped;
	RotationPart part;

	q_swing_twist (q, swing, twist);
	st_clamp (p, swing, twist, &clamped);
	if (clamped == 0)
		return FALSE;

	/* rotation that would put B on the clamped pose: c2 * (c1 * swing * twist)^-1 */
	q_mul (t, c_to_b1, swing);
	q_mul (t, t, twist);
	q_conj (t, t);
	q_mul (inv_initial, c_to_b2, t);
	memset (&part, 0, sizeof (part));
	return rotation_solve_position (&part, b1, b2, inv_initial, baumgarte);
}

/* ==================================================================
 * LrgJoint
 * ================================================================== */

struct _LrgJoint
{
	LrgPhysicsConstraint parent_instance;

	LrgJointKind kind;

	/* frames: anchor from each body's origin, joint-frame-to-body rotation */
	gfloat local_pos1[3];
	gfloat local_pos2[3];
	gfloat c_to_b1[4];
	gfloat c_to_b2[4];

	/* user configuration per axis */
	LrgJointAxisMode mode[J_N_AXES];
	gfloat user_min[J_N_AXES];
	gfloat user_max[J_N_AXES];
	gfloat spring_frequency[J_N_AXES];
	gfloat spring_damping[J_N_AXES];
	gfloat friction[J_N_AXES];
	LrgJointMotorMode motor[J_N_AXES];
	gfloat motor_max[J_N_AXES];
	gfloat motor_frequency[J_N_AXES];
	gfloat motor_damping[J_N_AXES];
	gfloat target_velocity[J_N_AXES];
	gfloat target_position[J_N_AXES];
	gfloat target_orientation[4];
	LrgJointSwingType swing_type;

	/* distance joint */
	gfloat distance_min;
	gfloat distance_max;

	/* breaking */
	gfloat break_force;
	gfloat break_torque;
	gboolean broken;

	/* derived limits (Jolt mLimitMin / mLimitMax) and axis masks */
	gfloat limit_min[J_N_AXES];
	gfloat limit_max[J_N_AXES];
	guint fixed_axes;
	guint free_axes;
	gboolean translation_motor_active;
	gboolean rotation_motor_active;
	guint rotation_position_motors;

	/* per-substep state */
	gfloat translation_axis[3][3];
	gfloat rotation_axis[3][3];
	gfloat displacement[3];
	gfloat motor_point[3];
	AxisPart translation_part[3];
	AxisPart motor_translation_part[3];
	AnglePart motor_rotation_part[3];
	PointPart point_part;
	RotationPart rotation_part;
	SwingTwistPart swing_twist;

	/* distance joint per-substep state */
	AxisPart distance_part;
	gfloat distance_normal[3];
	gfloat distance_point[3];
	gfloat distance_min_lambda;
	gfloat distance_max_lambda;

	/* last substep feedback */
	gfloat applied_force[3];
	gfloat applied_torque[3];
};

G_DEFINE_FINAL_TYPE (LrgJoint, lrg_joint, LRG_TYPE_PHYSICS_CONSTRAINT)

enum {
	PROP_0,
	PROP_KIND,
	PROP_SWING_TYPE,
	PROP_BREAK_FORCE,
	PROP_BREAK_TORQUE,
	PROP_BROKEN,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum {
	SIGNAL_BROKEN,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_QUARK (lrg-joint-error-quark, lrg_joint_error)

static GType
register_enum (const gchar *name, const GEnumValue *values)
{
	static GMutex lock;
	GType type;

	g_mutex_lock (&lock);
	type = g_type_from_name (name);
	if (type == 0)
		type = g_enum_register_static (name, values);
	g_mutex_unlock (&lock);
	return type;
}

GType
lrg_joint_kind_get_type (void)
{
	static const GEnumValue values[] = {
		{ LRG_JOINT_FIXED, "LRG_JOINT_FIXED", "fixed" },
		{ LRG_JOINT_POINT, "LRG_JOINT_POINT", "point" },
		{ LRG_JOINT_HINGE, "LRG_JOINT_HINGE", "hinge" },
		{ LRG_JOINT_SLIDER, "LRG_JOINT_SLIDER", "slider" },
		{ LRG_JOINT_DISTANCE, "LRG_JOINT_DISTANCE", "distance" },
		{ LRG_JOINT_CONE, "LRG_JOINT_CONE", "cone" },
		{ LRG_JOINT_SWING_TWIST, "LRG_JOINT_SWING_TWIST", "swing-twist" },
		{ LRG_JOINT_SIX_DOF, "LRG_JOINT_SIX_DOF", "six-dof" },
		{ 0, NULL, NULL }
	};

	return register_enum ("LrgJointKind", values);
}

GType
lrg_joint_axis_get_type (void)
{
	static const GEnumValue values[] = {
		{ LRG_JOINT_AXIS_TRANSLATION_X, "LRG_JOINT_AXIS_TRANSLATION_X", "translation-x" },
		{ LRG_JOINT_AXIS_TRANSLATION_Y, "LRG_JOINT_AXIS_TRANSLATION_Y", "translation-y" },
		{ LRG_JOINT_AXIS_TRANSLATION_Z, "LRG_JOINT_AXIS_TRANSLATION_Z", "translation-z" },
		{ LRG_JOINT_AXIS_ROTATION_X, "LRG_JOINT_AXIS_ROTATION_X", "rotation-x" },
		{ LRG_JOINT_AXIS_ROTATION_Y, "LRG_JOINT_AXIS_ROTATION_Y", "rotation-y" },
		{ LRG_JOINT_AXIS_ROTATION_Z, "LRG_JOINT_AXIS_ROTATION_Z", "rotation-z" },
		{ 0, NULL, NULL }
	};

	return register_enum ("LrgJointAxis", values);
}

GType
lrg_joint_axis_mode_get_type (void)
{
	static const GEnumValue values[] = {
		{ LRG_JOINT_AXIS_FREE, "LRG_JOINT_AXIS_FREE", "free" },
		{ LRG_JOINT_AXIS_LOCKED, "LRG_JOINT_AXIS_LOCKED", "locked" },
		{ LRG_JOINT_AXIS_LIMITED, "LRG_JOINT_AXIS_LIMITED", "limited" },
		{ 0, NULL, NULL }
	};

	return register_enum ("LrgJointAxisMode", values);
}

GType
lrg_joint_motor_mode_get_type (void)
{
	static const GEnumValue values[] = {
		{ LRG_JOINT_MOTOR_OFF, "LRG_JOINT_MOTOR_OFF", "off" },
		{ LRG_JOINT_MOTOR_VELOCITY, "LRG_JOINT_MOTOR_VELOCITY", "velocity" },
		{ LRG_JOINT_MOTOR_POSITION, "LRG_JOINT_MOTOR_POSITION", "position" },
		{ 0, NULL, NULL }
	};

	return register_enum ("LrgJointMotorMode", values);
}

GType
lrg_joint_swing_type_get_type (void)
{
	static const GEnumValue values[] = {
		{ LRG_JOINT_SWING_CONE, "LRG_JOINT_SWING_CONE", "cone" },
		{ LRG_JOINT_SWING_PYRAMID, "LRG_JOINT_SWING_PYRAMID", "pyramid" },
		{ 0, NULL, NULL }
	};

	return register_enum ("LrgJointSwingType", values);
}

/* ------------------------------------------------------------------
 * Limits bookkeeping
 * ------------------------------------------------------------------ */

static inline gboolean
is_fixed (const LrgJoint *self, guint axis)
{
	return (self->fixed_axes & (1u << axis)) != 0;
}

static inline gboolean
is_free (const LrgJoint *self, guint axis)
{
	return (self->free_axes & (1u << axis)) != 0;
}

static inline gboolean
translation_fully_constrained (const LrgJoint *self)
{
	return (self->fixed_axes & 0x7u) == 0x7u;
}

static inline gboolean
translation_constrained (const LrgJoint *self)
{
	return (self->free_axes & 0x7u) != 0x7u;
}

static inline gboolean
rotation_fully_constrained (const LrgJoint *self)
{
	return (self->fixed_axes & 0x38u) == 0x38u;
}

static inline gboolean
rotation_constrained (const LrgJoint *self)
{
	return (self->free_axes & 0x38u) != 0x38u;
}

static void
deactivate_all (LrgJoint *self)
{
	guint i;

	for (i = 0; i < 3; i++)
	{
		axis_deactivate (&self->translation_part[i]);
		axis_deactivate (&self->motor_translation_part[i]);
		angle_deactivate (&self->motor_rotation_part[i]);
	}
	point_deactivate (&self->point_part);
	rotation_deactivate (&self->rotation_part);
	st_deactivate (&self->swing_twist);
	axis_deactivate (&self->distance_part);
}

/*
 * update_limits:
 * Turns the user axis modes and limits into Jolt's limit arrays and the
 * fixed / free masks, and reconfigures the swing-twist part. Every
 * configuration setter calls it. When the masks change the accumulated
 * impulses no longer mean anything and are dropped.
 */
static void
update_limits (LrgJoint *self)
{
	guint old_fixed;
	guint old_free;
	guint a;
	gfloat lim;

	for (a = 0; a < J_N_AXES; a++)
	{
		lim = a >= 3 ? J_PI : FLT_MAX;
		switch (self->mode[a])
		{
		case LRG_JOINT_AXIS_FREE:
			self->limit_min[a] = -lim;
			self->limit_max[a] = lim;
			break;
		case LRG_JOINT_AXIS_LOCKED:
			self->limit_min[a] = self->user_min[a];
			self->limit_max[a] = self->user_min[a];
			break;
		case LRG_JOINT_AXIS_LIMITED:
		default:
			self->limit_min[a] = self->user_min[a];
			self->limit_max[a] = self->user_max[a];
			break;
		}
	}

	/* a cone swing limit is symmetric: the maximum is mirrored */
	if (self->swing_type == LRG_JOINT_SWING_CONE)
	{
		for (a = 4; a <= 5; a++)
			if (self->mode[a] != LRG_JOINT_AXIS_FREE)
			{
				self->limit_max[a] = MAX (0.0f, MAX (self->limit_max[a], -self->limit_min[a]));
				if (self->mode[a] == LRG_JOINT_AXIS_LOCKED)
					self->limit_max[a] = 0.0f;
				self->limit_min[a] = -self->limit_max[a];
			}
	}
	for (a = 3; a < J_N_AXES; a++)
	{
		self->limit_min[a] = CLAMP (self->limit_min[a], -J_PI, J_PI);
		self->limit_max[a] = CLAMP (self->limit_max[a], -J_PI, J_PI);
		if (self->limit_min[a] > self->limit_max[a])
			self->limit_min[a] = self->limit_max[a] = 0.0f;
	}
	for (a = 0; a < 3; a++)
		if (self->limit_min[a] > self->limit_max[a])
			self->limit_min[a] = self->limit_max[a] = 0.0f;

	self->swing_twist.swing_type = self->swing_type;
	st_set_limits (&self->swing_twist,
	               self->limit_min[3], self->limit_max[3],
	               self->limit_min[4], self->limit_max[4],
	               self->limit_min[5], self->limit_max[5]);

	old_fixed = self->fixed_axes;
	old_free = self->free_axes;
	self->fixed_axes = 0;
	self->free_axes = 0;
	for (a = 0; a < J_N_AXES; a++)
	{
		lim = a >= 3 ? J_PI : FLT_MAX;
		if (self->limit_min[a] >= self->limit_max[a])
			self->fixed_axes |= 1u << a;
		else if (self->limit_min[a] <= -lim && self->limit_max[a] >= lim)
			self->free_axes |= 1u << a;
	}
	if (old_fixed != self->fixed_axes || old_free != self->free_axes)
		deactivate_all (self);
}

/* motor/friction caches (Jolt CacheTranslationMotorActive & co) */
static void
update_motors (LrgJoint *self)
{
	guint i;

	self->translation_motor_active = FALSE;
	self->rotation_motor_active = FALSE;
	self->rotation_position_motors = 0;
	for (i = 0; i < 3; i++)
	{
		if (self->motor[i] != LRG_JOINT_MOTOR_OFF || self->friction[i] > 0.0f)
			self->translation_motor_active = TRUE;
		if (self->motor[i + 3] != LRG_JOINT_MOTOR_OFF || self->friction[i + 3] > 0.0f)
			self->rotation_motor_active = TRUE;
		if (self->motor[i + 3] == LRG_JOINT_MOTOR_POSITION)
			self->rotation_position_motors |= 1u << i;
	}
}

/* initial axis modes and limits of a kind */
static void
apply_kind_preset (LrgJoint *self)
{
	LrgJointAxisMode t;
	LrgJointAxisMode r;
	guint a;

	t = LRG_JOINT_AXIS_LOCKED;
	r = LRG_JOINT_AXIS_LOCKED;
	switch (self->kind)
	{
	case LRG_JOINT_POINT:
		r = LRG_JOINT_AXIS_FREE;
		break;
	case LRG_JOINT_SIX_DOF:
	case LRG_JOINT_DISTANCE:
		t = LRG_JOINT_AXIS_FREE;
		r = LRG_JOINT_AXIS_FREE;
		break;
	case LRG_JOINT_FIXED:
	case LRG_JOINT_HINGE:
	case LRG_JOINT_SLIDER:
	case LRG_JOINT_CONE:
	case LRG_JOINT_SWING_TWIST:
	default:
		break;
	}
	for (a = 0; a < 3; a++)
		self->mode[a] = t;
	for (a = 3; a < J_N_AXES; a++)
		self->mode[a] = r;

	switch (self->kind)
	{
	case LRG_JOINT_HINGE:
		self->mode[LRG_JOINT_AXIS_ROTATION_X] = LRG_JOINT_AXIS_FREE;
		break;
	case LRG_JOINT_SLIDER:
		self->mode[LRG_JOINT_AXIS_TRANSLATION_X] = LRG_JOINT_AXIS_FREE;
		break;
	case LRG_JOINT_CONE:
		self->mode[LRG_JOINT_AXIS_ROTATION_X] = LRG_JOINT_AXIS_FREE;
		self->mode[LRG_JOINT_AXIS_ROTATION_Y] = LRG_JOINT_AXIS_LIMITED;
		self->mode[LRG_JOINT_AXIS_ROTATION_Z] = LRG_JOINT_AXIS_LIMITED;
		self->user_min[4] = self->user_min[5] = -0.25f * J_PI;
		self->user_max[4] = self->user_max[5] = 0.25f * J_PI;
		break;
	case LRG_JOINT_SWING_TWIST:
		for (a = 3; a < J_N_AXES; a++)
		{
			self->mode[a] = LRG_JOINT_AXIS_LIMITED;
			self->user_min[a] = -0.25f * J_PI;
			self->user_max[a] = 0.25f * J_PI;
		}
		break;
	case LRG_JOINT_FIXED:
	case LRG_JOINT_POINT:
	case LRG_JOINT_DISTANCE:
	case LRG_JOINT_SIX_DOF:
	default:
		break;
	}
}

/* ------------------------------------------------------------------
 * 6-DOF solver flow (Jolt SixDOFConstraint)
 * ------------------------------------------------------------------ */

/* p1, p2 world anchors; r1_plus_u = p2 - x1, r2 = p2 - x2, u = p2 - p1 */
static void
position_properties (const LrgJoint      *self,
                     const LrgSolverBody *b1,
                     const LrgSolverBody *b2,
                     gfloat              *r1_plus_u,
                     gfloat              *r2,
                     gfloat              *u,
                     gfloat              *p2)
{
	gfloat p1[3];

	body_point (b1, self->local_pos1, p1);
	body_point (b2, self->local_pos2, p2);
	v3_sub (r1_plus_u, p2, b1->position);
	v3_sub (r2, p2, b2->position);
	v3_sub (u, p2, p1);
}

/* rotation of B's joint frame in A's joint frame */
static void
rotation_in_constraint_space (const LrgJoint      *self,
                              const LrgSolverBody *b1,
                              const LrgSolverBody *b2,
                              gfloat              *q,
                              gfloat              *c1_world)
{
	gfloat c2_world[4];
	gfloat inv[4];

	body_frame (b1, self->c_to_b1, c1_world);
	body_frame (b2, self->c_to_b2, c2_world);
	q_conj (inv, c1_world);
	q_mul (q, inv, c2_world);
}

static void
six_dof_setup (LrgJoint      *self,
               LrgSolverBody *b1,
               LrgSolverBody *b2,
               gfloat         dt)
{
	gfloat c1_world[4];
	gfloat c2_world[4];
	gfloat r1_plus_u[3];
	gfloat r2[3];
	gfloat u[3];
	gfloat q[4];
	guint i;

	body_frame (b1, self->c_to_b1, c1_world);
	for (i = 0; i < 3; i++)
		q_axis (self->translation_axis[i], c1_world, i);

	/* translation rows */
	if (translation_fully_constrained (self))
	{
		gfloat local1[3];
		gfloat off[3];
		gfloat lim[3];

		/* a lock at a non-zero position moves the anchor on body A */
		v3_set (lim, self->limit_min[0], self->limit_min[1], self->limit_min[2]);
		q_rotate (off, self->c_to_b1, lim);
		v3_add (local1, self->local_pos1, off);
		point_setup (&self->point_part, b1, local1, b2, self->local_pos2);
	}
	else if (translation_constrained (self) || self->translation_motor_active)
	{
		position_properties (self, b1, b2, r1_plus_u, r2, u, self->motor_point);
		for (i = 0; i < 3; i++)
		{
			const gfloat *axis = self->translation_axis[i];
			gfloat d;
			gboolean active;
			gfloat c;

			d = v3_dot (axis, u);
			self->displacement[i] = d;
			active = FALSE;
			c = 0.0f;
			if (is_fixed (self, i))
			{
				c = d - self->limit_min[i];
				active = TRUE;
			}
			else if (!is_free (self, i))
			{
				if (d <= self->limit_min[i])
				{
					c = d - self->limit_min[i];
					active = TRUE;
				}
				else if (d >= self->limit_max[i])
				{
					c = d - self->limit_max[i];
					active = TRUE;
				}
			}
			if (active)
				axis_setup_spring (&self->translation_part[i], dt, b1, r1_plus_u, b2, r2, axis,
				                   0.0f, c, self->spring_frequency[i], self->spring_damping[i]);
			else
				axis_deactivate (&self->translation_part[i]);

			switch (self->motor[i])
			{
			case LRG_JOINT_MOTOR_VELOCITY:
				axis_setup (&self->motor_translation_part[i], b1, r1_plus_u, b2, r2, axis,
				            -self->target_velocity[i]);
				break;
			case LRG_JOINT_MOTOR_POSITION:
				axis_setup_spring (&self->motor_translation_part[i], dt, b1, r1_plus_u, b2, r2,
				                   axis, 0.0f, d - self->target_position[i],
				                   self->motor_frequency[i], self->motor_damping[i]);
				break;
			case LRG_JOINT_MOTOR_OFF:
			default:
				if (self->friction[i] > 0.0f)
					axis_setup (&self->motor_translation_part[i], b1, r1_plus_u, b2, r2, axis, 0.0f);
				else
					axis_deactivate (&self->motor_translation_part[i]);
				break;
			}
		}
	}

	/* rotation rows */
	if (rotation_fully_constrained (self))
		rotation_setup (&self->rotation_part, b1, b2);
	else if (rotation_constrained (self) || self->rotation_motor_active)
	{
		rotation_in_constraint_space (self, b1, b2, q, c1_world);
		if (rotation_constrained (self))
			st_setup (&self->swing_twist, b1, b2, q, c1_world);
		else
			st_deactivate (&self->swing_twist);

		if (self->rotation_motor_active)
		{
			gfloat target[4];
			gfloat diff[4];
			gfloat proj[4];
			gfloat tw[4];
			gfloat inv[4];
			gfloat ex[3] = { 1.0f, 0.0f, 0.0f };
			gfloat ey[3] = { 0.0f, 1.0f, 0.0f };
			gfloat ez[3] = { 0.0f, 0.0f, 1.0f };
			gfloat err[3];

			body_frame (b2, self->c_to_b2, c2_world);
			for (i = 0; i < 3; i++)
				q_axis (self->rotation_axis[i], c2_world, i);

			/* error to the target, projected onto the axes with a position motor */
			if (q_dot (q, self->target_orientation) > 0.0f)
				q_copy (target, self->target_orientation);
			else
				q_neg (target, self->target_orientation);
			q_conj (inv, q);
			q_mul (diff, inv, target);
			switch (self->rotation_position_motors)
			{
			case 1u:
				q_get_twist (proj, diff, ex);
				break;
			case 2u:
				q_get_twist (proj, diff, ey);
				break;
			case 4u:
				q_get_twist (proj, diff, ez);
				break;
			case 3u:
				q_get_twist (tw, diff, ez);
				q_conj (tw, tw);
				q_mul (proj, diff, tw);
				break;
			case 5u:
				q_get_twist (tw, diff, ey);
				q_conj (tw, tw);
				q_mul (proj, diff, tw);
				break;
			case 6u:
				q_get_twist (tw, diff, ex);
				q_conj (tw, tw);
				q_mul (proj, diff, tw);
				break;
			default:
				q_copy (proj, diff);
				break;
			}
			v3_scale (err, proj, -2.0f);

			for (i = 0; i < 3; i++)
			{
				guint a = i + 3;
				const gfloat *axis = self->rotation_axis[i];

				switch (self->motor[a])
				{
				case LRG_JOINT_MOTOR_VELOCITY:
					angle_setup (&self->motor_rotation_part[i], b1, b2, axis, -self->target_velocity[a]);
					break;
				case LRG_JOINT_MOTOR_POSITION:
					angle_setup_spring (&self->motor_rotation_part[i], dt, b1, b2, axis, 0.0f, err[i],
					                    self->motor_frequency[a], self->motor_damping[a]);
					break;
				case LRG_JOINT_MOTOR_OFF:
				default:
					if (self->friction[a] > 0.0f)
						angle_setup (&self->motor_rotation_part[i], b1, b2, axis, 0.0f);
					else
						angle_deactivate (&self->motor_rotation_part[i]);
					break;
				}
			}
		}
	}
}

static void
six_dof_warm_start (LrgJoint *self, LrgSolverBody *b1, LrgSolverBody *b2, gfloat ratio)
{
	guint i;

	if (self->translation_motor_active)
		for (i = 0; i < 3; i++)
			if (axis_is_active (&self->motor_translation_part[i]))
				axis_warm_start (&self->motor_translation_part[i], b1, b2, self->translation_axis[i], ratio);
	if (self->rotation_motor_active)
		for (i = 0; i < 3; i++)
			if (angle_is_active (&self->motor_rotation_part[i]))
				angle_warm_start (&self->motor_rotation_part[i], b1, b2, ratio);
	if (rotation_fully_constrained (self))
	{
		if (self->rotation_part.active)
			rotation_warm_start (&self->rotation_part, b1, b2, ratio);
	}
	else if (rotation_constrained (self))
		st_warm_start (&self->swing_twist, b1, b2, ratio);
	if (translation_fully_constrained (self))
	{
		if (self->point_part.active)
			point_warm_start (&self->point_part, b1, b2, ratio);
	}
	else if (translation_constrained (self))
		for (i = 0; i < 3; i++)
			if (axis_is_active (&self->translation_part[i]))
				axis_warm_start (&self->translation_part[i], b1, b2, self->translation_axis[i], ratio);
}

static gboolean
six_dof_solve_velocity (LrgJoint *self, LrgSolverBody *b1, LrgSolverBody *b2, gfloat dt)
{
	gboolean impulse;
	gfloat cap;
	guint i;

	impulse = FALSE;

	/* motors and friction first, so the limits have the last word */
	if (self->translation_motor_active)
		for (i = 0; i < 3; i++)
			if (axis_is_active (&self->motor_translation_part[i]))
			{
				cap = (self->motor[i] == LRG_JOINT_MOTOR_OFF ? self->friction[i] : self->motor_max[i]) * dt;
				impulse |= axis_solve_velocity (&self->motor_translation_part[i], b1, b2,
				                                self->translation_axis[i], -cap, cap);
			}
	if (self->rotation_motor_active)
		for (i = 0; i < 3; i++)
			if (angle_is_active (&self->motor_rotation_part[i]))
			{
				cap = (self->motor[i + 3] == LRG_JOINT_MOTOR_OFF ? self->friction[i + 3] : self->motor_max[i + 3]) * dt;
				impulse |= angle_solve_velocity (&self->motor_rotation_part[i], b1, b2,
				                                 self->rotation_axis[i], -cap, cap);
			}

	if (rotation_fully_constrained (self))
	{
		if (self->rotation_part.active)
			impulse |= rotation_solve_velocity (&self->rotation_part, b1, b2);
	}
	else if (rotation_constrained (self))
		impulse |= st_solve_velocity (&self->swing_twist, b1, b2);

	if (translation_fully_constrained (self))
	{
		if (self->point_part.active)
			impulse |= point_solve_velocity (&self->point_part, b1, b2);
	}
	else if (translation_constrained (self))
		for (i = 0; i < 3; i++)
			if (axis_is_active (&self->translation_part[i]))
			{
				gfloat lo = -FLT_MAX;
				gfloat hi = FLT_MAX;

				if (!is_fixed (self, i))
				{
					if (self->displacement[i] <= self->limit_min[i])
						lo = 0.0f;
					else if (self->displacement[i] >= self->limit_max[i])
						hi = 0.0f;
				}
				impulse |= axis_solve_velocity (&self->translation_part[i], b1, b2,
				                                self->translation_axis[i], lo, hi);
			}
	return impulse;
}

static gboolean
six_dof_solve_position (LrgJoint *self, LrgSolverBody *b1, LrgSolverBody *b2, gfloat baumgarte)
{
	gboolean impulse;
	guint i;

	impulse = FALSE;
	if (rotation_fully_constrained (self))
	{
		gfloat lim[3];
		gfloat e[4];
		gfloat c1[4];
		gfloat inv_initial[4];

		v3_set (lim, self->limit_min[3], self->limit_min[4], self->limit_min[5]);
		q_from_euler (e, lim);
		q_mul (c1, self->c_to_b1, e);
		q_conj (c1, c1);
		q_mul (inv_initial, self->c_to_b2, c1);
		impulse |= rotation_solve_position (&self->rotation_part, b1, b2, inv_initial, baumgarte);
	}
	else if (rotation_constrained (self))
	{
		gfloat q[4];
		gfloat c1_world[4];

		rotation_in_constraint_space (self, b1, b2, q, c1_world);
		impulse |= st_solve_position (&self->swing_twist, b1, b2, q, self->c_to_b1, self->c_to_b2,
		                              baumgarte);
	}

	if (translation_fully_constrained (self))
	{
		gfloat local1[3];
		gfloat off[3];
		gfloat lim[3];

		v3_set (lim, self->limit_min[0], self->limit_min[1], self->limit_min[2]);
		q_rotate (off, self->c_to_b1, lim);
		v3_add (local1, self->local_pos1, off);
		point_setup (&self->point_part, b1, local1, b2, self->local_pos2);
		impulse |= point_solve_position (&self->point_part, b1, b2, baumgarte);
	}
	else if (translation_constrained (self))
	{
		for (i = 0; i < 3; i++)
		{
			gfloat r1_plus_u[3];
			gfloat r2[3];
			gfloat u[3];
			gfloat p2[3];
			gfloat c1_world[4];
			gfloat axis[3];
			gfloat d;
			gfloat error;
			AxisPart part;

			if (self->spring_frequency[i] > 0.0f || is_free (self, i))
				continue;
			position_properties (self, b1, b2, r1_plus_u, r2, u, p2);
			body_frame (b1, self->c_to_b1, c1_world);
			q_axis (axis, c1_world, i);
			d = v3_dot (u, axis);
			error = 0.0f;
			if (is_fixed (self, i))
				error = d - self->limit_min[i];
			else if (d <= self->limit_min[i])
				error = d - self->limit_min[i];
			else if (d >= self->limit_max[i])
				error = d - self->limit_max[i];
			if (error != 0.0f)
			{
				memset (&part, 0, sizeof (part));
				axis_setup (&part, b1, r1_plus_u, b2, r2, axis, 0.0f);
				impulse |= axis_solve_position (&part, b1, b2, axis, p2, error, baumgarte);
			}
		}
	}
	return impulse;
}

/* ------------------------------------------------------------------
 * Distance joint (Jolt DistanceConstraint)
 * ------------------------------------------------------------------ */

static void
distance_properties (LrgJoint *self, LrgSolverBody *b1, LrgSolverBody *b2, gfloat dt, gfloat *out_len)
{
	gfloat p1[3];
	gfloat delta[3];
	gfloat r1_plus_u[3];
	gfloat r2[3];
	gfloat len;
	gfloat c;

	body_point (b1, self->local_pos1, p1);
	body_point (b2, self->local_pos2, self->distance_point);
	v3_sub (delta, self->distance_point, p1);
	len = v3_length (delta);
	if (len > 0.0f)
		v3_scale (self->distance_normal, delta, 1.0f / len);
	else if (v3_is_zero (self->distance_normal))
		v3_set (self->distance_normal, 0.0f, 1.0f, 0.0f);
	v3_sub (r1_plus_u, self->distance_point, b1->position);
	v3_sub (r2, self->distance_point, b2->position);
	*out_len = len;

	if (self->distance_min == self->distance_max)
	{
		c = len - self->distance_min;
		self->distance_min_lambda = -FLT_MAX;
		self->distance_max_lambda = FLT_MAX;
	}
	else if (len <= self->distance_min)
	{
		c = len - self->distance_min;
		self->distance_min_lambda = 0.0f;
		self->distance_max_lambda = FLT_MAX;
	}
	else if (len >= self->distance_max)
	{
		c = len - self->distance_max;
		self->distance_min_lambda = -FLT_MAX;
		self->distance_max_lambda = 0.0f;
	}
	else
	{
		axis_deactivate (&self->distance_part);
		return;
	}
	axis_setup_spring (&self->distance_part, dt, b1, r1_plus_u, b2, r2, self->distance_normal,
	                   0.0f, c, self->spring_frequency[0], self->spring_damping[0]);
}

/* ------------------------------------------------------------------
 * LrgPhysicsConstraint vfuncs
 * ------------------------------------------------------------------ */

static void
lrg_joint_setup (LrgPhysicsConstraint *constraint,
                 LrgSolverBody        *a,
                 LrgSolverBody        *b,
                 gfloat                dt)
{
	LrgJoint *self = LRG_JOINT (constraint);
	gfloat len;

	if (self->kind == LRG_JOINT_DISTANCE)
		distance_properties (self, a, b, dt, &len);
	else
		six_dof_setup (self, a, b, dt);
}

static void
lrg_joint_warm_start (LrgPhysicsConstraint *constraint,
                      LrgSolverBody        *a,
                      LrgSolverBody        *b,
                      gfloat                ratio)
{
	LrgJoint *self = LRG_JOINT (constraint);

	if (self->kind == LRG_JOINT_DISTANCE)
	{
		if (axis_is_active (&self->distance_part))
			axis_warm_start (&self->distance_part, a, b, self->distance_normal, ratio);
	}
	else
		six_dof_warm_start (self, a, b, ratio);
}

static gboolean
lrg_joint_solve_velocity (LrgPhysicsConstraint *constraint,
                          LrgSolverBody        *a,
                          LrgSolverBody        *b,
                          gfloat                dt)
{
	LrgJoint *self = LRG_JOINT (constraint);

	if (self->kind == LRG_JOINT_DISTANCE)
	{
		if (!axis_is_active (&self->distance_part))
			return FALSE;
		return axis_solve_velocity (&self->distance_part, a, b, self->distance_normal,
		                            self->distance_min_lambda, self->distance_max_lambda);
	}
	return six_dof_solve_velocity (self, a, b, dt);
}

static gboolean
lrg_joint_solve_position (LrgPhysicsConstraint *constraint,
                          LrgSolverBody        *a,
                          LrgSolverBody        *b,
                          gfloat                dt,
                          gfloat                baumgarte)
{
	LrgJoint *self = LRG_JOINT (constraint);

	if (self->kind == LRG_JOINT_DISTANCE)
	{
		gfloat p1[3];
		gfloat p2[3];
		gfloat delta[3];
		gfloat distance;
		gfloat error;
		gfloat len;

		if (self->spring_frequency[0] > 0.0f)
			return FALSE;
		body_point (a, self->local_pos1, p1);
		body_point (b, self->local_pos2, p2);
		v3_sub (delta, p2, p1);
		distance = v3_dot (delta, self->distance_normal);
		error = 0.0f;
		if (distance < self->distance_min)
			error = distance - self->distance_min;
		else if (distance > self->distance_max)
			error = distance - self->distance_max;
		if (error == 0.0f)
			return FALSE;
		distance_properties (self, a, b, dt, &len);
		if (!axis_is_active (&self->distance_part))
			return FALSE;
		return axis_solve_position (&self->distance_part, a, b, self->distance_normal,
		                            self->distance_point, error, baumgarte);
	}
	return six_dof_solve_position (self, a, b, baumgarte);
}

/*
 * lrg_joint_post_solve:
 * Sums the accumulated impulses of every row into the force and torque
 * on body B, then breaks the joint when either exceeds its threshold.
 */
static void
lrg_joint_post_solve (LrgPhysicsConstraint *constraint,
                      LrgSolverBody        *a,
                      LrgSolverBody        *b,
                      gfloat                dt)
{
	LrgJoint *self = LRG_JOINT (constraint);
	gfloat f[3] = { 0.0f, 0.0f, 0.0f };
	gfloat t[3] = { 0.0f, 0.0f, 0.0f };
	gfloat fm;
	gfloat tm;
	guint i;

	if (dt <= 0.0f)
		return;
	if (self->kind == LRG_JOINT_DISTANCE)
		v3_madd (f, self->distance_normal, self->distance_part.total_lambda);
	else
	{
		SwingTwistPart *st = &self->swing_twist;

		if (translation_fully_constrained (self))
			v3_add (f, f, self->point_part.total_lambda);
		else
			for (i = 0; i < 3; i++)
				v3_madd (f, self->translation_axis[i], self->translation_part[i].total_lambda);
		for (i = 0; i < 3; i++)
		{
			v3_madd (f, self->translation_axis[i], self->motor_translation_part[i].total_lambda);
			v3_madd (t, self->rotation_axis[i], self->motor_rotation_part[i].total_lambda);
		}
		if (rotation_fully_constrained (self))
			v3_add (t, t, self->rotation_part.total_lambda);
		else
		{
			v3_madd (t, st->ws_swing_y_axis, st->swing_y.total_lambda);
			v3_madd (t, st->ws_swing_z_axis, st->swing_z.total_lambda);
			v3_madd (t, st->ws_twist_axis, st->twist.total_lambda);
		}
	}
	v3_scale (self->applied_force, f, 1.0f / dt);
	v3_scale (self->applied_torque, t, 1.0f / dt);

	if (self->broken)
		return;
	fm = v3_length (self->applied_force);
	tm = v3_length (self->applied_torque);
	if ((self->break_force > 0.0f && fm > self->break_force) ||
	    (self->break_torque > 0.0f && tm > self->break_torque))
	{
		self->broken = TRUE;
		deactivate_all (self);
		lrg_physics_constraint_set_enabled (constraint, FALSE);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BROKEN]);
		g_signal_emit (self, signals[SIGNAL_BROKEN], 0, fm, tm);
	}
}

static void
lrg_joint_reset_warm_start (LrgPhysicsConstraint *constraint)
{
	deactivate_all (LRG_JOINT (constraint));
}

static void
lrg_joint_debug_draw (LrgPhysicsConstraint *constraint,
                      const LrgSolverBody  *a,
                      const LrgSolverBody  *b,
                      LrgDebugDraw         *draw)
{
	LrgJoint *self = LRG_JOINT (constraint);
	GrlColor line = { 255, 200, 64, 255 };
	GrlColor axis_color = { 255, 80, 80, 255 };
	gfloat p1[3];
	gfloat p2[3];
	gfloat c1[4];
	gfloat ax[3];
	GrlVector3 v1;
	GrlVector3 v2;
	GrlVector3 v3;

	body_point (a, self->local_pos1, p1);
	body_point (b, self->local_pos2, p2);

	/* body centres to the anchors */
	v1.x = a->position[0];
	v1.y = a->position[1];
	v1.z = a->position[2];
	v2.x = p1[0];
	v2.y = p1[1];
	v2.z = p1[2];
	lrg_debug_draw_line (draw, &v1, &v2, &line, 0.0f);
	v1.x = b->position[0];
	v1.y = b->position[1];
	v1.z = b->position[2];
	v3.x = p2[0];
	v3.y = p2[1];
	v3.z = p2[2];
	lrg_debug_draw_line (draw, &v1, &v3, &line, 0.0f);
	if (self->kind == LRG_JOINT_DISTANCE)
	{
		lrg_debug_draw_line (draw, &v2, &v3, &axis_color, 0.0f);
		return;
	}

	/* the joint X axis on body A (hinge / slider / twist axis) */
	body_frame (a, self->c_to_b1, c1);
	q_axis (ax, c1, 0);
	v1.x = p1[0] + 0.25f * ax[0];
	v1.y = p1[1] + 0.25f * ax[1];
	v1.z = p1[2] + 0.25f * ax[2];
	lrg_debug_draw_line (draw, &v2, &v1, &axis_color, 0.0f);
}

/* ------------------------------------------------------------------
 * GObject
 * ------------------------------------------------------------------ */

static void
lrg_joint_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgJoint *self = LRG_JOINT (object);

	switch (prop_id)
	{
	case PROP_KIND:
		g_value_set_enum (value, self->kind);
		break;
	case PROP_SWING_TYPE:
		g_value_set_enum (value, self->swing_type);
		break;
	case PROP_BREAK_FORCE:
		g_value_set_float (value, self->break_force);
		break;
	case PROP_BREAK_TORQUE:
		g_value_set_float (value, self->break_torque);
		break;
	case PROP_BROKEN:
		g_value_set_boolean (value, self->broken);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_joint_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgJoint *self = LRG_JOINT (object);

	switch (prop_id)
	{
	case PROP_KIND:
		self->kind = (LrgJointKind) g_value_get_enum (value);
		apply_kind_preset (self);
		update_limits (self);
		break;
	case PROP_SWING_TYPE:
		lrg_joint_set_swing_type (self, (LrgJointSwingType) g_value_get_enum (value));
		break;
	case PROP_BREAK_FORCE:
		lrg_joint_set_break_force (self, g_value_get_float (value));
		break;
	case PROP_BREAK_TORQUE:
		lrg_joint_set_break_torque (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_joint_class_init (LrgJointClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgPhysicsConstraintClass *constraint_class = LRG_PHYSICS_CONSTRAINT_CLASS (klass);

	object_class->get_property = lrg_joint_get_property;
	object_class->set_property = lrg_joint_set_property;

	constraint_class->setup = lrg_joint_setup;
	constraint_class->warm_start = lrg_joint_warm_start;
	constraint_class->solve_velocity = lrg_joint_solve_velocity;
	constraint_class->solve_position = lrg_joint_solve_position;
	constraint_class->post_solve = lrg_joint_post_solve;
	constraint_class->reset_warm_start = lrg_joint_reset_warm_start;
	constraint_class->debug_draw = lrg_joint_debug_draw;

	/**
	 * LrgJoint:kind:
	 *
	 * The joint kind; fixed at construction.
	 */
	properties[PROP_KIND] =
		g_param_spec_enum ("kind", "Kind", "The joint kind",
		                   LRG_TYPE_JOINT_KIND, LRG_JOINT_SIX_DOF,
		                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
		                   G_PARAM_STATIC_STRINGS);

	/**
	 * LrgJoint:swing-type:
	 *
	 * How limited swing Y and Z combine (cone or pyramid).
	 */
	properties[PROP_SWING_TYPE] =
		g_param_spec_enum ("swing-type", "Swing type", "Cone or pyramid swing limit",
		                   LRG_TYPE_JOINT_SWING_TYPE, LRG_JOINT_SWING_CONE,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
		                   G_PARAM_STATIC_STRINGS);

	/**
	 * LrgJoint:break-force:
	 *
	 * Force in N above which the joint breaks; zero never breaks.
	 */
	properties[PROP_BREAK_FORCE] =
		g_param_spec_float ("break-force", "Break force", "Force that breaks the joint",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgJoint:break-torque:
	 *
	 * Torque in N m above which the joint breaks; zero never breaks.
	 */
	properties[PROP_BREAK_TORQUE] =
		g_param_spec_float ("break-torque", "Break torque", "Torque that breaks the joint",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgJoint:broken:
	 *
	 * Whether the joint broke. Cleared by lrg_joint_repair().
	 */
	properties[PROP_BROKEN] =
		g_param_spec_boolean ("broken", "Broken", "Whether the joint broke",
		                      FALSE,
		                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY |
		                      G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgJoint::broken:
	 * @self: the joint
	 * @force: magnitude of the force (N) that was applied
	 * @torque: magnitude of the torque (N m) that was applied
	 *
	 * Emitted once, from inside lrg_dynamics_world_step() (the joint's
	 * post-solve), when the joint's force or torque exceeded its break
	 * threshold. The joint is already disabled. Handlers may read the
	 * world and change joints and bodies' properties, but must not add
	 * or remove bodies or constraints until the step returns.
	 */
	signals[SIGNAL_BROKEN] =
		g_signal_new ("broken", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_FLOAT, G_TYPE_FLOAT);
}

static void
lrg_joint_init (LrgJoint *self)
{
	guint a;

	self->kind = LRG_JOINT_SIX_DOF;
	q_identity (self->c_to_b1);
	q_identity (self->c_to_b2);
	q_identity (self->target_orientation);
	self->swing_type = LRG_JOINT_SWING_CONE;
	for (a = 0; a < J_N_AXES; a++)
	{
		self->motor_frequency[a] = J_DEFAULT_MOTOR_FREQUENCY;
		self->motor_damping[a] = J_DEFAULT_MOTOR_DAMPING;
		self->mode[a] = LRG_JOINT_AXIS_FREE;
	}
	/* force the first update_limits to compute masks */
	self->fixed_axes = G_MAXUINT;
	update_limits (self);
	update_motors (self);
}

/* ------------------------------------------------------------------
 * Construction
 * ------------------------------------------------------------------ */

LrgJoint *
lrg_joint_new (LrgJointKind  kind,
               guint         body_a,
               guint         body_b,
               const gfloat *position_a,
               const gfloat *rotation_a,
               const gfloat *position_b,
               const gfloat *rotation_b)
{
	LrgJoint *self;

	g_return_val_if_fail (position_a != NULL, NULL);
	g_return_val_if_fail (position_b != NULL, NULL);

	self = g_object_new (LRG_TYPE_JOINT, "kind", kind, NULL);
	lrg_physics_constraint_set_bodies (LRG_PHYSICS_CONSTRAINT (self), body_a, body_b);
	v3_copy (self->local_pos1, position_a);
	v3_copy (self->local_pos2, position_b);
	if (rotation_a != NULL)
		q_normalize (self->c_to_b1, rotation_a);
	if (rotation_b != NULL)
		q_normalize (self->c_to_b2, rotation_b);
	return self;
}

/* pose of a body (identity for the world frame) */
static gboolean
read_pose (LrgDynamicsWorld  *world,
           guint              body,
           gfloat            *position,
           gfloat            *rotation,
           GError           **error)
{
	g_autoptr(GError) local = NULL;

	if (body == LRG_DYNAMICS_WORLD_BODY)
	{
		v3_set (position, 0.0f, 0.0f, 0.0f);
		q_identity (rotation);
		return TRUE;
	}
	if (!lrg_dynamics_world_get_position (world, body, position, &local) ||
	    !lrg_dynamics_world_get_orientation (world, body, rotation, &local))
	{
		g_set_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_BODY,
		             "joint body %u: %s", body, local != NULL ? local->message : "unknown");
		return FALSE;
	}
	q_normalize (rotation, rotation);
	return TRUE;
}

static gboolean
check_vector (const gfloat *v, gboolean unit, const gchar *what, GError **error)
{
	if (v == NULL || !v3_finite (v) || (unit && v3_length (v) < 1e-6f))
	{
		g_set_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_INVALID,
		             "joint %s must be a finite%s vector", what, unit ? " non-zero" : "");
		return FALSE;
	}
	return TRUE;
}

/* frame in world, then in each body's local space */
static void
localise (LrgJoint     *self,
          const gfloat *anchor,
          const gfloat *c_world,
          const gfloat *x1,
          const gfloat *q1,
          const gfloat *x2,
          const gfloat *q2)
{
	gfloat inv[4];
	gfloat d[3];

	q_conj (inv, q1);
	v3_sub (d, anchor, x1);
	q_rotate (self->local_pos1, inv, d);
	q_mul (self->c_to_b1, inv, c_world);
	q_normalize (self->c_to_b1, self->c_to_b1);
	q_conj (inv, q2);
	v3_sub (d, anchor, x2);
	q_rotate (self->local_pos2, inv, d);
	q_mul (self->c_to_b2, inv, c_world);
	q_normalize (self->c_to_b2, self->c_to_b2);
}

LrgJoint *
lrg_joint_new_in_world (LrgDynamicsWorld  *world,
                        LrgJointKind       kind,
                        guint              body_a,
                        guint              body_b,
                        const gfloat      *anchor,
                        const gfloat      *axis_x,
                        const gfloat      *axis_y,
                        GError           **error)
{
	LrgJoint *self;
	gfloat x1[3];
	gfloat q1[4];
	gfloat x2[3];
	gfloat q2[4];
	gfloat ax[3];
	gfloat ay[3];
	gfloat az[3];
	gfloat c_world[4];
	gfloat len;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (world), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!check_vector (anchor, FALSE, "anchor", error) ||
	    !check_vector (axis_x, TRUE, "axis", error))
		return NULL;
	if (axis_y != NULL && !check_vector (axis_y, FALSE, "second axis", error))
		return NULL;
	if (body_a == body_b)
	{
		g_set_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_BODY,
		             "joint needs two different bodies (both are %u)", body_a);
		return NULL;
	}
	if (!read_pose (world, body_a, x1, q1, error) || !read_pose (world, body_b, x2, q2, error))
		return NULL;

	/* orthonormal frame: X given, Y made perpendicular, Z = X x Y */
	v3_scale (ax, axis_x, 1.0f / v3_length (axis_x));
	if (axis_y != NULL)
	{
		v3_copy (ay, axis_y);
		v3_madd (ay, ax, -v3_dot (ay, ax));
		len = v3_length (ay);
		if (len < 1e-5f)
			v3_perpendicular (ay, ax);
		else
			v3_scale (ay, ay, 1.0f / len);
	}
	else
		v3_perpendicular (ay, ax);
	v3_cross (az, ax, ay);
	q_from_basis (c_world, ax, ay, az);

	self = g_object_new (LRG_TYPE_JOINT, "kind", kind, NULL);
	lrg_physics_constraint_set_bodies (LRG_PHYSICS_CONSTRAINT (self), body_a, body_b);
	localise (self, anchor, c_world, x1, q1, x2, q2);
	return self;
}

LrgJoint *
lrg_joint_new_fixed (LrgDynamicsWorld  *world,
                     guint              body_a,
                     guint              body_b,
                     GError           **error)
{
	gfloat x1[3];
	gfloat q1[4];
	gfloat x2[3];
	gfloat q2[4];
	gfloat mid[3];
	gfloat ax[3] = { 1.0f, 0.0f, 0.0f };
	gfloat ay[3] = { 0.0f, 1.0f, 0.0f };

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (world), NULL);

	if (!read_pose (world, body_a, x1, q1, error) || !read_pose (world, body_b, x2, q2, error))
		return NULL;
	if (body_b == LRG_DYNAMICS_WORLD_BODY)
		v3_copy (mid, x1);
	else
	{
		v3_add (mid, x1, x2);
		v3_scale (mid, mid, 0.5f);
	}
	return lrg_joint_new_in_world (world, LRG_JOINT_FIXED, body_a, body_b, mid, ax, ay, error);
}

LrgJoint *
lrg_joint_new_point (LrgDynamicsWorld  *world,
                     guint              body_a,
                     guint              body_b,
                     const gfloat      *anchor,
                     GError           **error)
{
	gfloat ax[3] = { 1.0f, 0.0f, 0.0f };

	return lrg_joint_new_in_world (world, LRG_JOINT_POINT, body_a, body_b, anchor, ax, NULL, error);
}

LrgJoint *
lrg_joint_new_hinge (LrgDynamicsWorld  *world,
                     guint              body_a,
                     guint              body_b,
                     const gfloat      *anchor,
                     const gfloat      *axis,
                     GError           **error)
{
	return lrg_joint_new_in_world (world, LRG_JOINT_HINGE, body_a, body_b, anchor, axis, NULL, error);
}

LrgJoint *
lrg_joint_new_slider (LrgDynamicsWorld  *world,
                      guint              body_a,
                      guint              body_b,
                      const gfloat      *anchor,
                      const gfloat      *axis,
                      GError           **error)
{
	return lrg_joint_new_in_world (world, LRG_JOINT_SLIDER, body_a, body_b, anchor, axis, NULL, error);
}

LrgJoint *
lrg_joint_new_distance (LrgDynamicsWorld  *world,
                        guint              body_a,
                        guint              body_b,
                        const gfloat      *anchor_a,
                        const gfloat      *anchor_b,
                        GError           **error)
{
	LrgJoint *self;
	gfloat x1[3];
	gfloat q1[4];
	gfloat x2[3];
	gfloat q2[4];
	gfloat inv[4];
	gfloat d[3];

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (world), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!check_vector (anchor_a, FALSE, "anchor A", error) ||
	    !check_vector (anchor_b, FALSE, "anchor B", error))
		return NULL;
	if (body_a == body_b)
	{
		g_set_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_BODY,
		             "joint needs two different bodies (both are %u)", body_a);
		return NULL;
	}
	if (!read_pose (world, body_a, x1, q1, error) || !read_pose (world, body_b, x2, q2, error))
		return NULL;

	self = g_object_new (LRG_TYPE_JOINT, "kind", LRG_JOINT_DISTANCE, NULL);
	lrg_physics_constraint_set_bodies (LRG_PHYSICS_CONSTRAINT (self), body_a, body_b);
	q_conj (inv, q1);
	v3_sub (d, anchor_a, x1);
	q_rotate (self->local_pos1, inv, d);
	q_conj (inv, q2);
	v3_sub (d, anchor_b, x2);
	q_rotate (self->local_pos2, inv, d);
	v3_sub (d, anchor_b, anchor_a);
	self->distance_min = self->distance_max = v3_length (d);
	return self;
}

LrgJoint *
lrg_joint_new_cone (LrgDynamicsWorld  *world,
                    guint              body_a,
                    guint              body_b,
                    const gfloat      *anchor,
                    const gfloat      *twist_axis,
                    gfloat             half_angle,
                    GError           **error)
{
	LrgJoint *self;

	if (!isfinite (half_angle) || half_angle < 0.0f || half_angle > J_PI)
	{
		g_set_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_INVALID,
		             "cone half angle %g is outside [0, pi]", (gdouble) half_angle);
		return NULL;
	}
	self = lrg_joint_new_in_world (world, LRG_JOINT_CONE, body_a, body_b, anchor, twist_axis, NULL,
	                               error);
	if (self == NULL)
		return NULL;
	lrg_joint_set_limits (self, LRG_JOINT_AXIS_ROTATION_Y, -half_angle, half_angle);
	lrg_joint_set_limits (self, LRG_JOINT_AXIS_ROTATION_Z, -half_angle, half_angle);
	return self;
}

LrgJoint *
lrg_joint_new_swing_twist (LrgDynamicsWorld  *world,
                           guint              body_a,
                           guint              body_b,
                           const gfloat      *anchor,
                           const gfloat      *twist_axis,
                           const gfloat      *plane_axis,
                           gfloat             swing_y,
                           gfloat             swing_z,
                           gfloat             twist_min,
                           gfloat             twist_max,
                           GError           **error)
{
	LrgJoint *self;

	if (!isfinite (swing_y) || !isfinite (swing_z) || swing_y < 0.0f || swing_z < 0.0f ||
	    swing_y > J_PI || swing_z > J_PI || !isfinite (twist_min) || !isfinite (twist_max) ||
	    twist_min < -J_PI || twist_max > J_PI || twist_min > twist_max)
	{
		g_set_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_INVALID,
		             "swing-twist limits out of range (swing %g, %g; twist %g..%g)",
		             (gdouble) swing_y, (gdouble) swing_z, (gdouble) twist_min, (gdouble) twist_max);
		return NULL;
	}
	self = lrg_joint_new_in_world (world, LRG_JOINT_SWING_TWIST, body_a, body_b, anchor, twist_axis,
	                               plane_axis, error);
	if (self == NULL)
		return NULL;
	lrg_joint_set_limits (self, LRG_JOINT_AXIS_ROTATION_X, twist_min, twist_max);
	lrg_joint_set_limits (self, LRG_JOINT_AXIS_ROTATION_Y, -swing_y, swing_y);
	lrg_joint_set_limits (self, LRG_JOINT_AXIS_ROTATION_Z, -swing_z, swing_z);
	return self;
}

LrgJoint *
lrg_joint_new_six_dof (LrgDynamicsWorld  *world,
                       guint              body_a,
                       guint              body_b,
                       const gfloat      *anchor,
                       const gfloat      *axis_x,
                       const gfloat      *axis_y,
                       GError           **error)
{
	return lrg_joint_new_in_world (world, LRG_JOINT_SIX_DOF, body_a, body_b, anchor, axis_x, axis_y,
	                               error);
}

/* ------------------------------------------------------------------
 * Accessors
 * ------------------------------------------------------------------ */

LrgJointKind
lrg_joint_get_kind (LrgJoint *self)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), LRG_JOINT_SIX_DOF);
	return self->kind;
}

void
lrg_joint_get_frame_a (LrgJoint *self,
                       gfloat   *position,
                       gfloat   *rotation)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	if (position != NULL)
		v3_copy (position, self->local_pos1);
	if (rotation != NULL)
		q_copy (rotation, self->c_to_b1);
}

void
lrg_joint_get_frame_b (LrgJoint *self,
                       gfloat   *position,
                       gfloat   *rotation)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	if (position != NULL)
		v3_copy (position, self->local_pos2);
	if (rotation != NULL)
		q_copy (rotation, self->c_to_b2);
}

#define CHECK_AXIS(axis, ...) \
	g_return_val_if_fail ((guint) (axis) < J_N_AXES, __VA_ARGS__)

void
lrg_joint_set_axis_mode (LrgJoint         *self,
                         LrgJointAxis      axis,
                         LrgJointAxisMode  mode)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	g_return_if_fail (mode <= LRG_JOINT_AXIS_LIMITED);

	self->mode[axis] = mode;
	update_limits (self);
}

LrgJointAxisMode
lrg_joint_get_axis_mode (LrgJoint     *self,
                         LrgJointAxis  axis)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), LRG_JOINT_AXIS_FREE);
	CHECK_AXIS (axis, LRG_JOINT_AXIS_FREE);
	return self->mode[axis];
}

void
lrg_joint_set_limits (LrgJoint     *self,
                      LrgJointAxis  axis,
                      gfloat        minimum,
                      gfloat        maximum)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	g_return_if_fail (isfinite (minimum) && isfinite (maximum));
	g_return_if_fail (minimum <= maximum);

	if (axis >= LRG_JOINT_AXIS_ROTATION_X)
	{
		minimum = CLAMP (minimum, -J_PI, J_PI);
		maximum = CLAMP (maximum, -J_PI, J_PI);
	}
	self->user_min[axis] = minimum;
	self->user_max[axis] = maximum;
	self->mode[axis] = minimum == maximum ? LRG_JOINT_AXIS_LOCKED : LRG_JOINT_AXIS_LIMITED;
	update_limits (self);
	/* a target outside the new limits must be clamped again */
	lrg_joint_set_target_orientation (self, self->target_orientation);
}

void
lrg_joint_get_limits (LrgJoint     *self,
                      LrgJointAxis  axis,
                      gfloat       *minimum,
                      gfloat       *maximum)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	if (minimum != NULL)
		*minimum = self->user_min[axis];
	if (maximum != NULL)
		*maximum = self->user_max[axis];
}

void
lrg_joint_set_limit_spring (LrgJoint     *self,
                            LrgJointAxis  axis,
                            gfloat        frequency,
                            gfloat        damping)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail (axis <= LRG_JOINT_AXIS_TRANSLATION_Z);
	g_return_if_fail (isfinite (frequency) && frequency >= 0.0f);
	g_return_if_fail (isfinite (damping) && damping >= 0.0f);

	self->spring_frequency[axis] = frequency;
	self->spring_damping[axis] = damping;
}

void
lrg_joint_get_limit_spring (LrgJoint     *self,
                            LrgJointAxis  axis,
                            gfloat       *frequency,
                            gfloat       *damping)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	if (frequency != NULL)
		*frequency = self->spring_frequency[axis];
	if (damping != NULL)
		*damping = self->spring_damping[axis];
}

void
lrg_joint_set_friction (LrgJoint     *self,
                        LrgJointAxis  axis,
                        gfloat        friction)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	g_return_if_fail (isfinite (friction) && friction >= 0.0f);

	self->friction[axis] = friction;
	update_motors (self);
}

gfloat
lrg_joint_get_friction (LrgJoint     *self,
                        LrgJointAxis  axis)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), 0.0f);
	CHECK_AXIS (axis, 0.0f);
	return self->friction[axis];
}

void
lrg_joint_set_swing_type (LrgJoint          *self,
                          LrgJointSwingType  swing_type)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail (swing_type <= LRG_JOINT_SWING_PYRAMID);

	if (self->swing_type == swing_type)
		return;
	self->swing_type = swing_type;
	update_limits (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SWING_TYPE]);
}

LrgJointSwingType
lrg_joint_get_swing_type (LrgJoint *self)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), LRG_JOINT_SWING_CONE);
	return self->swing_type;
}

void
lrg_joint_set_distance_range (LrgJoint *self,
                              gfloat    minimum,
                              gfloat    maximum)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail (isfinite (minimum) && isfinite (maximum));
	g_return_if_fail (minimum >= 0.0f && minimum <= maximum);

	self->distance_min = minimum;
	self->distance_max = maximum;
}

void
lrg_joint_get_distance_range (LrgJoint *self,
                              gfloat   *minimum,
                              gfloat   *maximum)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	if (minimum != NULL)
		*minimum = self->distance_min;
	if (maximum != NULL)
		*maximum = self->distance_max;
}

void
lrg_joint_set_motor (LrgJoint          *self,
                     LrgJointAxis       axis,
                     LrgJointMotorMode  mode,
                     gfloat             max_force)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	g_return_if_fail (mode <= LRG_JOINT_MOTOR_POSITION);
	g_return_if_fail (isfinite (max_force) && max_force >= 0.0f);

	if (self->motor[axis] != mode)
	{
		if (axis < 3)
			axis_deactivate (&self->motor_translation_part[axis]);
		else
			angle_deactivate (&self->motor_rotation_part[axis - 3]);
	}
	self->motor[axis] = mode;
	self->motor_max[axis] = max_force;
	update_motors (self);
}

LrgJointMotorMode
lrg_joint_get_motor_mode (LrgJoint     *self,
                          LrgJointAxis  axis)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), LRG_JOINT_MOTOR_OFF);
	CHECK_AXIS (axis, LRG_JOINT_MOTOR_OFF);
	return self->motor[axis];
}

gfloat
lrg_joint_get_motor_max_force (LrgJoint     *self,
                               LrgJointAxis  axis)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), 0.0f);
	CHECK_AXIS (axis, 0.0f);
	return self->motor_max[axis];
}

void
lrg_joint_set_motor_spring (LrgJoint     *self,
                            LrgJointAxis  axis,
                            gfloat        frequency,
                            gfloat        damping)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	g_return_if_fail (isfinite (frequency) && frequency > 0.0f);
	g_return_if_fail (isfinite (damping) && damping >= 0.0f);

	self->motor_frequency[axis] = frequency;
	self->motor_damping[axis] = damping;
}

void
lrg_joint_get_motor_spring (LrgJoint     *self,
                            LrgJointAxis  axis,
                            gfloat       *frequency,
                            gfloat       *damping)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	if (frequency != NULL)
		*frequency = self->motor_frequency[axis];
	if (damping != NULL)
		*damping = self->motor_damping[axis];
}

void
lrg_joint_set_motor_target_velocity (LrgJoint     *self,
                                     LrgJointAxis  axis,
                                     gfloat        velocity)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	g_return_if_fail (isfinite (velocity));
	self->target_velocity[axis] = velocity;
}

gfloat
lrg_joint_get_motor_target_velocity (LrgJoint     *self,
                                     LrgJointAxis  axis)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), 0.0f);
	CHECK_AXIS (axis, 0.0f);
	return self->target_velocity[axis];
}

void
lrg_joint_set_motor_target_position (LrgJoint     *self,
                                     LrgJointAxis  axis,
                                     gfloat        position)
{
	gfloat q[4];

	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail ((guint) axis < J_N_AXES);
	g_return_if_fail (isfinite (position));

	self->target_position[axis] = position;
	if (axis >= LRG_JOINT_AXIS_ROTATION_X)
	{
		q_from_euler (q, &self->target_position[3]);
		lrg_joint_set_target_orientation (self, q);
	}
}

gfloat
lrg_joint_get_motor_target_position (LrgJoint     *self,
                                     LrgJointAxis  axis)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), 0.0f);
	CHECK_AXIS (axis, 0.0f);
	return self->target_position[axis];
}

void
lrg_joint_set_target_orientation (LrgJoint     *self,
                                  const gfloat *rotation)
{
	gfloat q[4];
	gfloat swing[4];
	gfloat twist[4];
	guint clamped;

	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail (rotation != NULL);

	/* Jolt SetTargetOrientationCS: a target beyond the limits is clamped */
	q_normalize (q, rotation);
	q_swing_twist (q, swing, twist);
	st_clamp (&self->swing_twist, swing, twist, &clamped);
	if (clamped != 0 && rotation_constrained (self) && !rotation_fully_constrained (self))
		q_mul (self->target_orientation, swing, twist);
	else
		q_copy (self->target_orientation, q);
}

void
lrg_joint_get_target_orientation (LrgJoint *self,
                                  gfloat   *rotation)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail (rotation != NULL);
	q_copy (rotation, self->target_orientation);
}

void
lrg_joint_set_break_force (LrgJoint *self,
                           gfloat    force)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail (isfinite (force) && force >= 0.0f);

	if (self->break_force == force)
		return;
	self->break_force = force;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BREAK_FORCE]);
}

gfloat
lrg_joint_get_break_force (LrgJoint *self)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), 0.0f);
	return self->break_force;
}

void
lrg_joint_set_break_torque (LrgJoint *self,
                            gfloat    torque)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail (isfinite (torque) && torque >= 0.0f);

	if (self->break_torque == torque)
		return;
	self->break_torque = torque;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BREAK_TORQUE]);
}

gfloat
lrg_joint_get_break_torque (LrgJoint *self)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), 0.0f);
	return self->break_torque;
}

gboolean
lrg_joint_is_broken (LrgJoint *self)
{
	g_return_val_if_fail (LRG_IS_JOINT (self), FALSE);
	return self->broken;
}

void
lrg_joint_repair (LrgJoint *self)
{
	g_return_if_fail (LRG_IS_JOINT (self));

	deactivate_all (self);
	v3_set (self->applied_force, 0.0f, 0.0f, 0.0f);
	v3_set (self->applied_torque, 0.0f, 0.0f, 0.0f);
	lrg_physics_constraint_set_enabled (LRG_PHYSICS_CONSTRAINT (self), TRUE);
	if (self->broken)
	{
		self->broken = FALSE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BROKEN]);
	}
}

void
lrg_joint_get_applied_force (LrgJoint *self,
                             gfloat   *force)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail (force != NULL);
	v3_copy (force, self->applied_force);
}

void
lrg_joint_get_applied_torque (LrgJoint *self,
                              gfloat   *torque)
{
	g_return_if_fail (LRG_IS_JOINT (self));
	g_return_if_fail (torque != NULL);
	v3_copy (torque, self->applied_torque);
}

gboolean
lrg_joint_measure (LrgJoint          *self,
                   LrgDynamicsWorld  *world,
                   gfloat            *translation,
                   gfloat            *angles,
                   GError           **error)
{
	LrgPhysicsConstraint *c;
	gfloat x1[3];
	gfloat q1[4];
	gfloat x2[3];
	gfloat q2[4];
	gfloat p1[3];
	gfloat p2[3];
	gfloat u[3];
	gfloat c1[4];
	gfloat c2[4];
	gfloat inv[4];
	gfloat q[4];
	gfloat swing[4];
	gfloat twist[4];

	g_return_val_if_fail (LRG_IS_JOINT (self), FALSE);
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (world), FALSE);

	c = LRG_PHYSICS_CONSTRAINT (self);
	if (!read_pose (world, lrg_physics_constraint_get_body_a (c), x1, q1, error) ||
	    !read_pose (world, lrg_physics_constraint_get_body_b (c), x2, q2, error))
		return FALSE;

	/* anchors in world; the body origin is what get_position() reports */
	q_rotate (p1, q1, self->local_pos1);
	v3_add (p1, p1, x1);
	q_rotate (p2, q2, self->local_pos2);
	v3_add (p2, p2, x2);
	v3_sub (u, p2, p1);

	q_mul (c1, q1, self->c_to_b1);
	q_mul (c2, q2, self->c_to_b2);
	q_conj (inv, c1);
	if (translation != NULL)
	{
		if (self->kind == LRG_JOINT_DISTANCE)
			v3_set (translation, v3_length (u), 0.0f, 0.0f);
		else
			q_rotate (translation, inv, u);
	}
	if (angles != NULL)
	{
		q_mul (q, inv, c2);
		q_normalize (q, q);
		q_swing_twist (q, swing, twist);
		if (twist[3] < 0.0f)
			q_neg (twist, twist);
		if (swing[3] < 0.0f)
			q_neg (swing, swing);
		angles[0] = 2.0f * atan2f (twist[0], twist[3]);
		angles[1] = 2.0f * atan2f (swing[1], swing[3]);
		angles[2] = 2.0f * atan2f (swing[2], swing[3]);
	}
	return TRUE;
}
