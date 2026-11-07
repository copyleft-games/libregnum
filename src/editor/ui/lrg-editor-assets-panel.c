/* lrg-editor-assets-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorAssetsPanel: the asset browser on the retained UI toolkit.
 *
 *   +-----------------------------------------------------------------+
 *   | (branch) game > assets > textures   [filter....] [type v] [#] [+] |  toolbar
 *   +-----------------------------------------------------------------+
 *   |  [tile] [tile] [tile] [tile]          LrgTileView (grid), or      |
 *   |  [tile] [tile] ...                    LrgTable (list): name, type,|
 *   |                                       size, modified, git         |
 *   +-----------------------------------------------------------------+
 *   | 12 items, 2 selected                                            |  footer
 *   +-----------------------------------------------------------------+
 *
 * The panel lists ONE folder (non-recursive, like 0.8.0) into a
 * GListStore of EditorAssetItem objects that both views share, so the
 * list's column sort also orders the grid and the "display order" of
 * the public API is the store order. Hidden files and .meta sidecars are
 * never listed. The listing follows a GFileMonitor on the folder and is
 * rebuilt on the next frame after a change (the selection is carried
 * over by path).
 *
 * The app owns the project state (LrgAssetDatabase with write-meta on,
 * LrgEditorVcs) and replaces it when another project opens; the panel
 * notices in step() (and at every public entry point) by comparing the
 * pointers and the project directory, re-roots itself and reconnects to
 * LrgEditorVcs::changed. Tiles and cells read the git status cached on
 * their item at paint time, so a ::changed only updates the items.
 *
 * Every file operation goes through LrgAssetDatabase so .meta sidecars
 * (and therefore GUIDs) travel with their assets: rename, move (drag
 * onto a folder or a breadcrumb segment), duplicate (fresh GUID) and
 * trash. Without a project (a lone level) the panel uses a private
 * database rooted at the level's folder.
 *
 * Private types in this file:
 *   EditorAssetItem        one listed file or folder (the model item)
 *   EditorAssetTile        a grid tile, a list name cell or a list git
 *                          cell (one widget class, three modes)
 *   EditorAssetsButton     a flat icon button of the toolbar
 *   EditorAssetMetaSource  LrgPropertySource over an asset's .meta
 *                          import settings (the Import Settings dialog)
 */

#include "config.h"

#include "data/lrg-editor-data-editor.h"
#include "lrg-editor-assets-panel.h"
#include "lrg-editor-app-private.h"
#include "../lrg-asset-meta.h"
#include "../../ui/lrg-breadcrumb.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-tile-view.h"
#include "../../ui/lrg-table.h"
#include "../../ui/lrg-menu.h"
#include "../../ui/lrg-dialog.h"
#include "../../ui/lrg-drag.h"
#include "../../ui/lrg-drop-target.h"
#include "../../ui/lrg-syntax.h"
#include "../../ui/lrg-property-source.h"
#include "../../ui/lrg-property-grid.h"
#include "../../paint/lrg-paint-icons.h"
#include "../../text/lrg-font-manager.h"
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

/* Content types of asset drags (docs/modules/ui/drag-and-drop.org) */
#define ASSET_MIME      "application/x-lrg-asset"
#define ASSET_LIST_MIME "application/x-lrg-asset-list"

/* Grid tile size (canvas units, before the card inset of LrgTileView) */
#define TILE_W (108.0f)
#define TILE_H (124.0f)

/* Lines of a script shown on its live code tile */
#define CODE_LINES (12)

/* Bytes read from a script for its code tile */
#define CODE_BYTES (4096)

/* List columns */
enum
{
	COL_NAME,
	COL_TYPE,
	COL_SIZE,
	COL_MODIFIED,
	COL_GIT,
	N_COLS
};

/* Modes of EditorAssetTile */
typedef enum
{
	TILE_MODE_GRID,
	TILE_MODE_NAME_CELL,
	TILE_MODE_GIT_CELL
} TileMode;

/* Type filter entries; the index is the LrgAssetType value */
static const gchar *type_filters[] = {
	"All types", "Textures", "Models", "Audio", "Fonts", "Scripts", "Levels", "Prefabs",
	"Tilesets", "Scenes", "CAD"
};

/* ==========================================================================
 * EditorAssetItem: one listed file or folder
 * ========================================================================== */

#define EDITOR_TYPE_ASSET_ITEM (editor_asset_item_get_type ())
G_DECLARE_FINAL_TYPE (EditorAssetItem, editor_asset_item, EDITOR, ASSET_ITEM, GObject)

struct _EditorAssetItem
{
	GObject             parent_instance;

	gchar              *name;
	gchar              *path;          /* absolute */
	gboolean            is_dir;
	LrgAssetType        type;
	goffset             size;
	gint64              mtime;         /* seconds */
	LrgEditorVcsStatus  vcs;           /* own status, or the folder aggregate */

	/* Script code tile: the first lines, read once */
	gboolean            code_loaded;
	gchar             **code;

	/* Name ellipsized for the last width it was painted at */
	gchar              *short_name;
	gfloat              short_width;
	gfloat              short_size;
};

G_DEFINE_FINAL_TYPE (EditorAssetItem, editor_asset_item, G_TYPE_OBJECT)

static void
editor_asset_item_finalize (GObject *object)
{
	EditorAssetItem *self = EDITOR_ASSET_ITEM (object);

	g_free (self->name);
	g_free (self->path);
	g_strfreev (self->code);
	g_free (self->short_name);
	G_OBJECT_CLASS (editor_asset_item_parent_class)->finalize (object);
}

static void
editor_asset_item_class_init (EditorAssetItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_asset_item_finalize;
}

static void
editor_asset_item_init (EditorAssetItem *self)
{
}

/*
 * item_load_code:
 *
 * Reads the first CODE_BYTES of a script once and keeps its first
 * CODE_LINES lines, tabs expanded to two spaces (the tile is tiny), so
 * the code tile paints from memory every frame.
 */
static void
item_load_code (EditorAssetItem *item)
{
	g_autoptr(GFile)            file = NULL;
	g_autoptr(GFileInputStream) in = NULL;
	gchar                       buf[CODE_BYTES + 1];
	gssize                      n;
	g_auto(GStrv)               lines = NULL;
	GPtrArray                  *out;
	guint                       i;

	if (item->code_loaded)
		return;
	item->code_loaded = TRUE;

	file = g_file_new_for_path (item->path);
	in = g_file_read (file, NULL, NULL);
	if (in == NULL)
		return;
	n = g_input_stream_read (G_INPUT_STREAM (in), buf, CODE_BYTES, NULL, NULL);
	if (n <= 0)
		return;
	buf[n] = '\0';

	lines = g_strsplit (buf, "\n", CODE_LINES + 1);
	out = g_ptr_array_new ();
	for (i = 0; lines[i] != NULL && i < CODE_LINES; i++)
	{
		g_auto(GStrv) parts = g_strsplit (lines[i], "\t", -1);
		gchar        *line = g_strjoinv ("  ", parts);

		g_strchomp (line);
		/* Cut a line that ran into the read limit mid character */
		if (!g_utf8_validate (line, -1, NULL))
		{
			const gchar *end = NULL;

			g_utf8_validate (line, -1, &end);
			line[end - line] = '\0';
		}
		g_ptr_array_add (out, line);
	}
	g_ptr_array_add (out, NULL);
	item->code = (gchar **) g_ptr_array_free (out, FALSE);
}

/* ==========================================================================
 * Panel structure
 * ========================================================================== */

typedef struct _EditorAssetsButton EditorAssetsButton;
typedef struct _EditorAssetTile EditorAssetTile;

struct _LrgEditorAssetsPanel
{
	LrgContainer        parent_instance;

	LrgEditorApp       *app;             /* weak */

	/* What the panel follows on the app (compared in sync_app) */
	gchar              *root;            /* project root, or the level's folder */
	LrgEditorVcs       *vcs;             /* ref, with vcs_handler connected */
	gulong              vcs_handler;
	LrgAssetDatabase   *own_db;          /* without a project */

	/* The folder shown */
	gchar              *dir;
	GFileMonitor       *monitor;
	gboolean            dirty;
	GPtrArray          *all;             /* EditorAssetItem, unfiltered */
	GListStore         *store;           /* EditorAssetItem, shown */
	gchar              *filter;
	LrgAssetType        type_filter;
	gboolean            grid;

	/* Selection, by absolute path, in display order */
	GPtrArray          *selected;
	GPtrArray          *prev_selected;   /* before the last change (drags) */
	gchar              *pending_select;  /* select after the next rebuild */
	gboolean            syncing;

	/* Widgets (owned by the container) */
	LrgBreadcrumb      *crumb;
	LrgTextInput       *search;
	LrgComboBox        *type_combo;
	EditorAssetsButton *view_button;
	EditorAssetsButton *new_button;
	LrgTileView        *grid_view;
	LrgTable           *table;
	LrgTextInput       *rename;

	/* Toolbar geometry (panel-local) */
	gfloat              bar_h;
	gfloat              footer_h;
	gfloat              chip_x;
	gfloat              chip_w;

	/* Inline rename */
	gchar              *renaming;
	gboolean            rename_focused;

	/* Context menu */
	GSimpleActionGroup *actions;
	GPtrArray          *context;         /* paths the entry menu acts on */
	LrgMenu            *new_menu;

	/* Drag and drop */
	gchar              *drop_hover;      /* folder a drop would move into */
	GPtrArray          *dragging;        /* paths being dragged */

	/* Dialogs */
	LrgDialog          *trash_dialog;    /* weak */
	LrgDialog          *import_dialog;   /* weak */
	GPtrArray          *trash_paths;
};

G_DEFINE_FINAL_TYPE (LrgEditorAssetsPanel, lrg_editor_assets_panel, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_DIRECTORY,
	PROP_GRID_VIEW,
	PROP_FILTER,
	PROP_TYPE_FILTER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void sync_app (LrgEditorAssetsPanel *self);
static void rebuild_view (LrgEditorAssetsPanel *self);
static void refresh_listing (LrgEditorAssetsPanel *self);
static void open_entry (LrgEditorAssetsPanel *self, const gchar *path);
static void update_context_menu (LrgEditorAssetsPanel *self, gboolean background);
static void cancel_rename (LrgEditorAssetsPanel *self);
static EditorAssetItem * item_at (LrgEditorAssetsPanel *self, guint position);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static LrgTheme *
theme_of (gpointer widget)
{
	return lrg_widget_get_theme (LRG_WIDGET (widget));
}

static const GrlColor *
color_of (gpointer      widget,
          LrgThemeColor role)
{
	return lrg_theme_get_color (theme_of (widget), role);
}

static gfloat
metric_of (gpointer       widget,
           LrgThemeMetric metric)
{
	return lrg_theme_get_metric (theme_of (widget), metric);
}

/* Is @path @root or inside it? */
static gboolean
path_is_under (const gchar *path,
               const gchar *root)
{
	gsize len;

	if (path == NULL || root == NULL)
		return FALSE;
	len = strlen (root);
	if (len > 1 && root[len - 1] == G_DIR_SEPARATOR)
		len--;
	return strncmp (path, root, len) == 0 && (path[len] == '\0' || path[len] == G_DIR_SEPARATOR);
}

/* An absolute, canonical path for @path (relative to the project). */
static gchar *
abs_path (LrgEditorAssetsPanel *self,
          const gchar          *path)
{
	g_autofree gchar *joined = NULL;

	if (path == NULL)
		return NULL;
	if (g_path_is_absolute (path))
		return g_canonicalize_filename (path, NULL);
	joined = (self->root != NULL) ? g_build_filename (self->root, path, NULL) : g_strdup (path);
	return g_canonicalize_filename (joined, NULL);
}

/* The path relative to the project root (drag payloads, Copy Path). */
static gchar *
rel_path (LrgEditorAssetsPanel *self,
          const gchar          *path)
{
	gsize len;

	if (self->root == NULL || !path_is_under (path, self->root))
		return g_strdup (path);
	len = strlen (self->root);
	if (path[len] == '\0')
		return g_strdup ("");
	return g_strdup (path + len + 1);
}

/* The asset database the file operations use: the app's (project), or a
 * private one rooted at the panel's root (a level without a project). */
static LrgAssetDatabase *
get_db (LrgEditorAssetsPanel *self)
{
	if (self->app != NULL && self->app->assets != NULL)
		return self->app->assets;
	if (self->root == NULL)
		return NULL;
	if (self->own_db == NULL)
	{
		self->own_db = lrg_asset_database_new ();
		lrg_asset_database_add_search_dir (self->own_db, self->root);
		lrg_asset_database_scan (self->own_db, NULL);
	}
	return self->own_db;
}

/* The database entry of @path, scanning once if it is not known yet (a
 * file created outside the editor since the last scan). */
static LrgAssetEntry *
lookup_entry (LrgEditorAssetsPanel *self,
              const gchar          *path)
{
	LrgAssetDatabase *db = get_db (self);
	LrgAssetEntry    *entry;

	if (db == NULL)
		return NULL;
	entry = lrg_asset_database_lookup_path (db, path);
	if (entry == NULL && g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		lrg_asset_database_scan (db, NULL);
		entry = lrg_asset_database_lookup_path (db, path);
	}
	return entry;
}

static void
notify_user (LrgEditorAssetsPanel *self,
             gint                  level,
             const gchar          *text)
{
	if (self->app != NULL)
		editor_app_notify (self->app, level, "%s", text);
}

static void
set_status (LrgEditorAssetsPanel *self,
            const gchar          *text)
{
	if (self->app != NULL)
		editor_app_set_status (self->app, "%s", text);
}

/* After a file operation: list again and let git catch up. */
static void
after_file_op (LrgEditorAssetsPanel *self)
{
	refresh_listing (self);
	if (self->vcs != NULL)
		lrg_editor_vcs_queue_refresh (self->vcs);
}

/* Theme role of a file type (tile glyphs and list icons). */
static LrgThemeColor
type_role (LrgAssetType type,
           gboolean     is_dir)
{
	if (is_dir)
		return LRG_THEME_COLOR_WARNING;
	switch (type)
	{
	case LRG_ASSET_TYPE_LEVEL:
	case LRG_ASSET_TYPE_PREFAB:
	case LRG_ASSET_TYPE_SCENE:
		return LRG_THEME_COLOR_ACCENT;
	case LRG_ASSET_TYPE_SCRIPT:
		return LRG_THEME_COLOR_SUCCESS;
	case LRG_ASSET_TYPE_TEXTURE:
	case LRG_ASSET_TYPE_MODEL:
	case LRG_ASSET_TYPE_TILESET:
	case LRG_ASSET_TYPE_CAD:
		return LRG_THEME_COLOR_LINK;
	case LRG_ASSET_TYPE_AUDIO:
	case LRG_ASSET_TYPE_FONT:
		return LRG_THEME_COLOR_DANGER;
	default:
		return LRG_THEME_COLOR_TEXT_MUTED;
	}
}

/* Short glyph of a file type (letters: every UI font has them). */
static const gchar *
type_glyph (LrgAssetType type)
{
	switch (type)
	{
	case LRG_ASSET_TYPE_LEVEL:   return "L";
	case LRG_ASSET_TYPE_PREFAB:  return "P";
	case LRG_ASSET_TYPE_SCRIPT:  return "S";
	case LRG_ASSET_TYPE_TEXTURE: return "T";
	case LRG_ASSET_TYPE_MODEL:   return "M";
	case LRG_ASSET_TYPE_AUDIO:   return "A";
	case LRG_ASSET_TYPE_FONT:    return "F";
	case LRG_ASSET_TYPE_TILESET: return "Ts";
	case LRG_ASSET_TYPE_SCENE:   return "Sc";
	case LRG_ASSET_TYPE_CAD:     return "C";
	default:                     return NULL;
	}
}

static const gchar *
type_name (LrgAssetType type,
           gboolean     is_dir)
{
	if (is_dir)
		return "Folder";
	switch (type)
	{
	case LRG_ASSET_TYPE_TEXTURE: return "Texture";
	case LRG_ASSET_TYPE_MODEL:   return "Model";
	case LRG_ASSET_TYPE_AUDIO:   return "Audio";
	case LRG_ASSET_TYPE_FONT:    return "Font";
	case LRG_ASSET_TYPE_SCRIPT:  return "Script";
	case LRG_ASSET_TYPE_LEVEL:   return "Level";
	case LRG_ASSET_TYPE_PREFAB:  return "Prefab";
	case LRG_ASSET_TYPE_TILESET: return "Tileset";
	case LRG_ASSET_TYPE_SCENE:   return "Scene";
	case LRG_ASSET_TYPE_CAD:     return "CAD";
	default:                     return "File";
	}
}

/* Theme role of a git status badge. */
static LrgThemeColor
vcs_role (LrgEditorVcsStatus status)
{
	switch (status)
	{
	case LRG_EDITOR_VCS_STATUS_MODIFIED:   return LRG_THEME_COLOR_WARNING;
	case LRG_EDITOR_VCS_STATUS_ADDED:      return LRG_THEME_COLOR_SUCCESS;
	case LRG_EDITOR_VCS_STATUS_UNTRACKED:  return LRG_THEME_COLOR_SUCCESS;
	case LRG_EDITOR_VCS_STATUS_DELETED:    return LRG_THEME_COLOR_DANGER;
	case LRG_EDITOR_VCS_STATUS_CONFLICTED: return LRG_THEME_COLOR_DANGER;
	case LRG_EDITOR_VCS_STATUS_RENAMED:    return LRG_THEME_COLOR_LINK;
	default:                               return LRG_THEME_COLOR_TEXT_MUTED;
	}
}

/*
 * paint_badge:
 *
 * A git status badge: a small pill tinted with the status colour (22%
 * fill, full-strength border and letter), readable on dark and light
 * themes alike. Centred on (@cx, @cy).
 */
static void
paint_badge (LrgWidget          *widget,
             LrgPaintList       *list,
             LrgEditorVcsStatus  status,
             gfloat              cx,
             gfloat              cy,
             gfloat              size)
{
	const GrlColor *c = color_of (widget, vcs_role (status));
	GrlColor        fill;
	GrlColor        back;
	gchar           letter[2] = { 0, 0 };
	gfloat          tw = 0.0f;
	gfloat          th = 0.0f;
	gfloat          fs = size * 0.72f;

	letter[0] = lrg_editor_vcs_status_to_letter (status);
	if (letter[0] == '\0')
		return;
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme_of (widget)), letter, fs, 0.0f, &tw, &th);

	/* An opaque disc of the panel colour first, so the tint reads over
	 * thumbnails too */
	back = *color_of (widget, LRG_THEME_COLOR_PANEL);
	lrg_paint_list_rect (list, cx - size * 0.5f, cy - size * 0.5f, size, size, size * 0.5f, &back);
	lrg_paint_color_with_alpha (c, 0.22f, &fill);
	lrg_paint_list_rect (list, cx - size * 0.5f, cy - size * 0.5f, size, size, size * 0.5f, &fill);
	lrg_paint_list_border (list, cx - size * 0.5f, cy - size * 0.5f, size, size, size * 0.5f, 1.0f, c);
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme_of (widget)), letter, cx - tw * 0.5f,
	                     cy - th * 0.5f, fs, 0.0f, c);
}

/*
 * ellipsize:
 *
 * @text cut with a trailing ellipsis so it measures at most @max_w at
 * @size. Cached on the item for the width and size it was made for.
 */
