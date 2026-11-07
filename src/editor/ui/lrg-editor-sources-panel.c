/* lrg-editor-sources-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSourcesPanel: the asset source browser on the retained UI
 * toolkit.
 *
 *   +------------------------------------------------------------------+
 *   | [source v] [Up]  WoW Client / World / Azeroth                     |  row 1
 *   | [filter (Enter: search all)........................] [Search]     |  row 2
 *   +----------------------------------------+-------------------------+
 *   | Name               Type       Size     |  +-------------------+  |
 *   | Elwynn/            Folder              |  |    thumbnail      |  |
 *   | tree01.m2          Model      48.2 kB  |  +-------------------+  |
 *   | grass.blp          Texture    16.0 kB  |  tree01.m2              |
 *   |                                        |  Model, 48.2 kB         |
 *   |                                        |  [Import][Place][Export]|
 *   +----------------------------------------+-------------------------+
 *   | 3 items                                                          |  footer
 *   +------------------------------------------------------------------+
 *
 * State: the current source (by id), the virtual folder, the name
 * filter and, while a search is shown, its pattern. The unfiltered
 * listing (or the search results) lives in `all`; the GListStore the
 * table shows is `all` filtered by the name filter, so the display order
 * of the public API is the store order (folders first, then names).
 *
 * The panel follows the app's source registry by comparing the app's
 * serial number in step() and at every public entry point, like the
 * Assets panel follows the open project.
 *
 * Selecting an entry schedules its preview for the next step(): the
 * export (lrg_editor_app_export_source_entry() without a destination,
 * i.e. into the source's cache) and a thumbnail request on the exported
 * file. The one-frame delay lets the "Exporting..." line paint before a
 * slow first export blocks the UI thread; cached exports are instant.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-sources-panel.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-app-private.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-table.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-types.h"
#include <string.h>

/* Preview column width bounds and the thumbnail box height */
#define PREVIEW_MIN_W   (180.0f)
#define PREVIEW_MAX_W   (280.0f)
#define THUMB_H         (128.0f)

/* Below this panel width the preview goes under the list */
#define SIDE_BY_SIDE_W  (440.0f)

/* Search results the panel asks a source for */
#define SEARCH_LIMIT    (500)

/* List columns */
enum
{
	COL_NAME,
	COL_TYPE,
	COL_SIZE
};

struct _LrgEditorSourcesPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;            /* weak */
	guint                 serial;         /* app->sources_serial last synced */

	/* Model */
	gchar                *source_id;      /* nullable: no source registered */
	gconstpointer         source_obj;     /* the object listed (never dereferenced): a
	                                       * source replaced under the same id is re-listed */
	gchar                *folder;         /* "" = root */
	gchar                *filter;         /* "" = none */
	gboolean              searching;
	gchar                *pattern;        /* the search shown, when searching */
	GPtrArray            *all;            /* LrgAssetSourceEntry*: listing or results */
	GListStore           *store;          /* `all` filtered, what the table shows */
	gchar                *selected;       /* selected virtual path, nullable */
	gboolean              syncing;        /* widgets are being set from the model */

	/* Preview */
	gboolean              preview_pending;
	gchar                *preview_file;   /* cached export of the selection */
	gchar                *preview_error;

	/* Widgets */
	LrgWidget            *hint;           /* no source registered */
	LrgWidget            *row1;
	LrgWidget            *row2;
	LrgComboBox          *combo;
	LrgWidget            *up;
	LrgLabel             *path_label;
	LrgTextInput         *filter_input;
	LrgWidget            *search_chip;
	LrgTable             *table;
	LrgWidget            *preview;        /* LrgVBox */
	LrgWidget            *thumb;          /* LrgCustomWidget */
	LrgLabel             *name_label;
	LrgLabel             *info_label;
	LrgLabel             *state_label;
	LrgWidget            *actions_row;
	LrgWidget            *import_chip;
	LrgWidget            *place_chip;
	LrgWidget            *export_chip;
	LrgLabel             *count_label;
};

G_DEFINE_FINAL_TYPE (LrgEditorSourcesPanel, lrg_editor_sources_panel, LRG_TYPE_CONTAINER)

static void sync_sources   (LrgEditorSourcesPanel *self);
static void rebuild_store  (LrgEditorSourcesPanel *self);
static void sync_labels    (LrgEditorSourcesPanel *self);
static void clear_selection (LrgEditorSourcesPanel *self);

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgAssetSource *
current_source (LrgEditorSourcesPanel *self)
{
	if (self->app == NULL || self->source_id == NULL)
		return NULL;
	return lrg_editor_app_lookup_asset_source (self->app, self->source_id);
}

static void
set_status (LrgEditorSourcesPanel *self,
            const gchar           *text)
{
	if (self->app != NULL)
		editor_app_set_status (self->app, "%s", text);
}

