/* test-ui-menus.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMenu and LrgMenuBar.
 *
 * Headless: menus open on a canvas popup layer, are driven through
 * lrg_canvas_process_input() like the real input loop and stepped at
 * 60 Hz with lrg_canvas_update(), so hover delays, the safe triangle
 * and the gliding highlight are deterministic. Actions live in a
 * GSimpleActionGroup inserted on the canvas under the "test" prefix.
 * One GPU case renders a menu under a hidden window in every theme
 * preset and probes the highlight; it skips without a display.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * Fixture: a canvas, an owner button and a "test" action group
 * ========================================================================== */

typedef struct
{
	LrgCanvas          *canvas;
	LrgWidget          *button;     /* owned by the canvas */
	GSimpleActionGroup *group;
	GPtrArray          *log;        /* "name" or "name:target" per activation */
	gfloat              px;         /* last pointer position */
	gfloat              py;
} Fixture;

static void
on_action_activate (GSimpleAction *action,
                    GVariant      *parameter,
                    gpointer       user_data)
{
	Fixture *f = user_data;

	if (parameter != NULL && g_variant_is_of_type (parameter, G_VARIANT_TYPE_STRING))
		g_ptr_array_add (f->log, g_strdup_printf ("%s:%s", g_action_get_name (G_ACTION (action)),
		                                          g_variant_get_string (parameter, NULL)));
	else
		g_ptr_array_add (f->log, g_strdup (g_action_get_name (G_ACTION (action))));
}

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(GSimpleAction) open = g_simple_action_new ("open", NULL);
	g_autoptr(GSimpleAction) save = g_simple_action_new ("save", NULL);
	g_autoptr(GSimpleAction) quit = g_simple_action_new ("quit", NULL);
	g_autoptr(GSimpleAction) off = g_simple_action_new ("off", NULL);
	g_autoptr(GSimpleAction) pick = g_simple_action_new ("pick", G_VARIANT_TYPE_STRING);
	g_autoptr(GSimpleAction) grid = g_simple_action_new_stateful ("grid", NULL, g_variant_new_boolean (TRUE));
	g_autoptr(GSimpleAction) mode = g_simple_action_new_stateful ("mode", G_VARIANT_TYPE_STRING,
	                                                              g_variant_new_string ("b"));
	GSimpleAction *all[] = { open, save, quit, off, pick };
	guint          i;

	f->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (f->canvas), 800.0f, 600.0f);
	lrg_canvas_set_paint_background (f->canvas, FALSE);
	f->log = g_ptr_array_new_with_free_func (g_free);
	f->px = -10.0f;
	f->py = -10.0f;

	f->group = g_simple_action_group_new ();
	for (i = 0; i < G_N_ELEMENTS (all); i++)
	{
		g_signal_connect (all[i], "activate", G_CALLBACK (on_action_activate), f);
		g_action_map_add_action (G_ACTION_MAP (f->group), G_ACTION (all[i]));
	}
	g_simple_action_set_enabled (off, FALSE);
	/* Stateful actions keep GSimpleAction's default handlers (toggle /
	 * set state); log through change-state notifications instead */
	g_action_map_add_action (G_ACTION_MAP (f->group), G_ACTION (grid));
	g_action_map_add_action (G_ACTION_MAP (f->group), G_ACTION (mode));
	lrg_widget_insert_action_group (LRG_WIDGET (f->canvas), "test", G_ACTION_GROUP (f->group));

	f->button = LRG_WIDGET (lrg_button_new ("Menu"));
	lrg_container_add_child (LRG_CONTAINER (f->canvas), f->button);
	g_object_unref (f->button);
	lrg_widget_allocate (f->button, 40.0f, 40.0f, 100.0f, 30.0f);
	lrg_canvas_record (f->canvas);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->canvas);
	g_clear_object (&f->group);
	g_ptr_array_unref (f->log);
}

/* ==========================================================================
 * Input helpers
 * ========================================================================== */

static void
frame (Fixture        *f,
       LrgCanvasInput *input)
{
	lrg_canvas_input_init (input);
	input->pointer_x = f->px;
	input->pointer_y = f->py;
	input->delta_time = DT;
}

static void
move_to (Fixture *f,
         gfloat   x,
         gfloat   y)
{
	LrgCanvasInput input;

	f->px = x;
	f->py = y;
	frame (f, &input);
	lrg_canvas_process_input (f->canvas, &input);
}

static void
button_frame (Fixture *f,
              gfloat   x,
              gfloat   y,
              guint    mask,
              gboolean press)
{
	LrgCanvasInput input;

	f->px = x;
	f->py = y;
	frame (f, &input);
	if (press)
	{
		input.buttons_down = mask;
		input.buttons_pressed = mask;
	}
	else
		input.buttons_released = mask;
	lrg_canvas_process_input (f->canvas, &input);
}

static void
click_at (Fixture *f,
          gfloat   x,
          gfloat   y)
{
	move_to (f, x, y);
	button_frame (f, x, y, LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT), TRUE);
	button_frame (f, x, y, LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT), FALSE);
}

static void
key (Fixture *f,
     GrlKey   k)
{
	LrgCanvasInput input;

	frame (f, &input);
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (f->canvas, &input);
}

static void
type_char (Fixture  *f,
           gunichar  c)
{
	LrgCanvasInput input;

	frame (f, &input);
	lrg_canvas_input_add_char (&input, c);
	lrg_canvas_process_input (f->canvas, &input);
}

/* frames: @n idle frames at the current pointer */
static void
frames (Fixture *f,
        guint    n)
{
	guint i;

	for (i = 0; i < n; i++)
		move_to (f, f->px, f->py);
}

/* settle: paint once (appear animations start on their first paint),
 * then run frames until nothing moves; returns the frames it took */
static guint
settle (Fixture *f)
{
	guint i;

	lrg_canvas_record (f->canvas);
	for (i = 0; i < 600; i++)
	{
		LrgCanvasInput input;

		/* Refresh hover like a game loop's input pass (no time),
		 * then advance one frame and ask whether anything moves */
		frame (f, &input);
		input.delta_time = 0.0f;
		lrg_canvas_process_input (f->canvas, &input);
		if (!lrg_canvas_update (f->canvas, DT))
			return i;
	}
	return i;
}

