/* test-editor-actions.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorActions as a GActionGroup, accelerator conversion, accel
 * overrides, the deprecated LrgGuiKey API, and the 0.8.1 settings keys.
 * Headless: no window, no GPU.
 */

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <libregnum.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/*
 * State:
 *
 * The fake application state the action callbacks read and write.
 */
typedef struct
{
	gint     runs;
	gboolean can_act;
	gboolean toggle;
} State;

static void
run_count (gpointer data)
{
	((State *) data)->runs++;
}

static void
run_toggle (gpointer data)
{
	State *s = data;

	s->runs++;
	s->toggle = !s->toggle;
}

static gboolean
is_enabled (gpointer data)
{
	return ((State *) data)->can_act;
}

static gboolean
is_toggled (gpointer data)
{
	return ((State *) data)->toggle;
}

/*
 * Recorder:
 *
 * Collects GActionGroup / registry signal emissions as "kind:id" lines.
 */
typedef struct
{
	GPtrArray *events;
} Recorder;

static void
on_added (GActionGroup *g,
          const gchar  *name,
          gpointer      data)
{
	g_ptr_array_add (((Recorder *) data)->events, g_strdup_printf ("added:%s", name));
}

static void
on_removed (GActionGroup *g,
            const gchar  *name,
            gpointer      data)
{
	g_ptr_array_add (((Recorder *) data)->events, g_strdup_printf ("removed:%s", name));
}

static void
on_enabled_changed (GActionGroup *g,
                    const gchar  *name,
                    gboolean      enabled,
                    gpointer      data)
{
	g_ptr_array_add (((Recorder *) data)->events,
	                 g_strdup_printf ("enabled:%s=%d", name, enabled ? 1 : 0));
}

static void
on_state_changed (GActionGroup *g,
                  const gchar  *name,
                  GVariant     *state,
                  gpointer      data)
{
	g_ptr_array_add (((Recorder *) data)->events,
	                 g_strdup_printf ("state:%s=%d", name, g_variant_get_boolean (state) ? 1 : 0));
}

static void
on_activated (LrgEditorActions *a,
              const gchar      *id,
              gpointer          data)
{
	g_ptr_array_add (((Recorder *) data)->events, g_strdup_printf ("activated:%s", id));
}

static void
on_accels_changed (LrgEditorActions *a,
                   const gchar      *id,
                   gpointer          data)
{
	g_ptr_array_add (((Recorder *) data)->events, g_strdup_printf ("accels:%s", id));
}

static void
recorder_connect (Recorder         *r,
                  LrgEditorActions *actions)
{
	r->events = g_ptr_array_new_with_free_func (g_free);
	g_signal_connect (actions, "action-added", G_CALLBACK (on_added), r);
	g_signal_connect (actions, "action-removed", G_CALLBACK (on_removed), r);
	g_signal_connect (actions, "action-enabled-changed", G_CALLBACK (on_enabled_changed), r);
	g_signal_connect (actions, "action-state-changed", G_CALLBACK (on_state_changed), r);
	g_signal_connect (actions, "activated", G_CALLBACK (on_activated), r);
	g_signal_connect (actions, "accels-changed", G_CALLBACK (on_accels_changed), r);
}

static gboolean
recorder_has (Recorder    *r,
              const gchar *line)
{
	guint i;

	for (i = 0; i < r->events->len; i++)
		if (g_strcmp0 (g_ptr_array_index (r->events, i), line) == 0)
			return TRUE;
	return FALSE;
}

/*
 * make_registry:
 *
 * Three actions: a plain command, a conditionally enabled command and a
 * toggle.
 */
static LrgEditorActions *
make_registry (LrgEditorSettings *settings,
               State             *s)
{
	LrgEditorActions *actions = lrg_editor_actions_new (settings);

	lrg_editor_actions_add (actions, "file.save", "Save", "File", "Ctrl+S",
	                        run_count, NULL, NULL, s);
	lrg_editor_actions_add (actions, "edit.select-all", "Select All", "Edit", "<Ctrl>a",
	                        run_count, is_enabled, NULL, s);
	lrg_editor_actions_add (actions, "view.grid", "Show Grid", "View", "Shift+G",
	                        run_toggle, NULL, is_toggled, s);
	return actions;
}

/* ==========================================================================
 * GActionGroup conformance
 * ========================================================================== */

