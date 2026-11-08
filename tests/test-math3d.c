/* test-math3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the shared core 3D math (src/core/lrg-math3d.{c,h}):
 *  - bit-for-bit equality with the 0.8 editor implementation (a verbatim
 *    copy of src/editor/lrg-editor-math.c 0.8.1 lives below as the
 *    reference), and with the lrg_editor_math_* wrappers when the editor
 *    is built;
 *  - Euler <-> quaternion <-> matrix round trips, including gimbal lock;
 *  - TRS compose/decompose with quaternions and mirrored scale;
 *  - multiply / invert / transpose / transform / look-at / projections.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-4f)

/* Element (row, col) of a column-major 4x4 matrix. */
#define M(m, row, col) ((m)[(col) * 4 + (row)])

/* ==========================================================================
 * Reference: the 0.8.1 editor implementation, verbatim
 * ========================================================================== */

static gfloat
ref_v3_length (const gfloat *a)
{
	return sqrtf (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

static void
ref_compose (const gfloat *location,
             const gfloat *rotation,
             const gfloat *scale,
             gfloat       *out)
{
	gfloat ca = cosf (rotation[0]), sa = sinf (rotation[0]);
	gfloat cb = cosf (rotation[1]), sb = sinf (rotation[1]);
	gfloat cc = cosf (rotation[2]), sc = sinf (rotation[2]);
	gfloat r[3][3];
	gint   row, col;

	r[0][0] = cb * cc;
	r[0][1] = sa * sb * cc - ca * sc;
	r[0][2] = ca * sb * cc + sa * sc;
	r[1][0] = cb * sc;
	r[1][1] = sa * sb * sc + ca * cc;
	r[1][2] = ca * sb * sc - sa * cc;
	r[2][0] = -sb;
	r[2][1] = sa * cb;
	r[2][2] = ca * cb;

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

static gboolean
ref_decompose (const gfloat *m,
               gfloat       *location,
               gfloat       *rotation,
               gfloat       *scale)
{
	gfloat r[3][3];
	gfloat det;
	gint   row, col;

	location[0] = M (m, 0, 3);
	location[1] = M (m, 1, 3);
	location[2] = M (m, 2, 3);

	for (col = 0; col < 3; col++)
	{
		gfloat c[3] = { M (m, 0, col), M (m, 1, col), M (m, 2, col) };

		scale[col] = ref_v3_length (c);
		if (scale[col] < 1e-12f)
		{
			rotation[0] = rotation[1] = rotation[2] = 0.0f;
			return FALSE;
		}
	}

	det = M (m, 0, 0) * (M (m, 1, 1) * M (m, 2, 2) - M (m, 2, 1) * M (m, 1, 2)) -
	      M (m, 0, 1) * (M (m, 1, 0) * M (m, 2, 2) - M (m, 2, 0) * M (m, 1, 2)) +
	      M (m, 0, 2) * (M (m, 1, 0) * M (m, 2, 1) - M (m, 2, 0) * M (m, 1, 1));
	if (det < 0.0f)
		scale[0] = -scale[0];

	for (col = 0; col < 3; col++)
		for (row = 0; row < 3; row++)
			r[row][col] = M (m, row, col) / scale[col];

	if (r[2][0] < 0.9999f && r[2][0] > -0.9999f)
	{
		rotation[1] = asinf (-r[2][0]);
		rotation[0] = atan2f (r[2][1], r[2][2]);
		rotation[2] = atan2f (r[1][0], r[0][0]);
	}
	else
	{
		rotation[1] = (r[2][0] <= -0.9999f) ? (gfloat) G_PI_2 : (gfloat) -G_PI_2;
		rotation[0] = atan2f (-r[1][2], r[1][1]);
		rotation[2] = 0.0f;
	}

	return TRUE;
}

static void
ref_multiply (const gfloat *a,
              const gfloat *b,
              gfloat       *out)
{
	gfloat tmp[16];
	gint   row, col, k;

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

static gboolean
ref_invert (const gfloat *m,
            gfloat       *out)
{
	gfloat inv[16];
	gfloat det;
	gint   i;

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
	if (fabsf (det) < 1e-20f)
	{
		memset (out, 0, 16 * sizeof (gfloat));
		out[0] = out[5] = out[10] = out[15] = 1.0f;
		return FALSE;
	}

	det = 1.0f / det;
	for (i = 0; i < 16; i++)
		out[i] = inv[i] * det;

	return TRUE;
}

static void
ref_transform_point (const gfloat *m,
                     const gfloat *p,
                     gfloat       *out)
{
	gfloat x = M (m, 0, 0) * p[0] + M (m, 0, 1) * p[1] + M (m, 0, 2) * p[2] + M (m, 0, 3);
	gfloat y = M (m, 1, 0) * p[0] + M (m, 1, 1) * p[1] + M (m, 1, 2) * p[2] + M (m, 1, 3);
	gfloat z = M (m, 2, 0) * p[0] + M (m, 2, 1) * p[1] + M (m, 2, 2) * p[2] + M (m, 2, 3);
	gfloat w = M (m, 3, 0) * p[0] + M (m, 3, 1) * p[1] + M (m, 3, 2) * p[2] + M (m, 3, 3);

	if (w != 0.0f && w != 1.0f)
	{
		x /= w; y /= w; z /= w;
	}
	out[0] = x; out[1] = y; out[2] = z;
}

static void
ref_perspective (gfloat fovy_deg, gfloat aspect, gfloat n, gfloat f, gfloat *out)
{
	gfloat t = 1.0f / tanf (fovy_deg * (gfloat) G_PI / 360.0f);

	memset (out, 0, 16 * sizeof (gfloat));
	M (out, 0, 0) = t / aspect;
	M (out, 1, 1) = t;
	M (out, 2, 2) = -(f + n) / (f - n);
	M (out, 2, 3) = -(2.0f * f * n) / (f - n);
	M (out, 3, 2) = -1.0f;
}

static void
ref_ortho (gfloat ortho_height, gfloat aspect, gfloat n, gfloat f, gfloat *out)
{
	gfloat top = ortho_height * 0.5f;
	gfloat right = top * aspect;

	memset (out, 0, 16 * sizeof (gfloat));
	M (out, 0, 0) = 1.0f / right;
	M (out, 1, 1) = 1.0f / top;
	M (out, 2, 2) = -2.0f / (f - n);
	M (out, 2, 3) = -(f + n) / (f - n);
	M (out, 3, 3) = 1.0f;
}

/* ==========================================================================
 * Sample set
 * ========================================================================== */

/* Deterministic pseudo-random TRS samples, plus hand-picked edge cases. */
typedef struct
{
	gfloat loc[3];
	gfloat rot[3];
	gfloat scl[3];
} Sample;

#define N_RANDOM_SAMPLES (512)

static GArray *
make_samples (void)
{
	GArray  *a = g_array_new (FALSE, TRUE, sizeof (Sample));
	GRand   *rng = g_rand_new_with_seed (0x1e7e1);
	Sample   s;
	gint     i, k;
	static const gfloat edge_rot[][3] = {
		{ 0.0f, 0.0f, 0.0f },
		{ (gfloat) G_PI_2, 0.0f, 0.0f },
		{ 0.0f, (gfloat) G_PI_2, 0.0f },          /* gimbal lock (+90) */
		{ 0.0f, (gfloat) -G_PI_2, 0.0f },         /* gimbal lock (-90) */
		{ 0.3f, (gfloat) G_PI_2, 0.2f },
		{ 0.0f, 0.0f, (gfloat) G_PI },
		{ (gfloat) -G_PI, 0.1f, (gfloat) G_PI },
		{ 1.0f, 2.0f, 3.0f },
	};

	for (i = 0; i < (gint) G_N_ELEMENTS (edge_rot); i++)
	{
		memset (&s, 0, sizeof s);
		s.loc[0] = (gfloat) i; s.loc[1] = -2.0f; s.loc[2] = 0.5f;
		memcpy (s.rot, edge_rot[i], sizeof s.rot);
		s.scl[0] = 1.0f; s.scl[1] = 2.0f; s.scl[2] = 0.5f;
		g_array_append_val (a, s);
		/* Same rotation with a mirrored X scale. */
		s.scl[0] = -1.5f;
		g_array_append_val (a, s);
	}

	for (i = 0; i < N_RANDOM_SAMPLES; i++)
	{
		for (k = 0; k < 3; k++)
		{
			s.loc[k] = (gfloat) g_rand_double_range (rng, -100.0, 100.0);
			s.rot[k] = (gfloat) g_rand_double_range (rng, -G_PI, G_PI);
			s.scl[k] = (gfloat) g_rand_double_range (rng, 0.05, 8.0);
		}
		/* Keep the pitch off exact gimbal for the random part. */
		s.rot[1] = (gfloat) g_rand_double_range (rng, -1.5, 1.5);
		g_array_append_val (a, s);
	}

	g_rand_free (rng);
	return a;
}

static void
assert_bits_equal (const gfloat *a,
                   const gfloat *b,
                   gsize         n,
                   const gchar  *what,
                   guint         index)
{
	if (memcmp (a, b, n * sizeof (gfloat)) != 0)
	{
		gsize i;

		for (i = 0; i < n; i++)
			if (memcmp (&a[i], &b[i], sizeof (gfloat)) != 0)
				g_error ("%s: sample %u element %" G_GSIZE_FORMAT ": %.9g != %.9g",
				         what, index, i, (gdouble) a[i], (gdouble) b[i]);
	}
}

static void
assert_vec_near (const gfloat *a,
                 const gfloat *b,
                 gsize         n,
                 gfloat        eps)
{
	gsize i;

	for (i = 0; i < n; i++)
		g_assert_cmpfloat_with_epsilon (a[i], b[i], eps);
}

/* Two quaternions are the same rotation when q == +-p. */
static void
assert_same_rotation_quat (const gfloat *q,
                           const gfloat *p)
{
	gfloat d = q[0] * p[0] + q[1] * p[1] + q[2] * p[2] + q[3] * p[3];

	g_assert_cmpfloat_with_epsilon (fabsf (d), 1.0f, 1e-4f);
}

/* ==========================================================================
 * Bit-for-bit with the 0.8 editor math
 * ========================================================================== */

static void
test_bitwise_compose_decompose (void)
{
	g_autoptr(GArray) samples = make_samples ();
	guint             i;

	for (i = 0; i < samples->len; i++)
	{
		Sample  *s = &g_array_index (samples, Sample, i);
		gfloat   a[16], b[16];
		gfloat   la[3], ra[3], sa[3], lb[3], rb[3], sb[3];
		gboolean oka, okb;

		ref_compose (s->loc, s->rot, s->scl, a);
		lrg_math3d_compose (s->loc, s->rot, s->scl, b);
		assert_bits_equal (a, b, 16, "compose", i);

		oka = ref_decompose (a, la, ra, sa);
		okb = lrg_math3d_decompose (a, lb, rb, sb);
		g_assert_cmpint (oka, ==, okb);
		assert_bits_equal (la, lb, 3, "decompose.location", i);
		assert_bits_equal (ra, rb, 3, "decompose.rotation", i);
		assert_bits_equal (sa, sb, 3, "decompose.scale", i);

#ifdef LRG_BUILD_EDITOR
		/* The editor API is a wrapper over core now. */
		lrg_editor_math_compose (s->loc, s->rot, s->scl, b);
		assert_bits_equal (a, b, 16, "editor compose", i);
		lrg_editor_math_decompose (a, lb, rb, sb);
		assert_bits_equal (ra, rb, 3, "editor decompose", i);
#endif
	}
}

static void
test_bitwise_multiply_invert_transform (void)
{
	g_autoptr(GArray) samples = make_samples ();
	guint             i;

	for (i = 0; i + 1 < samples->len; i++)
	{
		Sample *s = &g_array_index (samples, Sample, i);
		Sample *t = &g_array_index (samples, Sample, i + 1);
		gfloat  ma[16], mb[16], ra[16], rb[16];
		gfloat  pa[3], pb[3];

		ref_compose (s->loc, s->rot, s->scl, ma);
		ref_compose (t->loc, t->rot, t->scl, mb);

		ref_multiply (ma, mb, ra);
		lrg_math3d_multiply (ma, mb, rb);
		assert_bits_equal (ra, rb, 16, "multiply", i);

		g_assert_cmpint (ref_invert (ra, ra), ==, lrg_math3d_invert (rb, rb));
		assert_bits_equal (ra, rb, 16, "invert", i);

		ref_transform_point (ma, t->loc, pa);
		lrg_math3d_transform_point (ma, t->loc, pb);
		assert_bits_equal (pa, pb, 3, "transform_point", i);

#ifdef LRG_BUILD_EDITOR
		lrg_editor_math_multiply (ma, mb, rb);
		ref_multiply (ma, mb, ra);
		assert_bits_equal (ra, rb, 16, "editor multiply", i);
		lrg_editor_math_transform_point (ma, t->loc, pb);
		assert_bits_equal (pa, pb, 3, "editor transform_point", i);
#endif
	}
}

/*
 * The editor's projection now calls lrg_math3d_perspective()/ortho() with
 * radians and explicit planes; the results must equal the 0.8 inline code.
 */
static void
test_bitwise_projection (void)
{
	static const gfloat fovs[] = { 30.0f, 45.0f, 60.0f, 75.5f, 90.0f, 120.0f };
	static const gfloat aspects[] = { 0.5f, 1.0f, 1.3333334f, 1.7777778f, 2.37f };
	guint i, j;

	for (i = 0; i < G_N_ELEMENTS (fovs); i++)
		for (j = 0; j < G_N_ELEMENTS (aspects); j++)
		{
			gfloat a[16], b[16];
			gfloat top, right;

			ref_perspective (fovs[i], aspects[j], 0.05f, 1000.0f, a);
			lrg_math3d_perspective (fovs[i] * (gfloat) G_PI / 180.0f,
			                        aspects[j], 0.05f, 1000.0f, b);
			assert_bits_equal (a, b, 16, "perspective", i * 16 + j);

			ref_ortho (fovs[i] * 0.1f, aspects[j], 0.05f, 500.0f, a);
			top = fovs[i] * 0.1f * 0.5f;
			right = top * aspects[j];
			lrg_math3d_ortho (-right, right, -top, top, 0.05f, 500.0f, b);
			assert_bits_equal (a, b, 16, "ortho", i * 16 + j);
		}
}

#ifdef LRG_BUILD_EDITOR
/* View matrix: lrg_editor_view_get_view_matrix() wraps lrg_math3d_look_at(). */
static void
test_editor_view_matches (void)
{
	LrgEditorView view;
	gfloat        a[16], b[16];

	lrg_editor_view_init_default (&view, 800.0f, 600.0f);
	lrg_editor_view_get_view_matrix (&view, a);
	lrg_math3d_look_at (view.position, view.target, view.up, b);
	assert_bits_equal (a, b, 16, "view", 0);

	/* Straight down (the top view): screen-up becomes -Z. */
	view.position[0] = 0.0f; view.position[1] = 10.0f; view.position[2] = 0.0f;
	view.target[0] = 0.0f; view.target[1] = 0.0f; view.target[2] = 0.0f;
	lrg_editor_view_get_view_matrix (&view, a);
	lrg_math3d_look_at (view.position, view.target, view.up, b);
	assert_bits_equal (a, b, 16, "top view", 1);
}
#endif

/* ==========================================================================
 * Round trips and conventions
 * ========================================================================== */

static void
test_euler_quat_round_trip (void)
{
	g_autoptr(GArray) samples = make_samples ();
	guint             i;

	for (i = 0; i < samples->len; i++)
	{
		Sample *s = &g_array_index (samples, Sample, i);
		gfloat  q[4], q2[4], e[3];
		gfloat  ma[16], mb[16];
		gfloat  zero[3] = { 0.0f, 0.0f, 0.0f };
		gfloat  one[3] = { 1.0f, 1.0f, 1.0f };

		/* The quaternion builds the same rotation matrix as the Euler. */
		lrg_math3d_euler_to_quat (s->rot, q);
		g_assert_cmpfloat_with_epsilon (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3],
		                                1.0f, 1e-5f);
		lrg_math3d_compose (zero, s->rot, one, ma);
		lrg_math3d_quat_to_matrix (q, mb);
		assert_vec_near (ma, mb, 16, EPS);

		/* quat -> euler -> quat is the same rotation (Euler angles are
		 * not unique, especially at gimbal lock). */
		lrg_math3d_quat_to_euler (q, e);
		lrg_math3d_euler_to_quat (e, q2);
		assert_same_rotation_quat (q, q2);

		/* matrix -> quat -> matrix */
		lrg_math3d_matrix_to_quat (ma, q2);
		g_assert_cmpfloat (q2[3], >=, 0.0f);
		assert_same_rotation_quat (q, q2);
	}
}

static void
test_gimbal (void)
{
	gfloat rot[3] = { 0.4f, (gfloat) G_PI_2, 0.25f };
	gfloat loc[3] = { 1.0f, 2.0f, 3.0f };
	gfloat scl[3] = { 1.0f, 1.0f, 1.0f };
	gfloat m[16], m2[16];
	gfloat l[3], r[3], s[3];

	/* At +90 pitch only X - Z is observable: Z folds into X. */
	lrg_math3d_compose (loc, rot, scl, m);
	g_assert_true (lrg_math3d_decompose (m, l, r, s));
	g_assert_cmpfloat_with_epsilon (r[1], (gfloat) G_PI_2, EPS);
	g_assert_cmpfloat (r[2], ==, 0.0f);
	g_assert_cmpfloat_with_epsilon (r[0], 0.4f - 0.25f, 1e-3f);
	lrg_math3d_compose (l, r, s, m2);
	assert_vec_near (m, m2, 16, 1e-3f);

	/* At -90 pitch it is X + Z. */
	rot[1] = (gfloat) -G_PI_2;
	lrg_math3d_compose (loc, rot, scl, m);
	g_assert_true (lrg_math3d_decompose (m, l, r, s));
	g_assert_cmpfloat_with_epsilon (r[1], (gfloat) -G_PI_2, EPS);
	g_assert_cmpfloat_with_epsilon (r[0], 0.4f + 0.25f, 1e-3f);
	lrg_math3d_compose (l, r, s, m2);
	assert_vec_near (m, m2, 16, 1e-3f);
}

static void
test_trs_quat_round_trip (void)
{
	g_autoptr(GArray) samples = make_samples ();
	guint             i;

	for (i = 0; i < samples->len; i++)
	{
		Sample *s = &g_array_index (samples, Sample, i);
		gfloat  q[4], q2[4];
		gfloat  m[16], m2[16];
		gfloat  l[3], sc[3];

		lrg_math3d_euler_to_quat (s->rot, q);
		lrg_math3d_compose_quat (s->loc, q, s->scl, m);

		/* Same matrix as the Euler compose. */
		lrg_math3d_compose (s->loc, s->rot, s->scl, m2);
		assert_vec_near (m, m2, 16, 1e-3f);

		g_assert_true (lrg_math3d_decompose_quat (m, l, q2, sc));
		assert_vec_near (l, s->loc, 3, EPS);
		lrg_math3d_compose_quat (l, q2, sc, m2);
		assert_vec_near (m, m2, 16, 1e-3f);

		/* Mirrored input: the sign lands on X like in the Euler path. */
		if (s->scl[0] < 0.0f)
			g_assert_cmpfloat (sc[0], <, 0.0f);
		else
		{
			assert_vec_near (sc, s->scl, 3, 1e-3f);
			assert_same_rotation_quat (q, q2);
		}
	}
}

static void
test_degenerate (void)
{
	gfloat m[16];
	gfloat l[3], r[3], s[3], q[4];
	gfloat inv[16];
	gfloat id[16];

	lrg_math3d_identity (m);
	M (m, 1, 1) = 0.0f;   /* zero Y scale */
	g_assert_false (lrg_math3d_decompose (m, l, r, s));
	g_assert_cmpfloat (r[0], ==, 0.0f);
	g_assert_false (lrg_math3d_decompose_quat (m, l, q, s));
	g_assert_cmpfloat (q[3], ==, 1.0f);

	memset (m, 0, sizeof m);
	g_assert_false (lrg_math3d_invert (m, inv));
	lrg_math3d_identity (id);
	assert_bits_equal (inv, id, 16, "singular invert", 0);

	/* Zero quaternion / axis normalise to the identity. */
	memset (q, 0, sizeof q);
	lrg_math3d_quat_normalize (q, q);
	g_assert_cmpfloat (q[3], ==, 1.0f);
	l[0] = l[1] = l[2] = 0.0f;
	lrg_math3d_quat_from_axis_angle (l, 1.0f, q);
	g_assert_cmpfloat (q[3], ==, 1.0f);
}

static void
test_matrix_ops (void)
{
	gfloat loc[3] = { 1.0f, -2.0f, 3.0f };
	gfloat rot[3] = { 0.3f, -0.7f, 1.1f };
	gfloat scl[3] = { 2.0f, 0.5f, 1.5f };
	gfloat m[16], inv[16], prod[16], id[16], t[16], tt[16];
	gfloat p[3] = { 4.0f, 5.0f, 6.0f };
	gfloat w[3], back[3], d[3], d2[3];

	lrg_math3d_compose (loc, rot, scl, m);
	g_assert_true (lrg_math3d_invert (m, inv));
	lrg_math3d_multiply (m, inv, prod);
	lrg_math3d_identity (id);
	assert_vec_near (prod, id, 16, EPS);

	/* Aliasing: out == input. */
	memcpy (prod, m, sizeof m);
	lrg_math3d_multiply (prod, inv, prod);
	assert_vec_near (prod, id, 16, EPS);

	lrg_math3d_transform_point (m, p, w);
	lrg_math3d_transform_point (inv, w, back);
	assert_vec_near (back, p, 3, EPS);

	/* Directions ignore translation. */
	lrg_math3d_transform_direction (m, p, d);
	lrg_math3d_transform_point (m, p, w);
	d2[0] = w[0] - loc[0]; d2[1] = w[1] - loc[1]; d2[2] = w[2] - loc[2];
	assert_vec_near (d, d2, 3, EPS);

	lrg_math3d_transpose (m, t);
	g_assert_cmpfloat (M (t, 0, 3), ==, M (m, 3, 0));
	g_assert_cmpfloat (M (t, 3, 0), ==, M (m, 0, 3));
	lrg_math3d_transpose (t, tt);
	assert_bits_equal (tt, m, 16, "transpose twice", 0);
}

static void
test_quat_ops (void)
{
	gfloat axis_y[3] = { 0.0f, 2.0f, 0.0f };   /* normalised internally */
	gfloat axis_x[3] = { 1.0f, 0.0f, 0.0f };
	gfloat qy[4], qx[4], q[4], inv[4], id[4], s[4];
	gfloat v[3] = { 1.0f, 0.0f, 0.0f };
	gfloat out[3];
	gfloat e_a[3] = { 0.0f, 0.0f, 0.0f };
	gfloat e_b[3] = { 0.0f, 1.0f, 0.0f };
	gfloat qa[4], qb[4], qh[4];
	gfloat e_h[3] = { 0.0f, 0.5f, 0.0f };

	/* +90 about Y sends +X to -Z (right-handed). */
	lrg_math3d_quat_from_axis_angle (axis_y, (gfloat) G_PI_2, qy);
	lrg_math3d_quat_rotate_vector (qy, v, out);
	g_assert_cmpfloat_with_epsilon (out[0], 0.0f, EPS);
	g_assert_cmpfloat_with_epsilon (out[2], -1.0f, EPS);

	/* a * b applies b first: rotate X by 90 about X (no-op on +X), then Y. */
	lrg_math3d_quat_from_axis_angle (axis_x, (gfloat) G_PI_2, qx);
	lrg_math3d_quat_multiply (qy, qx, q);
	lrg_math3d_quat_rotate_vector (q, v, out);
	g_assert_cmpfloat_with_epsilon (out[2], -1.0f, EPS);

	/* q * q^-1 = identity */
	lrg_math3d_quat_invert (q, inv);
	lrg_math3d_quat_multiply (q, inv, id);
	g_assert_cmpfloat_with_epsilon (id[3], 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (id[0], 0.0f, EPS);

	/* slerp endpoints and midpoint about one axis */
	lrg_math3d_euler_to_quat (e_a, qa);
	lrg_math3d_euler_to_quat (e_b, qb);
	lrg_math3d_euler_to_quat (e_h, qh);
	lrg_math3d_quat_slerp (qa, qb, 0.0f, s);
	assert_same_rotation_quat (s, qa);
	lrg_math3d_quat_slerp (qa, qb, 1.0f, s);
	assert_same_rotation_quat (s, qb);
	lrg_math3d_quat_slerp (qa, qb, 0.5f, s);
	assert_same_rotation_quat (s, qh);

	/* Shorter arc: slerp towards -qb is the same as towards qb. */
	qb[0] = -qb[0]; qb[1] = -qb[1]; qb[2] = -qb[2]; qb[3] = -qb[3];
	lrg_math3d_quat_slerp (qa, qb, 0.5f, s);
	assert_same_rotation_quat (s, qh);

	lrg_math3d_quat_identity (q);
	g_assert_cmpfloat (q[3], ==, 1.0f);
}

static void
test_camera (void)
{
	gfloat eye[3] = { 0.0f, 0.0f, 5.0f };
	gfloat target[3] = { 0.0f, 0.0f, 0.0f };
	gfloat up[3] = { 0.0f, 1.0f, 0.0f };
	gfloat view[16], proj[16], vp[16];
	gfloat p[3], out[3];

	/* Looking down -Z from z = 5: the origin is 5 units in front. */
	lrg_math3d_look_at (eye, target, up, view);
	lrg_math3d_transform_point (view, target, out);
	g_assert_cmpfloat_with_epsilon (out[2], -5.0f, EPS);

	/* The target projects to the centre of NDC, inside the depth range. */
	lrg_math3d_perspective ((gfloat) G_PI / 3.0f, 16.0f / 9.0f, 0.1f, 100.0f, proj);
	lrg_math3d_multiply (proj, view, vp);
	lrg_math3d_transform_point (vp, target, out);
	g_assert_cmpfloat_with_epsilon (out[0], 0.0f, EPS);
	g_assert_cmpfloat_with_epsilon (out[1], 0.0f, EPS);
	g_assert_cmpfloat (out[2], >, -1.0f);
	g_assert_cmpfloat (out[2], <, 1.0f);

	/* Near plane maps to -1, far plane to +1. */
	p[0] = 0.0f; p[1] = 0.0f; p[2] = -0.1f;
	lrg_math3d_transform_point (proj, p, out);
	g_assert_cmpfloat_with_epsilon (out[2], -1.0f, 1e-3f);
	p[2] = -100.0f;
	lrg_math3d_transform_point (proj, p, out);
	g_assert_cmpfloat_with_epsilon (out[2], 1.0f, 1e-3f);

	/* Ortho: box corners map to the NDC cube corners; symmetric -> +0. */
	lrg_math3d_ortho (-4.0f, 4.0f, -2.0f, 2.0f, 1.0f, 11.0f, proj);
	g_assert_true (signbit (M (proj, 0, 3)) == 0);
	p[0] = 4.0f; p[1] = -2.0f; p[2] = -11.0f;
	lrg_math3d_transform_point (proj, p, out);
	g_assert_cmpfloat_with_epsilon (out[0], 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (out[1], -1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (out[2], 1.0f, EPS);

	/* Degenerate look-at: eye == target, zero up, looking straight down. */
	lrg_math3d_look_at (eye, eye, target, view);
	g_assert_true (isfinite (view[0]) && isfinite (view[14]));
	target[0] = 0.0f; target[1] = -10.0f; target[2] = 5.0f;
	lrg_math3d_look_at (eye, target, up, view);
	g_assert_true (isfinite (view[0]) && isfinite (view[10]));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/math3d/bitwise/compose-decompose", test_bitwise_compose_decompose);
	g_test_add_func ("/math3d/bitwise/multiply-invert-transform", test_bitwise_multiply_invert_transform);
	g_test_add_func ("/math3d/bitwise/projection", test_bitwise_projection);
#ifdef LRG_BUILD_EDITOR
	g_test_add_func ("/math3d/bitwise/editor-view", test_editor_view_matches);
#endif
	g_test_add_func ("/math3d/euler-quat-round-trip", test_euler_quat_round_trip);
	g_test_add_func ("/math3d/gimbal", test_gimbal);
	g_test_add_func ("/math3d/trs-quat-round-trip", test_trs_quat_round_trip);
	g_test_add_func ("/math3d/degenerate", test_degenerate);
	g_test_add_func ("/math3d/matrix-ops", test_matrix_ops);
	g_test_add_func ("/math3d/quat-ops", test_quat_ops);
	g_test_add_func ("/math3d/camera", test_camera);

	return g_test_run ();
}
