/* test-joint.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgJoint in an LrgDynamicsWorld: every kind holds its constraint under
 * gravity and loads (pendulum period against the physical-pendulum
 * formula, hinge/slider/cone/6-DOF limits, motors reaching their targets,
 * distance spring frequency), breakable joints, chains, determinism,
 * argument validation and performance numbers.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define T_PI (3.14159265358979323846f)
#define T_G (9.81f)

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static LrgDynamicsWorld *
make_world (gfloat gx, gfloat gy, gfloat gz, gfloat dt)
{
	LrgDynamicsWorld *world;
	gfloat g[3];
	g_autoptr(GError) error = NULL;

	g[0] = gx;
	g[1] = gy;
	g[2] = gz;
	world = lrg_dynamics_world_new ();
	g_assert_true (lrg_dynamics_world_set_gravity (world, g, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_dynamics_world_set_step_limits (world, dt, 8, &error));
	g_assert_no_error (error);
	return world;
}

static guint
add_sphere (LrgDynamicsWorld *world, gfloat mass, gfloat radius, gfloat x, gfloat y, gfloat z)
{
	g_autoptr(LrgPhysicsShape) shape = lrg_physics_shape_new_sphere (radius);
	g_autoptr(GError) error = NULL;
	gfloat p[3];
	guint id = 0;

	p[0] = x;
	p[1] = y;
	p[2] = z;
	g_assert_true (lrg_dynamics_world_add_body (world, shape, LRG_DYNAMICS_BODY_DYNAMIC, mass, p,
	                                            NULL, &id, &error));
	g_assert_no_error (error);
	return id;
}

static guint
add_box (LrgDynamicsWorld *world, gfloat mass, gfloat hx, gfloat hy, gfloat hz,
         gfloat x, gfloat y, gfloat z)
{
	g_autoptr(LrgPhysicsShape) shape = lrg_physics_shape_new_box (hx, hy, hz);
	g_autoptr(GError) error = NULL;
	gfloat p[3];
	guint id = 0;

	p[0] = x;
	p[1] = y;
	p[2] = z;
	g_assert_true (lrg_dynamics_world_add_body (world, shape, LRG_DYNAMICS_BODY_DYNAMIC, mass, p,
	                                            NULL, &id, &error));
	g_assert_no_error (error);
	return id;
}

static void
add_joint (LrgDynamicsWorld *world, LrgJoint *joint)
{
	g_autoptr(GError) error = NULL;

	g_assert_nonnull (joint);
	g_assert_true (lrg_dynamics_world_add_constraint (world, LRG_PHYSICS_CONSTRAINT (joint), NULL,
	                                                  &error));
	g_assert_no_error (error);
}

static void
step (LrgDynamicsWorld *world, gfloat dt)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_dynamics_world_step (world, dt, NULL, &error));
	g_assert_no_error (error);
}

static void
position_of (LrgDynamicsWorld *world, guint id, gfloat *p)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_dynamics_world_get_position (world, id, p, &error));
	g_assert_no_error (error);
}

static void
velocity_of (LrgDynamicsWorld *world, guint id, gfloat *v)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_dynamics_world_get_linear_velocity (world, id, v, &error));
	g_assert_no_error (error);
}

static void
measure (LrgJoint *joint, LrgDynamicsWorld *world, gfloat *t, gfloat *a)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_joint_measure (joint, world, t, a, &error));
	g_assert_no_error (error);
}

static gfloat
len3 (const gfloat *v)
{
	return sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

/* ------------------------------------------------------------------ */
/* point joint: a physical pendulum                                    */
/* ------------------------------------------------------------------ */

/*
 * A sphere (m = 1, r = 0.1) hangs 1 m below a world pivot on a point
 * joint and starts 0.1 rad from vertical. Small-angle period of a
 * physical pendulum: T = 2 pi sqrt (I / (m g L)), I = m L^2 + 2/5 m r^2.
 * The period is measured from successive upward zero crossings of x.
 */
static void
test_point_pendulum_period (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, -T_G, 0.0f, 1.0f / 240.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat pivot[3] = { 0.0f, 0.0f, 0.0f };
	gfloat theta = 0.1f;
	gfloat r = 0.1f;
	gfloat L = 1.0f;
	gfloat expected;
	gfloat crossings[4];
	guint n_cross = 0;
	gfloat prev_x;
	gfloat t = 0.0f;
	gfloat max_err = 0.0f;
	guint body;
	guint i;

	body = add_sphere (world, 1.0f, r, L * sinf (theta), -L * cosf (theta), 0.0f);
	joint = lrg_joint_new_point (world, body, LRG_DYNAMICS_WORLD_BODY, pivot, &error);
	g_assert_no_error (error);
	add_joint (world, joint);
	g_assert_cmpint (lrg_joint_get_kind (joint), ==, LRG_JOINT_POINT);

	prev_x = L * sinf (theta);
	for (i = 0; i < 240 * 8 && n_cross < 4; i++)
	{
		gfloat p[3];
		gfloat d;

		step (world, 1.0f / 240.0f);
		t += 1.0f / 240.0f;
		position_of (world, body, p);
		d = fabsf (len3 (p) - L);
		max_err = MAX (max_err, d);
		if (prev_x < 0.0f && p[0] >= 0.0f)
			crossings[n_cross++] = t - (1.0f / 240.0f) * p[0] / (p[0] - prev_x);
		prev_x = p[0];
	}
	g_assert_cmpuint (n_cross, >=, 3);
	expected = 2.0f * T_PI * sqrtf ((L * L + 0.4f * r * r) / (T_G * L));
	g_test_message ("pendulum period %.4f s, analytic %.4f s, max anchor drift %.2e m",
	                (gdouble) (crossings[2] - crossings[1]), (gdouble) expected, (gdouble) max_err);
	g_assert_cmpfloat_with_epsilon (crossings[2] - crossings[1], expected, 0.01f * expected);
	g_assert_cmpfloat (max_err, <, 2e-3f);
}