static void
test_group_list_and_query (void)
{
	State                        s = { 0, TRUE, FALSE };
	g_autoptr(LrgEditorActions)  actions = make_registry (NULL, &s);
	GActionGroup                *group = G_ACTION_GROUP (actions);
	g_auto(GStrv)                names = NULL;
	g_autoptr(GVariant)          state = NULL;
	const GVariantType          *ptype = NULL;
	const GVariantType          *stype = NULL;
	gboolean                     enabled = FALSE;

	g_assert_true (G_IS_ACTION_GROUP (actions));

	/* list_actions: every id, sorted */
	names = g_action_group_list_actions (group);
	g_assert_cmpuint (g_strv_length (names), ==, 3);
	g_assert_cmpstr (names[0], ==, "edit.select-all");
	g_assert_cmpstr (names[1], ==, "file.save");
	g_assert_cmpstr (names[2], ==, "view.grid");

	/* has / enabled / parameter type */
	g_assert_true (g_action_group_has_action (group, "file.save"));
	g_assert_false (g_action_group_has_action (group, "file.nope"));
	g_assert_true (g_action_group_get_action_enabled (group, "file.save"));
	g_assert_null (g_action_group_get_action_parameter_type (group, "file.save"));

	s.can_act = FALSE;
	g_assert_false (g_action_group_get_action_enabled (group, "edit.select-all"));
	s.can_act = TRUE;
	g_assert_true (g_action_group_get_action_enabled (group, "edit.select-all"));

	/* Stateless vs boolean state */
	g_assert_null (g_action_group_get_action_state_type (group, "file.save"));
	g_assert_null (g_action_group_get_action_state (group, "file.save"));
	g_assert_true (g_variant_type_equal (g_action_group_get_action_state_type (group, "view.grid"),
	                                     G_VARIANT_TYPE_BOOLEAN));
	state = g_action_group_get_action_state (group, "view.grid");
	g_assert_nonnull (state);
	g_assert_false (g_variant_get_boolean (state));
	g_assert_null (g_action_group_get_action_state_hint (group, "view.grid"));

	/* query_action in one go */
	g_clear_pointer (&state, g_variant_unref);
	s.toggle = TRUE;
	g_assert_true (g_action_group_query_action (group, "view.grid", &enabled, &ptype, &stype, NULL,
	                                            &state));
	g_assert_true (enabled);
	g_assert_null (ptype);
	g_assert_nonnull (stype);
	g_assert_true (g_variant_get_boolean (state));
	g_assert_false (g_action_group_query_action (group, "zzz", NULL, NULL, NULL, NULL, NULL));
}

static void
test_group_activate (void)
{
	State                        s = { 0, TRUE, FALSE };
	g_autoptr(LrgEditorActions)  actions = make_registry (NULL, &s);
	GActionGroup                *group = G_ACTION_GROUP (actions);
	Recorder                     r;

	recorder_connect (&r, actions);

	/* Without a target: runs and emits ::activated */
	g_action_group_activate_action (group, "file.save", NULL);
	g_assert_cmpint (s.runs, ==, 1);
	g_assert_true (recorder_has (&r, "activated:file.save"));

	/* A disabled action does not run */
	s.can_act = FALSE;
	g_action_group_activate_action (group, "edit.select-all", NULL);
	g_assert_cmpint (s.runs, ==, 1);
	s.can_act = TRUE;

	/* A boolean target on a toggle sets the state (runs only on change) */
	g_action_group_activate_action (group, "view.grid", g_variant_new_boolean (TRUE));
	g_assert_true (s.toggle);
	g_assert_cmpint (s.runs, ==, 2);
	g_action_group_activate_action (group, "view.grid", g_variant_new_boolean (TRUE));
	g_assert_true (s.toggle);
	g_assert_cmpint (s.runs, ==, 2);

	/* change_action_state does the same */
	g_action_group_change_action_state (group, "view.grid", g_variant_new_boolean (FALSE));
	g_assert_false (s.toggle);
	g_assert_cmpint (s.runs, ==, 3);

	/* Plain activation of a toggle flips it */
	g_action_group_activate_action (group, "view.grid", NULL);
	g_assert_true (s.toggle);
	g_assert_cmpint (s.runs, ==, 4);

	/* A target on a parameterless command is ignored */
	g_action_group_activate_action (group, "file.save", g_variant_new_string ("x"));
	g_action_group_activate_action (group, "file.save", g_variant_new_boolean (TRUE));
	g_assert_cmpint (s.runs, ==, 4);

	/* Unknown names are ignored */
	g_action_group_activate_action (group, "nope.nope", NULL);
	g_assert_cmpint (s.runs, ==, 4);

	g_ptr_array_unref (r.events);
}

static void
test_group_refresh_signals (void)
{
	State                        s = { 0, TRUE, FALSE };
	g_autoptr(LrgEditorActions)  actions = make_registry (NULL, &s);
	Recorder                     r;

	recorder_connect (&r, actions);

	/* First refresh records silently, an unchanged one emits nothing */
	g_assert_cmpuint (lrg_editor_actions_refresh_state (actions), ==, 0);
	g_assert_cmpuint (lrg_editor_actions_refresh_state (actions), ==, 0);
	g_assert_cmpuint (r.events->len, ==, 0);

	/* Changes behind the registry's back show up on the next refresh */
	s.can_act = FALSE;
	s.toggle = TRUE;
	g_assert_cmpuint (lrg_editor_actions_refresh_state (actions), ==, 2);
	g_assert_true (recorder_has (&r, "enabled:edit.select-all=0"));
	g_assert_true (recorder_has (&r, "state:view.grid=1"));
	g_assert_cmpuint (r.events->len, ==, 2);
	g_assert_cmpuint (lrg_editor_actions_refresh_state (actions), ==, 0);

	/* Activation refreshes on its own */
	g_ptr_array_set_size (r.events, 0);
	g_assert_true (lrg_editor_actions_activate (actions, "view.grid"));
	g_assert_true (recorder_has (&r, "activated:view.grid"));
	g_assert_true (recorder_has (&r, "state:view.grid=0"));

	/* Stateless actions never emit state changes */
	g_ptr_array_set_size (r.events, 0);
	s.can_act = TRUE;
	lrg_editor_actions_refresh_state (actions);
	g_assert_true (recorder_has (&r, "enabled:edit.select-all=1"));
	g_assert_false (recorder_has (&r, "state:file.save=0"));

	g_ptr_array_unref (r.events);
}

