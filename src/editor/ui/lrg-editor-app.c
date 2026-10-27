/* lrg-editor-app.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The libregnum editor application: lifecycle, documents, the per-frame
 * UI pass (menu bar, toolbar, dock with panels, status bar, toasts),
 * autosave and crash recovery, file monitoring, and the window and
 * headless main loops. Panels, dialogs, actions, rendering and play mode
 * live in their own files (see lrg-editor-app-private.h).
 */

#include "lrg-editor-app-private.h"
#include "../../core/lrg-engine.h"
#include "../../core/lrg-cpu-affinity.h"
#include "../../gui/lrg-gui-raylib-private.h"
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define AUTOSAVE_DIR          ".lrg-editor/autosave"
#define TOAST_SECONDS         (4.0)
#define PROBLEMS_DEBOUNCE     (0.4)

G_DEFINE_FINAL_TYPE (LrgEditorApp, lrg_editor_app, G_TYPE_OBJECT)

static void on_editor_changed (LrgEditor *editor, gpointer user_data);
static void on_level_replaced (LrgEditor *editor, LrgLevel *level, gpointer user_data);
static void watch_level_file  (LrgEditorApp *self);
static void perform_pending   (LrgEditorApp *self, EditorPending what, const gchar *path);

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
toast_free (gpointer data)
{
	EditorToast *t = data;

	g_free (t->text);
	g_free (t);
}

static void
lrg_editor_app_dispose (GObject *object)
{
	LrgEditorApp *self = LRG_EDITOR_APP (object);

	if (self->playing)
		editor_play_stop (self);
	editor_run_stop (self);

	if (self->server != NULL)
		lrg_editor_server_stop (self->server);
	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	if (self->asset_monitor != NULL)
	{
		g_file_monitor_cancel (self->asset_monitor);
		g_signal_handlers_disconnect_by_data (self->asset_monitor, self);
		g_clear_object (&self->asset_monitor);
	}
	if (self->level_monitor != NULL)
	{
		g_file_monitor_cancel (self->level_monitor);
		g_signal_handlers_disconnect_by_data (self->level_monitor, self);
		g_clear_object (&self->level_monitor);
	}

	editor_render_shutdown (self);

	g_clear_object (&self->server);
	g_clear_object (&self->gizmo);
	g_clear_object (&self->dock);
	g_clear_object (&self->backend);
	g_clear_object (&self->gui);
	g_clear_object (&self->actions);
	g_clear_object (&self->external);
	g_clear_object (&self->log);
	g_clear_object (&self->project);
	g_clear_object (&self->settings);
	g_clear_object (&self->editor);
	g_clear_object (&self->play_world);

	G_OBJECT_CLASS (lrg_editor_app_parent_class)->dispose (object);
}

static void
lrg_editor_app_finalize (GObject *object)
{
	LrgEditorApp *self = LRG_EDITOR_APP (object);

	g_free (self->project_dir);
	g_free (self->settings_path);
	g_free (self->applied_theme);
	g_string_free (self->hierarchy_filter, TRUE);
	g_free (self->rename_guid);
	g_string_free (self->rename_buf, TRUE);
	g_free (self->range_anchor);
	g_ptr_array_unref (self->visible_rows);
	g_clear_pointer (&self->hierarchy_shown, g_hash_table_unref);
	g_free (self->hierarchy_shown_needle);
	g_string_free (self->component_search, TRUE);
	g_string_free (self->param_key, TRUE);
	g_string_free (self->scratch, TRUE);
	g_free (self->asset_dir);
	g_string_free (self->asset_filter, TRUE);
	g_ptr_array_unref (self->asset_entries);
	g_free (self->asset_selected);
	g_free (self->asset_rename);
	g_string_free (self->asset_rename_buf, TRUE);
	g_string_free (self->console_input, TRUE);
	g_string_free (self->console_filter, TRUE);
	g_ptr_array_unref (self->console_history);
	g_clear_pointer (&self->console_shown, g_ptr_array_unref);
	g_clear_pointer (&self->console_rows, g_ptr_array_unref);
	g_free (self->console_shown_needle);
	g_ptr_array_unref (self->problems);
	g_free (self->file_dir);
	g_string_free (self->file_name, TRUE);
	g_ptr_array_unref (self->file_entries);
	g_free (self->file_selected);
	g_free (self->file_ext);
	g_free (self->file_target_guid);
	g_free (self->pending_path);
	g_free (self->confirm_message);
	g_string_free (self->dialog_text1, TRUE);
	g_string_free (self->dialog_text2, TRUE);
	g_string_free (self->dialog_text3, TRUE);
	g_string_free (self->dialog_text4, TRUE);
	g_string_free (self->dialog_text5, TRUE);
	g_free (self->capture_action);
	g_string_free (self->palette_query, TRUE);
	g_ptr_array_unref (self->quick_files);
	g_free (self->recovery_file);
	g_free (self->play_snapshot);
	g_clear_pointer (&self->play_entries, g_hash_table_unref);
	g_free (self->window_shot);
	g_free (self->status);
	g_queue_clear_full (&self->toasts, toast_free);
	g_free (self->window_title);
	g_free (self->level_dir);

	G_OBJECT_CLASS (lrg_editor_app_parent_class)->finalize (object);
}

static void
lrg_editor_app_class_init (LrgEditorAppClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_editor_app_dispose;
	object_class->finalize = lrg_editor_app_finalize;
}

/* The layout new users get (and View > Reset Layout restores). */
void
editor_app_reset_layout (LrgEditorApp *self)
{
	g_clear_object (&self->dock);
	self->dock = lrg_gui_dock_new ();

	lrg_gui_dock_dock_panel (self->dock, "viewport", NULL, LRG_GUI_DOCK_CENTER, 0.0f);
	lrg_gui_dock_dock_panel (self->dock, "hierarchy", "viewport", LRG_GUI_DOCK_LEFT, 0.18f);
	lrg_gui_dock_dock_panel (self->dock, "inspector", "viewport", LRG_GUI_DOCK_RIGHT, 0.27f);
	lrg_gui_dock_dock_panel (self->dock, "assets", "viewport", LRG_GUI_DOCK_BOTTOM, 0.30f);
	lrg_gui_dock_dock_panel (self->dock, "console", "assets", LRG_GUI_DOCK_CENTER, 0.0f);
	lrg_gui_dock_dock_panel (self->dock, "problems", "assets", LRG_GUI_DOCK_CENTER, 0.0f);
	lrg_gui_dock_dock_panel (self->dock, "history", "inspector", LRG_GUI_DOCK_CENTER, 0.0f);
	lrg_gui_dock_open_panel (self->dock, "assets");
	lrg_gui_dock_open_panel (self->dock, "inspector");

	lrg_gui_dock_set_closable (self->dock, "viewport", FALSE);
	lrg_gui_dock_set_panel_flags (self->dock, "viewport", LRG_GUI_REGION_NO_PADDING);
	lrg_gui_dock_set_panel_flags (self->dock, "console", LRG_GUI_REGION_NONE);
	lrg_gui_dock_set_panel_flags (self->dock, "assets", LRG_GUI_REGION_NONE);
	lrg_gui_dock_set_panel_flags (self->dock, "hierarchy", LRG_GUI_REGION_NONE);
}

