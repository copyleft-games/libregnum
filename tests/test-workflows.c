/* test-workflows.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * M10 libraries: PO interchange, level streaming and instances, transform
 * math, GUID merge, collaboration, open scenes, game-view presets, UI
 * place and animation, node tags, and git diff/commit.
 */

#include <libregnum.h>

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

static gchar *
make_tmpdir (void)
{
	gchar *dir;
	g_autoptr(GError) error = NULL;

	dir = g_dir_make_tmp ("lrg-flow-XXXXXX", &error);
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
	g_autoptr(GError) error = NULL;

	g_assert_true (g_file_set_contents (path, text, -1, &error));
	g_assert_no_error (error);
}

static void
assert_near (gfloat value,
             gfloat expect)
{
	g_assert_cmpfloat (fabsf (value - expect), <, 0.001f);
}

static gboolean
strv_has (GStrv        list,
          const gchar *needle)
{
	guint i;

	if (list == NULL)
		return FALSE;
	for (i = 0; list[i] != NULL; i++)
	{
		if (g_strcmp0 (list[i], needle) == 0)
			return TRUE;
	}
	return FALSE;
}

static LrgLevel *
level_with (const gchar *guid,
            const gchar *name,
            gfloat       x)
{
	LrgLevel *level;
	LrgNode  *node;

	level = lrg_level_new ("lvl");
	node = lrg_node_new (name);
	lrg_node_set_guid (node, guid);
	lrg_node_set_location_xyz (node, x, 0.0f, 0.0f);
	lrg_node_add_child (lrg_level_get_root (level), node);
	g_object_unref (node);
	return level;
}

static LrgNode *
find_guid (LrgLevel    *level,
           const gchar *guid)
{
	return lrg_node_find_by_guid (lrg_level_get_root (level), guid);
}

static void
test_po (void)
{
	g_autoptr(LrgLocale) src = NULL;
	g_autoptr(LrgLocale) dst = NULL;
	g_autoptr(LrgLocale) plural = NULL;
	g_autoptr(LrgLocale) pseudo = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *plural_path = NULL;
	g_auto(GStrv) keys = NULL;
	const gchar *wrapped;
	const gchar *po =
		"msgid \"\"\n"
		"msgstr \"\"\n"
		"\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
		"\n"
		"msgid \"apple\"\n"
		"msgid_plural \"apples\"\n"
		"msgstr[0] \"one apple\"\n"
		"msgstr[1] \"many apples\"\n";

	src = lrg_locale_new ("en", "English");
	lrg_locale_set_string (src, "hello", "Hello");
	dir = make_tmpdir ();
	path = g_build_filename (dir, "en.po", NULL);
	g_assert_true (lrg_po_export_file (src, path, NULL));

	dst = lrg_locale_new ("en", "English");
	g_assert_cmpint (lrg_po_import_file (dst, path, NULL), >, 0);
	g_assert_cmpstr (lrg_locale_get_string (dst, "hello"), ==, "Hello");

	plural_path = g_build_filename (dir, "plural.po", NULL);
	write_file (plural_path, po);
	plural = lrg_locale_new ("en", "English");
	g_assert_cmpint (lrg_po_import_file (plural, plural_path, NULL), ==, 1);
	g_assert_cmpstr (lrg_locale_get_plural (plural, "apple", 1), ==, "one apple");
	g_assert_cmpstr (lrg_locale_get_plural (plural, "apple", 2), ==, "many apples");

	keys = lrg_po_extract ("say @hello.world and tr(\"x\")");
	g_assert_nonnull (keys);
	g_assert_cmpstr (keys[0], ==, "hello.world");
	g_assert_cmpstr (keys[1], ==, "x");
	g_assert_null (keys[2]);

	pseudo = lrg_po_pseudolocalize (src);
	g_assert_cmpstr (lrg_locale_get_code (pseudo), ==, "qps-ploc");
	wrapped = lrg_locale_get_string (pseudo, "hello");
	g_assert_nonnull (wrapped);
	g_assert_cmpint (wrapped[0], ==, '[');
	g_assert_cmpint (wrapped[strlen (wrapped) - 1], ==, ']');

	rm_rf (dir);
}

static void
test_stream (void)
{
	g_autoptr(LrgLevelStream) stream = NULL;
	g_auto(GStrv) load = NULL;
	g_auto(GStrv) unload = NULL;

	stream = lrg_level_stream_new ();
	g_assert_true (lrg_level_stream_add_cell (stream, "A", "a.rlevel",
	                                          0.0f, 0.0f, 0.0f,
	                                          10.0f, 10.0f, 10.0f));
	g_assert_true (lrg_level_stream_add_cell (stream, "B", "b.rlevel",
	                                          20.0f, 0.0f, 0.0f,
	                                          30.0f, 10.0f, 10.0f));

	g_assert_true (lrg_level_stream_update (stream, 5.0f, 5.0f, 5.0f, 0.0f,
	                                        &load, &unload));
	g_assert_true (strv_has (load, "A"));
	g_assert_false (strv_has (load, "B"));
	g_assert_cmpuint (g_strv_length (unload), ==, 0);
	g_strfreev (load);
	g_strfreev (unload);
	load = NULL;
	unload = NULL;

	g_assert_true (lrg_level_stream_update (stream, 25.0f, 5.0f, 5.0f, 0.0f,
	                                        &load, &unload));
	g_assert_true (strv_has (load, "B"));
	g_assert_true (strv_has (unload, "A"));
	g_strfreev (load);
	g_strfreev (unload);
	load = NULL;
	unload = NULL;

	g_assert_true (lrg_level_stream_update (stream, 25.0f, 5.0f, 5.0f, 0.0f,
	                                        &load, &unload));
	g_assert_cmpuint (g_strv_length (load), ==, 0);
	g_assert_cmpuint (g_strv_length (unload), ==, 0);
}

typedef struct
{
	GMainLoop *loop;
	LrgLevel  *level;
	GError    *error;
} AsyncLoad;

static void
on_level_loaded (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	AsyncLoad *async = user_data;

	async->level = lrg_level_stream_load_finish (LRG_LEVEL_STREAM (source),
	                                             result,
	                                             &async->error);
	g_main_loop_quit (async->loop);
}

