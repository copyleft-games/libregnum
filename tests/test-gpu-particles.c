/* test-gpu-particles.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGpuParticles: emission and kill bookkeeping, analytic motion, seeds,
 * bursts, baked tables and conversions run headless on the CPU backend;
 * the transform-feedback path (counts, GPU vs CPU agreement, 100k
 * particles, rendering, backend switches) runs only on the isolated X
 * display (build-aux/xrun.sh).
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#define FPP (LRG_GPU_PARTICLES_FLOATS_PER_PARTICLE)

/* a CPU-only emitter: the tests below must not depend on a window */
static LrgGpuParticles *
cpu_emitter (guint capacity)
{
	LrgGpuParticles *p = lrg_gpu_particles_new (capacity);

	lrg_gpu_particles_set_prefer_gpu (p, FALSE);
	return p;
}

static void
step_n (LrgGpuParticles *p,
        guint            n,
        gfloat           dt)
{
	guint i;

	for (i = 0; i < n; i++)
		lrg_gpu_particles_update (p, dt);
}

/* ==========================================================================
 * Headless (CPU backend)
 * ========================================================================== */

static void
test_defaults (void)
{
	g_autoptr(LrgGpuParticles) p = lrg_gpu_particles_new (1000);
	const gchar * const       *names = lrg_gpu_particles_list_parameters ();
	guint                      i;

	g_assert_cmpuint (lrg_gpu_particles_get_capacity (p), ==, 1000);
	g_assert_true (lrg_gpu_particles_get_prefer_gpu (p));
	g_assert_cmpint (lrg_gpu_particles_get_backend (p), ==, LRG_PARTICLE_BACKEND_CPU);
	g_assert_cmpuint (lrg_gpu_particles_count_alive (p), ==, 0);

	/* every listed description parameter is a readable, writable property */
	for (i = 0; names[i] != NULL; i++)
	{
		GParamSpec *ps = g_object_class_find_property (G_OBJECT_GET_CLASS (p), names[i]);

		g_assert_nonnull (ps);
		g_assert_true (ps->flags & G_PARAM_WRITABLE);
	}
	g_assert_cmpuint (i, >, 40);
}

static void
test_emission_rate (void)
{
	g_autoptr(LrgGpuParticles) p = cpu_emitter (10000);

	g_object_set (p, "emission-rate", 100.0f, "lifetime-min", 10.0f, "lifetime-max", 10.0f, NULL);
	step_n (p, 10, 0.1f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 100);
	g_assert_cmpuint (lrg_gpu_particles_count_alive (p), ==, 100);
	g_assert_cmpfloat_with_epsilon (lrg_gpu_particles_get_time (p), 1.0, 1e-6);

	/* fractional rates accumulate */
	g_object_set (p, "emission-rate", 2.5f, NULL);
	step_n (p, 4, 1.0f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 110);

	/* not emitting: nothing new, explicit bursts still go out */
	g_object_set (p, "emitting", FALSE, NULL);
	step_n (p, 4, 1.0f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 110);
	lrg_gpu_particles_emit (p, 7);
	lrg_gpu_particles_update (p, 0.01f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 117);

	/* time-scale stretches the clock */
	g_object_set (p, "emitting", TRUE, "emission-rate", 10.0f, "time-scale", 2.0f, NULL);
	lrg_gpu_particles_update (p, 1.0f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 137);
}

static void
test_ring_overflow (void)
{
	g_autoptr(LrgGpuParticles) p = cpu_emitter (50);

	g_object_set (p, "emission-rate", 100.0f, "lifetime-min", 10.0f, "lifetime-max", 10.0f, NULL);
	step_n (p, 10, 0.1f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 100);
	g_assert_cmpuint (lrg_gpu_particles_count_alive (p), ==, 50);

	/* a burst bigger than the capacity is capped at it */
	lrg_gpu_particles_emit (p, 500);
	lrg_gpu_particles_update (p, 0.0f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 150);
}

static void
test_lifetime_kill (void)
{
	g_autoptr(LrgGpuParticles) p = cpu_emitter (64);

	g_object_set (p, "emission-rate", 0.0f, "lifetime-min", 0.5f, "lifetime-max", 0.5f, NULL);
	lrg_gpu_particles_emit (p, 10);
	step_n (p, 4, 0.1f);      /* newborns are 0.05 old: 0.35 s */
	g_assert_cmpuint (lrg_gpu_particles_count_alive (p), ==, 10);
	step_n (p, 2, 0.1f);      /* 0.55 s */
	g_assert_cmpuint (lrg_gpu_particles_count_alive (p), ==, 0);

	/* clear rewinds everything */
	lrg_gpu_particles_emit (p, 3);
	lrg_gpu_particles_update (p, 0.1f);
	lrg_gpu_particles_clear (p);
	g_assert_cmpuint (lrg_gpu_particles_count_alive (p), ==, 0);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 0);
	g_assert_cmpfloat (lrg_gpu_particles_get_time (p), ==, 0.0);
}

