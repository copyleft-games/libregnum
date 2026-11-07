/* lrg-canvas-input.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One frame of input for an #LrgCanvas.
 *
 * The canvas never reads devices directly: lrg_canvas_handle_input()
 * fills an #LrgCanvasInput from graylib and hands it to
 * lrg_canvas_process_input(). Games with their own input stack (or
 * tests) can build the frame themselves and skip the polling.
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
 * LRG_CANVAS_INPUT_MAX_KEYS:
 *
 * Maximum number of key presses or typed characters one
 * #LrgCanvasInput frame carries. Extra input in the same frame is
 * dropped (raylib's own queue is the same size).
 */
#define LRG_CANVAS_INPUT_MAX_KEYS (16)

/**
 * LRG_CANVAS_BUTTON_MASK:
 * @button: a #GrlMouseButton
 *
 * Bit for @button in the #LrgCanvasInput button masks.
 */
#define LRG_CANVAS_BUTTON_MASK(button) (1u << (guint)(button))

/**
 * LRG_CANVAS_INPUT_MAX_TOUCHES:
 *
 * Maximum number of simultaneous touch points in one frame.
 */
#define LRG_CANVAS_INPUT_MAX_TOUCHES (10)

/**
 * LrgCanvasTouch:
 * @id: platform touch id, stable while the finger stays down
 * @x: position X in canvas coordinates
 * @y: position Y in canvas coordinates
 *
 * One finger on the screen.
 */
typedef struct
{
	gint   id;
	gfloat x;
	gfloat y;
} LrgCanvasTouch;

#define LRG_TYPE_CANVAS_INPUT (lrg_canvas_input_get_type ())

/**
 * LrgCanvasInput:
 * @pointer_x: pointer X in canvas coordinates
 * @pointer_y: pointer Y in canvas coordinates
 * @buttons_down: mask of buttons held this frame (see LRG_CANVAS_BUTTON_MASK())
 * @buttons_pressed: mask of buttons that went down this frame
 * @buttons_released: mask of buttons that went up this frame
 * @wheel_x: horizontal wheel movement this frame
 * @wheel_y: vertical wheel movement this frame
 * @keys: key presses this frame, oldest first
 * @n_keys: number of valid entries in @keys
 * @chars: composed characters typed this frame, oldest first
 * @n_chars: number of valid entries in @chars
 * @modifiers: modifier keys held this frame
 * @nav: focus-navigation request (gamepad D-pad/stick; %LRG_NAV_DIRECTION_NEXT
 *   and %LRG_NAV_DIRECTION_PREVIOUS behave like Tab and Shift+Tab)
 * @activate: activate the focused widget (gamepad A / Enter / Space)
 * @cancel: cancel / back (gamepad B / Escape)
 * @delta_time: seconds since the previous frame (drives springs)
 * @touches: fingers currently down, in canvas coordinates
 * @n_touches: number of valid entries in @touches
 * @keys_released: key releases this frame, oldest first (they become
 *   %LRG_UI_EVENT_KEY_UP events)
 * @n_keys_released: number of valid entries in @keys_released
 * @keys_repeat: bit i set when @keys[i] is an auto-repeat of a held key
 *   rather than a fresh press (it becomes lrg_ui_event_get_repeat())
 *
 * A plain value struct describing everything that happened to the
 * input devices during one frame. Zero it with lrg_canvas_input_init()
 * before filling it by hand.
 */
typedef struct
{
    gfloat           pointer_x;
    gfloat           pointer_y;
    guint            buttons_down;
    guint            buttons_pressed;
    guint            buttons_released;
    gfloat           wheel_x;
    gfloat           wheel_y;
    GrlKey           keys[LRG_CANVAS_INPUT_MAX_KEYS];
    guint            n_keys;
    gunichar         chars[LRG_CANVAS_INPUT_MAX_KEYS];
    guint            n_chars;
    LrgInputModifiers modifiers;
    LrgNavDirection  nav;
    gboolean         activate;
    gboolean         cancel;
    gfloat           delta_time;
    LrgCanvasTouch   touches[LRG_CANVAS_INPUT_MAX_TOUCHES];
    guint            n_touches;
    GrlKey           keys_released[LRG_CANVAS_INPUT_MAX_KEYS];
    guint            n_keys_released;
    guint            keys_repeat;
} LrgCanvasInput;

