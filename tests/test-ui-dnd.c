/* test-ui-dnd.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Drag and drop: LrgDragPayload, LrgDragSource, LrgDropTarget and the
 * canvas session that runs between them.
 *
 * Headless: widgets sit on a canvas at fixed rects and every case feeds
 * LrgCanvasInput frames through lrg_canvas_process_input(), exactly as
 * the real input loop does. Positions come from widget rects; time is
 * stepped at 60 Hz through the frames' delta_time.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define DT (1.0f / 60.0f)

#define LEFT (LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT))

#define NODE_TYPE "application/x-lrg-node"
#define ASSET_TYPE "application/x-lrg-asset"

/* ==========================================================================
 * Fixture and input helpers
 * ========================================================================== */

typedef struct
{
	LrgCanvas *canvas;
	GString   *log;            /* "name:event " entries in order */
	gint       prepare_count;
	gint       begin_count;
	gint       end_count;
	gboolean   end_dropped;
	LrgDragAction end_action;
	gboolean   drop_result;    /* what drop handlers return */
} Fixture;

static void
fixture_set_up (Fixture       *f,
                gconstpointer  user_data)
{
	f->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (f->canvas), 800.0f, 600.0f);
	f->log = g_string_new (NULL);
	f->drop_result = TRUE;
}

static void
fixture_tear_down (Fixture       *f,
                   gconstpointer  user_data)
{
	g_clear_object (&f->canvas);
	g_string_free (f->log, TRUE);
}

/* frame: one input frame with the pointer at (@x, @y) */
static void
frame (Fixture           *f,
       gfloat             x,
       gfloat             y,
       guint              down,
       guint              pressed,
       guint              released,
       LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = x;
	input.pointer_y = y;
	input.buttons_down = down;
	input.buttons_pressed = pressed;
	input.buttons_released = released;
	input.modifiers = mods;
	input.delta_time = DT;
	lrg_canvas_process_input (f->canvas, &input);
}

/* move_to: hover (no button) */
static void
move_to (Fixture *f,
         gfloat   x,
         gfloat   y)
{
	frame (f, x, y, 0, 0, 0, LRG_INPUT_MODIFIER_NONE);
}

/* press_at: hover there, then press the left button */
static void
press_at (Fixture *f,
          gfloat   x,
          gfloat   y)
{
	move_to (f, x, y);
	frame (f, x, y, LEFT, LEFT, 0, LRG_INPUT_MODIFIER_NONE);
}

/* drag_to: move with the left button held */
static void
drag_to (Fixture *f,
         gfloat   x,
         gfloat   y)
{
	frame (f, x, y, LEFT, 0, 0, LRG_INPUT_MODIFIER_NONE);
}

/* release_at: let go of the left button */
static void
release_at (Fixture *f,
            gfloat   x,
            gfloat   y)
{
	frame (f, x, y, 0, 0, LEFT, LRG_INPUT_MODIFIER_NONE);
}

/* key: press (and release) @key with nothing else happening */
static void
key (Fixture *f,
     gfloat   x,
     gfloat   y,
     GrlKey   k)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = x;
	input.pointer_y = y;
	input.buttons_down = LEFT;
	input.delta_time = DT;
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (f->canvas, &input);
}

/* centre of a widget in canvas coordinates */
static gfloat
cx (gpointer w)
{
	return lrg_widget_get_world_x (LRG_WIDGET (w)) + lrg_widget_get_width (LRG_WIDGET (w)) * 0.5f;
}

static gfloat
cy (gpointer w)
{
	return lrg_widget_get_world_y (LRG_WIDGET (w)) + lrg_widget_get_height (LRG_WIDGET (w)) * 0.5f;
}

/* add_panel: a named panel at a fixed rect inside @parent */
static LrgPanel *
add_panel (gpointer     parent,
           const gchar *name,
           gfloat       x,
           gfloat       y,
           gfloat       w,
           gfloat       h)
{
	LrgPanel *panel = lrg_panel_new ();

	lrg_widget_set_name (LRG_WIDGET (panel), name);
	lrg_widget_set_position (LRG_WIDGET (panel), x, y);
	lrg_widget_set_size (LRG_WIDGET (panel), w, h);
	lrg_container_add_child (LRG_CONTAINER (parent), LRG_WIDGET (panel));
	g_object_unref (panel);   /* the parent holds it */
	return panel;
}

/* ==========================================================================
 * Signal loggers
 * ========================================================================== */

/* target_name: the name of the widget a target was made for (kept on
 * the target, so it still answers after a detach) */
static const gchar *
target_name (LrgDropTarget *target)
{
	return g_object_get_data (G_OBJECT (target), "test-name");
}

static LrgDragPayload *
on_prepare_node (LrgDragSource *source,
                 gfloat         x,
                 gfloat         y,
                 Fixture       *f)
{
	LrgDragPayload *p = lrg_drag_payload_new_string (NODE_TYPE, "Player", LRG_DRAG_ACTION_ALL);

	f->prepare_count++;
	lrg_drag_payload_set_icon (p, LRG_ICON_FILE);
	return p;
}

static LrgDragPayload *
on_prepare_null (LrgDragSource *source,
                 gfloat         x,
                 gfloat         y,
                 Fixture       *f)
{
	f->prepare_count++;
	return NULL;
}

static void
on_drag_begin (LrgDragSource  *source,
               LrgDragPayload *payload,
               Fixture        *f)
{
	f->begin_count++;
	g_string_append (f->log, "begin ");
}

static void
on_drag_end (LrgDragSource  *source,
             LrgDragPayload *payload,
             gboolean        dropped,
             LrgDragAction   action,
             Fixture        *f)
{
	f->end_count++;
	f->end_dropped = dropped;
	f->end_action = action;
	g_string_append (f->log, "end ");
}

static void
on_enter (LrgDropTarget  *target,
          LrgDragPayload *payload,
          gfloat          x,
          gfloat          y,
          Fixture        *f)
{
	g_string_append_printf (f->log, "%s:enter ",
	                        target_name (target));
	g_assert_true (lrg_drop_target_get_current_drop (target) == payload);
}