static void
lrg_editor_app_init (LrgEditorApp *self)
{
	self->hierarchy_filter = g_string_new (NULL);
	self->rename_buf = g_string_new (NULL);
	self->visible_rows = g_ptr_array_new ();
	self->component_search = g_string_new (NULL);
	self->param_key = g_string_new (NULL);
	self->scratch = g_string_new (NULL);
	self->asset_filter = g_string_new (NULL);
	self->asset_entries = g_ptr_array_new_with_free_func (editor_file_entry_free);
	self->asset_rename_buf = g_string_new (NULL);
	self->console_input = g_string_new (NULL);
	self->console_filter = g_string_new (NULL);
	self->console_levels = G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING |
	                       G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO;
	self->console_autoscroll = TRUE;
	self->console_history = g_ptr_array_new_with_free_func (g_free);
	self->console_history_pos = -1;
	self->problems = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_problem_free);
	self->problems_dirty = TRUE;
	self->file_name = g_string_new (NULL);
	self->file_entries = g_ptr_array_new_with_free_func (editor_file_entry_free);
	self->dialog_text1 = g_string_new (NULL);
	self->dialog_text2 = g_string_new (NULL);
	self->dialog_text3 = g_string_new (NULL);
	self->dialog_text4 = g_string_new (NULL);
	self->dialog_text5 = g_string_new (NULL);
	self->palette_query = g_string_new (NULL);
	self->quick_files = g_ptr_array_new_with_free_func (g_free);
	g_queue_init (&self->toasts);

	self->show_grid = TRUE;
	self->show_icons = TRUE;
	self->show_stats = TRUE;
	self->fly_speed = 8.0f;
	self->nav_axis = -1;
	self->axis_hot = -1;
	self->width = 1600.0f;
	self->height = 960.0f;
	self->headless = TRUE;   /* until a window exists */
	self->assets_dirty = TRUE;

	lrg_gui_input_init (&self->input);
	lrg_editor_view_init_default (&self->view, 800.0f, 600.0f);
	editor_app_reset_layout (self);
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
	self->gui = lrg_gui_context_new ();
	self->gizmo = lrg_editor_gizmo_new (self->editor);
	self->server = lrg_editor_server_new (self->editor);
	lrg_editor_server_set_external_editor (self->server, self->external);
	lrg_editor_server_set_log (self->server, self->log);

	g_signal_connect (self->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);

	editor_app_register_actions (self);
	editor_app_register_methods (self);
	editor_app_apply_settings (self);
	{
		g_autofree gchar *cwd = g_get_current_dir ();

		editor_assets_set_dir (self, cwd);
	}
	editor_view_reset (self);

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
	return self->gui;
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
	return self->status;
}

gboolean
lrg_editor_app_is_playing (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);
	return self->playing;
}

gboolean
lrg_editor_app_activate_action (LrgEditorApp *self,
                                const gchar  *id)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);
	return lrg_editor_actions_activate (self->actions, id);
}

/* ==========================================================================
 * Messages
 * ========================================================================== */

void
editor_app_set_status (LrgEditorApp *self,
                       const gchar  *format,
                       ...)
{
	va_list args;

	g_free (self->status);
	va_start (args, format);
	self->status = g_strdup_vprintf (format, args);
	va_end (args);
}

/* A toast plus a log line (and the status bar). @level: 0 info, 1 ok,
 * 2 warning, 3 error. */
void
editor_app_notify (LrgEditorApp *self,
                   gint          level,
                   const gchar  *format,
                   ...)
{
	EditorToast *toast;
	va_list      args;
	gchar       *text;

	va_start (args, format);
	text = g_strdup_vprintf (format, args);
	va_end (args);

	lrg_editor_log_append (self->log,
	                       level >= 3 ? G_LOG_LEVEL_WARNING : G_LOG_LEVEL_MESSAGE,
	                       "editor", text);
	g_free (self->status);
	self->status = g_strdup (text);

	toast = g_new0 (EditorToast, 1);
	toast->text = text;
	toast->level = level;
	toast->expires = self->time + TOAST_SECONDS + (level >= 3 ? 3.0 : 0.0);
	g_queue_push_tail (&self->toasts, toast);
	while (g_queue_get_length (&self->toasts) > 5)
		toast_free (g_queue_pop_head (&self->toasts));
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

gboolean
editor_app_ensure_project_dir (LrgEditorApp *self)
{
	return self->project_dir != NULL;
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

void
editor_app_apply_settings (LrgEditorApp *self)
{
	g_autofree gchar *theme = NULL;
	g_autofree gchar *layout = NULL;
	g_autofree gchar *font_path = NULL;
	gdouble           scale, st, sr, ss, fly;
	gint              font_size;
	guint             history;
	gboolean          snap;
	LrgGuiStyle      *style;

	g_object_get (self->settings,
	              "theme", &theme, "ui-scale", &scale, "font-size", &font_size,
	              "font-path", &font_path, "layout", &layout,
	              "snap-enabled", &snap, "snap-translate", &st,
	              "snap-rotate-degrees", &sr, "snap-scale", &ss,
	              "history-limit", &history, "camera-fly-speed", &fly,
	              "grid-visible", &self->show_grid,
	              NULL);

	/* Theme and metrics; fonts follow the scaled size. */
	style = lrg_gui_context_get_style (self->gui);
	lrg_gui_style_init (style, theme, (gfloat) scale * (gfloat) font_size / 15.0f);
	if (self->backend != NULL &&
	    (self->applied_scale != style->font_size || g_strcmp0 (self->applied_theme, theme) != 0))
		lrg_gui_raylib_load_fonts (self->backend, font_path, NULL, style->font_size);
	self->applied_scale = style->font_size;
	g_free (self->applied_theme);
	self->applied_theme = g_strdup (theme);

	lrg_editor_set_snap_enabled (self->editor, snap);
	lrg_editor_set_snap (self->editor, st, sr * G_PI / 180.0, ss);
	lrg_editor_set_history_limit (self->editor, history);
	self->fly_speed = (gfloat) fly;

	if (layout != NULL && *layout != '\0')
	{
		g_autoptr(GError) error = NULL;

		if (!lrg_gui_dock_load_string (self->dock, layout, &error))
			lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "editor", error->message);
		else
		{
			/* The viewport can never be lost. */
			if (!lrg_gui_dock_has_panel (self->dock, "viewport"))
				lrg_gui_dock_open_panel (self->dock, "viewport");
		}
	}
}

gboolean
lrg_editor_app_load_settings (LrgEditorApp  *self,
                              const gchar   *path,
                              GError       **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);

	g_free (self->settings_path);
	self->settings_path = g_strdup (path);
	if (!lrg_editor_settings_load (self->settings, path, error))
		return FALSE;

	editor_app_apply_settings (self);
	return TRUE;
}

