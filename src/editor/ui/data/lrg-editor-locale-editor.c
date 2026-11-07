/* lrg-editor-locale-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorLocaleEditor: the locale data editor (see the header).
 *
 * Languages. The opened document is the reference (column 1). Every other
 * locale file of its folder (*.yaml / *.yml whose content is a locale) is
 * loaded as a companion LrgEditorDocument with the reference set, so the
 * document's own validation reports keys it lacks. Companions are saved
 * with the primary (Save saves every dirty language), and the dirty dot
 * covers all of them.
 *
 * Undo. Every cell write goes through one language's document; the editor
 * remembers which document each of its writes went to, so Ctrl+Z undoes
 * the last write wherever it went and Ctrl+Shift+Z redoes in the same
 * order. Edits made elsewhere to the primary (IPC data.set) fall back to
 * the primary's own stack.
 *
 * Rows. One per key of any language, the reference order first; a plural
 * entry (a mapping of forms) gives a row per form, written "key[form]".
 *
 * Grid. An LrgTable: the key column is plain text, each language column a
 * custom cell that paints the text (a danger tint and "missing" where the
 * reference has the key and the language does not) and selects the cell
 * on click. The selected cell is edited in the edit bar below the table
 * (Enter writes it; an empty text removes the entry).
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-locale-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../../i18n/lrg-csv.h"
#include "../../../ui/lrg-table.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-text-input.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-file-dialog.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../paint/lrg-paint-list.h"
#include <string.h>

/* ==========================================================================
 * Rows
 * ========================================================================== */

#define LOCALE_TYPE_KEY_ROW (locale_key_row_get_type ())
G_DECLARE_FINAL_TYPE (LocaleKeyRow, locale_key_row, LOCALE, KEY_ROW, GObject)

struct _LocaleKeyRow
{
	GObject  parent_instance;
	gchar   *key;
};

G_DEFINE_FINAL_TYPE (LocaleKeyRow, locale_key_row, G_TYPE_OBJECT)

static void
locale_key_row_finalize (GObject *object)
{
	g_free (LOCALE_KEY_ROW (object)->key);
	G_OBJECT_CLASS (locale_key_row_parent_class)->finalize (object);
}

static void
locale_key_row_class_init (LocaleKeyRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = locale_key_row_finalize;
}