static void
test_stream_async_and_mount (void)
{
	g_autoptr(LrgLevel) sub = NULL;
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(LrgLevelStream) stream = NULL;
	g_autoptr(LrgNode) host = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	LrgNode   *child;
	AsyncLoad  async;
	GPtrArray *children;

	sub = level_with ("child-guid", "Room", 3.0f);
	dir = make_tmpdir ();
	path = g_build_filename (dir, "room.rlevel", NULL);
	ser = lrg_level_serializer_new ();
	g_assert_true (lrg_level_serializer_save_to_file (ser, sub, path, &error));
	g_assert_no_error (error);

	stream = lrg_level_stream_new ();
	async.loop = g_main_loop_new (NULL, FALSE);
	async.level = NULL;
	async.error = NULL;
	lrg_level_stream_load_async (stream, path, on_level_loaded, &async);
	g_main_loop_run (async.loop);
	g_main_loop_unref (async.loop);
	g_assert_no_error (async.error);
	g_assert_nonnull (async.level);
	g_assert_nonnull (find_guid (async.level, "child-guid"));
	g_object_unref (async.level);

	host = lrg_node_new ("Instance");
	g_assert_cmpuint (lrg_level_instance_mount (host, path, &error), ==, 1);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_node_get_level_path (host), ==, path);
	children = lrg_node_get_children (host);
	g_assert_cmpuint (children->len, ==, 1);
	child = g_ptr_array_index (children, 0);
	g_assert_cmpstr (lrg_node_get_guid (child), ==, "child-guid");
	g_assert_cmpstr (lrg_node_get_name (child), ==, "Room");

	rm_rf (dir);
}

static void
test_tags_roundtrip (void)
{
	g_autoptr(LrgLevel) level = NULL;
	g_autoptr(LrgLevel) loaded = NULL;
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(GPtrArray) tagged = NULL;
	g_autofree gchar *yaml = NULL;
	g_auto(GStrv) tags = NULL;
	LrgNode *node;
	const gchar *set_tags[] = { "enemy", "boss", NULL };

	level = lrg_level_new ("tagged");
	node = lrg_node_new ("Mob");
	lrg_node_set_guid (node, "mob");
	lrg_node_add_tag (node, "enemy");
	lrg_node_add_tag (node, "boss");
	lrg_node_add_tag (node, "enemy");
	lrg_node_set_level_path (node, "rooms/a.rlevel");
	lrg_node_add_child (lrg_level_get_root (level), node);
	g_object_unref (node);

	ser = lrg_level_serializer_new ();
	yaml = lrg_level_serializer_save_to_data (ser, level, NULL);
	g_assert_nonnull (yaml);
	loaded = lrg_level_serializer_load_from_data (ser, yaml, -1, NULL);
	g_assert_nonnull (loaded);
	node = find_guid (loaded, "mob");
	g_assert_nonnull (node);
	tags = lrg_node_get_tags (node);
	g_assert_cmpstr (tags[0], ==, "boss");
	g_assert_cmpstr (tags[1], ==, "enemy");
	g_assert_null (tags[2]);
	g_assert_cmpstr (lrg_node_get_level_path (node), ==, "rooms/a.rlevel");
	g_assert_true (lrg_node_remove_tag (node, "boss"));
	g_assert_false (lrg_node_has_tag (node, "boss"));

	lrg_node_set_tags (node, set_tags);
	tagged = lrg_node_collect_tagged (lrg_level_get_root (loaded), "boss");
	g_assert_cmpuint (tagged->len, ==, 1);
	g_assert_true (lrg_node_has_tag (g_ptr_array_index (tagged, 0), "boss"));
}

static void
test_game_object_tags (void)
{
	g_autoptr(LrgGameObject) object = NULL;
	g_auto(GStrv) tags = NULL;

	object = lrg_game_object_new ();
	lrg_game_object_add_tag (object, "enemy");
	lrg_game_object_add_tag (object, "boss");
	lrg_game_object_add_tag (object, "enemy");
	g_assert_true (lrg_game_object_has_tag (object, "boss"));
	g_assert_false (lrg_game_object_has_tag (object, "npc"));
	tags = lrg_game_object_get_tags (object);
	g_assert_cmpstr (tags[0], ==, "boss");
	g_assert_cmpstr (tags[1], ==, "enemy");
	g_assert_null (tags[2]);
	g_assert_true (lrg_game_object_remove_tag (object, "boss"));
	g_assert_false (lrg_game_object_has_tag (object, "boss"));
}

static void
test_transform (void)
{
	gfloat xs[3];
	gfloat ys[3];
	gfloat zs[3];
	gfloat x;
	gfloat y;
	gfloat z;
	gfloat angle;
	gfloat t;
	gfloat min_x;
	gfloat min_y;
	gfloat min_z;
	gfloat max_x;
	gfloat max_y;
	gfloat max_z;
	gfloat px;
	gfloat py;
	gfloat pz;

	xs[0] = 3.0f;
	xs[1] = 1.0f;
	xs[2] = 4.0f;
	ys[0] = 0.0f;
	ys[1] = 0.0f;
	ys[2] = 0.0f;
	zs[0] = 0.0f;
	zs[1] = 0.0f;
	zs[2] = 0.0f;
	lrg_transform_align (LRG_TRANSFORM_ALIGN_MIN, 0, xs, ys, zs, 3);
	assert_near (xs[0], 1.0f);
	assert_near (xs[1], 1.0f);
	assert_near (xs[2], 1.0f);

	xs[0] = 0.0f;
	xs[1] = 10.0f;
	xs[2] = 1.0f;
	lrg_transform_distribute (0, xs, ys, zs, 3);
	assert_near (xs[0], 0.0f);
	assert_near (xs[1], 10.0f);
	assert_near (xs[2], 5.0f);

	x = 1.2f;
	y = 0.0f;
	z = -1.6f;
	lrg_transform_snap_position (&x, &y, &z, 1.0f);
	assert_near (x, 1.0f);
	assert_near (z, -2.0f);

	angle = 20.0f;
	lrg_transform_snap_angle (&angle, 15.0f);
	assert_near (angle, 15.0f);

	x = 4.0f;
	y = 5.0f;
	z = 6.0f;
	lrg_transform_drop (&x, &y, &z, 1, 0.0f);
	assert_near (x, 4.0f);
	assert_near (y, 0.0f);
	assert_near (z, 6.0f);

	g_assert_true (lrg_transform_ray_aabb (-2.0f, 0.0f, 0.0f,
	                                       1.0f, 0.0f, 0.0f,
	                                       0.0f, 0.0f, 0.0f,
	                                       1.0f, 1.0f, 1.0f,
	                                       &t));
	assert_near (t, 2.0f);

	min_x = 0.0f;
	min_y = 0.0f;
	min_z = 0.0f;
	max_x = 1.0f;
	max_y = 1.0f;
	max_z = 1.0f;
	g_assert_true (lrg_transform_snap_surface (-2.0f, 0.0f, 0.0f,
	                                           1.0f, 0.0f, 0.0f,
	                                           &min_x, &min_y, &min_z,
	                                           &max_x, &max_y, &max_z,
	                                           1,
	                                           &x, &y, &z));
	assert_near (x, 0.0f);

	xs[0] = 0.0f;
	xs[1] = 2.0f;
	ys[0] = 0.0f;
	ys[1] = 4.0f;
	zs[0] = 0.0f;
	zs[1] = 6.0f;
	lrg_transform_pivot (LRG_PIVOT_MEDIAN, xs, ys, zs, 2,
	                     9.0f, 9.0f, 9.0f,
	                     0.0f, 0.0f, 0.0f,
	                     &px, &py, &pz);
	assert_near (px, 1.0f);
	assert_near (py, 2.0f);
	assert_near (pz, 3.0f);
}

