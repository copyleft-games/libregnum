/* test-cloth.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless checks of LrgCloth: construction and constraint counts,
 * stretch bounds of a pinned hanging cloth, energy that settles instead
 * of exploding, sphere / capsule / plane / box collision, soft and hard
 * pins, bone attachment following a moving bone, wind pushing a flag,
 * bit-identical determinism and a performance measurement.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define DT (1.0f / 60.0f)

/* A cloth lying flat in the XZ plane, centred on (cx, y, cz). */
static LrgCloth *
make_flat (guint  n,
           gfloat size,
           gfloat cx,
           gfloat y,
           gfloat cz)
{
	LrgCloth *cloth = lrg_cloth_new ();
	gfloat    origin[3] = { cx - 0.5f * size, y, cz - 0.5f * size };
	gfloat    u[3] = { size / (gfloat) (n - 1), 0.0f, 0.0f };
	gfloat    v[3] = { 0.0f, 0.0f, size / (gfloat) (n - 1) };

	g_assert_true (lrg_cloth_set_grid (cloth, n, n, origin, u, v, NULL));
	return cloth;
}

static gboolean
all_finite (LrgCloth *cloth)
{
	guint         n;
	const gfloat *p = lrg_cloth_get_positions (cloth, &n);
	guint         i;

	for (i = 0; i < n; i++)
		if (!isfinite (p[i]))
			return FALSE;
	return TRUE;
}

static void
test_cloth_grid (void)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new_grid (32, 32, 1.0f, 1.0f);
	guint               cols = 0;
	guint               rows = 0;
	gfloat              p[3];
	guint               n_idx;

	g_assert_nonnull (cloth);
	g_assert_cmpuint (lrg_cloth_get_vertex_count (cloth), ==, 1024);
	g_assert_cmpuint (lrg_cloth_get_triangle_count (cloth), ==, 2 * 31 * 31);
	g_assert_true (lrg_cloth_get_grid_size (cloth, &cols, &rows));
	g_assert_cmpuint (cols, ==, 32);
	g_assert_cmpuint (rows, ==, 32);
	g_assert_cmpuint (lrg_cloth_get_grid_index (cloth, 3, 2), ==, 2 * 32 + 3);
	g_assert_cmpuint (lrg_cloth_get_grid_index (cloth, 32, 0), ==, G_MAXUINT);
	g_assert_nonnull (lrg_cloth_get_indices (cloth, &n_idx));
	g_assert_cmpuint (n_idx, ==, 6 * 31 * 31);

	/* Vertex (31, 31) hangs at the bottom right. */
	g_assert_true (lrg_cloth_get_position (cloth, 1023, p));
	g_assert_cmpfloat_with_epsilon (p[0], 1.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (p[1], -1.0f, 1e-5f);

	/* Mesh edges: 2 * 31 * 32 + 31 * 31 diagonals; one distance bend per interior non-diagonal edge. */
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_EDGE), ==,
	                  2 * 31 * 32 + 31 * 31 + (2 * 31 * 32 - 4 * 31));
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_BEND), ==, 0);
	g_object_set (cloth, "bend-mode", LRG_CLOTH_BEND_NONE, NULL);
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_EDGE), ==,
	                  2 * 31 * 32 + 31 * 31);
	/* Jolt's shear edges: the other diagonal of every quad. */
	g_object_set (cloth, "shear-edges", TRUE, NULL);
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_EDGE), ==,
	                  2 * 31 * 32 + 2 * 31 * 31);
	/* Dihedral: one bend per interior edge. */
	g_object_set (cloth, "shear-edges", FALSE, "bend-mode", LRG_CLOTH_BEND_DIHEDRAL, NULL);
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_BEND), ==,
	                  2 * 31 * 32 - 4 * 31 + 31 * 31);
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_LRA), ==, 0);
	g_assert_cmpfloat_with_epsilon (lrg_cloth_get_total_mass (cloth), 0.25f, 1e-4f);

	/* Bad input. */
	g_assert_null (lrg_cloth_new_grid (1, 4, 1.0f, 1.0f));
	g_assert_null (lrg_cloth_new_grid (4, 4, 0.0f, 1.0f));
}