static void
locale_key_row_init (LocaleKeyRow *self)
{
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

typedef struct
{
	gchar             *code;
	LrgEditorDocument *doc;      /* owned (the primary's is a second ref) */
} Language;

struct _LrgEditorLocaleEditor
{
	LrgEditorDataEditor  parent_instance;

	GPtrArray           *langs;      /* Language*, reference first */
	LrgTable            *table;
	GListStore          *rows;       /* LocaleKeyRow */
	LrgLabel            *edit_label;
	LrgTextInput        *edit_input;
	gchar               *sel_key;
	gchar               *sel_code;
	GPtrArray           *undo_order; /* LrgEditorDocument* (borrowed), newest last */
	GPtrArray           *redo_order;
	gboolean             replaying;  /* undo/redo in progress */
	gboolean             syncing;
};

G_DEFINE_FINAL_TYPE (LrgEditorLocaleEditor, lrg_editor_locale_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void
language_free (gpointer data)
{
	Language *l = data;

	g_free (l->code);
	g_clear_object (&l->doc);
	g_free (l);
}

static LrgEditorDocument *
primary (LrgEditorLocaleEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static Language *
lang_by_code (LrgEditorLocaleEditor *self,
              const gchar           *code)
{
	guint i;

	for (i = 0; code != NULL && i < self->langs->len; i++)
	{
		Language *l = g_ptr_array_index (self->langs, i);

		if (g_strcmp0 (l->code, code) == 0)
			return l;
	}
	return NULL;
}

/* The code of a locale document: its "code" key, else the file stem. */
static gchar *
doc_code (LrgEditorDocument *doc)
{
	const gchar *code = lrg_editor_document_get_scalar (doc, "/code");
	const gchar *name;
	const gchar *dot;

	if (code != NULL && *code != '\0')
		return g_strdup (code);
	name = lrg_editor_document_get_display_name (doc);
	dot = strrchr (name, '.');
	return dot != NULL ? g_strndup (name, (gsize) (dot - name)) : g_strdup (name);
}

/* "items[one]" -> ("items", "one"); "greeting" -> ("greeting", NULL) */
static void
split_key (const gchar  *key,
           gchar       **base,
           gchar       **form)
{
	const gchar *open = strrchr (key, '[');
	gsize        n = strlen (key);

	if (open != NULL && n > 2 && key[n - 1] == ']' && open != key)
	{
		*base = g_strndup (key, (gsize) (open - key));
		*form = g_strndup (open + 1, (gsize) (key + n - 1 - open - 1));
		return;
	}
	*base = g_strdup (key);
	*form = NULL;
}

/* The YAML path of a row key: /strings/<base>[/<form>] */
static gchar *
key_path (const gchar *key)
{
	g_autofree gchar *base = NULL;
	g_autofree gchar *form = NULL;
	g_autofree gchar *p = NULL;

	split_key (key, &base, &form);
	p = lrg_editor_document_path_join ("/strings", base);
	return form != NULL ? lrg_editor_document_path_join (p, form) : g_steal_pointer (&p);
}

/* Appends the row keys of @doc to @out (skipping ones already there). */
static void
collect_keys (LrgEditorDocument *doc,
              GPtrArray         *out,
              GHashTable        *seen)
{
	g_auto(GStrv) keys = lrg_editor_document_locale_keys (doc);
	guint         i, j;

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		g_autofree gchar *p = lrg_editor_document_path_join ("/strings", keys[i]);

		if (lrg_editor_document_get_kind (doc, p) == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		{
			g_auto(GStrv) forms = lrg_editor_document_get_keys (doc, p);

			for (j = 0; forms != NULL && forms[j] != NULL; j++)
			{
				gchar *k = g_strdup_printf ("%s[%s]", keys[i], forms[j]);

				if (g_hash_table_add (seen, k))
					g_ptr_array_add (out, g_strdup (k));
			}
			continue;
		}
		if (!g_hash_table_contains (seen, keys[i]))
		{
			g_hash_table_add (seen, g_strdup (keys[i]));
			g_ptr_array_add (out, g_strdup (keys[i]));
		}
	}
}

/* ==========================================================================
 * Cells
 * ========================================================================== */

typedef struct
{
	LrgEditorLocaleEditor *editor;
	guint                  lang;
	gchar                 *key;
} LocaleCell;

static void
locale_cell_free (gpointer data)
{
	LocaleCell *c = data;

	g_free (c->key);
	g_free (c);
}

static void
locale_cell_paint (LrgCustomWidget *widget,
                   LrgPaintList    *list,
                   gfloat           x,
                   gfloat           y,
                   gfloat           width,
                   gfloat           height,
                   gpointer         user_data)
{
	LocaleCell            *c = user_data;
	LrgEditorLocaleEditor *self = c->editor;
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont               *font = lrg_theme_get_ui_font (theme);
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	Language              *l;
	const gchar           *text;
	gboolean               missing, selected;
	GrlColor               c1;

	if (self == NULL || c->key == NULL || c->lang >= self->langs->len)
		return;
	l = g_ptr_array_index (self->langs, c->lang);
	text = lrg_editor_locale_editor_get_text (self, c->key, l->code);
	missing = lrg_editor_locale_editor_is_missing (self, c->key, l->code);
	selected = g_strcmp0 (self->sel_key, c->key) == 0 && g_strcmp0 (self->sel_code, l->code) == 0;

	if (missing)
	{
		const GrlColor *danger = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);

		lrg_paint_color_with_alpha (danger, 0.16f, &c1);
		lrg_paint_list_rect (list, x + 1.0f, y + 2.0f, width - 2.0f, height - 4.0f, 4.0f, &c1);
		lrg_paint_list_rect (list, x + 1.0f, y + 2.0f, 3.0f, height - 4.0f, 1.5f, danger);
	}
	if (selected)
		lrg_paint_list_border (list, x + 1.0f, y + 2.0f, width - 2.0f, height - 4.0f, 4.0f, 2.0f,
		                       lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));

	lrg_paint_list_push_clip (list, x + 8.0f, y, MAX (width - 14.0f, 0.0f), height);
	if (text != NULL)
		lrg_paint_list_text (list, font, text, x + 8.0f, y + (height - fs) * 0.5f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	else
		lrg_paint_list_text (list, font, missing ? "missing" : "\xe2\x80\x94", x + 8.0f, y + (height - fs) * 0.5f,
		                     fs, 0.0f, lrg_theme_get_color (theme, missing ? LRG_THEME_COLOR_DANGER
		                                                                     : LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);
}

static gboolean
locale_cell_event (LrgCustomWidget  *widget,
                   const LrgUIEvent *event,
                   gpointer          user_data)
{
	LocaleCell            *c = user_data;
	LrgEditorLocaleEditor *self = c->editor;
	Language              *l;
	LrgEditorApp          *app;

	if (self == NULL || c->key == NULL || lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN ||
	    lrg_ui_event_get_button (event) != 0 || c->lang >= self->langs->len)
		return FALSE;
	l = g_ptr_array_index (self->langs, c->lang);
	lrg_editor_locale_editor_select (self, c->key, l->code);
	/* straight into the edit bar */
	app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
	if (app != NULL && app->canvas != NULL)
		lrg_canvas_set_focused_widget (app->canvas, LRG_WIDGET (self->edit_input));
	return TRUE;
}

static LrgWidget *
locale_cell_create (LrgTable *table,
                    guint     column,
                    gpointer  user_data)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, locale_cell_paint, NULL, locale_cell_event, { NULL } };
	LrgCustomWidget *w = lrg_custom_widget_new ();
	LocaleCell      *c = g_new0 (LocaleCell, 1);

	c->editor = LRG_EDITOR_LOCALE_EDITOR (user_data);
	c->lang = column - 1;
	lrg_custom_widget_set_funcs (w, &funcs, c, locale_cell_free);
	g_object_set_data (G_OBJECT (w), "lrg-locale-cell", c);
	return LRG_WIDGET (w);
}

static gchar *
key_cell_text (LrgTable  *table,
               LrgWidget *cell,
               GObject   *item,
               guint      position,
               gpointer   user_data)
{
	return g_strdup (LOCALE_KEY_ROW (item)->key);
}

static gchar *
lang_cell_text (LrgTable  *table,
                LrgWidget *cell,
                GObject   *item,
                guint      position,
                gpointer   user_data)
{
	LocaleCell *c = (cell != NULL) ? g_object_get_data (G_OBJECT (cell), "lrg-locale-cell") : NULL;

	if (c != NULL)
	{
		g_free (c->key);
		c->key = g_strdup (LOCALE_KEY_ROW (item)->key);
	}
	return NULL;
}

/* ==========================================================================
 * Model
 * ========================================================================== */

static gint
row_of (LrgEditorLocaleEditor *self,
        const gchar           *key)
{
	guint i, n = g_list_model_get_n_items (G_LIST_MODEL (self->rows));

	for (i = 0; key != NULL && i < n; i++)
	{
		g_autoptr(LocaleKeyRow) row = g_list_model_get_item (G_LIST_MODEL (self->rows), i);

		if (g_strcmp0 (row->key, key) == 0)
			return (gint) i;
	}
	return -1;
}

static void
load_edit_bar (LrgEditorLocaleEditor *self)
{
	g_autofree gchar *label = NULL;
	const gchar      *text = NULL;

	self->syncing = TRUE;
	if (self->sel_key != NULL && self->sel_code != NULL)
	{
		label = g_strdup_printf ("%s \xc2\xb7 %s", self->sel_key, self->sel_code);
		text = lrg_editor_locale_editor_get_text (self, self->sel_key, self->sel_code);
		lrg_text_input_set_placeholder (self->edit_input,
		                                lrg_editor_locale_editor_is_missing (self, self->sel_key, self->sel_code)
		                                ? "Missing: type the translation and press Enter" : "Empty");
	}
	lrg_label_set_text (self->edit_label, label != NULL ? label : "Select a cell");
	lrg_text_input_set_text (self->edit_input, text != NULL ? text : "");
	lrg_widget_set_enabled (LRG_WIDGET (self->edit_input), label != NULL);
	self->syncing = FALSE;
}

static void
rebuild_rows (LrgEditorLocaleEditor *self)
{
	g_autoptr(GPtrArray)  keys = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GPtrArray)  items = g_ptr_array_new_with_free_func (g_object_unref);
	guint                 i;
	gint                  sel;

	for (i = 0; i < self->langs->len; i++)
		collect_keys (((Language *) g_ptr_array_index (self->langs, i))->doc, keys, seen);
	for (i = 0; i < keys->len; i++)
	{
		LocaleKeyRow *row = g_object_new (LOCALE_TYPE_KEY_ROW, NULL);

		row->key = g_strdup (g_ptr_array_index (keys, i));
		g_ptr_array_add (items, row);
	}
	g_list_store_splice (self->rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->rows)), items->pdata,
	                     items->len);
	sel = row_of (self, self->sel_key);
	if (sel < 0)
	{
		g_clear_pointer (&self->sel_key, g_free);
		lrg_table_unselect_all (self->table);
	}
	else
		lrg_table_select (self->table, (guint) sel);
	lrg_widget_queue_layout (LRG_WIDGET (self->table));
	load_edit_bar (self);
}

