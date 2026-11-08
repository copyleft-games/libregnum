/* test-runtime-import-settings.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Runtime guid resolution and runtime import settings (0.9.0, M1):
 *
 *  - the guid index of LrgAssetManager: sidecar guids, legacy guids for
 *    files without a sidecar, version-control dirs skipped, duplicate
 *    guids (first wins), project.ryaml asset_dirs, guid: names in every
 *    loader, reverse lookup, the cached index file and its self-healing
 *    rebuild, and (editor builds) agreement with LrgAssetDatabase;
 *  - levels whose reference-stored prefab instances name their source as
 *    `guid:<uuid>` (through the serializer's asset manager and through
 *    project-root discovery);
 *  - import settings: the policy switch, the sidecar reader, and with a
 *    GL context (run under xrun.sh) texture size limit / mipmaps / filter,
 *    model scale / up axis / generated normals and font size / code
 *    points; audio mode / volume / loop when an audio device opens.
 *
 * Headless parts run on a BUILD_EDITOR=0 library.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <raylib.h>
#include <libregnum.h>

#define GUID_TEX   "11111111-2222-4333-8444-555555555555"
#define GUID_MODEL "22222222-3333-4444-8555-666666666666"
#define GUID_DUP   "33333333-4444-4555-8666-777777777777"
#define GUID_FONT  "44444444-5555-4666-8777-888888888888"
#define GUID_SOUND "55555555-6666-4777-8888-999999999999"
#define GUID_MUSIC "66666666-7777-4888-8999-aaaaaaaaaaaa"
#define GUID_CRATE "77777777-8888-4999-8aaa-bbbbbbbbbbbb"

/* ==========================================================================
 * Fixture: a project
 *
 *   project.ryaml           asset_dirs: [assets]
 *   assets/
 *     tex.png  (+ .meta)    8x4, filter point, mipmaps, max-size 4
 *     models/cube.obj (+.meta)  scale 2, up-axis z, generate-normals
 *     plain.txt             no sidecar: legacy guid
 *     a_dup.txt (+.meta)    guid GUID_DUP
 *     b_dup.txt (+.meta)    guid GUID_DUP (loses: sorted after a_dup)
 *     .git/HEAD             never indexed
 *     crate.rprefab (+.meta)
 *     sfx.wav (+.meta)      mode stream, volume 0.5, loop
 *     click.wav (+.meta)    mode sample, volume 0.25
 *   other/outside.txt       not in an asset dir
 * ========================================================================== */

typedef struct
{
	gchar *root;
	gchar *assets;
} Fixture;

static void
write_file (const gchar *path,
            const gchar *text)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = g_path_get_dirname (path);

	g_assert_cmpint (g_mkdir_with_parents (dir, 0755), ==, 0);
	g_assert_true (g_file_set_contents (path, text, -1, &error));
	g_assert_no_error (error);
}

static void
write_meta (const gchar *asset,
            const gchar *guid,
            const gchar *importer,
            const gchar *settings)
{
	g_autofree gchar *path = g_strconcat (asset, ".meta", NULL);
	g_autofree gchar *text = g_strdup_printf ("version: 1\nguid: %s\nimporter: %s\n%s%s",
	                                          guid, importer,
	                                          settings != NULL ? "settings:\n" : "",
	                                          settings != NULL ? settings : "");

	write_file (path, text);
}

/* A 16-bit mono PCM WAV with @n silent samples. */
static void
write_wav (const gchar *path,
           guint        n)
{
	guint8   header[44];
	guint32  data_size = n * 2;
	guint32  riff_size = 36 + data_size;
	guint32  rate = 22050;
	guint32  byte_rate = rate * 2;
	g_autofree guint8 *buf = g_malloc0 (44 + data_size);
	g_autoptr(GError) error = NULL;

	memcpy (header, "RIFF", 4);
	header[4] = (guint8) (riff_size & 0xff);
	header[5] = (guint8) ((riff_size >> 8) & 0xff);
	header[6] = (guint8) ((riff_size >> 16) & 0xff);
	header[7] = (guint8) ((riff_size >> 24) & 0xff);
	memcpy (header + 8, "WAVEfmt ", 8);
	header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
	header[20] = 1; header[21] = 0;           /* PCM */
	header[22] = 1; header[23] = 0;           /* mono */
	header[24] = (guint8) (rate & 0xff);
	header[25] = (guint8) ((rate >> 8) & 0xff);
	header[26] = 0; header[27] = 0;
	header[28] = (guint8) (byte_rate & 0xff);
	header[29] = (guint8) ((byte_rate >> 8) & 0xff);
	header[30] = (guint8) ((byte_rate >> 16) & 0xff);
	header[31] = 0;
	header[32] = 2; header[33] = 0;           /* block align */
	header[34] = 16; header[35] = 0;          /* bits */
	memcpy (header + 36, "data", 4);
	header[40] = (guint8) (data_size & 0xff);
	header[41] = (guint8) ((data_size >> 8) & 0xff);
	header[42] = 0; header[43] = 0;
	memcpy (buf, header, 44);
	g_assert_true (g_file_set_contents (path, (const gchar *) buf, 44 + data_size, &error));
	g_assert_no_error (error);
}

