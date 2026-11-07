/* test-bt-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the .rbt behaviour tree asset: format round trip and strict
 * errors, every composite, decorator and leaf, C and script bindings,
 * the blackboard schema, subtrees (inline, file, cycles), custom node
 * types, the runner's per-node debug state, determinism, the blackboard
 * additions and the AI component.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* ==========================================================================
 * A tiny scripting backend for tests: named C functions behind the
 * LrgScripting call API
 * ========================================================================== */

#define TEST_TYPE_SCRIPTING (test_scripting_get_type ())
G_DECLARE_FINAL_TYPE (TestScripting, test_scripting, TEST, SCRIPTING, LrgScripting)

struct _TestScripting
{
	LrgScripting parent_instance;

	guint        calls;
	guint        run_calls;
	gint64       last_arg;
	guint        n_last_args;
};

G_DEFINE_FINAL_TYPE (TestScripting, test_scripting, LRG_TYPE_SCRIPTING)

/*
 * test_scripting_call:
 *
 * "script_run": returns "running" the first call, then "success";
 * "script_hit": records its third argument (the first param) and returns
 * TRUE; "script_yes": returns TRUE; "script_fail": fails with an error.
 */
static gboolean
test_scripting_call (LrgScripting  *scripting,
                     const gchar   *func_name,
                     GValue        *return_value,
                     guint          n_args,
                     const GValue  *args,
                     GError       **error)
{
	TestScripting *self = TEST_SCRIPTING (scripting);

	self->calls++;
	self->n_last_args = n_args;
	g_assert_cmpuint (n_args, >=, 2);
	g_assert_true (LRG_IS_BLACKBOARD (g_value_get_object (&args[0])));
	g_assert_true (G_VALUE_HOLDS_DOUBLE (&args[1]));

	if (g_strcmp0 (func_name, "script_run") == 0)
	{
		g_value_init (return_value, G_TYPE_STRING);
		self->run_calls++;
		g_value_set_string (return_value, self->run_calls <= 1 ? "running" : "success");
		return TRUE;
	}
	if (g_strcmp0 (func_name, "script_hit") == 0)
	{
		if (n_args >= 3 && G_VALUE_HOLDS_INT64 (&args[2]))
			self->last_arg = g_value_get_int64 (&args[2]);
		g_value_init (return_value, G_TYPE_BOOLEAN);
		g_value_set_boolean (return_value, TRUE);
		return TRUE;
	}
	if (g_strcmp0 (func_name, "script_yes") == 0)
	{
		g_value_init (return_value, G_TYPE_BOOLEAN);
		g_value_set_boolean (return_value, TRUE);
		return TRUE;
	}
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "no function %s", func_name);
	return FALSE;
}

static gboolean
test_scripting_has (LrgScripting *scripting,
                    const gchar  *func_name)
{
	return g_str_has_prefix (func_name, "script_");
}

static void
test_scripting_class_init (TestScriptingClass *klass)
{
	LrgScriptingClass *sc = LRG_SCRIPTING_CLASS (klass);

	sc->call_function = test_scripting_call;
	sc->has_function = test_scripting_has;
}

static void
test_scripting_init (TestScripting *self)
{
}

/* ==========================================================================
 * Registered C bindings
 * ========================================================================== */

/* counts calls in the blackboard key the params name ("counter") and
 * returns the status the params name ("status": success|failure|running) */
static LrgBTStatus
act_count (LrgBlackboard *bb,
           gfloat         dt,
           GVariant      *params,
           gpointer       user_data)
{
	const gchar *counter = lrg_bt_params_get_string (params, "counter", "count");
	const gchar *status = lrg_bt_params_get_string (params, "status", "success");
	gint64       step = lrg_bt_params_get_int (params, "step", 1);

	lrg_blackboard_set_int (bb, counter, lrg_blackboard_get_int (bb, counter, 0) + (gint) step);
	if (g_strcmp0 (status, "failure") == 0)
		return LRG_BT_STATUS_FAILURE;
	if (g_strcmp0 (status, "running") == 0)
		return LRG_BT_STATUS_RUNNING;
	return LRG_BT_STATUS_SUCCESS;
}

static gboolean
cond_flag (LrgBlackboard *bb,
           GVariant      *params,
           gpointer       user_data)
{
	return lrg_blackboard_get_bool (bb, lrg_bt_params_get_string (params, "key", "flag"), FALSE);
}

static void
register_bindings (void)
{
	lrg_bt_register_action ("count", act_count, NULL, NULL);
	lrg_bt_register_condition ("flag", cond_flag, NULL, NULL);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgBtAsset *
load_ok (const gchar *text)
{
	g_autoptr(GError) error = NULL;
	LrgBtAsset       *asset = lrg_bt_asset_new_from_data (text, -1, &error);

	g_assert_no_error (error);
	g_assert_nonnull (asset);
	return asset;
}

static LrgBtRunner *
runner_ok (LrgBtAsset   *asset,
           LrgScripting *scripting)
{
	g_autoptr(GError) error = NULL;
	LrgBtRunner      *runner = lrg_bt_runner_new (asset, scripting, NULL, &error);

	g_assert_no_error (error);
	g_assert_nonnull (runner);
	return runner;
}

/* loads @text and expects an error whose message starts with @prefix */
static void
load_fails (const gchar *text,
            gint         code,
            const gchar *prefix)
{
	g_autoptr(GError)     error = NULL;
	g_autoptr(LrgBtAsset) asset = lrg_bt_asset_new_from_data (text, -1, &error);

	g_assert_null (asset);
	g_assert_error (error, LRG_BT_ASSET_ERROR, code);
	if (!g_str_has_prefix (error->message, prefix))
		g_error ("expected '%s...', got '%s'", prefix, error->message);
}

/* ticks a one-node tree built around @root_yaml and returns the status */
static LrgBTStatus
tick_once (const gchar   *root_yaml,
           LrgBlackboard *bb)
{
	g_autofree gchar           *text = g_strdup_printf ("root:\n%s", root_yaml);
	g_autoptr(LrgBtAsset)       asset = load_ok (text);
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgBehaviorTree)  tree = lrg_bt_asset_build (asset, NULL, bb, &error);

	g_assert_no_error (error);
	return lrg_behavior_tree_tick (tree, 0.1f);
}

/* ==========================================================================
 * Format
 * ========================================================================== */

static const gchar *FULL_TREE =
	"version: 1\n"
	"name: guard\n"
	"blackboard:\n"
	"- name: health\n"
	"  type: float\n"
	"  default: 100\n"
	"- name: alert\n"
	"  type: bool\n"
	"  default: false\n"
	"- name: count\n"
	"  type: int\n"
	"- name: target\n"
	"  type: object\n"
	"- name: mood\n"
	"  type: string\n"
	"  default: \"calm\"\n"
	"subtrees:\n"
	"  patrol:\n"
	"    root:\n"
	"      type: sequence\n"
	"      id: walk\n"
	"      children:\n"
	"      - type: wait\n"
	"        duration: 0.5\n"
	"      - type: action\n"
	"        action: count\n"
	"    editor_color: blue\n"
	"root:\n"
	"  type: selector\n"
	"  id: root\n"
	"  name: Root\n"
	"  children:\n"
	"  - type: guard\n"
	"    id: g\n"
	"    key: alert\n"
	"    value: true\n"
	"    child:\n"
	"      type: action\n"
	"      id: attack\n"
	"      action: count\n"
	"      params:\n"
	"        counter: hits\n"
	"        step: 2\n"
	"        label: \"42\"\n"
	"        ratio: 0.5\n"
	"  - type: subtree\n"
	"    id: p\n"
	"    ref: patrol\n"
	"    x_pos: 120\n"
	"editor:\n"
	"  zoom: 2\n";