/* An error toast (also logged by the app). */
static void
notify_error (LrgEditorSourcesPanel *self,
              const gchar           *what,
              const GError          *error)
{
	if (self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "%s: %s", what,
		                   error != NULL ? error->message : "failed");
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

/* Folders first, then names (case-folded collation). */
static gint
compare_entries (gconstpointer a,
                 gconstpointer b)
{
	LrgAssetSourceEntry *x = *(LrgAssetSourceEntry * const *) a;
	LrgAssetSourceEntry *y = *(LrgAssetSourceEntry * const *) b;
	gboolean             fx = lrg_asset_source_entry_is_folder (x);
	gboolean             fy = lrg_asset_source_entry_is_folder (y);

	if (fx != fy)
		return fx ? -1 : 1;
	return g_utf8_collate (lrg_asset_source_entry_get_name (x), lrg_asset_source_entry_get_name (y));
}

/* Case-insensitive substring match of the name filter. */
static gboolean
entry_visible (LrgEditorSourcesPanel *self,
               LrgAssetSourceEntry   *entry)
{
	g_autofree gchar *name = NULL;
	g_autofree gchar *needle = NULL;

	if (self->filter == NULL || *self->filter == '\0')
		return TRUE;
	name = g_utf8_casefold (lrg_asset_source_entry_get_name (entry), -1);
	needle = g_utf8_casefold (self->filter, -1);
	return strstr (name, needle) != NULL;
}

static LrgAssetSourceEntry *
store_entry (LrgEditorSourcesPanel *self,
             guint                  index)
{
	LrgAssetSourceEntry *e = g_list_model_get_item (G_LIST_MODEL (self->store), index);

	if (e != NULL)
		g_object_unref (e);   /* the store keeps it alive */
	return e;
}

/* ==========================================================================
 * Model -> widgets
 * ========================================================================== */

/* Fills the store from `all` through the filter and restores the
 * selection by path. */
static void
rebuild_store (LrgEditorSourcesPanel *self)
{
	guint i;
	gint  sel = -1;

	self->syncing = TRUE;
	g_list_store_remove_all (self->store);
	for (i = 0; i < self->all->len; i++)
	{
		LrgAssetSourceEntry *e = g_ptr_array_index (self->all, i);

		if (!entry_visible (self, e))
			continue;
		if (self->selected != NULL && g_strcmp0 (lrg_asset_source_entry_get_path (e), self->selected) == 0)
			sel = (gint) g_list_model_get_n_items (G_LIST_MODEL (self->store));
		g_list_store_append (self->store, e);
	}
	lrg_table_unselect_all (self->table);
	if (sel >= 0)
		lrg_table_select (self->table, (guint) sel);
	else
		clear_selection (self);   /* filtered out: its preview goes too */
	self->syncing = FALSE;
	sync_labels (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* Replaces `all` with @entries (sorted). */
static void
take_entries (LrgEditorSourcesPanel *self,
              GPtrArray             *entries)
{
	guint i;

	g_ptr_array_set_size (self->all, 0);
	for (i = 0; entries != NULL && i < entries->len; i++)
		g_ptr_array_add (self->all, g_object_ref (g_ptr_array_index (entries, i)));
	g_ptr_array_sort (self->all, compare_entries);
}

/* Path line, footer, preview text and button states. */
static void
sync_labels (LrgEditorSourcesPanel *self)
{
	LrgAssetSource      *source = current_source (self);
	LrgAssetSourceEntry *sel = lrg_editor_sources_panel_get_selected (self);
	guint                shown = g_list_model_get_n_items (G_LIST_MODEL (self->store));
	g_autofree gchar    *path = NULL;
	g_autofree gchar    *count = NULL;
	gboolean             asset = (sel != NULL && !lrg_asset_source_entry_is_folder (sel));

	/* Path: "Title / a / b", or the search */
	if (source == NULL)
		path = g_strdup ("");
	else if (self->searching)
		path = g_strdup_printf ("%s: search \"%s\"", lrg_asset_source_get_title (source), self->pattern);
	else if (*self->folder == '\0')
		path = g_strdup (lrg_asset_source_get_title (source));
	else
	{
		g_auto(GStrv) parts = g_strsplit (self->folder, "/", -1);
		g_autofree gchar *joined = g_strjoinv (" / ", parts);

		path = g_strdup_printf ("%s / %s", lrg_asset_source_get_title (source), joined);
	}
	lrg_label_set_text (self->path_label, path);

	/* Footer */
	if (self->searching)
		count = g_strdup_printf ("%u result%s", shown, shown == 1 ? "" : "s");
	else if (shown != self->all->len)
		count = g_strdup_printf ("%u of %u items", shown, self->all->len);
	else
		count = g_strdup_printf ("%u item%s", shown, shown == 1 ? "" : "s");
	lrg_label_set_text (self->count_label, count);

	/* Preview text */
	if (sel == NULL)
	{
		lrg_label_set_text (self->name_label, "No selection");
		lrg_label_set_text (self->info_label, "");
		lrg_label_set_text (self->state_label, "");
	}
	else
	{
		g_autofree gchar *size = (lrg_asset_source_entry_get_size (sel) > 0)
		                         ? g_format_size (lrg_asset_source_entry_get_size (sel)) : NULL;
		g_autofree gchar *info = NULL;
		const gchar      *desc = lrg_asset_source_entry_get_description (sel);
		const gchar      *type = editor_entry_type_name (lrg_asset_source_entry_get_asset_type (sel),
		                                                 lrg_asset_source_entry_is_folder (sel));

		info = g_strdup_printf ("%s%s%s%s%s", type, size != NULL ? ", " : "", size != NULL ? size : "",
		                        desc != NULL ? " - " : "", desc != NULL ? desc : "");
		lrg_label_set_text (self->name_label, lrg_asset_source_entry_get_name (sel));
		lrg_label_set_text (self->info_label, info);
		if (!asset)
			lrg_label_set_text (self->state_label, "Double-click to open");
		else if (self->preview_pending)
			lrg_label_set_text (self->state_label, "Exporting...");
		else if (self->preview_error != NULL)
			lrg_label_set_text (self->state_label, self->preview_error);
		else if (self->preview_file != NULL)
		{
			g_autofree gchar *base = g_path_get_basename (self->preview_file);
			g_autofree gchar *line = g_strdup_printf ("Cached: %s", base);

			lrg_label_set_text (self->state_label, line);
		}
		else
			lrg_label_set_text (self->state_label, "");
	}
	lrg_widget_set_enabled (self->import_chip, asset);
	lrg_widget_set_enabled (self->place_chip, asset);
	lrg_widget_set_enabled (self->export_chip, asset);
	lrg_widget_set_enabled (self->up, source != NULL && (self->searching || *self->folder != '\0'));
}

/* Lists the current folder of the current source into `all`. */
static gboolean
list_folder (LrgEditorSourcesPanel  *self,
             const gchar            *folder,
             GError                **error)
{
	LrgAssetSource      *source = current_source (self);
	g_autoptr(GPtrArray) entries = NULL;
	g_autofree gchar    *norm = NULL;

	if (source == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No asset source");
		return FALSE;
	}
	norm = lrg_asset_source_normalize_path (folder, error);
	if (norm == NULL)
		return FALSE;
	entries = lrg_asset_source_list (source, norm, error);
	if (entries == NULL)
		return FALSE;

	g_free (self->folder);
	self->folder = g_steal_pointer (&norm);
	self->searching = FALSE;
	g_clear_pointer (&self->pattern, g_free);
	take_entries (self, entries);
	return TRUE;
}

/* Clears the filter field and model without re-filtering. */
static void
clear_filter (LrgEditorSourcesPanel *self)
{
	g_free (self->filter);
	self->filter = g_strdup ("");
	self->syncing = TRUE;
	lrg_text_input_set_text (self->filter_input, "");
	self->syncing = FALSE;
}

/* Forgets the selection and its preview. */
static void
clear_selection (LrgEditorSourcesPanel *self)
{
	g_clear_pointer (&self->selected, g_free);
	g_clear_pointer (&self->preview_file, g_free);
	g_clear_pointer (&self->preview_error, g_free);
	self->preview_pending = FALSE;
}

/* Shows the root folder of the current source (or nothing). */
static void
show_root (LrgEditorSourcesPanel *self)
{
	g_autoptr(GError) error = NULL;

	clear_selection (self);
	clear_filter (self);
	g_free (self->folder);
	self->folder = g_strdup ("");
	self->searching = FALSE;
	g_clear_pointer (&self->pattern, g_free);
	g_ptr_array_set_size (self->all, 0);
	self->source_obj = current_source (self);
	if (current_source (self) != NULL && !list_folder (self, "", &error))
		notify_error (self, "Cannot list the source", error);
	rebuild_store (self);
}

/* Follows the app's registry: chooser items, current source, hint. */
static void
sync_sources (LrgEditorSourcesPanel *self)
{
	g_autoptr(GPtrArray) sources = NULL;
	gint                 current = -1;
	guint                i;

	if (self->app == NULL || self->serial == self->app->sources_serial)
		return;
	self->serial = self->app->sources_serial;
	sources = lrg_editor_app_get_asset_sources (self->app);

	/* Rebuild the chooser, find the current source in it */
	self->syncing = TRUE;
	lrg_combo_box_remove_all (self->combo);
	for (i = 0; i < sources->len; i++)
	{
		LrgAssetSource *s = g_ptr_array_index (sources, i);

		lrg_combo_box_append (self->combo, lrg_asset_source_get_title (s));
		if (g_strcmp0 (lrg_asset_source_get_id (s), self->source_id) == 0)
			current = (gint) i;
	}

	/* The current source went away (or there was none): take the first;
	 * a different object under the same id starts over at its root */
	if (current >= 0 && (gconstpointer) g_ptr_array_index (sources, (guint) current) != self->source_obj)
	{
		self->syncing = FALSE;
		show_root (self);
		self->syncing = TRUE;
	}
	if (current < 0)
	{
		g_free (self->source_id);
		self->source_id = (sources->len > 0) ? g_strdup (lrg_asset_source_get_id (g_ptr_array_index (sources, 0)))
		                                     : NULL;
		current = (sources->len > 0) ? 0 : -1;
		self->syncing = FALSE;
		show_root (self);
		self->syncing = TRUE;
	}
	if (current >= 0)
		lrg_combo_box_set_selected (self->combo, current);
	self->syncing = FALSE;

	/* Hint without sources; the chooser only with a choice to make */
	lrg_widget_set_visible (self->hint, sources->len == 0);
	lrg_widget_set_visible (self->row1, sources->len > 0);
	lrg_widget_set_visible (self->row2, sources->len > 0);
	lrg_widget_set_visible (LRG_WIDGET (self->table), sources->len > 0);
	lrg_widget_set_visible (self->preview, sources->len > 0);
	lrg_widget_set_visible (LRG_WIDGET (self->count_label), sources->len > 0);
	lrg_widget_set_visible (LRG_WIDGET (self->combo), sources->len > 1);
	lrg_widget_set_visible (self->search_chip,
	                        current_source (self) != NULL && lrg_asset_source_implements_search (current_source (self)));
	sync_labels (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * Preview
 * ========================================================================== */

static void
run_preview (LrgEditorSourcesPanel *self)
{
	LrgAssetSourceEntry *sel;
	g_autoptr(GError)    error = NULL;
	gchar               *file;

	if (!self->preview_pending)
		return;
	self->preview_pending = FALSE;
	g_clear_pointer (&self->preview_file, g_free);
	g_clear_pointer (&self->preview_error, g_free);
	sel = lrg_editor_sources_panel_get_selected (self);
	if (sel == NULL || lrg_asset_source_entry_is_folder (sel) || self->app == NULL)
	{
		sync_labels (self);
		return;
	}

	/* Export into the cache (instant when cached), then ask for its
	 * thumbnail; the thumbnail widget polls the texture when painting */
	file = lrg_editor_app_export_source_entry (self->app, self->source_id, lrg_asset_source_entry_get_path (sel),
	                                           NULL, &error);
	if (file == NULL)
	{
		g_autofree gchar *msg = g_strdup_printf ("Preview of %s failed: %s", lrg_asset_source_entry_get_name (sel),
		                                         error->message);

		self->preview_error = g_strdup (error->message);
		set_status (self, msg);
	}
	else
	{
		gboolean pending = FALSE;

		self->preview_file = file;
		editor_thumbnail_texture (self->app, file, &pending);
	}
	sync_labels (self);
}

static void
thumb_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgEditorSourcesPanel *self = LRG_EDITOR_SOURCES_PANEL (user_data);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont               *font = lrg_theme_get_ui_font (theme);
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                 radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	LrgAssetSourceEntry   *sel = lrg_editor_sources_panel_get_selected (self);
	GObject               *tex = NULL;
	gboolean               pending = FALSE;
	const gchar           *text;
	gfloat                 tw = 0.0f;

	/* The well */
	lrg_paint_list_rect (list, x, y, width, height, radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));

	if (sel != NULL && self->preview_file != NULL && self->app != NULL)
		tex = editor_thumbnail_texture (self->app, self->preview_file, &pending);
	if (tex != NULL)
	{
		/* Fit the thumbnail, aspect kept */
		gfloat iw = (gfloat) grl_texture_get_width (GRL_TEXTURE (tex));
		gfloat ih = (gfloat) grl_texture_get_height (GRL_TEXTURE (tex));
		gfloat k = MIN ((width - 8.0f) / MAX (iw, 1.0f), (height - 8.0f) / MAX (ih, 1.0f));

		lrg_paint_list_image (list, GRL_TEXTURE (tex), NULL, x + (width - iw * k) * 0.5f, y + (height - ih * k) * 0.5f,
		                      iw * k, ih * k, radius * 0.5f, NULL);
		return;
	}

	/* No picture (yet): say why, centred */
	if (sel == NULL)
		text = "";
	else if (lrg_asset_source_entry_is_folder (sel))
		text = "Folder";
	else if (self->preview_pending)
		text = "Exporting...";
	else if (pending)
		text = "Rendering...";
	else if (self->preview_error != NULL)
		text = "Export failed";
	else
		text = editor_entry_type_name (lrg_asset_source_entry_get_asset_type (sel), FALSE);
	lrg_paint_measure_text (font, text, fs, 0.0f, &tw, NULL);
	lrg_paint_list_text (list, font, text, x + (width - tw) * 0.5f, y + (height - fs) * 0.5f, fs, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
}

/* ==========================================================================
 * Widgets -> model
 * ========================================================================== */

static void
on_selection_changed (LrgWidget *table,
                      gpointer   user_data)
{
	LrgEditorSourcesPanel *self = LRG_EDITOR_SOURCES_PANEL (user_data);
	guint                  pos;
	LrgAssetSourceEntry   *e;

	if (self->syncing)
		return;
	pos = lrg_table_get_selected (self->table);
	e = (pos != LRG_TABLE_INVALID_POSITION) ? store_entry (self, pos) : NULL;
	if (e == NULL)
	{
		clear_selection (self);
		sync_labels (self);
		return;
	}
	if (g_strcmp0 (self->selected, lrg_asset_source_entry_get_path (e)) == 0)
		return;
	lrg_editor_sources_panel_select (self, lrg_asset_source_entry_get_path (e));
}

static void
on_row_activated (LrgTable *table,
                  guint     position,
                  gpointer  user_data)
{
	lrg_editor_sources_panel_activate (LRG_EDITOR_SOURCES_PANEL (user_data), position);
}

static void
on_combo_changed (LrgComboBox *combo,
                  gpointer     user_data)
{
	LrgEditorSourcesPanel *self = LRG_EDITOR_SOURCES_PANEL (user_data);
	g_autoptr(GPtrArray)   sources = NULL;
	gint                   sel;

	if (self->syncing || self->app == NULL)
		return;
	sel = lrg_combo_box_get_selected (combo);
	sources = lrg_editor_app_get_asset_sources (self->app);
	if (sel >= 0 && (guint) sel < sources->len)
		lrg_editor_sources_panel_set_source (self, lrg_asset_source_get_id (g_ptr_array_index (sources, (guint) sel)));
}

static void
on_filter_changed (LrgTextInput *input,
                   gpointer      user_data)
{
	LrgEditorSourcesPanel *self = LRG_EDITOR_SOURCES_PANEL (user_data);

	if (!self->syncing)
		lrg_editor_sources_panel_set_filter (self, lrg_text_input_get_text (input));
}

/* Enter in the filter, or the Search button: search the whole source. */
static void
search_from_filter (LrgEditorSourcesPanel *self)
{
	LrgAssetSource    *source = current_source (self);
	const gchar       *text = lrg_text_input_get_text (self->filter_input);
	g_autoptr(GError)  error = NULL;

	if (source == NULL || text == NULL || *text == '\0' || !lrg_asset_source_implements_search (source))
		return;
	if (!lrg_editor_sources_panel_search (self, text, &error))
		notify_error (self, "Search failed", error);
}

static void
on_filter_submitted (LrgTextInput *input,
                     gpointer      user_data)
{
	search_from_filter (LRG_EDITOR_SOURCES_PANEL (user_data));
}

static void
on_search (LrgWidget *chip,
           gpointer   user_data)
{
	search_from_filter (LRG_EDITOR_SOURCES_PANEL (user_data));
}

static void
on_up (LrgWidget *chip,
       gpointer   user_data)
{
	lrg_editor_sources_panel_go_up (LRG_EDITOR_SOURCES_PANEL (user_data));
}

static void
on_import (LrgWidget *chip,
           gpointer   user_data)
{
	/* Errors and success are reported by the call itself */
	g_free (lrg_editor_sources_panel_import_selected (LRG_EDITOR_SOURCES_PANEL (user_data), NULL));
}

static void
on_place (LrgWidget *chip,
          gpointer   user_data)
{
	lrg_editor_sources_panel_place_selected (LRG_EDITOR_SOURCES_PANEL (user_data), NULL);
}

/* "Export to...": the entry is remembered by source and path, because
 * the selection may change while the folder dialog is open. */
typedef struct
{
	gchar *source_id;
	gchar *path;
} ExportRequest;

static void
export_request_free (gpointer data)
{
	ExportRequest *req = data;

	g_free (req->source_id);
	g_free (req->path);
	g_free (req);
}

static void
on_export_folder (LrgEditorApp *app,
                  const gchar  *folder,
                  gpointer      data)
{
	ExportRequest     *req = data;
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *file = NULL;

	if (folder == NULL)
		return;   /* cancelled */
	file = lrg_editor_app_export_source_entry (app, req->source_id, req->path, folder, &error);
	if (file == NULL)
		editor_app_notify (app, EDITOR_NOTIFY_ERROR, "Export failed: %s", error->message);
	else
		editor_app_notify (app, EDITOR_NOTIFY_SUCCESS, "Exported %s", file);
}

static void
on_export (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorSourcesPanel *self = LRG_EDITOR_SOURCES_PANEL (user_data);
	LrgAssetSourceEntry   *sel = lrg_editor_sources_panel_get_selected (self);
	ExportRequest         *req;

	if (sel == NULL || lrg_asset_source_entry_is_folder (sel) || self->app == NULL)
		return;
	req = g_new0 (ExportRequest, 1);
	req->source_id = g_strdup (self->source_id);
	req->path = g_strdup (lrg_asset_source_entry_get_path (sel));
	editor_dialog_select_folder (self->app, "Export To Folder", NULL, on_export_folder, req, export_request_free);
}

/* ==========================================================================
 * Table cells
 * ========================================================================== */

static gchar *
cell_name (LrgTable  *table,
           LrgWidget *cell,
           GObject   *item,
           guint      position,
           gpointer   user_data)
{
	LrgAssetSourceEntry *e = LRG_ASSET_SOURCE_ENTRY (item);

	return lrg_asset_source_entry_is_folder (e) ? g_strconcat (lrg_asset_source_entry_get_name (e), "/", NULL)
	                                            : g_strdup (lrg_asset_source_entry_get_name (e));
}

static gchar *
cell_type (LrgTable  *table,
           LrgWidget *cell,
           GObject   *item,
           guint      position,
           gpointer   user_data)
{
	LrgAssetSourceEntry *e = LRG_ASSET_SOURCE_ENTRY (item);

	return g_strdup (editor_entry_type_name (lrg_asset_source_entry_get_asset_type (e),
	                                         lrg_asset_source_entry_is_folder (e)));
}

static gchar *
cell_size (LrgTable  *table,
           LrgWidget *cell,
           GObject   *item,
           guint      position,
           gpointer   user_data)
{
	LrgAssetSourceEntry *e = LRG_ASSET_SOURCE_ENTRY (item);

	if (lrg_asset_source_entry_is_folder (e) || lrg_asset_source_entry_get_size (e) == 0)
		return g_strdup ("");
	return g_format_size (lrg_asset_source_entry_get_size (e));
}

/* ==========================================================================
 * LrgWidget / LrgContainer
 * ========================================================================== */

static gboolean
lrg_editor_sources_panel_step (LrgWidget *widget,
                               gfloat     delta_time)
{
	LrgEditorSourcesPanel *self = LRG_EDITOR_SOURCES_PANEL (widget);

	sync_sources (self);
	if (self->preview_pending)
		run_preview (self);
	return FALSE;
}

/*
 * The layout: the two toolbar rows at their preferred heights, the
 * footer at the bottom, and between them the list with the preview
 * column on its right (or under it when the panel is narrow).
 */
static void
lrg_editor_sources_panel_layout_children (LrgContainer *container)
{
	LrgEditorSourcesPanel *self = LRG_EDITOR_SOURCES_PANEL (container);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat                 w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                 h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                 pad = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y));
	gfloat                 gap = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING) * 0.75f);
	gfloat                 inner = MAX (0.0f, w - pad * 2.0f);
	gfloat                 y = pad;
	gfloat                 bottom = h - pad;
	gfloat                 pw, ph, body_h;

	/* Children are added one by one in init(): lay out once all exist */
	if (self->count_label == NULL)
		return;
	editor_panel_place_column (container, NULL, self->hint, NULL);
	if (!lrg_widget_get_visible (self->row1))
		return;

	/* Toolbar rows */
	lrg_widget_get_preferred_size (self->row1, &pw, &ph);
	lrg_widget_place_in_slot (self->row1, pad, y, inner, ph);
	y += ph + gap;
	lrg_widget_get_preferred_size (self->row2, &pw, &ph);
	lrg_widget_place_in_slot (self->row2, pad, y, inner, ph);
	y += ph + gap;

	/* Footer */
	lrg_widget_get_preferred_size (LRG_WIDGET (self->count_label), &pw, &ph);
	lrg_widget_place_in_slot (LRG_WIDGET (self->count_label), pad, MAX (y, bottom - ph), inner, ph);
	bottom = MAX (y, bottom - ph - gap);
	body_h = MAX (0.0f, bottom - y);

	/* List and preview */
	if (w >= SIDE_BY_SIDE_W)
	{
		gfloat side = CLAMP (inner * 0.36f, PREVIEW_MIN_W, PREVIEW_MAX_W);

		lrg_widget_place_in_slot (LRG_WIDGET (self->table), pad, y, MAX (0.0f, inner - side - gap), body_h);
		lrg_widget_place_in_slot (self->preview, pad + inner - side, y, side, body_h);
	}
	else
	{
		lrg_widget_get_preferred_size (self->preview, &pw, &ph);
		ph = MIN (ph, body_h * 0.6f);
		lrg_widget_place_in_slot (LRG_WIDGET (self->table), pad, y, inner, MAX (0.0f, body_h - ph - gap));
		lrg_widget_place_in_slot (self->preview, pad, y + body_h - ph, inner, ph);
	}
}