static LrgDragAction
on_motion (LrgDropTarget  *target,
           LrgDragPayload *payload,
           gfloat          x,
           gfloat          y,
           Fixture        *f)
{
	g_string_append_printf (f->log, "%s:motion ",
	                        target_name (target));
	return lrg_drop_target_get_proposed_action (target);
}

static void
on_leave (LrgDropTarget *target,
          Fixture       *f)
{
	g_string_append_printf (f->log, "%s:leave ",
	                        target_name (target));
}

static gboolean
on_drop (LrgDropTarget  *target,
         LrgDragPayload *payload,
         gfloat          x,
         gfloat          y,
         Fixture        *f)
{
	g_string_append_printf (f->log, "%s:drop ",
	                        target_name (target));
	return f->drop_result;
}

static gboolean
on_accept_veto (LrgDropTarget  *target,
                LrgDragPayload *payload,
                Fixture        *f)
{
	g_string_append_printf (f->log, "%s:veto ",
	                        target_name (target));
	return FALSE;
}

/* make_source: a node source on @w, logging into @f */
static LrgDragSource *
make_source (Fixture  *f,
             gpointer  w)
{
	LrgDragSource *source = lrg_drag_source_new ();

	g_signal_connect (source, "prepare", G_CALLBACK (on_prepare_node), f);
	g_signal_connect (source, "drag-begin", G_CALLBACK (on_drag_begin), f);
	g_signal_connect (source, "drag-end", G_CALLBACK (on_drag_end), f);
	lrg_widget_add_drag_source (LRG_WIDGET (w), source);
	return source;
}

/* make_target: a logging drop target on @w */
static LrgDropTarget *
make_target (Fixture       *f,
             gpointer       w,
             const gchar   *type,
             LrgDragAction  actions)
{
	const gchar   *types[] = { type, NULL };
	LrgDropTarget *target = lrg_drop_target_new (type != NULL ? types : NULL, actions);

	g_object_set_data_full (G_OBJECT (target), "test-name",
	                        g_strdup (lrg_widget_get_name (LRG_WIDGET (w))), g_free);
	g_signal_connect (target, "enter", G_CALLBACK (on_enter), f);
	g_signal_connect (target, "motion", G_CALLBACK (on_motion), f);
	g_signal_connect (target, "leave", G_CALLBACK (on_leave), f);
	g_signal_connect (target, "drop", G_CALLBACK (on_drop), f);
	lrg_widget_add_drop_target (LRG_WIDGET (w), target);
	return target;
}

/* ==========================================================================
 * Payload and actions
 * ========================================================================== */

static gint destroyed = 0;

static void
count_destroy (gpointer data)
{
	destroyed++;
}

static void
test_payload_basics (void)
{
	g_autoptr(LrgDragPayload) s = NULL;
	g_autoptr(LrgDragPayload) o = NULL;
	g_autoptr(GObject)        obj = g_object_new (G_TYPE_OBJECT, NULL);
	LrgDragPayload           *p;
	GValue                    v = G_VALUE_INIT;
	static gint               slot = 7;

	/* String payload: value, label default, wildcard matching */
	s = lrg_drag_payload_new_string (ASSET_TYPE, "sprites/hero.png", LRG_DRAG_ACTION_COPY);
	g_assert_cmpstr (lrg_drag_payload_get_content_type (s), ==, ASSET_TYPE);
	g_assert_cmpstr (lrg_drag_payload_get_string (s), ==, "sprites/hero.png");
	g_assert_cmpstr (lrg_drag_payload_get_label (s), ==, "sprites/hero.png");
	g_assert_null (lrg_drag_payload_get_object (s));
	g_assert_true (lrg_drag_payload_matches (s, ASSET_TYPE));
	g_assert_true (lrg_drag_payload_matches (s, "application/*"));
	g_assert_true (lrg_drag_payload_matches (s, "*"));
	g_assert_false (lrg_drag_payload_matches (s, "text/*"));
	g_assert_false (lrg_drag_payload_matches (s, NODE_TYPE));
	g_assert_cmpint (lrg_drag_payload_get_actions (s), ==, LRG_DRAG_ACTION_COPY);
	g_assert_cmpint (lrg_drag_payload_get_icon (s), ==, LRG_ICON_NONE);

	/* Object payload holds a reference */
	o = lrg_drag_payload_new_object (NODE_TYPE, obj, LRG_DRAG_ACTION_ALL);
	g_assert_true (lrg_drag_payload_get_object (o) == (gpointer) obj);
	g_assert_null (lrg_drag_payload_get_string (o));
	g_assert_cmpint (G_OBJECT (obj)->ref_count, ==, 2);

	/* Generic GValue payload is copied */
	g_value_init (&v, G_TYPE_INT);
	g_value_set_int (&v, 42);
	p = lrg_drag_payload_new ("application/x-number", &v, LRG_DRAG_ACTION_MOVE);
	g_value_unset (&v);
	g_assert_cmpint (g_value_get_int (lrg_drag_payload_get_value (p)), ==, 42);
	lrg_drag_payload_unref (p);

	/* Pointer payload: destroy notify on the last unref, boxed copy refs */
	destroyed = 0;
	p = lrg_drag_payload_new_pointer ("application/x-slot", &slot, count_destroy, LRG_DRAG_ACTION_MOVE);
	g_assert_true (lrg_drag_payload_get_pointer (p) == &slot);
	{
		LrgDragPayload *copy = g_boxed_copy (LRG_TYPE_DRAG_PAYLOAD, p);

		g_assert_true (copy == p);
		g_boxed_free (LRG_TYPE_DRAG_PAYLOAD, copy);
	}
	g_assert_cmpint (destroyed, ==, 0);
	lrg_drag_payload_unref (p);
	g_assert_cmpint (destroyed, ==, 1);

	/* Source is weak */
	{
		LrgPanel *w = lrg_panel_new ();

		lrg_drag_payload_set_source (s, LRG_WIDGET (w));
		g_assert_true (lrg_drag_payload_get_source (s) == LRG_WIDGET (w));
		g_object_unref (w);
		g_assert_null (lrg_drag_payload_get_source (s));
	}

	g_assert_true (G_TYPE_IS_FLAGS (LRG_TYPE_DRAG_ACTION));
}

