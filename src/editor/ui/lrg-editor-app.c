/* lrg-editor-app.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The libregnum editor application: lifecycle, documents, settings and
 * theme, the panels and their dock layout, the per-frame pass
 * (lrg_editor_app_process_frame), autosave and crash recovery, file
 * monitoring, and the window and headless main loops.
 *
 * Since 0.8.1 the UI is retained: an LrgCanvas holds an LrgEditorWindow
 * (menu bar, toolbar, dock, status bar) whose dock holds one LrgDockPanel
 * per panel. The canvas and window exist from construction, also without
 * a window, so tests drive them with synthetic LrgCanvasInput frames.
 * Actions, IPC, dialogs, playback and the viewport live in their own
 * files (see lrg-editor-app-private.h).
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include "lrg-editor-hierarchy-panel.h"
#include "lrg-editor-inspector-panel.h"
#include "lrg-editor-assets-panel.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-tiles-panel.h"
#include "lrg-editor-environment-panel.h"
#include "lrg-editor-bake-panel.h"
#include "lrg-editor-physics-panel.h"
#include "lrg-editor-sequencer-panel.h"
#include "lrg-editor-timeline-panel.h"
#include "lrg-editor-profiler-panel.h"
#include "lrg-editor-code-panel.h"
#include "lrg-editor-sources-panel.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-toast-queue.h"
#include "../../core/lrg-engine.h"
#include "../../core/lrg-cpu-affinity.h"
#include "../../graphics/lrg-grl-window.h"
#include "drawing/grl-fast2d-private.h"
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <raylib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define AUTOSAVE_DIR          ".lrg-editor/autosave"
#define PROBLEMS_DEBOUNCE     (0.4)
#define DEFAULT_THEME         "catppuccin-mocha"

G_DEFINE_FINAL_TYPE (LrgEditorApp, lrg_editor_app, G_TYPE_OBJECT)

static void on_editor_changed (LrgEditor *editor, gpointer user_data);
static void on_level_replaced (LrgEditor *editor, LrgLevel *level, gpointer user_data);
static void watch_level_file  (LrgEditorApp *self);
static void watch_project     (LrgEditorApp *self);

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
clear_monitors (LrgEditorApp *self)
{
	guint i;

	for (i = 0; self->asset_monitors != NULL && i < self->asset_monitors->len; i++)
	{
		GFileMonitor *m = g_ptr_array_index (self->asset_monitors, i);

		g_file_monitor_cancel (m);
		g_signal_handlers_disconnect_by_data (m, self);
	}
	if (self->asset_monitors != NULL)
		g_ptr_array_set_size (self->asset_monitors, 0);
}

static void
lrg_editor_app_dispose (GObject *object)
{
	LrgEditorApp *self = LRG_EDITOR_APP (object);

	editor_dialog_close_all (self);
	editor_play_dispose (self);

	if (self->server != NULL)
		lrg_editor_server_stop (self->server);
	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	if (self->settings != NULL)
		g_signal_handlers_disconnect_by_data (self->settings, self);
	clear_monitors (self);
	if (self->level_monitor != NULL)
	{
		g_file_monitor_cancel (self->level_monitor);
		g_signal_handlers_disconnect_by_data (self->level_monitor, self);
		g_clear_object (&self->level_monitor);
	}
	editor_thumbnails_dispose (self);
	editor_sources_dispose (self);

	/* The widgets point back at us: drop them first */
	if (self->window != NULL)
		g_signal_handlers_disconnect_by_data (lrg_editor_window_get_dock (self->window), self);
	g_clear_pointer (&self->panels, g_hash_table_unref);
	self->viewport_panel = NULL;
	g_clear_object (&self->window);
	g_clear_object (&self->canvas);
	g_clear_object (&self->theme);
	g_clear_object (&self->renderer);
	g_clear_pointer (&self->legacy, editor_legacy_input_free);

	g_clear_object (&self->nav);
	g_clear_object (&self->view);
	g_clear_object (&self->gizmo);
	g_clear_object (&self->server);
	g_clear_object (&self->actions);
	g_clear_object (&self->external);
	g_clear_object (&self->log);
	g_clear_object (&self->vcs);
	g_clear_object (&self->assets);
	g_clear_object (&self->project);
	g_clear_object (&self->settings);
	g_clear_object (&self->editor);

	G_OBJECT_CLASS (lrg_editor_app_parent_class)->dispose (object);
}

static void
lrg_editor_app_finalize (GObject *object)
{
	LrgEditorApp *self = LRG_EDITOR_APP (object);

	g_free (self->project_dir);
	g_free (self->settings_path);
	g_free (self->applied_theme);
	g_free (self->pending_path);
	g_free (self->recovery_file);
	g_free (self->window_shot);
	g_free (self->window_title);
	g_free (self->level_dir);
	g_ptr_array_unref (self->problems);
	g_ptr_array_unref (self->asset_monitors);
	g_clear_pointer (&self->sources, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_editor_app_parent_class)->finalize (object);
}

static void
lrg_editor_app_class_init (LrgEditorAppClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_editor_app_dispose;
	object_class->finalize = lrg_editor_app_finalize;
}

static void
lrg_editor_app_init (LrgEditorApp *self)
{
	self->problems = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_problem_free);
	self->problems_dirty = TRUE;
	self->asset_monitors = g_ptr_array_new_with_free_func (g_object_unref);
	self->panels = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	self->width = 1600;
	self->height = 960;
	self->applied_scale = -1.0;
	lrg_canvas_input_init (&self->last_input);
	editor_thumbnails_init (self);
	self->sources = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Panels and the dock layout
 * ========================================================================== */

static void
make_panel (LrgEditorApp *self,
            const gchar  *id,
            const gchar  *title,
            const gchar  *icon,
            LrgWidget    *content,
            gboolean      closable)
{
	LrgDockPanel *panel = lrg_dock_panel_new (id, title);

	lrg_dock_panel_set_icon (panel, icon);
	lrg_dock_panel_set_closable (panel, closable);
	lrg_dock_panel_set_child (panel, content);
	g_object_unref (content);
	g_hash_table_insert (self->panels, g_strdup (id), g_object_ref_sink (panel));
}

/* Every built-in panel, created once; docking happens in reset_layout. */
static void
create_panels (LrgEditorApp *self)
{
	/* The split owns all four viewport panels; viewport 0 is the one
	 * the 0.8.0 "viewport" rect and screenshots refer to */
	self->viewport_split = lrg_editor_viewport_split_new (self);
	self->viewport_panel = lrg_editor_viewport_split_get_panel (self->viewport_split, 0);
	make_panel (self, "viewport", "Viewport", "eye", LRG_WIDGET (self->viewport_split), FALSE);
	make_panel (self, "hierarchy", "Hierarchy", "list", LRG_WIDGET (lrg_editor_hierarchy_panel_new (self)), TRUE);
	make_panel (self, "inspector", "Inspector", "gear", LRG_WIDGET (lrg_editor_inspector_panel_new (self)), TRUE);
	make_panel (self, "assets", "Assets", "folder", LRG_WIDGET (lrg_editor_assets_panel_new (self)), TRUE);
	make_panel (self, "console", "Console", "info", LRG_WIDGET (lrg_editor_console_panel_new (self)), TRUE);
	make_panel (self, "problems", "Problems", "warning", LRG_WIDGET (lrg_editor_problems_panel_new (self)), TRUE);
	make_panel (self, "history", "History", "undo", LRG_WIDGET (lrg_editor_history_panel_new (self)), TRUE);
	make_panel (self, "timeline", "Timeline", "play", LRG_WIDGET (lrg_editor_timeline_panel_new (self)), TRUE);
	make_panel (self, LRG_EDITOR_SEQUENCER_PANEL_ID, "Sequencer", "clock", LRG_WIDGET (lrg_editor_sequencer_panel_new (self)), TRUE);
	make_panel (self, "profiler", "Profiler", "chart", LRG_WIDGET (lrg_editor_profiler_panel_new (self)), TRUE);
	make_panel (self, "code", "Code", "code", LRG_WIDGET (lrg_editor_code_panel_new (self)), TRUE);
	make_panel (self, "tiles", "Tiles", "grid", LRG_WIDGET (lrg_editor_tiles_panel_new (self)), TRUE);
	make_panel (self, "environment", "Environment", "star", LRG_WIDGET (lrg_editor_environment_panel_new (self)), TRUE);
	make_panel (self, LRG_EDITOR_BAKE_PANEL_ID, "Lighting", "refresh", LRG_WIDGET (lrg_editor_bake_panel_new (self)), TRUE);
	make_panel (self, LRG_EDITOR_PHYSICS_PANEL_ID, "Physics", "download", LRG_WIDGET (lrg_editor_physics_panel_new (self)), TRUE);
	make_panel (self, "sources", "Sources", "folder", LRG_WIDGET (lrg_editor_sources_panel_new (self)), TRUE);
}

static LrgDockPanel *
panel (LrgEditorApp *self,
       const gchar  *id)
{
	return g_hash_table_lookup (self->panels, id);
}

