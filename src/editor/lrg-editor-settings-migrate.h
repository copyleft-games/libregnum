/* lrg-editor-settings-migrate.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Migration of 0.8.0 editor settings (editor.yaml) to the 0.8.1 format.
 *
 * A settings file written by 0.8.0 has no `settings-version` key and
 * loads as version 1 (see #LrgEditorSettings:settings-version).
 * lrg_editor_settings_migrate() brings it to %LRG_EDITOR_SETTINGS_VERSION:
 *
 *   - the old compact dock layout string under `layout`
 *     ("split(v,0.20,tabs(0,hierarchy),...)", written by the immediate-mode
 *     #LrgGuiDock) is converted to #LrgDockArea layout YAML under
 *     `ui-layout`; `layout` itself is left untouched so 0.8.0 still finds
 *     its layout after a downgrade;
 *   - keybinding overrides in the 0.8.0 "Ctrl+Shift+Z" syntax are
 *     rewritten as accelerators ("<Ctrl><Shift>z"); values that cannot be
 *     converted are dropped (the action gets its default back) with a
 *     log message naming the action;
 *   - the 0.8.0 theme names become 0.8.1 names: "mocha" and "latte" the
 *     catppuccin presets, "macchiato" and "frappe" the theme files the
 *     editor ships (see lrg_editor_settings_find_theme_file());
 *   - `settings-version` is set to 2 and the file is saved, after a
 *     one-time backup to `editor.yaml.v0.8.0.bak`.
 *
 * Everything else (ui-scale, recents, unknown keys) is kept as it is.
 * The conversion helpers are public so tools and tests can use them on
 * their own.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-editor-settings.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_SETTINGS_BACKUP_SUFFIX:
 *
 * Appended to the settings file name for the copy
 * lrg_editor_settings_migrate() keeps of a 0.8.0 file
 * (`editor.yaml.v0.8.0.bak`).
 */
#define LRG_EDITOR_SETTINGS_BACKUP_SUFFIX ".v0.8.0.bak"

/**
 * lrg_editor_settings_needs_migration:
 * @settings: an #LrgEditorSettings
 *
 * Returns: %TRUE if @settings were read from an older file format
 *   (#LrgEditorSettings:settings-version below %LRG_EDITOR_SETTINGS_VERSION)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_settings_needs_migration (LrgEditorSettings *settings);

/**
 * lrg_editor_settings_migrate:
 * @settings: an #LrgEditorSettings, usually just loaded
 * @path: (nullable): the settings file to back up and rewrite, or %NULL
 *   for the file @settings were loaded from (lrg_editor_settings_get_path(),
 *   else the default path)
 * @error: (nullable): return location for an error
 *
 * Migrates @settings to %LRG_EDITOR_SETTINGS_VERSION as described in the
 * section introduction and saves them to @path. Does nothing (and
 * returns %TRUE) when @settings are already current, so calling it on
 * every start-up is cheap and running it twice changes nothing.
 *
 * Before rewriting, an existing file at @path is copied to
 * @path%LRG_EDITOR_SETTINGS_BACKUP_SUFFIX; an existing backup is never
 * overwritten. If the backup cannot be written nothing is changed and
 * %FALSE is returned. A layout string that does not parse leaves
 * `ui-layout` empty (the editor then uses its default layout) and is
 * logged; it is not an error.
 *
 * Returns: %TRUE on success or when there was nothing to do
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_settings_migrate (LrgEditorSettings  *settings,
                                      const gchar        *path,
                                      GError            **error);

/**
 * lrg_editor_settings_convert_legacy_layout:
 * @layout: a 0.8.0 layout string, e.g.
 *   "split(v,0.20,tabs(0,hierarchy),split(h,0.72,tabs(0,viewport),tabs(1,console,assets)))"
 * @error: (nullable): return location for a %G_IO_ERROR_INVALID_DATA error
 *
 * Converts the compact layout grammar of the 0.8.0 #LrgGuiDock to the
 * YAML lrg_dock_area_load_layout() reads. `split(v,R,A,B)` (children
 * side by side) becomes `split: horizontal`, `split(h,R,A,B)` (stacked)
 * becomes `split: vertical`, R becomes `ratio` (clamped to 0.05 - 0.95
 * like the old dock did), `tabs(N,id,...)` becomes `tabs: [id, ...]` with
 * the Nth id as `active`. Panel ids are copied unchanged (the editor's
 * panels kept their 0.8.0 ids). The grammar is parsed exactly as 0.8.0
 * did, including its nesting limit of 64.
 *
 * Returns: (transfer full) (nullable): the layout YAML, or %NULL if
 *   @layout does not parse
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_settings_convert_legacy_layout (const gchar  *layout,
                                                   GError      **error);

/**
 * lrg_editor_settings_convert_legacy_shortcut:
 * @shortcut: a stored keybinding in either syntax, alternatives separated
 *   by "|"
 *
 * Converts a stored keybinding to accelerator syntax with
 * lrg_editor_shortcut_to_accel(). Unlike that function it is lenient:
 * each alternative is converted on its own and invalid ones are left
 * out, so "Ctrl+S|Bogus+Key" gives "<Ctrl>s". "" (an unbinding) stays "".
 *
 * Returns: (transfer full) (nullable): the accelerators ("|"-separated),
 *   or %NULL if no alternative is valid
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_settings_convert_legacy_shortcut (const gchar *shortcut);

/**
 * lrg_editor_settings_convert_legacy_theme:
 * @theme: (nullable): a #LrgEditorSettings:theme value
 *
 * The 0.8.1 theme value for a 0.8.0 one: "mocha" gives
 * "catppuccin-mocha", "latte" "catppuccin-latte", "macchiato"
 * "catppuccin-macchiato" and "frappe" "catppuccin-frappe" (the last two
 * name the theme files shipped in data/editor/themes, found with
 * lrg_editor_settings_find_theme_file()). Matching ignores case. Any
 * other value (a preset name, a theme file path, an unknown name) is
 * returned unchanged.
 *
 * Returns: (transfer none) (nullable): the theme value; @theme itself
 *   when it is not a 0.8.0 name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_settings_convert_legacy_theme (const gchar *theme);

G_END_DECLS