static void
test_cloth_mesh_errors (void)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new ();
	g_autoptr(GError)   error = NULL;
	const gfloat        pos[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
	const guint32       good[6] = { 0, 1, 2, 0, 2, 3 };
	const guint32       bad[3] = { 0, 1, 7 };
	const guint32       degenerate[3] = { 0, 1, 1 };

	g_assert_false (lrg_cloth_set_mesh (cloth, 4, pos, NULL, 3, bad, &error));
	g_assert_error (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_INVALID);
	g_clear_error (&error);
	g_assert_false (lrg_cloth_set_mesh (cloth, 4, pos, NULL, 3, degenerate, &error));
	g_assert_error (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_INVALID);
	g_clear_error (&error);
	g_assert_cmpuint (lrg_cloth_get_vertex_count (cloth), ==, 0);

	g_assert_true (lrg_cloth_set_mesh (cloth, 4, pos, NULL, 6, good, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_cloth_get_triangle_count (cloth), ==, 2);
	/* Five mesh edges; the shared one is a quad diagonal (no distance bend across it). */
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_EDGE), ==, 5);
	g_object_set (cloth, "bend-mode", LRG_CLOTH_BEND_DIHEDRAL, NULL);
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_BEND), ==, 1);
	g_assert_false (lrg_cloth_pin (cloth, 4));
	g_assert_true (lrg_cloth_pin (cloth, 3));
	g_assert_true (lrg_cloth_is_pinned (cloth, 3));
	g_assert_true (lrg_cloth_unpin (cloth, 3));
	g_assert_false (lrg_cloth_unpin (cloth, 3));
}

/* A 32x32 cloth pinned along its top edge hangs within its stretch bound. */
static void
test_cloth_stretch_bounds (void)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new_grid (32, 32, 1.0f, 1.0f);
	gfloat              mn[3];
	gfloat              mx[3];
	guint               c;
	guint               s;
	gfloat              worst = 1.0f;

	for (c = 0; c < 32; c++)
		g_assert_true (lrg_cloth_pin (cloth, lrg_cloth_get_grid_index (cloth, c, 0)));
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_LRA), ==, 1024 - 32);
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_ATTACHMENT), ==, 32);

	/* Start it horizontal so it swings down hard. */
	{
		gfloat origin[3] = { 0.0f, 0.0f, 0.0f };
		gfloat u[3] = { 1.0f / 31.0f, 0.0f, 0.0f };
		gfloat v[3] = { 0.0f, 0.0f, 1.0f / 31.0f };

		g_assert_true (lrg_cloth_set_grid (cloth, 32, 32, origin, u, v, NULL));
		for (c = 0; c < 32; c++)
			lrg_cloth_pin (cloth, lrg_cloth_get_grid_index (cloth, c, 0));
	}
	for (s = 0; s < 240; s++)
	{
		lrg_cloth_step (cloth, DT);
		worst = MAX (worst, lrg_cloth_get_max_stretch (cloth));
	}
	g_test_message ("worst stretch over 4 s: %.4f", (gdouble) worst);
	g_assert_cmpfloat (worst, <, 1.10f);
	g_assert_cmpfloat (lrg_cloth_get_max_stretch (cloth), <, 1.05f);
	g_assert_true (all_finite (cloth));

	/* Pins did not move; the cloth hangs down to about its length. */
	{
		gfloat p[3];

		lrg_cloth_get_position (cloth, lrg_cloth_get_grid_index (cloth, 5, 0), p);
		g_assert_cmpfloat_with_epsilon (p[1], 0.0f, 1e-6f);
		g_assert_cmpfloat_with_epsilon (p[2], 0.0f, 1e-6f);
	}
	lrg_cloth_get_bounds (cloth, mn, mx);
	g_assert_cmpfloat (mn[1], >, -1.0f - 0.03f);
	g_assert_cmpfloat (mn[1], <, -0.85f);
}

/* Energy settles: a hanging cloth comes to rest instead of gaining energy. */
static void
test_cloth_energy (void)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new_grid (24, 24, 1.0f, 1.0f);
	gdouble             peak = 0.0;
	gdouble             e = 0.0;
	guint               s;

	g_object_set (cloth, "damping", 0.0f, NULL);
	lrg_cloth_pin (cloth, lrg_cloth_get_grid_index (cloth, 0, 0));
	lrg_cloth_pin (cloth, lrg_cloth_get_grid_index (cloth, 23, 0));
	for (s = 0; s < 600; s++)
	{
		lrg_cloth_step (cloth, DT);
		e = lrg_cloth_get_kinetic_energy (cloth);
		peak = MAX (peak, e);
		g_assert_true (isfinite (e));
	}
	/*
	 * The potential energy available is m g h with h at most the cloth's
	 * diagonal (~1.4 m): 0.25 kg * 9.81 * 1.5 = 3.7 J.  Kinetic energy may
	 * never exceed that.
	 */
	g_test_message ("peak kinetic energy %.4f J, after 10 s %.6f J", peak, e);
	g_assert_cmpfloat (peak, <, 3.7);
	g_assert_cmpfloat (e, <, 0.5 * peak);
	g_assert_true (all_finite (cloth));
}