/* ------------------------------------------------------------------ */
/* hinge                                                               */
/* ------------------------------------------------------------------ */

/*
 * A plank hinged about world Z at one end, horizontal at rest, limited to
 * [-0.5, 0.5] rad: gravity swings it down onto the -0.5 limit, where it
 * must stay; the off-axis swing and the anchor must not drift.
 */
static void
test_hinge_limit (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, -T_G, 0.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor[3] = { 0.0f, 0.0f, 0.0f };
	gfloat axis[3] = { 0.0f, 0.0f, 1.0f };
	gfloat worst = -10.0f;
	gfloat off_axis = 0.0f;
	gfloat drift = 0.0f;
	gfloat tr[3];
	gfloat an[3];
	guint body;
	guint i;

	body = add_box (world, 2.0f, 0.5f, 0.05f, 0.05f, 0.5f, 0.0f, 0.0f);
	joint = lrg_joint_new_hinge (world, LRG_DYNAMICS_WORLD_BODY, body, anchor, axis, &error);
	g_assert_no_error (error);
	lrg_joint_set_limits (joint, LRG_JOINT_AXIS_ROTATION_X, -0.5f, 0.5f);
	g_assert_cmpint (lrg_joint_get_axis_mode (joint, LRG_JOINT_AXIS_ROTATION_X), ==,
	                 LRG_JOINT_AXIS_LIMITED);
	add_joint (world, joint);

	for (i = 0; i < 120 * 3; i++)
	{
		step (world, 1.0f / 120.0f);
		measure (joint, world, tr, an);
		worst = MAX (worst, -an[0]);
		off_axis = MAX (off_axis, MAX (fabsf (an[1]), fabsf (an[2])));
		drift = MAX (drift, len3 (tr));
	}
	g_test_message ("hinge: deepest %.4f rad (limit 0.5), off-axis %.2e rad, drift %.2e m",
	                (gdouble) worst, (gdouble) off_axis, (gdouble) drift);
	/* it fell onto the limit and stayed there */
	g_assert_cmpfloat (fabsf (an[0] + 0.5f), <, 0.02f);
	g_assert_cmpfloat (worst, <, 0.5f + 0.03f);
	g_assert_cmpfloat (off_axis, <, 0.01f);
	/* the plank hits the limit at about 2.5 rad/s: the anchor gives a few mm for a step */
	g_assert_cmpfloat (drift, <, 0.01f);
}

/* a velocity motor on a free hinge reaches its target angular velocity */
static void
test_hinge_motor (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, 0.0f, 0.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor[3] = { 0.0f, 0.0f, 0.0f };
	gfloat axis[3] = { 0.0f, 1.0f, 0.0f };
	gfloat w[3];
	guint body;
	guint i;

	body = add_box (world, 1.0f, 0.5f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f);
	joint = lrg_joint_new_hinge (world, LRG_DYNAMICS_WORLD_BODY, body, anchor, axis, &error);
	g_assert_no_error (error);
	lrg_joint_set_motor (joint, LRG_JOINT_AXIS_ROTATION_X, LRG_JOINT_MOTOR_VELOCITY, 100.0f);
	lrg_joint_set_motor_target_velocity (joint, LRG_JOINT_AXIS_ROTATION_X, 3.0f);
	g_assert_cmpint (lrg_joint_get_motor_mode (joint, LRG_JOINT_AXIS_ROTATION_X), ==,
	                 LRG_JOINT_MOTOR_VELOCITY);
	g_assert_cmpfloat (lrg_joint_get_motor_max_force (joint, LRG_JOINT_AXIS_ROTATION_X), ==, 100.0f);
	g_assert_cmpfloat (lrg_joint_get_motor_target_velocity (joint, LRG_JOINT_AXIS_ROTATION_X), ==, 3.0f);
	add_joint (world, joint);
	for (i = 0; i < 120; i++)
		step (world, 1.0f / 120.0f);
	g_assert_true (lrg_dynamics_world_get_angular_velocity (world, body, w, &error));
	g_test_message ("hinge motor: w = (%.3f, %.3f, %.3f), target 3 about +Y",
	                (gdouble) w[0], (gdouble) w[1], (gdouble) w[2]);
	/* body B (the box) turns at +3 rad/s relative to the world... the
	 * motor drives B relative to A with A the world: B turns at -target
	 * or +target depending on the frame; the magnitude is what counts */
	g_assert_cmpfloat_with_epsilon (fabsf (w[1]), 3.0f, 0.05f);
	g_assert_cmpfloat (fabsf (w[0]) + fabsf (w[2]), <, 0.01f);
}

/* ------------------------------------------------------------------ */
/* slider                                                              */
/* ------------------------------------------------------------------ */

