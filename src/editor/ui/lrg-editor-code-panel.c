/* lrg-editor-code-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorCodePanel: the built-in code editor (see the header).
 *
 * Layout: a toolbar row (Save, Reload, Close, Go to line and a status
 * line "Ln 12, Col 4 · lua · scripts/door.lua") above an LrgTabView whose
 * tabs are LrgTextEditors; with no file open a muted hint takes the tab
 * view's place.
 *
 * Documents. One CodeDoc per tab, in tab order: the absolute path, the
 * editor (borrowed: the tab view owns it), the text as it is on disk
 * ("saved"), the dirty flag and a GFileMonitor. Dirtiness is the editor
 * text differing from "saved", so undoing back to the saved text clears
 * the mark. The disk check reads the file and compares it with "saved":
 * equal means nothing changed (this also swallows the monitor events of
 * our own writes), different reloads a clean tab or asks for a dirty one.
 *
 * Keys. Key events reach the focused text editor first and bubble up to
 * the panel when the editor does not use them (it uses Ctrl+A/C/V/X/Y/Z
 * and leaves other Ctrl combinations to the application), so the panel
 * sees Ctrl+S before the canvas shortcuts do: the accelerators of
 * "file.save" save the focused tab while focus is inside the panel and
 * the level is saved everywhere else. Ctrl+W and Ctrl+G work the same
 * way (or the accelerators bound to code.close-tab / code.goto-line).
 *
 * Dialogs follow the protocol of lrg-editor-dialogs.c: the open dialog is
 * object data "lrg-editor-dialog" on the app and its id goes into
 * LrgEditorViewState:dialog, which disables the editor's shortcuts and
 * lets editor_dialog_close_all() close it.
 *
 * Shaders (0.9.0). `.rshader` documents (and the engine's other YAML
 * assets: .rmat, .rshadergraph, .rpost) open with YAML highlighting,
 * GLSL stages (.glsl .vert .frag .vs .fs) with GLSL. Saving a shader
 * document or a stage compiles every `.rshader` of the folder that uses
 * it (LrgShaderAsset: GL when a window exists, otherwise only the
 * document is checked) and keeps the diagnostics, mapped to the stage
 * file and line (an inline stage maps into the .rshader below its key):
 *
 *  - a diagnostics strip under the editor lists the active file's
 *    messages; clicking one jumps to its line;
 *  - the problems panel shows them as "shader-error" / "shader-warning"
 *    problems (re-added from step() whenever the app replaces its
 *    problem array after a level validation);
 *  - opening a file at line 0 or 1 jumps to its first error.
 *
 * After a save the material preview's cached shader assets reload
 * (editor_mat_preview_file_changed), so open material editors, the
 * shader graph preview and thumbnails show the new code at once; a
 * failing compile keeps the last good program there. The shader.* IPC
 * methods drive the same machinery.
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include "lrg-editor-code-panel.h"
#include "lrg-editor-palette.h"
#include "lrg-editor-preferences.h"
#define LRG_EDITOR_PANEL_INTERNAL
#include "data/lrg-editor-material-editor.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-icons.h"
#include "../../graphics/lrg-shader-asset.h"
#include <raylib.h>
#include "../../ui/lrg-tab-view.h"
#include "../../ui/lrg-text-editor.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-syntax.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-dialog.h"
#include "../../ui/lrg-ui-actions.h"
#include "../../ui/lrg-ui-event.h"
#include <string.h>

#define DIALOG_KEY        "lrg-editor-dialog"
#define PANEL_KEY         "lrg-editor-code-panel"
#define PALETTE_KEY       "lrg-editor-palette"
#define MAX_FILE_SIZE     (8 * 1024 * 1024)
#define DIAG_ROW_H        (20.0f)
#define DIAG_MAX_ROWS     (6)
#define SHADER_CODE_ERROR "shader-error"
#define SHADER_CODE_WARN  "shader-warning"
#define DIRTY_MARK        " \xe2\x80\xa2"        /* " •" */
#define MOD_MASK          (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL | \
                           LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SUPER)

/* Responses of the prompts (LrgDialog's own are <= 0) */
enum
{
	RESPONSE_SAVE = 1,
	RESPONSE_DISCARD = 2,
	RESPONSE_RELOAD = 3,
	RESPONSE_GO = 4
};

typedef struct _CodeDoc CodeDoc;

/* One shader diagnostic, mapped to a file of the project. */
typedef struct
{
	gchar    *file;      /* absolute */
	guint     line;      /* 1-based, 0 unknown */
	guint     column;
	gboolean  error;
	gchar    *message;
	gchar    *text;      /* "file:line:col: error: message" */
} CodeDiag;

static void
code_diag_free (gpointer data)
{
	CodeDiag *d = data;

	g_free (d->file);
	g_free (d->message);
	g_free (d->text);
	g_free (d);
}

struct _CodeDoc
{
	LrgEditorCodePanel *panel;     /* borrowed */
	gchar              *path;      /* absolute */
	LrgTextEditor      *editor;    /* borrowed: the tab view owns it */
	gchar              *saved;     /* text as last read or written */
	gboolean            dirty;
	gboolean            asking;    /* the "changed on disk" prompt is up */
	GFileMonitor       *monitor;
};

struct _LrgEditorCodePanel
{
	LrgContainer        parent_instance;

	LrgEditorApp       *app;       /* weak */
	LrgHBox            *toolbar;
	LrgLabel           *status;
	LrgTabView         *tabs;
	LrgLabel           *empty;
	GPtrArray          *docs;      /* CodeDoc*, same order as the tabs */
	GSimpleActionGroup *group;     /* "code" on the canvas */
	gboolean            syncing;   /* updating labels: ignore change signals */

	/* shaders */
	GHashTable         *diags;     /* .rshader abs path -> GPtrArray of CodeDiag */
	LrgWidget          *diag_view; /* the strip under the editor */
	gpointer            problems_seen;
	guint               problems_len;
};

G_DEFINE_FINAL_TYPE (LrgEditorCodePanel, lrg_editor_code_panel, LRG_TYPE_CONTAINER)

static void doc_update_title (CodeDoc *doc);
static void update_status (LrgEditorCodePanel *self);

/* ==========================================================================
 * Documents
 * ========================================================================== */

static void
code_doc_free (gpointer data)
{
	CodeDoc *doc = data;

	if (doc->monitor != NULL)
	{
		g_signal_handlers_disconnect_by_data (doc->monitor, doc);
		g_file_monitor_cancel (doc->monitor);
		g_object_unref (doc->monitor);
	}
	if (doc->editor != NULL)
		g_signal_handlers_disconnect_by_data (doc->editor, doc);
	g_free (doc->path);
	g_free (doc->saved);
	g_free (doc);
}

/* The document for @path (absolute or relative), or the active one for
 * NULL; @index_out gets its tab index. */
static CodeDoc *
find_doc (LrgEditorCodePanel *self,
          const gchar        *path,
          guint              *index_out)
{
	g_autofree gchar *abs = NULL;
	guint             i;

	if (self->docs->len == 0)
		return NULL;
	if (path == NULL)
	{
		i = MIN (lrg_tab_view_get_active_tab (self->tabs), self->docs->len - 1);
		if (index_out != NULL)
			*index_out = i;
		return g_ptr_array_index (self->docs, i);
	}
	abs = (self->app != NULL) ? editor_app_project_path (self->app, path) : g_canonicalize_filename (path, NULL);
	for (i = 0; i < self->docs->len; i++)
	{
		CodeDoc *doc = g_ptr_array_index (self->docs, i);

		if (g_strcmp0 (doc->path, abs) == 0 || g_strcmp0 (doc->path, path) == 0)
		{
			if (index_out != NULL)
				*index_out = i;
			return doc;
		}
	}
	return NULL;
}

static gint
doc_index (LrgEditorCodePanel *self,
           CodeDoc            *doc)
{
	guint i;

	for (i = 0; i < self->docs->len; i++)
		if (g_ptr_array_index (self->docs, i) == doc)
			return (gint) i;
	return -1;
}

/* Reads @path as UTF-8 text no larger than MAX_FILE_SIZE. */
static gchar *
read_text (const gchar  *path,
           GError      **error)
{
	gchar *contents = NULL;
	gsize  length = 0;

	if (!g_file_get_contents (path, &contents, &length, error))
		return NULL;
	if (length > MAX_FILE_SIZE)
	{
		g_free (contents);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s is too large for the code editor", path);
		return NULL;
	}
	if (!g_utf8_validate (contents, (gssize) length, NULL) || memchr (contents, '\0', length) != NULL)
	{
		g_free (contents);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s is not a text file", path);
		return NULL;
	}
	return contents;
}

/* Recomputes the dirty mark from the editor text. */
static void
doc_refresh_dirty (CodeDoc *doc)
{
	g_autofree gchar *text = lrg_text_editor_get_text (doc->editor);
	gboolean          dirty = g_strcmp0 (text, doc->saved) != 0;

	if (dirty == doc->dirty)
		return;
	doc->dirty = dirty;
	doc_update_title (doc);
}

static void
doc_update_title (CodeDoc *doc)
{
	LrgEditorCodePanel *self = doc->panel;
	g_autofree gchar   *base = g_path_get_basename (doc->path);
	g_autofree gchar   *label = g_strconcat (base, doc->dirty ? DIRTY_MARK : "", NULL);
	gint                i = doc_index (self, doc);

	if (i >= 0)
		lrg_tab_view_set_tab_label (self->tabs, (guint) i, label);
	update_status (self);
}