static void
test_group_add_replace (void)
{
	State                        s = { 0, TRUE, FALSE };
	g_autoptr(LrgEditorActions)  actions = lrg_editor_actions_new (NULL);
	Recorder                     r;

	recorder_connect (&r, actions);
	lrg_editor_actions_add (actions, "file.save", "Save", "File", "Ctrl+S", run_count, NULL, NULL, &s);
	g_assert_true (recorder_has (&r, "added:file.save"));
	g_assert_cmpuint (r.events->len, ==, 1);

	/* Replacing announces remove + add, and the new default */
	lrg_editor_actions_add (actions, "file.save", "Save", "File", "Ctrl+Alt+S", run_count, NULL, NULL,
	                        &s);
	g_assert_cmpstr (g_ptr_array_index (r.events, 1), ==, "removed:file.save");
	g_assert_cmpstr (g_ptr_array_index (r.events, 2), ==, "added:file.save");
	g_assert_true (recorder_has (&r, "accels:file.save"));

	g_ptr_array_unref (r.events);
}

/* The ids lrgeditor registers (src/editor/ui/lrg-editor-app-actions.c):
 * all must be valid GAction names, as they are used verbatim. */
static const gchar * const app_action_ids[] = {
	"file.new-level", "file.open-level", "file.save", "file.save-as", "file.reload",
	"file.new-project", "file.open-project", "file.project-settings", "file.import-scene",
	"file.quit", "edit.undo", "edit.redo", "edit.cut", "edit.copy", "edit.paste",
	"edit.duplicate", "edit.delete", "edit.select-all", "edit.deselect", "edit.find",
	"edit.preferences", "node.rename", "node.toggle-visible", "node.toggle-lock", "node.group",
	"node.ungroup", "node.move-up", "node.move-down", "node.save-prefab",
	"node.instantiate-prefab", "node.attach-script", "node.open-scripts", "view.focus",
	"view.frame-all", "view.top", "view.bottom", "view.front", "view.back", "view.right",
	"view.left", "view.perspective", "view.orbit-left", "view.orbit-right", "view.orbit-up",
	"view.orbit-down", "view.zoom-in", "view.zoom-out", "view.toggle-2d", "view.grid",
	"view.icons", "view.wireframe", "view.stats", "view.reset-layout", "tool.select",
	"tool.move", "tool.rotate", "tool.scale", "tool.toggle-space", "tool.toggle-snap",
	"play.start", "play.pause", "play.step", "play.stop", "run.start", "run.stop",
	"tools.validate", "tools.palette", "tools.quick-open", "tools.open-level-yaml",
	"tools.reveal-socket", "tools.refresh-assets", "tools.rebuild-thumbnails", "view.asset-grid",
	"tilemap.paint-mode", "tilemap.tool-brush", "tilemap.tool-eraser", "tilemap.tool-rect",
	"tilemap.tool-fill", "tilemap.tool-pick", "tilemap.brush-grow", "tilemap.brush-shrink",
	"tilemap.add-layer", "tilemap.remove-layer", "tilemap.show-panel", "timeline.show-panel",
	"timeline.new", "timeline.key", "timeline.play", "timeline.stop", "timeline.next-key",
	"timeline.prev-key", "timeline.delete-key", "help.shortcuts", "help.docs", "help.about",
	NULL
};

/* The app's default shortcuts (0.8.0 syntax) all convert. */
static const gchar * const app_shortcuts[] = {
	"Ctrl+N", "Ctrl+O", "Ctrl+S", "Ctrl+Shift+S", "Ctrl+Shift+O", "Ctrl+Q", "Ctrl+Z",
	"Ctrl+Shift+Z|Ctrl+Y", "Ctrl+X", "Ctrl+C", "Ctrl+V", "Ctrl+D", "Delete", "Ctrl+A",
	"Ctrl+Shift+A", "Ctrl+F", "Ctrl+,", "F2", "H", "Ctrl+L", "Ctrl+G", "Ctrl+Shift+G", "Alt+Up",
	"Alt+Down", "Ctrl+Shift+I", "Ctrl+E", "F", "Home", "Num7", "Ctrl+Num7", "Num1", "Ctrl+Num1",
	"Num3", "Ctrl+Num3", "Num5", "Num4", "Num6", "Num8", "Num2", "=", "-", "Ctrl+Shift+2",
	"Shift+G", "Shift+Z", "Q", "W", "E", "R", "T", "Y", "F5", "F7", "F10", "Shift+F5|F8", "F6",
	"Shift+F6", "Ctrl+Shift+P|F1", "Ctrl+P", "Ctrl+Alt+E", "B", "]", "[", "I", "K", "Alt+Right",
	"Alt+Left", "Ctrl+/", NULL
};

