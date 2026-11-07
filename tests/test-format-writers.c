/* test-format-writers.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Round-trip tests for the engine's format writers and from-data loaders:
 * LrgLocale, LrgDialogTree, LrgSoundBank (manifest only, no audio
 * device), LrgInputMap (declaration order and unknown keys kept) and
 * LrgHudLayout. Every fixture is in its loader's real format; every
 * writer must produce text its loader reads back into the same object,
 * and saving that again must give the same bytes.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <libregnum.h>

/* a temporary directory, removed with its files by tmp_dir_free() */
static gchar *
tmp_dir_new (void)
{
	g_autoptr(GError) error = NULL;
	gchar *dir = g_dir_make_tmp ("lrg-writers-XXXXXX", &error);

	g_assert_no_error (error);
	return dir;
}

static void
tmp_dir_free (gchar *dir)
{
	g_autoptr(GDir) handle = g_dir_open (dir, 0, NULL);
	const gchar *name;

	while (handle != NULL && (name = g_dir_read_name (handle)) != NULL)
	{
		g_autofree gchar *path = g_build_filename (dir, name, NULL);

		g_unlink (path);
	}
	g_rmdir (dir);
	g_free (dir);
}

/* ==========================================================================
 * LrgLocale
 * ========================================================================== */

static const gchar *LOCALE_YAML =
	"code: en\n"
	"name: English\n"
	"strings:\n"
	"  greeting: Hello, %s!\n"
	"  farewell: Goodbye\n"
	"  items:\n"
	"    one: '%d item'\n"
	"    other: '%d items'\n"
	"  empty: \"\"\n"
	"  quoted: \"null\"\n";

static void
test_locale_round_trip (void)
{
	g_autoptr(LrgLocale) locale = NULL;
	g_autoptr(LrgLocale) again = NULL;
	g_autoptr(GPtrArray) keys = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *second = NULL;

	locale = lrg_locale_new_from_data (LOCALE_YAML, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_locale_get_plural (locale, "items", 3), ==, "%d items");

	/* the fixture is already in writer form: the same bytes come back */
	text = lrg_locale_save_to_data (locale, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, LOCALE_YAML);

	/* keys keep file order */
	keys = lrg_locale_get_keys (locale);
	g_assert_cmpuint (keys->len, ==, 5);
	g_assert_cmpstr (g_ptr_array_index (keys, 0), ==, "greeting");
	g_assert_cmpstr (g_ptr_array_index (keys, 2), ==, "items");
	g_assert_cmpstr (g_ptr_array_index (keys, 4), ==, "quoted");

	again = lrg_locale_new_from_data (text, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_locale_get_string (again, "greeting"), ==, "Hello, %s!");
	g_assert_cmpstr (lrg_locale_get_string (again, "empty"), ==, "");
	g_assert_cmpstr (lrg_locale_get_string (again, "quoted"), ==, "null");
	g_assert_cmpstr (lrg_locale_get_plural (again, "items", 1), ==, "%d item");
	second = lrg_locale_save_to_data (again, NULL, &error);
	g_assert_cmpstr (second, ==, text);
}

