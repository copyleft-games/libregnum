/* lrg-gui-types.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared value types of the immediate-mode GUI toolkit (LrgGui*).
 *
 * The toolkit is backend-agnostic: widgets read an #LrgGuiInput snapshot
 * and append drawing commands to #LrgGuiDrawList objects; a backend
 * (lrg-gui-raylib.h for the engine's own window) fills the input and
 * renders the lists. That split is what lets every widget be exercised in
 * headless unit tests by injecting input and inspecting the commands.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgGuiId:
 *
 * A hashed widget identity (see lrg_gui_get_id()).
 */
typedef guint32 LrgGuiId;

/**
 * LrgGuiColor:
 *
 * A colour packed as 0xRRGGBBAA.
 */
typedef guint32 LrgGuiColor;

/**
 * LRG_GUI_RGBA:
 * @r: red 0-255
 * @g: green 0-255
 * @b: blue 0-255
 * @a: alpha 0-255
 *
 * Packs a colour.
 */
#define LRG_GUI_RGBA(r, g, b, a) \
	((LrgGuiColor) ((((guint32) (r) & 0xff) << 24) | (((guint32) (g) & 0xff) << 16) | \
	                (((guint32) (b) & 0xff) << 8) | ((guint32) (a) & 0xff)))

/**
 * LRG_GUI_HEX:
 * @rgb: a 0xRRGGBB value
 *
 * An opaque colour from a hex triplet.
 */
#define LRG_GUI_HEX(rgb) ((LrgGuiColor) ((((guint32) (rgb)) << 8) | 0xff))

/**
 * LrgGuiRect:
 * @x: left edge
 * @y: top edge
 * @w: width
 * @h: height
 *
 * An axis-aligned rectangle in pixels.
 */
typedef struct
{
	gfloat x;
	gfloat y;
	gfloat w;
	gfloat h;
} LrgGuiRect;

/**
 * LrgGuiKey:
 * @LRG_GUI_KEY_NONE: no key
 * @LRG_GUI_KEY_TAB: Tab
 * @LRG_GUI_KEY_LEFT: Left arrow
 * @LRG_GUI_KEY_RIGHT: Right arrow
 * @LRG_GUI_KEY_UP: Up arrow
 * @LRG_GUI_KEY_DOWN: Down arrow
 * @LRG_GUI_KEY_PAGE_UP: Page Up
 * @LRG_GUI_KEY_PAGE_DOWN: Page Down
 * @LRG_GUI_KEY_HOME: Home
 * @LRG_GUI_KEY_END: End
 * @LRG_GUI_KEY_INSERT: Insert
 * @LRG_GUI_KEY_DELETE: Delete
 * @LRG_GUI_KEY_BACKSPACE: Backspace
 * @LRG_GUI_KEY_SPACE: Space
 * @LRG_GUI_KEY_ENTER: Enter / Return (keypad Enter maps here too)
 * @LRG_GUI_KEY_ESCAPE: Escape
 * @LRG_GUI_KEY_A: A (and so on through Z)
 * @LRG_GUI_KEY_B: B
 * @LRG_GUI_KEY_C: C
 * @LRG_GUI_KEY_D: D
 * @LRG_GUI_KEY_E: E
 * @LRG_GUI_KEY_F: F
 * @LRG_GUI_KEY_G: G
 * @LRG_GUI_KEY_H: H
 * @LRG_GUI_KEY_I: I
 * @LRG_GUI_KEY_J: J
 * @LRG_GUI_KEY_K: K
 * @LRG_GUI_KEY_L: L
 * @LRG_GUI_KEY_M: M
 * @LRG_GUI_KEY_N: N
 * @LRG_GUI_KEY_O: O
 * @LRG_GUI_KEY_P: P
 * @LRG_GUI_KEY_Q: Q
 * @LRG_GUI_KEY_R: R
 * @LRG_GUI_KEY_S: S
 * @LRG_GUI_KEY_T: T
 * @LRG_GUI_KEY_U: U
 * @LRG_GUI_KEY_V: V
 * @LRG_GUI_KEY_W: W
 * @LRG_GUI_KEY_X: X
 * @LRG_GUI_KEY_Y: Y
 * @LRG_GUI_KEY_Z: Z
 * @LRG_GUI_KEY_0: 0 (and so on through 9)
 * @LRG_GUI_KEY_1: 1
 * @LRG_GUI_KEY_2: 2
 * @LRG_GUI_KEY_3: 3
 * @LRG_GUI_KEY_4: 4
 * @LRG_GUI_KEY_5: 5
 * @LRG_GUI_KEY_6: 6
 * @LRG_GUI_KEY_7: 7
 * @LRG_GUI_KEY_8: 8
 * @LRG_GUI_KEY_9: 9
 * @LRG_GUI_KEY_F1: F1 (and so on through F12)
 * @LRG_GUI_KEY_F2: F2
 * @LRG_GUI_KEY_F3: F3
 * @LRG_GUI_KEY_F4: F4
 * @LRG_GUI_KEY_F5: F5
 * @LRG_GUI_KEY_F6: F6
 * @LRG_GUI_KEY_F7: F7
 * @LRG_GUI_KEY_F8: F8
 * @LRG_GUI_KEY_F9: F9
 * @LRG_GUI_KEY_F10: F10
 * @LRG_GUI_KEY_F11: F11
 * @LRG_GUI_KEY_F12: F12
 * @LRG_GUI_KEY_MINUS: -
 * @LRG_GUI_KEY_EQUAL: =
 * @LRG_GUI_KEY_LEFT_BRACKET: [
 * @LRG_GUI_KEY_RIGHT_BRACKET: ]
 * @LRG_GUI_KEY_SLASH: /
 * @LRG_GUI_KEY_BACKSLASH: backslash
 * @LRG_GUI_KEY_COMMA: ,
 * @LRG_GUI_KEY_PERIOD: .
 * @LRG_GUI_KEY_SEMICOLON: ;
 * @LRG_GUI_KEY_APOSTROPHE: '
 * @LRG_GUI_KEY_GRAVE: `
 * @LRG_GUI_KEY_KP_0: keypad 0 (and so on through keypad 9)
 * @LRG_GUI_KEY_KP_1: keypad 1
 * @LRG_GUI_KEY_KP_2: keypad 2
 * @LRG_GUI_KEY_KP_3: keypad 3
 * @LRG_GUI_KEY_KP_4: keypad 4
 * @LRG_GUI_KEY_KP_5: keypad 5
 * @LRG_GUI_KEY_KP_6: keypad 6
 * @LRG_GUI_KEY_KP_7: keypad 7
 * @LRG_GUI_KEY_KP_8: keypad 8
 * @LRG_GUI_KEY_KP_9: keypad 9
 * @LRG_GUI_KEY_COUNT: number of keys
 *
 * Keys the toolkit understands, independent of the backend.
 */
