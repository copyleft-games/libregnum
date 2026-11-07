/* lrg-editor-preferences.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPreferences: the editor's preferences.
 *
 * The content of the Preferences dialog (Edit > Preferences, Ctrl+,;
 * ui.state dialog "preferences"): an #LrgTabView with the pages
 *
 *  - "general": autosave, undo history, reload and confirmation behaviour;
 *  - "appearance": a gallery of the built-in themes (the five #LrgTheme
 *    presets plus the shipped catppuccin-macchiato and catppuccin-frappe
 *    files) with "Load YAML..." for any theme file, UI scale and font;
 *  - "external-editor": the external editor preset, its command template,
 *    the terminal, where scripts open (#LrgEditorSettings:script-editor)
 *    and Emacs integration, with a live preview of the command line;
 *  - "viewport": snapping, grid, camera navigation scheme, speeds, field
 *    of view and invert Y;
 *  - "keybindings": every action with its shortcuts, searchable, with
 *    "press a key" capture, alternatives, per-action and global reset and
 *    conflicting shortcuts highlighted;
 *  - "server": the IPC server switch, socket path and status;
 *
 * plus "Reset All" and "Edit editor.yaml" buttons. Settings pages are
 * #LrgPropertyGrid views of the app's #LrgEditorSettings (through an
 * #LrgObjectPropertySource), so every change is written at once and
 * applies live: a theme switch re-themes the editor immediately, snapping
 * and camera values reach the viewport. Closing the dialog saves
 * editor.yaml.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-property-grid.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_PREFERENCES (lrg_editor_preferences_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorPreferences, lrg_editor_preferences, LRG, EDITOR_PREFERENCES, LrgContainer)

/**
 * lrg_editor_preferences_new:
 * @app: the editor application whose settings it edits
 *
 * Creates the preferences pages (without a dialog around them; see
 * lrg_editor_preferences_present()).
 *
 * Returns: (transfer full): a new #LrgEditorPreferences
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPreferences * lrg_editor_preferences_new (LrgEditorApp *app);

/**
 * lrg_editor_preferences_get_app:
 * @self: an #LrgEditorPreferences
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_preferences_get_app (LrgEditorPreferences *self);

/**
 * lrg_editor_preferences_present:
 * @app: an #LrgEditorApp
 * @page: (nullable): the page to show ("general", "appearance",
 *   "external-editor", "viewport", "keybindings", "server"), or %NULL
 *   for the first
 *
 * Opens the Preferences dialog (closing any other dialog). ui.state
 * reports it as dialog "preferences".
 *
 * Returns: (transfer none) (nullable): the dialog's preferences widget,
 *   or %NULL when the app has no canvas
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPreferences * lrg_editor_preferences_present (LrgEditorApp *app,
                                                       const gchar  *page);

/**
 * lrg_editor_preferences_get_page:
 * @self: an #LrgEditorPreferences
 *
 * Returns: (transfer none): the id of the page that shows
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_preferences_get_page (LrgEditorPreferences *self);

/**
 * lrg_editor_preferences_set_page:
 * @self: an #LrgEditorPreferences
 * @page: a page id (see lrg_editor_preferences_present())
 *
 * Returns: %TRUE if @page exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_preferences_set_page (LrgEditorPreferences *self,
                                          const gchar          *page);

/**
 * lrg_editor_preferences_get_grid:
 * @self: an #LrgEditorPreferences
 * @page: a settings page id ("general", "appearance", "external-editor",
 *   "viewport" or "server")
 *
 * Returns: (transfer none) (nullable): the property grid of that page
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_preferences_get_grid (LrgEditorPreferences *self,
                                                   const gchar          *page);

/**
 * lrg_editor_preferences_reset_all:
 * @self: an #LrgEditorPreferences
 *
 * Restores every preference and keybinding to its default (the panel
 * layout and recent files are kept) and applies the result.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_preferences_reset_all (LrgEditorPreferences *self);

/**
 * lrg_editor_preferences_get_capturing:
 * @self: an #LrgEditorPreferences
 *
 * Returns: (transfer none) (nullable): the action whose shortcut is being
 *   captured on the Keybindings page ("press a key"), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_preferences_get_capturing (LrgEditorPreferences *self);

/**
 * lrg_editor_preferences_create_keymap:
 * @app: an #LrgEditorApp
 * @editable: whether shortcuts can be captured, added, removed and reset
 *   (the Keybindings page) or are only listed (Help > Keyboard Shortcuts)
 *
 * Builds the searchable shortcut table: a filter field (matching labels,
 * categories, ids and key names) above every action grouped by category
 * with its shortcuts; conflicting shortcuts are drawn in the warning
 * colour. The read-only table also lists the viewport's mouse
 * navigation. It follows accelerator changes live.
 *
 * Returns: (transfer full): the table widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_preferences_create_keymap (LrgEditorApp *app,
                                                  gboolean      editable);

G_END_DECLS
