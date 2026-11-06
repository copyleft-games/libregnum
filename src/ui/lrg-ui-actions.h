/* lrg-ui-actions.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Keyboard accelerators for UI shortcuts.
 *
 * Accelerators are written GTK-style: zero or more modifiers in angle
 * brackets followed by a key name, e.g. "<Ctrl>s", "<Ctrl><Shift>z",
 * "F5", "<Alt>Return". They are bound to GIO actions with
 * lrg_canvas_add_shortcut(); the actions themselves live in action
 * groups inserted on widgets (lrg_widget_insert_action_group()). Every
 * canvas carries two built-in groups, "app" and "ui" (see
 * docs/modules/ui/actions.org).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * lrg_accel_parse:
 * @accel: an accelerator, e.g. "<Ctrl><Shift>s"
 * @key: (out) (optional): the key
 * @mods: (out) (optional): the modifiers
 *
 * Parses an accelerator. Modifiers (case-insensitive): "<Ctrl>",
 * "<Control>", "<Primary>" (Ctrl), "<Shift>", "<Alt>", "<Super>",
 * "<Meta>" (Super). Keys: letters ("s" and "S" are the same key: add
 * "<Shift>" for the shifted one), digits, "F1" to "F12", "Escape",
 * "Return" / "Enter", "Tab", "BackSpace", "Delete", "Insert", "Home",
 * "End", "Page_Up", "Page_Down", "Up", "Down", "Left", "Right",
 * "space", "plus" (the =/+ key), "minus", "equal", "comma", "period",
 * "slash", "backslash", "semicolon", "apostrophe", "grave",
 * "bracketleft", "bracketright", "KP_0" to "KP_9", "KP_Add",
 * "KP_Subtract", "KP_Multiply", "KP_Divide", "KP_Decimal", "KP_Enter",
 * "Pause", "Print", "Menu", or a single punctuation character such as
 * "+" or "/".
 *
 * Returns: %TRUE if @accel is valid; on %FALSE the outputs are untouched
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_accel_parse (const gchar       *accel,
                          GrlKey            *key,
                          LrgInputModifiers *mods);

/**
 * lrg_accel_name:
 * @key: a key
 * @mods: modifiers
 *
 * Builds the canonical accelerator for a key combination, the inverse
 * of lrg_accel_parse(): modifiers in the order Ctrl, Alt, Shift, Super,
 * then the key's name (letters in lower case).
 *
 * Returns: (transfer full) (nullable): e.g. "<Ctrl><Shift>s", or %NULL
 *   for a key without a name
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_accel_name (GrlKey            key,
                        LrgInputModifiers mods);

/**
 * lrg_accel_to_label:
 * @accel: an accelerator
 *
 * The text to show for an accelerator in menus and tooltips:
 * "<Ctrl><Shift>s" becomes "Ctrl+Shift+S", "<Alt>Page_Up" becomes
 * "Alt+PgUp".
 *
 * Returns: (transfer full) (nullable): the label, or %NULL if @accel
 *   does not parse
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_accel_to_label (const gchar *accel);

G_END_DECLS
