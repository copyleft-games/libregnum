/* test-logic-machine.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the .rfsm logic state machine: format round trip and strict
 * errors, events, conditions (check, C, script), timers, hierarchy and
 * the enter/exit order, any-state transitions, self transitions, events
 * raised by actions, the transition cap, signals, debug state, bindings,
 * the editing API and determinism.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* the order actions ran in, as "a,b,c" */
static GString *trace;

static LrgBTStatus
act_log (LrgBlackboard *bb,
         gfloat         dt,
         GVariant      *params,
         gpointer       user_data)
{
	if (trace->len > 0)
		g_string_append_c (trace, ',');
	g_string_append (trace, lrg_bt_params_get_string (params, "msg", "?"));
	return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus
act_tick (LrgBlackboard *bb,
          gfloat         dt,
          GVariant      *params,
          gpointer       user_data)
{
	lrg_blackboard_set_float (bb, "elapsed", lrg_blackboard_get_float (bb, "elapsed", 0.0f) + dt);
	return LRG_BT_STATUS_SUCCESS;
}

static gboolean
cond_above (LrgBlackboard *bb,
            GVariant      *params,
            gpointer       user_data)
{
	return lrg_blackboard_get_float (bb, "elapsed", 0.0f) > (gfloat) lrg_bt_params_get_double (params, "limit", 0.0);
}

/* a scripting backend whose only function, "script_ok", returns TRUE */
#define TEST_TYPE_SCRIPTING (test_scripting_get_type ())
G_DECLARE_FINAL_TYPE (TestScripting, test_scripting, TEST, SCRIPTING, LrgScripting)

struct _TestScripting
{
	LrgScripting parent_instance;
	guint        calls;
};

G_DEFINE_FINAL_TYPE (TestScripting, test_scripting, LRG_TYPE_SCRIPTING)

static gboolean
test_scripting_call (LrgScripting  *scripting,
                     const gchar   *func_name,
                     GValue        *return_value,
                     guint          n_args,
                     const GValue  *args,
                     GError       **error)
{
	TEST_SCRIPTING (scripting)->calls++;
	g_value_init (return_value, G_TYPE_BOOLEAN);
	g_value_set_boolean (return_value, g_strcmp0 (func_name, "script_ok") == 0);
	return TRUE;
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
	LRG_SCRIPTING_CLASS (klass)->call_function = test_scripting_call;
	LRG_SCRIPTING_CLASS (klass)->has_function = test_scripting_has;
}

static void
test_scripting_init (TestScripting *self)
{
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgLogicMachine *
load_ok (const gchar *text)
{
	g_autoptr(GError) error = NULL;
	LrgLogicMachine  *m = lrg_logic_machine_new_from_data (text, -1, &error);

	g_assert_no_error (error);
	g_assert_nonnull (m);
	return m;
}

static LrgLogicMachine *
start_ok (const gchar  *text,
          LrgScripting *scripting)
{
	g_autoptr(GError) error = NULL;
	LrgLogicMachine  *m = load_ok (text);

	g_string_truncate (trace, 0);
	g_assert_true (lrg_logic_machine_start (m, NULL, scripting, &error));
	g_assert_no_error (error);
	return m;
}

static void
load_fails (const gchar *text,
            gint         code,
            const gchar *prefix)
{
	g_autoptr(GError)          error = NULL;
	g_autoptr(LrgLogicMachine) m = lrg_logic_machine_new_from_data (text, -1, &error);

	g_assert_null (m);
	g_assert_error (error, LRG_LOGIC_MACHINE_ERROR, code);
	if (!g_str_has_prefix (error->message, prefix))
		g_error ("expected '%s...', got '%s'", prefix, error->message);
}

/* ==========================================================================
 * Format
 * ========================================================================== */

static const gchar *DOOR =
	"version: 1\n"
	"name: door\n"
	"blackboard:\n"
	"- name: locked\n"
	"  type: bool\n"
	"  default: false\n"
	"- name: elapsed\n"
	"  type: float\n"
	"initial: closed\n"
	"enter:\n"
	"- action: log\n"
	"  params:\n"
	"    msg: machine\n"
	"transitions:\n"
	"- to: broken\n"
	"  event: smash\n"
	"states:\n"
	"- name: closed\n"
	"  enter:\n"
	"  - action: log\n"
	"    params:\n"
	"      msg: closed\n"
	"  transitions:\n"
	"  - to: opening\n"
	"    event: open\n"
	"    when:\n"
	"    - key: locked\n"
	"      value: false\n"
	"    do:\n"
	"    - set: elapsed\n"
	"      value: 0.0\n"
	"    hint: green\n"
	"  sprite: door_closed\n"
	"- name: opening\n"
	"  update:\n"
	"  - action: tick\n"
	"  transitions:\n"
	"  - to: open\n"
	"    after: 0.5\n"
	"- name: open\n"
	"  transitions:\n"
	"  - to: closed\n"
	"    when:\n"
	"    - condition: above\n"
	"      params:\n"
	"        limit: 2.0\n"
	"  - to: /closed\n"
	"    event: slam\n"
	"- name: broken\n"
	"  enter:\n"
	"  - event: alarm\n"
	"editor:\n"
	"  layout: auto\n";

static void
test_round_trip (void)
{
	g_autoptr(LrgLogicMachine) m = load_ok (DOOR);
	g_autoptr(LrgLogicMachine) again = NULL;
	g_autoptr(LrgLogicMachine) dup = NULL;
	g_autofree gchar          *a = NULL;
	g_autofree gchar          *b = NULL;
	g_autofree gchar          *c = NULL;
	g_autoptr(GError)          error = NULL;

	a = lrg_logic_machine_to_data (m, &error);
	g_assert_no_error (error);
	again = load_ok (a);
	b = lrg_logic_machine_to_data (again, NULL);
	g_assert_cmpstr (a, ==, b);
	g_assert_nonnull (strstr (b, "sprite: door_closed"));
	g_assert_nonnull (strstr (b, "hint: green"));
	g_assert_nonnull (strstr (b, "layout: auto"));
	g_assert_cmpstr (lrg_logic_machine_get_name (again), ==, "door");

	dup = lrg_logic_machine_duplicate (again, &error);
	g_assert_no_error (error);
	c = lrg_logic_machine_to_data (dup, NULL);
	g_assert_cmpstr (a, ==, c);
	g_assert_true (lrg_logic_machine_validate (dup, &error));
	g_assert_no_error (error);
}

static void
test_file_round_trip (void)
{
	g_autoptr(LrgLogicMachine) m = load_ok (DOOR);
	g_autoptr(LrgLogicMachine) loaded = NULL;
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *dir = g_dir_make_tmp ("lrg-fsm-XXXXXX", &error);
	g_autofree gchar          *path = g_build_filename (dir, "door.rfsm", NULL);
	g_autofree gchar          *a = NULL;
	g_autofree gchar          *b = NULL;

	g_assert_no_error (error);
	g_assert_true (lrg_logic_machine_save_to_file (m, path, &error));
	loaded = lrg_logic_machine_new_from_file (path, &error);
	g_assert_no_error (error);
	a = lrg_logic_machine_to_data (m, NULL);
	b = lrg_logic_machine_to_data (loaded, NULL);
	g_assert_cmpstr (a, ==, b);
	g_unlink (path);
	g_rmdir (dir);
}

static void
test_strict_errors (void)
{
	load_fails ("states:\n- name: a\n  transitions: [{to: b}]\n", LRG_LOGIC_MACHINE_ERROR_REFERENCE,
	            "states[0].transitions[0].to:");
	load_fails ("states:\n- name: a\n- name: a\n", LRG_LOGIC_MACHINE_ERROR_INVALID, "states[1].name:");
	load_fails ("initial: z\nstates:\n- name: a\n", LRG_LOGIC_MACHINE_ERROR_REFERENCE, "initial:");
	load_fails ("states:\n- name: a\n  enter: [{action: x, event: y}]\n", LRG_LOGIC_MACHINE_ERROR_INVALID,
	            "states[0].enter[0]:");
	load_fails ("states:\n- name: a\n  enter: [{shout: x}]\n", LRG_LOGIC_MACHINE_ERROR_INVALID, "states[0].enter[0]:");
	load_fails ("states:\n- name: a\n  enter: [{set: x}]\n", LRG_LOGIC_MACHINE_ERROR_INVALID, "states[0].enter[0]:");
	load_fails ("states:\n- name: a\n  transitions: [{to: a, when: [{key: k, op: about, value: 1}]}]\n",
	            LRG_LOGIC_MACHINE_ERROR_INVALID, "states[0].transitions[0].when[0].op:");
	load_fails ("states:\n- name: a\n  transitions: [{to: a, after: -1}]\n", LRG_LOGIC_MACHINE_ERROR_INVALID,
	            "states[0].transitions[0].after:");
	load_fails ("states:\n- name: a/b\n", LRG_LOGIC_MACHINE_ERROR_INVALID, "states[0].name:");
	load_fails ("version: 7\n", LRG_LOGIC_MACHINE_ERROR_VERSION, "version:");
	load_fails ("[1, 2]\n", LRG_LOGIC_MACHINE_ERROR_PARSE, "the document");
}

/* ==========================================================================
 * Runtime
 * ========================================================================== */

static void
test_events_conditions_timers (void)
{
	g_autoptr(LrgLogicMachine) m = start_ok (DOOR, NULL);
	LrgBlackboard             *bb = lrg_logic_machine_get_blackboard (m);
	const gchar               *from;
	const gchar               *to;
	const gchar               *event;

	g_assert_cmpstr (trace->str, ==, "machine,closed");
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "closed");
	g_assert_false (lrg_blackboard_get_bool (bb, "locked", TRUE));    /* schema default */

	/* locked: the open event is dropped */
	lrg_blackboard_set_bool (bb, "locked", TRUE);
	lrg_logic_machine_send_event (m, "open");
	lrg_logic_machine_update (m, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "closed");

	/* unlocked: it opens; the do: action reset the timer key */
	lrg_blackboard_set_bool (bb, "locked", FALSE);
	lrg_blackboard_set_float (bb, "elapsed", 9.0f);
	lrg_logic_machine_send_event (m, "open");
	lrg_logic_machine_update (m, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "opening");
	g_assert_true (lrg_logic_machine_get_last_transition (m, &from, &to, &event) != NULL);
	g_assert_cmpstr (from, ==, "closed");
	g_assert_cmpstr (to, ==, "opening");
	g_assert_cmpstr (event, ==, "open");

	/* after: 0.5 s in opening (update actions run each step) */
	lrg_logic_machine_update (m, 0.2f);
	lrg_logic_machine_update (m, 0.2f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "opening");
	g_assert_cmpfloat_with_epsilon (lrg_logic_machine_get_state_time (m, NULL), 0.4, 1e-6);
	lrg_logic_machine_update (m, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "open");
	/* opening's update ran in updates 2, 3 and 4 (0.1 + 0.2 + 0.2) */
	g_assert_cmpfloat_with_epsilon (lrg_blackboard_get_float (bb, "elapsed", 0.0f), 0.5f, 1e-5f);

	/* the C condition: elapsed > 2 closes it */
	lrg_logic_machine_update (m, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "open");
	lrg_blackboard_set_float (bb, "elapsed", 3.0f);
	lrg_logic_machine_update (m, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "closed");
	g_assert_cmpuint (lrg_logic_machine_get_update_count (m), ==, 7);
}

static void
test_any_state_and_raised_events (void)
{
	g_autoptr(LrgLogicMachine) m = start_ok (DOOR, NULL);
	g_autofree gchar          *state = NULL;

	lrg_logic_machine_send_event (m, "smash");
	lrg_logic_machine_update (m, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "broken");
	/* broken's enter raised "alarm": queued for the next update */
	state = lrg_logic_machine_dup_debug_state (m);
	g_assert_nonnull (strstr (state, "queued: [\"alarm\"]"));
	g_assert_nonnull (strstr (state, "last_transition: {from: \"closed\", to: \"broken\", event: \"smash\"}"));
	g_assert_nonnull (strstr (state, "{state: \"broken\", time: 0.0000}"));

	/* any-state transitions do not re-enter where they lead */
	g_string_truncate (trace, 0);
	lrg_logic_machine_send_event (m, "smash");
	lrg_logic_machine_update (m, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "broken");
	g_assert_cmpuint (lrg_logic_transition_get_fire_count (lrg_logic_state_get_transition (
	                      lrg_logic_machine_get_root (m), 0)), ==, 1);

	lrg_logic_machine_stop (m);
	g_assert_false (lrg_logic_machine_is_running (m));
	g_assert_null (lrg_logic_machine_get_active_state (m));
	g_assert_cmpfloat (lrg_logic_machine_get_state_time (m, NULL), ==, -1.0);
}

static const gchar *NESTED =
	"initial: idle\n"
	"states:\n"
	"- name: idle\n"
	"  enter: [{action: log, params: {msg: +idle}}]\n"
	"  exit: [{action: log, params: {msg: -idle}}]\n"
	"  transitions: [{to: combat, event: alert}]\n"
	"- name: combat\n"
	"  initial: approach\n"
	"  enter: [{action: log, params: {msg: +combat}}]\n"
	"  exit: [{action: log, params: {msg: -combat}}]\n"
	"  transitions:\n"
	"  - {to: idle, event: calm, do: [{action: log, params: {msg: do}}]}\n"
	"  - {to: combat, event: reset}\n"
	"  states:\n"
	"  - name: approach\n"
	"    enter: [{action: log, params: {msg: +approach}}]\n"
	"    exit: [{action: log, params: {msg: -approach}}]\n"
	"    transitions:\n"
	"    - {to: attack, event: near}\n"
	"    - {to: idle, event: bored}\n"
	"  - name: attack\n"
	"    enter: [{action: log, params: {msg: +attack}}]\n"
	"    exit: [{action: log, params: {msg: -attack}}]\n"
	"    transitions:\n"
	"    - {to: combat/approach, event: far}\n"
	"    - {to: attack, event: again}\n";

static guint entered;
static guint exited;
static guint transitioned;

static void
on_entered (LrgLogicMachine *m,
            const gchar     *path,
            gpointer         data)
{
	entered++;
}

static void
on_exited (LrgLogicMachine *m,
           const gchar     *path,
           gpointer         data)
{
	exited++;
}

static void
on_transitioned (LrgLogicMachine *m,
                 const gchar     *from,
                 const gchar     *to,
                 const gchar     *event,
                 gpointer         data)
{
	transitioned++;
}

static void
step_event (LrgLogicMachine *m,
            const gchar     *event)
{
	g_string_truncate (trace, 0);
	lrg_logic_machine_send_event (m, event);
	lrg_logic_machine_update (m, 0.1f);
}

static void
test_hierarchy (void)
{
	g_autoptr(LrgLogicMachine) m = start_ok (NESTED, NULL);
	g_auto(GStrv)              path = NULL;

	g_signal_connect (m, "state-entered", G_CALLBACK (on_entered), NULL);
	g_signal_connect (m, "state-exited", G_CALLBACK (on_exited), NULL);
	g_signal_connect (m, "transitioned", G_CALLBACK (on_transitioned), NULL);
	g_assert_cmpstr (trace->str, ==, "+idle");

	/* into a composite: its initial child is entered after it */
	step_event (m, "alert");
	g_assert_cmpstr (trace->str, ==, "-idle,+combat,+approach");
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "combat/approach");
	path = lrg_logic_machine_dup_active_path (m);
	g_assert_cmpuint (g_strv_length (path), ==, 2);
	g_assert_cmpstr (path[0], ==, "combat");
	g_assert_true (lrg_logic_machine_is_in_state (m, "combat"));
	g_assert_true (lrg_logic_machine_is_in_state (m, "/combat/approach"));
	g_assert_false (lrg_logic_machine_is_in_state (m, "idle"));
	g_assert_cmpuint (entered, ==, 2);
	g_assert_cmpuint (exited, ==, 1);

	/* between siblings: the parent stays */
	step_event (m, "near");
	g_assert_cmpstr (trace->str, ==, "-approach,+attack");
	step_event (m, "far");
	g_assert_cmpstr (trace->str, ==, "-attack,+approach");

	/* a self transition exits and re-enters */
	step_event (m, "near");
	step_event (m, "again");
	g_assert_cmpstr (trace->str, ==, "-attack,+attack");

	/* a parent's self transition restarts its sub-machine */
	step_event (m, "reset");
	g_assert_cmpstr (trace->str, ==, "-attack,-combat,+combat,+approach");

	/* an outer transition wins over an inner one for the same event;
	 * its do: actions run between the exits and the entries */
	step_event (m, "calm");
	g_assert_cmpstr (trace->str, ==, "-approach,-combat,do,+idle");

	/* a child leaving to a state outside its parent */
	step_event (m, "alert");
	step_event (m, "bored");
	g_assert_cmpstr (trace->str, ==, "-approach,-combat,+idle");
	g_assert_cmpuint (transitioned, ==, 9);

	/* stop exits innermost first */
	g_string_truncate (trace, 0);
	lrg_logic_machine_stop (m);
	g_assert_cmpstr (trace->str, ==, "-idle");
}

