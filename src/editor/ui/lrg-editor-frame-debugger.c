/* lrg-editor-frame-debugger.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Frame Debugger panel.  The call list is CPU data.  Replay and
 * frame.texture read back a GL texture only when a window is current,
 * and a headless scrub still succeeds.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-frame-debugger.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-scene-renderer.h"
#include "../../world3d/lrg-world3d-renderer.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-table.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-dock-area.h"
#include <json-glib/json-glib.h>
#include <raylib.h>

/* ==========================================================================
 * Rows
 * ========================================================================== */

#define LRG_TYPE_EDITOR_FRAME_ROW (lrg_editor_frame_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorFrameRow, lrg_editor_frame_row, LRG, EDITOR_FRAME_ROW, GObject)

struct _LrgEditorFrameRow
{
	GObject      parent_instance;
	guint        index;
	const gchar *pass;
	const gchar *kind;
	const gchar *entity;
	guint        triangles;
};

G_DEFINE_FINAL_TYPE (LrgEditorFrameRow, lrg_editor_frame_row, G_TYPE_OBJECT)

static void
lrg_editor_frame_row_class_init (LrgEditorFrameRowClass *klass)
{
}

static void
lrg_editor_frame_row_init (LrgEditorFrameRow *self)
{
}

/* ==========================================================================
 * Panel
 * ========================================================================== */

struct _LrgEditorFrameDebugger
{
	LrgContainer     parent_instance;

	LrgEditorApp    *app;            /* weak */
	LrgFrameCapture *capture;        /* ref */
	LrgCanvas       *canvas;         /* weak */
	LrgWidget       *tap;

	LrgWidget       *toolbar;
	LrgWidget       *capture_chip;
	LrgWidget       *detail;
	LrgTable        *table;
	GListStore      *rows;

	gboolean         suppress;
	guint            shown_calls;
	gint             shown_scrub;
};

G_DEFINE_FINAL_TYPE (LrgEditorFrameDebugger, lrg_editor_frame_debugger, LRG_TYPE_CONTAINER)

static void rebuild (LrgEditorFrameDebugger *self);
static void frame_present (LrgEditorFrameDebugger *self);

/* Attach the capture to the viewport renderer once it exists. */
static void
bind_renderer (LrgEditorFrameDebugger *self)
{
	LrgEditorSceneRenderer *scene;
	LrgWorld3DRenderer     *runtime;

	if (self->app == NULL || self->capture == NULL)
		return;
	scene = lrg_editor_app_get_scene_renderer (self->app);
	if (scene == NULL)
		return;
	runtime = lrg_editor_scene_renderer_get_runtime_renderer (scene);
	if (runtime == NULL)
		return;
	if (lrg_world3d_renderer_get_frame_capture (runtime) != self->capture)
		lrg_world3d_renderer_set_frame_capture (runtime, self->capture);
}

static gchar *
abs_path (LrgEditorFrameDebugger *self,
          const gchar           *path)
{
	const gchar *dir = NULL;

	if (path == NULL || path[0] == '\0')
		return NULL;
	if (g_path_is_absolute (path))
		return g_strdup (path);
	if (self->app != NULL)
		dir = lrg_editor_app_get_project_dir (self->app);
	if (dir == NULL || dir[0] == '\0')
		return g_strdup (path);
	return g_build_filename (dir, path, NULL);
}

static void
add_str (JsonBuilder *b,
         const gchar *name,
         const gchar *value)
{
	json_builder_set_member_name (b, name);
	if (value == NULL || value[0] == '\0')
		json_builder_add_null_value (b);
	else
		json_builder_add_string_value (b, value);
}

