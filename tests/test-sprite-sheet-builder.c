/* test-sprite-sheet-builder.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgSpriteSheetBuilder, headless: layout of frames of
 * different sizes, animations, key-colour and alpha-threshold
 * background removal, the shared palette and its determinism, and
 * saving and reloading the PNG and YAML.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <string.h>

static guint8 *
solid (gint   w,
       gint   h,
       guint8 r,
       guint8 g,
       guint8 b,
       guint8 a)
{
	guint8 *px = g_malloc ((gsize) w * h * 4);
	gint i;

	for (i = 0; i < w * h; i++)
	{
		px[i * 4] = r;
		px[i * 4 + 1] = g;
		px[i * 4 + 2] = b;
		px[i * 4 + 3] = a;
	}
	return px;
}

static void
assert_rgba (GrlImage *image,
             gint      x,
             gint      y,
             guint8    r,
             guint8    g,
             guint8    b,
             guint8    a)
{
	g_autoptr(GrlColor) c = grl_image_get_pixel (image, x, y);

	g_assert_cmpuint (c->r, ==, r);
	g_assert_cmpuint (c->g, ==, g);
	g_assert_cmpuint (c->b, ==, b);
	g_assert_cmpuint (c->a, ==, a);
}

static void
test_layout_and_animations (void)
{
	g_autoptr(LrgSpriteSheetBuilder) builder = lrg_sprite_sheet_builder_new ();
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree guint8 *red = solid (8, 8, 255, 0, 0, 255);
	g_autofree guint8 *green = solid (6, 4, 0, 255, 0, 255);
	g_autofree guint8 *blue = solid (8, 8, 0, 0, 255, 255);
	const guint bounce[] = { 0, 1, 2, 1 };
	GrlImage *image;
	LrgAtlasRegion *region;
	gint cw;
	gint ch;

	g_assert_cmpint (lrg_sprite_sheet_builder_add_frame_pixels (builder, red, 8, 8, 32), ==, 0);
	g_assert_cmpint (lrg_sprite_sheet_builder_add_frame_pixels (builder, green, 6, 4, 24), ==, 1);
	g_assert_cmpint (lrg_sprite_sheet_builder_add_frame_pixels (builder, blue, 8, 8, 32), ==, 2);
	g_assert_cmpuint (lrg_sprite_sheet_builder_get_n_frames (builder), ==, 3);

	g_assert_true (lrg_sprite_sheet_builder_add_animation_range (builder, "walk", 0, 3, 0.1f, TRUE));
	g_assert_true (lrg_sprite_sheet_builder_add_animation (builder, "bounce", bounce, 4, 0.2f, FALSE));
	g_assert_false (lrg_sprite_sheet_builder_add_animation_range (builder, "walk", 0, 1, 0.1f, TRUE));
	g_assert_false (lrg_sprite_sheet_builder_add_animation_range (builder, "far", 2, 2, 0.1f, TRUE));

	sheet = lrg_sprite_sheet_builder_build (builder, "hero", &error);
	g_assert_no_error (error);
	g_assert_nonnull (sheet);
	lrg_sprite_sheet_builder_get_cell_size (builder, &cw, &ch);
	g_assert_cmpint (cw, ==, 8);
	g_assert_cmpint (ch, ==, 8);

	/* 3 frames: a 2x2 grid of 8x8 cells with 1 px padding = 19x19 */
	image = lrg_sprite_sheet_builder_get_image (builder);
	g_assert_cmpint (grl_image_get_width (image), ==, 19);
	g_assert_cmpint (grl_image_get_height (image), ==, 19);
	g_assert_cmpint (lrg_sprite_sheet_get_texture_width (sheet), ==, 19);
	assert_rgba (image, 1, 1, 255, 0, 0, 255);          /* frame 0 */
	assert_rgba (image, 0, 0, 0, 0, 0, 0);              /* padding */
	assert_rgba (image, 1, 10, 0, 0, 255, 255);         /* frame 2, second row */
	/* frame 1 (6x4) centred on the bottom of its cell at (10, 1) */
	assert_rgba (image, 11, 5, 0, 255, 0, 255);
	assert_rgba (image, 16, 8, 0, 255, 0, 255);
	assert_rgba (image, 10, 8, 0, 0, 0, 0);
	assert_rgba (image, 11, 4, 0, 0, 0, 0);

	g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (sheet), ==, 3);
	region = lrg_sprite_sheet_get_frame (sheet, 1);
	g_assert_cmpint (lrg_atlas_region_get_x (region), ==, 10);
	g_assert_cmpint (lrg_atlas_region_get_y (region), ==, 1);
	g_assert_cmpint (lrg_atlas_region_get_width (region), ==, 8);
	g_assert_nonnull (lrg_sprite_sheet_get_frame_by_name (sheet, "frame-2"));
	g_assert_true (lrg_sprite_sheet_has_animation (sheet, "walk"));
	g_assert_cmpuint (lrg_sprite_sheet_get_animation_frame_count (sheet, "bounce"), ==, 4);

	/* one row */
	g_clear_object (&sheet);
	lrg_sprite_sheet_builder_set_layout (builder, 3, 0);
	sheet = lrg_sprite_sheet_builder_build (builder, "hero", &error);
	g_assert_cmpint (grl_image_get_width (lrg_sprite_sheet_builder_get_image (builder)), ==, 24);
	g_assert_cmpint (grl_image_get_height (lrg_sprite_sheet_builder_get_image (builder)), ==, 8);
}

