/* lrg-editor-sound-bank-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSoundBankEditor: the sound bank data editor (see the header).
 *
 * Layout: a horizontal splitter with the sound table on the left (Name,
 * File, and a status column: a check when the file resolves, a cross when
 * it is missing) and a property grid on the right. The grid always shows
 * the bank fields (name, base_path) and, when a sound is selected, that
 * sound's name and file. The file row is an "asset" reference, so audio
 * files dragged from the assets panel can be dropped on it; dropping on
 * the table adds a new sound instead.
 *
 * Paths. The assets panel drags project-relative paths. Sounds are stored
 * relative to the bank's sound folder (the manifest's folder joined with
 * base_path, as LrgSoundBank resolves them), so a dropped path is made
 * absolute against the project and then relative to that folder (with
 * "../" steps when it lies outside).
 *
 * Preview opens the default audio device on first use and plays the file
 * with a GrlSound kept until the next preview.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-sound-bank-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../../ui/lrg-table.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-drag.h"
#include "../../../ui/lrg-drop-target.h"
#include "../../../paint/lrg-paint-list.h"
#include <graylib.h>
#include <string.h>

#define ASSET_MIME "application/x-lrg-asset"

/* ==========================================================================
 * Rows
 * ========================================================================== */

#define SOUND_TYPE_ROW (sound_row_get_type ())
G_DECLARE_FINAL_TYPE (SoundRow, sound_row, SOUND, ROW, GObject)

struct _SoundRow
{
	GObject  parent_instance;
	gchar   *name;
};

G_DEFINE_FINAL_TYPE (SoundRow, sound_row, G_TYPE_OBJECT)

static void
sound_row_finalize (GObject *object)
{
	g_free (SOUND_ROW (object)->name);
	G_OBJECT_CLASS (sound_row_parent_class)->finalize (object);
}

static void
sound_row_class_init (SoundRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = sound_row_finalize;
}

static void
sound_row_init (SoundRow *self)
{
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

struct _LrgEditorSoundBankEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgTable            *table;
	GListStore          *rows;
	LrgPropertyGrid     *grid;
	LrgEditorDataSource *source;
	gchar               *selected;
	gchar               *grid_sig;
	gboolean             grid_stale;
	gboolean             syncing;
	GrlSound            *preview;
};

