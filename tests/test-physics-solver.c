/* test-physics-solver.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for the 0.9 LrgDynamicsWorld solver: stacking and
 * pyramids, restitution and friction against closed forms, islands and
 * sleeping, CCD, kinematic bodies, sensors, contact signals, world
 * queries with layers, determinism, the LrgPhysicsConstraint hook, and a
 * 1000-box performance figure. Run the perf case pinned to one core:
 *   taskset -c 2 tests/build/release/tests/test-physics-solver -m perf
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define DT (1.0f / 60.0f)

static void
require (gboolean  ok,
         GError  **error)
{
	if (!ok)
		g_error ("%s", (error != NULL && *error != NULL) ? (*error)->message : "dynamics call failed");
}

static LrgDynamicsWorld *
make_world (void)
{
	LrgDynamicsWorld *world;
	g_autoptr(GError) error = NULL;
	gfloat g[3];

	world = lrg_dynamics_world_new ();
	g[0] = 0.0f;
	g[1] = -9.81f;
	g[2] = 0.0f;
	require (lrg_dynamics_world_set_gravity (world, g, &error), &error);
	require (lrg_dynamics_world_set_step_limits (world, DT, 4, &error), &error);
	return world;
}

static guint
add_box (LrgDynamicsWorld    *world,
         LrgDynamicsBodyKind  kind,
         gfloat               mass,
         gfloat               hx,
         gfloat               hy,
         gfloat               hz,
         gfloat               x,
         gfloat               y,
         gfloat               z,
         const gfloat        *q)
{
	g_autoptr(LrgPhysicsShape) shape = NULL;
	g_autoptr(GError) error = NULL;
	gfloat p[3];
	guint id;

	shape = lrg_physics_shape_new_box (hx, hy, hz);
	p[0] = x;
	p[1] = y;
	p[2] = z;
	id = G_MAXUINT;
	require (lrg_dynamics_world_add_body (world, shape, kind, mass, p, q, &id, &error), &error);
	return id;
}

static guint
add_ball (LrgDynamicsWorld    *world,
          LrgDynamicsBodyKind  kind,
          gfloat               mass,
          gfloat               r,
          gfloat               x,
          gfloat               y,
          gfloat               z)
{
	g_autoptr(LrgPhysicsShape) shape = NULL;
	g_autoptr(GError) error = NULL;
	gfloat p[3];
	guint id;

	shape = lrg_physics_shape_new_sphere (r);
	p[0] = x;
	p[1] = y;
	p[2] = z;
	id = G_MAXUINT;
	require (lrg_dynamics_world_add_body (world, shape, kind, mass, p, NULL, &id, &error), &error);
	return id;
}

static guint
add_ground (LrgDynamicsWorld *world)
{
	return add_box (world, LRG_DYNAMICS_BODY_STATIC, 0.0f, 50.0f, 0.5f, 50.0f, 0.0f, -0.5f, 0.0f, NULL);
}

static void
step_n (LrgDynamicsWorld *world,
        guint             n)
{
	g_autoptr(GError) error = NULL;
	guint i;

	for (i = 0; i < n; i++)
		require (lrg_dynamics_world_step (world, DT, NULL, &error), &error);
}

static void
pos (LrgDynamicsWorld *world,
     guint             id,
     gfloat           *out)
{
	g_autoptr(GError) error = NULL;

	require (lrg_dynamics_world_get_position (world, id, out, &error), &error);
}

static gboolean
sleeping (LrgDynamicsWorld *world,
          guint             id)
{
	g_autoptr(GError) error = NULL;
	gboolean s;

	require (lrg_dynamics_world_get_sleeping (world, id, &s, &error), &error);
	return s;
}

/* ---------------------------------------------------------------------- */

static void
test_box_stack (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	guint ids[10];
	guint i;
	gfloat p[3];

	world = make_world ();
	add_ground (world);
	for (i = 0; i < 10u; i++)
		ids[i] = add_box (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 0.5f, 0.5f,
		                  0.0f, 0.5f + (gfloat) i * 1.0f, 0.0f, NULL);
	/* Ten seconds. */
	step_n (world, 600);
	for (i = 0; i < 10u; i++)
	{
		pos (world, ids[i], p);
		g_assert_cmpfloat (fabsf (p[0]), <, 0.05f);
		g_assert_cmpfloat (fabsf (p[2]), <, 0.05f);
		/* Resting height within the penetration slop per level. */
		g_assert_cmpfloat (fabsf (p[1] - (0.5f + (gfloat) i)), <, 0.05f + 0.02f * (gfloat) i);
	}
	/* A stack at rest goes to sleep as one island. */
	g_assert_true (sleeping (world, ids[9]));
	g_assert_true (sleeping (world, ids[0]));
	g_assert_cmpuint (lrg_dynamics_world_get_active_body_count (world), ==, 0);
}