/* ==========================================================================
 * Undo across languages
 * ========================================================================== */

static void
note_write (LrgEditorLocaleEditor *self,
            LrgEditorDocument     *doc)
{
	if (self->replaying)
		return;
	g_ptr_array_add (self->undo_order, doc);
	g_ptr_array_set_size (self->redo_order, 0);
}

static gboolean
locale_undo (LrgEditorDataEditor *editor)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (editor);
	LrgEditorDocument     *doc = NULL;
	gboolean               ok;

	while (self->undo_order->len > 0 && doc == NULL)
	{
		doc = g_ptr_array_steal_index (self->undo_order, self->undo_order->len - 1);
		if (!lrg_editor_document_can_undo (doc))
			doc = NULL;
	}
	if (doc == NULL)
		doc = primary (self);
	self->replaying = TRUE;
	ok = lrg_editor_document_undo (doc);
	self->replaying = FALSE;
	if (ok)
		g_ptr_array_add (self->redo_order, doc);
	return ok;
}

static gboolean
locale_redo (LrgEditorDataEditor *editor)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (editor);
	LrgEditorDocument     *doc = NULL;
	gboolean               ok;

	while (self->redo_order->len > 0 && doc == NULL)
	{
		doc = g_ptr_array_steal_index (self->redo_order, self->redo_order->len - 1);
		if (!lrg_editor_document_can_redo (doc))
			doc = NULL;
	}
	if (doc == NULL)
		doc = primary (self);
	self->replaying = TRUE;
	ok = lrg_editor_document_redo (doc);
	self->replaying = FALSE;
	if (ok)
		g_ptr_array_add (self->undo_order, doc);
	return ok;
}

static gboolean
locale_save (LrgEditorDataEditor  *editor,
             GError              **error)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (editor);
	guint                  i;

	for (i = 0; i < self->langs->len; i++)
	{
		Language *l = g_ptr_array_index (self->langs, i);

		if ((i == 0 || lrg_editor_document_get_dirty (l->doc)) && !lrg_editor_document_save (l->doc, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
locale_get_dirty (LrgEditorDataEditor *editor)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (editor);
	guint                  i;

	if (self->langs == NULL || self->langs->len == 0)
		return lrg_editor_document_get_dirty (primary (self));
	for (i = 0; i < self->langs->len; i++)
		if (lrg_editor_document_get_dirty (((Language *) g_ptr_array_index (self->langs, i))->doc))
			return TRUE;
	return FALSE;
}

/* ==========================================================================
 * Validation: missing keys per language as warnings on the reference
 * ========================================================================== */

static void
locale_validate (LrgEditorDataEditor *editor,
                 GPtrArray           *issues)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (editor);
	g_auto(GStrv)          keys = lrg_editor_locale_editor_get_keys (self);
	guint                  i, k;

	for (i = 1; i < self->langs->len; i++)
	{
		Language            *l = g_ptr_array_index (self->langs, i);
		g_autoptr(GPtrArray) own = NULL;

		/* the companion's own structural issues (not the reference check,
		 * which is reported per key below) */
		lrg_editor_document_set_reference (l->doc, NULL);
		lrg_editor_document_validate (l->doc, &own);
		lrg_editor_document_set_reference (l->doc, primary (self));
		for (k = 0; own != NULL && k < own->len; k++)
		{
			LrgEditorDocumentIssue *issue = g_ptr_array_index (own, k);
			g_autofree gchar       *msg = g_strdup_printf ("%s: %s", lrg_editor_document_get_display_name (l->doc),
			                                               issue->message);

			g_ptr_array_add (issues, lrg_editor_document_issue_new (issue->severity, issue->path, 0, 0, msg));
		}
		for (k = 0; keys[k] != NULL; k++)
		{
			if (lrg_editor_locale_editor_is_missing (self, keys[k], l->code))
			{
				g_autofree gchar *path = key_path (keys[k]);
				g_autofree gchar *msg = g_strdup_printf ("'%s' is missing in %s (%s)", keys[k], l->code,
				                                         lrg_editor_document_get_display_name (l->doc));
				guint             line = 0, col = 0;

				lrg_editor_document_get_line (primary (self), path, &line, &col);
				g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, line, 0,
				                                                        msg));
			}
		}
	}
}