/* Dropped on a sphere, every particle ends outside it. */
static void
test_cloth_sphere_collision (void)
{
	g_autoptr(LrgCloth) cloth = make_flat (24, 1.6f, 0.0f, 1.0f, 0.0f);
	const gfloat        center[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat        radius = 0.5f;
	gfloat              thickness = 0.0f;
	guint               n;
	const gfloat       *p;
	guint               s;
	guint               i;
	gfloat              closest = G_MAXFLOAT;

	g_assert_cmpuint (lrg_cloth_add_sphere (cloth, center, radius), !=, G_MAXUINT);
	g_object_get (cloth, "thickness", &thickness, NULL);
	for (s = 0; s < 180; s++)
		lrg_cloth_step (cloth, DT);
	g_assert_cmpuint (lrg_cloth_get_contact_count (cloth), >, 0);
	p = lrg_cloth_get_positions (cloth, &n);
	for (i = 0; i < n; i += 3)
	{
		gfloat d = sqrtf (p[i] * p[i] + p[i + 1] * p[i + 1] + p[i + 2] * p[i + 2]);

		closest = MIN (closest, d);
	}
	g_test_message ("closest particle %.4f, surface + thickness %.4f", closest, radius + thickness);
	g_assert_cmpfloat (closest, >=, radius + thickness - 2e-3f);
	/* It drapes: the corners hang below the top of the sphere. */
	{
		gfloat mn[3];
		gfloat mx[3];

		lrg_cloth_get_bounds (cloth, mn, mx);
		g_assert_cmpfloat (mn[1], <, 0.2f);
		g_assert_cmpfloat (mx[1], <, radius + 0.1f);
	}
}

/* Dropped across a horizontal capsule, nothing ends inside it. */
static void
test_cloth_capsule_collision (void)
{
	g_autoptr(LrgCloth) cloth = make_flat (20, 1.0f, 0.0f, 0.6f, 0.0f);
	const gfloat        a[3] = { -1.0f, 0.0f, 0.0f };
	const gfloat        b[3] = { 1.0f, 0.0f, 0.0f };
	guint               n;
	const gfloat       *p;
	guint               s;
	guint               i;
	gfloat              closest = G_MAXFLOAT;

	g_assert_cmpuint (lrg_cloth_add_capsule (cloth, a, b, 0.25f), !=, G_MAXUINT);
	for (s = 0; s < 120; s++)
		lrg_cloth_step (cloth, DT);
	p = lrg_cloth_get_positions (cloth, &n);
	for (i = 0; i < n; i += 3)
	{
		gfloat x = CLAMP (p[i], -1.0f, 1.0f);
		gfloat d = sqrtf ((p[i] - x) * (p[i] - x) + p[i + 1] * p[i + 1] + p[i + 2] * p[i + 2]);

		closest = MIN (closest, d);
	}
	g_test_message ("closest to the capsule axis %.4f", closest);
	g_assert_cmpfloat (closest, >=, 0.25f + 0.01f - 2e-3f);
	g_assert_true (all_finite (cloth));
}

/* A floor plane and a box: particles stay above and outside. */
static void
test_cloth_plane_box (void)
{
	g_autoptr(LrgCloth) cloth = make_flat (16, 1.0f, 0.0f, 0.8f, 0.0f);
	const gfloat        up[3] = { 0.0f, 2.0f, 0.0f };
	const gfloat        box_c[3] = { 0.0f, 0.15f, 0.0f };
	const gfloat        box_h[3] = { 0.2f, 0.15f, 0.2f };
	const gfloat        quat[4] = { 0.0f, 0.38268343f, 0.0f, 0.92387953f };
	guint               plane;
	guint               box;
	LrgClothColliderKind kind;
	guint               n;
	const gfloat       *p;
	guint               s;
	guint               i;

	plane = lrg_cloth_add_plane (cloth, up, 0.0f);
	box = lrg_cloth_add_box (cloth, box_c, box_h, quat);
	g_assert_cmpuint (lrg_cloth_get_collider_count (cloth), ==, 2);
	g_assert_true (lrg_cloth_get_collider_kind (cloth, box, &kind));
	g_assert_cmpint (kind, ==, LRG_CLOTH_COLLIDER_BOX);
	for (s = 0; s < 180; s++)
		lrg_cloth_step (cloth, DT);
	p = lrg_cloth_get_positions (cloth, &n);
	for (i = 0; i < n; i += 3)
	{
		/* Above the floor by the thickness. */
		g_assert_cmpfloat (p[i + 1], >=, 0.01f - 2e-3f);
		/* Outside the rotated box. */
		{
			gfloat c = 0.70710678f;
			gfloat lx = c * p[i] - c * p[i + 2];
			gfloat lz = c * p[i] + c * p[i + 2];
			gboolean inside = fabsf (lx) < 0.2f - 1e-3f && fabsf (p[i + 1] - 0.15f) < 0.15f - 1e-3f
			                  && fabsf (lz) < 0.2f - 1e-3f;

			g_assert_false (inside);
		}
	}
	g_assert_true (lrg_cloth_remove_collider (cloth, plane));
	g_assert_false (lrg_cloth_remove_collider (cloth, plane));
	lrg_cloth_clear_colliders (cloth);
	g_assert_cmpuint (lrg_cloth_get_collider_count (cloth), ==, 0);
	g_assert_cmpuint (lrg_cloth_add_sphere (cloth, box_c, -1.0f), ==, G_MAXUINT);
}

/* Dropped on a static box body of an LrgDynamicsWorld, it rests on top. */
static void
test_cloth_world_collision (void)
{
	g_autoptr(LrgCloth)         cloth = make_flat (16, 0.8f, 0.0f, 0.6f, 0.0f);
	g_autoptr(LrgDynamicsWorld) world = lrg_dynamics_world_new ();
	g_autoptr(LrgPhysicsShape)  box = lrg_physics_shape_new_box (1.0f, 0.25f, 1.0f);
	const gfloat                at[3] = { 0.0f, 0.0f, 0.0f };
	guint                       id = 0;
	guint                       n;
	const gfloat               *p;
	guint                       s;
	guint                       i;
	gfloat                      lowest = G_MAXFLOAT;

	g_assert_true (lrg_dynamics_world_add_body (world, box, LRG_DYNAMICS_BODY_STATIC, 0.0f, at, NULL, &id, NULL));
	lrg_cloth_set_dynamics_world (cloth, world, LRG_PHYSICS_LAYERS_ALL);
	g_assert_true (lrg_cloth_get_dynamics_world (cloth) == world);
	for (s = 0; s < 90; s++)
		lrg_cloth_step (cloth, DT);
	p = lrg_cloth_get_positions (cloth, &n);
	for (i = 0; i < n; i += 3)
		lowest = MIN (lowest, p[i + 1]);
	g_test_message ("lowest particle %.4f over a box top at 0.25", lowest);
	g_assert_cmpfloat (lowest, >=, 0.25f + 0.01f - 3e-3f);
	g_assert_cmpuint (lrg_cloth_get_contact_count (cloth), >, 0);
	lrg_cloth_set_dynamics_world (cloth, NULL, 0);
	for (s = 0; s < 30; s++)
		lrg_cloth_step (cloth, DT);
	p = lrg_cloth_get_positions (cloth, NULL);
	g_assert_cmpfloat (p[1], <, 0.2f);
}

/* A hard bone attachment follows the bone exactly; the rest of the cloth follows. */
static void
test_cloth_bone_attachment (void)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new_grid (16, 16, 1.0f, 1.0f);
	gfloat              bone[16];
	guint               c;
	guint               s;
	gfloat              p[3];
	gfloat              bottom0[3];

	lrg_math3d_identity (bone);
	lrg_cloth_set_bone_matrices (cloth, bone, 1);
	g_assert_cmpuint (lrg_cloth_get_bone_count (cloth), ==, 1);
	for (c = 0; c < 16; c++)
		g_assert_true (lrg_cloth_attach_to_bone (cloth, lrg_cloth_get_grid_index (cloth, c, 0), 0, NULL, 0.0f));
	g_assert_cmpint (lrg_cloth_get_attachment_bone (cloth, 3), ==, 0);
	g_assert_cmpint (lrg_cloth_get_attachment_bone (cloth, 200), ==, LRG_CLOTH_NO_BONE);
	/* A soft tether on the bottom corner keeps it within 0.3 m of its bone target. */
	g_assert_true (lrg_cloth_attach_to_bone (cloth, lrg_cloth_get_grid_index (cloth, 15, 15), 0, NULL, 0.3f));
	for (s = 0; s < 60; s++)
		lrg_cloth_step (cloth, DT);
	lrg_cloth_get_position (cloth, lrg_cloth_get_grid_index (cloth, 0, 15), bottom0);

	/* Move the bone 2 m along +X and 1 m up over two seconds. */
	for (s = 1; s <= 120; s++)
	{
		bone[12] = 2.0f * (gfloat) s / 120.0f;
		bone[13] = 1.0f * (gfloat) s / 120.0f;
		lrg_cloth_set_bone_matrices (cloth, bone, 1);
		lrg_cloth_step (cloth, DT);
		lrg_cloth_get_position (cloth, lrg_cloth_get_grid_index (cloth, 7, 0), p);
		g_assert_cmpfloat_with_epsilon (p[0], 7.0f / 15.0f + bone[12], 1e-4f);
		g_assert_cmpfloat_with_epsilon (p[1], bone[13], 1e-4f);
	}
	for (s = 0; s < 120; s++)
		lrg_cloth_step (cloth, DT);
	lrg_cloth_get_position (cloth, lrg_cloth_get_grid_index (cloth, 0, 15), p);
	g_assert_cmpfloat_with_epsilon (p[0], bottom0[0] + 2.0f, 0.05f);
	g_assert_cmpfloat_with_epsilon (p[1], bottom0[1] + 1.0f, 0.05f);

	/* The tethered corner is within its distance of its target (1, -1, 0) + bone. */
	lrg_cloth_get_position (cloth, lrg_cloth_get_grid_index (cloth, 15, 15), p);
	{
		gfloat dx = p[0] - 3.0f;
		gfloat dy = p[1] - 0.0f;
		gfloat dz = p[2];

		g_assert_cmpfloat (sqrtf (dx * dx + dy * dy + dz * dz), <=, 0.3f + 1e-4f);
	}
}