G_DEFINE_FINAL_TYPE (LrgEditorSoundBankEditor, lrg_editor_sound_bank_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_grid (LrgEditorSoundBankEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorSoundBankEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static gchar *
sound_path (const gchar *name)
{
	return lrg_editor_document_path_join ("/sounds", name);
}

static gboolean
has_sound (LrgEditorSoundBankEditor *self,
           const gchar              *name)
{
	g_autofree gchar *p = NULL;

	if (name == NULL)
		return FALSE;
	p = sound_path (name);
	return lrg_editor_document_has_path (doc_of (self), p);
}

/* The folder sounds resolve against: manifest dir + base_path. */
static gchar *
sound_folder (LrgEditorSoundBankEditor *self)
{
	LrgEditorDocument *doc = doc_of (self);
	const gchar       *base = lrg_editor_document_get_scalar (doc, "/base_path");
	g_autofree gchar  *dir = NULL;

	if (lrg_editor_document_get_base_dir (doc) != NULL)
		dir = g_strdup (lrg_editor_document_get_base_dir (doc));
	else if (lrg_editor_document_get_path (doc) != NULL)
		dir = g_path_get_dirname (lrg_editor_document_get_path (doc));
	else
		dir = g_get_current_dir ();
	if (base == NULL || *base == '\0')
		return g_steal_pointer (&dir);
	if (g_path_is_absolute (base))
		return g_strdup (base);
	return g_build_filename (dir, base, NULL);
}

/* @path relative to the directory @from ("../x/y.wav" when outside). */
static gchar *
relative_to (const gchar *from,
             const gchar *path)
{
	g_autoptr(GFile) a = g_file_new_for_path (from);
	g_autoptr(GFile) b = g_file_new_for_path (path);
	g_autofree gchar *ca = g_file_get_path (a);
	g_autofree gchar *cb = g_file_get_path (b);
	g_auto(GStrv)     pa = g_strsplit (ca, G_DIR_SEPARATOR_S, -1);
	g_auto(GStrv)     pb = g_strsplit (cb, G_DIR_SEPARATOR_S, -1);
	GString          *out = g_string_new (NULL);
	guint             i = 0, j;

	while (pa[i] != NULL && pb[i] != NULL && g_strcmp0 (pa[i], pb[i]) == 0)
		i++;
	for (j = i; pa[j] != NULL; j++)
		if (*pa[j] != '\0')
			g_string_append (out, ".." G_DIR_SEPARATOR_S);
	for (j = i; pb[j] != NULL; j++)
	{
		g_string_append (out, pb[j]);
		if (pb[j + 1] != NULL)
			g_string_append_c (out, G_DIR_SEPARATOR);
	}
	return g_string_free (out, FALSE);
}

/* An asset path from a drop (project-relative or absolute) as a sound
 * file relative to the bank's folder. */
static gchar *
asset_to_file (LrgEditorSoundBankEditor *self,
               const gchar              *asset)
{
	LrgEditorApp     *app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
	g_autofree gchar *abs = NULL;
	g_autofree gchar *folder = sound_folder (self);

	if (g_path_is_absolute (asset))
		abs = g_strdup (asset);
	else if (app != NULL && lrg_editor_app_get_project_dir (app) != NULL)
		abs = g_build_filename (lrg_editor_app_get_project_dir (app), asset, NULL);
	else
		return g_strdup (asset);
	return relative_to (folder, abs);
}

/* ==========================================================================
 * Table
 * ========================================================================== */

static gchar *
name_cell_text (LrgTable  *table,
                LrgWidget *cell,
                GObject   *item,
                guint      position,
                gpointer   user_data)
{
	return g_strdup (SOUND_ROW (item)->name);
}

static gchar *
file_cell_text (LrgTable  *table,
                LrgWidget *cell,
                GObject   *item,
                guint      position,
                gpointer   user_data)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (user_data);
	g_autofree gchar         *p = sound_path (SOUND_ROW (item)->name);
	const gchar              *file = lrg_editor_document_get_scalar (doc_of (self), p);

	return g_strdup (file != NULL ? file : "");
}

typedef struct
{
	LrgEditorSoundBankEditor *editor;
	gchar                    *name;
} StatusCell;

static void
status_cell_free (gpointer data)
{
	StatusCell *c = data;

	g_free (c->name);
	g_free (c);
}

static void
status_cell_paint (LrgCustomWidget *widget,
                   LrgPaintList    *list,
                   gfloat           x,
                   gfloat           y,
                   gfloat           width,
                   gfloat           height,
                   gpointer         user_data)
{
	StatusCell       *c = user_data;
	LrgTheme         *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat            icon = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL) + 2.0f;
	g_autofree gchar *file = NULL;
	gboolean          ok;

	if (c->editor == NULL || c->name == NULL)
		return;
	file = lrg_editor_document_sound_bank_resolve (doc_of (c->editor), c->name);
	ok = file != NULL && g_file_test (file, G_FILE_TEST_IS_REGULAR);
	lrg_paint_list_icon (list, ok ? LRG_ICON_CHECK : LRG_ICON_CROSS, x + (width - icon) * 0.5f,
	                     y + (height - icon) * 0.5f, icon, 0.0f,
	                     lrg_theme_get_color (theme, ok ? LRG_THEME_COLOR_SUCCESS : LRG_THEME_COLOR_DANGER));
}

static LrgWidget *
status_cell_create (LrgTable *table,
                    guint     column,
                    gpointer  user_data)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, status_cell_paint, NULL, NULL, { NULL } };
	LrgCustomWidget *w = lrg_custom_widget_new ();
	StatusCell      *c = g_new0 (StatusCell, 1);

	c->editor = LRG_EDITOR_SOUND_BANK_EDITOR (user_data);
	lrg_custom_widget_set_funcs (w, &funcs, c, status_cell_free);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (w), TRUE);
	g_object_set_data (G_OBJECT (w), "lrg-status-cell", c);
	return LRG_WIDGET (w);
}

static gchar *
status_cell_text (LrgTable  *table,
                  LrgWidget *cell,
                  GObject   *item,
                  guint      position,
                  gpointer   user_data)
{
	StatusCell *c = (cell != NULL) ? g_object_get_data (G_OBJECT (cell), "lrg-status-cell") : NULL;

	if (c != NULL)
	{
		g_free (c->name);
		c->name = g_strdup (SOUND_ROW (item)->name);
	}
	return NULL;
}