/* ==========================================================================
 * Edit bar, tools
 * ========================================================================== */

static void
on_edit_submitted (LrgTextInput *input,
                   gpointer      user_data)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (user_data);
	g_autofree gchar      *text = g_strdup (lrg_text_input_get_text (input));    /* edits reload the bar */
	g_autoptr(GError)      error = NULL;
	gint                   row;

	if (self->syncing || self->sel_key == NULL || self->sel_code == NULL)
		return;
	if (g_strcmp0 (text, lrg_editor_locale_editor_get_text (self, self->sel_key, self->sel_code)) == 0)
		return;
	if (!lrg_editor_locale_editor_set_text (self, self->sel_key, self->sel_code,
	                                        (text != NULL && *text != '\0') ? text : NULL, &error))
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return;
	}
	/* Enter moves down a row, like a spreadsheet */
	row = row_of (self, self->sel_key);
	if (row >= 0 && (guint) row + 1 < g_list_model_get_n_items (G_LIST_MODEL (self->rows)))
	{
		g_autoptr(LocaleKeyRow) next = g_list_model_get_item (G_LIST_MODEL (self->rows), (guint) row + 1);

		lrg_editor_locale_editor_select (self, next->key, self->sel_code);
	}
}

typedef struct
{
	LrgEditorLocaleEditor *self;     /* weak */
	LrgFileDialog         *dialog;
	gboolean               save;
} CsvRequest;

static void
csv_request_free (CsvRequest *req)
{
	g_clear_weak_pointer (&req->self);
	g_clear_object (&req->dialog);
	g_free (req);
}

static void
on_csv_chosen (GObject      *source,
               GAsyncResult *result,
               gpointer      user_data)
{
	CsvRequest       *req = user_data;
	g_autoptr(GFile)  file = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *path = NULL;
	LrgEditorApp     *app;

	file = req->save ? lrg_file_dialog_save_finish (req->dialog, result, &error)
	                 : lrg_file_dialog_open_finish (req->dialog, result, &error);
	if (req->self == NULL)
	{
		csv_request_free (req);
		return;
	}
	app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (req->self));
	if (app != NULL && g_strcmp0 (lrg_editor_view_state_get_dialog (app->view), "file") == 0)
	{
		lrg_editor_view_state_set_dialog (app->view, NULL);
		editor_window_set_menus_enabled (app->window, TRUE);
	}
	if (file != NULL && (path = g_file_get_path (file)) != NULL)
	{
		if (req->save)
		{
			g_autofree gchar *csv = lrg_editor_locale_editor_export_csv (req->self);

			if (!g_file_set_contents (path, csv, -1, &error))
				lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (req->self), error);
			else if (app != NULL)
				editor_app_notify (app, EDITOR_NOTIFY_SUCCESS, "Exported %s", path);
		}
		else
		{
			g_autofree gchar *csv = NULL;
			gint              n;

			if (!g_file_get_contents (path, &csv, NULL, &error) ||
			    (n = lrg_editor_locale_editor_import_csv (req->self, csv, &error)) < 0)
				lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (req->self), error);
			else if (app != NULL)
				editor_app_notify (app, EDITOR_NOTIFY_SUCCESS, "Imported %d string(s) from %s", n, path);
		}
	}
	csv_request_free (req);
}

static void
ask_csv (LrgEditorLocaleEditor *self,
         gboolean               save)
{
	LrgEditorApp             *app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
	CsvRequest               *req;
	g_autofree gchar         *dir = NULL;
	g_autoptr(GFile)          folder = NULL;
	g_autoptr(GListStore)     filters = g_list_store_new (LRG_TYPE_FILE_FILTER);
	g_autoptr(LrgFileFilter)  filter = lrg_file_filter_new ("CSV");

	if (app == NULL || app->canvas == NULL)
		return;
	editor_dialog_close_all (app);
	dir = g_path_get_dirname (lrg_editor_document_get_path (primary (self)));
	folder = g_file_new_for_path (dir);
	req = g_new0 (CsvRequest, 1);
	g_set_weak_pointer (&req->self, self);
	req->save = save;
	req->dialog = lrg_file_dialog_new ();
	lrg_file_dialog_set_title (req->dialog, save ? "Export Strings as CSV" : "Import Strings from CSV");
	lrg_file_dialog_set_prefer_native (req->dialog, FALSE);
	lrg_file_dialog_set_modal (req->dialog, TRUE);
	lrg_file_dialog_set_initial_folder (req->dialog, folder);
	if (save)
		lrg_file_dialog_set_initial_name (req->dialog, "strings.csv");
	lrg_file_filter_add_suffix (filter, "csv");
	g_list_store_append (filters, filter);
	lrg_file_dialog_set_filters (req->dialog, G_LIST_MODEL (filters));
	lrg_editor_view_state_set_dialog (app->view, "file");
	editor_window_set_menus_enabled (app->window, FALSE);
	if (save)
		lrg_file_dialog_save (req->dialog, LRG_WIDGET (app->canvas), NULL, on_csv_chosen, req);
	else
		lrg_file_dialog_open (req->dialog, LRG_WIDGET (app->canvas), NULL, on_csv_chosen, req);
}