typedef enum
{
	LRG_GUI_KEY_NONE,
	LRG_GUI_KEY_TAB,
	LRG_GUI_KEY_LEFT,
	LRG_GUI_KEY_RIGHT,
	LRG_GUI_KEY_UP,
	LRG_GUI_KEY_DOWN,
	LRG_GUI_KEY_PAGE_UP,
	LRG_GUI_KEY_PAGE_DOWN,
	LRG_GUI_KEY_HOME,
	LRG_GUI_KEY_END,
	LRG_GUI_KEY_INSERT,
	LRG_GUI_KEY_DELETE,
	LRG_GUI_KEY_BACKSPACE,
	LRG_GUI_KEY_SPACE,
	LRG_GUI_KEY_ENTER,
	LRG_GUI_KEY_ESCAPE,
	LRG_GUI_KEY_A, LRG_GUI_KEY_B, LRG_GUI_KEY_C, LRG_GUI_KEY_D, LRG_GUI_KEY_E,
	LRG_GUI_KEY_F, LRG_GUI_KEY_G, LRG_GUI_KEY_H, LRG_GUI_KEY_I, LRG_GUI_KEY_J,
	LRG_GUI_KEY_K, LRG_GUI_KEY_L, LRG_GUI_KEY_M, LRG_GUI_KEY_N, LRG_GUI_KEY_O,
	LRG_GUI_KEY_P, LRG_GUI_KEY_Q, LRG_GUI_KEY_R, LRG_GUI_KEY_S, LRG_GUI_KEY_T,
	LRG_GUI_KEY_U, LRG_GUI_KEY_V, LRG_GUI_KEY_W, LRG_GUI_KEY_X, LRG_GUI_KEY_Y,
	LRG_GUI_KEY_Z,
	LRG_GUI_KEY_0, LRG_GUI_KEY_1, LRG_GUI_KEY_2, LRG_GUI_KEY_3, LRG_GUI_KEY_4,
	LRG_GUI_KEY_5, LRG_GUI_KEY_6, LRG_GUI_KEY_7, LRG_GUI_KEY_8, LRG_GUI_KEY_9,
	LRG_GUI_KEY_F1, LRG_GUI_KEY_F2, LRG_GUI_KEY_F3, LRG_GUI_KEY_F4,
	LRG_GUI_KEY_F5, LRG_GUI_KEY_F6, LRG_GUI_KEY_F7, LRG_GUI_KEY_F8,
	LRG_GUI_KEY_F9, LRG_GUI_KEY_F10, LRG_GUI_KEY_F11, LRG_GUI_KEY_F12,
	LRG_GUI_KEY_MINUS,
	LRG_GUI_KEY_EQUAL,
	LRG_GUI_KEY_LEFT_BRACKET,
	LRG_GUI_KEY_RIGHT_BRACKET,
	LRG_GUI_KEY_SLASH,
	LRG_GUI_KEY_BACKSLASH,
	LRG_GUI_KEY_COMMA,
	LRG_GUI_KEY_PERIOD,
	LRG_GUI_KEY_SEMICOLON,
	LRG_GUI_KEY_APOSTROPHE,
	LRG_GUI_KEY_GRAVE,
	LRG_GUI_KEY_KP_0, LRG_GUI_KEY_KP_1, LRG_GUI_KEY_KP_2, LRG_GUI_KEY_KP_3,
	LRG_GUI_KEY_KP_4, LRG_GUI_KEY_KP_5, LRG_GUI_KEY_KP_6, LRG_GUI_KEY_KP_7,
	LRG_GUI_KEY_KP_8, LRG_GUI_KEY_KP_9,
	LRG_GUI_KEY_COUNT
} LrgGuiKey;