/* Transform-driven pins (a flagpole carried along). */
static void
test_cloth_transform_pins (void)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new_grid (8, 8, 1.0f, 1.0f);
	gfloat              m[16];
	gfloat              p[3];
	guint               s;

	lrg_cloth_pin (cloth, 0);
	lrg_math3d_identity (m);
	m[12] = 5.0f;
	lrg_cloth_set_transform (cloth, m);
	lrg_cloth_reset (cloth);
	lrg_cloth_get_position (cloth, 0, p);
	g_assert_cmpfloat_with_epsilon (p[0], 5.0f, 1e-6f);
	lrg_cloth_get_position (cloth, 63, p);
	g_assert_cmpfloat_with_epsilon (p[0], 6.0f, 1e-6f);
	m[14] = 3.0f;
	lrg_cloth_set_transform (cloth, m);
	for (s = 0; s < 4; s++)
		lrg_cloth_step (cloth, DT);
	lrg_cloth_get_position (cloth, 0, p);
	g_assert_cmpfloat_with_epsilon (p[2], 3.0f, 1e-6f);
	g_assert_true (lrg_cloth_pin_at (cloth, 7, NULL, 0.1f));
	g_assert_false (lrg_cloth_pin_at (cloth, 7, NULL, -1.0f));
}

/* Steady wind across a flag pushes it downwind. */
static gfloat
flag_mean_z (gfloat wind_z)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new_grid (20, 14, 1.5f, 1.0f);
	gfloat              wind[3] = { 0.0f, 0.0f, wind_z };
	const gfloat       *p;
	guint               n;
	guint               r;
	guint               s;
	guint               i;
	gfloat              sum = 0.0f;

	for (r = 0; r < 14; r++)
		lrg_cloth_pin (cloth, lrg_cloth_get_grid_index (cloth, 0, r));
	lrg_cloth_set_wind (cloth, wind);
	for (s = 0; s < 180; s++)
		lrg_cloth_step (cloth, DT);
	p = lrg_cloth_get_positions (cloth, &n);
	for (i = 0; i < n; i += 3)
		sum += p[i + 2];
	return sum / (gfloat) (n / 3);
}