static void
test_app_ids_valid (void)
{
	guint i;

	for (i = 0; app_action_ids[i] != NULL; i++)
		g_assert_true (g_action_name_is_valid (app_action_ids[i]));
	for (i = 0; app_shortcuts[i] != NULL; i++)
	{
		g_autofree gchar *accel = lrg_editor_shortcut_to_accel (app_shortcuts[i]);

		if (accel == NULL)
			g_error ("default shortcut '%s' does not convert", app_shortcuts[i]);
	}
}

/* ==========================================================================
 * Accelerator conversion
 * ========================================================================== */

static void
test_accel_conversion_table (void)
{
	static const struct
	{
		const gchar *in;
		const gchar *out;   /* NULL = invalid */
	} cases[] = {
		/* Letters, modifiers, order */
		{ "Ctrl+S", "<Ctrl>s" },
		{ "ctrl+s", "<Ctrl>s" },
		{ "Ctrl+Shift+Z", "<Ctrl><Shift>z" },
		{ "Shift+Ctrl+Z", "<Ctrl><Shift>z" },
		{ "Ctrl+Alt+E", "<Ctrl><Alt>e" },
		{ "Control+Shift+Alt+Super+X", "<Ctrl><Alt><Shift><Super>x" },
		{ "Win+L", "<Super>l" },
		{ "Meta+X", "<Alt>x" },
		{ "control + del", "<Ctrl>Delete" },
		{ "H", "h" },
		/* Digits and keypad */
		{ "Ctrl+Shift+2", "<Ctrl><Shift>2" },
		{ "Num7", "KP_7" },
		{ "Ctrl+Num7", "<Ctrl>KP_7" },
		{ "Shift+num0", "<Shift>KP_0" },
		/* F-keys */
		{ "F1", "F1" },
		{ "F12", "F12" },
		{ "Shift+F5", "<Shift>F5" },
		{ "F13", NULL },
		{ "F0", NULL },
		/* Named keys */
		{ "PageUp", "Page_Up" },
		{ "Alt+PageDown", "<Alt>Page_Down" },
		{ "Backspace", "BackSpace" },
		{ "Enter", "Return" },
		{ "Esc", "Escape" },
		{ "Space", "space" },
		{ "Alt+Up", "<Alt>Up" },
		{ "Insert", "Insert" },
		/* Punctuation */
		{ "Ctrl+,", "<Ctrl>comma" },
		{ "Ctrl+/", "<Ctrl>slash" },
		{ "=", "equal" },
		{ "-", "minus" },
		{ "Ctrl++", "<Ctrl>plus" },
		{ "Ctrl+Plus", "<Ctrl>plus" },
		{ "[", "bracketleft" },
		{ "]", "bracketright" },
		{ "Ctrl+;", "<Ctrl>semicolon" },
		{ "`", "grave" },
		{ "Ctrl+\\", "<Ctrl>backslash" },
		/* Alternatives */
		{ "Ctrl+Shift+Z|Ctrl+Y", "<Ctrl><Shift>z|<Ctrl>y" },
		{ "Shift+F5|F8", "<Shift>F5|F8" },
		{ "Ctrl+Shift+P | F1", "<Ctrl><Shift>p|F1" },
		/* Accelerator syntax passes through unchanged */
		{ "<Ctrl><Shift>z", "<Ctrl><Shift>z" },
		{ "<Control>S", "<Control>S" },
		{ "KP_7", "KP_7" },
		{ "<Ctrl>comma", "<Ctrl>comma" },
		{ "<Alt>Page_Up|<Ctrl>y", "<Alt>Page_Up|<Ctrl>y" },
		{ "Ctrl+comma", "<Ctrl>comma" },
		/* Mixed syntaxes across alternatives */
		{ "<Ctrl>y|Ctrl+Shift+Z", "<Ctrl>y|<Ctrl><Shift>z" },
		/* Invalid */
		{ "", NULL },
		{ "Ctrl+", NULL },
		{ "Hyper+X", NULL },
		{ "A+B", NULL },
		{ "Ctrl+Shift+", NULL },
		{ "<Hyper>x", NULL },
		{ "<Ctrl>", NULL },
		{ "Ctrl+S|", NULL },
		{ "NotAKey", NULL },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autofree gchar *got = lrg_editor_shortcut_to_accel (cases[i].in);

		if (g_strcmp0 (got, cases[i].out) != 0)
			g_error ("'%s' converted to '%s', expected '%s'", cases[i].in,
			         got != NULL ? got : "(null)", cases[i].out != NULL ? cases[i].out : "(null)");

		/* Every result is a valid accelerator, and converting it again is
		 * the identity (accelerator syntax passes through). */
		if (got != NULL)
		{
			g_auto(GStrv)     alts = g_strsplit (got, "|", -1);
			g_autofree gchar *again = lrg_editor_shortcut_to_accel (got);
			guint             j;

			for (j = 0; alts[j] != NULL; j++)
				g_assert_true (lrg_accel_parse (alts[j], NULL, NULL));
			g_assert_cmpstr (again, ==, got);
		}
	}
	g_assert_cmpuint (G_N_ELEMENTS (cases), >=, 30);
	g_assert_null (lrg_editor_shortcut_to_accel (NULL));
}

