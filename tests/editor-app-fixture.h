/* editor-app-fixture.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared fixture for the LrgEditorApp suites (tests/test-editor-app*.c):
 * an app with its canvas, driven headless one frame at a time through
 * lrg_editor_app_process_frame() with synthetic LrgCanvasInput frames.
 *
 *   efx_frame / efx_frames   run frames (1/60 s each)
 *   efx_move / efx_button    pointer position and button state
 *   efx_click / efx_click_widget / efx_drag
 *   efx_key / efx_type       one key press (and its release), typed text
 *   efx_ipc / efx_ipc_result an IPC request through lrg_editor_server_dispatch
 *   efx_rect_of / efx_find   widget rectangles in canvas coordinates
 *
 * Without a window text measures as 0.55 * size per glyph, so take
 * positions from widget rects (efx_rect_of, ui.state), never constants.
 * Header-only: every helper is static (G_GNUC_UNUSED: suites use a subset).
 */

#pragma once

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <libregnum.h>

#define EFX_W     (1600)
#define EFX_H     (960)
#define EFX_DT    (1.0f / 60.0f)

typedef struct
{
	LrgEditor         *editor;
	LrgEditorApp      *app;
	gchar             *dir;
	gchar             *project;
	gfloat             x;
	gfloat             y;
	guint              buttons;        /* held now */
	guint              prev_buttons;   /* held last frame */
	LrgInputModifiers  mods;
	gfloat             wheel_x;
	gfloat             wheel_y;
} EditorAppFixture;

G_GNUC_UNUSED static void
efx_rm_rf (const gchar *path)
{
	g_autoptr(GDir) d = g_dir_open (path, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *child = g_build_filename (path, name, NULL);

		if (g_file_test (child, G_FILE_TEST_IS_DIR) && !g_file_test (child, G_FILE_TEST_IS_SYMLINK))
			efx_rm_rf (child);
		else
			g_unlink (child);
	}
	g_rmdir (path);
}

/* A fresh app on a private settings file in a temp dir. */
G_GNUC_UNUSED static void
efx_setup (EditorAppFixture *f,
           gconstpointer     data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *settings = NULL;

	memset (f, 0, sizeof *f);
	f->dir = g_dir_make_tmp ("lrg-app-XXXXXX", NULL);
	settings = g_build_filename (f->dir, "editor.yaml", NULL);
	f->project = g_build_filename (f->dir, "game", NULL);

	f->editor = lrg_editor_new ();
	f->app = lrg_editor_app_new (f->editor);
	g_assert_true (lrg_editor_app_load_settings (f->app, settings, &error));
	g_assert_no_error (error);
	f->x = -1.0f;
	f->y = -1.0f;
}

G_GNUC_UNUSED static void
efx_teardown (EditorAppFixture *f,
              gconstpointer     data)
{
	g_clear_object (&f->app);
	g_clear_object (&f->editor);
	efx_rm_rf (f->dir);
	g_free (f->dir);
	g_free (f->project);
}

/* One frame with the current pointer / button state plus optional keys
 * and text; presses and releases are the difference to the last frame. */
G_GNUC_UNUSED static void
efx_frame_with (EditorAppFixture *f,
                const GrlKey     *keys,
                guint             n_keys,
                const GrlKey     *released,
                guint             n_released,
                const gchar      *text)
{
	LrgCanvasInput input;
	guint          i;

	while (g_main_context_iteration (NULL, FALSE))
		;

	lrg_canvas_input_init (&input);
	input.pointer_x = f->x;
	input.pointer_y = f->y;
	input.buttons_down = f->buttons;
	input.buttons_pressed = f->buttons & ~f->prev_buttons;
	input.buttons_released = f->prev_buttons & ~f->buttons;
	input.modifiers = f->mods;
	input.wheel_x = f->wheel_x;
	input.wheel_y = f->wheel_y;
	input.delta_time = EFX_DT;
	for (i = 0; i < n_keys; i++)
		lrg_canvas_input_add_key (&input, keys[i]);
	for (i = 0; i < n_released; i++)
		lrg_canvas_input_add_key_release (&input, released[i]);
	if (text != NULL)
		lrg_canvas_input_add_text (&input, text);

	lrg_editor_app_process_frame (f->app, &input, EFX_W, EFX_H);
	f->prev_buttons = f->buttons;
	f->wheel_x = f->wheel_y = 0.0f;
}

G_GNUC_UNUSED static void
efx_frame (EditorAppFixture *f)
{
	efx_frame_with (f, NULL, 0, NULL, 0, NULL);
}

G_GNUC_UNUSED static void
efx_frames (EditorAppFixture *f,
            gint              n)
{
	gint i;

	for (i = 0; i < n; i++)
		efx_frame (f);
}

G_GNUC_UNUSED static void
efx_move (EditorAppFixture *f,
          gfloat            x,
          gfloat            y)
{
	f->x = x;
	f->y = y;
	efx_frame (f);
}