gboolean
lrg_editor_app_save_settings (LrgEditorApp  *self,
                              GError       **error)
{
	g_autofree gchar *layout = NULL;
	gdouble           st, sr, ss;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);

	/* Persist the live layout and snap state with the preferences. */
	layout = lrg_gui_dock_to_string (self->dock);
	lrg_editor_get_snap (self->editor, &st, &sr, &ss);
	g_object_set (self->settings,
	              "layout", layout,
	              "snap-enabled", lrg_editor_get_snap_enabled (self->editor),
	              "snap-translate", st,
	              "snap-rotate-degrees", sr * 180.0 / G_PI,
	              "snap-scale", ss,
	              "grid-visible", self->show_grid,
	              NULL);

	return lrg_editor_settings_save (self->settings, self->settings_path, error);
}

/* ==========================================================================
 * Projects and levels
 * ========================================================================== */

static void
set_project_dir (LrgEditorApp *self,
                 const gchar  *dir)
{
	g_free (self->project_dir);
	self->project_dir = g_canonicalize_filename (dir, NULL);
	lrg_external_editor_set_project_dir (self->external, self->project_dir);
	lrg_editor_server_set_project_dir (self->server, self->project_dir);
	editor_assets_set_dir (self, self->project_dir);
	g_ptr_array_set_size (self->quick_files, 0);
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

	/* A directory without a manifest is still usable as a loose project. */
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

	/* Open the default level if nothing is open yet. */
	if (lrg_editor_get_path (self->editor) == NULL && !lrg_editor_is_dirty (self->editor) &&
	    lrg_project_get_default_level (project) != NULL)
	{
		g_autofree gchar *level = lrg_project_resolve (project, lrg_project_get_default_level (project));
		g_autoptr(GError) lerr = NULL;

		if (g_file_test (level, G_FILE_TEST_EXISTS) &&
		    !lrg_editor_app_open_level (self, level, &lerr))
			editor_app_notify (self, 3, "Could not open the default level: %s", lerr->message);
	}

	editor_app_notify (self, 1, "Opened project %s", lrg_project_get_name (project));
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

	if (self->playing)
		editor_play_stop (self);

	abs = editor_app_project_path (self, path);
	if (!lrg_editor_load_level (self->editor, abs, error))
		return FALSE;

	/* A level outside any project: treat its parent as the root for
	 * relative asset paths and the browser. */
	if (self->project_dir == NULL)
	{
		g_autofree gchar *dir = g_path_get_dirname (abs);

		editor_assets_set_dir (self, dir);
	}

	lrg_editor_settings_add_recent_level (self->settings, abs);
	self->level_saved_mtime = file_mtime (abs);
	self->level_changed_on_disk = FALSE;
	watch_level_file (self);

	/* Offer crash recovery when the autosave is newer than the file. */
	autosave = autosave_path_for (self, abs);
	if (autosave != NULL && file_mtime (autosave) > self->level_saved_mtime)
	{
		g_free (self->recovery_file);
		self->recovery_file = g_strdup (autosave);
		if (!self->headless)
			editor_dialog_open (self, EDITOR_DIALOG_RECOVER);
		else
			editor_app_notify (self, 2, "A newer autosave exists: %s", autosave);
	}

	editor_app_set_status (self, "Opened %s", abs);
	return TRUE;
}

gboolean
editor_app_save (LrgEditorApp *self)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *autosave = NULL;

	if (lrg_editor_get_path (self->editor) == NULL)
	{
		g_autofree gchar *dir = (self->project_dir != NULL)
		                        ? g_build_filename (self->project_dir, "levels", NULL)
		                        : g_get_current_dir ();

		editor_file_dialog_open (self, EDITOR_FILE_SAVE_LEVEL_AS, dir, ".rlevel");
		return FALSE;
	}

	if (!lrg_editor_save (self->editor, &error))
	{
		editor_app_notify (self, 3, "Save failed: %s", error->message);
		return FALSE;
	}

	/* Our own write must not trigger the "changed on disk" prompt. */
	self->level_saved_mtime = file_mtime (lrg_editor_get_path (self->editor));
	self->level_changed_on_disk = FALSE;
	autosave = autosave_path_for (self, lrg_editor_get_path (self->editor));
	if (autosave != NULL)
		g_unlink (autosave);

	editor_app_notify (self, 1, "Saved %s", lrg_editor_get_path (self->editor));
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
		perform_pending (self, what, pending_path);
	return TRUE;
}

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

/* Carries out a document operation that may discard changes. */
static void
perform_pending (LrgEditorApp  *self,
                 EditorPending  what,
                 const gchar   *path)
{
	g_autoptr(GError) error = NULL;

	switch (what)
	{
	case EDITOR_PENDING_NEW_LEVEL:
		if (self->playing)
			editor_play_stop (self);
		lrg_editor_new_level (self->editor, "untitled");
		watch_level_file (self);
		editor_app_set_status (self, "New level");
		break;
	case EDITOR_PENDING_OPEN_LEVEL:
		if (!lrg_editor_app_open_level (self, path, &error))
			editor_app_notify (self, 3, "Cannot open %s: %s", path, error->message);
		break;
	case EDITOR_PENDING_OPEN_PROJECT:
		lrg_editor_new_level (self->editor, "untitled");
		g_clear_object (&self->project);
		g_clear_pointer (&self->project_dir, g_free);
		if (!lrg_editor_app_open_project (self, path, &error))
			editor_app_notify (self, 3, "Cannot open project: %s", error->message);
		break;
	case EDITOR_PENDING_RELOAD:
		if (!lrg_editor_reload (self->editor, &error))
			editor_app_notify (self, 3, "Reload failed: %s", error->message);
		else
		{
			self->level_saved_mtime = file_mtime (lrg_editor_get_path (self->editor));
			self->level_changed_on_disk = FALSE;
			editor_app_notify (self, 0, "Reloaded %s", lrg_editor_get_path (self->editor));
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

	/* Ask before throwing away unsaved work (not possible headless). */
	if (lrg_editor_is_dirty (self->editor) && confirm && !self->headless)
	{
		self->pending = what;
		g_free (self->pending_path);
		self->pending_path = g_strdup (path);
		g_free (self->confirm_message);
		self->confirm_message = g_strdup_printf ("\"%s\" has unsaved changes.",
		                                         lrg_editor_get_path (self->editor) != NULL
		                                         ? lrg_editor_get_path (self->editor) : "untitled");
		editor_dialog_open (self, EDITOR_DIALOG_CONFIRM);
		return;
	}

	perform_pending (self, what, path);
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
		/* Untitled: Save As first, then carry on (editor_app_save_as). */
		editor_app_save (self);
		self->pending = what;
		self->pending_path = g_steal_pointer (&path);
		return;
	}
	if (choice == 0 && !editor_app_save (self))
	{
		self->quit_requested = FALSE;
		return;   /* the save failed */
	}

	perform_pending (self, what, path);
}

/* ==========================================================================
 * Opening files
 * ========================================================================== */

void
editor_app_open_file (LrgEditorApp *self,
                      const gchar  *path,
                      gint          line,
                      gint          col)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *abs = editor_app_project_path (self, path);

	if (!lrg_external_editor_open (self->external, abs, line, col, &error))
		editor_app_notify (self, 3, "Cannot open %s: %s", abs, error->message);
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
				editor_app_notify (self, 3, "No application for %s%s%s", abs,
				                   error != NULL ? ": " : "", error != NULL ? error->message : "");
		}
		break;
	default:
		editor_app_open_file (self, abs, 1, 1);
		break;
	}
}