/**
 * LrgGuiModifiers:
 * @LRG_GUI_MOD_NONE: no modifier
 * @LRG_GUI_MOD_CTRL: Control
 * @LRG_GUI_MOD_SHIFT: Shift
 * @LRG_GUI_MOD_ALT: Alt
 * @LRG_GUI_MOD_SUPER: Super / Logo
 *
 * Keyboard modifier bits.
 */
typedef enum
{
	LRG_GUI_MOD_NONE  = 0,
	LRG_GUI_MOD_CTRL  = 1 << 0,
	LRG_GUI_MOD_SHIFT = 1 << 1,
	LRG_GUI_MOD_ALT   = 1 << 2,
	LRG_GUI_MOD_SUPER = 1 << 3
} LrgGuiModifiers;

/**
 * LrgGuiMouseButton:
 * @LRG_GUI_MOUSE_LEFT: primary button
 * @LRG_GUI_MOUSE_RIGHT: secondary button
 * @LRG_GUI_MOUSE_MIDDLE: middle button / wheel click
 *
 * Mouse buttons.
 */
typedef enum
{
	LRG_GUI_MOUSE_LEFT,
	LRG_GUI_MOUSE_RIGHT,
	LRG_GUI_MOUSE_MIDDLE
} LrgGuiMouseButton;

/**
 * LRG_GUI_MOUSE_BUTTON_COUNT:
 *
 * Number of tracked mouse buttons.
 */
#define LRG_GUI_MOUSE_BUTTON_COUNT (3)

/**
 * LrgGuiCursor:
 * @LRG_GUI_CURSOR_ARROW: default pointer
 * @LRG_GUI_CURSOR_TEXT: I-beam over text fields
 * @LRG_GUI_CURSOR_HAND: pointing hand over links
 * @LRG_GUI_CURSOR_RESIZE_EW: horizontal resize (vertical splitters, drag fields)
 * @LRG_GUI_CURSOR_RESIZE_NS: vertical resize (horizontal splitters)
 * @LRG_GUI_CURSOR_RESIZE_ALL: move
 * @LRG_GUI_CURSOR_NOT_ALLOWED: forbidden (invalid drop target)
 *
 * The pointer shape the toolkit requests for the current frame.
 */
typedef enum
{
	LRG_GUI_CURSOR_ARROW,
	LRG_GUI_CURSOR_TEXT,
	LRG_GUI_CURSOR_HAND,
	LRG_GUI_CURSOR_RESIZE_EW,
	LRG_GUI_CURSOR_RESIZE_NS,
	LRG_GUI_CURSOR_RESIZE_ALL,
	LRG_GUI_CURSOR_NOT_ALLOWED
} LrgGuiCursor;

G_END_DECLS
