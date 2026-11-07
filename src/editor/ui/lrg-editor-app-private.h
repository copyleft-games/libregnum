/* lrg-editor-app-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal state of the editor application, shared by its modules:
 *
 *   lrg-editor-app.c            lifecycle, documents, settings, the frame,
 *                               run loops, autosave, file monitors
 *   lrg-editor-app-actions.c    the action table and every app IPC method
 *   lrg-editor-window.c         LrgEditorWindow: menu bar, toolbar, dock,
 *                               status bar, toasts, shortcuts
 *   lrg-editor-viewport-panel.c the viewport widget: scene rendering,
 *                               navigation, gizmo, picking, tile painting
 *   lrg-editor-dialogs.c        modal dialogs on LrgDialog / LrgFileDialog
 *   lrg-editor-play.c           play-session glue, Run Game, timeline
 *                               preview and the tile tool state
 *   lrg-editor-thumbnails.c     asset thumbnails (cache, uploads, GPU renders)
 *   lrg-editor-sources.c        asset sources: registry, cached exports,
 *                               imports, game module editor hook,
 *                               the "source.*" IPC methods
 *   lrg-editor-legacy-input.c   LrgGuiInput -> LrgCanvasInput (deprecated path)
 *
 * Since 0.8.1 the application is drawn with the retained src/ui toolkit
 * (an LrgCanvas holding an LrgEditorWindow). What the old immediate-mode
 * app kept here as loose fields now lives in headless objects
 * (LrgEditorViewState, LrgEditorViewportNav, LrgEditorPlaySession) or in
 * the widgets themselves; this struct holds the core handles, those
 * objects and pointers to the panels.
 */

#pragma once

#include "lrg-editor-app.h"
#include "lrg-editor-window.h"
#include "lrg-editor-viewport-panel.h"
#include "lrg-editor-viewport-split.h"
#include "lrg-editor-scene-renderer.h"
#include "lrg-editor-legacy-input.h"
#include "../lrg-editor.h"
#include "../lrg-editor-selection.h"
#include "../lrg-editor-command.h"
#include "../lrg-editor-math.h"
#include "../lrg-editor-gizmo.h"
#include "../lrg-editor-settings.h"
#include "../lrg-editor-settings-migrate.h"
#include "../lrg-external-editor.h"
#include "../lrg-editor-server.h"
#include "../lrg-editor-log.h"
#include "../lrg-editor-actions.h"
#include "../lrg-editor-validate.h"
#include "../lrg-editor-view-state.h"
#include "../lrg-editor-viewport-nav.h"
#include "../lrg-editor-play-session.h"
#include "../lrg-editor-vcs.h"
#include "../lrg-level.h"
#include "../lrg-node.h"
#include "../lrg-node-visual.h"
#include "../lrg-component-desc.h"
#include "../lrg-script-binding.h"
#include "../lrg-project.h"
#include "../lrg-asset-database.h"
#include "../lrg-scene-import.h"
#include "../lrg-prefab.h"
#include "../lrg-level-serializer.h"
#include "../lrg-level-instantiate.h"
#include "../lrg-node-tilemap.h"
#include "../lrg-editor-tile-painter.h"
#include "../lrg-timeline.h"
#include "../lrg-editor-timeline.h"
#include "../lrg-thumbnail-cache.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-canvas-input.h"
#include "../../ui/lrg-dock-area.h"
#include "../../ui/lrg-theme.h"
#include "../../ecs/lrg-world.h"
#include "../../lrg-enums.h"
#include <gio/gio.h>

G_BEGIN_DECLS

/* Notification levels (editor_app_notify): toast colour and log level. */
#define EDITOR_NOTIFY_INFO    (0)
#define EDITOR_NOTIFY_SUCCESS (1)
#define EDITOR_NOTIFY_WARNING (2)
#define EDITOR_NOTIFY_ERROR   (3)

/* What to do after the "unsaved changes" dialog says go ahead. */
typedef enum
{
	EDITOR_PENDING_NONE,
	EDITOR_PENDING_NEW_LEVEL,
	EDITOR_PENDING_OPEN_LEVEL,
	EDITOR_PENDING_OPEN_PROJECT,
	EDITOR_PENDING_RELOAD,
	EDITOR_PENDING_QUIT
} EditorPending;