static void
test_locale_built_and_rule (void)
{
	g_autoptr(LrgLocale) locale = lrg_locale_new ("en", "Test");
	g_autoptr(LrgLocale) loaded = NULL;
	g_autoptr(LrgLocale) bad = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	gchar *dir = tmp_dir_new ();
	g_autofree gchar *path = g_build_filename (dir, "en.yaml", NULL);

	/* forms are written in CLDR order whatever order they were set in */
	lrg_locale_set_plural (locale, "apples", LRG_PLURAL_OTHER, "%d apples");
	lrg_locale_set_plural (locale, "apples", LRG_PLURAL_FEW, "%d apples (few)");
	lrg_locale_set_plural (locale, "apples", LRG_PLURAL_ONE, "%d apple");
	lrg_locale_set_string (locale, "title", "Orchard");
	lrg_locale_set_plural_rule (locale, LRG_PLURAL_RULE_EAST_SLAVIC);

	text = lrg_locale_save_to_data (locale, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==,
		"code: en\n"
		"name: Test\n"
		"plural_rule: east-slavic\n"
		"strings:\n"
		"  apples:\n"
		"    one: '%d apple'\n"
		"    few: '%d apples (few)'\n"
		"    other: '%d apples'\n"
		"  title: Orchard\n");

	/* to a file and back: the rule override survives */
	g_assert_true (lrg_locale_save_to_file (locale, path, &error));
	g_assert_no_error (error);
	loaded = lrg_locale_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_locale_get_plural_rule (loaded), ==, LRG_PLURAL_RULE_EAST_SLAVIC);
	g_assert_cmpstr (lrg_locale_get_plural (loaded, "apples", 3), ==, "%d apples (few)");

	/* the default rule is not written; a bad rule is an error */
	lrg_locale_set_plural_rule (locale, LRG_PLURAL_RULE_ONE_OTHER);
	g_clear_pointer (&text, g_free);
	text = lrg_locale_save_to_data (locale, NULL, NULL);
	g_assert_null (strstr (text, "plural_rule"));
	bad = lrg_locale_new_from_data ("code: en\nname: E\nplural_rule: martian\n", -1, &error);
	g_assert_null (bad);
	g_assert_error (error, LRG_I18N_ERROR, LRG_I18N_ERROR_PARSE);
	g_clear_error (&error);
	g_assert_null (lrg_locale_new_from_data ("name: no code\n", -1, &error));
	g_assert_error (error, LRG_I18N_ERROR, LRG_I18N_ERROR_PARSE);

	tmp_dir_free (dir);
}

/* ==========================================================================
 * LrgDialogTree
 * ========================================================================== */

static const gchar *DIALOG_YAML =
	"id: briefing-01\n"
	"title: Mission 1 Briefing\n"
	"description: The first briefing\n"
	"start: middle\n"
	"nodes:\n"
	"- id: intro\n"
	"  speaker: hashbro\n"
	"  text: Crispy on the outside...\n"
	"  next: choice\n"
	"  metadata:\n"
	"    portrait: hashbro-determined\n"
	"    voice: vo-intro-01\n"
	"  conditions:\n"
	"  - flag:met-maple\n"
	"  effects:\n"
	"  - set-flag:briefed\n"
	"- id: middle\n"
	"  text: An auto-chained line.\n"
	"- id: choice\n"
	"  text: Ready to go?\n"
	"  responses:\n"
	"  - text: Let's go\n"
	"    next: outro\n"
	"    conditions:\n"
	"    - flag:ready\n"
	"    effects:\n"
	"    - set-flag:go\n"
	"  - text: Not yet\n"
	"    next: dead-end\n"
	"- id: dead-end\n"
	"  text: Then wait.\n"
	"  next: null\n"
	"- id: outro\n"
	"  text: Good luck out there.\n";

static void
test_dialog_round_trip (void)
{
	g_autoptr(LrgDialogTree) tree = NULL;
	g_autoptr(LrgDialogTree) again = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *second = NULL;
	GList *ids;
	LrgDialogNode *node;

	tree = lrg_dialog_tree_new_from_data (DIALOG_YAML, -1, &error);
	g_assert_no_error (error);

	/* the loader: auto-chaining, explicit terminal, description, order */
	g_assert_cmpstr (lrg_dialog_node_get_next_node_id (lrg_dialog_tree_get_node (tree, "middle")), ==, "choice");
	g_assert_null (lrg_dialog_node_get_next_node_id (lrg_dialog_tree_get_node (tree, "dead-end")));
	g_assert_null (lrg_dialog_node_get_next_node_id (lrg_dialog_tree_get_node (tree, "outro")));
	g_assert_cmpstr (lrg_dialog_tree_get_description (tree), ==, "The first briefing");
	ids = lrg_dialog_tree_get_node_ids (tree);
	g_assert_cmpstr (g_list_nth_data (ids, 0), ==, "intro");
	g_assert_cmpstr (g_list_nth_data (ids, 3), ==, "dead-end");
	g_list_free (ids);

	/* the fixture is in writer form: the same bytes come back */
	text = lrg_dialog_tree_save_to_data (tree, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, DIALOG_YAML);

	again = lrg_dialog_tree_new_from_data (text, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_dialog_tree_get_start_node_id (again), ==, "middle");
	node = lrg_dialog_tree_get_node (again, "intro");
	g_assert_cmpstr (lrg_dialog_node_get_metadata_value (node, "voice"), ==, "vo-intro-01");
	g_assert_cmpuint (lrg_dialog_node_get_conditions (node)->len, ==, 1);
	node = lrg_dialog_tree_get_node (again, "choice");
	g_assert_cmpuint (lrg_dialog_node_get_response_count (node), ==, 2);
	g_assert_cmpstr (lrg_dialog_response_get_next_node_id (lrg_dialog_node_get_response (node, 1)), ==, "dead-end");
	second = lrg_dialog_tree_save_to_data (again, NULL, &error);
	g_assert_cmpstr (second, ==, text);
}