static const gchar *
item_short_name (EditorAssetItem *item,
                 GrlFont         *font,
                 gfloat           size,
                 gfloat           max_w)
{
	gfloat w = 0.0f;
	glong  n;

	if (item->short_name != NULL && item->short_width == max_w && item->short_size == size)
		return item->short_name;

	g_free (item->short_name);
	item->short_width = max_w;
	item->short_size = size;
	lrg_paint_measure_text (font, item->name, size, 0.0f, &w, NULL);
	if (w <= max_w)
	{
		item->short_name = g_strdup (item->name);
		return item->short_name;
	}

	/* Drop characters until "name..." fits (at least one is kept) */
	for (n = g_utf8_strlen (item->name, -1) - 1; n > 1; n--)
	{
		g_autofree gchar *head = g_utf8_substring (item->name, 0, n);
		gchar            *cand = g_strconcat (head, "\xe2\x80\xa6", NULL);

		lrg_paint_measure_text (font, cand, size, 0.0f, &w, NULL);
		if (w <= max_w)
		{
			item->short_name = cand;
			return item->short_name;
		}
		g_free (cand);
	}
	item->short_name = g_strconcat ("\xe2\x80\xa6", NULL);
	return item->short_name;
}

/* The monospace face for code tiles, else the UI font. */
static GrlFont *
code_font (LrgWidget *widget)
{
	GrlFont *font = lrg_font_manager_get_font (lrg_font_manager_get_default (), "ui-mono");

	return (font != NULL) ? font : lrg_theme_get_ui_font (theme_of (widget));
}

/* ==========================================================================
 * EditorAssetTile: grid tile, list name cell, list git cell
 * ========================================================================== */

#define EDITOR_TYPE_ASSET_TILE (editor_asset_tile_get_type ())
G_DECLARE_FINAL_TYPE (EditorAssetTile, editor_asset_tile, EDITOR, ASSET_TILE, LrgWidget)

struct _EditorAssetTile
{
	LrgWidget             parent_instance;

	LrgEditorAssetsPanel *panel;   /* not owned: tiles die with the panel's views */
	EditorAssetItem      *item;    /* owned */
	TileMode              mode;
};

G_DEFINE_FINAL_TYPE (EditorAssetTile, editor_asset_tile, LRG_TYPE_WIDGET)

static void
tile_set_item (EditorAssetTile *self,
               EditorAssetItem *item)
{
	g_set_object (&self->item, item);
}

/*
 * paint_code_tile:
 *
 * A script's live code tile: the first CODE_LINES lines tokenized with
 * the file's LrgSyntax language and painted run by run in the token
 * colours on an INPUT-coloured sheet (the colours are tuned to read on
 * INPUT). Nothing is cached as an image, so a theme change recolours
 * the tile on the next frame.
 */
static void
paint_code_tile (EditorAssetTile *self,
                 LrgPaintList    *list,
                 gfloat           x,
                 gfloat           y,
                 gfloat           w,
                 gfloat           h)
{
	LrgWidget        *widget = LRG_WIDGET (self);
	LrgTheme         *theme = theme_of (widget);
	LrgSyntax        *syntax = lrg_syntax_get_for_filename (self->item->name);
	GrlFont          *font = code_font (widget);
	g_autoptr(GArray) spans = g_array_new (FALSE, FALSE, sizeof (LrgSyntaxSpan));
	GrlColor          sheet = *color_of (widget, LRG_THEME_COLOR_INPUT);
	GrlColor          text;
	gfloat            radius = metric_of (widget, LRG_THEME_METRIC_CORNER_RADIUS) * 0.75f;
	gfloat            pad = 5.0f;
	gfloat            fs;
	gfloat            line_h;
	guint             state = 0;
	guint             i;

	lrg_paint_list_rect (list, x, y, w, h, radius, &sheet);
	lrg_paint_list_border (list, x, y, w, h, radius, 1.0f, color_of (widget, LRG_THEME_COLOR_BORDER));
	lrg_syntax_get_token_color (syntax, theme, LRG_SYNTAX_TOKEN_TEXT, &text);

	item_load_code (self->item);
	if (self->item->code == NULL)
		return;

	/* Twelve lines fill the sheet: a minimap of the file's head */
	line_h = (h - pad * 2.0f) / (gfloat) CODE_LINES;
	fs = MAX (3.0f, line_h / 1.2f);
	lrg_paint_list_push_clip (list, x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f);
	for (i = 0; self->item->code[i] != NULL; i++)
	{
		const gchar *line = self->item->code[i];
		gsize        len = strlen (line);
		gsize        at = 0;
		gfloat       cx = x + pad;
		gfloat       cy = y + pad + line_h * (gfloat) i;
		guint        s;

		g_array_set_size (spans, 0);
		if (syntax != NULL)
			state = lrg_syntax_tokenize_line (syntax, line, (gssize) len, state, spans);

		/* Runs: the gap before each span in TEXT, then the span */
		for (s = 0; s <= spans->len && at < len; s++)
		{
			const LrgSyntaxSpan *span = (s < spans->len) ? &g_array_index (spans, LrgSyntaxSpan, s) : NULL;
			gsize                start = (span != NULL) ? MIN (span->start, len) : len;
			gsize                end = (span != NULL) ? MIN (span->end, len) : len;
			guint                k;

			for (k = 0; k < 2; k++)
			{
				gsize             a = (k == 0) ? at : start;
				gsize             b = (k == 0) ? start : end;
				g_autofree gchar *run = NULL;
				GrlColor          col = text;
				gfloat            rw = 0.0f;

				if (b <= a || (k == 1 && span == NULL))
					continue;
				run = g_strndup (line + a, b - a);
				if (k == 1)
					lrg_syntax_get_token_color (syntax, theme, span->token, &col);
				lrg_paint_list_text (list, font, run, cx, cy, fs, 0.0f, &col);
				lrg_paint_measure_text (font, run, fs, 0.0f, &rw, NULL);
				cx += rw;
			}
			at = MAX (at, end);
			if (cx > x + w)
				break;
		}
	}
	lrg_paint_list_pop_clip (list);
}

/*
 * paint_type_glyph:
 *
 * The fallback preview: a rounded square tinted with the type colour and
 * the type's letter (dimmed while a thumbnail is still coming).
 */
static void
paint_type_glyph (LrgWidget       *widget,
                  LrgPaintList    *list,
                  EditorAssetItem *item,
                  gfloat           x,
                  gfloat           y,
                  gfloat           size,
                  gboolean         pending)
{
	const GrlColor *c = color_of (widget, type_role (item->type, item->is_dir));
	const gchar    *glyph = type_glyph (item->type);
	GrlColor        fill;

	if (pending)
		lrg_paint_list_push_opacity (list, 0.55f);
	if (item->is_dir)
	{
		lrg_paint_list_icon (list, LRG_ICON_FOLDER, x, y, size, 0.0f, c);
	}
	else if (glyph == NULL)
	{
		lrg_paint_list_icon (list, LRG_ICON_FILE, x, y, size, 0.0f, c);
	}
	else
	{
		gfloat tw = 0.0f;
		gfloat th = 0.0f;
		gfloat fs = size * 0.46f;
		gfloat r = size * 0.22f;

		lrg_paint_color_with_alpha (c, 0.18f, &fill);
		lrg_paint_list_rect (list, x + size * 0.1f, y + size * 0.1f, size * 0.8f, size * 0.8f, r, &fill);
		lrg_paint_list_border (list, x + size * 0.1f, y + size * 0.1f, size * 0.8f, size * 0.8f, r, 1.0f, c);
		lrg_paint_measure_text (lrg_theme_get_ui_font (theme_of (widget)), glyph, fs, 0.0f, &tw, &th);
		lrg_paint_list_text (list, lrg_theme_get_ui_font (theme_of (widget)), glyph, x + (size - tw) * 0.5f,
		                     y + (size - th) * 0.5f, fs, 0.0f, c);
	}
	if (pending)
		lrg_paint_list_pop_opacity (list);
}

/* The grid tile: preview, name, git badge, drop highlight. */
static void
paint_grid_tile (EditorAssetTile *self,
                 LrgPaintList    *list)
{
	LrgWidget       *widget = LRG_WIDGET (self);
	EditorAssetItem *item = self->item;
	GrlFont         *font = lrg_theme_get_ui_font (theme_of (widget));
	gfloat           x = lrg_widget_get_world_x (widget);
	gfloat           y = lrg_widget_get_world_y (widget);
	gfloat           w = lrg_widget_get_width (widget);
	gfloat           h = lrg_widget_get_height (widget);
	gfloat           fs = metric_of (widget, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat           name_h = fs * 1.5f;
	gfloat           box = MIN (w, h - name_h - 4.0f);
	gfloat           bx = x + (w - box) * 0.5f;
	gfloat           by = y;
	const gchar     *name;
	gfloat           tw = 0.0f;

	/* 1. Preview */
	if (!item->is_dir && item->type == LRG_ASSET_TYPE_SCRIPT)
	{
		paint_code_tile (self, list, bx, by, box, box);
	}
	else
	{
		gboolean pending = FALSE;
		GObject *tex = NULL;

		if (!item->is_dir && self->panel->app != NULL)
			tex = editor_thumbnail_texture (self->panel->app, item->path, &pending);
		if (tex != NULL)
		{
			/* Fit the thumbnail in the box, aspect kept, on a sheet */
			gfloat   iw = (gfloat) grl_texture_get_width (GRL_TEXTURE (tex));
			gfloat   ih = (gfloat) grl_texture_get_height (GRL_TEXTURE (tex));
			gfloat   k = box / MAX (MAX (iw, ih), 1.0f);
			gfloat   radius = metric_of (widget, LRG_THEME_METRIC_CORNER_RADIUS) * 0.75f;
			GrlColor sheet;

			lrg_paint_color_with_alpha (color_of (widget, LRG_THEME_COLOR_WINDOW), 0.6f, &sheet);
			lrg_paint_list_rect (list, bx, by, box, box, radius, &sheet);
			lrg_paint_list_image (list, GRL_TEXTURE (tex), NULL, bx + (box - iw * k) * 0.5f,
			                      by + (box - ih * k) * 0.5f, iw * k, ih * k, radius * 0.5f, NULL);
		}
		else
		{
			gfloat gs = box * 0.66f;

			paint_type_glyph (widget, list, item, bx + (box - gs) * 0.5f, by + (box - gs) * 0.5f, gs, pending);
		}
	}

	/* 2. Name, centred and ellipsized */
	name = item_short_name (item, font, fs, w - 4.0f);
	lrg_paint_measure_text (font, name, fs, 0.0f, &tw, NULL);
	lrg_paint_list_text (list, font, name, x + (w - tw) * 0.5f, y + h - name_h + (name_h - fs) * 0.5f, fs, 0.0f,
	                     color_of (widget, LRG_THEME_COLOR_TEXT));

	/* 3. Git badge in the preview's top-right corner */
	if (item->vcs != LRG_EDITOR_VCS_STATUS_UNMODIFIED)
	{
		gfloat bs = MAX (12.0f, fs * 1.25f);

		paint_badge (widget, list, item->vcs, bx + box - bs * 0.5f, by + bs * 0.5f, bs);
	}

	/* 4. A folder under a drag that would move into it */
	if (item->is_dir && g_strcmp0 (self->panel->drop_hover, item->path) == 0)
	{
		const GrlColor *accent = color_of (widget, LRG_THEME_COLOR_ACCENT);
		GrlColor        wash;
		gfloat          r = metric_of (widget, LRG_THEME_METRIC_CORNER_RADIUS);

		lrg_paint_color_with_alpha (accent, 0.16f, &wash);
		lrg_paint_list_rect (list, x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, r, &wash);
		lrg_paint_list_border (list, x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, r, 2.0f, accent);
	}
}

/* The list's name cell: a small type glyph and the name. */
static void
paint_name_cell (EditorAssetTile *self,
                 LrgPaintList    *list)
{
	LrgWidget       *widget = LRG_WIDGET (self);
	EditorAssetItem *item = self->item;
	GrlFont         *font = lrg_theme_get_ui_font (theme_of (widget));
	gfloat           x = lrg_widget_get_world_x (widget);
	gfloat           y = lrg_widget_get_world_y (widget);
	gfloat           w = lrg_widget_get_width (widget);
	gfloat           h = lrg_widget_get_height (widget);
	gfloat           fs = metric_of (widget, LRG_THEME_METRIC_FONT_SIZE);
	gfloat           gs = MIN (h - 2.0f, fs * 1.3f);
	const gchar     *name;

	paint_type_glyph (widget, list, item, x + 2.0f, y + (h - gs) * 0.5f, gs, FALSE);
	name = item_short_name (item, font, fs, MAX (8.0f, w - gs - 10.0f));
	lrg_paint_list_text (list, font, name, x + gs + 8.0f, y + (h - fs) * 0.5f, fs, 0.0f,
	                     color_of (widget, LRG_THEME_COLOR_TEXT));

	if (item->is_dir && g_strcmp0 (self->panel->drop_hover, item->path) == 0)
		lrg_paint_list_border (list, x, y + 1.0f, w, h - 2.0f, 4.0f, 2.0f,
		                       color_of (widget, LRG_THEME_COLOR_ACCENT));
}

static void
editor_asset_tile_paint (LrgWidget    *widget,
                         LrgPaintList *list)
{
	EditorAssetTile *self = EDITOR_ASSET_TILE (widget);

	if (self->item == NULL || self->panel == NULL)
		return;
	switch (self->mode)
	{
	case TILE_MODE_GRID:
		paint_grid_tile (self, list);
		break;
	case TILE_MODE_NAME_CELL:
		paint_name_cell (self, list);
		break;
	case TILE_MODE_GIT_CELL:
		if (self->item->vcs != LRG_EDITOR_VCS_STATUS_UNMODIFIED)
		{
			gfloat h = lrg_widget_get_height (widget);
			gfloat bs = MIN (h - 4.0f, 16.0f);

			paint_badge (widget, list, self->item->vcs, lrg_widget_get_world_x (widget) + bs * 0.5f + 4.0f,
			             lrg_widget_get_world_y (widget) + h * 0.5f, bs);
		}
		break;
	default:
		break;
	}
}

static void
editor_asset_tile_measure (LrgWidget *widget,
                           gfloat    *width,
                           gfloat    *height)
{
	*width = 16.0f;
	*height = 16.0f;
}

static void
editor_asset_tile_dispose (GObject *object)
{
	EditorAssetTile *self = EDITOR_ASSET_TILE (object);

	g_clear_object (&self->item);
	G_OBJECT_CLASS (editor_asset_tile_parent_class)->dispose (object);
}

static void
editor_asset_tile_class_init (EditorAssetTileClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = editor_asset_tile_dispose;
	LRG_WIDGET_CLASS (klass)->paint = editor_asset_tile_paint;
	LRG_WIDGET_CLASS (klass)->measure = editor_asset_tile_measure;
}

static void
editor_asset_tile_init (EditorAssetTile *self)
{
}

static LrgWidget *
tile_new (LrgEditorAssetsPanel *panel,
          TileMode              mode)
{
	EditorAssetTile *tile = g_object_new (EDITOR_TYPE_ASSET_TILE, NULL);

	tile->panel = panel;
	tile->mode = mode;
	return LRG_WIDGET (tile);
}

/* ==========================================================================
 * EditorAssetsButton: a flat icon button
 * ========================================================================== */

typedef void (*ButtonFunc) (LrgEditorAssetsPanel *self);

#define EDITOR_TYPE_ASSETS_BUTTON (editor_assets_button_get_type ())
G_DECLARE_FINAL_TYPE (EditorAssetsButton, editor_assets_button, EDITOR, ASSETS_BUTTON, LrgWidget)

struct _EditorAssetsButton
{
	LrgWidget             parent_instance;

	LrgIcon               icon;
	ButtonFunc            func;
	LrgEditorAssetsPanel *panel;   /* not owned */
	gboolean              armed;
};

G_DEFINE_FINAL_TYPE (EditorAssetsButton, editor_assets_button, LRG_TYPE_WIDGET)

static void
editor_assets_button_style_defaults (LrgWidget      *widget,
                                     LrgUIPart       part,
                                     LrgWidgetState  state,
                                     LrgPartLook    *look)
{
	LrgTheme *theme = theme_of (widget);
	GrlColor  bg;

	if (part != LRG_UI_PART_BOX)
	{
		LRG_WIDGET_CLASS (editor_assets_button_parent_class)->style_defaults (widget, part, state, look);
		return;
	}

	/* Flat: transparent at rest, CONTROL tints on hover / press; the
	 * CHECKED state (grid view on) uses the selection colour */
	if (state & LRG_WIDGET_STATE_PRESSED)
		bg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED);
	else if (state & LRG_WIDGET_STATE_HOVERED)
		bg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
	else if (state & LRG_WIDGET_STATE_CHECKED)
		bg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
	else
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER), 0.0f, &bg);
	lrg_part_look_set_background (look, &bg);
	lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, (state & LRG_WIDGET_STATE_CHECKED)
	                                                         ? LRG_THEME_COLOR_ACCENT : LRG_THEME_COLOR_TEXT));
	lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
	if (state & LRG_WIDGET_STATE_DISABLED)
		lrg_part_look_set_opacity (look, 0.45f);
}

static void
editor_assets_button_paint (LrgWidget    *widget,
                            LrgPaintList *list)
{
	EditorAssetsButton *self = EDITOR_ASSETS_BUTTON (widget);
	LrgPartLook         look;
	gfloat              x = lrg_widget_get_world_x (widget);
	gfloat              y = lrg_widget_get_world_y (widget);
	gfloat              w = lrg_widget_get_width (widget);
	gfloat              h = lrg_widget_get_height (widget);
	gfloat              s = MIN (w, h) * 0.62f;

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
	lrg_widget_paint_box (widget, list, &look, x, y, w, h);
	if (self->icon != LRG_ICON_NONE)
	{
		lrg_paint_list_icon (list, self->icon, x + (w - s) * 0.5f, y + (h - s) * 0.5f, s, 0.0f, &look.foreground);
		return;
	}

	/* The view toggle draws the current view: four tiles for the grid,
	 * three rows for the list */
	{
		gfloat ox = x + (w - s) * 0.5f;
		gfloat oy = y + (h - s) * 0.5f;
		gfloat g = s * 0.14f;
		gfloat c = (s - g) * 0.5f;

		if (self->panel != NULL && self->panel->grid)
		{
			lrg_paint_list_rect (list, ox, oy, c, c, c * 0.25f, &look.foreground);
			lrg_paint_list_rect (list, ox + c + g, oy, c, c, c * 0.25f, &look.foreground);
			lrg_paint_list_rect (list, ox, oy + c + g, c, c, c * 0.25f, &look.foreground);
			lrg_paint_list_rect (list, ox + c + g, oy + c + g, c, c, c * 0.25f, &look.foreground);
		}
		else
		{
			gfloat bh = s * 0.16f;
			guint  i;

			for (i = 0; i < 3; i++)
			{
				gfloat by = oy + s * 0.08f + (gfloat) i * (s - bh - s * 0.16f) * 0.5f;

				lrg_paint_list_rect (list, ox, by, bh, bh, bh * 0.5f, &look.foreground);
				lrg_paint_list_rect (list, ox + bh * 1.6f, by, s - bh * 1.6f, bh, bh * 0.5f, &look.foreground);
			}
		}
	}
}

