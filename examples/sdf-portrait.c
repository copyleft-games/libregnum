/* sdf-portrait.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A character bust as a CPU "fragment shader": signed distance
 * primitives (ellipsoids, capsules, round cones, a torus) joined with
 * smooth unions and carved with smooth subtractions, raymarched into
 * an LrgHdrCanvas with stratified supersampling, lit with a soft key
 * shadow, ambient occlusion, a cool rim light and a wrapped (subsurface
 * looking) diffuse, textured with band-limited fBm (skin, hair strands,
 * cloth), ACES tone-mapped and dithered to a PNG.  The issue that asked
 * for this (#7) needed about 700 lines of hand-written maths outside the
 * library for the same kind of render; here the library provides the
 * distance functions, the marcher, normals, shadows, occlusion, noise,
 * the float canvas and the tone map.  Headless: no window, no GL.
 *
 * Run:   ./build/release/examples/sdf-portrait
 *        ./build/release/examples/sdf-portrait --size 960x1200 --samples 3 --out shots
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <raylib.h>

static gchar    *opt_out = NULL;
static gchar    *opt_size = NULL;
static gint      opt_samples = 2;
static gboolean  opt_license = FALSE;

static GOptionEntry entries[] = {
	{ "out", 'o', 0, G_OPTION_ARG_FILENAME, &opt_out, "Folder for the PNG (default: current folder)", "DIR" },
	{ "size", 's', 0, G_OPTION_ARG_STRING, &opt_size, "Image size (default: 640x800)", "WxH" },
	{ "samples", 'n', 0, G_OPTION_ARG_INT, &opt_samples, "Samples per pixel per axis, 1..8 (default: 2)", "N" },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Print the license and exit", NULL },
	{ NULL }
};

/* ==========================================================================
 * Vectors and the camera (the only hand-written maths besides shading)
 * ========================================================================== */