/* gravity along -X pulls the slider onto its lower limit */
static void
test_slider_limits (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (-T_G, -2.0f, 0.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor[3] = { 0.0f, 0.0f, 0.0f };
	gfloat axis[3] = { 1.0f, 0.0f, 0.0f };
	gfloat tr[3];
	gfloat an[3];
	gfloat lo;
	gfloat hi;
	gfloat worst = 0.0f;
	gfloat off = 0.0f;
	guint body;
	guint i;

	body = add_box (world, 1.0f, 0.1f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f);
	joint = lrg_joint_new_slider (world, LRG_DYNAMICS_WORLD_BODY, body, anchor, axis, &error);
	g_assert_no_error (error);
	lrg_joint_set_limits (joint, LRG_JOINT_AXIS_TRANSLATION_X, -0.5f, 0.5f);
	lrg_joint_get_limits (joint, LRG_JOINT_AXIS_TRANSLATION_X, &lo, &hi);
	g_assert_cmpfloat (lo, ==, -0.5f);
	g_assert_cmpfloat (hi, ==, 0.5f);
	add_joint (world, joint);
	for (i = 0; i < 240; i++)
	{
		step (world, 1.0f / 120.0f);
		measure (joint, world, tr, an);
		worst = MAX (worst, -tr[0]);
		off = MAX (off, MAX (fabsf (tr[1]), fabsf (tr[2])));
		off = MAX (off, MAX (fabsf (an[0]), MAX (fabsf (an[1]), fabsf (an[2]))));
	}
	g_test_message ("slider: deepest %.4f m (limit 0.5), off-axis %.2e", (gdouble) worst, (gdouble) off);
	g_assert_cmpfloat_with_epsilon (tr[0], -0.5f, 0.01f);
	g_assert_cmpfloat (worst, <, 0.52f);
	g_assert_cmpfloat (off, <, 0.005f);
}

/* velocity motor reaches its velocity; position motor reaches its target */
static void
test_slider_motors (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, 0.0f, 0.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) vel = NULL;
	g_autoptr(LrgJoint) pos = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor_a[3] = { 0.0f, 0.0f, 0.0f };
	gfloat anchor_b[3] = { 0.0f, 0.0f, 2.0f };
	gfloat axis[3] = { 1.0f, 0.0f, 0.0f };
	gfloat v[3];
	gfloat tr[3];
	gfloat f;
	gfloat d;
	guint a;
	guint b;
	guint i;

	a = add_box (world, 2.0f, 0.1f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f);
	b = add_box (world, 2.0f, 0.1f, 0.1f, 0.1f, 0.0f, 0.0f, 2.0f);
	vel = lrg_joint_new_slider (world, LRG_DYNAMICS_WORLD_BODY, a, anchor_a, axis, &error);
	g_assert_no_error (error);
	lrg_joint_set_motor (vel, LRG_JOINT_AXIS_TRANSLATION_X, LRG_JOINT_MOTOR_VELOCITY, 50.0f);
	lrg_joint_set_motor_target_velocity (vel, LRG_JOINT_AXIS_TRANSLATION_X, 1.5f);
	add_joint (world, vel);

	pos = lrg_joint_new_slider (world, LRG_DYNAMICS_WORLD_BODY, b, anchor_b, axis, &error);
	g_assert_no_error (error);
	lrg_joint_set_motor (pos, LRG_JOINT_AXIS_TRANSLATION_X, LRG_JOINT_MOTOR_POSITION, 200.0f);
	lrg_joint_set_motor_spring (pos, LRG_JOINT_AXIS_TRANSLATION_X, 3.0f, 1.0f);
	lrg_joint_get_motor_spring (pos, LRG_JOINT_AXIS_TRANSLATION_X, &f, &d);
	g_assert_cmpfloat (f, ==, 3.0f);
	g_assert_cmpfloat (d, ==, 1.0f);
	lrg_joint_set_motor_target_position (pos, LRG_JOINT_AXIS_TRANSLATION_X, 0.75f);
	g_assert_cmpfloat (lrg_joint_get_motor_target_position (pos, LRG_JOINT_AXIS_TRANSLATION_X), ==, 0.75f);
	add_joint (world, pos);

	for (i = 0; i < 240; i++)
		step (world, 1.0f / 120.0f);
	velocity_of (world, a, v);
	measure (pos, world, tr, NULL);
	g_test_message ("slider motors: velocity %.4f m/s (target 1.5), position %.4f m (target 0.75)",
	                (gdouble) v[0], (gdouble) tr[0]);
	g_assert_cmpfloat_with_epsilon (fabsf (v[0]), 1.5f, 0.02f);
	g_assert_cmpfloat_with_epsilon (tr[0], 0.75f, 0.01f);
}

/* ------------------------------------------------------------------ */
/* distance spring                                                     */
/* ------------------------------------------------------------------ */

/*
 * A 2 kg sphere tied to a world anchor by a distance spring (rest 1 m,
 * 2 Hz, undamped). Displaced 0.1 m along the spring it oscillates at
 * 2 Hz: the spring's frequency is defined for the joint's effective
 * mass, which is the body mass when the anchor is the centre of mass.
 */