LRG_AVAILABLE_IN_ALL
GType lrg_canvas_input_get_type (void) G_GNUC_CONST;

/**
 * lrg_canvas_input_new:
 *
 * Allocates a zeroed input frame. Mostly useful for language
 * bindings; C code usually keeps an #LrgCanvasInput on the stack.
 *
 * Returns: (transfer full): a new #LrgCanvasInput
 */
LRG_AVAILABLE_IN_ALL
LrgCanvasInput * lrg_canvas_input_new (void);

/**
 * lrg_canvas_input_copy:
 * @self: an #LrgCanvasInput
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgCanvasInput * lrg_canvas_input_copy (const LrgCanvasInput *self);

/**
 * lrg_canvas_input_free:
 * @self: (nullable): an #LrgCanvasInput
 *
 * Frees a heap-allocated input frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_free (LrgCanvasInput *self);

/**
 * lrg_canvas_input_init:
 * @self: (out caller-allocates): the frame to reset
 *
 * Resets every field to "nothing happened": pointer at the origin,
 * no buttons, no keys, no navigation and a zero time step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_init (LrgCanvasInput *self);

/**
 * lrg_canvas_input_add_key:
 * @self: an #LrgCanvasInput
 * @key: the key that was pressed
 *
 * Appends a key press. Silently ignored once the frame is full.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_add_key (LrgCanvasInput *self,
                               GrlKey          key);

/**
 * lrg_canvas_input_add_key_repeat:
 * @self: an #LrgCanvasInput
 * @key: the key being held
 *
 * Appends an auto-repeated press of a held key (marked in
 * @keys_repeat). Silently ignored once the frame is full.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_add_key_repeat (LrgCanvasInput *self,
                                      GrlKey          key);

/**
 * lrg_canvas_input_add_key_release:
 * @self: an #LrgCanvasInput
 * @key: the key that was released
 *
 * Appends a key release. Silently ignored once the frame is full.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_add_key_release (LrgCanvasInput *self,
                                       GrlKey          key);

/**
 * lrg_canvas_input_add_char:
 * @self: an #LrgCanvasInput
 * @codepoint: the composed character that was typed
 *
 * Appends a typed character. Silently ignored once the frame is full.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_add_char (LrgCanvasInput *self,
                                gunichar        codepoint);

/**
 * lrg_canvas_input_add_text:
 * @self: an #LrgCanvasInput
 * @utf8: a UTF-8 string
 *
 * Appends every character of @utf8 as typed input, for tests and
 * on-screen keyboards.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_add_text (LrgCanvasInput *self,
                                const gchar    *utf8);

/**
 * lrg_canvas_input_poll:
 * @self: (out caller-allocates): the frame to fill
 * @delta_time: seconds since the previous frame
 *
 * Fills @self from the graylib input devices: mouse, wheel, the
 * key-press and character queues, modifiers, and the first gamepad
 * (D-pad and left stick become @nav, face buttons become @activate
 * and @cancel). Tab is queued as a key like any other: the canvas
 * offers it to a focused widget that accepts Tab and otherwise moves
 * focus (see lrg_widget_set_accepts_tab()). Keys held long enough to
 * auto-repeat (the operating system's repeat delay and rate) are
 * queued again every repeat with their @keys_repeat bit set; modifier
 * keys never repeat. The pointer is mapped through the
 * process-wide pointer transform installed with
 * lrg_canvas_set_default_pointer_transform().
 *
 * On desktop builds the first poll on a window chains GLFW mouse
 * button and key callbacks in front of raylib's (see
 * lrg_canvas_input_set_latch_enabled()), and every poll merges what
 * they saw since the previous poll (lrg_canvas_input_apply_latch()), so
 * a click whose press and release fall between two frames, and the
 * modifiers of a chord typed within one frame, are not lost.
 *
 * Requires an open window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_poll (LrgCanvasInput *self,
                            gfloat          delta_time);

/**
 * lrg_canvas_input_add_touch:
 * @self: an #LrgCanvasInput
 * @id: touch id (stable while the finger is down)
 * @x: position X in canvas coordinates
 * @y: position Y in canvas coordinates
 *
 * Adds a finger to the frame. Silently ignored once the frame is full.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_add_touch (LrgCanvasInput *self,
                                 gint            id,
                                 gfloat          x,
                                 gfloat          y);

/**
 * lrg_canvas_input_set_poll_touch:
 * @enabled: read touch points in lrg_canvas_input_poll()
 *
 * Touch polling is off by default because desktop raylib reports the
 * mouse as touch point 0 while a button is held, which would turn
 * every click into a gesture. Turn it on for touch screens (Android,
 * handhelds, kiosks).
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_set_poll_touch (gboolean enabled);

/**
 * lrg_canvas_input_get_poll_touch:
 *
 * Returns: %TRUE if lrg_canvas_input_poll() reads touch points
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_canvas_input_get_poll_touch (void);

/* ==========================================================================
 * Event latch
 *
 * graylib samples buttons and modifiers once per frame, so a press and a
 * release that both land between two polls (a touchpad tap, a synthetic
 * click, a fast click on a slow frame) are never seen, and a chord typed
 * faster than a frame (Ctrl down, S, Ctrl up) loses its Ctrl. The latch
 * remembers what the platform's event callbacks reported between two
 * polls and lrg_canvas_input_poll() merges it into the frame.
 * ========================================================================== */