static gint
row_of (LrgEditorSoundBankEditor *self,
        const gchar              *name)
{
	guint i, n = g_list_model_get_n_items (G_LIST_MODEL (self->rows));

	for (i = 0; name != NULL && i < n; i++)
	{
		g_autoptr(SoundRow) row = g_list_model_get_item (G_LIST_MODEL (self->rows), i);

		if (g_strcmp0 (row->name, name) == 0)
			return (gint) i;
	}
	return -1;
}

static void
rebuild_rows (LrgEditorSoundBankEditor *self)
{
	g_auto(GStrv)        names = NULL;
	g_autoptr(GPtrArray) items = g_ptr_array_new_with_free_func (g_object_unref);
	guint                i;
	gint                 sel;

	if (lrg_editor_document_get_kind (doc_of (self), "/sounds") == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		names = lrg_editor_document_get_keys (doc_of (self), "/sounds");
	for (i = 0; names != NULL && names[i] != NULL; i++)
	{
		SoundRow *row = g_object_new (SOUND_TYPE_ROW, NULL);

		row->name = g_strdup (names[i]);
		g_ptr_array_add (items, row);
	}
	self->syncing = TRUE;
	g_list_store_splice (self->rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->rows)), items->pdata,
	                     items->len);
	if (self->selected != NULL && !has_sound (self, self->selected))
		g_clear_pointer (&self->selected, g_free);
	sel = row_of (self, self->selected);
	if (sel >= 0)
		lrg_table_select (self->table, (guint) sel);
	else
		lrg_table_unselect_all (self->table);
	self->syncing = FALSE;
	lrg_widget_queue_layout (LRG_WIDGET (self->table));
}

static void
on_table_selection (LrgTable *table,
                    gpointer  user_data)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (user_data);
	guint                     pos = lrg_table_get_selected (table);
	g_autoptr(SoundRow)       row = NULL;

	if (self->syncing || pos >= g_list_model_get_n_items (G_LIST_MODEL (self->rows)))
		return;
	row = g_list_model_get_item (G_LIST_MODEL (self->rows), pos);
	if (g_strcmp0 (row->name, self->selected) != 0)
		lrg_editor_sound_bank_editor_select (self, row->name);
}