static void
editor_assets_button_measure (LrgWidget *widget,
                              gfloat    *width,
                              gfloat    *height)
{
	gfloat h = metric_of (widget, LRG_THEME_METRIC_CONTROL_HEIGHT);

	*width = h;
	*height = h;
}

static gboolean
editor_assets_button_handle_event (LrgWidget        *widget,
                                   const LrgUIEvent *event)
{
	EditorAssetsButton *self = EDITOR_ASSETS_BUTTON (widget);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->armed = TRUE;
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0 || !self->armed)
			return FALSE;
		self->armed = FALSE;
		if (lrg_widget_contains_point (widget, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)) &&
		    self->func != NULL)
			self->func (self->panel);
		return TRUE;
	case LRG_UI_EVENT_ACTIVATE:
		if (self->func != NULL)
			self->func (self->panel);
		return TRUE;
	default:
		return FALSE;
	}
}

static void
editor_assets_button_class_init (EditorAssetsButtonClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	widget_class->paint = editor_assets_button_paint;
	widget_class->measure = editor_assets_button_measure;
	widget_class->handle_event = editor_assets_button_handle_event;
	widget_class->style_defaults = editor_assets_button_style_defaults;
}

static void
editor_assets_button_init (EditorAssetsButton *self)
{
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

static EditorAssetsButton *
button_new (LrgEditorAssetsPanel *panel,
            LrgIcon               icon,
            ButtonFunc            func,
            const gchar          *name)
{
	EditorAssetsButton *b = g_object_new (EDITOR_TYPE_ASSETS_BUTTON, NULL);

	b->icon = icon;
	b->func = func;
	b->panel = panel;
	lrg_widget_set_name (LRG_WIDGET (b), name);
	return b;
}

/* ==========================================================================
 * EditorAssetMetaSource: import settings as an LrgPropertySource
 * ========================================================================== */

#define EDITOR_TYPE_ASSET_META_SOURCE (editor_asset_meta_source_get_type ())
G_DECLARE_FINAL_TYPE (EditorAssetMetaSource, editor_asset_meta_source, EDITOR, ASSET_META_SOURCE, GObject)

struct _EditorAssetMetaSource
{
	GObject               parent_instance;

	LrgEditorAssetsPanel *panel;     /* weak */
	gchar                *path;      /* absolute asset path */
	gchar                *importer;
};

static void editor_asset_meta_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorAssetMetaSource, editor_asset_meta_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE,
                                                      editor_asset_meta_source_iface_init))

/* The live sidecar of the asset (entries are replaced by rescans, so it
 * is looked up again for every access). */
static LrgAssetMeta *
meta_source_meta (EditorAssetMetaSource *self,
                  LrgAssetEntry        **entry_out)
{
	LrgAssetEntry *entry;

	if (self->panel == NULL)
		return NULL;
	entry = lookup_entry (self->panel, self->path);
	if (entry_out != NULL)
		*entry_out = entry;
	return (entry != NULL) ? lrg_asset_entry_get_meta (entry) : NULL;
}

/* Spin ranges for the numeric settings the editor knows (the sidecar
 * validates them again; unknown keys get a wide range). */
static void
setting_range (const gchar *key,
               gdouble     *min,
               gdouble     *max)
{
	if (g_strcmp0 (key, "max-size") == 0)
	{
		*min = 0.0;
		*max = 16384.0;
	}
	else if (g_strcmp0 (key, "size") == 0)
	{
		*min = 1.0;
		*max = 1024.0;
	}
	else if (g_strcmp0 (key, "volume") == 0)
	{
		*min = 0.0;
		*max = 4.0;
	}
	else if (g_strcmp0 (key, "scale") == 0)
	{
		*min = 0.0001;
		*max = 10000.0;
	}
	else
	{
		*min = -1.0e9;
		*max = 1.0e9;
	}
}

/* Labels where "first letter upper-cased" reads wrong. */
static const gchar *
setting_label (const gchar *key)
{
	if (g_strcmp0 (key, "srgb") == 0)
		return "sRGB";
	if (g_strcmp0 (key, "max-size") == 0)
		return "Max size";
	if (g_strcmp0 (key, "codepoints") == 0)
		return "Code points";
	if (g_strcmp0 (key, "up-axis") == 0)
		return "Up axis";
	return NULL;
}

/*
 * meta_source_list:
 *
 * One property per known setting of the importer, typed by its default
 * (b -> boolean, x -> int64, d -> double, s -> string; strings with
 * choices get an "enum-strings:" hint so the grid shows a combo box).
 */
static GPtrArray *
meta_source_list (LrgPropertySource *source)
{
	EditorAssetMetaSource *self = EDITOR_ASSET_META_SOURCE (source);
	GPtrArray             *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	g_auto(GStrv)          keys = lrg_asset_meta_list_known_settings (self->importer);
	guint                  i;

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		g_autoptr(GVariant) def = lrg_asset_meta_get_default_setting (self->importer, keys[i]);
		g_auto(GStrv)       choices = lrg_asset_meta_get_setting_choices (self->importer, keys[i]);
		GParamSpec         *pspec = NULL;
		gdouble             min;
		gdouble             max;

		if (def == NULL)
			continue;
		setting_range (keys[i], &min, &max);
		if (g_variant_is_of_type (def, G_VARIANT_TYPE_BOOLEAN))
			pspec = g_param_spec_boolean (keys[i], NULL, NULL, g_variant_get_boolean (def), G_PARAM_READWRITE);
		else if (g_variant_is_of_type (def, G_VARIANT_TYPE_INT64))
			pspec = g_param_spec_int64 (keys[i], NULL, NULL, (gint64) min, (gint64) max,
			                            CLAMP (g_variant_get_int64 (def), (gint64) min, (gint64) max),
			                            G_PARAM_READWRITE);
		else if (g_variant_is_of_type (def, G_VARIANT_TYPE_DOUBLE))
			pspec = g_param_spec_double (keys[i], NULL, NULL, min, max,
			                             CLAMP (g_variant_get_double (def), min, max), G_PARAM_READWRITE);
		else if (g_variant_is_of_type (def, G_VARIANT_TYPE_STRING))
			pspec = g_param_spec_string (keys[i], NULL, NULL, g_variant_get_string (def, NULL), G_PARAM_READWRITE);
		if (pspec == NULL)
			continue;
		g_param_spec_ref_sink (pspec);
		if (choices != NULL)
		{
			g_autofree gchar *list = g_strjoinv (",", choices);

			g_param_spec_set_qdata_full (pspec, lrg_property_hint_quark (),
			                             g_strconcat ("enum-strings:", list, NULL), g_free);
		}
		{
			LrgPropertyInfo *info = lrg_property_info_new (pspec);
			const gchar     *label = setting_label (keys[i]);

			if (label != NULL)
				lrg_property_info_set_label (info, label);
			g_ptr_array_add (out, info);
		}
		g_param_spec_unref (pspec);
	}
	return out;
}

static gboolean
meta_source_get (LrgPropertySource *source,
                 const gchar       *name,
                 GValue            *value)
{
	EditorAssetMetaSource *self = EDITOR_ASSET_META_SOURCE (source);
	LrgAssetMeta          *meta = meta_source_meta (self, NULL);
	g_autoptr(GVariant)    v = NULL;

	if (meta == NULL)
		return FALSE;
	v = lrg_asset_meta_get_setting_variant (meta, name);
	if (v == NULL)
		return FALSE;
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN))
	{
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, g_variant_get_boolean (v));
	}
	else if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT64))
	{
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, g_variant_get_int64 (v));
	}
	else if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
	{
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, g_variant_get_double (v));
	}
	else if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, g_variant_get_string (v, NULL));
	}
	else
		return FALSE;
	return TRUE;
}

/*
 * meta_source_store:
 *
 * Validates and stores one setting (@v NULL resets it), writes the
 * sidecar and drops the asset's thumbnail so the preview follows the
 * new settings.
 */
static gboolean
meta_source_store (EditorAssetMetaSource *self,
                   const gchar           *name,
                   GVariant              *v)
{
	LrgAssetEntry    *entry = NULL;
	LrgAssetMeta     *meta = meta_source_meta (self, &entry);
	g_autoptr(GError) error = NULL;

	if (meta == NULL)
		return FALSE;
	if (v == NULL)
		lrg_asset_meta_remove_setting (meta, name);
	else if (!lrg_asset_meta_set_setting_variant (meta, name, v, &error))
	{
		g_autofree gchar *msg = g_strdup_printf ("Import setting %s: %s", name, error->message);

		notify_user (self->panel, EDITOR_NOTIFY_WARNING, msg);
		return FALSE;
	}
	if (!lrg_asset_database_save_meta (get_db (self->panel), entry, &error))
	{
		g_autofree gchar *msg = g_strdup_printf ("Cannot save %s.meta: %s", self->path, error->message);

		notify_user (self->panel, EDITOR_NOTIFY_ERROR, msg);
	}
	if (self->panel->app != NULL)
		editor_thumbnails_file_changed (self->panel->app, self->path);
	lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self), name);
	return TRUE;
}

static gboolean
meta_source_set (LrgPropertySource *source,
                 const gchar       *name,
                 const GValue      *value)
{
	EditorAssetMetaSource *self = EDITOR_ASSET_META_SOURCE (source);
	g_autoptr(GVariant)    v = NULL;

	switch (G_VALUE_TYPE (value))
	{
	case G_TYPE_BOOLEAN:
		v = g_variant_ref_sink (g_variant_new_boolean (g_value_get_boolean (value)));
		break;
	case G_TYPE_INT64:
		v = g_variant_ref_sink (g_variant_new_int64 (g_value_get_int64 (value)));
		break;
	case G_TYPE_INT:
		v = g_variant_ref_sink (g_variant_new_int64 (g_value_get_int (value)));
		break;
	case G_TYPE_DOUBLE:
		v = g_variant_ref_sink (g_variant_new_double (g_value_get_double (value)));
		break;
	case G_TYPE_STRING:
		v = g_variant_ref_sink (g_variant_new_string (g_value_get_string (value) != NULL
		                                              ? g_value_get_string (value) : ""));
		break;
	default:
		return FALSE;
	}
	return meta_source_store (self, name, v);
}

static LrgPropertyState
meta_source_state (LrgPropertySource *source,
                   const gchar       *name)
{
	LrgAssetMeta *meta = meta_source_meta (EDITOR_ASSET_META_SOURCE (source), NULL);

	if (meta == NULL)
		return LRG_PROPERTY_STATE_NONE;
	return lrg_asset_meta_has_setting (meta, name) ? LRG_PROPERTY_STATE_NONE : LRG_PROPERTY_STATE_DEFAULT;
}

static gboolean
meta_source_reset (LrgPropertySource *source,
                   const gchar       *name)
{
	return meta_source_store (EDITOR_ASSET_META_SOURCE (source), name, NULL);
}

static void
editor_asset_meta_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = meta_source_list;
	iface->get_value = meta_source_get;
	iface->set_value = meta_source_set;
	iface->get_state = meta_source_state;
	iface->reset = meta_source_reset;
}

static void
editor_asset_meta_source_finalize (GObject *object)
{
	EditorAssetMetaSource *self = EDITOR_ASSET_META_SOURCE (object);

	g_clear_weak_pointer (&self->panel);
	g_free (self->path);
	g_free (self->importer);
	G_OBJECT_CLASS (editor_asset_meta_source_parent_class)->finalize (object);
}

static void
editor_asset_meta_source_class_init (EditorAssetMetaSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_asset_meta_source_finalize;
}

static void
editor_asset_meta_source_init (EditorAssetMetaSource *self)
{
}

/* ==========================================================================
 * Listing
 * ========================================================================== */

static gint
compare_items (gconstpointer a,
               gconstpointer b)
{
	const EditorAssetItem *x = *(EditorAssetItem * const *) a;
	const EditorAssetItem *y = *(EditorAssetItem * const *) b;

	/* Folders first, then natural order */
	if (x->is_dir != y->is_dir)
		return x->is_dir ? -1 : 1;
	return g_utf8_collate (x->name, y->name);
}

/* The git status an item shows (a folder: the aggregate below it). */
static LrgEditorVcsStatus
item_vcs (LrgEditorAssetsPanel *self,
          EditorAssetItem      *item)
{
	if (self->vcs == NULL || !lrg_editor_vcs_get_is_repo (self->vcs))
		return LRG_EDITOR_VCS_STATUS_UNMODIFIED;
	return item->is_dir ? lrg_editor_vcs_get_directory_status (self->vcs, item->path)
	                    : lrg_editor_vcs_get_status (self->vcs, item->path);
}

/*
 * list_dir:
 *
 * The entries of the current folder: hidden files (dot names, the
 * platform's hidden attribute) and .meta sidecars are skipped, folders
 * come first.
 */
static GPtrArray *
list_dir (LrgEditorAssetsPanel *self)
{
	GPtrArray      *out = g_ptr_array_new_with_free_func (g_object_unref);
	g_autoptr(GDir) d = NULL;
	const gchar    *name;

	if (self->dir == NULL || (d = g_dir_open (self->dir, 0, NULL)) == NULL)
		return out;

	while ((name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *path = g_build_filename (self->dir, name, NULL);
		EditorAssetItem  *item;
		GStatBuf          st;

		if (name[0] == '.' || grl_fs_is_file_hidden (path) || lrg_asset_meta_is_meta_path (path))
			continue;
		if (g_stat (path, &st) != 0)
			continue;

		item = g_object_new (EDITOR_TYPE_ASSET_ITEM, NULL);
		item->name = g_strdup (name);
		item->path = g_steal_pointer (&path);
		item->is_dir = S_ISDIR (st.st_mode);
		item->size = st.st_size;
		item->mtime = st.st_mtime;
		item->type = item->is_dir ? LRG_ASSET_TYPE_UNKNOWN : lrg_asset_database_classify (item->path);
		item->vcs = item_vcs (self, item);
		g_ptr_array_add (out, item);
	}
	g_ptr_array_sort (out, compare_items);
	return out;
}

static gboolean
item_visible (LrgEditorAssetsPanel *self,
              EditorAssetItem      *item)
{
	gboolean has_filter = self->filter != NULL && self->filter[0] != '\0';

	if (self->type_filter != LRG_ASSET_TYPE_UNKNOWN && !item->is_dir && item->type != self->type_filter)
		return FALSE;
	if (has_filter && lrg_editor_fuzzy_score (self->filter, item->name) == 0)
		return FALSE;
	return TRUE;
}

static EditorAssetItem *
item_at (LrgEditorAssetsPanel *self,
         guint                 position)
{
	EditorAssetItem *item;

	if (position >= g_list_model_get_n_items (G_LIST_MODEL (self->store)))
		return NULL;
	item = g_list_model_get_item (G_LIST_MODEL (self->store), position);
	g_object_unref (item);   /* the store keeps it alive */
	return item;
}

/* The path of the entry at @position, or NULL. */
static const gchar *
item_path (LrgEditorAssetsPanel *self,
           guint                 position)
{
	EditorAssetItem *item = item_at (self, position);

	return (item != NULL) ? item->path : NULL;
}

static gint
find_position (LrgEditorAssetsPanel *self,
               const gchar          *path)
{
	guint n = g_list_model_get_n_items (G_LIST_MODEL (self->store));
	guint i;

	for (i = 0; path != NULL && i < n; i++)
		if (g_strcmp0 (item_path (self, i), path) == 0)
			return (gint) i;
	return -1;
}

static gboolean
path_selected (LrgEditorAssetsPanel *self,
               const gchar          *path)
{
	guint i;

	for (i = 0; i < self->selected->len; i++)
		if (g_strcmp0 (g_ptr_array_index (self->selected, i), path) == 0)
			return TRUE;
	return FALSE;
}

/*
 * push_selection:
 *
 * Shows self->selected in the active view (after a rebuild, a view
 * switch or a reveal) and scrolls the first selected entry into view.
 */
static void
push_selection (LrgEditorAssetsPanel *self,
                gboolean              scroll)
{
	gint  first = -1;
	guint i;

	self->syncing = TRUE;
	lrg_tile_view_unselect_all (self->grid_view);
	lrg_table_unselect_all (self->table);
	for (i = 0; i < self->selected->len; i++)
	{
		gint p = find_position (self, g_ptr_array_index (self->selected, i));

		if (p < 0)
			continue;
		lrg_tile_view_select_full (self->grid_view, (guint) p,
		                           first < 0 ? LRG_SELECT_MODE_REPLACE : LRG_SELECT_MODE_TOGGLE);
		lrg_table_select (self->table, (guint) p);
		if (first < 0)
			first = p;
	}
	self->syncing = FALSE;

	if (scroll && first >= 0)
	{
		lrg_tile_view_scroll_to (self->grid_view, (guint) first, FALSE);
		lrg_table_scroll_to (self->table, (guint) first, FALSE);
	}
}

/* Rebuilds the shown list from self->all with the filters applied. */
static void
rebuild_view (LrgEditorAssetsPanel *self)
{
	g_autoptr(GPtrArray) shown = g_ptr_array_new ();
	g_autoptr(GPtrArray) keep = g_ptr_array_new_with_free_func (g_free);
	guint                i;

	for (i = 0; i < self->all->len; i++)
	{
		EditorAssetItem *item = g_ptr_array_index (self->all, i);

		if (item_visible (self, item))
			g_ptr_array_add (shown, item);
	}

	self->syncing = TRUE;
	g_list_store_splice (self->store, 0, g_list_model_get_n_items (G_LIST_MODEL (self->store)),
	                     shown->pdata, shown->len);
	self->syncing = FALSE;

	/* Carry the selection over by path (entries that are gone or
	 * filtered out drop out of it); a pending selection wins */
	if (self->pending_select != NULL)
	{
		g_ptr_array_set_size (self->selected, 0);
		g_ptr_array_add (self->selected, g_steal_pointer (&self->pending_select));
	}
	for (i = 0; i < self->selected->len; i++)
		if (find_position (self, g_ptr_array_index (self->selected, i)) >= 0)
			g_ptr_array_add (keep, g_strdup (g_ptr_array_index (self->selected, i)));
	g_ptr_array_set_size (self->selected, 0);
	for (i = 0; i < keep->len; i++)
		g_ptr_array_add (self->selected, g_strdup (g_ptr_array_index (keep, i)));
	push_selection (self, TRUE);
	update_context_menu (self, self->selected->len == 0);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* Lists the folder again (keeps the selection by path). */
static void
refresh_listing (LrgEditorAssetsPanel *self)
{
	self->dirty = FALSE;
	g_clear_pointer (&self->all, g_ptr_array_unref);
	self->all = list_dir (self);
	if (self->renaming != NULL && !g_file_test (self->renaming, G_FILE_TEST_EXISTS))
		cancel_rename (self);
	rebuild_view (self);
}

/* LrgEditorVcs::changed: recompute every badge (tiles repaint from the
 * items, no rebinding needed). */
static void
on_vcs_changed (LrgEditorVcs *vcs,
                gpointer      user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);
	guint                 i;

	for (i = 0; self->all != NULL && i < self->all->len; i++)
	{
		EditorAssetItem *item = g_ptr_array_index (self->all, i);

		item->vcs = item_vcs (self, item);
	}
}

static void
on_dir_changed (GFileMonitor      *monitor,
                GFile             *file,
                GFile             *other,
                GFileMonitorEvent  event,
                gpointer           user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);

	if (event == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED)
		return;
	self->dirty = TRUE;
}

