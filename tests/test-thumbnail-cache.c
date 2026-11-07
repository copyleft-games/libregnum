/* test-thumbnail-cache.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for LrgThumbnailCache: cache keys (path + mtime + size),
 * synchronous generation for textures and level/prefab schematics,
 * staleness after edits, invalidation, the asynchronous worker path and
 * the main-thread GPU queue with a fake renderer. Since 0.8.1 also audio
 * waveforms (decoded on the CPU; skipped if decoding is unavailable),
 * texture import settings from ".meta" sidecars (max-size and the
 * settings hash in the cache key) and the font renderer hook.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

typedef struct
{
	gchar             *dir;     /* assets */
	gchar             *cache;   /* cache dir */
	LrgThumbnailCache *thumbs;
} Fixture;

static void
remove_tree (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *p = g_build_filename (dir, name, NULL);

		if (g_file_test (p, G_FILE_TEST_IS_DIR))
			remove_tree (p);
		else
			g_unlink (p);
	}
	g_rmdir (dir);
}

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	f->dir = g_dir_make_tmp ("lrg-thumbs-XXXXXX", NULL);
	f->cache = g_build_filename (f->dir, ".cache", "thumbnails", NULL);
	f->thumbs = lrg_thumbnail_cache_new (f->cache);
	lrg_thumbnail_cache_set_size (f->thumbs, 32);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->thumbs);
	remove_tree (f->dir);
	g_free (f->dir);
	g_free (f->cache);
}

/* Writes a w x h PNG filled with one colour. */
static gchar *
write_png (Fixture     *f,
           const gchar *name,
           gint         w,
           gint         h,
           guint8       red)
{
	GrlColor            c = { red, 64, 32, 255 };
	g_autoptr(GrlImage) img = grl_image_new_color (w, h, &c);
	gchar              *path = g_build_filename (f->dir, name, NULL);

	g_assert_true (grl_image_export (img, path));
	return path;
}

static gchar *
write_level (Fixture     *f,
             const gchar *name)
{
	g_autoptr(LrgEditor)     editor = lrg_editor_new ();
	g_autoptr(LrgNode)       a = lrg_node_new ("A");
	g_autoptr(LrgNode)       b = lrg_node_new ("B");
	g_autoptr(LrgNodeVisual) va = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(GError)        error = NULL;
	gchar                   *path = g_build_filename (f->dir, name, NULL);

	lrg_node_set_visual (a, va);
	lrg_node_set_location_xyz (a, -5.0f, 0.0f, 3.0f);
	lrg_node_set_location_xyz (b, 5.0f, 0.0f, -3.0f);
	lrg_editor_add_node (editor, a, NULL);
	lrg_editor_add_node (editor, b, a);
	g_assert_true (lrg_editor_save_level (editor, path, &error));
	g_assert_no_error (error);
	return path;
}

static guint
count_files (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	guint           n = 0;

	while (d != NULL && g_dir_read_name (d) != NULL)
		n++;
	return n;
}

static void
test_keys (Fixture       *f,
           gconstpointer  data)
{
	g_autofree gchar *k1 = lrg_thumbnail_cache_make_key ("/a/b.png", 100, 5);
	g_autofree gchar *k2 = lrg_thumbnail_cache_make_key ("/a/b.png", 101, 5);
	g_autofree gchar *k3 = lrg_thumbnail_cache_make_key ("/a/b.png", 100, 6);
	g_autofree gchar *k4 = lrg_thumbnail_cache_make_key ("/a/c.png", 100, 5);
	g_autofree gchar *png = write_png (f, "x.png", 8, 8, 10);
	g_autofree gchar *entry = NULL;

	g_assert_true (g_str_has_suffix (k1, "-100-5.png"));
	g_assert_cmpstr (k1, !=, k2);
	g_assert_cmpstr (k1, !=, k3);
	g_assert_cmpstr (k1, !=, k4);
	/* Same file -> same 40-char path hash prefix. */
	g_assert_cmpint (strncmp (k1, k2, 40), ==, 0);

	entry = lrg_thumbnail_cache_get_entry_path (f->thumbs, png);
	g_assert_nonnull (entry);
	g_assert_true (g_str_has_prefix (entry, lrg_thumbnail_cache_get_dir (f->thumbs)));
	g_assert_null (lrg_thumbnail_cache_get_entry_path (f->thumbs, "/nonexistent/file.png"));
	g_assert_null (lrg_thumbnail_cache_lookup (f->thumbs, png));
}