static void
refresh_detail (LrgEditorFrameDebugger *self)
{
	const LrgFrameCall *call;
	gint                scrub;
	g_autofree gchar   *text = NULL;

	scrub = lrg_frame_capture_get_scrub (self->capture);
	if (scrub < 0)
	{
		lrg_label_set_text (LRG_LABEL (self->detail), "Full frame");
		return;
	}
	call = lrg_frame_capture_get_call (self->capture, (guint) scrub);
	if (call == NULL)
	{
		lrg_label_set_text (LRG_LABEL (self->detail), "No draw call");
		return;
	}
	text = g_strdup_printf ("#%d  %s  %s  %s\nshader %s (%u)  tex %u  %u verts  %u tris  x%u\n"
	                        "blend %s mode %d  depth %s/%s  cull %s  wire %s  fb %u",
	                        scrub,
	                        call->pass != NULL ? call->pass : "",
	                        lrg_frame_call_kind_to_nick (call->kind),
	                        call->entity != NULL ? call->entity : "-",
	                        call->shader != NULL ? call->shader : "-",
	                        call->shader_id, call->texture_id,
	                        call->vertices, call->triangles, call->instances,
	                        call->blend ? "on" : "off", call->blend_mode,
	                        call->depth_test ? "test" : "off",
	                        call->depth_write ? "write" : "off",
	                        call->cull ? "on" : "off",
	                        call->wireframe ? "on" : "off",
	                        call->framebuffer);
	lrg_label_set_text (LRG_LABEL (self->detail), text);
}

static void
rebuild (LrgEditorFrameDebugger *self)
{
	g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func (g_object_unref);
	guint                n;
	guint                i;
	gint                 scrub;

	if (self->table == NULL || self->capture == NULL)
		return;
	n = lrg_frame_capture_get_n_calls (self->capture);
	for (i = 0; i < n; i++)
	{
		const LrgFrameCall *call = lrg_frame_capture_get_call (self->capture, i);
		LrgEditorFrameRow  *row = g_object_new (LRG_TYPE_EDITOR_FRAME_ROW, NULL);

		row->index = i;
		row->pass = (call != NULL) ? call->pass : NULL;
		row->kind = (call != NULL) ? lrg_frame_call_kind_to_nick (call->kind) : "";
		row->entity = (call != NULL) ? call->entity : NULL;
		row->triangles = (call != NULL) ? call->triangles : 0;
		g_ptr_array_add (rows, row);
	}
	self->suppress = TRUE;
	g_list_store_splice (self->rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->rows)),
	                     rows->pdata, rows->len);
	scrub = lrg_frame_capture_get_scrub (self->capture);
	if (scrub >= 0 && (guint) scrub < n)
		lrg_table_select (self->table, (guint) scrub);
	else
		lrg_table_unselect_all (self->table);
	self->suppress = FALSE;
	self->shown_calls = n;
	self->shown_scrub = scrub;
	refresh_detail (self);
	if (self->capture_chip != NULL)
		editor_chip_set_checked (self->capture_chip, lrg_frame_capture_is_armed (self->capture));
}

static gboolean
frame_tap (LrgCustomWidget *tap,
           gfloat           delta_time,
           gpointer         user_data)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	guint                   n;
	gint                    scrub;

	(void) tap;
	(void) delta_time;
	bind_renderer (self);
	n = lrg_frame_capture_get_n_calls (self->capture);
	scrub = lrg_frame_capture_get_scrub (self->capture);
	if (n != self->shown_calls || scrub != self->shown_scrub)
		rebuild (self);
	return FALSE;
}

static void
on_capture (LrgWidget *chip,
            gpointer   user_data)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);

	(void) chip;
	bind_renderer (self);
	lrg_frame_capture_arm (self->capture);
	editor_chip_set_checked (self->capture_chip, TRUE);
}

static void
on_full (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);

	(void) chip;
	lrg_frame_capture_set_scrub (self->capture, -1);
	rebuild (self);
}

static void
on_row_selected (LrgTable *table,
                 gpointer  user_data)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	guint                   sel;
	GObject                *item;
	LrgEditorFrameRow      *row;

	if (self->suppress)
		return;
	sel = lrg_table_get_selected (table);
	if (sel == LRG_TABLE_INVALID_POSITION)
		return;
	item = g_list_model_get_item (G_LIST_MODEL (self->rows), sel);
	if (item == NULL)
		return;
	row = LRG_EDITOR_FRAME_ROW (item);
	lrg_frame_capture_set_scrub (self->capture, (gint) row->index);
	self->shown_scrub = (gint) row->index;
	refresh_detail (self);
	g_object_unref (item);
	if (IsWindowReady ())
	{
		g_autoptr(GError) error = NULL;

		lrg_frame_capture_replay (self->capture, &error);
	}
}