static void
test_pyramid (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(GArray) ids = NULL;
	g_autoptr(GArray) start = NULL;
	guint row;
	guint i;
	guint base;

	world = make_world ();
	add_ground (world);
	ids = g_array_new (FALSE, FALSE, sizeof (guint));
	start = g_array_new (FALSE, FALSE, sizeof (gfloat));
	base = 8;
	for (row = 0; row < base; row++)
	{
		guint count;

		count = base - row;
		for (i = 0; i < count; i++)
		{
			gfloat x;
			gfloat y;
			guint id;

			x = ((gfloat) i - (gfloat) (count - 1u) * 0.5f) * 1.05f;
			y = 0.5f + (gfloat) row * 1.0f;
			id = add_box (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 0.5f, 0.5f, x, y, 0.0f, NULL);
			g_array_append_val (ids, id);
			g_array_append_val (start, x);
			g_array_append_val (start, y);
		}
	}
	g_assert_cmpuint (ids->len, ==, 36);
	step_n (world, 300);
	for (i = 0; i < ids->len; i++)
	{
		gfloat p[3];

		pos (world, g_array_index (ids, guint, i), p);
		g_assert_cmpfloat (fabsf (p[0] - g_array_index (start, gfloat, 2u * i)), <, 0.1f);
		g_assert_cmpfloat (fabsf (p[1] - g_array_index (start, gfloat, 2u * i + 1u)), <, 0.15f);
		g_assert_cmpfloat (fabsf (p[2]), <, 0.1f);
	}
}

/* Drop a ball from h with restitution e: the first rebound peaks near e^2 h. */
static void
test_restitution (void)
{
	const gfloat cases[3] = { 0.0f, 0.5f, 0.8f };
	guint c;

	for (c = 0; c < 3u; c++)
	{
		g_autoptr(LrgDynamicsWorld) world = NULL;
		g_autoptr(GError) error = NULL;
		guint ground;
		guint ball;
		guint i;
		gfloat p[3];
		gfloat peak;
		gboolean bounced;
		gfloat v[3];
		gfloat h;
		gfloat expected;

		world = make_world ();
		require (lrg_dynamics_world_set_step_limits (world, 1.0f / 240.0f, 8, &error), &error);
		lrg_contact_solver_set_restitution_threshold (lrg_dynamics_world_get_contact_solver (world), 0.2f);
		ground = add_ground (world);
		h = 2.0f;
		ball = add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.25f, 0.0f, 0.25f + h, 0.0f);
		require (lrg_dynamics_world_set_body_material (world, ground, 0.0f, cases[c], &error), &error);
		require (lrg_dynamics_world_set_body_material (world, ball, 0.0f, cases[c], &error), &error);
		peak = 0.0f;
		bounced = FALSE;
		for (i = 0; i < 480u * 2u; i++)
		{
			require (lrg_dynamics_world_step (world, 1.0f / 240.0f, NULL, &error), &error);
			require (lrg_dynamics_world_get_linear_velocity (world, ball, v, &error), &error);
			pos (world, ball, p);
			if (!bounced && v[1] > 0.0f)
				bounced = TRUE;
			if (bounced)
			{
				if (v[1] < 0.0f)
					break;
				peak = MAX (peak, p[1] - 0.25f);
			}
		}
		expected = cases[c] * cases[c] * h;
		g_test_message ("restitution %.2f: rebound %.4f m, expected %.4f m", cases[c], peak, expected);
		g_assert_cmpfloat (fabsf (peak - expected), <, 0.08f * h);
	}
}

/* A box on a 30-degree incline: slides with a = g (sin - mu cos) when mu
 * is below tan 30, stays put above it. */
static void
test_friction_incline (void)
{
	const gfloat theta = (gfloat) (G_PI / 6.0);
	const gfloat mus[2] = { 0.3f, 0.8f };
	guint c;

	for (c = 0; c < 2u; c++)
	{
		g_autoptr(LrgDynamicsWorld) world = NULL;
		g_autoptr(GError) error = NULL;
		gfloat q[4];
		gfloat n[3];
		gfloat down[3];
		gfloat p0[3];
		gfloat p[3];
		guint ramp;
		guint box;
		gfloat travelled;
		gfloat expected;
		gfloat t;
		guint i;

		world = make_world ();
		require (lrg_dynamics_world_set_step_limits (world, 1.0f / 240.0f, 8, &error), &error);
		q[0] = 0.0f;
		q[1] = 0.0f;
		q[2] = sinf (theta * 0.5f);
		q[3] = cosf (theta * 0.5f);
		n[0] = -sinf (theta);
		n[1] = cosf (theta);
		n[2] = 0.0f;
		down[0] = -cosf (theta);
		down[1] = -sinf (theta);
		down[2] = 0.0f;
		ramp = add_box (world, LRG_DYNAMICS_BODY_STATIC, 0.0f, 20.0f, 0.5f, 5.0f, 0.0f, 0.0f, 0.0f, q);
		box = add_box (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.25f, 0.25f, 0.25f,
		               n[0] * 0.75f, n[1] * 0.75f, 0.0f, q);
		require (lrg_dynamics_world_set_body_material (world, ramp, mus[c], 0.0f, &error), &error);
		require (lrg_dynamics_world_set_body_material (world, box, mus[c], 0.0f, &error), &error);
		pos (world, box, p0);
		t = 1.0f;
		for (i = 0; i < 240u; i++)
			require (lrg_dynamics_world_step (world, 1.0f / 240.0f, NULL, &error), &error);
		pos (world, box, p);
		travelled = (p[0] - p0[0]) * down[0] + (p[1] - p0[1]) * down[1] + (p[2] - p0[2]) * down[2];
		expected = 0.5f * 9.81f * (sinf (theta) - mus[c] * cosf (theta)) * t * t;
		if (expected < 0.0f)
			expected = 0.0f;
		g_test_message ("friction mu %.2f: slid %.4f m, expected %.4f m", mus[c], travelled, expected);
		if (expected > 0.0f)
			g_assert_cmpfloat (fabsf (travelled - expected), <, 0.1f * expected);
		else
			g_assert_cmpfloat (fabsf (travelled), <, 0.02f);
	}
}