static void
test_accel_label (void)
{
	g_autofree gchar *a = lrg_editor_accel_to_label ("<Ctrl><Shift>z|<Ctrl>y");
	g_autofree gchar *b = lrg_editor_accel_to_label ("Num7");
	g_autofree gchar *c = lrg_editor_accel_to_label ("<Ctrl>comma");
	g_autofree gchar *d = lrg_editor_accel_to_label ("bogus|F5");

	g_assert_cmpstr (a, ==, "Ctrl+Shift+Z, Ctrl+Y");
	g_assert_cmpstr (b, ==, "Num 7");
	g_assert_cmpstr (c, ==, "Ctrl+,");
	g_assert_cmpstr (d, ==, "F5");
	g_assert_null (lrg_editor_accel_to_label ("bogus"));
	g_assert_null (lrg_editor_accel_to_label (""));
}

/* ==========================================================================
 * Accel overrides
 * ========================================================================== */

static void
test_accels_set_get (void)
{
	State                        s = { 0, TRUE, FALSE };
	g_autoptr(LrgEditorActions)  actions = make_registry (NULL, &s);
	g_auto(GStrv)                acc = NULL;
	const gchar                 *two[] = { "Ctrl+Alt+S", "<Ctrl>F12", NULL };
	const gchar                 *none[] = { NULL };
	const gchar                 *bad[] = { "<Ctrl>s", "Hyper+Q", NULL };
	Recorder                     r;

	recorder_connect (&r, actions);

	/* Defaults, converted from either syntax */
	acc = lrg_editor_actions_get_accels (actions, "file.save");
	g_assert_cmpuint (g_strv_length (acc), ==, 1);
	g_assert_cmpstr (acc[0], ==, "<Ctrl>s");
	g_clear_pointer (&acc, g_strfreev);
	acc = lrg_editor_actions_get_default_accels (actions, "edit.select-all");
	g_assert_cmpstr (acc[0], ==, "<Ctrl>a");
	g_clear_pointer (&acc, g_strfreev);
	g_assert_null (lrg_editor_actions_get_accels (actions, "nope"));
	g_assert_null (lrg_editor_actions_get_default_accels (actions, "nope"));

	/* Override with two alternatives in mixed syntax */
	g_assert_true (lrg_editor_actions_set_accels (actions, "file.save", two));
	g_assert_true (recorder_has (&r, "accels:file.save"));
	acc = lrg_editor_actions_get_accels (actions, "file.save");
	g_assert_cmpuint (g_strv_length (acc), ==, 2);
	g_assert_cmpstr (acc[0], ==, "<Ctrl><Alt>s");
	g_assert_cmpstr (acc[1], ==, "<Ctrl>F12");
	g_clear_pointer (&acc, g_strfreev);
	acc = lrg_editor_actions_get_default_accels (actions, "file.save");
	g_assert_cmpstr (acc[0], ==, "<Ctrl>s");
	g_clear_pointer (&acc, g_strfreev);

	/* Matching follows the override */
	g_assert_cmpstr (lrg_editor_actions_find_by_accel (actions, GRL_KEY_S,
	                                                   LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_ALT),
	                 ==, "file.save");
	g_assert_cmpstr (lrg_editor_actions_find_by_accel (actions, GRL_KEY_F12, LRG_INPUT_MODIFIER_CTRL),
	                 ==, "file.save");
	g_assert_null (lrg_editor_actions_find_by_accel (actions, GRL_KEY_S, LRG_INPUT_MODIFIER_CTRL));

	/* Setting the same value again emits nothing */
	g_ptr_array_set_size (r.events, 0);
	g_assert_true (lrg_editor_actions_set_accels (actions, "file.save", two));
	g_assert_cmpuint (r.events->len, ==, 0);

	/* An invalid element rejects the whole call */
	g_assert_false (lrg_editor_actions_set_accels (actions, "file.save", bad));
	g_assert_cmpuint (r.events->len, ==, 0);
	/* Unknown id without settings */
	g_assert_false (lrg_editor_actions_set_accels (actions, "nope.nope", two));

	/* Empty array unbinds; NULL restores the default */
	g_assert_true (lrg_editor_actions_set_accels (actions, "file.save", none));
	acc = lrg_editor_actions_get_accels (actions, "file.save");
	g_assert_nonnull (acc);
	g_assert_cmpuint (g_strv_length (acc), ==, 0);
	g_clear_pointer (&acc, g_strfreev);
	g_assert_true (lrg_editor_actions_set_accels (actions, "file.save", NULL));
	acc = lrg_editor_actions_get_accels (actions, "file.save");
	g_assert_cmpstr (acc[0], ==, "<Ctrl>s");
	g_clear_pointer (&acc, g_strfreev);

	/* Disabled actions do not claim their chord; modifiers must match */
	s.can_act = FALSE;
	g_assert_null (lrg_editor_actions_find_by_accel (actions, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL));
	s.can_act = TRUE;
	g_assert_cmpstr (lrg_editor_actions_find_by_accel (actions, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL),
	                 ==, "edit.select-all");
	g_assert_null (lrg_editor_actions_find_by_accel (actions, GRL_KEY_A,
	                                                 LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT));
	g_assert_cmpstr (lrg_editor_actions_find_by_accel (actions, GRL_KEY_G, LRG_INPUT_MODIFIER_SHIFT),
	                 ==, "view.grid");

	g_ptr_array_unref (r.events);
}