/* A unit cube without normals (OBJ). */
static const gchar *cube_obj =
	"v -0.5 -0.5 -0.5\nv 0.5 -0.5 -0.5\nv 0.5 0.5 -0.5\nv -0.5 0.5 -0.5\n"
	"v -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv 0.5 0.5 0.5\nv -0.5 0.5 0.5\n"
	"f 1 3 2\nf 1 4 3\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\n"
	"f 4 7 3\nf 4 8 7\nf 1 5 8\nf 1 8 4\nf 2 3 7\nf 2 7 6\n";

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *p = NULL;

	f->root = g_dir_make_tmp ("lrg-import-settings-XXXXXX", &error);
	g_assert_no_error (error);
	f->assets = g_build_filename (f->root, "assets", NULL);

	p = g_build_filename (f->root, "project.ryaml", NULL);
	write_file (p, "project:\n  name: import-test\n  asset_dirs:\n  - assets\n");
	g_free (p);

	/* Texture (written as a real PNG when a GL test runs; a placeholder
	 * is enough for the index). */
	p = g_build_filename (f->assets, "tex.png", NULL);
	write_file (p, "not-a-png-yet");
	write_meta (p, GUID_TEX, "texture", "  filter: point\n  mipmaps: true\n  max-size: 4\n  wrap: clamp\n");
	g_free (p);

	p = g_build_filename (f->assets, "models", "cube.obj", NULL);
	write_file (p, cube_obj);
	write_meta (p, GUID_MODEL, "model", "  scale: 2\n  up-axis: z\n  generate-normals: true\n");
	g_free (p);

	p = g_build_filename (f->assets, "plain.txt", NULL);
	write_file (p, "no sidecar");
	g_free (p);

	p = g_build_filename (f->assets, "a_dup.txt", NULL);
	write_file (p, "first");
	write_meta (p, GUID_DUP, "generic", NULL);
	g_free (p);
	p = g_build_filename (f->assets, "b_dup.txt", NULL);
	write_file (p, "second");
	write_meta (p, GUID_DUP, "generic", NULL);
	g_free (p);

	p = g_build_filename (f->assets, ".git", "HEAD", NULL);
	write_file (p, "ref: refs/heads/main\n");
	g_free (p);

	p = g_build_filename (f->assets, "sfx.wav", NULL);
	write_file (p, "");
	write_wav (p, 2205);
	write_meta (p, GUID_SOUND, "audio", "  mode: stream\n  volume: 0.5\n  loop: true\n");
	g_free (p);
	p = g_build_filename (f->assets, "click.wav", NULL);
	write_file (p, "");
	write_wav (p, 441);
	write_meta (p, GUID_MUSIC, "audio", "  mode: sample\n  volume: 0.25\n");
	g_free (p);

	p = g_build_filename (f->root, "other", "outside.txt", NULL);
	write_file (p, "outside");
}

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
			g_remove (p);
	}
	g_rmdir (dir);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	remove_tree (f->root);
	g_free (f->root);
	g_free (f->assets);
}

static gchar *
asset (Fixture     *f,
       const gchar *rel)
{
	g_autofree gchar *p = g_build_filename (f->assets, rel, NULL);

	return g_canonicalize_filename (p, NULL);
}

static LrgAssetManager *
project_manager (Fixture *f)
{
	LrgAssetManager *manager = lrg_asset_manager_new ();

	lrg_asset_manager_set_project_root (manager, f->root);
	return manager;
}