static void
test_merge (void)
{
	g_autoptr(LrgLevel) base = NULL;
	g_autoptr(LrgLevel) ours = NULL;
	g_autoptr(LrgLevel) theirs = NULL;
	g_autoptr(LrgLevel) result = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) conflicts = NULL;
	LrgNode *extra;
	LrgNode *found;

	base = level_with ("a", "A", 0.0f);
	ours = level_with ("a", "A", 1.0f);
	theirs = level_with ("a", "A", 0.0f);
	extra = lrg_node_new ("B");
	lrg_node_set_guid (extra, "b");
	lrg_node_add_child (lrg_level_get_root (theirs), extra);
	g_object_unref (extra);
	result = lrg_level_merge (base, ours, theirs, &conflicts, &error);
	g_assert_no_error (error);
	g_assert_nonnull (result);
	found = find_guid (result, "a");
	g_assert_nonnull (found);
	assert_near (lrg_node_get_location (found)->x, 1.0f);
	g_assert_nonnull (find_guid (result, "b"));
	g_assert_cmpuint (g_strv_length (conflicts), ==, 0);
	g_clear_object (&base);
	g_clear_object (&ours);
	g_clear_object (&theirs);
	g_clear_object (&result);
	g_clear_pointer (&conflicts, g_strfreev);

	base = level_with ("a", "A", 0.0f);
	ours = level_with ("a", "A", 1.0f);
	theirs = level_with ("a", "A", 2.0f);
	result = lrg_level_merge (base, ours, theirs, &conflicts, &error);
	g_assert_no_error (error);
	g_assert_true (strv_has (conflicts, "a"));
	found = find_guid (result, "a");
	assert_near (lrg_node_get_location (found)->x, 1.0f);
	g_clear_object (&base);
	g_clear_object (&ours);
	g_clear_object (&theirs);
	g_clear_object (&result);
	g_clear_pointer (&conflicts, g_strfreev);

	base = level_with ("a", "A", 0.0f);
	ours = level_with ("a", "A", 0.0f);
	theirs = lrg_level_new ("empty");
	result = lrg_level_merge (base, ours, theirs, &conflicts, &error);
	g_assert_no_error (error);
	g_assert_null (find_guid (result, "a"));
	g_assert_cmpuint (g_strv_length (conflicts), ==, 0);
	g_clear_object (&base);
	g_clear_object (&ours);
	g_clear_object (&theirs);
	g_clear_object (&result);
	g_clear_pointer (&conflicts, g_strfreev);

	base = level_with ("a", "A", 0.0f);
	ours = level_with ("a", "A", 1.0f);
	theirs = lrg_level_new ("empty");
	result = lrg_level_merge (base, ours, theirs, &conflicts, &error);
	g_assert_no_error (error);
	g_assert_true (strv_has (conflicts, "a"));
	g_assert_nonnull (find_guid (result, "a"));
}