static void
test_action_from_modifiers (void)
{
	LrgDragAction all = LRG_DRAG_ACTION_ALL;

	g_assert_cmpint (lrg_drag_action_from_modifiers (0, all, 0), ==, LRG_DRAG_ACTION_MOVE);
	g_assert_cmpint (lrg_drag_action_from_modifiers (LRG_INPUT_MODIFIER_CTRL, all, 0), ==,
	                 LRG_DRAG_ACTION_COPY);
	g_assert_cmpint (lrg_drag_action_from_modifiers (LRG_INPUT_MODIFIER_SHIFT, all, 0), ==,
	                 LRG_DRAG_ACTION_MOVE);
	g_assert_cmpint (lrg_drag_action_from_modifiers (LRG_INPUT_MODIFIER_ALT, all, 0), ==,
	                 LRG_DRAG_ACTION_LINK);
	g_assert_cmpint (lrg_drag_action_from_modifiers (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT,
	                                                 all, 0), ==, LRG_DRAG_ACTION_LINK);
	g_assert_cmpint (lrg_drag_action_from_modifiers (0, all, LRG_DRAG_ACTION_LINK), ==,
	                 LRG_DRAG_ACTION_LINK);
	/* A request outside @allowed falls back to the default order */
	g_assert_cmpint (lrg_drag_action_from_modifiers (LRG_INPUT_MODIFIER_CTRL, LRG_DRAG_ACTION_MOVE, 0),
	                 ==, LRG_DRAG_ACTION_MOVE);
	g_assert_cmpint (lrg_drag_action_from_modifiers (0, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_LINK, 0),
	                 ==, LRG_DRAG_ACTION_COPY);
	g_assert_cmpint (lrg_drag_action_from_modifiers (LRG_INPUT_MODIFIER_CTRL, LRG_DRAG_ACTION_NONE, 0),
	                 ==, LRG_DRAG_ACTION_NONE);
}

static void
test_attach_detach (void)
{
	g_autoptr(LrgPanel) w = lrg_panel_new ();
	LrgDragSource      *src = lrg_drag_source_new ();
	LrgDropTarget      *dst = lrg_drop_target_new (NULL, LRG_DRAG_ACTION_COPY);
	GList              *l;

	g_assert_cmpfloat (lrg_drag_source_get_threshold (src), ==, LRG_DRAG_DEFAULT_THRESHOLD);
	g_assert_cmpint (lrg_drag_source_get_button (src), ==, GRL_MOUSE_BUTTON_LEFT);
	g_assert_true (lrg_drop_target_get_paint_highlight (dst));
	g_assert_null (lrg_drop_target_get_types (dst));

	lrg_widget_add_drag_source (LRG_WIDGET (w), src);
	lrg_widget_add_drop_target (LRG_WIDGET (w), dst);
	g_assert_true (lrg_drag_source_get_widget (src) == LRG_WIDGET (w));
	g_assert_true (lrg_drop_target_get_widget (dst) == LRG_WIDGET (w));

	l = lrg_widget_list_drag_sources (LRG_WIDGET (w));
	g_assert_cmpuint (g_list_length (l), ==, 1);
	g_list_free (l);
	l = lrg_widget_list_drop_targets (LRG_WIDGET (w));
	g_assert_cmpuint (g_list_length (l), ==, 1);
	g_list_free (l);

	g_object_ref (src);
	lrg_widget_remove_drag_source (LRG_WIDGET (w), src);
	g_assert_null (lrg_drag_source_get_widget (src));
	g_assert_null (lrg_widget_list_drag_sources (LRG_WIDGET (w)));
	g_object_unref (src);
	lrg_widget_remove_drop_target (LRG_WIDGET (w), dst);
	g_assert_null (lrg_widget_list_drop_targets (LRG_WIDGET (w)));
}

/* ==========================================================================
 * Session
 * ========================================================================== */

static void
test_threshold (Fixture       *f,
                gconstpointer  user_data)
{
	LrgPanel *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	gfloat    x;
	gfloat    y;

	make_source (f, src);
	lrg_canvas_record (f->canvas);
	x = cx (src);
	y = cy (src);

	press_at (f, x, y);
	drag_to (f, x + 3.0f, y);          /* below the 4 px threshold */
	g_assert_cmpint (f->prepare_count, ==, 0);
	g_assert_false (lrg_canvas_is_dragging (f->canvas));

	drag_to (f, x + 6.0f, y);          /* past it */
	g_assert_cmpint (f->prepare_count, ==, 1);
	g_assert_cmpint (f->begin_count, ==, 1);
	g_assert_true (lrg_canvas_is_dragging (f->canvas));
	g_assert_cmpstr (lrg_drag_payload_get_content_type (lrg_canvas_get_drag_payload (f->canvas)),
	                 ==, NODE_TYPE);
	/* The canvas recorded the source widget on the payload */
	g_assert_true (lrg_drag_payload_get_source (lrg_canvas_get_drag_payload (f->canvas))
	               == LRG_WIDGET (src));

	release_at (f, x + 6.0f, y);
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	g_assert_cmpint (f->end_count, ==, 1);
	g_assert_false (f->end_dropped);
	g_assert_null (lrg_canvas_get_drag_payload (f->canvas));
}

static void
test_threshold_custom (Fixture       *f,
                       gconstpointer  user_data)
{
	LrgPanel      *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgDragSource *s = make_source (f, src);

	lrg_drag_source_set_threshold (s, 20.0f);
	lrg_canvas_record (f->canvas);
	press_at (f, cx (src), cy (src));
	drag_to (f, cx (src) + 10.0f, cy (src));
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	drag_to (f, cx (src) + 25.0f, cy (src));
	g_assert_true (lrg_canvas_is_dragging (f->canvas));
	release_at (f, cx (src) + 25.0f, cy (src));
}