/* ==========================================================================
 * Navigation
 * ========================================================================== */

static void
rebuild_breadcrumb (LrgEditorAssetsPanel *self)
{
	g_autofree gchar *rel = NULL;
	g_autofree gchar *acc = NULL;
	g_auto(GStrv)     parts = NULL;
	guint             i;

	lrg_breadcrumb_remove_all (self->crumb);
	if (self->root == NULL || self->dir == NULL)
		return;

	{
		g_autofree gchar *label = g_path_get_basename (self->root);

		lrg_breadcrumb_append_with_data (self->crumb, label, g_strdup (self->root), g_free);
	}
	rel = rel_path (self, self->dir);
	acc = g_strdup (self->root);
	parts = g_strsplit (rel, G_DIR_SEPARATOR_S, -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		gchar *next;

		if (parts[i][0] == '\0')
			continue;
		next = g_build_filename (acc, parts[i], NULL);
		g_free (acc);
		acc = next;
		lrg_breadcrumb_append_with_data (self->crumb, parts[i], g_strdup (acc), g_free);
	}
}

/*
 * go_to:
 *
 * Shows @dir (absolute, canonical, inside the root): a new monitor, a
 * new breadcrumb, an empty selection and a fresh listing.
 */
static gboolean
go_to (LrgEditorAssetsPanel *self,
       const gchar          *dir)
{
	g_autoptr(GFile) file = NULL;

	if (dir == NULL || self->root == NULL || !path_is_under (dir, self->root) ||
	    !g_file_test (dir, G_FILE_TEST_IS_DIR))
		return FALSE;

	cancel_rename (self);
	if (g_strcmp0 (dir, self->dir) != 0)
	{
		g_free (self->dir);
		self->dir = g_strdup (dir);
		g_ptr_array_set_size (self->selected, 0);

		if (self->monitor != NULL)
		{
			g_file_monitor_cancel (self->monitor);
			g_signal_handlers_disconnect_by_data (self->monitor, self);
			g_clear_object (&self->monitor);
		}
		file = g_file_new_for_path (self->dir);
		self->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
		if (self->monitor != NULL)
			g_signal_connect (self->monitor, "changed", G_CALLBACK (on_dir_changed), self);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTORY]);
	}
	rebuild_breadcrumb (self);
	refresh_listing (self);
	return TRUE;
}

static void
go_up (LrgEditorAssetsPanel *self)
{
	g_autofree gchar *up = NULL;

	if (self->dir == NULL || g_strcmp0 (self->dir, self->root) == 0)
		return;
	up = g_path_get_dirname (self->dir);
	go_to (self, up);
	/* Select the folder we came from */
}

/*
 * sync_app:
 *
 * Follows the app: the root is the project directory (or, without a
 * project, the open level's folder). When it changes the panel shows
 * the new root; when the app's LrgEditorVcs object changes the panel
 * reconnects to its ::changed signal.
 */
static void
sync_app (LrgEditorAssetsPanel *self)
{
	const gchar      *base;
	g_autofree gchar *root = NULL;
	LrgEditorVcs     *vcs;

	if (self->app == NULL)
		return;

	base = editor_app_base_dir (self->app);
	root = (base != NULL) ? g_canonicalize_filename (base, NULL) : NULL;
	vcs = self->app->vcs;

	if (vcs != self->vcs)
	{
		if (self->vcs != NULL)
			g_signal_handler_disconnect (self->vcs, self->vcs_handler);
		self->vcs_handler = 0;
		g_set_object (&self->vcs, vcs);
		if (self->vcs != NULL)
			self->vcs_handler = g_signal_connect (self->vcs, "changed", G_CALLBACK (on_vcs_changed), self);
		on_vcs_changed (self->vcs, self);
	}

	if (g_strcmp0 (root, self->root) != 0)
	{
		g_free (self->root);
		self->root = g_steal_pointer (&root);
		g_clear_object (&self->own_db);
		g_clear_pointer (&self->dir, g_free);
		if (self->root != NULL)
			go_to (self, self->root);
		else
		{
			rebuild_breadcrumb (self);
			refresh_listing (self);
		}
	}
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

/* The active view's selection changed: copy it into self->selected. */
static void
on_view_selection_changed (LrgWidget *view,
                           gpointer   user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);
	GPtrArray            *now;
	guint                 n = g_list_model_get_n_items (G_LIST_MODEL (self->store));
	guint                 i;

	if (self->syncing || (self->grid != (view == LRG_WIDGET (self->grid_view))))
		return;

	now = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < n; i++)
	{
		gboolean on = self->grid ? lrg_tile_view_is_selected (self->grid_view, i)
		                         : lrg_table_is_selected (self->table, i);

		if (on)
			g_ptr_array_add (now, g_strdup (item_path (self, i)));
	}
	g_ptr_array_unref (self->prev_selected);
	self->prev_selected = self->selected;
	self->selected = now;
	update_context_menu (self, self->selected->len == 0);
}

/* ==========================================================================
 * Opening
 * ========================================================================== */

/*
 * open_in_code_panel:
 *
 * The internal code editor belongs to the code panel; it is reached by
 * name only, so this file does not depend on it: a "code.open" GAction
 * with a string parameter anywhere up the widget tree, else the
 * "code.open" IPC method ({"path": ...}).
 *
 * Returns: %TRUE if the code panel took the file
 */
static gboolean
open_in_code_panel (LrgEditorAssetsPanel *self,
                    const gchar          *path)
{
	GAction          *action = lrg_widget_lookup_action (LRG_WIDGET (self), "code.open");
	LrgEditorServer  *server;
	GList            *methods;
	gboolean          has_method;
	g_autoptr(JsonBuilder) b = NULL;
	g_autoptr(JsonGenerator) gen = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar *line = NULL;
	g_autofree gchar *reply = NULL;

	if (action != NULL && g_action_get_enabled (action) && g_action_get_parameter_type (action) != NULL &&
	    g_variant_type_equal (g_action_get_parameter_type (action), G_VARIANT_TYPE_STRING))
		return lrg_widget_activate_action (LRG_WIDGET (self), "code.open", g_variant_new_string (path));

	if (self->app == NULL)
		return FALSE;
	server = self->app->server;
	methods = lrg_editor_server_list_methods (server);
	has_method = g_list_find_custom (methods, "code.open", (GCompareFunc) g_strcmp0) != NULL;
	g_list_free (methods);
	if (!has_method)
		return FALSE;

	b = json_builder_new ();
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "id");
	json_builder_add_int_value (b, 1);
	json_builder_set_member_name (b, "method");
	json_builder_add_string_value (b, "code.open");
	json_builder_set_member_name (b, "params");
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "path");
	json_builder_add_string_value (b, path);
	json_builder_end_object (b);
	json_builder_end_object (b);
	root = json_builder_get_root (b);
	gen = json_generator_new ();
	json_generator_set_root (gen, root);
	line = json_generator_to_data (gen, NULL);
	reply = lrg_editor_server_dispatch (server, line);
	return reply != NULL && strstr (reply, "\"error\"") == NULL;
}

/*
 * open_entry:
 *
 * Double click / Enter: folders are entered, scripts go to the internal
 * code editor when Preferences > script-editor is "internal" (falling
 * back to the external editor), everything else follows
 * editor_app_open_asset() (levels and prefabs in the editor, media in
 * the desktop application, the rest in the external editor).
 */
static void
open_entry (LrgEditorAssetsPanel *self,
            const gchar          *path)
{
	const gchar *mode;

	if (self->app == NULL || path == NULL)
		return;
	if (g_file_test (path, G_FILE_TEST_IS_DIR))
	{
		go_to (self, path);
		return;
	}
	/* data formats (input maps, locales, themes, ...) open in their editor;
	 * a file that does not parse falls through to the usual handling */
	if (lrg_editor_data_editor_can_open (path) && lrg_editor_data_editor_open (self->app, path, NULL) != NULL)
		return;
	mode = lrg_editor_settings_get_script_editor (self->app->settings);
	if (lrg_asset_database_classify (path) == LRG_ASSET_TYPE_SCRIPT && g_strcmp0 (mode, "internal") == 0 &&
	    open_in_code_panel (self, path))
		return;
	editor_app_open_asset (self->app, path);
}

static void
on_tile_activated (LrgTileView *view,
                   guint        position,
                   gpointer     user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);
	EditorAssetItem      *item = item_at (self, position);

	if (item != NULL)
	{
		g_autofree gchar *path = g_strdup (item->path);

		open_entry (self, path);
	}
}

static void
on_row_activated (LrgTable *table,
                  guint     position,
                  gpointer  user_data)
{
	on_tile_activated (NULL, position, user_data);
}

/* ==========================================================================
 * Operations
 * ========================================================================== */

/* Keeps the open level's path in step with a rename or move. */
static void
follow_open_level (LrgEditorAssetsPanel *self,
                   const gchar          *from,
                   const gchar          *to)
{
	const gchar *open;

	if (self->app == NULL)
		return;
	open = lrg_editor_get_path (self->app->editor);
	if (open == NULL)
		return;
	if (g_strcmp0 (open, from) == 0)
		lrg_editor_set_path (self->app->editor, to);
	else if (path_is_under (open, from))
	{
		g_autofree gchar *moved = g_strconcat (to, open + strlen (from), NULL);

		lrg_editor_set_path (self->app->editor, moved);
	}
}

/*
 * do_rename:
 *
 * Renames @path to @new_name in its folder through the asset database
 * (the .meta sidecar and the GUID follow).
 *
 * Returns: (transfer full) (nullable): the new absolute path
 */
static gchar *
do_rename (LrgEditorAssetsPanel  *self,
           const gchar           *path,
           const gchar           *new_name,
           GError               **error)
{
	LrgAssetDatabase *db = get_db (self);
	g_autofree gchar *dir = g_path_get_dirname (path);
	gchar            *target;

	if (db == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No project is open");
		return NULL;
	}
	if (!lrg_asset_database_rename (db, path, new_name, error))
		return NULL;
	target = g_build_filename (dir, new_name, NULL);
	follow_open_level (self, path, target);
	g_free (self->pending_select);
	self->pending_select = g_strdup (target);
	after_file_op (self);
	return target;
}

static gchar *
do_move (LrgEditorAssetsPanel  *self,
         const gchar           *path,
         const gchar           *dest_dir,
         GError               **error)
{
	LrgAssetDatabase *db = get_db (self);
	g_autofree gchar *base = g_path_get_basename (path);
	gchar            *target;

	if (db == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No project is open");
		return NULL;
	}
	if (path_is_under (dest_dir, path))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot move %s into itself", base);
		return NULL;
	}
	if (!lrg_asset_database_move (db, path, dest_dir, error))
		return NULL;
	target = g_build_filename (dest_dir, base, NULL);
	follow_open_level (self, path, target);
	after_file_op (self);
	return target;
}

static gchar *
do_duplicate (LrgEditorAssetsPanel  *self,
              const gchar           *path,
              GError               **error)
{
	LrgAssetDatabase *db = get_db (self);
	LrgAssetEntry    *entry;

	if (db == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No project is open");
		return NULL;
	}
	if (lookup_entry (self, path) == NULL && !g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s is not a file", path);
		return NULL;
	}
	entry = lrg_asset_database_duplicate (db, path, error);
	if (entry == NULL)
		return NULL;
	g_free (self->pending_select);
	self->pending_select = g_strdup (lrg_asset_entry_get_full_path (entry));
	after_file_op (self);
	return g_strdup (lrg_asset_entry_get_full_path (entry));
}

static gboolean
do_trash (LrgEditorAssetsPanel  *self,
          const gchar           *path,
          GError               **error)
{
	LrgAssetDatabase *db = get_db (self);

	if (db == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No project is open");
		return FALSE;
	}
	if (!lrg_asset_database_trash (db, path, error))
		return FALSE;
	after_file_op (self);
	return TRUE;
}

/* The trash confirmation answered. */
static void
on_trash_response (LrgDialog *dialog,
                   gint       response,
                   gpointer   user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);
	g_autoptr(GPtrArray)  paths = g_steal_pointer (&self->trash_paths);
	guint                 done = 0;
	guint                 i;

	if (response != 1 || paths == NULL)
		return;
	for (i = 0; i < paths->len; i++)
	{
		g_autoptr(GError) error = NULL;
		const gchar      *p = g_ptr_array_index (paths, i);

		if (do_trash (self, p, &error))
			done++;
		else
		{
			g_autofree gchar *msg = g_strdup_printf ("Could not move %s to the trash: %s", p, error->message);

			notify_user (self, EDITOR_NOTIFY_ERROR, msg);
		}
	}
	if (done > 0)
	{
		g_autofree gchar *msg = (done == 1) ? g_strdup ("Moved 1 item to the trash")
		                                    : g_strdup_printf ("Moved %u items to the trash", done);

		notify_user (self, EDITOR_NOTIFY_INFO, msg);
	}
}

/*
 * confirm_trash:
 *
 * Asks before moving @paths to the trash (Delete key, context menu).
 * Without a canvas (headless) it trashes right away.
 */
static void
confirm_trash (LrgEditorAssetsPanel *self,
               GPtrArray            *paths)
{
	LrgWidget        *root = lrg_widget_get_root (LRG_WIDGET (self));
	g_autoptr(GString) body = g_string_new (NULL);
	LrgDialog        *dialog;
	LrgButton        *ok;
	guint             i;

	if (paths == NULL || paths->len == 0)
		return;
	g_clear_pointer (&self->trash_paths, g_ptr_array_unref);
	self->trash_paths = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < paths->len; i++)
		g_ptr_array_add (self->trash_paths, g_strdup (g_ptr_array_index (paths, i)));

	if (!LRG_IS_CANVAS (root))
	{
		on_trash_response (NULL, 1, self);
		return;
	}

	for (i = 0; i < paths->len && i < 6; i++)
	{
		g_autofree gchar *base = g_path_get_basename (g_ptr_array_index (paths, i));

		g_string_append_printf (body, "%s%s", i > 0 ? "\n" : "", base);
	}
	if (paths->len > 6)
		g_string_append_printf (body, "\n... and %u more", paths->len - 6);
	g_string_append (body, "\n\nSidecar .meta files go with them. The desktop trash can restore them.");

	dialog = lrg_dialog_new (paths->len == 1 ? "Move to Trash?" : "Move these items to the Trash?", body->str);
	lrg_widget_set_name (LRG_WIDGET (dialog), "assets-trash-dialog");
	lrg_dialog_add_button (dialog, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	ok = lrg_dialog_add_button (dialog, "Move to Trash", 1, TRUE);
	lrg_widget_add_class (LRG_WIDGET (ok), "danger");
	g_signal_connect_object (dialog, "response", G_CALLBACK (on_trash_response), self, 0);
	g_set_weak_pointer (&self->trash_dialog, dialog);
	lrg_dialog_present (dialog, LRG_CANVAS (root));
	g_object_unref (dialog);
}

/* Creates "New Folder" (or "New Folder N") in the folder and renames it. */
static void
new_folder (LrgEditorAssetsPanel *self)
{
	guint n;

	if (self->dir == NULL)
		return;
	for (n = 1; n < 1000; n++)
	{
		g_autofree gchar *name = (n == 1) ? g_strdup ("New Folder") : g_strdup_printf ("New Folder %u", n);
		g_autofree gchar *path = g_build_filename (self->dir, name, NULL);

		if (g_file_test (path, G_FILE_TEST_EXISTS))
			continue;
		if (g_mkdir (path, 0755) != 0)
		{
			g_autofree gchar *msg = g_strdup_printf ("Cannot create a folder: %s", g_strerror (errno));

			notify_user (self, EDITOR_NOTIFY_ERROR, msg);
			return;
		}
		refresh_listing (self);
		lrg_editor_assets_panel_begin_rename (self, path);
		return;
	}
}

/* Creates an empty level in the folder (as 0.8.0) and renames it. */
static void
new_level (LrgEditorAssetsPanel *self)
{
	guint n;

	if (self->dir == NULL)
		return;
	for (n = 1; n < 1000; n++)
	{
		g_autofree gchar *name = (n == 1) ? g_strdup ("new_level.rlevel")
		                                  : g_strdup_printf ("new_level%u.rlevel", n);
		g_autofree gchar *path = g_build_filename (self->dir, name, NULL);
		g_autoptr(GError) error = NULL;

		if (g_file_test (path, G_FILE_TEST_EXISTS))
			continue;
		if (!g_file_set_contents (path, "type: level\nname: new_level\ndefault_view: 3d\nnodes: []\n", -1, &error))
		{
			g_autofree gchar *msg = g_strdup_printf ("Cannot create a level: %s", error->message);

			notify_user (self, EDITOR_NOTIFY_ERROR, msg);
			return;
		}
		refresh_listing (self);
		lrg_editor_assets_panel_begin_rename (self, path);
		return;
	}
}

/* Opens the desktop file manager on a folder. */
static void
show_in_file_manager (LrgEditorAssetsPanel *self,
                      const gchar          *path)
{
	g_autofree gchar *dir = g_file_test (path, G_FILE_TEST_IS_DIR) ? g_strdup (path) : g_path_get_dirname (path);
	g_autofree gchar *uri = g_filename_to_uri (dir, NULL, NULL);
	g_autoptr(GError) error = NULL;

	if (uri == NULL || !g_app_info_launch_default_for_uri (uri, NULL, &error))
	{
		g_autofree gchar *msg = g_strdup_printf ("Cannot open the file manager%s%s",
		                                         error != NULL ? ": " : "", error != NULL ? error->message : "");

		notify_user (self, EDITOR_NOTIFY_ERROR, msg);
	}
}

/*
 * place_in_scene:
 *
 * Context menu "Place in Scene": editor_app_place_asset() (shared with
 * the Sources panel) plus the panel's status line. Files that cannot be
 * placed are ignored quietly (the menu offers the item for every file).
 */