static void
test_round_trip (void)
{
	g_autoptr(LrgBtAsset) asset = load_ok (FULL_TREE);
	g_autoptr(LrgBtAsset) again = NULL;
	g_autofree gchar     *first = NULL;
	g_autofree gchar     *second = NULL;
	LrgBtAssetNode       *attack;
	GVariant             *v;
	g_autoptr(GError)     error = NULL;

	first = lrg_bt_asset_to_data (asset, &error);
	g_assert_no_error (error);
	again = load_ok (first);
	second = lrg_bt_asset_to_data (again, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (first, ==, second);

	/* unknown keys survive at every level */
	g_assert_nonnull (strstr (second, "editor:"));
	g_assert_nonnull (strstr (second, "x_pos: 120"));
	g_assert_nonnull (strstr (second, "editor_color: blue"));

	/* typed params: the quoted "42" stays a string, step an integer,
	 * ratio a double */
	attack = lrg_bt_asset_find_node (again, "attack");
	g_assert_nonnull (attack);
	v = lrg_bt_asset_node_get_param (attack, "label");
	g_assert_true (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING));
	g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "42");
	g_assert_true (g_variant_is_of_type (lrg_bt_asset_node_get_param (attack, "step"), G_VARIANT_TYPE_INT64));
	g_assert_true (g_variant_is_of_type (lrg_bt_asset_node_get_param (attack, "ratio"), G_VARIANT_TYPE_DOUBLE));

	/* the schema */
	g_assert_cmpuint (lrg_blackboard_schema_get_key_count (lrg_bt_asset_get_schema (again)), ==, 5);
	g_assert_true (lrg_blackboard_schema_get_key_type (lrg_bt_asset_get_schema (again), "health") == G_TYPE_FLOAT);
	g_assert_cmpfloat (g_variant_get_double (lrg_blackboard_schema_get_key_default (lrg_bt_asset_get_schema (again),
	                                                                                "health")), ==, 100.0);
	g_assert_cmpstr (g_variant_get_string (lrg_blackboard_schema_get_key_default (lrg_bt_asset_get_schema (again),
	                                                                              "mood"), NULL), ==, "calm");
	g_assert_true (lrg_bt_asset_validate (again, &error));
	g_assert_no_error (error);
}

static void
test_file_round_trip (void)
{
	g_autoptr(LrgBtAsset) asset = load_ok (FULL_TREE);
	g_autoptr(LrgBtAsset) loaded = NULL;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *dir = g_dir_make_tmp ("lrg-bt-XXXXXX", &error);
	g_autofree gchar     *path = g_build_filename (dir, "guard.rbt", NULL);
	g_autofree gchar     *a = NULL;
	g_autofree gchar     *b = NULL;

	g_assert_no_error (error);
	g_assert_true (lrg_bt_asset_save_to_file (asset, path, &error));
	g_assert_no_error (error);
	loaded = lrg_bt_asset_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_bt_asset_get_base_dir (loaded), ==, dir);
	a = lrg_bt_asset_to_data (asset, NULL);
	b = lrg_bt_asset_to_data (loaded, NULL);
	g_assert_cmpstr (a, ==, b);
	g_unlink (path);
	g_rmdir (dir);
}

static void
test_strict_errors (void)
{
	load_fails ("root:\n  type: check\n  key: hp\n  op: almost\n  value: 3\n", LRG_BT_ASSET_ERROR_INVALID, "root.op:");
	load_fails ("root:\n  type: sequence\n  children:\n  - type: wait\n    duration: soon\n",
	            LRG_BT_ASSET_ERROR_INVALID, "root.children[0].duration:");
	load_fails ("root:\n  type: repeater\n  count: -2\n  child: {type: wait, duration: 1}\n",
	            LRG_BT_ASSET_ERROR_INVALID, "root.count:");
	load_fails ("root:\n  type: sequence\n  children:\n  - {type: wait, id: a, duration: 1}\n"
	            "  - {type: wait, id: a, duration: 1}\n", LRG_BT_ASSET_ERROR_INVALID, "root.children[1].id:");
	load_fails ("root:\n  type: subtree\n  ref: nowhere\n", LRG_BT_ASSET_ERROR_REFERENCE, "root.ref:");
	load_fails ("root:\n  type: inverter\n  children: []\n", LRG_BT_ASSET_ERROR_INVALID, "root:");
	load_fails ("root:\n  type: parallel\n  policy: most\n", LRG_BT_ASSET_ERROR_INVALID, "root.policy:");
	load_fails ("version: 99\nroot: {type: wait, duration: 1}\n", LRG_BT_ASSET_ERROR_VERSION, "version:");
	load_fails ("blackboard:\n- {name: hp, type: double}\n", LRG_BT_ASSET_ERROR_INVALID, "blackboard[0].type:");
	load_fails ("blackboard:\n- {name: hp, type: int, default: 1.5}\n", LRG_BT_ASSET_ERROR_INVALID,
	            "blackboard[0].default:");
	load_fails ("- just\n- a list\n", LRG_BT_ASSET_ERROR_PARSE, "the document");
	load_fails ("root:\n  id: x\n", LRG_BT_ASSET_ERROR_INVALID, "root:");
}

static void
test_validate_errors (void)
{
	g_autoptr(GError)     error = NULL;
	g_autoptr(LrgBtAsset) a = load_ok ("root:\n  type: inverter\n");
	g_autoptr(LrgBtAsset) b = load_ok ("blackboard:\n- {name: hp, type: int}\n"
	                                   "root:\n  type: set\n  id: s\n  key: mana\n  value: 1\n");
	g_autoptr(LrgBtAsset) c = load_ok ("root:\n  type: wait\n");
	g_autoptr(LrgBtAsset) d = lrg_bt_asset_new ();

	g_assert_false (lrg_bt_asset_validate (a, &error));
	g_assert_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_INVALID);
	g_assert_nonnull (strstr (error->message, "exactly one child"));
	g_clear_error (&error);

	g_assert_false (lrg_bt_asset_validate (b, &error));
	g_assert_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_REFERENCE);
	g_assert_nonnull (strstr (error->message, "'mana'"));
	g_clear_error (&error);

	g_assert_false (lrg_bt_asset_validate (c, &error));
	g_assert_nonnull (strstr (error->message, "duration"));
	g_clear_error (&error);

	g_assert_false (lrg_bt_asset_validate (d, &error));
	g_assert_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_INVALID);
}

/* ==========================================================================
 * Composites, decorators, leaves
 * ========================================================================== */

