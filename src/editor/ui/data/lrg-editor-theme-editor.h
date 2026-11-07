/* lrg-editor-theme-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorThemeEditor: the data editor of LrgTheme YAML files. A property
 * grid over the theme's general keys, colour roles and metrics, a live
 * gallery of widgets drawn with the edited theme, and "Use for editor",
 * which makes the file the editor's own theme.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-theme.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_THEME_EDITOR (lrg_editor_theme_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorThemeEditor, lrg_editor_theme_editor, LRG, EDITOR_THEME_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_theme_editor_new:
 * @app: (nullable): the editor application
 * @document: a theme document
 *
 * Returns: (transfer full): a new #LrgEditorThemeEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorThemeEditor * lrg_editor_theme_editor_new (LrgEditorApp      *app,
                                                    LrgEditorDocument *document);

/**
 * lrg_editor_theme_editor_get_preview_theme:
 * @self: an #LrgEditorThemeEditor
 *
 * The theme built from the document as it is now (the last version that
 * loaded, while the current one has an error).
 *
 * Returns: (transfer none): the preview theme
 */
LRG_AVAILABLE_IN_ALL
LrgTheme * lrg_editor_theme_editor_get_preview_theme (LrgEditorThemeEditor *self);

/**
 * lrg_editor_theme_editor_get_preview_error:
 * @self: an #LrgEditorThemeEditor
 *
 * Returns: (nullable): why the current document does not load as a theme,
 *   %NULL when it does
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_theme_editor_get_preview_error (LrgEditorThemeEditor *self);

/**
 * lrg_editor_theme_editor_get_gallery:
 * @self: an #LrgEditorThemeEditor
 *
 * Returns: (transfer none): the widget gallery drawn with the preview theme
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_theme_editor_get_gallery (LrgEditorThemeEditor *self);

/**
 * lrg_editor_theme_editor_use_for_editor:
 * @self: an #LrgEditorThemeEditor
 * @error: return location for a #GError
 *
 * Saves the file when it has unsaved changes, sets the editor's theme
 * setting to its path and applies it. While it is the editor's theme,
 * every later save re-applies it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_theme_editor_use_for_editor (LrgEditorThemeEditor  *self,
                                                 GError               **error);

G_END_DECLS
