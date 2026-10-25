/* lrg-editor-actions.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Named editor commands with shortcuts.
 *
 * Every user-facing operation of the editor application ("file.save",
 * "edit.duplicate", "view.focus", "play.start", ...) is registered here
 * once with a label, a category and a default shortcut. Menus, the command
 * palette, keyboard handling and the IPC "action.run" method all go
 * through the registry, so a binding change or a new action shows up
 * everywhere. Users rebind shortcuts in #LrgEditorSettings (the
 * "keybindings" mapping of editor.yaml).
 *
 * Shortcuts are written like "Ctrl+Shift+S", "F2", "Delete", "Alt+Up";
 * see lrg_editor_shortcut_parse() for the accepted names. Several
 * alternatives are separated by "|" ("Ctrl+Shift+Z|Ctrl+Y"); menus show
 * the first.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../gui/lrg-gui-types.h"

G_BEGIN_DECLS

/**
 * LrgEditorActionFunc:
 * @user_data: (closure): data given when the action was added
 *
 * Runs an action.
 */
typedef void (*LrgEditorActionFunc) (gpointer user_data);

/**
 * LrgEditorActionStateFunc:
 * @user_data: (closure): data given when the action was added
 *
 * Reports whether an action is enabled (or checked).
 *
 * Returns: the state
 */
typedef gboolean (*LrgEditorActionStateFunc) (gpointer user_data);

/**
 * lrg_editor_shortcut_parse:
 * @text: e.g. "Ctrl+Shift+S", "F5", "Delete", "Alt+Up", "Ctrl+=",
 *   "Shift+Num7"
 * @mods: (out) (optional): the modifiers
 * @key: (out) (optional): the key
 *
 * Modifier names: Ctrl/Control, Shift, Alt/Meta, Super/Win/Logo.
 * Key names: letters, digits, F1-F12, Num0-Num9, Up/Down/Left/Right,
 * Home, End, PageUp, PageDown, Insert, Delete/Del, Backspace,
 * Enter/Return, Escape/Esc, Tab, Space and the punctuation characters
 * - = [ ] / \ , . ; ' `. Case is ignored.
 *
 * Returns: %TRUE if @text names exactly one key with optional modifiers
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shortcut_parse (const gchar     *text,
                                    LrgGuiModifiers *mods,
                                    LrgGuiKey       *key);

/**
 * lrg_editor_shortcut_format:
 * @mods: modifiers
 * @key: key
 *
 * Returns: (transfer full): the canonical text, e.g. "Ctrl+Shift+S"
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_shortcut_format (LrgGuiModifiers mods,
                                    LrgGuiKey       key);

/**
 * lrg_editor_fuzzy_score:
 * @pattern: what the user typed
 * @text: a candidate
 *
 * Case-insensitive subsequence match with bonuses for consecutive
 * characters and word starts (the command palette and quick open).
 *
 * Returns: a score > 0 for a match (higher is better), or 0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_fuzzy_score (const gchar *pattern,
                             const gchar *text);

#define LRG_TYPE_EDITOR_ACTIONS (lrg_editor_actions_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorActions, lrg_editor_actions, LRG, EDITOR_ACTIONS, GObject)

/**
 * lrg_editor_actions_new:
 * @settings: (nullable) (transfer none): settings providing keybinding
 *   overrides
 *
 * Returns: (transfer full): an empty registry
 */
LRG_AVAILABLE_IN_ALL
LrgEditorActions * lrg_editor_actions_new (LrgEditorSettings *settings);

/**
 * lrg_editor_actions_add:
 * @self: an #LrgEditorActions
 * @id: unique id ("category.name")
 * @label: menu / palette label
 * @category: grouping ("File", "Edit", ...)
 * @shortcut: (nullable): default shortcut text
 * @activate: (scope notified) (closure user_data): what the action does
 * @enabled: (scope notified) (closure user_data) (nullable): enabled-state
 *   query (%NULL = always enabled)
 * @checked: (scope notified) (closure user_data) (nullable): checked-state
 *   query for toggles (%NULL = not a toggle)
 * @user_data: data for the callbacks
 *
 * Registers (or replaces) an action.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_actions_add (LrgEditorActions         *self,
                             const gchar              *id,
                             const gchar              *label,
                             const gchar              *category,
                             const gchar              *shortcut,
                             LrgEditorActionFunc       activate,
                             LrgEditorActionStateFunc  enabled,
                             LrgEditorActionStateFunc  checked,
                             gpointer                  user_data);

/**
 * lrg_editor_actions_has:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * Returns: %TRUE if @id is registered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_actions_has (LrgEditorActions *self,
                                 const gchar      *id);

/**
 * lrg_editor_actions_get_label:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * Returns: (transfer none) (nullable): the label
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_actions_get_label (LrgEditorActions *self,
                                            const gchar      *id);

/**
 * lrg_editor_actions_get_category:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * Returns: (transfer none) (nullable): the category
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_actions_get_category (LrgEditorActions *self,
                                               const gchar      *id);

/**
 * lrg_editor_actions_get_shortcut:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * Returns: (transfer none) (nullable): the effective shortcut (user override
 *   or default), or %NULL if unbound
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_actions_get_shortcut (LrgEditorActions *self,
                                               const gchar      *id);

/**
 * lrg_editor_actions_get_default_shortcut:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * Returns: (transfer none) (nullable): the built-in shortcut
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_actions_get_default_shortcut (LrgEditorActions *self,
                                                       const gchar      *id);

/**
 * lrg_editor_actions_is_enabled:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * Returns: %TRUE if registered and enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_actions_is_enabled (LrgEditorActions *self,
                                        const gchar      *id);

/**
 * lrg_editor_actions_is_checked:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * Returns: %TRUE for a toggle that is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_actions_is_checked (LrgEditorActions *self,
                                        const gchar      *id);

/**
 * lrg_editor_actions_activate:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * Runs the action if it exists and is enabled, then emits
 * #LrgEditorActions::activated.
 *
 * Returns: %TRUE if it ran
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_actions_activate (LrgEditorActions *self,
                                      const gchar      *id);

/**
 * lrg_editor_actions_list:
 * @self: an #LrgEditorActions
 *
 * Returns: (transfer container) (element-type utf8): ids sorted by category
 *   then label
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_editor_actions_list (LrgEditorActions *self);

/**
 * lrg_editor_actions_search:
 * @self: an #LrgEditorActions
 * @query: what the user typed ("" lists everything)
 *
 * Fuzzy-matches "Category: Label" and the id.
 *
 * Returns: (transfer container) (element-type utf8): matching ids, best first
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_editor_actions_search (LrgEditorActions *self,
                                   const gchar      *query);

/**
 * lrg_editor_actions_find_by_shortcut:
 * @self: an #LrgEditorActions
 * @mods: modifiers
 * @key: key
 *
 * Returns: (transfer none) (nullable): the id of the enabled action bound
 *   to the chord, if any
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_actions_find_by_shortcut (LrgEditorActions *self,
                                                   LrgGuiModifiers   mods,
                                                   LrgGuiKey         key);

/**
 * lrg_editor_actions_find_conflicts:
 * @self: an #LrgEditorActions
 *
 * Returns: (transfer full) (element-type utf8): human-readable descriptions
 *   of shortcuts bound to more than one action (preferences dialog)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_actions_find_conflicts (LrgEditorActions *self);

G_END_DECLS