/* ==========================================================================
 * Guid index
 * ========================================================================== */

static void
test_guid_index (Fixture       *f,
                 gconstpointer  data)
{
	g_autoptr(LrgAssetManager) manager = project_manager (f);
	g_autofree gchar          *tex = asset (f, "tex.png");
	g_autofree gchar          *a_dup = asset (f, "a_dup.txt");
	g_autofree gchar          *plain = asset (f, "plain.txt");
	g_autofree gchar          *legacy = lrg_asset_meta_compute_legacy_guid ("plain.txt");
	g_autofree gchar          *got = NULL;
	g_autofree gchar          *back = NULL;
	const GPtrArray           *dirs;

	/* The manifest's asset_dirs, resolved against the root. */
	dirs = lrg_asset_manager_get_asset_dirs (manager);
	g_assert_cmpuint (dirs->len, ==, 1);
	g_assert_true (g_str_has_suffix (g_ptr_array_index (dirs, 0), "assets"));
	g_assert_cmpstr (lrg_asset_manager_get_project_root (manager), !=, NULL);
	g_assert_true (lrg_asset_manager_get_apply_import_settings (manager));

	/* Lazily built; prefix optional. */
	got = lrg_asset_manager_resolve_guid (manager, GUID_TEX);
	g_assert_cmpstr (got, ==, tex);
	g_clear_pointer (&got, g_free);
	got = lrg_asset_manager_resolve_guid (manager, "guid:" GUID_TEX);
	g_assert_cmpstr (got, ==, tex);
	g_clear_pointer (&got, g_free);
	got = lrg_asset_manager_resolve_reference (manager, "guid:" GUID_TEX);
	g_assert_cmpstr (got, ==, tex);
	g_clear_pointer (&got, g_free);

	/* Files without a sidecar have the legacy guid. */
	got = lrg_asset_manager_resolve_guid (manager, legacy);
	g_assert_cmpstr (got, ==, plain);
	g_clear_pointer (&got, g_free);

	/* Duplicate guid: the first file (sorted) wins. */
	got = lrg_asset_manager_resolve_guid (manager, GUID_DUP);
	g_assert_cmpstr (got, ==, a_dup);
	g_clear_pointer (&got, g_free);

	/* Version control and outside files are not indexed; unknown guid. */
	{
		g_autofree gchar *head = lrg_asset_meta_compute_legacy_guid (".git/HEAD");

		g_assert_null (lrg_asset_manager_resolve_guid (manager, head));
		g_assert_null (lrg_asset_manager_resolve_guid (manager, "guid:00000000-0000-4000-8000-000000000000"));
		g_assert_null (lrg_asset_manager_resolve_guid (manager, "guid:"));
	}
	/* 9 assets: tex, cube, plain, a_dup, b_dup (its own path entry
	 * loses the guid), sfx, click -> 6 distinct guids from sidecars and
	 * one legacy. */
	g_assert_cmpuint (lrg_asset_manager_get_guid_index_size (manager), ==, 6);

	/* Reverse lookup, by name relative to a search path (the root). */
	back = lrg_asset_manager_lookup_guid (manager, "assets/tex.png");
	g_assert_cmpstr (back, ==, GUID_TEX);
	g_clear_pointer (&back, g_free);
	back = lrg_asset_manager_lookup_guid (manager, "guid:" GUID_MODEL);
	g_assert_cmpstr (back, ==, GUID_MODEL);
	g_clear_pointer (&back, g_free);
	g_assert_null (lrg_asset_manager_lookup_guid (manager, "other/outside.txt"));

	/* Loader names: guid: references resolve through the index. */
	{
		g_autofree gchar *path = lrg_asset_manager_resolve_path (manager, "guid:" GUID_MODEL);
		g_autofree gchar *cube = asset (f, "models/cube.obj");

		g_assert_cmpstr (path, ==, cube);
	}

	/* A new asset is found after an explicit rebuild. */
	{
		g_autofree gchar *p = g_build_filename (f->assets, "late.txt", NULL);
		g_autofree gchar *late = NULL;

		write_file (p, "late");
		write_meta (p, "88888888-9999-4aaa-8bbb-cccccccccccc", "generic", NULL);
		g_assert_cmpuint (lrg_asset_manager_rebuild_guid_index (manager), ==, 7);
		late = lrg_asset_manager_resolve_guid (manager, "88888888-9999-4aaa-8bbb-cccccccccccc");
		g_assert_nonnull (late);
	}

	/* No asset dirs and no project: the search paths are indexed. */
	{
		g_autoptr(LrgAssetManager) plain_mgr = lrg_asset_manager_new ();
		g_autofree gchar          *found = NULL;

		lrg_asset_manager_add_search_path (plain_mgr, f->assets);
		found = lrg_asset_manager_resolve_guid (plain_mgr, GUID_TEX);
		g_assert_cmpstr (found, ==, tex);
		lrg_asset_manager_clear_asset_dirs (plain_mgr);
	}

	/* Forgetting the project drops the root search path and dirs. */
	lrg_asset_manager_set_project_root (manager, NULL);
	g_assert_cmpuint (lrg_asset_manager_get_asset_dirs (manager)->len, ==, 0);
	g_assert_null (lrg_asset_manager_get_project_root (manager));
}