/* row_center: world centre of a menu row */
static void
row_center (LrgMenu *menu,
            guint    index,
            gfloat  *x,
            gfloat  *y)
{
	gfloat rx;
	gfloat ry;
	gfloat rw;
	gfloat rh;

	g_assert_true (lrg_menu_get_entry_rect (menu, index, &rx, &ry, &rw, &rh));
	*x = rx + rw * 0.5f;
	*y = ry + rh * 0.5f;
}

static void
hover_row (Fixture *f,
           LrgMenu *menu,
           guint    index)
{
	gfloat x;
	gfloat y;

	row_center (menu, index, &x, &y);
	move_to (f, x, y);
}

/* ==========================================================================
 * Models
 * ========================================================================== */

/*
 * file_model:
 *   0 _Open          test.open   <Ctrl>o
 *   1 _Save          test.save   <Ctrl>s
 *   2 ---
 *   3 Recent (header)
 *   4 Pick A         test.pick("a")
 *   5 Pick B         test.pick("b")
 *   6 ---
 *   7 _Export >      submenu: PNG test.pick("png"), SVG test.pick("svg")
 *   8 Disabled       test.off
 *   9 No action
 *  10 ---
 *  11 _Quit          test.quit
 */
static GMenu *
file_model (void)
{
	GMenu           *menu = g_menu_new ();
	g_autoptr(GMenu) top = g_menu_new ();
	g_autoptr(GMenu) recent = g_menu_new ();
	g_autoptr(GMenu) more = g_menu_new ();
	g_autoptr(GMenu) exp = g_menu_new ();
	g_autoptr(GMenu) last = g_menu_new ();
	g_autoptr(GMenuItem) open = g_menu_item_new ("_Open", "test.open");
	g_autoptr(GMenuItem) save = g_menu_item_new ("_Save", "test.save");

	g_menu_item_set_attribute (open, "accel", "s", "<Ctrl>o");
	g_menu_item_set_attribute (open, "icon", "s", "folder");
	g_menu_item_set_attribute (save, "accel", "s", "<Ctrl>s");
	g_menu_append_item (top, open);
	g_menu_append_item (top, save);
	g_menu_append_section (menu, NULL, G_MENU_MODEL (top));

	g_menu_append (recent, "Pick A", "test.pick::a");
	g_menu_append (recent, "Pick B", "test.pick::b");
	g_menu_append_section (menu, "Recent", G_MENU_MODEL (recent));

	g_menu_append (exp, "PNG", "test.pick::png");
	g_menu_append (exp, "SVG", "test.pick::svg");
	g_menu_append_submenu (more, "_Export", G_MENU_MODEL (exp));
	g_menu_append (more, "Disabled", "test.off");
	g_menu_append (more, "No action", NULL);
	g_menu_append_section (menu, NULL, G_MENU_MODEL (more));

	g_menu_append (last, "_Quit", "test.quit");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (last));
	return menu;
}

/* view_model: View > Grid (check), Mode A / Mode B (radio) */
static GMenu *
view_model (void)
{
	GMenu *menu = g_menu_new ();

	g_menu_append (menu, "Grid", "test.grid");
	g_menu_append (menu, "Mode A", "test.mode::a");
	g_menu_append (menu, "Mode B", "test.mode::b");
	return menu;
}

static LrgMenu *
open_file_menu (Fixture *f)
{
	g_autoptr(GMenu) model = file_model ();
	LrgMenu         *menu = lrg_menu_new_from_model (G_MENU_MODEL (model));

	lrg_menu_set_submenu_delay (menu, 0.2f);
	lrg_menu_popup_at_widget (menu, f->button, LRG_POPUP_PLACEMENT_BELOW);
	frames (f, 1);
	return menu;
}

/* ==========================================================================
 * Building
 * ========================================================================== */

static void
test_build_from_gmenu (void)
{
	g_autoptr(GMenu)   model = file_model ();
	g_autoptr(LrgMenu) menu = lrg_menu_new_from_model (G_MENU_MODEL (model));
	g_autofree gchar  *plain = NULL;
	gint               mn = 0;

	g_assert_cmpuint (lrg_menu_get_n_entries (menu), ==, 12);
	g_assert_cmpint (lrg_menu_get_entry_kind (menu, 0), ==, LRG_MENU_ENTRY_ITEM);
	g_assert_cmpstr (lrg_menu_get_entry_label (menu, 0), ==, "Open");
	g_assert_cmpstr (lrg_menu_get_entry_action (menu, 0), ==, "test.open");
	g_assert_cmpint (lrg_menu_get_entry_kind (menu, 2), ==, LRG_MENU_ENTRY_SEPARATOR);
	g_assert_cmpint (lrg_menu_get_entry_kind (menu, 3), ==, LRG_MENU_ENTRY_HEADER);
	g_assert_cmpstr (lrg_menu_get_entry_label (menu, 3), ==, "Recent");
	g_assert_cmpint (lrg_menu_get_entry_kind (menu, 6), ==, LRG_MENU_ENTRY_SEPARATOR);
	g_assert_cmpint (lrg_menu_get_entry_kind (menu, 7), ==, LRG_MENU_ENTRY_SUBMENU);
	g_assert_cmpstr (lrg_menu_get_entry_label (menu, 7), ==, "Export");
	g_assert_cmpint (lrg_menu_get_entry_kind (menu, 10), ==, LRG_MENU_ENTRY_SEPARATOR);
	g_assert_cmpstr (lrg_menu_get_entry_label (menu, 11), ==, "Quit");
	g_assert_cmpint (lrg_menu_get_entry_kind (menu, 99), ==, LRG_MENU_ENTRY_SEPARATOR);

	/* Mnemonic markers */
	plain = lrg_menu_split_mnemonic ("Save __As_x", &mn);
	g_assert_cmpstr (plain, ==, "Save _Asx");
	g_assert_cmpint (mn, ==, 8);

	/* Editing the model rebuilds the rows */
	g_menu_remove (model, 3);
	g_assert_cmpuint (lrg_menu_get_n_entries (menu), ==, 10);
	g_assert_cmpint (lrg_menu_get_entry_kind (menu, 9), ==, LRG_MENU_ENTRY_ITEM);
}