/* a particle dropped from rest falls -g t^2 / 2 (semi-implicit Euler
 * with dt = 1 ms is within 0.5 % after a second) */
static void
test_gravity_analytic (void)
{
	g_autoptr(LrgGpuParticles) p = cpu_emitter (4);
	GrlVector3                 pos, vel;
	gfloat                     age, life;
	gfloat                     expected;

	g_object_set (p, "emission-rate", 0.0f, "speed-min", 0.0f, "speed-max", 0.0f,
	              "lifetime-min", 10.0f, "lifetime-max", 10.0f, NULL);
	lrg_gpu_particles_set_gravity (p, 0.0f, -10.0f, 0.0f);
	lrg_gpu_particles_set_position (p, 1.0f, 100.0f, -2.0f);
	lrg_gpu_particles_emit (p, 1);
	step_n (p, 1000, 0.001f);

	g_assert_true (lrg_gpu_particles_get_particle (p, 0, &pos, &vel, &age, &life));
	expected = 100.0f - 0.5f * 10.0f * age * age;
	g_assert_cmpfloat_with_epsilon (pos.x, 1.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (pos.z, -2.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (pos.y, expected, 0.03f);
	g_assert_cmpfloat_with_epsilon (vel.y, -10.0f * age, 0.02f);
	g_assert_cmpfloat_with_epsilon (life, 10.0f, 1e-6f);

	/* drag toward a wind: the velocity converges to the air velocity */
	lrg_gpu_particles_clear (p);
	lrg_gpu_particles_set_gravity (p, 0.0f, 0.0f, 0.0f);
	lrg_gpu_particles_set_wind (p, 3.0f, 0.0f, -1.0f);
	g_object_set (p, "drag", 4.0f, NULL);
	lrg_gpu_particles_emit (p, 1);
	step_n (p, 300, 0.01f);
	lrg_gpu_particles_get_particle (p, 0, NULL, &vel, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (vel.x, 3.0f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (vel.z, -1.0f, 1e-3f);
}

static void
test_collision_plane (void)
{
	g_autoptr(LrgGpuParticles) p = cpu_emitter (4);
	GrlVector3                 pos;
	guint                      i;
	gfloat                     lowest = 1e9f;

	g_object_set (p, "emission-rate", 0.0f, "speed-min", 0.0f, "speed-max", 0.0f,
	              "lifetime-min", 10.0f, "lifetime-max", 10.0f,
	              "collide", TRUE, "plane-height", 0.0f, "bounce", 0.5f, NULL);
	lrg_gpu_particles_set_gravity (p, 0.0f, -10.0f, 0.0f);
	lrg_gpu_particles_set_position (p, 0.0f, 2.0f, 0.0f);
	lrg_gpu_particles_emit (p, 1);
	for (i = 0; i < 400; i++)
	{
		lrg_gpu_particles_update (p, 0.005f);
		lrg_gpu_particles_get_particle (p, 0, &pos, NULL, NULL, NULL);
		lowest = MIN (lowest, pos.y);
	}
	g_assert_cmpfloat (lowest, >=, -1e-4f);
}

static void
test_determinism (void)
{
	g_autoptr(LrgGpuParticles) a = cpu_emitter (2000);
	g_autoptr(LrgGpuParticles) b = cpu_emitter (2000);
	g_autoptr(LrgGpuParticles) c = cpu_emitter (2000);
	g_autofree gfloat         *da = NULL;
	g_autofree gfloat         *db = NULL;
	g_autofree gfloat         *dc = NULL;
	guint                      na, nb, nc;

	g_object_set (a, "seed", 42u, "shape", LRG_GPU_PARTICLES_SHAPE_SPHERE, "emission-rate", 500.0f,
	              "turbulence-strength", 2.0f, "spin-max", 90.0f, NULL);
	g_object_set (b, "seed", 42u, "shape", LRG_GPU_PARTICLES_SHAPE_SPHERE, "emission-rate", 500.0f,
	              "turbulence-strength", 2.0f, "spin-max", 90.0f, NULL);
	g_object_set (c, "seed", 43u, "shape", LRG_GPU_PARTICLES_SHAPE_SPHERE, "emission-rate", 500.0f,
	              "turbulence-strength", 2.0f, "spin-max", 90.0f, NULL);
	step_n (a, 30, 1.0f / 60.0f);
	step_n (b, 30, 1.0f / 60.0f);
	step_n (c, 30, 1.0f / 60.0f);
	da = lrg_gpu_particles_read_back (a, &na);
	db = lrg_gpu_particles_read_back (b, &nb);
	dc = lrg_gpu_particles_read_back (c, &nc);
	g_assert_cmpuint (na, ==, 2000 * FPP);
	g_assert_cmpuint (na, ==, nb);
	g_assert_cmpmem (da, na * sizeof (gfloat), db, nb * sizeof (gfloat));
	g_assert_true (memcmp (da, dc, na * sizeof (gfloat)) != 0);
}

static void
test_shapes (void)
{
	const LrgGpuParticlesShape shapes[] = {
		LRG_GPU_PARTICLES_SHAPE_POINT, LRG_GPU_PARTICLES_SHAPE_SPHERE,
		LRG_GPU_PARTICLES_SHAPE_BOX, LRG_GPU_PARTICLES_SHAPE_CONE
	};
	guint s;

	for (s = 0; s < G_N_ELEMENTS (shapes); s++)
	{
		g_autoptr(LrgGpuParticles) p = cpu_emitter (500);
		guint                      i;

		g_object_set (p, "shape", shapes[s], "emission-rate", 0.0f, "shape-radius", 2.0f,
		              "box-x", 4.0f, "box-y", 1.0f, "box-z", 6.0f, "cone-angle", 30.0f,
		              "speed-min", 1.0f, "speed-max", 1.0f, NULL);
		lrg_gpu_particles_emit (p, 500);
		lrg_gpu_particles_update (p, 0.0f);   /* spawn without moving */
		for (i = 0; i < 500; i++)
		{
			GrlVector3 pos, vel;

			lrg_gpu_particles_get_particle (p, i, &pos, &vel, NULL, NULL);
			g_assert_cmpfloat_with_epsilon (sqrtf (vel.x * vel.x + vel.y * vel.y + vel.z * vel.z), 1.0f, 1e-4f);
			switch (shapes[s])
			{
			case LRG_GPU_PARTICLES_SHAPE_POINT:
				g_assert_cmpfloat (fabsf (pos.x) + fabsf (pos.y) + fabsf (pos.z), <, 1e-6f);
				/* inside the 30 degree cone around +Y */
				g_assert_cmpfloat (vel.y, >=, cosf (30.0f * G_PI / 180.0f) - 1e-4f);
				break;
			case LRG_GPU_PARTICLES_SHAPE_SPHERE:
				g_assert_cmpfloat (sqrtf (pos.x * pos.x + pos.y * pos.y + pos.z * pos.z), <=, 2.0f + 1e-4f);
				break;
			case LRG_GPU_PARTICLES_SHAPE_BOX:
				g_assert_cmpfloat (fabsf (pos.x), <=, 2.0f + 1e-5f);
				g_assert_cmpfloat (fabsf (pos.y), <=, 0.5f + 1e-5f);
				g_assert_cmpfloat (fabsf (pos.z), <=, 3.0f + 1e-5f);
				break;
			case LRG_GPU_PARTICLES_SHAPE_CONE:
			default:
				g_assert_cmpfloat (fabsf (pos.y), <, 1e-5f);   /* base disc on the XZ plane */
				g_assert_cmpfloat (sqrtf (pos.x * pos.x + pos.z * pos.z), <=, 2.0f + 1e-4f);
				break;
			}
		}
	}
}

static void
test_bursts (void)
{
	g_autoptr(LrgGpuParticles) p = cpu_emitter (1000);
	gfloat                     t = 0.0f;
	guint                      count = 0;

	g_object_set (p, "emission-rate", 0.0f, "duration", 1.0f, "loop", TRUE,
	              "lifetime-min", 100.0f, "lifetime-max", 100.0f, NULL);
	lrg_gpu_particles_add_burst (p, 0.5f, 20);
	g_assert_cmpuint (lrg_gpu_particles_get_burst_count (p), ==, 1);
	g_assert_true (lrg_gpu_particles_get_burst (p, 0, &t, &count));
	g_assert_cmpfloat (t, ==, 0.5f);
	g_assert_cmpuint (count, ==, 20);
	g_assert_false (lrg_gpu_particles_get_burst (p, 1, NULL, NULL));

	step_n (p, 4, 0.1f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 0);
	step_n (p, 2, 0.1f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 20);
	step_n (p, 10, 0.1f);    /* 1.6 s: the second cycle fired at 1.5 */
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 40);
	lrg_gpu_particles_update (p, 3.0f);   /* one long step crosses three cycles */
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 100);

	/* without loop the cycle runs once */
	lrg_gpu_particles_clear (p);
	g_object_set (p, "loop", FALSE, "emission-rate", 10.0f, NULL);
	step_n (p, 30, 0.1f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 30);
	lrg_gpu_particles_clear_bursts (p);
	g_assert_cmpuint (lrg_gpu_particles_get_burst_count (p), ==, 0);
}

static void
test_tables (void)
{
	g_autoptr(LrgGpuParticles)  p = cpu_emitter (8);
	g_autoptr(LrgKeyframeCurve) curve = lrg_keyframe_curve_new ();
	g_autoptr(LrgGradientStops) stops = NULL;
	GrlColor                    red = { 255, 0, 0, 255 };
	GrlColor                    blue = { 0, 0, 255, 0 };
	GrlColor                    c;

	lrg_gpu_particles_set_colors (p, &red, &blue);
	lrg_gpu_particles_sample_color (p, 0.0f, &c);
	g_assert_cmpuint (c.r, ==, 255);
	g_assert_cmpuint (c.b, ==, 0);
	lrg_gpu_particles_sample_color (p, 1.0f, &c);
	g_assert_cmpuint (c.r, ==, 0);
	g_assert_cmpuint (c.b, ==, 255);
	g_assert_cmpuint (c.a, ==, 0);
	lrg_gpu_particles_sample_color (p, 0.5f, &c);
	g_assert_cmpint (ABS ((gint) c.r - 128), <=, 2);
	g_assert_cmpint (ABS ((gint) c.b - 128), <=, 2);

	stops = lrg_gradient_stops_new_from_string ("0 #00ff00ff; 1 #00ff0000");
	g_assert_nonnull (stops);
	lrg_gpu_particles_set_color_gradient (p, stops);
	lrg_gpu_particles_sample_color (p, 0.0f, &c);
	g_assert_cmpuint (c.g, ==, 255);
	g_assert_cmpuint (c.r, ==, 0);
	g_assert_nonnull (lrg_gpu_particles_get_color_gradient (p));

	g_object_set (p, "size-start", 2.0f, "size-end", 0.0f, NULL);
	g_assert_cmpfloat_with_epsilon (lrg_gpu_particles_sample_size (p, 0.5f), 1.0f, 1e-5f);
	lrg_keyframe_curve_add_key (curve, 0.0f, 0.0f, LRG_EASING_LINEAR);
	lrg_keyframe_curve_add_key (curve, 1.0f, 1.0f, LRG_EASING_LINEAR);
	lrg_gpu_particles_set_size_curve (p, curve);
	g_assert_true (lrg_gpu_particles_get_size_curve (p) == curve);
	g_assert_cmpfloat_with_epsilon (lrg_gpu_particles_sample_size (p, 1.0f), 2.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (lrg_gpu_particles_sample_size (p, 0.0f), 0.0f, 1e-4f);
}

static void
test_copy_parameters (void)
{
	g_autoptr(LrgGpuParticles) a = cpu_emitter (100);
	g_autoptr(LrgGpuParticles) b = cpu_emitter (300);
	gfloat                     rate = 0.0f, gy = 0.0f;
	LrgGpuParticlesShape       shape;

	g_object_set (a, "emission-rate", 77.0f, "shape", LRG_GPU_PARTICLES_SHAPE_BOX, "gravity-y", -3.0f, NULL);
	lrg_gpu_particles_add_burst (a, 0.2f, 5);
	lrg_gpu_particles_copy_parameters (b, a);
	g_object_get (b, "emission-rate", &rate, "shape", &shape, "gravity-y", &gy, NULL);
	g_assert_cmpfloat (rate, ==, 77.0f);
	g_assert_cmpint (shape, ==, LRG_GPU_PARTICLES_SHAPE_BOX);
	g_assert_cmpfloat (gy, ==, -3.0f);
	g_assert_cmpuint (lrg_gpu_particles_get_burst_count (b), ==, 1);
	g_assert_cmpuint (lrg_gpu_particles_get_capacity (b), ==, 300);
}

static void
test_from_system (void)
{
	g_autoptr(LrgParticleSystem)  sys = lrg_particle_system_new (500);
	g_autoptr(LrgParticleEmitter) em = lrg_particle_emitter_new ();
	g_autoptr(LrgParticleForce)   grav = lrg_particle_force_gravity_new (0.0f, -9.0f, 0.0f);
	g_autoptr(LrgGpuParticles)    p = NULL;
	g_autoptr(GError)             error = NULL;
	gfloat                        gy = 0.0f, rate = 0.0f, angle = 0.0f;

	lrg_particle_emitter_set_emission_rate (em, 42.0f);
	lrg_particle_emitter_set_emission_shape (em, LRG_EMISSION_SHAPE_CONE);
	lrg_particle_emitter_set_spread_angle (em, (gfloat) G_PI / 6.0f);
	lrg_particle_system_add_emitter (sys, em);
	lrg_particle_system_add_force (sys, grav);

	p = lrg_gpu_particles_new_from_system (sys, 0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (p);
	g_assert_cmpuint (lrg_gpu_particles_get_capacity (p), ==, 500);
	g_object_get (p, "gravity-y", &gy, "emission-rate", &rate, "cone-angle", &angle, NULL);
	g_assert_cmpfloat_with_epsilon (gy, -9.0f, 1e-5f);
	g_assert_cmpfloat (rate, ==, 42.0f);
	g_assert_cmpfloat_with_epsilon (angle, 30.0f, 1e-3f);

	/* mesh shapes stay on the CPU */
	g_clear_object (&p);
	lrg_particle_emitter_set_emission_shape (em, LRG_EMISSION_SHAPE_MESH);
	p = lrg_gpu_particles_new_from_system (sys, 0, &error);
	g_assert_null (p);
	g_assert_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED);
}

/* the CPU backend's cost per update for 100k live particles (reported,
 * not asserted: docs/modules/particles/gpu-particles.org#performance) */
static void
test_cpu_perf (void)
{
	g_autoptr(LrgGpuParticles) p = cpu_emitter (100000);
	gint64                     t0, t1;

	g_object_set (p, "emission-rate", 60000.0f, "lifetime-min", 1.5f, "lifetime-max", 2.0f,
	              "turbulence-strength", 1.0f, NULL);
	lrg_gpu_particles_set_gravity (p, 0.0f, -9.81f, 0.0f);
	step_n (p, 120, 1.0f / 60.0f);
	t0 = g_get_monotonic_time ();
	step_n (p, 60, 1.0f / 60.0f);
	t1 = g_get_monotonic_time ();
	g_test_message ("CPU backend, 100k: %.3f ms per update", (gdouble) (t1 - t0) / 1000.0 / 60.0);
	g_assert_cmpuint (lrg_gpu_particles_count_alive (p), >=, 90000);
}

/* builds a one-emitter asset with a GPU-capable stack */
static LrgParticleAsset *
make_asset (gboolean with_sub_emitter)
{
	LrgParticleAsset                  *asset = lrg_particle_asset_new ();
	g_autoptr(LrgParticleAssetEmitter) em = lrg_particle_asset_emitter_new ("sparks");
	LrgParticleModule                 *m;
	GrlVector2                         speed = { 2.0f, 4.0f };
	GrlVector3                         gravity = { 0.0f, -9.81f, 0.0f };

	lrg_particle_asset_set_seed (asset, 1234);
	lrg_particle_asset_emitter_set_max_particles (em, 4096);
	m = lrg_particle_asset_emitter_add_module (em, "spawn-rate");
	g_object_set (m, "rate", 250.0f, NULL);
	m = lrg_particle_asset_emitter_add_module (em, "velocity");
	g_object_set (m, "speed", &speed, NULL);
	m = lrg_particle_asset_emitter_add_module (em, "gravity");
	g_object_set (m, "gravity", &gravity, NULL);
	if (with_sub_emitter)
		lrg_particle_asset_emitter_add_module (em, "sub-emitter");
	lrg_particle_asset_add_emitter (asset, em);
	return asset;
}

static void
test_from_asset (void)
{
	g_autoptr(LrgParticleAsset) ok = make_asset (FALSE);
	g_autoptr(LrgParticleAsset) bad = make_asset (TRUE);
	g_autoptr(LrgGpuParticles)  p = NULL;
	g_autoptr(GError)           error = NULL;
	gfloat                      rate = 0.0f, gy = 0.0f, smin = 0.0f, smax = 0.0f;
	guint                       seed = 0;

	g_assert_true (lrg_gpu_particles_check_emitter (ok, lrg_particle_asset_get_emitter (ok, 0), &error));
	g_assert_no_error (error);
	p = lrg_gpu_particles_new_from_asset (ok, 0, 0, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_gpu_particles_get_capacity (p), ==, 4096);
	g_object_get (p, "emission-rate", &rate, "gravity-y", &gy, "speed-min", &smin, "speed-max", &smax,
	              "seed", &seed, NULL);
	g_assert_cmpfloat (rate, ==, 250.0f);
	g_assert_cmpfloat_with_epsilon (gy, -9.81f, 1e-5f);
	g_assert_cmpfloat (smin, ==, 2.0f);
	g_assert_cmpfloat (smax, ==, 4.0f);
	g_assert_cmpuint (seed, ==, 1234);

	/* a sub-emitter keeps the effect on the CPU, with the module named */
	g_assert_false (lrg_gpu_particles_check_emitter (bad, lrg_particle_asset_get_emitter (bad, 0), &error));
	g_assert_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED);
	g_assert_nonnull (strstr (error->message, "sub-emitter"));
	g_clear_error (&error);
	g_assert_null (lrg_gpu_particles_new_from_asset (bad, 0, 0, &error));
	g_assert_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED);
}

/* ==========================================================================
 * GPU (isolated X display only)
 * ========================================================================== */

static gboolean gpu_ready;

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	/* never on a live Wayland session: xrun.sh unsets WAYLAND_DISPLAY */
	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (256, 256, "test-gpu-particles");
	return IsWindowReady ();
}