static void
place_in_scene (LrgEditorAssetsPanel *self,
                const gchar          *path)
{
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *stem = g_path_get_basename (path);
	LrgNode           *node;
	gchar             *dot = strrchr (stem, '.');

	if (self->app == NULL)
		return;
	if (dot != NULL)
		*dot = '\0';
	node = editor_app_place_asset (self->app, path, &error);
	if (node != NULL)
	{
		g_autofree gchar *msg = g_strdup_printf ("Placed %s", stem);

		set_status (self, msg);
	}
	else if (error != NULL && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
	{
		g_autofree gchar *msg = g_strdup_printf ("%s: %s", stem, error->message);

		notify_user (self, EDITOR_NOTIFY_ERROR, msg);
	}
}

/* Trashes the orphaned sidecars of the last scan. */
static guint
trash_orphans (LrgEditorAssetsPanel *self,
               GPtrArray            *failed)
{
	LrgAssetDatabase    *db = get_db (self);
	GPtrArray           *orphans;
	g_autoptr(GPtrArray) copy = g_ptr_array_new_with_free_func (g_free);
	guint                done = 0;
	guint                i;

	if (db == NULL)
		return 0;
	orphans = lrg_asset_database_get_orphaned_metas (db);
	for (i = 0; orphans != NULL && i < orphans->len; i++)
		g_ptr_array_add (copy, g_strdup (g_ptr_array_index (orphans, i)));
	for (i = 0; i < copy->len; i++)
	{
		g_autoptr(GFile)  file = g_file_new_for_path (g_ptr_array_index (copy, i));
		g_autoptr(GError) error = NULL;

		if (g_file_trash (file, NULL, &error))
			done++;
		else if (failed != NULL)
			g_ptr_array_add (failed, g_strdup (g_ptr_array_index (copy, i)));
	}
	if (done > 0)
		lrg_asset_database_scan (db, NULL);
	return done;
}

/* ==========================================================================
 * Inline rename
 * ========================================================================== */

static void
cancel_rename (LrgEditorAssetsPanel *self)
{
	if (self->renaming == NULL)
		return;
	g_clear_pointer (&self->renaming, g_free);
	self->rename_focused = FALSE;
	lrg_widget_set_visible (LRG_WIDGET (self->rename), FALSE);
}

static void
on_rename_submitted (LrgTextInput *input,
                     gpointer      user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);
	g_autofree gchar     *path = g_strdup (self->renaming);
	g_autofree gchar     *base = NULL;
	g_autofree gchar     *name = NULL;
	g_autofree gchar     *done = NULL;
	g_autoptr(GError)     error = NULL;
	LrgWidget            *root;

	if (path == NULL)
		return;
	name = g_strstrip (g_strdup (lrg_text_input_get_text (input)));
	base = g_path_get_basename (path);
	cancel_rename (self);

	/* Back to the view, so the keyboard keeps working there */
	root = lrg_widget_get_root (LRG_WIDGET (self));
	if (LRG_IS_CANVAS (root))
		lrg_canvas_set_focused_widget (LRG_CANVAS (root), self->grid ? LRG_WIDGET (self->grid_view)
		                                                             : LRG_WIDGET (self->table));

	if (name[0] == '\0' || g_strcmp0 (name, base) == 0)
		return;
	done = do_rename (self, path, name, &error);
	if (done == NULL)
	{
		g_autofree gchar *msg = g_strdup_printf ("Rename failed: %s", error->message);

		notify_user (self, EDITOR_NOTIFY_WARNING, msg);
	}
	else
	{
		g_autofree gchar *msg = g_strdup_printf ("Renamed %s to %s", base, name);

		set_status (self, msg);
	}
}

/*
 * place_rename:
 *
 * Puts the rename field over the name of the entry being renamed: the
 * label strip of a grid tile, or the name cell of a list row. Called
 * every frame while renaming (the views scroll on their own).
 */
static void
place_rename (LrgEditorAssetsPanel *self)
{
	gint   p;
	gfloat px = lrg_widget_get_world_x (LRG_WIDGET (self));
	gfloat py = lrg_widget_get_world_y (LRG_WIDGET (self));
	gfloat ch = metric_of (self, LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat x = 0.0f;
	gfloat y = 0.0f;
	gfloat w = 0.0f;
	gfloat h = 0.0f;

	if (self->renaming == NULL)
		return;
	p = find_position (self, self->renaming);
	if (p < 0)
	{
		cancel_rename (self);
		return;
	}
	if (self->grid)
	{
		if (!lrg_tile_view_get_tile_rect (self->grid_view, (guint) p, &x, &y, &w, &h))
			return;
		y = y + h - ch - 2.0f;
		x -= 4.0f;
		w += 8.0f;
	}
	else
	{
		LrgWidget *cell = lrg_table_get_cell_widget (self->table, (guint) p, COL_NAME);

		if (cell == NULL)
			return;
		x = lrg_widget_get_world_x (cell) + 20.0f;
		w = MAX (80.0f, lrg_widget_get_width (cell) - 20.0f);
		y = lrg_widget_get_world_y (cell) + (lrg_widget_get_height (cell) - ch) * 0.5f;
	}
	lrg_widget_place_in_slot (LRG_WIDGET (self->rename), x - px, y - py, w, ch);
}

/* ==========================================================================
 * Context menus and actions
 * ========================================================================== */

/* The paths an entry action acts on (the context set, else the selection). */
static GPtrArray *
action_targets (LrgEditorAssetsPanel *self)
{
	GPtrArray *src = (self->context != NULL && self->context->len > 0) ? self->context : self->selected;
	GPtrArray *out = g_ptr_array_new_with_free_func (g_free);
	guint      i;

	for (i = 0; i < src->len; i++)
		g_ptr_array_add (out, g_strdup (g_ptr_array_index (src, i)));
	return out;
}

static const gchar *
first_target (LrgEditorAssetsPanel *self)
{
	GPtrArray *src = (self->context != NULL && self->context->len > 0) ? self->context : self->selected;

	return (src->len > 0) ? g_ptr_array_index (src, 0) : NULL;
}

static void
act_open (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	g_autoptr(GPtrArray)  t = action_targets (self);
	guint                 i;

	/* One folder at most can be entered; files all open */
	for (i = 0; i < t->len; i++)
	{
		open_entry (self, g_ptr_array_index (t, i));
		if (g_file_test (g_ptr_array_index (t, i), G_FILE_TEST_IS_DIR))
			break;
	}
}

static void
act_open_external (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	g_autoptr(GPtrArray)  t = action_targets (self);
	guint                 i;

	for (i = 0; i < t->len && self->app != NULL; i++)
		if (!g_file_test (g_ptr_array_index (t, i), G_FILE_TEST_IS_DIR))
			editor_app_open_file (self->app, g_ptr_array_index (t, i), 1, 1);
}

static void
act_place (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	g_autoptr(GPtrArray)  t = action_targets (self);
	guint                 i;

	for (i = 0; i < t->len; i++)
		place_in_scene (self, g_ptr_array_index (t, i));
}

static void
act_import_settings (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	const gchar          *path = first_target (self);

	if (path != NULL)
		lrg_editor_assets_panel_show_import_settings (self, path);
}

static void
act_rename (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	const gchar          *path = first_target (self);

	if (path != NULL)
	{
		g_autofree gchar *copy = g_strdup (path);

		lrg_editor_assets_panel_begin_rename (self, copy);
	}
}

static void
act_duplicate (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	g_autoptr(GPtrArray)  t = action_targets (self);
	guint                 i;

	for (i = 0; i < t->len; i++)
	{
		g_autoptr(GError) error = NULL;
		g_autofree gchar *done = NULL;

		if (g_file_test (g_ptr_array_index (t, i), G_FILE_TEST_IS_DIR))
			continue;
		done = do_duplicate (self, g_ptr_array_index (t, i), &error);
		if (done == NULL)
		{
			g_autofree gchar *msg = g_strdup_printf ("Duplicate failed: %s", error->message);

			notify_user (self, EDITOR_NOTIFY_ERROR, msg);
		}
	}
}

static void
act_trash (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	g_autoptr(GPtrArray)  t = action_targets (self);

	confirm_trash (self, t);
}

static void
copy_text (LrgEditorAssetsPanel *self,
           const gchar          *text,
           const gchar          *what)
{
	g_autofree gchar *msg = g_strdup_printf ("Copied %s: %s", what, text);

	if (self->app == NULL)
		return;
	editor_app_set_clipboard (self->app, text);
	set_status (self, msg);
}

static void
act_copy_path (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	g_autoptr(GPtrArray)  t = action_targets (self);
	g_autoptr(GString)    text = g_string_new (NULL);
	guint                 i;

	for (i = 0; i < t->len; i++)
	{
		g_autofree gchar *rel = rel_path (self, g_ptr_array_index (t, i));

		g_string_append_printf (text, "%s%s", i > 0 ? "\n" : "", rel);
	}
	if (t->len > 0)
		copy_text (self, text->str, "path");
}

static void
act_copy_guid (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	g_autoptr(GPtrArray)  t = action_targets (self);
	g_autoptr(GString)    text = g_string_new (NULL);
	guint                 i;

	for (i = 0; i < t->len; i++)
	{
		LrgAssetEntry *entry = lookup_entry (self, g_ptr_array_index (t, i));

		if (entry == NULL || lrg_asset_entry_get_guid (entry) == NULL)
			continue;
		g_string_append_printf (text, "%s%s", text->len > 0 ? "\n" : "", lrg_asset_entry_get_guid (entry));
	}
	if (text->len > 0)
		copy_text (self, text->str, "GUID");
	else
		set_status (self, "No GUID: folders and files outside the project have none");
}

static void
act_show_in_file_manager (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	const gchar          *path = first_target (self);

	show_in_file_manager (self, path != NULL ? path : self->dir);
}

static void
act_new_folder (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	new_folder (user_data);
}

static void
act_new_level (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	new_level (user_data);
}

static void
act_new_script (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;

	if (self->app != NULL)
		editor_dialog_new_script (self->app, NULL);
}

static void
act_refresh (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	lrg_editor_assets_panel_refresh (user_data);
}

static void
act_clean_orphans (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;
	g_autoptr(GPtrArray)  failed = g_ptr_array_new_with_free_func (g_free);
	guint                 n = trash_orphans (self, failed);
	g_autofree gchar     *msg = NULL;

	if (failed->len > 0)
		msg = g_strdup_printf ("Moved %u orphaned sidecar(s) to the trash, %u failed", n, failed->len);
	else
		msg = g_strdup_printf ("Moved %u orphaned sidecar(s) to the trash", n);
	notify_user (self, failed->len > 0 ? EDITOR_NOTIFY_WARNING : EDITOR_NOTIFY_INFO, msg);
}

static void
act_grid_view (GSimpleAction *a, GVariant *p, gpointer user_data)
{
	LrgEditorAssetsPanel *self = user_data;

	lrg_editor_assets_panel_set_grid_view (self, !self->grid);
}

static const GActionEntry action_entries[] = {
	{ "open", act_open, NULL, NULL, NULL, { 0 } },
	{ "open-external", act_open_external, NULL, NULL, NULL, { 0 } },
	{ "place", act_place, NULL, NULL, NULL, { 0 } },
	{ "import-settings", act_import_settings, NULL, NULL, NULL, { 0 } },
	{ "rename", act_rename, NULL, NULL, NULL, { 0 } },
	{ "duplicate", act_duplicate, NULL, NULL, NULL, { 0 } },
	{ "trash", act_trash, NULL, NULL, NULL, { 0 } },
	{ "copy-path", act_copy_path, NULL, NULL, NULL, { 0 } },
	{ "copy-guid", act_copy_guid, NULL, NULL, NULL, { 0 } },
	{ "show-in-file-manager", act_show_in_file_manager, NULL, NULL, NULL, { 0 } },
	{ "new-folder", act_new_folder, NULL, NULL, NULL, { 0 } },
	{ "new-level", act_new_level, NULL, NULL, NULL, { 0 } },
	{ "new-script", act_new_script, NULL, NULL, NULL, { 0 } },
	{ "refresh", act_refresh, NULL, NULL, NULL, { 0 } },
	{ "clean-orphans", act_clean_orphans, NULL, NULL, NULL, { 0 } },
	{ "grid-view", act_grid_view, NULL, "false", NULL, { 0 } },
};

static void
menu_add (GMenu       *section,
          const gchar *label,
          const gchar *action,
          const gchar *accel)
{
	g_autoptr(GMenuItem) item = g_menu_item_new (label, action);

	if (accel != NULL)
		g_menu_item_set_attribute (item, "accel", "s", accel);
	g_menu_append_item (section, item);
}

/* Does any target have import settings? */
static gboolean
targets_have_settings (LrgEditorAssetsPanel *self)
{
	const gchar   *path = first_target (self);
	g_auto(GStrv)  keys = NULL;

	if (path == NULL || g_file_test (path, G_FILE_TEST_IS_DIR))
		return FALSE;
	keys = lrg_asset_meta_list_known_settings (
	           lrg_asset_meta_importer_for_type (lrg_asset_database_classify (path)));
	return keys != NULL && keys[0] != NULL;
}

/* The entry menu: every 0.8.0 entry, plus Copy GUID and Import Settings. */
static GMenuModel *
build_entry_menu (LrgEditorAssetsPanel *self)
{
	GMenu            *menu = g_menu_new ();
	g_autoptr(GMenu)  open = g_menu_new ();
	g_autoptr(GMenu)  edit = g_menu_new ();
	g_autoptr(GMenu)  copy = g_menu_new ();
	const gchar      *first = first_target (self);
	gboolean          is_dir = first != NULL && g_file_test (first, G_FILE_TEST_IS_DIR);
	LrgAssetType      type = (first != NULL && !is_dir) ? lrg_asset_database_classify (first) : LRG_ASSET_TYPE_UNKNOWN;

	menu_add (open, is_dir ? "Open Folder" : "Open", "assets.open", "Enter");
	if (!is_dir)
		menu_add (open, "Open in External Editor", "assets.open-external", NULL);
	if (type == LRG_ASSET_TYPE_PREFAB || type == LRG_ASSET_TYPE_MODEL || type == LRG_ASSET_TYPE_TEXTURE ||
	    type == LRG_ASSET_TYPE_CAD)
		menu_add (open, "Place in Scene", "assets.place", NULL);
	if (targets_have_settings (self))
		menu_add (open, "Import Settings\xe2\x80\xa6", "assets.import-settings", NULL);

	menu_add (edit, "Rename", "assets.rename", "F2");
	if (!is_dir)
		menu_add (edit, "Duplicate", "assets.duplicate", NULL);
	menu_add (edit, "Move to Trash", "assets.trash", "Delete");

	menu_add (copy, "Copy Path", "assets.copy-path", NULL);
	if (!is_dir)
		menu_add (copy, "Copy GUID", "assets.copy-guid", NULL);
	menu_add (copy, "Show in File Manager", "assets.show-in-file-manager", NULL);

	g_menu_append_section (menu, NULL, G_MENU_MODEL (open));
	g_menu_append_section (menu, NULL, G_MENU_MODEL (edit));
	g_menu_append_section (menu, NULL, G_MENU_MODEL (copy));
	return G_MENU_MODEL (menu);
}

/* The background menu (empty space, the "+" button). */
static GMenuModel *
build_background_menu (LrgEditorAssetsPanel *self)
{
	GMenu            *menu = g_menu_new ();
	g_autoptr(GMenu)  create = g_menu_new ();
	g_autoptr(GMenu)  view = g_menu_new ();
	LrgAssetDatabase *db = (self->root != NULL) ? get_db (self) : NULL;
	GPtrArray        *orphans = (db != NULL) ? lrg_asset_database_get_orphaned_metas (db) : NULL;

	menu_add (create, "New Folder", "assets.new-folder", NULL);
	menu_add (create, "New Level", "assets.new-level", NULL);
	menu_add (create, "New Script\xe2\x80\xa6", "assets.new-script", NULL);

	menu_add (view, "Refresh", "assets.refresh", NULL);
	menu_add (view, "Show in File Manager", "assets.show-in-file-manager", NULL);
	menu_add (view, "Grid View", "assets.grid-view", NULL);
	if (orphans != NULL && orphans->len > 0)
	{
		g_autofree gchar *label = g_strdup_printf ("Remove Orphaned Sidecars (%u)", orphans->len);

		menu_add (view, label, "assets.clean-orphans", NULL);
	}

	g_menu_append_section (menu, NULL, G_MENU_MODEL (create));
	g_menu_append_section (menu, NULL, G_MENU_MODEL (view));
	return G_MENU_MODEL (menu);
}

/*
 * update_context_menu:
 *
 * Points the panel's context menu (LrgWidget:context-menu, opened by
 * the canvas on an unhandled right press or Shift+F10) at the entry
 * menu for the selection, or at the background menu.
 */
static void
update_context_menu (LrgEditorAssetsPanel *self,
                     gboolean              background)
{
	g_autoptr(GMenuModel) model = NULL;

	if (background)
	{
		if (self->context != NULL)
			g_ptr_array_set_size (self->context, 0);
		model = build_background_menu (self);
	}
	else
	{
		g_clear_pointer (&self->context, g_ptr_array_unref);
		self->context = g_ptr_array_new_with_free_func (g_free);
		{
			guint i;

			for (i = 0; i < self->selected->len; i++)
				g_ptr_array_add (self->context, g_strdup (g_ptr_array_index (self->selected, i)));
		}
		model = build_entry_menu (self);
	}
	lrg_widget_set_context_menu (LRG_WIDGET (self), model);
}

/* "+" button: the background menu below it. */
static void
on_new_clicked (LrgEditorAssetsPanel *self)
{
	g_autoptr(GMenuModel) model = build_background_menu (self);

	if (self->new_menu == NULL)
		self->new_menu = g_object_ref_sink (lrg_menu_new ());
	lrg_menu_set_model (self->new_menu, model);
	lrg_menu_popup_at_widget (self->new_menu, LRG_WIDGET (self->new_button), LRG_POPUP_PLACEMENT_BELOW);
}

static void
on_view_clicked (LrgEditorAssetsPanel *self)
{
	lrg_editor_assets_panel_set_grid_view (self, !self->grid);
}

/* ==========================================================================
 * Drag and drop
 * ========================================================================== */

/* The list row under a canvas point (rows are found through their name
 * cells, which the table realizes only for visible rows). */
static guint
table_row_at (LrgEditorAssetsPanel *self,
              gfloat                x,
              gfloat                y)
{
	guint  n = g_list_model_get_n_items (G_LIST_MODEL (self->store));
	gfloat rh = lrg_table_get_row_height (self->table);
	gfloat ty = lrg_widget_get_world_y (LRG_WIDGET (self->table));
	gfloat th = lrg_widget_get_height (LRG_WIDGET (self->table));
	guint  i;

	if (!lrg_widget_contains_point (LRG_WIDGET (self->table), x, y))
		return G_MAXUINT;
	for (i = 0; i < n; i++)
	{
		LrgWidget *cell = lrg_table_get_cell_widget (self->table, i, COL_NAME);
		gfloat     cy;
		gfloat     ch;

		if (cell == NULL || !lrg_widget_get_visible (cell))
			continue;
		ch = MAX (lrg_widget_get_height (cell), rh);
		cy = lrg_widget_get_world_y (cell) + (lrg_widget_get_height (cell) - ch) * 0.5f;
		if (y >= cy && y < cy + ch && cy >= ty - 1.0f && cy < ty + th)
			return i;
	}
	return G_MAXUINT;
}

/* The entry under a canvas point in the active view, or G_MAXUINT. */
static guint
position_at (LrgEditorAssetsPanel *self,
             gfloat                x,
             gfloat                y)
{
	if (self->grid)
	{
		guint p;

		if (!lrg_widget_contains_point (LRG_WIDGET (self->grid_view), x, y))
			return G_MAXUINT;
		p = lrg_tile_view_get_item_at (self->grid_view, x, y);
		return (p == LRG_TILE_VIEW_INVALID_POSITION) ? G_MAXUINT : p;
	}
	return table_row_at (self, x, y);
}

/*
 * on_drag_prepare:
 *
 * A press on a tile or row passed the drag threshold. The pressed entry
 * is dragged; if it was part of a multi-selection that the press itself
 * collapsed (a plain click selects only the clicked tile), the previous
 * selection is restored and the whole set is dragged as an
 * "application/x-lrg-asset-list" (a GStrv of project-relative paths).
 */
static LrgDragPayload *
on_drag_prepare (LrgDragSource *source,
                 gfloat         x,
                 gfloat         y,
                 gpointer       user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);
	guint                 p = position_at (self, x, y);
	EditorAssetItem      *item = (p != G_MAXUINT) ? item_at (self, p) : NULL;
	GPtrArray            *set = NULL;
	LrgDragPayload       *payload;
	guint                 i;

	if (item == NULL || self->renaming != NULL)
		return NULL;

	/* Which paths travel */
	if (self->selected->len > 1 && path_selected (self, item->path))
		set = self->selected;
	else if (self->prev_selected->len > 1 && self->selected->len <= 1)
	{
		for (i = 0; i < self->prev_selected->len; i++)
			if (g_strcmp0 (g_ptr_array_index (self->prev_selected, i), item->path) == 0)
				break;
		if (i < self->prev_selected->len)
		{
			GPtrArray *tmp = self->selected;

			self->selected = self->prev_selected;
			self->prev_selected = tmp;
			push_selection (self, FALSE);
			set = self->selected;
		}
	}

	g_clear_pointer (&self->dragging, g_ptr_array_unref);
	self->dragging = g_ptr_array_new_with_free_func (g_free);
	if (set == NULL)
	{
		g_autofree gchar *rel = rel_path (self, item->path);

		g_ptr_array_add (self->dragging, g_strdup (item->path));
		payload = lrg_drag_payload_new_string (ASSET_MIME, rel, LRG_DRAG_ACTION_ALL);
		lrg_drag_payload_set_label (payload, item->name);
		lrg_drag_payload_set_icon (payload, item->is_dir ? LRG_ICON_FOLDER : LRG_ICON_FILE);
	}
	else
	{
		GStrvBuilder     *sb = g_strv_builder_new ();
		g_auto(GStrv)     rels = NULL;
		g_auto(GValue)    value = G_VALUE_INIT;
		g_autofree gchar *label = g_strdup_printf ("%u items", set->len);

		for (i = 0; i < set->len; i++)
		{
			g_autofree gchar *rel = rel_path (self, g_ptr_array_index (set, i));

			g_ptr_array_add (self->dragging, g_strdup (g_ptr_array_index (set, i)));
			g_strv_builder_add (sb, rel);
		}
		rels = g_strv_builder_end (sb);
		g_strv_builder_unref (sb);
		g_value_init (&value, G_TYPE_STRV);
		g_value_set_boxed (&value, rels);
		payload = lrg_drag_payload_new (ASSET_LIST_MIME, &value, LRG_DRAG_ACTION_ALL);
		lrg_drag_payload_set_label (payload, label);
		lrg_drag_payload_set_icon (payload, LRG_ICON_FILE);
	}
	return payload;
}