static void
test_dialog_built (void)
{
	g_autoptr(LrgDialogTree) tree = lrg_dialog_tree_new ("built");
	g_autoptr(LrgDialogTree) loaded = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	gchar *dir = tmp_dir_new ();
	g_autofree gchar *path = g_build_filename (dir, "built.yaml", NULL);
	LrgDialogNode *a = lrg_dialog_node_new ("a");
	LrgDialogNode *b = lrg_dialog_node_new ("b");
	LrgDialogNode *c = lrg_dialog_node_new ("c");

	/* a terminal node in the middle, a jump back, a chain to the next */
	lrg_dialog_node_set_text (a, "one");
	lrg_dialog_node_set_next_node_id (a, "b");
	lrg_dialog_node_set_text (b, "two");
	lrg_dialog_node_set_text (c, "three");
	lrg_dialog_node_set_next_node_id (c, "a");
	lrg_dialog_tree_add_node (tree, a);
	lrg_dialog_tree_add_node (tree, b);
	lrg_dialog_tree_add_node (tree, c);
	lrg_dialog_tree_set_start_node_id (tree, "a");

	text = lrg_dialog_tree_save_to_data (tree, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==,
		"id: built\n"
		"nodes:\n"
		"- id: a\n"
		"  text: one\n"
		"- id: b\n"
		"  text: two\n"
		"  next: null\n"
		"- id: c\n"
		"  text: three\n"
		"  next: a\n");

	g_assert_true (lrg_dialog_tree_save_to_file (tree, path, &error));
	g_assert_no_error (error);
	loaded = lrg_dialog_tree_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_dialog_node_get_next_node_id (lrg_dialog_tree_get_node (loaded, "a")), ==, "b");
	g_assert_null (lrg_dialog_node_get_next_node_id (lrg_dialog_tree_get_node (loaded, "b")));
	g_assert_cmpstr (lrg_dialog_node_get_next_node_id (lrg_dialog_tree_get_node (loaded, "c")), ==, "a");

	/* removing keeps the others in order */
	g_assert_true (lrg_dialog_tree_remove_node (tree, "b"));
	lrg_dialog_node_set_next_node_id (a, "c");
	g_clear_pointer (&text, g_free);
	text = lrg_dialog_tree_save_to_data (tree, NULL, &error);
	g_assert_cmpstr (text, ==, "id: built\nnodes:\n- id: a\n  text: one\n- id: c\n  text: three\n  next: a\n");

	tmp_dir_free (dir);
}