#define NEED_GPU() \
	do { \
		if (!gpu_ready || !lrg_gpu_particles_is_gpu_available ()) \
		{ \
			g_test_skip ("needs the isolated X display (xrun.sh) and GL 3.3 transform feedback"); \
			return; \
		} \
	} while (0)

/* a busy emitter exercising every simulation feature */
static LrgGpuParticles *
busy_emitter (guint    capacity,
              gboolean gpu)
{
	LrgGpuParticles *p = lrg_gpu_particles_new (capacity);

	lrg_gpu_particles_set_prefer_gpu (p, gpu);
	g_object_set (p, "seed", 7u, "shape", LRG_GPU_PARTICLES_SHAPE_SPHERE, "shape-radius", 1.5f,
	              "emission-rate", 3000.0f, "speed-min", 1.0f, "speed-max", 3.0f,
	              "lifetime-min", 0.5f, "lifetime-max", 1.5f, "spin-max", 180.0f,
	              "drag", 0.5f, "turbulence-strength", 1.5f,
	              "attractor-y", 3.0f, "attractor-strength", 2.0f, "attractor-radius", 6.0f,
	              "vortex-strength", 1.0f, "collide", TRUE, "plane-height", -1.0f, NULL);
	lrg_gpu_particles_set_gravity (p, 0.0f, -4.0f, 0.0f);
	lrg_gpu_particles_set_wind (p, 1.0f, 0.0f, 0.5f);
	return p;
}