static void
test_cap_and_scripts (void)
{
	g_autoptr(TestScripting)   scripting = g_object_new (TEST_TYPE_SCRIPTING, NULL);
	g_autoptr(LrgLogicMachine) loop = NULL;
	g_autoptr(LrgLogicMachine) m = NULL;

	/* two states that flip every check: the cap bounds one update */
	loop = start_ok ("states:\n- name: a\n  transitions: [{to: b}]\n- name: b\n  transitions: [{to: a}]\n", NULL);
	lrg_logic_machine_set_max_transitions (loop, 3);
	g_assert_cmpuint (lrg_logic_machine_get_max_transitions (loop), ==, 3);
	lrg_logic_machine_update (loop, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (loop), ==, "b");
	g_assert_cmpuint (lrg_logic_transition_get_fire_count (lrg_logic_state_get_transition (
	                      lrg_logic_machine_find_state (loop, "a"), 0)), ==, 2);

	/* script conditions and actions */
	m = start_ok ("states:\n- name: a\n  enter: [{script: script_note, params: {n: 1}}]\n"
	              "  transitions: [{to: b, when: [{script: script_nope}]}, {to: c, when: [{script: script_ok}]}]\n"
	              "- name: b\n- name: c\n", LRG_SCRIPTING (scripting));
	g_assert_cmpuint (scripting->calls, ==, 1);
	lrg_logic_machine_update (m, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (m), ==, "c");
}