static void
frame_present (LrgEditorFrameDebugger *self)
{
	LrgDockArea  *dock;
	LrgDockPanel *panel;

	if (self->app == NULL)
		return;
	dock = lrg_editor_app_get_dock (self->app);
	if (dock != NULL && lrg_dock_area_get_panel (dock, "frame") == NULL)
	{
		panel = lrg_dock_panel_new ("frame", "Frame Debugger");
		lrg_dock_panel_set_icon (panel, "eye");
		lrg_dock_panel_set_closable (panel, TRUE);
		lrg_dock_panel_set_child (panel, LRG_WIDGET (self));
		lrg_editor_app_add_panel (self->app, panel, "tab:assets");
		g_object_unref (panel);
	}
	editor_app_show_panel (self->app, "frame");
}

static gchar *
cell_text (LrgTable  *table,
           LrgWidget *cell,
           GObject   *item,
           guint      position,
           gpointer   user_data)
{
	LrgEditorFrameRow *row = LRG_EDITOR_FRAME_ROW (item);

	(void) table;
	(void) cell;
	(void) position;
	switch (GPOINTER_TO_INT (user_data))
	{
	case 0:
		return g_strdup_printf ("%u", row->index);
	case 1:
		return g_strdup (row->pass != NULL ? row->pass : "");
	case 2:
		return g_strdup (row->kind != NULL ? row->kind : "");
	case 3:
		return g_strdup (row->entity != NULL ? row->entity : "");
	default:
		return g_strdup_printf ("%u", row->triangles);
	}
}

/* --------------------------------------------------------------------------
 * IPC
 * -------------------------------------------------------------------------- */

static void
add_status (JsonBuilder            *b,
            LrgEditorFrameDebugger *self)
{
	g_auto(GStrv) passes = lrg_frame_capture_get_passes (self->capture);
	guint         i;

	json_builder_set_member_name (b, "armed");
	json_builder_add_boolean_value (b, lrg_frame_capture_is_armed (self->capture));
	json_builder_set_member_name (b, "captured");
	json_builder_add_boolean_value (b, lrg_frame_capture_is_captured (self->capture));
	json_builder_set_member_name (b, "calls");
	json_builder_add_int_value (b, (gint64) lrg_frame_capture_get_n_calls (self->capture));
	json_builder_set_member_name (b, "width");
	json_builder_add_int_value (b, lrg_frame_capture_get_width (self->capture));
	json_builder_set_member_name (b, "height");
	json_builder_add_int_value (b, lrg_frame_capture_get_height (self->capture));
	json_builder_set_member_name (b, "scrub");
	json_builder_add_int_value (b, lrg_frame_capture_get_scrub (self->capture));
	json_builder_set_member_name (b, "passes");
	json_builder_begin_array (b);
	for (i = 0; passes != NULL && passes[i] != NULL; i++)
	{
		json_builder_begin_object (b);
		json_builder_set_member_name (b, "pass");
		json_builder_add_string_value (b, passes[i]);
		json_builder_set_member_name (b, "calls");
		json_builder_add_int_value (b, (gint64) lrg_frame_capture_count_pass (self->capture, passes[i]));
		json_builder_end_object (b);
	}
	json_builder_end_array (b);
}

