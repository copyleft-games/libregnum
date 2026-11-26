/* lrg-touch-button.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One on-screen button of an LrgTouchControls layout.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgTouchAnchor:
 * @LRG_TOUCH_ANCHOR_TOP_LEFT: the screen's top-left corner
 * @LRG_TOUCH_ANCHOR_TOP: the middle of the top edge
 * @LRG_TOUCH_ANCHOR_TOP_RIGHT: the top-right corner
 * @LRG_TOUCH_ANCHOR_LEFT: the middle of the left edge
 * @LRG_TOUCH_ANCHOR_CENTER: the screen's centre
 * @LRG_TOUCH_ANCHOR_RIGHT: the middle of the right edge
 * @LRG_TOUCH_ANCHOR_BOTTOM_LEFT: the bottom-left corner
 * @LRG_TOUCH_ANCHOR_BOTTOM: the middle of the bottom edge
 * @LRG_TOUCH_ANCHOR_BOTTOM_RIGHT: the bottom-right corner
 *
 * The screen point a button's offset is measured from.  The button's
 * centre is the anchor plus its offset.
 *
 * Since: 0.13.0
 */
typedef enum
{
	LRG_TOUCH_ANCHOR_TOP_LEFT,
	LRG_TOUCH_ANCHOR_TOP,
	LRG_TOUCH_ANCHOR_TOP_RIGHT,
	LRG_TOUCH_ANCHOR_LEFT,
	LRG_TOUCH_ANCHOR_CENTER,
	LRG_TOUCH_ANCHOR_RIGHT,
	LRG_TOUCH_ANCHOR_BOTTOM_LEFT,
	LRG_TOUCH_ANCHOR_BOTTOM,
	LRG_TOUCH_ANCHOR_BOTTOM_RIGHT
} LrgTouchAnchor;

LRG_AVAILABLE_IN_ALL
GType lrg_touch_anchor_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TOUCH_ANCHOR (lrg_touch_anchor_get_type ())

/**
 * LrgTouchButtonMode:
 * @LRG_TOUCH_BUTTON_HOLD: down while the finger is down (move, sprint,
 *   charge): pressed when it lands, released when it lifts
 * @LRG_TOUCH_BUTTON_TAP: activates when the finger lifts inside the
 *   button (menus, abilities): pressed and released then; lifting outside
 *   cancels
 *
 * How a button turns a finger into input.
 *
 * Since: 0.13.0
 */
typedef enum
{
	LRG_TOUCH_BUTTON_HOLD,
	LRG_TOUCH_BUTTON_TAP
} LrgTouchButtonMode;

LRG_AVAILABLE_IN_ALL
GType lrg_touch_button_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TOUCH_BUTTON_MODE (lrg_touch_button_mode_get_type ())

/**
 * LrgTouchButtonShape:
 * @LRG_TOUCH_BUTTON_CIRCLE: an ellipse filling the size (a circle when
 *   width and height are equal); hits inside the ellipse
 * @LRG_TOUCH_BUTTON_ROUNDED: a rounded rectangle; hits inside the rectangle
 *
 * How a button is drawn and hit.
 *
 * Since: 0.13.0
 */
typedef enum
{
	LRG_TOUCH_BUTTON_CIRCLE,
	LRG_TOUCH_BUTTON_ROUNDED
} LrgTouchButtonShape;

LRG_AVAILABLE_IN_ALL
GType lrg_touch_button_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TOUCH_BUTTON_SHAPE (lrg_touch_button_shape_get_type ())

#define LRG_TYPE_TOUCH_BUTTON (lrg_touch_button_get_type ())

