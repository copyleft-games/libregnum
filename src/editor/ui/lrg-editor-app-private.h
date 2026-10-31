/* lrg-editor-app-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal state of the editor application, shared by its modules:
 *
 *   lrg-editor-app.c            lifecycle, documents, frame, menus, bars
 *   lrg-editor-app-actions.c    action and IPC method registration
 *   lrg-editor-panel-hierarchy.c
 *   lrg-editor-panel-inspector.c
 *   lrg-editor-panel-viewport.c viewport interaction (headless-safe)
 *   lrg-editor-render.c         3D viewport rendering (needs a window)
 *   lrg-editor-panel-assets.c
 *   lrg-editor-panel-console.c  console, history and problems panels
 *   lrg-editor-dialogs.c        modal dialogs, palette, quick open
 *   lrg-editor-play.c           play-in-editor and external runs
 */

#pragma once

#include "lrg-editor-app.h"
#include "../lrg-editor.h"
#include "../lrg-editor-selection.h"
#include "../lrg-editor-command.h"
#include "../lrg-editor-math.h"
#include "../lrg-editor-gizmo.h"
#include "../lrg-editor-settings.h"
#include "../lrg-external-editor.h"
#include "../lrg-editor-server.h"
#include "../lrg-editor-log.h"
#include "../lrg-editor-actions.h"
#include "../lrg-editor-validate.h"
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
#include "../../gui/lrg-gui-context.h"
#include "../../gui/lrg-gui-dock.h"
#include "../../gui/lrg-gui-raylib.h"
#include "../../ecs/lrg-world.h"
#include "../../lrg-enums.h"
#include <gio/gio.h>

G_BEGIN_DECLS

#define EDITOR_MENUBAR_H   (26.0f)
#define EDITOR_TOOLBAR_H   (34.0f)
#define EDITOR_STATUSBAR_H (24.0f)

/* Modal dialogs (one at a time). */
typedef enum
{
	EDITOR_DIALOG_NONE,
	EDITOR_DIALOG_PREFERENCES,
	EDITOR_DIALOG_PROJECT_SETTINGS,
	EDITOR_DIALOG_FILE,
	EDITOR_DIALOG_CONFIRM,
	EDITOR_DIALOG_NEW_PROJECT,
	EDITOR_DIALOG_NEW_SCRIPT,
	EDITOR_DIALOG_ABOUT,
	EDITOR_DIALOG_SHORTCUTS,
	EDITOR_DIALOG_RECOVER
} EditorDialog;

/* What the file dialog is for. */
typedef enum
{
	EDITOR_FILE_OPEN_LEVEL,
	EDITOR_FILE_SAVE_LEVEL_AS,
	EDITOR_FILE_OPEN_PROJECT,
	EDITOR_FILE_SAVE_PREFAB,
	EDITOR_FILE_IMPORT_SCENE,
	EDITOR_FILE_PICK_ASSET,
	EDITOR_FILE_PICK_SCRIPT
} EditorFileMode;

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

/* Viewport navigation in progress. */
typedef enum
{
	EDITOR_NAV_NONE,
	EDITOR_NAV_FLY,     /* right mouse: look + WASD */
	EDITOR_NAV_ORBIT,   /* Alt + left mouse */
	EDITOR_NAV_PAN,     /* middle mouse (or Shift + middle) */
	EDITOR_NAV_BOX,     /* left drag on empty space: box select */
	EDITOR_NAV_ZOOM,    /* drag to dolly (blender scheme: Ctrl + middle) */
	EDITOR_NAV_GIZMO    /* left drag on the orientation gizmo: orbit */
} EditorNav;

typedef struct
{
	gchar    *name;
	gchar    *path;     /* absolute */
	gboolean  is_dir;
	gint      type;     /* LrgAssetType */
	goffset   size;
	gint64    mtime;
} EditorFileEntry;

typedef struct
{
	gchar   *text;
	gint     level;     /* 0 info, 1 success, 2 warning, 3 error */
	gdouble  expires;
} EditorToast;

