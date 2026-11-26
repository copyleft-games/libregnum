/* lrg-touch-controls.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * On-screen touch controls: a floating virtual stick, a look region and
 * buttons, feeding the input system.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-touch-tracker.h"
#include "lrg-touch-button.h"

G_BEGIN_DECLS

/**
 * SECTION:lrg-touch-controls
 * @title: LrgTouchControls
 * @short_description: Virtual stick, look region and buttons for touch screens
 *
 * The usual phone layout for a 3D game, driving the same input a gamepad
 * does:
 *
 * - a *stick*: a finger landing in the stick region
 *   (#LrgTouchControls:stick-region, a fraction of the screen) spawns a
 *   virtual stick where it lands; moving it gives a 2D axis with a
 *   deadzone and a run threshold;
 * - a *look region* (#LrgTouchControls:look-region): one finger dragging
 *   turns the camera, two fingers pinching zoom;
 * - *buttons* (#LrgTouchButton), each driving an #LrgInputAction, held or
 *   tapped.
 *
 * Each finger belongs to at most one of them: the first that claims it
 * when it lands (buttons, topmost first, then the stick, then the look
 * region), until it lifts; a finger none claims is the game's pointer.
 * Look fingers and pointer fingers go through two #LrgTouchTracker instances
 * (so a tap is not lost while the other hand turns the camera): the look
 * fingers' drags and pinches are the camera, every other gesture (their
 * taps and long presses, and everything the pointer fingers do) is the
 * game's: #LrgTouchControls::pointer-gesture, and, with
 * #LrgTouchControls:pointer-emulation, a left click (tap) or right click
 * (long press) at the finger through the sink.
 *
 * *Into the input system.*  The controls write into an #LrgInputSoftware
 * (#LrgTouchControls:sink) added to the #LrgInputManager, so #LrgInputMap
 * actions see them like any other device: the stick sets the gamepad's
 * left stick axes (or the right ones, #LrgTouchControls:stick-side) of
 * gamepad #LrgTouchControls:gamepad, the look region the other stick
 * (pixels moved this frame times #LrgTouchControls:look-axis-gain,
 * clamped to -1..1), and a button presses #LrgTouchButton:key,
 * #LrgTouchButton:gamepad-button, or else the first key, mouse button or
 * gamepad button bound to its action in #LrgTouchControls:input-map.
 * lrg_touch_controls_attach() sets all that up.  Exact values are also
 * available directly: lrg_touch_controls_get_stick(),
 * lrg_touch_controls_get_look_delta(), lrg_touch_controls_get_zoom_delta(),
 * lrg_touch_controls_is_button_down().
 *
 * *Frames.*  Call lrg_touch_controls_update() once per frame (it reads
 * raylib's touch points and the screen size) before
 * lrg_input_manager_poll(), or lrg_touch_controls_feed() with points of
 * your own; lrg_touch_controls_draw() draws the overlay.
 *
 * *Showing.*  #LrgTouchControls:mode %LRG_TOUCH_CONTROLS_AUTO (the
 * default) shows the controls from the first touch and hides them after
 * #LrgTouchControls:auto-hide-delay seconds during which only the mouse,
 * keyboard or a gamepad was used; %LRG_TOUCH_CONTROLS_SHOWN and
 * %LRG_TOUCH_CONTROLS_HIDDEN force it.  Hidden controls claim no fingers
 * and leave the sink at rest.
 *
 * *Layouts* are YAML (lrg_touch_controls_load_from_file()); see
 * docs/modules/input/touch.org.  Sizes are in dp, multiplied by
 * #LrgTouchControls:dp-scale and the player's #LrgTouchControls:scale.
 *
 * Since: 0.13.0
 */

/**
 * LrgTouchControlsMode:
 * @LRG_TOUCH_CONTROLS_AUTO: shown after a touch, hidden after a while of
 *   other input only
 * @LRG_TOUCH_CONTROLS_SHOWN: always shown
 * @LRG_TOUCH_CONTROLS_HIDDEN: never shown
 *
 * When #LrgTouchControls are shown and active.
 *
 * Since: 0.13.0
 */
typedef enum
{
	LRG_TOUCH_CONTROLS_AUTO,
	LRG_TOUCH_CONTROLS_SHOWN,
	LRG_TOUCH_CONTROLS_HIDDEN
} LrgTouchControlsMode;

LRG_AVAILABLE_IN_ALL
GType lrg_touch_controls_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TOUCH_CONTROLS_MODE (lrg_touch_controls_mode_get_type ())

/**
 * LrgTouchStickSide:
 * @LRG_TOUCH_STICK_LEFT: the stick is the gamepad's left stick (the look
 *   region the right one)
 * @LRG_TOUCH_STICK_RIGHT: the stick is the right stick (the look region
 *   the left one)
 *
 * Which gamepad stick the virtual stick drives in the sink.
 *
 * Since: 0.13.0
 */
typedef enum
{
	LRG_TOUCH_STICK_LEFT,
	LRG_TOUCH_STICK_RIGHT
} LrgTouchStickSide;