/* Docks @id where the default layout has it. */
static void
dock_default (LrgEditorApp *self,
              const gchar  *id)
{
	static const struct { const gchar *id; const gchar *ref; LrgDockPosition pos; gfloat share; } where[] = {
		{ "viewport", NULL, LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "hierarchy", "viewport", LRG_DOCK_POSITION_LEFT, 0.18f },
		{ "inspector", "viewport", LRG_DOCK_POSITION_RIGHT, 0.27f },
		{ "assets", "viewport", LRG_DOCK_POSITION_BOTTOM, 0.30f },
		{ "console", "assets", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "problems", "assets", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "history", "assets", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "timeline", "assets", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "sequencer", "assets", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "profiler", "assets", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "code", "assets", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "tiles", "inspector", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "environment", "inspector", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "bake", "inspector", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "physics", "inspector", LRG_DOCK_POSITION_CENTER, 0.0f },
		{ "sources", "assets", LRG_DOCK_POSITION_CENTER, 0.0f },
	};
	LrgDockArea  *dock = lrg_editor_window_get_dock (self->window);
	LrgDockPanel *p = panel (self, id);
	LrgDockPanel *ref = NULL;
	gsize         i;

	if (p == NULL || lrg_dock_panel_get_dock_area (p) != NULL)
		return;
	for (i = 0; i < G_N_ELEMENTS (where); i++)
	{
		if (g_strcmp0 (where[i].id, id) != 0)
			continue;
		if (where[i].ref != NULL)
			ref = lrg_dock_area_get_panel (dock, where[i].ref);
		if (ref == NULL && where[i].ref != NULL)
			ref = lrg_dock_area_get_panel (dock, "viewport");
		lrg_dock_area_add_panel (dock, p, ref,
		                         (ref == NULL && where[i].ref != NULL) ? where[i].pos : where[i].pos);
		if (where[i].share > 0.0f)
			lrg_dock_area_set_panel_share (dock, p, where[i].share);
		return;
	}
	/* Custom panels without a remembered place become tabs */
	lrg_dock_area_add_panel (dock, p, NULL, LRG_DOCK_POSITION_CENTER);
}

/* The layout new users get (and View > Reset Layout restores). */
void
editor_app_reset_layout (LrgEditorApp *self)
{
	static const gchar *order[] = {
		"viewport", "hierarchy", "inspector", "assets", "console", "problems", "history",
		"timeline", "sequencer", "profiler", "code", "tiles", "environment", "bake", "physics", "sources", NULL
	};
	LrgDockArea   *dock = lrg_editor_window_get_dock (self->window);
	GList         *docked = lrg_dock_area_get_panels (dock);
	GList         *l;
	GHashTableIter iter;
	gpointer       key;
	guint          i;

	for (l = docked; l != NULL; l = l->next)
		lrg_dock_area_remove_panel (dock, l->data);
	g_list_free (docked);

	for (i = 0; order[i] != NULL; i++)
		dock_default (self, order[i]);
	g_hash_table_iter_init (&iter, self->panels);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		dock_default (self, key);

	lrg_dock_area_present_panel (dock, panel (self, "assets"));
	lrg_dock_area_present_panel (dock, panel (self, "inspector"));
	lrg_dock_area_present_panel (dock, panel (self, "viewport"));
}

gboolean
editor_app_panel_is_shown (LrgEditorApp *self,
                           const gchar  *id)
{
	LrgDockPanel *p = panel (self, id);

	return p != NULL && lrg_dock_panel_get_dock_area (p) != NULL;
}

/* Docks @id if it was closed and brings it to the front. */
void
editor_app_show_panel (LrgEditorApp *self,
                       const gchar  *id)
{
	LrgDockPanel *p = panel (self, id);

	if (p == NULL || self->window == NULL)
		return;
	if (lrg_dock_panel_get_dock_area (p) == NULL)
		dock_default (self, id);
	lrg_dock_area_present_panel (lrg_editor_window_get_dock (self->window), p);
}

/* View > Panels: a visible panel closes (the viewport never does),
 * a hidden or closed one comes to the front. */
void
editor_app_toggle_panel (LrgEditorApp *self,
                         const gchar  *id)
{
	LrgDockArea  *dock = lrg_editor_window_get_dock (self->window);
	LrgDockPanel *p = panel (self, id);

	if (p == NULL)
		return;
	if (lrg_dock_panel_get_dock_area (p) != NULL && g_strcmp0 (id, "viewport") != 0 &&
	    lrg_widget_get_visible (LRG_WIDGET (p)) && lrg_dock_area_get_active_panel (dock) == p)
		lrg_dock_area_close_panel (dock, p);
	else
		editor_app_show_panel (self, id);
}

/* The arrangement changed (drag, split, close): remember it in the
 * settings; the file is written by lrg_editor_app_save_settings(). */
static void
on_layout_changed (LrgDockArea *dock,
                   gpointer     user_data)
{
	LrgEditorApp     *self = LRG_EDITOR_APP (user_data);
	g_autofree gchar *yaml = lrg_dock_area_save_layout (dock);

	g_object_set (self->settings, "ui-layout", yaml, NULL);
}

void
lrg_editor_app_add_panel (LrgEditorApp *self,
                          LrgDockPanel *panel_,
                          const gchar  *where)
{
	LrgDockArea  *dock;
	LrgDockPanel *ref = NULL;
	LrgDockPosition pos = LRG_DOCK_POSITION_CENTER;
	const gchar  *id;

	g_return_if_fail (LRG_IS_EDITOR_APP (self));
	g_return_if_fail (LRG_IS_DOCK_PANEL (panel_));

	id = lrg_dock_panel_get_id (panel_);
	g_return_if_fail (id != NULL);
	dock = lrg_editor_window_get_dock (self->window);

	g_hash_table_replace (self->panels, g_strdup (id), g_object_ref_sink (panel_));
	if (where == NULL || g_strcmp0 (where, "center") == 0)
		ref = lrg_dock_area_get_panel (dock, "viewport");
	else if (g_str_has_prefix (where, "tab:"))
		ref = lrg_dock_area_get_panel (dock, where + 4);
	else if (g_strcmp0 (where, "left") == 0)
		pos = LRG_DOCK_POSITION_LEFT;
	else if (g_strcmp0 (where, "right") == 0)
		pos = LRG_DOCK_POSITION_RIGHT;
	else if (g_strcmp0 (where, "bottom") == 0)
		pos = LRG_DOCK_POSITION_BOTTOM;
	else if (g_strcmp0 (where, "top") == 0)
		pos = LRG_DOCK_POSITION_TOP;
	else
		g_warning ("lrg_editor_app_add_panel: unknown position \"%s\", adding as a tab", where);

	if (lrg_dock_panel_get_dock_area (panel_) == NULL)
		lrg_dock_area_add_panel (dock, panel_, ref, pos);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgEditorApp *
lrg_editor_app_new (LrgEditor *editor)
{
	LrgEditorApp *self;

	self = g_object_new (LRG_TYPE_EDITOR_APP, NULL);
	self->editor = (editor != NULL) ? g_object_ref (editor) : lrg_editor_new ();
	self->settings = lrg_editor_settings_new ();
	self->external = lrg_external_editor_new (self->settings);
	self->log = g_object_ref (lrg_editor_log_install ());
	self->actions = lrg_editor_actions_new (self->settings);
	self->server = lrg_editor_server_new (self->editor);
	lrg_editor_server_set_external_editor (self->server, self->external);
	lrg_editor_server_set_log (self->server, self->log);

	/* The headless view model */
	self->view = lrg_editor_view_state_new (self->editor);
	self->nav = lrg_editor_viewport_nav_new (self->view);
	lrg_editor_viewport_nav_bind_settings (self->nav, self->settings);
	self->gizmo = lrg_editor_gizmo_new (self->editor);
	editor_play_init (self);

	/* The scene renderer makes no GL calls until it first renders */
	self->renderer = lrg_editor_scene_renderer_new (self->editor);
	lrg_editor_scene_renderer_set_settings (self->renderer, self->settings);
	lrg_editor_scene_renderer_set_gizmo (self->renderer, self->gizmo);
	lrg_editor_scene_renderer_set_play_session (self->renderer, self->play);

	g_signal_connect (self->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);

	/* Actions first: the window builds its menus from them */
	editor_app_register_actions (self);
	editor_app_register_methods (self);
	editor_sources_init (self);

	/* The UI: canvas, window, panels, toasts */
	self->canvas = g_object_ref_sink (lrg_canvas_new ());
	lrg_canvas_set_paint_background (self->canvas, TRUE);
	self->window = g_object_ref_sink (lrg_editor_window_new (self));
	lrg_container_add_child (LRG_CONTAINER (self->canvas), LRG_WIDGET (self->window));
	lrg_canvas_add_overlay (self->canvas, LRG_WIDGET (lrg_editor_window_get_toasts (self->window)));
	editor_window_set_menus_enabled (self->window, TRUE);
	create_panels (self);
	editor_app_reset_layout (self);
	g_signal_connect (lrg_editor_window_get_dock (self->window), "layout-changed",
	                  G_CALLBACK (on_layout_changed), self);

	editor_app_apply_settings (self);
	editor_window_sync_shortcuts (self->window);
	lrg_editor_view_state_reset_camera (self->view);
	lrg_editor_scene_renderer_set_base_dir (self->renderer, editor_app_base_dir (self));
	return self;
}

LrgEditor *
lrg_editor_app_get_editor (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->editor;
}

LrgEditorSettings *
lrg_editor_app_get_settings (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->settings;
}

LrgEditorActions *
lrg_editor_app_get_actions (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->actions;
}

LrgEditorServer *
lrg_editor_app_get_server (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->server;
}

LrgExternalEditor *
lrg_editor_app_get_external_editor (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->external;
}

LrgGuiContext *
lrg_editor_app_get_gui (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return NULL;
}

LrgCanvas *
lrg_editor_app_get_canvas (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->canvas;
}

LrgEditorWindow *
lrg_editor_app_get_window (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->window;
}

LrgDockArea *
lrg_editor_app_get_dock (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return lrg_editor_window_get_dock (self->window);
}

LrgEditorViewState *
lrg_editor_app_get_view_state (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->view;
}

LrgEditorViewportNav *
lrg_editor_app_get_viewport_nav (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->nav;
}

LrgEditorPlaySession *
lrg_editor_app_get_play_session (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->play;
}

const gchar *
lrg_editor_app_get_project_dir (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->project_dir;
}

const gchar *
lrg_editor_app_get_status (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return lrg_editor_view_state_get_status (self->view);
}

gboolean
lrg_editor_app_is_playing (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);
	return editor_play_is_playing (self);
}

