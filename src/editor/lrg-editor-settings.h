/* lrg-editor-settings.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Per-user editor preferences.
 *
 * LrgEditorSettings holds everything that belongs to the person rather than
 * the project: which external code editor to launch and how, the terminal
 * used for terminal editors, Emacs integration, UI scale and theme, camera
 * and snap defaults, autosave, recent files, keybinding overrides and the
 * saved panel layout.
 *
 * Settings live in a YAML file, by default
 * `$XDG_CONFIG_HOME/libregnum/editor.yaml` (override with the
 * `LRG_EDITOR_CONFIG` environment variable). Every GObject property of
 * this class is a key in that file using its canonical property name, so
 * the file is easy to edit by hand; unknown keys are preserved on save.
 *
 * Project-wide settings (default level, asset dirs, run command) live in
 * the project's `project.lrgproj` instead (see #LrgProject).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_SETTINGS_MAX_RECENT:
 *
 * Maximum number of entries kept in each recent-files list.
 */
#define LRG_EDITOR_SETTINGS_MAX_RECENT (12)

#define LRG_TYPE_EDITOR_SETTINGS (lrg_editor_settings_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSettings, lrg_editor_settings, LRG, EDITOR_SETTINGS, GObject)

/**
 * lrg_editor_settings_new:
 *
 * Creates settings with built-in defaults (nothing is read from disk).
 *
 * Returns: (transfer full): a new #LrgEditorSettings
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSettings * lrg_editor_settings_new (void);

/**
 * lrg_editor_settings_get_default_path:
 *
 * Returns: (transfer full): `$LRG_EDITOR_CONFIG`, or
 *   `$XDG_CONFIG_HOME/libregnum/editor.yaml`
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_settings_get_default_path (void);

/**
 * lrg_editor_settings_load:
 * @self: an #LrgEditorSettings
 * @path: (nullable): file to read, or %NULL for the default path
 * @error: (nullable): return location for an error
 *
 * Reads settings from YAML. A missing file is not an error (defaults stay
 * in effect). Keys with the wrong type are skipped with a warning in
 * @error-less form (they are logged), so one bad value never discards the
 * rest of the file.
 *
 * Returns: %TRUE unless the file exists but cannot be parsed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_settings_load (LrgEditorSettings  *self,
                                   const gchar        *path,
                                   GError            **error);

/**
 * lrg_editor_settings_save:
 * @self: an #LrgEditorSettings
 * @path: (nullable): file to write, or %NULL for the path last loaded
 *   (or the default path)
 * @error: (nullable): return location for an error
 *
 * Writes all settings atomically, creating parent directories.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_settings_save (LrgEditorSettings  *self,
                                   const gchar        *path,
                                   GError            **error);

/**
 * lrg_editor_settings_to_data:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer full): the settings as YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_settings_to_data (LrgEditorSettings *self);

/**
 * lrg_editor_settings_load_from_data:
 * @self: an #LrgEditorSettings
 * @data: YAML text
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_settings_load_from_data (LrgEditorSettings  *self,
                                             const gchar        *data,
                                             GError            **error);

/**
 * lrg_editor_settings_reset:
 * @self: an #LrgEditorSettings
 *
 * Restores every property to its default value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_settings_reset (LrgEditorSettings *self);

/* ==========================================================================
 * External editor
 * ========================================================================== */

/**
 * lrg_editor_settings_get_external_editor:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer none): the external editor preset id ("auto" to follow
 *   `$VISUAL`/`$EDITOR`, "custom" for #LrgEditorSettings:external-editor-command,
 *   or a preset such as "emacsclient", "nvim", "codium"; see
 *   lrg_external_editor_list_presets())
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_external_editor (LrgEditorSettings *self);

/**
 * lrg_editor_settings_get_external_editor_command:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer none) (nullable): the custom command template using the
 *   placeholders `{file}`, `{line}`, `{col}`, `{project}` and `{dir}`
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_external_editor_command (LrgEditorSettings *self);

/**
 * lrg_editor_settings_get_terminal_command:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer none) (nullable): the terminal wrapper used to run
 *   terminal editors, e.g. "gst -e" or "foot"; %NULL or empty to detect
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_terminal_command (LrgEditorSettings *self);

/**
 * lrg_editor_settings_get_emacs_integration:
 * @self: an #LrgEditorSettings
 *
 * Returns: %TRUE if files should be opened in a connected Emacs (via the
 *   editor server) before falling back to launching a process
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_settings_get_emacs_integration (LrgEditorSettings *self);

/* ==========================================================================
 * Recent files
 * ========================================================================== */

/**
 * lrg_editor_settings_add_recent_project:
 * @self: an #LrgEditorSettings
 * @path: a project directory
 *
 * Moves @path to the front of the recent projects list.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_settings_add_recent_project (LrgEditorSettings *self,
                                             const gchar       *path);

/**
 * lrg_editor_settings_add_recent_level:
 * @self: an #LrgEditorSettings
 * @path: a level file
 *
 * Moves @path to the front of the recent levels list.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_settings_add_recent_level (LrgEditorSettings *self,
                                           const gchar       *path);

/**
 * lrg_editor_settings_get_recent_projects:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer none) (array zero-terminated=1): recent projects,
 *   newest first
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_editor_settings_get_recent_projects (LrgEditorSettings *self);

/**
 * lrg_editor_settings_get_recent_levels:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer none) (array zero-terminated=1): recent levels,
 *   newest first
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_editor_settings_get_recent_levels (LrgEditorSettings *self);

/* ==========================================================================
 * Keybindings
 * ========================================================================== */

/**
 * lrg_editor_settings_get_keybinding:
 * @self: an #LrgEditorSettings
 * @action: an action id, e.g. "file.save"
 *
 * Returns: (transfer none) (nullable): the user's shortcut for @action
 *   (e.g. "Ctrl+S"), "" if the user unbound it, or %NULL to use the
 *   action's default
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_keybinding (LrgEditorSettings *self,
                                                  const gchar       *action);

/**
 * lrg_editor_settings_set_keybinding:
 * @self: an #LrgEditorSettings
 * @action: an action id
 * @shortcut: (nullable): the shortcut, "" to unbind, or %NULL to restore
 *   the default
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_settings_set_keybinding (LrgEditorSettings *self,
                                         const gchar       *action,
                                         const gchar       *shortcut);

/**
 * lrg_editor_settings_list_keybindings:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer container) (element-type utf8): sorted action ids that
 *   have user overrides
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_editor_settings_list_keybindings (LrgEditorSettings *self);

G_END_DECLS