static void
on_drag_end (LrgDragSource  *source,
             LrgDragPayload *payload,
             gboolean        dropped,
             LrgDragAction   action,
             gpointer        user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);

	g_clear_pointer (&self->dragging, g_ptr_array_unref);
	g_clear_pointer (&self->drop_hover, g_free);
}

/* The project-relative paths a payload carries, as absolute paths. */
static GPtrArray *
payload_paths (LrgEditorAssetsPanel *self,
               LrgDragPayload       *payload)
{
	GPtrArray *out = g_ptr_array_new_with_free_func (g_free);

	if (lrg_drag_payload_matches (payload, ASSET_MIME))
	{
		const gchar *s = lrg_drag_payload_get_string (payload);

		if (s != NULL)
			g_ptr_array_add (out, abs_path (self, s));
	}
	else if (lrg_drag_payload_matches (payload, ASSET_LIST_MIME))
	{
		const GValue *v = lrg_drag_payload_get_value (payload);
		const gchar * const *strv = (v != NULL && G_VALUE_HOLDS (v, G_TYPE_STRV))
		                            ? (const gchar * const *) g_value_get_boxed (v) : NULL;
		guint i;

		for (i = 0; strv != NULL && strv[i] != NULL; i++)
			g_ptr_array_add (out, abs_path (self, strv[i]));
	}
	return out;
}

/* Can @paths move into @dest? (not into themselves, not where they are) */
static gboolean
can_move_into (GPtrArray   *paths,
               const gchar *dest)
{
	guint i;
	guint useful = 0;

	if (dest == NULL || !g_file_test (dest, G_FILE_TEST_IS_DIR))
		return FALSE;
	for (i = 0; i < paths->len; i++)
	{
		const gchar      *p = g_ptr_array_index (paths, i);
		g_autofree gchar *parent = g_path_get_dirname (p);

		if (path_is_under (dest, p))
			return FALSE;
		if (g_strcmp0 (parent, dest) != 0)
			useful++;
	}
	return useful > 0;
}

/* The folder a drop at (x, y) on @widget would move into, or NULL. */
static const gchar *
drop_folder_at (LrgEditorAssetsPanel *self,
                LrgWidget            *widget,
                gfloat                x,
                gfloat                y)
{
	if (widget == LRG_WIDGET (self->crumb))
	{
		guint n = lrg_breadcrumb_get_n_segments (self->crumb);
		guint i;

		for (i = 0; i < n; i++)
		{
			GrlRectangle r;

			if (lrg_breadcrumb_get_segment_bounds (self->crumb, (gint) i, &r) &&
			    x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
				return lrg_breadcrumb_get_segment_data (self->crumb, i);
		}
		return NULL;
	}
	else
	{
		guint            p = position_at (self, x, y);
		EditorAssetItem *item = (p != G_MAXUINT) ? item_at (self, p) : NULL;

		return (item != NULL && item->is_dir) ? item->path : NULL;
	}
}

static LrgDragAction
on_drop_motion (LrgDropTarget  *target,
                LrgDragPayload *payload,
                gfloat          x,
                gfloat          y,
                gpointer        user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);
	g_autoptr(GPtrArray)  paths = payload_paths (self, payload);
	const gchar          *dest = drop_folder_at (self, lrg_drop_target_get_widget (target), x, y);

	if (dest == NULL || !can_move_into (paths, dest))
	{
		g_clear_pointer (&self->drop_hover, g_free);
		return LRG_DRAG_ACTION_NONE;
	}
	if (g_strcmp0 (self->drop_hover, dest) != 0)
	{
		g_free (self->drop_hover);
		self->drop_hover = g_strdup (dest);
	}
	return LRG_DRAG_ACTION_MOVE;
}

static void
on_drop_leave (LrgDropTarget *target,
               gpointer       user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);

	g_clear_pointer (&self->drop_hover, g_free);
}

/* A drop on a folder (tile, row or breadcrumb segment): move there. */
static gboolean
on_drop (LrgDropTarget  *target,
         LrgDragPayload *payload,
         gfloat          x,
         gfloat          y,
         gpointer        user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);
	g_autoptr(GPtrArray)  paths = payload_paths (self, payload);
	g_autofree gchar     *dest = g_strdup (drop_folder_at (self, lrg_drop_target_get_widget (target), x, y));
	guint                 moved = 0;
	guint                 i;

	g_clear_pointer (&self->drop_hover, g_free);
	if (dest == NULL || !can_move_into (paths, dest))
		return FALSE;
	for (i = 0; i < paths->len; i++)
	{
		g_autoptr(GError) error = NULL;
		g_autofree gchar *parent = g_path_get_dirname (g_ptr_array_index (paths, i));
		g_autofree gchar *done = NULL;

		if (g_strcmp0 (parent, dest) == 0)
			continue;
		done = do_move (self, g_ptr_array_index (paths, i), dest, &error);
		if (done != NULL)
			moved++;
		else
		{
			g_autofree gchar *msg = g_strdup_printf ("Move failed: %s", error->message);

			notify_user (self, EDITOR_NOTIFY_WARNING, msg);
		}
	}
	if (moved > 0)
	{
		g_autofree gchar *base = g_path_get_basename (dest);
		g_autofree gchar *msg = g_strdup_printf ("Moved %u item(s) into %s", moved, base);

		set_status (self, msg);
	}
	return moved > 0;
}

static void
add_drop_target (LrgEditorAssetsPanel *self,
                 LrgWidget            *widget)
{
	static const gchar *types[] = { ASSET_MIME, ASSET_LIST_MIME, NULL };
	LrgDropTarget      *target = lrg_drop_target_new (types, LRG_DRAG_ACTION_MOVE);

	lrg_drop_target_set_paint_highlight (target, widget == LRG_WIDGET (self->crumb));
	g_signal_connect (target, "motion", G_CALLBACK (on_drop_motion), self);
	g_signal_connect (target, "leave", G_CALLBACK (on_drop_leave), self);
	g_signal_connect (target, "drop", G_CALLBACK (on_drop), self);
	lrg_widget_add_drop_target (widget, target);
}

static void
add_drag_source (LrgEditorAssetsPanel *self,
                 LrgWidget            *widget)
{
	LrgDragSource *source = lrg_drag_source_new ();

	g_signal_connect (source, "prepare", G_CALLBACK (on_drag_prepare), self);
	g_signal_connect (source, "drag-end", G_CALLBACK (on_drag_end), self);
	lrg_widget_add_drag_source (widget, source);
}

/* ==========================================================================
 * View factories
 * ========================================================================== */

static LrgWidget *
grid_create (LrgTileView *view,
             gpointer     user_data)
{
	return tile_new (LRG_EDITOR_ASSETS_PANEL (user_data), TILE_MODE_GRID);
}

static void
grid_bind (LrgTileView *view,
           LrgWidget   *tile,
           GObject     *item,
           guint        position,
           gpointer     user_data)
{
	tile_set_item (EDITOR_ASSET_TILE (tile), EDITOR_ASSET_ITEM (item));
}

static LrgWidget *
cell_create (LrgTable *table,
             guint     column,
             gpointer  user_data)
{
	return tile_new (LRG_EDITOR_ASSETS_PANEL (user_data),
	                 column == COL_GIT ? TILE_MODE_GIT_CELL : TILE_MODE_NAME_CELL);
}

static gchar *
cell_widget (LrgTable  *table,
             LrgWidget *cell,
             GObject   *item,
             guint      position,
             gpointer   user_data)
{
	if (cell != NULL)
		tile_set_item (EDITOR_ASSET_TILE (cell), EDITOR_ASSET_ITEM (item));
	return NULL;
}

static gchar *
cell_type (LrgTable  *table,
           LrgWidget *cell,
           GObject   *item,
           guint      position,
           gpointer   user_data)
{
	EditorAssetItem *it = EDITOR_ASSET_ITEM (item);

	return g_strdup (type_name (it->type, it->is_dir));
}

static gchar *
cell_size (LrgTable  *table,
           LrgWidget *cell,
           GObject   *item,
           guint      position,
           gpointer   user_data)
{
	EditorAssetItem *it = EDITOR_ASSET_ITEM (item);

	return it->is_dir ? g_strdup ("") : g_format_size ((guint64) it->size);
}

static gchar *
cell_modified (LrgTable  *table,
               LrgWidget *cell,
               GObject   *item,
               guint      position,
               gpointer   user_data)
{
	g_autoptr(GDateTime) dt = g_date_time_new_from_unix_local (EDITOR_ASSET_ITEM (item)->mtime);

	return (dt != NULL) ? g_date_time_format (dt, "%Y-%m-%d %H:%M") : g_strdup ("");
}

/* Sort functions: folders first, then the column's key. */
#define DIRS_FIRST(a, b) \
	if ((a)->is_dir != (b)->is_dir) \
		return (a)->is_dir ? -1 : 1

static gint
sort_name (gconstpointer a, gconstpointer b, gpointer d)
{
	const EditorAssetItem *x = a;
	const EditorAssetItem *y = b;

	DIRS_FIRST (x, y);
	return g_utf8_collate (x->name, y->name);
}

static gint
sort_type (gconstpointer a, gconstpointer b, gpointer d)
{
	const EditorAssetItem *x = a;
	const EditorAssetItem *y = b;

	DIRS_FIRST (x, y);
	if (x->type != y->type)
		return (x->type < y->type) ? -1 : 1;
	return g_utf8_collate (x->name, y->name);
}

static gint
sort_size (gconstpointer a, gconstpointer b, gpointer d)
{
	const EditorAssetItem *x = a;
	const EditorAssetItem *y = b;

	DIRS_FIRST (x, y);
	if (x->size != y->size)
		return (x->size < y->size) ? -1 : 1;
	return g_utf8_collate (x->name, y->name);
}

static gint
sort_modified (gconstpointer a, gconstpointer b, gpointer d)
{
	const EditorAssetItem *x = a;
	const EditorAssetItem *y = b;

	DIRS_FIRST (x, y);
	if (x->mtime != y->mtime)
		return (x->mtime < y->mtime) ? -1 : 1;
	return g_utf8_collate (x->name, y->name);
}

static gint
sort_git (gconstpointer a, gconstpointer b, gpointer d)
{
	const EditorAssetItem *x = a;
	const EditorAssetItem *y = b;

	DIRS_FIRST (x, y);
	if (x->vcs != y->vcs)
		return (x->vcs < y->vcs) ? 1 : -1;
	return g_utf8_collate (x->name, y->name);
}

/* ==========================================================================
 * Toolbar signals
 * ========================================================================== */

static void
on_crumb_activated (LrgBreadcrumb *crumb,
                    guint          index,
                    gpointer       user_data)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (user_data);
	g_autofree gchar     *dir = g_strdup (lrg_breadcrumb_get_segment_data (crumb, index));

	if (dir != NULL && g_strcmp0 (dir, self->dir) != 0)
		go_to (self, dir);
}

static void
on_search_changed (LrgTextInput *input,
                   gpointer      user_data)
{
	lrg_editor_assets_panel_set_filter (LRG_EDITOR_ASSETS_PANEL (user_data), lrg_text_input_get_text (input));
}

static void
on_type_changed (LrgComboBox *combo,
                 gpointer     user_data)
{
	gint sel = lrg_combo_box_get_selected (combo);

	lrg_editor_assets_panel_set_type_filter (LRG_EDITOR_ASSETS_PANEL (user_data),
	                                         sel > 0 ? (LrgAssetType) sel : LRG_ASSET_TYPE_UNKNOWN);
}

/* ==========================================================================
 * LrgWidget / LrgContainer
 * ========================================================================== */

/*
 * The branch chip: the git branch in a pill left of the breadcrumb,
 * hidden outside a repository.
 */
static const gchar *
branch_name (LrgEditorAssetsPanel *self)
{
	if (self->vcs == NULL || !lrg_editor_vcs_get_is_repo (self->vcs))
		return NULL;
	return lrg_editor_vcs_get_branch (self->vcs);
}

static void
lrg_editor_assets_panel_layout_children (LrgContainer *container)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (container);
	LrgWidget            *widget = LRG_WIDGET (self);
	gfloat                w = lrg_widget_get_width (widget);
	gfloat                h = lrg_widget_get_height (widget);
	gfloat                pad = metric_of (self, LRG_THEME_METRIC_PADDING_X) * 0.75f;
	gfloat                gap = metric_of (self, LRG_THEME_METRIC_SPACING);
	gfloat                ch = metric_of (self, LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat                fs = metric_of (self, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                search_w = CLAMP (w * 0.22f, 90.0f, 220.0f);
	gfloat                type_w = CLAMP (w * 0.14f, 120.0f, 150.0f);
	gfloat                x;
	gfloat                right;
	gfloat                body_y;
	const gchar          *branch = branch_name (self);

	/* add_child() lays out while init is still adding children */
	if (self->rename == NULL)
		return;

	self->bar_h = ch + pad * 2.0f;
	self->footer_h = fs * 1.7f;

	/* Right side, right to left: + / view / type / search */
	right = w - pad;
	right -= ch;
	lrg_widget_place_in_slot (LRG_WIDGET (self->new_button), right, pad, ch, ch);
	right -= gap * 0.5f + ch;
	lrg_widget_place_in_slot (LRG_WIDGET (self->view_button), right, pad, ch, ch);
	right -= gap + type_w;
	lrg_widget_place_in_slot (LRG_WIDGET (self->type_combo), right, pad, type_w, ch);
	right -= gap + search_w;
	lrg_widget_place_in_slot (LRG_WIDGET (self->search), right, pad, search_w, ch);

	/* Left: branch chip, breadcrumb */
	x = pad;
	self->chip_x = x;
	self->chip_w = 0.0f;
	if (branch != NULL)
	{
		gfloat tw = 0.0f;

		lrg_paint_measure_text (lrg_theme_get_ui_font (theme_of (self)), branch, fs, 0.0f, &tw, NULL);
		self->chip_w = tw + fs * 1.0f + 16.0f;
		x += self->chip_w + gap;
	}
	lrg_widget_place_in_slot (LRG_WIDGET (self->crumb), x, pad, MAX (40.0f, right - gap - x), ch);

	/* Body: one of the two views. Tiles shrink (down to 84 high) so a
	 * short bottom dock still shows one whole row, names included */
	body_y = self->bar_h;
	{
		gfloat body_h = h - body_y - self->footer_h;
		gfloat th = CLAMP (body_h - 18.0f, 84.0f, TILE_H);
		gfloat tw = TILE_W * th / TILE_H;
		gfloat cw = 0.0f;
		gfloat chh = 0.0f;

		lrg_tile_view_get_tile_size (self->grid_view, &cw, &chh);
		if (fabsf (cw - tw) > 0.5f || fabsf (chh - th) > 0.5f)
			lrg_tile_view_set_tile_size (self->grid_view, tw, th);
	}
	lrg_widget_place_in_slot (LRG_WIDGET (self->grid_view), 0.0f, body_y, w, MAX (0.0f, h - body_y - self->footer_h));
	lrg_widget_place_in_slot (LRG_WIDGET (self->table), 0.0f, body_y, w, MAX (0.0f, h - body_y - self->footer_h));

	if (self->renaming != NULL)
		place_rename (self);
}

static void
lrg_editor_assets_panel_paint (LrgWidget    *widget,
                               LrgPaintList *list)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (widget);
	gfloat                x = lrg_widget_get_world_x (widget);
	gfloat                y = lrg_widget_get_world_y (widget);
	gfloat                w = lrg_widget_get_width (widget);
	gfloat                h = lrg_widget_get_height (widget);
	gfloat                fs = metric_of (self, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GrlFont              *font = lrg_theme_get_ui_font (theme_of (self));
	const gchar          *branch = branch_name (self);
	guint                 n = g_list_model_get_n_items (G_LIST_MODEL (self->store));
	GrlColor              strip;

	/* Toolbar and footer strips, a hairline under the toolbar */
	lrg_paint_color_with_alpha (color_of (self, LRG_THEME_COLOR_SURFACE), 0.55f, &strip);
	lrg_paint_list_rect (list, x, y, w, self->bar_h, 0.0f, &strip);
	lrg_paint_list_rect (list, x, y + self->bar_h - 1.0f, w, 1.0f, 0.0f, color_of (self, LRG_THEME_COLOR_BORDER));

	/* Branch chip */
	if (branch != NULL && self->chip_w > 0.0f)
	{
		const GrlColor *accent = color_of (self, LRG_THEME_COLOR_ACCENT);
		gfloat          ch = metric_of (self, LRG_THEME_METRIC_CONTROL_HEIGHT);
		gfloat          cy = y + (self->bar_h - ch * 0.78f) * 0.5f;
		gfloat          chh = ch * 0.78f;
		GrlColor        fill;
		gfloat          dot = fs * 0.5f;

		lrg_paint_color_with_alpha (accent, 0.16f, &fill);
		lrg_paint_list_rect (list, x + self->chip_x, cy, self->chip_w, chh, chh * 0.5f, &fill);
		lrg_paint_list_border (list, x + self->chip_x, cy, self->chip_w, chh, chh * 0.5f, 1.0f, accent);
		/* A tiny commit-graph glyph: two dots joined by a line */
		lrg_paint_list_circle (list, x + self->chip_x + 8.0f + dot * 0.5f, cy + chh * 0.5f, dot * 0.5f, accent);
		lrg_paint_list_text (list, font, branch, x + self->chip_x + 10.0f + dot + 4.0f,
		                     cy + (chh - fs) * 0.5f, fs, 0.0f, color_of (self, LRG_THEME_COLOR_TEXT));
	}

	/* The views and the toolbar widgets */
	LRG_WIDGET_CLASS (lrg_editor_assets_panel_parent_class)->paint (widget, list);

	/* Empty state */
	if (n == 0)
	{
		const gchar *msg = (self->root == NULL) ? "Open a project to browse its files."
		                 : (self->all != NULL && self->all->len > 0) ? "Nothing matches the filter."
		                 : "This folder is empty.";
		gfloat       tw = 0.0f;
		gfloat       fm = metric_of (self, LRG_THEME_METRIC_FONT_SIZE);

		lrg_paint_measure_text (font, msg, fm, 0.0f, &tw, NULL);
		lrg_paint_list_text (list, font, msg, x + (w - tw) * 0.5f, y + self->bar_h + (h - self->bar_h) * 0.4f,
		                     fm, 0.0f, color_of (self, LRG_THEME_COLOR_TEXT_MUTED));
	}

	/* Footer: count and selection */
	{
		g_autofree gchar *text = NULL;

		if (self->selected->len > 0)
			text = g_strdup_printf ("%u item%s, %u selected", n, n == 1 ? "" : "s", self->selected->len);
		else
			text = g_strdup_printf ("%u item%s", n, n == 1 ? "" : "s");
		lrg_paint_list_rect (list, x, y + h - self->footer_h, w, 1.0f, 0.0f,
		                     color_of (self, LRG_THEME_COLOR_BORDER));
		lrg_paint_list_text (list, font, text, x + metric_of (self, LRG_THEME_METRIC_PADDING_X),
		                     y + h - self->footer_h + (self->footer_h - fs) * 0.5f, fs, 0.0f,
		                     color_of (self, LRG_THEME_COLOR_TEXT_MUTED));
	}
}

/*
 * step:
 *
 * Every frame while the panel is shown: follow the app (project, git),
 * list again after a monitor change, keep the rename field over its
 * entry and cancel the rename when focus went elsewhere.
 */
static gboolean
lrg_editor_assets_panel_step (LrgWidget *widget,
                              gfloat     delta_time)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (widget);
	LrgWidget            *root;

	sync_app (self);
	if (self->dirty)
		refresh_listing (self);

	if (self->renaming != NULL)
	{
		root = lrg_widget_get_root (widget);
		if (LRG_IS_CANVAS (root))
		{
			LrgWidget *focus = lrg_canvas_get_focused_widget (LRG_CANVAS (root));

			if (focus == LRG_WIDGET (self->rename))
				self->rename_focused = TRUE;
			else if (self->rename_focused)
				cancel_rename (self);
		}
		place_rename (self);
	}
	{
		GAction *a = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "grid-view");

		if (a != NULL)
			g_simple_action_set_state (G_SIMPLE_ACTION (a), g_variant_new_boolean (self->grid));
	}
	return FALSE;
}