static void
on_row_activated (LrgTable *table,
                  guint     position,
                  gpointer  user_data)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;

	if (!lrg_editor_sound_bank_editor_play (self, NULL, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* --- drop on the table: new sounds ---------------------------------------- */

static gboolean
on_table_accept (LrgDropTarget  *target,
                 LrgDragPayload *payload,
                 gpointer        user_data)
{
	const GValue *v = lrg_drag_payload_get_value (payload);

	return v != NULL && G_VALUE_HOLDS_STRING (v) && g_value_get_string (v) != NULL;
}

static gboolean
on_table_drop (LrgDropTarget  *target,
               LrgDragPayload *payload,
               gfloat          x,
               gfloat          y,
               gpointer        user_data)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (user_data);
	const GValue             *v = lrg_drag_payload_get_value (payload);
	g_autoptr(GError)         error = NULL;

	if (v == NULL || !G_VALUE_HOLDS_STRING (v))
		return FALSE;
	if (lrg_editor_sound_bank_editor_add_asset (self, g_value_get_string (v), &error) == NULL)
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Grid
 * ========================================================================== */

static gboolean
source_read (LrgEditorDataSource *source,
             const gchar         *name,
             GValue              *value,
             gpointer             user_data)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (user_data);

	if (g_strcmp0 (name, "sound-name") == 0 && self->selected != NULL)
	{
		g_value_set_string (value, self->selected);
		return TRUE;
	}
	return FALSE;
}

static gboolean
source_write (LrgEditorDataSource *source,
              const gchar         *name,
              GValue              *value,
              gpointer             user_data)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;

	if (g_strcmp0 (name, "sound-name") == 0 && self->selected != NULL)
	{
		const gchar      *to = g_value_get_string (value);
		g_autofree gchar *from = g_strdup (self->selected);

		if (to == NULL || *to == '\0' || g_strcmp0 (to, from) == 0)
			return FALSE;
		g_free (self->selected);
		self->selected = g_strdup (to);
		if (!lrg_editor_document_rename_key (doc_of (self), "/sounds", from, to, &error))
		{
			g_free (self->selected);
			self->selected = g_steal_pointer (&from);
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		}
		return FALSE;
	}
	/* a dropped asset arrives project-relative */
	if (g_strcmp0 (name, "sound-file") == 0)
	{
		LrgEditorApp     *app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
		const gchar      *v = g_value_get_string (value);
		g_autofree gchar *abs = NULL;

		if (v != NULL && app != NULL && lrg_editor_app_get_project_dir (app) != NULL && !g_path_is_absolute (v))
			abs = g_build_filename (lrg_editor_app_get_project_dir (app), v, NULL);
		if (abs != NULL && g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		{
			g_autofree gchar *folder = sound_folder (self);
			g_autofree gchar *probe = g_build_filename (folder, v, NULL);

			/* only rewrite when the text is not already bank-relative */
			if (!g_file_test (probe, G_FILE_TEST_IS_REGULAR))
				g_value_take_string (value, relative_to (folder, abs));
		}
	}
	return TRUE;
}

static gchar *
grid_signature (LrgEditorSoundBankEditor *self)
{
	return g_strdup (self->selected != NULL ? self->selected : "");
}

static void
rebuild_grid (LrgEditorSoundBankEditor *self)
{
	lrg_editor_data_source_clear (self->source);
	lrg_editor_data_source_add_field (self->source, "bank-name", "/name", G_TYPE_STRING, "Name", "Bank", NULL,
	                                  NULL);
	lrg_editor_data_source_add_field (self->source, "base-path", "/base_path", G_TYPE_STRING, "Sound folder",
	                                  "Bank", NULL, NULL);
	if (self->selected != NULL)
	{
		g_autofree gchar *p = sound_path (self->selected);

		lrg_editor_data_source_add_field (self->source, "sound-name", "", G_TYPE_STRING, "Name", "Sound", NULL,
		                                  NULL);
		lrg_editor_data_source_add_field (self->source, "sound-file", p, G_TYPE_STRING, "File", "Sound", "asset",
		                                  NULL);
	}
	g_free (self->grid_sig);
	self->grid_sig = grid_signature (self);
	self->grid_stale = FALSE;
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_property_grid_rebuild (self->grid);
}

/* Lists the project's audio files for the File row's picker. */
static gchar **
reference_items (LrgPropertyGrid *grid,
                 const gchar     *name,
                 const gchar     *hint,
                 gpointer         user_data)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (user_data);
	g_autofree gchar         *folder = sound_folder (self);
	g_autoptr(GDir)           d = g_dir_open (folder, 0, NULL);
	GStrvBuilder             *b = g_strv_builder_new ();
	const gchar              *f;
	gchar                   **out;

	while (d != NULL && (f = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *lower = g_ascii_strdown (f, -1);

		if (g_str_has_suffix (lower, ".wav") || g_str_has_suffix (lower, ".ogg") ||
		    g_str_has_suffix (lower, ".mp3") || g_str_has_suffix (lower, ".flac") ||
		    g_str_has_suffix (lower, ".qoa"))
			g_strv_builder_add (b, f);
	}
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

/* ==========================================================================
 * Tools
 * ========================================================================== */

static void
on_add (LrgWidget *chip,
        gpointer   user_data)
{
	g_autoptr(GError) error = NULL;

	if (lrg_editor_sound_bank_editor_add_sound (LRG_EDITOR_SOUND_BANK_EDITOR (user_data), "sound", "sound.wav",
	                                            &error) == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (user_data), error);
}

static void
on_remove (LrgWidget *chip,
           gpointer   user_data)
{
	lrg_editor_sound_bank_editor_remove_selected (LRG_EDITOR_SOUND_BANK_EDITOR (user_data));
}

static void
on_play (LrgWidget *chip,
         gpointer   user_data)
{
	g_autoptr(GError) error = NULL;

	if (!lrg_editor_sound_bank_editor_play (LRG_EDITOR_SOUND_BANK_EDITOR (user_data), NULL, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (user_data), error);
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

static void
sound_bank_refresh (LrgEditorDataEditor *editor,
                    const gchar         *path)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (editor);
	g_autofree gchar         *sig = NULL;

	rebuild_rows (self);
	sig = grid_signature (self);
	if (g_strcmp0 (sig, self->grid_sig) != 0)
	{
		self->grid_stale = TRUE;
		return;
	}
	lrg_editor_data_source_document_changed (self->source, path);
	lrg_property_grid_refresh (self->grid);
}

static void
sound_bank_reveal (LrgEditorDataEditor *editor,
                   const gchar         *path,
                   guint                line)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (editor);
	g_auto(GStrv)             parts = lrg_editor_document_path_split (path, NULL);

	if (parts != NULL && g_strcmp0 (parts[0], "sounds") == 0 && parts[1] != NULL)
	{
		gint row;

		lrg_editor_sound_bank_editor_select (self, parts[1]);
		row = row_of (self, parts[1]);
		if (row >= 0)
			lrg_table_scroll_to (self->table, (guint) row, TRUE);
	}
	else
		lrg_editor_sound_bank_editor_select (self, NULL);
}

/* Missing files as warnings (the document checks the files too, but only
 * by extension when it cannot resolve them). */
static void
sound_bank_validate (LrgEditorDataEditor *editor,
                     GPtrArray           *issues)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (editor);
	guint                     i, n = g_list_model_get_n_items (G_LIST_MODEL (self->rows));

	for (i = 0; i < n; i++)
	{
		g_autoptr(SoundRow) row = g_list_model_get_item (G_LIST_MODEL (self->rows), i);
		g_autofree gchar   *file = lrg_editor_document_sound_bank_resolve (doc_of (self), row->name);
		g_autofree gchar   *path = sound_path (row->name);
		g_autofree gchar   *msg = NULL;
		guint               line = 0, col = 0, k;
		gboolean            reported = FALSE;

		if (file == NULL || g_file_test (file, G_FILE_TEST_IS_REGULAR))
			continue;
		for (k = 0; k < issues->len; k++)
			if (g_strcmp0 (((LrgEditorDocumentIssue *) g_ptr_array_index (issues, k))->path, path) == 0)
				reported = TRUE;
		if (reported)
			continue;
		lrg_editor_document_get_line (doc_of (self), path, &line, &col);
		msg = g_strdup_printf ("sound '%s': file not found (%s)", row->name, file);
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, line, col, msg));
	}
}