static void
test_background_removal (void)
{
	g_autoptr(LrgSpriteSheetBuilder) builder = lrg_sprite_sheet_builder_new ();
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	g_autofree guint8 *px = solid (4, 4, 250, 2, 248, 255);   /* near magenta */
	GrlColor magenta = { 255, 0, 255, 255 };
	GrlImage *image;

	/* a white pixel, a half-transparent pixel, a faint pixel */
	memcpy (px + (1 * 4 + 1) * 4, "\xff\xff\xff\xff", 4);
	memcpy (px + (2 * 4 + 2) * 4, "\x10\x20\x30\xc8", 4);     /* alpha 200 */
	memcpy (px + (3 * 4 + 3) * 4, "\x10\x20\x30\x40", 4);     /* alpha 64 */
	lrg_sprite_sheet_builder_add_frame_pixels (builder, px, 4, 4, 16);
	lrg_sprite_sheet_builder_set_layout (builder, 1, 0);

	/* a tolerance of 4 misses the key by 5 */
	lrg_sprite_sheet_builder_set_key_color (builder, &magenta, 4);
	sheet = lrg_sprite_sheet_builder_build (builder, "k", NULL);
	assert_rgba (lrg_sprite_sheet_builder_get_image (builder), 0, 0, 250, 2, 248, 255);
	g_clear_object (&sheet);

	lrg_sprite_sheet_builder_set_key_color (builder, &magenta, 8);
	lrg_sprite_sheet_builder_set_alpha_threshold (builder, 128, TRUE);
	sheet = lrg_sprite_sheet_builder_build (builder, "k", NULL);
	image = lrg_sprite_sheet_builder_get_image (builder);
	assert_rgba (image, 0, 0, 0, 0, 0, 0);
	assert_rgba (image, 1, 1, 255, 255, 255, 255);
	assert_rgba (image, 2, 2, 0x10, 0x20, 0x30, 255);
	assert_rgba (image, 3, 3, 0, 0, 0, 0);
	g_clear_object (&sheet);

	/* soft alpha kept when not binary; the key turned off */
	lrg_sprite_sheet_builder_set_key_color (builder, NULL, 0);
	lrg_sprite_sheet_builder_set_alpha_threshold (builder, 128, FALSE);
	sheet = lrg_sprite_sheet_builder_build (builder, "k", NULL);
	image = lrg_sprite_sheet_builder_get_image (builder);
	assert_rgba (image, 0, 0, 250, 2, 248, 255);
	assert_rgba (image, 2, 2, 0x10, 0x20, 0x30, 200);
}

static guint
count_colors (GrlImage *image,
              gboolean *saw_transparent)
{
	g_autoptr(GHashTable) seen = g_hash_table_new (g_direct_hash, g_direct_equal);
	const guint8 *px = grl_image_get_pixels (image, NULL);
	gint n = grl_image_get_width (image) * grl_image_get_height (image);
	gint i;

	*saw_transparent = FALSE;
	for (i = 0; i < n; i++)
	{
		if (px[i * 4 + 3] == 0)
		{
			*saw_transparent = TRUE;
			continue;
		}
		g_hash_table_add (seen, GUINT_TO_POINTER (1u + (px[i * 4] << 16 | px[i * 4 + 1] << 8 |
		                                                px[i * 4 + 2])));
	}
	return g_hash_table_size (seen);
}