static void
test_enabled_needs_action (Fixture       *f,
                           gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);

	g_assert_true (lrg_menu_get_open (menu));
	g_assert_true (lrg_menu_get_owner (menu) == f->button);
	g_assert_true (lrg_menu_get_entry_enabled (menu, 0));
	g_assert_false (lrg_menu_get_entry_enabled (menu, 2));   /* separator */
	g_assert_false (lrg_menu_get_entry_enabled (menu, 3));   /* header */
	g_assert_true (lrg_menu_get_entry_enabled (menu, 7));    /* submenu */
	g_assert_false (lrg_menu_get_entry_enabled (menu, 8));   /* disabled action */
	g_assert_false (lrg_menu_get_entry_enabled (menu, 9));   /* no action */
	lrg_menu_close (menu);
	settle (f);
}

/* ==========================================================================
 * Placement
 * ========================================================================== */

static void
test_popup_placement (Fixture       *f,
                      gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);
	LrgWidget         *mw = LRG_WIDGET (menu);
	gfloat             h;

	/* Below the button, left edges aligned, on the popup layer */
	g_assert_true (lrg_canvas_get_top_popup (f->canvas) == mw);
	g_assert_cmpfloat (lrg_widget_get_world_x (mw), ==, 40.0f);
	g_assert_cmpfloat (lrg_widget_get_world_y (mw), >=, 70.0f);
	g_assert_cmpfloat (lrg_widget_get_world_y (mw), <, 80.0f);
	g_assert_cmpfloat (lrg_widget_get_width (mw), >=, 160.0f);
	h = lrg_widget_get_height (mw);
	g_assert_cmpfloat (h, >, 100.0f);
	g_assert_true (lrg_widget_get_state_flags (f->button) & LRG_WIDGET_STATE_OPEN);

	/* Near the bottom edge it flips above the button */
	lrg_menu_close (menu);
	settle (f);
	lrg_widget_allocate (f->button, 40.0f, 560.0f, 100.0f, 30.0f);
	lrg_menu_popup_at_widget (menu, f->button, LRG_POPUP_PLACEMENT_BELOW);
	g_assert_cmpfloat (lrg_widget_get_world_y (mw) + h, <=, 560.0f);

	/* Context menu near the bottom-right corner flips both ways */
	lrg_menu_popup_at_point (menu, f->button, 790.0f, 590.0f);
	g_assert_cmpfloat (lrg_widget_get_world_x (mw) + lrg_widget_get_width (mw), <=, 790.0f);
	g_assert_cmpfloat (lrg_widget_get_world_y (mw) + h, <=, 590.0f);

	/* ... and opens down-right where there is room */
	lrg_menu_popup_at_point (menu, f->button, 200.0f, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_world_x (mw), ==, 200.0f);
	g_assert_cmpfloat (lrg_widget_get_world_y (mw), ==, 100.0f);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);
	lrg_menu_close (menu);
	settle (f);
}

static void
test_context_release_does_not_activate (Fixture       *f,
                                        gconstpointer  data)
{
	g_autoptr(GMenu)   model = file_model ();
	g_autoptr(LrgMenu) menu = lrg_menu_new_from_model (G_MENU_MODEL (model));
	guint              right = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_RIGHT);
	gfloat             x;
	gfloat             y;

	/* Right press opens at the pointer; its release lands on the menu */
	move_to (f, 300.0f, 200.0f);
	button_frame (f, 300.0f, 200.0f, right, TRUE);
	lrg_menu_popup_at_point (menu, f->button, 300.0f, 200.0f);
	button_frame (f, 300.0f, 200.0f, right, FALSE);
	g_assert_true (lrg_menu_get_open (menu));
	g_assert_cmpuint (f->log->len, ==, 0);

	/* Moving on and releasing the right button over a row picks it */
	hover_row (f, menu, 0);
	row_center (menu, 0, &x, &y);
	button_frame (f, x, y, right, TRUE);
	button_frame (f, x, y, right, FALSE);
	g_assert_false (lrg_menu_get_open (menu));
	g_assert_cmpuint (f->log->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (f->log, 0), ==, "open");
	settle (f);
}

/*
 * TestTarget: a widget with a context menu, written the documented way:
 * a right press opens the menu at the pointer, and while it is open the
 * captured moves / releases are forwarded to it.
 */
#define TEST_TYPE_TARGET (test_target_get_type ())
G_DECLARE_FINAL_TYPE (TestTarget, test_target, TEST, TARGET, LrgWidget)

struct _TestTarget
{
	LrgWidget  parent_instance;
	LrgMenu   *menu;   /* owned */
};

G_DEFINE_FINAL_TYPE (TestTarget, test_target, LRG_TYPE_WIDGET)

static gboolean
test_target_handle_event (LrgWidget        *widget,
                          const LrgUIEvent *event)
{
	TestTarget *self = TEST_TARGET (widget);

	if (lrg_menu_get_open (self->menu) && lrg_menu_forward_pointer_event (self->menu, event))
		return TRUE;

	if (lrg_ui_event_get_event_type (event) == LRG_UI_EVENT_MOUSE_BUTTON_DOWN &&
	    lrg_ui_event_get_button (event) == 1)
	{
		LrgWidget *canvas = lrg_widget_get_root (widget);

		lrg_menu_popup_at_point (self->menu, widget,
		                         lrg_ui_event_get_x (event) - lrg_widget_get_world_x (canvas),
		                         lrg_ui_event_get_y (event) - lrg_widget_get_world_y (canvas));
		return TRUE;
	}
	return FALSE;
}

static void
test_target_dispose (GObject *object)
{
	g_clear_object (&TEST_TARGET (object)->menu);
	G_OBJECT_CLASS (test_target_parent_class)->dispose (object);
}

static void
test_target_class_init (TestTargetClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = test_target_dispose;
	LRG_WIDGET_CLASS (klass)->handle_event = test_target_handle_event;
}

static void
test_target_init (TestTarget *self)
{
}