static void
test_gpu_counts (void)
{
	g_autoptr(LrgGpuParticles) p = NULL;
	g_autoptr(GError)          error = NULL;

	NEED_GPU ();
	p = lrg_gpu_particles_new (10000);
	g_assert_true (lrg_gpu_particles_prepare (p, &error));
	g_assert_no_error (error);
	g_assert_cmpint (lrg_gpu_particles_get_backend (p), ==, LRG_PARTICLE_BACKEND_GPU);

	g_object_set (p, "emission-rate", 600.0f, "lifetime-min", 0.5f, "lifetime-max", 0.5f, NULL);
	step_n (p, 30, 1.0f / 60.0f);     /* 300 emitted, those older than 0.5 s died */
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 300);
	g_assert_cmpuint (lrg_gpu_particles_count_alive (p), ==, 300);
	step_n (p, 30, 1.0f / 60.0f);
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (p), ==, 600);
	/* steady state: rate x lifetime, +-1 for the newborns' sub-frame ages */
	g_assert_cmpint (ABS ((gint) lrg_gpu_particles_count_alive (p) - 300), <=, 6);
}

/* the GPU and CPU twins draw the same random numbers and agree to float
 * rounding on every slot */
static void
test_gpu_matches_cpu (void)
{
	g_autoptr(LrgGpuParticles) gpu = NULL;
	g_autoptr(LrgGpuParticles) cpu = NULL;
	g_autofree gfloat         *dg = NULL;
	g_autofree gfloat         *dc = NULL;
	guint                      ng, nc, i, alive = 0;
	gdouble                    worst = 0.0;

	NEED_GPU ();
	gpu = busy_emitter (8192, TRUE);
	cpu = busy_emitter (8192, FALSE);
	lrg_gpu_particles_prepare (gpu, NULL);
	g_assert_cmpint (lrg_gpu_particles_get_backend (gpu), ==, LRG_PARTICLE_BACKEND_GPU);
	for (i = 0; i < 60; i++)
	{
		gfloat x = sinf ((gfloat) i * 0.1f) * 3.0f;

		lrg_gpu_particles_set_position (gpu, x, 0.0f, 0.0f);   /* a moving emitter */
		lrg_gpu_particles_set_position (cpu, x, 0.0f, 0.0f);
		lrg_gpu_particles_update (gpu, 1.0f / 60.0f);
		lrg_gpu_particles_update (cpu, 1.0f / 60.0f);
	}
	dg = lrg_gpu_particles_read_back (gpu, &ng);
	dc = lrg_gpu_particles_read_back (cpu, &nc);
	g_assert_cmpuint (ng, ==, nc);
	for (i = 0; i < ng / FPP; i++)
	{
		const gfloat *a = dg + i * FPP;
		const gfloat *b = dc + i * FPP;
		guint         k;

		/* identical spawn indices and lifetimes, age to rounding */
		g_assert_cmpfloat (a[10], ==, b[10]);
		g_assert_cmpfloat (a[11], ==, b[11]);
		g_assert_cmpfloat_with_epsilon (a[7], b[7], 1e-5f);
		g_assert_cmpfloat_with_epsilon (a[3], b[3], 1e-4f);
		if (b[3] < b[7])
			alive++;
		for (k = 0; k < 7; k++)
			if (k != 3)
				worst = MAX (worst, fabs ((gdouble) a[k] - (gdouble) b[k]));
	}
	g_test_message ("alive %u, worst position/velocity difference %.3g", alive, worst);
	g_assert_cmpuint (alive, >, 1000);
	g_assert_cmpfloat (worst, <, 5e-3);
}