static void
test_collab_scenes_view (void)
{
	g_autoptr(LrgCollabSession) session = NULL;
	g_autoptr(LrgEditorScenes) scenes = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *user = NULL;
	gint width = 0;
	gint height = 0;
	gfloat x = 0.0f;

	session = lrg_collab_session_new ();
	g_assert_true (lrg_collab_apply (session, "ada", "guid-1", 2,
	                                 "location", "1,0,0", &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_collab_get (session, "guid-1", "location"), ==, "1,0,0");
	g_assert_false (lrg_collab_apply (session, "ada", "guid-1", 1,
	                                  "location", "old", &error));
	g_assert_error (error, LRG_COLLAB_ERROR, LRG_COLLAB_ERROR_STALE);
	g_clear_error (&error);

	g_assert_true (lrg_collab_lock (session, "ada", "guid-1", &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_collab_lock_holder (session, "guid-1"), ==, "ada");
	g_assert_false (lrg_collab_apply (session, "bea", "guid-1", 3,
	                                  "location", "stolen", &error));
	g_assert_error (error, LRG_COLLAB_ERROR, LRG_COLLAB_ERROR_LOCKED);
	g_clear_error (&error);
	g_assert_true (lrg_collab_unlock (session, "ada", "guid-1", &error));
	g_assert_no_error (error);
	g_assert_null (lrg_collab_lock_holder (session, "guid-1"));

	lrg_collab_set_cursor (session, "ada", 4.0f, 5.0f, 6.0f);
	g_assert_cmpuint (lrg_collab_presence_count (session), ==, 1);
	g_assert_true (lrg_collab_presence_get (session, 0, &user, &x, NULL, NULL));
	g_assert_cmpstr (user, ==, "ada");
	assert_near (x, 4.0f);

	scenes = lrg_editor_scenes_new ();
	g_assert_cmpint (lrg_editor_scenes_get_active (scenes), ==, -1);
	g_assert_cmpuint (lrg_editor_scenes_add (scenes, "a.rlevel"), ==, 0);
	g_assert_cmpuint (lrg_editor_scenes_add (scenes, "b.rlevel"), ==, 1);
	g_assert_cmpint (lrg_editor_scenes_get_active (scenes), ==, 1);
	g_assert_true (lrg_editor_scenes_remove (scenes, 0));
	g_assert_cmpint (lrg_editor_scenes_get_active (scenes), ==, 0);
	g_assert_cmpstr (lrg_editor_scenes_get_path (scenes, 0), ==, "b.rlevel");
	g_assert_cmpuint (lrg_editor_scenes_get_count (scenes), ==, 1);

	g_assert_cmpuint (lrg_game_view_preset_count (), ==, 7);
	g_assert_true (lrg_game_view_preset_find ("4:3", &width, &height));
	g_assert_cmpint (width, ==, 1024);
	g_assert_cmpint (height, ==, 768);
	g_assert_true (lrg_game_view_preset_find ("16:9", &width, &height));
	g_assert_cmpint (width, ==, 1920);
	g_assert_cmpint (height, ==, 1080);
	g_assert_true (lrg_game_view_preset_find ("steam-deck", &width, &height));
	g_assert_cmpint (width, ==, 1280);
	g_assert_cmpint (height, ==, 800);
	g_assert_true (lrg_game_view_preset_get (0, NULL, &width, &height));
	g_assert_cmpint (width, >, 0);
}

static void
test_ui_author (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgUiAnimation) anim = NULL;
	g_autoptr(LrgUiAnimation) loaded = NULL;
	g_autofree gchar *placed = NULL;
	g_autofree gchar *saved = NULL;
	gfloat value = 0.0f;
	const gchar *yaml =
		"widgets:\n"
		"  - name: panel\n"
		"    x: 0\n"
		"    y: 0\n";

	placed = lrg_ui_layout_place (yaml, "panel", 12.0f, 34.0f, &error);
	g_assert_no_error (error);
	g_assert_nonnull (placed);
	g_assert_nonnull (strstr (placed, "12"));
	g_assert_nonnull (strstr (placed, "34"));
	g_assert_null (lrg_ui_layout_place (yaml, "missing", 1.0f, 1.0f, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);

	anim = lrg_ui_animation_new ();
	lrg_ui_animation_add_key (anim, "panel", "x", 0.0f, 0.0f);
	lrg_ui_animation_add_key (anim, "panel", "x", 2.0f, 10.0f);
	g_assert_true (lrg_ui_animation_sample (anim, "panel", "x", 1.0f, &value));
	assert_near (value, 5.0f);
	g_assert_false (lrg_ui_animation_sample (anim, "other", "x", 1.0f, &value));

	saved = lrg_ui_animation_save (anim);
	g_assert_nonnull (saved);
	loaded = lrg_ui_animation_load (saved, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);
	g_assert_true (lrg_ui_animation_sample (loaded, "panel", "x", 1.0f, &value));
	assert_near (value, 5.0f);
}

static void
test_vcs (void)
{
	g_autofree gchar *git = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *note = NULL;
	g_autofree gchar *diff = NULL;
	g_autofree gchar *log_text = NULL;
	g_autofree gchar *committed = NULL;
	g_autofree gchar *locks = NULL;
	g_autoptr(LrgEditorVcs) vcs = NULL;
	g_autoptr(GError) error = NULL;
	gchar *argv[6];
	gint status = 0;
	gboolean available = TRUE;

	git = g_find_program_in_path ("git");
	if (git == NULL)
	{
		g_test_skip ("git is not installed");
		return;
	}

	dir = make_tmpdir ();
	argv[0] = "git";
	argv[1] = "-c";
	argv[2] = "init.defaultBranch=master";
	argv[3] = "init";
	argv[4] = "-q";
	argv[5] = NULL;
	g_assert_true (g_spawn_sync (dir, argv, NULL, G_SPAWN_SEARCH_PATH,
	                             NULL, NULL, NULL, NULL, &status, &error));
	g_assert_no_error (error);
	g_assert_true (g_spawn_check_wait_status (status, &error));
	g_assert_no_error (error);

	note = g_build_filename (dir, "note.txt", NULL);
	write_file (note, "one\n");
	vcs = lrg_editor_vcs_new (dir);
	committed = lrg_editor_vcs_commit (vcs, "hello", &error);
	g_assert_no_error (error);
	g_assert_nonnull (committed);

	write_file (note, "one\ntwo\n");
	diff = lrg_editor_vcs_diff (vcs, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (diff, "two"));

	log_text = lrg_editor_vcs_log (vcs, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (log_text, "hello"));
	g_assert_nonnull (strstr (log_text, "note.txt"));
	g_assert_cmpstr (lrg_editor_vcs_magit_handoff (vcs), ==, "(magit-status)");

	locks = lrg_editor_vcs_lfs_locks (vcs, &available, &error);
	g_assert_no_error (error);
	g_assert_nonnull (locks);

	rm_rf (dir);
}

static void
save_level (LrgLevel    *level,
            const gchar *path)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_level_serializer_save_to_file (ser, level, path, &error));
	g_assert_no_error (error);
}

static gboolean
git_init (const gchar *dir)
{
	gchar *argv[6];
	gint   status = 0;
	g_autoptr(GError) error = NULL;

	argv[0] = (gchar *) "git";
	argv[1] = (gchar *) "-c";
	argv[2] = (gchar *) "init.defaultBranch=master";
	argv[3] = (gchar *) "init";
	argv[4] = (gchar *) "-q";
	argv[5] = NULL;
	if (!g_spawn_sync (dir, argv, NULL, G_SPAWN_SEARCH_PATH,
	                   NULL, NULL, NULL, NULL, &status, &error))
		return FALSE;
	return g_spawn_check_wait_status (status, &error);
}

static JsonNode *
ipc_ok (LrgEditorServer *server,
        const gchar     *line)
{
	g_autofree gchar     *resp = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject           *obj;

	resp = lrg_editor_server_dispatch (server, line);
	g_assert_nonnull (resp);
	parser = json_parser_new ();
	g_assert_true (json_parser_load_from_data (parser, resp, -1, NULL));
	obj = json_node_get_object (json_parser_get_root (parser));
	if (json_object_has_member (obj, "error"))
		g_error ("ipc: %s",
		         json_object_get_string_member (json_object_get_object_member (obj, "error"), "message"));
	g_assert_true (json_object_has_member (obj, "result"));
	return json_node_copy (json_object_get_member (obj, "result"));
}