static void
test_bindings (void)
{
	g_autoptr(LrgLogicMachine) m = load_ok ("states:\n- name: a\n  enter: [{action: missing_act}]\n"
	                                        "  transitions: [{to: a, event: e, when: [{condition: missing_cond}]},"
	                                        " {to: a, event: f, when: [{script: whatever}]}]\n");
	g_autoptr(GError)          error = NULL;
	g_auto(GStrv)              missing = NULL;

	g_assert_false (lrg_logic_machine_start (m, NULL, NULL, &error));
	g_assert_error (error, LRG_LOGIC_MACHINE_ERROR, LRG_LOGIC_MACHINE_ERROR_BINDING);
	g_clear_error (&error);
	missing = lrg_logic_machine_dup_unbound (m, NULL);
	g_assert_cmpuint (g_strv_length (missing), ==, 3);
	g_assert_cmpstr (missing[0], ==, "action:missing_act");

	lrg_logic_machine_set_allow_unbound (m, TRUE);
	g_assert_true (lrg_logic_machine_get_allow_unbound (m));
	g_test_expect_message ("Libregnum-AI", G_LOG_LEVEL_WARNING, "*missing_act*");
	g_assert_true (lrg_logic_machine_start (m, NULL, NULL, &error));
	g_test_assert_expected_messages ();
	g_test_expect_message ("Libregnum-AI", G_LOG_LEVEL_WARNING, "*missing_cond*");
	lrg_logic_machine_send_event (m, "e");
	lrg_logic_machine_update (m, 0.1f);
	g_test_assert_expected_messages ();
	/* once only */
	lrg_logic_machine_send_event (m, "e");
	lrg_logic_machine_update (m, 0.1f);
}