static void
test_composites (void)
{
	g_autoptr(LrgBlackboard) bb = lrg_blackboard_new ();

	/* sequence: stops at the first failure */
	g_assert_cmpint (tick_once ("  type: sequence\n  children:\n"
	                            "  - {type: action, action: count}\n"
	                            "  - {type: action, action: count, params: {status: failure}}\n"
	                            "  - {type: action, action: count}\n", bb), ==, LRG_BT_STATUS_FAILURE);
	g_assert_cmpint (lrg_blackboard_get_int (bb, "count", 0), ==, 2);

	/* selector: stops at the first success */
	lrg_blackboard_clear (bb);
	g_assert_cmpint (tick_once ("  type: selector\n  children:\n"
	                            "  - {type: action, action: count, params: {status: failure}}\n"
	                            "  - {type: action, action: count}\n"
	                            "  - {type: action, action: count}\n", bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (lrg_blackboard_get_int (bb, "count", 0), ==, 2);

	/* parallel: one / all */
	lrg_blackboard_clear (bb);
	g_assert_cmpint (tick_once ("  type: parallel\n  policy: one\n  children:\n"
	                            "  - {type: action, action: count, params: {status: running}}\n"
	                            "  - {type: action, action: count}\n", bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (lrg_blackboard_get_int (bb, "count", 0), ==, 2);
	g_assert_cmpint (tick_once ("  type: parallel\n  policy: all\n  children:\n"
	                            "  - {type: action, action: count, params: {status: running}}\n"
	                            "  - {type: action, action: count}\n", bb), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (tick_once ("  type: parallel\n  children:\n"
	                            "  - {type: action, action: count, params: {status: failure}}\n"
	                            "  - {type: action, action: count}\n", bb), ==, LRG_BT_STATUS_FAILURE);
}

static void
test_decorators (void)
{
	g_autoptr(LrgBlackboard)  bb = lrg_blackboard_new ();
	g_autoptr(LrgBtAsset)     asset = NULL;
	g_autoptr(LrgBtRunner)    runner = NULL;
	guint                     i;

	g_assert_cmpint (tick_once ("  type: inverter\n  child: {type: action, action: count}\n", bb), ==,
	                 LRG_BT_STATUS_FAILURE);
	g_assert_cmpint (tick_once ("  type: succeeder\n  child: {type: action, action: count, params: {status: failure}}\n",
	                            bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (tick_once ("  type: failer\n  child: {type: action, action: count}\n", bb), ==,
	                 LRG_BT_STATUS_FAILURE);

	/* repeater: three runs of the child, one per tick */
	asset = load_ok ("root:\n  type: repeater\n  count: 3\n  child: {type: action, action: count}\n");
	runner = runner_ok (asset, NULL);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (lrg_blackboard_get_int (lrg_bt_runner_get_blackboard (runner), "count", 0), ==, 3);
	g_clear_object (&runner);
	g_clear_object (&asset);

	/* cooldown: runs, then refuses for 1 s of tick time */
	asset = load_ok ("root:\n  type: cooldown\n  id: cd\n  seconds: 1\n  child: {type: action, action: count}\n");
	runner = runner_ok (asset, NULL);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.25f), ==, LRG_BT_STATUS_SUCCESS);
	for (i = 0; i < 3; i++)
		g_assert_cmpint (lrg_bt_runner_tick (runner, 0.25f), ==, LRG_BT_STATUS_FAILURE);
	g_assert_true (LRG_IS_BT_COOLDOWN (lrg_bt_runner_get_node (runner, "cd")));
	g_assert_cmpfloat_with_epsilon (lrg_bt_cooldown_get_remaining (LRG_BT_COOLDOWN (lrg_bt_runner_get_node (runner, "cd"))),
	                                0.25f, 1e-5f);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.25f), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (lrg_blackboard_get_int (lrg_bt_runner_get_blackboard (runner), "count", 0), ==, 2);
	g_clear_object (&runner);
	g_clear_object (&asset);

	/* time limit: a child running past 0.5 s fails */
	asset = load_ok ("root:\n  type: time_limit\n  seconds: 0.5\n"
	                 "  child: {type: action, action: count, params: {status: running}}\n");
	runner = runner_ok (asset, NULL);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.2f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.2f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.2f), ==, LRG_BT_STATUS_FAILURE);
	g_clear_object (&runner);
	g_clear_object (&asset);

	/* guard: runs its child while the test holds, aborts when it stops */
	asset = load_ok ("root:\n  type: guard\n  key: hp\n  op: gt\n  value: 10\n"
	                 "  child: {type: action, id: work, action: count, params: {status: running}}\n");
	runner = runner_ok (asset, NULL);
	lrg_blackboard_set_float (lrg_bt_runner_get_blackboard (runner), "hp", 50.0f);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	lrg_blackboard_set_float (lrg_bt_runner_get_blackboard (runner), "hp", 5.0f);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_FAILURE);
	g_assert_false (lrg_bt_node_is_running (lrg_bt_runner_get_node (runner, "work")));
	g_assert_cmpint (lrg_blackboard_get_int (lrg_bt_runner_get_blackboard (runner), "count", 0), ==, 1);
}

static void
test_leaves (void)
{
	g_autoptr(LrgBlackboard) bb = lrg_blackboard_new ();
	g_autoptr(LrgBtAsset)    asset = NULL;
	g_autoptr(LrgBtRunner)   runner = NULL;

	/* condition bound to C, with params */
	lrg_blackboard_set_bool (bb, "ready", TRUE);
	g_assert_cmpint (tick_once ("  type: condition\n  condition: flag\n  params: {key: ready}\n", bb), ==,
	                 LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (tick_once ("  type: condition\n  condition: flag\n  params: {key: missing}\n", bb), ==,
	                 LRG_BT_STATUS_FAILURE);

	/* check: every operator family */
	lrg_blackboard_set_int (bb, "ammo", 3);
	lrg_blackboard_set_string (bb, "mood", "angry");
	g_assert_cmpint (tick_once ("  {type: check, key: ammo, op: ge, value: 3}\n", bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (tick_once ("  {type: check, key: ammo, op: lt, value: 2.5}\n", bb), ==, LRG_BT_STATUS_FAILURE);
	g_assert_cmpint (tick_once ("  {type: check, key: mood, value: angry}\n", bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (tick_once ("  {type: check, key: mood, op: ne, value: calm}\n", bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (tick_once ("  {type: check, key: ammo}\n", bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (tick_once ("  {type: check, key: nothing, op: unset}\n", bb), ==, LRG_BT_STATUS_SUCCESS);

	/* set: typed writes */
	g_assert_cmpint (tick_once ("  {type: set, key: alert, value: true}\n", bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_true (lrg_blackboard_get_bool (bb, "alert", FALSE));
	g_assert_cmpint (tick_once ("  {type: set, key: speed, value: 2.5}\n", bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpfloat (lrg_blackboard_get_float (bb, "speed", 0.0f), ==, 2.5f);
	g_assert_cmpint (tick_once ("  {type: set, key: name, value: \"7\"}\n", bb), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpstr (lrg_blackboard_get_string (bb, "name"), ==, "7");

	/* wait */
	asset = load_ok ("root: {type: wait, duration: 0.3}\n");
	runner = runner_ok (asset, NULL);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.15f), ==, LRG_BT_STATUS_SUCCESS);
}

static void
test_script_binding (void)
{
	g_autoptr(TestScripting) scripting = g_object_new (TEST_TYPE_SCRIPTING, NULL);
	g_autoptr(LrgBtAsset)    asset = NULL;
	g_autoptr(LrgBtRunner)   runner = NULL;
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgBehaviorTree) tree = NULL;

	asset = load_ok ("root:\n  type: sequence\n  children:\n"
	                 "  - {type: condition, script: script_yes}\n"
	                 "  - {type: action, script: script_hit, params: {damage: 7, kind: fire}}\n"
	                 "  - {type: action, id: run, script: script_run}\n");
	runner = runner_ok (asset, LRG_SCRIPTING (scripting));
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	/* (blackboard, delta, damage, kind) */
	g_assert_cmpint (scripting->last_arg, ==, 7);
	g_assert_cmpuint (scripting->n_last_args, ==, 2);   /* script_run has no params */
	g_assert_cmpstr (lrg_bt_runner_get_current_node (runner), ==, "run");
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_SUCCESS);

	/* without a scripting context the script leaves are unbound */
	tree = lrg_bt_asset_build (asset, NULL, NULL, &error);
	g_assert_null (tree);
	g_assert_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_BINDING);
}

#ifdef LRG_HAS_LUAJIT
/* the same bindings through a real backend: Lua functions get (blackboard,
 * delta, params...) and return a status string or a truth value */
static void
test_lua_binding (void)
{
	static const gchar *code =
		"function attack(bb, dt, damage)\n"
		"  hits = (hits or 0) + damage\n"
		"  if hits >= 10 then return 'success' end\n"
		"  return 'running'\n"
		"end\n"
		"function ready(bb, dt) return true end\n";
	g_autoptr(LrgScriptingLua)  lua = lrg_scripting_lua_new ();
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgBtAsset)       asset = NULL;
	g_autoptr(LrgBtRunner)      runner = NULL;
	g_autoptr(LrgLogicMachine)  m = NULL;

	g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (lua), "ai", code, &error));
	g_assert_no_error (error);
	asset = load_ok ("root:\n  type: sequence\n  children:\n"
	                 "  - {type: condition, script: ready}\n"
	                 "  - {type: action, script: attack, params: {damage: 4}}\n");
	runner = runner_ok (asset, LRG_SCRIPTING (lua));
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_SUCCESS);

	m = lrg_logic_machine_new_from_data ("states:\n- name: a\n  transitions: [{to: b, when: [{script: ready}]}]\n"
	                                     "- name: b\n", -1, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_logic_machine_start (m, NULL, LRG_SCRIPTING (lua), &error));
	lrg_logic_machine_update (m, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "b");
}
#endif

static void
test_unbound (void)
{
	g_autoptr(LrgBtAsset)      asset = load_ok ("root:\n  type: selector\n  children:\n"
	                                            "  - {type: action, action: nope}\n"
	                                            "  - {type: condition, condition: nope_either}\n"
	                                            "  - {type: frobnicate, speed: 3}\n"
	                                            "  - {type: action, action: count}\n");
	g_autoptr(GError)          error = NULL;
	g_autoptr(LrgBehaviorTree) tree = NULL;
	g_auto(GStrv)              missing = NULL;
	g_autoptr(LrgBtRunner)     runner = NULL;

	tree = lrg_bt_asset_build (asset, NULL, NULL, &error);
	g_assert_null (tree);
	g_assert_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_BINDING);
	g_clear_error (&error);

	missing = lrg_bt_asset_dup_unbound (asset, NULL);
	g_assert_cmpuint (g_strv_length (missing), ==, 3);
	g_assert_cmpstr (missing[0], ==, "action:nope");
	g_assert_cmpstr (missing[1], ==, "condition:nope_either");
	g_assert_cmpstr (missing[2], ==, "type:frobnicate");

	/* lenient: unbound leaves fail, the tree goes on */
	lrg_bt_asset_set_allow_unbound (asset, TRUE);
	runner = runner_ok (asset, NULL);
	g_test_expect_message ("Libregnum-AI", G_LOG_LEVEL_WARNING, "*not bound*");
	g_test_expect_message ("Libregnum-AI", G_LOG_LEVEL_WARNING, "*not bound*");
	g_test_expect_message ("Libregnum-AI", G_LOG_LEVEL_WARNING, "*not bound*");
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_SUCCESS);
	g_test_assert_expected_messages ();
	/* warned once only */
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_SUCCESS);
}

/* ==========================================================================
 * Schema, subtrees, custom types
 * ========================================================================== */

static void
test_schema_defaults (void)
{
	g_autoptr(LrgBtAsset)    asset = load_ok (FULL_TREE);
	g_autoptr(LrgBlackboard) bb = lrg_blackboard_new ();
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgBehaviorTree) tree = NULL;

	/* a value already there is kept; missing keys get their defaults */
	lrg_blackboard_set_float (bb, "health", 42.0f);
	tree = lrg_bt_asset_build (asset, NULL, bb, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_behavior_tree_get_blackboard (tree) == bb);
	g_assert_cmpfloat (lrg_blackboard_get_float (bb, "health", 0.0f), ==, 42.0f);
	g_assert_false (lrg_blackboard_get_bool (bb, "alert", TRUE));
	g_assert_cmpint (lrg_blackboard_get_int (bb, "count", -1), ==, 0);
	g_assert_cmpstr (lrg_blackboard_get_string (bb, "mood"), ==, "calm");
	g_assert_false (lrg_blackboard_has_key (bb, "target"));

	/* validation against the schema */
	g_assert_true (lrg_blackboard_schema_validate (lrg_bt_asset_get_schema (asset), bb, &error));
	lrg_blackboard_set_string (bb, "health", "lots");
	g_assert_false (lrg_blackboard_schema_validate (lrg_bt_asset_get_schema (asset), bb, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
test_subtrees (void)
{
	g_autoptr(GError)       error = NULL;
	g_autofree gchar       *dir = g_dir_make_tmp ("lrg-bt-XXXXXX", &error);
	g_autofree gchar       *sub_path = g_build_filename (dir, "sub.rbt", NULL);
	g_autofree gchar       *main_path = g_build_filename (dir, "main.rbt", NULL);
	g_autofree gchar       *loop_path = g_build_filename (dir, "loop.rbt", NULL);
	g_autoptr(LrgBtAsset)   main_asset = NULL;
	g_autoptr(LrgBtAsset)   loop = NULL;
	g_autoptr(LrgBtRunner)  runner = NULL;
	g_autoptr(LrgBehaviorTree) tree = NULL;

	g_assert_no_error (error);
	g_assert_true (g_file_set_contents (sub_path,
	                                    "blackboard:\n- {name: steps, type: int, default: 5}\n"
	                                    "root:\n  type: action\n  id: inner\n  action: count\n", -1, NULL));
	g_assert_true (g_file_set_contents (main_path,
	                                    "subtrees:\n  ext: {path: sub.rbt}\n"
	                                    "  local:\n    root: {type: action, id: here, action: count}\n"
	                                    "root:\n  type: sequence\n  id: top\n  children:\n"
	                                    "  - {type: subtree, id: a, ref: ext}\n"
	                                    "  - {type: subtree, id: b, ref: local}\n"
	                                    "  - {type: subtree, id: c, ref: local}\n", -1, NULL));
	main_asset = lrg_bt_asset_new_from_file (main_path, &error);
	g_assert_no_error (error);
	runner = runner_ok (main_asset, NULL);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (lrg_blackboard_get_int (lrg_bt_runner_get_blackboard (runner), "count", 0), ==, 3);
	/* the file subtree's schema defaults reach the shared blackboard */
	g_assert_cmpint (lrg_blackboard_get_int (lrg_bt_runner_get_blackboard (runner), "steps", 0), ==, 5);
	/* expanded nodes have prefixed ids; the subtree node aliases its root */
	g_assert_nonnull (lrg_bt_runner_get_node (runner, "a/inner"));
	g_assert_nonnull (lrg_bt_runner_get_node (runner, "b/here"));
	g_assert_nonnull (lrg_bt_runner_get_node (runner, "c/here"));
	g_assert_true (lrg_bt_runner_get_node (runner, "b") == lrg_bt_runner_get_node (runner, "b/here"));
	g_assert_true (lrg_bt_runner_get_node (runner, "b/here") != lrg_bt_runner_get_node (runner, "c/here"));
	g_assert_true (lrg_bt_runner_get_node_visited (runner, "c"));
	g_assert_cmpint (lrg_bt_runner_get_node_status (runner, "a/inner"), ==, LRG_BT_STATUS_SUCCESS);

	/* a subtree that contains itself is refused */
	g_assert_true (g_file_set_contents (loop_path,
	                                    "subtrees:\n  me: {path: loop.rbt}\n"
	                                    "root: {type: subtree, id: again, ref: me}\n", -1, NULL));
	loop = lrg_bt_asset_new_from_file (loop_path, &error);
	g_assert_no_error (error);
	tree = lrg_bt_asset_build (loop, NULL, NULL, &error);
	g_assert_null (tree);
	g_assert_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_REFERENCE);
	g_assert_nonnull (strstr (error->message, "contains itself"));

	g_unlink (sub_path);
	g_unlink (main_path);
	g_unlink (loop_path);
	g_rmdir (dir);
}

/* a custom leaf: succeeds after "needed" ticks */
#define TEST_TYPE_PATIENT (test_patient_get_type ())
G_DECLARE_FINAL_TYPE (TestPatient, test_patient, TEST, PATIENT, LrgBTNode)

struct _TestPatient
{
	LrgBTNode parent_instance;
	gint      needed;
	gint      seen;
	gboolean  loud;
};

G_DEFINE_FINAL_TYPE (TestPatient, test_patient, LRG_TYPE_BT_NODE)

static LrgBTStatus
test_patient_tick (LrgBTNode     *node,
                   LrgBlackboard *bb,
                   gfloat         dt)
{
	TestPatient *self = TEST_PATIENT (node);

	return ++self->seen >= self->needed ? LRG_BT_STATUS_SUCCESS : LRG_BT_STATUS_RUNNING;
}

static void
test_patient_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	if (prop_id == 1)
		TEST_PATIENT (object)->needed = g_value_get_int (value);
	else
		TEST_PATIENT (object)->loud = g_value_get_boolean (value);
}

static void
test_patient_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	if (prop_id == 1)
		g_value_set_int (value, TEST_PATIENT (object)->needed);
	else
		g_value_set_boolean (value, TEST_PATIENT (object)->loud);
}

static void
test_patient_class_init (TestPatientClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);

	oc->set_property = test_patient_set_property;
	oc->get_property = test_patient_get_property;
	LRG_BT_NODE_CLASS (klass)->tick = test_patient_tick;
	g_object_class_install_property (oc, 1, g_param_spec_int ("ticks-needed", NULL, NULL, 0, 100, 1,
	                                                          G_PARAM_READWRITE));
	g_object_class_install_property (oc, 2, g_param_spec_boolean ("loud", NULL, NULL, FALSE, G_PARAM_READWRITE));
}

static void
test_patient_init (TestPatient *self)
{
	self->needed = 1;
}

static void
test_custom_type (void)
{
	g_autoptr(LrgBtAsset)  asset = NULL;
	g_autoptr(LrgBtRunner) runner = NULL;
	g_auto(GStrv)          names = NULL;
	LrgBtNodeKind          kind;

	g_assert_false (lrg_bt_register_node_type ("sequence", TEST_TYPE_PATIENT));
	g_assert_true (lrg_bt_register_node_type ("patient", TEST_TYPE_PATIENT));
	g_assert_true (lrg_bt_lookup_node_type ("patient", &kind));
	g_assert_cmpint (kind, ==, LRG_BT_NODE_KIND_LEAF);
	names = lrg_bt_dup_node_type_names ();
	g_assert_true (g_strv_contains ((const gchar * const *) names, "patient"));
	g_assert_true (g_strv_contains ((const gchar * const *) names, "guard"));

	asset = load_ok ("root: {type: patient, id: p, ticks_needed: 3, loud: true}\n");
	runner = runner_ok (asset, NULL);
	g_assert_true (TEST_PATIENT (lrg_bt_runner_get_node (runner, "p"))->loud);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_SUCCESS);
}