static void
on_export (LrgWidget *chip,
           gpointer   user_data)
{
	ask_csv (LRG_EDITOR_LOCALE_EDITOR (user_data), TRUE);
}

static void
on_import (LrgWidget *chip,
           gpointer   user_data)
{
	ask_csv (LRG_EDITOR_LOCALE_EDITOR (user_data), FALSE);
}

static void
on_add_key (LrgWidget *chip,
            gpointer   user_data)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (user_data);
	g_autofree gchar      *key = NULL;
	g_autoptr(GError)      error = NULL;
	guint                  i;
	Language              *ref = g_ptr_array_index (self->langs, 0);

	for (i = 1; key == NULL || lrg_editor_locale_editor_get_text (self, key, ref->code) != NULL; i++)
	{
		g_free (key);
		key = g_strdup_printf ("new_key_%u", i);
	}
	if (!lrg_editor_locale_editor_set_text (self, key, ref->code, key, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else
		lrg_editor_locale_editor_select (self, key, ref->code);
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

static void
on_companion_changed (LrgEditorDocument *doc,
                      const gchar       *path,
                      gpointer           user_data)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (user_data);

	rebuild_rows (self);
	lrg_editor_data_editor_sync_title (LRG_EDITOR_DATA_EDITOR (self));
}

static void
on_companion_dirty (LrgEditorDocument *doc,
                    gboolean           dirty,
                    gpointer           user_data)
{
	lrg_editor_data_editor_sync_title (LRG_EDITOR_DATA_EDITOR (user_data));
}

static void
locale_refresh (LrgEditorDataEditor *editor,
                const gchar         *path)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (editor);
	Language              *ref;

	if (self->langs->len == 0)
		return;
	/* the reference's code may have been edited */
	ref = g_ptr_array_index (self->langs, 0);
	if (g_str_has_prefix (path, "/code") || *path == '\0')
	{
		g_free (ref->code);
		ref->code = doc_code (primary (self));
		lrg_table_set_column_title (self->table, 1, ref->code);
	}
	rebuild_rows (self);
}

static void
locale_reveal (LrgEditorDataEditor *editor,
               const gchar         *path,
               guint                line)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (editor);
	g_auto(GStrv)          parts = lrg_editor_document_path_split (path, NULL);
	g_autofree gchar      *key = NULL;
	guint                  i;
	gint                   row;

	if (parts == NULL || g_strcmp0 (parts[0], "strings") != 0 || parts[1] == NULL)
		return;
	key = (parts[2] != NULL) ? g_strdup_printf ("%s[%s]", parts[1], parts[2]) : g_strdup (parts[1]);
	if (row_of (self, key) < 0)
		return;
	/* the first language lacking it, else the reference */
	for (i = 1; i < self->langs->len; i++)
	{
		Language *l = g_ptr_array_index (self->langs, i);

		if (lrg_editor_locale_editor_is_missing (self, key, l->code))
		{
			lrg_editor_locale_editor_select (self, key, l->code);
			break;
		}
	}
	if (i == self->langs->len)
		lrg_editor_locale_editor_select (self, key, NULL);
	row = row_of (self, key);
	if (row >= 0)
		lrg_table_scroll_to (self->table, (guint) row, TRUE);
}

static gchar *
locale_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (editor);

	return g_strdup_printf ("languages=%u keys=%u selected=%s/%s", self->langs->len,
	                        g_list_model_get_n_items (G_LIST_MODEL (self->rows)),
	                        self->sel_key != NULL ? self->sel_key : "-", self->sel_code != NULL ? self->sel_code : "-");
}

/* Loads every other locale file of the primary's folder. */
static void
load_companions (LrgEditorLocaleEditor *self)
{
	LrgEditorDocument *doc = primary (self);
	const gchar       *path = lrg_editor_document_get_path (doc);
	g_autofree gchar  *dir = NULL;
	g_autoptr(GDir)    d = NULL;
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
	const gchar       *name;
	Language          *l;
	guint              i;

	l = g_new0 (Language, 1);
	l->doc = g_object_ref (doc);
	l->code = doc_code (doc);
	g_ptr_array_add (self->langs, l);

	if (path == NULL)
		return;
	dir = g_path_get_dirname (path);
	d = g_dir_open (dir, 0, NULL);
	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
		if (g_str_has_suffix (name, ".yaml") || g_str_has_suffix (name, ".yml"))
			g_ptr_array_add (names, g_build_filename (dir, name, NULL));
	g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);
	for (i = 0; i < names->len; i++)
	{
		const gchar                 *file = g_ptr_array_index (names, i);
		g_autoptr(LrgEditorDocument) other = NULL;

		if (g_strcmp0 (file, path) == 0)
			continue;
		other = lrg_editor_document_new_from_file (file, NULL);
		if (other == NULL || lrg_editor_document_get_format (other) != LRG_EDITOR_DOCUMENT_FORMAT_LOCALE)
			continue;
		lrg_editor_document_set_reference (other, doc);
		l = g_new0 (Language, 1);
		l->doc = g_steal_pointer (&other);
		l->code = doc_code (l->doc);
		g_signal_connect_object (l->doc, "changed", G_CALLBACK (on_companion_changed), self, 0);
		g_signal_connect_object (l->doc, "dirty-changed", G_CALLBACK (on_companion_dirty), self, 0);
		g_ptr_array_add (self->langs, l);
	}
}

