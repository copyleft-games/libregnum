/* lrg-editor-shortcut-legacy.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Deprecated #LrgGuiKey based shortcut helpers.
 *
 * Until 0.8.0 the editor's action registry spoke the immediate-mode GUI's
 * key type (#LrgGuiKey / #LrgGuiModifiers) and wrote shortcuts as
 * "Ctrl+Shift+S", "Num7" or "Ctrl+,". Since 0.8.1 the registry is a
 * #GActionGroup and its shortcuts are GTK-style accelerators ("<Ctrl><Shift>s",
 * "KP_7", "<Ctrl>comma"); see lrg_editor_shortcut_to_accel() and
 * lrg_editor_actions_get_accels(). The functions here keep 0.8.0 callers
 * compiling and working for one release. lrg-editor-actions.h includes this
 * header, so nothing changes for code that includes <libregnum.h>.
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
 * lrg_editor_shortcut_parse:
 * @text: e.g. "Ctrl+Shift+S", "F5", "Delete", "Alt+Up", "Ctrl+=",
 *   "Shift+Num7"
 * @mods: (out) (optional): the modifiers
 * @key: (out) (optional): the key
 *
 * Parses one shortcut in the 0.8.0 syntax. Modifier names: Ctrl/Control,
 * Shift, Alt/Meta, Super/Win/Logo. Key names: letters, digits, F1-F12,
 * Num0-Num9, Up/Down/Left/Right, Home, End, PageUp, PageDown, Insert,
 * Delete/Del, Backspace, Enter/Return, Escape/Esc, Tab, Space and the
 * punctuation characters - = [ ] / \ , . ; ' `. Case is ignored.
 *
 * Returns: %TRUE if @text names exactly one key with optional modifiers
 *
 * Deprecated: 0.8.1: Use lrg_editor_shortcut_to_accel() and lrg_accel_parse()
 *   instead.
 */
LRG_DEPRECATED_IN_0_8_1_FOR (lrg_editor_shortcut_to_accel)
gboolean lrg_editor_shortcut_parse (const gchar     *text,
                                    LrgGuiModifiers *mods,
                                    LrgGuiKey       *key);

/**
 * lrg_editor_shortcut_format:
 * @mods: modifiers
 * @key: key
 *
 * Formats a chord in the 0.8.0 syntax.
 *
 * Returns: (transfer full): the canonical text, e.g. "Ctrl+Shift+S"
 *
 * Deprecated: 0.8.1: Use lrg_accel_name() instead.
 */
LRG_DEPRECATED_IN_0_8_1_FOR (lrg_accel_name)
gchar * lrg_editor_shortcut_format (LrgGuiModifiers mods,
                                    LrgGuiKey       key);

/**
 * lrg_editor_actions_find_by_shortcut:
 * @self: an #LrgEditorActions
 * @mods: modifiers
 * @key: key
 *
 * Looks up the enabled action bound to a chord. Bindings in either syntax
 * match.
 *
 * Returns: (transfer none) (nullable): the id of the enabled action bound
 *   to the chord, if any
 *
 * Deprecated: 0.8.1: Use lrg_editor_actions_find_by_accel() instead.
 */
LRG_DEPRECATED_IN_0_8_1_FOR (lrg_editor_actions_find_by_accel)
const gchar * lrg_editor_actions_find_by_shortcut (LrgEditorActions *self,
                                                   LrgGuiModifiers   mods,
                                                   LrgGuiKey         key);

/**
 * lrg_editor_actions_get_shortcut:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * The effective binding (user override or default) written in the 0.8.0
 * syntax, alternatives separated by "|". A binding stored in the 0.8.0
 * syntax is returned exactly as written; one stored as accelerators is
 * converted ("<Ctrl><Shift>z|<Ctrl>y" gives "Ctrl+Shift+Z|Ctrl+Y").
 *
 * Returns: (transfer none) (nullable): the effective shortcut, or %NULL if
 *   unbound or unknown. The string is valid until the bindings change.
 *
 * Deprecated: 0.8.1: Use lrg_editor_actions_get_accels() instead.
 */
LRG_DEPRECATED_IN_0_8_1_FOR (lrg_editor_actions_get_accels)
const gchar * lrg_editor_actions_get_shortcut (LrgEditorActions *self,
                                               const gchar      *id);

/**
 * lrg_editor_actions_get_default_shortcut:
 * @self: an #LrgEditorActions
 * @id: an action id
 *
 * The built-in binding in the 0.8.0 syntax (see
 * lrg_editor_actions_get_shortcut()).
 *
 * Returns: (transfer none) (nullable): the built-in shortcut
 *
 * Deprecated: 0.8.1: Use lrg_editor_actions_get_default_accels() instead.
 */
LRG_DEPRECATED_IN_0_8_1_FOR (lrg_editor_actions_get_default_accels)
const gchar * lrg_editor_actions_get_default_shortcut (LrgEditorActions *self,
                                                       const gchar      *id);

G_END_DECLS
