/* lrg-input-inject.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgInputInject: the application end of the frame protocol's input
 * direction.  It reads pointer, button, scroll, key and resize messages
 * a host sends (LrgFrameReceiver or any host speaking
 * lrg-frame-protocol.h) and feeds them into the application: into an
 * LrgInputSoftware source, so code reading LrgInputManager sees them,
 * and out as signals for code with its own input path (a GUI context,
 * a canvas).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../input/lrg-input-software.h"
#include "lrg-frame-protocol.h"

G_BEGIN_DECLS

#define LRG_TYPE_INPUT_INJECT (lrg_input_inject_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputInject, lrg_input_inject, LRG, INPUT_INJECT, GObject)

/**
 * lrg_input_inject_new:
 * @fd: the connected socket the host writes input to (usually the same
 *   descriptor as the #LrgFrameExport's)
 *
 * Creates an input reader.  It never closes @fd and never blocks on it.
 * Frames and hellos arriving on @fd (a host that echoes) are ignored.
 *
 * Returns: (transfer full): a new #LrgInputInject
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgInputInject * lrg_input_inject_new (gint fd);

/**
 * lrg_input_inject_set_target:
 * @self: an #LrgInputInject
 * @target: (nullable): the software input source to drive (add it to
 *   the #LrgInputManager), or %NULL for signals only
 *
 * Pointer moves and button and key presses and releases are applied to
 * @target; scrolls and resizes only go out as signals and accumulators,
 * because #LrgInput has no wheel or window size.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_inject_set_target (LrgInputInject   *self,
                                  LrgInputSoftware *target);

/**
 * lrg_input_inject_get_target:
 * @self: an #LrgInputInject
 *
 * Returns: (transfer none) (nullable): the software input source
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgInputSoftware * lrg_input_inject_get_target (LrgInputInject *self);

/**
 * lrg_input_inject_dispatch:
 * @self: an #LrgInputInject
 * @error: (nullable): return location for an error
 *
 * Reads everything the host has sent without blocking and applies it,
 * in order.  Call it once a frame, before the input is polled.
 *
 * Returns: %FALSE on a protocol or socket error, or when the host closed
 *   the connection (%LRG_FRAME_ERROR_CLOSED)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_inject_dispatch (LrgInputInject  *self,
                                    GError         **error);

/**
 * lrg_input_inject_get_pointer:
 * @self: an #LrgInputInject
 * @x: (out) (optional): last pointer x in frame pixels
 * @y: (out) (optional): last pointer y
 *
 * Gets where the host last put the pointer.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_inject_get_pointer (LrgInputInject *self,
                                   gfloat         *x,
                                   gfloat         *y);

/**
 * lrg_input_inject_is_button_down:
 * @self: an #LrgInputInject
 * @button: 0 left, 1 right, 2 middle
 *
 * Returns: %TRUE while the host holds @button down
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_inject_is_button_down (LrgInputInject *self,
                                          guint           button);

/**
 * lrg_input_inject_take_scroll:
 * @self: an #LrgInputInject
 * @dx: (out) (optional): horizontal steps since the last take
 * @dy: (out) (optional): vertical steps since the last take
 *
 * Reads and resets the scroll accumulated by dispatches.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_inject_take_scroll (LrgInputInject *self,
                                   gfloat         *dx,
                                   gfloat         *dy);

/**
 * lrg_input_inject_get_size:
 * @self: an #LrgInputInject
 * @width: (out) (optional): the last size the host asked for
 * @height: (out) (optional): its height
 *
 * Gets the host's requested frame size.
 *
 * Returns: %FALSE until a resize has arrived
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_inject_get_size (LrgInputInject *self,
                                    guint          *width,
                                    guint          *height);

/**
 * lrg_input_inject_get_modifiers:
 * @self: an #LrgInputInject
 *
 * Returns: the #LrgFrameModifier bits of the last key message
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_input_inject_get_modifiers (LrgInputInject *self);

G_END_DECLS
