/* test-ui-actions.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Widget-scoped GIO actions, the built-in "app" / "ui" groups, canvas
 * keyboard shortcuts and accelerator parsing, right-to-left layout
 * mirroring, and the YAML loader's action handlers, `shortcuts:` and
 * `direction:`. Input goes through lrg_canvas_process_input() exactly
 * like the real frame loop; everything is headless.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* A recorder action: counts activations and keeps the last parameter */
typedef struct
{
	gint      count;
	GVariant *last;
} Recorder;

static void
recorder_clear (Recorder *r)
{
	g_clear_pointer (&r->last, g_variant_unref);
	r->count = 0;
}

static void
on_record (GSimpleAction *action,
           GVariant      *parameter,
           gpointer       user_data)
{
	Recorder *r = user_data;

	r->count++;
	g_clear_pointer (&r->last, g_variant_unref);
	if (parameter != NULL)
		r->last = g_variant_ref (parameter);
}

/* add_recorder: a GSimpleAction @name on @group, typed @ptype (nullable) */
static GSimpleAction *
add_recorder (GSimpleActionGroup *group,
              const gchar        *name,
              const gchar        *ptype,
              Recorder           *r)
{
	g_autoptr(GVariantType) vt = ptype != NULL ? g_variant_type_new (ptype) : NULL;
	GSimpleAction          *a = g_simple_action_new (name, vt);

	g_signal_connect (a, "activate", G_CALLBACK (on_record), r);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (a));
	g_object_unref (a);
	return a;
}

/* key_frame: one frame pressing @key with @mods and typing @ch (0: none) */
static void
press_key (LrgCanvas         *canvas,
           GrlKey             key,
           LrgInputModifiers  mods,
           gunichar           ch)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = -1.0f;
	input.pointer_y = -1.0f;
	input.delta_time = 1.0f / 60.0f;
	input.keys[0] = key;
	input.n_keys = 1;
	input.modifiers = mods;
	if (ch != 0)
	{
		input.chars[0] = ch;
		input.n_chars = 1;
	}
	lrg_canvas_process_input (canvas, &input);
}

/* fixed: a plain widget whose preferred size is @w x @h */
static LrgWidget *
fixed (gfloat w,
       gfloat h)
{
	LrgWidget *widget = g_object_new (LRG_TYPE_WIDGET, NULL);

	lrg_widget_set_min_size (widget, w, h);
	return widget;
}

static LrgCanvas *
new_canvas (void)
{
	LrgCanvas *canvas = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), 800.0f, 600.0f);
	return canvas;
}

/* ==========================================================================
 * Action scoping
 * ========================================================================== */

static void
test_actions_scope (void)
{
	g_autoptr(LrgCanvas)          canvas = new_canvas ();
	g_autoptr(GSimpleActionGroup) outer = g_simple_action_group_new ();
	g_autoptr(GSimpleActionGroup) inner = g_simple_action_group_new ();
	g_auto(GStrv)                 prefixes = NULL;
	Recorder                      ro = { 0 };
	Recorder                      ri = { 0 };
	LrgVBox                      *box = lrg_vbox_new ();
	LrgWidget                    *leaf = fixed (10.0f, 10.0f);

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (box));
	lrg_container_add_child (LRG_CONTAINER (box), leaf);

	/* Built-ins are on every canvas */
	g_assert_nonnull (lrg_widget_get_action_group (LRG_WIDGET (canvas), "app"));
	g_assert_nonnull (lrg_widget_lookup_action (leaf, "ui.show"));
	g_assert_nonnull (lrg_widget_lookup_action (leaf, "app.quit"));

	/* An ancestor's group is reachable from below */
	add_recorder (outer, "a", NULL, &ro);
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "game", G_ACTION_GROUP (outer));
	g_assert_nonnull (lrg_widget_lookup_action (leaf, "game.a"));
	g_assert_true (lrg_widget_activate_action (leaf, "game.a", NULL));
	g_assert_cmpint (ro.count, ==, 1);

	/* A nearer group with the same prefix shadows it entirely */
	add_recorder (inner, "b", NULL, &ri);
	lrg_widget_insert_action_group (LRG_WIDGET (box), "game", G_ACTION_GROUP (inner));
	g_assert_null (lrg_widget_lookup_action (leaf, "game.a"));
	g_assert_nonnull (lrg_widget_lookup_action (leaf, "game.b"));
	g_assert_false (lrg_widget_activate_action (leaf, "game.a", NULL));
	g_assert_true (lrg_widget_activate_action (leaf, "game.b", NULL));
	g_assert_cmpint (ri.count, ==, 1);
	g_assert_true (lrg_widget_find_action_group (leaf, "game") == G_ACTION_GROUP (inner));

	/* ... but not for the canvas itself, which is above it */
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (canvas), "game.a", NULL));
	g_assert_cmpint (ro.count, ==, 2);

	/* Prefix listing: nearest first, no duplicates */
	prefixes = lrg_widget_list_action_prefixes (leaf);
	g_assert_cmpstr (prefixes[0], ==, "game");
	g_assert_true (g_strv_contains ((const gchar * const *) prefixes, "app"));
	g_assert_true (g_strv_contains ((const gchar * const *) prefixes, "ui"));
	g_assert_cmpuint (g_strv_length (prefixes), ==, 3);

	/* Removing the inner group uncovers the outer one again */
	lrg_widget_insert_action_group (LRG_WIDGET (box), "game", NULL);
	g_assert_null (lrg_widget_get_action_group (LRG_WIDGET (box), "game"));
	g_assert_true (lrg_widget_activate_action (leaf, "game.a", NULL));
	g_assert_cmpint (ro.count, ==, 3);

	/* Malformed and unknown names */
	g_assert_false (lrg_widget_activate_action (leaf, "game", NULL));
	g_assert_false (lrg_widget_activate_action (leaf, ".a", NULL));
	g_assert_false (lrg_widget_activate_action (leaf, "nope.a", NULL));
	g_assert_false (lrg_widget_activate_action (leaf, "game.zzz", NULL));
	g_assert_null (lrg_widget_lookup_action (leaf, "game"));

	recorder_clear (&ro);
	recorder_clear (&ri);
}

/* A popup resolves through its anchor */
static void
test_actions_popup_scope (void)
{
	g_autoptr(LrgCanvas)          canvas = new_canvas ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	g_autoptr(LrgWidget)          popup = fixed (40.0f, 40.0f);
	Recorder                      r = { 0 };
	LrgVBox                      *box = lrg_vbox_new ();
	LrgWidget                    *anchor = fixed (20.0f, 20.0f);

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (box));
	lrg_container_add_child (LRG_CONTAINER (box), anchor);
	add_recorder (group, "go", NULL, &r);
	lrg_widget_insert_action_group (LRG_WIDGET (box), "panel", G_ACTION_GROUP (group));

	lrg_canvas_show_popup (canvas, popup, anchor, LRG_POPUP_PLACEMENT_BELOW, FALSE);
	g_assert_true (lrg_widget_activate_action (popup, "panel.go", NULL));
	g_assert_cmpint (r.count, ==, 1);
	g_assert_true (lrg_widget_activate_action (popup, "ui.popup-close", NULL));
	g_assert_cmpuint (lrg_canvas_get_popup_count (canvas), ==, 0);
	recorder_clear (&r);
}