gboolean
lrg_editor_app_activate_action (LrgEditorApp *self,
                                const gchar  *id)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);
	return lrg_editor_actions_activate (self->actions, id);
}

gboolean
editor_app_is_headless (LrgEditorApp *self)
{
	return !self->window_open;
}

/* ==========================================================================
 * Messages and clipboard
 * ========================================================================== */

void
editor_app_set_status (LrgEditorApp *self,
                       const gchar  *format,
                       ...)
{
	g_autofree gchar *text = NULL;
	va_list           args;

	va_start (args, format);
	text = g_strdup_vprintf (format, args);
	va_end (args);
	lrg_editor_view_state_set_status (self->view, text);
}

/* A toast plus a log line (and the status bar). @level: EDITOR_NOTIFY_*. */
void
editor_app_notify (LrgEditorApp *self,
                   gint          level,
                   const gchar  *format,
                   ...)
{
	g_autofree gchar *text = NULL;
	va_list           args;

	va_start (args, format);
	text = g_strdup_vprintf (format, args);
	va_end (args);

	lrg_editor_log_append (self->log, level >= EDITOR_NOTIFY_ERROR ? G_LOG_LEVEL_WARNING : G_LOG_LEVEL_MESSAGE,
	                       "editor", text);
	lrg_editor_view_state_set_status (self->view, text);
	if (self->window != NULL)
		editor_window_toast (self->window, level, text);
}

/* The system clipboard with a window, else a private one (headless). */
void
editor_app_set_clipboard (LrgEditorApp *self,
                          const gchar  *text)
{
	if (IsWindowReady ())
		SetClipboardText (text);
	g_object_set_data_full (G_OBJECT (self), "lrg-editor-clipboard", g_strdup (text), g_free);
}

gchar *
editor_app_get_clipboard (LrgEditorApp *self)
{
	if (IsWindowReady ())
	{
		const gchar *text = GetClipboardText ();

		if (text != NULL && *text != '\0')
			return g_strdup (text);
	}
	return g_strdup (g_object_get_data (G_OBJECT (self), "lrg-editor-clipboard"));
}

/* Nodes in the level, cached against the node tree serial. */
guint
editor_app_count_nodes (LrgEditorApp *self)
{
	return lrg_level_count_nodes (lrg_editor_get_level (self->editor));
}

/* ==========================================================================
 * Paths
 * ========================================================================== */

const gchar *
editor_app_base_dir (LrgEditorApp *self)
{
	const gchar *path;

	if (self->project_dir != NULL)
		return self->project_dir;

	/* Without a project, paths are relative to the open level. */
	path = lrg_editor_get_path (self->editor);
	if (path != NULL)
	{
		g_free (self->level_dir);
		self->level_dir = g_path_get_dirname (path);
		return self->level_dir;
	}

	return NULL;
}

gchar *
editor_app_project_path (LrgEditorApp *self,
                         const gchar  *path)
{
	const gchar *base;

	if (path == NULL)
		return NULL;
	if (g_path_is_absolute (path))
		return g_strdup (path);

	base = editor_app_base_dir (self);
	return (base != NULL) ? g_build_filename (base, path, NULL) : g_canonicalize_filename (path, NULL);
}

gchar *
editor_app_relative_path (LrgEditorApp *self,
                          const gchar  *path)
{
	const gchar *base = editor_app_base_dir (self);
	gsize        len;

	if (path == NULL)
		return NULL;
	if (base == NULL || !g_path_is_absolute (path))
		return g_strdup (path);

	len = strlen (base);
	if (g_str_has_prefix (path, base) && path[len] == G_DIR_SEPARATOR)
		return g_strdup (path + len + 1);
	return g_strdup (path);
}


/* ==========================================================================
 * Settings and theme
 * ========================================================================== */

/* Builds the canvas theme from the settings: a theme file, else a preset,
 * with every metric scaled by ui-scale x font-size / 16. */
static void
apply_theme (LrgEditorApp *self)
{
	const gchar         *file = lrg_editor_settings_get_theme_file (self->settings);
	const gchar         *preset = lrg_editor_settings_get_theme_preset (self->settings);
	g_autofree gchar    *value = NULL;
	g_autoptr(LrgTheme)  theme = NULL;
	g_autoptr(GError)    error = NULL;
	gdouble              scale = 1.0;
	gint                 font_size = 16;
	gint                 m;

	g_object_get (self->settings, "theme", &value, "ui-scale", &scale, "font-size", &font_size, NULL);
	scale = CLAMP (scale, 0.5, 4.0) * (font_size > 0 ? font_size / 16.0 : 1.0);
	if (self->theme != NULL && g_strcmp0 (value, self->applied_theme) == 0 && fabs (scale - self->applied_scale) < 1e-6)
		return;

	if (file != NULL)
	{
		theme = lrg_theme_new_from_file (file, &error);
		if (theme == NULL)
			lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "editor", error->message);
	}
	if (theme == NULL)
		theme = lrg_theme_new_preset (preset != NULL ? preset : DEFAULT_THEME);
	if (theme == NULL)
		theme = lrg_theme_new_preset (DEFAULT_THEME);
	if (fabs (scale - 1.0) > 1e-6)
		for (m = 0; m < LRG_THEME_METRIC_COUNT; m++)
			lrg_theme_set_metric (theme, (LrgThemeMetric) m,
			                      lrg_theme_get_metric (theme, (LrgThemeMetric) m) * (gfloat) scale);

	g_set_object (&self->theme, theme);
	lrg_widget_set_theme (LRG_WIDGET (self->canvas), self->theme);
	g_free (self->applied_theme);
	self->applied_theme = g_steal_pointer (&value);
	self->applied_scale = scale;
}

void
editor_app_apply_settings (LrgEditorApp *self)
{
	g_autofree gchar *layout = NULL;
	gdouble           st, sr, ss;
	guint             history;
	gboolean          snap;

	g_object_get (self->settings,
	              "snap-enabled", &snap, "snap-translate", &st,
	              "snap-rotate-degrees", &sr, "snap-scale", &ss,
	              "history-limit", &history, "ui-layout", &layout,
	              NULL);

	apply_theme (self);
	lrg_editor_set_snap_enabled (self->editor, snap);
	lrg_editor_set_snap (self->editor, st, sr * G_PI / 180.0, ss);
	lrg_editor_set_history_limit (self->editor, history);
	lrg_editor_view_state_load_settings (self->view, self->settings);

	/* The saved dock layout; the viewport can never be lost */
	if (layout != NULL && *layout != '\0')
	{
		LrgDockArea       *dock = lrg_editor_window_get_dock (self->window);
		g_autoptr(GError)  error = NULL;

		g_signal_handlers_block_by_func (dock, on_layout_changed, self);
		if (!lrg_dock_area_load_layout (dock, layout, &error))
			lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "editor", error->message);
		if (lrg_dock_area_get_panel (dock, "viewport") == NULL)
			dock_default (self, "viewport");
		g_signal_handlers_unblock_by_func (dock, on_layout_changed, self);
	}
}

static void
on_settings_notify (GObject    *settings,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);
	const gchar  *name = g_param_spec_get_name (pspec);

	/* Theme and scale apply live (Preferences) */
	if (g_strcmp0 (name, "theme") == 0 || g_strcmp0 (name, "ui-scale") == 0 || g_strcmp0 (name, "font-size") == 0)
		apply_theme (self);
}

gboolean
lrg_editor_app_load_settings (LrgEditorApp  *self,
                              const gchar   *path,
                              GError       **error)
{
	g_autoptr(GError) merr = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);

	g_free (self->settings_path);
	self->settings_path = g_strdup (path);
	if (!lrg_editor_settings_load (self->settings, path, error))
		return FALSE;

	/* A 0.8.0 file is brought up to date (a backup is written first) */
	if (!lrg_editor_settings_migrate (self->settings, path, &merr))
		lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "editor", merr->message);

	editor_app_apply_settings (self);
	g_signal_handlers_disconnect_by_func (self->settings, on_settings_notify, self);
	g_signal_connect (self->settings, "notify", G_CALLBACK (on_settings_notify), self);
	return TRUE;
}

gboolean
lrg_editor_app_save_settings (LrgEditorApp  *self,
                              GError       **error)
{
	g_autofree gchar *layout = NULL;
	gdouble           st, sr, ss;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);

	/* Persist the live layout, cameras and snap state with the preferences */
	layout = lrg_dock_area_save_layout (lrg_editor_window_get_dock (self->window));
	lrg_editor_get_snap (self->editor, &st, &sr, &ss);
	g_object_set (self->settings,
	              "ui-layout", layout,
	              "snap-enabled", lrg_editor_get_snap_enabled (self->editor),
	              "snap-translate", st,
	              "snap-rotate-degrees", sr * 180.0 / G_PI,
	              "snap-scale", ss,
	              NULL);
	lrg_editor_view_state_save_settings (self->view, self->settings);

	return lrg_editor_settings_save (self->settings, self->settings_path, error);
}

