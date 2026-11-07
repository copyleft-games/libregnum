/* lrg-editor-locale-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorLocaleEditor: the data editor of locale (string table) files.
 * Opening one locale loads every locale file next to it into a key x
 * language grid; the opened file is the reference language and the cells
 * other languages lack are highlighted. Strings import from and export to
 * CSV.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_LOCALE_EDITOR (lrg_editor_locale_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorLocaleEditor, lrg_editor_locale_editor, LRG, EDITOR_LOCALE_EDITOR,
                      LrgEditorDataEditor)

/**
 * lrg_editor_locale_editor_new:
 * @app: (nullable): the editor application
 * @document: a locale document; it becomes the reference language and the
 *   other locale files of its folder are loaded beside it
 *
 * Returns: (transfer full): a new #LrgEditorLocaleEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorLocaleEditor * lrg_editor_locale_editor_new (LrgEditorApp      *app,
                                                      LrgEditorDocument *document);

/**
 * lrg_editor_locale_editor_get_n_languages:
 * @self: an #LrgEditorLocaleEditor
 *
 * Returns: the number of language columns (the reference first)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_locale_editor_get_n_languages (LrgEditorLocaleEditor *self);

/**
 * lrg_editor_locale_editor_get_language:
 * @self: an #LrgEditorLocaleEditor
 * @index: a column index
 *
 * Returns: (nullable): the language code of column @index ("en")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_locale_editor_get_language (LrgEditorLocaleEditor *self,
                                                     guint                  index);

/**
 * lrg_editor_locale_editor_get_language_document:
 * @self: an #LrgEditorLocaleEditor
 * @code: a language code
 *
 * Returns: (transfer none) (nullable): the document of @code
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocument * lrg_editor_locale_editor_get_language_document (LrgEditorLocaleEditor *self,
                                                                    const gchar           *code);

/**
 * lrg_editor_locale_editor_get_keys:
 * @self: an #LrgEditorLocaleEditor
 *
 * The rows of the grid: every key of every language, the reference's
 * order first; plural entries give one row per form, written
 * "key[form]".
 *
 * Returns: (transfer full): the row keys
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_locale_editor_get_keys (LrgEditorLocaleEditor *self);

/**
 * lrg_editor_locale_editor_get_text:
 * @self: an #LrgEditorLocaleEditor
 * @key: a row key (see lrg_editor_locale_editor_get_keys())
 * @code: a language code
 *
 * Returns: (nullable): the text, %NULL when the language lacks @key
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_locale_editor_get_text (LrgEditorLocaleEditor *self,
                                                 const gchar           *key,
                                                 const gchar           *code);

/**
 * lrg_editor_locale_editor_is_missing:
 * @self: an #LrgEditorLocaleEditor
 * @key: a row key
 * @code: a language code
 *
 * Returns: whether the reference has @key and @code does not (the cells
 *   shown highlighted)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_locale_editor_is_missing (LrgEditorLocaleEditor *self,
                                              const gchar           *key,
                                              const gchar           *code);

/**
 * lrg_editor_locale_editor_set_text:
 * @self: an #LrgEditorLocaleEditor
 * @key: a row key ("greeting" or "items[one]")
 * @code: a language code
 * @text: (nullable): the text, %NULL removes the entry
 * @error: return location for a #GError
 *
 * Writes one cell as one undoable command of that language's document.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_locale_editor_set_text (LrgEditorLocaleEditor  *self,
                                            const gchar            *key,
                                            const gchar            *code,
                                            const gchar            *text,
                                            GError                **error);

/**
 * lrg_editor_locale_editor_select:
 * @self: an #LrgEditorLocaleEditor
 * @key: (nullable): a row key, %NULL to clear
 * @code: (nullable): a language code (the reference when %NULL)
 *
 * Selects a cell and loads it into the edit bar.
 *
 * Returns: %FALSE when @key or @code is unknown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_locale_editor_select (LrgEditorLocaleEditor *self,
                                          const gchar           *key,
                                          const gchar           *code);

/**
 * lrg_editor_locale_editor_get_selected_key:
 * @self: an #LrgEditorLocaleEditor
 *
 * Returns: (nullable): the selected row key
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_locale_editor_get_selected_key (LrgEditorLocaleEditor *self);

/**
 * lrg_editor_locale_editor_get_selected_language:
 * @self: an #LrgEditorLocaleEditor
 *
 * Returns: (nullable): the selected language code
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_locale_editor_get_selected_language (LrgEditorLocaleEditor *self);

/**
 * lrg_editor_locale_editor_get_cell_rect:
 * @self: an #LrgEditorLocaleEditor
 * @key: a row key
 * @code: a language code
 * @x: (out) (optional): left, canvas coordinates
 * @y: (out) (optional): top
 * @width: (out) (optional): width
 * @height: (out) (optional): height
 *
 * Returns: %FALSE when the cell is not on screen
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_locale_editor_get_cell_rect (LrgEditorLocaleEditor *self,
                                                 const gchar           *key,
                                                 const gchar           *code,
                                                 gfloat                *x,
                                                 gfloat                *y,
                                                 gfloat                *width,
                                                 gfloat                *height);

/**
 * lrg_editor_locale_editor_export_csv:
 * @self: an #LrgEditorLocaleEditor
 *
 * The grid as CSV (RFC 4180, ','): a header "key,<code>,<code>..." and one
 * row per key; a missing entry is an empty field.
 *
 * Returns: (transfer full): the CSV text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_locale_editor_export_csv (LrgEditorLocaleEditor *self);

/**
 * lrg_editor_locale_editor_import_csv:
 * @self: an #LrgEditorLocaleEditor
 * @csv: CSV text with a "key,<code>..." header (read by lrg_csv_parse())
 * @error: return location for a #GError
 *
 * Writes every non-empty field of the columns whose code is one of the
 * loaded languages (other columns are skipped with a warning), as one
 * undo step per language.
 *
 * Returns: the number of cells written, or -1 on a parse error
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_locale_editor_import_csv (LrgEditorLocaleEditor  *self,
                                          const gchar            *csv,
                                          GError                **error);

G_END_DECLS