/* Replaces the tab's text with @text, keeping the caret where it was. */
static void
doc_set_text (CodeDoc     *doc,
              const gchar *text)
{
	guint line = 0;
	guint col = 0;

	lrg_text_editor_get_cursor (doc->editor, &line, &col);
	g_free (doc->saved);
	doc->saved = g_strdup (text);
	lrg_text_editor_set_text (doc->editor, text);
	lrg_text_editor_clear_history (doc->editor);
	lrg_text_editor_set_cursor (doc->editor, line, col);
	doc->dirty = FALSE;
	doc_update_title (doc);
}

static gboolean
doc_save (CodeDoc  *doc,
          GError  **error)
{
	g_autofree gchar *text = lrg_text_editor_get_text (doc->editor);

	if (!g_file_set_contents (doc->path, text, -1, error))
		return FALSE;
	g_free (doc->saved);
	doc->saved = g_steal_pointer (&text);
	doc->dirty = FALSE;
	doc_update_title (doc);
	return TRUE;
}

/* ==========================================================================
 * Dialogs (the lrg-editor-dialogs.c protocol)
 * ========================================================================== */

typedef void (*PromptDone) (LrgEditorCodePanel *self, LrgDialog *dialog, gint response);

static void
on_prompt_response (LrgDialog *dialog,
                    gint       response,
                    gpointer   user_data)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (user_data);
	PromptDone          done = (PromptDone) g_object_get_data (G_OBJECT (dialog), "lrg-code-done");
	g_autoptr(LrgDialog) keep = g_object_ref (dialog);

	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), DIALOG_KEY) == dialog)
	{
		g_object_set_data (G_OBJECT (self->app), DIALOG_KEY, NULL);
		lrg_editor_view_state_set_dialog (self->app->view, NULL);
	}
	if (done != NULL)
		done (self, dialog, response);
}

static void
present_prompt (LrgEditorCodePanel *self,
                LrgDialog          *dialog,
                const gchar        *id,
                PromptDone          done)
{
	LrgEditorApp *app = self->app;

	if (app == NULL)
		return;
	editor_dialog_close_all (app);
	g_object_set_data (G_OBJECT (dialog), "lrg-code-done", (gpointer) done);
	g_signal_connect_object (dialog, "response", G_CALLBACK (on_prompt_response), self, 0);
	g_object_set_data_full (G_OBJECT (app), DIALOG_KEY, g_object_ref_sink (dialog), g_object_unref);
	lrg_editor_view_state_set_dialog (app->view, id);
	editor_window_set_menus_enabled (app->window, FALSE);
	lrg_dialog_present (dialog, app->canvas);
}

/* --- Close a dirty tab -------------------------------------------------- */

static void
close_prompt_done (LrgEditorCodePanel *self,
                   LrgDialog          *dialog,
                   gint                response)
{
	const gchar      *path = g_object_get_data (G_OBJECT (dialog), "path");
	CodeDoc          *doc = find_doc (self, path, NULL);
	g_autoptr(GError) error = NULL;

	if (doc == NULL)
		return;
	if (response == RESPONSE_SAVE)
	{
		if (!doc_save (doc, &error))
		{
			if (self->app != NULL)
				editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Could not save %s: %s", doc->path,
				                   error->message);
			return;
		}
		lrg_editor_code_panel_close (self, path, TRUE);
	}
	else if (response == RESPONSE_DISCARD)
		lrg_editor_code_panel_close (self, path, TRUE);
}