/* ==========================================================================
 * The node model (editor API)
 * ========================================================================== */

static void
test_node_model (void)
{
	g_autoptr(LrgBtAsset)     asset = lrg_bt_asset_new ();
	g_autoptr(LrgBtAssetNode) root = lrg_bt_asset_node_new ("selector");
	g_autoptr(LrgBtAssetNode) act = lrg_bt_asset_node_new ("action");
	g_autoptr(LrgBtAssetNode) chk = lrg_bt_asset_node_new ("check");
	g_autoptr(LrgBtAssetNode) copy = NULL;
	g_autoptr(LrgBtAsset)     back = NULL;
	g_autofree gchar         *text = NULL;
	g_auto(GStrv)             props = NULL;
	g_autoptr(LrgBtRunner)    runner = NULL;

	lrg_bt_asset_node_set_id (root, "root");
	lrg_bt_asset_node_set_prop (chk, "key", g_variant_new_string ("hp"));
	lrg_bt_asset_node_set_prop (chk, "op", g_variant_new_string ("lt"));
	lrg_bt_asset_node_set_prop (chk, "value", g_variant_new_int64 (5));
	lrg_bt_asset_node_set_prop (act, "action", g_variant_new_string ("count"));
	lrg_bt_asset_node_set_param (act, "step", g_variant_new_int64 (4));
	lrg_bt_asset_node_insert_child (root, act, -1);
	lrg_bt_asset_node_insert_child (root, chk, 0);
	lrg_bt_asset_set_root (asset, root);
	g_assert_true (lrg_bt_asset_node_get_parent (act) == root);
	g_assert_true (lrg_bt_asset_node_get_child (root, 0) == chk);
	g_assert_cmpint (lrg_bt_asset_node_get_kind (root), ==, LRG_BT_NODE_KIND_COMPOSITE);

	props = lrg_bt_asset_node_dup_prop_names (chk);
	g_assert_cmpuint (g_strv_length (props), ==, 3);
	g_assert_cmpstr (props[0], ==, "key");
	g_assert_cmpstr (props[2], ==, "value");

	/* generated ids are stable and not saved */
	g_assert_nonnull (lrg_bt_asset_find_node (asset, "root"));
	g_assert_nonnull (lrg_bt_asset_node_get_id (act));
	g_assert_false (lrg_bt_asset_node_has_explicit_id (act));
	text = lrg_bt_asset_to_data (asset, NULL);
	g_assert_null (strstr (text, "id: n"));
	g_assert_nonnull (strstr (text, "id: root"));

	back = load_ok (text);
	runner = runner_ok (back, NULL);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_cmpint (lrg_blackboard_get_int (lrg_bt_runner_get_blackboard (runner), "count", 0), ==, 4);

	/* copy and remove */
	copy = lrg_bt_asset_node_copy (root);
	g_assert_cmpuint (lrg_bt_asset_node_get_child_count (copy), ==, 2);
	g_assert_true (lrg_bt_asset_node_remove_child (root, chk));
	g_assert_null (lrg_bt_asset_node_get_parent (chk));
	g_assert_cmpuint (lrg_bt_asset_node_get_child_count (root), ==, 1);
	lrg_bt_asset_node_set_param (act, "step", NULL);
	g_assert_null (lrg_bt_asset_node_get_params (act));

	/* subtrees by API */
	lrg_bt_asset_set_subtree_path (asset, "far", "far.rbt");
	lrg_bt_asset_set_subtree_root (asset, "near", copy);
	{
		g_auto(GStrv) st = lrg_bt_asset_dup_subtree_names (asset);

		g_assert_cmpuint (g_strv_length (st), ==, 2);
		g_assert_cmpstr (lrg_bt_asset_get_subtree_path (asset, "far"), ==, "far.rbt");
		g_assert_true (lrg_bt_asset_get_subtree_root (asset, "near") == copy);
		g_assert_true (lrg_bt_asset_remove_subtree (asset, "far"));
	}
}