/*
 * handle_event:
 *
 * Events that the views and toolbar widgets leave unhandled bubble here:
 * a right press picks the entry under the pointer (or clears the
 * selection on empty space) and sets the matching context menu, which
 * the canvas then opens; keys: Delete trashes (confirmed), F2 renames,
 * Backspace goes up, Escape cancels a rename or clears the filter.
 */
static gboolean
lrg_editor_assets_panel_handle_event (LrgWidget        *widget,
                                      const LrgUIEvent *event)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (widget);
	LrgWidget            *target = lrg_ui_event_get_target (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			gfloat x = lrg_ui_event_get_x (event);
			gfloat y = lrg_ui_event_get_y (event);
			guint  p;

			if (lrg_ui_event_get_button (event) != 1)
				return FALSE;
			if (y < lrg_widget_get_world_y (widget) + self->bar_h)
			{
				lrg_widget_set_context_menu (widget, NULL);
				return FALSE;
			}
			p = position_at (self, x, y);
			if (p == G_MAXUINT)
			{
				self->syncing = TRUE;
				lrg_tile_view_unselect_all (self->grid_view);
				lrg_table_unselect_all (self->table);
				self->syncing = FALSE;
				g_ptr_array_set_size (self->selected, 0);
				update_context_menu (self, TRUE);
				return FALSE;
			}
			if (!path_selected (self, item_path (self, p)))
			{
				g_ptr_array_set_size (self->selected, 0);
				g_ptr_array_add (self->selected, g_strdup (item_path (self, p)));
				push_selection (self, FALSE);
			}
			update_context_menu (self, FALSE);
			return FALSE;   /* the canvas opens LrgWidget:context-menu */
		}

	case LRG_UI_EVENT_CANCEL:
		if (self->renaming != NULL)
		{
			cancel_rename (self);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_KEY_DOWN:
		{
			GrlKey key = lrg_ui_event_get_key (event);

			if (target == LRG_WIDGET (self->rename))
			{
				if (key == GRL_KEY_ESCAPE)
				{
					cancel_rename (self);
					return TRUE;
				}
				return FALSE;
			}
			if (target == LRG_WIDGET (self->search))
			{
				if (key == GRL_KEY_ESCAPE && self->filter != NULL && self->filter[0] != '\0')
				{
					lrg_text_input_set_text (self->search, "");
					return TRUE;
				}
				return FALSE;
			}
			if (lrg_ui_event_get_modifiers (event) & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_ALT))
				return FALSE;
			switch (key)
			{
			case GRL_KEY_DELETE:
				if (self->selected->len == 0)
					return FALSE;
				update_context_menu (self, FALSE);
				act_trash (NULL, NULL, self);
				return TRUE;
			case GRL_KEY_F2:
				if (self->selected->len == 0)
					return FALSE;
				{
					g_autofree gchar *path = g_strdup (g_ptr_array_index (self->selected, 0));

					lrg_editor_assets_panel_begin_rename (self, path);
				}
				return TRUE;
			case GRL_KEY_BACKSPACE:
				if (self->dir == NULL || g_strcmp0 (self->dir, self->root) == 0)
					return FALSE;
				{
					g_autofree gchar *from = g_strdup (self->dir);

					go_up (self);
					g_ptr_array_set_size (self->selected, 0);
					g_ptr_array_add (self->selected, g_steal_pointer (&from));
					push_selection (self, TRUE);
				}
				return TRUE;
			default:
				return FALSE;
			}
		}

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * App integration: registry actions and IPC methods
 * ========================================================================== */

/* The assets panel of @app (the dock panel "assets"), or NULL. */
static LrgEditorAssetsPanel *
panel_of (LrgEditorApp *app)
{
	LrgDockPanel *dp = (app->panels != NULL) ? g_hash_table_lookup (app->panels, "assets") : NULL;
	LrgWidget    *child = (dp != NULL) ? lrg_dock_panel_get_child (dp) : NULL;

	return (child != NULL && LRG_IS_EDITOR_ASSETS_PANEL (child)) ? LRG_EDITOR_ASSETS_PANEL (child) : NULL;
}

static gboolean
reg_grid_checked (gpointer data)
{
	LrgEditorAssetsPanel *self = panel_of (LRG_EDITOR_APP (data));

	return self != NULL && self->grid;
}

static void
reg_grid_toggle (gpointer data)
{
	LrgEditorAssetsPanel *self = panel_of (LRG_EDITOR_APP (data));

	if (self != NULL)
		lrg_editor_assets_panel_set_grid_view (self, !self->grid);
}

/* Tools > Refresh Assets: rescan the project database, list again. */
static void
reg_refresh (gpointer data)
{
	LrgEditorApp         *app = LRG_EDITOR_APP (data);
	LrgEditorAssetsPanel *self = panel_of (app);
	g_autoptr(GError)     error = NULL;

	if (app->assets != NULL && !lrg_asset_database_scan (app->assets, &error))
		editor_app_notify (app, EDITOR_NOTIFY_WARNING, "Refresh assets: %s", error->message);
	if (app->vcs != NULL)
		lrg_editor_vcs_queue_refresh (app->vcs);
	if (self != NULL)
		lrg_editor_assets_panel_refresh (self);
	editor_app_set_status (app, "Assets refreshed");
}

/* --- IPC helpers --- */

static const gchar *
param_string (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	       ? json_node_get_string (n) : NULL;
}

static gboolean
param_bool (JsonObject  *params,
            const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	return JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_BOOLEAN && json_node_get_boolean (n);
}

/* The method's panel: always there in the editor (created with the app). */
static LrgEditorAssetsPanel *
method_panel (gpointer   user_data,
              GError   **error)
{
	LrgEditorAssetsPanel *self = panel_of (LRG_EDITOR_APP (user_data));

	if (self == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "The assets panel is not available");
		return NULL;
	}
	sync_app (self);
	if (self->root == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "No project is open");
		return NULL;
	}
	return self;
}

/* The "path" parameter as an existing absolute path inside the root. */
static gchar *
method_path (LrgEditorAssetsPanel  *self,
             JsonObject            *params,
             const gchar           *name,
             GError               **error)
{
	const gchar      *p = param_string (params, name);
	g_autofree gchar *abs = NULL;

	if (p == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing string parameter '%s'", name);
		return NULL;
	}
	abs = abs_path (self, p);
	if (!path_is_under (abs, self->root) || !g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No file '%s' in the project", p);
		return NULL;
	}
	return g_steal_pointer (&abs);
}

/* {path, relative, guid, is_dir} of a file or folder. */
static JsonNode *
entry_json (LrgEditorAssetsPanel *self,
            const gchar          *path)
{
	JsonObject       *o = json_object_new ();
	JsonNode         *node = json_node_new (JSON_NODE_OBJECT);
	g_autofree gchar *rel = rel_path (self, path);
	gboolean          is_dir = g_file_test (path, G_FILE_TEST_IS_DIR);
	LrgAssetEntry    *entry = is_dir ? NULL : lookup_entry (self, path);

	json_object_set_string_member (o, "path", path);
	json_object_set_string_member (o, "relative", rel);
	json_object_set_boolean_member (o, "is_dir", is_dir);
	if (entry != NULL && lrg_asset_entry_get_guid (entry) != NULL)
		json_object_set_string_member (o, "guid", lrg_asset_entry_get_guid (entry));
	else
		json_object_set_null_member (o, "guid");
	json_node_take_object (node, o);
	return node;
}

static void
set_failed (GError     **error,
            GError      *cause,
            const gchar *what)
{
	gint code = g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_EXISTS) ||
	            g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME) ||
	            g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)
	            ? LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS : LRG_EDITOR_SERVER_ERROR_FAILED;

	g_set_error (error, LRG_EDITOR_SERVER_ERROR, code, "%s: %s", what, cause->message);
}

/* asset.reveal {path}: show the panel, its folder, select it. */
static JsonNode *
m_asset_reveal (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorAssetsPanel *self = method_panel (user_data, error);
	g_autofree gchar     *path = NULL;
	JsonNode             *node;

	if (self == NULL || (path = method_path (self, params, "path", error)) == NULL)
		return NULL;
	editor_app_show_panel (LRG_EDITOR_APP (user_data), "assets");
	if (!lrg_editor_assets_panel_reveal (self, path))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "Cannot reveal '%s'", path);
		return NULL;
	}
	node = entry_json (self, path);
	json_object_set_string_member (json_node_get_object (node), "directory", self->dir);
	json_object_set_int_member (json_node_get_object (node), "index", find_position (self, path));
	return node;
}

/* asset.rename {path, name} */
static JsonNode *
m_asset_rename (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorAssetsPanel *self = method_panel (user_data, error);
	g_autofree gchar     *path = NULL;
	g_autofree gchar     *done = NULL;
	g_autoptr(GError)     cause = NULL;
	const gchar          *name = param_string (params, "name");

	if (self == NULL || (path = method_path (self, params, "path", error)) == NULL)
		return NULL;
	if (name == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'name'");
		return NULL;
	}
	if ((done = do_rename (self, path, name, &cause)) == NULL)
	{
		set_failed (error, cause, "Rename failed");
		return NULL;
	}
	return entry_json (self, done);
}

/* asset.move {path, dest} (dest: a folder, "" for the project root) */
static JsonNode *
m_asset_move (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorAssetsPanel *self = method_panel (user_data, error);
	g_autofree gchar     *path = NULL;
	g_autofree gchar     *dest = NULL;
	g_autofree gchar     *done = NULL;
	g_autoptr(GError)     cause = NULL;
	const gchar          *d = param_string (params, "dest");

	if (self == NULL || (path = method_path (self, params, "path", error)) == NULL)
		return NULL;
	if (d == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'dest'");
		return NULL;
	}
	dest = (d[0] == '\0') ? g_strdup (self->root) : abs_path (self, d);
	if (!path_is_under (dest, self->root) || !g_file_test (dest, G_FILE_TEST_IS_DIR))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No folder '%s' in the project", d);
		return NULL;
	}
	if ((done = do_move (self, path, dest, &cause)) == NULL)
	{
		set_failed (error, cause, "Move failed");
		return NULL;
	}
	return entry_json (self, done);
}

/* asset.duplicate {path}: the copy, with a fresh GUID */
static JsonNode *
m_asset_duplicate (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorAssetsPanel *self = method_panel (user_data, error);
	g_autofree gchar     *path = NULL;
	g_autofree gchar     *done = NULL;
	g_autoptr(GError)     cause = NULL;

	if (self == NULL || (path = method_path (self, params, "path", error)) == NULL)
		return NULL;
	if ((done = do_duplicate (self, path, &cause)) == NULL)
	{
		set_failed (error, cause, "Duplicate failed");
		return NULL;
	}
	return entry_json (self, done);
}