static void
lrg_editor_sources_panel_dispose (GObject *object)
{
	LrgEditorSourcesPanel *self = LRG_EDITOR_SOURCES_PANEL (object);

	g_clear_weak_pointer (&self->app);
	g_clear_object (&self->store);
	G_OBJECT_CLASS (lrg_editor_sources_panel_parent_class)->dispose (object);
}

static void
lrg_editor_sources_panel_finalize (GObject *object)
{
	LrgEditorSourcesPanel *self = LRG_EDITOR_SOURCES_PANEL (object);

	g_free (self->source_id);
	g_free (self->folder);
	g_free (self->filter);
	g_free (self->pattern);
	g_free (self->selected);
	g_free (self->preview_file);
	g_free (self->preview_error);
	g_ptr_array_unref (self->all);
	G_OBJECT_CLASS (lrg_editor_sources_panel_parent_class)->finalize (object);
}

static void
lrg_editor_sources_panel_class_init (LrgEditorSourcesPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_sources_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_sources_panel_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_sources_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_sources_panel_layout_children;
}

static LrgWidget *
hrow (const gchar *name)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());

	lrg_widget_set_name (row, name);
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	return row;
}

static LrgLabel *
named_label (const gchar *name,
             gboolean     dim)
{
	LrgLabel *label = lrg_label_new ("");

	lrg_widget_set_name (LRG_WIDGET (label), name);
	if (dim)
		lrg_widget_add_class (LRG_WIDGET (label), "dim-label");
	return label;
}