static void
test_context_drag_release (Fixture       *f,
                           gconstpointer  data)
{
	g_autoptr(GMenu) model = file_model ();
	TestTarget      *target = g_object_new (TEST_TYPE_TARGET, NULL);
	guint            right = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_RIGHT);
	LrgCanvasInput   input;
	gfloat           x;
	gfloat           y;

	target->menu = lrg_menu_new_from_model (G_MENU_MODEL (model));
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (target));
	g_object_unref (target);
	lrg_widget_allocate (LRG_WIDGET (target), 200.0f, 150.0f, 300.0f, 300.0f);

	/* Right press on the target opens its menu; the target holds the
	 * capture, so the release (still on the menu corner) reaches it,
	 * is forwarded, and activates nothing */
	move_to (f, 300.0f, 200.0f);
	button_frame (f, 300.0f, 200.0f, right, TRUE);
	g_assert_true (lrg_menu_get_open (target->menu));
	g_assert_true (lrg_menu_get_owner (target->menu) == LRG_WIDGET (target));
	button_frame (f, 300.0f, 200.0f, right, FALSE);
	g_assert_true (lrg_menu_get_open (target->menu));
	g_assert_cmpuint (f->log->len, ==, 0);
	lrg_menu_close (target->menu);
	settle (f);

	/* Press, drag (captured) onto "Save", release: picks it */
	move_to (f, 300.0f, 200.0f);
	button_frame (f, 300.0f, 200.0f, right, TRUE);
	row_center (target->menu, 1, &x, &y);
	f->px = x;
	f->py = y;
	frame (f, &input);
	input.buttons_down = right;
	lrg_canvas_process_input (f->canvas, &input);
	g_assert_cmpint (lrg_menu_get_highlighted (target->menu), ==, 1);
	button_frame (f, x, y, right, FALSE);
	g_assert_false (lrg_menu_get_open (target->menu));
	g_assert_cmpuint (f->log->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (f->log, 0), ==, "save");
	settle (f);
}

static void
test_accel_from_shortcut (Fixture       *f,
                          gconstpointer  data)
{
	g_autoptr(GMenu)   model = view_model ();
	g_autoptr(LrgMenu) menu = lrg_menu_new_from_model (G_MENU_MODEL (model));
	g_autoptr(LrgTheme) theme = lrg_theme_new_preset ("midnight");
	gfloat             plain;
	gfloat             with_accel;

	/* Measuring needs a real font; skip when text measures nothing */
	lrg_widget_set_theme (LRG_WIDGET (f->canvas), theme);
	if (lrg_theme_get_ui_font (theme) == NULL)
	{
		g_test_skip ("No UI font without a graphics context");
		return;
	}

	lrg_menu_popup_at_widget (menu, f->button, LRG_POPUP_PLACEMENT_BELOW);
	plain = lrg_widget_get_width (LRG_WIDGET (menu));
	lrg_menu_close (menu);
	settle (f);

	/* A canvas shortcut bound to the item shows up in its accel column */
	lrg_canvas_add_shortcut (f->canvas, "<Ctrl><Shift><Alt>F12", "test.grid", NULL);
	lrg_menu_popup_at_widget (menu, f->button, LRG_POPUP_PLACEMENT_BELOW);
	with_accel = lrg_widget_get_width (LRG_WIDGET (menu));
	g_assert_cmpfloat (with_accel, >, plain);
	lrg_menu_close (menu);
	settle (f);
}

/* ==========================================================================
 * Highlight
 * ========================================================================== */

static void
test_hover_glide (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);
	guint              n;

	settle (f);
	hover_row (f, menu, 0);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 0);
	settle (f);

	/* Moving three rows down glides: still moving for several frames */
	hover_row (f, menu, 4);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 4);
	g_assert_true (lrg_canvas_update (f->canvas, DT));
	n = settle (f);
	g_assert_cmpuint (n, >, 3);
	g_assert_cmpuint (n, <, 600);

	/* Separators and headers keep nothing lit (no submenu open) */
	hover_row (f, menu, 2);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, -1);
	hover_row (f, menu, 8);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, -1);

	/* Highlight paints a CONTROL_HOVER shape at the row once settled */
	hover_row (f, menu, 1);
	settle (f);
	{
		LrgPaintList   *list = lrg_canvas_record (f->canvas);
		LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (menu));
		const GrlColor *hc = lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
		gfloat          rx;
		gfloat          ry;
		gfloat          rw;
		gfloat          rh;
		gboolean        found = FALSE;
		guint           i;

		lrg_menu_get_entry_rect (menu, 1, &rx, &ry, &rw, &rh);
		for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
		{
			const LrgPaintInstance *in = lrg_paint_list_get_instance (list, i);

			if (in->color[0] == hc->r && in->color[1] == hc->g && in->color[2] == hc->b &&
			    in->color[3] == hc->a && fabsf (in->rect[1] - ry) < 1.0f &&
			    fabsf (in->rect[3] - (ry + rh)) < 1.0f)
				found = TRUE;
		}
		g_assert_true (found);
	}
	lrg_menu_close (menu);
	settle (f);
}

/* ==========================================================================
 * Submenus
 * ========================================================================== */

static void
test_submenu_delay (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);
	LrgMenu           *sub;
	guint              i;

	/* Resting on the submenu row opens it after the delay, not before */
	hover_row (f, menu, 7);
	g_assert_null (lrg_menu_get_open_submenu (menu));
	frames (f, 5);
	g_assert_null (lrg_menu_get_open_submenu (menu));
	for (i = 0; i < 15 && lrg_menu_get_open_submenu (menu) == NULL; i++)
		frames (f, 1);
	sub = lrg_menu_get_open_submenu (menu);
	g_assert_nonnull (sub);
	g_assert_true (lrg_menu_get_parent_menu (sub) == menu);
	g_assert_true (lrg_menu_get_owner (sub) == f->button);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 2);

	/* To the right, flush with the parent, first row level with the item */
	{
		gfloat ix;
		gfloat iy;
		gfloat sx;
		gfloat sy;

		lrg_menu_get_entry_rect (menu, 7, &ix, &iy, NULL, NULL);
		lrg_menu_get_entry_rect (sub, 0, &sx, &sy, NULL, NULL);
		g_assert_cmpfloat (lrg_widget_get_world_x (LRG_WIDGET (sub)), >=,
		                   lrg_widget_get_world_x (LRG_WIDGET (menu)) + lrg_widget_get_width (LRG_WIDGET (menu)));
		g_assert_cmpfloat (fabsf (sy - iy), <, 1.5f);
	}
	/* Opened by the pointer, keys stay with the parent */
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (menu));

	/* Resting on another row closes it after the delay */
	hover_row (f, menu, 1);
	g_assert_nonnull (lrg_menu_get_open_submenu (menu));
	frames (f, 20);
	g_assert_null (lrg_menu_get_open_submenu (menu));
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);
	lrg_menu_close (menu);
	settle (f);
}

