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
 * Format versions: a file written by 0.8.1 carries `settings-version: 2`
 * and may use the 0.8.1 keys (`ui-layout`, `script-editor`,
 * `viewport-layout`, theme preset names or paths, accelerator-syntax
 * keybindings). A file without the key is a 0.8.0 file and loads with
 * version 1; it is still read as is (legacy theme names and "Ctrl+S"
 * keybindings keep working) and the editor migrates it with
 * lrg_editor_settings_migrate() (lrg-editor-settings-migrate.h).
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

/**
 * LRG_EDITOR_SETTINGS_VERSION:
 *
 * The settings file format this version writes (#LrgEditorSettings:settings-version).
 * Version 1 is the 0.8.0 format (no version key); version 2 adds
 * ui-layout, script-editor, viewport-layout, theme preset names and
 * accelerator-syntax keybindings.
 */
#define LRG_EDITOR_SETTINGS_VERSION (2)

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
 * The stored override exactly as written. Since 0.8.1 the editor writes
 * accelerator syntax ("<Ctrl>s", alternatives separated by "|"); files
 * from 0.8.0 hold the older "Ctrl+S" syntax, which is still accepted
 * everywhere. Use lrg_editor_settings_get_keybinding_accel() to read the
 * value converted to accelerators.
 *
 * Returns: (transfer none) (nullable): the user's shortcut for @action
 *   (e.g. "<Ctrl>s" or "Ctrl+S"), "" if the user unbound it, or %NULL to
 *   use the action's default
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
 *
 * Stores an override as given (either syntax is accepted; new code should
 * write accelerators, e.g. from lrg_editor_shortcut_to_accel()) and emits
 * #LrgEditorSettings::keybindings-changed.
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

/**
 * lrg_editor_settings_get_keybinding_accel:
 * @self: an #LrgEditorSettings
 * @action: an action id
 *
 * The override for @action converted to accelerator syntax with
 * lrg_editor_shortcut_to_accel(), whichever syntax it was stored in.
 *
 * Returns: (transfer full) (nullable): the accelerators ("|"-separated),
 *   "" if the user unbound @action, or %NULL when there is no override or
 *   the stored text is not a valid shortcut
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_settings_get_keybinding_accel (LrgEditorSettings *self,
                                                  const gchar       *action);

/* ==========================================================================
 * 0.8.1 keys
 * ========================================================================== */

/**
 * lrg_editor_settings_get_settings_version:
 * @self: an #LrgEditorSettings
 *
 * Returns: the file format version (see #LrgEditorSettings:settings-version)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_settings_get_settings_version (LrgEditorSettings *self);

/**
 * lrg_editor_settings_set_settings_version:
 * @self: an #LrgEditorSettings
 * @version: the format version (at least 1)
 *
 * Sets the format version; the migration sets %LRG_EDITOR_SETTINGS_VERSION
 * after rewriting a 0.8.0 file.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_settings_set_settings_version (LrgEditorSettings *self,
                                               gint               version);

/**
 * lrg_editor_settings_get_ui_layout:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer none) (nullable): the saved #LrgDockArea YAML
 *   (#LrgEditorSettings:ui-layout)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_ui_layout (LrgEditorSettings *self);

/**
 * lrg_editor_settings_get_script_editor:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer none): "internal", "external" or "emacs"
 *   (#LrgEditorSettings:script-editor)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_script_editor (LrgEditorSettings *self);

/**
 * lrg_editor_settings_get_viewport_layout:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer none): "single", "split-h", "split-v" or "quad"
 *   (#LrgEditorSettings:viewport-layout)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_viewport_layout (LrgEditorSettings *self);

/**
 * lrg_editor_settings_get_theme_preset:
 * @self: an #LrgEditorSettings
 *
 * The built-in #LrgTheme preset the #LrgEditorSettings:theme value
 * resolves to: a preset name is returned as is; the 0.8.0 names map
 * "mocha" to "catppuccin-mocha", "latte" to "catppuccin-latte", and
 * "macchiato" and "frappe" to "catppuccin-mocha" (their closest preset;
 * the settings migration rewrites them to "catppuccin-macchiato" and
 * "catppuccin-frappe", which name shipped theme files); any other bare
 * name that lrg_editor_settings_find_theme_file() finds is a file theme
 * (%NULL here); an unknown name gives "catppuccin-mocha".
 *
 * Returns: (transfer none) (nullable): the preset name, or %NULL when the
 *   theme is a file (see lrg_editor_settings_get_theme_file())
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_theme_preset (LrgEditorSettings *self);

/**
 * lrg_editor_settings_get_theme_file:
 * @self: an #LrgEditorSettings
 *
 * The theme YAML path when #LrgEditorSettings:theme names a file: the
 * value itself when it contains a directory separator or ends in ".yaml"
 * / ".yml", or, for a bare name that is neither a preset nor a 0.8.0
 * name (e.g. "catppuccin-macchiato"), the file
 * lrg_editor_settings_find_theme_file() finds for it. The lookup is
 * cached per theme name.
 *
 * Returns: (transfer none) (nullable): the path, or %NULL for a preset,
 *   a legacy name or a name without a theme file
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_theme_file (LrgEditorSettings *self);

/**
 * lrg_editor_settings_find_theme_file:
 * @name: a bare theme name such as "catppuccin-macchiato" (".yaml" is
 *   appended unless it ends in ".yaml" or ".yml")
 *
 * Looks up the theme file NAME.yaml in, in order: each directory of
 * `$LRG_EDITOR_THEME_PATH` (separated like `$PATH`),
 * `$XDG_DATA_HOME/libregnum/editor/themes`, the install prefix's
 * `share/libregnum/editor/themes` (when the library was built with
 * `LRG_DATADIR`), `libregnum/editor/themes` under each of
 * `$XDG_DATA_DIRS`, and finally `data/editor/themes` in the executable's
 * directory or one of its parents (an uninstalled build tree). The
 * editor ships "catppuccin-macchiato" and "catppuccin-frappe" there.
 *
 * Returns: (transfer full) (nullable): the file's path, or %NULL when
 *   @name is not a bare name or no directory has it
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_settings_find_theme_file (const gchar *name);

/**
 * lrg_editor_settings_get_viewport_cameras:
 * @self: an #LrgEditorSettings
 *
 * Returns: (transfer none) (nullable): the saved viewport cameras
 *   (#LrgEditorSettings:viewport-cameras), "" when none are saved
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_viewport_cameras (LrgEditorSettings *self);

/**
 * lrg_editor_settings_get_path:
 * @self: an #LrgEditorSettings
 *
 * The file lrg_editor_settings_load() last read (or tried to read: a
 * missing file is remembered too), which lrg_editor_settings_save() and
 * lrg_editor_settings_migrate() use when given %NULL.
 *
 * Returns: (transfer none) (nullable): the path, or %NULL if nothing was
 *   loaded
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_get_path (LrgEditorSettings *self);

G_END_DECLS