static void
test_sleep_and_wake (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(GError) error = NULL;
	guint low;
	guint high;
	guint i;
	gboolean woke;

	world = make_world ();
	add_ground (world);
	low = add_box (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f, NULL);
	step_n (world, 120);
	g_assert_true (sleeping (world, low));
	g_assert_cmpuint (lrg_dynamics_world_get_active_body_count (world), ==, 0);

	/* A box dropped on the sleeper wakes it. */
	high = add_box (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f, 3.0f, 0.0f, NULL);
	woke = FALSE;
	for (i = 0; i < 120u; i++)
	{
		step_n (world, 1);
		if (!sleeping (world, low))
			woke = TRUE;
	}
	g_assert_true (woke);
	/* ...and both settle and sleep again. */
	step_n (world, 240);
	g_assert_true (sleeping (world, low));
	g_assert_true (sleeping (world, high));

	/* An impulse wakes a sleeper. */
	{
		gfloat j[3];

		j[0] = 1.0f;
		j[1] = 0.0f;
		j[2] = 0.0f;
		require (lrg_dynamics_world_apply_impulse (world, high, j, NULL, &error), &error);
		g_assert_false (sleeping (world, high));
	}
}

static void
run_bullet (gboolean  ccd,
            gfloat   *final_x)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(GError) error = NULL;
	gfloat zero[3];
	gfloat v[3];
	gfloat p[3];
	guint bullet;

	world = lrg_dynamics_world_new ();
	memset (zero, 0, sizeof (zero));
	require (lrg_dynamics_world_set_gravity (world, zero, &error), &error);
	add_box (world, LRG_DYNAMICS_BODY_STATIC, 0.0f, 0.05f, 2.0f, 2.0f, 5.0f, 0.0f, 0.0f, NULL);
	bullet = add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 0.01f, 0.05f, 0.0f, 0.0f, 0.0f);
	require (lrg_dynamics_world_set_ccd (world, bullet, ccd, &error), &error);
	v[0] = 400.0f;
	v[1] = 0.0f;
	v[2] = 0.0f;
	require (lrg_dynamics_world_set_linear_velocity (world, bullet, v, &error), &error);
	step_n (world, 30);
	pos (world, bullet, p);
	*final_x = p[0];
}

static void
test_ccd_bullet (void)
{
	gfloat x;

	/* 400 m/s moves 6.7 m per substep: without CCD it passes the wall. */
	run_bullet (FALSE, &x);
	g_assert_cmpfloat (x, >, 5.05f);
	run_bullet (TRUE, &x);
	g_test_message ("CCD bullet stopped at x = %.4f (wall face at 4.95)", x);
	g_assert_cmpfloat (x, <, 4.95f);
}

static void
test_kinematic_push (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(GError) error = NULL;
	guint pusher;
	guint crate;
	guint i;
	gfloat p[3];
	gfloat target[3];
	gfloat kp[3];

	world = make_world ();
	add_ground (world);
	pusher = add_box (world, LRG_DYNAMICS_BODY_KINEMATIC, 0.0f, 0.5f, 0.5f, 0.5f, -2.0f, 0.5f, 0.0f, NULL);
	crate = add_box (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f, NULL);
	require (lrg_dynamics_world_set_body_material (world, crate, 0.2f, 0.0f, &error), &error);
	/* Drive the pusher at 1 m/s for 3 s: it travels 3 m, the crate moves
	 * with it once they touch. */
	for (i = 0; i < 180u; i++)
	{
		pos (world, pusher, kp);
		target[0] = -2.0f + (gfloat) (i + 1u) * DT;
		target[1] = 0.5f;
		target[2] = 0.0f;
		require (lrg_dynamics_world_move_kinematic (world, pusher, target, NULL, DT, &error), &error);
		step_n (world, 1);
	}
	pos (world, pusher, kp);
	pos (world, crate, p);
	g_test_message ("kinematic at %.3f, crate at %.3f", kp[0], p[0]);
	g_assert_cmpfloat (fabsf (kp[0] - 1.0f), <, 0.01f);
	g_assert_cmpfloat (fabsf (kp[1] - 0.5f), <, 1.0e-4f);
	/* The crate is in front of the pusher, about one box width ahead. */
	g_assert_cmpfloat (p[0], >, kp[0] + 0.9f);
	g_assert_cmpfloat (p[0], <, kp[0] + 1.2f);
}

typedef struct
{
	guint entered;
	guint exited;
	guint last_sensor;
	guint last_other;
	guint added;
	guint removed;
	gfloat speed;
} Counts;

static void
on_entered (LrgDynamicsWorld *world,
            guint             sensor,
            guint             other,
            gpointer          data)
{
	Counts *c;

	(void) world;
	c = data;
	c->entered++;
	c->last_sensor = sensor;
	c->last_other = other;
}

static void
on_exited (LrgDynamicsWorld *world,
           guint             sensor,
           guint             other,
           gpointer          data)
{
	Counts *c;

	(void) world;
	c = data;
	c->exited++;
	c->last_sensor = sensor;
	c->last_other = other;
}

static void
on_added (LrgDynamicsWorld   *world,
          LrgDynamicsContact *contact,
          gpointer            data)
{
	Counts *c;

	(void) world;
	c = data;
	c->added++;
	c->speed = MAX (c->speed, contact->relative_speed);
}