static void
v3 (gfloat o[3], gfloat x, gfloat y, gfloat z)
{
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

static gfloat
dot3 (const gfloat a[3], const gfloat b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
norm3 (gfloat a[3])
{
	gfloat l = sqrtf (dot3 (a, a));

	if (l > 0.0f)
	{
		a[0] /= l;
		a[1] /= l;
		a[2] /= l;
	}
}

static gfloat
clampf (gfloat v, gfloat lo, gfloat hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static gfloat
mixf (gfloat a, gfloat b, gfloat t)
{
	return a + (b - a) * t;
}

static gfloat
smoothstepf (gfloat e0, gfloat e1, gfloat x)
{
	gfloat t = clampf ((x - e0) / (e1 - e0), 0.0f, 1.0f);

	return t * t * (3.0f - 2.0f * t);
}

typedef struct
{
	gfloat eye[3];
	gfloat fwd[3];
	gfloat right[3];
	gfloat up[3];
	gfloat focal;
	gfloat width;
	gfloat height;
	gfloat pixel_angle;
	gfloat footprint;   /* world size of a pixel at the current hit */
} Portrait;

/* ==========================================================================
 * The model
 * ========================================================================== */

typedef enum
{
	BUST_SKIN,
	BUST_LIPS,
	BUST_EYE,
	BUST_HAIR,
	BUST_SHIRT,
	BUST_COLLAR,
	BUST_WALL
} BustMaterial;

/* p minus (x, y, z), with x mirrored (|x|) when mirror is set */
static void
at (gfloat       q[3],
    const gfloat p[3],
    gboolean     mirror,
    gfloat       x,
    gfloat       y,
    gfloat       z)
{
	q[0] = (mirror ? fabsf (p[0]) : p[0]) - x;
	q[1] = p[1] - y;
	q[2] = p[2] - z;
}

static gfloat
model (const gfloat p[3],
       Portrait    *pt,
       BustMaterial    *mat)
{
	static const gfloat head_r[3] = { 0.33f, 0.41f, 0.36f };
	static const gfloat skull_r[3] = { 0.345f, 0.36f, 0.37f };
	static const gfloat jaw_r[3] = { 0.245f, 0.19f, 0.28f };
	static const gfloat ear_r[3] = { 0.045f, 0.095f, 0.035f };
	static const gfloat lid_r[3] = { 0.08f, 0.026f, 0.068f };
	static const gfloat upper_lip_r[3] = { 0.06f, 0.016f, 0.03f };
	static const gfloat lower_lip_r[3] = { 0.054f, 0.019f, 0.03f };
	static const gfloat hair_r[3] = { 0.37f, 0.30f, 0.40f };
	static const gfloat fringe_r[3] = { 0.30f, 0.085f, 0.16f };
	static const gfloat torso_r[3] = { 0.72f, 0.40f, 0.36f };
	gfloat q[3];
	gfloat a[3];
	gfloat b[3];
	gfloat skin;
	gfloat lips;
	gfloat eye;
	gfloat hair;
	gfloat shirt;
	gfloat collar;
	gfloat wall;
	gfloat d;

	/* head: skull, face, jaw and chin */
	at (q, p, FALSE, 0.0f, 0.48f, 0.0f);
	skin = lrg_sdf3_ellipsoid (q, head_r);
	at (q, p, FALSE, 0.0f, 0.58f, -0.07f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_ellipsoid (q, skull_r), 0.1f);
	at (q, p, FALSE, 0.0f, 0.27f, 0.05f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_ellipsoid (q, jaw_r), 0.12f);
	at (q, p, FALSE, 0.0f, 0.17f, 0.20f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_sphere (q, 0.085f), 0.08f);

	/* cheekbones and the brow ridge */
	at (q, p, TRUE, 0.16f, 0.39f, 0.19f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_sphere (q, 0.10f), 0.08f);
	v3 (a, -0.2f, 0.60f, 0.25f);
	v3 (b, 0.2f, 0.60f, 0.25f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_capsule (p, a, b, 0.045f), 0.07f);

	/* eye sockets carved out, lids added back */
	at (q, p, TRUE, 0.125f, 0.52f, 0.335f);
	skin = lrg_sdf_smooth_subtract (skin, lrg_sdf3_sphere (q, 0.068f), 0.03f);
	at (q, p, TRUE, 0.125f, 0.572f, 0.285f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_ellipsoid (q, lid_r), 0.02f);

	/* nose: bridge capsule, tip, wings */
	v3 (a, 0.0f, 0.53f, 0.33f);
	v3 (b, 0.0f, 0.415f, 0.39f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_capsule (p, a, b, 0.032f), 0.05f);
	at (q, p, FALSE, 0.0f, 0.395f, 0.392f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_sphere (q, 0.045f), 0.03f);
	at (q, p, TRUE, 0.042f, 0.375f, 0.365f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_sphere (q, 0.032f), 0.03f);

	/* ears: a flattened ellipsoid with a round cone lobe */
	at (q, p, TRUE, 0.325f, 0.46f, -0.02f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_ellipsoid (q, ear_r), 0.04f);

	/* lips, and the line between them */
	at (q, p, FALSE, 0.0f, 0.302f, 0.342f);
	lips = lrg_sdf3_ellipsoid (q, upper_lip_r);
	at (q, p, FALSE, 0.0f, 0.268f, 0.332f);
	lips = lrg_sdf_smooth_union (lips, lrg_sdf3_ellipsoid (q, lower_lip_r), 0.02f);
	v3 (a, -0.065f, 0.285f, 0.365f);
	v3 (b, 0.065f, 0.285f, 0.365f);
	lips = lrg_sdf_smooth_subtract (lips, lrg_sdf3_capsule (p, a, b, 0.006f), 0.008f);

	/* neck */
	v3 (a, 0.0f, -0.2f, -0.03f);
	v3 (b, 0.0f, 0.25f, -0.03f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_capsule (p, a, b, 0.125f), 0.09f);
	skin = lrg_sdf_smooth_union (skin, lips, 0.015f);

	/* eyeballs */
	at (q, p, TRUE, 0.125f, 0.52f, 0.265f);
	eye = lrg_sdf3_sphere (q, 0.062f);

	/*
	 * Hair: a cap and a swept fringe, displaced by stretched,
	 * band-limited fBm (strands run vertically).  The noise is only
	 * evaluated near the hair, and its footprint is the pixel size
	 * at the hit, so strands fade to a sheen instead of flickering.
	 */
	at (q, p, FALSE, 0.0f, 0.70f, -0.07f);
	hair = lrg_sdf3_ellipsoid (q, hair_r);
	at (q, p, FALSE, 0.07f, 0.82f, 0.20f);
	hair = lrg_sdf_smooth_union (hair, lrg_sdf3_ellipsoid (q, fringe_r), 0.08f);
	if (hair < 0.05f)
		hair -= 0.006f * lrg_noise_fbm3 (p[0] * 55.0f, p[1] * 9.0f, p[2] * 55.0f, 4, 2.0f, 0.5f,
		                                 pt->footprint * 55.0f, 3u);

	/* shirt and collar */
	at (q, p, FALSE, 0.0f, -0.43f, -0.03f);
	shirt = lrg_sdf3_ellipsoid (q, torso_r);
	at (q, p, FALSE, 0.0f, -0.04f, -0.03f);
	{
		gfloat t2[2] = { 0.15f, 0.035f };

		collar = lrg_sdf3_torus (q, t2);
	}

	/* the wall */
	wall = p[2] + 1.1f;

	d = lrg_sdf_smooth_union (skin, shirt, 0.04f);
	d = lrg_sdf_union (d, collar);
	d = lrg_sdf_smooth_union (d, hair, 0.025f);
	d = lrg_sdf_union (d, eye);
	d = lrg_sdf_union (d, wall);

	if (mat != NULL)
	{
		gfloat best = skin;

		*mat = lips < 0.004f ? BUST_LIPS : BUST_SKIN;
		if (shirt < best) { best = shirt; *mat = BUST_SHIRT; }
		if (collar < best) { best = collar; *mat = BUST_COLLAR; }
		if (hair < best) { best = hair; *mat = BUST_HAIR; }
		if (eye < best) { best = eye; *mat = BUST_EYE; }
		if (wall < best) { *mat = BUST_WALL; }
	}
	return d;
}

static gfloat
model_sdf (const gfloat p[3],
           gpointer     user_data)
{
	/* the field is an estimate (ellipsoids, blends, displacement) */
	return model (p, user_data, NULL) * 0.75f;
}

/* ==========================================================================
 * Shading
 * ========================================================================== */

static void
albedo_at (const gfloat p[3],
           BustMaterial     mat,
           Portrait    *pt,
           gfloat       out[3],
           gfloat      *gloss)
{
	gfloat fp = pt->footprint;
	gfloat t;

	switch (mat)
	{
	case BUST_SKIN:
	case BUST_LIPS:
	{
		/* mottled skin, warmer on cheeks and nose */
		gfloat m = lrg_noise_fbm3 (p[0] * 40.0f, p[1] * 40.0f, p[2] * 40.0f, 5, 2.0f, 0.5f, fp * 40.0f, 9u);
		gfloat cheek = smoothstepf (0.16f, 0.0f, fabsf (fabsf (p[0]) - 0.16f) + fabsf (p[1] - 0.38f));
		gfloat nose = smoothstepf (0.08f, 0.0f, fabsf (p[0]) + fabsf (p[1] - 0.40f) * 0.7f);

		v3 (out, 0.70f, 0.45f, 0.34f);
		t = MAX (cheek, nose) * 0.6f;
		out[0] = mixf (out[0], 0.74f, t);
		out[1] = mixf (out[1], 0.36f, t);
		out[2] = mixf (out[2], 0.30f, t);
		if (mat == BUST_LIPS)
			v3 (out, 0.55f, 0.24f, 0.22f);
		out[0] *= 1.0f + 0.08f * m;
		out[1] *= 1.0f + 0.06f * m;
		out[2] *= 1.0f + 0.06f * m;
		*gloss = mat == BUST_LIPS ? 0.5f : 0.3f;
		break;
	}
	case BUST_EYE:
	{
		/* sclera, a textured iris and the pupil, from the gaze angle */
		gfloat q[3];
		gfloat c;

		q[0] = fabsf (p[0]) - 0.125f;
		q[1] = p[1] - 0.52f;
		q[2] = p[2] - 0.265f;
		norm3 (q);
		c = q[2];
		if (c > 0.955f)
			v3 (out, 0.015f, 0.012f, 0.012f);
		else if (c > 0.86f)
		{
			gfloat r = lrg_noise_fbm2 (atan2f (q[1], q[0]) * 6.0f, c * 30.0f, 4, 2.0f, 0.5f, fp * 300.0f, 4u);

			v3 (out, 0.16f + 0.08f * r, 0.30f + 0.10f * r, 0.36f + 0.1f * r);
		}
		else
			v3 (out, 0.82f, 0.80f, 0.77f);
		*gloss = 1.0f;
		break;
	}
	case BUST_HAIR:
	{
		gfloat s = lrg_noise_fbm3 (p[0] * 55.0f, p[1] * 9.0f, p[2] * 55.0f, 4, 2.0f, 0.5f, fp * 55.0f, 3u);

		v3 (out, 0.11f, 0.065f, 0.04f);
		t = 0.75f + 0.45f * s;
		out[0] *= t;
		out[1] *= t;
		out[2] *= t;
		*gloss = 0.55f;
		break;
	}
	case BUST_SHIRT:
	{
		/* cloth: a fine band-limited weave over a slow fold shading */
		gfloat w = lrg_noise_fbm3 (p[0] * 160.0f, p[1] * 160.0f, p[2] * 160.0f, 3, 2.0f, 0.5f, fp * 160.0f, 5u);
		gfloat f = lrg_noise_fbm3 (p[0] * 4.0f, p[1] * 4.0f, p[2] * 4.0f, 3, 2.0f, 0.5f, fp * 4.0f, 6u);

		v3 (out, 0.03f, 0.055f, 0.11f);
		t = 1.0f + 0.12f * w + 0.18f * f;
		out[0] *= t;
		out[1] *= t;
		out[2] *= t;
		*gloss = 0.05f;
		break;
	}
	case BUST_COLLAR:
		v3 (out, 0.05f, 0.07f, 0.13f);
		*gloss = 0.1f;
		break;
	case BUST_WALL:
	default:
		v3 (out, 0.16f, 0.15f, 0.16f);
		*gloss = 0.0f;
		break;
	}
}

static void
shade (gfloat   x,
       gfloat   y,
       gfloat  *out,
       gpointer user_data)
{
	Portrait *pt = user_data;
	gfloat    rd[3];
	gfloat    p[3];
	gfloat    n[3];
	gfloat    key[3];
	gfloat    rim[3];
	gfloat    half[3];
	gfloat    lifted[3];
	gfloat    albedo[3];
	gfloat    nx;
	gfloat    ny;
	gfloat    t;
	gfloat    ndl;
	gfloat    shadow;
	gfloat    ao;
	gfloat    gloss;
	gfloat    fres;
	gfloat    spec;
	gfloat    wraps[3];
	BustMaterial  mat;
	gint      i;

	/* --- the primary ray --- */
	nx = (2.0f * x - pt->width) / pt->height;
	ny = (pt->height - 2.0f * y) / pt->height;
	for (i = 0; i < 3; i++)
		rd[i] = pt->fwd[i] * pt->focal + pt->right[i] * nx + pt->up[i] * ny;
	norm3 (rd);
	out[3] = 1.0f;
	pt->footprint = 0.0f;
	if (!lrg_sdf3_raymarch (model_sdf, pt, pt->eye, rd, 10.0f, 260, &t))
	{
		v3 (out, 0.02f, 0.02f, 0.025f);
		return;
	}
	for (i = 0; i < 3; i++)
		p[i] = pt->eye[i] + rd[i] * t;
	pt->footprint = t * pt->pixel_angle;
	lrg_sdf3_normal (model_sdf, pt, p, 1e-3f, n);
	model (p, pt, &mat);
	albedo_at (p, mat, pt, albedo, &gloss);

	/* --- key: warm, upper left, soft shadow; wrapped diffuse per channel --- */
	v3 (key, -0.5f, 0.32f, 0.85f);
	norm3 (key);
	ndl = dot3 (n, key);
	wraps[0] = clampf ((ndl + 0.45f) / 1.45f, 0.0f, 1.0f);
	wraps[1] = clampf ((ndl + 0.25f) / 1.25f, 0.0f, 1.0f);
	wraps[2] = clampf ((ndl + 0.20f) / 1.20f, 0.0f, 1.0f);
	if (mat != BUST_SKIN && mat != BUST_LIPS)
		wraps[0] = wraps[1] = wraps[2] = clampf (ndl, 0.0f, 1.0f);
	for (i = 0; i < 3; i++)
		lifted[i] = p[i] + n[i] * 0.004f;
	shadow = wraps[0] > 0.0f ? lrg_sdf3_soft_shadow (model_sdf, pt, lifted, key, 0.01f, 4.0f, 4.0f) : 0.0f;
	ao = lrg_sdf3_ambient_occlusion (model_sdf, pt, p, n);

	/* --- rim: cool, behind right --- */
	v3 (rim, 0.75f, 0.35f, -0.55f);
	norm3 (rim);
	fres = powf (1.0f - clampf (-dot3 (n, rd), 0.0f, 1.0f), 3.0f);

	/* --- highlight --- */
	for (i = 0; i < 3; i++)
		half[i] = key[i] - rd[i];
	norm3 (half);
	spec = gloss * powf (clampf (dot3 (n, half), 0.0f, 1.0f), 12.0f + 140.0f * gloss) * shadow
	     * (0.04f + 0.96f * fres + 0.3f * gloss);

	for (i = 0; i < 3; i++)
	{
		static const gfloat key_c[3] = { 2.0f, 1.72f, 1.42f };
		static const gfloat sky_c[3] = { 0.20f, 0.24f, 0.34f };
		static const gfloat rim_c[3] = { 1.2f, 1.7f, 2.8f };

		out[i] = albedo[i] * (key_c[i] * wraps[i] * shadow + sky_c[i] * (0.6f + 0.4f * n[1]) * ao)
		       + (mat == BUST_WALL ? 0.0f : rim_c[i] * fres * clampf (dot3 (n, rim) + 0.35f, 0.0f, 1.0f) * ao)
		       + key_c[i] * spec * 1.6f;
	}

	/* a pool of light on the wall, falling off from the head */
	if (mat == BUST_WALL)
	{
		gfloat dx = p[0] + 0.35f;
		gfloat dy = p[1] - 0.5f;
		gfloat pool = 0.35f + 1.3f * expf (-(dx * dx + dy * dy) * 1.2f);

		for (i = 0; i < 3; i++)
			out[i] *= pool;
	}
}

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgHdrCanvas)   canvas = NULL;
	g_autoptr(GrlImage)       image = NULL;
	g_autofree gchar         *path = NULL;
	Portrait                  pt;
	gfloat                    target[3] = { 0.0f, 0.27f, 0.0f };
	gfloat                    world_up[3] = { 0.0f, 1.0f, 0.0f };
	guint                     width = 640;
	guint                     height = 800;
	gchar                     tail;
	gint64                    t0;

	ctx = g_option_context_new ("- render an SDF character bust to a PNG");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Writes sdf-portrait.png.  CPU only; no display is needed.\n"
		"\n"
		"Examples:\n"
		"  sdf-portrait                                     # 640x800, 2x2 samples\n"
		"  sdf-portrait --samples 1 --size 320x400          # quick look\n"
		"  sdf-portrait --samples 3 --size 960x1200 --out shots\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("sdf-portrait - part of libregnum\n"
		         "Copyright 2026 Zach Podbielniak\n"
		         "License: AGPL-3.0-or-later <https://www.gnu.org/licenses/agpl-3.0.html>\n");
		return 0;
	}
	if (opt_size != NULL &&
	    (sscanf (opt_size, "%ux%u%c", &width, &height, &tail) != 2 || width < 16 || height < 16 ||
	     width > 8192 || height > 8192))
	{
		g_printerr ("--size: expected WxH (16x16 .. 8192x8192), got \"%s\"\n", opt_size);
		return 2;
	}
	if (opt_samples < 1 || opt_samples > 8)
	{
		g_printerr ("--samples: expected 1 .. 8, got %d\n", opt_samples);
		return 2;
	}
	SetTraceLogLevel (LOG_WARNING);

	/* --- a look-at camera --- */
	v3 (pt.eye, 0.42f, 0.42f, 2.75f);
	v3 (pt.fwd, target[0] - pt.eye[0], target[1] - pt.eye[1], target[2] - pt.eye[2]);
	norm3 (pt.fwd);
	v3 (pt.right, pt.fwd[1] * world_up[2] - pt.fwd[2] * world_up[1],
	    pt.fwd[2] * world_up[0] - pt.fwd[0] * world_up[2],
	    pt.fwd[0] * world_up[1] - pt.fwd[1] * world_up[0]);
	norm3 (pt.right);
	v3 (pt.up, pt.right[1] * pt.fwd[2] - pt.right[2] * pt.fwd[1],
	    pt.right[2] * pt.fwd[0] - pt.right[0] * pt.fwd[2],
	    pt.right[0] * pt.fwd[1] - pt.right[1] * pt.fwd[0]);
	pt.focal = 1.0f / tanf (34.0f * (gfloat) G_PI / 360.0f);
	pt.width = (gfloat) width;
	pt.height = (gfloat) height;
	pt.pixel_angle = 2.0f / (pt.focal * pt.height);
	pt.footprint = 0.0f;

	/* --- shade, tone-map, save --- */
	t0 = g_get_monotonic_time ();
	canvas = lrg_hdr_canvas_new (width, height);
	lrg_hdr_canvas_shade (canvas, shade, &pt, (guint) opt_samples);
	image = lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_ACES, 0.0f, TRUE);

	if (opt_out != NULL && g_mkdir_with_parents (opt_out, 0755) != 0)
	{
		g_printerr ("--out: cannot create %s\n", opt_out);
		return 1;
	}
	path = g_build_filename (opt_out != NULL ? opt_out : ".", "sdf-portrait.png", NULL);
	if (image == NULL || !grl_image_export (image, path))
	{
		g_printerr ("could not write %s\n", path);
		return 1;
	}
	g_print ("%s (%ux%u, %dx%d samples, %.1f s)\n", path, width, height, opt_samples, opt_samples,
	         (gdouble) (g_get_monotonic_time () - t0) / G_USEC_PER_SEC);
	return 0;
}