static void
lrg_editor_sources_panel_init (LrgEditorSourcesPanel *self)
{
	static const LrgCustomWidgetFuncs thumb_funcs = { NULL, thumb_paint, NULL, NULL, { NULL } };
	LrgContainer                     *c = LRG_CONTAINER (self);

	self->serial = G_MAXUINT;
	self->folder = g_strdup ("");
	self->filter = g_strdup ("");
	self->all = g_ptr_array_new_with_free_func (g_object_unref);
	self->store = g_list_store_new (LRG_TYPE_ASSET_SOURCE_ENTRY);
	lrg_widget_set_name (LRG_WIDGET (self), "sources-panel");

	self->hint = editor_panel_hint_new ("sources-empty",
	                                    "No asset sources. A game module registers them "
	                                    "(lrgeditor --module, lrg_editor_app_add_asset_source)");
	add_to (LRG_WIDGET (c), self->hint);

	/* Row 1: source chooser, Up, path */
	self->row1 = hrow ("sources-row-path");
	self->combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->combo), "sources-source");
	lrg_widget_set_min_size (LRG_WIDGET (self->combo), 140.0f, 0.0f);
	g_signal_connect (self->combo, "changed", G_CALLBACK (on_combo_changed), self);
	add_to (self->row1, LRG_WIDGET (self->combo));
	self->up = editor_chip_new ("sources-up", LRG_ICON_CHEVRON_UP, "Up", on_up, self);
	add_to (self->row1, self->up);
	self->path_label = named_label ("sources-path", FALSE);
	lrg_widget_set_valign (LRG_WIDGET (self->path_label), LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->path_label), 1.0f);
	add_to (self->row1, LRG_WIDGET (self->path_label));
	lrg_container_add_child (c, self->row1);
	g_object_unref (self->row1);

	/* Row 2: filter, Search */
	self->row2 = hrow ("sources-row-filter");
	self->filter_input = lrg_text_input_new_with_placeholder ("Filter this folder (Enter: search the source)");
	lrg_widget_set_name (LRG_WIDGET (self->filter_input), "sources-filter");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->filter_input), 1.0f);
	lrg_widget_set_min_size (LRG_WIDGET (self->filter_input), 100.0f, 0.0f);
	g_signal_connect (self->filter_input, "text-changed", G_CALLBACK (on_filter_changed), self);
	g_signal_connect (self->filter_input, "submitted", G_CALLBACK (on_filter_submitted), self);
	add_to (self->row2, LRG_WIDGET (self->filter_input));
	self->search_chip = editor_chip_new ("sources-search", LRG_ICON_SEARCH, "Search", on_search, self);
	add_to (self->row2, self->search_chip);
	lrg_container_add_child (c, self->row2);
	g_object_unref (self->row2);

	/* The list */
	self->table = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->table), "sources-list");
	lrg_table_set_selection_mode (self->table, LRG_SELECTION_MODE_SINGLE);
	lrg_table_add_column (self->table, "Name", cell_name, NULL, NULL, 220.0f,
	                      LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_EXPAND);
	lrg_table_add_column (self->table, "Type", cell_type, NULL, NULL, 80.0f, LRG_TABLE_COLUMN_RESIZABLE);
	lrg_table_add_column (self->table, "Size", cell_size, NULL, NULL, 80.0f, LRG_TABLE_COLUMN_RESIZABLE);
	lrg_table_set_model (self->table, G_LIST_MODEL (self->store));
	g_signal_connect (self->table, "selection-changed", G_CALLBACK (on_selection_changed), self);
	g_signal_connect (self->table, "row-activated", G_CALLBACK (on_row_activated), self);
	lrg_container_add_child (c, LRG_WIDGET (self->table));
	g_object_unref (self->table);

	/* The preview column */
	self->preview = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->preview, "sources-preview");
	lrg_container_set_spacing (LRG_CONTAINER (self->preview), 4.0f);
	self->thumb = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->thumb, "sources-thumb");
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->thumb), THUMB_H);
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (self->thumb), PREVIEW_MIN_W);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->thumb), &thumb_funcs, self, NULL);
	add_to (self->preview, self->thumb);
	self->name_label = named_label ("sources-name", FALSE);
	add_to (self->preview, LRG_WIDGET (self->name_label));
	self->info_label = named_label ("sources-info", TRUE);
	add_to (self->preview, LRG_WIDGET (self->info_label));
	self->state_label = named_label ("sources-state", TRUE);
	add_to (self->preview, LRG_WIDGET (self->state_label));
	self->actions_row = hrow ("sources-actions");
	self->import_chip = editor_chip_new ("sources-import", LRG_ICON_DOWNLOAD, "Import", on_import, self);
	add_to (self->actions_row, self->import_chip);
	self->place_chip = editor_chip_new ("sources-place", LRG_ICON_PLUS, "Place", on_place, self);
	add_to (self->actions_row, self->place_chip);
	self->export_chip = editor_chip_new ("sources-export", LRG_ICON_UPLOAD, "Export to...", on_export, self);
	add_to (self->actions_row, self->export_chip);
	add_to (self->preview, self->actions_row);
	lrg_container_add_child (c, self->preview);
	g_object_unref (self->preview);

	/* Footer */
	self->count_label = named_label ("sources-count", TRUE);
	lrg_container_add_child (c, LRG_WIDGET (self->count_label));
	g_object_unref (self->count_label);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorSourcesPanel *
lrg_editor_sources_panel_new (LrgEditorApp *app)
{
	LrgEditorSourcesPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_SOURCES_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	sync_sources (self);
	return self;
}

LrgAssetSource *
lrg_editor_sources_panel_get_source (LrgEditorSourcesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), NULL);
	sync_sources (self);
	return current_source (self);
}

gboolean
lrg_editor_sources_panel_set_source (LrgEditorSourcesPanel *self,
                                     const gchar           *id)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), FALSE);

	sync_sources (self);
	if (self->app == NULL || id == NULL || lrg_editor_app_lookup_asset_source (self->app, id) == NULL)
		return FALSE;
	if (g_strcmp0 (id, self->source_id) != 0)
	{
		g_autoptr(GPtrArray) sources = lrg_editor_app_get_asset_sources (self->app);
		guint                i;

		g_free (self->source_id);
		self->source_id = g_strdup (id);
		show_root (self);

		/* Chooser and Search button follow */
		self->syncing = TRUE;
		for (i = 0; i < sources->len; i++)
			if (g_strcmp0 (lrg_asset_source_get_id (g_ptr_array_index (sources, i)), id) == 0)
				lrg_combo_box_set_selected (self->combo, (gint) i);
		self->syncing = FALSE;
		lrg_widget_set_visible (self->search_chip, lrg_asset_source_implements_search (current_source (self)));
		sync_labels (self);
	}
	return TRUE;
}