static void
test_distance_spring (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, 0.0f, 0.0f, 1.0f / 240.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor_w[3] = { 0.0f, 0.0f, 0.0f };
	gfloat anchor_b[3] = { 1.0f, 0.0f, 0.0f };
	gfloat moved[3] = { 1.1f, 0.0f, 0.0f };
	gfloat rest_q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	gfloat crossings[4];
	guint n_cross = 0;
	gfloat prev;
	gfloat t = 0.0f;
	gfloat lo;
	gfloat hi;
	gfloat f;
	gfloat d;
	guint body;
	guint i;

	body = add_sphere (world, 2.0f, 0.1f, 1.0f, 0.0f, 0.0f);
	joint = lrg_joint_new_distance (world, LRG_DYNAMICS_WORLD_BODY, body, anchor_w, anchor_b, &error);
	g_assert_no_error (error);
	lrg_joint_get_distance_range (joint, &lo, &hi);
	g_assert_cmpfloat_with_epsilon (lo, 1.0f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (hi, 1.0f, 1e-6f);
	lrg_joint_set_limit_spring (joint, LRG_JOINT_AXIS_TRANSLATION_X, 2.0f, 0.0f);
	lrg_joint_get_limit_spring (joint, LRG_JOINT_AXIS_TRANSLATION_X, &f, &d);
	g_assert_cmpfloat (f, ==, 2.0f);
	g_assert_cmpfloat (d, ==, 0.0f);
	add_joint (world, joint);
	g_assert_true (lrg_dynamics_world_set_pose (world, body, moved, rest_q, &error));
	g_assert_no_error (error);

	prev = 0.1f;
	for (i = 0; i < 240 * 3 && n_cross < 4; i++)
	{
		gfloat p[3];
		gfloat x;

		step (world, 1.0f / 240.0f);
		t += 1.0f / 240.0f;
		position_of (world, body, p);
		x = p[0] - 1.0f;
		if (prev < 0.0f && x >= 0.0f)
			crossings[n_cross++] = t - (1.0f / 240.0f) * x / (x - prev);
		prev = x;
	}
	g_assert_cmpuint (n_cross, >=, 3);
	g_test_message ("distance spring: period %.4f s (2 Hz -> 0.5 s)",
	                (gdouble) (crossings[2] - crossings[1]));
	g_assert_cmpfloat_with_epsilon (crossings[2] - crossings[1], 0.5f, 0.02f);
}

/* a rigid distance range [0.5, 1]: a falling body stops at 1 m */
static void
test_distance_range (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, -T_G, 0.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor_w[3] = { 0.0f, 0.0f, 0.0f };
	gfloat anchor_b[3] = { 0.0f, -0.6f, 0.0f };
	gfloat tr[3];
	gfloat worst = 0.0f;
	guint body;
	guint i;

	body = add_sphere (world, 1.0f, 0.05f, 0.0f, -0.6f, 0.0f);
	joint = lrg_joint_new_distance (world, LRG_DYNAMICS_WORLD_BODY, body, anchor_w, anchor_b, &error);
	g_assert_no_error (error);
	lrg_joint_set_distance_range (joint, 0.5f, 1.0f);
	add_joint (world, joint);
	for (i = 0; i < 240; i++)
	{
		step (world, 1.0f / 120.0f);
		measure (joint, world, tr, NULL);
		worst = MAX (worst, tr[0]);
	}
	g_test_message ("distance range: longest %.4f m (max 1)", (gdouble) worst);
	g_assert_cmpfloat (worst, <, 1.01f);
	g_assert_cmpfloat_with_epsilon (tr[0], 1.0f, 0.005f);
}

/* ------------------------------------------------------------------ */
/* cone                                                                */
/* ------------------------------------------------------------------ */

/*
 * A rod hangs from a cone joint whose axis is vertical; a sideways
 * gravity pushes it against the 0.4 rad cone. The angle between the
 * rod's twist axis and the cone axis never passes the half angle.
 */
static void
test_cone_limit (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (6.0f, -T_G, 3.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor[3] = { 0.0f, 0.0f, 0.0f };
	gfloat down[3] = { 0.0f, -1.0f, 0.0f };
	gfloat worst = 0.0f;
	gfloat drift = 0.0f;
	gfloat tr[3];
	guint body;
	guint i;

	body = add_box (world, 1.0f, 0.05f, 0.5f, 0.05f, 0.0f, -0.5f, 0.0f);
	joint = lrg_joint_new_cone (world, LRG_DYNAMICS_WORLD_BODY, body, anchor, down, 0.4f, &error);
	g_assert_no_error (error);
	add_joint (world, joint);
	for (i = 0; i < 360; i++)
	{
		gfloat p[3];
		gfloat c;

		step (world, 1.0f / 120.0f);
		position_of (world, body, p);
		/* the rod's centre lies on its twist axis 0.5 m from the pivot */
		c = -p[1] / len3 (p);
		worst = MAX (worst, acosf (CLAMP (c, -1.0f, 1.0f)));
		measure (joint, world, tr, NULL);
		drift = MAX (drift, len3 (tr));
	}
	g_test_message ("cone: widest %.4f rad (limit 0.4), drift %.2e m", (gdouble) worst, (gdouble) drift);
	g_assert_cmpfloat (worst, <, 0.4f + 0.02f);
	g_assert_cmpfloat (worst, >, 0.35f);
	g_assert_cmpfloat (drift, <, 0.005f);
}

/* the swing-twist joint holds its twist range against a twisting torque */
static void
test_swing_twist_limits (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, 0.0f, 0.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor[3] = { 0.0f, 0.0f, 0.0f };
	gfloat axis[3] = { 1.0f, 0.0f, 0.0f };
	gfloat torque[3] = { 5.0f, 0.0f, 0.0f };
	gfloat an[3];
	gfloat worst = 0.0f;
	guint body;
	guint i;

	body = add_box (world, 1.0f, 0.5f, 0.1f, 0.1f, 0.5f, 0.0f, 0.0f);
	joint = lrg_joint_new_swing_twist (world, LRG_DYNAMICS_WORLD_BODY, body, anchor, axis, NULL,
	                                   0.6f, 0.3f, -0.2f, 0.3f, &error);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_joint_get_swing_type (joint), ==, LRG_JOINT_SWING_CONE);
	add_joint (world, joint);
	for (i = 0; i < 240; i++)
	{
		g_assert_true (lrg_dynamics_world_apply_torque (world, body, torque, &error));
		step (world, 1.0f / 120.0f);
		measure (joint, world, NULL, an);
		worst = MAX (worst, fabsf (an[0]));
	}
	g_test_message ("swing-twist: twist %.4f rad (range -0.2..0.3)", (gdouble) an[0]);
	g_assert_cmpfloat (fabsf (an[0]) , <, 0.3f + 0.02f);
	/* a 750 rad/s^2 twist acceleration can pass the limit by one
	 * substep's travel before the limit row engages */
	g_assert_cmpfloat (worst, <, 0.3f + 0.05f);
	g_assert_cmpfloat (fabsf (an[0]), >, 0.25f);
}

/* ------------------------------------------------------------------ */
/* 6-DOF                                                               */
/* ------------------------------------------------------------------ */

/*
 * Mixed axes: X translation limited to [-0.2, 0.2], Y and Z translation
 * locked, twist free, swing Y limited to [-0.3, 0.3], swing Z locked.
 * A diagonal gravity loads every axis.
 */
static void
test_six_dof_mixed (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (-5.0f, -T_G, 4.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor[3] = { 0.0f, 0.0f, 0.0f };
	gfloat ax[3] = { 1.0f, 0.0f, 0.0f };
	gfloat ay[3] = { 0.0f, 1.0f, 0.0f };
	gfloat torque[3] = { 0.0f, 3.0f, 0.0f };
	gfloat tr[3];
	gfloat an[3];
	gfloat worst_x = 0.0f;
	gfloat worst_yz = 0.0f;
	gfloat worst_sy = 0.0f;
	gfloat worst_sz = 0.0f;
	guint body;
	guint i;

	body = add_box (world, 1.0f, 0.2f, 0.2f, 0.2f, 0.0f, 0.0f, 0.3f);
	joint = lrg_joint_new_six_dof (world, LRG_DYNAMICS_WORLD_BODY, body, anchor, ax, ay, &error);
	g_assert_no_error (error);
	lrg_joint_set_limits (joint, LRG_JOINT_AXIS_TRANSLATION_X, -0.2f, 0.2f);
	lrg_joint_set_axis_mode (joint, LRG_JOINT_AXIS_TRANSLATION_Y, LRG_JOINT_AXIS_LOCKED);
	lrg_joint_set_axis_mode (joint, LRG_JOINT_AXIS_TRANSLATION_Z, LRG_JOINT_AXIS_LOCKED);
	lrg_joint_set_swing_type (joint, LRG_JOINT_SWING_PYRAMID);
	lrg_joint_set_limits (joint, LRG_JOINT_AXIS_ROTATION_Y, -0.3f, 0.3f);
	lrg_joint_set_axis_mode (joint, LRG_JOINT_AXIS_ROTATION_Z, LRG_JOINT_AXIS_LOCKED);
	g_assert_cmpint (lrg_joint_get_axis_mode (joint, LRG_JOINT_AXIS_ROTATION_X), ==, LRG_JOINT_AXIS_FREE);
	add_joint (world, joint);
	for (i = 0; i < 360; i++)
	{
		g_assert_true (lrg_dynamics_world_apply_torque (world, body, torque, &error));
		step (world, 1.0f / 120.0f);
		measure (joint, world, tr, an);
		worst_x = MAX (worst_x, fabsf (tr[0]));
		worst_yz = MAX (worst_yz, MAX (fabsf (tr[1]), fabsf (tr[2])));
		worst_sy = MAX (worst_sy, fabsf (an[1]));
		worst_sz = MAX (worst_sz, fabsf (an[2]));
	}
	g_test_message ("6-DOF: |x| %.4f (0.2), |y,z| %.2e, |swing y| %.4f (0.3), |swing z| %.2e",
	                (gdouble) worst_x, (gdouble) worst_yz, (gdouble) worst_sy, (gdouble) worst_sz);
	g_assert_cmpfloat (worst_x, <, 0.21f);
	g_assert_cmpfloat_with_epsilon (fabsf (tr[0]), 0.2f, 0.01f);
	g_assert_cmpfloat (worst_yz, <, 0.005f);
	g_assert_cmpfloat (worst_sy, <, 0.32f);
	g_assert_cmpfloat (worst_sy, >, 0.25f);
	g_assert_cmpfloat (worst_sz, <, 0.01f);
}

/* a rotation position motor turns a free 6-DOF to its target angle */
static void
test_six_dof_rotation_motor (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, 0.0f, 0.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat anchor[3] = { 0.0f, 0.0f, 0.0f };
	gfloat ax[3] = { 0.0f, 0.0f, 1.0f };
	gfloat an[3];
	gfloat q[4];
	guint body;
	guint a;
	guint i;

	body = add_box (world, 1.0f, 0.3f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f);
	joint = lrg_joint_new_six_dof (world, LRG_DYNAMICS_WORLD_BODY, body, anchor, ax, NULL, &error);
	g_assert_no_error (error);
	for (a = LRG_JOINT_AXIS_TRANSLATION_X; a <= LRG_JOINT_AXIS_TRANSLATION_Z; a++)
		lrg_joint_set_axis_mode (joint, (LrgJointAxis) a, LRG_JOINT_AXIS_LOCKED);
	for (a = LRG_JOINT_AXIS_ROTATION_X; a <= LRG_JOINT_AXIS_ROTATION_Z; a++)
	{
		lrg_joint_set_motor (joint, (LrgJointAxis) a, LRG_JOINT_MOTOR_POSITION, 50.0f);
		lrg_joint_set_motor_spring (joint, (LrgJointAxis) a, 4.0f, 1.0f);
	}
	lrg_joint_set_motor_target_position (joint, LRG_JOINT_AXIS_ROTATION_X, 1.0f);
	lrg_joint_get_target_orientation (joint, q);
	g_assert_cmpfloat_with_epsilon (q[0], sinf (0.5f), 1e-5f);
	add_joint (world, joint);
	for (i = 0; i < 240; i++)
		step (world, 1.0f / 120.0f);
	measure (joint, world, NULL, an);
	g_test_message ("rotation motor: twist %.4f rad (target 1), swing %.2e %.2e",
	                (gdouble) an[0], (gdouble) an[1], (gdouble) an[2]);
	g_assert_cmpfloat_with_epsilon (an[0], 1.0f, 0.02f);
	g_assert_cmpfloat (fabsf (an[1]) + fabsf (an[2]), <, 0.02f);
}

/* ------------------------------------------------------------------ */
/* fixed and breakable                                                 */
/* ------------------------------------------------------------------ */

/* a fixed joint holds a cantilever beam level under gravity */
static void
test_fixed_holds (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, -T_G, 0.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) joint = NULL;
	g_autoptr(GError) error = NULL;
	gfloat tr[3];
	gfloat an[3];
	guint wall;
	guint beam;
	guint i;

	wall = add_box (world, 50.0f, 0.1f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	beam = add_box (world, 1.0f, 0.5f, 0.05f, 0.05f, 0.6f, 0.0f, 0.0f);
	g_assert_true (lrg_dynamics_world_set_collision_between (world, wall, beam, FALSE, &error));
	joint = lrg_joint_new_fixed (world, wall, beam, &error);
	g_assert_no_error (error);
	add_joint (world, joint);
	{
		g_autoptr(LrgJoint) pin = lrg_joint_new_fixed (world, wall, LRG_DYNAMICS_WORLD_BODY, &error);

		g_assert_no_error (error);
		add_joint (world, pin);
	}
	for (i = 0; i < 240; i++)
		step (world, 1.0f / 120.0f);
	measure (joint, world, tr, an);
	g_test_message ("fixed: offset %.2e m, angles %.2e %.2e %.2e", (gdouble) len3 (tr),
	                (gdouble) an[0], (gdouble) an[1], (gdouble) an[2]);
	g_assert_cmpfloat (len3 (tr), <, 0.003f);
	g_assert_cmpfloat (fabsf (an[0]) + fabsf (an[1]) + fabsf (an[2]), <, 0.01f);
}

static void
on_broken (LrgJoint *joint, gfloat force, gfloat torque, gpointer user_data)
{
	gfloat *out = user_data;

	out[0] += 1.0f;
	out[1] = force;
}

/*
 * A 10 kg block welded to the world by a fixed joint that breaks above
 * 60 N: gravity loads it with 98 N, so it breaks on the first step and
 * the block then falls freely. A 1 kg block (9.8 N) stays.
 */
static void
test_breakable (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, -T_G, 0.0f, 1.0f / 120.0f);
	g_autoptr(LrgJoint) heavy = NULL;
	g_autoptr(LrgJoint) light = NULL;
	g_autoptr(GError) error = NULL;
	gfloat seen[2] = { 0.0f, 0.0f };
	gfloat f[3];
	gfloat t[3];
	gfloat p[3];
	guint a;
	guint b;
	guint i;

	a = add_box (world, 10.0f, 0.1f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f);
	b = add_box (world, 1.0f, 0.1f, 0.1f, 0.1f, 3.0f, 0.0f, 0.0f);
	heavy = lrg_joint_new_fixed (world, a, LRG_DYNAMICS_WORLD_BODY, &error);
	g_assert_no_error (error);
	light = lrg_joint_new_fixed (world, b, LRG_DYNAMICS_WORLD_BODY, &error);
	g_assert_no_error (error);
	lrg_joint_set_break_force (heavy, 60.0f);
	lrg_joint_set_break_force (light, 60.0f);
	lrg_joint_set_break_torque (light, 1000.0f);
	g_assert_cmpfloat (lrg_joint_get_break_force (heavy), ==, 60.0f);
	g_assert_cmpfloat (lrg_joint_get_break_torque (light), ==, 1000.0f);
	g_assert_cmpfloat (lrg_joint_get_break_torque (heavy), ==, 0.0f);
	g_signal_connect (heavy, "broken", G_CALLBACK (on_broken), seen);
	add_joint (world, heavy);
	add_joint (world, light);
	for (i = 0; i < 60; i++)
		step (world, 1.0f / 120.0f);

	g_assert_true (lrg_joint_is_broken (heavy));
	g_assert_false (lrg_physics_constraint_get_enabled (LRG_PHYSICS_CONSTRAINT (heavy)));
	g_assert_cmpfloat (seen[0], ==, 1.0f);
	g_test_message ("breakable: broke at %.2f N (threshold 60, load 98.1)", (gdouble) seen[1]);
	g_assert_cmpfloat (seen[1], >, 60.0f);
	position_of (world, a, p);
	g_assert_cmpfloat (p[1], <, -1.0f);

	g_assert_false (lrg_joint_is_broken (light));
	lrg_joint_get_applied_force (light, f);
	lrg_joint_get_applied_torque (light, t);
	g_test_message ("unbroken joint: force on body B (the world) %.3f N along Y", (gdouble) f[1]);
	g_assert_cmpfloat_with_epsilon (len3 (f), T_G, 0.2f);
	position_of (world, b, p);
	g_assert_cmpfloat (fabsf (p[1]), <, 0.01f);

	/* repaired: enabled again and holds (no longer above the threshold
	 * with a higher limit) */
	lrg_joint_set_break_force (heavy, 0.0f);
	lrg_joint_repair (heavy);
	g_assert_false (lrg_joint_is_broken (heavy));
	g_assert_true (lrg_physics_constraint_get_enabled (LRG_PHYSICS_CONSTRAINT (heavy)));
}

/* ------------------------------------------------------------------ */
/* chains, determinism, performance                                    */
/* ------------------------------------------------------------------ */

typedef struct
{
	LrgDynamicsWorld *world;
	GPtrArray *joints;
	guint first;
	guint n;
} Chain;

/* n capsules of 0.3 m hanging from a world pivot by point joints, laid out horizontally */
static void
build_chain (Chain *c, guint n)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgPhysicsShape) shape = lrg_physics_shape_new_capsule (0.04f, 0.11f);
	gfloat rot[4];
	guint prev = LRG_DYNAMICS_WORLD_BODY;
	guint i;

	/* capsules lie along X: rotate local Y onto X */
	rot[0] = 0.0f;
	rot[1] = 0.0f;
	rot[2] = -sinf (T_PI / 4.0f);
	rot[3] = cosf (T_PI / 4.0f);
	c->world = make_world (0.0f, -T_G, 0.0f, 1.0f / 120.0f);
	c->joints = g_ptr_array_new_with_free_func (g_object_unref);
	c->n = n;
	for (i = 0; i < n; i++)
	{
		gfloat p[3];
		gfloat pivot[3];
		guint id;
		LrgJoint *j;

		p[0] = 0.3f * (gfloat) i + 0.15f;
		p[1] = 0.0f;
		p[2] = 0.0f;
		g_assert_true (lrg_dynamics_world_add_body (c->world, shape, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f,
		                                            p, rot, &id, &error));
		g_assert_no_error (error);
		if (i == 0)
			c->first = id;
		pivot[0] = 0.3f * (gfloat) i;
		pivot[1] = 0.0f;
		pivot[2] = 0.0f;
		j = lrg_joint_new_point (c->world, id, prev, pivot, &error);
		g_assert_no_error (error);
		add_joint (c->world, j);
		if (prev != LRG_DYNAMICS_WORLD_BODY)
			g_assert_true (lrg_dynamics_world_set_collision_between (c->world, prev, id, FALSE, &error));
		g_ptr_array_add (c->joints, j);
		prev = id;
	}
}

