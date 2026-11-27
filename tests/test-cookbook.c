/* test-cookbook.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Renders every image-quality cookbook scene (examples/cookbook/) at a
 * small size and checks what "polished" means in numbers:
 *
 *   - the scene renders, at the requested size, as RGBA8;
 *   - it is opaque (no transparent or partly transparent pixel);
 *   - it has at least a per-scene number of distinct colours: smooth
 *     light, anti-aliased edges and dithered gradients produce
 *     thousands, flat fills and aliased edges produce a few dozen;
 *   - scenes that place elements return an LrgFrameLayout that
 *     validates (no overlaps, nothing outside the frame or safe area);
 *   - rendering is deterministic.
 *
 * CPU only: no window, no GL context, no display.
 */

#include <glib.h>
#include <libregnum.h>

#include "lrg-cookbook-scenes.h"

#define TEST_WIDTH  (256)
#define TEST_HEIGHT (160)

/*
 * The colour floors: about half of what each scene produces at
 * 256 x 160, so a change that flattens the shading (a lost gradient,
 * aliased edges, no dither) fails while small tweaks do not.  The
 * test prints the measured counts with --verbose.
 */
typedef struct
{
	const gchar *name;
	guint        min_colors;
} SceneFloor;

static const SceneFloor scene_floors[] = {
	{ "aa-shapes",              3000 },
	{ "glow-title",             10000 },
	{ "sdf-character-bust",     4000 },
	{ "product-shot",           1300 },
	{ "band-limited-texture",   3500 },
	{ "sky-gradient-vignette",  12000 },
	{ "speech-bubble-layout",   7000 },
	{ "hdr-sunset",             8000 },
	{ "supersampled-line-art",  6500 },
	{ "poster-typography",      3800 },
	{ "noise-terrain",          4500 },
	{ "particles-glow",         7000 },
	{ "pitfall-moire",          4500 },
	{ "pitfall-thin-detail",    1200 }
};

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static guint
scene_floor (const gchar *name)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (scene_floors); i++)
		if (g_str_equal (scene_floors[i].name, name))
			return scene_floors[i].min_colors;
	g_assert_not_reached ();
	return 0;
}

/*
 * count_colors:
 * Counts the distinct RGB colours of an RGBA8 image and the pixels
 * whose alpha is not 255.
 */