struct _LrgEditorApp
{
	GObject            parent_instance;

	/* Core */
	LrgEditor         *editor;
	LrgEditorSettings *settings;
	LrgExternalEditor *external;
	LrgEditorServer   *server;
	LrgEditorLog      *log;
	LrgEditorActions  *actions;
	LrgProject        *project;       /* nullable */
	gchar             *project_dir;   /* nullable, absolute */
	gchar             *settings_path; /* nullable override */

	/* UI */
	LrgGuiContext     *gui;
	LrgGuiDock        *dock;
	LrgGuiRaylib      *backend;       /* NULL until a window exists */
	LrgGuiInput        input;
	gboolean           headless;
	gboolean           window_open;
	gboolean           quit_requested;
	gboolean           quit_now;
	gfloat             width;
	gfloat             height;
	gfloat             applied_scale;
	gchar             *applied_theme;
	gdouble            time;
	gfloat             fps;
	guint64            frames;

	/* Viewport */
	LrgEditorView      view;
	LrgEditorGizmo    *gizmo;
	LrgGuiRect         viewport_rect;
	gboolean           viewport_visible;
	gboolean           viewport_hovered;
	gboolean           view_2d;
	gboolean           show_grid;
	gboolean           show_icons;
	gboolean           wireframe;
	gboolean           show_stats;
	gfloat             fly_speed;
	EditorNav          nav;
	LrgGuiMouseButton  nav_button;    /* the button driving nav */
	gboolean           nav_moved;     /* nav passed the click slop */
	gint               nav_axis;      /* orientation-gizmo end pressed, -1 none */
	gint               axis_hot;      /* orientation-gizmo end hovered, -1 none */
	gboolean           ortho_from_align; /* orbiting returns to perspective */
	gfloat             box_x0;
	gfloat             box_y0;
	gfloat             press_x;
	gfloat             press_y;
	gboolean           press_on_empty;
	gfloat             last_pick_x;
	gfloat             last_pick_y;
	guint              pick_cycle;
	gpointer           render;        /* EditorRender (lrg-editor-render.c) */

	/* Hierarchy */
	GString           *hierarchy_filter;
	gchar             *rename_guid;
	GString           *rename_buf;
	gboolean           rename_focus;
	gboolean           reveal_pending;
	gchar             *range_anchor;
	GPtrArray         *visible_rows;  /* LrgNode* in display order (borrowed) */
	LrgNode           *last_primary;  /* to detect selection changes (no ref) */
	/* Per-frame derived data, cached against lrg_node_get_tree_serial()
	 * and the level root (identity only, never dereferenced). */
	LrgNode           *node_count_root;
	guint              node_count_serial;     /* 0 = not cached */
	guint              node_count;
	GHashTable        *hierarchy_shown;       /* LrgNode* matching the filter (borrowed) */
	gchar             *hierarchy_shown_needle;
	LrgNode           *hierarchy_shown_root;
	guint              hierarchy_shown_serial; /* 0 = not cached */

	/* Inspector */
	GString           *component_search;
	GString           *param_key;
	GString           *scratch;
	gboolean           inspector_dragging; /* an inspector drag is merging */
	gint               new_param_type;

	/* Assets */
	gchar             *asset_dir;
	GString           *asset_filter;
	gint               asset_type_filter;
	GPtrArray         *asset_entries;  /* EditorFileEntry* */
	gboolean           assets_dirty;
	GFileMonitor      *asset_monitor;
	gchar             *asset_selected;
	gchar             *asset_rename;
	GString           *asset_rename_buf;
	gboolean           asset_rename_focus;
	gboolean           asset_grid;

