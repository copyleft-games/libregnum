/* test-pack-writer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPackWriter round trip (rres, VFS, tar, zip), the reference index,
 * importers, project config and templates, and a plugin that does not
 * link libregnum.
 */

#include <libregnum.h>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const gchar plugin_src[] =
	"typedef struct {\n"
	"  void (*add_action) (void *, const char *, const char *, const char *);\n"
	"  void (*add_panel) (void *, const char *, const char *);\n"
	"  void (*add_inspector) (void *, const char *);\n"
	"} Api;\n"
	"void lrg_plugin_init (void *manager, const Api *api)\n"
	"{\n"
	"  api->add_action (manager, \"demo.hello\", \"Hello\", \"Demo\");\n"
	"  api->add_panel (manager, \"demo-panel\", \"Demo\");\n"
	"  api->add_inspector (manager, \"LrgNode\");\n"
	"}\n";

static gchar *
make_tmpdir (void)
{
	gchar *dir;
	g_autoptr(GError) error = NULL;

	dir = g_dir_make_tmp ("lrg-pack-XXXXXX", &error);
	g_assert_no_error (error);
	g_assert_nonnull (dir);
	return dir;
}

static void
rm_rf (const gchar *path)
{
	GDir *dir;
	const gchar *name;

	if (g_file_test (path, G_FILE_TEST_IS_DIR) &&
	    !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
	{
		dir = g_dir_open (path, 0, NULL);
		if (dir != NULL)
		{
			while ((name = g_dir_read_name (dir)) != NULL)
			{
				gchar *child;

				child = g_build_filename (path, name, NULL);
				rm_rf (child);
				g_free (child);
			}
			g_dir_close (dir);
		}
		g_rmdir (path);
		return;
	}
	g_remove (path);
}

static void
write_file (const gchar *path,
            const gchar *text)
{
	g_autofree gchar *parent = NULL;
	g_autoptr(GError) error = NULL;

	parent = g_path_get_dirname (path);
	g_assert_cmpint (g_mkdir_with_parents (parent, 0755), ==, 0);
	g_assert_true (g_file_set_contents (path, text, -1, &error));
	g_assert_no_error (error);
}

static gboolean
bytes_contain (const guint8 *data,
               gsize         size,
               const gchar  *needle)
{
	gsize n;
	gsize i;

	n = strlen (needle);
	if (size < n)
		return FALSE;
	for (i = 0; i + n <= size; i++)
		if (memcmp (data + i, needle, n) == 0)
			return TRUE;
	return FALSE;
}

static GBytes *
gunzip_file (const gchar *path)
{
	gchar *raw;
	gsize size;
	g_autoptr(GZlibDecompressor) dec = NULL;
	g_autoptr(GInputStream) mem = NULL;
	g_autoptr(GInputStream) conv = NULL;
	g_autoptr(GOutputStream) out = NULL;
	g_autoptr(GError) error = NULL;
	gssize n;

	g_assert_true (g_file_get_contents (path, &raw, &size, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (size, >=, 2);
	g_assert_cmpuint ((guint) (guint8) raw[0], ==, 0x1f);
	g_assert_cmpuint ((guint) (guint8) raw[1], ==, 0x8b);
	dec = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
	mem = g_memory_input_stream_new_from_data (raw, size, g_free);
	conv = g_converter_input_stream_new (mem, G_CONVERTER (dec));
	out = g_memory_output_stream_new_resizable ();
	n = g_output_stream_splice (out, conv,
	                            G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
	                            G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
	                            NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (n, >, 0);
	return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (out));
}

static gboolean
strv_has (GStrv        list,
          const gchar *needle)
{
	guint i;

	for (i = 0; list[i] != NULL; i++)
		if (g_str_equal (list[i], needle))
			return TRUE;
	return FALSE;
}

static void
test_rres_roundtrip (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(LrgPackWriter) writer = NULL;
	g_autoptr(LrgAssetPack) pack = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 hello[] = { 'h', 'e', 'l', 'l', 'o' };
	guint8 *raw;
	gsize size;

	dir = make_tmpdir ();
	path = g_build_filename (dir, "game.rres", NULL);
	writer = lrg_pack_writer_new ();
	lrg_pack_writer_set_cook (writer, FALSE);
	g_assert_true (lrg_pack_writer_add_bytes (writer, "notes/hello.txt", hello, sizeof hello, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_pack_writer_add_bytes (writer, "../nope.txt", hello, sizeof hello, &error));
	g_assert_nonnull (error);
	g_clear_error (&error);
	g_assert_true (lrg_pack_writer_write_rres (writer, path, &error));
	g_assert_no_error (error);

	pack = lrg_asset_pack_new (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (pack);
	g_assert_true (lrg_asset_pack_contains (pack, "notes/hello.txt"));
	raw = lrg_asset_pack_load_raw (pack, "notes/hello.txt", &size, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (size, ==, sizeof hello);
	g_assert_cmpint (memcmp (raw, hello, size), ==, 0);
	g_free (raw);
	rm_rf (dir);
}

static void
test_scan_skips_vcs (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *keep = NULL;
	g_autofree gchar *git = NULL;
	g_autofree gchar *link = NULL;
	g_autoptr(LrgPackWriter) writer = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) files = NULL;

	dir = make_tmpdir ();
	keep = g_build_filename (dir, "keep.txt", NULL);
	git = g_build_filename (dir, ".git", "config", NULL);
	link = g_build_filename (dir, "link.txt", NULL);
	write_file (keep, "kept\n");
	write_file (git, "git\n");
	g_assert_cmpint (symlink ("keep.txt", link), ==, 0);

	writer = lrg_pack_writer_new ();
	lrg_pack_writer_set_project (writer, dir);
	lrg_pack_writer_set_cook (writer, FALSE);
	g_assert_true (lrg_pack_writer_add_bytes (writer, "only-bytes.txt", (const guint8 *) "x", 1, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_pack_writer_scan (writer, &error));
	g_assert_no_error (error);
	files = lrg_pack_writer_get_files (writer);
	g_assert_true (strv_has (files, "keep.txt"));
	g_assert_false (strv_has (files, ".git/config"));
	g_assert_false (strv_has (files, "only-bytes.txt"));
	g_assert_false (strv_has (files, "link.txt"));
	g_assert_cmpuint (lrg_pack_writer_get_count (writer), ==, 1);
	rm_rf (dir);
}

static void
test_vfs_tar_zip (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *vfs = NULL;
	g_autofree gchar *tar_path = NULL;
	g_autofree gchar *zip_path = NULL;
	g_autofree gchar *index_path = NULL;
	g_autofree gchar *index = NULL;
	g_autoptr(LrgPackWriter) writer = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) tar = NULL;
	gchar *zip;
	gsize zip_size;
	const guint8 payload[] = { 'p', 'a', 'y' };

	dir = make_tmpdir ();
	vfs = g_build_filename (dir, "vfs", NULL);
	tar_path = g_build_filename (dir, "game.tar.gz", NULL);
	zip_path = g_build_filename (dir, "game.zip", NULL);
	writer = lrg_pack_writer_new ();
	lrg_pack_writer_set_cook (writer, FALSE);
	g_assert_true (lrg_pack_writer_add_bytes (writer, "assets/a.bin", payload, sizeof payload, &error));
	g_assert_no_error (error);

	g_assert_true (lrg_pack_writer_write_vfs (writer, vfs, &error));
	g_assert_no_error (error);
	index_path = g_build_filename (vfs, "index.yaml", NULL);
	g_assert_true (g_file_get_contents (index_path, &index, NULL, &error));
	g_assert_no_error (error);
	g_assert_nonnull (strstr (index, "sha256"));
	g_assert_nonnull (strstr (index, "assets/a.bin"));

	g_assert_true (lrg_pack_writer_export (writer, "linux-tar", tar_path, &error));
	g_assert_no_error (error);
	tar = gunzip_file (tar_path);
	g_assert_true (bytes_contain (g_bytes_get_data (tar, NULL), g_bytes_get_size (tar), "ustar"));

	g_assert_true (lrg_pack_writer_export (writer, "windows-zip", zip_path, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_get_contents (zip_path, &zip, &zip_size, &error));
	g_assert_no_error (error);
	g_assert_true (bytes_contain ((const guint8 *) zip, zip_size, "PK\x03\x04"));
	g_assert_true (bytes_contain ((const guint8 *) zip, zip_size, "PK\x05\x06"));
	g_free (zip);

	g_assert_false (lrg_pack_writer_export (writer, "nope", tar_path, &error));
	g_assert_error (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_UNSUPPORTED);
	rm_rf (dir);
}

static void
test_refs (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *png = NULL;
	g_autofree gchar *level = NULL;
	g_autofree gchar *moved = NULL;
	g_autofree gchar *text = NULL;
	g_autoptr(LrgAssetRefs) refs = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) who = NULL;

	dir = make_tmpdir ();
	png = g_build_filename (dir, "assets", "a.png", NULL);
	level = g_build_filename (dir, "levels", "main.rlevel", NULL);
	write_file (png, "png");
	write_file (level, "nodes:\n  - sprite: \"assets/a.png\"\n");
	refs = lrg_asset_refs_new ();
	g_assert_true (lrg_asset_refs_scan (refs, dir, &error));
	g_assert_no_error (error);
	who = lrg_asset_refs_get_referrers (refs, "assets/a.png");
	g_assert_true (strv_has (who, "levels/main.rlevel"));

	g_assert_true (lrg_asset_refs_rename (refs, "assets/a.png", "assets/b.png", &error));
	g_assert_no_error (error);
	moved = g_build_filename (dir, "assets", "b.png", NULL);
	g_assert_true (g_file_test (moved, G_FILE_TEST_IS_REGULAR));
	g_assert_false (g_file_test (png, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_get_contents (level, &text, NULL, &error));
	g_assert_no_error (error);
	g_assert_nonnull (strstr (text, "\"assets/b.png\""));

	g_assert_false (lrg_asset_refs_delete (refs, "assets/b.png", &error));
	g_assert_error (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_IN_USE);
	g_clear_error (&error);
	write_file (level, "nodes: []\n");
	g_assert_true (lrg_asset_refs_scan (refs, dir, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_asset_refs_delete (refs, "assets/b.png", &error));
	g_assert_no_error (error);
	g_assert_false (g_file_test (moved, G_FILE_TEST_EXISTS));
	rm_rf (dir);
}

static void
test_importer_and_classify (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *src = NULL;
	g_autofree gchar *dest = NULL;
	g_autofree gchar *copied = NULL;
	g_autoptr(LrgAssetImporterRegistry) reg = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) names = NULL;

	g_assert_cmpint (lrg_asset_meta_classify ("hero.ply"), ==, LRG_ASSET_TYPE_UNKNOWN);
	g_assert_cmpint (lrg_asset_meta_classify ("hero.fbx"), ==, LRG_ASSET_TYPE_MODEL);
	g_assert_cmpint (lrg_asset_meta_classify ("hero.obj"), ==, LRG_ASSET_TYPE_MODEL);
	g_assert_false (lrg_asset_importer_model_loadable ("hero.ply"));
	g_assert_false (lrg_asset_importer_model_loadable ("hero.fbx"));
	g_assert_true (lrg_asset_importer_model_loadable ("hero.obj"));

	reg = lrg_asset_importer_registry_new ();
	names = lrg_asset_importer_registry_names (reg);
	g_assert_true (strv_has (names, "copy"));
	g_assert_true (strv_has (names, "fbx"));
	g_assert_nonnull (lrg_asset_importer_registry_lookup (reg, "generic"));

	dir = make_tmpdir ();
	src = g_build_filename (dir, "in.bin", NULL);
	dest = g_build_filename (dir, "out", "in.bin", NULL);
	write_file (src, "bytes");
	g_assert_true (lrg_asset_importer_registry_import (reg, "copy", src, dest, NULL, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_get_contents (dest, &copied, NULL, &error));
	g_assert_cmpstr (copied, ==, "bytes");
	g_assert_false (lrg_asset_importer_registry_import (reg, "fbx", src, dest, NULL, &error));
	g_assert_error (error, LRG_ASSET_IMPORTER_ERROR, LRG_ASSET_IMPORTER_ERROR_FAILED);
	rm_rf (dir);
}

static void
test_project_config_and_template (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *manifest = NULL;
	g_autofree gchar *mod_yaml = NULL;
	g_autofree gchar *level = NULL;
	g_autoptr(LrgProjectConfig) cfg = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) loads = NULL;
	const LrgProjectTemplateInfo *info;

	g_assert_cmpuint (lrg_project_template_count (), ==, 2);
	info = lrg_project_template_get (0);
	g_assert_nonnull (info);
	g_assert_cmpstr (info->id, ==, "empty");
	g_assert_null (lrg_project_template_get (9));

	dir = make_tmpdir ();
	g_assert_true (lrg_project_template_instantiate ("mod", dir, "Demo", &error));
	g_assert_no_error (error);
	manifest = g_build_filename (dir, "project.ryaml", NULL);
	mod_yaml = g_build_filename (dir, "mods", "example", "mod.yaml", NULL);
	level = g_build_filename (dir, "levels", "main.rlevel", NULL);
	g_assert_true (g_file_test (manifest, G_FILE_TEST_IS_REGULAR));
	g_assert_true (g_file_test (mod_yaml, G_FILE_TEST_IS_REGULAR));
	g_assert_true (g_file_test (level, G_FILE_TEST_IS_REGULAR));
	g_assert_false (lrg_project_template_instantiate ("empty", dir, "Demo", &error));
	g_assert_error (error, LRG_PROJECT_TEMPLATE_ERROR, LRG_PROJECT_TEMPLATE_ERROR_EXISTS);
	g_clear_error (&error);

	write_file (manifest,
	            "autoloads:\n"
	            "  - scripts/boot.lua\n"
	            "settings:\n"
	            "  width: \"1280\"\n"
	            "platforms:\n"
	            "  linux:\n"
	            "    width: \"1920\"\n"
	            "steam:\n"
	            "  app-id: \"4242\"\n"
	            "  achievements: \"true\"\n");
	cfg = lrg_project_config_new_from_file (manifest, &error);
	g_assert_no_error (error);
	loads = lrg_project_config_get_autoloads (cfg);
	g_assert_cmpstr (loads[0], ==, "scripts/boot.lua");
	g_assert_cmpstr (lrg_project_config_get_platform_setting (cfg, NULL, "width"), ==, "1280");
	g_assert_cmpstr (lrg_project_config_get_platform_setting (cfg, "linux", "width"), ==, "1920");
	g_assert_cmpstr (lrg_project_config_get_platform_setting (cfg, "windows", "width"), ==, "1280");
	g_assert_cmpint (lrg_project_config_get_steam_app_id (cfg), ==, 4242);
	g_assert_true (lrg_project_config_get_steam_achievements (cfg));
	rm_rf (dir);
}

static void
test_plugin (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *src = NULL;
	g_autofree gchar *so = NULL;
	g_autofree gchar *yaml = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgEditorPluginManager) mgr = NULL;
	g_auto(GStrv) actions = NULL;
	g_auto(GStrv) panels = NULL;
	g_auto(GStrv) inspectors = NULL;
	g_autofree gchar *gcc = NULL;
	gchar *argv[7];
	gint status;

	gcc = g_find_program_in_path ("gcc");
	if (gcc == NULL)
	{
		g_test_skip ("gcc is not on PATH");
		return;
	}
	dir = make_tmpdir ();
	src = g_build_filename (dir, "plugin.c", NULL);
	so = g_build_filename (dir, "plugin.so", NULL);
	yaml = g_build_filename (dir, "plugin.yaml", NULL);
	write_file (src, plugin_src);
	write_file (yaml, "name: demo\nentry: plugin.so\n");
	argv[0] = (gchar *) "gcc";
	argv[1] = (gchar *) "-shared";
	argv[2] = (gchar *) "-fPIC";
	argv[3] = (gchar *) "-o";
	argv[4] = so;
	argv[5] = src;
	argv[6] = NULL;
	g_assert_true (g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
	                             NULL, NULL, NULL, NULL, &status, &error));
	g_assert_no_error (error);
	g_assert_true (g_spawn_check_wait_status (status, &error));
	g_assert_no_error (error);

	mgr = lrg_editor_plugin_manager_new ();
	g_assert_true (lrg_editor_plugin_manager_load (mgr, dir, &error));
	g_assert_no_error (error);
	actions = lrg_editor_plugin_manager_list_actions (mgr);
	panels = lrg_editor_plugin_manager_list_panels (mgr);
	inspectors = lrg_editor_plugin_manager_list_inspectors (mgr);
	g_assert_cmpstr (actions[0], ==, "demo.hello");
	g_assert_cmpstr (lrg_editor_plugin_manager_action_label (mgr, "demo.hello"), ==, "Hello");
	g_assert_cmpstr (panels[0], ==, "demo-panel");
	g_assert_cmpstr (inspectors[0], ==, "LrgNode");
	rm_rf (dir);
}

static void
test_export_ipc (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *file = NULL;
	g_autofree gchar *out = NULL;
	g_autofree gchar *req = NULL;
	g_autofree gchar *reply = NULL;
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgEditorServer) server = NULL;
	g_autoptr(LrgAssetPack) pack = NULL;
	g_autoptr(GError) error = NULL;

	dir = make_tmpdir ();
	file = g_build_filename (dir, "keep.txt", NULL);
	out = g_build_filename (dir, "out.rres", NULL);
	write_file (file, "kept\n");
	editor = lrg_editor_new ();
	server = lrg_editor_server_new (editor);
	lrg_editor_export_register (server, dir);
	reply = lrg_editor_server_dispatch (server, "{'id':1,'method':'export.profiles','params':{}}");
	g_assert_nonnull (strstr (reply, "linux-tar"));
	g_assert_nonnull (strstr (reply, "windows-zip"));
	g_free (reply);

	req = g_strdup_printf ("{'id':2,'method':'export.run','params':{'profile':'rres','output':'%s'}}", out);
	reply = lrg_editor_server_dispatch (server, req);
	g_assert_nonnull (strstr (reply, "\"ok\":true"));
	pack = lrg_asset_pack_new (out, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_asset_pack_contains (pack, "keep.txt"));
	rm_rf (dir);
}

static void
test_cook_dxt1 (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *png = NULL;
	g_autofree gchar *parent = NULL;
	g_autofree gchar *meta_path = NULL;
	g_autofree gchar *out = NULL;
	g_autofree guint8 *raw = NULL;
	g_autoptr(GrlColor) color = NULL;
	g_autoptr(GrlImage) image = NULL;
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autoptr(LrgPackWriter) writer = NULL;
	g_autoptr(LrgAssetPack) pack = NULL;
	g_autoptr(GError) error = NULL;
	gsize len;

	dir = make_tmpdir ();
	png = g_build_filename (dir, "assets", "red.png", NULL);
	parent = g_path_get_dirname (png);
	g_assert_cmpint (g_mkdir_with_parents (parent, 0755), ==, 0);
	color = grl_color_new (200, 10, 10, 255);
	image = grl_image_new_color (4, 4, color);
	g_assert_nonnull (image);
	g_assert_true (grl_image_export (image, png));
	meta = lrg_asset_meta_new ();
	lrg_asset_meta_set_setting_string (meta, "compression", "dxt1");
	meta_path = lrg_asset_meta_path_for_asset (png);
	g_assert_true (lrg_asset_meta_save_to_file (meta, meta_path, &error));
	g_assert_no_error (error);
	writer = lrg_pack_writer_new ();
	lrg_pack_writer_set_project (writer, dir);
	lrg_pack_writer_set_cook (writer, TRUE);
	g_assert_true (lrg_pack_writer_scan (writer, &error));
	g_assert_no_error (error);
	out = g_build_filename (dir, "out.rres", NULL);
	g_assert_true (lrg_pack_writer_write_rres (writer, out, &error));
	g_assert_no_error (error);
	g_assert_nonnull (strstr (lrg_pack_writer_get_log (writer), "cook: compressed texture dxt1"));
	pack = lrg_asset_pack_new (out, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_asset_pack_contains (pack, "assets/red.png"));
	len = 0;
	raw = lrg_asset_pack_load_raw (pack, "assets/red.png", &len, &error);
	g_assert_no_error (error);
	g_assert_nonnull (raw);
	g_assert_cmpuint (len, >=, 12);
	g_assert_cmpint (memcmp (raw, "DXT1", 4), ==, 0);
	rm_rf (dir);
}

static void
test_reimport (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *src = NULL;
	g_autofree gchar *dest = NULL;
	g_autofree gchar *stamp = NULL;
	g_autoptr(LrgAssetImporterRegistry) reg = NULL;
	g_autoptr(GError) error = NULL;
	GStatBuf st;
	struct timespec times[2];
	gboolean changed;

	dir = make_tmpdir ();
	src = g_build_filename (dir, "in.bin", NULL);
	dest = g_build_filename (dir, "out.bin", NULL);
	stamp = g_strconcat (dest, ".stamp", NULL);
	write_file (src, "one");
	reg = lrg_asset_importer_registry_new ();
	changed = FALSE;
	g_assert_true (lrg_asset_importer_registry_reimport_if_changed (reg, "copy", src, dest, NULL,
	                                                                &changed, &error));
	g_assert_no_error (error);
	g_assert_true (changed);
	g_assert_true (g_file_test (stamp, G_FILE_TEST_IS_REGULAR));
	changed = TRUE;
	g_assert_true (lrg_asset_importer_registry_reimport_if_changed (reg, "copy", src, dest, NULL,
	                                                                &changed, &error));
	g_assert_no_error (error);
	g_assert_false (changed);

	g_assert_cmpint (g_stat (src, &st), ==, 0);
	times[0].tv_sec = (time_t) st.st_mtime + 10;
	times[0].tv_nsec = 0;
	times[1].tv_sec = times[0].tv_sec;
	times[1].tv_nsec = 0;
	g_assert_cmpint (utimensat (AT_FDCWD, src, times, 0), ==, 0);
	g_assert_true (lrg_asset_importer_registry_reimport_if_changed (reg, "copy", src, dest, NULL,
	                                                                &changed, &error));
	g_assert_no_error (error);
	g_assert_true (changed);
	rm_rf (dir);
}

static void
test_fbx_cube (void)
{
	g_autofree gchar *src = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *dest = NULL;
	g_autofree gchar *obj = NULL;
	g_autoptr(LrgAssetImporterRegistry) reg = NULL;
	g_autoptr(GError) error = NULL;

	src = g_test_build_filename (G_TEST_DIST, "data", "blender_cube.fbx", NULL);
	g_assert_true (g_file_test (src, G_FILE_TEST_IS_REGULAR));
	g_assert_false (lrg_asset_importer_model_loadable ("hero.fbx"));
	g_assert_false (lrg_asset_importer_model_loadable ("hero.ply"));
	dir = make_tmpdir ();
	dest = g_build_filename (dir, "cube.obj", NULL);
	reg = lrg_asset_importer_registry_new ();
	g_assert_true (lrg_asset_importer_registry_import (reg, "fbx", src, dest, NULL, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_get_contents (dest, &obj, NULL, &error));
	g_assert_no_error (error);
	g_assert_nonnull (strstr (obj, "\nv "));
	g_assert_nonnull (strstr (obj, "\nf "));
	rm_rf (dir);
}

static void
test_autoload (void)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *item = NULL;
	g_autofree gchar *manifest = NULL;
	g_autofree gchar *name = NULL;
	g_autoptr(LrgEngine) engine = NULL;
	g_autoptr(LrgHeadlessHost) host = NULL;
	g_autoptr(GPtrArray) loaded = NULL;
	g_autoptr(GError) error = NULL;
	GObject *obj;

	dir = make_tmpdir ();
	item = g_build_filename (dir, "items", "sword.yaml", NULL);
	manifest = g_build_filename (dir, "project.ryaml", NULL);
	write_file (item,
	            "type: item-def\n"
	            "id: sword\n"
	            "name: Sword\n");
	write_file (manifest, "autoloads:\n  - items/sword.yaml\n");
	engine = g_object_new (LRG_TYPE_ENGINE, NULL);
	g_assert_true (lrg_engine_startup (engine, &error));
	g_assert_no_error (error);
	host = lrg_headless_host_new (engine, 1.0 / 60.0);
	loaded = lrg_game_host_load_autoloads (LRG_GAME_HOST (host), dir, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);
	g_assert_cmpuint (loaded->len, ==, 1);
	obj = g_ptr_array_index (loaded, 0);
	g_assert_true (LRG_IS_ITEM_DEF (obj));
	g_object_get (obj, "name", &name, NULL);
	g_assert_cmpstr (name, ==, "Sword");
	rm_rf (dir);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	g_test_add_func ("/pack-writer/rres", test_rres_roundtrip);
	g_test_add_func ("/pack-writer/scan", test_scan_skips_vcs);
	g_test_add_func ("/pack-writer/vfs-tar-zip", test_vfs_tar_zip);
	g_test_add_func ("/pack-writer/refs", test_refs);
	g_test_add_func ("/pack-writer/importer", test_importer_and_classify);
	g_test_add_func ("/pack-writer/project", test_project_config_and_template);
	g_test_add_func ("/pack-writer/plugin", test_plugin);
	g_test_add_func ("/pack-writer/export-ipc", test_export_ipc);
	g_test_add_func ("/pack-writer/cook-dxt1", test_cook_dxt1);
	g_test_add_func ("/pack-writer/reimport", test_reimport);
	g_test_add_func ("/pack-writer/fbx", test_fbx_cube);
	g_test_add_func ("/pack-writer/autoload", test_autoload);
	return g_test_run ();
}
