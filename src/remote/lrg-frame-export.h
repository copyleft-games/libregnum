/* lrg-frame-export.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFrameExport: renders a libregnum view offscreen and ships each
 * frame as RGBA over a Unix stream socket to a host that draws it (a
 * GTK widget, a web page, a compositor surface) -- in a sealed memfd
 * passed with SCM_RIGHTS, or inline in the message.  It never blocks the
 * render loop: when the host falls behind, frames are dropped and
 * counted, never queued.  The protocol is in lrg-frame-protocol.h.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-frame-protocol.h"

G_BEGIN_DECLS

#define LRG_TYPE_FRAME_EXPORT (lrg_frame_export_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFrameExport, lrg_frame_export, LRG, FRAME_EXPORT, GObject)

/**
 * lrg_frame_export_new:
 * @fd: a connected Unix stream socket (one end of socketpair() or an
 *   accepted connection)
 * @width: initial frame width in pixels (> 0)
 * @height: initial frame height in pixels (> 0)
 * @take_fd: close @fd when the exporter is finalized
 *
 * Creates an exporter and sends %LRG_FRAME_MSG_HELLO.  Sends use
 * MSG_DONTWAIT, so the descriptor's own blocking mode is left alone.
 * Frames go in memfds where the system has memfd_create() (Linux), and
 * inline otherwise; see lrg_frame_export_set_use_memfd().
 *
 * Returns: (transfer full): a new #LrgFrameExport
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgFrameExport * lrg_frame_export_new (gint     fd,
                                       guint    width,
                                       guint    height,
                                       gboolean take_fd);

/**
 * lrg_frame_export_set_use_memfd:
 * @self: an #LrgFrameExport
 * @use_memfd: pass frames in sealed memfds (%TRUE) or inline (%FALSE)
 *
 * Chooses how pixels travel.  A memfd frame is a 64-byte message plus a
 * descriptor, so a slow host costs nothing but a dropped frame; an
 * inline frame is the whole image through the socket.  Ignored (inline)
 * where memfds do not exist.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_export_set_use_memfd (LrgFrameExport *self,
                                     gboolean        use_memfd);

/**
 * lrg_frame_export_get_use_memfd:
 * @self: an #LrgFrameExport
 *
 * Returns: %TRUE when frames go in memfds
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_export_get_use_memfd (LrgFrameExport *self);

/**
 * lrg_frame_export_set_size:
 * @self: an #LrgFrameExport
 * @width: frame width in pixels (> 0)
 * @height: frame height in pixels (> 0)
 *
 * Changes the size lrg_frame_export_begin() renders at, for example
 * after the host sent %LRG_FRAME_MSG_RESIZE (#LrgInputInject::resize).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_export_set_size (LrgFrameExport *self,
                                guint           width,
                                guint           height);

/**
 * lrg_frame_export_get_size:
 * @self: an #LrgFrameExport
 * @width: (out) (optional): frame width
 * @height: (out) (optional): frame height
 *
 * Gets the render size.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_export_get_size (LrgFrameExport *self,
                                guint          *width,
                                guint          *height);

/**
 * lrg_frame_export_begin:
 * @self: an #LrgFrameExport
 *
 * Starts drawing a frame into the exporter's render texture (created
 * or resized here).  Draw as usual, cameras included, then call
 * lrg_frame_export_end().  Needs a GL context (a window, which may be
 * hidden).
 *
 * Returns: %FALSE without a GL context (nothing to end)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_export_begin (LrgFrameExport *self);

/**
 * lrg_frame_export_end:
 * @self: an #LrgFrameExport
 * @error: (nullable): return location for an error
 *
 * Finishes the frame, reads it back, flips it to top-down rows and
 * sends it (lrg_frame_export_send_pixels()).
 *
 * Returns: %FALSE when the connection failed; a dropped frame is not a
 *   failure
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_export_end (LrgFrameExport  *self,
                               GError         **error);

/**
 * lrg_frame_export_get_texture:
 * @self: an #LrgFrameExport
 *
 * Returns: (transfer none) (nullable): the render texture frames are
 *   drawn into, to show the same picture locally too
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
GrlRenderTexture * lrg_frame_export_get_texture (LrgFrameExport *self);

/**
 * lrg_frame_export_send_pixels:
 * @self: an #LrgFrameExport
 * @rgba: (array) (element-type guint8): top-down RGBA8 rows
 * @width: width in pixels (> 0)
 * @height: height in pixels (> 0)
 * @stride: bytes from one row to the next (>= @width x 4)
 * @error: (nullable): return location for an error
 *
 * Sends one frame of pixels the caller already has (a software
 * renderer, a GrlImage, a test pattern); needs no GL.  Each frame gets
 * the next frame number, sent or dropped.
 *
 * Returns: %FALSE when the connection failed or the arguments are
 *   invalid; a dropped frame is not a failure (see
 *   lrg_frame_export_get_dropped_frames())
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_export_send_pixels (LrgFrameExport  *self,
                                       const guint8    *rgba,
                                       guint            width,
                                       guint            height,
                                       guint            stride,
                                       GError         **error);

/**
 * lrg_frame_export_send_image:
 * @self: an #LrgFrameExport
 * @image: an image (converted to RGBA8 if it is not)
 * @error: (nullable): return location for an error
 *
 * Sends a #GrlImage as one frame.
 *
 * Returns: as lrg_frame_export_send_pixels()
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_export_send_image (LrgFrameExport  *self,
                                      GrlImage        *image,
                                      GError         **error);

/**
 * lrg_frame_export_flush:
 * @self: an #LrgFrameExport
 * @error: (nullable): return location for an error
 *
 * Writes what is left of a message the socket only partly took.  Every
 * send does this first; call it on its own when idle.
 *
 * Returns: %FALSE when the connection failed
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_export_flush (LrgFrameExport  *self,
                                 GError         **error);

/**
 * lrg_frame_export_get_frame_number:
 * @self: an #LrgFrameExport
 *
 * Returns: the number the next frame will carry (frames count from 0)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_frame_export_get_frame_number (LrgFrameExport *self);

/**
 * lrg_frame_export_get_sent_frames:
 * @self: an #LrgFrameExport
 *
 * Returns: frames handed to the socket
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_frame_export_get_sent_frames (LrgFrameExport *self);

/**
 * lrg_frame_export_get_dropped_frames:
 * @self: an #LrgFrameExport
 *
 * Returns: frames dropped because the host had not taken the previous
 *   one yet
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_frame_export_get_dropped_frames (LrgFrameExport *self);

G_END_DECLS