	/* Console */
	GString           *console_input;
	GString           *console_filter;
	guint              console_levels;     /* bitmask of shown levels */
	gboolean           console_autoscroll;
	GPtrArray         *console_history;    /* gchar* */
	gint               console_history_pos;
	guint              console_seen;       /* records seen (scroll on new) */
	GPtrArray         *console_rows;       /* EditorConsoleRow*, mirrors the log ring (lazy) */
	GPtrArray         *console_shown;      /* borrowed rows passing the level/text filter */
	gchar             *console_shown_needle; /* casefolded filter console_shown was built for */
	guint              console_shown_levels; /* level mask console_shown was built for */
	gfloat             console_shown_w;    /* widest row in console_shown */
	gfloat             console_font_size;  /* font size the row widths were measured at */
	guint64            console_popup_serial; /* row whose context menu is open, 0 = none */

	/* Problems */
	GPtrArray         *problems;           /* LrgEditorProblem* */
	gboolean           problems_dirty;
	gdouble            problems_time;

	/* Dialogs */
	EditorDialog       dialog;
	gboolean           dialog_just_opened;
	EditorFileMode     file_mode;
	gchar             *file_dir;
	GString           *file_name;
	GPtrArray         *file_entries;
	gchar             *file_selected;
	gchar             *file_ext;           /* filter, e.g. ".rlevel" */
	gchar             *file_target_guid;   /* node the picked asset applies to */
	gint               file_target_slot;   /* script index etc. */
	EditorPending      pending;
	gchar             *pending_path;
	gchar             *confirm_message;
	GString           *dialog_text1;
	GString           *dialog_text2;
	GString           *dialog_text3;
	GString           *dialog_text4;
	GString           *dialog_text5;
	gint               dialog_choice;
	gint               prefs_tab;
	gchar             *capture_action;     /* keybinding being captured */
	gboolean           palette_open;
	gboolean           quick_open;
	GString           *palette_query;
	gint               palette_index;
	GPtrArray         *quick_files;        /* gchar* relative paths */
	gchar             *recovery_file;

	/* Play */
	LrgWorld          *play_world;
	gchar             *play_snapshot;
	GHashTable        *play_entries;       /* GUID -> PlayEntry (lrg-editor-play.c) */
	guint              play_undo_base;
	gboolean           play_was_dirty;
	gboolean           playing;
	gboolean           paused;
	gboolean           step_once;
	gdouble            play_time;
	GSubprocess       *run_proc;
	GCancellable      *run_cancel;

	/* Status, toasts, autosave, file monitor */
	gchar             *status;
	GQueue             toasts;             /* EditorToast* */
	gdouble            last_autosave;
	GFileMonitor      *level_monitor;
	gboolean           level_changed_on_disk;
	gint64             level_saved_mtime;
	gchar             *window_title;
	gchar             *window_shot;        /* pending full-window screenshot */
	gchar             *level_dir;          /* base dir cache without a project */
	const LrgGuiInput *cur_input;          /* input of the frame being built */
	LrgGuiInput        last_input;         /* copy of the last frame's input (ui.state) */
	LrgGuiRect         menu_rects[8];      /* menubar headers, for ui.state */
};