static void
test_submenu_keyboard (Fixture       *f,
                       gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);
	LrgMenu           *sub;

	/* Down walks to the submenu row, skipping separators and headers */
	key (f, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 0);
	key (f, GRL_KEY_DOWN);
	key (f, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 4);
	key (f, GRL_KEY_DOWN);
	key (f, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 7);

	/* Right opens it at once, focused, first row highlighted */
	key (f, GRL_KEY_RIGHT);
	sub = lrg_menu_get_open_submenu (menu);
	g_assert_nonnull (sub);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (sub));
	g_assert_cmpint (lrg_menu_get_highlighted (sub), ==, 0);

	/* Left closes that level only */
	key (f, GRL_KEY_LEFT);
	g_assert_null (lrg_menu_get_open_submenu (menu));
	g_assert_true (lrg_menu_get_open (menu));
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (menu));
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 7);

	/* Enter on a submenu row opens it too; Enter in it activates */
	key (f, GRL_KEY_ENTER);
	sub = lrg_menu_get_open_submenu (menu);
	g_assert_nonnull (sub);
	key (f, GRL_KEY_DOWN);
	key (f, GRL_KEY_ENTER);
	g_assert_false (lrg_menu_get_open (menu));
	g_assert_false (lrg_menu_get_open (sub));
	g_assert_cmpuint (f->log->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (f->log, 0), ==, "pick:svg");
	settle (f);
}

static void
test_safe_triangle (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);
	LrgMenu           *sub;
	gfloat             x;
	gfloat             y;
	gfloat             sx;
	gfloat             mx;
	gfloat             mw;
	gfloat             rh;

	/* Open Export by hovering its row (pointer near the right side) */
	lrg_menu_get_entry_rect (menu, 7, &mx, &y, &mw, &rh);
	x = mx + mw - 20.0f;
	y += rh * 0.5f;
	move_to (f, x, y);
	frames (f, 20);
	sub = lrg_menu_get_open_submenu (menu);
	g_assert_nonnull (sub);
	sx = lrg_widget_get_world_x (LRG_WIDGET (sub));

	/* Head diagonally down-right towards the submenu: crossing the
	 * "Disabled" and "No action" rows does not steal the highlight */
	move_to (f, x + 6.0f, y + rh * 0.6f);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 7);
	g_assert_nonnull (lrg_menu_get_open_submenu (menu));
	move_to (f, x + 12.0f, y + rh * 1.2f);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 7);
	g_assert_nonnull (lrg_menu_get_open_submenu (menu));

	/* Arriving in the submenu keeps it; the pointer brings keys along */
	hover_row (f, sub, 1);
	g_assert_nonnull (lrg_menu_get_open_submenu (menu));
	g_assert_cmpint (lrg_menu_get_highlighted (sub), ==, 1);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 7);
	g_assert_cmpfloat (sx, >, x);

	/* A move away from the submenu (straight up, outside the
	 * triangle) takes the highlight at once */
	move_to (f, x, y);
	frames (f, 2);
	{
		gfloat ux;
		gfloat uy;

		row_center (menu, 1, &ux, &uy);
		move_to (f, ux - 40.0f, uy);
	}
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 1);

	/* Resting inside the triangle eventually hands over too */
	lrg_menu_close (menu);
	settle (f);
	lrg_menu_popup_at_widget (menu, f->button, LRG_POPUP_PLACEMENT_BELOW);
	move_to (f, x, y);
	frames (f, 20);
	g_assert_nonnull (lrg_menu_get_open_submenu (menu));
	move_to (f, x + 4.0f, y + rh * 0.9f);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 7);
	frames (f, 60);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, -1);
	g_assert_null (lrg_menu_get_open_submenu (menu));
	lrg_menu_close (menu);
	settle (f);
}

/* ==========================================================================
 * Activation
 * ========================================================================== */

static void
on_activated (LrgMenu     *menu,
              const gchar *action,
              gpointer     user_data)
{
	gchar **out = user_data;

	g_free (*out);
	*out = g_strdup (action);
}

static void
test_activation (Fixture       *f,
                 gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);
	g_autofree gchar  *seen = NULL;
	gfloat             x;
	gfloat             y;

	g_signal_connect (menu, "activated", G_CALLBACK (on_activated), &seen);

	/* Click "Pick B": the target reaches the action */
	row_center (menu, 5, &x, &y);
	click_at (f, x, y);
	g_assert_false (lrg_menu_get_open (menu));
	g_assert_cmpuint (f->log->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (f->log, 0), ==, "pick:b");
	g_assert_cmpstr (seen, ==, "test.pick");
	g_assert_false (lrg_widget_get_state_flags (f->button) & LRG_WIDGET_STATE_OPEN);
	settle (f);

	/* Clicking a disabled row does nothing and keeps the menu */
	lrg_menu_popup_at_widget (menu, f->button, LRG_POPUP_PLACEMENT_BELOW);
	frames (f, 1);
	row_center (menu, 8, &x, &y);
	click_at (f, x, y);
	g_assert_true (lrg_menu_get_open (menu));
	g_assert_cmpuint (f->log->len, ==, 1);
	g_assert_false (lrg_menu_activate_entry (menu, 9));

	/* The API activates like a click */
	g_assert_true (lrg_menu_activate_entry (menu, 11));
	g_assert_cmpstr (g_ptr_array_index (f->log, 1), ==, "quit");
	g_assert_false (lrg_menu_get_open (menu));
	settle (f);
}

/*
 * count_marks:
 *
 * Accent-coloured instances inside row @index's leading gutter: the
 * check stroke or the radio dot.
 */