struct _LrgEditorApp
{
	GObject               parent_instance;

	/* Core handles */
	LrgEditor            *editor;
	LrgEditorSettings    *settings;
	LrgExternalEditor    *external;
	LrgEditorServer      *server;
	LrgEditorLog         *log;
	LrgEditorActions     *actions;
	LrgProject           *project;        /* nullable */
	gchar                *project_dir;    /* nullable, absolute */
	gchar                *settings_path;  /* nullable override */
	LrgAssetDatabase     *assets;         /* project scan, write-meta on; nullable */
	LrgEditorVcs         *vcs;            /* nullable (no project) */

	/* Headless view model (exists in every mode) */
	LrgEditorViewState   *view;
	LrgEditorViewportNav *nav;
	LrgEditorPlaySession *play;
	LrgEditorGizmo       *gizmo;
	LrgEditorTilePainter *tile_painter;

	/* UI (the canvas exists headless too; only drawing needs a window) */
	LrgCanvas            *canvas;
	LrgEditorWindow      *window;
	LrgEditorSceneRenderer *renderer;
	LrgTheme             *theme;          /* owned; the canvas' theme */
	gchar                *applied_theme;  /* theme value the theme was built from */
	gdouble               applied_scale;
	GHashTable           *panels;         /* id -> LrgDockPanel* (owned refs, docked or not) */
	LrgEditorViewportPanel *viewport_panel;   /* viewport 0 of the split */
	LrgEditorViewportSplit *viewport_split;
	gpointer              legacy;         /* EditorLegacyInput (lrg-editor-legacy-input.c) */

	/* Frame state */
	gboolean              window_open;
	gboolean              quit_requested;
	gboolean              quit_now;
	gint                  width;
	gint                  height;
	gdouble               time;
	gfloat                fps;
	guint64               frames;
	LrgCanvasInput        last_input;     /* echoed by ui.state */

	/* Documents */
	EditorPending         pending;
	gchar                *pending_path;
	gchar                *recovery_file;
	GFileMonitor         *level_monitor;
	gboolean              level_changed_on_disk;
	gint64                level_saved_mtime;
	gchar                *level_dir;      /* base dir cache without a project */
	gdouble               last_autosave;
	gchar                *window_title;
	gchar                *window_shot;    /* pending full-window screenshot */
	GPtrArray            *asset_monitors; /* GFileMonitor* of the project folders */

	/* Problems (validation) */
	GPtrArray            *problems;       /* LrgEditorProblem* */
	gboolean              problems_dirty;
	gdouble               problems_time;

	/* Run Game */
	GSubprocess          *run_proc;
	GCancellable         *run_cancel;

	/* Timeline preview (lrg-editor-play.c) */
	GHashTable           *timeline_rest;  /* "guid\nchannel" -> gfloat* rest value */
	GHashTable           *timeline_play;  /* play-in-editor: autoplay timeline names (set) */
	gchar                *timeline_key_guid;
	gchar                *timeline_key_channel;
	gfloat                timeline_key_time;
	gboolean              timeline_key_valid;
	gint                  timeline_easing; /* LrgEasingType for new keys */

	/* Tile painting hover (written by the viewport, read by tilemap.tool) */
	gboolean              tile_hover_valid;
	gint                  tile_hover_x;
	gint                  tile_hover_y;

	/* Thumbnails (lrg-editor-thumbnails.c) */
	LrgThumbnailCache    *thumbs;
	gchar                *thumbs_dir;
	GHashTable           *thumb_entries;  /* abs path -> EditorThumb* */
	guint64               thumb_upload_frame;
	guint                 thumb_uploads;

	/* Asset sources (lrg-editor-sources.c) */
	GPtrArray            *sources;        /* LrgAssetSource* (owned refs) */
	guint                 sources_serial; /* bumped on add / remove (panels poll it) */
};