/**
 * LrgTouchButton:
 *
 * One on-screen button: where it is (#LrgTouchButton:anchor, offset and
 * size in dp), what it shows (#LrgTouchButton:label,
 * #LrgTouchButton:icon), how it reacts (#LrgTouchButton:mode) and the
 * #LrgInputAction it drives (#LrgTouchButton:action).  The input it
 * presses for that action is #LrgTouchButton:key or
 * #LrgTouchButton:gamepad-button when set, otherwise the first key, mouse
 * or gamepad button binding of the action in the controls'
 * #LrgTouchControls:input-map.  Add it to an #LrgTouchControls; its
 * properties may change at any time.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTouchButton, lrg_touch_button, LRG, TOUCH_BUTTON, GObject)

/**
 * lrg_touch_button_new:
 * @id: the button's id, unique within its controls
 *
 * Creates a 64x64 dp circular hold button anchored to the bottom-right
 * corner, its action named @id.
 *
 * Returns: (transfer full): a new #LrgTouchButton
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgTouchButton *lrg_touch_button_new (const gchar *id);

/**
 * lrg_touch_button_get_id:
 * @self: an #LrgTouchButton
 *
 * Returns: (transfer none): the id
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_touch_button_get_id (LrgTouchButton *self);

/**
 * lrg_touch_button_get_action:
 * @self: an #LrgTouchButton
 *
 * Returns: (transfer none) (nullable): the #LrgInputAction name it drives
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_touch_button_get_action (LrgTouchButton *self);

/**
 * lrg_touch_button_get_label:
 * @self: an #LrgTouchButton
 *
 * Returns: (transfer none) (nullable): the text drawn on it
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_touch_button_get_label (LrgTouchButton *self);

/**
 * lrg_touch_button_get_icon:
 * @self: an #LrgTouchButton
 *
 * The icon name, for a game drawing its own skin (the built-in drawing
 * shows the label, or the icon name when there is no label).
 *
 * Returns: (transfer none) (nullable): the icon name
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_touch_button_get_icon (LrgTouchButton *self);

/**
 * lrg_touch_button_get_mode:
 * @self: an #LrgTouchButton
 *
 * Returns: hold or tap
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgTouchButtonMode lrg_touch_button_get_mode (LrgTouchButton *self);

/**
 * lrg_touch_button_set_placement:
 * @self: an #LrgTouchButton
 * @anchor: the screen point to measure from
 * @offset_x: the centre's horizontal offset from @anchor, in dp
 * @offset_y: the centre's vertical offset from @anchor, in dp (down is
 *   positive)
 * @width: the width in dp
 * @height: the height in dp
 *
 * Places the button (the anchor, offset-x, offset-y, width and height
 * properties at once).
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_button_set_placement (LrgTouchButton *self,
                                     LrgTouchAnchor  anchor,
                                     gfloat          offset_x,
                                     gfloat          offset_y,
                                     gfloat          width,
                                     gfloat          height);

/**
 * lrg_touch_button_get_rect:
 * @self: an #LrgTouchButton
 * @screen_width: the screen's width in pixels
 * @screen_height: the screen's height in pixels
 * @px_per_dp: pixels per dp (dp scale times the controls' scale)
 * @x: (out): left edge in pixels
 * @y: (out): top edge in pixels
 * @width: (out): width in pixels
 * @height: (out): height in pixels
 *
 * Where the button is on a screen.  Offsets and sizes scale with
 * @px_per_dp; the anchor stays on its screen point.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_button_get_rect (LrgTouchButton *self,
                                gfloat          screen_width,
                                gfloat          screen_height,
                                gfloat          px_per_dp,
                                gfloat         *x,
                                gfloat         *y,
                                gfloat         *width,
                                gfloat         *height);

/**
 * lrg_touch_button_hit:
 * @self: an #LrgTouchButton
 * @screen_width: the screen's width in pixels
 * @screen_height: the screen's height in pixels
 * @px_per_dp: pixels per dp
 * @x: a point's horizontal position in pixels
 * @y: a point's vertical position in pixels
 *
 * Returns: %TRUE when the point is on the button (inside its ellipse for
 *   %LRG_TOUCH_BUTTON_CIRCLE)
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_button_hit (LrgTouchButton *self,
                               gfloat          screen_width,
                               gfloat          screen_height,
                               gfloat          px_per_dp,
                               gfloat          x,
                               gfloat          y);

G_END_DECLS