/* ==========================================================================
 * Parameters and enabled state
 * ========================================================================== */

static void
test_actions_parameters (void)
{
	g_autoptr(LrgCanvas)          canvas = new_canvas ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	Recorder                      r = { 0 };
	GSimpleAction                *pick;

	pick = add_recorder (group, "pick", "s", &r);
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "game", G_ACTION_GROUP (group));

	/* The right type is delivered */
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (canvas), "game.pick", g_variant_new_string ("sword")));
	g_assert_cmpint (r.count, ==, 1);
	g_assert_cmpstr (g_variant_get_string (r.last, NULL), ==, "sword");

	/* Missing or mistyped parameters are refused (and freed) */
	g_assert_false (lrg_widget_activate_action (LRG_WIDGET (canvas), "game.pick", NULL));
	g_assert_false (lrg_widget_activate_action (LRG_WIDGET (canvas), "game.pick", g_variant_new_int32 (3)));
	g_assert_cmpint (r.count, ==, 1);

	/* Disabled actions do not run */
	g_simple_action_set_enabled (pick, FALSE);
	g_assert_false (lrg_widget_activate_action (LRG_WIDGET (canvas), "game.pick", g_variant_new_string ("x")));
	g_assert_cmpint (r.count, ==, 1);
	recorder_clear (&r);
}

/* ==========================================================================
 * Built-in groups
 * ========================================================================== */

/* A minimal stack with a "visible-child-name" property for ui.page */
#define TEST_TYPE_STACK (test_stack_get_type ())
G_DECLARE_FINAL_TYPE (TestStack, test_stack, TEST, STACK, LrgContainer)

struct _TestStack
{
	LrgContainer  parent_instance;
	gchar        *page;
};

G_DEFINE_FINAL_TYPE (TestStack, test_stack, LRG_TYPE_CONTAINER)

static void
test_stack_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	TestStack *self = TEST_STACK (object);

	g_free (self->page);
	self->page = g_value_dup_string (value);
}

static void
test_stack_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	g_value_set_string (value, TEST_STACK (object)->page);
}

static void
test_stack_finalize (GObject *object)
{
	g_free (TEST_STACK (object)->page);
	G_OBJECT_CLASS (test_stack_parent_class)->finalize (object);
}

static void
test_stack_class_init (TestStackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = test_stack_set_property;
	object_class->get_property = test_stack_get_property;
	object_class->finalize = test_stack_finalize;
	g_object_class_install_property (object_class, 1,
	                                 g_param_spec_string ("visible-child-name", NULL, NULL, NULL,
	                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
test_stack_init (TestStack *self)
{
}

static void
on_quit (LrgCanvas *canvas,
         gpointer   user_data)
{
	(*(gint *) user_data)++;
}

static void
test_actions_builtins (void)
{
	g_autoptr(LrgCanvas)  canvas = new_canvas ();
	LrgVBox              *box = lrg_vbox_new ();
	LrgWidget            *details = fixed (10.0f, 10.0f);
	LrgButton            *button = lrg_button_new ("Go");
	LrgTabView           *tabs = lrg_tab_view_new ();
	TestStack            *stack = g_object_new (TEST_TYPE_STACK, NULL);
	LrgWidget            *w = LRG_WIDGET (canvas);
	gint                  quits = 0;

	lrg_widget_set_name (details, "details");
	lrg_widget_set_name (LRG_WIDGET (button), "go");
	lrg_widget_set_name (LRG_WIDGET (tabs), "tabs");
	lrg_widget_set_name (LRG_WIDGET (stack), "stack");
	lrg_tab_view_add_tab (tabs, "One", fixed (5.0f, 5.0f));
	lrg_tab_view_add_tab (tabs, "Two", fixed (5.0f, 5.0f));
	lrg_container_add_child (LRG_CONTAINER (box), details);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (button));
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (tabs));
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (stack));
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (box));

	/* show / hide / toggle by name */
	g_assert_true (lrg_widget_activate_action (w, "ui.hide", g_variant_new_string ("details")));
	g_assert_false (lrg_widget_get_visible (details));
	g_assert_true (lrg_widget_activate_action (w, "ui.show", g_variant_new_string ("details")));
	g_assert_true (lrg_widget_get_visible (details));
	g_assert_true (lrg_widget_activate_action (w, "ui.toggle", g_variant_new_string ("details")));
	g_assert_false (lrg_widget_get_visible (details));
	/* An unknown name is harmless */
	g_assert_true (lrg_widget_activate_action (w, "ui.toggle", g_variant_new_string ("nobody")));

	/* page: tab views by label or index, anything with visible-child-name */
	g_assert_true (lrg_widget_activate_action (w, "ui.page", g_variant_new_string ("tabs:Two")));
	g_assert_cmpuint (lrg_tab_view_get_active_tab (tabs), ==, 1);
	lrg_widget_activate_action (w, "ui.page", g_variant_new_string ("tabs:0"));
	g_assert_cmpuint (lrg_tab_view_get_active_tab (tabs), ==, 0);
	lrg_widget_activate_action (w, "ui.page", g_variant_new_string ("stack:settings"));
	g_assert_cmpstr (stack->page, ==, "settings");

	/* theme and motion */
	lrg_widget_activate_action (w, "ui.theme", g_variant_new_string ("daylight"));
	g_assert_cmpstr (lrg_theme_get_name (lrg_widget_get_theme (LRG_WIDGET (button))), ==, "daylight");
	lrg_widget_activate_action (w, "ui.motion", g_variant_new_string ("bouncy"));
	g_assert_cmpint (lrg_widget_get_motion (LRG_WIDGET (button)), ==, LRG_MOTION_BOUNCY);
	g_assert_cmpstr (lrg_theme_get_name (lrg_widget_get_theme (LRG_WIDGET (button))), ==, "daylight");

	/* focus shows the ring */
	lrg_widget_activate_action (w, "ui.focus", g_variant_new_string ("go"));
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (button));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (button)) & LRG_WIDGET_STATE_FOCUS_VISIBLE);

	/* direction */
	lrg_widget_activate_action (w, "ui.direction", g_variant_new_string ("rtl"));
	g_assert_cmpint (lrg_canvas_get_default_direction (canvas), ==, LRG_WIDGET_DIRECTION_RTL);

	/* app.quit only asks */
	g_signal_connect (canvas, "quit-requested", G_CALLBACK (on_quit), &quits);
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (button), "app.quit", NULL));
	g_assert_cmpint (quits, ==, 1);
}