static void
test_accels_with_settings (void)
{
	State                         s = { 0, TRUE, FALSE };
	g_autoptr(LrgEditorSettings)  settings = lrg_editor_settings_new ();
	g_autoptr(LrgEditorActions)   actions = make_registry (settings, &s);
	g_auto(GStrv)                 acc = NULL;
	g_autoptr(GPtrArray)          conflicts = NULL;
	g_autofree gchar             *stored = NULL;
	const gchar                  *one[] = { "Ctrl+Shift+2", NULL };
	Recorder                      r;

	recorder_connect (&r, actions);

	/* set_accels writes the settings as accelerators */
	g_assert_true (lrg_editor_actions_set_accels (actions, "view.grid", one));
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (settings, "view.grid"), ==, "<Ctrl><Shift>2");
	g_assert_true (recorder_has (&r, "accels:view.grid"));

	/* With settings, an override may name an action not registered yet */
	g_assert_true (lrg_editor_actions_set_accels (actions, "later.action", one));
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (settings, "later.action"), ==,
	                 "<Ctrl><Shift>2");

	/* A settings change in the old syntax converts and signals */
	g_ptr_array_set_size (r.events, 0);
	lrg_editor_settings_set_keybinding (settings, "file.save", "Ctrl+Alt+S|Num5");
	g_assert_true (recorder_has (&r, "accels:file.save"));
	g_assert_false (recorder_has (&r, "accels:edit.select-all"));
	acc = lrg_editor_actions_get_accels (actions, "file.save");
	g_assert_cmpstr (acc[0], ==, "<Ctrl><Alt>s");
	g_assert_cmpstr (acc[1], ==, "KP_5");
	g_clear_pointer (&acc, g_strfreev);
	stored = lrg_editor_settings_get_keybinding_accel (settings, "file.save");
	g_assert_cmpstr (stored, ==, "<Ctrl><Alt>s|KP_5");

	/* "" unbinds */
	lrg_editor_settings_set_keybinding (settings, "file.save", "");
	acc = lrg_editor_actions_get_accels (actions, "file.save");
	g_assert_cmpuint (g_strv_length (acc), ==, 0);
	g_clear_pointer (&acc, g_strfreev);

	/* Conflicts are detected across syntaxes */
	lrg_editor_settings_set_keybinding (settings, "file.save", "<Ctrl>a");
	conflicts = lrg_editor_actions_find_conflicts (actions);
	g_assert_cmpuint (conflicts->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (conflicts, 0), ==, "Ctrl+A: edit.select-all and file.save");

	g_ptr_array_unref (r.events);
}

/* ==========================================================================
 * Deprecated LrgGuiKey API
 * ========================================================================== */

G_GNUC_BEGIN_IGNORE_DEPRECATIONS

static void
test_legacy_api (void)
{
	State                         s = { 0, TRUE, FALSE };
	g_autoptr(LrgEditorSettings)  settings = lrg_editor_settings_new ();
	g_autoptr(LrgEditorActions)   actions = make_registry (settings, &s);
	LrgGuiModifiers               m;
	LrgGuiKey                     k;
	g_autofree gchar             *f = NULL;

	/* Parse / format still speak the 0.8.0 syntax */
	g_assert_true (lrg_editor_shortcut_parse ("Ctrl+Shift+Num7", &m, &k));
	g_assert_cmpint (m, ==, LRG_GUI_MOD_CTRL | LRG_GUI_MOD_SHIFT);
	g_assert_cmpint (k, ==, LRG_GUI_KEY_KP_7);
	f = lrg_editor_shortcut_format (m, k);
	g_assert_cmpstr (f, ==, "Ctrl+Shift+Num7");
	g_assert_false (lrg_editor_shortcut_parse ("<Ctrl>s", NULL, NULL));

	/* Lookup by LrgGuiKey matches bindings in both syntaxes */
	g_assert_cmpstr (lrg_editor_actions_find_by_shortcut (actions, LRG_GUI_MOD_CTRL, LRG_GUI_KEY_S),
	                 ==, "file.save");
	g_assert_cmpstr (lrg_editor_actions_find_by_shortcut (actions, LRG_GUI_MOD_CTRL, LRG_GUI_KEY_A),
	                 ==, "edit.select-all");
	g_assert_null (lrg_editor_actions_find_by_shortcut (actions, LRG_GUI_MOD_NONE, LRG_GUI_KEY_S));

	/* String getters return 0.8.0 text: as written, or converted */
	g_assert_cmpstr (lrg_editor_actions_get_shortcut (actions, "file.save"), ==, "Ctrl+S");
	g_assert_cmpstr (lrg_editor_actions_get_default_shortcut (actions, "edit.select-all"), ==, "Ctrl+A");
	g_assert_cmpstr (lrg_editor_actions_get_shortcut (actions, "edit.select-all"), ==, "Ctrl+A");

	lrg_editor_settings_set_keybinding (settings, "file.save", "<Ctrl><Shift>z|KP_7");
	g_assert_cmpstr (lrg_editor_actions_get_shortcut (actions, "file.save"), ==, "Ctrl+Shift+Z|Num7");
	g_assert_cmpstr (lrg_editor_actions_find_by_shortcut (actions, LRG_GUI_MOD_NONE, LRG_GUI_KEY_KP_7),
	                 ==, "file.save");
	lrg_editor_settings_set_keybinding (settings, "file.save", "Ctrl+Alt+S");
	g_assert_cmpstr (lrg_editor_actions_get_shortcut (actions, "file.save"), ==, "Ctrl+Alt+S");
	lrg_editor_settings_set_keybinding (settings, "file.save", "");
	g_assert_null (lrg_editor_actions_get_shortcut (actions, "file.save"));
	g_assert_null (lrg_editor_actions_get_shortcut (actions, "nope"));
}