static void
add_call (JsonBuilder        *b,
          const LrgFrameCall *call,
          guint               index,
          gboolean            full)
{
	guint i;

	json_builder_begin_object (b);
	json_builder_set_member_name (b, "index");
	json_builder_add_int_value (b, (gint64) index);
	add_str (b, "pass", call->pass);
	add_str (b, "kind", lrg_frame_call_kind_to_nick (call->kind));
	add_str (b, "entity", call->entity);
	add_str (b, "guid", call->guid);
	add_str (b, "mesh", call->mesh);
	add_str (b, "material", call->material);
	add_str (b, "shader", call->shader);
	if (!full)
	{
		json_builder_set_member_name (b, "vertices");
		json_builder_add_int_value (b, (gint64) call->vertices);
		json_builder_set_member_name (b, "triangles");
		json_builder_add_int_value (b, (gint64) call->triangles);
		json_builder_end_object (b);
		return;
	}
	json_builder_set_member_name (b, "shader_id");
	json_builder_add_int_value (b, (gint64) call->shader_id);
	json_builder_set_member_name (b, "texture_id");
	json_builder_add_int_value (b, (gint64) call->texture_id);
	json_builder_set_member_name (b, "vertices");
	json_builder_add_int_value (b, (gint64) call->vertices);
	json_builder_set_member_name (b, "triangles");
	json_builder_add_int_value (b, (gint64) call->triangles);
	json_builder_set_member_name (b, "instances");
	json_builder_add_int_value (b, (gint64) call->instances);
	json_builder_set_member_name (b, "color");
	json_builder_begin_array (b);
	for (i = 0; i < 4; i++)
		json_builder_add_double_value (b, call->color[i]);
	json_builder_end_array (b);
	json_builder_set_member_name (b, "transform");
	json_builder_begin_array (b);
	for (i = 0; i < 16; i++)
		json_builder_add_double_value (b, call->transform[i]);
	json_builder_end_array (b);
	json_builder_set_member_name (b, "state");
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "blend");
	json_builder_add_boolean_value (b, call->blend);
	json_builder_set_member_name (b, "blend_mode");
	json_builder_add_int_value (b, call->blend_mode);
	json_builder_set_member_name (b, "depth_test");
	json_builder_add_boolean_value (b, call->depth_test);
	json_builder_set_member_name (b, "depth_write");
	json_builder_add_boolean_value (b, call->depth_write);
	json_builder_set_member_name (b, "cull");
	json_builder_add_boolean_value (b, call->cull);
	json_builder_set_member_name (b, "wireframe");
	json_builder_add_boolean_value (b, call->wireframe);
	json_builder_set_member_name (b, "framebuffer");
	json_builder_add_int_value (b, (gint64) call->framebuffer);
	json_builder_end_object (b);
	json_builder_end_object (b);
}

static JsonNode *
m_frame_capture (LrgEditorServer *server,
                 JsonObject      *params,
                 gpointer         user_data,
                 GError         **error)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();

	(void) server;
	(void) params;
	(void) error;
	bind_renderer (self);
	lrg_frame_capture_arm (self->capture);
	editor_chip_set_checked (self->capture_chip, TRUE);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "armed");
	json_builder_add_boolean_value (b, TRUE);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_frame_status (LrgEditorServer *server,
                JsonObject      *params,
                gpointer         user_data,
                GError         **error)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();

	(void) server;
	(void) params;
	(void) error;
	bind_renderer (self);
	json_builder_begin_object (b);
	add_status (b, self);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_frame_calls (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();
	const gchar            *pass = NULL;
	gint64                  offset = 0;
	gint64                  limit = 200;
	guint                   n;
	guint                   i;
	guint                   matched = 0;
	guint                   emitted = 0;

	(void) server;
	if (params != NULL && json_object_has_member (params, "pass"))
		pass = json_object_get_string_member (params, "pass");
	if (params != NULL && json_object_has_member (params, "offset"))
		offset = json_object_get_int_member (params, "offset");
	if (params != NULL && json_object_has_member (params, "limit"))
		limit = json_object_get_int_member (params, "limit");
	if (offset < 0 || limit < 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "offset and limit must be >= 0");
		return NULL;
	}
	n = lrg_frame_capture_get_n_calls (self->capture);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "calls");
	json_builder_begin_array (b);
	for (i = 0; i < n; i++)
	{
		const LrgFrameCall *call = lrg_frame_capture_get_call (self->capture, i);

		if (call == NULL)
			continue;
		if (pass != NULL && g_strcmp0 (call->pass, pass) != 0)
			continue;
		if ((gint64) matched >= offset && (gint64) emitted < limit)
		{
			add_call (b, call, i, FALSE);
			emitted++;
		}
		matched++;
	}
	json_builder_end_array (b);
	json_builder_set_member_name (b, "total");
	json_builder_add_int_value (b, (gint64) matched);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_frame_call (LrgEditorServer *server,
              JsonObject      *params,
              gpointer         user_data,
              GError         **error)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();
	const LrgFrameCall     *call;
	gint64                  index;

	(void) server;
	if (params == NULL || !json_object_has_member (params, "index"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "index is required");
		return NULL;
	}
	index = json_object_get_int_member (params, "index");
	if (index < 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "no draw call %d", (gint) index);
		return NULL;
	}
	call = lrg_frame_capture_get_call (self->capture, (guint) index);
	if (call == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "no draw call %d", (gint) index);
		return NULL;
	}
	add_call (b, call, (guint) index, TRUE);
	return json_builder_get_root (b);
}