static gchar *
sound_bank_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (editor);

	return g_strdup_printf ("sounds=%u selected=%s", g_list_model_get_n_items (G_LIST_MODEL (self->rows)),
	                        self->selected != NULL ? self->selected : "-");
}

static gboolean
lrg_editor_sound_bank_editor_step (LrgWidget *widget,
                                   gfloat     delta_time)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (widget);
	gboolean                  busy = LRG_WIDGET_CLASS (lrg_editor_sound_bank_editor_parent_class)->step (widget,
	                                                                                                     delta_time);

	if (self->grid_stale)
		rebuild_grid (self);
	return busy;
}

static void
lrg_editor_sound_bank_editor_constructed (GObject *object)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (object);
	LrgEditorDataEditor      *base = LRG_EDITOR_DATA_EDITOR (object);
	static const gchar * const types[] = { ASSET_MIME, NULL };
	LrgSplitter              *split;
	LrgDropTarget            *drop;
	guint                     col;

	G_OBJECT_CLASS (lrg_editor_sound_bank_editor_parent_class)->constructed (object);

	self->rows = g_list_store_new (SOUND_TYPE_ROW);
	self->table = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->table), "sound-list");
	lrg_table_add_column (self->table, "Sound", name_cell_text, NULL, NULL, 150.0f, LRG_TABLE_COLUMN_RESIZABLE);
	lrg_table_add_column (self->table, "File", file_cell_text, self, NULL, 220.0f,
	                      LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_EXPAND);
	col = lrg_table_add_column (self->table, "", status_cell_text, NULL, NULL, 34.0f, LRG_TABLE_COLUMN_NONE);
	lrg_table_set_column_widget_factory (self->table, col, status_cell_create, self, NULL);
	lrg_table_set_striped (self->table, TRUE);
	lrg_table_set_model (self->table, G_LIST_MODEL (self->rows));
	g_signal_connect_object (self->table, "selection-changed", G_CALLBACK (on_table_selection), self, 0);
	g_signal_connect_object (self->table, "row-activated", G_CALLBACK (on_row_activated), self, 0);
	drop = lrg_drop_target_new (types, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_LINK);
	g_signal_connect_object (drop, "accept", G_CALLBACK (on_table_accept), self, 0);
	g_signal_connect_object (drop, "drop", G_CALLBACK (on_table_drop), self, 0);
	lrg_widget_add_drop_target (LRG_WIDGET (self->table), drop);

	self->source = lrg_editor_data_source_new (base);
	lrg_editor_data_source_set_read_func (self->source, source_read, self, NULL);
	lrg_editor_data_source_set_write_func (self->source, source_write, self, NULL);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "sound-grid");
	lrg_property_grid_set_label_width (self->grid, 110.0f);
	lrg_property_grid_set_reference_func (self->grid, reference_items, self, NULL);
	lrg_editor_data_source_bind_grid (self->source, self->grid);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_splitter_set_start_child (split, LRG_WIDGET (self->table));
	lrg_splitter_set_end_child (split, LRG_WIDGET (self->grid));
	lrg_splitter_set_default_fraction (split, 0.55f);
	lrg_splitter_set_fraction (split, 0.55f, FALSE);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (split));

	lrg_editor_data_editor_add_tool (base, editor_chip_new ("sound-add", LRG_ICON_PLUS, "Sound", on_add, self));
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("sound-play", LRG_ICON_PLAY, "Play", on_play, self));
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("sound-remove", LRG_ICON_TRASH, "Remove", on_remove,
	                                                        self));
	rebuild_rows (self);
	rebuild_grid (self);
}