static void
test_prepare_null (Fixture       *f,
                   gconstpointer  user_data)
{
	LrgPanel                 *outer = add_panel (f->canvas, "outer", 20, 20, 300, 200);
	LrgPanel                 *inner = add_panel (outer, "inner", 10, 10, 100, 40);
	LrgDragSource            *s = lrg_drag_source_new ();
	LrgDragSource            *os;
	g_autoptr(LrgDragPayload) fixed = lrg_drag_payload_new_string ("text/plain", "outer", LRG_DRAG_ACTION_COPY);

	g_signal_connect (s, "prepare", G_CALLBACK (on_prepare_null), f);
	lrg_widget_add_drag_source (LRG_WIDGET (inner), s);
	lrg_canvas_record (f->canvas);

	/* Alone, a NULL payload leaves the press an ordinary press */
	press_at (f, cx (inner), cy (inner));
	drag_to (f, cx (inner) + 10.0f, cy (inner));
	drag_to (f, cx (inner) + 20.0f, cy (inner));
	g_assert_cmpint (f->prepare_count, ==, 1);   /* asked once per press */
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	release_at (f, cx (inner) + 20.0f, cy (inner));

	/* An outer source with a fixed payload gets its turn */
	os = lrg_drag_source_new ();
	lrg_drag_source_set_payload (os, fixed);
	lrg_widget_add_drag_source (LRG_WIDGET (outer), os);
	press_at (f, cx (inner), cy (inner));
	drag_to (f, cx (inner) + 10.0f, cy (inner));
	g_assert_cmpint (f->prepare_count, ==, 2);
	g_assert_true (lrg_canvas_is_dragging (f->canvas));
	g_assert_true (lrg_canvas_get_drag_payload (f->canvas) == fixed);
	g_assert_true (lrg_drag_source_get_dragging (os));
	release_at (f, cx (inner) + 10.0f, cy (inner));
	g_assert_false (lrg_drag_source_get_dragging (os));
}

static void
test_type_matching (Fixture       *f,
                    gconstpointer  user_data)
{
	LrgPanel      *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel      *text = add_panel (f->canvas, "text", 200, 20, 100, 100);
	LrgPanel      *any_app = add_panel (f->canvas, "app", 400, 20, 100, 100);
	LrgPanel      *veto = add_panel (f->canvas, "veto", 600, 20, 100, 100);
	LrgPanel      *copy_only = add_panel (f->canvas, "copyonly", 200, 300, 100, 100);
	LrgDropTarget *vt;
	LrgDragSource *s;

	s = make_source (f, src);
	make_target (f, text, "text/plain", LRG_DRAG_ACTION_ALL);
	make_target (f, any_app, "application/*", LRG_DRAG_ACTION_ALL);
	vt = make_target (f, veto, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	g_signal_connect (vt, "accept", G_CALLBACK (on_accept_veto), f);
	make_target (f, copy_only, NODE_TYPE, LRG_DRAG_ACTION_COPY);
	lrg_canvas_record (f->canvas);

	press_at (f, cx (src), cy (src));
	drag_to (f, cx (src) + 10.0f, cy (src));
	g_string_truncate (f->log, 0);

	/* Wrong type: nothing */
	drag_to (f, cx (text), cy (text));
	g_assert_null (lrg_canvas_get_drop_target (f->canvas));
	g_assert_cmpstr (f->log->str, ==, "");

	/* Wildcard type matches */
	drag_to (f, cx (any_app), cy (any_app));
	g_assert_nonnull (lrg_canvas_get_drop_target (f->canvas));
	g_assert_cmpstr (f->log->str, ==, "app:enter app:motion ");

	/* accept veto */
	g_string_truncate (f->log, 0);
	drag_to (f, cx (veto), cy (veto));
	g_assert_cmpstr (f->log->str, ==, "veto:veto app:leave ");
	g_assert_null (lrg_canvas_get_drop_target (f->canvas));
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_NONE);
	release_at (f, cx (veto), cy (veto));
	g_assert_false (f->end_dropped);

	/* No common action: a COPY-only target refuses a MOVE-only payload */
	lrg_widget_remove_drag_source (LRG_WIDGET (src), s);
	{
		LrgDragSource            *ms = lrg_drag_source_new ();
		g_autoptr(LrgDragPayload) mp = lrg_drag_payload_new_string (NODE_TYPE, "n", LRG_DRAG_ACTION_MOVE);

		lrg_drag_source_set_payload (ms, mp);
		lrg_widget_add_drag_source (LRG_WIDGET (src), ms);
	}
	press_at (f, cx (src), cy (src));
	drag_to (f, cx (src) + 10.0f, cy (src));
	g_string_truncate (f->log, 0);
	drag_to (f, cx (copy_only), cy (copy_only));
	g_assert_null (lrg_canvas_get_drop_target (f->canvas));
	g_assert_cmpstr (f->log->str, ==, "");
	release_at (f, cx (copy_only), cy (copy_only));
}