static JsonNode *
m_frame_scrub (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();
	gint64                  index;

	(void) server;
	if (params == NULL || !json_object_has_member (params, "index"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "index is required");
		return NULL;
	}
	index = json_object_get_int_member (params, "index");
	lrg_frame_capture_set_scrub (self->capture, (gint) index);
	/* Headless replay fails; the scrub itself is what IPC promises. */
	if (IsWindowReady ())
	{
		g_autoptr(GError) gpu = NULL;

		lrg_frame_capture_replay (self->capture, &gpu);
	}
	rebuild (self);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "scrub");
	json_builder_add_int_value (b, lrg_frame_capture_get_scrub (self->capture));
	json_builder_set_member_name (b, "calls");
	json_builder_add_int_value (b, (gint64) lrg_frame_capture_get_n_calls (self->capture));
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_frame_texture (LrgEditorServer *server,
                 JsonObject      *params,
                 gpointer         user_data,
                 GError         **error)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();
	g_autoptr(GError)       local = NULL;
	g_autofree gchar       *path = NULL;
	const gchar            *target = NULL;
	const gchar            *raw = NULL;
	guint                   id = 0;
	gint                    width = 0;
	gint                    height = 0;
	static const gchar     *known[] = { "replay", "depth", "normal", "ssao", NULL };
	guint                   i;

	(void) server;
	if (params == NULL || !json_object_has_member (params, "path"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "path is required");
		return NULL;
	}
	raw = json_object_get_string_member (params, "path");
	path = abs_path (self, raw);
	if (path == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "path is required");
		return NULL;
	}
	if (json_object_has_member (params, "target"))
		target = json_object_get_string_member (params, "target");
	if (target != NULL && g_strcmp0 (target, "shadow") == 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "the shadow atlas texture id is not public");
		return NULL;
	}
	if (target != NULL)
	{
		id = lrg_frame_capture_get_named_texture (self->capture, target, &width, &height);
		if (id == 0)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "no %s texture in this capture", target);
			return NULL;
		}
	}
	else if (json_object_has_member (params, "id"))
	{
		gint64 want = json_object_get_int_member (params, "id");

		for (i = 0; known[i] != NULL && id == 0; i++)
		{
			gint  w = 0;
			gint  h = 0;
			guint got = lrg_frame_capture_get_named_texture (self->capture, known[i], &w, &h);

			if (got != 0 && (gint64) got == want)
			{
				id = got;
				width = w;
				height = h;
			}
		}
		if (id == 0)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "texture %d is not a published target", (gint) want);
			return NULL;
		}
	}
	else
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "target or id is required");
		return NULL;
	}
	if (!lrg_frame_capture_save_texture (self->capture, id, width, height, path, &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "%s", local != NULL ? local->message : "texture save failed");
		return NULL;
	}
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "path");
	json_builder_add_string_value (b, path);
	json_builder_set_member_name (b, "width");
	json_builder_add_int_value (b, width);
	json_builder_set_member_name (b, "height");
	json_builder_add_int_value (b, height);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_frame_save (LrgEditorServer *server,
              JsonObject      *params,
              gpointer         user_data,
              GError         **error)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();
	g_autoptr(GError)       local = NULL;
	g_autofree gchar       *path = NULL;

	(void) server;
	if (params == NULL || !json_object_has_member (params, "path"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "path is required");
		return NULL;
	}
	path = abs_path (self, json_object_get_string_member (params, "path"));
	if (path == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "path is required");
		return NULL;
	}
	if (!lrg_frame_capture_save_to_file (self->capture, path, &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "%s", local != NULL ? local->message : "save failed");
		return NULL;
	}
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "path");
	json_builder_add_string_value (b, path);
	json_builder_set_member_name (b, "calls");
	json_builder_add_int_value (b, (gint64) lrg_frame_capture_get_n_calls (self->capture));
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_frame_load (LrgEditorServer *server,
              JsonObject      *params,
              gpointer         user_data,
              GError         **error)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();
	g_autoptr(GError)       local = NULL;
	g_autofree gchar       *path = NULL;

	(void) server;
	if (params == NULL || !json_object_has_member (params, "path"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "path is required");
		return NULL;
	}
	path = abs_path (self, json_object_get_string_member (params, "path"));
	if (!lrg_frame_capture_load_file (self->capture, path, &local))
	{
		gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

		if (local != NULL && g_error_matches (local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, code, "%s",
		             local != NULL ? local->message : "load failed");
		return NULL;
	}
	rebuild (self);
	json_builder_begin_object (b);
	add_status (b, self);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_frame_show (LrgEditorServer *server,
              JsonObject      *params,
              gpointer         user_data,
              GError         **error)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();

	(void) server;
	(void) params;
	(void) error;
	frame_present (self);
	json_builder_begin_object (b);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

/* --------------------------------------------------------------------------
 * Widget
 * -------------------------------------------------------------------------- */

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static void
lrg_editor_frame_debugger_layout_children (LrgContainer *container)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (container);
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat                  w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                  h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                  pad = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y));
	gfloat                  gap = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING) * 0.75f);
	gfloat                  pw, ph, dw, dh, y = pad;

	if (self->toolbar == NULL || self->table == NULL || self->detail == NULL)
		return;
	editor_panel_fit_toolbar (self->toolbar, w - pad * 2.0f);
	lrg_widget_get_preferred_size (self->toolbar, &pw, &ph);
	lrg_widget_place_in_slot (self->toolbar, pad, y, MAX (0.0f, w - pad * 2.0f), ph);
	y += ph + gap;
	lrg_widget_get_preferred_size (self->detail, &dw, &dh);
	dh = MAX (36.0f, dh);
	lrg_widget_place_in_slot (LRG_WIDGET (self->table), pad, y, MAX (0.0f, w - pad * 2.0f),
	                          MAX (0.0f, h - y - dh - gap - pad));
	lrg_widget_place_in_slot (self->detail, pad, h - pad - dh, MAX (0.0f, w - pad * 2.0f), dh);
}