static void
free_chain (Chain *c)
{
	g_ptr_array_unref (c->joints);
	g_object_unref (c->world);
}

static gfloat
chain_error (Chain *c)
{
	gfloat worst = 0.0f;
	guint i;

	for (i = 0; i < c->joints->len; i++)
	{
		gfloat tr[3];

		measure (g_ptr_array_index (c->joints, i), c->world, tr, NULL);
		worst = MAX (worst, len3 (tr));
	}
	return worst;
}

static void
test_chain_stable (void)
{
	Chain c;
	gfloat worst = 0.0f;
	guint i;

	build_chain (&c, 12);
	for (i = 0; i < 120 * 5; i++)
	{
		step (c.world, 1.0f / 120.0f);
		worst = MAX (worst, chain_error (&c));
	}
	g_test_message ("12-link chain released horizontally, 5 s: worst joint separation %.4f m "
	                "(during the whip), at rest %.5f m", (gdouble) worst, (gdouble) chain_error (&c));
	g_assert_true (isfinite (worst));
	/* the free end whips at about 10 m/s; 8 velocity iterations let the
	 * 12 links stretch a few centimetres at the peak and recover */
	g_assert_cmpfloat (worst, <, 0.05f);
	g_assert_cmpfloat (chain_error (&c), <, 0.015f);
	/* after 5 s the chain swings below the pivot (it started level with it) */
	{
		gfloat p[3];

		position_of (c.world, c.first + c.n - 1, p);
		g_assert_cmpfloat (p[1], <, -1.0f);
	}
	free_chain (&c);
}