/* ==========================================================================
 * Projects and levels
 * ========================================================================== */

/* The project's asset database (sidecars written: this is the editor),
 * git status and folder monitors. */
static void
scan_project (LrgEditorApp *self)
{
	g_autoptr(GError) error = NULL;

	g_clear_object (&self->assets);
	g_clear_object (&self->vcs);
	if (self->project_dir == NULL)
		return;

	self->assets = lrg_asset_database_new ();
	lrg_asset_database_set_write_meta (self->assets, TRUE);
	lrg_asset_database_add_search_dir (self->assets, self->project_dir);
	if (!lrg_asset_database_scan (self->assets, &error))
		lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "editor", error->message);

	self->vcs = lrg_editor_vcs_new (self->project_dir);
	lrg_editor_vcs_queue_refresh (self->vcs);
}

static void
set_project_dir (LrgEditorApp *self,
                 const gchar  *dir)
{
	g_free (self->project_dir);
	self->project_dir = g_canonicalize_filename (dir, NULL);
	lrg_external_editor_set_project_dir (self->external, self->project_dir);
	lrg_editor_server_set_project_dir (self->server, self->project_dir);
	lrg_editor_scene_renderer_set_base_dir (self->renderer, self->project_dir);
	scan_project (self);
	watch_project (self);
	self->problems_dirty = TRUE;
}

gboolean
lrg_editor_app_open_project (LrgEditorApp  *self,
                             const gchar   *dir,
                             GError       **error)
{
	g_autofree gchar     *abs = NULL;
	g_autofree gchar     *manifest = NULL;
	g_autoptr(LrgProject) project = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);
	g_return_val_if_fail (dir != NULL, FALSE);

	abs = g_canonicalize_filename (dir, NULL);
	if (!g_file_test (abs, G_FILE_TEST_IS_DIR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, "%s is not a directory", abs);
		return FALSE;
	}

	/* A directory without a manifest is still usable as a loose project */
	manifest = g_build_filename (abs, LRG_PROJECT_MANIFEST, NULL);
	if (g_file_test (manifest, G_FILE_TEST_EXISTS))
	{
		project = lrg_project_open (abs, error);
		if (project == NULL)
			return FALSE;
	}
	else
	{
		g_autofree gchar *base = g_path_get_basename (abs);

		project = lrg_project_new (abs, base);
	}

	g_set_object (&self->project, project);
	set_project_dir (self, abs);
	lrg_editor_settings_add_recent_project (self->settings, abs);

	/* Open the default level if nothing is open yet */
	if (lrg_editor_get_path (self->editor) == NULL && !lrg_editor_is_dirty (self->editor) &&
	    lrg_project_get_default_level (project) != NULL)
	{
		g_autofree gchar *level = lrg_project_resolve (project, lrg_project_get_default_level (project));
		g_autoptr(GError) lerr = NULL;

		if (g_file_test (level, G_FILE_TEST_EXISTS) &&
		    !lrg_editor_app_open_level (self, level, &lerr))
			editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Could not open the default level: %s", lerr->message);
	}

	editor_app_notify (self, EDITOR_NOTIFY_SUCCESS, "Opened project %s", lrg_project_get_name (project));
	return TRUE;
}

static const gchar *starter_level =
	"type: level\n"
	"name: main\n"
	"default_view: 3d\n"
	"nodes:\n"
	"  - name: Sun\n"
	"    transform:\n"
	"      location: [4.0, 8.0, 4.0]\n"
	"      rotation: [-0.9, 0.6, 0.0]\n"
	"    visual:\n"
	"      kind: light\n"
	"      params:\n"
	"        light_type: directional\n"
	"  - name: Camera\n"
	"    transform:\n"
	"      location: [0.0, 3.0, 8.0]\n"
	"    visual:\n"
	"      kind: camera\n"
	"  - name: Ground\n"
	"    transform:\n"
	"      scale: [20.0, 1.0, 20.0]\n"
	"    visual:\n"
	"      kind: primitive\n"
	"      primitive: plane\n"
	"      material:\n"
	"        color: [0.35, 0.45, 0.35, 1.0]\n";

gboolean
lrg_editor_app_new_project (LrgEditorApp  *self,
                            const gchar   *dir,
                            const gchar   *name,
                            GError       **error)
{
	static const gchar *subdirs[] = { "levels", "scripts", "assets", "prefabs", NULL };
	g_autofree gchar     *abs = NULL;
	g_autofree gchar     *level = NULL;
	g_autoptr(LrgProject) project = NULL;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);
	g_return_val_if_fail (dir != NULL, FALSE);

	abs = g_canonicalize_filename (dir, NULL);
	for (i = 0; subdirs[i] != NULL; i++)
	{
		g_autofree gchar *sub = g_build_filename (abs, subdirs[i], NULL);

		if (g_mkdir_with_parents (sub, 0755) != 0)
		{
			g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
			             "Cannot create %s: %s", sub, g_strerror (errno));
			return FALSE;
		}
	}

	project = lrg_project_new (abs, (name != NULL && *name != '\0') ? name : "New Game");
	lrg_project_set_default_level (project, "levels/main.rlevel");
	lrg_project_add_asset_dir (project, "assets");
	lrg_project_add_asset_dir (project, "levels");
	lrg_project_add_asset_dir (project, "prefabs");
	lrg_project_add_asset_dir (project, "scripts");
	if (!lrg_project_save (project, error))
		return FALSE;

	level = g_build_filename (abs, "levels", "main.rlevel", NULL);
	if (!g_file_test (level, G_FILE_TEST_EXISTS) &&
	    !g_file_set_contents (level, starter_level, -1, error))
		return FALSE;

	lrg_editor_new_level (self->editor, "main");
	return lrg_editor_app_open_project (self, abs, error);
}

static gchar *
autosave_path_for (LrgEditorApp *self,
                   const gchar  *level_path)
{
	g_autofree gchar *base = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *name = NULL;

	if (level_path == NULL)
		return NULL;

	/* <project>/.lrg-editor/autosave/<name>-<hash>.rlevel; without a
	 * project the folder sits next to the level. */
	base = g_path_get_basename (level_path);
	if (self->project_dir != NULL)
		dir = g_build_filename (self->project_dir, AUTOSAVE_DIR, NULL);
	else
	{
		g_autofree gchar *parent = g_path_get_dirname (level_path);

		dir = g_build_filename (parent, AUTOSAVE_DIR, NULL);
	}
	checksum = g_compute_checksum_for_string (G_CHECKSUM_SHA1, level_path, -1);
	checksum[8] = '\0';
	name = g_strdup_printf ("%s-%s.autosave", base, checksum);
	return g_build_filename (dir, name, NULL);
}

/* Modification time in microseconds: whole seconds would let an external
 * save in the same second as ours pass for our own write. */
static gint64
file_mtime (const gchar *path)
{
	g_autoptr(GFile)     file = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(GDateTime) when = NULL;

	if (path == NULL)
		return 0;
	file = g_file_new_for_path (path);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED "," G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
	                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (info == NULL || (when = g_file_info_get_modification_date_time (info)) == NULL)
		return 0;
	return g_date_time_to_unix_usec (when);
}

gboolean
lrg_editor_app_open_level (LrgEditorApp  *self,
                           const gchar   *path,
                           GError       **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *autosave = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	editor_play_stop (self);
	editor_timeline_end_preview (self);

	abs = editor_app_project_path (self, path);
	if (!lrg_editor_load_level (self->editor, abs, error))
		return FALSE;

	/* A level outside any project: relative asset paths resolve next to it */
	if (self->project_dir == NULL)
		lrg_editor_scene_renderer_set_base_dir (self->renderer, editor_app_base_dir (self));

	lrg_editor_settings_add_recent_level (self->settings, abs);
	self->level_saved_mtime = file_mtime (abs);
	self->level_changed_on_disk = FALSE;
	watch_level_file (self);

	/* Offer crash recovery when the autosave is newer than the file */
	autosave = autosave_path_for (self, abs);
	if (autosave != NULL && file_mtime (autosave) > self->level_saved_mtime)
	{
		g_free (self->recovery_file);
		self->recovery_file = g_strdup (autosave);
		if (!editor_app_is_headless (self))
			editor_dialog_recover (self);
		else
			editor_app_notify (self, EDITOR_NOTIFY_WARNING, "A newer autosave exists: %s", autosave);
	}

	editor_app_set_status (self, "Opened %s", abs);
	return TRUE;
}

gboolean
editor_app_save (LrgEditorApp *self)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *autosave = NULL;

	/* A timeline preview poses nodes outside the history: never save it */
	editor_timeline_end_preview (self);

	if (lrg_editor_get_path (self->editor) == NULL)
	{
		editor_dialog_save_level_as (self);
		return FALSE;
	}

	if (!lrg_editor_save (self->editor, &error))
	{
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Save failed: %s", error->message);
		return FALSE;
	}

	/* Our own write must not trigger the "changed on disk" prompt */
	self->level_saved_mtime = file_mtime (lrg_editor_get_path (self->editor));
	self->level_changed_on_disk = FALSE;
	autosave = autosave_path_for (self, lrg_editor_get_path (self->editor));
	if (autosave != NULL)
		g_unlink (autosave);

	editor_app_notify (self, EDITOR_NOTIFY_SUCCESS, "Saved %s", lrg_editor_get_path (self->editor));
	return TRUE;
}

/* Save As: the document moves to @path (watch, recents) and an action
 * waiting on the save (close, open...) carries on. */