static void
test_actions_toast (void)
{
	g_autoptr(LrgCanvas) canvas = new_canvas ();
	LrgToastQueue       *queue = lrg_toast_queue_new ();

	lrg_canvas_add_overlay (canvas, LRG_WIDGET (queue));
	g_object_unref (queue);
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (canvas), "ui.toast", g_variant_new_string ("Saved")));
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (queue) + lrg_toast_queue_get_n_pending (queue), ==, 1);
}

/* ==========================================================================
 * Accelerators
 * ========================================================================== */

static void
test_accel_parse (void)
{
	GrlKey            key = GRL_KEY_NULL;
	LrgInputModifiers mods = LRG_INPUT_MODIFIER_NONE;
	g_autofree gchar *l1 = NULL;
	g_autofree gchar *l2 = NULL;
	g_autofree gchar *l3 = NULL;
	g_autofree gchar *l4 = NULL;

	g_assert_true (lrg_accel_parse ("<Ctrl><Shift>s", &key, &mods));
	g_assert_cmpint (key, ==, GRL_KEY_S);
	g_assert_cmpint (mods, ==, LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT);

	g_assert_true (lrg_accel_parse ("<Primary>Q", &key, &mods));
	g_assert_cmpint (key, ==, GRL_KEY_Q);
	g_assert_cmpint (mods, ==, LRG_INPUT_MODIFIER_CTRL);

	g_assert_true (lrg_accel_parse ("F5", &key, &mods));
	g_assert_cmpint (key, ==, GRL_KEY_F5);
	g_assert_cmpint (mods, ==, LRG_INPUT_MODIFIER_NONE);

	g_assert_true (lrg_accel_parse ("<alt><SUPER>page_up", &key, &mods));
	g_assert_cmpint (key, ==, GRL_KEY_PAGE_UP);
	g_assert_cmpint (mods, ==, LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SUPER);

	g_assert_true (lrg_accel_parse ("<Ctrl>+", &key, NULL));
	g_assert_cmpint (key, ==, GRL_KEY_EQUAL);
	g_assert_true (lrg_accel_parse ("KP_7", &key, NULL));
	g_assert_cmpint (key, ==, GRL_KEY_KP_7);

	l1 = lrg_accel_to_label ("<Ctrl><Shift>s");
	l2 = lrg_accel_to_label ("<Alt>Page_Up");
	l3 = lrg_accel_to_label ("<Ctrl>plus");
	l4 = lrg_accel_to_label ("Escape");
	g_assert_cmpstr (l1, ==, "Ctrl+Shift+S");
	g_assert_cmpstr (l2, ==, "Alt+PgUp");
	g_assert_cmpstr (l3, ==, "Ctrl++");
	g_assert_cmpstr (l4, ==, "Esc");
}

static void
test_accel_bad (void)
{
	static const gchar *bad[] = {
		"", "<Ctrl>", "<Hyper>a", "<Ctrl><Ctrl>a", "abc", "F13", "F0", "<Ctrl", "Ctrl>a", "<Ctrl>a b"
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (bad); i++)
	{
		GrlKey key = GRL_KEY_A;

		g_assert_false (lrg_accel_parse (bad[i], &key, NULL));
		g_assert_cmpint (key, ==, GRL_KEY_A);   /* untouched */
		g_assert_null (lrg_accel_to_label (bad[i]));
	}
}

/* lrg_accel_name and lrg_accel_parse invert each other */
static void
test_accel_round_trip (void)
{
	static const GrlKey keys[] = {
		GRL_KEY_A, GRL_KEY_Z, GRL_KEY_ZERO, GRL_KEY_NINE, GRL_KEY_F1, GRL_KEY_F12, GRL_KEY_ESCAPE,
		GRL_KEY_ENTER, GRL_KEY_TAB, GRL_KEY_BACKSPACE, GRL_KEY_DELETE, GRL_KEY_INSERT, GRL_KEY_HOME,
		GRL_KEY_END, GRL_KEY_PAGE_UP, GRL_KEY_PAGE_DOWN, GRL_KEY_UP, GRL_KEY_DOWN, GRL_KEY_LEFT,
		GRL_KEY_RIGHT, GRL_KEY_SPACE, GRL_KEY_MINUS, GRL_KEY_EQUAL, GRL_KEY_COMMA, GRL_KEY_SLASH,
		GRL_KEY_LEFT_BRACKET, GRL_KEY_KP_0, GRL_KEY_KP_ADD, GRL_KEY_KP_ENTER
	};
	static const LrgInputModifiers mods[] = {
		LRG_INPUT_MODIFIER_NONE, LRG_INPUT_MODIFIER_CTRL,
		LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SUPER
	};
	guint i;
	guint m;

	for (i = 0; i < G_N_ELEMENTS (keys); i++)
		for (m = 0; m < G_N_ELEMENTS (mods); m++)
		{
			g_autofree gchar *name = lrg_accel_name (keys[i], mods[m]);
			g_autofree gchar *label = NULL;
			GrlKey            k;
			LrgInputModifiers md;

			g_assert_nonnull (name);
			g_assert_true (lrg_accel_parse (name, &k, &md));
			g_assert_cmpint (k, ==, keys[i]);
			g_assert_cmpint (md, ==, mods[m]);
			label = lrg_accel_to_label (name);
			g_assert_nonnull (label);
		}

	g_assert_null (lrg_accel_name (GRL_KEY_NULL, LRG_INPUT_MODIFIER_NONE));
}

/* ==========================================================================
 * Shortcuts
 * ========================================================================== */