static gchar *
built_tool (const gchar *name)
{
	const gchar      *srcdir = g_getenv ("G_TEST_SRCDIR");
	g_autofree gchar *rel = NULL;

	if (srcdir == NULL)
		srcdir = ".";
	rel = g_build_filename (srcdir, "..", "build", "release", name, NULL);
	if (g_file_test (rel, G_FILE_TEST_IS_EXECUTABLE))
		return g_steal_pointer (&rel);
	return g_find_program_in_path (name);
}

static void
test_tag_filter (void)
{
	g_autoptr(LrgLevel) level = NULL;
	g_autoptr(LrgEditorNodeModel) model = NULL;
	LrgNode *tagged;
	LrgNode *named;

	level = lrg_level_new ("tags");
	tagged = lrg_node_new ("Mob");
	lrg_node_add_tag (tagged, "Quest");
	lrg_node_add_child (lrg_level_get_root (level), tagged);
	g_object_unref (tagged);
	named = lrg_node_new ("tag:quest");
	lrg_node_add_child (lrg_level_get_root (level), named);
	g_object_unref (named);
	tagged = g_ptr_array_index (lrg_node_get_children (lrg_level_get_root (level)), 0);
	named = g_ptr_array_index (lrg_node_get_children (lrg_level_get_root (level)), 1);

	model = lrg_editor_node_model_new_for_level (level);
	lrg_editor_node_model_set_filter (model, "tag:quest");
	g_assert_true (lrg_editor_node_model_is_match (model, tagged));
	g_assert_false (lrg_editor_node_model_is_match (model, named));
	lrg_editor_node_model_set_filter (model, "tag:");
	g_assert_true (lrg_editor_node_model_is_match (model, tagged));
	g_assert_false (lrg_editor_node_model_is_match (model, named));
}

static void
test_gizmo_ops (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgLevel) level = NULL;
	g_autoptr(LrgEditorGizmo) gizmo = NULL;
	g_autoptr(GPtrArray) sel = NULL;
	GPtrArray *kids;
	LrgNode   *a;
	LrgNode   *b;
	LrgNode   *c;
	gfloat     origin[3];

	level = lrg_level_new ("gizmo");
	a = lrg_node_new ("A");
	lrg_node_set_location_xyz (a, 0.0f, 0.0f, 0.0f);
	lrg_node_add_child (lrg_level_get_root (level), a);
	g_object_unref (a);
	b = lrg_node_new ("B");
	lrg_node_set_location_xyz (b, 2.0f, 0.0f, 0.0f);
	lrg_node_add_child (lrg_level_get_root (level), b);
	g_object_unref (b);
	c = lrg_node_new ("C");
	lrg_node_set_location_xyz (c, 10.0f, 0.0f, 0.0f);
	lrg_node_add_child (lrg_level_get_root (level), c);
	g_object_unref (c);

	editor = lrg_editor_new ();
	lrg_editor_set_level (editor, level);
	kids = lrg_node_get_children (lrg_level_get_root (lrg_editor_get_level (editor)));
	a = g_ptr_array_index (kids, 0);
	b = g_ptr_array_index (kids, 1);
	c = g_ptr_array_index (kids, 2);
	sel = g_ptr_array_new ();
	g_ptr_array_add (sel, a);
	g_ptr_array_add (sel, b);
	g_ptr_array_add (sel, c);
	lrg_editor_select_nodes (editor, sel, FALSE);

	gizmo = lrg_editor_gizmo_new (editor);
	lrg_editor_gizmo_set_pivot_mode (gizmo, LRG_PIVOT_MEDIAN);
	lrg_editor_gizmo_get_origin (gizmo, origin);
	assert_near (origin[0], 4.0f);

	g_assert_true (lrg_editor_gizmo_apply (gizmo, "distribute", 0, 0.0f));
	assert_near (lrg_node_get_location (a)->x, 0.0f);
	assert_near (lrg_node_get_location (b)->x, 5.0f);
	assert_near (lrg_node_get_location (c)->x, 10.0f);
	g_assert_false (lrg_editor_gizmo_apply (gizmo, "nope", 0, 0.0f));
}

static void
test_gizmo_surface (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgLevel) level = NULL;
	g_autoptr(LrgEditorGizmo) gizmo = NULL;
	g_autoptr(GPtrArray) sel = NULL;
	GPtrArray *kids;
	LrgNode   *floor;
	LrgNode   *primary;

	level = lrg_level_new ("surface");
	floor = lrg_node_new ("Floor");
	lrg_node_set_location_xyz (floor, 0.0f, 0.0f, 0.0f);
	lrg_node_add_child (lrg_level_get_root (level), floor);
	g_object_unref (floor);
	primary = lrg_node_new ("Drop");
	lrg_node_set_location_xyz (primary, 0.5f, 10.0f, 0.5f);
	lrg_node_add_child (lrg_level_get_root (level), primary);
	g_object_unref (primary);

	editor = lrg_editor_new ();
	lrg_editor_set_level (editor, level);
	kids = lrg_node_get_children (lrg_level_get_root (lrg_editor_get_level (editor)));
	floor = g_ptr_array_index (kids, 0);
	primary = g_ptr_array_index (kids, 1);
	sel = g_ptr_array_new ();
	g_ptr_array_add (sel, floor);
	g_ptr_array_add (sel, primary);
	lrg_editor_select_nodes (editor, sel, FALSE);

	gizmo = lrg_editor_gizmo_new (editor);
	g_assert_true (lrg_editor_gizmo_apply (gizmo, "surface", 1, 0.0f));
	assert_near (lrg_node_get_location (floor)->y, 0.0f);
	assert_near (lrg_node_get_location (primary)->x, 0.5f);
	assert_near (lrg_node_get_location (primary)->y, 1.0f);
	assert_near (lrg_node_get_location (primary)->z, 0.5f);
}