static void
ask_close (LrgEditorCodePanel *self,
           CodeDoc            *doc)
{
	g_autofree gchar    *base = g_path_get_basename (doc->path);
	g_autofree gchar    *body = g_strdup_printf ("%s has unsaved changes. Save them before closing?", base);
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("Save changes?", body);

	g_object_set_data_full (G_OBJECT (dialog), "path", g_strdup (doc->path), g_free);
	lrg_dialog_add_button (dialog, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Don't Save", RESPONSE_DISCARD, FALSE);
	lrg_dialog_add_button (dialog, "Save", RESPONSE_SAVE, TRUE);
	present_prompt (self, dialog, "confirm", close_prompt_done);
}

/* --- Changed on disk while dirty ---------------------------------------- */

static void
reload_prompt_done (LrgEditorCodePanel *self,
                    LrgDialog          *dialog,
                    gint                response)
{
	const gchar      *path = g_object_get_data (G_OBJECT (dialog), "path");
	CodeDoc          *doc = find_doc (self, path, NULL);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;

	if (doc == NULL)
		return;
	doc->asking = FALSE;
	text = read_text (doc->path, &error);
	if (text == NULL)
		return;
	if (response == RESPONSE_RELOAD)
		doc_set_text (doc, text);
	else
	{
		/* Keep mine: what is on disk becomes the new baseline, so the
		 * tab stays dirty and this change is not asked about again */
		g_free (doc->saved);
		doc->saved = g_steal_pointer (&text);
		doc_refresh_dirty (doc);
	}
}

static void
ask_reload (LrgEditorCodePanel *self,
            CodeDoc            *doc)
{
	g_autofree gchar    *base = g_path_get_basename (doc->path);
	g_autofree gchar    *body = g_strdup_printf ("%s changed on disk, and this tab has unsaved changes.\n"
	                                             "Reload it (losing your changes) or keep your version?", base);
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("File changed on disk", body);

	doc->asking = TRUE;
	g_object_set_data_full (G_OBJECT (dialog), "path", g_strdup (doc->path), g_free);
	lrg_dialog_add_button (dialog, "Keep Mine", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Reload", RESPONSE_RELOAD, TRUE);
	present_prompt (self, dialog, "code-reload", reload_prompt_done);
}

/* --- Go to line --------------------------------------------------------- */

static void
goto_prompt_done (LrgEditorCodePanel *self,
                  LrgDialog          *dialog,
                  gint                response)
{
	LrgTextInput     *input = g_object_get_data (G_OBJECT (dialog), "input");
	g_autofree gchar *text = NULL;
	gint64            line = 0;

	if (response != RESPONSE_GO || input == NULL)
		return;
	text = g_strstrip (g_strdup (lrg_text_input_get_text (input) != NULL ? lrg_text_input_get_text (input) : ""));
	if (!g_ascii_string_to_signed (text, 10, 1, G_MAXINT, &line, NULL))
		return;
	lrg_editor_code_panel_goto_line (self, (gint) line);
}

/* Enter in the line field answers "Go" (the field consumes Enter) */
static void
on_goto_submitted (LrgTextInput *input,
                   gpointer      user_data)
{
	lrg_dialog_respond (LRG_DIALOG (user_data), RESPONSE_GO);
}

static void
ask_goto_line (LrgEditorCodePanel *self)
{
	g_autoptr(LrgDialog)    dialog = NULL;
	g_autoptr(LrgTextInput) input = lrg_text_input_new_with_placeholder ("Line number");
	CodeDoc                *doc = find_doc (self, NULL, NULL);
	g_autofree gchar       *body = NULL;

	if (doc == NULL)
		return;
	body = g_strdup_printf ("1 to %u", lrg_text_editor_get_line_count (doc->editor));
	dialog = lrg_dialog_new ("Go to Line", body);
	lrg_widget_set_name (LRG_WIDGET (input), "goto-line");
	lrg_widget_set_min_size (LRG_WIDGET (input), 260.0f, 0.0f);
	g_object_set_data (G_OBJECT (dialog), "input", input);
	lrg_dialog_set_content (dialog, LRG_WIDGET (input));
	lrg_dialog_add_button (dialog, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Go", RESPONSE_GO, TRUE);
	g_signal_connect_object (input, "submitted", G_CALLBACK (on_goto_submitted), dialog, 0);
	present_prompt (self, dialog, "goto-line", goto_prompt_done);
	if (self->app != NULL)
		lrg_canvas_set_focused_widget (self->app->canvas, LRG_WIDGET (input));
}

/* ==========================================================================
 * Disk
 * ========================================================================== */

/* Compares one document with its file; TRUE when it was reloaded. */
static gboolean
doc_check_disk (CodeDoc *doc)
{
	g_autofree gchar *text = NULL;

	if (doc->asking || !g_file_test (doc->path, G_FILE_TEST_IS_REGULAR))
		return FALSE;
	text = read_text (doc->path, NULL);
	if (text == NULL || g_strcmp0 (text, doc->saved) == 0)
		return FALSE;

	doc_refresh_dirty (doc);
	if (!doc->dirty)
	{
		doc_set_text (doc, text);
		if (doc->panel->app != NULL)
			editor_app_set_status (doc->panel->app, "Reloaded %s (changed on disk)", doc->path);
		return TRUE;
	}
	ask_reload (doc->panel, doc);
	return FALSE;
}

static void
on_file_changed (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *other,
                 GFileMonitorEvent  event,
                 gpointer           user_data)
{
	CodeDoc *doc = user_data;

	if (event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event == G_FILE_MONITOR_EVENT_CREATED ||
	    event == G_FILE_MONITOR_EVENT_RENAMED || event == G_FILE_MONITOR_EVENT_MOVED_IN)
		doc_check_disk (doc);
}

guint
lrg_editor_code_panel_check_disk (LrgEditorCodePanel *self)
{
	g_autoptr(GPtrArray) docs = NULL;
	guint                i;
	guint                n = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), 0);

	/* A borrowed copy (no free func): a prompt may close tabs */
	docs = g_ptr_array_sized_new (self->docs->len);
	for (i = 0; i < self->docs->len; i++)
		g_ptr_array_add (docs, g_ptr_array_index (self->docs, i));
	for (i = 0; i < docs->len; i++)
		if (doc_index (self, g_ptr_array_index (docs, i)) >= 0 && doc_check_disk (g_ptr_array_index (docs, i)))
			n++;
	return n;
}

/* ==========================================================================
 * Shaders: compile on save, diagnostics, problems
 * ========================================================================== */

static gboolean
is_shader_document (const gchar *path)
{
	g_autofree gchar *lower = g_ascii_strdown (path, -1);

	return g_str_has_suffix (lower, ".rshader");
}

static gboolean
is_shader_stage (const gchar *path)
{
	g_autofree gchar *lower = g_ascii_strdown (path, -1);

	return g_str_has_suffix (lower, ".glsl") || g_str_has_suffix (lower, ".vert") ||
	       g_str_has_suffix (lower, ".frag") || g_str_has_suffix (lower, ".vs") || g_str_has_suffix (lower, ".fs");
}

/* YAML highlighting for the engine's YAML assets without a .yaml name. */
static LrgSyntax *
syntax_for (const gchar *path)
{
	g_autofree gchar *lower = g_ascii_strdown (path, -1);

	if (g_str_has_suffix (lower, ".rshader") || g_str_has_suffix (lower, ".rmat") ||
	    g_str_has_suffix (lower, ".rshadergraph") || g_str_has_suffix (lower, ".rpost"))
		return lrg_syntax_get_for_name ("yaml");
	return lrg_syntax_get_for_filename (path);
}

/* The 1-based line of "<key>:" at the start of a line of @path, 0 when
 * absent (inline stages start on the next line). */
static guint
key_line (const gchar *path,
          const gchar *key)
{
	g_autofree gchar *text = NULL;
	g_auto(GStrv)     lines = NULL;
	g_autofree gchar *prefix = g_strconcat (key, ":", NULL);
	guint             i;

	if (!g_file_get_contents (path, &text, NULL, NULL))
		return 0;
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
		if (g_str_has_prefix (lines[i], prefix))
			return i + 1;
	return 0;
}

/* A driver diagnostic of @rshader mapped to a project file and line. */
static CodeDiag *
map_diagnostic (const gchar               *rshader,
                const LrgShaderDiagnostic *d)
{
	CodeDiag         *c = g_new0 (CodeDiag, 1);
	const gchar      *src = lrg_shader_diagnostic_get_source (d);
	g_autofree gchar *dir = g_path_get_dirname (rshader);
	g_autofree gchar *rel = NULL;

	c->line = lrg_shader_diagnostic_get_line (d);
	c->column = lrg_shader_diagnostic_get_column (d);
	c->error = lrg_shader_diagnostic_is_error (d);
	c->message = g_strdup (lrg_shader_diagnostic_get_message (d));
	if (src == NULL || lrg_shader_diagnostic_is_in_preamble (d))
		c->file = g_strdup (rshader);
	else if (g_str_has_suffix (src, "#fragment") || g_str_has_suffix (src, "#vertex"))
	{
		/* inline stage: lines count from the line after its key */
		guint base = key_line (rshader, g_str_has_suffix (src, "#fragment") ? "fragment" : "vertex");

		c->file = g_strdup (rshader);
		if (c->line > 0)
			c->line += base;
	}
	else
		c->file = g_canonicalize_filename (src, dir);
	rel = g_path_get_basename (c->file);
	if (c->line > 0 && c->column > 0)
		c->text = g_strdup_printf ("%s:%u:%u: %s: %s", rel, c->line, c->column, c->error ? "error" : "warning",
		                           c->message);
	else if (c->line > 0)
		c->text = g_strdup_printf ("%s:%u: %s: %s", rel, c->line, c->error ? "error" : "warning", c->message);
	else
		c->text = g_strdup_printf ("%s: %s: %s", rel, c->error ? "error" : "warning", c->message);
	return c;
}

/*
 * compile_document:
 *
 * Loads and (with a window) compiles one .rshader; returns its mapped
 * diagnostics (empty when it is fine). Without a window only loading is
 * checked: a YAML or stage-file problem is still an error.
 */
static GPtrArray *
compile_document (const gchar *rshader)
{
	GPtrArray                *out = g_ptr_array_new_with_free_func (code_diag_free);
	g_autoptr(LrgShaderAsset) asset = NULL;
	g_autoptr(GError)         error = NULL;
	GPtrArray                *diags;
	guint                     i;

	asset = lrg_shader_asset_new_from_file (rshader, &error);
	if (asset == NULL)
	{
		CodeDiag *c = g_new0 (CodeDiag, 1);
		g_autofree gchar *base = g_path_get_basename (rshader);

		c->file = g_strdup (rshader);
		c->error = TRUE;
		c->message = g_strdup (error->message);
		c->text = g_strdup_printf ("%s: error: %s", base, error->message);
		g_ptr_array_add (out, c);
		return out;
	}
	if (!IsWindowReady ())
		return out;
	lrg_shader_asset_compile (asset, NULL);
	diags = lrg_shader_asset_get_diagnostics (asset);
	for (i = 0; diags != NULL && i < diags->len; i++)
		g_ptr_array_add (out, map_diagnostic (rshader, g_ptr_array_index (diags, i)));
	lrg_shader_asset_release (asset);
	return out;
}

/* The .rshader documents a save of @path concerns: itself, or the
 * documents of its folder whose stages are @path. */
static GPtrArray *
documents_for (const gchar *path)
{
	GPtrArray        *out = g_ptr_array_new_with_free_func (g_free);
	g_autofree gchar *dir = NULL;
	g_autoptr(GDir)   d = NULL;
	const gchar      *name;

	if (is_shader_document (path))
	{
		g_ptr_array_add (out, g_strdup (path));
		return out;
	}
	if (!is_shader_stage (path))
		return out;
	dir = g_path_get_dirname (path);
	d = g_dir_open (dir, 0, NULL);
	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar         *doc = NULL;
		g_autoptr(LrgShaderAsset) asset = NULL;
		guint                     s;

		if (!is_shader_document (name))
			continue;
		doc = g_build_filename (dir, name, NULL);
		asset = lrg_shader_asset_new_from_file (doc, NULL);
		for (s = 0; asset != NULL && s < 2; s++)
		{
			const gchar      *sp = lrg_shader_asset_get_source_path (asset, (LrgShaderStage) s);
			g_autofree gchar *abs = sp != NULL ? g_canonicalize_filename (sp, dir) : NULL;

			if (g_strcmp0 (abs, path) == 0)
			{
				g_ptr_array_add (out, g_strdup (doc));
				break;
			}
		}
	}
	return out;
}

/* Diagnostics of the active file (borrowed CodeDiag pointers). */
static GPtrArray *
active_diags (LrgEditorCodePanel *self)
{
	GPtrArray     *out = g_ptr_array_new ();
	CodeDoc       *doc = find_doc (self, NULL, NULL);
	GHashTableIter iter;
	gpointer       value;

	if (doc == NULL || self->diags == NULL)
		return out;
	g_hash_table_iter_init (&iter, self->diags);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		GPtrArray *list = value;
		guint      i;

		for (i = 0; i < list->len; i++)
			if (g_strcmp0 (((CodeDiag *) g_ptr_array_index (list, i))->file, doc->path) == 0)
				g_ptr_array_add (out, g_ptr_array_index (list, i));
	}
	return out;
}

static guint
active_diag_count (LrgEditorCodePanel *self)
{
	g_autoptr(GPtrArray) list = active_diags (self);

	return list->len;
}

/* Puts the shader diagnostics into the app's problem list (a new array,
 * so the problems panel notices). */
static void
sync_problems (LrgEditorCodePanel *self)
{
	LrgEditorApp  *app = self->app;
	GPtrArray     *fresh;
	GHashTableIter iter;
	gpointer       value;
	guint          i;

	if (app == NULL || app->problems == NULL)
		return;
	fresh = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_problem_free);
	for (i = 0; i < app->problems->len; i++)
	{
		LrgEditorProblem *p = g_ptr_array_index (app->problems, i);

		if (p->code == NULL || !g_str_has_prefix (p->code, "shader-"))
			g_ptr_array_add (fresh, lrg_editor_problem_copy (p));
	}
	g_hash_table_iter_init (&iter, self->diags);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		GPtrArray *list = value;
		guint      k;

		for (k = 0; k < list->len; k++)
		{
			CodeDiag         *d = g_ptr_array_index (list, k);
			LrgEditorProblem *p = g_new0 (LrgEditorProblem, 1);

			p->severity = d->error ? LRG_EDITOR_PROBLEM_ERROR : LRG_EDITOR_PROBLEM_WARNING;
			p->code = g_strdup (d->error ? SHADER_CODE_ERROR : SHADER_CODE_WARN);
			p->message = g_strdup (d->text);
			p->file = editor_app_relative_path (app, d->file);
			g_ptr_array_add (fresh, p);
		}
	}
	g_ptr_array_unref (app->problems);
	app->problems = fresh;
	self->problems_seen = fresh;
	self->problems_len = fresh->len;
}