/* asset.trash {path}: no confirmation (the caller asked) */
static JsonNode *
m_asset_trash (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorAssetsPanel *self = method_panel (user_data, error);
	g_autofree gchar     *path = NULL;
	g_autoptr(GError)     cause = NULL;
	JsonObject           *o;
	JsonNode             *node;

	if (self == NULL || (path = method_path (self, params, "path", error)) == NULL)
		return NULL;
	if (!do_trash (self, path, &cause))
	{
		set_failed (error, cause, "Trash failed");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", path);
	json_object_set_boolean_member (o, "trashed", TRUE);
	node = json_node_new (JSON_NODE_OBJECT);
	json_node_take_object (node, o);
	return node;
}

/* asset.orphans {trash?}: sidecars whose asset is gone; trash: TRUE
 * moves them to the trash */
static JsonNode *
m_asset_orphans (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorAssetsPanel *self = method_panel (user_data, error);
	LrgAssetDatabase     *db;
	GPtrArray            *orphans;
	g_autoptr(GPtrArray)  failed = g_ptr_array_new_with_free_func (g_free);
	JsonArray            *arr = json_array_new ();
	JsonObject           *o = json_object_new ();
	JsonNode             *node = json_node_new (JSON_NODE_OBJECT);
	guint                 trashed = 0;
	guint                 i;

	if (self == NULL)
	{
		json_array_unref (arr);
		json_object_unref (o);
		json_node_free (node);
		return NULL;
	}
	db = get_db (self);
	lrg_asset_database_scan (db, NULL);
	orphans = lrg_asset_database_get_orphaned_metas (db);
	for (i = 0; orphans != NULL && i < orphans->len; i++)
	{
		g_autofree gchar *rel = rel_path (self, g_ptr_array_index (orphans, i));

		json_array_add_string_element (arr, rel);
	}
	if (param_bool (params, "trash"))
		trashed = trash_orphans (self, failed);
	json_object_set_array_member (o, "orphans", arr);
	json_object_set_int_member (o, "trashed", trashed);
	json_node_take_object (node, o);
	return node;
}

static void
register_with_app (LrgEditorAssetsPanel *self)
{
	LrgEditorApp     *app = self->app;
	LrgEditorActions *actions = lrg_editor_app_get_actions (app);
	LrgEditorServer  *server = lrg_editor_app_get_server (app);

	/* Give the shell's placeholder actions their real callbacks */
	lrg_editor_actions_add (actions, "view.asset-grid", "Asset Thumbnails (Grid View)", "View", NULL,
	                        reg_grid_toggle, NULL, reg_grid_checked, app);
	lrg_editor_actions_add (actions, "tools.refresh-assets", "Refresh Assets", "Tools", NULL,
	                        reg_refresh, NULL, NULL, app);

	if (server == NULL)
		return;
	lrg_editor_server_register_method (server, "asset.reveal", m_asset_reveal, app, NULL);
	lrg_editor_server_register_method (server, "asset.rename", m_asset_rename, app, NULL);
	lrg_editor_server_register_method (server, "asset.move", m_asset_move, app, NULL);
	lrg_editor_server_register_method (server, "asset.duplicate", m_asset_duplicate, app, NULL);
	lrg_editor_server_register_method (server, "asset.trash", m_asset_trash, app, NULL);
	lrg_editor_server_register_method (server, "asset.orphans", m_asset_orphans, app, NULL);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_assets_panel_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (object);

	switch (prop_id)
	{
	case PROP_DIRECTORY:
		g_value_set_string (value, self->dir);
		break;
	case PROP_GRID_VIEW:
		g_value_set_boolean (value, self->grid);
		break;
	case PROP_FILTER:
		g_value_set_string (value, self->filter != NULL ? self->filter : "");
		break;
	case PROP_TYPE_FILTER:
		g_value_set_enum (value, self->type_filter);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_assets_panel_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (object);

	switch (prop_id)
	{
	case PROP_DIRECTORY:
		lrg_editor_assets_panel_set_directory (self, g_value_get_string (value));
		break;
	case PROP_GRID_VIEW:
		lrg_editor_assets_panel_set_grid_view (self, g_value_get_boolean (value));
		break;
	case PROP_FILTER:
		lrg_editor_assets_panel_set_filter (self, g_value_get_string (value));
		break;
	case PROP_TYPE_FILTER:
		lrg_editor_assets_panel_set_type_filter (self, g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_assets_panel_dispose (GObject *object)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (object);

	if (self->monitor != NULL)
	{
		g_file_monitor_cancel (self->monitor);
		g_signal_handlers_disconnect_by_data (self->monitor, self);
		g_clear_object (&self->monitor);
	}
	if (self->vcs != NULL && self->vcs_handler != 0)
		g_signal_handler_disconnect (self->vcs, self->vcs_handler);
	self->vcs_handler = 0;
	g_clear_object (&self->vcs);
	g_clear_object (&self->own_db);
	g_clear_object (&self->new_menu);
	g_clear_object (&self->actions);
	g_clear_weak_pointer (&self->trash_dialog);
	g_clear_weak_pointer (&self->import_dialog);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_assets_panel_parent_class)->dispose (object);
}

static void
lrg_editor_assets_panel_finalize (GObject *object)
{
	LrgEditorAssetsPanel *self = LRG_EDITOR_ASSETS_PANEL (object);

	g_free (self->root);
	g_free (self->dir);
	g_free (self->filter);
	g_free (self->pending_select);
	g_free (self->renaming);
	g_free (self->drop_hover);
	g_clear_pointer (&self->all, g_ptr_array_unref);
	g_clear_object (&self->store);
	g_clear_pointer (&self->selected, g_ptr_array_unref);
	g_clear_pointer (&self->prev_selected, g_ptr_array_unref);
	g_clear_pointer (&self->context, g_ptr_array_unref);
	g_clear_pointer (&self->dragging, g_ptr_array_unref);
	g_clear_pointer (&self->trash_paths, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_editor_assets_panel_parent_class)->finalize (object);
}

static void
lrg_editor_assets_panel_class_init (LrgEditorAssetsPanelClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->get_property = lrg_editor_assets_panel_get_property;
	object_class->set_property = lrg_editor_assets_panel_set_property;
	object_class->dispose = lrg_editor_assets_panel_dispose;
	object_class->finalize = lrg_editor_assets_panel_finalize;
	widget_class->paint = lrg_editor_assets_panel_paint;
	widget_class->step = lrg_editor_assets_panel_step;
	widget_class->handle_event = lrg_editor_assets_panel_handle_event;
	container_class->layout_children = lrg_editor_assets_panel_layout_children;

	/**
	 * LrgEditorAssetsPanel:directory:
	 *
	 * The absolute path of the folder shown, %NULL without a project.
	 * Setting a folder outside the project is ignored.
	 */
	properties[PROP_DIRECTORY] =
		g_param_spec_string ("directory", "Directory", "The folder shown", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditorAssetsPanel:grid-view:
	 *
	 * %TRUE for the thumbnail grid (#LrgTileView), %FALSE for the list
	 * (#LrgTable). The "view.asset-grid" action toggles it.
	 */
	properties[PROP_GRID_VIEW] =
		g_param_spec_boolean ("grid-view", "Grid View", "Thumbnail grid instead of the list", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditorAssetsPanel:filter:
	 *
	 * The fuzzy name filter ("" for none).
	 */
	properties[PROP_FILTER] =
		g_param_spec_string ("filter", "Filter", "Fuzzy name filter", "",
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditorAssetsPanel:type-filter:
	 *
	 * The asset type shown, %LRG_ASSET_TYPE_UNKNOWN for all.
	 */
	properties[PROP_TYPE_FILTER] =
		g_param_spec_enum ("type-filter", "Type Filter", "Asset type shown", LRG_TYPE_ASSET_TYPE,
		                   LRG_ASSET_TYPE_UNKNOWN,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_editor_assets_panel_init (LrgEditorAssetsPanel *self)
{
	LrgContainer *c = LRG_CONTAINER (self);
	guint         i;

	self->grid = TRUE;
	self->type_filter = LRG_ASSET_TYPE_UNKNOWN;
	self->filter = g_strdup ("");
	self->all = g_ptr_array_new_with_free_func (g_object_unref);
	self->store = g_list_store_new (EDITOR_TYPE_ASSET_ITEM);
	self->selected = g_ptr_array_new_with_free_func (g_free);
	self->prev_selected = g_ptr_array_new_with_free_func (g_free);
	lrg_widget_set_name (LRG_WIDGET (self), "assets-panel");

	/* Toolbar */
	self->crumb = lrg_breadcrumb_new ();
	lrg_widget_set_name (LRG_WIDGET (self->crumb), "assets-breadcrumb");
	g_signal_connect (self->crumb, "activated", G_CALLBACK (on_crumb_activated), self);
	lrg_container_add_child (c, LRG_WIDGET (self->crumb));
	g_object_unref (self->crumb);

	self->search = lrg_text_input_new_with_placeholder ("Filter files");
	lrg_widget_set_name (LRG_WIDGET (self->search), "assets-filter");
	g_signal_connect (self->search, "text-changed", G_CALLBACK (on_search_changed), self);
	lrg_container_add_child (c, LRG_WIDGET (self->search));
	g_object_unref (self->search);

	self->type_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->type_combo), "assets-type");
	for (i = 0; i < G_N_ELEMENTS (type_filters); i++)
		lrg_combo_box_append (self->type_combo, type_filters[i]);
	lrg_combo_box_set_selected (self->type_combo, 0);
	g_signal_connect (self->type_combo, "changed", G_CALLBACK (on_type_changed), self);
	lrg_container_add_child (c, LRG_WIDGET (self->type_combo));
	g_object_unref (self->type_combo);

	self->view_button = button_new (self, LRG_ICON_NONE, on_view_clicked, "assets-view-toggle");
	lrg_widget_set_state_flags (LRG_WIDGET (self->view_button), LRG_WIDGET_STATE_CHECKED);
	lrg_container_add_child (c, LRG_WIDGET (self->view_button));
	g_object_unref (self->view_button);

	self->new_button = button_new (self, LRG_ICON_PLUS, on_new_clicked, "assets-new");
	lrg_container_add_child (c, LRG_WIDGET (self->new_button));
	g_object_unref (self->new_button);

	/* Grid */
	self->grid_view = lrg_tile_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid_view), "assets-grid");
	lrg_tile_view_set_tile_size (self->grid_view, TILE_W, TILE_H);
	lrg_tile_view_set_selection_mode (self->grid_view, LRG_SELECTION_MODE_MULTIPLE);
	lrg_tile_view_set_factory (self->grid_view, grid_create, grid_bind, self, NULL);
	lrg_tile_view_set_model (self->grid_view, G_LIST_MODEL (self->store));
	g_signal_connect (self->grid_view, "selection-changed", G_CALLBACK (on_view_selection_changed), self);
	g_signal_connect (self->grid_view, "tile-activated", G_CALLBACK (on_tile_activated), self);
	lrg_container_add_child (c, LRG_WIDGET (self->grid_view));
	g_object_unref (self->grid_view);

	/* List */
	self->table = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->table), "assets-list");
	lrg_table_set_selection_mode (self->table, LRG_SELECTION_MODE_MULTIPLE);
	lrg_table_add_column (self->table, "Name", cell_widget, NULL, NULL, 260.0f,
	                      LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_SORTABLE | LRG_TABLE_COLUMN_EXPAND);
	lrg_table_add_column (self->table, "Type", cell_type, NULL, NULL, 90.0f,
	                      LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_add_column (self->table, "Size", cell_size, NULL, NULL, 90.0f,
	                      LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_add_column (self->table, "Modified", cell_modified, NULL, NULL, 140.0f,
	                      LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_add_column (self->table, "Git", cell_widget, NULL, NULL, 48.0f, LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_set_column_widget_factory (self->table, COL_NAME, cell_create, self, NULL);
	lrg_table_set_column_widget_factory (self->table, COL_GIT, cell_create, self, NULL);
	lrg_table_set_sort_func (self->table, COL_NAME, sort_name, NULL, NULL);
	lrg_table_set_sort_func (self->table, COL_TYPE, sort_type, NULL, NULL);
	lrg_table_set_sort_func (self->table, COL_SIZE, sort_size, NULL, NULL);
	lrg_table_set_sort_func (self->table, COL_MODIFIED, sort_modified, NULL, NULL);
	lrg_table_set_sort_func (self->table, COL_GIT, sort_git, NULL, NULL);
	lrg_table_set_model (self->table, G_LIST_MODEL (self->store));
	g_signal_connect (self->table, "selection-changed", G_CALLBACK (on_view_selection_changed), self);
	g_signal_connect (self->table, "row-activated", G_CALLBACK (on_row_activated), self);
	lrg_widget_set_visible (LRG_WIDGET (self->table), FALSE);
	lrg_container_add_child (c, LRG_WIDGET (self->table));
	g_object_unref (self->table);

	/* Drags out of both views; drops onto folders and crumbs */
	add_drag_source (self, LRG_WIDGET (self->grid_view));
	add_drag_source (self, LRG_WIDGET (self->table));
	add_drop_target (self, LRG_WIDGET (self->grid_view));
	add_drop_target (self, LRG_WIDGET (self->table));
	add_drop_target (self, LRG_WIDGET (self->crumb));

	/* Inline rename field, above the views */
	self->rename = lrg_text_input_new ();
	lrg_widget_set_name (LRG_WIDGET (self->rename), "assets-rename");
	g_signal_connect (self->rename, "submitted", G_CALLBACK (on_rename_submitted), self);
	lrg_widget_set_visible (LRG_WIDGET (self->rename), FALSE);
	lrg_container_add_child (c, LRG_WIDGET (self->rename));
	g_object_unref (self->rename);

	/* Context menu actions ("assets.*") */
	self->actions = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (self->actions), action_entries,
	                                 G_N_ELEMENTS (action_entries), self);
	lrg_widget_insert_action_group (LRG_WIDGET (self), "assets", G_ACTION_GROUP (self->actions));
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorAssetsPanel *
lrg_editor_assets_panel_new (LrgEditorApp *app)
{
	LrgEditorAssetsPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_ASSETS_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	lrg_editor_data_editor_install (app);     /* data.open for open_entry() */
	register_with_app (self);
	sync_app (self);
	update_context_menu (self, TRUE);
	return self;
}

LrgEditorApp *
lrg_editor_assets_panel_get_app (LrgEditorAssetsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), NULL);
	return self->app;
}

const gchar *
lrg_editor_assets_panel_get_directory (LrgEditorAssetsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), NULL);
	sync_app (self);
	return self->dir;
}

gboolean
lrg_editor_assets_panel_set_directory (LrgEditorAssetsPanel *self,
                                       const gchar          *dir)
{
	g_autofree gchar *abs = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), FALSE);
	sync_app (self);
	if (dir == NULL)
		return FALSE;
	abs = abs_path (self, dir);
	return go_to (self, abs);
}

gboolean
lrg_editor_assets_panel_get_grid_view (LrgEditorAssetsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), FALSE);
	return self->grid;
}

void
lrg_editor_assets_panel_set_grid_view (LrgEditorAssetsPanel *self,
                                       gboolean              grid)
{
	g_return_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self));

	grid = !!grid;
	if (self->grid == grid)
		return;
	cancel_rename (self);
	self->grid = grid;
	lrg_widget_set_visible (LRG_WIDGET (self->grid_view), grid);
	lrg_widget_set_visible (LRG_WIDGET (self->table), !grid);
	if (grid)
		lrg_widget_set_state_flags (LRG_WIDGET (self->view_button), LRG_WIDGET_STATE_CHECKED);
	else
		lrg_widget_unset_state_flags (LRG_WIDGET (self->view_button), LRG_WIDGET_STATE_CHECKED);
	push_selection (self, TRUE);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID_VIEW]);
}

const gchar *
lrg_editor_assets_panel_get_filter (LrgEditorAssetsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), "");
	return self->filter;
}

void
lrg_editor_assets_panel_set_filter (LrgEditorAssetsPanel *self,
                                    const gchar          *filter)
{
	g_return_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self));

	if (filter == NULL)
		filter = "";
	if (g_strcmp0 (filter, self->filter) == 0)
		return;
	g_free (self->filter);
	self->filter = g_strdup (filter);
	if (g_strcmp0 (lrg_text_input_get_text (self->search), filter) != 0)
		lrg_text_input_set_text (self->search, filter);
	rebuild_view (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTER]);
}

LrgAssetType
lrg_editor_assets_panel_get_type_filter (LrgEditorAssetsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), LRG_ASSET_TYPE_UNKNOWN);
	return self->type_filter;
}

void
lrg_editor_assets_panel_set_type_filter (LrgEditorAssetsPanel *self,
                                         LrgAssetType          type)
{
	g_return_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self));

	if ((guint) type >= G_N_ELEMENTS (type_filters))
		type = LRG_ASSET_TYPE_UNKNOWN;
	if (type == self->type_filter)
		return;
	self->type_filter = type;
	if (lrg_combo_box_get_selected (self->type_combo) != (gint) type)
		lrg_combo_box_set_selected (self->type_combo, (gint) type);
	rebuild_view (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TYPE_FILTER]);
}

void
lrg_editor_assets_panel_refresh (LrgEditorAssetsPanel *self)
{
	g_return_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self));
	sync_app (self);
	refresh_listing (self);
}

guint
lrg_editor_assets_panel_get_n_entries (LrgEditorAssetsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), 0);
	return g_list_model_get_n_items (G_LIST_MODEL (self->store));
}

const gchar *
lrg_editor_assets_panel_get_entry_path (LrgEditorAssetsPanel *self,
                                        guint                 index)
{
	EditorAssetItem *item;

	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), NULL);
	item = item_at (self, index);
	return (item != NULL) ? item->path : NULL;
}

gint
lrg_editor_assets_panel_find_entry (LrgEditorAssetsPanel *self,
                                    const gchar          *path)
{
	g_autofree gchar *abs = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), -1);
	abs = abs_path (self, path);
	return find_position (self, abs);
}

LrgEditorVcsStatus
lrg_editor_assets_panel_get_entry_vcs_status (LrgEditorAssetsPanel *self,
                                              guint                 index)
{
	EditorAssetItem *item;

	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), LRG_EDITOR_VCS_STATUS_UNMODIFIED);
	item = item_at (self, index);
	return (item != NULL) ? item->vcs : LRG_EDITOR_VCS_STATUS_UNMODIFIED;
}

GStrv
lrg_editor_assets_panel_get_selected_paths (LrgEditorAssetsPanel *self)
{
	GStrvBuilder *b;
	GStrv         out;
	guint         n;
	guint         i;

	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), NULL);
	b = g_strv_builder_new ();
	n = g_list_model_get_n_items (G_LIST_MODEL (self->store));
	for (i = 0; i < n; i++)
		if (path_selected (self, item_path (self, i)))
			g_strv_builder_add (b, item_path (self, i));
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

gboolean
lrg_editor_assets_panel_reveal (LrgEditorAssetsPanel *self,
                                const gchar          *path)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *dir = NULL;
	EditorAssetItem  *item;
	gint              p;

	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), FALSE);
	sync_app (self);
	abs = abs_path (self, path);
	if (abs == NULL || self->root == NULL || !path_is_under (abs, self->root) ||
	    g_strcmp0 (abs, self->root) == 0 || !g_file_test (abs, G_FILE_TEST_EXISTS))
		return FALSE;

	dir = g_path_get_dirname (abs);
	if (g_strcmp0 (dir, self->dir) != 0 && !go_to (self, dir))
		return FALSE;
	else if (self->dirty)
		refresh_listing (self);

	/* Filters that would hide it go */
	p = find_position (self, abs);
	if (p < 0)
	{
		g_free (self->filter);
		self->filter = g_strdup ("");
		lrg_text_input_set_text (self->search, "");
		self->type_filter = LRG_ASSET_TYPE_UNKNOWN;
		lrg_combo_box_set_selected (self->type_combo, 0);
		refresh_listing (self);
		p = find_position (self, abs);
	}
	if (p < 0)
		return FALSE;
	item = item_at (self, (guint) p);
	if (item == NULL)
		return FALSE;
	g_ptr_array_set_size (self->selected, 0);
	g_ptr_array_add (self->selected, g_strdup (item->path));
	push_selection (self, TRUE);
	update_context_menu (self, FALSE);
	return TRUE;
}

gboolean
lrg_editor_assets_panel_begin_rename (LrgEditorAssetsPanel *self,
                                      const gchar          *path)
{
	g_autofree gchar *abs = NULL;
	EditorAssetItem  *item;
	LrgWidget        *root;
	const gchar      *dot;
	gint              p;
	glong             stem;

	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), FALSE);
	abs = abs_path (self, path);
	p = find_position (self, abs);
	if (p < 0)
		return FALSE;
	item = item_at (self, (guint) p);
	if (item == NULL)
		return FALSE;

	cancel_rename (self);
	self->renaming = g_strdup (item->path);
	self->rename_focused = FALSE;
	g_ptr_array_set_size (self->selected, 0);
	g_ptr_array_add (self->selected, g_strdup (item->path));
	push_selection (self, TRUE);

	/* The name, selected up to its extension */
	lrg_text_input_set_text (self->rename, item->name);
	dot = item->is_dir ? NULL : strrchr (item->name, '.');
	stem = (dot != NULL && dot != item->name) ? g_utf8_pointer_to_offset (item->name, dot)
	                                          : g_utf8_strlen (item->name, -1);
	lrg_text_input_set_selection (self->rename, 0, (gint) stem);
	lrg_widget_set_visible (LRG_WIDGET (self->rename), TRUE);
	place_rename (self);

	root = lrg_widget_get_root (LRG_WIDGET (self));
	if (LRG_IS_CANVAS (root))
		lrg_canvas_set_focused_widget (LRG_CANVAS (root), LRG_WIDGET (self->rename));
	lrg_text_input_set_selection (self->rename, 0, (gint) stem);
	return TRUE;
}

const gchar *
lrg_editor_assets_panel_get_renaming (LrgEditorAssetsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), NULL);
	return self->renaming;
}

gboolean
lrg_editor_assets_panel_show_import_settings (LrgEditorAssetsPanel *self,
                                              const gchar          *path)
{
	g_autofree gchar               *abs = NULL;
	g_autofree gchar               *base = NULL;
	g_autofree gchar               *title = NULL;
	g_autofree gchar               *body = NULL;
	g_autoptr(EditorAssetMetaSource) src = NULL;
	g_auto(GStrv)                   keys = NULL;
	LrgAssetEntry                  *entry;
	LrgPropertyGrid                *grid;
	LrgDialog                      *dialog;
	LrgWidget                      *root;
	const gchar                    *importer;

	g_return_val_if_fail (LRG_IS_EDITOR_ASSETS_PANEL (self), FALSE);
	sync_app (self);
	abs = abs_path (self, path);
	if (abs == NULL || !g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		return FALSE;
	entry = lookup_entry (self, abs);
	if (entry == NULL || lrg_asset_entry_get_meta (entry) == NULL)
		return FALSE;
	importer = lrg_asset_meta_get_importer (lrg_asset_entry_get_meta (entry));
	keys = lrg_asset_meta_list_known_settings (importer);
	if (keys == NULL || keys[0] == NULL)
		return FALSE;

	root = lrg_widget_get_root (LRG_WIDGET (self));
	if (!LRG_IS_CANVAS (root))
		return FALSE;

	src = g_object_new (EDITOR_TYPE_ASSET_META_SOURCE, NULL);
	g_set_weak_pointer (&src->panel, self);
	src->path = g_strdup (abs);
	src->importer = g_strdup (importer);

	base = g_path_get_basename (abs);
	title = g_strdup_printf ("Import Settings: %s", base);
	body = g_strdup_printf ("%s importer, GUID %s\nChanges are saved to %s.meta right away and apply to "
	                        "editor previews.", importer, lrg_asset_entry_get_guid (entry), base);

	grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (grid), "assets-import-grid");
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (src));
	lrg_widget_set_min_size (LRG_WIDGET (grid), 380.0f, 0.0f);

	if (self->import_dialog != NULL)
		lrg_dialog_close (self->import_dialog);
	dialog = lrg_dialog_new (title, body);
	lrg_widget_set_name (LRG_WIDGET (dialog), "assets-import-dialog");
	lrg_dialog_set_content (dialog, LRG_WIDGET (grid));
	g_object_unref (grid);
	lrg_dialog_add_button (dialog, "Close", LRG_DIALOG_RESPONSE_CLOSE, TRUE);
	g_set_weak_pointer (&self->import_dialog, dialog);
	lrg_dialog_present (dialog, LRG_CANVAS (root));
	g_object_unref (dialog);
	return TRUE;
}