LRG_AVAILABLE_IN_ALL
GType lrg_touch_stick_side_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TOUCH_STICK_SIDE (lrg_touch_stick_side_get_type ())

/**
 * LRG_TOUCH_CONTROLS_ERROR:
 *
 * Error domain of layout loading.
 *
 * Since: 0.13.0
 */
#define LRG_TOUCH_CONTROLS_ERROR (lrg_touch_controls_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_touch_controls_error_quark (void);

/**
 * LrgTouchControlsError:
 * @LRG_TOUCH_CONTROLS_ERROR_PARSE: not valid YAML
 * @LRG_TOUCH_CONTROLS_ERROR_INVALID: valid YAML, not a valid layout
 *
 * Layout loading errors.
 *
 * Since: 0.13.0
 */
typedef enum
{
	LRG_TOUCH_CONTROLS_ERROR_PARSE,
	LRG_TOUCH_CONTROLS_ERROR_INVALID
} LrgTouchControlsError;

#define LRG_TYPE_TOUCH_CONTROLS (lrg_touch_controls_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTouchControls, lrg_touch_controls, LRG, TOUCH_CONTROLS, GObject)

/**
 * lrg_touch_controls_new:
 *
 * Creates controls with a stick in the lower-left part of the screen, a
 * look region on the right half, no buttons, in auto mode, without a
 * sink.
 *
 * Returns: (transfer full): new #LrgTouchControls
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgTouchControls *lrg_touch_controls_new (void);

/**
 * lrg_touch_controls_attach:
 * @self: an #LrgTouchControls
 * @manager: the #LrgInputManager the game polls
 *
 * Connects the controls to @manager: a new #LrgInputSoftware sink added
 * to it (#LrgTouchControls:sink; any previous sink is removed from
 * @manager), and its "mouse" source as #LrgTouchControls:mouse-source.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_controls_attach (LrgTouchControls *self,
                                LrgInputManager  *manager);

/* ==========================================================================
 * Layout
 * ========================================================================== */

/**
 * lrg_touch_controls_load_from_data:
 * @self: an #LrgTouchControls
 * @data: YAML text
 * @length: length of @data, or -1 when nul-terminated
 * @error: return location for a #GError
 *
 * Replaces the layout (stick, look region, buttons; scale and opacity
 * when given) with the one in @data.  Everything is checked before
 * anything changes: on error the controls are unchanged.  Fingers down
 * are released.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_controls_load_from_data (LrgTouchControls  *self,
                                            const gchar       *data,
                                            gssize             length,
                                            GError           **error);

/**
 * lrg_touch_controls_load_from_file:
 * @self: an #LrgTouchControls
 * @path: (type filename): a YAML layout file
 * @error: return location for a #GError
 *
 * lrg_touch_controls_load_from_data() with a file's contents.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_controls_load_from_file (LrgTouchControls  *self,
                                            const gchar       *path,
                                            GError           **error);

/**
 * lrg_touch_controls_add_button:
 * @self: an #LrgTouchControls
 * @button: (transfer none): a button
 *
 * Adds @button on top of the others, replacing a button with the same id.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_controls_add_button (LrgTouchControls *self,
                                    LrgTouchButton   *button);

/**
 * lrg_touch_controls_remove_button:
 * @self: an #LrgTouchControls
 * @id: a button id
 *
 * Removes a button, releasing it first when it is held.
 *
 * Returns: %TRUE when there was such a button
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_controls_remove_button (LrgTouchControls *self,
                                           const gchar      *id);

/**
 * lrg_touch_controls_get_button:
 * @self: an #LrgTouchControls
 * @id: a button id
 *
 * Returns: (transfer none) (nullable): the button, or %NULL
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgTouchButton *lrg_touch_controls_get_button (LrgTouchControls *self,
                                               const gchar      *id);

/**
 * lrg_touch_controls_get_n_buttons:
 * @self: an #LrgTouchControls
 *
 * Returns: the number of buttons
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_touch_controls_get_n_buttons (LrgTouchControls *self);

/**
 * lrg_touch_controls_get_nth_button:
 * @self: an #LrgTouchControls
 * @index: a position, bottom first
 *
 * Returns: (transfer none): the button at @index
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgTouchButton *lrg_touch_controls_get_nth_button (LrgTouchControls *self,
                                                   guint             index);

/**
 * lrg_touch_controls_get_tracker:
 * @self: an #LrgTouchControls
 *
 * The tracker that recognises the look region's gestures; its
 * thresholds (and dp scale) are also the pointer fingers' tracker's, so
 * set them here.
 *
 * Returns: (transfer none): the tracker
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgTouchTracker *lrg_touch_controls_get_tracker (LrgTouchControls *self);

/* ==========================================================================
 * Frames
 * ========================================================================== */