static void
lrg_editor_frame_debugger_dispose (GObject *object)
{
	LrgEditorFrameDebugger *self = LRG_EDITOR_FRAME_DEBUGGER (object);

	if (self->canvas != NULL && self->tap != NULL)
	{
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tap), NULL, NULL, NULL);
		lrg_canvas_remove_overlay (self->canvas, self->tap);
	}
	self->tap = NULL;
	g_clear_weak_pointer (&self->canvas);
	g_clear_object (&self->capture);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_frame_debugger_parent_class)->dispose (object);
}

static void
lrg_editor_frame_debugger_finalize (GObject *object)
{
	g_clear_object (&LRG_EDITOR_FRAME_DEBUGGER (object)->rows);
	G_OBJECT_CLASS (lrg_editor_frame_debugger_parent_class)->finalize (object);
}

static void
lrg_editor_frame_debugger_class_init (LrgEditorFrameDebuggerClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_frame_debugger_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_frame_debugger_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_frame_debugger_layout_children;
}

static void
lrg_editor_frame_debugger_init (LrgEditorFrameDebugger *self)
{
	static const struct { const gchar *title; gfloat width; } columns[5] = {
		{ "#", 48.0f }, { "Pass", 110.0f }, { "Kind", 100.0f }, { "Entity", 160.0f },
		{ "Tris", 70.0f }
	};
	LrgWidget *chip;
	guint      i;

	self->shown_scrub = -1;
	self->rows = g_list_store_new (LRG_TYPE_EDITOR_FRAME_ROW);
	self->capture = lrg_frame_capture_new ();

	self->toolbar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->toolbar, "frame-toolbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 6.0f);
	self->capture_chip = editor_chip_new ("frame-capture", LRG_ICON_EYE, "Capture", on_capture, self);
	add_to (self->toolbar, self->capture_chip);
	chip = editor_chip_new ("frame-full", LRG_ICON_STOP, "Full frame", on_full, self);
	add_to (self->toolbar, chip);
	lrg_container_add_child (LRG_CONTAINER (self), self->toolbar);
	g_object_unref (self->toolbar);

	self->table = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->table), "frame-calls");
	lrg_table_set_striped (self->table, TRUE);
	for (i = 0; i < G_N_ELEMENTS (columns); i++)
		lrg_table_add_column (self->table, columns[i].title, cell_text, GINT_TO_POINTER ((gint) i), NULL,
		                      columns[i].width,
		                      LRG_TABLE_COLUMN_RESIZABLE | (i == 3 ? LRG_TABLE_COLUMN_EXPAND : LRG_TABLE_COLUMN_NONE));
	lrg_table_set_model (self->table, G_LIST_MODEL (self->rows));
	g_signal_connect (self->table, "selection-changed", G_CALLBACK (on_row_selected), self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->table));
	g_object_unref (self->table);

	self->detail = LRG_WIDGET (lrg_label_new ("No frame captured"));
	lrg_widget_set_name (self->detail, "frame-detail");
	lrg_container_add_child (LRG_CONTAINER (self), self->detail);
	g_object_unref (self->detail);
}