/* Whether the app's problem array is the one we last filled. */
static void
check_problems (LrgEditorCodePanel *self)
{
	if (self->app == NULL || self->app->problems == NULL || g_hash_table_size (self->diags) == 0)
		return;
	if (self->app->problems != self->problems_seen || self->app->problems->len != self->problems_len)
		sync_problems (self);
}

/*
 * shader_check:
 *
 * After a save (or shader.compile): compiles the documents @path
 * concerns, stores their diagnostics, reloads the preview's copies and
 * updates the strip and the problems. Returns the number of errors.
 */
static guint
shader_check (LrgEditorCodePanel *self,
              const gchar        *path)
{
	g_autoptr(GPtrArray) docs = documents_for (path);
	guint                i, errors = 0, k;

	if (self->app != NULL)
		editor_mat_preview_file_changed (self->app, path);
	for (i = 0; i < docs->len; i++)
	{
		const gchar *doc = g_ptr_array_index (docs, i);
		GPtrArray   *diags = compile_document (doc);

		for (k = 0; k < diags->len; k++)
			if (((CodeDiag *) g_ptr_array_index (diags, k))->error)
				errors++;
		if (diags->len == 0)
		{
			g_ptr_array_unref (diags);
			g_hash_table_remove (self->diags, doc);
		}
		else
			g_hash_table_replace (self->diags, g_strdup (doc), diags);
		if (self->app != NULL && g_strcmp0 (doc, path) != 0)
			editor_mat_preview_file_changed (self->app, doc);
	}
	sync_problems (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	if (self->app != NULL && docs->len > 0)
	{
		if (errors > 0)
			editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Shader: %u error(s)", errors);
		else
			editor_app_set_status (self->app, IsWindowReady () ? "Shader compiled" : "Shader checked");
	}
	return errors;
}

/* --- the diagnostics strip ------------------------------------------------ */

static void
diag_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorCodePanel   *self = LRG_EDITOR_CODE_PANEL (user_data);
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont              *font = lrg_theme_get_ui_font (theme);
	gfloat                fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	g_autoptr(GPtrArray)  rows = active_diags (self);
	GrlColor              wash;
	guint                 i;

	lrg_paint_list_rect (list, x, y, width, height, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	lrg_paint_list_rect (list, x, y, width, 1.0f, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	lrg_paint_list_push_clip (list, x, y, width, height);
	for (i = 0; i < rows->len && i < DIAG_MAX_ROWS; i++)
	{
		CodeDiag       *d = g_ptr_array_index (rows, i);
		const GrlColor *c = lrg_theme_get_color (theme, d->error ? LRG_THEME_COLOR_DANGER : LRG_THEME_COLOR_WARNING);
		gfloat          ry = y + 3.0f + (gfloat) i * DIAG_ROW_H;
		gfloat          icon = fs + 2.0f;

		if (i % 2 == 1)
		{
			lrg_paint_color_with_alpha (c, 0.06f, &wash);
			lrg_paint_list_rect (list, x, ry, width, DIAG_ROW_H, 0.0f, &wash);
		}
		lrg_paint_list_rect (list, x, ry + 2.0f, 3.0f, DIAG_ROW_H - 4.0f, 1.5f, c);
		lrg_paint_list_icon (list, d->error ? LRG_ICON_CROSS : LRG_ICON_WARNING, x + 10.0f,
		                     ry + (DIAG_ROW_H - icon) * 0.5f, icon, 0.0f, c);
		lrg_paint_list_text (list, font, d->text, x + 16.0f + icon, ry + (DIAG_ROW_H - fs) * 0.5f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	}
	lrg_paint_list_pop_clip (list);
}

static void code_panel_goto_diag (LrgEditorCodePanel *self, guint index);

static gboolean
diag_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (user_data);
	gfloat              y;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
		return FALSE;
	y = lrg_ui_event_get_y (event) - lrg_widget_get_world_y (LRG_WIDGET (widget)) - 3.0f;
	if (y < 0.0f)
		return TRUE;
	code_panel_goto_diag (self, (guint) (y / DIAG_ROW_H));
	return TRUE;
}

/* ==========================================================================
 * Status and layout
 * ========================================================================== */

static void
update_status (LrgEditorCodePanel *self)
{
	CodeDoc          *doc = find_doc (self, NULL, NULL);
	g_autofree gchar *rel = NULL;
	g_autofree gchar *text = NULL;
	guint             line = 0;
	guint             col = 0;
	LrgSyntax        *syntax;

	lrg_widget_set_visible (LRG_WIDGET (self->tabs), doc != NULL);
	lrg_widget_set_visible (LRG_WIDGET (self->empty), doc == NULL);
	if (doc == NULL)
	{
		lrg_label_set_text (self->status, "No file open");
		lrg_widget_queue_layout (LRG_WIDGET (self));
		return;
	}
	lrg_text_editor_get_cursor (doc->editor, &line, &col);
	syntax = lrg_text_editor_get_syntax (doc->editor);
	rel = (self->app != NULL) ? editor_app_relative_path (self->app, doc->path) : g_strdup (doc->path);
	text = g_strdup_printf ("Ln %u, Col %u  \xc2\xb7  %s  \xc2\xb7  %s%s", line + 1, col + 1,
	                        syntax != NULL ? lrg_syntax_get_name (syntax) : "text", rel,
	                        doc->dirty ? "  (modified)" : "");
	lrg_label_set_text (self->status, text);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
lrg_editor_code_panel_layout_children (LrgContainer *container)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (container);
	gfloat              w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat              h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat              tw = 0.0f;
	gfloat              th = 0.0f;

	/* Children are added one by one in init */
	if (self->toolbar == NULL || self->tabs == NULL || self->empty == NULL)
		return;
	lrg_widget_get_preferred_size (LRG_WIDGET (self->toolbar), &tw, &th);
	lrg_widget_place_in_slot (LRG_WIDGET (self->toolbar), 0.0f, 0.0f, w, th);
	{
		/* the diagnostics strip of the active file at the bottom */
		guint  rows = self->diag_view != NULL ? MIN (active_diag_count (self), (guint) DIAG_MAX_ROWS) : 0;
		gfloat dh = rows > 0 ? (gfloat) rows * DIAG_ROW_H + 6.0f : 0.0f;

		dh = MIN (dh, MAX (h - th, 0.0f) * 0.45f);
		if (self->diag_view != NULL)
		{
			lrg_widget_set_visible (self->diag_view, rows > 0);
			if (rows > 0)
				lrg_widget_place_in_slot (self->diag_view, 0.0f, h - dh, w, dh);
		}
		lrg_widget_place_in_slot (LRG_WIDGET (self->tabs), 0.0f, th, w, MAX (h - th - dh, 0.0f));
	}
	lrg_widget_place_in_slot (LRG_WIDGET (self->empty), 0.0f, th, w, MAX (h - th, 0.0f));
}

static void
lrg_editor_code_panel_measure (LrgWidget *widget,
                               gfloat    *width,
                               gfloat    *height)
{
	*width = 240.0f;
	*height = 160.0f;
}

/* ==========================================================================
 * Keys
 * ========================================================================== */

/* Whether @key + @mods is one of @action's accelerators (or @fallback
 * when the action has none). */
static gboolean
matches_action (LrgEditorCodePanel *self,
                const gchar        *action,
                const gchar        *fallback,
                GrlKey              key,
                LrgInputModifiers   mods)
{
	g_auto(GStrv) accels = NULL;
	guint         i;

	if (self->app != NULL)
		accels = lrg_editor_actions_get_accels (self->app->actions, action);
	if ((accels == NULL || accels[0] == NULL) && fallback != NULL)
	{
		g_strfreev (accels);
		accels = g_strsplit (fallback, "|", -1);
	}
	for (i = 0; accels != NULL && accels[i] != NULL; i++)
	{
		GrlKey            k;
		LrgInputModifiers m;

		if (lrg_accel_parse (accels[i], &k, &m) && k == key && (m & MOD_MASK) == (mods & MOD_MASK))
			return TRUE;
	}
	return FALSE;
}

static gboolean
lrg_editor_code_panel_handle_event (LrgWidget        *widget,
                                    const LrgUIEvent *event)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (widget);
	GrlKey              key;
	LrgInputModifiers   mods;
	g_autoptr(GError)   error = NULL;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_KEY_DOWN || self->docs->len == 0 ||
	    lrg_ui_event_get_repeat (event))
		return FALSE;
	key = lrg_ui_event_get_key (event);
	mods = lrg_ui_event_get_modifiers (event);

	/* Panel-scoped: the level's Save becomes "save this file" here */
	if (matches_action (self, "file.save", "<Ctrl>s", key, mods) ||
	    matches_action (self, "code.save", NULL, key, mods))
	{
		if (!lrg_editor_code_panel_save (self, NULL, &error) && self->app != NULL)
			editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Save failed: %s", error->message);
		return TRUE;
	}
	if (matches_action (self, "code.close-tab", "<Ctrl>w", key, mods))
	{
		lrg_editor_code_panel_close (self, NULL, FALSE);
		return TRUE;
	}
	if (matches_action (self, "code.goto-line", "<Ctrl>g", key, mods))
	{
		ask_goto_line (self);
		return TRUE;
	}
	return FALSE;
}

/* ==========================================================================
 * Signals
 * ========================================================================== */

static void
on_editor_changed (LrgTextEditor *editor,
                   gpointer       user_data)
{
	CodeDoc *doc = user_data;

	if (!doc->panel->syncing)
		doc_refresh_dirty (doc);
}

