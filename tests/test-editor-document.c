/* test-editor-document.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests of LrgEditorDocument: YAML paths, edits, undo/redo with
 * gesture merging and groups, dirty tracking, save/reload with comments
 * and unknown keys, external-change detection, line mapping and the
 * per-format validators. Every fixture is written in the real format of
 * its engine loader.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <libregnum.h>

/* ==========================================================================
 * Fixtures: one temporary directory per test
 * ========================================================================== */

typedef struct
{
	gchar *dir;
} Fixture;

static void
fixture_set_up (Fixture       *fixture,
                gconstpointer  data)
{
	g_autoptr(GError) error = NULL;

	(void) data;
	fixture->dir = g_dir_make_tmp ("lrg-doc-test-XXXXXX", &error);
	g_assert_no_error (error);
}

/* removes @path and everything below it */
static void
remove_tree (const gchar *path)
{
	GDir *dir;
	const gchar *name;

	if (g_file_test (path, G_FILE_TEST_IS_DIR))
	{
		dir = g_dir_open (path, 0, NULL);
		while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
		{
			g_autofree gchar *child = g_build_filename (path, name, NULL);

			remove_tree (child);
		}
		if (dir != NULL)
			g_dir_close (dir);
		g_rmdir (path);
	}
	else
	{
		g_unlink (path);
	}
}

static void
fixture_tear_down (Fixture       *fixture,
                   gconstpointer  data)
{
	(void) data;
	remove_tree (fixture->dir);
	g_free (fixture->dir);
}

/* writes @text to @name inside the fixture directory; returns the path */
static gchar *
write_file (Fixture     *fixture,
            const gchar *name,
            const gchar *text)
{
	g_autoptr(GError) error = NULL;
	gchar *path;
	g_autofree gchar *dir = NULL;

	path = g_build_filename (fixture->dir, name, NULL);
	dir = g_path_get_dirname (path);
	g_mkdir_with_parents (dir, 0700);
	g_file_set_contents (path, text, -1, &error);
	g_assert_no_error (error);

	return path;
}

/* opens @text written as @name */
static LrgEditorDocument *
open_fixture (Fixture     *fixture,
              const gchar *name,
              const gchar *text)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *path = write_file (fixture, name, text);
	LrgEditorDocument *doc;

	doc = lrg_editor_document_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (doc);

	return doc;
}

/* the file's current text */
static gchar *
read_file (const gchar *path)
{
	g_autoptr(GError) error = NULL;
	gchar *text = NULL;

	g_file_get_contents (path, &text, NULL, &error);
	g_assert_no_error (error);

	return text;
}

/* finds the first issue whose message contains @needle */
static LrgEditorDocumentIssue *
find_issue (GPtrArray   *issues,
            const gchar *needle)
{
	guint i;

	for (i = 0; i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);

		if (strstr (issue->message, needle) != NULL)
			return issue;
	}

	return NULL;
}

/* like find_issue() but fails the test when there is none */
static LrgEditorDocumentIssue *
need_issue (GPtrArray   *issues,
            const gchar *needle)
{
	LrgEditorDocumentIssue *issue = find_issue (issues, needle);

	if (issue == NULL)
		g_error ("no issue mentions \"%s\"", needle);

	return issue;
}

/* validates and asserts the outcome; returns the issues */
static GPtrArray *
validate (LrgEditorDocument *doc,
          gboolean           expect_ok)
{
	GPtrArray *issues = NULL;
	gboolean ok;
	guint i;

	ok = lrg_editor_document_validate (doc, &issues);
	if (ok != expect_ok)
	{
		for (i = 0; i < issues->len; i++)
		{
			LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);

			g_test_message ("issue %d %s:%u: %s", issue->severity,
			                issue->path != NULL ? issue->path : "(none)", issue->line, issue->message);
		}
	}
	g_assert_cmpint (ok, ==, expect_ok);

	return issues;
}

/* ==========================================================================
 * Fixture texts (each in its engine loader's real format)
 * ========================================================================== */

static const gchar *INPUT_MAP_YAML =
	"# Player controls\n"
	"actions:\n"
	"  jump:\n"
	"    bindings:\n"
	"      - type: keyboard\n"
	"        key: SPACE\n"
	"      - type: gamepad_button\n"
	"        gamepad: 0\n"
	"        button: RIGHT_FACE_DOWN\n"
	"    deadzone: 0.2  # unknown to the engine, kept by the editor\n"
	"  fire:\n"
	"    bindings:\n"
	"      - type: mouse_button\n"
	"        button: LEFT\n"
	"        modifiers: [SHIFT]\n"
	"editor-note: keep me\n";

static const gchar *LOCALE_EN_YAML =
	"code: en\n"
	"name: English\n"
	"strings:\n"
	"  greeting: 'Hello, %s!'\n"
	"  farewell: Goodbye\n"
	"  items:\n"
	"    one: '%d item'\n"
	"    other: '%d items'\n";

static const gchar *LOCALE_DE_YAML =
	"code: de\n"
	"name: Deutsch\n"
	"strings:\n"
	"  greeting: Hallo!\n"
	"  items:\n"
	"    one: '%d Ding'\n"
	"    other: '%d Dinge'\n"
	"  extra: Nur hier\n"
	"translator: Jo\n";

static const gchar *SOUND_BANK_YAML =
	"name: player\n"
	"base_path: sounds\n"
	"sounds:\n"
	"  jump: jump.wav\n"
	"  land: land.ogg\n"
	"volume: 0.8\n";

static const gchar *HUD_LAYOUT_YAML =
	"version: 1\n"
	"pieces:\n"
	"  health:\n"
	"    anchor: top-left\n"
	"    x: 8\n"
	"    y: 8\n"
	"  minimap:\n"
	"    anchor: top-right\n"
	"    x: -8.5\n"
	"    y: 8\n"
	"    locked: true\n";

static const gchar *UI_LAYOUT_YAML =
	"type: vbox\n"
	"id: root\n"
	"children:\n"
	"  - type: label\n"
	"    id: title\n"
	"    text: Hello\n"
	"  - type: button\n"
	"    id: ok\n";