static void
test_letterbox (void)
{
	gint x = -1;
	gint y = -1;
	gint w = -1;
	gint h = -1;
	g_autoptr(LrgWidget) panel = NULL;

	lrg_game_view_letterbox ("16:9", 1920, 1200, &x, &y, &w, &h);
	g_assert_cmpint (x, ==, 0);
	g_assert_cmpint (y, ==, 60);
	g_assert_cmpint (w, ==, 1920);
	g_assert_cmpint (h, ==, 1080);

	lrg_game_view_letterbox ("not-a-preset", 100, 80, &x, &y, &w, &h);
	g_assert_cmpint (x, ==, 0);
	g_assert_cmpint (y, ==, 0);
	g_assert_cmpint (w, ==, 100);
	g_assert_cmpint (h, ==, 80);

	panel = lrg_editor_game_view_panel_new ();
	lrg_editor_game_view_panel_set_preset (panel, "16:9");
	lrg_editor_game_view_panel_get_rect (panel, 1920, 1200, &x, &y, &w, &h);
	g_assert_cmpint (y, ==, 60);
	g_assert_cmpint (h, ==, 1080);
}

static void
test_nested_items (void)
{
	GVariantBuilder obj;
	GVariantBuilder items;
	GVariantBuilder root;
	g_autoptr(GVariant) axe = NULL;
	g_autoptr(GVariant) item = NULL;
	g_autoptr(GVariant) list = NULL;
	g_autoptr(GVariant) doc = NULL;
	g_autoptr(GVariant) sword = NULL;
	g_autoptr(GVariant) edited = NULL;
	g_autoptr(GVariant) got = NULL;
	g_autoptr(GVariant) still = NULL;
	g_autoptr(GError) error = NULL;

	axe = g_variant_ref_sink (g_variant_new_string ("axe"));
	g_variant_builder_init (&obj, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&obj, "{sv}", "name", axe);
	item = g_variant_ref_sink (g_variant_builder_end (&obj));

	g_variant_builder_init (&items, G_VARIANT_TYPE ("av"));
	g_variant_builder_add (&items, "v", item);
	list = g_variant_ref_sink (g_variant_builder_end (&items));

	g_variant_builder_init (&root, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&root, "{sv}", "items", list);
	doc = g_variant_ref_sink (g_variant_builder_end (&root));

	got = lrg_inspector_nested_get (doc, "items/0/name", &error);
	g_assert_no_error (error);
	g_assert_nonnull (got);
	g_assert_cmpstr (g_variant_get_string (got, NULL), ==, "axe");

	sword = g_variant_ref_sink (g_variant_new_string ("sword"));
	edited = lrg_inspector_nested_set (doc, "items/0/name", sword, &error);
	g_assert_no_error (error);
	g_assert_nonnull (edited);
	g_clear_pointer (&got, g_variant_unref);
	got = lrg_inspector_nested_get (edited, "items/0/name", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (g_variant_get_string (got, NULL), ==, "sword");
	still = lrg_inspector_nested_get (doc, "items/0/name", &error);
	g_assert_cmpstr (g_variant_get_string (still, NULL), ==, "axe");
}

static void
test_ui_place (void)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(LrgEditorUiLayoutEditor) editor = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *yaml = NULL;
	const gchar *src =
		"widgets:\n"
		"  - name: panel\n"
		"    x: 0\n"
		"    y: 0\n";

	doc = lrg_editor_document_new_from_data (src, -1, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, &error);
	g_assert_no_error (error);
	editor = lrg_editor_ui_layout_editor_new (NULL, doc);
	g_assert_nonnull (editor);
	g_assert_true (lrg_editor_ui_layout_editor_place (editor, "panel", 12.0f, 34.0f, &error));
	g_assert_no_error (error);
	yaml = lrg_editor_document_get_yaml (doc, "");
	g_assert_nonnull (yaml);
	g_assert_nonnull (strstr (yaml, "12"));
	g_assert_nonnull (strstr (yaml, "34"));
}

static void
test_scene_bar (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgWidget) bar = NULL;
	g_autoptr(LrgLevel) first = NULL;
	g_autoptr(LrgLevel) second = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path_a = NULL;
	g_autofree gchar *path_b = NULL;

	dir = make_tmpdir ();
	path_a = g_build_filename (dir, "a.rlevel", NULL);
	path_b = g_build_filename (dir, "b.rlevel", NULL);
	first = level_with ("guid-a", "A", 1.0f);
	second = level_with ("guid-b", "B", 2.0f);
	save_level (first, path_a);
	save_level (second, path_b);

	editor = lrg_editor_new ();
	bar = lrg_editor_scene_bar_new (editor);
	g_assert_true (lrg_editor_scene_bar_open (bar, path_a, &error));
	g_assert_no_error (error);
	g_assert_nonnull (find_guid (lrg_editor_get_level (editor), "guid-a"));
	g_assert_true (lrg_editor_scene_bar_open (bar, path_b, &error));
	g_assert_no_error (error);
	g_assert_nonnull (find_guid (lrg_editor_get_level (editor), "guid-b"));
	g_assert_true (lrg_editor_scene_bar_set_active (bar, 0, &error));
	g_assert_no_error (error);
	g_assert_nonnull (find_guid (lrg_editor_get_level (editor), "guid-a"));
	g_assert_null (find_guid (lrg_editor_get_level (editor), "guid-b"));
	rm_rf (dir);
}

static void
test_vcs_panel (void)
{
	g_autoptr(LrgWidget) panel = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *note = NULL;
	LrgWidget *diff;
	LrgWidget *locks;
	LrgWidget *magit;
	LrgWidget *message;
	LrgWidget *commit;
	LrgWidget *logw;

	if (g_find_program_in_path ("git") == NULL)
	{
		g_test_skip ("git is not installed");
		return;
	}
	dir = make_tmpdir ();
	g_assert_true (git_init (dir));
	note = g_build_filename (dir, "note.txt", NULL);
	write_file (note, "one\n");

	panel = lrg_editor_vcs_panel_new ();
	lrg_editor_vcs_panel_set_project (panel, dir);
	diff = lrg_widget_find (panel, "vcs-diff");
	locks = lrg_widget_find (panel, "vcs-locks");
	magit = lrg_widget_find (panel, "vcs-magit");
	message = lrg_widget_find (panel, "vcs-message");
	commit = lrg_widget_find (panel, "vcs-commit");
	logw = lrg_widget_find (panel, "vcs-log");
	g_assert_nonnull (diff);
	g_assert_nonnull (locks);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (magit)), ==, "(magit-status)");
	g_assert_nonnull (lrg_label_get_text (LRG_LABEL (locks)));

	lrg_text_input_set_text (LRG_TEXT_INPUT (message), "panel-commit");
	g_signal_emit_by_name (commit, "clicked");
	g_assert_nonnull (strstr (lrg_label_get_text (LRG_LABEL (logw)), "panel-commit"));
	(void) error;
	rm_rf (dir);
}