static void
test_guid_index_cache (Fixture       *f,
                       gconstpointer  data)
{
	g_autoptr(LrgAssetManager) writer = project_manager (f);
	g_autoptr(LrgAssetManager) reader = project_manager (f);
	g_autofree gchar          *index = g_build_filename (f->root, "guids.yaml", NULL);
	g_autofree gchar          *text = NULL;
	g_autofree gchar          *got = NULL;
	g_autoptr(GError)          error = NULL;

	g_assert_true (lrg_asset_manager_save_guid_index (writer, index, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_get_contents (index, &text, NULL, NULL));
	g_assert_nonnull (strstr (text, "version: 1"));
	g_assert_nonnull (strstr (text, GUID_TEX ": assets/tex.png"));

	g_assert_true (lrg_asset_manager_load_guid_index (reader, index, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_asset_manager_get_guid_index_size (reader), ==, 6);
	got = lrg_asset_manager_resolve_guid (reader, GUID_TEX);
	g_assert_nonnull (got);
	g_clear_pointer (&got, g_free);

	/* Stale cache: the asset moved (sidecar with it). The lookup heals
	 * itself with one rebuild. */
	{
		g_autofree gchar *old = asset (f, "tex.png");
		g_autofree gchar *old_meta = g_strconcat (old, ".meta", NULL);
		g_autofree gchar *moved = g_build_filename (f->assets, "moved.png", NULL);
		g_autofree gchar *moved_meta = g_strconcat (moved, ".meta", NULL);
		g_autofree gchar *canon = g_canonicalize_filename (moved, NULL);

		g_assert_cmpint (g_rename (old, moved), ==, 0);
		g_assert_cmpint (g_rename (old_meta, moved_meta), ==, 0);
		got = lrg_asset_manager_resolve_guid (reader, GUID_TEX);
		g_assert_cmpstr (got, ==, canon);
	}

	/* Broken index files are errors. */
	{
		g_autofree gchar *bad = g_build_filename (f->root, "bad.yaml", NULL);

		write_file (bad, "- 1\n- 2\n");
		g_assert_false (lrg_asset_manager_load_guid_index (reader, bad, &error));
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	}
}

#ifdef LRG_BUILD_EDITOR
/* The editor's asset database and the runtime index agree. */
static void
test_guid_index_matches_editor (Fixture       *f,
                                gconstpointer  data)
{
	g_autoptr(LrgAssetManager)  manager = project_manager (f);
	g_autoptr(LrgAssetDatabase) db = lrg_asset_database_new ();
	g_autoptr(GError)           error = NULL;
	GPtrArray                  *entries;
	guint                       i;
	guint                       checked = 0;

	lrg_asset_database_add_search_dir (db, f->assets);
	g_assert_true (lrg_asset_database_scan (db, &error));
	g_assert_no_error (error);

	entries = lrg_asset_database_get_entries (db);
	for (i = 0; i < entries->len; i++)
	{
		LrgAssetEntry    *e = g_ptr_array_index (entries, i);
		g_autofree gchar *runtime = lrg_asset_manager_resolve_guid (manager, lrg_asset_entry_get_guid (e));
		g_autofree gchar *editor = g_canonicalize_filename (lrg_asset_entry_get_full_path (e), NULL);

		/* The losing duplicate got a fresh in-memory guid in the editor
		 * (written on its next sidecar save); everything else agrees. */
		if (g_str_has_suffix (editor, "b_dup.txt"))
		{
			g_assert_null (runtime);
			continue;
		}
		g_assert_cmpstr (runtime, ==, editor);
		checked++;
	}
	g_assert_cmpuint (checked, ==, 6);
	g_assert_cmpuint (lrg_asset_database_classify ("x.PNG"), ==, lrg_asset_meta_classify ("x.png"));
}
#endif

/* ==========================================================================
 * guid: prefab sources in levels
 * ========================================================================== */

static void
test_level_guid_source (Fixture       *f,
                        gconstpointer  data)
{
	g_autofree gchar             *crate_path = g_build_filename (f->assets, "crate.rprefab", NULL);
	g_autofree gchar             *level_path = g_build_filename (f->root, "levels", "main.rlevel", NULL);
	g_autoptr(LrgNode)            crate = lrg_node_new ("Crate");
	g_autoptr(LrgNode)            lid = lrg_node_new ("Lid");
	g_autoptr(LrgNode)            prefab = NULL;
	g_autoptr(LrgLevel)           level = lrg_level_new ("main");
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(GError)             error = NULL;
	LrgNode                      *inst;

	lrg_node_add_child (crate, lid);
	g_assert_true (lrg_prefab_save (crate, crate_path, &error));
	g_assert_no_error (error);
	write_meta (crate_path, GUID_CRATE, "prefab", NULL);

	/* The link names its source by guid only. */
	prefab = lrg_prefab_load (crate_path, &error);
	g_assert_no_error (error);
	inst = lrg_prefab_instantiate_linked (prefab, "guid:" GUID_CRATE, GUID_CRATE);
	lrg_level_add_node (level, inst, NULL);
	g_object_unref (inst);
	write_file (level_path, "");
	lrg_level_serializer_set_prefab_storage (ser, LRG_PREFAB_STORAGE_REFERENCE);
	g_assert_true (lrg_level_serializer_save_to_file (ser, level, level_path, &error));
	g_assert_no_error (error);

	/* 1. Through project discovery (project.ryaml above levels/). */
	{
		g_autoptr(LrgLevelSerializer) loader = lrg_level_serializer_new ();
		g_autoptr(LrgLevel)           back = lrg_level_serializer_load_from_file (loader, level_path, &error);
		LrgNode                      *root;

		g_assert_no_error (error);
		root = g_ptr_array_index (lrg_node_get_children (lrg_level_get_root (back)), 0);
		g_assert_nonnull (lrg_node_find_by_path (root, "Lid"));
	}

	/* 2. Through an explicit asset manager, loading from data. */
	{
		g_autoptr(LrgLevelSerializer) loader = lrg_level_serializer_new ();
		g_autoptr(LrgAssetManager)    manager = project_manager (f);
		g_autofree gchar             *text = NULL;
		g_autoptr(LrgLevel)           back = NULL;
		LrgNode                      *root;

		lrg_level_serializer_set_asset_manager (loader, manager);
		g_assert_true (lrg_level_serializer_get_asset_manager (loader) == manager);
		g_assert_true (g_file_get_contents (level_path, &text, NULL, NULL));
		back = lrg_level_serializer_load_from_data (loader, text, -1, &error);
		g_assert_no_error (error);
		root = g_ptr_array_index (lrg_node_get_children (lrg_level_get_root (back)), 0);
		g_assert_nonnull (lrg_node_find_by_path (root, "Lid"));
	}
}

/* ==========================================================================
 * Import settings (headless parts)
 * ========================================================================== */

static void
test_settings_headless (Fixture       *f,
                        gconstpointer  data)
{
	g_autoptr(LrgAssetManager) manager = project_manager (f);
	g_autoptr(LrgAssetMeta)    meta = NULL;
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *p = NULL;
	gboolean                   apply = FALSE;

	/* The sidecar reader is reachable through the manager by guid. */
	meta = lrg_asset_manager_get_asset_meta (manager, "guid:" GUID_MODEL);
	g_assert_nonnull (meta);
	g_assert_cmpfloat_with_epsilon (lrg_asset_meta_get_model_scale (meta), 2.0, 1e-9);
	g_assert_cmpint (lrg_asset_meta_get_model_up_axis (meta), ==, LRG_ASSET_UP_AXIS_Z);
	g_assert_null (lrg_asset_manager_get_asset_meta (manager, "assets/plain.txt"));
	g_assert_null (lrg_asset_manager_get_asset_meta (manager, "guid:00000000-0000-4000-8000-000000000000"));

	/* Policy switch: property and project.ryaml key. */
	g_object_set (manager, "apply-import-settings", FALSE, NULL);
	g_object_get (manager, "apply-import-settings", &apply, NULL);
	g_assert_false (apply);
	lrg_asset_manager_set_apply_import_settings (manager, TRUE);
	g_assert_true (lrg_asset_manager_get_apply_import_settings (manager));

	p = g_build_filename (f->root, "project.ryaml", NULL);
	write_file (p, "project:\n  name: t\n  apply_import_settings: false\n  asset_dirs:\n  - assets\n");
	{
		g_autoptr(LrgAssetManager) off = project_manager (f);

		g_assert_false (lrg_asset_manager_get_apply_import_settings (off));
	}

	/* Missing guid references fail like missing paths, naming the guid. */
	g_assert_null (lrg_asset_manager_load_texture (manager, "guid:00000000-0000-4000-8000-000000000000", &error));
	g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
	g_assert_nonnull (strstr (error->message, "guid:"));
}

/* ==========================================================================
 * Import settings with a GL context (xrun.sh)
 * ========================================================================== */

static GrlWindow *
open_window (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");
	GrlWindow   *window;

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return NULL;
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	window = grl_window_new (64, 64, "test-runtime-import-settings");
	if (window == NULL || !grl_window_is_ready (window))
	{
		g_clear_object (&window);
		return NULL;
	}
	return window;
}

static void
test_settings_gpu (Fixture       *f,
                   gconstpointer  data)
{
	g_autoptr(GrlWindow)       window = open_window ();
	g_autoptr(LrgAssetManager) manager = NULL;
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *tex_path = NULL;
	GrlTexture                *tex;
	GrlModel                  *model;

	if (window == NULL)
	{
		g_test_skip ("No display / GL context (run under xrun.sh)");
		return;
	}
	manager = project_manager (f);

	/* --- Texture: 8x4 PNG, max-size 4 -> 4x2, mipmaps, point filter. */
	tex_path = asset (f, "tex.png");
	{
		g_autoptr(GrlColor) red = grl_color_new (255, 0, 0, 255);
		g_autoptr(GrlImage) image = grl_image_new_color (8, 4, red);

		g_assert_true (grl_image_export (image, tex_path));
	}
	tex = lrg_asset_manager_load_texture (manager, "guid:" GUID_TEX, &error);
	g_assert_no_error (error);
	g_assert_nonnull (tex);
	g_assert_cmpint (grl_texture_get_width (tex), ==, 4);
	g_assert_cmpint (grl_texture_get_height (tex), ==, 2);
	g_assert_cmpint (grl_texture_get_mipmaps (tex), >, 1);
	/* One cache entry for the guid and the path spelling. */
	g_assert_true (lrg_asset_manager_load_texture (manager, tex_path, &error) == tex);
	g_assert_true (lrg_asset_manager_is_cached (manager, "guid:" GUID_TEX));

	/* Policy off: the same file loads untouched. */
	{
		g_autoptr(LrgAssetManager) raw = project_manager (f);
		GrlTexture                *t2;

		lrg_asset_manager_set_apply_import_settings (raw, FALSE);
		t2 = lrg_asset_manager_load_texture (raw, "guid:" GUID_TEX, &error);
		g_assert_no_error (error);
		g_assert_cmpint (grl_texture_get_width (t2), ==, 8);
		g_assert_cmpint (grl_texture_get_mipmaps (t2), ==, 1);
		lrg_asset_manager_unload_all (raw);
	}

	/* --- Model: scale 2, Z-up turned Y-up, normals generated. */
	model = lrg_asset_manager_load_model (manager, "guid:" GUID_MODEL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (model);
	{
		g_autoptr(GrlMatrix) m = grl_model_get_transform (model);
		Model               *handle = grl_model_get_handle (model);
		gint                 v;

		g_assert_cmpfloat_with_epsilon (m->m0, 2.0f, 1e-4);
		g_assert_cmpfloat_with_epsilon (m->m5, 0.0f, 1e-4);
		/* +Z of the file maps to +Y: row 1, column 2. */
		g_assert_cmpfloat_with_epsilon (m->m9, 2.0f, 1e-4);
		g_assert_cmpfloat_with_epsilon (m->m6, -2.0f, 1e-4);

		g_assert_cmpint (handle->meshCount, >=, 1);
		g_assert_nonnull (handle->meshes[0].normals);
		for (v = 0; v < handle->meshes[0].vertexCount; v++)
		{
			gfloat *n = &handle->meshes[0].normals[v * 3];
			gfloat  len = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];

			g_assert_cmpfloat_with_epsilon (len, 1.0f, 1e-3);
		}
		g_assert_cmpuint (handle->meshes[0].vboId[2], !=, 0);
	}

	/* --- Font: sidecar size and code points (the bundled font). */
	{
		const gchar *src = g_getenv ("LRG_TEST_FONT");
		g_autofree gchar *font_path = g_build_filename (f->assets, "ui.ttf", NULL);
		g_autofree gchar *contents = NULL;
		gsize             len = 0;
		GrlFont          *font;

		if (src == NULL)
			src = "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf";
		if (g_file_get_contents (src, &contents, &len, NULL))
		{
			g_assert_true (g_file_set_contents (font_path, contents, (gssize) len, NULL));
			write_meta (font_path, GUID_FONT, "font", "  size: 20\n  codepoints: 65-70\n");
			lrg_asset_manager_rebuild_guid_index (manager);
			font = GRL_FONT (lrg_asset_manager_load_asset (manager, "guid:" GUID_FONT, &error));
			g_assert_no_error (error);
			g_assert_cmpint (grl_font_get_base_size (font), ==, 20);
			g_assert_cmpint (grl_font_get_glyph_count (font), ==, 6);
		}
		else
			g_test_message ("no TTF found; font settings not checked");
	}

	lrg_asset_manager_unload_all (manager);
}

static void
test_settings_audio (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(LrgAssetManager) manager = project_manager (f);
	g_autoptr(GError)          error = NULL;
	GrlAudioDevice            *device = grl_audio_device_get_default ();
	GObject                   *stream;
	GObject                   *sample;

	if (device == NULL || !grl_audio_device_is_ready (device))
	{
		g_test_skip ("No audio device");
		return;
	}

	/* sfx.wav says stream: music despite the .wav; click.wav says sample. */
	stream = lrg_asset_manager_load_audio (manager, "guid:" GUID_SOUND, &error);
	g_assert_no_error (error);
	g_assert_true (GRL_IS_MUSIC (stream));
	g_assert_cmpfloat_with_epsilon (grl_music_get_volume (GRL_MUSIC (stream)), 0.5f, 1e-4);
	g_assert_true (grl_music_get_looping (GRL_MUSIC (stream)));

	sample = lrg_asset_manager_load_asset (manager, "guid:" GUID_MUSIC, &error);
	g_assert_no_error (error);
	g_assert_true (GRL_IS_SOUND (sample));
	g_assert_cmpfloat_with_epsilon (grl_sound_get_volume (GRL_SOUND (sample)), 0.25f, 1e-4);

	lrg_asset_manager_unload_all (manager);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/runtime-import/guid/index", Fixture, NULL,
	            fixture_setup, test_guid_index, fixture_teardown);
	g_test_add ("/runtime-import/guid/cache", Fixture, NULL,
	            fixture_setup, test_guid_index_cache, fixture_teardown);
#ifdef LRG_BUILD_EDITOR
	g_test_add ("/runtime-import/guid/matches-editor", Fixture, NULL,
	            fixture_setup, test_guid_index_matches_editor, fixture_teardown);
#endif
	g_test_add ("/runtime-import/guid/level-source", Fixture, NULL,
	            fixture_setup, test_level_guid_source, fixture_teardown);
	g_test_add ("/runtime-import/settings/headless", Fixture, NULL,
	            fixture_setup, test_settings_headless, fixture_teardown);
	g_test_add ("/runtime-import/settings/gpu", Fixture, NULL,
	            fixture_setup, test_settings_gpu, fixture_teardown);
	g_test_add ("/runtime-import/settings/audio", Fixture, NULL,
	            fixture_setup, test_settings_audio, fixture_teardown);

	return g_test_run ();
}