static void
on_cursor_moved (LrgTextEditor *editor,
                 guint          line,
                 guint          column,
                 gpointer       user_data)
{
	CodeDoc *doc = user_data;

	if (find_doc (doc->panel, NULL, NULL) == doc)
		update_status (doc->panel);
}

static void
on_tab_changed (LrgTabView *tabs,
                guint       index,
                gpointer    user_data)
{
	update_status (LRG_EDITOR_CODE_PANEL (user_data));
}

static void
on_toolbar_save (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (user_data);
	g_autoptr(GError)   error = NULL;

	if (self->docs->len > 0 && !lrg_editor_code_panel_save (self, NULL, &error) && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Save failed: %s", error->message);
}

static void
on_toolbar_reload (LrgButton *button,
                   gpointer   user_data)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (user_data);
	CodeDoc            *doc = find_doc (self, NULL, NULL);
	g_autofree gchar   *text = NULL;

	if (doc == NULL)
		return;
	if (doc->dirty)
	{
		ask_reload (self, doc);
		return;
	}
	text = read_text (doc->path, NULL);
	if (text != NULL)
		doc_set_text (doc, text);
}

static void
on_toolbar_close (LrgButton *button,
                  gpointer   user_data)
{
	lrg_editor_code_panel_close (LRG_EDITOR_CODE_PANEL (user_data), NULL, FALSE);
}

static void
on_toolbar_goto (LrgButton *button,
                 gpointer   user_data)
{
	ask_goto_line (LRG_EDITOR_CODE_PANEL (user_data));
}

/* ==========================================================================
 * Routing (script-editor setting)
 * ========================================================================== */

static LrgEditorCodePanel *
app_code_panel (LrgEditorApp *app)
{
	return (app != NULL) ? g_object_get_data (G_OBJECT (app), PANEL_KEY) : NULL;
}

/* "path" or "path:line" (a trailing ":digits" is the line). */
static gchar *
split_target (const gchar *target,
              gint        *line)
{
	const gchar *colon = strrchr (target, ':');
	gint64       n = 0;

	*line = 0;
	if (colon != NULL && colon[1] != '\0' && g_ascii_string_to_signed (colon + 1, 10, 0, G_MAXINT, &n, NULL))
	{
		*line = (gint) n;
		return g_strndup (target, (gsize) (colon - target));
	}
	return g_strdup (target);
}

gboolean
lrg_editor_code_panel_open_script (LrgEditorApp *app,
                                   const gchar  *path,
                                   gint          line)
{
	LrgEditorCodePanel *panel;
	g_autoptr(GError)   error = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	panel = app_code_panel (app);
	if (panel == NULL || g_strcmp0 (lrg_editor_settings_get_script_editor (app->settings), "internal") != 0)
	{
		editor_app_open_file (app, path, MAX (line, 1), 1);
		return FALSE;
	}
	if (!lrg_editor_code_panel_open (panel, path, line, &error))
	{
		editor_app_notify (app, EDITOR_NOTIFY_ERROR, "Cannot open %s: %s", path, error->message);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_open_scripts (gpointer data)
{
	LrgEditorApp *app = LRG_EDITOR_APP (data);
	LrgNode      *node = lrg_editor_selection_get_primary (lrg_editor_get_selection (app->editor));
	GPtrArray    *scripts;
	guint         i;

	if (node == NULL)
		return;
	scripts = lrg_node_get_scripts (node);
	for (i = 0; scripts != NULL && i < scripts->len; i++)
	{
		const gchar *path = lrg_script_binding_get_script (g_ptr_array_index (scripts, i));

		if (path != NULL)
			lrg_editor_code_panel_open_script (app, path, 0);
	}
}

static gboolean
primary_has_scripts (gpointer data)
{
	LrgEditorApp *app = LRG_EDITOR_APP (data);
	LrgNode      *node = lrg_editor_selection_get_primary (lrg_editor_get_selection (app->editor));

	return node != NULL && lrg_node_get_scripts (node) != NULL && lrg_node_get_scripts (node)->len > 0;
}

static gboolean
has_file (gpointer data)
{
	LrgEditorCodePanel *panel = app_code_panel (LRG_EDITOR_APP (data));

	return panel != NULL && panel->docs->len > 0;
}

static void
a_code_save (gpointer data)
{
	LrgEditorCodePanel *panel = app_code_panel (LRG_EDITOR_APP (data));
	g_autoptr(GError)   error = NULL;

	if (panel != NULL && panel->docs->len > 0 && !lrg_editor_code_panel_save (panel, NULL, &error))
		editor_app_notify (LRG_EDITOR_APP (data), EDITOR_NOTIFY_ERROR, "Save failed: %s", error->message);
}

static void
a_code_save_all (gpointer data)
{
	LrgEditorCodePanel *panel = app_code_panel (LRG_EDITOR_APP (data));
	g_autoptr(GError)   error = NULL;

	if (panel != NULL && !lrg_editor_code_panel_save_all (panel, &error))
		editor_app_notify (LRG_EDITOR_APP (data), EDITOR_NOTIFY_ERROR, "Save failed: %s", error->message);
}

static void
a_code_close (gpointer data)
{
	LrgEditorCodePanel *panel = app_code_panel (LRG_EDITOR_APP (data));

	if (panel != NULL)
		lrg_editor_code_panel_close (panel, NULL, FALSE);
}

static void
a_code_reload (gpointer data)
{
	LrgEditorCodePanel *panel = app_code_panel (LRG_EDITOR_APP (data));

	if (panel != NULL)
		on_toolbar_reload (NULL, panel);
}

static void
a_code_goto (gpointer data)
{
	LrgEditorCodePanel *panel = app_code_panel (LRG_EDITOR_APP (data));

	if (panel != NULL)
		ask_goto_line (panel);
}

static void
a_preferences (gpointer data)
{
	lrg_editor_preferences_present (LRG_EDITOR_APP (data), NULL);
}

/* --- the "code" group on the canvas (parameterised) --------------------- */

static void
g_open (GSimpleAction *action,
        GVariant      *parameter,
        gpointer       user_data)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (user_data);
	g_autoptr(GError)   error = NULL;
	g_autofree gchar   *path = NULL;
	gint                line = 0;

	path = split_target (g_variant_get_string (parameter, NULL), &line);
	if (!lrg_editor_code_panel_open (self, path, line, &error) && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Cannot open %s: %s", path, error->message);
}

static void
g_goto (GSimpleAction *action,
        GVariant      *parameter,
        gpointer       user_data)
{
	lrg_editor_code_panel_goto_line (LRG_EDITOR_CODE_PANEL (user_data), g_variant_get_int32 (parameter));
}

static void
g_simple (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (user_data);
	const gchar        *name = g_action_get_name (G_ACTION (action));

	if (self->app == NULL)
		return;
	if (g_strcmp0 (name, "save") == 0)
		a_code_save (self->app);
	else if (g_strcmp0 (name, "save-all") == 0)
		a_code_save_all (self->app);
	else if (g_strcmp0 (name, "close") == 0)
		a_code_close (self->app);
	else if (g_strcmp0 (name, "reload") == 0)
		a_code_reload (self->app);
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static JsonNode *
doc_to_json (LrgEditorCodePanel *self,
             CodeDoc            *doc)
{
	JsonObject *o = json_object_new ();
	JsonNode   *n = json_node_new (JSON_NODE_OBJECT);
	guint       line = 0;
	guint       col = 0;
	gint        i = doc_index (self, doc);

	lrg_text_editor_get_cursor (doc->editor, &line, &col);
	json_object_set_string_member (o, "path", doc->path);
	json_object_set_int_member (o, "tab", i);
	json_object_set_boolean_member (o, "active", find_doc (self, NULL, NULL) == doc);
	json_object_set_boolean_member (o, "dirty", doc->dirty);
	json_object_set_int_member (o, "line", (gint64) line + 1);
	json_object_set_int_member (o, "column", (gint64) col + 1);
	json_object_set_int_member (o, "lines", lrg_text_editor_get_line_count (doc->editor));
	json_object_set_string_member (o, "language", lrg_text_editor_get_syntax (doc->editor) != NULL
	                               ? lrg_syntax_get_name (lrg_text_editor_get_syntax (doc->editor)) : "text");
	json_node_take_object (n, o);
	return n;
}

static LrgEditorCodePanel *
ipc_panel (gpointer   user_data,
           GError   **error)
{
	LrgEditorCodePanel *panel = app_code_panel (LRG_EDITOR_APP (user_data));

	if (panel == NULL)
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "The code panel is gone");
	return panel;
}

static const gchar *
param_string (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING) ? json_node_get_string (n)
	                                                                                    : NULL;
}

static JsonNode *
m_code_open (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorCodePanel *self = ipc_panel (user_data, error);
	const gchar        *path = param_string (params, "path");
	g_autoptr(GError)   err = NULL;
	gint                line = 0;

	if (self == NULL)
		return NULL;
	if (path == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'path'");
		return NULL;
	}
	if (json_object_has_member (params, "line"))
		line = (gint) json_object_get_int_member (params, "line");
	if (!lrg_editor_code_panel_open (self, path, line, &err))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR,
		                     g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT)
		                     ? LRG_EDITOR_SERVER_ERROR_NOT_FOUND : LRG_EDITOR_SERVER_ERROR_FAILED, err->message);
		return NULL;
	}
	return doc_to_json (self, find_doc (self, NULL, NULL));
}

