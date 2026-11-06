/* test-editor-asset-meta.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for LrgAssetMeta (the "<file>.meta" sidecar) and the
 * sidecar-aware parts of LrgAssetDatabase: round trips, unknown keys,
 * typed and GVariant access, the settings hash, guid migration from the
 * legacy sha256(path), lookup by guid, rename/move/duplicate/trash
 * carrying the sidecar, orphan detection and the write guard.
 *
 * HOME and XDG_DATA_HOME point into the test's temp root (set before any
 * GLib call caches them) so g_file_trash() uses a trash on the same file
 * system and never touches the user's real trash.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <libregnum.h>

static gchar *test_root = NULL;

typedef struct
{
	gchar *dir;      /* project root */
	gchar *assets;   /* <dir>/assets, the search dir */
} Fixture;

static void
remove_tree (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *p = g_build_filename (dir, name, NULL);

		if (g_file_test (p, G_FILE_TEST_IS_DIR) && !g_file_test (p, G_FILE_TEST_IS_SYMLINK))
			remove_tree (p);
		else
			g_unlink (p);
	}
	g_rmdir (dir);
}

static gchar *
write_file (const gchar *dir,
            const gchar *rel,
            const gchar *contents)
{
	gchar            *path = g_build_filename (dir, rel, NULL);
	g_autofree gchar *parent = g_path_get_dirname (path);

	g_assert_cmpint (g_mkdir_with_parents (parent, 0755), ==, 0);
	g_assert_true (g_file_set_contents (path, contents, -1, NULL));
	return path;
}

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autofree gchar *tmpl = g_build_filename (test_root, "proj-XXXXXX", NULL);

	f->dir = g_mkdtemp (g_steal_pointer (&tmpl));
	g_assert_nonnull (f->dir);
	f->assets = g_build_filename (f->dir, "assets", NULL);
	g_assert_cmpint (g_mkdir_with_parents (f->assets, 0755), ==, 0);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	remove_tree (f->dir);
	g_free (f->dir);
	g_free (f->assets);
}

static LrgAssetDatabase *
make_db (Fixture  *f,
         gboolean  write_meta)
{
	LrgAssetDatabase  *db = lrg_asset_database_new ();
	g_autoptr(GError)  error = NULL;

	lrg_asset_database_add_search_dir (db, f->assets);
	lrg_asset_database_set_write_meta (db, write_meta);
	g_assert_true (lrg_asset_database_scan (db, &error));
	g_assert_no_error (error);
	return db;
}

/* ==========================================================================
 * LrgAssetMeta
 * ========================================================================== */

static void
test_meta_new (void)
{
	g_autoptr(LrgAssetMeta) a = lrg_asset_meta_new_for_asset ("dir/Hero.PNG");
	g_autoptr(LrgAssetMeta) b = lrg_asset_meta_new_for_asset ("music.ogg");
	g_autoptr(LrgAssetMeta) c = lrg_asset_meta_new ();

	g_assert_cmpstr (lrg_asset_meta_get_importer (a), ==, "texture");
	g_assert_cmpstr (lrg_asset_meta_get_importer (b), ==, "audio");
	g_assert_cmpstr (lrg_asset_meta_get_importer (c), ==, "generic");
	g_assert_true (g_uuid_string_is_valid (lrg_asset_meta_get_guid (a)));
	g_assert_cmpstr (lrg_asset_meta_get_guid (a), !=, lrg_asset_meta_get_guid (b));
	g_assert_true (lrg_asset_meta_is_dirty (a));

	/* Defaults with nothing stored. */
	g_assert_cmpint (lrg_asset_meta_get_texture_filter (a), ==, LRG_ASSET_TEXTURE_FILTER_BILINEAR);
	g_assert_cmpint (lrg_asset_meta_get_texture_wrap (a), ==, LRG_ASSET_TEXTURE_WRAP_REPEAT);
	g_assert_false (lrg_asset_meta_get_texture_mipmaps (a));
	g_assert_true (lrg_asset_meta_get_texture_srgb (a));
	g_assert_cmpuint (lrg_asset_meta_get_texture_max_size (a), ==, 0);
	g_assert_cmpint (lrg_asset_meta_get_audio_mode (b), ==, LRG_ASSET_AUDIO_MODE_SAMPLE);
	g_assert_cmpfloat (lrg_asset_meta_get_audio_volume (b), ==, 1.0);
	g_assert_false (lrg_asset_meta_has_setting (a, "filter"));

	g_assert_true (lrg_asset_meta_is_meta_path ("a/b.png.meta"));
	g_assert_false (lrg_asset_meta_is_meta_path ("a/b.png"));
	{
		g_autofree gchar *p = lrg_asset_meta_path_for_asset ("x/y.png");

		g_assert_cmpstr (p, ==, "x/y.png.meta");
	}
}

