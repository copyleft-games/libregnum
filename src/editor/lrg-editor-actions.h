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
 * #LrgEditorActions implements #GActionGroup: each registered action is
 * a GIO action named exactly by its id ("file.save"), so inserting the
 * registry on a widget as group "editor" lets menus refer to
 * "editor.file.save". An action is enabled when its enabled callback says
 * so; an action with a checked callback carries a boolean state (a
 * toggle), others are stateless; none takes a parameter. Callbacks are
 * polled, not pushed: call lrg_editor_actions_refresh_state() once per
 * frame and it emits #GActionGroup::action-enabled-changed and
 * #GActionGroup::action-state-changed for the actions that changed.
 *
 * Shortcuts are GTK-style accelerators ("<Ctrl><Shift>s", "F2", "Delete",
 * "<Alt>Up", "KP_7", "<Ctrl>comma"; see lrg_accel_parse()). Several
 * alternatives are separated by "|" in a single string
 * ("<Ctrl><Shift>z|<Ctrl>y"); menus show the first. The 0.8.0 syntax
 * ("Ctrl+Shift+Z|Ctrl+Y", "Num7", "Ctrl+,") is still accepted everywhere a
 * shortcut string comes in (lrg_editor_actions_add(), settings overrides,
 * lrg_editor_actions_set_accels()) and converted with
 * lrg_editor_shortcut_to_accel(). The #LrgGuiKey based helpers of 0.8.0
 * live, deprecated, in lrg-editor-shortcut-legacy.h.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

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
 * lrg_editor_shortcut_to_accel:
 * @shortcut: a shortcut in either syntax, alternatives separated by "|":
 *   "Ctrl+Shift+2", "Num7", "Ctrl+,", "Shift+F5|F8", "<Ctrl>s", "KP_7"
 *
 * Converts a shortcut written in the 0.8.0 syntax ("Ctrl+Shift+Z") to
 * accelerator syntax ("<Ctrl><Shift>z"). Each alternative is converted on
 * its own and the results are joined with "|". Alternatives already in
 * accelerator syntax (anything with a "<Modifier>" prefix, or a bare key
 * name lrg_accel_parse() accepts such as "KP_7" or "comma") are passed
 * through unchanged after validation. Converted modifiers come out in the
 * order Ctrl, Alt, Shift, Super and letters in lower case, like
 * lrg_accel_name(); the 0.8.0 key names map as "Num7" to "KP_7", "PageUp"
 * to "Page_Up", "Backspace" to "BackSpace", "Enter" to "Return", "Esc" to
 * "Escape", "Del" to "Delete", "=" to "equal", "Plus" (and "Ctrl++") to
 * "plus", "," to "comma" and so on.
 *
 * Returns: (transfer full) (nullable): the accelerator string, or %NULL
 *   if @shortcut is %NULL, empty, or any alternative is invalid
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_shortcut_to_accel (const gchar *shortcut);