static void
on_removed (LrgDynamicsWorld *world,
            guint             a,
            guint             b,
            gpointer          data)
{
	Counts *c;

	(void) world;
	(void) a;
	(void) b;
	c = data;
	c->removed++;
}

static void
test_sensors (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GArray) inside = NULL;
	Counts counts;
	guint trigger;
	guint ball;
	guint i;
	gboolean seen_inside;

	world = make_world ();
	memset (&counts, 0, sizeof (counts));
	g_signal_connect (world, "sensor-entered", G_CALLBACK (on_entered), &counts);
	g_signal_connect (world, "sensor-exited", G_CALLBACK (on_exited), &counts);
	trigger = add_box (world, LRG_DYNAMICS_BODY_STATIC, 0.0f, 1.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, NULL);
	require (lrg_dynamics_world_set_sensor (world, trigger, TRUE, &error), &error);
	ball = add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 0.0f, 2.0f, 0.0f);
	seen_inside = FALSE;
	for (i = 0; i < 120u; i++)
	{
		step_n (world, 1);
		inside = lrg_dynamics_world_get_sensor_overlaps (world, trigger);
		if (inside->len == 1 && g_array_index (inside, guint, 0) == ball)
			seen_inside = TRUE;
		g_clear_pointer (&inside, g_array_unref);
	}
	/* The ball fell straight through (no response) and was seen inside. */
	g_assert_true (seen_inside);
	g_assert_cmpuint (counts.entered, ==, 1);
	g_assert_cmpuint (counts.exited, ==, 1);
	g_assert_cmpuint (counts.last_sensor, ==, trigger);
	g_assert_cmpuint (counts.last_other, ==, ball);
	{
		gfloat p[3];

		pos (world, ball, p);
		g_assert_cmpfloat (p[1], <, -2.0f);
	}
}

static void
test_contact_signals (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(GError) error = NULL;
	Counts counts;
	guint ball;
	LrgDynamicsContact info;

	world = make_world ();
	memset (&counts, 0, sizeof (counts));
	g_signal_connect (world, "contact-added", G_CALLBACK (on_added), &counts);
	g_signal_connect (world, "contact-removed", G_CALLBACK (on_removed), &counts);
	add_ground (world);
	ball = add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.25f, 0.0f, 1.25f, 0.0f);
	step_n (world, 60);
	g_assert_cmpuint (counts.added, ==, 1);
	/* sqrt (2 g h) for the 1 m fall. */
	g_assert_cmpfloat (fabsf (counts.speed - sqrtf (2.0f * 9.81f)), <, 0.4f);
	g_assert_cmpuint (lrg_dynamics_world_get_contact_count (world), >=, 1);
	require (lrg_dynamics_world_get_contact_info (world, 0, &info, &error), &error);
	g_assert_cmpint (info.kind, ==, LRG_DYNAMICS_CONTACT_SHAPES);
	g_assert_cmpfloat (info.normal_impulse, >, 0.0f);
	/* Removing the ball reports the removal at the next step. */
	require (lrg_dynamics_world_remove_body (world, ball, &error), &error);
	step_n (world, 1);
	g_assert_cmpuint (counts.removed, ==, 1);
	g_assert_null (lrg_dynamics_world_get_shape (world, ball));
}

static void
test_queries (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgPhysicsShape) probe = NULL;
	g_autoptr(GArray) all = NULL;
	g_autoptr(GArray) over = NULL;
	LrgRayHit hit;
	gfloat origin[3];
	gfloat down[3];
	guint ground;
	guint crate;
	guint ball;

	world = make_world ();
	ground = add_ground (world);
	crate = add_box (world, LRG_DYNAMICS_BODY_STATIC, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f, NULL);
	ball = add_ball (world, LRG_DYNAMICS_BODY_STATIC, 0.0f, 0.5f, 3.0f, 0.5f, 0.0f);
	require (lrg_dynamics_world_set_body_layer (world, crate, 3, LRG_PHYSICS_LAYERS_ALL, &error), &error);

	origin[0] = 0.0f;
	origin[1] = 10.0f;
	origin[2] = 0.0f;
	down[0] = 0.0f;
	down[1] = -1.0f;
	down[2] = 0.0f;

	/* Closest: the crate top at y = 1. */
	g_assert_true (lrg_dynamics_world_raycast (world, origin, down, 100.0f, LRG_PHYSICS_LAYERS_ALL,
	                                           G_MAXUINT, &hit));
	g_assert_cmpuint (hit.body, ==, crate);
	g_assert_cmpfloat_with_epsilon (hit.fraction, 9.0f, 1.0e-3f);
	g_assert_cmpfloat_with_epsilon (hit.normal[1], 1.0f, 1.0e-3f);

	/* Masking out layer 3 sees the ground (top at y = 0). */
	g_assert_true (lrg_dynamics_world_raycast (world, origin, down, 100.0f,
	                                           LRG_PHYSICS_LAYERS_ALL & ~LRG_PHYSICS_LAYER_BIT (3),
	                                           G_MAXUINT, &hit));
	g_assert_cmpuint (hit.body, ==, ground);
	g_assert_cmpfloat_with_epsilon (hit.fraction, 10.0f, 1.0e-3f);

	/* Ignoring the crate does the same. */
	g_assert_true (lrg_dynamics_world_raycast (world, origin, down, 100.0f, LRG_PHYSICS_LAYERS_ALL,
	                                           crate, &hit));
	g_assert_cmpuint (hit.body, ==, ground);

	/* Too short a ray misses. */
	g_assert_false (lrg_dynamics_world_raycast (world, origin, down, 5.0f, LRG_PHYSICS_LAYERS_ALL,
	                                            G_MAXUINT, &hit));

	/* All hits, sorted. */
	all = lrg_dynamics_world_raycast_all (world, origin, down, 100.0f, LRG_PHYSICS_LAYERS_ALL, G_MAXUINT);
	g_assert_cmpuint (all->len, ==, 2);
	g_assert_cmpuint (g_array_index (all, LrgRayHit, 0).body, ==, crate);
	g_assert_cmpuint (g_array_index (all, LrgRayHit, 1).body, ==, ground);

	/* Shape cast: a 0.25 sphere dropped over the ball (top at y = 1)
	 * touches when its centre is at y = 1.25: t = 8.75. */
	probe = lrg_physics_shape_new_sphere (0.25f);
	origin[0] = 3.0f;
	g_assert_true (lrg_dynamics_world_shape_cast (world, probe, origin, NULL, down, 100.0f,
	                                              LRG_PHYSICS_LAYERS_ALL, G_MAXUINT, &hit));
	g_assert_cmpuint (hit.body, ==, ball);
	g_assert_cmpfloat (fabsf (hit.fraction - 8.75f), <, 0.01f);

	/* Overlap: a sphere at the crate's corner touches crate and ground. */
	origin[0] = 0.5f;
	origin[1] = 0.1f;
	origin[2] = 0.5f;
	over = lrg_dynamics_world_overlap_shape (world, probe, origin, NULL, LRG_PHYSICS_LAYERS_ALL, G_MAXUINT);
	g_assert_cmpuint (over->len, ==, 2);
	g_assert_cmpuint (g_array_index (over, guint, 0), ==, ground);
	g_assert_cmpuint (g_array_index (over, guint, 1), ==, crate);
	g_clear_pointer (&over, g_array_unref);
	over = lrg_dynamics_world_overlap_shape (world, probe, origin, NULL, LRG_PHYSICS_LAYER_BIT (3), G_MAXUINT);
	g_assert_cmpuint (over->len, ==, 1);
	g_assert_cmpuint (g_array_index (over, guint, 0), ==, crate);
}