static void
test_meta_round_trip (Fixture       *f,
                      gconstpointer  data)
{
	g_autoptr(LrgAssetMeta) m = lrg_asset_meta_new_for_asset ("a.png");
	g_autoptr(LrgAssetMeta) back = NULL;
	g_autoptr(LrgAssetMeta) model = NULL;
	g_autoptr(GError)       error = NULL;
	g_autofree gchar       *path = g_build_filename (f->dir, "a.png.meta", NULL);
	g_autoptr(GVariant)     v1 = NULL;
	g_autoptr(GVariant)     v2 = NULL;
	g_autofree gchar       *cps = NULL;
	g_autofree gchar       *asset = NULL;

	lrg_asset_meta_set_texture_filter (m, LRG_ASSET_TEXTURE_FILTER_POINT);
	lrg_asset_meta_set_texture_mipmaps (m, TRUE);
	lrg_asset_meta_set_texture_wrap (m, LRG_ASSET_TEXTURE_WRAP_MIRROR);
	lrg_asset_meta_set_texture_srgb (m, FALSE);
	lrg_asset_meta_set_texture_max_size (m, 512);
	g_assert_true (lrg_asset_meta_save_to_file (m, path, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_asset_meta_is_dirty (m));

	back = lrg_asset_meta_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_asset_meta_get_guid (back), ==, lrg_asset_meta_get_guid (m));
	g_assert_cmpstr (lrg_asset_meta_get_importer (back), ==, "texture");
	g_assert_cmpint (lrg_asset_meta_get_texture_filter (back), ==, LRG_ASSET_TEXTURE_FILTER_POINT);
	g_assert_true (lrg_asset_meta_get_texture_mipmaps (back));
	g_assert_cmpint (lrg_asset_meta_get_texture_wrap (back), ==, LRG_ASSET_TEXTURE_WRAP_MIRROR);
	g_assert_false (lrg_asset_meta_get_texture_srgb (back));
	g_assert_cmpuint (lrg_asset_meta_get_texture_max_size (back), ==, 512);
	g_assert_false (lrg_asset_meta_is_dirty (back));

	v1 = lrg_asset_meta_to_variant (m);
	v2 = lrg_asset_meta_to_variant (back);
	g_assert_true (g_variant_equal (v1, v2));

	/* Model and font settings, through a second file. */
	model = lrg_asset_meta_new_for_asset ("ship.glb");
	lrg_asset_meta_set_model_scale (model, 0.025);
	lrg_asset_meta_set_model_up_axis (model, LRG_ASSET_UP_AXIS_Z);
	lrg_asset_meta_set_model_generate_normals (model, TRUE);
	lrg_asset_meta_set_model_import_animations (model, FALSE);
	lrg_asset_meta_set_font_codepoints (model, "32-126,0x4E00-0x4E0F");
	g_assert_true (lrg_asset_meta_save_to_file (model, path, &error));
	g_clear_object (&back);
	asset = g_strndup (path, strlen (path) - strlen (LRG_ASSET_META_SUFFIX));
	back = lrg_asset_meta_load_for_asset (asset, &error);
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (lrg_asset_meta_get_model_scale (back), 0.025, 1e-12);
	g_assert_cmpint (lrg_asset_meta_get_model_up_axis (back), ==, LRG_ASSET_UP_AXIS_Z);
	g_assert_true (lrg_asset_meta_get_model_generate_normals (back));
	g_assert_false (lrg_asset_meta_get_model_import_animations (back));
	cps = lrg_asset_meta_get_font_codepoints (back);
	g_assert_cmpstr (cps, ==, "32-126,0x4E00-0x4E0F");

	/* Missing sidecar: NOT_FOUND. */
	g_clear_object (&back);
	back = lrg_asset_meta_load_for_asset ("/nonexistent/thing.png", &error);
	g_assert_null (back);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_meta_unknown_keys (void)
{
	static const gchar *yaml =
		"version: 1\n"
		"guid: 0123abcd-0000-4000-8000-000000000001\n"
		"importer: texture\n"
		"author: someone\n"
		"custom:\n"
		"  nested: [1, 2, 3]\n"
		"settings:\n"
		"  filter: trilinear\n"
		"  future-key: hello\n"
		"  atlas:\n"
		"    padding: 2\n";
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgAssetMeta) m = lrg_asset_meta_new_from_data (yaml, -1, &error);
	g_autoptr(LrgAssetMeta) back = NULL;
	g_autoptr(LrgAssetMeta) copy = NULL;
	g_autofree gchar       *out = NULL;
	g_autofree gchar       *hello = NULL;
	g_auto(GStrv)           keys = NULL;
	g_autoptr(GVariant)     atlas = NULL;
	gint64                  padding = 0;

	g_assert_no_error (error);
	g_assert_cmpint (lrg_asset_meta_get_texture_filter (m), ==, LRG_ASSET_TEXTURE_FILTER_TRILINEAR);

	/* Modify a known key, serialize, parse again: unknowns survive. */
	lrg_asset_meta_set_texture_filter (m, LRG_ASSET_TEXTURE_FILTER_POINT);
	out = lrg_asset_meta_to_data (m, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (out, "author"));
	g_assert_nonnull (strstr (out, "custom"));
	g_assert_nonnull (strstr (out, "nested"));
	back = lrg_asset_meta_new_from_data (out, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_asset_meta_get_texture_filter (back), ==, LRG_ASSET_TEXTURE_FILTER_POINT);
	hello = lrg_asset_meta_get_setting_string (back, "future-key");
	g_assert_cmpstr (hello, ==, "hello");
	keys = lrg_asset_meta_get_setting_keys (back);
	g_assert_cmpuint (g_strv_length (keys), ==, 3);
	g_assert_true (g_strv_contains ((const gchar * const *) keys, "atlas"));
	atlas = lrg_asset_meta_get_setting_variant (back, "atlas");
	g_assert_true (g_variant_is_of_type (atlas, G_VARIANT_TYPE_VARDICT));
	g_assert_true (g_variant_lookup (atlas, "padding", "x", &padding));
	g_assert_cmpint (padding, ==, 2);
	g_assert_cmpstr (lrg_asset_meta_get_guid (back), ==, "0123abcd-0000-4000-8000-000000000001");

	/* A copy is deep. */
	copy = lrg_asset_meta_copy (back);
	lrg_asset_meta_set_setting_string (copy, "future-key", "changed");
	g_free (hello);
	hello = lrg_asset_meta_get_setting_string (back, "future-key");
	g_assert_cmpstr (hello, ==, "hello");

	/* Empty data is an empty sidecar (no guid); a scalar root is an error. */
	g_clear_object (&m);
	m = lrg_asset_meta_new_from_data ("", 0, &error);
	g_assert_no_error (error);
	g_assert_null (lrg_asset_meta_get_guid (m));
	g_clear_object (&m);
	m = lrg_asset_meta_new_from_data ("just a string\n", -1, &error);
	g_assert_null (m);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
on_setting_changed (LrgAssetMeta *meta,
                    const gchar  *key,
                    gpointer      user_data)
{
	(*(guint *) user_data)++;
}

static void
test_meta_variant (void)
{
	g_autoptr(LrgAssetMeta) m = lrg_asset_meta_new_for_asset ("a.png");
	g_autoptr(GError)       error = NULL;
	g_autoptr(GVariant)     v = NULL;
	g_autoptr(GVariant)     all = NULL;
	g_autoptr(GVariant)     def = NULL;
	g_auto(GStrv)           known = NULL;
	g_auto(GStrv)           choices = NULL;
	guint                   filter_changes = 0;
	GVariantBuilder         b;

	g_signal_connect (m, "setting-changed::filter", G_CALLBACK (on_setting_changed), &filter_changes);

	/* Known keys are validated. */
	g_assert_false (lrg_asset_meta_set_setting_variant (m, "filter", g_variant_new_string ("nearest"), &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_true (lrg_asset_meta_set_setting_variant (m, "filter", g_variant_new_string ("point"), &error));
	g_assert_no_error (error);
	g_assert_cmpuint (filter_changes, ==, 1);
	/* Same value again: no change signal. */
	g_assert_true (lrg_asset_meta_set_setting_variant (m, "filter", g_variant_new_string ("point"), NULL));
	g_assert_cmpuint (filter_changes, ==, 1);
	g_assert_false (lrg_asset_meta_set_setting_variant (m, "max-size", g_variant_new_int64 (99999), &error));
	g_clear_error (&error);
	g_assert_false (lrg_asset_meta_set_setting_variant (m, "max-size", g_variant_new_double (1.5), &error));
	g_clear_error (&error);
	g_assert_true (lrg_asset_meta_set_setting_variant (m, "max-size", g_variant_new_double (256.0), &error));
	g_assert_true (lrg_asset_meta_set_setting_variant (m, "mipmaps", g_variant_new_string ("true"), &error));
	g_assert_false (lrg_asset_meta_set_setting_variant (m, "srgb", g_variant_new_int32 (3), &error));
	g_clear_error (&error);
	/* Boxed "v" as found in an a{sv} from IPC. */
	g_assert_true (lrg_asset_meta_set_setting_variant (m, "wrap", g_variant_new_variant (g_variant_new_string ("clamp")),
	                                                   &error));
	g_assert_no_error (error);

	g_assert_cmpint (lrg_asset_meta_get_texture_filter (m), ==, LRG_ASSET_TEXTURE_FILTER_POINT);
	g_assert_cmpuint (lrg_asset_meta_get_texture_max_size (m), ==, 256);
	g_assert_true (lrg_asset_meta_get_texture_mipmaps (m));
	g_assert_cmpint (lrg_asset_meta_get_texture_wrap (m), ==, LRG_ASSET_TEXTURE_WRAP_CLAMP);

	/* Typed reads. */
	v = lrg_asset_meta_get_setting_variant (m, "max-size");
	g_assert_true (g_variant_is_of_type (v, G_VARIANT_TYPE_INT64));
	g_assert_cmpint (g_variant_get_int64 (v), ==, 256);
	g_clear_pointer (&v, g_variant_unref);
	v = lrg_asset_meta_get_setting_variant (m, "srgb");   /* default */
	g_assert_true (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN));
	g_assert_true (g_variant_get_boolean (v));
	g_clear_pointer (&v, g_variant_unref);
	g_assert_null (lrg_asset_meta_get_setting_variant (m, "nope"));

	/* Unknown keys take any scalar or container. */
	g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&b, "{sv}", "x", g_variant_new_int32 (4));
	g_variant_builder_add (&b, "{sv}", "name", g_variant_new_string ("hi"));
	g_assert_true (lrg_asset_meta_set_setting_variant (m, "extra", g_variant_builder_end (&b), &error));
	g_assert_true (lrg_asset_meta_set_setting_variant (m, "ratio", g_variant_new_double (0.5), &error));
	g_assert_no_error (error);
	v = lrg_asset_meta_get_setting_variant (m, "ratio");
	g_assert_cmpfloat (g_variant_get_double (v), ==, 0.5);
	g_clear_pointer (&v, g_variant_unref);

	/* NULL resets to the default. */
	g_assert_true (lrg_asset_meta_set_setting_variant (m, "max-size", NULL, &error));
	g_assert_false (lrg_asset_meta_has_setting (m, "max-size"));
	g_assert_cmpuint (lrg_asset_meta_get_texture_max_size (m), ==, 0);

	/* The settings dict includes defaults and unknowns, sorted. */
	all = lrg_asset_meta_get_settings_variant (m, TRUE);
	v = g_variant_lookup_value (all, "srgb", G_VARIANT_TYPE_BOOLEAN);
	g_assert_nonnull (v);
	g_clear_pointer (&v, g_variant_unref);
	v = g_variant_lookup_value (all, "extra", G_VARIANT_TYPE_VARDICT);
	g_assert_nonnull (v);
	g_clear_pointer (&v, g_variant_unref);

	/* Schema helpers for property grids. */
	known = lrg_asset_meta_list_known_settings ("texture");
	g_assert_cmpuint (g_strv_length (known), ==, 5);
	g_assert_cmpstr (known[0], ==, "filter");
	choices = lrg_asset_meta_get_setting_choices ("model", "up-axis");
	g_assert_cmpuint (g_strv_length (choices), ==, 2);
	g_assert_null (lrg_asset_meta_get_setting_choices ("texture", "srgb"));
	def = lrg_asset_meta_get_default_setting ("font", "size");
	g_assert_cmpint (g_variant_get_int64 (def), ==, 32);
	g_assert_null (lrg_asset_meta_get_default_setting ("font", "nope"));
}

static void
test_meta_settings_hash (void)
{
	g_autoptr(LrgAssetMeta) a = lrg_asset_meta_new_for_asset ("a.png");
	g_autoptr(LrgAssetMeta) b = lrg_asset_meta_new_for_asset ("b.png");
	g_autofree gchar       *ha = NULL;
	g_autofree gchar       *hb = NULL;
	g_autofree gchar       *h0 = NULL;

	/* Same values, different order and guid: equal. Defaults stored
	 * explicitly hash like defaults left implicit. */
	h0 = lrg_asset_meta_get_settings_hash (a);
	lrg_asset_meta_set_texture_srgb (b, TRUE);
	hb = lrg_asset_meta_get_settings_hash (b);
	g_assert_cmpstr (h0, ==, hb);
	g_clear_pointer (&hb, g_free);

	lrg_asset_meta_set_texture_max_size (a, 64);
	lrg_asset_meta_set_texture_filter (a, LRG_ASSET_TEXTURE_FILTER_POINT);
	lrg_asset_meta_set_texture_filter (b, LRG_ASSET_TEXTURE_FILTER_POINT);
	lrg_asset_meta_set_texture_max_size (b, 64);
	ha = lrg_asset_meta_get_settings_hash (a);
	hb = lrg_asset_meta_get_settings_hash (b);
	g_assert_cmpuint (strlen (ha), ==, 40);
	g_assert_cmpstr (ha, ==, hb);
	g_assert_cmpstr (ha, !=, h0);
}

static void
test_meta_codepoints (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GArray) cps = lrg_asset_meta_parse_codepoints ("65-67, 0x41, U+44,32", &error);

	g_assert_no_error (error);
	g_assert_cmpuint (cps->len, ==, 5);
	g_assert_cmpuint (g_array_index (cps, guint32, 0), ==, 32);
	g_assert_cmpuint (g_array_index (cps, guint32, 1), ==, 65);
	g_assert_cmpuint (g_array_index (cps, guint32, 4), ==, 68);
	g_clear_pointer (&cps, g_array_unref);

	cps = lrg_asset_meta_parse_codepoints ("70-60", &error);
	g_assert_null (cps);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	cps = lrg_asset_meta_parse_codepoints ("abc", &error);
	g_assert_null (cps);
	g_clear_error (&error);
	cps = lrg_asset_meta_parse_codepoints ("0x110000", &error);
	g_assert_null (cps);
}