static void
test_validate (void)
{
	g_autoptr(LrgLogicMachine) empty = lrg_logic_machine_new ();
	g_autoptr(LrgLogicMachine) keys = load_ok ("blackboard:\n- {name: hp, type: int}\n"
	                                           "states:\n- name: a\n  enter: [{set: mana, value: 1}]\n");
	g_autoptr(GError)          error = NULL;

	g_assert_false (lrg_logic_machine_validate (empty, &error));
	g_assert_error (error, LRG_LOGIC_MACHINE_ERROR, LRG_LOGIC_MACHINE_ERROR_INVALID);
	g_clear_error (&error);
	g_assert_false (lrg_logic_machine_start (keys, NULL, NULL, &error));
	g_assert_error (error, LRG_LOGIC_MACHINE_ERROR, LRG_LOGIC_MACHINE_ERROR_REFERENCE);
	g_assert_nonnull (strstr (error->message, "'mana'"));
}

/* ==========================================================================
 * Editing API
 * ========================================================================== */

static void
test_editing_api (void)
{
	g_autoptr(LrgLogicMachine)    m = lrg_logic_machine_new ();
	g_autoptr(LrgLogicState)      a = lrg_logic_state_new ("a");
	g_autoptr(LrgLogicState)      b = lrg_logic_state_new ("b");
	g_autoptr(LrgLogicState)      b1 = lrg_logic_state_new ("b1");
	g_autoptr(LrgLogicTransition) t = lrg_logic_transition_new ("b");
	g_autoptr(LrgLogicAction)     act = lrg_logic_action_new_call ("log", NULL);
	g_autoptr(LrgLogicAction)     set = lrg_logic_action_new_set ("ready", g_variant_new_boolean (TRUE));
	g_autoptr(LrgLogicAction)     ev = lrg_logic_action_new_event ("ping");
	g_autoptr(LrgLogicAction)     scr = lrg_logic_action_new_script ("script_x", NULL);
	g_autoptr(LrgLogicCondition)  chk = lrg_logic_condition_new_check ("ready", LRG_BLACKBOARD_OP_EQ,
	                                                                   g_variant_new_boolean (TRUE));
	g_autoptr(LrgLogicCondition)  call = lrg_logic_condition_new_call ("above", NULL);
	g_autoptr(LrgLogicCondition)  sc = lrg_logic_condition_new_script ("script_ok", NULL);
	g_autoptr(LrgLogicMachine)    back = NULL;
	g_autoptr(GError)             error = NULL;
	g_autofree gchar             *text = NULL;
	g_autofree gchar             *p = NULL;
	LrgLogicState                *root = lrg_logic_machine_get_root (m);

	lrg_logic_machine_set_name (m, "made");
	lrg_blackboard_schema_add_key (lrg_logic_machine_get_schema (m), "ready", G_TYPE_BOOLEAN, NULL);
	lrg_logic_state_add_state (root, a);
	lrg_logic_state_add_state (root, b);
	lrg_logic_state_add_state (b, b1);
	lrg_logic_state_set_initial (root, "a");
	g_assert_cmpstr (lrg_logic_state_get_initial (root), ==, "a");
	lrg_logic_state_add_action (a, LRG_LOGIC_PHASE_ENTER, set);
	lrg_logic_state_add_action (a, LRG_LOGIC_PHASE_EXIT, act);
	lrg_logic_state_add_action (b1, LRG_LOGIC_PHASE_UPDATE, ev);
	lrg_logic_transition_set_event (t, "go");
	lrg_logic_transition_set_after (t, 0.25);
	lrg_logic_transition_add_condition (t, chk);
	lrg_logic_transition_add_action (t, act);
	lrg_logic_state_add_transition (a, t);

	g_assert_cmpuint (lrg_logic_state_get_state_count (root), ==, 2);
	g_assert_true (lrg_logic_state_get_state (root, 1) == b);
	g_assert_true (lrg_logic_state_find_state (b, "b1") == b1);
	g_assert_true (lrg_logic_state_get_parent (b1) == b);
	g_assert_true (lrg_logic_machine_find_state (m, "b/b1") == b1);
	p = lrg_logic_state_dup_path (b1);
	g_assert_cmpstr (p, ==, "b/b1");
	g_assert_cmpuint (lrg_logic_state_get_action_count (a, LRG_LOGIC_PHASE_ENTER), ==, 1);
	g_assert_cmpint (lrg_logic_action_get_kind (lrg_logic_state_get_action (a, LRG_LOGIC_PHASE_ENTER, 0)), ==,
	                 LRG_LOGIC_ACTION_SET);
	g_assert_cmpstr (lrg_logic_action_get_name (scr), ==, "script_x");
	g_assert_null (lrg_logic_action_get_params (scr));
	g_assert_true (g_variant_get_boolean (lrg_logic_action_get_value (set)));
	g_assert_cmpuint (lrg_logic_transition_get_condition_count (t), ==, 1);
	g_assert_cmpint (lrg_logic_condition_get_kind (lrg_logic_transition_get_condition (t, 0)), ==,
	                 LRG_LOGIC_CONDITION_CHECK);
	g_assert_cmpint (lrg_logic_condition_get_op (chk), ==, LRG_BLACKBOARD_OP_EQ);
	g_assert_true (g_variant_get_boolean (lrg_logic_condition_get_value (chk)));
	g_assert_cmpstr (lrg_logic_condition_get_name (call), ==, "above");
	g_assert_null (lrg_logic_condition_get_params (sc));
	g_assert_cmpuint (lrg_logic_transition_get_action_count (t), ==, 1);
	g_assert_cmpstr (lrg_logic_action_get_name (lrg_logic_transition_get_action (t, 0)), ==, "log");
	g_assert_cmpstr (lrg_logic_transition_get_target (t), ==, "b");
	g_assert_cmpstr (lrg_logic_transition_get_event (t), ==, "go");
	g_assert_cmpfloat (lrg_logic_transition_get_after (t), ==, 0.25);
	g_assert_cmpuint (lrg_logic_state_get_transition_count (a), ==, 1);

	text = lrg_logic_machine_to_data (m, &error);
	g_assert_no_error (error);
	back = load_ok (text);
	g_assert_true (lrg_logic_machine_validate (back, &error));
	g_assert_no_error (error);

	/* run it: go is refused before 0.25 s, then taken */
	g_string_truncate (trace, 0);
	g_assert_true (lrg_logic_machine_start (back, NULL, NULL, &error));
	lrg_logic_machine_send_event (back, "go");
	lrg_logic_machine_update (back, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (back), ==, "a");
	lrg_logic_machine_update (back, 0.2f);
	lrg_logic_machine_send_event (back, "go");
	lrg_logic_machine_update (back, 0.1f);
	g_assert_cmpstr (lrg_logic_machine_get_active_state (back), ==, "b/b1");
	g_assert_cmpstr (trace->str, ==, "?,?");    /* a's exit, the transition's do */

	/* removal */
	g_assert_true (lrg_logic_transition_remove_condition (t, 0));
	g_assert_false (lrg_logic_transition_remove_condition (t, 0));
	g_assert_true (lrg_logic_transition_remove_action (t, 0));
	g_assert_true (lrg_logic_state_remove_action (a, LRG_LOGIC_PHASE_EXIT, 0));
	g_assert_true (lrg_logic_state_remove_transition (a, t));
	g_assert_true (lrg_logic_state_remove_state (b, b1));
	g_assert_null (lrg_logic_state_get_parent (b1));
	lrg_logic_state_set_name (b, "bee");
	g_assert_cmpstr (lrg_logic_state_get_name (b), ==, "bee");
	lrg_logic_transition_set_target (t, "bee");
}