/* ==========================================================================
 * Runner debug state and determinism
 * ========================================================================== */

static guint ticked_seen;

static void
on_ticked (LrgBtRunner *runner,
           guint64      tick,
           LrgBTStatus  status,
           gpointer     user_data)
{
	ticked_seen++;
	g_assert_cmpuint (tick, ==, lrg_bt_runner_get_tick_count (runner));
}

static void
test_runner_debug (void)
{
	g_autoptr(LrgBtAsset)  asset = load_ok ("root:\n  type: selector\n  id: root\n  children:\n"
	                                        "  - type: sequence\n    id: fight\n    children:\n"
	                                        "    - {type: check, id: seen, key: enemy}\n"
	                                        "    - {type: wait, id: aim, duration: 1}\n"
	                                        "  - {type: action, id: idle, action: count}\n");
	g_autoptr(LrgBtRunner) runner = runner_ok (asset, NULL);
	g_auto(GStrv)          path = NULL;
	g_autofree gchar      *state = NULL;

	g_signal_connect (runner, "ticked", G_CALLBACK (on_ticked), NULL);
	g_assert_cmpuint (lrg_bt_runner_get_node_count (runner), ==, 5);
	g_assert_cmpstr (lrg_bt_runner_get_node_id (runner, 0), ==, "root");
	g_assert_null (lrg_bt_runner_get_current_node (runner));

	/* no enemy: the check fails, idle runs */
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_SUCCESS);
	g_assert_true (lrg_bt_runner_get_node_visited (runner, "seen"));
	g_assert_false (lrg_bt_runner_get_node_visited (runner, "aim"));
	g_assert_true (lrg_bt_runner_get_node_visited (runner, "idle"));
	g_assert_cmpint (lrg_bt_runner_get_node_status (runner, "seen"), ==, LRG_BT_STATUS_FAILURE);
	g_assert_cmpstr (lrg_bt_runner_get_current_node (runner), ==, "idle");

	/* an enemy: the fight branch runs and stays running */
	lrg_blackboard_set_bool (lrg_bt_runner_get_blackboard (runner), "enemy", TRUE);
	g_assert_cmpint (lrg_bt_runner_tick (runner, 0.1f), ==, LRG_BT_STATUS_RUNNING);
	g_assert_false (lrg_bt_runner_get_node_visited (runner, "idle"));
	path = lrg_bt_runner_dup_active_path (runner);
	g_assert_cmpuint (g_strv_length (path), ==, 3);
	g_assert_cmpstr (path[0], ==, "root");
	g_assert_cmpstr (path[1], ==, "fight");
	g_assert_cmpstr (path[2], ==, "aim");
	g_assert_cmpstr (lrg_bt_runner_get_current_node (runner), ==, "aim");

	state = lrg_bt_runner_dup_debug_state (runner);
	g_assert_nonnull (strstr (state, "tick: 2\n"));
	g_assert_nonnull (strstr (state, "current: aim\n"));
	g_assert_nonnull (strstr (state, "{id: \"aim\", status: running, visited: true"));
	g_assert_nonnull (strstr (state, "{id: \"idle\", status: success, visited: false"));
	g_assert_cmpuint (ticked_seen, ==, 2);

	lrg_bt_runner_reset (runner);
	g_assert_cmpint (lrg_bt_runner_get_status (runner), ==, LRG_BT_STATUS_INVALID);
	g_assert_null (lrg_bt_runner_get_current_node (runner));
}