/* Layers: two boxes on layers the matrix keeps apart fall through each other. */
static void
test_layers (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(LrgPhysicsLayers) layers = NULL;
	g_autoptr(GError) error = NULL;
	guint ground;
	guint debris;
	gfloat p[3];

	world = make_world ();
	layers = lrg_physics_layers_new_default ();
	lrg_physics_layers_set_collides (layers, LRG_PHYSICS_LAYER_STATIC, LRG_PHYSICS_LAYER_DEBRIS, FALSE);
	lrg_dynamics_world_set_layers (world, layers);
	ground = add_ground (world);
	require (lrg_dynamics_world_set_body_layer (world, ground, LRG_PHYSICS_LAYER_STATIC,
	                                            LRG_PHYSICS_LAYERS_ALL, &error), &error);
	debris = add_box (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 0.2f, 0.2f, 0.0f, 1.0f, 0.0f, NULL);
	require (lrg_dynamics_world_set_body_layer (world, debris, LRG_PHYSICS_LAYER_DEBRIS,
	                                            LRG_PHYSICS_LAYERS_ALL, &error), &error);
	step_n (world, 60);
	pos (world, debris, p);
	g_assert_cmpfloat (p[1], <, -1.0f);
}

static void
run_pile (guint   n,
          guint   steps,
          GArray *out)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	guint i;

	world = make_world ();
	add_ground (world);
	for (i = 0; i < n; i++)
	{
		gfloat q[4];
		gfloat a;

		/* A deterministic jumble: a 5 x 5 column grid, slight turns. */
		a = (gfloat) (i % 7u) * 0.3f;
		q[0] = 0.0f;
		q[1] = sinf (a * 0.5f);
		q[2] = 0.0f;
		q[3] = cosf (a * 0.5f);
		add_box (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.4f, 0.4f, 0.4f,
		         ((gfloat) (i % 5u) - 2.0f) * 1.0f + (gfloat) ((i / 25u) % 2u) * 0.3f,
		         0.5f + (gfloat) (i / 25u) * 0.9f,
		         ((gfloat) ((i / 5u) % 5u) - 2.0f) * 1.0f, q);
	}
	step_n (world, steps);
	if (out != NULL)
	{
		g_autoptr(GArray) ids = NULL;

		ids = lrg_dynamics_world_get_body_ids (world);
		for (i = 0; i < ids->len; i++)
		{
			gfloat p[3];
			gfloat o[4];
			g_autoptr(GError) error = NULL;

			pos (world, g_array_index (ids, guint, i), p);
			require (lrg_dynamics_world_get_orientation (world, g_array_index (ids, guint, i), o, &error), &error);
			g_array_append_vals (out, p, 3);
			g_array_append_vals (out, o, 4);
		}
	}
}

static void
test_determinism (void)
{
	g_autoptr(GArray) a = NULL;
	g_autoptr(GArray) b = NULL;

	a = g_array_new (FALSE, FALSE, sizeof (gfloat));
	b = g_array_new (FALSE, FALSE, sizeof (gfloat));
	run_pile (100, 180, a);
	run_pile (100, 180, b);
	g_assert_cmpuint (a->len, ==, b->len);
	g_assert_cmpint (memcmp (a->data, b->data, a->len * sizeof (gfloat)), ==, 0);
}