static void
test_generate_texture_and_staleness (Fixture       *f,
                                     gconstpointer  data)
{
	g_autofree gchar   *png = write_png (f, "big.png", 128, 64, 200);
	g_autoptr(GError)   error = NULL;
	g_autofree gchar   *thumb = NULL;
	g_autofree gchar   *hit = NULL;
	g_autofree gchar   *thumb2 = NULL;
	g_autoptr(GrlImage) img = NULL;
	g_autofree gchar   *png_small = write_png (f, "tile.png", 8, 8, 1);
	g_autofree gchar   *thumb3 = NULL;

	thumb = lrg_thumbnail_cache_generate (f->thumbs, png, &error);
	g_assert_no_error (error);
	g_assert_nonnull (thumb);
	img = grl_image_new_from_file (thumb);
	g_assert_true (grl_image_is_valid (img));
	g_assert_cmpint (grl_image_get_width (img), ==, 32);
	g_assert_cmpint (grl_image_get_height (img), ==, 16);
	g_clear_object (&img);

	hit = lrg_thumbnail_cache_lookup (f->thumbs, png);
	g_assert_cmpstr (hit, ==, thumb);

	/* Rewriting the file (new size and mtime) misses; storing the new
	 * version sweeps the old one. */
	g_usleep (20000);
	g_free (write_png (f, "big.png", 64, 64, 100));
	g_assert_null (lrg_thumbnail_cache_lookup (f->thumbs, png));
	thumb2 = lrg_thumbnail_cache_generate (f->thumbs, png, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (thumb2, !=, thumb);
	g_assert_false (g_file_test (thumb, G_FILE_TEST_EXISTS));

	/* Pixel art is enlarged crisply (nearest, whole multiples). */
	thumb3 = lrg_thumbnail_cache_generate (f->thumbs, png_small, &error);
	g_assert_no_error (error);
	img = grl_image_new_from_file (thumb3);
	g_assert_cmpint (grl_image_get_width (img), ==, 32);

	/* Invalidation deletes every version of the path. */
	g_assert_cmpuint (lrg_thumbnail_cache_invalidate (f->thumbs, png), ==, 1);
	g_assert_null (lrg_thumbnail_cache_lookup (f->thumbs, png));
	g_assert_cmpuint (count_files (f->cache), ==, 1);
}

static void
test_generate_level_and_unsupported (Fixture       *f,
                                     gconstpointer  data)
{
	g_autofree gchar   *level = write_level (f, "demo.rlevel");
	g_autofree gchar   *model = g_build_filename (f->dir, "thing.glb", NULL);
	g_autofree gchar   *thumb = NULL;
	g_autoptr(GError)   error = NULL;
	g_autoptr(GrlImage) img = NULL;

	g_assert_true (lrg_thumbnail_cache_is_supported (f->thumbs, level));
	thumb = lrg_thumbnail_cache_generate (f->thumbs, level, &error);
	g_assert_no_error (error);
	img = grl_image_new_from_file (thumb);
	g_assert_cmpint (grl_image_get_width (img), ==, 32);
	g_assert_cmpint (grl_image_get_height (img), ==, 32);

	/* Models need a renderer (a GL context). */
	g_assert_true (g_file_set_contents (model, "glTF", -1, NULL));
	g_assert_false (lrg_thumbnail_cache_is_supported (f->thumbs, model));
	g_assert_null (lrg_thumbnail_cache_generate (f->thumbs, model, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_assert_false (lrg_thumbnail_cache_request (f->thumbs, model));
}

typedef struct
{
	GHashTable *ready;   /* path -> thumb ("" on failure) */
} ReadyLog;

static void
on_ready (LrgThumbnailCache *cache,
          const gchar       *path,
          const gchar       *thumb,
          gpointer           user_data)
{
	ReadyLog *log = user_data;

	g_hash_table_replace (log->ready, g_strdup (path), g_strdup (thumb != NULL ? thumb : ""));
}

/* Pumps process() until @n results arrived (or a few seconds passed). */
static void
pump (Fixture  *f,
      ReadyLog *log,
      guint     n)
{
	gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

	while (g_hash_table_size (log->ready) < n && g_get_monotonic_time () < deadline)
	{
		lrg_thumbnail_cache_process (f->thumbs, 1);
		g_usleep (2000);
	}
}

static GrlImage *
fake_render (const gchar *path,
             guint        size,
             gpointer     user_data)
{
	GrlColor c = { 9, 9, 9, 255 };

	(*(guint *) user_data)++;
	return grl_image_new_color ((gint) size, (gint) size, &c);
}

static void
test_async_requests (Fixture       *f,
                     gconstpointer  data)
{
	g_autofree gchar *a = write_png (f, "a.png", 40, 40, 1);
	g_autofree gchar *b = write_png (f, "b.png", 40, 20, 2);
	g_autofree gchar *lvl = write_level (f, "l.rlevel");
	g_autofree gchar *model = g_build_filename (f->dir, "m.gltf", NULL);
	g_autofree gchar *broken = g_build_filename (f->dir, "broken.png", NULL);
	ReadyLog          log;
	guint             renders = 0;

	log.ready = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	g_signal_connect (f->thumbs, "ready", G_CALLBACK (on_ready), &log);
	g_assert_true (g_file_set_contents (model, "{}", -1, NULL));
	g_assert_true (g_file_set_contents (broken, "not a png", -1, NULL));
	lrg_thumbnail_cache_set_renderer (f->thumbs, fake_render, &renders, NULL);

	g_assert_true (lrg_thumbnail_cache_request (f->thumbs, a));
	g_assert_true (lrg_thumbnail_cache_request (f->thumbs, a));   /* deduplicated */
	g_assert_true (lrg_thumbnail_cache_request (f->thumbs, b));
	g_assert_true (lrg_thumbnail_cache_request (f->thumbs, lvl));
	g_assert_true (lrg_thumbnail_cache_request (f->thumbs, model));
	g_assert_true (lrg_thumbnail_cache_request (f->thumbs, broken));
	g_assert_false (lrg_thumbnail_cache_request (f->thumbs, "/x/readme.txt"));
	g_assert_cmpuint (lrg_thumbnail_cache_get_n_pending (f->thumbs), ==, 5);

	/* Nothing is generated on the requesting call for GPU kinds. */
	g_assert_cmpuint (renders, ==, 0);
	pump (f, &log, 5);
	g_assert_cmpuint (g_hash_table_size (log.ready), ==, 5);
	g_assert_cmpuint (renders, ==, 1);
	g_assert_cmpuint (lrg_thumbnail_cache_get_n_pending (f->thumbs), ==, 0);
	g_assert_true (g_file_test (g_hash_table_lookup (log.ready, a), G_FILE_TEST_IS_REGULAR));
	g_assert_true (g_file_test (g_hash_table_lookup (log.ready, lvl), G_FILE_TEST_IS_REGULAR));
	g_assert_true (g_file_test (g_hash_table_lookup (log.ready, model), G_FILE_TEST_IS_REGULAR));
	g_assert_cmpstr (g_hash_table_lookup (log.ready, broken), ==, "");

	/* A second request is a cache hit answered without rendering. */
	g_hash_table_remove_all (log.ready);
	lrg_thumbnail_cache_request (f->thumbs, model);
	pump (f, &log, 1);
	g_assert_cmpuint (renders, ==, 1);
	g_assert_nonnull (g_hash_table_lookup (log.ready, model));

	g_hash_table_unref (log.ready);
}

/* Writes a mono 16-bit WAV: silence for the first half, a full-scale
 * square wave at @amp for the second half. */
static gchar *
write_wav (Fixture     *f,
           const gchar *name,
           gfloat       amp)
{
	const guint         rate = 8000;
	const guint         frames = 8000;
	g_autofree gint16  *pcm = g_new0 (gint16, frames);
	g_autoptr(GrlWave)  wave = NULL;
	g_autoptr(GError)   error = NULL;
	gchar              *path = g_build_filename (f->dir, name, NULL);
	guint               i;

	for (i = frames / 2; i < frames; i++)
		pcm[i] = (gint16) (((i / 20) % 2 == 0 ? amp : -amp) * 32767.0f);
	wave = grl_wave_new_from_samples (rate, 16, 1, (const guint8 *) pcm, frames * sizeof (gint16));
	g_assert_nonnull (wave);
	if (!grl_wave_export (wave, path, &error))
	{
		g_free (path);
		return NULL;
	}
	return path;
}

/* RGBA of one pixel. */
static void
pixel (GrlImage *img,
       gint      x,
       gint      y,
       guint8    out[4])
{
	gsize   n = 0;
	guint8 *px = grl_image_get_pixels (img, &n);
	gsize   at = ((gsize) y * (gsize) grl_image_get_width (img) + (gsize) x) * 4;

	g_assert_nonnull (px);
	g_assert_cmpuint (at + 3, <, n);
	memcpy (out, px + at, 4);
}

static void
test_waveform (Fixture       *f,
               gconstpointer  data)
{
	g_autofree gchar   *wav = write_wav (f, "beep.wav", 0.8f);
	g_autoptr(GError)   error = NULL;
	g_autoptr(GrlImage) img = NULL;
	g_autofree gchar   *thumb = NULL;
	guint8              top_quiet[4], top_loud[4], mid_quiet[4], bg[4];
	gint                h;

	if (wav == NULL)
	{
		g_test_skip ("Cannot write WAV files in this build");
		return;
	}
	g_assert_cmpint (lrg_thumbnail_cache_get_kind (wav), ==, LRG_THUMBNAIL_KIND_WAVEFORM);
	g_assert_true (lrg_thumbnail_cache_is_supported (f->thumbs, wav));

	img = lrg_thumbnail_cache_render_waveform (wav, 64, 32, &error);
	if (img == NULL && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
	{
		g_test_skip ("Audio decoding unavailable");
		return;
	}
	g_assert_no_error (error);
	g_assert_cmpint (grl_image_get_width (img), ==, 64);
	g_assert_cmpint (grl_image_get_height (img), ==, 32);
	h = grl_image_get_height (img);

	/* Quiet half: only the centre line is drawn; loud half: the envelope
	 * reaches well above the centre (0.8 * 0.45 * h ~ 11 px). */
	pixel (img, 8, h / 2 - 8, top_quiet);
	pixel (img, 56, h / 2 - 8, top_loud);
	pixel (img, 8, h / 2, mid_quiet);
	pixel (img, 56, 0, bg);
	g_assert_cmpint (top_quiet[0], ==, bg[0]);
	g_assert_cmpint (top_loud[0], !=, bg[0]);
	g_assert_cmpint (mid_quiet[0], !=, bg[0]);

	/* Through the cache (sync), at the cache size (square). */
	thumb = lrg_thumbnail_cache_generate (f->thumbs, wav, &error);
	g_assert_no_error (error);
	g_clear_object (&img);
	img = grl_image_new_from_file (thumb);
	g_assert_cmpint (grl_image_get_width (img), ==, 32);
	g_assert_cmpint (grl_image_get_height (img), ==, 32);

	/* Garbage audio fails cleanly. */
	g_clear_object (&img);
	{
		g_autofree gchar *bad = g_build_filename (f->dir, "bad.wav", NULL);

		g_assert_true (g_file_set_contents (bad, "RIFFnope", -1, NULL));
		img = lrg_thumbnail_cache_render_waveform (bad, 32, 32, &error);
		g_assert_null (img);
		g_assert_nonnull (error);
	}
}

static void
test_texture_meta (Fixture       *f,
                   gconstpointer  data)
{
	g_autofree gchar       *png = write_png (f, "big.png", 64, 32, 50);
	g_autofree gchar       *meta_path = g_strconcat (png, ".meta", NULL);
	g_autofree gchar       *plain = NULL;
	g_autofree gchar       *keyed = NULL;
	g_autofree gchar       *keyed2 = NULL;
	g_autofree gchar       *k = NULL;
	g_autoptr(LrgAssetMeta) meta = lrg_asset_meta_new_for_asset (png);
	g_autoptr(GrlImage)     img = NULL;
	g_autoptr(GError)       error = NULL;

	k = lrg_thumbnail_cache_make_key_full ("/a.png", 1, 2, "0123456789abcdef");
	g_assert_true (g_str_has_suffix (k, "-1-2-0123456789ab.png"));

	plain = lrg_thumbnail_cache_get_entry_path (f->thumbs, png);

	/* A sidecar changes the key (settings hash) and max-size shrinks the
	 * preview before fitting. */
	lrg_asset_meta_set_texture_max_size (meta, 8);
	g_assert_true (lrg_asset_meta_save_to_file (meta, meta_path, &error));
	keyed = lrg_thumbnail_cache_get_entry_path (f->thumbs, png);
	g_assert_cmpstr (keyed, !=, plain);
	img = lrg_thumbnail_cache_render_cpu (png, 32, &error);
	g_assert_no_error (error);
	g_assert_cmpint (grl_image_get_width (img), ==, 8);
	g_assert_cmpint (grl_image_get_height (img), ==, 4);

	/* Rewriting the sidecar with only a new guid keeps the key: the hash
	 * covers settings, not identity. */
	lrg_asset_meta_regenerate_guid (meta);
	g_assert_true (lrg_asset_meta_save_to_file (meta, meta_path, &error));
	keyed2 = lrg_thumbnail_cache_get_entry_path (f->thumbs, png);
	g_assert_cmpstr (keyed2, ==, keyed);
}

static void
test_font_renderer (Fixture       *f,
                    gconstpointer  data)
{
	g_autofree gchar *ttf = g_build_filename (f->dir, "ui.ttf", NULL);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *thumb = NULL;
	guint             renders = 0;

	g_assert_true (g_file_set_contents (ttf, "not really a font", -1, NULL));
	g_assert_cmpint (lrg_thumbnail_cache_get_kind (ttf), ==, LRG_THUMBNAIL_KIND_FONT);
	g_assert_cmpint (lrg_thumbnail_cache_get_kind ("x.txt"), ==, LRG_THUMBNAIL_KIND_NONE);

	/* Without the UI hook fonts are unsupported. */
	g_assert_false (lrg_thumbnail_cache_is_supported (f->thumbs, ttf));
	g_assert_null (lrg_thumbnail_cache_generate (f->thumbs, ttf, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_clear_error (&error);

	/* The model renderer does not cover fonts, the font hook does. */
	lrg_thumbnail_cache_set_renderer (f->thumbs, fake_render, &renders, NULL);
	g_assert_false (lrg_thumbnail_cache_is_supported (f->thumbs, ttf));
	lrg_thumbnail_cache_set_kind_renderer (f->thumbs, LRG_THUMBNAIL_KIND_FONT, fake_render, &renders, NULL);
	g_assert_true (lrg_thumbnail_cache_is_supported (f->thumbs, ttf));
	thumb = lrg_thumbnail_cache_generate (f->thumbs, ttf, &error);
	g_assert_no_error (error);
	g_assert_nonnull (thumb);
	g_assert_cmpuint (renders, ==, 1);

	/* Only GPU kinds have a slot. */
	g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, "*no renderer slot*");
	lrg_thumbnail_cache_set_kind_renderer (f->thumbs, LRG_THUMBNAIL_KIND_IMAGE, fake_render, &renders, NULL);
	g_test_assert_expected_messages ();
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#define ADD(path, fn) g_test_add (path, Fixture, NULL, fixture_setup, fn, fixture_teardown)
	ADD ("/thumbnail-cache/keys", test_keys);
	ADD ("/thumbnail-cache/texture-staleness", test_generate_texture_and_staleness);
	ADD ("/thumbnail-cache/level-unsupported", test_generate_level_and_unsupported);
	ADD ("/thumbnail-cache/async", test_async_requests);
	ADD ("/thumbnail-cache/waveform", test_waveform);
	ADD ("/thumbnail-cache/texture-meta", test_texture_meta);
	ADD ("/thumbnail-cache/font-renderer", test_font_renderer);
#undef ADD

	return g_test_run ();
}