static void
test_dialog_unwritable (void)
{
	g_autoptr(LrgDialogTree) empty = lrg_dialog_tree_new ("empty");
	g_autoptr(LrgDialogTree) tree = lrg_dialog_tree_new ("t");
	g_autoptr(GError) error = NULL;
	LrgDialogNode *node = lrg_dialog_node_new ("n");

	/* the format needs nodes, and a next node on every response */
	g_assert_null (lrg_dialog_tree_save_to_data (empty, NULL, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error (&error);

	lrg_dialog_node_set_text (node, "hi");
	lrg_dialog_node_add_response (node, lrg_dialog_response_new ("r", "bye", NULL));
	lrg_dialog_tree_add_node (tree, node);
	g_assert_null (lrg_dialog_tree_save_to_data (tree, NULL, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

/* ==========================================================================
 * LrgSoundBank (manifest only: no audio device, no sound files)
 * ========================================================================== */

static const gchar *SOUND_BANK_YAML =
	"name: player\n"
	"base_path: sounds\n"
	"volume: 0.8\n"
	"sounds:\n"
	"  jump: jump.wav\n"
	"  land: land.ogg\n"
	"  hurt: sub/hurt.wav\n";

static void
test_sound_bank_round_trip (void)
{
	g_autoptr(LrgSoundBank) bank = NULL;
	g_autoptr(LrgSoundBank) again = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) names = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *second = NULL;
	g_autofree gchar *expected_base = g_build_filename ("/game/audio", "sounds", NULL);

	bank = lrg_sound_bank_new_from_data (SOUND_BANK_YAML, -1, "/game/audio", TRUE, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_sound_bank_get_name (bank), ==, "player");
	g_assert_cmpstr (lrg_sound_bank_get_base_path (bank), ==, expected_base);
	g_assert_cmpfloat_with_epsilon (lrg_sound_bank_get_volume (bank), 0.8, 1e-6);

	/* manifest only: entries, no decoded sounds */
	g_assert_cmpuint (lrg_sound_bank_get_count (bank), ==, 0);
	names = lrg_sound_bank_get_file_names (bank);
	g_assert_cmpstrv (names, ((const gchar *[]) { "jump", "land", "hurt", NULL }));
	g_assert_cmpstr (lrg_sound_bank_get_file (bank, "hurt"), ==, "sub/hurt.wav");

	text = lrg_sound_bank_save_to_data (bank, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, SOUND_BANK_YAML);

	again = lrg_sound_bank_new_from_data (text, -1, NULL, TRUE, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_sound_bank_get_base_path (again), ==, "sounds");
	second = lrg_sound_bank_save_to_data (again, NULL, &error);
	g_assert_cmpstr (second, ==, text);
}

static void
test_sound_bank_edit (void)
{
	g_autoptr(LrgSoundBank) bank = lrg_sound_bank_new ("ui");
	g_autoptr(LrgSoundBank) loaded = NULL;
	g_autoptr(LrgSoundBank) bad = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	gchar *dir = tmp_dir_new ();
	g_autofree gchar *path = g_build_filename (dir, "ui.yaml", NULL);

	/* entries without audio; replacing keeps the place, NULL removes */
	lrg_sound_bank_set_file (bank, "click", "click.wav");
	lrg_sound_bank_set_file (bank, "hover", "hover.wav");
	lrg_sound_bank_set_file (bank, "back", "back.wav");
	lrg_sound_bank_set_file (bank, "click", "click2.wav");
	lrg_sound_bank_set_file (bank, "hover", NULL);
	g_assert_null (lrg_sound_bank_get_file (bank, "hover"));

	text = lrg_sound_bank_save_to_data (bank, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, "name: ui\nsounds:\n  click: click2.wav\n  back: back.wav\n");

	/* remove drops the entry; a base path set by hand is written */
	g_assert_true (lrg_sound_bank_remove (bank, "back"));
	lrg_sound_bank_set_base_path (bank, "sfx");
	lrg_sound_bank_set_volume (bank, 0.5f);
	g_clear_pointer (&text, g_free);
	text = lrg_sound_bank_save_to_data (bank, NULL, &error);
	g_assert_cmpstr (text, ==, "name: ui\nbase_path: sfx\nvolume: 0.5\nsounds:\n  click: click2.wav\n");

	/* a manifest without sounds also loads through the file API */
	lrg_sound_bank_clear (bank);
	g_assert_true (lrg_sound_bank_save_to_file (bank, path, &error));
	g_assert_no_error (error);
	loaded = lrg_sound_bank_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (lrg_sound_bank_get_volume (loaded), 0.5, 1e-6);

	/* bad volume, no name */
	bad = lrg_sound_bank_new_from_data ("name: x\nvolume: loud\n", -1, NULL, TRUE, &error);
	g_assert_null (bad);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error (&error);
	g_assert_null (lrg_sound_bank_new_from_data ("sounds: {}\n", -1, NULL, TRUE, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

	tmp_dir_free (dir);
}

/* ==========================================================================
 * LrgInputMap: order and unknown keys
 * ========================================================================== */

static const gchar *INPUT_MAP_YAML =
	"version: 2\n"
	"actions:\n"
	"  jump:\n"
	"    label: Jump\n"
	"    bindings:\n"
	"    - type: keyboard\n"
	"      key: SPACE\n"
	"      hint: primary\n"
	"    - type: gamepad_button\n"
	"      gamepad: 0\n"
	"      button: RIGHT_FACE_DOWN\n"
	"    deadzone: 0.2\n"
	"  fire:\n"
	"    bindings:\n"
	"    - type: mouse_button\n"
	"      button: LEFT\n"
	"      modifiers:\n"
	"      - SHIFT\n"
	"  aim:\n"
	"    bindings:\n"
	"    - type: gamepad_axis\n"
	"      gamepad: 1\n"
	"      axis: LEFT_X\n"
	"      threshold: 0.35\n"
	"      positive: false\n"
	"editor-note: keep me\n";

static void
test_input_map_round_trip (void)
{
	g_autoptr(LrgInputMap) map = lrg_input_map_new ();
	g_autoptr(LrgInputMap) again = lrg_input_map_new ();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *second = NULL;
	GList *actions;

	g_assert_true (lrg_input_map_load_from_data (map, INPUT_MAP_YAML, -1, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_input_map_get_action_count (map), ==, 3);

	/* actions in declaration order */
	actions = lrg_input_map_get_actions (map);
	g_assert_cmpstr (lrg_input_action_get_name (g_list_nth_data (actions, 0)), ==, "jump");
	g_assert_cmpstr (lrg_input_action_get_name (g_list_nth_data (actions, 1)), ==, "fire");
	g_assert_cmpstr (lrg_input_action_get_name (g_list_nth_data (actions, 2)), ==, "aim");
	g_list_free (actions);

	/* every unknown key in place: the same bytes come back */
	text = lrg_input_map_save_to_data (map, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, INPUT_MAP_YAML);

	g_assert_true (lrg_input_map_load_from_data (again, text, -1, &error));
	second = lrg_input_map_save_to_data (again, NULL, &error);
	g_assert_cmpstr (second, ==, text);
}

static void
test_input_map_edit (void)
{
	g_autoptr(LrgInputMap) map = lrg_input_map_new ();
	g_autoptr(LrgInputMap) loaded = lrg_input_map_new ();
	g_autoptr(LrgInputAction) dash = lrg_input_action_new ("dash");
	g_autoptr(LrgInputBinding) shift = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *from_file = NULL;
	gchar *dir = tmp_dir_new ();
	g_autofree gchar *path = g_build_filename (dir, "input.yaml", NULL);

	g_assert_true (lrg_input_map_load_from_data (map, INPUT_MAP_YAML, -1, &error));

	/* a new action goes last; a removed one goes with its unknown keys */
	shift = lrg_input_binding_new_keyboard (GRL_KEY_LEFT_SHIFT, LRG_INPUT_MODIFIER_NONE);
	lrg_input_action_add_binding (dash, shift);
	lrg_input_map_add_action (map, dash);
	lrg_input_map_remove_action (map, "jump");
	text = lrg_input_map_save_to_data (map, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==,
		"version: 2\n"
		"actions:\n"
		"  fire:\n"
		"    bindings:\n"
		"    - type: mouse_button\n"
		"      button: LEFT\n"
		"      modifiers:\n"
		"      - SHIFT\n"
		"  aim:\n"
		"    bindings:\n"
		"    - type: gamepad_axis\n"
		"      gamepad: 1\n"
		"      axis: LEFT_X\n"
		"      threshold: 0.35\n"
		"      positive: false\n"
		"  dash:\n"
		"    bindings:\n"
		"    - type: keyboard\n"
		"      key: LEFT_SHIFT\n"
		"editor-note: keep me\n");

	/* the file API writes the same text */
	g_assert_true (lrg_input_map_save_to_file (map, path, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_get_contents (path, &from_file, NULL, NULL));
	g_assert_cmpstr (from_file, ==, text);
	g_assert_true (lrg_input_map_load_from_file (loaded, path, &error));
	g_assert_true (lrg_input_map_has_action (loaded, "dash"));

	/* bad documents fail */
	g_assert_false (lrg_input_map_load_from_data (loaded, "- not a mapping\n", -1, &error));
	g_assert_nonnull (error);
	g_clear_error (&error);
	g_assert_false (lrg_input_map_load_from_data (loaded, "bindings: {}\n", -1, &error));
	g_assert_nonnull (error);

	tmp_dir_free (dir);
}

/* ==========================================================================
 * LrgHudLayout
 * ========================================================================== */

static const gchar *HUD_YAML =
	"version: 1\n"
	"pieces:\n"
	"  health:\n"
	"    anchor: top-left\n"
	"    x: 8\n"
	"    y: 8\n"
	"  minimap:\n"
	"    anchor: top-right\n"
	"    x: -8.5\n"
	"    y: 0.1\n"
	"    locked: true\n"
	"  tracker:\n"
	"    hidden: true\n";

static void
test_hud_layout_data (void)
{
	g_autoptr(LrgHudLayout) layout = lrg_hud_layout_new ();
	g_autoptr(LrgHudLayout) again = lrg_hud_layout_new ();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *second = NULL;
	LrgHudAnchor anchor;
	gfloat x;
	gfloat y;

	g_assert_true (lrg_hud_layout_load_from_data (layout, HUD_YAML, -1, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_hud_layout_add_piece (layout, "minimap", NULL, LRG_HUD_ANCHOR_BOTTOM_RIGHT, 0.0f, 0.0f));
	g_assert_true (lrg_hud_layout_get_position (layout, "minimap", &anchor, &x, &y));
	g_assert_cmpint (anchor, ==, LRG_HUD_ANCHOR_TOP_RIGHT);
	g_assert_cmpfloat (x, ==, -8.5f);
	g_assert_cmpfloat (y, ==, 0.1f);

	text = lrg_hud_layout_save_to_data (layout, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, HUD_YAML);
	g_assert_true (lrg_hud_layout_load_from_data (again, text, -1, &error));
	second = lrg_hud_layout_save_to_data (again, NULL, &error);
	g_assert_cmpstr (second, ==, text);

	/* empty text is an empty layout; a bad one changes nothing */
	g_assert_false (lrg_hud_layout_load_from_data (again, "version: 1\npieces:\n  x:\n    colour: red\n",
	                                               -1, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_pointer (&second, g_free);
	second = lrg_hud_layout_save_to_data (again, NULL, NULL);
	g_assert_cmpstr (second, ==, text);
	g_assert_true (lrg_hud_layout_load_from_data (again, "", 0, NULL));
	g_clear_pointer (&second, g_free);
	second = lrg_hud_layout_save_to_data (again, NULL, NULL);
	g_assert_cmpstr (second, ==, "version: 1\npieces: {}\n");
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/format-writers/locale/round-trip", test_locale_round_trip);
	g_test_add_func ("/format-writers/locale/built-and-rule", test_locale_built_and_rule);
	g_test_add_func ("/format-writers/dialog/round-trip", test_dialog_round_trip);
	g_test_add_func ("/format-writers/dialog/built", test_dialog_built);
	g_test_add_func ("/format-writers/dialog/unwritable", test_dialog_unwritable);
	g_test_add_func ("/format-writers/sound-bank/round-trip", test_sound_bank_round_trip);
	g_test_add_func ("/format-writers/sound-bank/edit", test_sound_bank_edit);
	g_test_add_func ("/format-writers/input-map/round-trip", test_input_map_round_trip);
	g_test_add_func ("/format-writers/input-map/edit", test_input_map_edit);
	g_test_add_func ("/format-writers/hud-layout/data", test_hud_layout_data);

	return g_test_run ();
}