/* ---------------------------------------------------------------------- */
/* A trivial constraint through the public hook: keeps a body's anchor at
 * a fixed distance from a world point (a pendulum rod). */

#define TEST_TYPE_ROD (test_rod_get_type ())
G_DECLARE_FINAL_TYPE (TestRod, test_rod, TEST, ROD, LrgPhysicsConstraint)

struct _TestRod
{
	LrgPhysicsConstraint parent_instance;
	gfloat pivot[3];
	gfloat length;
	gfloat n[3];
	gfloat point[3];
	gfloat mass;
	gfloat impulse;
	guint  setups;
	guint  warms;
	guint  velocities;
	guint  positions;
	guint  posts;
	guint  resets;
};

G_DEFINE_FINAL_TYPE (TestRod, test_rod, LRG_TYPE_PHYSICS_CONSTRAINT)

static void
rod_setup (LrgPhysicsConstraint *c,
           LrgSolverBody        *a,
           LrgSolverBody        *b,
           gfloat                dt)
{
	TestRod *rod;
	gfloat zero[3];
	gfloat d[3];
	gfloat len;

	(void) b;
	(void) dt;
	rod = TEST_ROD (c);
	rod->setups++;
	memset (zero, 0, sizeof (zero));
	lrg_solver_body_local_to_world (a, zero, rod->point);
	d[0] = rod->point[0] - rod->pivot[0];
	d[1] = rod->point[1] - rod->pivot[1];
	d[2] = rod->point[2] - rod->pivot[2];
	len = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	rod->n[0] = d[0] / len;
	rod->n[1] = d[1] / len;
	rod->n[2] = d[2] / len;
	rod->mass = 1.0f / lrg_solver_body_get_effective_mass_inv (a, rod->point, rod->n);
}

static void
rod_warm (LrgPhysicsConstraint *c,
          LrgSolverBody        *a,
          LrgSolverBody        *b,
          gfloat                ratio)
{
	TestRod *rod;
	gfloat j[3];

	(void) b;
	rod = TEST_ROD (c);
	rod->warms++;
	rod->impulse *= ratio;
	j[0] = rod->n[0] * rod->impulse;
	j[1] = rod->n[1] * rod->impulse;
	j[2] = rod->n[2] * rod->impulse;
	lrg_solver_body_apply_impulse (a, j, rod->point);
}

static gboolean
rod_velocity (LrgPhysicsConstraint *c,
              LrgSolverBody        *a,
              LrgSolverBody        *b,
              gfloat                dt)
{
	TestRod *rod;
	gfloat v[3];
	gfloat lambda;
	gfloat j[3];

	(void) b;
	(void) dt;
	rod = TEST_ROD (c);
	rod->velocities++;
	lrg_solver_body_get_point_velocity (a, rod->point, v);
	lambda = -(v[0] * rod->n[0] + v[1] * rod->n[1] + v[2] * rod->n[2]) * rod->mass;
	rod->impulse += lambda;
	j[0] = rod->n[0] * lambda;
	j[1] = rod->n[1] * lambda;
	j[2] = rod->n[2] * lambda;
	lrg_solver_body_apply_impulse (a, j, rod->point);
	return TRUE;
}

static gboolean
rod_position (LrgPhysicsConstraint *c,
              LrgSolverBody        *a,
              LrgSolverBody        *b,
              gfloat                dt,
              gfloat                baumgarte)
{
	TestRod *rod;
	gfloat zero[3];
	gfloat p[3];
	gfloat d[3];
	gfloat len;
	gfloat n[3];
	gfloat err;
	gfloat k;
	gfloat j[3];

	(void) b;
	(void) dt;
	rod = TEST_ROD (c);
	rod->positions++;
	memset (zero, 0, sizeof (zero));
	lrg_solver_body_local_to_world (a, zero, p);
	d[0] = p[0] - rod->pivot[0];
	d[1] = p[1] - rod->pivot[1];
	d[2] = p[2] - rod->pivot[2];
	len = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	n[0] = d[0] / len;
	n[1] = d[1] / len;
	n[2] = d[2] / len;
	err = len - rod->length;
	k = lrg_solver_body_get_effective_mass_inv (a, p, n);
	j[0] = -n[0] * baumgarte * err / k;
	j[1] = -n[1] * baumgarte * err / k;
	j[2] = -n[2] * baumgarte * err / k;
	lrg_solver_body_apply_position_correction (a, j, p, NULL);
	return TRUE;
}

static void
rod_post (LrgPhysicsConstraint *c,
          LrgSolverBody        *a,
          LrgSolverBody        *b,
          gfloat                dt)
{
	(void) a;
	(void) b;
	(void) dt;
	TEST_ROD (c)->posts++;
}

static void
rod_reset (LrgPhysicsConstraint *c)
{
	TEST_ROD (c)->resets++;
	TEST_ROD (c)->impulse = 0.0f;
}

static void
test_rod_class_init (TestRodClass *klass)
{
	LrgPhysicsConstraintClass *cc;

	cc = LRG_PHYSICS_CONSTRAINT_CLASS (klass);
	cc->setup = rod_setup;
	cc->warm_start = rod_warm;
	cc->solve_velocity = rod_velocity;
	cc->solve_position = rod_position;
	cc->post_solve = rod_post;
	cc->reset_warm_start = rod_reset;
}

static void
test_rod_init (TestRod *self)
{
	(void) self;
}