/* the debug state of every tick of a scripted session, as one string */
static gchar *
session (LrgBtAsset *asset)
{
	g_autoptr(LrgBtRunner) runner = runner_ok (asset, NULL);
	GString               *out = g_string_new (NULL);
	guint                  i;

	for (i = 0; i < 40; i++)
	{
		g_autofree gchar *s = NULL;

		lrg_blackboard_set_bool (lrg_bt_runner_get_blackboard (runner), "alert", (i / 7) % 2 == 1);
		lrg_bt_runner_tick (runner, 1.0f / 30.0f);
		s = lrg_bt_runner_dup_debug_state (runner);
		g_string_append (out, s);
	}
	return g_string_free (out, FALSE);
}

static void
test_determinism (void)
{
	g_autoptr(LrgBtAsset) asset = load_ok (FULL_TREE);
	g_autofree gchar     *a = session (asset);
	g_autofree gchar     *b = session (asset);

	g_assert_cmpstr (a, ==, b);
}

/* ==========================================================================
 * Blackboard additions
 * ========================================================================== */

static void
test_blackboard_api (void)
{
	g_autoptr(LrgBlackboard) bb = lrg_blackboard_new ();
	g_autoptr(GVariant)      v = NULL;
	g_autofree gchar        *text = NULL;
	guint64                  serial;
	LrgBlackboardOp          op;

	serial = lrg_blackboard_get_serial (bb);
	lrg_blackboard_set_int (bb, "i", 3);
	lrg_blackboard_set_float (bb, "f", 0.5f);
	lrg_blackboard_set_bool (bb, "b", TRUE);
	lrg_blackboard_set_string (bb, "s", "hi");
	lrg_blackboard_set_object (bb, "o", G_OBJECT (bb));
	g_assert_cmpuint (lrg_blackboard_get_serial (bb), ==, serial + 5);
	lrg_blackboard_remove (bb, "o");
	g_assert_cmpuint (lrg_blackboard_get_serial (bb), ==, serial + 6);

	g_assert_true (lrg_blackboard_get_value_type (bb, "i") == G_TYPE_INT);
	g_assert_true (lrg_blackboard_get_value_type (bb, "f") == G_TYPE_FLOAT);
	g_assert_true (lrg_blackboard_get_value_type (bb, "b") == G_TYPE_BOOLEAN);
	g_assert_true (lrg_blackboard_get_value_type (bb, "s") == G_TYPE_STRING);
	g_assert_true (lrg_blackboard_get_value_type (bb, "nope") == G_TYPE_INVALID);

	text = lrg_blackboard_dup_value_text (bb, "f");
	g_assert_cmpstr (text, ==, "0.5");
	v = lrg_blackboard_dup_variant (bb, "i");
	g_assert_cmpint (g_variant_get_int32 (v), ==, 3);

	g_assert_true (lrg_blackboard_set_variant (bb, "x", g_variant_new_int64 (9)));
	g_assert_cmpint (lrg_blackboard_get_int (bb, "x", 0), ==, 9);
	g_assert_true (lrg_blackboard_test (bb, "x", LRG_BLACKBOARD_OP_GT, g_variant_new_double (8.5)));
	g_assert_true (lrg_blackboard_test (bb, "f", LRG_BLACKBOARD_OP_EQ, g_variant_new_double (0.5)));
	g_assert_true (lrg_blackboard_test (bb, "b", LRG_BLACKBOARD_OP_EQ, g_variant_new_boolean (TRUE)));
	g_assert_false (lrg_blackboard_test (bb, "s", LRG_BLACKBOARD_OP_LT, g_variant_new_int32 (1)));
	g_assert_true (lrg_blackboard_test (bb, "s", LRG_BLACKBOARD_OP_NE, g_variant_new_int32 (1)));
	g_assert_true (lrg_blackboard_test (bb, "missing", LRG_BLACKBOARD_OP_UNSET, NULL));

	g_assert_true (lrg_blackboard_op_from_string ("<=", &op));
	g_assert_cmpint (op, ==, LRG_BLACKBOARD_OP_LE);
	g_assert_cmpstr (lrg_blackboard_op_to_string (LRG_BLACKBOARD_OP_UNSET), ==, "unset");
	g_assert_false (lrg_blackboard_op_from_string ("~=", &op));
	g_assert_cmpstr (lrg_blackboard_schema_type_to_string (G_TYPE_BOOLEAN), ==, "bool");
	g_assert_true (lrg_blackboard_schema_type_from_string ("object") == G_TYPE_OBJECT);
	g_assert_cmpstr (lrg_bt_status_to_string (LRG_BT_STATUS_RUNNING), ==, "running");
}