static void
lrg_editor_sound_bank_editor_finalize (GObject *object)
{
	LrgEditorSoundBankEditor *self = LRG_EDITOR_SOUND_BANK_EDITOR (object);

	g_clear_object (&self->rows);
	g_clear_object (&self->source);
	g_clear_object (&self->preview);
	g_free (self->selected);
	g_free (self->grid_sig);
	G_OBJECT_CLASS (lrg_editor_sound_bank_editor_parent_class)->finalize (object);
}

static void
lrg_editor_sound_bank_editor_class_init (LrgEditorSoundBankEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_sound_bank_editor_constructed;
	object_class->finalize = lrg_editor_sound_bank_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_sound_bank_editor_step;
	data_class->refresh = sound_bank_refresh;
	data_class->reveal = sound_bank_reveal;
	data_class->validate = sound_bank_validate;
	data_class->get_state = sound_bank_get_state;
}

static void
lrg_editor_sound_bank_editor_init (LrgEditorSoundBankEditor *self)
{
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorSoundBankEditor *
lrg_editor_sound_bank_editor_new (LrgEditorApp      *app,
                                  LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_SOUND_BANK_EDITOR, "app", app, "document", document, NULL);
}

gboolean
lrg_editor_sound_bank_editor_select (LrgEditorSoundBankEditor *self,
                                     const gchar              *name)
{
	gint row;

	g_return_val_if_fail (LRG_IS_EDITOR_SOUND_BANK_EDITOR (self), FALSE);
	if (name != NULL && !has_sound (self, name))
		return FALSE;
	if (name != self->selected)
	{
		g_free (self->selected);
		self->selected = g_strdup (name);
	}
	row = row_of (self, name);
	self->syncing = TRUE;
	if (row >= 0)
		lrg_table_select (self->table, (guint) row);
	else
		lrg_table_unselect_all (self->table);
	self->syncing = FALSE;
	rebuild_grid (self);
	return TRUE;
}

const gchar *
lrg_editor_sound_bank_editor_get_selected (LrgEditorSoundBankEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SOUND_BANK_EDITOR (self), NULL);
	return self->selected;
}