static void
test_event_order (Fixture       *f,
                  gconstpointer  user_data)
{
	LrgPanel *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel *a = add_panel (f->canvas, "a", 200, 20, 100, 100);
	LrgPanel *b = add_panel (f->canvas, "b", 400, 20, 100, 100);

	make_source (f, src);
	make_target (f, a, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	make_target (f, b, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	lrg_canvas_record (f->canvas);

	press_at (f, cx (src), cy (src));
	drag_to (f, cx (src) + 10.0f, cy (src));
	drag_to (f, cx (a), cy (a));
	drag_to (f, cx (a) + 5.0f, cy (a));
	drag_to (f, cx (b), cy (b));
	release_at (f, cx (b), cy (b));

	g_assert_cmpstr (f->log->str, ==,
	                 "begin a:enter a:motion a:motion a:leave b:enter b:motion b:drop b:leave end ");
	g_assert_true (f->end_dropped);
	g_assert_cmpint (f->end_action, ==, LRG_DRAG_ACTION_MOVE);
}

static void
test_drop_result (Fixture       *f,
                  gconstpointer  user_data)
{
	LrgPanel      *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel      *a = add_panel (f->canvas, "a", 200, 20, 100, 100);
	LrgDropTarget *t;
	gint           canvas_end = 0;

	make_source (f, src);
	t = make_target (f, a, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	g_signal_connect_swapped (f->canvas, "drag-end", G_CALLBACK (g_atomic_int_inc), &canvas_end);
	lrg_canvas_record (f->canvas);

	/* Accepted drop */
	press_at (f, cx (src), cy (src));
	drag_to (f, cx (a), cy (a));
	g_assert_true (lrg_drop_target_get_current_drop (t) == lrg_canvas_get_drag_payload (f->canvas));
	g_assert_cmpint (lrg_drop_target_get_current_action (t), ==, LRG_DRAG_ACTION_MOVE);
	release_at (f, cx (a), cy (a));
	g_assert_true (f->end_dropped);
	g_assert_cmpint (f->end_action, ==, LRG_DRAG_ACTION_MOVE);
	g_assert_null (lrg_drop_target_get_current_drop (t));
	g_assert_cmpint (canvas_end, ==, 1);

	/* Refused drop: ::drop returned FALSE */
	f->drop_result = FALSE;
	press_at (f, cx (src), cy (src));
	drag_to (f, cx (a), cy (a));
	release_at (f, cx (a), cy (a));
	g_assert_false (f->end_dropped);
	g_assert_cmpint (f->end_action, ==, LRG_DRAG_ACTION_NONE);

	/* Released over nothing */
	f->drop_result = TRUE;
	g_string_truncate (f->log, 0);
	press_at (f, cx (src), cy (src));
	drag_to (f, 700.0f, 500.0f);
	release_at (f, 700.0f, 500.0f);
	g_assert_false (f->end_dropped);
	g_assert_cmpstr (f->log->str, ==, "begin end ");
	g_assert_cmpint (f->end_count, ==, 3);
	g_assert_cmpint (canvas_end, ==, 3);
}

static void
on_canvas_cancel (LrgCanvas *canvas,
                  gint      *count)
{
	(*count)++;
}

static void
test_escape_cancels (Fixture       *f,
                     gconstpointer  user_data)
{
	LrgPanel *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel *a = add_panel (f->canvas, "a", 200, 20, 100, 100);
	gint      cancels = 0;

	make_source (f, src);
	make_target (f, a, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	g_signal_connect (f->canvas, "cancel", G_CALLBACK (on_canvas_cancel), &cancels);
	lrg_canvas_record (f->canvas);

	press_at (f, cx (src), cy (src));
	drag_to (f, cx (a), cy (a));
	g_string_truncate (f->log, 0);
	key (f, cx (a), cy (a), GRL_KEY_ESCAPE);
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	g_assert_cmpstr (f->log->str, ==, "a:leave end ");
	g_assert_false (f->end_dropped);
	/* The drag ate the Escape: no canvas ::cancel */
	g_assert_cmpint (cancels, ==, 0);

	/* The release after the cancel drops nothing */
	release_at (f, cx (a), cy (a));
	g_assert_cmpint (f->end_count, ==, 1);

	/* The gamepad cancel flag works the same */
	press_at (f, cx (src), cy (src));
	drag_to (f, cx (a), cy (a));
	{
		LrgCanvasInput input;

		lrg_canvas_input_init (&input);
		input.pointer_x = cx (a);
		input.pointer_y = cy (a);
		input.buttons_down = LEFT;
		input.cancel = TRUE;
		input.delta_time = DT;
		lrg_canvas_process_input (f->canvas, &input);
	}
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	g_assert_cmpint (f->end_count, ==, 2);
	g_assert_cmpint (cancels, ==, 0);
	release_at (f, cx (a), cy (a));

	/* lrg_canvas_cancel_drag() from code */
	press_at (f, cx (src), cy (src));
	drag_to (f, cx (a), cy (a));
	lrg_canvas_cancel_drag (f->canvas);
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	g_assert_cmpint (f->end_count, ==, 3);
	release_at (f, cx (a), cy (a));
}

static void
test_lost_button (Fixture       *f,
                  gconstpointer  user_data)
{
	LrgPanel *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel *a = add_panel (f->canvas, "a", 200, 20, 100, 100);

	make_source (f, src);
	make_target (f, a, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	lrg_canvas_record (f->canvas);

	press_at (f, cx (src), cy (src));
	drag_to (f, cx (a), cy (a));
	g_assert_true (lrg_canvas_is_dragging (f->canvas));

	/* The window lost the button (focus change): no release ever came */
	move_to (f, cx (a), cy (a));
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	g_assert_false (f->end_dropped);
	g_assert_true (strstr (f->log->str, "a:drop") == NULL);
}

static void
test_programmatic (Fixture       *f,
                   gconstpointer  user_data)
{
	LrgPanel                 *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel                 *a = add_panel (f->canvas, "a", 200, 20, 100, 100);
	g_autoptr(LrgDragPayload) p = lrg_drag_payload_new_string (NODE_TYPE, "Camera", LRG_DRAG_ACTION_LINK);
	LrgDragSource            *s = lrg_drag_source_new ();

	g_signal_connect (s, "drag-begin", G_CALLBACK (on_drag_begin), f);
	g_signal_connect (s, "drag-end", G_CALLBACK (on_drag_end), f);
	lrg_widget_add_drag_source (LRG_WIDGET (src), s);
	make_target (f, a, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	lrg_canvas_record (f->canvas);

	/* No button held: click to drop */
	move_to (f, cx (src), cy (src));
	g_assert_true (lrg_canvas_begin_drag (f->canvas, LRG_WIDGET (src), p, cx (src), cy (src)));
	g_assert_true (lrg_canvas_is_dragging (f->canvas));
	g_assert_false (lrg_canvas_begin_drag (f->canvas, NULL, p, 0.0f, 0.0f));
	g_assert_cmpint (f->begin_count, ==, 1);

	move_to (f, cx (a), cy (a));        /* no button: not a lost button */
	g_assert_true (lrg_canvas_is_dragging (f->canvas));
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_LINK);

	frame (f, cx (a), cy (a), LEFT, LEFT, 0, 0);     /* the click drops */
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	g_assert_true (f->end_dropped);
	g_assert_cmpint (f->end_action, ==, LRG_DRAG_ACTION_LINK);
	release_at (f, cx (a), cy (a));
	g_assert_cmpint (f->end_count, ==, 1);
	g_assert_true (g_str_has_suffix (f->log->str, "a:drop a:leave end "));

	/* Without a source widget: no source signals, still drops */
	g_assert_true (lrg_canvas_begin_drag (f->canvas, NULL, p, cx (a), cy (a)));
	g_assert_nonnull (lrg_canvas_get_drop_target (f->canvas));
	frame (f, cx (a), cy (a), LEFT, LEFT, 0, 0);
	release_at (f, cx (a), cy (a));
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	g_assert_cmpint (f->end_count, ==, 1);
}

static void
test_modifier_actions (Fixture       *f,
                       gconstpointer  user_data)
{
	LrgPanel      *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel      *a = add_panel (f->canvas, "a", 200, 20, 100, 100);
	LrgPanel      *b = add_panel (f->canvas, "b", 400, 20, 100, 100);
	LrgDropTarget *tb;
	gfloat         x;
	gfloat         y;

	make_source (f, src);
	make_target (f, a, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	tb = make_target (f, b, NODE_TYPE, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_LINK);
	lrg_drop_target_set_preferred_action (tb, LRG_DRAG_ACTION_LINK);
	lrg_canvas_record (f->canvas);
	x = cx (a);
	y = cy (a);

	press_at (f, cx (src), cy (src));
	drag_to (f, x, y);
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_MOVE);

	/* Modifier changes alone (no movement) re-run motion */
	frame (f, x, y, LEFT, 0, 0, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_COPY);
	frame (f, x, y, LEFT, 0, 0, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_MOVE);
	frame (f, x, y, LEFT, 0, 0, LRG_INPUT_MODIFIER_ALT);
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_LINK);
	frame (f, x, y, LEFT, 0, 0, LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT);
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_LINK);

	/* b: COPY | LINK with LINK preferred */
	frame (f, cx (b), cy (b), LEFT, 0, 0, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_LINK);
	frame (f, cx (b), cy (b), LEFT, 0, 0, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_COPY);
	/* Shift asks for MOVE, which b does not support: the preferred action */
	frame (f, cx (b), cy (b), LEFT, 0, 0, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_LINK);

	frame (f, cx (b), cy (b), 0, 0, LEFT, LRG_INPUT_MODIFIER_CTRL);
	g_assert_true (f->end_dropped);
	g_assert_cmpint (f->end_action, ==, LRG_DRAG_ACTION_COPY);
}

/* motion handler refusing drops on the right half of the widget */
static LrgDragAction
on_motion_left_half (LrgDropTarget  *target,
                     LrgDragPayload *payload,
                     gfloat          x,
                     gfloat          y,
                     gpointer        user_data)
{
	LrgWidget *w = lrg_drop_target_get_widget (target);

	if (x > cx (w))
		return LRG_DRAG_ACTION_NONE;
	return lrg_drop_target_get_proposed_action (target);
}

static void
test_motion_refuses (Fixture       *f,
                     gconstpointer  user_data)
{
	LrgPanel      *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel      *a = add_panel (f->canvas, "a", 200, 20, 200, 100);
	LrgDropTarget *t = lrg_drop_target_new (NULL, LRG_DRAG_ACTION_ALL);

	make_source (f, src);
	g_signal_connect (t, "motion", G_CALLBACK (on_motion_left_half), NULL);
	g_object_set_data (G_OBJECT (t), "test-name", (gpointer) "a");
	g_signal_connect (t, "drop", G_CALLBACK (on_drop), f);
	lrg_widget_add_drop_target (LRG_WIDGET (a), t);
	lrg_canvas_record (f->canvas);

	press_at (f, cx (src), cy (src));
	drag_to (f, cx (a) - 50.0f, cy (a));
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_MOVE);
	drag_to (f, cx (a) + 50.0f, cy (a));
	g_assert_cmpint (lrg_canvas_get_drag_action (f->canvas), ==, LRG_DRAG_ACTION_NONE);
	g_assert_true (lrg_canvas_get_drop_target (f->canvas) == t);
	release_at (f, cx (a) + 50.0f, cy (a));
	/* No ::drop for a refused spot */
	g_assert_true (strstr (f->log->str, "drop") == NULL);
	g_assert_false (f->end_dropped);
}

static void
test_nested_targets (Fixture       *f,
                     gconstpointer  user_data)
{
	LrgPanel      *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel      *outer = add_panel (f->canvas, "outer", 200, 100, 400, 300);
	LrgPanel      *inner = add_panel (outer, "inner", 20, 20, 120, 120);
	LrgPanel      *deep = add_panel (inner, "deep", 10, 10, 50, 50);
	LrgPanel      *picky = add_panel (outer, "picky", 200, 20, 120, 120);
	LrgDropTarget *pt;

	make_source (f, src);
	make_target (f, outer, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	make_target (f, inner, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	pt = make_target (f, picky, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	g_signal_connect (pt, "accept", G_CALLBACK (on_accept_veto), f);
	lrg_canvas_record (f->canvas);

	press_at (f, cx (src), cy (src));
	drag_to (f, cx (src) + 10.0f, cy (src));

	/* Over a child without a target: its closest ancestor with one */
	drag_to (f, cx (deep), cy (deep));
	g_assert_true (lrg_drop_target_get_widget (lrg_canvas_get_drop_target (f->canvas)) == LRG_WIDGET (inner));

	/* Over the outer area only */
	drag_to (f, lrg_widget_get_world_x (LRG_WIDGET (outer)) + 5.0f,
	         lrg_widget_get_world_y (LRG_WIDGET (outer)) + 290.0f);
	g_assert_true (lrg_drop_target_get_widget (lrg_canvas_get_drop_target (f->canvas)) == LRG_WIDGET (outer));

	/* The inner vetoes: the next one up wins */
	g_string_truncate (f->log, 0);
	drag_to (f, cx (picky), cy (picky));
	g_assert_true (lrg_drop_target_get_widget (lrg_canvas_get_drop_target (f->canvas)) == LRG_WIDGET (outer));
	g_assert_cmpstr (f->log->str, ==, "picky:veto outer:motion ");

	release_at (f, cx (picky), cy (picky));
	g_assert_true (f->end_dropped);
	g_assert_true (g_str_has_suffix (f->log->str, "outer:drop outer:leave end "));
}

static void
on_clicked (LrgButton *button,
            gint      *count)
{
	(*count)++;
}

static void
test_no_click_after_drag (Fixture       *f,
                          gconstpointer  user_data)
{
	LrgButton *button = lrg_button_new ("Drag me");
	gint       clicks = 0;
	gfloat     x;
	gfloat     y;

	lrg_widget_set_position (LRG_WIDGET (button), 50.0f, 50.0f);
	lrg_widget_set_size (LRG_WIDGET (button), 120.0f, 40.0f);
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (button));
	g_object_unref (button);
	g_signal_connect (button, "clicked", G_CALLBACK (on_clicked), &clicks);
	make_source (f, button);
	lrg_canvas_record (f->canvas);
	x = cx (button);
	y = cy (button);

	/* A plain click still clicks */
	press_at (f, x, y);
	release_at (f, x, y);
	g_assert_cmpint (clicks, ==, 1);

	/* Drag off and back onto the button, release on it: no click */
	press_at (f, x, y);
	drag_to (f, x + 30.0f, y);
	g_assert_true (lrg_canvas_is_dragging (f->canvas));
	g_assert_false (lrg_widget_get_state_flags (LRG_WIDGET (button)) & LRG_WIDGET_STATE_PRESSED);
	drag_to (f, x + 300.0f, y + 200.0f);
	drag_to (f, x, y);
	release_at (f, x, y);
	g_assert_false (lrg_canvas_is_dragging (f->canvas));
	g_assert_cmpint (clicks, ==, 1);

	/* And the next plain click works again */
	press_at (f, x, y);
	release_at (f, x, y);
	g_assert_cmpint (clicks, ==, 2);
}

static void
test_no_click_on_other_widgets (Fixture       *f,
                                gconstpointer  user_data)
{
	LrgPanel  *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgButton *other = lrg_button_new ("Other");
	gint       clicks = 0;

	lrg_widget_set_position (LRG_WIDGET (other), 300.0f, 300.0f);
	lrg_widget_set_size (LRG_WIDGET (other), 120.0f, 40.0f);
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (other));
	g_object_unref (other);
	g_signal_connect (other, "clicked", G_CALLBACK (on_clicked), &clicks);
	make_source (f, src);
	lrg_canvas_record (f->canvas);

	/* Release a drag over a button (and press others while dragging) */
	press_at (f, cx (src), cy (src));
	drag_to (f, cx (other), cy (other));
	frame (f, cx (other), cy (other), LEFT | LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_RIGHT),
	       LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_RIGHT), 0, 0);
	release_at (f, cx (other), cy (other));
	g_assert_cmpint (clicks, ==, 0);
	g_assert_null (lrg_canvas_get_top_popup (f->canvas));
}

/* find_instance_with_color: an instance filled with @c whose top-left is
 * inside the given box */
static gboolean
find_instance_with_color (LrgPaintList   *list,
                          const GrlColor *c,
                          gfloat          x0,
                          gfloat          y0,
                          gfloat          x1,
                          gfloat          y1)
{
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (inst->color[0] == c->r && inst->color[1] == c->g && inst->color[2] == c->b &&
		    inst->rect[0] >= x0 && inst->rect[0] <= x1 && inst->rect[1] >= y0 && inst->rect[1] <= y1)
			return TRUE;
	}
	return FALSE;
}