/* ==========================================================================
 * Nodes and views
 * ========================================================================== */

LrgNode *
editor_app_create_node (LrgEditorApp      *self,
                        const gchar       *name,
                        LrgNodeVisualKind  kind,
                        LrgPrimitiveType   primitive,
                        LrgNode           *parent)
{
	g_autoptr(LrgNode) node = lrg_node_new (name);
	LrgNode           *target = parent;
	gfloat             pos[3] = { 0.0f, 0.0f, 0.0f };

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

	/* New top-level nodes appear where the camera looks, on the ground. */
	if (target == NULL)
	{
		pos[0] = roundf (self->view.target[0]);
		pos[2] = roundf (self->view.target[2]);
		if (kind == LRG_NODE_VISUAL_PRIMITIVE && primitive != LRG_PRIMITIVE_PLANE &&
		    primitive != LRG_PRIMITIVE_GRID && !self->view_2d)
			pos[1] = 0.5f;
	}
	if (self->view_2d)
	{
		lrg_node_set_is_2d (node, TRUE);
		pos[1] = roundf (self->view.target[1]);
		pos[2] = 0.0f;
	}
	lrg_node_set_location_xyz (node, pos[0], pos[1], pos[2]);

	lrg_editor_insert_node (self->editor, node, target, -1);
	self->reveal_pending = TRUE;
	return node;   /* the level holds a reference */
}

void
editor_app_frame_selection (LrgEditorApp *self)
{
	g_autoptr(GPtrArray) roots = lrg_editor_get_selected_roots (self->editor);
	gfloat               center[3], radius;

	if (roots->len == 0)
	{
		editor_app_frame_all (self);
		return;
	}
	if (lrg_editor_compute_bounds (roots, center, &radius))
		lrg_editor_view_frame (&self->view, center, radius);
}

void
editor_app_frame_all (LrgEditorApp *self)
{
	GPtrArray *top = lrg_node_get_children (lrg_level_get_root (lrg_editor_get_level (self->editor)));
	gfloat     center[3], radius;

	if (lrg_editor_compute_bounds (top, center, &radius))
		lrg_editor_view_frame (&self->view, center, radius);
	else
		editor_view_reset (self);
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

	self->assets_dirty = TRUE;
	return editor_app_relative_path (self, path);
}