static void
test_shortcuts_fire (void)
{
	g_autoptr(LrgCanvas)          canvas = new_canvas ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	g_autofree gchar             *accel = NULL;
	Recorder                      save = { 0 };
	Recorder                      pick = { 0 };

	add_recorder (group, "save", NULL, &save);
	add_recorder (group, "pick", "s", &pick);
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "doc", G_ACTION_GROUP (group));

	g_assert_true (lrg_canvas_add_shortcut (canvas, "<Ctrl>s", "doc.save", NULL));
	g_assert_true (lrg_canvas_add_shortcut (canvas, "F2", "doc.pick", g_variant_new_string ("two")));
	g_assert_cmpuint (lrg_canvas_get_n_shortcuts (canvas), ==, 2);

	/* Modifiers must match exactly */
	press_key (canvas, GRL_KEY_S, LRG_INPUT_MODIFIER_NONE, 0);
	press_key (canvas, GRL_KEY_S, LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT, 0);
	g_assert_cmpint (save.count, ==, 0);
	press_key (canvas, GRL_KEY_S, LRG_INPUT_MODIFIER_CTRL, 0);
	g_assert_cmpint (save.count, ==, 1);

	/* Targets are delivered */
	press_key (canvas, GRL_KEY_F2, LRG_INPUT_MODIFIER_NONE, 0);
	g_assert_cmpint (pick.count, ==, 1);
	g_assert_cmpstr (g_variant_get_string (pick.last, NULL), ==, "two");

	/* Lookup for menus */
	accel = lrg_canvas_get_accel_for_action (canvas, "doc.pick", g_variant_new_string ("two"));
	g_assert_cmpstr (accel, ==, "F2");
	g_assert_null (lrg_canvas_get_accel_for_action (canvas, "doc.pick", NULL));
	g_assert_null (lrg_canvas_get_accel_for_action (canvas, "doc.pick", g_variant_new_string ("one")));

	/* Rebinding replaces; another spelling of the same keys removes */
	g_assert_true (lrg_canvas_add_shortcut (canvas, "<Primary>S", "doc.pick", g_variant_new_string ("s")));
	g_assert_cmpuint (lrg_canvas_get_n_shortcuts (canvas), ==, 2);
	press_key (canvas, GRL_KEY_S, LRG_INPUT_MODIFIER_CTRL, 0);
	g_assert_cmpint (save.count, ==, 1);
	g_assert_cmpint (pick.count, ==, 2);
	g_assert_true (lrg_canvas_remove_shortcut (canvas, "<Control>s"));
	g_assert_false (lrg_canvas_remove_shortcut (canvas, "<Control>s"));
	press_key (canvas, GRL_KEY_S, LRG_INPUT_MODIFIER_CTRL, 0);
	g_assert_cmpint (pick.count, ==, 2);

	/* A bad accelerator is refused with a warning */
	g_test_expect_message ("Libregnum-UI", G_LOG_LEVEL_WARNING, "*cannot parse accelerator*");
	g_assert_false (lrg_canvas_add_shortcut (canvas, "<Ctrl>", "doc.save", g_variant_new_string ("x")));
	g_test_assert_expected_messages ();

	lrg_canvas_remove_all_shortcuts (canvas);
	g_assert_cmpuint (lrg_canvas_get_n_shortcuts (canvas), ==, 0);
	recorder_clear (&save);
	recorder_clear (&pick);
}

/* Escape bound to an action replaces the canvas "cancel" */
static void
on_cancel (LrgCanvas *canvas,
           gpointer   user_data)
{
	(*(gint *) user_data)++;
}

static void
test_shortcuts_before_defaults (void)
{
	g_autoptr(LrgCanvas)          canvas = new_canvas ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	Recorder                      back = { 0 };
	gint                          cancels = 0;

	add_recorder (group, "back", NULL, &back);
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "game", G_ACTION_GROUP (group));
	g_signal_connect (canvas, "cancel", G_CALLBACK (on_cancel), &cancels);

	press_key (canvas, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE, 0);
	g_assert_cmpint (cancels, ==, 1);

	lrg_canvas_add_shortcut (canvas, "Escape", "game.back", NULL);
	press_key (canvas, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE, 0);
	g_assert_cmpint (back.count, ==, 1);
	g_assert_cmpint (cancels, ==, 1);

	/* A shortcut whose action is missing falls through to the default */
	lrg_canvas_add_shortcut (canvas, "Escape", "game.missing", NULL);
	press_key (canvas, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE, 0);
	g_assert_cmpint (cancels, ==, 2);
	recorder_clear (&back);
}

/* Widget-scoped groups apply to shortcuts fired while focus is inside */
static void
test_shortcuts_focus_scope (void)
{
	g_autoptr(LrgCanvas)          canvas = new_canvas ();
	g_autoptr(GSimpleActionGroup) outer = g_simple_action_group_new ();
	g_autoptr(GSimpleActionGroup) inner = g_simple_action_group_new ();
	Recorder                      ro = { 0 };
	Recorder                      ri = { 0 };
	LrgVBox                      *box = lrg_vbox_new ();
	LrgButton                    *button = lrg_button_new ("x");

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (box));
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (button));
	add_recorder (outer, "del", NULL, &ro);
	add_recorder (inner, "del", NULL, &ri);
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "edit", G_ACTION_GROUP (outer));
	lrg_widget_insert_action_group (LRG_WIDGET (box), "edit", G_ACTION_GROUP (inner));
	lrg_canvas_add_shortcut (canvas, "Delete", "edit.del", NULL);

	press_key (canvas, GRL_KEY_DELETE, LRG_INPUT_MODIFIER_NONE, 0);
	g_assert_cmpint (ro.count, ==, 1);

	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (button));
	press_key (canvas, GRL_KEY_DELETE, LRG_INPUT_MODIFIER_NONE, 0);
	g_assert_cmpint (ri.count, ==, 1);
	g_assert_cmpint (ro.count, ==, 1);
	recorder_clear (&ro);
	recorder_clear (&ri);
}

/* A focused text field keeps the keys it uses */
static void
test_shortcuts_text_input (void)
{
	g_autoptr(LrgCanvas)          canvas = new_canvas ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	Recorder                      plain = { 0 };
	Recorder                      quit = { 0 };
	Recorder                      all = { 0 };
	LrgTextInput                 *entry = lrg_text_input_new ();

	lrg_widget_set_size (LRG_WIDGET (entry), 200.0f, 30.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (entry));
	add_recorder (group, "plain", NULL, &plain);
	add_recorder (group, "quit", NULL, &quit);
	add_recorder (group, "all", NULL, &all);
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "t", G_ACTION_GROUP (group));
	lrg_canvas_add_shortcut (canvas, "q", "t.plain", NULL);
	lrg_canvas_add_shortcut (canvas, "<Ctrl>q", "t.quit", NULL);
	lrg_canvas_add_shortcut (canvas, "<Ctrl>a", "t.all", NULL);

	/* Nothing focused: a printable shortcut fires */
	press_key (canvas, GRL_KEY_Q, LRG_INPUT_MODIFIER_NONE, 'q');
	g_assert_cmpint (plain.count, ==, 1);

	/* Typing into the field: the character is text, not a shortcut */
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (entry));
	press_key (canvas, GRL_KEY_Q, LRG_INPUT_MODIFIER_NONE, 'q');
	g_assert_cmpint (plain.count, ==, 1);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "q");

	/* Ctrl+A is the field's select-all: it consumes it */
	press_key (canvas, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL, 0);
	g_assert_cmpint (all.count, ==, 0);

	/* Ctrl+Q means nothing to the field: the shortcut still fires */
	press_key (canvas, GRL_KEY_Q, LRG_INPUT_MODIFIER_CTRL, 'q');
	g_assert_cmpint (quit.count, ==, 1);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "q");

	recorder_clear (&plain);
	recorder_clear (&quit);
	recorder_clear (&all);
}

/* ==========================================================================
 * Right-to-left
 * ========================================================================== */