static void
lrg_editor_locale_editor_constructed (GObject *object)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (object);
	LrgEditorDataEditor   *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgVBox               *box;
	LrgHBox               *bar;
	guint                  i;

	G_OBJECT_CLASS (lrg_editor_locale_editor_parent_class)->constructed (object);
	load_companions (self);

	/* the grid */
	self->rows = g_list_store_new (LOCALE_TYPE_KEY_ROW);
	self->table = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->table), "locale-grid");
	lrg_table_add_column (self->table, "Key", key_cell_text, NULL, NULL, 180.0f,
	                      LRG_TABLE_COLUMN_RESIZABLE);
	for (i = 0; i < self->langs->len; i++)
	{
		Language         *l = g_ptr_array_index (self->langs, i);
		g_autofree gchar *title = (i == 0) ? g_strdup_printf ("%s (reference)", l->code) : g_strdup (l->code);
		guint             col = lrg_table_add_column (self->table, title, lang_cell_text, NULL, NULL, 220.0f,
		                                              LRG_TABLE_COLUMN_RESIZABLE |
		                                              (i + 1 == self->langs->len ? LRG_TABLE_COLUMN_EXPAND
		                                                                          : LRG_TABLE_COLUMN_NONE));

		lrg_table_set_column_widget_factory (self->table, col, locale_cell_create, self, NULL);
	}
	lrg_table_set_striped (self->table, TRUE);
	lrg_table_set_model (self->table, G_LIST_MODEL (self->rows));
	lrg_widget_set_fill_weight (LRG_WIDGET (self->table), 1.0f);

	/* the edit bar */
	bar = lrg_hbox_new ();
	lrg_widget_set_name (LRG_WIDGET (bar), "locale-edit-bar");
	lrg_container_set_padding (LRG_CONTAINER (bar), 6.0f);
	lrg_container_set_spacing (LRG_CONTAINER (bar), 8.0f);
	self->edit_label = lrg_label_new ("Select a cell");
	lrg_widget_set_name (LRG_WIDGET (self->edit_label), "locale-edit-label");
	lrg_widget_set_valign (LRG_WIDGET (self->edit_label), LRG_ALIGN_CENTER);
	lrg_widget_set_min_size (LRG_WIDGET (self->edit_label), 160.0f, 0.0f);
	self->edit_input = lrg_text_input_new_with_placeholder ("Select a cell");
	lrg_widget_set_name (LRG_WIDGET (self->edit_input), "locale-edit-input");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->edit_input), 1.0f);
	g_signal_connect_object (self->edit_input, "submitted", G_CALLBACK (on_edit_submitted), self, 0);
	lrg_container_add_child (LRG_CONTAINER (bar), LRG_WIDGET (self->edit_label));
	lrg_container_add_child (LRG_CONTAINER (bar), LRG_WIDGET (self->edit_input));

	box = lrg_vbox_new ();
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (self->table));
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (bar));
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (box));

	lrg_editor_data_editor_add_tool (base, editor_chip_new ("locale-add-key", LRG_ICON_PLUS, "Key", on_add_key,
	                                                        self));
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("locale-import", LRG_ICON_DOWNLOAD, "Import CSV",
	                                                        on_import, self));
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("locale-export", LRG_ICON_UPLOAD, "Export CSV",
	                                                        on_export, self));
	rebuild_rows (self);
}

static void
lrg_editor_locale_editor_dispose (GObject *object)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (object);

	if (self->langs != NULL)
		g_ptr_array_set_size (self->langs, 0);
	if (self->undo_order != NULL)
		g_ptr_array_set_size (self->undo_order, 0);
	if (self->redo_order != NULL)
		g_ptr_array_set_size (self->redo_order, 0);
	G_OBJECT_CLASS (lrg_editor_locale_editor_parent_class)->dispose (object);
}

static void
lrg_editor_locale_editor_finalize (GObject *object)
{
	LrgEditorLocaleEditor *self = LRG_EDITOR_LOCALE_EDITOR (object);

	g_clear_pointer (&self->langs, g_ptr_array_unref);
	g_clear_pointer (&self->undo_order, g_ptr_array_unref);
	g_clear_pointer (&self->redo_order, g_ptr_array_unref);
	g_clear_object (&self->rows);
	g_free (self->sel_key);
	g_free (self->sel_code);
	G_OBJECT_CLASS (lrg_editor_locale_editor_parent_class)->finalize (object);
}

static void
lrg_editor_locale_editor_class_init (LrgEditorLocaleEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_locale_editor_constructed;
	object_class->dispose = lrg_editor_locale_editor_dispose;
	object_class->finalize = lrg_editor_locale_editor_finalize;
	data_class->refresh = locale_refresh;
	data_class->reveal = locale_reveal;
	data_class->validate = locale_validate;
	data_class->get_state = locale_get_state;
	data_class->save = locale_save;
	data_class->undo = locale_undo;
	data_class->redo = locale_redo;
	data_class->get_dirty = locale_get_dirty;
}