static JsonNode *
m_code_list (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorCodePanel *self = ipc_panel (user_data, error);
	JsonArray          *a;
	JsonNode           *n;
	guint               i;

	if (self == NULL)
		return NULL;
	a = json_array_new ();
	for (i = 0; i < self->docs->len; i++)
		json_array_add_element (a, doc_to_json (self, g_ptr_array_index (self->docs, i)));
	n = json_node_new (JSON_NODE_ARRAY);
	json_node_take_array (n, a);
	return n;
}

static JsonNode *
m_code_save (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorCodePanel *self = ipc_panel (user_data, error);
	const gchar        *path = param_string (params, "path");
	g_autoptr(GError)   err = NULL;
	CodeDoc            *doc;

	if (self == NULL)
		return NULL;
	doc = find_doc (self, path, NULL);
	if (doc == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "%s is not open",
		             path != NULL ? path : "No file");
		return NULL;
	}
	if (!doc_save (doc, &err))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, err->message);
		return NULL;
	}
	return doc_to_json (self, doc);
}

/* preferences.open {page?}: the Preferences dialog on a page */
static JsonNode *
m_preferences_open (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgEditorApp         *app = LRG_EDITOR_APP (user_data);
	const gchar          *page = param_string (params, "page");
	LrgEditorPreferences *prefs;
	JsonNode             *n;

	prefs = lrg_editor_preferences_present (app, NULL);
	if (prefs == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No canvas");
		return NULL;
	}
	if (page != NULL && !lrg_editor_preferences_set_page (prefs, page))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Unknown preferences page '%s'", page);
		return NULL;
	}
	n = json_node_new (JSON_NODE_VALUE);
	json_node_set_string (n, lrg_editor_preferences_get_page (prefs));
	return n;
}

/* palette.open {mode?: "commands"|"files", query?}: show the palette */
static JsonNode *
m_palette_open (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	LrgEditorPalette *palette = g_object_get_data (G_OBJECT (app), PALETTE_KEY);
	const gchar      *mode = param_string (params, "mode");
	JsonObject       *o;
	JsonNode         *n;

	if (palette == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No palette");
		return NULL;
	}
	if (mode != NULL && g_strcmp0 (mode, "commands") != 0 && g_strcmp0 (mode, "files") != 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "mode must be \"commands\" or \"files\", not '%s'", mode);
		return NULL;
	}
	lrg_editor_palette_present (palette, g_strcmp0 (mode, "files") == 0 ? LRG_EDITOR_PALETTE_MODE_FILES
	                                                                    : LRG_EDITOR_PALETTE_MODE_COMMANDS,
	                            param_string (params, "query"));
	o = json_object_new ();
	json_object_set_boolean_member (o, "open", lrg_editor_palette_get_open (palette));
	json_object_set_int_member (o, "results", lrg_editor_palette_get_n_results (palette));
	json_object_set_boolean_member (o, "scanning", lrg_editor_palette_get_scanning (palette));
	n = json_node_new (JSON_NODE_OBJECT);
	json_node_take_object (n, o);
	return n;
}

/* --- shader.* ------------------------------------------------------------- */

static void
code_panel_goto_diag (LrgEditorCodePanel *self,
                      guint               index)
{
	g_autoptr(GPtrArray) rows = active_diags (self);
	CodeDiag            *d;
	LrgTextEditor       *editor;
	g_autofree gchar    *file = NULL;

	if (index >= rows->len)
		return;
	d = g_ptr_array_index (rows, index);
	file = g_strdup (d->file);
	if (!lrg_editor_code_panel_open (self, file, (gint) MAX (d->line, 1u), NULL))
		return;
	editor = lrg_editor_code_panel_get_editor (self, file);
	if (editor != NULL && d->line > 0)
		lrg_text_editor_set_cursor (editor, d->line - 1, d->column > 0 ? d->column - 1 : 0);
}

/* Diagnostics of the documents @path concerns, as JSON. */
static JsonNode *
diags_json (LrgEditorCodePanel *self,
            const gchar        *path,
            gboolean            ok)
{
	g_autoptr(GPtrArray) docs = documents_for (path);
	JsonObject          *o = json_object_new ();
	JsonArray           *a = json_array_new ();
	JsonNode            *n = json_node_new (JSON_NODE_OBJECT);
	guint                i, k;

	for (i = 0; i < docs->len; i++)
	{
		GPtrArray *list = g_hash_table_lookup (self->diags, g_ptr_array_index (docs, i));

		for (k = 0; list != NULL && k < list->len; k++)
		{
			CodeDiag   *d = g_ptr_array_index (list, k);
			JsonObject *e = json_object_new ();

			json_object_set_string_member (e, "document", g_ptr_array_index (docs, i));
			json_object_set_string_member (e, "file", d->file);
			json_object_set_int_member (e, "line", d->line);
			json_object_set_int_member (e, "column", d->column);
			json_object_set_boolean_member (e, "error", d->error);
			json_object_set_string_member (e, "message", d->message);
			json_array_add_object_element (a, e);
		}
	}
	json_object_set_boolean_member (o, "ok", ok);
	json_object_set_boolean_member (o, "compiled", IsWindowReady ());
	json_object_set_array_member (o, "diagnostics", a);
	json_node_take_object (n, o);
	return n;
}

static gchar *
ipc_shader_path (LrgEditorApp  *app,
                 JsonObject    *params,
                 GError       **error)
{
	const gchar      *path = param_string (params, "path");
	g_autofree gchar *abs = NULL;

	if (path == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'path'");
		return NULL;
	}
	abs = editor_app_project_path (app, path);
	if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "%s does not exist", abs);
		return NULL;
	}
	return g_canonicalize_filename (abs, NULL);
}

static JsonNode *
m_shader_create (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = param_string (params, "path");
	gboolean           lit = TRUE;
	g_autofree gchar  *abs = NULL;
	g_autofree gchar  *frag = NULL;
	g_autoptr(GError)  err = NULL;
	JsonObject        *o;
	JsonNode          *n;

	if (path == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'path'");
		return NULL;
	}
	if (json_object_has_member (params, "lit"))
		lit = json_object_get_boolean_member (params, "lit");
	if (!lrg_editor_material_editor_create_shader (app, path, lit, &err))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, err->message);
		return NULL;
	}
	abs = editor_app_project_path (app, path);
	{
		g_autofree gchar *dir = g_path_get_dirname (abs);
		g_autofree gchar *base = g_path_get_basename (abs);
		gchar            *dot = strrchr (base, '.');

		if (dot != NULL)
			*dot = '\0';
		frag = g_strdup_printf ("%s" G_DIR_SEPARATOR_S "%s.frag", dir, base);
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	json_object_set_string_member (o, "fragment", frag);
	n = json_node_new (JSON_NODE_OBJECT);
	json_node_take_object (n, o);
	return n;
}

static JsonNode *
m_shader_compile (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorApp       *app = LRG_EDITOR_APP (user_data);
	LrgEditorCodePanel *self = ipc_panel (user_data, error);
	g_autofree gchar   *abs = NULL;
	guint               errors;

	if (self == NULL || (abs = ipc_shader_path (app, params, error)) == NULL)
		return NULL;
	errors = shader_check (self, abs);
	return diags_json (self, abs, errors == 0);
}

static JsonNode *
m_shader_diagnostics (LrgEditorServer  *server,
                      JsonObject       *params,
                      gpointer          user_data,
                      GError          **error)
{
	LrgEditorApp       *app = LRG_EDITOR_APP (user_data);
	LrgEditorCodePanel *self = ipc_panel (user_data, error);
	g_autofree gchar   *abs = NULL;
	g_autoptr(GPtrArray) docs = NULL;
	gboolean            ok = TRUE;
	guint               i, k;

	if (self == NULL || (abs = ipc_shader_path (app, params, error)) == NULL)
		return NULL;
	docs = documents_for (abs);
	for (i = 0; i < docs->len; i++)
	{
		GPtrArray *list = g_hash_table_lookup (self->diags, g_ptr_array_index (docs, i));

		for (k = 0; list != NULL && k < list->len; k++)
			if (((CodeDiag *) g_ptr_array_index (list, k))->error)
				ok = FALSE;
	}
	return diags_json (self, abs, ok);
}