static void
test_rtl_hbox (void)
{
	g_autoptr(LrgCanvas) canvas = new_canvas ();
	LrgHBox             *box = lrg_hbox_new ();
	LrgWidget           *c[3];
	guint                i;

	lrg_container_set_spacing (LRG_CONTAINER (box), 0.0f);
	for (i = 0; i < 3; i++)
	{
		c[i] = fixed (50.0f, 20.0f);
		lrg_container_add_child (LRG_CONTAINER (box), c[i]);
	}
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (box));
	lrg_widget_allocate (LRG_WIDGET (box), 0.0f, 0.0f, 200.0f, 20.0f);

	g_assert_cmpfloat (lrg_widget_get_x (c[0]), ==, 0.0f);
	g_assert_cmpfloat (lrg_widget_get_x (c[2]), ==, 100.0f);

	/* The canvas default flips the box; the first child is rightmost */
	lrg_canvas_set_default_direction (canvas, LRG_WIDGET_DIRECTION_RTL);
	g_assert_true (lrg_widget_get_needs_layout (LRG_WIDGET (box)));
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_x (c[0]), ==, 150.0f);
	g_assert_cmpfloat (lrg_widget_get_x (c[1]), ==, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_x (c[2]), ==, 50.0f);
	g_assert_cmpfloat (lrg_widget_get_width (c[0]), ==, 50.0f);

	/* World coordinates follow */
	g_assert_true (lrg_canvas_widget_at_point (canvas, 175.0f, 10.0f) == c[0]);
}

static void
test_rtl_halign (void)
{
	g_autoptr(LrgCanvas) canvas = new_canvas ();
	LrgVBox             *box = lrg_vbox_new ();
	LrgWidget           *start = fixed (50.0f, 20.0f);
	LrgWidget           *end = fixed (50.0f, 20.0f);
	LrgWidget           *margin = fixed (50.0f, 20.0f);
	LrgInsets            m = { 10.0f, 0.0f, 0.0f, 0.0f };

	lrg_widget_set_halign (start, LRG_ALIGN_START);
	lrg_widget_set_halign (end, LRG_ALIGN_END);
	lrg_widget_set_halign (margin, LRG_ALIGN_START);
	lrg_widget_set_margin (margin, &m);
	lrg_container_add_child (LRG_CONTAINER (box), start);
	lrg_container_add_child (LRG_CONTAINER (box), end);
	lrg_container_add_child (LRG_CONTAINER (box), margin);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (box));
	lrg_widget_allocate (LRG_WIDGET (box), 0.0f, 0.0f, 200.0f, 100.0f);

	g_assert_cmpfloat (lrg_widget_get_x (start), ==, 0.0f);
	g_assert_cmpfloat (lrg_widget_get_x (end), ==, 150.0f);
	g_assert_cmpfloat (lrg_widget_get_x (margin), ==, 10.0f);

	/* START / END and the leading margin swap sides */
	lrg_widget_set_direction (LRG_WIDGET (box), LRG_WIDGET_DIRECTION_RTL);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_x (start), ==, 150.0f);
	g_assert_cmpfloat (lrg_widget_get_x (end), ==, 0.0f);
	g_assert_cmpfloat (lrg_widget_get_x (margin), ==, 140.0f);
}

static void
test_rtl_grid_and_wrap (void)
{
	g_autoptr(LrgCanvas) canvas = new_canvas ();
	LrgGrid             *grid = lrg_grid_new (2);
	LrgWrapBox          *wrap = lrg_wrap_box_new ();
	LrgWidget           *g[2];
	LrgWidget           *wr[3];
	guint                i;

	lrg_grid_set_column_spacing (grid, 0.0f);
	for (i = 0; i < 2; i++)
	{
		g[i] = fixed (10.0f, 10.0f);
		lrg_container_add_child (LRG_CONTAINER (grid), g[i]);
	}
	lrg_container_set_spacing (LRG_CONTAINER (wrap), 0.0f);
	for (i = 0; i < 3; i++)
	{
		wr[i] = fixed (40.0f, 10.0f);
		lrg_container_add_child (LRG_CONTAINER (wrap), wr[i]);
	}
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (grid));
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (wrap));
	lrg_widget_allocate (LRG_WIDGET (grid), 0.0f, 0.0f, 200.0f, 50.0f);
	lrg_widget_allocate (LRG_WIDGET (wrap), 0.0f, 100.0f, 100.0f, 50.0f);

	/* LTR: first column / item on the left */
	g_assert_cmpfloat (lrg_widget_get_x (g[0]), <, lrg_widget_get_x (g[1]));
	g_assert_cmpfloat (lrg_widget_get_x (wr[0]), ==, 0.0f);
	g_assert_cmpfloat (lrg_widget_get_x (wr[1]), ==, 40.0f);
	g_assert_cmpfloat (lrg_widget_get_y (wr[2]), >, lrg_widget_get_y (wr[0]));

	lrg_canvas_set_default_direction (canvas, LRG_WIDGET_DIRECTION_RTL);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_x (g[0]), >, lrg_widget_get_x (g[1]));
	g_assert_cmpfloat (lrg_widget_get_x (g[0]) + lrg_widget_get_width (g[0]), ==, 200.0f);
	g_assert_cmpfloat (lrg_widget_get_x (wr[0]), ==, 60.0f);
	g_assert_cmpfloat (lrg_widget_get_x (wr[1]), ==, 20.0f);
	g_assert_cmpfloat (lrg_widget_get_x (wr[2]), ==, 60.0f);
}

static void
test_rtl_inheritance (void)
{
	g_autoptr(LrgCanvas) canvas = new_canvas ();
	LrgVBox             *outer = lrg_vbox_new ();
	LrgHBox             *inner = lrg_hbox_new ();
	LrgWidget           *leaf = fixed (5.0f, 5.0f);

	lrg_container_add_child (LRG_CONTAINER (inner), leaf);
	lrg_container_add_child (LRG_CONTAINER (outer), LRG_WIDGET (inner));
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (outer));

	/* Detached widgets and fresh canvases are LTR */
	g_assert_cmpint (lrg_widget_get_direction (leaf), ==, LRG_WIDGET_DIRECTION_INHERIT);
	g_assert_cmpint (lrg_widget_get_effective_direction (leaf), ==, LRG_WIDGET_DIRECTION_LTR);
	g_assert_false (lrg_widget_is_rtl (leaf));

	lrg_canvas_set_default_direction (canvas, LRG_WIDGET_DIRECTION_RTL);
	g_assert_true (lrg_widget_is_rtl (leaf));

	/* A subtree can opt back into LTR, and its leaves follow */
	lrg_widget_set_direction (LRG_WIDGET (inner), LRG_WIDGET_DIRECTION_LTR);
	g_assert_false (lrg_widget_is_rtl (leaf));
	g_assert_true (lrg_widget_is_rtl (LRG_WIDGET (outer)));
	g_assert_true (lrg_widget_get_needs_layout (leaf));

	/* The property goes through GObject */
	g_object_set (leaf, "direction", LRG_WIDGET_DIRECTION_RTL, NULL);
	g_assert_true (lrg_widget_is_rtl (leaf));

	/* Opt-out classes keep their own placement */
	{
		g_autoptr(GTypeClass) scroll = g_type_class_ref (LRG_TYPE_SCROLL_BOX);
		g_autoptr(GTypeClass) canv = g_type_class_ref (LRG_TYPE_CANVAS);
		g_autoptr(GTypeClass) hbox = g_type_class_ref (LRG_TYPE_HBOX);

		g_assert_true (lrg_widget_class_get_handles_direction (LRG_WIDGET_CLASS (scroll)));
		g_assert_true (lrg_widget_class_get_handles_direction (LRG_WIDGET_CLASS (canv)));
		g_assert_false (lrg_widget_class_get_handles_direction (LRG_WIDGET_CLASS (hbox)));
	}
}