static guint
count_colors (GrlImage *image,
              guint    *out_not_opaque)
{
	g_autoptr(GHashTable) seen = NULL;
	const guint8         *px;
	gsize                 size;
	gsize                 i;

	seen = g_hash_table_new (g_direct_hash, g_direct_equal);
	px = grl_image_get_pixels (image, &size);
	g_assert_nonnull (px);
	*out_not_opaque = 0;
	for (i = 0; i + 3 < size; i += 4)
	{
		guint rgb = ((guint) px[i] << 16) | ((guint) px[i + 1] << 8) | px[i + 2];

		/* +1: key 0 (black) must not look like NULL */
		g_hash_table_add (seen, GUINT_TO_POINTER (rgb + 1));
		if (px[i + 3] != 255)
			(*out_not_opaque)++;
	}
	return g_hash_table_size (seen);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Renders one scene and checks size, opacity, colours and layout. */
static void
test_scene (gconstpointer data)
{
	const LrgCookbookScene   *scene = data;
	g_autoptr(GrlImage)       image = NULL;
	g_autoptr(LrgFrameLayout) layout = NULL;
	g_autoptr(GError)         error = NULL;
	guint                     colors;
	guint                     not_opaque;
	gint64                    t0;

	t0 = g_get_monotonic_time ();
	image = scene->func (TEST_WIDTH, TEST_HEIGHT, &layout);
	g_assert_nonnull (image);
	g_assert_cmpint (grl_image_get_width (image), ==, TEST_WIDTH);
	g_assert_cmpint (grl_image_get_height (image), ==, TEST_HEIGHT);
	g_assert_cmpint (grl_image_get_format (image), ==, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

	/* smooth shading and no holes */
	colors = count_colors (image, &not_opaque);
	g_test_message ("%s: %u distinct colours, %.2f s", scene->name, colors,
	                (gdouble) (g_get_monotonic_time () - t0) / G_USEC_PER_SEC);
	g_assert_cmpuint (not_opaque, ==, 0);
	g_assert_cmpuint (colors, >=, scene_floor (scene->name));

	/* the layout, when the scene has one, is clean */
	if (scene->has_layout)
	{
		g_assert_nonnull (layout);
		g_assert_cmpfloat (lrg_frame_layout_get_width (layout), ==, (gfloat) TEST_WIDTH);
		g_assert_cmpfloat (lrg_frame_layout_get_height (layout), ==, (gfloat) TEST_HEIGHT);
		lrg_frame_layout_validate (layout, &error);
		g_assert_no_error (error);
	}
	else
		g_assert_null (layout);
}

/* The table: twelve scenes, unique names, all findable. */
static void
test_table (void)
{
	const LrgCookbookScene *scenes;
	const LrgCookbookScene *pitfalls;
	guint                   n;
	guint                   n_pitfalls;
	guint                   i;
	guint                   j;

	scenes = lrg_cookbook_get_scenes (&n);
	pitfalls = lrg_cookbook_get_pitfalls (&n_pitfalls);
	g_assert_cmpuint (n, ==, 12);
	g_assert_cmpuint (n_pitfalls, ==, 2);
	for (i = 0; i < n; i++)
	{
		g_assert_nonnull (scenes[i].func);
		g_assert_true (lrg_cookbook_find_scene (scenes[i].name) == &scenes[i]);
		for (j = i + 1; j < n; j++)
			g_assert_cmpstr (scenes[i].name, !=, scenes[j].name);
	}
	for (i = 0; i < n_pitfalls; i++)
		g_assert_true (lrg_cookbook_find_scene (pitfalls[i].name) == &pitfalls[i]);
	g_assert_null (lrg_cookbook_find_scene ("no-such-scene"));
}

/* The same size renders the same pixels (scenes are seeded). */
static void
test_deterministic (void)
{
	const LrgCookbookScene *scene;
	g_autoptr(GrlImage)     a = NULL;
	g_autoptr(GrlImage)     b = NULL;
	gsize                   size_a;
	gsize                   size_b;
	const guint8           *pa;
	const guint8           *pb;

	scene = lrg_cookbook_find_scene ("particles-glow");
	a = scene->func (96, 64, NULL);
	b = scene->func (96, 64, NULL);
	pa = grl_image_get_pixels (a, &size_a);
	pb = grl_image_get_pixels (b, &size_b);
	g_assert_cmpuint (size_a, ==, size_b);
	g_assert_cmpmem (pa, size_a, pb, size_b);
}

/* Sizes below the minimum return NULL instead of garbage. */
static void
test_too_small (void)
{
	const LrgCookbookScene *scenes;
	guint                   n;
	guint                   i;

	scenes = lrg_cookbook_get_scenes (&n);
	for (i = 0; i < n; i++)
	{
		g_autoptr(LrgFrameLayout) layout = NULL;
		g_autoptr(GrlImage)       image = scenes[i].func (8, 8, &layout);

		g_assert_null (image);
		g_assert_null (layout);
	}
}

/* Text falls back to the bitmap font when no TrueType font is found. */
static void
test_no_font (void)
{
	const LrgCookbookScene *scene;
	g_autoptr(GrlImage)     image = NULL;
	guint                   not_opaque;

	g_setenv ("LRG_COOKBOOK_FONT", "/no/such/font.ttf", TRUE);
	g_assert_nonnull (lrg_cookbook_find_scene ("poster-typography"));
	scene = lrg_cookbook_find_scene ("poster-typography");
	image = scene->func (128, 80, NULL);
	g_unsetenv ("LRG_COOKBOOK_FONT");
	g_assert_nonnull (image);
	count_colors (image, &not_opaque);
	g_assert_cmpuint (not_opaque, ==, 0);
}

int
main (int   argc,
      char *argv[])
{
	const LrgCookbookScene *scenes;
	guint                   n;
	guint                   i;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/cookbook/table", test_table);
	g_test_add_func ("/cookbook/deterministic", test_deterministic);
	g_test_add_func ("/cookbook/too-small", test_too_small);
	g_test_add_func ("/cookbook/no-font", test_no_font);

	/* one case per scene and per pitfall render */
	scenes = lrg_cookbook_get_scenes (&n);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *path = g_strdup_printf ("/cookbook/scene/%s", scenes[i].name);

		g_test_add_data_func (path, &scenes[i], test_scene);
	}
	scenes = lrg_cookbook_get_pitfalls (&n);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *path = g_strdup_printf ("/cookbook/pitfall/%s", scenes[i].name);

		g_test_add_data_func (path, &scenes[i], test_scene);
	}
	return g_test_run ();
}