static void
lrg_editor_locale_editor_init (LrgEditorLocaleEditor *self)
{
	self->langs = g_ptr_array_new_with_free_func (language_free);
	self->undo_order = g_ptr_array_new ();
	self->redo_order = g_ptr_array_new ();
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorLocaleEditor *
lrg_editor_locale_editor_new (LrgEditorApp      *app,
                              LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_LOCALE_EDITOR, "app", app, "document", document, NULL);
}

guint
lrg_editor_locale_editor_get_n_languages (LrgEditorLocaleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), 0);
	return self->langs->len;
}

const gchar *
lrg_editor_locale_editor_get_language (LrgEditorLocaleEditor *self,
                                       guint                  index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), NULL);
	return index < self->langs->len ? ((Language *) g_ptr_array_index (self->langs, index))->code : NULL;
}

LrgEditorDocument *
lrg_editor_locale_editor_get_language_document (LrgEditorLocaleEditor *self,
                                                const gchar           *code)
{
	Language *l;

	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), NULL);
	l = lang_by_code (self, code);
	return l != NULL ? l->doc : NULL;
}

GStrv
lrg_editor_locale_editor_get_keys (LrgEditorLocaleEditor *self)
{
	GStrvBuilder *b;
	guint         i, n;
	GStrv         out;

	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), NULL);
	b = g_strv_builder_new ();
	n = g_list_model_get_n_items (G_LIST_MODEL (self->rows));
	for (i = 0; i < n; i++)
	{
		g_autoptr(LocaleKeyRow) row = g_list_model_get_item (G_LIST_MODEL (self->rows), i);

		g_strv_builder_add (b, row->key);
	}
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

const gchar *
lrg_editor_locale_editor_get_text (LrgEditorLocaleEditor *self,
                                   const gchar           *key,
                                   const gchar           *code)
{
	Language         *l;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	l = lang_by_code (self, code);
	if (l == NULL)
		return NULL;
	path = key_path (key);
	if (lrg_editor_document_get_kind (l->doc, path) != LRG_EDITOR_DOCUMENT_NODE_SCALAR)
		return NULL;
	return lrg_editor_document_get_scalar (l->doc, path);
}

gboolean
lrg_editor_locale_editor_is_missing (LrgEditorLocaleEditor *self,
                                     const gchar           *key,
                                     const gchar           *code)
{
	Language         *ref;
	Language         *l;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	l = lang_by_code (self, code);
	if (l == NULL || self->langs->len == 0)
		return FALSE;
	ref = g_ptr_array_index (self->langs, 0);
	if (l == ref)
		return FALSE;
	path = key_path (key);
	return lrg_editor_document_has_path (ref->doc, path) && !lrg_editor_document_has_path (l->doc, path);
}

gboolean
lrg_editor_locale_editor_set_text (LrgEditorLocaleEditor  *self,
                                   const gchar            *key,
                                   const gchar            *code,
                                   const gchar            *text,
                                   GError                **error)
{
	Language         *l;
	g_autofree gchar *path = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *form = NULL;
	g_autofree gchar *key_copy = NULL;
	g_autofree gchar *code_copy = NULL;
	g_autofree gchar *text_copy = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL && *key != '\0', FALSE);

	/* the arguments may point into the edit bar or the rows, which every
	 * step below rebuilds */
	key = key_copy = g_strdup (key);
	code = code_copy = g_strdup (code);
	text = text_copy = g_strdup (text);
	l = lang_by_code (self, code);
	if (l == NULL)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		             "No language '%s' is loaded", code != NULL ? code : "(null)");
		return FALSE;
	}
	path = key_path (key);
	split_key (key, &base, &form);

	if (text == NULL)
		ok = !lrg_editor_document_has_path (l->doc, path) || lrg_editor_document_remove (l->doc, path, error);
	else if (lrg_editor_document_has_path (l->doc, path))
		ok = lrg_editor_document_set_string (l->doc, path, text, 0, error);
	else
	{
		g_autofree gchar *bpath = lrg_editor_document_path_join ("/strings", base);

		/* create /strings, the plural mapping, then the entry */
		lrg_editor_document_begin_group (l->doc, "Add translation");
		ok = TRUE;
		if (lrg_editor_document_get_kind (l->doc, "/strings") != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
			ok = lrg_editor_document_has_path (l->doc, "/strings")
			     ? lrg_editor_document_replace (l->doc, "/strings", "{}", error)
			     : lrg_editor_document_add_key (l->doc, "", "strings", "{}", -1, error);
		if (ok && form != NULL && lrg_editor_document_get_kind (l->doc, bpath) != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
			ok = lrg_editor_document_has_path (l->doc, bpath)
			     ? lrg_editor_document_replace (l->doc, bpath, "{}", error)
			     : lrg_editor_document_add_key (l->doc, "/strings", base, "{}", -1, error);
		/* an empty entry first, then the text as a properly quoted scalar */
		if (ok)
			ok = (form != NULL) ? lrg_editor_document_add_key (l->doc, bpath, form, "''", -1, error)
			                    : lrg_editor_document_add_key (l->doc, "/strings", base, "''", -1, error);
		if (ok)
			ok = lrg_editor_document_set_string (l->doc, path, text, 0, error);
		lrg_editor_document_end_group (l->doc);
	}
	if (ok)
		note_write (self, l->doc);
	return ok;
}

gboolean
lrg_editor_locale_editor_select (LrgEditorLocaleEditor *self,
                                 const gchar           *key,
                                 const gchar           *code)
{
	gint row;

	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), FALSE);

	if (code == NULL && self->langs->len > 0)
		code = ((Language *) g_ptr_array_index (self->langs, 0))->code;
	row = row_of (self, key);
	if (key != NULL && (row < 0 || lang_by_code (self, code) == NULL))
		return FALSE;
	g_free (self->sel_key);
	self->sel_key = g_strdup (key);
	g_free (self->sel_code);
	self->sel_code = (key != NULL) ? g_strdup (code) : NULL;
	if (row >= 0)
		lrg_table_select (self->table, (guint) row);
	else
		lrg_table_unselect_all (self->table);
	load_edit_bar (self);
	lrg_widget_queue_layout (LRG_WIDGET (self->table));
	return TRUE;
}