static guint
count_marks (Fixture *f,
             LrgMenu *menu,
             guint    index)
{
	LrgPaintList   *list = lrg_canvas_record (f->canvas);
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (menu));
	const GrlColor *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	gfloat          rx;
	gfloat          ry;
	gfloat          rw;
	gfloat          rh;
	guint           n = 0;
	guint           i;

	lrg_menu_get_entry_rect (menu, index, &rx, &ry, &rw, &rh);
	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *in = lrg_paint_list_get_instance (list, i);
		gfloat                  cx = (in->rect[0] + in->rect[2]) * 0.5f;
		gfloat                  cy = (in->rect[1] + in->rect[3]) * 0.5f;

		if (in->color[0] == accent->r && in->color[1] == accent->g && in->color[2] == accent->b &&
		    cx >= rx && cx < rx + 40.0f && cy >= ry && cy < ry + rh)
			n++;
	}
	return n;
}

static void
test_check_and_radio (Fixture       *f,
                      gconstpointer  data)
{
	g_autoptr(GMenu)   model = view_model ();
	g_autoptr(LrgMenu) menu = lrg_menu_new_from_model (G_MENU_MODEL (model));
	GAction           *grid = g_action_map_lookup_action (G_ACTION_MAP (f->group), "grid");
	GAction           *mode = g_action_map_lookup_action (G_ACTION_MAP (f->group), "mode");
	g_autoptr(GVariant) st = NULL;

	lrg_menu_popup_at_widget (menu, f->button, LRG_POPUP_PLACEMENT_BELOW);
	settle (f);

	/* Grid is on: a check; Mode B is chosen: a dot on B only */
	g_assert_cmpuint (count_marks (f, menu, 0), >, 0);
	g_assert_cmpuint (count_marks (f, menu, 1), ==, 0);
	g_assert_cmpuint (count_marks (f, menu, 2), >, 0);

	/* State changes show on the next frame */
	g_action_change_state (grid, g_variant_new_boolean (FALSE));
	g_action_change_state (mode, g_variant_new_string ("a"));
	g_assert_cmpuint (count_marks (f, menu, 0), ==, 0);
	g_assert_cmpuint (count_marks (f, menu, 1), >, 0);
	g_assert_cmpuint (count_marks (f, menu, 2), ==, 0);

	/* Activating the check item toggles its boolean state */
	g_assert_true (lrg_menu_activate_entry (menu, 0));
	st = g_action_get_state (grid);
	g_assert_true (g_variant_get_boolean (st));
	settle (f);
}

static void
test_disabled_skip (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);

	/* From Export, Down skips Disabled, No action and the separator */
	lrg_menu_set_highlighted (menu, 7);
	key (f, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 11);

	/* Wrapping both ways */
	key (f, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 0);
	key (f, GRL_KEY_UP);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 11);

	/* Highlighting a disabled row clears instead */
	lrg_menu_set_highlighted (menu, 8);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, -1);

	/* Disabling an action at runtime takes it out of the walk */
	g_simple_action_set_enabled (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (f->group), "save")),
	                             FALSE);
	lrg_menu_set_highlighted (menu, 0);
	key (f, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 4);
	lrg_menu_close (menu);
	settle (f);
}

static void
test_typeahead (Fixture       *f,
                gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);

	/* Mnemonic letters first, cycling through repeats */
	type_char (f, 'q');
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 11);
	frames (f, 70);
	type_char (f, 'p');
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 4);
	frames (f, 70);
	type_char (f, 'p');
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 5);

	/* A quick prefix matches whole labels */
	frames (f, 70);
	type_char (f, 's');
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 1);
	lrg_menu_close (menu);
	settle (f);
}

/* ==========================================================================
 * Closing
 * ========================================================================== */

static void
count_closed (guint *counter)
{
	(*counter)++;
}

static void
test_escape_levels (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);
	LrgMenu           *sub;
	guint              closed = 0;

	g_signal_connect_swapped (menu, "closed", G_CALLBACK (count_closed), &closed);
	lrg_menu_set_highlighted (menu, 7);
	key (f, GRL_KEY_RIGHT);
	sub = lrg_menu_get_open_submenu (menu);
	g_assert_nonnull (sub);

	key (f, GRL_KEY_ESCAPE);
	g_assert_false (lrg_menu_get_open (sub));
	g_assert_true (lrg_menu_get_open (menu));
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (menu));

	key (f, GRL_KEY_ESCAPE);
	g_assert_false (lrg_menu_get_open (menu));
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_cmpuint (closed, ==, 1);

	/* The ghost fades out and is released */
	settle (f);
	g_assert_null (lrg_widget_get_parent (LRG_WIDGET (menu)));
}

static void
test_close_on_outside_click (Fixture       *f,
                             gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = open_file_menu (f);

	lrg_menu_set_highlighted (menu, 7);
	key (f, GRL_KEY_RIGHT);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 2);

	click_at (f, 700.0f, 500.0f);
	g_assert_false (lrg_menu_get_open (menu));
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_cmpuint (f->log->len, ==, 0);
	settle (f);
}

/* ==========================================================================
 * Menu bar
 * ========================================================================== */

static LrgMenuBar *
add_bar (Fixture *f)
{
	g_autoptr(GMenu) bar_model = g_menu_new ();
	g_autoptr(GMenu) file = file_model ();
	g_autoptr(GMenu) view = view_model ();
	g_autoptr(GMenu) help = g_menu_new ();
	LrgMenuBar      *bar;

	g_menu_append (help, "About", "test.open");
	g_menu_append_submenu (bar_model, "_File", G_MENU_MODEL (file));
	g_menu_append_submenu (bar_model, "_View", G_MENU_MODEL (view));
	g_menu_append_submenu (bar_model, "_Help", G_MENU_MODEL (help));
	g_menu_append (bar_model, "Run", "test.save");

	bar = lrg_menu_bar_new_from_model (G_MENU_MODEL (bar_model));
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (bar));
	g_object_unref (bar);
	lrg_widget_allocate (LRG_WIDGET (bar), 0.0f, 0.0f, 800.0f, 30.0f);
	lrg_canvas_record (f->canvas);
	return bar;
}

static void
title_center (LrgMenuBar *bar,
              guint       index,
              gfloat     *x,
              gfloat     *y)
{
	gfloat tx;
	gfloat ty;
	gfloat tw;
	gfloat th;

	g_assert_true (lrg_menu_bar_get_title_rect (bar, index, &tx, &ty, &tw, &th));
	*x = tx + tw * 0.5f;
	*y = ty + th * 0.5f;
}