static JsonNode *
m_shader_uniforms (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp              *app = LRG_EDITOR_APP (user_data);
	g_autofree gchar          *abs = NULL;
	g_autoptr(LrgShaderAsset)  asset = NULL;
	g_autoptr(GError)          err = NULL;
	GEnumClass                *klass;
	JsonObject                *o;
	JsonArray                 *a;
	JsonNode                  *n;
	guint                      i;

	if ((abs = ipc_shader_path (app, params, error)) == NULL)
		return NULL;
	asset = lrg_shader_asset_new_from_file (abs, &err);
	if (asset == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, err->message);
		return NULL;
	}
	klass = g_type_class_ref (LRG_TYPE_SHADER_UNIFORM_TYPE);
	a = json_array_new ();
	for (i = 0; i < lrg_shader_asset_get_n_uniforms (asset); i++)
	{
		const LrgShaderUniform *u = lrg_shader_asset_get_uniform_at (asset, i);
		LrgShaderUniformType    type = lrg_shader_uniform_get_uniform_type (u);
		GEnumValue             *ev = g_enum_get_value (klass, (gint) type);
		JsonObject             *e = json_object_new ();
		gfloat                  def[4], lo, hi;
		guint                   c, nc = lrg_shader_uniform_type_get_components (type);

		json_object_set_string_member (e, "name", lrg_shader_uniform_get_name (u));
		json_object_set_string_member (e, "type", ev != NULL ? ev->value_nick : "float");
		if (type == LRG_SHADER_UNIFORM_SAMPLER2D)
			json_object_set_string_member (e, "default", lrg_shader_uniform_get_default_texture (u));
		else
		{
			JsonArray *d = json_array_new ();

			lrg_shader_uniform_get_default (u, def);
			for (c = 0; c < MAX (nc, 1u) && c < 4; c++)
				json_array_add_double_element (d, def[c]);
			json_object_set_array_member (e, "default", d);
		}
		if (lrg_shader_uniform_get_range (u, &lo, &hi))
		{
			json_object_set_double_member (e, "min", lo);
			json_object_set_double_member (e, "max", hi);
		}
		if (lrg_shader_uniform_get_hint (u) != NULL)
			json_object_set_string_member (e, "hint", lrg_shader_uniform_get_hint (u));
		json_array_add_object_element (a, e);
	}
	g_type_class_unref (klass);
	o = json_object_new ();
	json_object_set_array_member (o, "uniforms", a);
	n = json_node_new (JSON_NODE_OBJECT);
	json_node_take_object (n, o);
	return n;
}

static JsonNode *
m_shader_reload (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	/* the same as a save: reload the preview's copies, recompile, report */
	return m_shader_compile (server, params, user_data, error);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_code_panel_dispose (GObject *object)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (object);

	if (self->app != NULL)
	{
		if (g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
			g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
		if (self->app->canvas != NULL &&
		    lrg_widget_get_action_group (LRG_WIDGET (self->app->canvas), "code") == G_ACTION_GROUP (self->group))
			lrg_widget_insert_action_group (LRG_WIDGET (self->app->canvas), "code", NULL);
	}
	g_clear_weak_pointer (&self->app);
	if (self->docs != NULL)
		g_ptr_array_set_size (self->docs, 0);
	if (self->diag_view != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->diag_view), NULL, NULL, NULL);
	g_clear_object (&self->group);
	G_OBJECT_CLASS (lrg_editor_code_panel_parent_class)->dispose (object);
}

static void
lrg_editor_code_panel_finalize (GObject *object)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (object);

	g_clear_pointer (&self->docs, g_ptr_array_unref);
	g_clear_pointer (&self->diags, g_hash_table_unref);
	G_OBJECT_CLASS (lrg_editor_code_panel_parent_class)->finalize (object);
}

/* Keeps the shader problems in the app's list after level validations
 * replaced it. */
static gboolean
lrg_editor_code_panel_step (LrgWidget *widget,
                            gfloat     delta_time)
{
	LrgEditorCodePanel *self = LRG_EDITOR_CODE_PANEL (widget);
	LrgWidgetClass     *parent = LRG_WIDGET_CLASS (lrg_editor_code_panel_parent_class);
	gboolean            busy = parent->step != NULL ? parent->step (widget, delta_time) : FALSE;

	check_problems (self);
	return busy;
}

static void
lrg_editor_code_panel_class_init (LrgEditorCodePanelClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_editor_code_panel_dispose;
	object_class->finalize = lrg_editor_code_panel_finalize;
	widget_class->measure = lrg_editor_code_panel_measure;
	widget_class->handle_event = lrg_editor_code_panel_handle_event;
	widget_class->step = lrg_editor_code_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_code_panel_layout_children;
}

static LrgButton *
toolbar_button (LrgEditorCodePanel *self,
                const gchar        *label,
                const gchar        *name,
                GCallback           callback)
{
	LrgButton *button = lrg_button_new (label);

	lrg_widget_set_name (LRG_WIDGET (button), name);
	lrg_widget_add_class (LRG_WIDGET (button), "flat");
	g_signal_connect_object (button, "clicked", callback, self, 0);
	lrg_container_add_child (LRG_CONTAINER (self->toolbar), LRG_WIDGET (button));
	g_object_unref (button);
	return button;
}

static void
lrg_editor_code_panel_init (LrgEditorCodePanel *self)
{
	g_autoptr(LrgLabel) empty = lrg_label_new ("No file open.  Ctrl+P opens a project file; scripts open here "
	                                           "when Preferences > External Editor > Scripts open in is "
	                                           "\"internal\".");

	self->docs = g_ptr_array_new_with_free_func (code_doc_free);

	/* Toolbar */
	self->toolbar = lrg_hbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 4.0f);
	lrg_container_set_padding (LRG_CONTAINER (self->toolbar), 4.0f);
	lrg_widget_set_name (LRG_WIDGET (self->toolbar), "code-toolbar");
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->toolbar));
	g_object_unref (self->toolbar);
	toolbar_button (self, "Save", "code-save", G_CALLBACK (on_toolbar_save));
	toolbar_button (self, "Reload", "code-reload", G_CALLBACK (on_toolbar_reload));
	toolbar_button (self, "Close", "code-close", G_CALLBACK (on_toolbar_close));
	toolbar_button (self, "Go to Line", "code-goto", G_CALLBACK (on_toolbar_goto));
	self->status = lrg_label_new ("No file open");
	lrg_widget_set_name (LRG_WIDGET (self->status), "code-status");
	lrg_widget_add_class (LRG_WIDGET (self->status), "dim-label");
	lrg_widget_set_valign (LRG_WIDGET (self->status), LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->status), 1.0f);
	lrg_widget_set_margin_all (LRG_WIDGET (self->status), 4.0f);
	lrg_container_add_child (LRG_CONTAINER (self->toolbar), LRG_WIDGET (self->status));
	g_object_unref (self->status);

	/* Tabs */
	self->tabs = lrg_tab_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->tabs), "code-tabs");
	g_signal_connect_object (self->tabs, "tab-changed", G_CALLBACK (on_tab_changed), self, 0);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->tabs));
	g_object_unref (self->tabs);

	/* Empty state */
	self->empty = g_steal_pointer (&empty);
	lrg_widget_set_name (LRG_WIDGET (self->empty), "code-empty");
	lrg_widget_add_class (LRG_WIDGET (self->empty), "dim-label");
	lrg_label_set_alignment (self->empty, LRG_TEXT_ALIGN_CENTER);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->empty));
	g_object_unref (self->empty);
	lrg_widget_set_visible (LRG_WIDGET (self->tabs), FALSE);

	/* Shader diagnostics of the active file */
	self->diags = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
	{
		static const LrgCustomWidgetFuncs funcs = { NULL, diag_paint, NULL, diag_event, { NULL } };

		self->diag_view = LRG_WIDGET (lrg_custom_widget_new ());
		lrg_widget_set_name (self->diag_view, "code-diagnostics");
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->diag_view), &funcs, self, NULL);
		lrg_widget_set_visible (self->diag_view, FALSE);
		lrg_container_add_child (LRG_CONTAINER (self), self->diag_view);
		g_object_unref (self->diag_view);
	}
}

/* Registers the panel's actions and methods, plus the palette and the
 * preferences action, which have no panel of their own. */
static void
install (LrgEditorCodePanel *self,
         LrgEditorApp       *app)
{
	static const GActionEntry entries[] = {
		{ "open", g_open, "s", NULL, NULL, { 0 } },
		{ "goto-line", g_goto, "i", NULL, NULL, { 0 } },
		{ "save", g_simple, NULL, NULL, NULL, { 0 } },
		{ "save-all", g_simple, NULL, NULL, NULL, { 0 } },
		{ "close", g_simple, NULL, NULL, NULL, { 0 } },
		{ "reload", g_simple, NULL, NULL, NULL, { 0 } },
	};
	LrgEditorActions *actions = app->actions;

	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);

	self->group = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (self->group), entries, G_N_ELEMENTS (entries), self);
	if (app->canvas != NULL)
		lrg_widget_insert_action_group (LRG_WIDGET (app->canvas), "code", G_ACTION_GROUP (self->group));

	lrg_editor_actions_add (actions, "code.save", "Save File", "Code", NULL, a_code_save, has_file, NULL, app);
	lrg_editor_actions_add (actions, "code.save-all", "Save All Files", "Code", NULL, a_code_save_all, has_file,
	                        NULL, app);
	lrg_editor_actions_add (actions, "code.close-tab", "Close File", "Code", NULL, a_code_close, has_file, NULL, app);
	lrg_editor_actions_add (actions, "code.reload", "Reload File from Disk", "Code", NULL, a_code_reload, has_file,
	                        NULL, app);
	lrg_editor_actions_add (actions, "code.goto-line", "Go to Line...", "Code", NULL, a_code_goto, has_file, NULL,
	                        app);
	lrg_editor_actions_add (actions, "node.open-scripts", "Open Scripts in Editor", "Node", "Ctrl+E",
	                        a_open_scripts, primary_has_scripts, NULL, app);
	lrg_editor_actions_add (actions, "edit.preferences", "Preferences...", "Edit", "Ctrl+,", a_preferences, NULL,
	                        NULL, app);

	lrg_editor_server_register_method (app->server, "code.open", m_code_open, app, NULL);
	lrg_editor_server_register_method (app->server, "code.list", m_code_list, app, NULL);
	lrg_editor_server_register_method (app->server, "code.save", m_code_save, app, NULL);
	lrg_editor_server_register_method (app->server, "preferences.open", m_preferences_open, app, NULL);
	lrg_editor_server_register_method (app->server, "palette.open", m_palette_open, app, NULL);
	lrg_editor_server_register_method (app->server, "shader.create", m_shader_create, app, NULL);
	lrg_editor_server_register_method (app->server, "shader.compile", m_shader_compile, app, NULL);
	lrg_editor_server_register_method (app->server, "shader.diagnostics", m_shader_diagnostics, app, NULL);
	lrg_editor_server_register_method (app->server, "shader.uniforms", m_shader_uniforms, app, NULL);
	lrg_editor_server_register_method (app->server, "shader.reload", m_shader_reload, app, NULL);

	/* The command palette lives on the canvas popup layer; one per app */
	if (g_object_get_data (G_OBJECT (app), PALETTE_KEY) == NULL)
		g_object_set_data_full (G_OBJECT (app), PALETTE_KEY, lrg_editor_palette_new (app),
		                        g_object_unref);
}