gboolean
editor_app_save_as (LrgEditorApp *self,
                    const gchar  *path)
{
	g_autofree gchar *abs = editor_app_project_path (self, path);
	g_autofree gchar *old = g_strdup (lrg_editor_get_path (self->editor));
	EditorPending     what;
	g_autofree gchar *pending_path = NULL;

	lrg_editor_set_path (self->editor, abs);
	if (!editor_app_save (self))
	{
		lrg_editor_set_path (self->editor, old);
		return FALSE;
	}

	lrg_editor_settings_add_recent_level (self->settings, abs);
	watch_level_file (self);
	self->problems_dirty = TRUE;

	what = self->pending;
	pending_path = g_steal_pointer (&self->pending_path);
	self->pending = EDITOR_PENDING_NONE;
	if (what != EDITOR_PENDING_NONE)
		editor_app_perform_pending (self, what, pending_path);
	return TRUE;
}

/* Carries out a document operation that may discard changes. */
void
editor_app_perform_pending (LrgEditorApp  *self,
                            EditorPending  what,
                            const gchar   *path)
{
	g_autoptr(GError) error = NULL;

	switch (what)
	{
	case EDITOR_PENDING_NEW_LEVEL:
		editor_play_stop (self);
		lrg_editor_new_level (self->editor, "untitled");
		watch_level_file (self);
		editor_app_set_status (self, "New level");
		break;
	case EDITOR_PENDING_OPEN_LEVEL:
		if (!lrg_editor_app_open_level (self, path, &error))
			editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Cannot open %s: %s", path, error->message);
		break;
	case EDITOR_PENDING_OPEN_PROJECT:
		lrg_editor_new_level (self->editor, "untitled");
		g_clear_object (&self->project);
		g_clear_pointer (&self->project_dir, g_free);
		if (!lrg_editor_app_open_project (self, path, &error))
			editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Cannot open project: %s", error->message);
		break;
	case EDITOR_PENDING_RELOAD:
		if (!lrg_editor_reload (self->editor, &error))
			editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Reload failed: %s", error->message);
		else
		{
			self->level_saved_mtime = file_mtime (lrg_editor_get_path (self->editor));
			self->level_changed_on_disk = FALSE;
			editor_app_notify (self, EDITOR_NOTIFY_INFO, "Reloaded %s", lrg_editor_get_path (self->editor));
		}
		break;
	case EDITOR_PENDING_QUIT:
		self->quit_now = TRUE;
		break;
	default:
		break;
	}
}

void
editor_app_request (LrgEditorApp  *self,
                    EditorPending  what,
                    const gchar   *path)
{
	gboolean confirm = TRUE;

	g_object_get (self->settings, "confirm-on-close", &confirm, NULL);

	/* Ask before throwing away unsaved work (not possible headless) */
	if (lrg_editor_is_dirty (self->editor) && confirm && !editor_app_is_headless (self))
	{
		g_autofree gchar *message = NULL;

		self->pending = what;
		g_free (self->pending_path);
		self->pending_path = g_strdup (path);
		message = g_strdup_printf ("\"%s\" has unsaved changes.",
		                           lrg_editor_get_path (self->editor) != NULL
		                           ? lrg_editor_get_path (self->editor) : "untitled");
		editor_dialog_confirm (self, message);
		return;
	}

	editor_app_perform_pending (self, what, path);
}

void
lrg_editor_app_request_quit (LrgEditorApp *self)
{
	g_return_if_fail (LRG_IS_EDITOR_APP (self));

	self->quit_requested = TRUE;
	editor_app_request (self, EDITOR_PENDING_QUIT, NULL);
}

gboolean
lrg_editor_app_should_quit (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), TRUE);

	return self->quit_now;
}

/* Called by the confirm dialog. @choice: 0 save, 1 discard, 2 cancel. */
void
editor_app_confirm_choice (LrgEditorApp *self,
                           gint          choice)
{
	EditorPending     what = self->pending;
	g_autofree gchar *path = g_steal_pointer (&self->pending_path);

	self->pending = EDITOR_PENDING_NONE;
	if (choice == 2)
	{
		self->quit_requested = FALSE;
		return;
	}
	if (choice == 0 && lrg_editor_get_path (self->editor) == NULL)
	{
		/* Untitled: Save As first, then carry on (editor_app_save_as) */
		self->pending = what;
		self->pending_path = g_steal_pointer (&path);
		editor_app_save (self);
		return;
	}
	if (choice == 0 && !editor_app_save (self))
	{
		self->quit_requested = FALSE;
		return;   /* the save failed */
	}

	editor_app_perform_pending (self, what, path);
}

/* ==========================================================================
 * Opening files
 * ========================================================================== */

LrgScriptLanguage
editor_script_language_for_path (const gchar *path)
{
	if (g_str_has_suffix (path, ".py"))
		return LRG_SCRIPT_LANGUAGE_PYTHON;
	if (g_str_has_suffix (path, ".js"))
		return LRG_SCRIPT_LANGUAGE_GJS;
	if (g_str_has_suffix (path, ".c"))
		return LRG_SCRIPT_LANGUAGE_CRISPY;
	if (g_str_has_suffix (path, ".el"))
		return LRG_SCRIPT_LANGUAGE_ELISP;
	return LRG_SCRIPT_LANGUAGE_LUA;
}

void
editor_app_open_file (LrgEditorApp *self,
                      const gchar  *path,
                      gint          line,
                      gint          col)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *abs = editor_app_project_path (self, path);

	/* script-editor "internal": the code panel takes it (only when the
	 * panel exists, or open_script would come back here) */
	if (g_strcmp0 (lrg_editor_settings_get_script_editor (self->settings), "internal") == 0 &&
	    g_object_get_data (G_OBJECT (self), "lrg-editor-code-panel") != NULL &&
	    lrg_editor_code_panel_open_script (self, path, line))
		return;

	if (!lrg_external_editor_open (self->external, abs, line, col, &error))
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Cannot open %s: %s", abs, error->message);
	else
		editor_app_set_status (self, "Opened %s in the external editor", abs);
}

/* Double-click on an asset: levels and prefabs open in the editor,
 * models and prefabs can also be placed; everything else goes to the
 * code editor or the desktop. */
void
editor_app_open_asset (LrgEditorApp *self,
                       const gchar  *path)
{
	LrgAssetType      type = lrg_asset_database_classify (path);
	g_autofree gchar *abs = editor_app_project_path (self, path);

	/* .rseq sequences open in the docked sequencer panel */
	if (g_str_has_suffix (abs, ".rseq") && lrg_editor_sequencer_panel_open_file (self, abs, NULL))
		return;

	switch (type)
	{
	case LRG_ASSET_TYPE_LEVEL:
	case LRG_ASSET_TYPE_PREFAB:
		editor_app_request (self, EDITOR_PENDING_OPEN_LEVEL, abs);
		break;
	case LRG_ASSET_TYPE_TEXTURE:
	case LRG_ASSET_TYPE_AUDIO:
	case LRG_ASSET_TYPE_MODEL:
	case LRG_ASSET_TYPE_FONT:
		{
			g_autofree gchar *uri = g_filename_to_uri (abs, NULL, NULL);
			g_autoptr(GError) error = NULL;

			if (uri == NULL || !g_app_info_launch_default_for_uri (uri, NULL, &error))
				editor_app_notify (self, EDITOR_NOTIFY_ERROR, "No application for %s%s%s", abs,
				                   error != NULL ? ": " : "", error != NULL ? error->message : "");
		}
		break;
	default:
		editor_app_open_file (self, abs, 1, 1);
		break;
	}
}

/* ==========================================================================
 * Nodes
 * ========================================================================== */

/* The Create menu (and the viewport context menu): node kinds in menu
 * order; "-" entries separate groups. */
const EditorCreateItem *
editor_create_items (guint *n_items)
{
	static const EditorCreateItem items[] = {
		{ "empty", "Empty", "Node", LRG_NODE_VISUAL_NONE, 0 },
		{ "cube", "Cube", "Cube", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_CUBE },
		{ "sphere", "Sphere", "Sphere", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_UV_SPHERE },
		{ "plane", "Plane", "Plane", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_PLANE },
		{ "cylinder", "Cylinder", "Cylinder", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_CYLINDER },
		{ "cone", "Cone", "Cone", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_CONE },
		{ "torus", "Torus", "Torus", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_TORUS },
		{ "rectangle", "Rectangle (2D)", "Rectangle", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_RECTANGLE_2D },
		{ "circle", "Circle (2D)", "Circle", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_CIRCLE_2D },
		{ "-", NULL, NULL, 0, 0 },
		{ "light", "Point Light", "Light", LRG_NODE_VISUAL_LIGHT, 0 },
		{ "spot-light", "Spot Light", "Spot Light", LRG_NODE_VISUAL_LIGHT, 0 },
		{ "directional-light", "Directional Light", "Sun", LRG_NODE_VISUAL_LIGHT, 0 },
		{ "camera", "Camera", "Camera", LRG_NODE_VISUAL_CAMERA, 0 },
		{ "audio", "Audio Emitter", "Audio", LRG_NODE_VISUAL_AUDIO_EMITTER, 0 },
		{ "sprite", "Sprite", "Sprite", LRG_NODE_VISUAL_SPRITE, 0 },
		{ "mesh", "Mesh (asset)", "Mesh", LRG_NODE_VISUAL_MESH_ASSET, 0 },
		{ "tilemap", "Tilemap", "Tilemap", LRG_NODE_VISUAL_TILEMAP, 0 },
		{ "-", NULL, NULL, 0, 0 },
		{ "decal", "Decal", "Decal", LRG_NODE_VISUAL_DECAL, 0 },
		{ "reflection-probe", "Reflection Probe", "Reflection Probe", LRG_NODE_VISUAL_REFLECTION_PROBE, 0 },
		{ "probe-volume", "Probe Volume", "Probe Volume", LRG_NODE_VISUAL_PROBE_VOLUME, 0 },
	};

	*n_items = G_N_ELEMENTS (items);
	return items;
}