static void
test_palette (void)
{
	g_autoptr(LrgSpriteSheetBuilder) builder = lrg_sprite_sheet_builder_new ();
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	g_autofree guint8 *first = NULL;
	guint8 px[16 * 8 * 4];
	gboolean transparent;
	gint i;

	/* two frames of gradients: 128 distinct colours, one transparent pixel */
	for (i = 0; i < 16 * 8; i++)
	{
		px[i * 4] = (guint8) (i * 2);
		px[i * 4 + 1] = (guint8) (255 - i);
		px[i * 4 + 2] = (guint8) ((i % 16) * 16);
		px[i * 4 + 3] = 255;
	}
	px[3] = 0;
	lrg_sprite_sheet_builder_add_frame_pixels (builder, px, 16, 8, 64);
	lrg_sprite_sheet_builder_add_frame_pixels (builder, px, 8, 16, 32);

	sheet = lrg_sprite_sheet_builder_build (builder, "p", NULL);
	g_assert_cmpuint (count_colors (lrg_sprite_sheet_builder_get_image (builder), &transparent), >, 64);
	g_clear_object (&sheet);

	lrg_sprite_sheet_builder_set_palette_size (builder, 6);
	sheet = lrg_sprite_sheet_builder_build (builder, "p", NULL);
	g_assert_cmpuint (count_colors (lrg_sprite_sheet_builder_get_image (builder), &transparent), <=, 6);
	g_assert_cmpuint (count_colors (lrg_sprite_sheet_builder_get_image (builder), &transparent), >=, 2);
	g_assert_true (transparent);
	first = g_memdup2 (grl_image_get_pixels (lrg_sprite_sheet_builder_get_image (builder), NULL),
	                   (gsize) grl_image_get_width (lrg_sprite_sheet_builder_get_image (builder)) *
	                   grl_image_get_height (lrg_sprite_sheet_builder_get_image (builder)) * 4);
	g_clear_object (&sheet);

	/* the same input gives the same pixels */
	sheet = lrg_sprite_sheet_builder_build (builder, "p", NULL);
	g_assert_cmpmem (first,
	                 (gsize) grl_image_get_width (lrg_sprite_sheet_builder_get_image (builder)) *
	                 grl_image_get_height (lrg_sprite_sheet_builder_get_image (builder)) * 4,
	                 grl_image_get_pixels (lrg_sprite_sheet_builder_get_image (builder), NULL),
	                 (gsize) grl_image_get_width (lrg_sprite_sheet_builder_get_image (builder)) *
	                 grl_image_get_height (lrg_sprite_sheet_builder_get_image (builder)) * 4);

	/* 2 is the smallest palette */
	g_clear_object (&sheet);
	lrg_sprite_sheet_builder_set_palette_size (builder, 1);
	sheet = lrg_sprite_sheet_builder_build (builder, "p", NULL);
	g_assert_cmpuint (count_colors (lrg_sprite_sheet_builder_get_image (builder), &transparent), ==, 2);
}

static void
test_save_and_reload (void)
{
	g_autoptr(LrgSpriteSheetBuilder) builder = lrg_sprite_sheet_builder_new ();
	g_autoptr(LrgSpriteSheet) loaded = NULL;
	g_autoptr(GrlImage) png = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = g_dir_make_tmp ("lrg-sheet-XXXXXX", &error);
	g_autofree gchar *png_path = g_build_filename (dir, "hero.png", NULL);
	g_autofree gchar *yaml_path = g_build_filename (dir, "hero.yaml", NULL);
	g_autofree gchar *frame_path = g_build_filename (dir, "frame.png", NULL);
	g_autofree guint8 *a = solid (5, 7, 10, 20, 30, 255);

	g_assert_no_error (error);

	/* nothing to build */
	g_assert_false (lrg_sprite_sheet_builder_save (builder, "hero", png_path, yaml_path, &error));
	g_assert_error (error, LRG_SPRITE_SHEET_BUILDER_ERROR, LRG_SPRITE_SHEET_BUILDER_ERROR_EMPTY);
	g_clear_error (&error);
	g_assert_null (lrg_sprite_sheet_builder_get_image (builder));

	/* frames from files */
	g_assert_true (lrg_screenshot_write_png (frame_path, a, 5, 7, 20, NULL));
	g_assert_cmpint (lrg_sprite_sheet_builder_add_frame_file (builder, frame_path, &error), ==, 0);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_sprite_sheet_builder_add_frame_file (builder, "/nonexistent.png", &error),
	                 ==, -1);
	g_assert_error (error, LRG_SPRITE_SHEET_BUILDER_ERROR, LRG_SPRITE_SHEET_BUILDER_ERROR_IO);
	g_clear_error (&error);
	png = grl_image_new_from_file (frame_path);
	g_assert_cmpint (lrg_sprite_sheet_builder_add_frame_image (builder, png), ==, 1);
	g_assert_true (lrg_sprite_sheet_builder_add_animation_range (builder, "idle", 0, 2, 0.5f, TRUE));

	g_assert_true (lrg_sprite_sheet_builder_save (builder, "hero", png_path, yaml_path, &error));
	g_assert_no_error (error);

	loaded = lrg_sprite_sheet_new_from_file (yaml_path, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_sprite_sheet_get_name (loaded), ==, "hero");
	g_assert_cmpstr (lrg_sprite_sheet_get_texture_path (loaded), ==, "hero.png");
	g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (loaded), ==, 2);
	g_assert_cmpuint (lrg_sprite_sheet_get_animation_frame_count (loaded, "idle"), ==, 2);

	g_clear_object (&png);
	png = grl_image_new_from_file (png_path);
	g_assert_cmpint (grl_image_get_width (png), ==,
	                 grl_image_get_width (lrg_sprite_sheet_builder_get_image (builder)));
	assert_rgba (png, 1, 1, 10, 20, 30, 255);

	g_unlink (png_path);
	g_unlink (yaml_path);
	g_unlink (frame_path);
	g_rmdir (dir);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/sprite-sheet-builder/layout-and-animations", test_layout_and_animations);
	g_test_add_func ("/sprite-sheet-builder/background-removal", test_background_removal);
	g_test_add_func ("/sprite-sheet-builder/palette", test_palette);
	g_test_add_func ("/sprite-sheet-builder/save-and-reload", test_save_and_reload);

	return g_test_run ();
}