static void
test_cloth_wind (void)
{
	gfloat calm = flag_mean_z (0.0f);
	gfloat windy = flag_mean_z (8.0f);
	g_autoptr(LrgCloth) cloth = lrg_cloth_new ();
	gfloat w[3] = { 1.0f, 2.0f, 3.0f };
	gfloat out[3];

	g_test_message ("mean z: calm %.4f, 8 m/s wind %.4f", calm, windy);
	g_assert_cmpfloat (fabsf (calm), <, 0.01f);
	g_assert_cmpfloat (windy, >, 0.3f);
	lrg_cloth_set_wind (cloth, w);
	lrg_cloth_get_wind (cloth, out);
	g_assert_cmpfloat (out[2], ==, 3.0f);
}

/* Two cloths fed the same way stay bit-identical. */
static void
run_scene (LrgCloth *cloth)
{
	const gfloat c[3] = { 0.3f, -0.6f, 0.2f };
	const gfloat wind[3] = { 2.0f, 0.0f, 3.0f };
	guint        s;

	g_object_set (cloth, "self-collision", TRUE, NULL);
	lrg_cloth_pin (cloth, 0);
	lrg_cloth_pin (cloth, 15);
	lrg_cloth_add_sphere (cloth, c, 0.3f);
	lrg_cloth_set_wind (cloth, wind);
	for (s = 0; s < 200; s++)
		lrg_cloth_step (cloth, DT);
}