static const gchar *DIALOG_YAML =
	"id: intro\n"
	"title: Intro\n"
	"start: hello\n"
	"nodes:\n"
	"  - id: hello\n"
	"    speaker: Guard\n"
	"    text: Halt!\n"
	"    responses:\n"
	"      - text: Friend\n"
	"        next: friend\n"
	"      - text: Foe\n"
	"        next: foe\n"
	"  - id: friend\n"
	"    text: Pass.\n"
	"    next: end\n"
	"  - id: foe\n"
	"    text: Begone!\n"
	"  - id: end\n"
	"    text: '...'\n"
	"mood: tense\n";

static const gchar *DUNGEON_YAML =
	"version: 1\n"
	"generator: bsp\n"
	"width: 32\n"
	"height: 24\n"
	"seed: 7\n"
	"params:\n"
	"  min-room-size: 4\n"
	"  max-room-size: 9\n"
	"post:\n"
	"  connect: true\n"
	"spawns:\n"
	"  - { kind: goblin, count: 3, min-distance: 4, rooms-only: false }\n";

static const gchar *THEME_YAML =
	"# a warm theme\n"
	"preset: midnight\n"
	"name: ember\n"
	"dark: true\n"
	"accent: '#ff7a3d'\n"
	"colors:\n"
	"  panel: '#1d1512'\n";

/* ==========================================================================
 * Paths
 * ========================================================================== */

static void
test_path_escaping (void)
{
	g_autoptr(LrgEditorDocument) doc = lrg_editor_document_new (LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *escaped = NULL;
	g_autofree gchar *joined = NULL;
	g_auto(GStrv) split = NULL;
	g_auto(GStrv) keys = NULL;

	escaped = lrg_editor_document_path_escape ("a/b~c");
	g_assert_cmpstr (escaped, ==, "a~1b~0c");
	joined = lrg_editor_document_path_join ("", "x/y");
	g_assert_cmpstr (joined, ==, "/x~1y");

	split = lrg_editor_document_path_split ("/a~1b~0c/0/~01", &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_strv_length (split), ==, 3);
	g_assert_cmpstr (split[0], ==, "a/b~c");
	g_assert_cmpstr (split[1], ==, "0");
	g_assert_cmpstr (split[2], ==, "~1");

	/* malformed paths */
	g_assert_null (lrg_editor_document_path_split ("no-slash", &error));
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH);
	g_clear_error (&error);
	g_assert_null (lrg_editor_document_path_split ("/bad~2", &error));
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH);
	g_clear_error (&error);

	/* keys with '/' and '~' through the document */
	g_assert_true (lrg_editor_document_add_key (doc, "", "a/b~c", "{inner: [1, 2]}", -1, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/a~1b~0c/inner/1"), ==, "2");
	keys = lrg_editor_document_get_keys (doc, "/");
	g_assert_cmpstrv (keys, ((const gchar *[]) { "a/b~c", NULL }));

	/* root, missing and wrong-kind lookups */
	g_assert_cmpint (lrg_editor_document_get_kind (doc, ""), ==, LRG_EDITOR_DOCUMENT_NODE_MAPPING);
	g_assert_cmpint (lrg_editor_document_get_kind (doc, "/a~1b~0c/inner"), ==, LRG_EDITOR_DOCUMENT_NODE_SEQUENCE);
	g_assert_cmpint (lrg_editor_document_get_kind (doc, "/a~1b~0c/inner/5"), ==, LRG_EDITOR_DOCUMENT_NODE_MISSING);
	g_assert_cmpint (lrg_editor_document_get_kind (doc, "/a~1b~0c/inner/x"), ==, LRG_EDITOR_DOCUMENT_NODE_MISSING);
	g_assert_false (lrg_editor_document_set_string (doc, "/a~1b~0c/inner/0/deeper", "x", 0, &error));
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND);
	g_clear_error (&error);
	g_assert_false (lrg_editor_document_set_string (doc, "/a~1b~0c/inner/7", "x", 0, &error));
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND);
	g_clear_error (&error);

	/* "-" appends */
	g_assert_true (lrg_editor_document_set_int (doc, "/a~1b~0c/inner/-", 3, 0, &error));
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/a~1b~0c/inner"), ==, 3);
}