/**
 * lrg_editor_accel_to_label:
 * @accel: an accelerator string, alternatives separated by "|" (the 0.8.0
 *   syntax is accepted too)
 *
 * The text to show for a binding in menus, tooltips and the shortcuts
 * dialog: each alternative formatted with lrg_accel_to_label() and joined
 * with ", " ("<Ctrl><Shift>z|<Ctrl>y" gives "Ctrl+Shift+Z, Ctrl+Y").
 * Invalid alternatives are skipped.
 *
 * Returns: (transfer full) (nullable): the label, or %NULL if nothing in
 *   @accel is valid
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_accel_to_label (const gchar *accel);

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
 * @shortcut: (nullable): default shortcut, in accelerator syntax or the
 *   0.8.0 syntax (alternatives separated by "|"); invalid alternatives are
 *   dropped with a debug message
 * @activate: (scope notified) (closure user_data): what the action does
 * @enabled: (scope notified) (closure user_data) (nullable): enabled-state
 *   query (%NULL = always enabled)
 * @checked: (scope notified) (closure user_data) (nullable): checked-state
 *   query for toggles (%NULL = not a toggle)
 * @user_data: data for the callbacks
 *
 * Registers (or replaces) an action. The id becomes the #GActionGroup
 * action name, so it must satisfy g_action_name_is_valid() (letters,
 * digits, "-" and "."). Emits #GActionGroup::action-added (preceded by
 * #GActionGroup::action-removed when an existing id is replaced). The
 * callbacks are not called here.
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
 * lrg_editor_actions_get_accels:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * The effective accelerators of an action: the user override (from
 * lrg_editor_actions_set_accels() or the settings' keybindings) when
 * there is one, else the default given to lrg_editor_actions_add(). Each
 * element is one alternative in accelerator syntax.
 *
 * Returns: (transfer full) (nullable) (array zero-terminated=1): the
 *   accelerators (an empty array when unbound), or %NULL for an unknown id
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_actions_get_accels (LrgEditorActions *self,
                                     const gchar      *id);

/**
 * lrg_editor_actions_get_default_accels:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * Returns: (transfer full) (nullable) (array zero-terminated=1): the
 *   built-in accelerators (an empty array when the action has no default),
 *   or %NULL for an unknown id
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_actions_get_default_accels (LrgEditorActions *self,
                                             const gchar      *id);

/**
 * lrg_editor_actions_set_accels:
 * @self: an #LrgEditorActions
 * @id: an action id
 * @accels: (nullable) (array zero-terminated=1): the new accelerators
 *   (either syntax; an element may itself hold "|" alternatives), an empty
 *   array to unbind, or %NULL to restore the default
 *
 * Overrides the shortcuts of an action. With settings (see
 * lrg_editor_actions_new()) the override is written to them as a
 * "|"-separated accelerator string (so it is saved with editor.yaml) and
 * may name an action that is not registered yet; without settings it is
 * kept by the registry and @id must be registered. Emits
 * #LrgEditorActions::accels-changed when the effective accelerators
 * change.
 *
 * Returns: %TRUE if the override was stored, %FALSE if @accels holds an
 *   invalid accelerator (nothing changes then) or @id is unknown without
 *   settings
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_actions_set_accels (LrgEditorActions    *self,
                                        const gchar         *id,
                                        const gchar * const *accels);

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
 * #LrgEditorActions::activated and refreshes the enabled and checked
 * states (see lrg_editor_actions_refresh_state()).
 * g_action_group_activate_action() does the same.
 *
 * Returns: %TRUE if it ran
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_actions_activate (LrgEditorActions *self,
                                      const gchar      *id);

/**
 * lrg_editor_actions_refresh_state:
 * @self: an #LrgEditorActions
 *
 * Polls every action's enabled and checked callbacks and emits
 * #GActionGroup::action-enabled-changed and
 * #GActionGroup::action-state-changed for those whose value differs from
 * the last one seen. The first value seen for an action (by a refresh or a
 * #GActionGroup query) is recorded without a signal. Meant to be called
 * once per frame by the editor window; activation through the registry
 * refreshes on its own.
 *
 * Returns: the number of signals emitted
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_actions_refresh_state (LrgEditorActions *self);

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
 * lrg_editor_actions_find_by_accel:
 * @self: an #LrgEditorActions
 * @key: a key
 * @mods: the held modifiers
 *
 * Finds the enabled action one of whose effective accelerators is exactly
 * @key with @mods. When several match, the id that sorts first wins, so
 * the result is deterministic.
 *
 * Returns: (transfer none) (nullable): the action id, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_actions_find_by_accel (LrgEditorActions  *self,
                                                GrlKey             key,
                                                LrgInputModifiers  mods);

/**
 * lrg_editor_actions_find_conflicts:
 * @self: an #LrgEditorActions
 *
 * Returns: (transfer full) (element-type utf8): human-readable descriptions
 *   of shortcuts bound to more than one action (preferences dialog), e.g.
 *   "Ctrl+Alt+S: file.save and view.grid"
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_actions_find_conflicts (LrgEditorActions *self);

G_END_DECLS

/* Deprecated LrgGuiKey helpers, kept for one release (0.8.1). */
#include "lrg-editor-shortcut-legacy.h"