static void
test_second_inspector (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgEditorApp) app = NULL;
	LrgWidget *second;
	GVariantBuilder root;
	g_autoptr(GVariant) doc = NULL;
	g_autoptr(GVariant) sword = NULL;
	g_autoptr(GVariant) got = NULL;
	g_autoptr(GError) error = NULL;

	editor = lrg_editor_new ();
	app = lrg_editor_app_new (editor);
	second = lrg_widget_find (LRG_WIDGET (lrg_editor_app_get_canvas (app)), "inspector-2");
	g_assert_nonnull (second);

	g_variant_builder_init (&root, G_VARIANT_TYPE_VARDICT);
	doc = g_variant_ref_sink (g_variant_builder_end (&root));
	lrg_editor_inspector_panel_set_nested_root (LRG_EDITOR_INSPECTOR_PANEL (second), doc);
	sword = g_variant_ref_sink (g_variant_new_string ("sword"));
	g_assert_true (lrg_editor_inspector_panel_edit_nested (LRG_EDITOR_INSPECTOR_PANEL (second),
	                                                       "name", sword, &error));
	g_assert_no_error (error);
	got = lrg_editor_inspector_panel_get_nested (LRG_EDITOR_INSPECTOR_PANEL (second), "name", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (g_variant_get_string (got, NULL), ==, "sword");
}

static void
test_merge_files (void)
{
	g_autoptr(LrgLevel) base = NULL;
	g_autoptr(LrgLevel) ours = NULL;
	g_autoptr(LrgLevel) theirs = NULL;
	g_autoptr(LrgLevel) loaded = NULL;
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) conflicts = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *base_path = NULL;
	g_autofree gchar *ours_path = NULL;
	g_autofree gchar *theirs_path = NULL;
	g_autofree gchar *out_path = NULL;
	g_autofree gchar *attrs = NULL;
	g_autofree gchar *editor_bin = NULL;
	gchar *argv[6];
	gint   status = 0;

	dir = make_tmpdir ();
	base_path = g_build_filename (dir, "base.rlevel", NULL);
	ours_path = g_build_filename (dir, "ours.rlevel", NULL);
	theirs_path = g_build_filename (dir, "theirs.rlevel", NULL);
	out_path = g_build_filename (dir, "out.rlevel", NULL);
	base = level_with ("a", "A", 0.0f);
	ours = level_with ("a", "A", 1.0f);
	theirs = level_with ("a", "A", 2.0f);
	save_level (base, base_path);
	save_level (ours, ours_path);
	save_level (theirs, theirs_path);

	g_assert_true (lrg_level_merge_files (base_path, ours_path, theirs_path, out_path,
	                                      &conflicts, &error));
	g_assert_no_error (error);
	g_assert_true (strv_has (conflicts, "a"));
	ser = lrg_level_serializer_new ();
	loaded = lrg_level_serializer_load_from_file (ser, out_path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (find_guid (loaded, "a"));
	assert_near (lrg_node_get_location (find_guid (loaded, "a"))->x, 1.0f);

	if (g_find_program_in_path ("git") != NULL)
	{
		g_assert_true (git_init (dir));
		g_assert_true (lrg_level_merge_install_driver (dir, NULL, &error));
		g_assert_no_error (error);
		attrs = g_build_filename (dir, ".gitattributes", NULL);
		{
			gchar *text = NULL;

			g_assert_true (g_file_get_contents (attrs, &text, NULL, &error));
			g_assert_nonnull (strstr (text, "*.rlevel merge=rlevel"));
			g_free (text);
		}
	}

	editor_bin = built_tool ("lrgeditor");
	if (editor_bin != NULL)
	{
		g_autofree gchar *err_text = NULL;
		g_clear_pointer (&conflicts, g_strfreev);
		argv[0] = editor_bin;
		argv[1] = (gchar *) "--merge";
		argv[2] = base_path;
		argv[3] = ours_path;
		argv[4] = theirs_path;
		argv[5] = NULL;
		g_assert_true (g_spawn_sync (dir, argv, NULL, 0, NULL, NULL, NULL, &err_text, &status, &error));
		g_assert_no_error (error);
		g_assert_true (g_spawn_check_wait_status (status, &error));
		g_assert_no_error (error);
		g_assert_nonnull (err_text);
		g_assert_nonnull (strstr (err_text, "conflict: a"));
		g_clear_object (&loaded);
		loaded = lrg_level_serializer_load_from_file (ser, ours_path, &error);
		g_assert_no_error (error);
		assert_near (lrg_node_get_location (find_guid (loaded, "a"))->x, 1.0f);
	}
	rm_rf (dir);
}