/**
 * lrg_canvas_input_latch_button:
 * @button: a #GrlMouseButton (left, right or middle)
 * @pressed: %TRUE for a press event, %FALSE for a release event
 *
 * Records a mouse button event as the platform reported it, for the
 * next lrg_canvas_input_apply_latch(). lrg_canvas_input_poll() feeds the
 * latch itself from GLFW callbacks on desktop builds; other input
 * backends (SDL, a remote session, tests) call this from their event
 * handlers. Other buttons are ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_latch_button (GrlMouseButton button,
                                    gboolean       pressed);

/**
 * lrg_canvas_input_latch_key:
 * @key: the key
 * @pressed: %TRUE for a press event, %FALSE for a release event
 * @modifiers: the modifiers the platform reported with the event
 *
 * Records a key event for the next lrg_canvas_input_apply_latch(): the
 * press itself (added to the frame when the key queue missed it), the
 * modifiers held when it was pressed, and its release.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_latch_key (GrlKey            key,
                                 gboolean          pressed,
                                 LrgInputModifiers modifiers);

/**
 * lrg_canvas_input_apply_latch:
 * @self: a frame that was just filled from the devices (or by hand)
 *
 * Merges what the latch recorded since the last call into @self and
 * empties the latch:
 *
 * - a latched button press missing from @buttons_pressed becomes a
 *   press edge, plus a release edge when the button is no longer in
 *   @buttons_down (pressed and released within the frame);
 * - latched key presses missing from @keys are appended;
 * - when @self carries key presses, the modifiers latched with them are
 *   added to @modifiers (a chord typed faster than a frame);
 * - a key that was pressed and released within the frame (its last
 *   latched event is a release) gets a release in @keys_released.
 *
 * lrg_canvas_input_poll() calls this; call it yourself after building
 * a frame from another source that a latch was fed for.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_apply_latch (LrgCanvasInput *self);

/**
 * lrg_canvas_input_clear_latch:
 *
 * Forgets everything latched since the last poll (for example after a
 * modal pause or when focus left the window).
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_clear_latch (void);

/**
 * lrg_canvas_input_set_latch_enabled:
 * @enabled: whether lrg_canvas_input_poll() installs the GLFW event
 *   callbacks and applies the latch (default %TRUE)
 *
 * The callbacks are chained in front of raylib's own, which still run.
 * Turning the latch off restores raylib's callbacks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_set_latch_enabled (gboolean enabled);

/**
 * lrg_canvas_input_get_latch_enabled:
 *
 * Returns: %TRUE if lrg_canvas_input_poll() latches events
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_canvas_input_get_latch_enabled (void);

/**
 * lrg_canvas_input_detach_latch:
 *
 * Restores raylib's GLFW callbacks on the current window and forgets
 * it. Call before CloseWindow() when the program may open another
 * window later (the next lrg_canvas_input_poll() on a new window
 * installs the latch again). Harmless when nothing is installed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_input_detach_latch (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgCanvasInput, lrg_canvas_input_free)

G_END_DECLS