static void
test_detect_format (void)
{
	g_assert_cmpint (lrg_editor_document_detect_format ("x/controls.input.yaml", NULL, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP);
	g_assert_cmpint (lrg_editor_document_detect_format ("x/en.locale.yml", NULL, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_LOCALE);
	g_assert_cmpint (lrg_editor_document_detect_format ("data/dungeon.yaml", NULL, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON);
	g_assert_cmpint (lrg_editor_document_detect_format ("a.yaml", INPUT_MAP_YAML, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP);
	g_assert_cmpint (lrg_editor_document_detect_format ("a.yaml", LOCALE_EN_YAML, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_LOCALE);
	g_assert_cmpint (lrg_editor_document_detect_format (NULL, SOUND_BANK_YAML, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK);
	g_assert_cmpint (lrg_editor_document_detect_format (NULL, HUD_LAYOUT_YAML, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT);
	/* content beats the directory: data/ui/hud.yaml is a UI layout */
	g_assert_cmpint (lrg_editor_document_detect_format ("data/ui/hud.yaml", UI_LAYOUT_YAML, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT);
	g_assert_cmpint (lrg_editor_document_detect_format (NULL, DIALOG_YAML, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE);
	g_assert_cmpint (lrg_editor_document_detect_format (NULL, DUNGEON_YAML, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON);
	g_assert_cmpint (lrg_editor_document_detect_format (NULL, THEME_YAML, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_THEME);
	g_assert_cmpint (lrg_editor_document_detect_format ("data/themes/x.yaml", "{}", -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_THEME);
	g_assert_cmpint (lrg_editor_document_detect_format ("x.yaml", "just: data", -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN);

	g_assert_cmpstr (lrg_editor_document_format_to_string (LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK), ==,
	                 "sound-bank");
	g_assert_cmpint (lrg_editor_document_format_from_string ("dialog-tree"), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE);
	g_assert_cmpint (lrg_editor_document_format_from_string ("nope"), ==, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN);
}

/* every skeleton of lrg_editor_document_new() passes its own validator */
static void
test_new_skeletons (void)
{
	LrgEditorDocumentFormat format;

	for (format = LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN; format <= LRG_EDITOR_DOCUMENT_FORMAT_THEME; format++)
	{
		g_autoptr(LrgEditorDocument) doc = lrg_editor_document_new (format);
		g_autoptr(GPtrArray) issues = NULL;

		g_test_message ("skeleton %s", lrg_editor_document_format_to_string (format));
		g_assert_cmpint (lrg_editor_document_get_format (doc), ==, format);
		g_assert_false (lrg_editor_document_get_dirty (doc));
		g_assert_null (lrg_editor_document_get_path (doc));
		g_assert_cmpstr (lrg_editor_document_get_display_name (doc), ==, "untitled");
		issues = validate (doc, TRUE);
	}
}

static void
test_open_errors (Fixture       *fixture,
                  gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *bad = NULL;
	g_autofree gchar *missing = NULL;
	LrgEditorDocument *doc;

	(void) data;
	bad = write_file (fixture, "bad.yaml", "a: [1, 2\nb: 3\n");
	doc = lrg_editor_document_new_from_file (bad, &error);
	g_assert_null (doc);
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_PARSE);
	g_assert_nonnull (strstr (error->message, "line"));
	g_clear_error (&error);

	missing = g_build_filename (fixture->dir, "nope.yaml", NULL);
	g_assert_null (lrg_editor_document_new_from_file (missing, &error));
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_IO);
}

/* ==========================================================================
 * Undo / redo / merge / groups / dirty
 * ========================================================================== */

static void
count_signal (gpointer instance,
              gpointer arg,
              gpointer user_data)
{
	(void) instance;
	(void) arg;
	(*(guint *) user_data)++;
}

/* the same for signals without arguments */
static void
count_signal0 (gpointer instance,
               gpointer user_data)
{
	(void) instance;
	(*(guint *) user_data)++;
}

static void
test_undo_merge (void)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GError) error = NULL;

	doc = lrg_editor_document_new_from_data ("hud:\n  x: 1\n  y: 2\n", -1,
	                                         LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, &error);
	g_assert_no_error (error);

	/* a drag: four steps on one path, one gesture -> one undo step */
	g_assert_true (lrg_editor_document_set_double (doc, "/hud/x", 1.5, 7, &error));
	g_assert_true (lrg_editor_document_set_double (doc, "/hud/x", 2.5, 7, &error));
	g_assert_true (lrg_editor_document_set_double (doc, "/hud/x", 3.0, 7, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/hud/x"), ==, "3.0");
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc), ==, "Set /hud/x");

	/* another path with the same gesture does not merge */
	g_assert_true (lrg_editor_document_set_int (doc, "/hud/y", 9, 7, &error));
	/* gesture 0 never merges */
	g_assert_true (lrg_editor_document_set_int (doc, "/hud/y", 10, 0, &error));
	g_assert_true (lrg_editor_document_set_int (doc, "/hud/y", 11, 0, &error));

	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/hud/y"), ==, "10");
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/hud/y"), ==, "9");
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/hud/y"), ==, "2");
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/hud/x"), ==, "1");
	g_assert_false (lrg_editor_document_can_undo (doc));
	g_assert_false (lrg_editor_document_undo (doc));
	g_assert_false (lrg_editor_document_get_dirty (doc));

	/* redo the whole merged gesture at once */
	g_assert_true (lrg_editor_document_redo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/hud/x"), ==, "3.0");
	g_assert_true (lrg_editor_document_get_dirty (doc));

	/* an undo breaks a gesture: the next step with the same id is new */
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_set_double (doc, "/hud/x", 4.0, 7, &error));
	g_assert_false (lrg_editor_document_can_redo (doc));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/hud/x"), ==, "1");

	/* a key created by a gesture merges with the gesture's later sets */
	g_assert_true (lrg_editor_document_set_int (doc, "/hud/z", 1, 3, &error));
	g_assert_true (lrg_editor_document_set_int (doc, "/hud/z", 2, 3, &error));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_false (lrg_editor_document_has_path (doc, "/hud/z"));
	g_assert_true (lrg_editor_document_redo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/hud/z"), ==, "2");
}

static void
test_undo_group (void)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) keys = NULL;

	doc = lrg_editor_document_new_from_data ("list: [a, b, c]\nmap: {k1: 1, k2: 2, k3: 3}\n", -1,
	                                         LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, &error);
	g_assert_no_error (error);

	lrg_editor_document_begin_group (doc, "Shuffle");
	g_assert_true (lrg_editor_document_insert_item (doc, "/list", 1, "x", &error));
	g_assert_true (lrg_editor_document_move_item (doc, "/list", 0, 3, &error));
	lrg_editor_document_begin_group (doc, "Inner");
	g_assert_true (lrg_editor_document_rename_key (doc, "/map", "k2", "middle", &error));
	g_assert_true (lrg_editor_document_add_key (doc, "/map", "first", "0", 0, &error));
	g_assert_true (lrg_editor_document_remove (doc, "/map/k3", &error));
	lrg_editor_document_end_group (doc);
	g_assert_true (lrg_editor_document_remove (doc, "/list/0", &error));
	g_assert_no_error (error);

	/* undo is refused while a group is open */
	g_assert_false (lrg_editor_document_can_undo (doc));
	g_assert_true (lrg_editor_document_get_dirty (doc));
	lrg_editor_document_end_group (doc);

	/* list: [a, x, b, c] -> move 0 to 3 -> [x, b, c, a] -> remove 0 -> [b, c, a] */
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/list/0"), ==, "b");
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/list/2"), ==, "a");
	keys = lrg_editor_document_get_keys (doc, "/map");
	g_assert_cmpstrv (keys, ((const gchar *[]) { "first", "k1", "middle", NULL }));
	g_clear_pointer (&keys, g_strfreev);
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc), ==, "Shuffle");

	/* one undo reverts the whole group, order included */
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_false (lrg_editor_document_can_undo (doc));
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/list"), ==, 3);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/list/0"), ==, "a");
	keys = lrg_editor_document_get_keys (doc, "/map");
	g_assert_cmpstrv (keys, ((const gchar *[]) { "k1", "k2", "k3", NULL }));
	g_clear_pointer (&keys, g_strfreev);
	g_assert_false (lrg_editor_document_get_dirty (doc));

	g_assert_true (lrg_editor_document_redo (doc));
	keys = lrg_editor_document_get_keys (doc, "/map");
	g_assert_cmpstrv (keys, ((const gchar *[]) { "first", "k1", "middle", NULL }));

	/* an empty group adds no step */
	lrg_editor_document_begin_group (doc, NULL);
	lrg_editor_document_end_group (doc);
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc), ==, "Shuffle");
}

static void
test_dirty_tracking (Fixture       *fixture,
                     gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GError) error = NULL;
	guint dirty_changes;
	guint changes;
	guint saves;

	(void) data;
	doc = open_fixture (fixture, "plain.yaml", "a: 1\nb: 2\n");
	dirty_changes = 0;
	changes = 0;
	saves = 0;
	g_signal_connect (doc, "dirty-changed", G_CALLBACK (count_signal), &dirty_changes);
	g_signal_connect (doc, "changed", G_CALLBACK (count_signal), &changes);
	g_signal_connect (doc, "saved", G_CALLBACK (count_signal), &saves);

	g_assert_true (lrg_editor_document_set_int (doc, "/a", 5, 0, &error));
	g_assert_true (lrg_editor_document_get_dirty (doc));
	g_assert_cmpuint (dirty_changes, ==, 1);
	g_assert_cmpuint (changes, ==, 1);

	g_assert_true (lrg_editor_document_set_int (doc, "/b", 6, 0, &error));
	g_assert_cmpuint (dirty_changes, ==, 1);

	/* save makes the current state clean */
	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_editor_document_get_dirty (doc));
	g_assert_cmpuint (dirty_changes, ==, 2);
	g_assert_cmpuint (saves, ==, 1);

	/* undo past the save is dirty, redo back to it is clean */
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_get_dirty (doc));
	g_assert_true (lrg_editor_document_redo (doc));
	g_assert_false (lrg_editor_document_get_dirty (doc));

	/* clearing the history keeps the dirty flag either way */
	g_assert_true (lrg_editor_document_set_int (doc, "/a", 7, 0, &error));
	lrg_editor_document_clear_undo (doc);
	g_assert_true (lrg_editor_document_get_dirty (doc));
	g_assert_false (lrg_editor_document_can_undo (doc));
	g_assert_true (lrg_editor_document_save (doc, &error));
	lrg_editor_document_clear_undo (doc);
	g_assert_false (lrg_editor_document_get_dirty (doc));

	/* the undo limit drops the oldest steps */
	lrg_editor_document_set_undo_limit (doc, 2);
	g_assert_true (lrg_editor_document_set_int (doc, "/a", 10, 0, &error));
	g_assert_true (lrg_editor_document_set_int (doc, "/a", 11, 0, &error));
	g_assert_true (lrg_editor_document_set_int (doc, "/a", 12, 0, &error));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_false (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/a"), ==, "10");
	g_assert_true (lrg_editor_document_get_dirty (doc));

	/* read-only refuses edits and saves */
	lrg_editor_document_set_read_only (doc, TRUE);
	g_assert_false (lrg_editor_document_set_int (doc, "/a", 1, 0, &error));
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_READ_ONLY);
	g_clear_error (&error);
	g_assert_false (lrg_editor_document_save (doc, &error));
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_READ_ONLY);
}