LrgEditorCodePanel *
lrg_editor_code_panel_new (LrgEditorApp *app)
{
	LrgEditorCodePanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_CODE_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	install (self, app);
	return self;
}

LrgEditorApp *
lrg_editor_code_panel_get_app (LrgEditorCodePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), NULL);
	return self->app;
}

/* ==========================================================================
 * API
 * ========================================================================== */

/* Shows the panel and gives the active editor keyboard focus. */
static void
present (LrgEditorCodePanel *self)
{
	CodeDoc *doc = find_doc (self, NULL, NULL);

	if (self->app == NULL)
		return;
	editor_app_show_panel (self->app, "code");
	if (doc != NULL && self->app->canvas != NULL && lrg_editor_view_state_get_dialog (self->app->view) == NULL)
		lrg_canvas_set_focused_widget (self->app->canvas, LRG_WIDGET (doc->editor));
}

gboolean
lrg_editor_code_panel_open (LrgEditorCodePanel  *self,
                            const gchar         *path,
                            gint                 line,
                            GError             **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *base = NULL;
	g_autoptr(GFile)  file = NULL;
	LrgTextEditor    *editor;
	LrgSyntax        *syntax;
	CodeDoc          *doc;
	guint             index = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	/* Already open: switch to it */
	doc = find_doc (self, path, &index);
	if (doc != NULL)
	{
		lrg_tab_view_set_active_tab (self->tabs, index);
		if (line > 0)
			lrg_editor_code_panel_goto_line (self, line);
		update_status (self);
		present (self);
		return TRUE;
	}

	abs = (self->app != NULL) ? editor_app_project_path (self->app, path) : g_canonicalize_filename (path, NULL);
	text = read_text (abs, error);
	if (text == NULL)
		return FALSE;

	/* A text editor with the syntax for the file name */
	editor = lrg_text_editor_new ();
	lrg_widget_set_name (LRG_WIDGET (editor), "code-editor");
	lrg_text_editor_set_show_line_numbers (editor, TRUE);
	lrg_text_editor_set_highlight_current_line (editor, TRUE);
	lrg_text_editor_set_auto_indent (editor, TRUE);
	syntax = syntax_for (abs);
	if (syntax != NULL)
		lrg_text_editor_set_syntax (editor, syntax);

	doc = g_new0 (CodeDoc, 1);
	doc->panel = self;
	doc->path = g_steal_pointer (&abs);
	doc->editor = editor;
	doc->saved = g_strdup (text);

	self->syncing = TRUE;
	lrg_text_editor_set_text (editor, text);
	lrg_text_editor_clear_history (editor);
	self->syncing = FALSE;
	g_signal_connect (editor, "changed", G_CALLBACK (on_editor_changed), doc);
	g_signal_connect (editor, "cursor-moved", G_CALLBACK (on_cursor_moved), doc);

	/* Reload when the file changes on disk */
	file = g_file_new_for_path (doc->path);
	doc->monitor = g_file_monitor_file (file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
	if (doc->monitor != NULL)
		g_signal_connect (doc->monitor, "changed", G_CALLBACK (on_file_changed), doc);

	g_ptr_array_add (self->docs, doc);
	base = g_path_get_basename (doc->path);
	lrg_tab_view_add_tab (self->tabs, base, LRG_WIDGET (editor));
	g_object_unref (editor);
	lrg_tab_view_set_active_tab (self->tabs, self->docs->len - 1);
	lrg_text_editor_set_cursor (editor, (guint) MAX (line - 1, 0), 0);
	if (line > 1)
		lrg_text_editor_scroll_to_line (editor, (guint) MAX (line - 4, 0), FALSE);
	else
	{
		/* a shader file with errors opens at the first one */
		g_autoptr(GPtrArray) diags = active_diags (self);

		if (diags->len > 0 && ((CodeDiag *) g_ptr_array_index (diags, 0))->line > 0)
		{
			CodeDiag *d = g_ptr_array_index (diags, 0);

			lrg_text_editor_set_cursor (editor, d->line - 1, d->column > 0 ? d->column - 1 : 0);
			lrg_text_editor_scroll_to_line (editor, d->line > 4 ? d->line - 4 : 0, FALSE);
		}
	}
	update_status (self);
	present (self);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Opened %s", doc->path);
	return TRUE;
}

gboolean
lrg_editor_code_panel_save (LrgEditorCodePanel  *self,
                            const gchar         *path,
                            GError             **error)
{
	CodeDoc *doc;

	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), FALSE);

	doc = find_doc (self, path, NULL);
	if (doc == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s is not open", path != NULL ? path : "No file");
		return FALSE;
	}
	if (!doc_save (doc, error))
		return FALSE;
	if (self->app != NULL)
		editor_app_set_status (self->app, "Saved %s", doc->path);
	if (is_shader_document (doc->path) || is_shader_stage (doc->path))
		shader_check (self, doc->path);
	return TRUE;
}

gboolean
lrg_editor_code_panel_save_all (LrgEditorCodePanel  *self,
                                GError             **error)
{
	gboolean ok = TRUE;
	guint    i;

	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), FALSE);

	for (i = 0; i < self->docs->len; i++)
	{
		CodeDoc *doc = g_ptr_array_index (self->docs, i);

		if (!doc->dirty)
			continue;
		if (!doc_save (doc, ok ? error : NULL))
			ok = FALSE;
		else if (is_shader_document (doc->path) || is_shader_stage (doc->path))
			shader_check (self, doc->path);
	}
	return ok;
}

gboolean
lrg_editor_code_panel_close (LrgEditorCodePanel *self,
                             const gchar        *path,
                             gboolean            force)
{
	CodeDoc *doc;
	guint    index = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), FALSE);

	doc = find_doc (self, path, &index);
	if (doc == NULL)
		return FALSE;
	doc_refresh_dirty (doc);
	if (doc->dirty && !force)
	{
		ask_close (self, doc);
		return FALSE;
	}

	/* The tab view drops the editor; the doc goes with it */
	g_signal_handlers_disconnect_by_data (doc->editor, doc);
	doc->editor = NULL;
	g_ptr_array_remove_index (self->docs, index);
	lrg_tab_view_remove_tab (self->tabs, index);
	if (self->docs->len > 0)
		lrg_tab_view_set_active_tab (self->tabs, MIN (index, self->docs->len - 1));
	update_status (self);
	return TRUE;
}

void
lrg_editor_code_panel_goto_line (LrgEditorCodePanel *self,
                                 gint                line)
{
	CodeDoc *doc;
	guint    target;

	g_return_if_fail (LRG_IS_EDITOR_CODE_PANEL (self));

	doc = find_doc (self, NULL, NULL);
	if (doc == NULL || line < 1)
		return;
	target = MIN ((guint) line - 1, MAX (lrg_text_editor_get_line_count (doc->editor), 1) - 1);
	lrg_text_editor_set_cursor (doc->editor, target, 0);
	lrg_text_editor_scroll_to_line (doc->editor, target > 3 ? target - 3 : 0, TRUE);
	update_status (self);
	present (self);
}

guint
lrg_editor_code_panel_get_n_files (LrgEditorCodePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), 0);
	return self->docs->len;
}

const gchar *
lrg_editor_code_panel_get_path (LrgEditorCodePanel *self,
                                guint               index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), NULL);

	if (index >= self->docs->len)
		return NULL;
	return ((CodeDoc *) g_ptr_array_index (self->docs, index))->path;
}

const gchar *
lrg_editor_code_panel_get_active_path (LrgEditorCodePanel *self)
{
	CodeDoc *doc;

	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), NULL);

	doc = find_doc (self, NULL, NULL);
	return (doc != NULL) ? doc->path : NULL;
}

LrgTextEditor *
lrg_editor_code_panel_get_editor (LrgEditorCodePanel *self,
                                  const gchar        *path)
{
	CodeDoc *doc;

	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), NULL);

	doc = find_doc (self, path, NULL);
	return (doc != NULL) ? doc->editor : NULL;
}

gboolean
lrg_editor_code_panel_is_dirty (LrgEditorCodePanel *self,
                                const gchar        *path)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_CODE_PANEL (self), FALSE);

	if (path != NULL)
	{
		CodeDoc *doc = find_doc (self, path, NULL);

		if (doc == NULL)
			return FALSE;
		doc_refresh_dirty (doc);
		return doc->dirty;
	}
	for (i = 0; i < self->docs->len; i++)
	{
		CodeDoc *doc = g_ptr_array_index (self->docs, i);

		doc_refresh_dirty (doc);
		if (doc->dirty)
			return TRUE;
	}
	return FALSE;
}