LrgNode *
editor_app_create_node (LrgEditorApp      *self,
                        const gchar       *name,
                        LrgNodeVisualKind  kind,
                        LrgPrimitiveType   primitive,
                        LrgNode           *parent)
{
	g_autoptr(LrgNode)   node = lrg_node_new (name);
	LrgEditorViewCamera *camera = lrg_editor_view_state_get_camera (self->view, -1);
	gfloat               pos[3] = { 0.0f, 0.0f, 0.0f };

	if (kind != LRG_NODE_VISUAL_NONE)
	{
		g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (kind);

		if (kind == LRG_NODE_VISUAL_PRIMITIVE)
		{
			g_autoptr(LrgMaterial3D) mat = lrg_material3d_new_with_color (0.75f, 0.75f, 0.78f, 1.0f);

			lrg_node_visual_set_primitive (visual, primitive);
			lrg_node_visual_set_material (visual, mat);
		}
		lrg_node_set_visual (node, visual);
	}

	/* New top-level nodes appear where the camera looks, on the ground */
	if (parent == NULL)
	{
		pos[0] = roundf (camera->view.target[0]);
		pos[2] = roundf (camera->view.target[2]);
		if (kind == LRG_NODE_VISUAL_PRIMITIVE && primitive != LRG_PRIMITIVE_PLANE &&
		    primitive != LRG_PRIMITIVE_GRID && !camera->view_2d)
			pos[1] = 0.5f;
	}
	if (camera->view_2d)
	{
		lrg_node_set_is_2d (node, TRUE);
		pos[1] = roundf (camera->view.target[1]);
		pos[2] = 0.0f;
	}
	lrg_node_set_location_xyz (node, pos[0], pos[1], pos[2]);

	lrg_editor_insert_node (self->editor, node, parent, -1);
	return node;   /* the level holds a reference */
}

/* A node of a Create-menu kind ("cube", "light"...), or NULL. */
LrgNode *
editor_app_create_node_kind (LrgEditorApp *self,
                             const gchar  *kind,
                             LrgNode      *parent)
{
	const EditorCreateItem *items;
	guint                   n, i;

	items = editor_create_items (&n);
	for (i = 0; i < n; i++)
	{
		LrgNode       *node;
		LrgNodeVisual *visual;
		gboolean       spot;

		if (items[i].name == NULL || g_strcmp0 (items[i].id, kind) != 0)
			continue;

		node = editor_app_create_node (self, items[i].name, items[i].kind, items[i].primitive, parent);
		visual = (node != NULL) ? lrg_node_get_visual (node) : NULL;

		/* Light presets: a spot light and a sun are light nodes with a
		 * light_type, tilted -90 degrees about X so their -Z axis points
		 * down, and lifted 4 m.  The node was just inserted, so these
		 * values belong to the object the create command restores. */
		spot = g_strcmp0 (kind, "spot-light") == 0;
		if (visual != NULL && (spot || g_strcmp0 (kind, "directional-light") == 0))
		{
			g_auto(GValue) type = G_VALUE_INIT;
			GrlVector3    *loc = lrg_node_get_location (node);

			g_value_init (&type, G_TYPE_STRING);
			g_value_set_string (&type, spot ? "spot" : "directional");
			lrg_node_visual_set_param (visual, "light_type", &type);
			if (spot)
			{
				lrg_node_visual_set_param_double (visual, "spot_angle", 45.0);
				lrg_node_visual_set_param_double (visual, "range", 12.0);
			}
			lrg_node_set_rotation_xyz (node, (gfloat)(-G_PI / 2.0), 0.0f, 0.0f);
			lrg_node_set_location_xyz (node, loc->x, 4.0f, loc->z);
		}
		return node;
	}
	return NULL;
}

/*
 * editor_app_place_asset:
 * @path: an asset file (absolute, or relative to the project)
 *
 * "Place in Scene" for the Assets and Sources panels: a prefab becomes a
 * linked instance, a model / texture / CAD part a node showing it (mesh
 * asset, sprite, CAD part), placed at the active camera's target point
 * as one undo step and selected. Paths inside the project are stored
 * relative to it.
 *
 * Returns: (transfer none) (nullable): the new node (the level holds it),
 *   or %NULL with @error set; %G_IO_ERROR_NOT_SUPPORTED for a file type
 *   that cannot be placed
 */
LrgNode *
editor_app_place_asset (LrgEditorApp  *self,
                        const gchar   *path,
                        GError       **error)
{
	g_autofree gchar    *abs = editor_app_project_path (self, path);
	LrgAssetType         type = lrg_asset_database_classify (abs);
	LrgEditorViewCamera *cam;
	g_autofree gchar    *rel = NULL;
	g_autofree gchar    *stem = NULL;
	LrgNode             *node = NULL;
	gchar               *dot;

	if (type != LRG_ASSET_TYPE_PREFAB && type != LRG_ASSET_TYPE_MODEL && type != LRG_ASSET_TYPE_TEXTURE &&
	    type != LRG_ASSET_TYPE_CAD)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "%s cannot be placed in the scene", abs);
		return NULL;
	}
	rel = (self->project_dir != NULL) ? editor_app_relative_path (self, abs) : g_strdup (abs);
	stem = g_path_get_basename (abs);
	dot = strrchr (stem, '.');
	if (dot != NULL)
		*dot = '\0';
	cam = lrg_editor_view_state_get_camera (self->view, -1);

	/* One undo step: create (or instantiate), set the asset, move */
	lrg_editor_begin_group (self->editor, "Place Asset");
	if (type == LRG_ASSET_TYPE_PREFAB)
		node = lrg_editor_instantiate_prefab (self->editor, abs, NULL, error);
	else
	{
		LrgNodeVisualKind kind = (type == LRG_ASSET_TYPE_MODEL) ? LRG_NODE_VISUAL_MESH_ASSET
		                       : (type == LRG_ASSET_TYPE_CAD) ? LRG_NODE_VISUAL_CAD_PART
		                                                      : LRG_NODE_VISUAL_SPRITE;

		node = editor_app_create_node (self, stem, kind, 0, NULL);
		if (node != NULL && lrg_node_get_visual (node) != NULL)
		{
			g_autoptr(LrgNode) before = lrg_editor_snapshot_node (node);

			lrg_node_visual_set_asset (lrg_node_get_visual (node), rel);
			lrg_editor_commit_node_state (self->editor, node, before, "Set Asset", FALSE);
		}
	}
	if (node != NULL && cam != NULL)
		lrg_editor_transform_node (self->editor, node, cam->view.target, NULL, NULL, FALSE);
	lrg_editor_end_group (self->editor);

	if (node == NULL)
	{
		if (error != NULL && *error == NULL)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not place %s", stem);
		return NULL;
	}
	lrg_editor_select (self->editor, node, FALSE);
	return node;
}

gchar *
editor_app_create_script (LrgEditorApp       *self,
                          const gchar        *name,
                          LrgScriptLanguage   lang,
                          GError            **error)
{
	const gchar      *ext;
	const gchar      *body;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *file = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *content = NULL;
	const gchar      *scripts = (self->project != NULL) ? lrg_project_get_scripts_dir (self->project) : "scripts";

	switch (lang)
	{
	case LRG_SCRIPT_LANGUAGE_PYTHON:
		ext = ".py";
		body = "# %s\n\ndef on_attach(obj):\n    pass\n\n\ndef on_update(obj, delta):\n    pass\n";
		break;
	case LRG_SCRIPT_LANGUAGE_GJS:
		ext = ".js";
		body = "// %s\n\nfunction on_attach(obj) {\n}\n\nfunction on_update(obj, delta) {\n}\n";
		break;
	case LRG_SCRIPT_LANGUAGE_CRISPY:
		ext = ".c";
		body = "/* %s */\n\n#include <libregnum.h>\n\nvoid\non_update (gpointer obj,\n           gdouble  delta)\n{\n}\n";
		break;
	case LRG_SCRIPT_LANGUAGE_ELISP:
		ext = ".el";
		body = ";;; %s -*- lexical-binding: t; -*-\n\n(defun on-update (obj delta)\n  nil)\n";
		break;
	default:
		ext = ".lua";
		body = "-- %s\n\nfunction on_attach(obj)\nend\n\nfunction on_update(obj, delta)\nend\n";
		break;
	}

	dir = editor_app_project_path (self, scripts);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
		             "Cannot create %s: %s", dir, g_strerror (errno));
		return NULL;
	}

	file = g_str_has_suffix (name, ext) ? g_strdup (name) : g_strconcat (name, ext, NULL);
	path = g_build_filename (dir, file, NULL);
	if (!g_file_test (path, G_FILE_TEST_EXISTS))
	{
		GString *text = g_string_new (body);

		/* The template's only placeholder is the file name. */
		g_string_replace (text, "%s", file, 1);
		content = g_string_free (text, FALSE);
		if (!g_file_set_contents (path, content, -1, error))
			return NULL;
	}

	return editor_app_relative_path (self, path);
}