const gchar *
lrg_editor_sources_panel_get_folder (LrgEditorSourcesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), "");
	sync_sources (self);
	return self->folder;
}

gboolean
lrg_editor_sources_panel_set_folder (LrgEditorSourcesPanel  *self,
                                     const gchar            *folder,
                                     GError                **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), FALSE);

	sync_sources (self);
	if (!list_folder (self, folder, error))
		return FALSE;
	clear_selection (self);
	clear_filter (self);
	rebuild_store (self);
	return TRUE;
}

void
lrg_editor_sources_panel_go_up (LrgEditorSourcesPanel *self)
{
	g_autofree gchar  *parent = NULL;
	g_autoptr(GError)  error = NULL;
	gchar             *slash;

	g_return_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self));

	sync_sources (self);
	if (current_source (self) == NULL)
		return;

	/* From search results back to the folder the search started in */
	if (self->searching)
		parent = g_strdup (self->folder);
	else if (*self->folder == '\0')
		return;
	else
	{
		parent = g_strdup (self->folder);
		slash = strrchr (parent, '/');
		if (slash != NULL)
			*slash = '\0';
		else
			*parent = '\0';
	}
	if (!lrg_editor_sources_panel_set_folder (self, parent, &error))
		notify_error (self, "Cannot list the folder", error);
}