static void
test_cloth_determinism (void)
{
	g_autoptr(LrgCloth) a = lrg_cloth_new_grid (16, 16, 1.0f, 1.0f);
	g_autoptr(LrgCloth) b = lrg_cloth_new_grid (16, 16, 1.0f, 1.0f);
	guint               n;
	const gfloat       *pa;
	const gfloat       *pb;

	run_scene (a);
	run_scene (b);
	pa = lrg_cloth_get_positions (a, &n);
	pb = lrg_cloth_get_positions (b, NULL);
	g_assert_cmpint (memcmp (pa, pb, n * sizeof (gfloat)), ==, 0);
	g_assert_cmpint (memcmp (lrg_cloth_get_velocities (a, NULL), lrg_cloth_get_velocities (b, NULL),
	                         n * sizeof (gfloat)), ==, 0);
	g_assert_true (all_finite (a));
}

/* Normals of a flat grid in XY face +Z or -Z consistently; readback arrays. */
static void
test_cloth_readback (void)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new_grid (4, 4, 1.0f, 1.0f);
	const gfloat       *n;
	guint               len;
	guint               i;
	gfloat              imp[3] = { 0.0f, 0.0f, 0.001f };
	gfloat              at[3] = { 9.0f, 9.0f, 9.0f };

	n = lrg_cloth_get_normals (cloth, &len);
	g_assert_cmpuint (len, ==, 48);
	for (i = 0; i < 16; i++)
		g_assert_cmpfloat_with_epsilon (fabsf (n[3 * i + 2]), 1.0f, 1e-5f);
	g_assert_nonnull (lrg_cloth_get_texcoords (cloth, &len));
	g_assert_cmpuint (len, ==, 32);
	g_assert_nonnull (lrg_cloth_get_rest_positions (cloth, &len));
	g_assert_true (lrg_cloth_apply_impulse (cloth, 5, imp));
	g_assert_cmpfloat (lrg_cloth_get_kinetic_energy (cloth), >, 0.0);
	g_assert_true (lrg_cloth_set_position (cloth, 5, at, FALSE));
	g_assert_cmpfloat (lrg_cloth_get_kinetic_energy (cloth), ==, 0.0);
	lrg_cloth_unpin_all (cloth);
	lrg_cloth_set_substeps (cloth, 1000);
	g_assert_cmpuint (lrg_cloth_get_substeps (cloth), ==, LRG_CLOTH_MAX_SUBSTEPS);
	lrg_cloth_set_compliance (cloth, 0.1f, 0.2f, 0.3f);
	{
		gfloat bend = 0.0f;

		g_object_get (cloth, "bend-compliance", &bend, NULL);
		g_assert_cmpfloat (bend, ==, 0.3f);
	}
}

/* Mesh creation needs a graphics context. */
static void
test_cloth_no_gpu (void)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new_grid (4, 4, 1.0f, 1.0f);
	g_autoptr(GError)   error = NULL;
	g_autoptr(GrlMesh)  mesh = NULL;

	mesh = lrg_cloth_create_mesh (cloth, &error);
	g_assert_null (mesh);
	g_assert_error (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_NO_GPU);
	lrg_cloth_draw (cloth, NULL, NULL);
}