/* Sets @button (a GrlMouseButton) held or not at (@x, @y), one frame. */
G_GNUC_UNUSED static void
efx_button (EditorAppFixture *f,
            gfloat            x,
            gfloat            y,
            GrlMouseButton    button,
            gboolean          down)
{
	f->x = x;
	f->y = y;
	if (down)
		f->buttons |= LRG_CANVAS_BUTTON_MASK (button);
	else
		f->buttons &= ~LRG_CANVAS_BUTTON_MASK (button);
	efx_frame (f);
}

/* Hover, press, release (three frames). */
G_GNUC_UNUSED static void
efx_click (EditorAppFixture *f,
           gfloat            x,
           gfloat            y)
{
	efx_move (f, x, y);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_LEFT, FALSE);
}

/* The rectangle of @widget in canvas coordinates. */
G_GNUC_UNUSED static void
efx_rect_of (LrgWidget *widget,
             gfloat    *x,
             gfloat    *y,
             gfloat    *w,
             gfloat    *h)
{
	*x = lrg_widget_get_world_x (widget);
	*y = lrg_widget_get_world_y (widget);
	*w = lrg_widget_get_width (widget);
	*h = lrg_widget_get_height (widget);
}

G_GNUC_UNUSED static void
efx_click_widget (EditorAppFixture *f,
                  LrgWidget        *widget)
{
	gfloat x, y, w, h;

	efx_rect_of (widget, &x, &y, &w, &h);
	g_assert_cmpfloat (w, >, 0.0f);
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
}

/* Press @button at (x0, y0), move in @steps to (x1, y1), release. */
G_GNUC_UNUSED static void
efx_drag (EditorAppFixture *f,
          GrlMouseButton    button,
          gfloat            x0,
          gfloat            y0,
          gfloat            x1,
          gfloat            y1,
          gint              steps)
{
	gint i;

	efx_move (f, x0, y0);
	efx_button (f, x0, y0, button, TRUE);
	for (i = 1; i <= steps; i++)
		efx_move (f, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
	efx_button (f, x1, y1, button, FALSE);
}

/* Presses @key with @mods for one frame and releases it on the next. */
G_GNUC_UNUSED static void
efx_key (EditorAppFixture  *f,
         GrlKey             key,
         LrgInputModifiers  mods)
{
	LrgInputModifiers saved = f->mods;

	f->mods = mods;
	efx_frame_with (f, &key, 1, NULL, 0, NULL);
	f->mods = saved;
	efx_frame_with (f, NULL, 0, &key, 1, NULL);
}

G_GNUC_UNUSED static void
efx_type (EditorAppFixture *f,
          const gchar      *text)
{
	efx_frame_with (f, NULL, 0, NULL, 0, text);
}

/* Dispatches a request written with single quotes (turned into JSON's
 * double quotes) and returns the raw reply line. */
G_GNUC_UNUSED static gchar *
efx_ipc (EditorAppFixture *f,
         const gchar      *request)
{
	g_autofree gchar *json = g_strdelimit (g_strdup (request), "'", '"');

	return lrg_editor_server_dispatch (lrg_editor_app_get_server (f->app), json);
}

/* The "result" member of a reply (asserting no error); free with
 * json_node_unref. */
G_GNUC_UNUSED static JsonNode *
efx_ipc_result (EditorAppFixture *f,
                const gchar      *request)
{
	g_autofree gchar     *reply = efx_ipc (f, request);
	g_autoptr(JsonParser) parser = json_parser_new ();
	JsonObject           *o;

	g_assert_true (json_parser_load_from_data (parser, reply, -1, NULL));
	o = json_node_get_object (json_parser_get_root (parser));
	if (json_object_has_member (o, "error"))
		g_error ("%s -> %s", request, reply);
	return json_node_copy (json_object_get_member (o, "result"));
}

/* A named widget anywhere under the canvas (lrg_widget_set_name). */
G_GNUC_UNUSED static LrgWidget *
efx_find (EditorAppFixture *f,
          const gchar      *name)
{
	return lrg_widget_find (LRG_WIDGET (lrg_editor_app_get_canvas (f->app)), name);
}

/* The viewport's content rectangle, from ui.state. */
G_GNUC_UNUSED static void
efx_viewport_rect (EditorAppFixture *f,
                   gfloat           *x,
                   gfloat           *y,
                   gfloat           *w,
                   gfloat           *h)
{
	g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'ui.state'}");
	JsonArray          *a = json_object_get_array_member (json_node_get_object (r), "viewport");

	*x = (gfloat) json_array_get_double_element (a, 0);
	*y = (gfloat) json_array_get_double_element (a, 1);
	*w = (gfloat) json_array_get_double_element (a, 2);
	*h = (gfloat) json_array_get_double_element (a, 3);
	g_assert_cmpfloat (*w, >, 0.0f);
}

/* The open menu bar title, or NULL. */
G_GNUC_UNUSED static const gchar *
efx_open_menu (EditorAppFixture *f)
{
	return lrg_menu_bar_get_open_title (lrg_editor_window_get_menu_bar (lrg_editor_app_get_window (f->app)));
}