static void
test_values (void)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GError) error = NULL;
	GValue value = G_VALUE_INIT;

	doc = lrg_editor_document_new_from_data ("b: yes\ni: -42\nd: 0.25\ns: 'true'\nn: ~\nt: plain\n", -1,
	                                         LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, &error);
	g_assert_no_error (error);

	g_assert_true (lrg_editor_document_get_value (doc, "/b", &value));
	g_assert_true (G_VALUE_HOLDS_BOOLEAN (&value) && g_value_get_boolean (&value));
	g_value_unset (&value);
	g_assert_true (lrg_editor_document_get_value (doc, "/i", &value));
	g_assert_cmpint (g_value_get_int64 (&value), ==, -42);
	g_value_unset (&value);
	g_assert_true (lrg_editor_document_get_value (doc, "/d", &value));
	g_assert_cmpfloat (g_value_get_double (&value), ==, 0.25);
	g_value_unset (&value);
	g_assert_true (lrg_editor_document_get_value (doc, "/s", &value));
	g_assert_cmpstr (g_value_get_string (&value), ==, "true");
	g_value_unset (&value);
	g_assert_true (lrg_editor_document_get_value (doc, "/n", &value));
	g_assert_true (G_VALUE_HOLDS_POINTER (&value));
	g_value_unset (&value);
	g_assert_cmpint (lrg_editor_document_get_kind (doc, "/n"), ==, LRG_EDITOR_DOCUMENT_NODE_NULL);

	/* typed writes round-trip through the serialized text */
	g_value_init (&value, G_TYPE_FLOAT);
	g_value_set_float (&value, 0.5f);
	g_assert_true (lrg_editor_document_set_value (doc, "/d", &value, 0, &error));
	g_value_unset (&value);
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, "42");
	g_assert_true (lrg_editor_document_set_value (doc, "/t", &value, 0, &error));
	g_value_unset (&value);
	g_assert_true (lrg_editor_document_set_boolean (doc, "/b", FALSE, 0, &error));
	g_assert_true (lrg_editor_document_set_null (doc, "/i", 0, &error));
	g_assert_true (lrg_editor_document_set_double (doc, "/whole", 2.0, 0, &error));
	g_assert_no_error (error);

	{
		g_autofree gchar *text = lrg_editor_document_to_data (doc, NULL);
		g_autoptr(LrgEditorDocument) again = NULL;

		again = lrg_editor_document_new_from_data (text, -1, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, &error);
		g_assert_no_error (error);
		g_assert_true (lrg_editor_document_get_value (again, "/t", &value));
		g_assert_true (G_VALUE_HOLDS_STRING (&value));   /* "42" stays a string */
		g_assert_cmpstr (g_value_get_string (&value), ==, "42");
		g_value_unset (&value);
		g_assert_true (lrg_editor_document_get_value (again, "/whole", &value));
		g_assert_true (G_VALUE_HOLDS_DOUBLE (&value));   /* 2.0 stays a float */
		g_value_unset (&value);
		g_assert_true (lrg_editor_document_get_value (again, "/b", &value));
		g_assert_false (g_value_get_boolean (&value));
		g_value_unset (&value);
		g_assert_cmpint (lrg_editor_document_get_kind (again, "/i"), ==, LRG_EDITOR_DOCUMENT_NODE_NULL);
		g_assert_cmpstr (lrg_editor_document_get_scalar (again, "/d"), ==, "0.5");
	}
}

/* ==========================================================================
 * Save / reload / comments / external changes / lines / duplicates
 * ========================================================================== */