/* ==========================================================================
 * Signals and file monitoring
 * ========================================================================== */

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	LRG_EDITOR_APP (user_data)->problems_dirty = TRUE;
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);

	self->problems_dirty = TRUE;
	self->timeline_key_valid = FALSE;
	g_hash_table_remove_all (self->timeline_rest);
	lrg_editor_view_state_set_timeline_previewing (self->view, FALSE);
	lrg_editor_view_state_set_timeline_playing (self->view, FALSE);
	lrg_editor_view_state_reset_for_level (self->view, level);
}

static void
on_level_file_changed (GFileMonitor      *monitor,
                       GFile             *file,
                       GFile             *other,
                       GFileMonitorEvent  event,
                       gpointer           user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);
	const gchar  *path = lrg_editor_get_path (self->editor);
	gboolean      reload = TRUE;

	if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT && event != G_FILE_MONITOR_EVENT_CREATED &&
	    event != G_FILE_MONITOR_EVENT_RENAMED && event != G_FILE_MONITOR_EVENT_MOVED_IN)
		return;
	if (path == NULL || file_mtime (path) == self->level_saved_mtime)
		return;   /* our own save */

	/* Edited elsewhere (e.g. the YAML in Emacs): reload silently when we
	 * have nothing to lose, otherwise ask in the status bar */
	g_object_get (self->settings, "reload-on-external-change", &reload, NULL);
	if (reload && !lrg_editor_is_dirty (self->editor))
		editor_app_perform_pending (self, EDITOR_PENDING_RELOAD, NULL);
	else
	{
		self->level_changed_on_disk = TRUE;
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "%s changed on disk", path);
	}
}

static void
watch_level_file (LrgEditorApp *self)
{
	const gchar      *path = lrg_editor_get_path (self->editor);
	g_autoptr(GFile)  file = NULL;

	if (self->level_monitor != NULL)
	{
		g_file_monitor_cancel (self->level_monitor);
		g_signal_handlers_disconnect_by_data (self->level_monitor, self);
		g_clear_object (&self->level_monitor);
	}
	if (path == NULL)
		return;

	/* Watch the file (editors often replace it atomically) */
	file = g_file_new_for_path (path);
	self->level_monitor = g_file_monitor_file (file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
	if (self->level_monitor != NULL)
		g_signal_connect (self->level_monitor, "changed", G_CALLBACK (on_level_file_changed), self);
}

/*
 * A file in a project folder changed: stale thumbnails and GPU copies go,
 * prefabs edited on disk sync their linked instances (a no-op when they
 * are already up to date, e.g. after our own save), and git status is
 * refreshed (debounced).
 */
static void
on_project_file_changed (GFileMonitor      *monitor,
                         GFile             *file,
                         GFile             *other,
                         GFileMonitorEvent  event,
                         gpointer           user_data)
{
	LrgEditorApp     *self = LRG_EDITOR_APP (user_data);
	g_autofree gchar *path = (file != NULL) ? g_file_get_path (file) : NULL;

	if (path == NULL || strstr (path, "/.cache/") != NULL || strstr (path, "/.lrg-editor/") != NULL)
		return;
	if (event == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED || event == G_FILE_MONITOR_EVENT_PRE_UNMOUNT ||
	    event == G_FILE_MONITOR_EVENT_UNMOUNTED)
		return;

	editor_thumbnails_file_changed (self, path);
	if (self->renderer != NULL)
		lrg_editor_scene_renderer_forget_asset (self->renderer, path);
	if (g_str_has_suffix (path, ".rprefab") &&
	    (event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event == G_FILE_MONITOR_EVENT_CREATED ||
	     event == G_FILE_MONITOR_EVENT_MOVED_IN || event == G_FILE_MONITOR_EVENT_RENAMED))
	{
		guint n = lrg_editor_prefab_changed (self->editor, path);

		if (n > 0)
			editor_app_set_status (self, "Synced %u prefab instance(s) with %s", n, path);
	}
	if (self->vcs != NULL)
		lrg_editor_vcs_queue_refresh (self->vcs);
}

static void
monitor_dir (LrgEditorApp *self,
             const gchar  *dir)
{
	g_autoptr(GFile) file = g_file_new_for_path (dir);
	GFileMonitor    *m;

	if (!g_file_test (dir, G_FILE_TEST_IS_DIR))
		return;
	m = g_file_monitor_directory (file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
	if (m == NULL)
		return;
	g_signal_connect (m, "changed", G_CALLBACK (on_project_file_changed), self);
	g_ptr_array_add (self->asset_monitors, m);
}

/* Monitors the project root and its asset folders (not recursive, as
 * before 0.8.1). */
static void
watch_project (LrgEditorApp *self)
{
	GPtrArray *dirs;
	guint      i;

	clear_monitors (self);
	if (self->project_dir == NULL)
		return;
	monitor_dir (self, self->project_dir);
	dirs = (self->project != NULL) ? lrg_project_get_asset_dirs (self->project) : NULL;
	for (i = 0; dirs != NULL && i < dirs->len; i++)
	{
		g_autofree gchar *abs = editor_app_project_path (self, g_ptr_array_index (dirs, i));

		if (g_strcmp0 (abs, self->project_dir) != 0)
			monitor_dir (self, abs);
	}
}

static void
autosave_tick (LrgEditorApp *self)
{
	guint             interval = 0;
	g_autofree gchar *path = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *data = NULL;

	g_object_get (self->settings, "autosave-interval", &interval, NULL);
	if (interval == 0 || !lrg_editor_is_dirty (self->editor) || editor_play_is_playing (self))
	{
		self->last_autosave = self->time;
		return;
	}
	if (self->time - self->last_autosave < interval)
		return;
	self->last_autosave = self->time;

	/* Untitled levels autosave into the project (or the working dir). */
	if (lrg_editor_get_path (self->editor) != NULL)
		path = autosave_path_for (self, lrg_editor_get_path (self->editor));
	else
	{
		g_autofree gchar *base = (self->project_dir != NULL) ? g_strdup (self->project_dir)
		                                                     : g_get_current_dir ();
		g_autofree gchar *fake = g_build_filename (base, "untitled.rlevel", NULL);

		path = autosave_path_for (self, fake);
	}

	dir = g_path_get_dirname (path);
	data = lrg_editor_serialize (self->editor);
	if (g_mkdir_with_parents (dir, 0755) == 0 && data != NULL &&
	    g_file_set_contents (path, data, -1, NULL))
		editor_app_set_status (self, "Autosaved to %s", path);
}

/* ==========================================================================
 * Frame
 * ========================================================================== */

static void
update_title (LrgEditorApp *self)
{
	const gchar      *path = lrg_editor_get_path (self->editor);
	g_autofree gchar *base = (path != NULL) ? g_path_get_basename (path) : g_strdup ("untitled");
	g_autofree gchar *title = g_strdup_printf ("%s%s%s%s - libregnum editor",
	                                           lrg_editor_is_dirty (self->editor) ? "\xe2\x97\x8f " : "",
	                                           base,
	                                           self->project != NULL ? " - " : "",
	                                           self->project != NULL ? lrg_project_get_name (self->project) : "");

	if (g_strcmp0 (title, self->window_title) == 0)
		return;

	g_free (self->window_title);
	self->window_title = g_steal_pointer (&title);
	if (self->window_open)
		SetWindowTitle (self->window_title);
}

/* Validates the level now (Tools > Validate, debounced edits). */
void
editor_app_validate_now (LrgEditorApp *self)
{
	LrgEngine *engine = lrg_engine_get_default ();

	g_ptr_array_unref (self->problems);
	self->problems = lrg_editor_validate_level (lrg_editor_get_level (self->editor), editor_app_base_dir (self),
	                                            engine != NULL ? lrg_engine_get_registry (engine) : NULL);
	self->problems_dirty = FALSE;
	self->problems_time = self->time;
}

/* Timers that do not depend on the UI (frames and the headless loop). */
static void
tick_timers (LrgEditorApp *self,
             gfloat        delta)
{
	lrg_editor_play_session_tick (self->play, delta);
	editor_timeline_tick (self, delta);
	editor_thumbnails_pump (self);
	autosave_tick (self);
	if (self->problems_dirty && self->time - self->problems_time > PROBLEMS_DEBOUNCE)
		editor_app_validate_now (self);
}

void
lrg_editor_app_process_frame (LrgEditorApp         *self,
                              const LrgCanvasInput *input,
                              gint                  width,
                              gint                  height)
{
	LrgProfiler *prof;

	g_return_if_fail (LRG_IS_EDITOR_APP (self));
	g_return_if_fail (input != NULL);

	/* Editor phases show in the profiler panel (no-ops while it is off) */
	prof = lrg_profiler_get_default ();
	lrg_profiler_begin_frame (prof);

	/* Clock and size */
	self->width = MAX (width, 1);
	self->height = MAX (height, 1);
	self->time += MAX (input->delta_time, 0.0f);
	self->frames++;
	if (input->delta_time > 0.0f)
		self->fps = self->fps * 0.95f + (1.0f / input->delta_time) * 0.05f;
	self->last_input = *input;

	lrg_widget_set_size (LRG_WIDGET (self->canvas), (gfloat) self->width, (gfloat) self->height);
	lrg_widget_set_size (LRG_WIDGET (self->window), (gfloat) self->width, (gfloat) self->height);
	lrg_widget_set_size (LRG_WIDGET (lrg_editor_window_get_toasts (self->window)),
	                     (gfloat) self->width, (gfloat) self->height);

	/* Model -> widgets (enabled / checked states, shortcuts, status
	 * bar), then a layout pass so input hits this frame's rectangles */
	lrg_profiler_begin_section (prof, "ui.update");
	editor_window_set_menus_enabled (self->window, lrg_editor_view_state_get_dialog (self->view) == NULL);
	editor_window_refresh (self->window);
	lrg_widget_layout (LRG_WIDGET (self->canvas));
	lrg_profiler_end_section (prof, "ui.update");

	/* Input: shortcuts, menus, dialogs, the viewport; advances springs */
	lrg_profiler_begin_section (prof, "ui.input");
	lrg_canvas_process_input (self->canvas, input);
	lrg_profiler_end_section (prof, "ui.input");
	editor_viewport_panel_tick (self->viewport_panel, input->delta_time);

	tick_timers (self, input->delta_time);
	update_title (self);

	/* Headless, record so popups and widgets are laid out for ui.state */
	if (!self->window_open)
		lrg_canvas_record (self->canvas);

	if (self->quit_requested && lrg_editor_view_state_get_dialog (self->view) == NULL &&
	    !lrg_editor_is_dirty (self->editor))
		self->quit_now = TRUE;

	lrg_profiler_end_frame (prof);
}

void
lrg_editor_app_frame (LrgEditorApp      *self,
                      const LrgGuiInput *input,
                      gfloat             width,
                      gfloat             height)
{
	LrgCanvasInput frame;

	g_return_if_fail (LRG_IS_EDITOR_APP (self));
	g_return_if_fail (input != NULL);

	if (self->legacy == NULL)
		self->legacy = editor_legacy_input_new ();
	editor_legacy_input_translate (self->legacy, input, &frame);
	lrg_editor_app_process_frame (self, &frame, (gint) width, (gint) height);
}

/* ==========================================================================
 * Server
 * ========================================================================== */

gboolean
lrg_editor_app_start_server (LrgEditorApp  *self,
                             const gchar   *socket_path,
                             GError       **error)
{
	g_autofree gchar *configured = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);

	g_object_get (self->settings, "server-socket", &configured, NULL);
	if (socket_path == NULL && configured != NULL && *configured != '\0')
		socket_path = configured;

	if (!lrg_editor_server_start (self->server, socket_path, error))
		return FALSE;

	lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "editor",
	                       lrg_editor_server_get_socket_path (self->server));
	return TRUE;
}