static void a_capture (gpointer d) { on_capture (NULL, d); }
static void a_full (gpointer d) { on_full (NULL, d); }
static void a_show (gpointer d) { frame_present (LRG_EDITOR_FRAME_DEBUGGER (d)); }

LrgEditorFrameDebugger *
lrg_editor_frame_debugger_new (LrgEditorApp *app)
{
	static const LrgCustomWidgetFuncs tap_funcs = { NULL, NULL, frame_tap, NULL, { NULL } };
	LrgEditorFrameDebugger *self;
	LrgEditorActions       *actions;
	LrgEditorServer        *server;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_FRAME_DEBUGGER, NULL);
	g_set_weak_pointer (&self->app, app);

	g_set_weak_pointer (&self->canvas, lrg_editor_app_get_canvas (app));
	self->tap = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->tap, "frame-tap");
	lrg_widget_set_pointer_transparent (self->tap, TRUE);
	lrg_widget_set_size (self->tap, 0.0f, 0.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tap), &tap_funcs, self, NULL);
	lrg_canvas_add_overlay (self->canvas, self->tap);
	g_object_unref (self->tap);

	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "frame.capture", "Capture Frame", "Frame", NULL, a_capture, NULL, NULL, self);
	lrg_editor_actions_add (actions, "frame.show", "Frame Debugger", "Frame", NULL, a_show, NULL, NULL, self);
	lrg_editor_actions_add (actions, "frame.scrub-full", "Show Full Frame", "Frame", NULL, a_full, NULL, NULL, self);

	server = lrg_editor_app_get_server (app);
	lrg_editor_server_register_method (server, "frame.capture", m_frame_capture, self, NULL);
	lrg_editor_server_register_method (server, "frame.status", m_frame_status, self, NULL);
	lrg_editor_server_register_method (server, "frame.calls", m_frame_calls, self, NULL);
	lrg_editor_server_register_method (server, "frame.call", m_frame_call, self, NULL);
	lrg_editor_server_register_method (server, "frame.scrub", m_frame_scrub, self, NULL);
	lrg_editor_server_register_method (server, "frame.texture", m_frame_texture, self, NULL);
	lrg_editor_server_register_method (server, "frame.save", m_frame_save, self, NULL);
	lrg_editor_server_register_method (server, "frame.load", m_frame_load, self, NULL);
	lrg_editor_server_register_method (server, "frame.show", m_frame_show, self, NULL);
	return self;
}

LrgFrameCapture *
lrg_editor_frame_debugger_get_capture (LrgEditorFrameDebugger *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FRAME_DEBUGGER (self), NULL);
	return self->capture;
}

gint
lrg_editor_frame_debugger_get_scrub (LrgEditorFrameDebugger *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FRAME_DEBUGGER (self), -1);
	return lrg_frame_capture_get_scrub (self->capture);
}