static gboolean
find_border_with_color (LrgPaintList   *list,
                        const GrlColor *c,
                        gfloat          x,
                        gfloat          y)
{
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (inst->border[0] == c->r && inst->border[1] == c->g && inst->border[2] == c->b &&
		    inst->params[0] > 0.0f && fabsf (inst->rect[0] - x) < 3.0f && fabsf (inst->rect[1] - y) < 3.0f)
			return TRUE;
	}
	return FALSE;
}

static void
test_preview_painted (Fixture       *f,
                      gconstpointer  user_data)
{
	LrgPanel       *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel       *a = add_panel (f->canvas, "a", 300, 200, 150, 100);
	LrgDropTarget  *t;
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (f->canvas));
	const GrlColor *popup = lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP);
	const GrlColor *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	LrgPaintList   *list;
	guint           n_idle;
	guint           n_drag;
	gfloat          px;
	gfloat          py;
	gint            i;

	make_source (f, src);
	t = make_target (f, a, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	n_idle = lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas));

	press_at (f, cx (src), cy (src));
	px = cx (a);
	py = cy (a);
	drag_to (f, px, py);
	for (i = 0; i < 60; i++)
		drag_to (f, px, py);   /* let the chip springs settle */

	list = lrg_canvas_record (f->canvas);
	n_drag = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (n_drag, >, n_idle);

	/* The chip: a POPUP-coloured body right of and below the pointer */
	g_assert_true (find_instance_with_color (list, popup, px + 4.0f, py + 4.0f, px + 30.0f, py + 30.0f));
	/* The default highlight: an accent outline around the target */
	g_assert_true (find_border_with_color (list, accent, lrg_widget_get_world_x (LRG_WIDGET (a)) - 1.0f,
	                                       lrg_widget_get_world_y (LRG_WIDGET (a)) - 1.0f));

	/* A target that paints its own feedback gets no outline */
	lrg_drop_target_set_paint_highlight (t, FALSE);
	list = lrg_canvas_record (f->canvas);
	g_assert_false (find_border_with_color (list, accent, lrg_widget_get_world_x (LRG_WIDGET (a)) - 1.0f,
	                                        lrg_widget_get_world_y (LRG_WIDGET (a)) - 1.0f));

	release_at (f, px, py);
	list = lrg_canvas_record (f->canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), <, n_drag);
}