static void
test_registry (void)
{
	g_auto(GStrv) actions = NULL;
	g_auto(GStrv) conditions = NULL;

	g_assert_true (lrg_bt_has_action ("count"));
	g_assert_true (lrg_bt_has_condition ("flag"));
	lrg_bt_register_action ("temp", act_count, NULL, NULL);
	actions = lrg_bt_dup_action_names ();
	g_assert_true (g_strv_contains ((const gchar * const *) actions, "temp"));
	g_assert_true (lrg_bt_unregister_action ("temp"));
	g_assert_false (lrg_bt_unregister_action ("temp"));
	conditions = lrg_bt_dup_condition_names ();
	g_assert_cmpstr (conditions[0], ==, "flag");
	lrg_bt_register_condition ("temp_cond", cond_flag, NULL, NULL);
	g_assert_true (lrg_bt_unregister_condition ("temp_cond"));
	g_assert_cmpfloat (lrg_bt_params_get_double (NULL, "x", 2.0), ==, 2.0);
	g_assert_true (lrg_bt_params_get_bool (NULL, "x", TRUE));
}

/* ==========================================================================
 * The AI component
 * ========================================================================== */

static guint completed_seen;

static void
on_completed (LrgAiComponent *ai,
              LrgBTStatus     status,
              gpointer        user_data)
{
	completed_seen++;
}

static void
test_component_tree (void)
{
	g_autoptr(LrgBtAsset)     asset = load_ok ("blackboard:\n- {name: hp, type: float, default: 10}\n"
	                                           "root:\n  type: sequence\n  id: s\n  children:\n"
	                                           "  - {type: wait, id: w, duration: 0.5}\n"
	                                           "  - {type: action, id: a, action: count}\n");
	g_autoptr(LrgAiComponent) ai = lrg_ai_component_new_with_tree (asset);
	g_autoptr(LrgEntity3D)    entity = lrg_entity3d_new ();
	g_autofree gchar         *state = NULL;
	guint                     i;

	g_signal_connect (ai, "tree-completed", G_CALLBACK (on_completed), NULL);
	g_assert_true (lrg_entity3d_add_component (entity, LRG_COMPONENT (ai)));
	g_assert_true (lrg_blackboard_get_pointer (lrg_ai_component_get_blackboard (ai), LRG_AI_COMPONENT_OWNER_KEY)
	               == (gpointer) entity);
	g_assert_null (lrg_ai_component_get_runner (ai));

	/* a 0.25 s tick interval: four 0.1 s updates step twice */
	lrg_ai_component_set_tick_interval (ai, 0.25f);
	for (i = 0; i < 4; i++)
		lrg_ai_component_step (ai, 0.1f);
	g_assert_nonnull (lrg_ai_component_get_runner (ai));
	g_assert_cmpuint (lrg_bt_runner_get_tick_count (lrg_ai_component_get_runner (ai)), ==, 1);
	g_assert_cmpfloat (lrg_blackboard_get_float (lrg_ai_component_get_blackboard (ai), "hp", 0.0f), ==, 10.0f);
	g_assert_cmpstr (lrg_ai_component_get_current_node (ai), ==, "w");
	for (i = 0; i < 4; i++)
		lrg_ai_component_step (ai, 0.1f);
	g_assert_cmpuint (completed_seen, ==, 1);
	g_assert_cmpint (lrg_blackboard_get_int (lrg_ai_component_get_blackboard (ai), "count", 0), ==, 1);

	/* paused: nothing moves */
	lrg_ai_component_set_running (ai, FALSE);
	lrg_ai_component_step (ai, 1.0f);
	g_assert_cmpuint (lrg_bt_runner_get_tick_count (lrg_ai_component_get_runner (ai)), ==, 2);

	state = lrg_ai_component_dup_debug_state (ai);
	g_assert_nonnull (strstr (state, "tree:\n  tick: 2"));
	g_assert_nonnull (strstr (state, "machine: ~"));
	g_assert_nonnull (strstr (state, "\"hp\": \"10\""));

	lrg_ai_component_restart (ai);
	g_assert_null (lrg_ai_component_get_runner (ai));
	g_assert_true (lrg_entity3d_remove_component (entity, LRG_COMPONENT (ai)));
	g_assert_false (lrg_blackboard_has_key (lrg_ai_component_get_blackboard (ai), LRG_AI_COMPONENT_OWNER_KEY));
}

static void
test_component_paths (void)
{
	g_autoptr(GError)         error = NULL;
	g_autofree gchar         *dir = g_dir_make_tmp ("lrg-ai-XXXXXX", &error);
	g_autofree gchar         *tree_path = g_build_filename (dir, "t.rbt", NULL);
	g_autofree gchar         *fsm_path = g_build_filename (dir, "m.rfsm", NULL);
	g_autoptr(LrgAiComponent) ai = NULL;
	g_autoptr(LrgGameObject)  owner = lrg_game_object_new ();

	g_assert_no_error (error);
	g_assert_true (g_file_set_contents (tree_path,
	                                    "root: {type: check, id: c, key: mode, value: hunt}\n", -1, NULL));
	g_assert_true (g_file_set_contents (fsm_path,
	                                    "initial: idle\nstates:\n"
	                                    "- name: idle\n  enter: [{set: mode, value: rest}]\n"
	                                    "  transitions: [{to: hunt, event: seen}]\n"
	                                    "- name: hunt\n  enter: [{set: mode, value: hunt}]\n", -1, NULL));

	/* set the way a level file sets it: by property name */
	ai = g_object_new (LRG_TYPE_AI_COMPONENT, "tree-path", tree_path, "machine-path", fsm_path, NULL);
	lrg_game_object_add_component (owner, LRG_COMPONENT (ai));
	g_assert_true (lrg_blackboard_get_pointer (lrg_ai_component_get_blackboard (ai), LRG_AI_COMPONENT_OWNER_KEY)
	               == (gpointer) owner);
	lrg_ai_component_step (ai, 0.1f);
	g_assert_cmpstr (lrg_ai_component_get_current_state (ai), ==, "idle");
	g_assert_cmpint (lrg_bt_runner_get_status (lrg_ai_component_get_runner (ai)), ==, LRG_BT_STATUS_FAILURE);

	/* the machine runs first in a step: the tree sees its writes */
	lrg_ai_component_send_event (ai, "seen");
	lrg_ai_component_step (ai, 0.1f);
	g_assert_cmpstr (lrg_ai_component_get_current_state (ai), ==, "hunt");
	g_assert_cmpint (lrg_bt_runner_get_status (lrg_ai_component_get_runner (ai)), ==, LRG_BT_STATUS_SUCCESS);

	g_unlink (tree_path);
	g_unlink (fsm_path);
	g_rmdir (dir);
}

/* ==========================================================================
 * The LLM decision inspector hooks (LrgDecisionBrain)
 * ========================================================================== */

