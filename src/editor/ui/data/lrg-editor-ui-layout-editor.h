/* lrg-editor-ui-layout-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorUiLayoutEditor: the data editor of #LrgUILoader documents.
 *
 * Three panes: an outline of the built widget tree, the YAML in an
 * #LrgTextEditor with YAML highlighting, and a live preview built by an
 * #LrgUILoader with source tracking on. Typing rebuilds the preview once
 * the edits pause (a short debounce on the canvas clock); a document that
 * does not load keeps the last good preview and shows the loader's error
 * in a banner over it, with a mark on the offending line of the YAML.
 *
 * Selection is shared by the three panes: clicking a widget in the
 * preview (pick mode, the default; the Pick chip switches to interacting
 * with the widgets) selects its outline row and puts the caret on its
 * YAML node (lrg_ui_loader_get_source_location()); selecting an outline
 * row or moving the caret into a node highlights the widget in the
 * preview. Paths are the loader's JSON pointers ("/children/2"), which are
 * also #LrgEditorDocument paths.
 *
 * The text is written into the #LrgEditorDocument (one undo step per
 * pause in typing), so undo, save, validation and the IPC data.* methods
 * work on the same model as every other data editor; a change from
 * outside (undo, IPC, reload) replaces the text.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-text-editor.h"
#include "../../../ui/lrg-ui-loader.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_UI_LAYOUT_EDITOR (lrg_editor_ui_layout_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorUiLayoutEditor, lrg_editor_ui_layout_editor, LRG, EDITOR_UI_LAYOUT_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_ui_layout_editor_new:
 * @app: (nullable): the editor application
 * @document: a UI layout document
 *
 * Creates the editor of @document. lrg_editor_data_editor_open() is the
 * usual way to get one; this is for embedding and tests.
 *
 * Returns: (transfer full): a new #LrgEditorUiLayoutEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorUiLayoutEditor * lrg_editor_ui_layout_editor_new (LrgEditorApp      *app,
                                                           LrgEditorDocument *document);

/**
 * lrg_editor_ui_layout_editor_get_text_editor:
 * @self: an #LrgEditorUiLayoutEditor
 *
 * Returns: (transfer none): the YAML editor
 */
LRG_AVAILABLE_IN_ALL
LrgTextEditor * lrg_editor_ui_layout_editor_get_text_editor (LrgEditorUiLayoutEditor *self);

/**
 * lrg_editor_ui_layout_editor_get_loader:
 * @self: an #LrgEditorUiLayoutEditor
 *
 * Returns: (transfer none): the loader that builds the preview
 */
LRG_AVAILABLE_IN_ALL
LrgUILoader * lrg_editor_ui_layout_editor_get_loader (LrgEditorUiLayoutEditor *self);

/**
 * lrg_editor_ui_layout_editor_get_preview:
 * @self: an #LrgEditorUiLayoutEditor
 *
 * Returns: (transfer none) (nullable): the root widget of the preview (the
 *   last document that loaded), or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_ui_layout_editor_get_preview (LrgEditorUiLayoutEditor *self);

/**
 * lrg_editor_ui_layout_editor_apply:
 * @self: an #LrgEditorUiLayoutEditor
 *
 * Applies the text now instead of after the typing pause: rebuilds the
 * preview and writes the text into the document.
 *
 * Returns: %TRUE when the text loaded (the preview is current)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_ui_layout_editor_apply (LrgEditorUiLayoutEditor *self);

/**
 * lrg_editor_ui_layout_editor_get_pending:
 * @self: an #LrgEditorUiLayoutEditor
 *
 * Returns: whether typed text waits for the debounce to apply it
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_ui_layout_editor_get_pending (LrgEditorUiLayoutEditor *self);

/**
 * lrg_editor_ui_layout_editor_get_error:
 * @self: an #LrgEditorUiLayoutEditor
 * @line: (out) (optional): 1-based line of the error, 0 when unknown
 *
 * Returns: (nullable): the message of the last load failure, %NULL when
 *   the preview is current
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_ui_layout_editor_get_error (LrgEditorUiLayoutEditor *self,
                                                     guint                   *line);

/**
 * lrg_editor_ui_layout_editor_get_selected_path:
 * @self: an #LrgEditorUiLayoutEditor
 *
 * Returns: (nullable): the JSON pointer of the selected widget node ("" is
 *   the root), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_ui_layout_editor_get_selected_path (LrgEditorUiLayoutEditor *self);

/**
 * lrg_editor_ui_layout_editor_select_path:
 * @self: an #LrgEditorUiLayoutEditor
 * @path: (nullable): a widget node's JSON pointer, %NULL to clear
 *
 * Selects the widget built from @path in the three panes and puts the
 * caret on its node.
 *
 * Returns: %TRUE when a widget was built from @path
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_ui_layout_editor_select_path (LrgEditorUiLayoutEditor *self,
                                                  const gchar             *path);

/**
 * lrg_editor_ui_layout_editor_get_pick_mode:
 * @self: an #LrgEditorUiLayoutEditor
 *
 * Returns: whether clicks in the preview select widgets (%TRUE) or reach
 *   them (%FALSE)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_ui_layout_editor_get_pick_mode (LrgEditorUiLayoutEditor *self);

/**
 * lrg_editor_ui_layout_editor_set_pick_mode:
 * @self: an #LrgEditorUiLayoutEditor
 * @pick: %TRUE to select widgets by clicking them, %FALSE to use them
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_ui_layout_editor_set_pick_mode (LrgEditorUiLayoutEditor *self,
                                                gboolean                 pick);

/**
 * lrg_editor_ui_layout_editor_get_n_outline_rows:
 * @self: an #LrgEditorUiLayoutEditor
 *
 * Returns: the number of widget nodes in the outline (all levels)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_ui_layout_editor_get_n_outline_rows (LrgEditorUiLayoutEditor *self);

/**
 * lrg_editor_ui_layout_editor_place:
 * @self: a UI layout editor
 * @widget: the widget `name:`
 * @x: new x
 * @y: new y
 * @error: (nullable): return location for a #GError
 *
 * Rewrites the widget's x and y with lrg_ui_layout_place() and replaces
 * the document. The preview's mouse-up calls this after a drag.
 *
 * Returns: %TRUE when the document changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_ui_layout_editor_place (LrgEditorUiLayoutEditor  *self,
                                            const gchar              *widget,
                                            gfloat                    x,
                                            gfloat                    y,
                                            GError                  **error);

G_END_DECLS