static void
test_preview_rtl (Fixture       *f,
                  gconstpointer  user_data)
{
	LrgPanel       *src = add_panel (f->canvas, "src", 600, 20, 100, 40);
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (f->canvas));
	const GrlColor *popup = lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP);
	LrgPaintList   *list;
	gfloat          px = 500.0f;
	gfloat          py = 300.0f;
	gint            i;

	lrg_canvas_set_default_direction (f->canvas, LRG_WIDGET_DIRECTION_RTL);
	make_source (f, src);
	lrg_canvas_record (f->canvas);

	press_at (f, cx (src), cy (src));
	for (i = 0; i < 60; i++)
		drag_to (f, px, py);
	list = lrg_canvas_record (f->canvas);

	/* The chip sits left of the pointer: its left edge well before it */
	g_assert_true (find_instance_with_color (list, popup, 0.0f, py + 4.0f, px - 20.0f, py + 30.0f));
	g_assert_false (find_instance_with_color (list, popup, px + 4.0f, py + 4.0f, px + 30.0f, py + 30.0f));
	release_at (f, px, py);
}

static void
test_dock_panels (Fixture       *f,
                  gconstpointer  user_data)
{
	LrgDockArea  *dock = lrg_dock_area_new ();
	LrgDockPanel *p1 = lrg_dock_panel_new ("hierarchy", "Hierarchy");
	LrgDockPanel *p2 = lrg_dock_panel_new ("viewport", "Viewport");
	LrgPanel     *row = lrg_panel_new ();
	LrgPanel     *view = lrg_panel_new ();

	lrg_widget_set_position (LRG_WIDGET (dock), 0.0f, 0.0f);
	lrg_widget_set_size (LRG_WIDGET (dock), 800.0f, 600.0f);
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (dock));
	lrg_widget_set_name (LRG_WIDGET (row), "row");
	lrg_widget_set_name (LRG_WIDGET (view), "view");
	lrg_dock_panel_set_child (p1, LRG_WIDGET (row));
	lrg_dock_panel_set_child (p2, LRG_WIDGET (view));
	lrg_dock_area_add_panel (dock, p1, NULL, LRG_DOCK_POSITION_CENTER);
	lrg_dock_area_add_panel (dock, p2, p1, LRG_DOCK_POSITION_RIGHT);
	g_object_unref (row);
	g_object_unref (view);
	g_object_unref (p1);
	g_object_unref (p2);
	g_object_unref (dock);
	make_source (f, row);
	make_target (f, view, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	lrg_canvas_record (f->canvas);

	g_assert_cmpfloat (lrg_widget_get_width (LRG_WIDGET (row)), >, 0.0f);
	g_assert_cmpfloat (lrg_widget_get_width (LRG_WIDGET (view)), >, 0.0f);
	g_assert_cmpfloat (cx (view), >, cx (row));

	press_at (f, cx (row), cy (row));
	drag_to (f, cx (row) + 20.0f, cy (row));
	drag_to (f, cx (view), cy (view));
	g_assert_true (lrg_drop_target_get_widget (lrg_canvas_get_drop_target (f->canvas)) == LRG_WIDGET (view));
	/* The dock's own tab dragging is not involved */
	g_assert_false (lrg_dock_area_get_dragging (dock));
	release_at (f, cx (view), cy (view));
	g_assert_true (f->end_dropped);
	g_assert_true (g_str_has_suffix (f->log->str, "view:drop view:leave end "));
}