static void
test_save_preserves (Fixture       *fixture,
                     gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(LrgEditorDocument) again = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_auto(GStrv) keys = NULL;
	const gchar *path;

	(void) data;
	doc = open_fixture (fixture, "controls.input.yaml", INPUT_MAP_YAML);
	path = lrg_editor_document_get_path (doc);

	/* rename an action: position, value and comments are kept */
	g_assert_true (lrg_editor_document_rename_key (doc, "/actions", "jump", "leap", &error));
	g_assert_true (lrg_editor_document_set_string (doc, "/actions/leap/bindings/0/key", "W", 0, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_no_error (error);

	text = read_file (path);
	g_assert_nonnull (strstr (text, "# Player controls"));
	g_assert_nonnull (strstr (text, "# unknown to the engine, kept by the editor"));
	g_assert_nonnull (strstr (text, "editor-note: keep me"));

	again = lrg_editor_document_new_from_file (path, &error);
	g_assert_no_error (error);
	keys = lrg_editor_document_get_keys (again, "/actions");
	g_assert_cmpstrv (keys, ((const gchar *[]) { "leap", "fire", NULL }));
	g_clear_pointer (&keys, g_strfreev);
	keys = lrg_editor_document_get_keys (again, "/actions/leap");
	g_assert_cmpstrv (keys, ((const gchar *[]) { "bindings", "deadzone", NULL }));
	g_assert_cmpstr (lrg_editor_document_get_scalar (again, "/actions/leap/deadzone"), ==, "0.2");
	g_assert_cmpstr (lrg_editor_document_get_scalar (again, "/actions/leap/bindings/0/key"), ==, "W");

	/* saving without a path needs save-as */
	{
		g_autoptr(LrgEditorDocument) untitled = lrg_editor_document_new (LRG_EDITOR_DOCUMENT_FORMAT_THEME);
		g_autofree gchar *target = g_build_filename (fixture->dir, "sub", "new.theme.yaml", NULL);

		g_assert_false (lrg_editor_document_save (untitled, &error));
		g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NO_PATH);
		g_clear_error (&error);
		g_assert_true (lrg_editor_document_save_as (untitled, target, &error));
		g_assert_no_error (error);
		g_assert_cmpstr (lrg_editor_document_get_display_name (untitled), ==, "new.theme.yaml");
		g_assert_true (g_file_test (target, G_FILE_TEST_IS_REGULAR));
	}
}

/* pushes the file's mtime forward so the change is visible on any fs */
static void
bump_mtime (const gchar *path,
            guint64      seconds_ahead)
{
	g_autoptr(GFile) file = g_file_new_for_path (path);
	g_autoptr(GError) error = NULL;
	guint64 now = (guint64) (g_get_real_time () / G_USEC_PER_SEC);

	g_file_set_attribute_uint64 (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, now + seconds_ahead,
	                             G_FILE_QUERY_INFO_NONE, NULL, &error);
	g_assert_no_error (error);
}

static void
test_reload_external (Fixture       *fixture,
                      gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *path = NULL;
	guint file_changes;
	guint reloads;

	(void) data;
	doc = open_fixture (fixture, "en.locale.yaml", LOCALE_EN_YAML);
	path = g_strdup (lrg_editor_document_get_path (doc));
	file_changes = 0;
	reloads = 0;
	g_signal_connect (doc, "file-changed", G_CALLBACK (count_signal0), &file_changes);
	g_signal_connect (doc, "reloaded", G_CALLBACK (count_signal0), &reloads);

	/* nothing changed yet; our own save is not an external change */
	g_assert_false (lrg_editor_document_check_external_change (doc));
	g_assert_true (lrg_editor_document_set_string (doc, "/strings/farewell", "Bye", 0, &error));
	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_false (lrg_editor_document_check_external_change (doc));

	/* a touch without a content change is not a change either */
	bump_mtime (path, 50);
	g_assert_false (lrg_editor_document_check_external_change (doc));

	/* another program rewrites the file */
	g_file_set_contents (path, "code: en\nname: English\nstrings:\n  farewell: Later\n", -1, &error);
	g_assert_no_error (error);
	bump_mtime (path, 100);
	g_assert_true (lrg_editor_document_check_external_change (doc));
	g_assert_true (lrg_editor_document_check_external_change (doc));
	g_assert_cmpuint (file_changes, ==, 1);

	/* unsaved edits are dropped by a reload */
	g_assert_true (lrg_editor_document_set_string (doc, "/strings/farewell", "Mine", 0, &error));
	g_assert_true (lrg_editor_document_reload (doc, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (reloads, ==, 1);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/strings/farewell"), ==, "Later");
	g_assert_false (lrg_editor_document_get_dirty (doc));
	g_assert_false (lrg_editor_document_can_undo (doc));
	g_assert_false (lrg_editor_document_check_external_change (doc));

	/* a failed reload leaves the document alone */
	g_file_set_contents (path, "strings: [unclosed\n", -1, &error);
	g_assert_false (lrg_editor_document_reload (doc, &error));
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_PARSE);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/strings/farewell"), ==, "Later");
}

static void
test_lines_and_duplicates (Fixture       *fixture,
                           gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *at = NULL;
	g_autofree gchar *text = NULL;
	g_auto(GStrv) lines = NULL;
	LrgEditorDocumentIssue *issue;
	guint line;
	guint column;

	(void) data;
	doc = open_fixture (fixture, "dup.input.yaml",
	                    "actions:\n"
	                    "  jump:\n"
	                    "    bindings: [{type: keyboard, key: SPACE}]\n"
	                    "  jump:\n"
	                    "    bindings: [{type: keyboard, key: W}]\n");

	/* yaml-glib keeps one of the two; the walk reports the repeat */
	issues = validate (doc, TRUE);
	issue = need_issue (issues, "duplicate key 'jump'");
	g_assert_cmpint (issue->severity, ==, LRG_EDITOR_PROBLEM_WARNING);
	g_assert_cmpuint (issue->line, ==, 4);
	g_assert_cmpstr (issue->path, ==, "/actions/jump");

	/* path <-> line in the serialized text */
	text = lrg_editor_document_to_data (doc, NULL);
	lines = g_strsplit (text, "\n", -1);
	g_assert_true (lrg_editor_document_get_line (doc, "/actions/jump/bindings/0/key", &line, &column));
	g_assert_nonnull (strstr (lines[line - 1], "key"));
	at = lrg_editor_document_get_path_at_line (doc, line);
	g_assert_cmpstr (at, ==, "/actions/jump/bindings/0/key");
	g_assert_true (lrg_editor_document_get_line (doc, "/", &line, NULL));
	g_assert_cmpuint (line, ==, 1);
	g_assert_false (lrg_editor_document_get_line (doc, "/nope", NULL, NULL));

	/* duplicates are gone once saved */
	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);
	g_assert_null (find_issue (issues, "duplicate key"));
}

/* ==========================================================================
 * Per-format: load, edit, undo, save, reload, validate pass and fail
 * ========================================================================== */

static void
test_format_input_map (Fixture       *fixture,
                       gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) actions = NULL;
	LrgEditorDocumentIssue *issue;

	(void) data;
	doc = open_fixture (fixture, "controls.input.yaml", INPUT_MAP_YAML);
	g_assert_cmpint (lrg_editor_document_get_format (doc), ==, LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP);
	actions = lrg_editor_document_input_actions (doc);
	g_assert_cmpstrv (actions, ((const gchar *[]) { "jump", "fire", NULL }));
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);

	/* add a binding through a group: insert + set */
	lrg_editor_document_begin_group (doc, "Add binding");
	g_assert_true (lrg_editor_document_insert_item (doc, "/actions/fire/bindings", -1,
	                                                "{type: keyboard, key: ENTER}", &error));
	g_assert_true (lrg_editor_document_set_string (doc, "/actions/fire/bindings/1/key", "F", 0, &error));
	lrg_editor_document_end_group (doc);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/actions/fire/bindings"), ==, 2);
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/actions/fire/bindings"), ==, 1);
	g_assert_true (lrg_editor_document_redo (doc));

	/* an unknown key name and the same key on two actions */
	g_assert_true (lrg_editor_document_set_string (doc, "/actions/jump/bindings/0/key", "NOT_A_KEY", 0, &error));
	g_assert_true (lrg_editor_document_set_string (doc, "/actions/fire/bindings/1/key", "SPACE", 0, &error));
	g_assert_true (lrg_editor_document_insert_item (doc, "/actions/jump/bindings", -1,
	                                                "{type: keyboard, key: SPACE}", &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	issue = need_issue (issues, "unknown key name 'NOT_A_KEY'");
	g_assert_cmpstr (issue->path, ==, "/actions/jump/bindings/0/key");
	g_assert_cmpuint (issue->line, >, 0);
	issue = need_issue (issues, "also bound at /actions/jump/bindings/2");
	g_assert_cmpint (issue->severity, ==, LRG_EDITOR_PROBLEM_WARNING);
	g_assert_cmpstr (issue->path, ==, "/actions/fire/bindings/1");

	/* an unknown binding type is an error at its type */
	g_assert_true (lrg_editor_document_set_string (doc, "/actions/jump/bindings/0/key", "SPACE", 0, &error));
	g_assert_true (lrg_editor_document_remove (doc, "/actions/jump/bindings/2", &error));
	g_assert_true (lrg_editor_document_remove (doc, "/actions/fire/bindings/1", &error));
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);
	g_assert_true (lrg_editor_document_add_key (doc, "/actions", "broken",
	                                            "{bindings: [{type: telepathy}]}", -1, &error));
	g_test_expect_message (LRG_LOG_DOMAIN_INPUT, G_LOG_LEVEL_WARNING, "Unknown binding type: telepathy");
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	g_test_assert_expected_messages ();
	issue = need_issue (issues, "unknown binding type 'telepathy'");
	g_assert_cmpstr (issue->path, ==, "/actions/broken/bindings/0/type");

	/* save -> reload round trip */
	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_true (lrg_editor_document_reload (doc, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/broken/bindings/0/type"), ==, "telepathy");
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/editor-note"), ==, "keep me");
}

static void
test_format_locale (Fixture       *fixture,
                    gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) en = NULL;
	g_autoptr(LrgEditorDocument) de = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) keys = NULL;
	g_auto(GStrv) missing = NULL;
	LrgEditorDocumentIssue *issue;

	(void) data;
	en = open_fixture (fixture, "locales/en.yaml", LOCALE_EN_YAML);
	de = open_fixture (fixture, "locales/de.yaml", LOCALE_DE_YAML);
	g_assert_cmpint (lrg_editor_document_get_format (de), ==, LRG_EDITOR_DOCUMENT_FORMAT_LOCALE);

	keys = lrg_editor_document_locale_keys (en);
	g_assert_cmpstrv (keys, ((const gchar *[]) { "greeting", "farewell", "items", NULL }));
	missing = lrg_editor_document_locale_missing_keys (de, en);
	g_assert_cmpstrv (missing, ((const gchar *[]) { "farewell", NULL }));

	/* against the reference: warnings, not errors */
	lrg_editor_document_set_reference (de, en);
	issues = validate (de, TRUE);
	issue = need_issue (issues, "missing key 'farewell' (present in en.yaml)");
	g_assert_cmpint (issue->severity, ==, LRG_EDITOR_PROBLEM_WARNING);
	issue = need_issue (issues, "placeholder");
	g_assert_cmpstr (issue->path, ==, "/strings/greeting");
	issue = need_issue (issues, "'extra' is not in en.yaml");
	g_assert_cmpint (issue->severity, ==, LRG_EDITOR_PROBLEM_INFO);

	/* fix them in one gesture-free edit each, then undo one */
	g_assert_true (lrg_editor_document_add_key (de, "/strings", "farewell", "Tschüss", 1, &error));
	g_assert_true (lrg_editor_document_set_string (de, "/strings/greeting", "Hallo, %s!", 0, &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (de, TRUE);
	g_assert_null (find_issue (issues, "missing key"));
	g_assert_null (find_issue (issues, "placeholder"));
	g_clear_pointer (&keys, g_strfreev);
	keys = lrg_editor_document_locale_keys (de);
	g_assert_cmpstrv (keys, ((const gchar *[]) { "greeting", "farewell", "items", "extra", NULL }));

	/* the loader's own error: a locale needs a name */
	g_assert_true (lrg_editor_document_remove (de, "/name", &error));
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (de, FALSE);
	issue = need_issue (issues, "missing 'name'");
	g_assert_null (strstr (issue->message, "lrg-document-"));   /* temp file name hidden */
	g_assert_true (lrg_editor_document_undo (de));

	/* save -> reload keeps the unknown translator key and the non-ASCII text */
	g_assert_true (lrg_editor_document_save (de, &error));
	g_assert_true (lrg_editor_document_reload (de, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (de, "/translator"), ==, "Jo");
	g_assert_cmpstr (lrg_editor_document_get_scalar (de, "/strings/farewell"), ==, "Tschüss");
}

static void
test_format_sound_bank (Fixture       *fixture,
                        gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *wav = NULL;
	g_autofree gchar *ogg = NULL;
	g_autofree gchar *resolved = NULL;
	g_autofree gchar *expected = NULL;
	LrgEditorDocumentIssue *issue;

	(void) data;
	wav = write_file (fixture, "audio/sounds/jump.wav", "RIFF");
	ogg = write_file (fixture, "audio/sounds/land.ogg", "OggS");
	doc = open_fixture (fixture, "audio/player.bank.yaml", SOUND_BANK_YAML);

	resolved = lrg_editor_document_sound_bank_resolve (doc, "jump");
	g_assert_cmpstr (resolved, ==, wav);
	issues = validate (doc, TRUE);

	/* a sound whose file does not exist */
	g_assert_true (lrg_editor_document_set_string (doc, "/sounds/hurt", "hurt.wav", 0, &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	issue = need_issue (issues, "sound file not found");
	g_assert_cmpstr (issue->path, ==, "/sounds/hurt");

	/* the base-dir property moves resolution */
	lrg_editor_document_set_base_dir (doc, fixture->dir);
	g_clear_pointer (&resolved, g_free);
	resolved = lrg_editor_document_sound_bank_resolve (doc, "jump");
	expected = g_build_filename (fixture->dir, "sounds", "jump.wav", NULL);
	g_assert_cmpstr (resolved, ==, expected);
	lrg_editor_document_set_base_dir (doc, NULL);

	/* undo, then the loader's error for a missing name */
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_remove (doc, "/name", &error));
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	g_assert_nonnull (find_issue (issues, "missing 'name'"));
	g_assert_true (lrg_editor_document_undo (doc));

	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_true (lrg_editor_document_reload (doc, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/volume"), ==, "0.8");
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);
}

static void
test_format_hud_layout (Fixture       *fixture,
                        gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(GError) error = NULL;
	LrgEditorDocumentIssue *issue;

	(void) data;
	doc = open_fixture (fixture, "player.hud.yaml", HUD_LAYOUT_YAML);
	issues = validate (doc, TRUE);

	/* drag a piece: one merged undo step */
	g_assert_true (lrg_editor_document_set_double (doc, "/pieces/health/x", 10.0, 42, &error));
	g_assert_true (lrg_editor_document_set_double (doc, "/pieces/health/x", 14.5, 42, &error));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/pieces/health/x"), ==, "8");
	g_assert_true (lrg_editor_document_redo (doc));

	/* every structural problem, each with its path */
	g_assert_true (lrg_editor_document_set_string (doc, "/pieces/health/anchor", "sideways", 0, &error));
	g_assert_true (lrg_editor_document_add_key (doc, "/pieces/minimap", "scale", "2", -1, &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	issue = need_issue (issues, "unknown anchor 'sideways'");
	g_assert_cmpstr (issue->path, ==, "/pieces/health/anchor");
	issue = need_issue (issues, "unknown key 'scale'");
	g_assert_cmpstr (issue->path, ==, "/pieces/minimap/scale");

	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_true (lrg_editor_document_reload (doc, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/pieces/health/x"), ==, "14.5");
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);
}

static void
test_format_ui_layout (Fixture       *fixture,
                       gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(GError) error = NULL;
	LrgEditorDocumentIssue *issue;

	(void) data;
	doc = open_fixture (fixture, "menu.ui.yaml", UI_LAYOUT_YAML);
	issues = validate (doc, TRUE);

	/* replace a subtree from YAML text */
	g_assert_true (lrg_editor_document_replace (doc, "/children/1", "{type: label, id: footer, text: Bye}",
	                                            &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/children/1/text"), ==, "Bye");
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/children/1/type"), ==, "button");
	g_assert_true (lrg_editor_document_redo (doc));

	/* the loader's path "children[0]" becomes /children/0 */
	g_assert_true (lrg_editor_document_add_key (doc, "/children/0", "colour", "red", -1, &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	issue = need_issue (issues, "unknown property 'colour'");
	g_assert_cmpstr (issue->path, ==, "/children/0");

	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_true (lrg_editor_document_reload (doc, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/children/1/id"), ==, "footer");
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);
}

static void
test_format_dialog_tree (Fixture       *fixture,
                         gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(GPtrArray) edges = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) nodes = NULL;
	LrgEditorDocumentEdge *edge;
	LrgEditorDocumentIssue *issue;

	(void) data;
	doc = open_fixture (fixture, "intro.dialog.yaml", DIALOG_YAML);
	issues = validate (doc, TRUE);

	nodes = lrg_editor_document_dialog_nodes (doc);
	g_assert_cmpstrv (nodes, ((const gchar *[]) { "hello", "friend", "foe", "end", NULL }));
	edges = lrg_editor_document_dialog_edges (doc);
	g_assert_cmpuint (edges->len, ==, 4);
	edge = g_ptr_array_index (edges, 1);
	g_assert_cmpstr (edge->from, ==, "hello");
	g_assert_cmpstr (edge->to, ==, "foe");
	g_assert_cmpint (edge->response, ==, 1);
	g_assert_cmpstr (edge->path, ==, "/nodes/0/responses/1/next");
	edge = g_ptr_array_index (edges, 3);       /* foe chains to end implicitly */
	g_assert_cmpstr (edge->from, ==, "foe");
	g_assert_cmpstr (edge->to, ==, "end");
	g_assert_null (edge->path);

	/* reorder nodes, then a dangling reference */
	g_assert_true (lrg_editor_document_move_item (doc, "/nodes", 3, 1, &error));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/nodes/1/id"), ==, "end");
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_set_string (doc, "/nodes/1/next", "nowhere", 0, &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	issue = need_issue (issues, "'friend' leads to missing node 'nowhere'");
	g_assert_cmpstr (issue->path, ==, "/nodes/1/next");
	g_assert_cmpuint (issue->line, >, 0);

	/* a duplicate id and an unreachable node */
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_insert_item (doc, "/nodes", -1, "{id: hello, text: again, next: end}",
	                                                &error));
	g_assert_true (lrg_editor_document_insert_item (doc, "/nodes", -1, "{id: island, text: alone}", &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	issue = need_issue (issues, "duplicate node id 'hello'");
	g_assert_cmpstr (issue->path, ==, "/nodes/4/id");
	issue = need_issue (issues, "node 'island' cannot be reached");
	g_assert_cmpint (issue->severity, ==, LRG_EDITOR_PROBLEM_WARNING);

	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_true (lrg_editor_document_reload (doc, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/mood"), ==, "tense");
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);
}

static void
test_format_dungeon (Fixture       *fixture,
                     gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(GError) error = NULL;
	LrgEditorDocumentIssue *issue;

	(void) data;
	doc = open_fixture (fixture, "data/dungeon.yaml", DUNGEON_YAML);
	g_assert_cmpint (lrg_editor_document_get_format (doc), ==, LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON);
	issues = validate (doc, TRUE);

	/* a slider gesture on the seed */
	g_assert_true (lrg_editor_document_set_int (doc, "/seed", 8, 5, &error));
	g_assert_true (lrg_editor_document_set_int (doc, "/seed", 9, 5, &error));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/seed"), ==, "7");
	g_assert_true (lrg_editor_document_redo (doc));

	/* the strict loader names the offending key */
	g_assert_true (lrg_editor_document_add_key (doc, "/params", "bogus-param", "3", -1, &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	issue = need_issue (issues, "bogus-param");
	g_assert_cmpstr (issue->path, ==, "/params/bogus-param");

	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_true (lrg_editor_document_reload (doc, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/seed"), ==, "9");
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/spawns/0/kind"), ==, "goblin");
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);
}

static void
test_format_theme (Fixture       *fixture,
                   gconstpointer  data)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;

	(void) data;
	doc = open_fixture (fixture, "ember.theme.yaml", THEME_YAML);
	issues = validate (doc, TRUE);

	/* colours stay quoted strings ('#' starts a YAML comment) */
	g_assert_true (lrg_editor_document_set_string (doc, "/colors/window", "#120d0b", 0, &error));
	g_assert_true (lrg_editor_document_set_string (doc, "/accent", "#7c6cff", 0, &error));
	g_assert_no_error (error);
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);

	/* themes reject unknown colour roles */
	g_assert_true (lrg_editor_document_set_string (doc, "/colors/bogus-role", "#ffffff", 0, &error));
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, FALSE);
	g_assert_nonnull (find_issue (issues, "bogus-role"));
	g_assert_true (lrg_editor_document_undo (doc));

	g_assert_true (lrg_editor_document_save (doc, &error));
	g_assert_no_error (error);
	text = read_file (lrg_editor_document_get_path (doc));
	g_assert_nonnull (strstr (text, "# a warm theme"));
	g_assert_true (lrg_editor_document_reload (doc, &error));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/colors/window"), ==, "#120d0b");
	g_clear_pointer (&issues, g_ptr_array_unref);
	issues = validate (doc, TRUE);
}

/* A UI layout naming a game handler validates with a warning: the
 * editor does not have the game's functions */
static void
test_ui_layout_game_handler (void)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autoptr(GError)            error = NULL;
	g_autoptr(GPtrArray)         issues = NULL;
	gboolean                     warned = FALSE;
	guint                        i;

	doc = lrg_editor_document_new_from_data ("type: vbox\nchildren:\n"
	                                         "  - {type: button, text: Resume, on: {clicked: on_resume}}\n"
	                                         "  - {type: button, text: Quit, on: {clicked: on_quit}}\n",
	                                         -1, LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT, &error);
	g_assert_no_error (error);
	issues = validate (doc, TRUE);
	for (i = 0; i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);

		g_assert_cmpint (issue->severity, !=, LRG_EDITOR_PROBLEM_ERROR);
		if (strstr (issue->message, "on_resume") != NULL)
			warned = TRUE;
	}
	g_assert_true (warned);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor/document/path/escaping", test_path_escaping);
	g_test_add_func ("/editor/document/detect-format", test_detect_format);
	g_test_add_func ("/editor/document/new-skeletons", test_new_skeletons);
	g_test_add ("/editor/document/open-errors", Fixture, NULL, fixture_set_up, test_open_errors,
	            fixture_tear_down);
	g_test_add_func ("/editor/document/undo/merge", test_undo_merge);
	g_test_add_func ("/editor/document/undo/group", test_undo_group);
	g_test_add ("/editor/document/dirty", Fixture, NULL, fixture_set_up, test_dirty_tracking,
	            fixture_tear_down);
	g_test_add_func ("/editor/document/values", test_values);
	g_test_add_func ("/editor/document/ui-layout/game-handler", test_ui_layout_game_handler);
	g_test_add ("/editor/document/save-preserves", Fixture, NULL, fixture_set_up, test_save_preserves,
	            fixture_tear_down);
	g_test_add ("/editor/document/reload-external", Fixture, NULL, fixture_set_up, test_reload_external,
	            fixture_tear_down);
	g_test_add ("/editor/document/lines-duplicates", Fixture, NULL, fixture_set_up,
	            test_lines_and_duplicates, fixture_tear_down);
	g_test_add ("/editor/document/format/input-map", Fixture, NULL, fixture_set_up, test_format_input_map,
	            fixture_tear_down);
	g_test_add ("/editor/document/format/locale", Fixture, NULL, fixture_set_up, test_format_locale,
	            fixture_tear_down);
	g_test_add ("/editor/document/format/sound-bank", Fixture, NULL, fixture_set_up,
	            test_format_sound_bank, fixture_tear_down);
	g_test_add ("/editor/document/format/hud-layout", Fixture, NULL, fixture_set_up,
	            test_format_hud_layout, fixture_tear_down);
	g_test_add ("/editor/document/format/ui-layout", Fixture, NULL, fixture_set_up,
	            test_format_ui_layout, fixture_tear_down);
	g_test_add ("/editor/document/format/dialog-tree", Fixture, NULL, fixture_set_up,
	            test_format_dialog_tree, fixture_tear_down);
	g_test_add ("/editor/document/format/dungeon", Fixture, NULL, fixture_set_up, test_format_dungeon,
	            fixture_tear_down);
	g_test_add ("/editor/document/format/theme", Fixture, NULL, fixture_set_up, test_format_theme,
	            fixture_tear_down);

	return g_test_run ();
}