/* ==========================================================================
 * Determinism
 * ========================================================================== */

static gchar *
run_session (const gchar *text)
{
	g_autoptr(LrgLogicMachine) m = start_ok (text, NULL);
	GString                   *out = g_string_new (NULL);
	guint                      i;
	static const gchar * const events[] = { "alert", "near", "far", "near", "again", "calm", "reset" };

	for (i = 0; i < 60; i++)
	{
		g_autofree gchar *s = NULL;

		if (i % 5 == 0)
			lrg_logic_machine_send_event (m, events[(i / 5) % G_N_ELEMENTS (events)]);
		lrg_logic_machine_update (m, 1.0f / 60.0f);
		s = lrg_logic_machine_dup_debug_state (m);
		g_string_append (out, s);
	}
	g_string_append (out, trace->str);
	return g_string_free (out, FALSE);
}

static void
test_determinism (void)
{
	g_autofree gchar *a = run_session (NESTED);
	g_autofree gchar *b = run_session (NESTED);

	g_assert_cmpstr (a, ==, b);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	trace = g_string_new (NULL);
	lrg_bt_register_action ("log", act_log, NULL, NULL);
	lrg_bt_register_action ("tick", act_tick, NULL, NULL);
	lrg_bt_register_condition ("above", cond_above, NULL, NULL);

	g_test_add_func ("/logic-machine/format/round-trip", test_round_trip);
	g_test_add_func ("/logic-machine/format/file-round-trip", test_file_round_trip);
	g_test_add_func ("/logic-machine/format/strict-errors", test_strict_errors);
	g_test_add_func ("/logic-machine/runtime/events-conditions-timers", test_events_conditions_timers);
	g_test_add_func ("/logic-machine/runtime/any-state-and-raised-events", test_any_state_and_raised_events);
	g_test_add_func ("/logic-machine/runtime/hierarchy", test_hierarchy);
	g_test_add_func ("/logic-machine/runtime/cap-and-scripts", test_cap_and_scripts);
	g_test_add_func ("/logic-machine/runtime/bindings", test_bindings);
	g_test_add_func ("/logic-machine/validate", test_validate);
	g_test_add_func ("/logic-machine/editing-api", test_editing_api);
	g_test_add_func ("/logic-machine/determinism", test_determinism);

	return g_test_run ();
}