static void
test_target_removed_while_hovering (Fixture       *f,
                                    gconstpointer  user_data)
{
	LrgPanel      *src = add_panel (f->canvas, "src", 20, 20, 100, 40);
	LrgPanel      *a = add_panel (f->canvas, "a", 200, 20, 100, 100);
	LrgDropTarget *t;

	make_source (f, src);
	t = make_target (f, a, NODE_TYPE, LRG_DRAG_ACTION_ALL);
	lrg_canvas_record (f->canvas);

	press_at (f, cx (src), cy (src));
	drag_to (f, cx (a), cy (a));
	g_object_ref (t);
	lrg_widget_remove_drop_target (LRG_WIDGET (a), t);
	drag_to (f, cx (a) + 2.0f, cy (a));
	g_assert_null (lrg_canvas_get_drop_target (f->canvas));
	g_assert_null (lrg_drop_target_get_current_drop (t));
	g_assert_true (g_str_has_suffix (f->log->str, "a:leave "));
	g_object_unref (t);
	release_at (f, cx (a) + 2.0f, cy (a));
	g_assert_false (f->end_dropped);
}

/* ==========================================================================
 * main
 * ========================================================================== */

#define ADD(path, fn) \
	g_test_add (path, Fixture, NULL, fixture_set_up, fn, fixture_tear_down)

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/dnd/payload", test_payload_basics);
	g_test_add_func ("/ui/dnd/action-from-modifiers", test_action_from_modifiers);
	g_test_add_func ("/ui/dnd/attach-detach", test_attach_detach);

	ADD ("/ui/dnd/threshold", test_threshold);
	ADD ("/ui/dnd/threshold-custom", test_threshold_custom);
	ADD ("/ui/dnd/prepare-null", test_prepare_null);
	ADD ("/ui/dnd/type-matching", test_type_matching);
	ADD ("/ui/dnd/event-order", test_event_order);
	ADD ("/ui/dnd/drop-result", test_drop_result);
	ADD ("/ui/dnd/escape-cancels", test_escape_cancels);
	ADD ("/ui/dnd/lost-button", test_lost_button);
	ADD ("/ui/dnd/programmatic", test_programmatic);
	ADD ("/ui/dnd/modifier-actions", test_modifier_actions);
	ADD ("/ui/dnd/motion-refuses", test_motion_refuses);
	ADD ("/ui/dnd/nested-targets", test_nested_targets);
	ADD ("/ui/dnd/no-click-after-drag", test_no_click_after_drag);
	ADD ("/ui/dnd/no-click-on-other-widgets", test_no_click_on_other_widgets);
	ADD ("/ui/dnd/preview-painted", test_preview_painted);
	ADD ("/ui/dnd/preview-rtl", test_preview_rtl);
	ADD ("/ui/dnd/dock-panels", test_dock_panels);
	ADD ("/ui/dnd/target-removed", test_target_removed_while_hovering);

	return g_test_run ();
}