static void
test_gpu_backend_switch (void)
{
	g_autoptr(LrgGpuParticles) p = NULL;
	g_autoptr(LrgGpuParticles) ref = NULL;
	g_autofree gfloat         *dp = NULL;
	g_autofree gfloat         *dr = NULL;
	guint                      np, nr, i;

	NEED_GPU ();
	p = busy_emitter (4096, TRUE);
	ref = busy_emitter (4096, FALSE);
	step_n (p, 20, 1.0f / 60.0f);
	step_n (ref, 20, 1.0f / 60.0f);
	g_assert_cmpint (lrg_gpu_particles_get_backend (p), ==, LRG_PARTICLE_BACKEND_GPU);

	/* GPU -> CPU carries the live particles over */
	lrg_gpu_particles_set_prefer_gpu (p, FALSE);
	g_assert_cmpint (lrg_gpu_particles_get_backend (p), ==, LRG_PARTICLE_BACKEND_CPU);
	step_n (p, 10, 1.0f / 60.0f);
	step_n (ref, 10, 1.0f / 60.0f);
	/* and back */
	lrg_gpu_particles_set_prefer_gpu (p, TRUE);
	step_n (p, 10, 1.0f / 60.0f);
	step_n (ref, 10, 1.0f / 60.0f);
	g_assert_cmpint (lrg_gpu_particles_get_backend (p), ==, LRG_PARTICLE_BACKEND_GPU);

	dp = lrg_gpu_particles_read_back (p, &np);
	dr = lrg_gpu_particles_read_back (ref, &nr);
	for (i = 0; i < np / FPP; i++)
	{
		g_assert_cmpfloat (dp[i * FPP + 10], ==, dr[i * FPP + 10]);
		g_assert_cmpfloat_with_epsilon (dp[i * FPP + 1], dr[i * FPP + 1], 5e-3f);
	}
}