/* ==========================================================================
 * LrgAssetDatabase + sidecars
 * ========================================================================== */

static void
test_guid_migration (Fixture       *f,
                     gconstpointer  data)
{
	g_autofree gchar           *png = write_file (f->assets, "sprites/p.png", "x");
	g_autofree gchar           *lvl = write_file (f->assets, "l.rlevel", "nodes: []\n");
	g_autofree gchar           *png_meta = lrg_asset_meta_path_for_asset (png);
	g_autofree gchar           *legacy = g_compute_checksum_for_string (G_CHECKSUM_SHA256, "sprites/p.png", -1);
	g_autoptr(LrgAssetDatabase) ro = NULL;
	g_autoptr(LrgAssetDatabase) rw = NULL;
	g_autoptr(LrgAssetDatabase) again = NULL;
	LrgAssetEntry              *e;

	/* Read-only database: the in-memory guid is the 0.8.0 value, and
	 * nothing is written. */
	ro = make_db (f, FALSE);
	g_assert_false (lrg_asset_database_get_write_meta (ro));
	g_assert_cmpuint (lrg_asset_database_get_count (ro), ==, 2);
	e = lrg_asset_database_lookup_path (ro, "sprites/p.png");
	g_assert_nonnull (e);
	g_assert_cmpstr (lrg_asset_entry_get_guid (e), ==, legacy);
	g_assert_false (lrg_asset_entry_has_meta_file (e));
	g_assert_cmpstr (lrg_asset_meta_get_importer (lrg_asset_entry_get_meta (e)), ==, "texture");
	g_assert_false (g_file_test (png_meta, G_FILE_TEST_EXISTS));

	/* Editor database: sidecars appear with the same guid. */
	rw = make_db (f, TRUE);
	g_assert_true (g_file_test (png_meta, G_FILE_TEST_IS_REGULAR));
	e = lrg_asset_database_lookup_path (rw, png);
	g_assert_true (lrg_asset_entry_has_meta_file (e));
	g_assert_cmpstr (lrg_asset_entry_get_guid (e), ==, legacy);

	/* Sidecars are not assets, and a fresh scan reads the guid back. */
	again = make_db (f, FALSE);
	g_assert_cmpuint (lrg_asset_database_get_count (again), ==, 2);
	e = lrg_asset_database_lookup_guid (again, legacy);
	g_assert_nonnull (e);
	g_assert_cmpstr (lrg_asset_entry_get_path (e), ==, "sprites" G_DIR_SEPARATOR_S "p.png");
	g_assert_true (lrg_asset_entry_has_meta_file (e));
	g_assert_null (lrg_asset_database_lookup_guid (again, "nope"));
	g_assert_cmpuint (lrg_asset_database_get_orphaned_metas (again)->len, ==, 0);
	{
		g_autofree gchar *legacy2 = lrg_asset_meta_compute_legacy_guid ("sprites/p.png");

		g_assert_cmpstr (legacy2, ==, legacy);
	}
}