static gchar *
scripted_answer (LrgDecider   *decider,
                 const gchar  *request_json,
                 gpointer      user_data,
                 GError      **error)
{
	if (strstr (request_json, "\"broken\"") != NULL)
	{
		g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_FAILED, "the model is away");
		return NULL;
	}
	return g_strdup ("{\"model\":\"test-1\",\"answers\":{\"hostile\":{\"type\":\"noul\",\"noul\":0.9}},"
	                 "\"usage\":{\"input_tokens\":30,\"output_tokens\":4}}");
}

static void
count_asked (LrgDecisionBrain *brain,
             const gchar      *topic,
             const gchar      *request,
             gpointer          user_data)
{
	g_assert_nonnull (strstr (request, "hostile"));
	(*(guint *) user_data)++;
}

static void
wait_idle (LrgDecisionBrain *brain,
           const gchar      *topic)
{
	guint i;

	for (i = 0; i < 1000 && lrg_decision_brain_is_busy (brain, topic); i++)
		g_main_context_iteration (NULL, TRUE);
	g_assert_false (lrg_decision_brain_is_busy (brain, topic));
}

static void
test_decision_inspector (void)
{
	g_autoptr(LrgDecider)         decider = lrg_decider_new_scripted (scripted_answer, NULL, NULL);
	g_autoptr(LrgDecisionBrain)   brain = lrg_decision_brain_new (decider);
	g_autoptr(LrgDecisionRequest) request = lrg_decision_request_new ();
	g_autoptr(LrgDecisionRequest) broken = lrg_decision_request_new ();
	g_autoptr(GError)             error = NULL;
	g_auto(GStrv)                 topics = NULL;
	g_autofree gchar             *state = NULL;
	guint                         asked = 0;
	guint                         n_asked;
	guint                         n_answered;
	guint                         n_cached;
	guint                         n_failed;
	gint64                        in_tokens;
	gdouble                       mean;

	g_assert_true (lrg_decision_request_add_noul (request, "hostile", "Is the stranger hostile?", NULL, NULL, &error));
	lrg_decision_request_set_state_text (request, "a stranger with a drawn sword");
	g_assert_true (lrg_decision_request_add_noul (broken, "hostile", "Is it hostile?", NULL, NULL, &error));
	lrg_decision_request_set_state_text (broken, "broken");
	lrg_decision_brain_set_cache_seconds (brain, 60.0);
	g_signal_connect (brain, "asked", G_CALLBACK (count_asked), &asked);

	g_assert_null (lrg_decision_brain_get_request (brain, "guard"));
	g_assert_cmpfloat (lrg_decision_brain_get_latency (brain, "guard"), ==, -1.0);
	g_assert_null (lrg_decision_brain_dup_topic_state (brain, "guard"));

	g_assert_true (lrg_decision_brain_ask (brain, "guard", request, NULL, &error));
	g_assert_cmpfloat (lrg_decision_brain_get_pending_seconds (brain, "guard"), >=, 0.0);
	wait_idle (brain, "guard");
	g_assert_cmpfloat (lrg_decision_brain_get_pending_seconds (brain, "guard"), ==, -1.0);
	g_assert_cmpfloat (lrg_decision_brain_get_latency (brain, "guard"), >=, 0.0);
	g_assert_nonnull (strstr (lrg_decision_brain_get_request (brain, "guard"), "drawn sword"));

	/* the same question again comes from the cache */
	g_assert_true (lrg_decision_brain_ask (brain, "guard", request, NULL, &error));
	g_assert_true (lrg_decision_brain_ask (brain, "door", broken, NULL, &error));
	wait_idle (brain, "door");
	g_assert_cmpuint (asked, ==, 2);

	topics = lrg_decision_brain_dup_topics (brain);
	g_assert_cmpuint (g_strv_length (topics), ==, 2);
	g_assert_cmpstr (topics[0], ==, "door");
	g_assert_cmpstr (topics[1], ==, "guard");
	g_assert_true (lrg_decision_brain_get_topic_counts (brain, "guard", &n_asked, &n_answered, &n_cached, &n_failed));
	g_assert_cmpuint (n_asked, ==, 1);
	g_assert_cmpuint (n_answered, ==, 1);
	g_assert_cmpuint (n_cached, ==, 1);
	g_assert_cmpuint (n_failed, ==, 0);
	g_assert_true (lrg_decision_brain_get_topic_counts (brain, "door", NULL, NULL, NULL, &n_failed));
	g_assert_cmpuint (n_failed, ==, 1);
	g_assert_false (lrg_decision_brain_get_topic_counts (brain, "nobody", &n_asked, NULL, NULL, NULL));
	g_assert_cmpuint (n_asked, ==, 0);

	lrg_decision_brain_get_totals (brain, &n_asked, &n_answered, &n_cached, &n_failed, &in_tokens, NULL, &mean);
	g_assert_cmpuint (n_asked, ==, 2);
	g_assert_cmpuint (n_answered, ==, 1);
	g_assert_cmpuint (n_cached, ==, 1);
	g_assert_cmpuint (n_failed, ==, 1);
	g_assert_cmpint (in_tokens, ==, 30);
	g_assert_cmpfloat (mean, >=, 0.0);

	state = lrg_decision_brain_dup_topic_state (brain, "guard");
	g_assert_nonnull (strstr (state, "topic: \"guard\"\n"));
	g_assert_nonnull (strstr (state, "busy: false\n"));
	g_assert_nonnull (strstr (state, "model: \"test-1\"\n"));
	g_assert_nonnull (strstr (state, "asked: 1\nanswered: 1\ncached: 1\nfailed: 0\n"));
	g_assert_nonnull (strstr (state, "latest: \"{"));
	g_clear_pointer (&state, g_free);
	state = lrg_decision_brain_dup_topic_state (brain, "door");
	g_assert_nonnull (strstr (state, "error: \"the model is away\""));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	register_bindings ();

	g_test_add_func ("/bt-asset/format/round-trip", test_round_trip);
	g_test_add_func ("/bt-asset/format/file-round-trip", test_file_round_trip);
	g_test_add_func ("/bt-asset/format/strict-errors", test_strict_errors);
	g_test_add_func ("/bt-asset/format/validate-errors", test_validate_errors);
	g_test_add_func ("/bt-asset/nodes/composites", test_composites);
	g_test_add_func ("/bt-asset/nodes/decorators", test_decorators);
	g_test_add_func ("/bt-asset/nodes/leaves", test_leaves);
	g_test_add_func ("/bt-asset/binding/script", test_script_binding);
	g_test_add_func ("/bt-asset/binding/unbound", test_unbound);
#ifdef LRG_HAS_LUAJIT
	g_test_add_func ("/bt-asset/binding/lua", test_lua_binding);
#endif
	g_test_add_func ("/bt-asset/binding/registry", test_registry);
	g_test_add_func ("/bt-asset/schema/defaults", test_schema_defaults);
	g_test_add_func ("/bt-asset/subtrees", test_subtrees);
	g_test_add_func ("/bt-asset/custom-type", test_custom_type);
	g_test_add_func ("/bt-asset/node-model", test_node_model);
	g_test_add_func ("/bt-asset/runner/debug", test_runner_debug);
	g_test_add_func ("/bt-asset/runner/determinism", test_determinism);
	g_test_add_func ("/bt-asset/blackboard", test_blackboard_api);
	g_test_add_func ("/bt-asset/component/tree", test_component_tree);
	g_test_add_func ("/bt-asset/component/paths", test_component_paths);
	g_test_add_func ("/bt-asset/decision-inspector", test_decision_inspector);

	return g_test_run ();
}