/* --- lrg-editor-app.c --- */
G_GNUC_INTERNAL void     editor_app_notify          (LrgEditorApp *self, gint level, const gchar *format, ...) G_GNUC_PRINTF (3, 4);
G_GNUC_INTERNAL void     editor_app_set_status      (LrgEditorApp *self, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
G_GNUC_INTERNAL gchar *  editor_app_project_path    (LrgEditorApp *self, const gchar *path);
G_GNUC_INTERNAL gchar *  editor_app_relative_path   (LrgEditorApp *self, const gchar *path);
G_GNUC_INTERNAL const gchar * editor_app_base_dir   (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_request         (LrgEditorApp *self, EditorPending what, const gchar *path);
G_GNUC_INTERNAL gboolean editor_app_save            (LrgEditorApp *self);
G_GNUC_INTERNAL gboolean editor_app_save_as         (LrgEditorApp *self, const gchar *path);
G_GNUC_INTERNAL LrgScriptLanguage editor_script_language_for_path (const gchar *path);
G_GNUC_INTERNAL void     editor_app_open_file       (LrgEditorApp *self, const gchar *path, gint line, gint col);
G_GNUC_INTERNAL void     editor_app_open_asset      (LrgEditorApp *self, const gchar *path);
G_GNUC_INTERNAL LrgNode * editor_app_create_node    (LrgEditorApp *self, const gchar *name, LrgNodeVisualKind kind,
                                                     LrgPrimitiveType primitive, LrgNode *parent);
G_GNUC_INTERNAL void     editor_app_frame_selection (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_frame_all       (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_menu_create     (LrgEditorApp *self, LrgNode *parent);
G_GNUC_INTERNAL gboolean editor_app_ensure_project_dir (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_apply_settings  (LrgEditorApp *self);
G_GNUC_INTERNAL gchar *  editor_app_create_script   (LrgEditorApp *self, const gchar *name, LrgScriptLanguage lang,
                                                     GError **error);
G_GNUC_INTERNAL void     editor_app_menu_item_action (LrgEditorApp *self, const gchar *id);
G_GNUC_INTERNAL void     editor_app_confirm_choice  (LrgEditorApp *self, gint choice);
G_GNUC_INTERNAL void     editor_app_reset_layout    (LrgEditorApp *self);

/* --- lrg-editor-app-actions.c --- */
G_GNUC_INTERNAL void     editor_app_register_actions (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_register_methods (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_app_process_shortcuts (LrgEditorApp *self);

/* --- panels --- */
G_GNUC_INTERNAL void     editor_panel_hierarchy     (LrgEditorApp *self);
G_GNUC_INTERNAL guint    editor_app_count_nodes     (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_panel_inspector     (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_panel_viewport      (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_panel_assets        (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_panel_console       (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_panel_history       (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_panel_problems      (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_assets_refresh      (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_assets_set_dir      (LrgEditorApp *self, const gchar *dir);
G_GNUC_INTERNAL void     editor_file_entry_free     (gpointer data);
G_GNUC_INTERNAL GPtrArray * editor_list_dir         (const gchar *dir, const gchar *ext, gboolean dirs_only);
G_GNUC_INTERNAL const gchar * editor_asset_glyph    (gint type, gboolean is_dir);
G_GNUC_INTERNAL void     editor_console_execute     (LrgEditorApp *self, const gchar *line);
G_GNUC_INTERNAL void     editor_viewport_drop_asset (LrgEditorApp *self, const gchar *path, gfloat x, gfloat y);
G_GNUC_INTERNAL void     editor_view_reset          (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_view_align          (LrgEditorApp *self, gfloat x, gfloat y, gfloat z);
G_GNUC_INTERNAL void     editor_view_orbit          (LrgEditorApp *self, gfloat yaw, gfloat pitch);

/* --- lrg-editor-render.c --- */
G_GNUC_INTERNAL void     editor_render_init         (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_render_shutdown     (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_render_viewport     (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_render_blit         (const LrgGuiRect *rect, gpointer user_data);
G_GNUC_INTERNAL gboolean editor_render_screenshot   (LrgEditorApp *self, const gchar *path, GError **error);

/* --- lrg-editor-dialogs.c --- */
G_GNUC_INTERNAL void     editor_dialogs_draw        (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_dialog_open         (LrgEditorApp *self, EditorDialog dialog);
G_GNUC_INTERNAL void     editor_file_dialog_open    (LrgEditorApp *self, EditorFileMode mode, const gchar *dir,
                                                     const gchar *ext);
G_GNUC_INTERNAL void     editor_palette_open        (LrgEditorApp *self, gboolean quick_open);

/* --- lrg-editor-play.c --- */
G_GNUC_INTERNAL void     editor_play_start          (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_play_stop           (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_play_toggle_pause   (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_play_step           (LrgEditorApp *self);
G_GNUC_INTERNAL void     editor_play_tick           (LrgEditorApp *self, gfloat delta);
G_GNUC_INTERNAL gboolean editor_run_start           (LrgEditorApp *self, GError **error);
G_GNUC_INTERNAL void     editor_run_stop            (LrgEditorApp *self);

G_END_DECLS