/* 32x32 cloth substeps per millisecond (pinned top edge, a sphere, dihedral bends). */
static void
test_cloth_perf (void)
{
	g_autoptr(LrgCloth) cloth = lrg_cloth_new_grid (32, 32, 1.0f, 1.0f);
	const gfloat        c[3] = { 0.5f, -0.6f, 0.2f };
	gint64              t0;
	gint64              t1;
	guint               s;
	guint               c2;
	gdouble             per_ms;

	for (c2 = 0; c2 < 32; c2++)
		lrg_cloth_pin (cloth, lrg_cloth_get_grid_index (cloth, c2, 0));
	lrg_cloth_add_sphere (cloth, c, 0.3f);
	lrg_cloth_step (cloth, DT);
	t0 = g_get_monotonic_time ();
	for (s = 0; s < 120; s++)
		lrg_cloth_step (cloth, DT);
	t1 = g_get_monotonic_time ();
	per_ms = (gdouble) (120 * lrg_cloth_get_substeps (cloth)) / ((gdouble) (t1 - t0) / 1000.0);
	g_test_message ("32x32 cloth: %.2f substeps/ms (%.3f ms per 8-substep step)",
	                per_ms, (gdouble) (t1 - t0) / 1000.0 / 120.0);
	/* A loose floor: sanitizer and debug builds run about ten times slower. */
	g_assert_cmpfloat (per_ms, >, 0.1);
}

/* The component: grid and pins from properties, follows its entity, renders one MESH item. */
static void
test_cloth_component (void)
{
	g_autoptr(LrgWorld3D)    world = lrg_world3d_new ();
	g_autoptr(LrgEntity3D)   pole = lrg_entity3d_new_with_name ("pole");
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();
	LrgClothComponent       *flag = lrg_cloth_component_new_flag (1.0f, 0.6f);
	LrgCloth                *cloth;
	const LrgRenderItem     *item;
	guint                    cols = 0;
	guint                    rows = 0;
	guint                    s;
	gfloat                   p[3];
	gfloat                   mn[3];
	gfloat                   mx[3];

	g_assert_cmpuint (lrg_cloth_component_get_pin_edges (flag), ==, LRG_CLOTH_PIN_LEFT);
	lrg_cloth_component_set_wind (flag, 0.0f, 0.0f, 6.0f);
	lrg_cloth_component_set_color (flag, 0.1f, 0.2f, 0.9f, 1.0f);
	lrg_entity3d_set_position_xyz (pole, 2.0f, 3.0f, 0.0f);
	lrg_entity3d_add_component (pole, LRG_COMPONENT (flag));
	g_object_unref (flag);
	lrg_world3d_add_entity (world, pole);

	cloth = lrg_cloth_component_get_cloth (flag);
	g_assert_true (lrg_cloth_get_grid_size (cloth, &cols, &rows));
	g_assert_cmpuint (cols, ==, 21);
	g_assert_cmpuint (rows, ==, 13);
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_ATTACHMENT), ==, 13);

	for (s = 0; s < 90; s++)
		lrg_world3d_step (world, 1.0f / 60.0f);
	/* The pinned corner sits at the entity; the free edge is blown along +Z. */
	lrg_cloth_get_position (cloth, 0, p);
	g_assert_cmpfloat_with_epsilon (p[0], 2.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (p[1], 3.0f, 1e-4f);
	lrg_cloth_get_bounds (cloth, mn, mx);
	g_assert_cmpfloat (mx[2], >, 0.2f);

	/* Moving the entity carries the pins along. */
	lrg_entity3d_set_position_xyz (pole, 2.0f, 4.0f, 0.0f);
	for (s = 0; s < 10; s++)
		lrg_world3d_step (world, 1.0f / 60.0f);
	lrg_cloth_get_position (cloth, 0, p);
	g_assert_cmpfloat_with_epsilon (p[1], 4.0f, 1e-4f);

	/* One MESH item (no mesh headless) with an identity matrix and the colour. */
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_OPAQUE), ==, 1);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 0);
	g_assert_cmpint (item->kind, ==, LRG_RENDER_ITEM_MESH);
	g_assert_null (item->mesh);
	g_assert_null (lrg_cloth_component_get_mesh (flag));
	g_assert_cmpfloat (item->world_matrix[13], ==, 0.0f);
	g_assert_cmpfloat (item->color[2], ==, 0.9f);
	g_assert_true ((item->flags & LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED) != 0);
	g_assert_cmpfloat (item->bounds.max.y, >=, 4.0f - 1e-3f);

	/* A grid change rebuilds; reset puts it back at rest. */
	lrg_cloth_component_set_grid (flag, 8, 6, 2.0f, 1.0f);
	lrg_cloth_component_set_pin_edges (flag, LRG_CLOTH_PIN_TOP_CORNERS);
	cloth = lrg_cloth_component_get_cloth (flag);
	g_assert_cmpuint (lrg_cloth_get_vertex_count (cloth), ==, 48);
	g_assert_cmpuint (lrg_cloth_get_constraint_count (cloth, LRG_CLOTH_CONSTRAINT_ATTACHMENT), ==, 2);
	lrg_cloth_component_reset (flag);
	lrg_cloth_get_position (cloth, 47, p);
	g_assert_cmpfloat_with_epsilon (p[0], 4.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (p[1], 3.0f, 1e-4f);
	g_assert_null (lrg_cloth_component_get_skeleton (flag));
}