const gchar *
lrg_editor_sources_panel_get_filter (LrgEditorSourcesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), "");
	return self->filter;
}

void
lrg_editor_sources_panel_set_filter (LrgEditorSourcesPanel *self,
                                     const gchar           *filter)
{
	g_return_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self));

	sync_sources (self);
	if (filter == NULL)
		filter = "";
	if (g_strcmp0 (filter, self->filter) == 0)
		return;
	g_free (self->filter);
	self->filter = g_strdup (filter);
	if (g_strcmp0 (lrg_text_input_get_text (self->filter_input), filter) != 0)
	{
		self->syncing = TRUE;
		lrg_text_input_set_text (self->filter_input, filter);
		self->syncing = FALSE;
	}
	rebuild_store (self);
}

gboolean
lrg_editor_sources_panel_search (LrgEditorSourcesPanel  *self,
                                 const gchar            *pattern,
                                 GError                **error)
{
	LrgAssetSource      *source;
	g_autoptr(GPtrArray) results = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), FALSE);
	g_return_val_if_fail (pattern != NULL, FALSE);

	sync_sources (self);
	source = current_source (self);
	if (source == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No asset source");
		return FALSE;
	}
	if (!lrg_asset_source_implements_search (source))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Asset source '%s' has no search",
		             lrg_asset_source_get_id (source));
		return FALSE;
	}
	results = lrg_asset_source_search (source, pattern, SEARCH_LIMIT, error);
	if (results == NULL)
		return FALSE;

	/* Results replace the listing; the filter field keeps the pattern
	 * but no longer filters (the results already match it) */
	self->searching = TRUE;
	g_free (self->pattern);
	self->pattern = g_strdup (pattern);
	take_entries (self, results);
	clear_selection (self);
	g_free (self->filter);
	self->filter = g_strdup ("");
	rebuild_store (self);
	return TRUE;
}

gboolean
lrg_editor_sources_panel_is_searching (LrgEditorSourcesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), FALSE);
	return self->searching;
}

void
lrg_editor_sources_panel_refresh (LrgEditorSourcesPanel *self)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *pattern = NULL;
	g_autofree gchar *folder = NULL;
	gboolean          ok;

	g_return_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self));

	sync_sources (self);
	if (current_source (self) == NULL)
		return;
	if (self->searching)
	{
		LrgAssetSource      *source = current_source (self);
		g_autoptr(GPtrArray) results = NULL;

		pattern = g_strdup (self->pattern);
		results = lrg_asset_source_search (source, pattern, SEARCH_LIMIT, &error);
		ok = (results != NULL);
		if (ok)
			take_entries (self, results);
	}
	else
	{
		folder = g_strdup (self->folder);
		ok = list_folder (self, folder, &error);
	}
	if (!ok)
		notify_error (self, "Refresh failed", error);
	rebuild_store (self);
}

guint
lrg_editor_sources_panel_get_n_entries (LrgEditorSourcesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), 0);
	sync_sources (self);
	return g_list_model_get_n_items (G_LIST_MODEL (self->store));
}

LrgAssetSourceEntry *
lrg_editor_sources_panel_get_entry (LrgEditorSourcesPanel *self,
                                    guint                  index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), NULL);
	sync_sources (self);
	return store_entry (self, index);
}