/**
 * lrg_touch_controls_set_screen_size:
 * @self: an #LrgTouchControls
 * @width: the screen's width in pixels
 * @height: the screen's height in pixels
 *
 * The screen the layout is placed on.  lrg_touch_controls_update() sets
 * it from the window.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_controls_set_screen_size (LrgTouchControls *self,
                                         gfloat            width,
                                         gfloat            height);

/**
 * lrg_touch_controls_feed:
 * @self: an #LrgTouchControls
 * @points: (array length=n_points) (nullable): the fingers down this frame
 * @n_points: the number of @points
 * @time: the frame's time in seconds
 *
 * Processes one frame of touches: ownership, stick, look, buttons,
 * pointer gestures, and the sink.  Clears the previous frame's pressed
 * and released states and deltas first.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_controls_feed (LrgTouchControls    *self,
                              const LrgTouchPoint *points,
                              guint                n_points,
                              gdouble              time);

/**
 * lrg_touch_controls_note_other_input:
 * @self: an #LrgTouchControls
 * @time: when, in seconds
 *
 * Tells auto mode that the mouse, keyboard or a gamepad was used at
 * @time.  lrg_touch_controls_update() does this itself.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_controls_note_other_input (LrgTouchControls *self,
                                          gdouble           time);

/**
 * lrg_touch_controls_update:
 * @self: an #LrgTouchControls
 * @time: the frame's time in seconds
 *
 * One frame from the real devices: the screen size, raylib's touch
 * points (lrg_touch_controls_feed()) and, for auto mode, whether a key,
 * mouse button, mouse movement without touches, or gamepad was used.
 * Needs an open window.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_controls_update (LrgTouchControls *self,
                                gdouble           time);

/**
 * lrg_touch_controls_is_visible:
 * @self: an #LrgTouchControls
 *
 * Returns: %TRUE when the controls are shown and claim fingers
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_controls_is_visible (LrgTouchControls *self);

/* ==========================================================================
 * Output
 * ========================================================================== */

/**
 * lrg_touch_controls_get_stick:
 * @self: an #LrgTouchControls
 * @x: (out) (optional): -1 (left) to 1 (right)
 * @y: (out) (optional): -1 (up) to 1 (down), as a gamepad stick
 *
 * The stick's axes after the deadzone: 0 inside it, then rising from 0
 * at its edge to 1 at the stick's radius; the vector's length is at
 * most 1.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_controls_get_stick (LrgTouchControls *self,
                                   gfloat           *x,
                                   gfloat           *y);

/**
 * lrg_touch_controls_is_stick_active:
 * @self: an #LrgTouchControls
 *
 * Returns: %TRUE while a finger holds the stick
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_controls_is_stick_active (LrgTouchControls *self);

/**
 * lrg_touch_controls_is_running:
 * @self: an #LrgTouchControls
 *
 * Returns: %TRUE when the stick is pushed at least
 *   #LrgTouchControls:stick-run-threshold of the way
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_controls_is_running (LrgTouchControls *self);

/**
 * lrg_touch_controls_get_look_delta:
 * @self: an #LrgTouchControls
 * @dx: (out) (optional): horizontal camera movement this frame
 * @dy: (out) (optional): vertical camera movement this frame
 *
 * Pixels the look finger moved this frame times
 * #LrgTouchControls:look-sensitivity.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_controls_get_look_delta (LrgTouchControls *self,
                                        gfloat           *dx,
                                        gfloat           *dy);

/**
 * lrg_touch_controls_get_zoom_delta:
 * @self: an #LrgTouchControls
 *
 * The look region's pinch this frame as a factor: above 1 when the
 * fingers spread (zoom in), below 1 when they pinch, 1 without a pinch.
 *
 * Returns: the zoom factor
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_touch_controls_get_zoom_delta (LrgTouchControls *self);

/**
 * lrg_touch_controls_is_button_down:
 * @self: an #LrgTouchControls
 * @id: a button id
 *
 * Returns: %TRUE while a finger is on the button (a tap button too, until
 *   it lifts)
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_controls_is_button_down (LrgTouchControls *self,
                                            const gchar      *id);

/**
 * lrg_touch_controls_is_button_pressed:
 * @self: an #LrgTouchControls
 * @id: a button id
 *
 * Returns: %TRUE when the button's action was pressed this frame (a hold
 *   button when the finger landed, a tap button when it lifted inside)
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_controls_is_button_pressed (LrgTouchControls *self,
                                               const gchar      *id);

/**
 * lrg_touch_controls_is_button_released:
 * @self: an #LrgTouchControls
 * @id: a button id
 *
 * Returns: %TRUE when the button's action was released this frame
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_controls_is_button_released (LrgTouchControls *self,
                                                const gchar      *id);

/**
 * lrg_touch_controls_draw:
 * @self: an #LrgTouchControls
 *
 * Draws the overlay (stick, buttons and labels) with graylib's 2D
 * primitives, in screen space, between BeginDrawing() and EndDrawing(),
 * after the game's own drawing.  Draws nothing while hidden.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_controls_draw (LrgTouchControls *self);

G_END_DECLS
