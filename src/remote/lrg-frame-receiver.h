/* lrg-frame-receiver.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFrameReceiver: the host end of the frame protocol for a host that
 * links libregnum -- reads frames from an LrgFrameExport (inline or in
 * memfds) and sends pointer, scroll, key and resize events back.  A host
 * that does not link libregnum implements the same bytes from
 * lrg-frame-protocol.h.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-frame-protocol.h"

G_BEGIN_DECLS

#define LRG_TYPE_FRAME_RECEIVER (lrg_frame_receiver_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFrameReceiver, lrg_frame_receiver, LRG, FRAME_RECEIVER, GObject)

/**
 * lrg_frame_receiver_new:
 * @fd: a connected Unix stream socket to the application
 * @take_fd: close @fd when the receiver is finalized
 *
 * Creates a receiver.  Reads and writes use MSG_DONTWAIT; poll @fd for
 * input (or attach it to a main loop) and call
 * lrg_frame_receiver_dispatch() when it is readable.
 *
 * Returns: (transfer full): a new #LrgFrameReceiver
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgFrameReceiver * lrg_frame_receiver_new (gint     fd,
                                           gboolean take_fd);

/**
 * lrg_frame_receiver_dispatch:
 * @self: an #LrgFrameReceiver
 * @error: (nullable): return location for an error
 *
 * Reads everything available without blocking and handles every whole
 * message: a hello records the application's size and capabilities, a
 * frame replaces the current frame and emits
 * #LrgFrameReceiver::frame.  Only the newest frame is kept.
 *
 * Returns: %FALSE on a protocol error, a socket error, or when the
 *   application closed the connection (%LRG_FRAME_ERROR_CLOSED)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_receiver_dispatch (LrgFrameReceiver  *self,
                                      GError           **error);

/**
 * lrg_frame_receiver_get_hello:
 * @self: an #LrgFrameReceiver
 * @capabilities: (out) (optional): the application's capability bits
 * @width: (out) (optional): its initial width
 * @height: (out) (optional): its initial height
 *
 * Reads the hello.
 *
 * Returns: %FALSE until the hello has arrived
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_receiver_get_hello (LrgFrameReceiver *self,
                                       guint32          *capabilities,
                                       guint            *width,
                                       guint            *height);

/**
 * lrg_frame_receiver_has_frame:
 * @self: an #LrgFrameReceiver
 *
 * Returns: %TRUE once a frame has arrived
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_receiver_has_frame (LrgFrameReceiver *self);

/**
 * lrg_frame_receiver_get_frame:
 * @self: an #LrgFrameReceiver
 * @width: (out) (optional): width in pixels
 * @height: (out) (optional): height in pixels
 * @frame_number: (out) (optional): the sender's frame number
 * @in_memfd: (out) (optional): whether it arrived in a memfd
 *
 * Describes the newest frame.  Its pixels are tightly packed RGBA8 rows,
 * top down (a stride other than width x 4 is repacked on arrival).
 *
 * Returns: (transfer none) (nullable): the pixels, width x height x 4
 *   bytes, valid until the next dispatch; %NULL before the first frame
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const guint8 * lrg_frame_receiver_get_frame (LrgFrameReceiver *self,
                                             guint            *width,
                                             guint            *height,
                                             guint64          *frame_number,
                                             gboolean         *in_memfd);

/**
 * lrg_frame_receiver_get_frames_received:
 * @self: an #LrgFrameReceiver
 *
 * Returns: frames received so far
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_frame_receiver_get_frames_received (LrgFrameReceiver *self);

/**
 * lrg_frame_receiver_send_pointer_move:
 * @self: an #LrgFrameReceiver
 * @x: x in frame pixels
 * @y: y in frame pixels
 * @error: (nullable): return location for an error
 *
 * Sends %LRG_FRAME_MSG_POINTER_MOVE.  Input is never dropped: what the
 * socket cannot take now is queued and flushed by the next send or
 * dispatch.
 *
 * Returns: %FALSE when the connection failed
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_receiver_send_pointer_move (LrgFrameReceiver  *self,
                                               gfloat             x,
                                               gfloat             y,
                                               GError           **error);

/**
 * lrg_frame_receiver_send_button:
 * @self: an #LrgFrameReceiver
 * @button: 0 left, 1 right, 2 middle (a #GrlMouseButton)
 * @pressed: %TRUE for a press, %FALSE for a release
 * @x: x in frame pixels
 * @y: y in frame pixels
 * @error: (nullable): return location for an error
 *
 * Sends %LRG_FRAME_MSG_POINTER_BUTTON.
 *
 * Returns: %FALSE when the connection failed
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_receiver_send_button (LrgFrameReceiver  *self,
                                         guint              button,
                                         gboolean           pressed,
                                         gfloat             x,
                                         gfloat             y,
                                         GError           **error);

/**
 * lrg_frame_receiver_send_scroll:
 * @self: an #LrgFrameReceiver
 * @dx: horizontal wheel steps
 * @dy: vertical wheel steps, positive away from the user
 * @error: (nullable): return location for an error
 *
 * Sends %LRG_FRAME_MSG_SCROLL.
 *
 * Returns: %FALSE when the connection failed
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_receiver_send_scroll (LrgFrameReceiver  *self,
                                         gfloat             dx,
                                         gfloat             dy,
                                         GError           **error);

/**
 * lrg_frame_receiver_send_key:
 * @self: an #LrgFrameReceiver
 * @key: a #GrlKey (raylib key code), or 0 for text only
 * @pressed: %TRUE for a press, %FALSE for a release
 * @modifiers: #LrgFrameModifier bits held
 * @codepoint: the Unicode character typed, 0 for none
 * @error: (nullable): return location for an error
 *
 * Sends %LRG_FRAME_MSG_KEY.
 *
 * Returns: %FALSE when the connection failed
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_receiver_send_key (LrgFrameReceiver  *self,
                                      guint              key,
                                      gboolean           pressed,
                                      guint              modifiers,
                                      gunichar           codepoint,
                                      GError           **error);

/**
 * lrg_frame_receiver_send_resize:
 * @self: an #LrgFrameReceiver
 * @width: the width the host wants frames at
 * @height: the height
 * @error: (nullable): return location for an error
 *
 * Sends %LRG_FRAME_MSG_RESIZE.
 *
 * Returns: %FALSE when the connection failed
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_receiver_send_resize (LrgFrameReceiver  *self,
                                         guint              width,
                                         guint              height,
                                         GError           **error);

G_END_DECLS