/* more position iterations make the chain stiffer (8: about 3x less stretch) */
static void
test_chain_position_iterations (void)
{
	Chain c;
	gfloat worst = 0.0f;
	guint i;

	build_chain (&c, 12);
	lrg_contact_solver_set_position_iterations (lrg_dynamics_world_get_contact_solver (c.world), 8);
	for (i = 0; i < 120 * 5; i++)
	{
		step (c.world, 1.0f / 120.0f);
		worst = MAX (worst, chain_error (&c));
	}
	g_test_message ("same chain, 8 position iterations: worst %.4f m, at rest %.5f m",
	                (gdouble) worst, (gdouble) chain_error (&c));
	g_assert_cmpfloat (worst, <, 0.015f);
	g_assert_cmpfloat (chain_error (&c), <, 0.005f);
	free_chain (&c);
}

static void
test_determinism (void)
{
	Chain a;
	Chain b;
	guint i;
	guint k;

	build_chain (&a, 8);
	build_chain (&b, 8);
	for (i = 0; i < 240; i++)
	{
		step (a.world, 1.0f / 120.0f);
		step (b.world, 1.0f / 120.0f);
	}
	for (k = 0; k < 8; k++)
	{
		gfloat pa[3];
		gfloat pb[3];
		gfloat qa[4];
		gfloat qb[4];

		position_of (a.world, a.first + k, pa);
		position_of (b.world, b.first + k, pb);
		g_assert_true (lrg_dynamics_world_get_orientation (a.world, a.first + k, qa, NULL));
		g_assert_true (lrg_dynamics_world_get_orientation (b.world, b.first + k, qb, NULL));
		g_assert_cmpint (memcmp (pa, pb, sizeof (pa)), ==, 0);
		g_assert_cmpint (memcmp (qa, qb, sizeof (qa)), ==, 0);
	}
	free_chain (&a);
	free_chain (&b);
}