gint
lrg_editor_sources_panel_find_entry (LrgEditorSourcesPanel *self,
                                     const gchar           *path)
{
	guint n, i;

	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), -1);

	sync_sources (self);
	n = g_list_model_get_n_items (G_LIST_MODEL (self->store));
	for (i = 0; path != NULL && i < n; i++)
		if (g_strcmp0 (lrg_asset_source_entry_get_path (store_entry (self, i)), path) == 0)
			return (gint) i;
	return -1;
}

gboolean
lrg_editor_sources_panel_select (LrgEditorSourcesPanel *self,
                                 const gchar           *path)
{
	gint at;

	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), FALSE);

	sync_sources (self);
	if (path == NULL)
	{
		clear_selection (self);
		self->syncing = TRUE;
		lrg_table_unselect_all (self->table);
		self->syncing = FALSE;
		sync_labels (self);
		return TRUE;
	}
	at = lrg_editor_sources_panel_find_entry (self, path);
	if (at < 0)
		return FALSE;

	/* New selection: drop the old preview, schedule the new one */
	if (g_strcmp0 (self->selected, path) != 0)
	{
		clear_selection (self);
		self->selected = g_strdup (path);
		self->preview_pending = !lrg_asset_source_entry_is_folder (store_entry (self, (guint) at));
	}
	if (!lrg_table_is_selected (self->table, (guint) at))
	{
		self->syncing = TRUE;
		lrg_table_unselect_all (self->table);
		lrg_table_select (self->table, (guint) at);
		self->syncing = FALSE;
	}
	sync_labels (self);
	return TRUE;
}

LrgAssetSourceEntry *
lrg_editor_sources_panel_get_selected (LrgEditorSourcesPanel *self)
{
	gint at;

	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), NULL);

	if (self->selected == NULL)
		return NULL;
	at = lrg_editor_sources_panel_find_entry (self, self->selected);
	return (at >= 0) ? store_entry (self, (guint) at) : NULL;
}

gboolean
lrg_editor_sources_panel_activate (LrgEditorSourcesPanel *self,
                                   guint                  index)
{
	LrgAssetSourceEntry *e;
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), FALSE);

	sync_sources (self);
	e = store_entry (self, index);
	if (e == NULL)
		return FALSE;
	path = g_strdup (lrg_asset_source_entry_get_path (e));
	if (lrg_asset_source_entry_is_folder (e))
	{
		if (!lrg_editor_sources_panel_set_folder (self, path, &error))
			notify_error (self, "Cannot open the folder", error);
		return TRUE;
	}
	lrg_editor_sources_panel_select (self, path);
	return TRUE;
}

void
lrg_editor_sources_panel_update_preview (LrgEditorSourcesPanel *self)
{
	g_return_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self));
	sync_sources (self);
	run_preview (self);
}

const gchar *
lrg_editor_sources_panel_get_preview_file (LrgEditorSourcesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), NULL);
	return self->preview_file;
}

/* The selected asset (not a folder), or NULL with an error. */
static LrgAssetSourceEntry *
require_asset (LrgEditorSourcesPanel  *self,
               GError                **error)
{
	LrgAssetSourceEntry *sel = lrg_editor_sources_panel_get_selected (self);

	if (sel == NULL || lrg_asset_source_entry_is_folder (sel) || self->app == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Select an asset first");
		return NULL;
	}
	return sel;
}

gchar *
lrg_editor_sources_panel_import_selected (LrgEditorSourcesPanel  *self,
                                          GError                **error)
{
	LrgAssetSourceEntry *sel;
	g_autoptr(GError)    local = NULL;
	gchar               *rel;

	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), NULL);

	sync_sources (self);
	sel = require_asset (self, &local);
	rel = (sel != NULL)
	      ? lrg_editor_app_import_source_entry (self->app, self->source_id, lrg_asset_source_entry_get_path (sel),
	                                            &local)
	      : NULL;
	if (rel == NULL)
	{
		/* No project: a status line says what to do; anything else toasts */
		if (g_error_matches (local, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED))
			set_status (self, local->message);
		else
			notify_error (self, "Import failed", local);
		g_propagate_error (error, g_steal_pointer (&local));
		return NULL;
	}
	{
		g_autofree gchar *msg = g_strdup_printf ("Imported %s", rel);

		set_status (self, msg);
	}
	return rel;
}

LrgNode *
lrg_editor_sources_panel_place_selected (LrgEditorSourcesPanel  *self,
                                         GError                **error)
{
	LrgAssetSourceEntry *sel;
	g_autoptr(GError)    local = NULL;
	LrgNode             *node;

	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), NULL);

	sync_sources (self);
	sel = require_asset (self, &local);
	node = (sel != NULL)
	       ? lrg_editor_app_place_source_entry (self->app, self->source_id, lrg_asset_source_entry_get_path (sel),
	                                            &local)
	       : NULL;
	if (node == NULL)
	{
		if (g_error_matches (local, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED))
			set_status (self, local->message);
		else
			notify_error (self, "Place failed", local);
		g_propagate_error (error, g_steal_pointer (&local));
		return NULL;
	}
	{
		g_autofree gchar *msg = g_strdup_printf ("Placed %s", lrg_node_get_name (node));

		set_status (self, msg);
	}
	return node;
}

gchar *
lrg_editor_sources_panel_export_selected (LrgEditorSourcesPanel  *self,
                                          const gchar            *dest_dir,
                                          GError                **error)
{
	LrgAssetSourceEntry *sel;

	g_return_val_if_fail (LRG_IS_EDITOR_SOURCES_PANEL (self), NULL);
	g_return_val_if_fail (dest_dir != NULL, NULL);

	sync_sources (self);
	sel = require_asset (self, error);
	if (sel == NULL)
		return NULL;
	return lrg_editor_app_export_source_entry (self->app, self->source_id, lrg_asset_source_entry_get_path (sel),
	                                           dest_dir, error);
}