const gchar *
lrg_editor_locale_editor_get_selected_key (LrgEditorLocaleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), NULL);
	return self->sel_key;
}

const gchar *
lrg_editor_locale_editor_get_selected_language (LrgEditorLocaleEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), NULL);
	return self->sel_code;
}

gboolean
lrg_editor_locale_editor_get_cell_rect (LrgEditorLocaleEditor *self,
                                        const gchar           *key,
                                        const gchar           *code,
                                        gfloat                *x,
                                        gfloat                *y,
                                        gfloat                *width,
                                        gfloat                *height)
{
	LrgWidget *cell;
	gint       row;
	guint      i;

	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), FALSE);
	row = row_of (self, key);
	for (i = 0; i < self->langs->len; i++)
		if (g_strcmp0 (((Language *) g_ptr_array_index (self->langs, i))->code, code) == 0)
			break;
	if (row < 0 || i == self->langs->len)
		return FALSE;
	cell = lrg_table_get_cell_widget (self->table, (guint) row, i + 1);
	if (cell == NULL || !lrg_widget_get_visible (cell))
		return FALSE;
	if (x != NULL)
		*x = lrg_widget_get_world_x (cell);
	if (y != NULL)
		*y = lrg_widget_get_world_y (cell);
	if (width != NULL)
		*width = lrg_widget_get_width (cell);
	if (height != NULL)
		*height = lrg_widget_get_height (cell);
	return TRUE;
}

/* Appends @field to @out as one CSV field (quoted when needed). */
static void
csv_field (GString     *out,
           const gchar *field)
{
	const gchar *p;

	if (field == NULL)
		return;
	if (strpbrk (field, ",\"\r\n") == NULL && field[0] != ' ' && (field[0] == '\0' || field[strlen (field) - 1] != ' '))
	{
		g_string_append (out, field);
		return;
	}
	g_string_append_c (out, '"');
	for (p = field; *p != '\0'; p++)
	{
		if (*p == '"')
			g_string_append_c (out, '"');
		g_string_append_c (out, *p);
	}
	g_string_append_c (out, '"');
}

gchar *
lrg_editor_locale_editor_export_csv (LrgEditorLocaleEditor *self)
{
	GString       *out;
	g_auto(GStrv)  keys = NULL;
	guint          i, k;

	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), NULL);
	out = g_string_new ("key");
	for (i = 0; i < self->langs->len; i++)
	{
		g_string_append_c (out, ',');
		csv_field (out, ((Language *) g_ptr_array_index (self->langs, i))->code);
	}
	g_string_append_c (out, '\n');
	keys = lrg_editor_locale_editor_get_keys (self);
	for (k = 0; keys[k] != NULL; k++)
	{
		csv_field (out, keys[k]);
		for (i = 0; i < self->langs->len; i++)
		{
			g_string_append_c (out, ',');
			csv_field (out, lrg_editor_locale_editor_get_text (self, keys[k],
			                                                   ((Language *) g_ptr_array_index (self->langs,
			                                                                                    i))->code));
		}
		g_string_append_c (out, '\n');
	}
	return g_string_free (out, FALSE);
}

gint
lrg_editor_locale_editor_import_csv (LrgEditorLocaleEditor  *self,
                                     const gchar            *csv,
                                     GError                **error)
{
	g_autoptr(GPtrArray) rows = NULL;
	GStrv                header;
	gint                 written = 0;
	guint                c, r;

	g_return_val_if_fail (LRG_IS_EDITOR_LOCALE_EDITOR (self), -1);
	g_return_val_if_fail (csv != NULL, -1);

	rows = lrg_csv_parse (csv, -1, ',', error);
	if (rows == NULL)
		return -1;
	if (rows->len == 0)
		return 0;
	header = g_ptr_array_index (rows, 0);

	/* one undo group per language column */
	for (c = 1; header[c] != NULL; c++)
	{
		Language *l = lang_by_code (self, header[c]);
		gboolean  any = FALSE;

		if (l == NULL)
		{
			g_warning ("CSV import: no language '%s' is loaded, column skipped", header[c]);
			continue;
		}
		lrg_editor_document_begin_group (l->doc, "Import CSV");
		for (r = 1; r < rows->len; r++)
		{
			GStrv             row = g_ptr_array_index (rows, r);
			g_autoptr(GError) err = NULL;

			if (row[0] == NULL || *row[0] == '\0' || g_strv_length (row) <= c || row[c][0] == '\0')
				continue;
			if (g_strcmp0 (lrg_editor_locale_editor_get_text (self, row[0], l->code), row[c]) == 0)
				continue;
			self->replaying = TRUE;    /* one note per language, below */
			if (lrg_editor_locale_editor_set_text (self, row[0], l->code, row[c], &err))
			{
				written++;
				any = TRUE;
			}
			else
				g_warning ("CSV import: %s/%s: %s", row[0], l->code, err->message);
			self->replaying = FALSE;
		}
		lrg_editor_document_end_group (l->doc);
		if (any)
			note_write (self, l->doc);
	}
	return written;
}