static void
test_bar_hover_switch (Fixture       *f,
                       gconstpointer  data)
{
	LrgMenuBar *bar = add_bar (f);
	gfloat      x;
	gfloat      y;
	gfloat      tx;

	g_assert_cmpuint (lrg_menu_bar_get_n_titles (bar), ==, 4);
	g_assert_cmpstr (lrg_menu_bar_get_title (bar, 0), ==, "File");
	g_assert_null (lrg_menu_bar_get_menu (bar, 3));

	/* Hovering alone opens nothing */
	title_center (bar, 0, &x, &y);
	move_to (f, x, y);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, -1);
	g_assert_cmpint (lrg_menu_bar_get_highlighted (bar), ==, 0);

	/* Click opens File below its title */
	click_at (f, x, y);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, 0);
	g_assert_true (lrg_menu_get_open (lrg_menu_bar_get_menu (bar, 0)));
	lrg_menu_bar_get_title_rect (bar, 0, &tx, NULL, NULL, NULL);
	g_assert_cmpfloat (lrg_widget_get_world_x (LRG_WIDGET (lrg_menu_bar_get_menu (bar, 0))), ==, tx);
	g_assert_cmpfloat (lrg_widget_get_world_y (LRG_WIDGET (lrg_menu_bar_get_menu (bar, 0))), >=, 30.0f);

	/* While open, hovering View switches to it */
	title_center (bar, 1, &x, &y);
	move_to (f, x, y);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, 1);
	g_assert_false (lrg_menu_get_open (lrg_menu_bar_get_menu (bar, 0)));
	g_assert_true (lrg_menu_get_open (lrg_menu_bar_get_menu (bar, 1)));
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);

	/* Clicking the open title closes it, and stays closed */
	click_at (f, x, y);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, -1);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	frames (f, 2);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, -1);

	/* Closed: hovering another title does not open it */
	title_center (bar, 2, &x, &y);
	move_to (f, x, y);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, -1);

	/* The plain "Run" title runs its action */
	title_center (bar, 3, &x, &y);
	click_at (f, x, y);
	g_assert_cmpuint (f->log->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (f->log, 0), ==, "save");
	settle (f);
}

static void
test_bar_arrows (Fixture       *f,
                 gconstpointer  data)
{
	LrgMenuBar *bar = add_bar (f);
	gfloat      x;
	gfloat      y;

	title_center (bar, 0, &x, &y);
	click_at (f, x, y);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, 0);

	/* Right in the open menu (no submenu row) moves to View, with its
	 * first row highlighted */
	key (f, GRL_KEY_RIGHT);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, 1);
	g_assert_cmpint (lrg_menu_get_highlighted (lrg_menu_bar_get_menu (bar, 1)), ==, 0);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (lrg_menu_bar_get_menu (bar, 1)));

	/* Left back, then Left wraps to the last title (Run: plain button,
	 * runs its action) */
	key (f, GRL_KEY_LEFT);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, 0);

	/* Escape closes the menu and returns focus to the bar */
	key (f, GRL_KEY_ESCAPE);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, -1);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (bar));

	/* Bar keyboard navigation (F10's job) */
	lrg_menu_bar_focus_titles (bar);
	g_assert_cmpint (lrg_menu_bar_get_highlighted (bar), ==, 0);
	key (f, GRL_KEY_RIGHT);
	g_assert_cmpint (lrg_menu_bar_get_highlighted (bar), ==, 1);
	key (f, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, 1);
	key (f, GRL_KEY_ESCAPE);
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, -1);

	/* Typing a title's mnemonic opens it */
	type_char (f, 'h');
	g_assert_cmpint (lrg_menu_bar_get_open_index (bar), ==, 2);
	lrg_menu_bar_close (bar);
	settle (f);
}

static void
test_bar_drag_release (Fixture       *f,
                       gconstpointer  data)
{
	LrgMenuBar *bar = add_bar (f);
	guint       left = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	LrgMenu    *menu;
	gfloat      x;
	gfloat      y;
	gfloat      rx;
	gfloat      ry;
	LrgCanvasInput input;

	/* Press on File, drag onto Save, release: one gesture */
	title_center (bar, 0, &x, &y);
	move_to (f, x, y);
	button_frame (f, x, y, left, TRUE);
	menu = lrg_menu_bar_get_menu (bar, 0);
	g_assert_true (lrg_menu_get_open (menu));

	row_center (menu, 1, &rx, &ry);
	f->px = rx;
	f->py = ry;
	frame (f, &input);
	input.buttons_down = left;
	lrg_canvas_process_input (f->canvas, &input);
	g_assert_cmpint (lrg_menu_get_highlighted (menu), ==, 1);

	button_frame (f, rx, ry, left, FALSE);
	g_assert_false (lrg_menu_get_open (menu));
	g_assert_cmpuint (f->log->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (f->log, 0), ==, "save");
	settle (f);
}

/* ==========================================================================
 * Right to left
 * ========================================================================== */

static void
test_rtl (Fixture       *f,
          gconstpointer  data)
{
	g_autoptr(LrgMenu) menu = NULL;
	LrgMenuBar        *bar;
	LrgMenu           *sub;
	gfloat             tx;
	gfloat             tw;

	lrg_canvas_set_default_direction (f->canvas, LRG_WIDGET_DIRECTION_RTL);
	lrg_widget_allocate (f->button, 600.0f, 40.0f, 100.0f, 30.0f);
	menu = open_file_menu (f);

	/* Below the button, right edges aligned */
	g_assert_cmpfloat (fabsf (lrg_widget_get_world_x (LRG_WIDGET (menu)) +
	                          lrg_widget_get_width (LRG_WIDGET (menu)) - 700.0f), <, 1.0f);

	/* Left opens submenus (forward), and they open on the left */
	lrg_menu_set_highlighted (menu, 7);
	key (f, GRL_KEY_RIGHT);
	g_assert_null (lrg_menu_get_open_submenu (menu));
	key (f, GRL_KEY_LEFT);
	sub = lrg_menu_get_open_submenu (menu);
	g_assert_nonnull (sub);
	g_assert_cmpfloat (lrg_widget_get_world_x (LRG_WIDGET (sub)) + lrg_widget_get_width (LRG_WIDGET (sub)), <=,
	                   lrg_widget_get_world_x (LRG_WIDGET (menu)));
	key (f, GRL_KEY_RIGHT);
	g_assert_null (lrg_menu_get_open_submenu (menu));
	lrg_menu_close (menu);
	settle (f);

	/* Bar titles start from the right edge */
	bar = add_bar (f);
	lrg_menu_bar_get_title_rect (bar, 0, &tx, NULL, &tw, NULL);
	g_assert_cmpfloat (tx + tw, >, 780.0f);
	lrg_menu_bar_get_title_rect (bar, 1, &tx, NULL, NULL, NULL);
	g_assert_cmpfloat (tx + 1.0f, <, 780.0f - tw);
}