/* 100k particles: simulate, then draw into a render texture and find lit
 * pixels where the particles are */
static void
test_gpu_100k_render (void)
{
	g_autoptr(LrgGpuParticles)  p = NULL;
	g_autoptr(GrlRenderTexture) target = NULL;
	g_autoptr(GrlImage)         img = NULL;
	g_autoptr(GrlTexture)       tex = NULL;
	Camera3D                    cam = { 0 };
	const guint8               *px;
	gsize                       size = 0, i;
	guint                       lit = 0;
	gint64                      t0, t1;
	GrlColor                    warm = { 255, 160, 60, 255 };
	GrlColor                    cold = { 255, 40, 10, 0 };

	NEED_GPU ();
	p = lrg_gpu_particles_new (100000);
	g_object_set (p, "emission-rate", 60000.0f, "lifetime-min", 1.5f, "lifetime-max", 2.0f,
	              "shape", LRG_GPU_PARTICLES_SHAPE_SPHERE, "shape-radius", 0.5f,
	              "speed-min", 1.0f, "speed-max", 4.0f, "size-start", 0.08f, "size-end", 0.02f, NULL);
	lrg_gpu_particles_set_colors (p, &warm, &cold);
	t0 = g_get_monotonic_time ();
	step_n (p, 120, 1.0f / 60.0f);
	lrg_gpu_particles_count_alive (p);   /* waits for the GPU */
	t1 = g_get_monotonic_time ();
	g_test_message ("100k: %.3f ms per update", (gdouble) (t1 - t0) / 1000.0 / 120.0);
	g_assert_cmpuint (lrg_gpu_particles_count_alive (p), >=, 90000);

	target = grl_render_texture_new (256, 256);
	cam.position = (Vector3) { 0.0f, 0.0f, 10.0f };
	cam.target = (Vector3) { 0.0f, 0.0f, 0.0f };
	cam.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	cam.fovy = 45.0f;
	cam.projection = CAMERA_PERSPECTIVE;
	grl_render_texture_begin (target);
	ClearBackground ((Color) { 0, 0, 0, 255 });
	BeginMode3D (cam);
	lrg_gpu_particles_draw (p);
	EndMode3D ();
	grl_render_texture_end (target);
	g_assert_cmpuint (lrg_gpu_particles_get_draw_count (p), ==, 1);

	tex = grl_render_texture_get_texture (target);
	img = grl_texture_to_image (tex);
	px = grl_image_get_pixels (img, &size);
	for (i = 0; i + 3 < size; i += 4)
		if (px[i] > 40)
			lit++;
	g_test_message ("lit pixels: %u", lit);
	g_assert_cmpuint (lit, >, 2000);

	/* a hidden render layer draws nothing */
	{
		guint32 mask = lrg_render_layers_get_active_mask ();

		g_object_set (p, "layer", 5u, NULL);
		lrg_render_layers_set_active_mask (mask & ~(1u << 5));
		lrg_gpu_particles_draw (p);
		lrg_render_layers_set_active_mask (mask);
		g_assert_cmpuint (lrg_gpu_particles_get_draw_count (p), ==, 1);
	}
}