static void
test_m10_ipc (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgEditorServer) server = NULL;
	g_autoptr(LrgLevel) level = NULL;
	g_autoptr(GPtrArray) sel = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path_a = NULL;
	g_autofree gchar *path_b = NULL;
	g_autofree gchar *req = NULL;
	g_autoptr(JsonNode) result = NULL;
	JsonObject *obj;
	JsonArray  *keys;
	GPtrArray  *kids;
	LrgNode    *mid;

	dir = make_tmpdir ();
	editor = lrg_editor_new ();
	server = lrg_editor_server_new (editor);
	lrg_editor_server_set_project_dir (server, dir);

	result = ipc_ok (server, "{\"id\":1,\"method\":\"po.extract\",\"params\":{\"text\":\"say @hello.world\"}}");
	obj = json_node_get_object (result);
	keys = json_object_get_array_member (obj, "keys");
	g_assert_cmpuint (json_array_get_length (keys), ==, 1);
	g_assert_cmpstr (json_array_get_string_element (keys, 0), ==, "hello.world");
	g_clear_pointer (&result, json_node_unref);

	result = ipc_ok (server, "{\"id\":1,\"method\":\"po.pseudo\",\"params\":{\"text\":\"Hello\"}}");
	g_assert_cmpint (json_object_get_string_member (json_node_get_object (result), "text")[0], ==, '[');
	g_clear_pointer (&result, json_node_unref);

	result = ipc_ok (server, "{\"id\":1,\"method\":\"view.letterbox\",\"params\":{\"preset\":\"16:9\",\"w\":1920,\"h\":1200}}");
	obj = json_node_get_object (result);
	g_assert_cmpint (json_object_get_int_member (obj, "y"), ==, 60);
	g_assert_cmpint (json_object_get_int_member (obj, "h"), ==, 1080);
	g_clear_pointer (&result, json_node_unref);

	level = lrg_level_new ("ipc");
	{
		LrgNode *a = lrg_node_new ("A");
		LrgNode *b = lrg_node_new ("B");
		LrgNode *c = lrg_node_new ("C");

		lrg_node_set_location_xyz (a, 0.0f, 0.0f, 0.0f);
		lrg_node_set_location_xyz (b, 2.0f, 0.0f, 0.0f);
		lrg_node_set_location_xyz (c, 10.0f, 0.0f, 0.0f);
		lrg_node_add_child (lrg_level_get_root (level), a);
		lrg_node_add_child (lrg_level_get_root (level), b);
		lrg_node_add_child (lrg_level_get_root (level), c);
		g_object_unref (a);
		g_object_unref (b);
		g_object_unref (c);
	}
	lrg_editor_set_level (editor, level);
	kids = lrg_node_get_children (lrg_level_get_root (lrg_editor_get_level (editor)));
	sel = g_ptr_array_new ();
	g_ptr_array_add (sel, g_ptr_array_index (kids, 0));
	g_ptr_array_add (sel, g_ptr_array_index (kids, 1));
	g_ptr_array_add (sel, g_ptr_array_index (kids, 2));
	lrg_editor_select_nodes (editor, sel, FALSE);
	result = ipc_ok (server, "{\"id\":1,\"method\":\"gizmo.apply\",\"params\":{\"op\":\"distribute\",\"axis\":0,\"value\":0}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (result), "ok"));
	mid = g_ptr_array_index (kids, 1);
	assert_near (lrg_node_get_location (mid)->x, 5.0f);
	g_clear_pointer (&result, json_node_unref);

	result = ipc_ok (server, "{\"id\":1,\"method\":\"gizmo.pivot\",\"params\":{\"mode\":\"median\"}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "mode"), ==, "median");
	g_clear_pointer (&result, json_node_unref);

	result = ipc_ok (server,
	                 "{\"id\":1,\"method\":\"collab.apply\",\"params\":{\"user\":\"ada\",\"guid\":\"n1\",\"revision\":2,\"prop\":\"location\",\"value\":\"1,0,0\"}}");
	g_clear_pointer (&result, json_node_unref);
	result = ipc_ok (server, "{\"id\":1,\"method\":\"collab.get\",\"params\":{\"guid\":\"n1\",\"prop\":\"location\"}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "value"), ==, "1,0,0");
	g_clear_pointer (&result, json_node_unref);
	result = ipc_ok (server, "{\"id\":1,\"method\":\"collab.lock\",\"params\":{\"user\":\"ada\",\"guid\":\"n1\"}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "holder"), ==, "ada");
	g_clear_pointer (&result, json_node_unref);
	result = ipc_ok (server, "{\"id\":1,\"method\":\"collab.cursor\",\"params\":{\"user\":\"ada\",\"x\":4,\"y\":5,\"z\":6}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (result), "count"), ==, 1);
	g_clear_pointer (&result, json_node_unref);
	result = ipc_ok (server, "{\"id\":1,\"method\":\"collab.presence\",\"params\":{}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (result), "users")), ==, 1);
	g_clear_pointer (&result, json_node_unref);

	path_a = g_build_filename (dir, "a.rlevel", NULL);
	path_b = g_build_filename (dir, "b.rlevel", NULL);
	{
		g_autoptr(LrgLevel) la = level_with ("guid-a", "A", 1.0f);
		g_autoptr(LrgLevel) lb = level_with ("guid-b", "B", 2.0f);

		save_level (la, path_a);
		save_level (lb, path_b);
	}
	req = g_strdup_printf ("{\"id\":1,\"method\":\"scenes.open\",\"params\":{\"path\":\"%s\"}}", path_a);
	result = ipc_ok (server, req);
	g_clear_pointer (&result, json_node_unref);
	g_clear_pointer (&req, g_free);
	req = g_strdup_printf ("{\"id\":1,\"method\":\"scenes.open\",\"params\":{\"path\":\"%s\"}}", path_b);
	result = ipc_ok (server, req);
	g_clear_pointer (&result, json_node_unref);
	g_assert_nonnull (find_guid (lrg_editor_get_level (editor), "guid-b"));
	result = ipc_ok (server, "{\"id\":1,\"method\":\"scenes.active\",\"params\":{\"index\":0}}");
	g_assert_nonnull (find_guid (lrg_editor_get_level (editor), "guid-a"));
	g_clear_pointer (&result, json_node_unref);

	rm_rf (dir);
	(void) error;
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	g_test_add_func ("/workflows/po", test_po);
	g_test_add_func ("/workflows/stream", test_stream);
	g_test_add_func ("/workflows/stream-async", test_stream_async_and_mount);
	g_test_add_func ("/workflows/tags", test_tags_roundtrip);
	g_test_add_func ("/workflows/game-object-tags", test_game_object_tags);
	g_test_add_func ("/workflows/transform", test_transform);
	g_test_add_func ("/workflows/merge", test_merge);
	g_test_add_func ("/workflows/collab", test_collab_scenes_view);
	g_test_add_func ("/workflows/ui", test_ui_author);
	g_test_add_func ("/workflows/vcs", test_vcs);
	g_test_add_func ("/workflows/tag-filter", test_tag_filter);
	g_test_add_func ("/workflows/gizmo", test_gizmo_ops);
	g_test_add_func ("/workflows/gizmo-surface", test_gizmo_surface);
	g_test_add_func ("/workflows/letterbox", test_letterbox);
	g_test_add_func ("/workflows/nested", test_nested_items);
	g_test_add_func ("/workflows/ui-place", test_ui_place);
	g_test_add_func ("/workflows/scene-bar", test_scene_bar);
	g_test_add_func ("/workflows/vcs-panel", test_vcs_panel);
	g_test_add_func ("/workflows/inspector-2", test_second_inspector);
	g_test_add_func ("/workflows/merge-files", test_merge_files);
	g_test_add_func ("/workflows/ipc", test_m10_ipc);
	return g_test_run ();
}