/* ==========================================================================
 * Themes
 * ========================================================================== */

static void
test_all_presets (Fixture       *f,
                  gconstpointer  data)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                i;

	for (i = 0; presets[i] != NULL; i++)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (presets[i]);
		g_autoptr(LrgMenu)  menu = NULL;
		LrgPaintList       *list;

		lrg_widget_set_theme (LRG_WIDGET (f->canvas), theme);
		move_to (f, 700.0f, 500.0f);
		menu = open_file_menu (f);
		hover_row (f, menu, 7);
		frames (f, 20);
		settle (f);
		list = lrg_canvas_record (f->canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 10);
		g_assert_nonnull (lrg_menu_get_open_submenu (menu));
		lrg_menu_close (menu);
		settle (f);
	}
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define GPU_W (400)
#define GPU_H (300)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-ui-menus-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

static Image
render_canvas (LrgCanvas *canvas)
{
	RenderTexture2D target = LoadRenderTexture (GPU_W, GPU_H);
	Image           image;

	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	lrg_canvas_render (canvas);
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);
	return image;
}

static gboolean
pixel_near (Image          *image,
            gint            x,
            gint            y,
            const GrlColor *c,
            gint            tol)
{
	Color p = GetImageColor (*image, x, y);

	return ABS ((gint) p.r - (gint) c->r) <= tol &&
	       ABS ((gint) p.g - (gint) c->g) <= tol &&
	       ABS ((gint) p.b - (gint) c->b) <= tol;
}

/* GPU: the highlight row shows CONTROL_HOVER, its neighbour the POPUP fill */
static void
test_gpu_menu (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                i;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	for (i = 0; presets[i] != NULL; i++)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (presets[i]);
		g_autoptr(GMenu)    model = file_model ();
		g_autoptr(LrgMenu)  menu = lrg_menu_new_from_model (G_MENU_MODEL (model));
		Fixture             f;
		gfloat              rx;
		gfloat              ry;
		gfloat              rw;
		gfloat              rh;
		Image               img;

		memset (&f, 0, sizeof (Fixture));
		fixture_setup (&f, NULL);
		lrg_widget_set_size (LRG_WIDGET (f.canvas), (gfloat) GPU_W, (gfloat) GPU_H);
		lrg_widget_set_theme (LRG_WIDGET (f.canvas), theme);

		lrg_menu_popup_at_widget (menu, f.button, LRG_POPUP_PLACEMENT_BELOW);
		settle (&f);
		hover_row (&f, menu, 7);
		frames (&f, 20);
		hover_row (&f, menu, 4);
		settle (&f);

		lrg_menu_get_entry_rect (menu, 4, &rx, &ry, &rw, &rh);
		img = render_canvas (f.canvas);
		if (g_getenv ("MENU_SHOT_DIR") != NULL)
		{
			g_autofree gchar *path = g_strdup_printf ("%s/menu-%s.png", g_getenv ("MENU_SHOT_DIR"), presets[i]);

			ExportImage (img, path);
		}
		g_assert_true (pixel_near (&img, (gint) (rx + rw - 12.0f), (gint) (ry + rh * 0.5f),
		                           lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER), 6));
		g_assert_true (pixel_near (&img, (gint) (rx + rw - 12.0f), (gint) (ry + rh * 1.5f),
		                           lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP), 6));
		UnloadImage (img);

		lrg_menu_close (menu);
		settle (&f);
		fixture_teardown (&f, NULL);
	}
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/ui/menu/build-from-gmenu", test_build_from_gmenu);

#define ADD(path, fn) g_test_add (path, Fixture, NULL, fixture_setup, fn, fixture_teardown)
	ADD ("/ui/menu/enabled-needs-action", test_enabled_needs_action);
	ADD ("/ui/menu/popup-placement", test_popup_placement);
	ADD ("/ui/menu/context-release", test_context_release_does_not_activate);
	ADD ("/ui/menu/context-drag-release", test_context_drag_release);
	ADD ("/ui/menu/accel-from-shortcut", test_accel_from_shortcut);
	ADD ("/ui/menu/hover-glide", test_hover_glide);
	ADD ("/ui/menu/submenu-delay", test_submenu_delay);
	ADD ("/ui/menu/submenu-keyboard", test_submenu_keyboard);
	ADD ("/ui/menu/safe-triangle", test_safe_triangle);
	ADD ("/ui/menu/activation", test_activation);
	ADD ("/ui/menu/check-and-radio", test_check_and_radio);
	ADD ("/ui/menu/disabled-skip", test_disabled_skip);
	ADD ("/ui/menu/typeahead", test_typeahead);
	ADD ("/ui/menu/escape-levels", test_escape_levels);
	ADD ("/ui/menu/close-on-outside-click", test_close_on_outside_click);
	ADD ("/ui/menu-bar/hover-switch", test_bar_hover_switch);
	ADD ("/ui/menu-bar/arrows", test_bar_arrows);
	ADD ("/ui/menu-bar/drag-release", test_bar_drag_release);
	ADD ("/ui/menu/rtl", test_rtl);
	ADD ("/ui/menu/all-presets", test_all_presets);
#undef ADD
	g_test_add_func ("/ui/gpu/menu", test_gpu_menu);

	{
		gint result = g_test_run ();

		g_clear_object (&test_window);
		return result;
	}
}