static void
test_constraint_hook (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(TestRod) rod = NULL;
	g_autoptr(GError) error = NULL;
	guint bob;
	guint id;
	guint i;
	gfloat worst;
	gfloat lowest;
	LrgContactSolver *solver;

	world = make_world ();
	require (lrg_dynamics_world_set_sleep (world, 1.0e-12f, 100000, &error), &error);
	bob = add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.1f, 2.0f, 5.0f, 0.0f);
	rod = g_object_new (TEST_TYPE_ROD, "body-a", bob, "body-b", LRG_DYNAMICS_WORLD_BODY, NULL);
	rod->pivot[0] = 0.0f;
	rod->pivot[1] = 5.0f;
	rod->pivot[2] = 0.0f;
	rod->length = 2.0f;
	require (lrg_dynamics_world_add_constraint (world, LRG_PHYSICS_CONSTRAINT (rod), &id, &error), &error);
	g_assert_true (lrg_physics_constraint_get_in_world (LRG_PHYSICS_CONSTRAINT (rod)));
	g_assert_cmpuint (lrg_physics_constraint_get_id (LRG_PHYSICS_CONSTRAINT (rod)), ==, id);
	g_assert_cmpuint (lrg_dynamics_world_get_constraint_count (world), ==, 1);
	g_assert_true (lrg_dynamics_world_get_constraint (world, 0) == LRG_PHYSICS_CONSTRAINT (rod));
	/* Bodies are fixed while in a world. */
	g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*remove it first*");
	g_assert_false (lrg_physics_constraint_set_bodies (LRG_PHYSICS_CONSTRAINT (rod), bob, bob));
	g_test_assert_expected_messages ();

	worst = 0.0f;
	lowest = 5.0f;
	for (i = 0; i < 120u; i++)
	{
		gfloat p[3];
		gfloat d;

		step_n (world, 1);
		pos (world, bob, p);
		d = sqrtf (p[0] * p[0] + (p[1] - 5.0f) * (p[1] - 5.0f) + p[2] * p[2]);
		worst = MAX (worst, fabsf (d - 2.0f));
		lowest = MIN (lowest, p[1]);
	}
	g_test_message ("rod: worst length error %.5f m", worst);
	g_assert_cmpfloat (worst, <, 0.01f);
	/* It swung down to the bottom (y = 3) rather than falling away. */
	g_assert_cmpfloat (lowest, <, 3.05f);

	/* Every vfunc ran once per substep, iterations as configured. */
	solver = lrg_dynamics_world_get_contact_solver (world);
	g_assert_cmpuint (rod->setups, ==, 120);
	g_assert_cmpuint (rod->warms, ==, 120);
	g_assert_cmpuint (rod->posts, ==, 120);
	g_assert_cmpuint (rod->velocities, ==, 120u * lrg_contact_solver_get_velocity_iterations (solver));
	g_assert_cmpuint (rod->positions, ==, 120u * lrg_contact_solver_get_position_iterations (solver));
	g_assert_cmpuint (rod->resets, >=, 1);

	/* Disabled: skipped. Removed: out of the world. */
	lrg_physics_constraint_set_enabled (LRG_PHYSICS_CONSTRAINT (rod), FALSE);
	step_n (world, 1);
	g_assert_cmpuint (rod->setups, ==, 120);
	g_assert_true (lrg_dynamics_world_remove_constraint (world, LRG_PHYSICS_CONSTRAINT (rod)));
	g_assert_false (lrg_physics_constraint_get_in_world (LRG_PHYSICS_CONSTRAINT (rod)));
	g_assert_cmpuint (lrg_dynamics_world_get_constraint_count (world), ==, 0);
}

static void
test_ids_and_remove (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GArray) ids = NULL;
	guint a;
	guint b;
	guint c;
	guint i;
	gboolean active;

	world = make_world ();
	a = add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 0.0f, 5.0f, 0.0f);
	b = add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 3.0f, 5.0f, 0.0f);
	g_assert_cmpuint (a, ==, 0);
	g_assert_cmpuint (b, ==, 1);
	require (lrg_dynamics_world_remove_body (world, a, &error), &error);
	g_assert_false (lrg_dynamics_world_body_is_active (world, a, &active, &error));
	g_assert_error (error, LRG_DYNAMICS_ERROR, LRG_DYNAMICS_ERROR_MISSING);
	g_clear_error (&error);
	/* The slot is reused under a new id. */
	c = add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 6.0f, 5.0f, 0.0f);
	g_assert_cmpuint (c, !=, a);
	g_assert_cmpuint (c & 0xffffffu, ==, 0);
	g_assert_cmpuint (lrg_dynamics_world_get_body_count (world), ==, 2);
	ids = lrg_dynamics_world_get_body_ids (world);
	g_assert_cmpuint (ids->len, ==, 2);
	g_assert_cmpuint (g_array_index (ids, guint, 0), ==, c);
	g_assert_cmpuint (g_array_index (ids, guint, 1), ==, b);
	/* Hundreds of bodies: no 16-body cap. */
	for (i = 0; i < 300u; i++)
		add_ball (world, LRG_DYNAMICS_BODY_STATIC, 0.0f, 0.1f, (gfloat) i, 0.0f, 0.0f);
	g_assert_cmpuint (lrg_dynamics_world_get_body_count (world), ==, 302);
}