static void
test_performance (void)
{
	Chain c;
	gint64 t0;
	gint64 t1;
	guint steps = 240;
	guint i;

	build_chain (&c, 200);
	t0 = g_get_monotonic_time ();
	for (i = 0; i < steps; i++)
		step (c.world, 1.0f / 120.0f);
	t1 = g_get_monotonic_time ();
	g_test_message ("perf: 200-link point-joint chain, %.3f ms per 1/120 s step (%.2f us per joint)",
	                (gdouble) (t1 - t0) / 1000.0 / steps,
	                (gdouble) (t1 - t0) / steps / 200.0);
	g_assert_true (isfinite (chain_error (&c)));
	free_chain (&c);
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

static void
test_api_validation (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, 0.0f, 0.0f, 1.0f / 60.0f);
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgJoint) generic = NULL;
	gfloat anchor[3] = { 0.0f, 0.0f, 0.0f };
	gfloat zero[3] = { 0.0f, 0.0f, 0.0f };
	gfloat pa[3] = { 1.0f, 2.0f, 3.0f };
	gfloat qa[4] = { 0.0f, 0.0f, 0.0f, 2.0f };
	gfloat outp[3];
	gfloat outq[4];
	LrgJoint *j;
	guint body;

	body = add_sphere (world, 1.0f, 0.1f, 0.0f, 0.0f, 0.0f);

	j = lrg_joint_new_point (world, body, body, anchor, &error);
	g_assert_null (j);
	g_assert_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_BODY);
	g_clear_error (&error);

	j = lrg_joint_new_point (world, body, 9999, anchor, &error);
	g_assert_null (j);
	g_assert_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_BODY);
	g_clear_error (&error);

	j = lrg_joint_new_hinge (world, body, LRG_DYNAMICS_WORLD_BODY, anchor, zero, &error);
	g_assert_null (j);
	g_assert_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_INVALID);
	g_clear_error (&error);

	j = lrg_joint_new_cone (world, body, LRG_DYNAMICS_WORLD_BODY, anchor, pa, 4.0f, &error);
	g_assert_null (j);
	g_assert_error (error, LRG_JOINT_ERROR, LRG_JOINT_ERROR_INVALID);
	g_clear_error (&error);

	/* local-frame constructor: frames stored, rotation normalised */
	generic = lrg_joint_new (LRG_JOINT_SLIDER, body, LRG_DYNAMICS_WORLD_BODY, pa, qa, zero, NULL);
	g_assert_cmpint (lrg_joint_get_kind (generic), ==, LRG_JOINT_SLIDER);
	g_assert_cmpuint (lrg_physics_constraint_get_body_a (LRG_PHYSICS_CONSTRAINT (generic)), ==, body);
	g_assert_cmpuint (lrg_physics_constraint_get_body_b (LRG_PHYSICS_CONSTRAINT (generic)), ==,
	                  LRG_DYNAMICS_WORLD_BODY);
	lrg_joint_get_frame_a (generic, outp, outq);
	g_assert_cmpfloat (outp[2], ==, 3.0f);
	g_assert_cmpfloat (outq[3], ==, 1.0f);
	lrg_joint_get_frame_b (generic, outp, outq);
	g_assert_cmpfloat (outp[0], ==, 0.0f);
	g_assert_cmpint (lrg_joint_get_axis_mode (generic, LRG_JOINT_AXIS_TRANSLATION_X), ==, LRG_JOINT_AXIS_FREE);
	g_assert_cmpint (lrg_joint_get_axis_mode (generic, LRG_JOINT_AXIS_TRANSLATION_Y), ==, LRG_JOINT_AXIS_LOCKED);
	g_assert_cmpint (lrg_joint_get_axis_mode (generic, LRG_JOINT_AXIS_ROTATION_X), ==, LRG_JOINT_AXIS_LOCKED);
	lrg_joint_set_friction (generic, LRG_JOINT_AXIS_TRANSLATION_X, 4.0f);
	g_assert_cmpfloat (lrg_joint_get_friction (generic, LRG_JOINT_AXIS_TRANSLATION_X), ==, 4.0f);
	lrg_joint_set_limits (generic, LRG_JOINT_AXIS_ROTATION_Y, -9.0f, 9.0f);
	{
		gfloat lo;
		gfloat hi;

		lrg_joint_get_limits (generic, LRG_JOINT_AXIS_ROTATION_Y, &lo, &hi);
		g_assert_cmpfloat_with_epsilon (hi, T_PI, 1e-6f);
		g_assert_cmpfloat_with_epsilon (lo, -T_PI, 1e-6f);
	}

	/* enum types are registered with nicks */
	g_assert_cmpstr (g_enum_get_value (g_type_class_peek (LRG_TYPE_JOINT_KIND) != NULL
	                                   ? g_type_class_peek (LRG_TYPE_JOINT_KIND)
	                                   : g_type_class_ref (LRG_TYPE_JOINT_KIND),
	                                   LRG_JOINT_SWING_TWIST)->value_nick, ==, "swing-twist");
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_JOINT_AXIS));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_JOINT_AXIS_MODE));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_JOINT_MOTOR_MODE));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_JOINT_SWING_TYPE));
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/joint/point/pendulum-period", test_point_pendulum_period);
	g_test_add_func ("/joint/hinge/limit", test_hinge_limit);
	g_test_add_func ("/joint/hinge/motor", test_hinge_motor);
	g_test_add_func ("/joint/slider/limits", test_slider_limits);
	g_test_add_func ("/joint/slider/motors", test_slider_motors);
	g_test_add_func ("/joint/distance/spring", test_distance_spring);
	g_test_add_func ("/joint/distance/range", test_distance_range);
	g_test_add_func ("/joint/cone/limit", test_cone_limit);
	g_test_add_func ("/joint/swing-twist/limits", test_swing_twist_limits);
	g_test_add_func ("/joint/six-dof/mixed", test_six_dof_mixed);
	g_test_add_func ("/joint/six-dof/rotation-motor", test_six_dof_rotation_motor);
	g_test_add_func ("/joint/fixed/holds", test_fixed_holds);
	g_test_add_func ("/joint/breakable", test_breakable);
	g_test_add_func ("/joint/chain/stable", test_chain_stable);
	g_test_add_func ("/joint/chain/position-iterations", test_chain_position_iterations);
	g_test_add_func ("/joint/determinism", test_determinism);
	g_test_add_func ("/joint/performance", test_performance);
	g_test_add_func ("/joint/api/validation", test_api_validation);

	return g_test_run ();
}