G_GNUC_END_IGNORE_DEPRECATIONS

/* ==========================================================================
 * Settings
 * ========================================================================== */

static void
test_settings_new_keys_round_trip (void)
{
	g_autoptr(LrgEditorSettings) a = lrg_editor_settings_new ();
	g_autoptr(LrgEditorSettings) b = lrg_editor_settings_new ();
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *data = NULL;
	g_autofree gchar            *layout = NULL;

	/* Defaults */
	g_assert_cmpint (lrg_editor_settings_get_settings_version (a), ==, LRG_EDITOR_SETTINGS_VERSION);
	g_assert_cmpint (LRG_EDITOR_SETTINGS_VERSION, ==, 2);
	g_assert_cmpstr (lrg_editor_settings_get_script_editor (a), ==, "external");
	g_assert_cmpstr (lrg_editor_settings_get_viewport_layout (a), ==, "single");
	g_assert_null (lrg_editor_settings_get_ui_layout (a));
	g_assert_cmpstr (lrg_editor_settings_get_theme_preset (a), ==, "catppuccin-mocha");

	g_object_set (a,
	              "ui-layout", "root:\n  type: split\n",
	              "layout", "split(h,0.25,[hierarchy],[viewport])",
	              "script-editor", "emacs",
	              "viewport-layout", "quad",
	              "theme", "high-contrast",
	              NULL);
	lrg_editor_settings_set_keybinding (a, "file.save", "<Ctrl><Alt>s");
	lrg_editor_settings_set_keybinding (a, "edit.undo", "Ctrl+Z");

	data = lrg_editor_settings_to_data (a);
	g_assert_nonnull (strstr (data, "settings-version"));
	g_assert_true (lrg_editor_settings_load_from_data (b, data, &error));
	g_assert_no_error (error);

	g_assert_cmpint (lrg_editor_settings_get_settings_version (b), ==, 2);
	g_assert_cmpstr (lrg_editor_settings_get_ui_layout (b), ==, "root:\n  type: split\n");
	g_object_get (b, "layout", &layout, NULL);
	g_assert_cmpstr (layout, ==, "split(h,0.25,[hierarchy],[viewport])");
	g_assert_cmpstr (lrg_editor_settings_get_script_editor (b), ==, "emacs");
	g_assert_cmpstr (lrg_editor_settings_get_viewport_layout (b), ==, "quad");
	g_assert_cmpstr (lrg_editor_settings_get_theme_preset (b), ==, "high-contrast");
	g_assert_null (lrg_editor_settings_get_theme_file (b));
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (b, "file.save"), ==, "<Ctrl><Alt>s");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (b, "edit.undo"), ==, "Ctrl+Z");
}

static void
test_settings_legacy_file (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *data = NULL;
	g_autofree gchar            *kb = NULL;
	g_autofree gchar            *kb2 = NULL;
	g_autofree gchar            *kb3 = NULL;
	const gchar                 *legacy =
		"theme: latte\n"
		"layout: split(h,0.2,[hierarchy],[viewport])\n"
		"future-key:\n"
		"  nested: [1, 2]\n"
		"keybindings:\n"
		"  view.toggle-2d: Ctrl+Shift+2\n"
		"  view.top: Num7\n"
		"  edit.preferences: Ctrl+,\n"
		"  edit.delete: ''\n"
		"  tools.palette: Hyper+P\n";

	/* A 0.8.0 file: no version key -> version 1, still fully readable */
	g_assert_true (lrg_editor_settings_load_from_data (s, legacy, &error));
	g_assert_no_error (error);
	g_assert_cmpint (lrg_editor_settings_get_settings_version (s), ==, 1);
	g_assert_cmpstr (lrg_editor_settings_get_theme_preset (s), ==, "catppuccin-latte");

	kb = lrg_editor_settings_get_keybinding_accel (s, "view.toggle-2d");
	g_assert_cmpstr (kb, ==, "<Ctrl><Shift>2");
	kb2 = lrg_editor_settings_get_keybinding_accel (s, "edit.preferences");
	g_assert_cmpstr (kb2, ==, "<Ctrl>comma");
	kb3 = lrg_editor_settings_get_keybinding_accel (s, "edit.delete");
	g_assert_cmpstr (kb3, ==, "");
	g_assert_null (lrg_editor_settings_get_keybinding_accel (s, "tools.palette"));
	g_assert_null (lrg_editor_settings_get_keybinding_accel (s, "file.save"));
	/* Raw values are untouched (the migration rewrites them) */
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "view.top"), ==, "Num7");

	/* Unknown keys survive a save */
	data = lrg_editor_settings_to_data (s);
	g_assert_nonnull (strstr (data, "future-key"));
	g_assert_nonnull (strstr (data, "nested"));

	/* Bumping the version is what the migration does */
	lrg_editor_settings_set_settings_version (s, LRG_EDITOR_SETTINGS_VERSION);
	g_assert_cmpint (lrg_editor_settings_get_settings_version (s), ==, 2);

	/* An empty document means defaults, i.e. the current version */
	{
		g_autoptr(LrgEditorSettings) e = lrg_editor_settings_new ();

		g_assert_true (lrg_editor_settings_load_from_data (e, "", &error));
		g_assert_no_error (error);
		g_assert_cmpint (lrg_editor_settings_get_settings_version (e), ==, 2);
	}
}