static void
test_interpolation (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(GError) error = NULL;
	gfloat v[3];
	gfloat p[3];
	gfloat q[4];
	guint ball;

	world = lrg_dynamics_world_new ();
	ball = add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f);
	v[0] = 6.0f;
	v[1] = 0.0f;
	v[2] = 0.0f;
	require (lrg_dynamics_world_set_linear_velocity (world, ball, v, &error), &error);
	/* 1.5 substeps: one runs, half a step is left over. */
	require (lrg_dynamics_world_step (world, 1.5f / 60.0f, NULL, &error), &error);
	g_assert_cmpfloat_with_epsilon (lrg_dynamics_world_get_interpolation_alpha (world), 0.5f, 1.0e-3f);
	require (lrg_dynamics_world_get_interpolated_pose (world, ball, p, q, &error), &error);
	/* Halfway between 0 and 0.1. */
	g_assert_cmpfloat_with_epsilon (p[0], 0.05f, 1.0e-3f);
	g_assert_cmpfloat_with_epsilon (q[3], 1.0f, 1.0e-5f);
}

static void
test_debug_draw (void)
{
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(LrgDebugDraw) draw = NULL;

	world = make_world ();
	add_ground (world);
	add_box (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.6f, 0.0f, NULL);
	add_ball (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.3f, 2.0f, 0.3f, 0.0f);
	step_n (world, 2);
	draw = lrg_debug_draw_new ();
	lrg_dynamics_world_debug_draw (world, draw, LRG_DYNAMICS_DEBUG_ALL);
	g_assert_cmpuint (lrg_debug_draw_get_count (draw), >, 5);
	g_assert_cmpuint (lrg_debug_draw_get_category (draw), ==, LRG_DEBUG_CATEGORY_GENERAL);
}

static void
test_island_builder (void)
{
	g_autoptr(LrgIslandBuilder) b = NULL;
	const guint *members;
	guint n;
	gboolean active[6] = { TRUE, FALSE, TRUE, TRUE, TRUE, TRUE };

	b = lrg_island_builder_new ();
	lrg_island_builder_reset (b, 6);
	lrg_island_builder_link (b, 4, 1);
	lrg_island_builder_link (b, 1, 3);
	lrg_island_builder_link (b, 5, 2);
	lrg_island_builder_link (b, 0, G_MAXUINT);
	g_assert_cmpuint (lrg_island_builder_finish (b, NULL, 0), ==, 3);
	g_assert_cmpuint (lrg_island_builder_find (b, 4), ==, 1);
	members = lrg_island_builder_get_island (b, lrg_island_builder_get_island_of (b, 4), &n);
	g_assert_cmpuint (n, ==, 3);
	g_assert_cmpuint (members[0], ==, 1);
	g_assert_cmpuint (members[1], ==, 3);
	g_assert_cmpuint (members[2], ==, 4);
	g_assert_cmpuint (lrg_island_builder_get_island_of (b, 0), ==, 0);
	g_assert_cmpuint (lrg_island_builder_get_island_of (b, 2), ==, 2);
	/* Element 1 inactive: 3 and 4 still share an island through it. */
	g_assert_cmpuint (lrg_island_builder_finish (b, active, 6), ==, 3);
	g_assert_cmpuint (lrg_island_builder_get_island_of (b, 1), ==, G_MAXUINT);
	g_assert_cmpuint (lrg_island_builder_get_island_of (b, 3), ==, lrg_island_builder_get_island_of (b, 4));
	g_assert_cmpuint (lrg_island_builder_get_island_count (b), ==, 3);
}

static void
test_perf_pile (void)
{
	gint64 t0;
	gint64 t1;
	guint steps;
	gdouble ms;

	if (!g_test_perf ())
	{
		g_test_skip ("run with -m perf (pinned: taskset -c 2)");
		return;
	}
	steps = 300;
	t0 = g_get_monotonic_time ();
	run_pile (1000, steps, NULL);
	t1 = g_get_monotonic_time ();
	ms = (gdouble) (t1 - t0) / 1000.0 / (gdouble) steps;
	g_test_message ("1000-box pile: %.3f ms per 1/60 s step (%u steps, setup included), %.1f bodies/ms",
	                ms, steps, 1000.0 / ms);
	g_test_minimized_result (ms, "ms per step for a 1000-box pile");
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/physics-solver/box-stack", test_box_stack);
	g_test_add_func ("/physics-solver/pyramid", test_pyramid);
	g_test_add_func ("/physics-solver/restitution", test_restitution);
	g_test_add_func ("/physics-solver/friction-incline", test_friction_incline);
	g_test_add_func ("/physics-solver/sleep-and-wake", test_sleep_and_wake);
	g_test_add_func ("/physics-solver/ccd-bullet", test_ccd_bullet);
	g_test_add_func ("/physics-solver/kinematic-push", test_kinematic_push);
	g_test_add_func ("/physics-solver/sensors", test_sensors);
	g_test_add_func ("/physics-solver/contact-signals", test_contact_signals);
	g_test_add_func ("/physics-solver/queries", test_queries);
	g_test_add_func ("/physics-solver/layers", test_layers);
	g_test_add_func ("/physics-solver/determinism", test_determinism);
	g_test_add_func ("/physics-solver/constraint-hook", test_constraint_hook);
	g_test_add_func ("/physics-solver/ids-and-remove", test_ids_and_remove);
	g_test_add_func ("/physics-solver/interpolation", test_interpolation);
	g_test_add_func ("/physics-solver/debug-draw", test_debug_draw);
	g_test_add_func ("/physics-solver/island-builder", test_island_builder);
	g_test_add_func ("/physics-solver/perf-1000-boxes", test_perf_pile);
	return g_test_run ();
}