/* ==========================================================================
 * Signals and file monitoring
 * ========================================================================== */

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);

	self->problems_dirty = TRUE;
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);

	/* Node pointers held by views are now stale. */
	g_clear_pointer (&self->rename_guid, g_free);
	g_clear_pointer (&self->range_anchor, g_free);
	g_ptr_array_set_size (self->visible_rows, 0);
	self->last_primary = NULL;
	self->problems_dirty = TRUE;
	self->view_2d = lrg_level_get_default_2d (level);
	editor_view_reset (self);
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
	 * have nothing to lose, otherwise ask. */
	g_object_get (self->settings, "reload-on-external-change", &reload, NULL);
	if (reload && !lrg_editor_is_dirty (self->editor))
		perform_pending (self, EDITOR_PENDING_RELOAD, NULL);
	else
	{
		self->level_changed_on_disk = TRUE;
		editor_app_notify (self, 2, "%s changed on disk", path);
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

	/* Watch the file (editors often replace it atomically). */
	file = g_file_new_for_path (path);
	self->level_monitor = g_file_monitor_file (file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
	if (self->level_monitor != NULL)
		g_signal_connect (self->level_monitor, "changed", G_CALLBACK (on_level_file_changed), self);
}

static void
autosave_tick (LrgEditorApp *self)
{
	guint             interval = 0;
	g_autofree gchar *path = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *data = NULL;

	g_object_get (self->settings, "autosave-interval", &interval, NULL);
	if (interval == 0 || !lrg_editor_is_dirty (self->editor) || self->playing)
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
 * Frame: chrome
 * ========================================================================== */

/* Menu item bound to an action: label, shortcut, enabled and check state
 * all come from the registry. */
void
editor_app_menu_item_action (LrgEditorApp *self,
                             const gchar  *id)
{
	const gchar      *label = lrg_editor_actions_get_label (self->actions, id);
	const gchar      *shortcut = lrg_editor_actions_get_shortcut (self->actions, id);
	g_autofree gchar *first = NULL;

	if (label == NULL)
		return;

	/* Only the first of several alternatives is shown. */
	if (shortcut != NULL)
	{
		const gchar *bar = strchr (shortcut, '|');

		first = (bar != NULL) ? g_strndup (shortcut, bar - shortcut) : g_strdup (shortcut);
	}
	if (lrg_gui_menu_item (self->gui, label, first,
	                       lrg_editor_actions_is_checked (self->actions, id),
	                       lrg_editor_actions_is_enabled (self->actions, id)))
		lrg_editor_actions_activate (self->actions, id);
}

static void
menu_items (LrgEditorApp        *self,
            const gchar * const *ids)
{
	guint i;

	for (i = 0; ids[i] != NULL; i++)
	{
		if (*ids[i] == '-')
			lrg_gui_separator (self->gui);
		else
			editor_app_menu_item_action (self, ids[i]);
	}
}

void
editor_app_menu_create (LrgEditorApp *self,
                        LrgNode      *parent)
{
	static const struct
	{
		const gchar      *label;
		const gchar      *name;
		LrgNodeVisualKind kind;
		LrgPrimitiveType  prim;
	} items[] = {
		{ "Empty", "Node", LRG_NODE_VISUAL_NONE, 0 },
		{ "Cube", "Cube", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_CUBE },
		{ "Sphere", "Sphere", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_UV_SPHERE },
		{ "Plane", "Plane", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_PLANE },
		{ "Cylinder", "Cylinder", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_CYLINDER },
		{ "Cone", "Cone", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_CONE },
		{ "Torus", "Torus", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_TORUS },
		{ "Rectangle (2D)", "Rectangle", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_RECTANGLE_2D },
		{ "Circle (2D)", "Circle", LRG_NODE_VISUAL_PRIMITIVE, LRG_PRIMITIVE_CIRCLE_2D },
		{ NULL, NULL, 0, 0 },
		{ "Light", "Light", LRG_NODE_VISUAL_LIGHT, 0 },
		{ "Camera", "Camera", LRG_NODE_VISUAL_CAMERA, 0 },
		{ "Audio Emitter", "Audio", LRG_NODE_VISUAL_AUDIO_EMITTER, 0 },
		{ "Sprite", "Sprite", LRG_NODE_VISUAL_SPRITE, 0 },
		{ "Mesh (asset)", "Mesh", LRG_NODE_VISUAL_MESH_ASSET, 0 },
		{ "Tilemap", "Tilemap", LRG_NODE_VISUAL_TILEMAP, 0 },
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (items); i++)
	{
		if (items[i].label == NULL)
		{
			lrg_gui_separator (self->gui);
			continue;
		}
		if (lrg_gui_menu_item (self->gui, items[i].label, NULL, FALSE, TRUE))
		{
			LrgNode *node = editor_app_create_node (self, items[i].name, items[i].kind, items[i].prim,
			                                        parent);

			editor_app_set_status (self, "Created %s", lrg_node_get_name (node));
		}
	}
	lrg_gui_separator (self->gui);
	editor_app_menu_item_action (self, "node.instantiate-prefab");
}

/* Top-level menu header; remembers where it is for ui.state. */
static gboolean
begin_top_menu (LrgEditorApp *self,
                gint          index,
                const gchar  *label)
{
	gboolean open = lrg_gui_begin_menu (self->gui, label, TRUE);

	/* Opening the menu adds no item, so this is still the header. */
	lrg_gui_get_item_rect (self->gui, &self->menu_rects[index]);
	return open;
}

static void
draw_menu_bar (LrgEditorApp *self)
{
	static const gchar *file_items[] = {
		"file.new-level", "file.open-level", "-", "file.save", "file.save-as", "file.reload", "-",
		"file.new-project", "file.open-project", "file.project-settings", "-",
		"file.import-scene", "-", "file.quit", NULL
	};
	static const gchar *edit_items[] = {
		"edit.undo", "edit.redo", "-", "edit.cut", "edit.copy", "edit.paste", "edit.duplicate",
		"edit.delete", "-", "edit.select-all", "edit.deselect", "edit.find", "-",
		"edit.preferences", NULL
	};
	static const gchar *node_items[] = {
		"node.rename", "node.toggle-visible", "node.toggle-lock", "node.group", "node.ungroup", "-",
		"node.save-prefab", "node.instantiate-prefab", "-", "node.attach-script",
		"node.open-scripts", NULL
	};
	static const gchar *view_items[] = {
		"view.focus", "view.frame-all", "-", "view.top", "view.front", "view.right",
		"view.perspective", "view.toggle-2d", "-", "view.grid", "view.icons", "view.wireframe",
		"view.stats", NULL
	};
	static const gchar *panels[][2] = {
		{ "hierarchy", "Hierarchy" }, { "inspector", "Inspector" }, { "viewport", "Viewport" },
		{ "assets", "Assets" }, { "console", "Console" }, { "history", "History" },
		{ "problems", "Problems" }
	};
	static const gchar *play_items[] = {
		"play.start", "play.pause", "play.step", "play.stop", "-", "run.start", "run.stop", NULL
	};
	static const gchar *tool_items[] = {
		"tool.select", "tool.move", "tool.rotate", "tool.scale", "-", "tool.toggle-space",
		"tool.toggle-snap", "-", "tools.validate", "tools.palette", "tools.quick-open",
		"tools.open-level-yaml", "tools.reveal-socket", NULL
	};
	static const gchar *help_items[] = { "help.shortcuts", "help.docs", "help.about", NULL };
	const LrgGuiRect    bar = { 0.0f, 0.0f, self->width, EDITOR_MENUBAR_H };
	gsize               i;

	lrg_gui_begin_menu_bar (self->gui, &bar);

	/* Alt + the first letter opens a menu from the keyboard, under its
	 * header (where it was drawn last frame). */
	if (self->dialog == EDITOR_DIALOG_NONE && !self->palette_open &&
	    !lrg_gui_context_wants_keyboard (self->gui))
	{
		static const struct { LrgGuiKey key; const gchar *label; } keys[] = {
			{ LRG_GUI_KEY_F, "File" }, { LRG_GUI_KEY_E, "Edit" }, { LRG_GUI_KEY_C, "Create" },
			{ LRG_GUI_KEY_N, "Node" }, { LRG_GUI_KEY_V, "View" }, { LRG_GUI_KEY_P, "Play" },
			{ LRG_GUI_KEY_T, "Tools" }, { LRG_GUI_KEY_H, "Help" }
		};

		for (i = 0; i < G_N_ELEMENTS (keys); i++)
			if (lrg_gui_shortcut (self->gui, LRG_GUI_MOD_ALT, keys[i].key) && self->menu_rects[i].w > 0.0f)
			{
				lrg_gui_close_all_popups (self->gui);
				lrg_gui_set_next_popup_pos (self->gui, self->menu_rects[i].x,
				                            self->menu_rects[i].y + self->menu_rects[i].h);
				lrg_gui_open_popup (self->gui, keys[i].label);
			}
	}

	if (begin_top_menu (self, 0, "File"))
	{
		menu_items (self, file_items);
		lrg_gui_separator (self->gui);
		if (lrg_gui_begin_menu (self->gui, "Recent", TRUE))
		{
			const gchar * const *levels = lrg_editor_settings_get_recent_levels (self->settings);
			const gchar * const *projects = lrg_editor_settings_get_recent_projects (self->settings);

			for (i = 0; levels != NULL && levels[i] != NULL; i++)
			{
				lrg_gui_push_id_int (self->gui, (gint64) i);
				if (lrg_gui_menu_item (self->gui, levels[i], NULL, FALSE, TRUE))
					editor_app_request (self, EDITOR_PENDING_OPEN_LEVEL, levels[i]);
				lrg_gui_pop_id (self->gui);
			}
			lrg_gui_separator (self->gui);
			for (i = 0; projects != NULL && projects[i] != NULL; i++)
			{
				lrg_gui_push_id_int (self->gui, (gint64) i + 1000);
				if (lrg_gui_menu_item (self->gui, projects[i], NULL, FALSE, TRUE))
					editor_app_request (self, EDITOR_PENDING_OPEN_PROJECT, projects[i]);
				lrg_gui_pop_id (self->gui);
			}
			lrg_gui_end_menu (self->gui);
		}
		lrg_gui_end_menu (self->gui);
	}
	if (begin_top_menu (self, 1, "Edit"))
	{
		menu_items (self, edit_items);
		lrg_gui_end_menu (self->gui);
	}
	if (begin_top_menu (self, 2, "Create"))
	{
		editor_app_menu_create (self, NULL);
		lrg_gui_end_menu (self->gui);
	}
	if (begin_top_menu (self, 3, "Node"))
	{
		menu_items (self, node_items);
		lrg_gui_end_menu (self->gui);
	}
	if (begin_top_menu (self, 4, "View"))
	{
		menu_items (self, view_items);
		lrg_gui_separator (self->gui);
		if (lrg_gui_begin_menu (self->gui, "Panels", TRUE))
		{
			for (i = 0; i < G_N_ELEMENTS (panels); i++)
				if (lrg_gui_menu_item (self->gui, panels[i][1], NULL,
				                       lrg_gui_dock_has_panel (self->dock, panels[i][0]), TRUE))
				{
					if (lrg_gui_dock_has_panel (self->dock, panels[i][0]) &&
					    lrg_gui_dock_is_panel_visible (self->dock, panels[i][0]) &&
					    g_strcmp0 (panels[i][0], "viewport") != 0)
						lrg_gui_dock_close_panel (self->dock, panels[i][0]);
					else
						lrg_gui_dock_open_panel (self->dock, panels[i][0]);
				}
			lrg_gui_end_menu (self->gui);
		}
		editor_app_menu_item_action (self, "view.reset-layout");
		lrg_gui_end_menu (self->gui);
	}
	if (begin_top_menu (self, 5, "Play"))
	{
		menu_items (self, play_items);
		lrg_gui_end_menu (self->gui);
	}
	if (begin_top_menu (self, 6, "Tools"))
	{
		menu_items (self, tool_items);
		lrg_gui_end_menu (self->gui);
	}
	if (begin_top_menu (self, 7, "Help"))
	{
		menu_items (self, help_items);
		lrg_gui_end_menu (self->gui);
	}

	lrg_gui_end_menu_bar (self->gui);
}

/* A toolbar button that runs an action and reflects its checked state. */
static void
tool_button (LrgEditorApp *self,
             const gchar  *label,
             const gchar  *action)
{
	gboolean          checked = lrg_editor_actions_is_checked (self->actions, action);
	gboolean          enabled = lrg_editor_actions_is_enabled (self->actions, action);
	g_autofree gchar *id = g_strdup_printf ("%s##tb-%s", label, action);

	if (lrg_gui_toggle_button (self->gui, id, &checked) && enabled)
		lrg_editor_actions_activate (self->actions, action);
	{
		const gchar      *sc = lrg_editor_actions_get_shortcut (self->actions, action);
		g_autofree gchar *tip = g_strdup_printf ("%s%s%s%s",
		                                         lrg_editor_actions_get_label (self->actions, action),
		                                         sc != NULL ? " (" : "", sc != NULL ? sc : "",
		                                         sc != NULL ? ")" : "");

		lrg_gui_set_item_tooltip (self->gui, tip);
	}
}

static void
draw_toolbar (LrgEditorApp *self)
{
	const LrgGuiRect bar = { 0.0f, EDITOR_MENUBAR_H, self->width, EDITOR_TOOLBAR_H };
	gdouble          st, sr, ss;
	gfloat           v;

	lrg_gui_begin_region (self->gui, "##toolbar", &bar,
	                      LRG_GUI_REGION_HORIZONTAL | LRG_GUI_REGION_BACKGROUND);

	tool_button (self, "Select", "tool.select");
	tool_button (self, "Move", "tool.move");
	tool_button (self, "Rotate", "tool.rotate");
	tool_button (self, "Scale", "tool.scale");
	lrg_gui_dummy (self->gui, 8.0f, 1.0f);
	tool_button (self, lrg_editor_get_space (self->editor) == LRG_EDITOR_SPACE_LOCAL ? "Local" : "World",
	             "tool.toggle-space");
	tool_button (self, "Snap", "tool.toggle-snap");

	/* Snap increments, edited in place. */
	lrg_editor_get_snap (self->editor, &st, &sr, &ss);
	lrg_gui_set_next_item_width (self->gui, 64.0f);
	v = (gfloat) st;
	if (lrg_gui_drag_float (self->gui, "##snap-t", &v, 0.01f, 0.0f, 1000.0f, "%.2f m"))
		lrg_editor_set_snap (self->editor, v, sr, ss);
	lrg_gui_set_item_tooltip (self->gui, "Move snap");
	lrg_gui_set_next_item_width (self->gui, 64.0f);
	v = (gfloat) (sr * 180.0 / G_PI);
	if (lrg_gui_drag_float (self->gui, "##snap-r", &v, 0.5f, 0.0f, 360.0f, "%.0f deg"))
		lrg_editor_set_snap (self->editor, st, v * G_PI / 180.0, ss);
	lrg_gui_set_item_tooltip (self->gui, "Rotate snap");
	lrg_gui_set_next_item_width (self->gui, 56.0f);
	v = (gfloat) ss;
	if (lrg_gui_drag_float (self->gui, "##snap-s", &v, 0.01f, 0.0f, 100.0f, "%.2f"))
		lrg_editor_set_snap (self->editor, st, sr, v);
	lrg_gui_set_item_tooltip (self->gui, "Scale snap");

	lrg_gui_dummy (self->gui, 8.0f, 1.0f);
	tool_button (self, self->view_2d ? "2D" : "3D", "view.toggle-2d");
	lrg_gui_dummy (self->gui, 16.0f, 1.0f);

	tool_button (self, self->playing ? "Playing" : "Play", "play.start");
	tool_button (self, "Pause", "play.pause");
	tool_button (self, "Step", "play.step");
	tool_button (self, "Stop", "play.stop");
	lrg_gui_dummy (self->gui, 8.0f, 1.0f);
	tool_button (self, self->run_proc != NULL ? "Running" : "Run Game", "run.start");

	lrg_gui_end_region (self->gui);
}

static void
draw_status_bar (LrgEditorApp *self)
{
	const LrgGuiRect  bar = { 0.0f, self->height - EDITOR_STATUSBAR_H, self->width, EDITOR_STATUSBAR_H };
	LrgGuiStyle      *style = lrg_gui_context_get_style (self->gui);
	guint             warnings, errors, clients;
	g_autofree gchar *right = NULL;
	g_autofree gchar *doc = NULL;
	const gchar      *path = lrg_editor_get_path (self->editor);
	gfloat            rw;

	lrg_gui_begin_region (self->gui, "##statusbar", &bar,
	                      LRG_GUI_REGION_HORIZONTAL | LRG_GUI_REGION_BACKGROUND);

	/* Document and dirty marker. */
	{
		g_autofree gchar *base = (path != NULL) ? g_path_get_basename (path) : g_strdup ("untitled");

		doc = g_strdup_printf ("%s%s", base, lrg_editor_is_dirty (self->editor) ? " *" : "");
	}
	lrg_gui_text (self->gui, doc);
	if (self->playing)
		lrg_gui_text_colored (self->gui, style->colors[LRG_GUI_COL_SUCCESS],
		                      self->paused ? "PAUSED" : "PLAYING");
	if (self->level_changed_on_disk)
	{
		lrg_gui_text_colored (self->gui, style->colors[LRG_GUI_COL_WARNING], "changed on disk:");
		if (lrg_gui_button (self->gui, "Reload##status"))
			perform_pending (self, EDITOR_PENDING_RELOAD, NULL);
		if (lrg_gui_button (self->gui, "Keep mine##status"))
			self->level_changed_on_disk = FALSE;
	}
	if (self->status != NULL)
		lrg_gui_text_disabled (self->gui, self->status);

	/* Right side: selection, log counters, IPC clients, fps. */
	warnings = lrg_editor_log_count_level (self->log, G_LOG_LEVEL_WARNING);
	errors = lrg_editor_log_count_level (self->log, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	clients = lrg_editor_server_get_n_clients (self->server);
	right = g_strdup_printf ("%u selected   %u nodes   %s%u errors  %u warnings   %s%s   %.0f fps",
	                         lrg_editor_selection_get_count (lrg_editor_get_selection (self->editor)),
	                         editor_app_count_nodes (self),
	                         "", errors, warnings,
	                         lrg_editor_server_get_socket_path (self->server) != NULL ? "IPC " : "",
	                         lrg_editor_server_get_socket_path (self->server) != NULL
	                         ? (lrg_editor_server_has_capability (self->server, "open-file")
	                            ? "emacs" : (clients > 0 ? "client" : "idle"))
	                         : "",
	                         self->fps);
	rw = lrg_gui_calc_text_width (self->gui, right);
	lrg_gui_set_cursor (self->gui, MAX (bar.x + 200.0f, bar.x + bar.w - rw - 12.0f),
	                    bar.y + (bar.h - style->font_size) * 0.5f);
	lrg_gui_text_disabled (self->gui, right);
	if (lrg_gui_is_item_clicked (self->gui, LRG_GUI_MOUSE_LEFT))
		lrg_gui_dock_open_panel (self->dock, "console");

	lrg_gui_end_region (self->gui);
}

static void
draw_toasts (LrgEditorApp *self)
{
	LrgGuiStyle    *style = lrg_gui_context_get_style (self->gui);
	LrgGuiDrawList *dl;
	GList          *l;
	gfloat          y = self->height - EDITOR_STATUSBAR_H - 12.0f;
	static const LrgGuiStyleColor accents[] = {
		LRG_GUI_COL_ACCENT, LRG_GUI_COL_SUCCESS, LRG_GUI_COL_WARNING, LRG_GUI_COL_ERROR
	};

	/* Expire old toasts. */
	while (!g_queue_is_empty (&self->toasts) &&
	       ((EditorToast *) g_queue_peek_head (&self->toasts))->expires < self->time)
		toast_free (g_queue_pop_head (&self->toasts));

	dl = lrg_gui_get_draw_list (self->gui);
	for (l = g_queue_peek_tail_link (&self->toasts); l != NULL; l = l->prev)
	{
		EditorToast *t = l->data;
		gfloat       w = MIN (self->width * 0.4f, lrg_gui_calc_text_width (self->gui, t->text) + 28.0f);
		LrgGuiRect   r = { self->width - w - 14.0f, y - style->font_size - 14.0f, w, style->font_size + 14.0f };
		LrgGuiRect   bar = { r.x, r.y, 4.0f, r.h };
		LrgGuiRect   text = { r.x + 14.0f, r.y, r.w - 18.0f, r.h };

		lrg_gui_draw_list_rect_filled (dl, &r, style->colors[LRG_GUI_COL_POPUP_BG], style->rounding);
		lrg_gui_draw_list_rect_outline (dl, &r, style->colors[LRG_GUI_COL_BORDER], 1.0f, style->rounding);
		lrg_gui_draw_list_rect_filled (dl, &bar, style->colors[accents[CLAMP (t->level, 0, 3)]], 2.0f);
		lrg_gui_draw_list_push_clip (dl, &text);
		lrg_gui_draw_list_text (dl, text.x, text.y + 7.0f, t->text, -1, style->font_size,
		                        style->colors[LRG_GUI_COL_TEXT], FALSE);
		lrg_gui_draw_list_pop_clip (dl);
		y = r.y - 6.0f;
	}
}

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

/* ==========================================================================
 * Frame
 * ========================================================================== */

void
lrg_editor_app_frame (LrgEditorApp      *self,
                      const LrgGuiInput *input,
                      gfloat             width,
                      gfloat             height)
{
	LrgGuiRect dock_rect;
	LrgNode   *primary;

	g_return_if_fail (LRG_IS_EDITOR_APP (self));
	g_return_if_fail (input != NULL);

	self->width = width;
	self->height = height;
	self->time = input->time;
	self->frames++;
	if (input->delta > 0.0f)
		self->fps = self->fps * 0.95f + (1.0f / input->delta) * 0.05f;

	lrg_gui_context_begin_frame (self->gui, input, width, height);
	self->cur_input = input;
	self->last_input = *input;

	/* Shortcuts first, while focus state is still last frame's. */
	editor_app_process_shortcuts (self);

	/* Selection changes from anywhere (viewport, IPC) reveal the node. */
	primary = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));
	if (primary != self->last_primary)
	{
		self->last_primary = primary;
		if (primary != NULL)
			self->reveal_pending = TRUE;
	}

	draw_menu_bar (self);
	draw_toolbar (self);

	dock_rect.x = 0.0f;
	dock_rect.y = EDITOR_MENUBAR_H + EDITOR_TOOLBAR_H;
	dock_rect.w = width;
	dock_rect.h = MAX (50.0f, height - dock_rect.y - EDITOR_STATUSBAR_H);

	lrg_gui_dock_begin (self->dock, self->gui, &dock_rect);

	self->viewport_visible = FALSE;
	if (lrg_gui_dock_begin_panel (self->dock, self->gui, "viewport",
	                              self->playing ? "Viewport (playing)###viewport" : "Viewport###viewport"))
	{
		editor_panel_viewport (self);
		lrg_gui_dock_end_panel (self->dock, self->gui);
	}
	if (lrg_gui_dock_begin_panel (self->dock, self->gui, "hierarchy", "Hierarchy"))
	{
		editor_panel_hierarchy (self);
		lrg_gui_dock_end_panel (self->dock, self->gui);
	}
	if (lrg_gui_dock_begin_panel (self->dock, self->gui, "inspector", "Inspector"))
	{
		editor_panel_inspector (self);
		lrg_gui_dock_end_panel (self->dock, self->gui);
	}
	if (lrg_gui_dock_begin_panel (self->dock, self->gui, "assets", "Assets"))
	{
		editor_panel_assets (self);
		lrg_gui_dock_end_panel (self->dock, self->gui);
	}
	{
		guint             errors = lrg_editor_log_count_level (self->log, G_LOG_LEVEL_ERROR |
		                                                       G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);
		g_autofree gchar *title = (errors > 0) ? g_strdup_printf ("Console (%u)###console", errors)
		                                       : g_strdup ("Console###console");

		if (lrg_gui_dock_begin_panel (self->dock, self->gui, "console", title))
		{
			editor_panel_console (self);
			lrg_gui_dock_end_panel (self->dock, self->gui);
		}
	}
	if (lrg_gui_dock_begin_panel (self->dock, self->gui, "history", "History"))
	{
		editor_panel_history (self);
		lrg_gui_dock_end_panel (self->dock, self->gui);
	}
	{
		g_autofree gchar *title = (self->problems->len > 0)
		                          ? g_strdup_printf ("Problems (%u)###problems", self->problems->len)
		                          : g_strdup ("Problems###problems");

		if (lrg_gui_dock_begin_panel (self->dock, self->gui, "problems", title))
		{
			editor_panel_problems (self);
			lrg_gui_dock_end_panel (self->dock, self->gui);
		}
	}
	lrg_gui_dock_end (self->dock, self->gui);

	draw_status_bar (self);
	editor_dialogs_draw (self);
	draw_toasts (self);

	lrg_gui_context_end_frame (self->gui);
	self->cur_input = NULL;

	/* Timers that do not depend on the UI. */
	if (self->playing)
		editor_play_tick (self, input->delta);
	autosave_tick (self);
	if (self->problems_dirty && self->time - self->problems_time > PROBLEMS_DEBOUNCE)
	{
		LrgEngine *engine = lrg_engine_get_default ();

		g_ptr_array_unref (self->problems);
		self->problems = lrg_editor_validate_level (lrg_editor_get_level (self->editor),
		                                            editor_app_base_dir (self),
		                                            engine != NULL ? lrg_engine_get_registry (engine) : NULL);
		self->problems_dirty = FALSE;
		self->problems_time = self->time;
	}
	update_title (self);

	if (self->quit_requested && self->dialog == EDITOR_DIALOG_NONE && !lrg_editor_is_dirty (self->editor))
		self->quit_now = TRUE;
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

	self->width = (gfloat) MAX (width, 320);
	self->height = (gfloat) MAX (height, 240);
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
	gint w, h;

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
	InitWindow ((gint) self->width, (gint) self->height, "libregnum editor");
	if (!IsWindowReady ())
	{
		g_printerr ("lrgeditor: could not open a window\n");
		return 1;
	}
	SetWindowMinSize (960, 600);
	SetExitKey (KEY_NULL);
	SetTargetFPS (editor_frame_cap (GetMonitorRefreshRate (GetCurrentMonitor ())));
	self->window_open = TRUE;
	self->headless = FALSE;

	self->backend = lrg_gui_raylib_new ();
	lrg_gui_raylib_attach (self->backend, self->gui);
	self->applied_scale = -1.0f;
	editor_app_apply_settings (self);
	editor_render_init (self);

	while (!self->quit_now)
	{
		GMainContext *ctx = g_main_context_default ();

		/* Keep IPC, monitors and subprocess reaping alive. */
		while (g_main_context_iteration (ctx, FALSE))
			;

		if (WindowShouldClose ())
			lrg_editor_app_request_quit (self);

		lrg_gui_raylib_poll_input (self->backend, &self->input);
		w = GetScreenWidth ();
		h = GetScreenHeight ();
		lrg_editor_app_frame (self, &self->input, (gfloat) w, (gfloat) h);
		lrg_gui_raylib_apply_cursor (self->backend, lrg_gui_context_get_cursor (self->gui));

		/* 3D into its render target, then the UI on top. */
		editor_render_viewport (self);
		BeginDrawing ();
		ClearBackground ((Color) { 17, 17, 27, 255 });
		lrg_gui_raylib_render (self->backend, lrg_gui_context_get_draw_list (self->gui));
		if (self->window_shot != NULL)
		{
			Image shot = LoadImageFromScreen ();

			if (!ExportImage (shot, self->window_shot))
				editor_app_notify (self, 3, "Could not write %s", self->window_shot);
			UnloadImage (shot);
			g_clear_pointer (&self->window_shot, g_free);
		}
		EndDrawing ();
	}

	{
		g_autoptr(GError) error = NULL;

		if (!lrg_editor_app_save_settings (self, &error))
			g_printerr ("lrgeditor: could not save settings: %s\n", error->message);
	}

	editor_render_shutdown (self);
	g_clear_object (&self->backend);
	lrg_gui_context_set_measure_func (self->gui, NULL, NULL, NULL);
	lrg_gui_context_set_clipboard_funcs (self->gui, NULL, NULL, NULL, NULL);
	CloseWindow ();
	self->window_open = FALSE;
	self->headless = TRUE;
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
} HeadlessData;

static gboolean
headless_tick (gpointer user_data)
{
	HeadlessData *data = user_data;

	data->self->time = g_get_monotonic_time () / (gdouble) G_USEC_PER_SEC;
	autosave_tick (data->self);
	if (data->self->playing)
		editor_play_tick (data->self, 0.1f);
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

	self->headless = TRUE;
	loop = g_main_loop_new (NULL, FALSE);
	data.self = self;
	data.loop = loop;

	sigint = g_unix_signal_add (SIGINT, on_quit_signal, loop);
	sigterm = g_unix_signal_add (SIGTERM, on_quit_signal, loop);
	tick = g_timeout_add (100, headless_tick, &data);

	g_main_loop_run (loop);

	g_source_remove (tick);
	g_source_remove (sigint);
	g_source_remove (sigterm);
	return 0;
}