/* the CPU backend draws through the same instanced path */
static void
test_gpu_cpu_backend_draws (void)
{
	g_autoptr(LrgGpuParticles) p = NULL;
	Camera3D                   cam = { 0 };

	NEED_GPU ();
	p = cpu_emitter (1000);
	g_object_set (p, "emission-rate", 1000.0f, NULL);
	step_n (p, 10, 0.016f);
	cam.position = (Vector3) { 0.0f, 0.0f, 10.0f };
	cam.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	cam.fovy = 45.0f;
	BeginDrawing ();
	BeginMode3D (cam);
	lrg_gpu_particles_draw (p);
	EndMode3D ();
	EndDrawing ();
	g_assert_cmpint (lrg_gpu_particles_get_backend (p), ==, LRG_PARTICLE_BACKEND_CPU);
	g_assert_cmpuint (lrg_gpu_particles_get_draw_count (p), ==, 1);
}

int
main (int argc, char *argv[])
{
	gint rc;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/gpu-particles/defaults", test_defaults);
	g_test_add_func ("/gpu-particles/emission-rate", test_emission_rate);
	g_test_add_func ("/gpu-particles/ring-overflow", test_ring_overflow);
	g_test_add_func ("/gpu-particles/lifetime-kill", test_lifetime_kill);
	g_test_add_func ("/gpu-particles/gravity-analytic", test_gravity_analytic);
	g_test_add_func ("/gpu-particles/collision-plane", test_collision_plane);
	g_test_add_func ("/gpu-particles/determinism", test_determinism);
	g_test_add_func ("/gpu-particles/shapes", test_shapes);
	g_test_add_func ("/gpu-particles/bursts", test_bursts);
	g_test_add_func ("/gpu-particles/tables", test_tables);
	g_test_add_func ("/gpu-particles/copy-parameters", test_copy_parameters);
	g_test_add_func ("/gpu-particles/from-system", test_from_system);
	g_test_add_func ("/gpu-particles/from-asset", test_from_asset);
	g_test_add_func ("/gpu-particles/cpu-perf", test_cpu_perf);
	g_test_add_func ("/gpu-particles/gpu/counts", test_gpu_counts);
	g_test_add_func ("/gpu-particles/gpu/matches-cpu", test_gpu_matches_cpu);
	g_test_add_func ("/gpu-particles/gpu/backend-switch", test_gpu_backend_switch);
	g_test_add_func ("/gpu-particles/gpu/100k-render", test_gpu_100k_render);
	g_test_add_func ("/gpu-particles/gpu/cpu-backend-draws", test_gpu_cpu_backend_draws);

	gpu_ready = gpu_init ();
	rc = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return rc;
}