/* Labels hug the right edge in RTL */
static gfloat
min_instance_x (LrgPaintList *list)
{
	gfloat x = G_MAXFLOAT;
	guint  i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
		x = MIN (x, lrg_paint_list_get_instance (list, i)->rect[0]);
	return x;
}

static void
test_rtl_label (void)
{
	g_autoptr(LrgCanvas)    canvas = new_canvas ();
	g_autoptr(LrgPaintList) ltr = lrg_paint_list_new ();
	g_autoptr(LrgPaintList) rtl = lrg_paint_list_new ();
	LrgLabel               *label = lrg_label_new ("Hi");

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (label));
	lrg_widget_allocate (LRG_WIDGET (label), 0.0f, 0.0f, 400.0f, 30.0f);
	lrg_widget_paint (LRG_WIDGET (label), ltr);
	if (lrg_paint_list_get_n_instances (ltr) == 0)
	{
		g_test_skip ("no font to lay text out with");
		return;
	}
	lrg_widget_set_direction (LRG_WIDGET (label), LRG_WIDGET_DIRECTION_RTL);
	lrg_widget_paint (LRG_WIDGET (label), rtl);
	g_assert_cmpfloat (min_instance_x (ltr), <, 50.0f);
	g_assert_cmpfloat (min_instance_x (rtl), >, 300.0f);
}

/* The tab strip and the splitter mirror too */
static void
test_rtl_tabs_and_splitter (void)
{
	g_autoptr(LrgCanvas) canvas = new_canvas ();
	LrgTabView          *tabs = lrg_tab_view_new ();
	LrgSplitter         *split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	LrgWidget           *a = fixed (10.0f, 10.0f);
	LrgWidget           *b = fixed (10.0f, 10.0f);
	gfloat               rx;

	lrg_tab_view_add_tab (tabs, "One", fixed (5.0f, 5.0f));
	lrg_tab_view_add_tab (tabs, "Two", fixed (5.0f, 5.0f));
	lrg_splitter_set_start_child (split, a);
	lrg_splitter_set_end_child (split, b);
	lrg_splitter_set_position (split, 100.0f, FALSE);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (tabs));
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (split));
	lrg_widget_allocate (LRG_WIDGET (tabs), 0.0f, 0.0f, 200.0f, 100.0f);
	lrg_widget_allocate (LRG_WIDGET (split), 0.0f, 200.0f, 400.0f, 100.0f);

	g_assert_true (lrg_tab_view_get_indicator_rect (tabs, &rx, NULL, NULL, NULL));
	g_assert_cmpfloat (rx, <, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_x (a), ==, 0.0f);

	lrg_canvas_set_default_direction (canvas, LRG_WIDGET_DIRECTION_RTL);
	lrg_canvas_record (canvas);
	g_assert_true (lrg_tab_view_get_indicator_rect (tabs, &rx, NULL, NULL, NULL));
	g_assert_cmpfloat (rx, >=, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_x (a) + lrg_widget_get_width (a), ==, 400.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b), ==, 0.0f);
}

/* ==========================================================================
 * Loader
 * ========================================================================== */

static void
test_loader_on_action (void)
{
	g_autoptr(LrgCanvas)          canvas = new_canvas ();
	g_autoptr(LrgUILoader)        loader = lrg_ui_loader_new ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	g_autoptr(GError)             error = NULL;
	Recorder                      r = { 0 };
	LrgWidget                    *root;
	LrgWidget                    *details;
	const gchar                  *doc =
		"type: vbox\n"
		"children:\n"
		"  - {type: label, id: details, text: Details}\n"
		"  - {type: button, id: hide, text: Hide, on-clicked: {action: ui.hide, target: details}}\n"
		"  - {type: button, id: show, text: Show, on: {clicked: {action: ui.show, target: details}}}\n"
		"  - {type: button, id: num, text: Five, on-clicked: {action: game.n, target: '5', target-type: i}}\n"
		"  - {type: button, id: bare, text: Quit, on_clicked: app.quit}\n";

	add_recorder (group, "n", "i", &r);
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "game", G_ACTION_GROUP (group));

	root = lrg_ui_loader_load_from_data (loader, doc, -1, &error);
	g_assert_no_error (error);
	lrg_container_add_child (LRG_CONTAINER (canvas), root);
	g_object_unref (root);
	details = lrg_ui_loader_get_widget (loader, "details");

	g_signal_emit_by_name (lrg_ui_loader_get_widget (loader, "hide"), "clicked");
	g_assert_false (lrg_widget_get_visible (details));
	g_signal_emit_by_name (lrg_ui_loader_get_widget (loader, "show"), "clicked");
	g_assert_true (lrg_widget_get_visible (details));
	g_signal_emit_by_name (lrg_ui_loader_get_widget (loader, "num"), "clicked");
	g_assert_cmpint (r.count, ==, 1);
	g_assert_cmpint (g_variant_get_int32 (r.last), ==, 5);
	recorder_clear (&r);
}

static void
test_loader_errors (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	static const gchar *bad[] = {
		"type: button\non-clicked: {action: nodot}\n",
		"type: button\non-nosuchsignal: ui.hide\n",
		"type: button\non-clicked: {action: ui.hide, target: x, target-type: '!!'}\n",
		"type: button\non-clicked: {action: game.n, target: abc, target-type: i}\n",
		"type: vbox\nshortcuts: [{accel: '<Ctrl>', action: app.quit}]\n",
		"type: vbox\nshortcuts: [{accel: '<Ctrl>q', action: quit}]\n",
		"type: vbox\nshortcuts: {accel: q, action: app.quit}\n",
		"type: vbox\ndirection: sideways\n"
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (bad); i++)
	{
		g_autoptr(GError) error = NULL;
		LrgWidget        *w = lrg_ui_loader_load_from_data (loader, bad[i], -1, &error);

		g_assert_null (w);
		g_assert_nonnull (error);
	}
}