const gchar *
lrg_editor_sound_bank_editor_add_sound (LrgEditorSoundBankEditor  *self,
                                        const gchar               *name,
                                        const gchar               *file,
                                        GError                   **error)
{
	LrgEditorDocument *doc;
	g_autofree gchar  *stem = NULL;
	g_autofree gchar  *fresh = NULL;
	g_autofree gchar  *base = NULL;
	gboolean           ok;
	guint              i;

	g_return_val_if_fail (LRG_IS_EDITOR_SOUND_BANK_EDITOR (self), NULL);
	g_return_val_if_fail (file != NULL, NULL);
	doc = doc_of (self);

	/* the name: given, else the file stem; made unique with a number */
	if (name == NULL)
	{
		const gchar *dot;

		base = g_path_get_basename (file);
		dot = strrchr (base, '.');
		stem = dot != NULL && dot != base ? g_strndup (base, (gsize) (dot - base)) : g_strdup (base);
		name = stem;
	}
	fresh = g_strdup (name);
	for (i = 2; has_sound (self, fresh); i++)
	{
		g_free (fresh);
		fresh = g_strdup_printf ("%s_%u", name, i);
	}

	lrg_editor_document_begin_group (doc, "Add sound");
	if (lrg_editor_document_get_kind (doc, "/sounds") != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		ok = lrg_editor_document_has_path (doc, "/sounds")
		     ? lrg_editor_document_replace (doc, "/sounds", "{}", error)
		     : lrg_editor_document_add_key (doc, "", "sounds", "{}", -1, error);
	else
		ok = TRUE;
	if (ok)
		ok = lrg_editor_document_add_key (doc, "/sounds", fresh, "''", -1, error);
	if (ok)
	{
		g_autofree gchar *p = sound_path (fresh);

		ok = lrg_editor_document_set_string (doc, p, file, 0, error);
	}
	lrg_editor_document_end_group (doc);
	if (!ok)
		return NULL;
	lrg_editor_sound_bank_editor_select (self, fresh);
	return self->selected;
}

const gchar *
lrg_editor_sound_bank_editor_add_asset (LrgEditorSoundBankEditor  *self,
                                        const gchar               *asset,
                                        GError                   **error)
{
	g_autofree gchar *file = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SOUND_BANK_EDITOR (self), NULL);
	g_return_val_if_fail (asset != NULL, NULL);
	file = asset_to_file (self, asset);
	return lrg_editor_sound_bank_editor_add_sound (self, NULL, file, error);
}

gboolean
lrg_editor_sound_bank_editor_remove_selected (LrgEditorSoundBankEditor *self)
{
	g_autofree gchar *p = NULL;
	g_autoptr(GError) error = NULL;
	gint              row;

	g_return_val_if_fail (LRG_IS_EDITOR_SOUND_BANK_EDITOR (self), FALSE);
	if (self->selected == NULL)
		return FALSE;
	row = row_of (self, self->selected);
	p = sound_path (self->selected);
	if (!lrg_editor_document_remove (doc_of (self), p, &error))
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	/* select the neighbour */
	if (row >= 0 && g_list_model_get_n_items (G_LIST_MODEL (self->rows)) > 0)
	{
		g_autoptr(SoundRow) next = g_list_model_get_item (G_LIST_MODEL (self->rows),
		                                                  MIN ((guint) row,
		                                                       g_list_model_get_n_items (G_LIST_MODEL (self->rows))
		                                                       - 1));

		lrg_editor_sound_bank_editor_select (self, next->name);
	}
	else
		lrg_editor_sound_bank_editor_select (self, NULL);
	return TRUE;
}

gboolean
lrg_editor_sound_bank_editor_play (LrgEditorSoundBankEditor  *self,
                                   const gchar               *name,
                                   GError                   **error)
{
	g_autofree gchar *file = NULL;
	GrlAudioDevice   *device;

	g_return_val_if_fail (LRG_IS_EDITOR_SOUND_BANK_EDITOR (self), FALSE);
	if (name == NULL)
		name = self->selected;
	if (name == NULL || !has_sound (self, name))
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		                     "No sound selected");
		return FALSE;
	}
	file = lrg_editor_document_sound_bank_resolve (doc_of (self), name);
	if (file == NULL || !g_file_test (file, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "Sound file not found: %s",
		             file != NULL ? file : name);
		return FALSE;
	}

	/* the device opens on the first preview */
	device = grl_audio_device_get_default ();
	if (device == NULL || (!grl_audio_device_is_ready (device) && !grl_audio_device_init_audio (device)))
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_IO,
		                     "No audio device available for the preview");
		return FALSE;
	}
	if (self->preview != NULL)
		grl_sound_stop (self->preview);
	g_clear_object (&self->preview);
	self->preview = grl_sound_new_from_file (file, error);
	if (self->preview == NULL)
		return FALSE;
	grl_sound_play (self->preview);
	return TRUE;
}