/* --- lrg-editor-app.c --- */
G_GNUC_INTERNAL void     editor_app_notify          (LrgEditorApp *self, gint level, const gchar *format, ...) G_GNUC_PRINTF (3, 4);
G_GNUC_INTERNAL void     editor_app_set_status      (LrgEditorApp *self, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
G_GNUC_INTERNAL gchar *  editor_app_project_path    (LrgEditorApp *self, const gchar *path);
G_GNUC_INTERNAL gchar *  editor_app_relative_path   (LrgEditorApp *self, const gchar *path);
G_GNUC_INTERNAL const gchar * editor_app_base_dir   (LrgEditorApp *self);
G_GNUC_INTERNAL gboolean editor_app_is_headless     (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_request         (LrgEditorApp *self, EditorPending what, const gchar *path);
G_GNUC_INTERNAL void     editor_app_perform_pending (LrgEditorApp *self, EditorPending what, const gchar *path);
G_GNUC_INTERNAL gboolean editor_app_save            (LrgEditorApp *self);
G_GNUC_INTERNAL gboolean editor_app_save_as         (LrgEditorApp *self, const gchar *path);
G_GNUC_INTERNAL void     editor_app_confirm_choice  (LrgEditorApp *self, gint choice);
G_GNUC_INTERNAL LrgScriptLanguage editor_script_language_for_path (const gchar *path);
G_GNUC_INTERNAL void     editor_app_open_file       (LrgEditorApp *self, const gchar *path, gint line, gint col);
G_GNUC_INTERNAL void     editor_app_open_asset      (LrgEditorApp *self, const gchar *path);
G_GNUC_INTERNAL LrgNode * editor_app_create_node    (LrgEditorApp *self, const gchar *name, LrgNodeVisualKind kind,
                                                     LrgPrimitiveType primitive, LrgNode *parent);
G_GNUC_INTERNAL LrgNode * editor_app_create_node_kind (LrgEditorApp *self, const gchar *kind, LrgNode *parent);
G_GNUC_INTERNAL LrgNode * editor_app_place_asset    (LrgEditorApp *self, const gchar *path, GError **error);
G_GNUC_INTERNAL gchar *  editor_app_create_script   (LrgEditorApp *self, const gchar *name, LrgScriptLanguage lang,
                                                     GError **error);
G_GNUC_INTERNAL void     editor_app_apply_settings  (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_reset_layout    (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_show_panel      (LrgEditorApp *self, const gchar *id);
G_GNUC_INTERNAL void     editor_app_toggle_panel    (LrgEditorApp *self, const gchar *id);
G_GNUC_INTERNAL gboolean editor_app_panel_is_shown  (LrgEditorApp *self, const gchar *id);
G_GNUC_INTERNAL void     editor_app_set_clipboard   (LrgEditorApp *self, const gchar *text);
G_GNUC_INTERNAL gchar *  editor_app_get_clipboard   (LrgEditorApp *self);
G_GNUC_INTERNAL guint    editor_app_count_nodes     (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_validate_now    (LrgEditorApp *self);

/* --- lrg-editor-app-actions.c --- */
G_GNUC_INTERNAL void     editor_app_register_actions (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_register_methods (LrgEditorApp *self);

/* Node kinds of the Create menu: label, default node name and kind id. */
typedef struct
{
	const gchar       *id;      /* "cube", "light"... ("-" separator) */
	const gchar       *label;
	const gchar       *name;
	LrgNodeVisualKind  kind;
	LrgPrimitiveType   primitive;
} EditorCreateItem;

G_GNUC_INTERNAL const EditorCreateItem * editor_create_items (guint *n_items);

/* --- lrg-editor-dialogs.c --- */
typedef void (*EditorFolderFunc) (LrgEditorApp *self, const gchar *folder, gpointer data);

G_GNUC_INTERNAL void     editor_dialog_select_folder (LrgEditorApp *self, const gchar *title, const gchar *dir,
                                                      EditorFolderFunc func, gpointer data, GDestroyNotify destroy);
G_GNUC_INTERNAL void     editor_dialog_confirm      (LrgEditorApp *self, const gchar *message);
G_GNUC_INTERNAL void     editor_dialog_open_level   (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_save_level_as (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_open_project (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_import_scene (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_save_prefab  (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_instantiate_prefab (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_new_project  (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_new_script   (LrgEditorApp *self, const gchar *node_guid);
G_GNUC_INTERNAL void     editor_dialog_recover      (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_about        (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_shortcuts    (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_project_settings (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_close_all    (LrgEditorApp *self);

/* --- lrg-editor-play.c --- */
G_GNUC_INTERNAL void     editor_play_init           (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_play_dispose        (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_play_start          (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_play_stop           (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_play_toggle_pause   (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_play_step           (LrgEditorApp *self);
G_GNUC_INTERNAL gboolean editor_play_is_playing     (LrgEditorApp *self);
G_GNUC_INTERNAL gboolean editor_run_start           (LrgEditorApp *self, GError **error);
G_GNUC_INTERNAL void     editor_run_stop            (LrgEditorApp *self);
G_GNUC_INTERNAL LrgTimeline * editor_timeline_current (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_timeline_seek       (LrgEditorApp *self, gfloat time);
G_GNUC_INTERNAL void     editor_timeline_set_playing (LrgEditorApp *self, gboolean playing);
G_GNUC_INTERNAL void     editor_timeline_end_preview (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_timeline_tick       (LrgEditorApp *self, gfloat delta);
G_GNUC_INTERNAL guint    editor_timeline_key_selection (LrgEditorApp *self);
G_GNUC_INTERNAL LrgNode * editor_tiles_target       (LrgEditorApp *self);

/* --- lrg-editor-thumbnails.c --- */
G_GNUC_INTERNAL void     editor_thumbnails_init     (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_thumbnails_dispose  (LrgEditorApp *self);
G_GNUC_INTERNAL LrgThumbnailCache * editor_thumbnails_cache (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_thumbnails_pump     (LrgEditorApp *self);
G_GNUC_INTERNAL GObject * editor_thumbnail_texture  (LrgEditorApp *self, const gchar *path, gboolean *pending);
G_GNUC_INTERNAL void     editor_thumbnails_file_changed (LrgEditorApp *self, const gchar *path);
G_GNUC_INTERNAL void     editor_thumbnails_window_opened (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_thumbnails_window_closing (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_thumbnails_clear    (LrgEditorApp *self);

/* --- lrg-editor-sources.c --- */
G_GNUC_INTERNAL void     editor_sources_init        (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_sources_dispose     (LrgEditorApp *self);
G_GNUC_INTERNAL const gchar * editor_entry_type_name (LrgAssetType type, gboolean is_folder);

/* --- lrg-editor-legacy-input.c --- */
typedef struct _EditorLegacyInput EditorLegacyInput;

G_GNUC_INTERNAL EditorLegacyInput * editor_legacy_input_new (void);
G_GNUC_INTERNAL void     editor_legacy_input_free   (gpointer data);
G_GNUC_INTERNAL void     editor_legacy_input_translate (EditorLegacyInput *self, const LrgGuiInput *input,
                                                        LrgCanvasInput *out);

/* --- lrg-editor-window.c --- */
G_GNUC_INTERNAL void     editor_window_refresh      (LrgEditorWindow *self);
G_GNUC_INTERNAL void     editor_window_toast        (LrgEditorWindow *self, gint level, const gchar *text);
G_GNUC_INTERNAL void     editor_window_set_menus_enabled (LrgEditorWindow *self, gboolean enabled);
G_GNUC_INTERNAL void     editor_window_sync_shortcuts (LrgEditorWindow *self);
G_GNUC_INTERNAL void     editor_window_rebuild_recent (LrgEditorWindow *self);
G_GNUC_INTERNAL GActionGroup * editor_window_get_editor_group (LrgEditorWindow *self);

/* --- lrg-editor-viewport-panel.c --- */
G_GNUC_INTERNAL gboolean editor_viewport_panel_screenshot (LrgEditorViewportPanel *self, const gchar *path,
                                                           GError **error);
G_GNUC_INTERNAL gboolean editor_viewport_panel_get_rect (LrgEditorViewportPanel *self, gfloat *x, gfloat *y,
                                                         gfloat *w, gfloat *h);
G_GNUC_INTERNAL gboolean editor_viewport_panel_is_hovered (LrgEditorViewportPanel *self);
G_GNUC_INTERNAL void     editor_viewport_panel_tick (LrgEditorViewportPanel *self, gfloat dt);

G_END_DECLS