/* ==========================================================================
 * Main loops
 * ========================================================================== */

static gboolean
has_display (void)
{
	const gchar *x = g_getenv ("DISPLAY");
	const gchar *w = g_getenv ("WAYLAND_DISPLAY");

	return (x != NULL && *x != '\0') || (w != NULL && *w != '\0');
}

void
lrg_editor_app_set_window_size (LrgEditorApp *self,
                                gint          width,
                                gint          height)
{
	g_return_if_fail (LRG_IS_EDITOR_APP (self));

	self->width = MAX (width, 320);
	self->height = MAX (height, 240);
}

/*
 * editor_frame_cap:
 *
 * The editor's frame cap for a monitor refreshing at @refresh Hz: the
 * largest whole fraction of the refresh rate that is at most 75 fps
 * (60 on 60/120 Hz, 72 on 144 Hz, 55 on 165 Hz). With vsync on, a cap
 * that does not divide the refresh rate (60 on 144 Hz) alternates two-
 * and three-vblank frames, which judders; an even divisor paces evenly.
 * Unknown refresh rates get 60.
 */
static gint
editor_frame_cap (gint refresh)
{
	gint divisor;

	if (refresh <= 0)
		return 60;

	for (divisor = 1; refresh / divisor > 75; divisor++)
		;
	return MAX (refresh / divisor, 30);
}

/* Frames in flight for the editor window (see LrgGameTemplate:max-frames-in-flight) */
#define EDITOR_MAX_FRAMES_IN_FLIGHT (2)

/* LRG_CPU_AFFINITY=none (or off, false, 0) keeps the default affinity */
static gboolean
editor_cpu_affinity_disabled (void)
{
	const gchar *value = g_getenv ("LRG_CPU_AFFINITY");

	return value != NULL &&
	       (g_ascii_strcasecmp (value, "none") == 0 ||
	        g_ascii_strcasecmp (value, "off") == 0 ||
	        g_ascii_strcasecmp (value, "false") == 0 ||
	        g_strcmp0 (value, "0") == 0);
}

gint
lrg_editor_app_run (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), 1);

	if (!has_display ())
	{
		g_printerr ("lrgeditor: no graphical display (DISPLAY/WAYLAND_DISPLAY unset); "
		            "use --headless to run the IPC server only.\n");
		return 1;
	}

	/* Keep the UI thread (and the GL driver threads the window starts) on
	 * a hybrid CPU's fast cores, as LrgEngine does for games; honours
	 * LRG_CPU_AFFINITY=none */
	if (!editor_cpu_affinity_disabled ())
		lrg_cpu_affinity_pin_to_performance_cores (NULL);

	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
	SetTraceLogLevel (LOG_WARNING);
	InitWindow (self->width, self->height, "libregnum editor");
	if (!IsWindowReady ())
	{
		g_printerr ("lrgeditor: could not open a window\n");
		return 1;
	}
	SetWindowMinSize (960, 600);
	SetExitKey (KEY_NULL);
	SetTargetFPS (editor_frame_cap (GetMonitorRefreshRate (GetCurrentMonitor ())));
	/* Bound the frames queued ahead of the GPU, as games do (one frame
	 * of CPU/GPU overlap); LRG_MAX_FRAMES_IN_FLIGHT overrides it */
	grl_frame_sync_set_max_frames_in_flight (
		lrg_grl_window_resolve_max_frames_in_flight (EDITOR_MAX_FRAMES_IN_FLIGHT));
	self->window_open = TRUE;
	g_clear_pointer (&self->window_title, g_free);
	editor_thumbnails_window_opened (self);

	while (!self->quit_now)
	{
		GMainContext   *ctx = g_main_context_default ();
		LrgCanvasInput  input;

		/* Keep IPC, monitors, file dialogs and subprocess reaping alive */
		while (g_main_context_iteration (ctx, FALSE))
			;

		if (WindowShouldClose ())
			lrg_editor_app_request_quit (self);

		lrg_canvas_input_poll (&input, GetFrameTime ());
		lrg_editor_app_process_frame (self, &input, GetScreenWidth (), GetScreenHeight ());

		/* The canvas steps the viewport (its render texture is drawn
		 * there), so drawing is just the UI pass */
		BeginDrawing ();
		ClearBackground ((Color) { 17, 17, 27, 255 });
		lrg_canvas_render (self->canvas);
		if (self->window_shot != NULL)
		{
			Image shot = LoadImageFromScreen ();

			if (!ExportImage (shot, self->window_shot))
				editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Could not write %s", self->window_shot);
			UnloadImage (shot);
			g_clear_pointer (&self->window_shot, g_free);
		}
		/* EndDrawing() with the frame-sync wait before its swap */
		grl_frame_sync_end_drawing ();
	}

	{
		g_autoptr(GError) error = NULL;

		if (!lrg_editor_app_save_settings (self, &error))
			g_printerr ("lrgeditor: could not save settings: %s\n", error->message);
	}

	/* GL objects go before the context: thumbnails, the scene renderer,
	 * the viewport's render texture (leaving render mode frees it) */
	editor_dialog_close_all (self);
	editor_thumbnails_window_closing (self);
	lrg_editor_scene_renderer_release (self->renderer);
	lrg_editor_viewport_split_set_gpu_enabled (self->viewport_split, FALSE);
	lrg_canvas_input_detach_latch ();
	grl_frame_sync_reset ();	/* its fences belong to the context closing */
	grl_fast2d_shutdown ();	/* graylib's 2D batch, ditto */
	CloseWindow ();
	self->window_open = FALSE;
	lrg_editor_viewport_split_set_gpu_enabled (self->viewport_split, TRUE);
	return 0;
}

static gboolean
on_quit_signal (gpointer user_data)
{
	g_main_loop_quit (user_data);
	return G_SOURCE_CONTINUE;
}

typedef struct
{
	LrgEditorApp *self;
	GMainLoop    *loop;
	gint64        last;
} HeadlessData;

/* The headless loop has no frames: timers run every 100 ms (play,
 * timeline playback, thumbnails, autosave, validation). view.*, play.*
 * and timeline.* IPC act on the same headless objects as the UI. */
static gboolean
headless_tick (gpointer user_data)
{
	HeadlessData *data = user_data;
	gint64        now = g_get_monotonic_time ();
	gfloat        delta = (data->last > 0) ? (gfloat) (now - data->last) / (gfloat) G_USEC_PER_SEC : 0.1f;

	data->last = now;
	data->self->time = now / (gdouble) G_USEC_PER_SEC;
	tick_timers (data->self, MIN (delta, 0.1f));
	if (data->self->quit_now)
		g_main_loop_quit (data->loop);
	return G_SOURCE_CONTINUE;
}

gint
lrg_editor_app_run_headless (LrgEditorApp *self)
{
	g_autoptr(GMainLoop) loop = NULL;
	HeadlessData         data;
	guint                sigint, sigterm, tick;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), 1);

	loop = g_main_loop_new (NULL, FALSE);
	data.self = self;
	data.loop = loop;
	data.last = 0;

	sigint = g_unix_signal_add (SIGINT, on_quit_signal, loop);
	sigterm = g_unix_signal_add (SIGTERM, on_quit_signal, loop);
	tick = g_timeout_add (100, headless_tick, &data);

	g_main_loop_run (loop);

	g_source_remove (tick);
	g_source_remove (sigint);
	g_source_remove (sigterm);
	return 0;
}

LrgEditorSceneRenderer *
lrg_editor_app_get_scene_renderer (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);

	return self->renderer;
}