static void
test_loader_shortcuts_and_direction (void)
{
	g_autoptr(LrgCanvas)   canvas = new_canvas ();
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(GError)      error = NULL;
	LrgWidget             *root;
	gint                   quits = 0;
	const gchar           *doc =
		"shortcuts:\n"
		"  - {accel: '<Ctrl>q', action: app.quit}\n"
		"  - {accel: F1, action: ui.toggle, target: help}\n"
		"root:\n"
		"  type: hbox\n"
		"  direction: rtl\n"
		"  children:\n"
		"    - {type: label, id: help, text: Help}\n";

	g_signal_connect (canvas, "quit-requested", G_CALLBACK (on_quit), &quits);
	root = lrg_ui_loader_load_from_data (loader, doc, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_widget_get_direction (root), ==, LRG_WIDGET_DIRECTION_RTL);

	/* Bound once the root is in a canvas */
	g_assert_cmpuint (lrg_canvas_get_n_shortcuts (canvas), ==, 0);
	lrg_container_add_child (LRG_CONTAINER (canvas), root);
	g_assert_cmpuint (lrg_canvas_get_n_shortcuts (canvas), ==, 2);

	press_key (canvas, GRL_KEY_Q, LRG_INPUT_MODIFIER_CTRL, 0);
	g_assert_cmpint (quits, ==, 1);
	press_key (canvas, GRL_KEY_F1, LRG_INPUT_MODIFIER_NONE, 0);
	g_assert_false (lrg_widget_get_visible (lrg_ui_loader_get_widget (loader, "help")));

	/* Releasing the root unbinds them */
	lrg_container_remove_child (LRG_CONTAINER (canvas), root);
	g_object_unref (root);
	g_assert_cmpuint (lrg_canvas_get_n_shortcuts (canvas), ==, 0);
}

/* Hot reload swaps handlers and shortcuts without stacking them */
static void
test_loader_reload (void)
{
	g_autoptr(LrgCanvas)          canvas = new_canvas ();
	g_autoptr(LrgUILoader)        loader = lrg_ui_loader_new ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	g_autoptr(GError)             error = NULL;
	g_autofree gchar             *dir = NULL;
	g_autofree gchar             *path = NULL;
	Recorder                      a = { 0 };
	Recorder                      b = { 0 };
	LrgWidget                    *root;
	const gchar                  *v1 =
		"shortcuts: [{accel: '<Ctrl>1', action: g.a}]\n"
		"root: {type: button, id: btn, text: B, on-clicked: g.a}\n";
	const gchar                  *v2 =
		"shortcuts: [{accel: '<Ctrl>2', action: g.b}]\n"
		"root: {type: button, id: btn, text: B, on-clicked: g.b}\n";

	add_recorder (group, "a", NULL, &a);
	add_recorder (group, "b", NULL, &b);
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "g", G_ACTION_GROUP (group));

	dir = g_dir_make_tmp ("lrg-actions-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "ui.yaml", NULL);
	g_assert_true (g_file_set_contents (path, v1, -1, &error));

	root = lrg_ui_loader_watch_file (loader, path, LRG_CONTAINER (canvas), &error);
	g_assert_no_error (error);
	g_signal_emit_by_name (root, "clicked");
	press_key (canvas, GRL_KEY_ONE, LRG_INPUT_MODIFIER_CTRL, 0);
	g_assert_cmpint (a.count, ==, 2);

	g_assert_true (g_file_set_contents (path, v2, -1, &error));
	g_assert_true (lrg_ui_loader_reload (loader, &error));
	g_assert_no_error (error);
	root = lrg_ui_loader_get_widget (loader, "btn");

	g_signal_emit_by_name (root, "clicked");
	press_key (canvas, GRL_KEY_ONE, LRG_INPUT_MODIFIER_CTRL, 0);
	press_key (canvas, GRL_KEY_TWO, LRG_INPUT_MODIFIER_CTRL, 0);
	g_assert_cmpint (a.count, ==, 2);
	g_assert_cmpint (b.count, ==, 2);
	g_assert_cmpuint (lrg_canvas_get_n_shortcuts (canvas), ==, 1);

	lrg_ui_loader_unwatch_file (loader, path);
	g_unlink (path);
	g_rmdir (dir);
	recorder_clear (&a);
	recorder_clear (&b);
}

/* ==========================================================================
 * Core input and container additions
 * ========================================================================== */

/* A widget that logs the events it receives */
typedef struct
{
	gint moves;
	gint ups;
	gint downs;
	gint key_ups;
	gint last_button;
} EventLog;

static gboolean
on_log_event (LrgWidget        *widget,
              const LrgUIEvent *event,
              gpointer          user_data)
{
	EventLog *log = user_data;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		log->moves++;
		break;
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		log->downs++;
		log->last_button = lrg_ui_event_get_button (event);
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		log->ups++;
		log->last_button = lrg_ui_event_get_button (event);
		return TRUE;
	case LRG_UI_EVENT_KEY_UP:
		log->key_ups++;
		return TRUE;
	default:
		break;
	}
	return FALSE;
}

#define TEST_TYPE_LOGGER (test_logger_get_type ())
G_DECLARE_FINAL_TYPE (TestLogger, test_logger, TEST, LOGGER, LrgWidget)

struct _TestLogger
{
	LrgWidget  parent_instance;
	EventLog  *log;
};

G_DEFINE_FINAL_TYPE (TestLogger, test_logger, LRG_TYPE_WIDGET)

static gboolean
test_logger_handle_event (LrgWidget        *widget,
                          const LrgUIEvent *event)
{
	return on_log_event (widget, event, TEST_LOGGER (widget)->log);
}

static void
test_logger_class_init (TestLoggerClass *klass)
{
	LRG_WIDGET_CLASS (klass)->handle_event = test_logger_handle_event;
}

static void
test_logger_init (TestLogger *self)
{
}

static LrgWidget *
logger_new (EventLog *log)
{
	TestLogger *w = g_object_new (TEST_TYPE_LOGGER, NULL);

	w->log = log;
	lrg_widget_set_focusable (LRG_WIDGET (w), TRUE);
	return LRG_WIDGET (w);
}

/* pointer_frame: pointer at (@x, @y) with button masks */
static void
pointer_frame (LrgCanvas *canvas,
               gfloat     x,
               gfloat     y,
               guint      down,
               guint      pressed,
               guint      released)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = x;
	input.pointer_y = y;
	input.buttons_down = down;
	input.buttons_pressed = pressed;
	input.buttons_released = released;
	input.delta_time = 1.0f / 60.0f;
	lrg_canvas_process_input (canvas, &input);
}