static void
test_rename_move_duplicate (Fixture       *f,
                            gconstpointer  data)
{
	g_autofree gchar           *png = write_file (f->assets, "sprites/p.png", "pixels");
	g_autofree gchar           *other = g_build_filename (f->assets, "other", NULL);
	g_autoptr(LrgAssetDatabase) db = NULL;
	g_autoptr(GError)           error = NULL;
	g_autofree gchar           *guid = NULL;
	g_autofree gchar           *renamed = g_build_filename (f->assets, "sprites", "hero.png", NULL);
	g_autofree gchar           *moved = g_build_filename (other, "hero.png", NULL);
	g_autofree gchar           *moved_meta = lrg_asset_meta_path_for_asset (moved);
	LrgAssetEntry              *e;
	LrgAssetEntry              *dup;
	g_autofree gchar           *contents = NULL;

	g_assert_cmpint (g_mkdir_with_parents (other, 0755), ==, 0);

	/* write-meta off: the rename must still persist the in-memory
	 * sidecar so the guid survives the new path. */
	db = make_db (f, FALSE);
	e = lrg_asset_database_lookup_path (db, "sprites/p.png");
	guid = g_strdup (lrg_asset_entry_get_guid (e));
	lrg_asset_meta_set_texture_filter (lrg_asset_entry_get_meta (e), LRG_ASSET_TEXTURE_FILTER_POINT);

	g_assert_true (lrg_asset_database_rename (db, "sprites/p.png", "hero.png", &error));
	g_assert_no_error (error);
	g_assert_false (g_file_test (png, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (renamed, G_FILE_TEST_IS_REGULAR));
	e = lrg_asset_database_lookup_guid (db, guid);
	g_assert_nonnull (e);
	g_assert_cmpstr (lrg_asset_entry_get_name (e), ==, "hero.png");
	g_assert_cmpint (lrg_asset_meta_get_texture_filter (lrg_asset_entry_get_meta (e)), ==,
	                 LRG_ASSET_TEXTURE_FILTER_POINT);

	/* Bad names and existing targets are refused. */
	g_assert_false (lrg_asset_database_rename (db, "sprites/hero.png", "a/b.png", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME);
	g_clear_error (&error);
	g_assert_false (lrg_asset_database_rename (db, "sprites/hero.png", "x.png.meta", &error));
	g_clear_error (&error);
	g_free (write_file (f->assets, "sprites/taken.png", "t"));
	g_assert_false (lrg_asset_database_rename (db, "sprites/hero.png", "taken.png", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
	g_clear_error (&error);

	/* Move into another folder: guid and sidecar follow. */
	g_assert_true (lrg_asset_database_move (db, renamed, "other", &error));
	g_assert_no_error (error);
	g_assert_true (g_file_test (moved, G_FILE_TEST_IS_REGULAR));
	g_assert_true (g_file_test (moved_meta, G_FILE_TEST_IS_REGULAR));
	e = lrg_asset_database_lookup_guid (db, guid);
	g_assert_cmpstr (lrg_asset_entry_get_path (e), ==, "other" G_DIR_SEPARATOR_S "hero.png");

	/* Moving a folder carries its contents' sidecars. */
	g_assert_true (lrg_asset_database_move (db, "other", "sprites", &error));
	g_assert_no_error (error);
	e = lrg_asset_database_lookup_guid (db, guid);
	g_assert_nonnull (e);
	g_assert_cmpstr (lrg_asset_entry_get_path (e), ==,
	                 "sprites" G_DIR_SEPARATOR_S "other" G_DIR_SEPARATOR_S "hero.png");
	g_assert_false (lrg_asset_database_move (db, "sprites", "sprites/other", &error));
	g_clear_error (&error);

	/* Duplicate: new file, settings kept, fresh guid. */
	dup = lrg_asset_database_duplicate (db, "sprites/other/hero.png", &error);
	g_assert_no_error (error);
	g_assert_nonnull (dup);
	g_assert_cmpstr (lrg_asset_entry_get_name (dup), ==, "hero copy.png");
	g_assert_cmpstr (lrg_asset_entry_get_guid (dup), !=, guid);
	g_assert_true (g_uuid_string_is_valid (lrg_asset_entry_get_guid (dup)));
	g_assert_true (lrg_asset_entry_has_meta_file (dup));
	g_assert_cmpint (lrg_asset_meta_get_texture_filter (lrg_asset_entry_get_meta (dup)), ==,
	                 LRG_ASSET_TEXTURE_FILTER_POINT);
	g_assert_true (g_file_get_contents (lrg_asset_entry_get_full_path (dup), &contents, NULL, NULL));
	g_assert_cmpstr (contents, ==, "pixels");
	dup = lrg_asset_database_duplicate (db, "sprites/other/hero.png", &error);
	g_assert_cmpstr (lrg_asset_entry_get_name (dup), ==, "hero copy 2.png");
	/* The original is untouched. */
	g_assert_nonnull (lrg_asset_database_lookup_guid (db, guid));
	g_assert_null (lrg_asset_database_duplicate (db, "sprites", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
}

static void
test_orphans_and_guard (Fixture       *f,
                        gconstpointer  data)
{
	g_autofree gchar           *orphan = write_file (f->assets, "gone.png.meta", "guid: abc\n");
	g_autofree gchar           *hidden = write_file (f->assets, ".cache/x.png", "x");
	g_autofree gchar           *dotfile = write_file (f->assets, ".gitignore", "*.tmp\n");
	g_autofree gchar           *gitobj = write_file (f->assets, ".git/objects/ab", "blob");
	g_autofree gchar           *outside = write_file (f->dir, "outside.png", "x");
	g_autofree gchar           *ok = write_file (f->assets, "ok.png", "x");
	g_autofree gchar           *canon_orphan = g_canonicalize_filename (orphan, NULL);
	g_autoptr(LrgAssetDatabase) db = NULL;
	g_autofree gchar           *hidden_meta = lrg_asset_meta_path_for_asset (hidden);
	g_autofree gchar           *dot_meta = lrg_asset_meta_path_for_asset (dotfile);
	g_autofree gchar           *ok_meta = lrg_asset_meta_path_for_asset (ok);
	g_autofree gchar           *outside_meta = lrg_asset_meta_path_for_asset (outside);
	GPtrArray                  *orphans;
	guint                       i;

	db = make_db (f, TRUE);

	/* Orphans are reported, not listed. */
	orphans = lrg_asset_database_get_orphaned_metas (db);
	g_assert_cmpuint (orphans->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (orphans, 0), ==, canon_orphan);

	/* .git is skipped entirely; hidden files are listed but never get
	 * sidecars; normal assets do. */
	for (i = 0; i < lrg_asset_database_get_count (db); i++)
		g_assert_null (strstr (lrg_asset_entry_get_path (lrg_asset_database_get_entry (db, i)), ".git" G_DIR_SEPARATOR_S));
	g_assert_nonnull (lrg_asset_database_lookup_path (db, hidden));
	g_assert_false (g_file_test (hidden_meta, G_FILE_TEST_EXISTS));
	g_assert_false (g_file_test (dot_meta, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (ok_meta, G_FILE_TEST_IS_REGULAR));

	g_assert_false (lrg_asset_database_is_writable_path (db, outside_meta));
	g_assert_false (lrg_asset_database_is_writable_path (db, "/etc/passwd.meta"));
	g_assert_false (lrg_asset_database_is_writable_path (db, hidden_meta));
	g_assert_true (lrg_asset_database_is_writable_path (db, ok_meta));

	/* Deleting the asset outside the editor turns its sidecar into an
	 * orphan on the next scan. */
	g_unlink (ok);
	g_assert_true (lrg_asset_database_scan (db, NULL));
	g_assert_cmpuint (lrg_asset_database_get_orphaned_metas (db)->len, ==, 2);
}

static void
test_broken_and_duplicate_guid (Fixture       *f,
                                gconstpointer  data)
{
	g_autofree gchar           *a = write_file (f->assets, "a.png", "a");
	g_autofree gchar           *b = write_file (f->assets, "b.png", "b");
	g_autofree gchar           *c = write_file (f->assets, "c.png", "c");
	g_autofree gchar           *a_meta = write_file (f->assets, "a.png.meta", "guid: 11111111-1111-4111-8111-111111111111\n");
	g_autofree gchar           *b_meta = write_file (f->assets, "b.png.meta", "guid: 11111111-1111-4111-8111-111111111111\n");
	g_autofree gchar           *c_meta = write_file (f->assets, "c.png.meta", "guid: [unclosed\n");
	g_autoptr(LrgAssetDatabase) db = NULL;
	g_autofree gchar           *c_text = NULL;
	LrgAssetEntry              *ea, *eb;

	g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*unreadable asset sidecar*");
	db = make_db (f, TRUE);
	g_test_assert_expected_messages ();

	/* A copied sidecar's guid is replaced on the later entry only. */
	ea = lrg_asset_database_lookup_path (db, "a.png");
	eb = lrg_asset_database_lookup_path (db, "b.png");
	g_assert_cmpstr (lrg_asset_entry_get_guid (ea), ==, "11111111-1111-4111-8111-111111111111");
	g_assert_cmpstr (lrg_asset_entry_get_guid (eb), !=, lrg_asset_entry_get_guid (ea));
	g_assert_true (lrg_asset_database_lookup_guid (db, lrg_asset_entry_get_guid (eb)) == eb);

	/* A broken sidecar is never overwritten. */
	g_assert_true (g_file_get_contents (c_meta, &c_text, NULL, NULL));
	g_assert_cmpstr (c_text, ==, "guid: [unclosed\n");
	(void) a; (void) b; (void) c; (void) a_meta; (void) b_meta;
}

static void
test_trash (Fixture       *f,
            gconstpointer  data)
{
	g_autofree gchar           *png = write_file (f->assets, "t.png", "x");
	g_autofree gchar           *meta = lrg_asset_meta_path_for_asset (png);
	g_autoptr(LrgAssetDatabase) db = NULL;
	g_autoptr(GError)           error = NULL;

	db = make_db (f, TRUE);
	g_assert_true (g_file_test (meta, G_FILE_TEST_IS_REGULAR));
	if (!lrg_asset_database_trash (db, "t.png", &error))
	{
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
		{
			g_test_skip ("No trash on this file system");
			return;
		}
		g_assert_no_error (error);
	}
	g_assert_false (g_file_test (png, G_FILE_TEST_EXISTS));
	g_assert_false (g_file_test (meta, G_FILE_TEST_EXISTS));
	g_assert_cmpuint (lrg_asset_database_get_count (db), ==, 0);
	g_assert_cmpuint (lrg_asset_database_get_orphaned_metas (db)->len, ==, 0);
}

int
main (int   argc,
      char *argv[])
{
	g_autofree gchar *data_home = NULL;
	int               status;

	/* Before any GLib call caches them: isolate HOME (trash device check)
	 * and XDG_DATA_HOME (trash location) inside a private temp root. */
	test_root = g_dir_make_tmp ("lrg-asset-meta-XXXXXX", NULL);
	g_assert_nonnull (test_root);
	data_home = g_build_filename (test_root, "data", NULL);
	g_mkdir_with_parents (data_home, 0700);
	g_setenv ("HOME", test_root, TRUE);
	g_setenv ("XDG_DATA_HOME", data_home, TRUE);

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/asset-meta/new", test_meta_new);
	g_test_add_func ("/asset-meta/unknown-keys", test_meta_unknown_keys);
	g_test_add_func ("/asset-meta/variant", test_meta_variant);
	g_test_add_func ("/asset-meta/settings-hash", test_meta_settings_hash);
	g_test_add_func ("/asset-meta/codepoints", test_meta_codepoints);
#define ADD(path, fn) g_test_add (path, Fixture, NULL, fixture_setup, fn, fixture_teardown)
	ADD ("/asset-meta/round-trip", test_meta_round_trip);
	ADD ("/asset-meta/guid-migration", test_guid_migration);
	ADD ("/asset-meta/rename-move-duplicate", test_rename_move_duplicate);
	ADD ("/asset-meta/orphans-and-guard", test_orphans_and_guard);
	ADD ("/asset-meta/broken-and-duplicate-guid", test_broken_and_duplicate_guid);
	ADD ("/asset-meta/trash", test_trash);
#undef ADD

	status = g_test_run ();
	remove_tree (test_root);
	g_free (test_root);
	return status;
}