static void
test_settings_theme_names (void)
{
	static const struct
	{
		const gchar *theme;
		const gchar *preset;
		const gchar *file;
	} cases[] = {
		{ "mocha", "catppuccin-mocha", NULL },
		{ "macchiato", "catppuccin-mocha", NULL },
		{ "frappe", "catppuccin-mocha", NULL },
		{ "latte", "catppuccin-latte", NULL },
		{ "midnight", "midnight", NULL },
		{ "daylight", "daylight", NULL },
		{ "catppuccin-mocha", "catppuccin-mocha", NULL },
		{ "catppuccin-latte", "catppuccin-latte", NULL },
		{ "high-contrast", "high-contrast", NULL },
		{ "Daylight", "daylight", NULL },
		{ "something-else", "catppuccin-mocha", NULL },
		{ "/home/me/themes/dusk.yaml", NULL, "/home/me/themes/dusk.yaml" },
		{ "dusk.yml", NULL, "dusk.yml" },
	};
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	const gchar * const         *presets = lrg_theme_list_presets ();
	guint                        i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_object_set (s, "theme", cases[i].theme, NULL);
		g_assert_cmpstr (lrg_editor_settings_get_theme_preset (s), ==, cases[i].preset);
		g_assert_cmpstr (lrg_editor_settings_get_theme_file (s), ==, cases[i].file);
		/* Every resolved preset is one the toolkit knows */
		if (cases[i].preset != NULL)
			g_assert_true (g_strv_contains (presets, cases[i].preset));
	}
}

static void
test_settings_invalid_choices (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(GError)            error = NULL;

	/* Bad values are ignored (with a message) and the default stays */
	g_assert_true (lrg_editor_settings_load_from_data (s,
	                                                   "settings-version: 2\n"
	                                                   "script-editor: vscode\n"
	                                                   "viewport-layout: hex\n",
	                                                   &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_settings_get_script_editor (s), ==, "external");
	g_assert_cmpstr (lrg_editor_settings_get_viewport_layout (s), ==, "single");

	g_object_set (s, "viewport-layout", "split-v", "script-editor", "internal", NULL);
	g_assert_cmpstr (lrg_editor_settings_get_viewport_layout (s), ==, "split-v");
	g_assert_cmpstr (lrg_editor_settings_get_script_editor (s), ==, "internal");

	/* reset() goes back to the current version and defaults */
	lrg_editor_settings_set_settings_version (s, 1);
	lrg_editor_settings_reset (s);
	g_assert_cmpint (lrg_editor_settings_get_settings_version (s), ==, 2);
	g_assert_cmpstr (lrg_editor_settings_get_viewport_layout (s), ==, "single");
}

static void
count_notify (GObject    *obj,
              GParamSpec *pspec,
              gpointer    data)
{
	(*(gint *) data)++;
}

static void
test_settings_explicit_notify (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	gint                         n = 0;

	g_signal_connect (s, "notify::viewport-layout", G_CALLBACK (count_notify), &n);
	g_object_set (s, "viewport-layout", "single", NULL);
	g_assert_cmpint (n, ==, 0);
	g_object_set (s, "viewport-layout", "quad", NULL);
	g_assert_cmpint (n, ==, 1);
	g_object_set (s, "viewport-layout", "quad", NULL);
	g_assert_cmpint (n, ==, 1);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor-actions/group/list-and-query", test_group_list_and_query);
	g_test_add_func ("/editor-actions/group/activate", test_group_activate);
	g_test_add_func ("/editor-actions/group/refresh-signals", test_group_refresh_signals);
	g_test_add_func ("/editor-actions/group/add-replace", test_group_add_replace);
	g_test_add_func ("/editor-actions/app-ids-valid", test_app_ids_valid);
	g_test_add_func ("/editor-actions/accel/conversion-table", test_accel_conversion_table);
	g_test_add_func ("/editor-actions/accel/label", test_accel_label);
	g_test_add_func ("/editor-actions/accels/set-get", test_accels_set_get);
	g_test_add_func ("/editor-actions/accels/with-settings", test_accels_with_settings);
	g_test_add_func ("/editor-actions/legacy-api", test_legacy_api);
	g_test_add_func ("/editor-actions/settings/new-keys-round-trip", test_settings_new_keys_round_trip);
	g_test_add_func ("/editor-actions/settings/legacy-file", test_settings_legacy_file);
	g_test_add_func ("/editor-actions/settings/theme-names", test_settings_theme_names);
	g_test_add_func ("/editor-actions/settings/invalid-choices", test_settings_invalid_choices);
	g_test_add_func ("/editor-actions/settings/explicit-notify", test_settings_explicit_notify);

	return g_test_run ();
}