static void
test_core_capture_all_buttons (void)
{
	g_autoptr(LrgCanvas) canvas = new_canvas ();
	EventLog             log = { 0 };
	LrgWidget           *w = logger_new (&log);
	guint                right = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_RIGHT);
	guint                middle = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_MIDDLE);

	lrg_container_add_child (LRG_CONTAINER (canvas), w);
	lrg_widget_allocate (w, 10.0f, 10.0f, 50.0f, 50.0f);

	/* Right drag starting on the widget, released far outside it */
	pointer_frame (canvas, 20.0f, 20.0f, 0, 0, 0);
	pointer_frame (canvas, 20.0f, 20.0f, right, right, 0);
	pointer_frame (canvas, 400.0f, 400.0f, right, 0, 0);
	pointer_frame (canvas, 420.0f, 410.0f, right, 0, 0);
	g_assert_cmpint (log.downs, ==, 1);
	g_assert_cmpint (log.moves, >=, 3);
	pointer_frame (canvas, 420.0f, 410.0f, 0, 0, right);
	g_assert_cmpint (log.ups, ==, 1);
	g_assert_cmpint (log.last_button, ==, GRL_MOUSE_BUTTON_RIGHT);
	/* Only left presses make it look pressed */
	g_assert_false (lrg_widget_get_state_flags (w) & LRG_WIDGET_STATE_PRESSED);

	/* Capture ends with the release: later moves outside are not sent */
	log.moves = 0;
	pointer_frame (canvas, 500.0f, 500.0f, 0, 0, 0);
	g_assert_cmpint (log.moves, ==, 0);

	/* Middle + right held together: capture lasts until both are up */
	pointer_frame (canvas, 20.0f, 20.0f, 0, 0, 0);
	pointer_frame (canvas, 20.0f, 20.0f, middle, middle, 0);
	pointer_frame (canvas, 300.0f, 20.0f, middle | right, right, 0);
	pointer_frame (canvas, 300.0f, 20.0f, right, 0, middle);
	log.moves = 0;
	pointer_frame (canvas, 310.0f, 30.0f, right, 0, 0);
	g_assert_cmpint (log.moves, ==, 1);
	pointer_frame (canvas, 310.0f, 30.0f, 0, 0, right);
	log.moves = 0;
	pointer_frame (canvas, 320.0f, 30.0f, 0, 0, 0);
	g_assert_cmpint (log.moves, ==, 0);
}

static void
test_core_key_up (void)
{
	g_autoptr(LrgCanvas) canvas = new_canvas ();
	EventLog             log = { 0 };
	LrgWidget           *w = logger_new (&log);
	LrgCanvasInput       input;

	lrg_container_add_child (LRG_CONTAINER (canvas), w);
	lrg_widget_allocate (w, 10.0f, 10.0f, 50.0f, 50.0f);
	lrg_canvas_set_focused_widget (canvas, w);

	lrg_canvas_input_init (&input);
	lrg_canvas_input_add_key_release (&input, GRL_KEY_W);
	lrg_canvas_process_input (canvas, &input);
	g_assert_cmpint (log.key_ups, ==, 1);
}

static void
test_core_overflow_hit_test (void)
{
	g_autoptr(LrgCanvas) canvas = new_canvas ();
	LrgPanel            *panel = lrg_panel_new ();
	LrgWidget           *outside = fixed (20.0f, 20.0f);

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (panel));
	lrg_widget_allocate (LRG_WIDGET (panel), 100.0f, 100.0f, 50.0f, 50.0f);
	lrg_container_add_child (LRG_CONTAINER (panel), outside);
	lrg_widget_set_position (outside, 80.0f, 0.0f);   /* beyond the right edge */
	lrg_widget_set_size (outside, 20.0f, 20.0f);

	g_assert_null (lrg_canvas_widget_at_point (canvas, 190.0f, 110.0f));
	lrg_container_set_overflow_hit_test (LRG_CONTAINER (panel), TRUE);
	g_assert_true (lrg_container_get_overflow_hit_test (LRG_CONTAINER (panel)));
	g_assert_true (lrg_canvas_widget_at_point (canvas, 190.0f, 110.0f) == outside);
	/* Empty space outside the container is still not the container */
	g_assert_null (lrg_canvas_widget_at_point (canvas, 300.0f, 300.0f));
}

static void
test_core_insert_reorder (void)
{
	g_autoptr(LrgVBox) box = lrg_vbox_new ();
	LrgWidget         *a = fixed (1.0f, 1.0f);
	LrgWidget         *b = fixed (1.0f, 1.0f);
	LrgWidget         *c = fixed (1.0f, 1.0f);
	LrgContainer      *ct = LRG_CONTAINER (box);

	lrg_container_add_child (ct, a);
	lrg_container_insert_child_at (ct, b, 0);
	lrg_container_insert_child_at (ct, c, G_MAXUINT);
	g_assert_true (lrg_container_get_child (ct, 0) == b);
	g_assert_true (lrg_container_get_child (ct, 1) == a);
	g_assert_true (lrg_container_get_child (ct, 2) == c);
	g_assert_true (lrg_widget_get_parent (b) == ct);

	lrg_container_reorder_child (ct, c, 0);
	g_assert_true (lrg_container_get_child (ct, 0) == c);
	g_assert_true (lrg_container_get_child (ct, 1) == b);
	lrg_container_reorder_child (ct, c, G_MAXUINT);
	g_assert_true (lrg_container_get_child (ct, 2) == c);
	g_assert_true (lrg_widget_get_parent (c) == ct);
	g_assert_cmpuint (lrg_container_get_child_count (ct), ==, 3);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/actions/scope", test_actions_scope);
	g_test_add_func ("/ui/actions/popup-scope", test_actions_popup_scope);
	g_test_add_func ("/ui/actions/parameters", test_actions_parameters);
	g_test_add_func ("/ui/actions/builtins", test_actions_builtins);
	g_test_add_func ("/ui/actions/toast", test_actions_toast);
	g_test_add_func ("/ui/actions/accel/parse", test_accel_parse);
	g_test_add_func ("/ui/actions/accel/bad", test_accel_bad);
	g_test_add_func ("/ui/actions/accel/round-trip", test_accel_round_trip);
	g_test_add_func ("/ui/actions/shortcuts/fire", test_shortcuts_fire);
	g_test_add_func ("/ui/actions/shortcuts/before-defaults", test_shortcuts_before_defaults);
	g_test_add_func ("/ui/actions/shortcuts/focus-scope", test_shortcuts_focus_scope);
	g_test_add_func ("/ui/actions/shortcuts/text-input", test_shortcuts_text_input);
	g_test_add_func ("/ui/actions/rtl/hbox", test_rtl_hbox);
	g_test_add_func ("/ui/actions/rtl/halign", test_rtl_halign);
	g_test_add_func ("/ui/actions/rtl/grid-and-wrap", test_rtl_grid_and_wrap);
	g_test_add_func ("/ui/actions/rtl/inheritance", test_rtl_inheritance);
	g_test_add_func ("/ui/actions/rtl/label", test_rtl_label);
	g_test_add_func ("/ui/actions/rtl/tabs-and-splitter", test_rtl_tabs_and_splitter);
	g_test_add_func ("/ui/actions/loader/on-action", test_loader_on_action);
	g_test_add_func ("/ui/actions/loader/errors", test_loader_errors);
	g_test_add_func ("/ui/actions/loader/shortcuts-and-direction", test_loader_shortcuts_and_direction);
	g_test_add_func ("/ui/actions/loader/reload", test_loader_reload);

	g_test_add_func ("/ui/actions/core/capture-all-buttons", test_core_capture_all_buttons);
	g_test_add_func ("/ui/actions/core/key-up", test_core_key_up);
	g_test_add_func ("/ui/actions/core/overflow-hit-test", test_core_overflow_hit_test);
	g_test_add_func ("/ui/actions/core/insert-reorder", test_core_insert_reorder);

	return g_test_run ();
}