/* The component collides with a body in the world's physics. */
static void
test_cloth_component_physics (void)
{
	g_autoptr(LrgWorld3D)       world = lrg_world3d_new ();
	g_autoptr(LrgEntity3D)      sheet = lrg_entity3d_new_with_name ("sheet");
	g_autoptr(LrgPhysicsShape)  ball = lrg_physics_shape_new_sphere (0.4f);
	LrgDynamicsWorld           *physics = lrg_world3d_ensure_physics (world);
	LrgClothComponent          *cloth = lrg_cloth_component_new ();
	const gfloat                at[3] = { 0.5f, -0.5f, 0.5f };
	guint                       id;
	guint                       s;
	guint                       n;
	guint                       i;
	const gfloat               *p;
	gfloat                      closest = G_MAXFLOAT;

	/* A horizontal sheet: the grid's local XY plane turned to world XZ. */
	g_assert_true (lrg_dynamics_world_add_body (physics, ball, LRG_DYNAMICS_BODY_STATIC, 0.0f, at, NULL, &id, NULL));
	g_object_set (cloth, "pin-edges", LRG_CLOTH_PIN_NONE, "drag", 0.0f, NULL);
	lrg_entity3d_set_position_xyz (sheet, 0.0f, 0.0f, 0.0f);
	{
		/* -90 degrees about X: local -Y (down the grid) becomes world +Z. */
		const gfloat q[4] = { -0.70710678f, 0.0f, 0.0f, 0.70710678f };

		lrg_entity3d_set_rotation (sheet, q);
	}
	lrg_entity3d_add_component (sheet, LRG_COMPONENT (cloth));
	g_object_unref (cloth);
	lrg_world3d_add_entity (world, sheet);
	for (s = 0; s < 60; s++)
		lrg_world3d_step (world, 1.0f / 60.0f);
	g_assert_true (lrg_cloth_get_dynamics_world (lrg_cloth_component_get_cloth (cloth)) == physics);
	p = lrg_cloth_get_positions (lrg_cloth_component_get_cloth (cloth), &n);
	for (i = 0; i < n; i += 3)
	{
		gfloat d[3] = { p[i] - at[0], p[i + 1] - at[1], p[i + 2] - at[2] };

		closest = MIN (closest, sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
	}
	g_test_message ("closest particle to the ball %.4f (radius 0.4)", closest);
	g_assert_cmpfloat (closest, >=, 0.4f + 0.01f - 5e-3f);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/cloth/grid", test_cloth_grid);
	g_test_add_func ("/cloth/mesh-errors", test_cloth_mesh_errors);
	g_test_add_func ("/cloth/stretch-bounds", test_cloth_stretch_bounds);
	g_test_add_func ("/cloth/energy", test_cloth_energy);
	g_test_add_func ("/cloth/collision/sphere", test_cloth_sphere_collision);
	g_test_add_func ("/cloth/collision/capsule", test_cloth_capsule_collision);
	g_test_add_func ("/cloth/collision/plane-box", test_cloth_plane_box);
	g_test_add_func ("/cloth/collision/world", test_cloth_world_collision);
	g_test_add_func ("/cloth/bone-attachment", test_cloth_bone_attachment);
	g_test_add_func ("/cloth/transform-pins", test_cloth_transform_pins);
	g_test_add_func ("/cloth/wind", test_cloth_wind);
	g_test_add_func ("/cloth/determinism", test_cloth_determinism);
	g_test_add_func ("/cloth/readback", test_cloth_readback);
	g_test_add_func ("/cloth/no-gpu", test_cloth_no_gpu);
	g_test_add_func ("/cloth/perf", test_cloth_perf);
	g_test_add_func ("/cloth/component", test_cloth_component);
	g_test_add_func ("/cloth/component/physics", test_cloth_component_physics);
	return g_test_run ();
}
