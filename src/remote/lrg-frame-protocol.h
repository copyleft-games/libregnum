/* lrg-frame-protocol.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The frame protocol: how a libregnum application rendering offscreen
 * (LrgFrameExport) ships RGBA frames to a host over a Unix stream
 * socket, and how the host sends pointer, scroll, key and resize events
 * back (LrgInputInject reads them; LrgFrameReceiver is the host side).
 * The wire format is documented in docs/modules/remote/frame-protocol.org
 * and summarised here so a host that does not link libregnum can speak
 * it from this header alone.
 *
 * Every message is a 16-byte header followed by body_size bytes.  All
 * integers are little-endian, all floats IEEE-754 binary32
 * little-endian:
 *
 *   offset 0   u32  magic      LRG_FRAME_PROTOCOL_MAGIC ("LFRM")
 *   offset 4   u16  version    LRG_FRAME_PROTOCOL_VERSION
 *   offset 6   u16  type       LrgFrameMessageType
 *   offset 8   u32  body_size  bytes after the header
 *   offset 12  u32  reserved   0 (ignored by readers)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_FRAME_PROTOCOL_MAGIC:
 *
 * The first four bytes of every message: "LFRM" (0x4D52464C read as a
 * little-endian u32).
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_PROTOCOL_MAGIC ((guint32) 0x4D52464Cu)

/**
 * LRG_FRAME_PROTOCOL_VERSION:
 *
 * The protocol version this library speaks.  A reader refuses a message
 * with a different version.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_PROTOCOL_VERSION (1)

/**
 * LRG_FRAME_HEADER_SIZE:
 *
 * Size of the message header in bytes.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_HEADER_SIZE (16)

/**
 * LRG_FRAME_HELLO_BODY_SIZE:
 *
 * Body size of %LRG_FRAME_MSG_HELLO: u32 capabilities (bit 0: frames may
 * arrive in memfds), u32 width, u32 height, u32 reserved.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_HELLO_BODY_SIZE (16)

/**
 * LRG_FRAME_FRAME_BODY_SIZE:
 *
 * Size of the fixed part of an %LRG_FRAME_MSG_FRAME body: u32 width, u32
 * height, u32 stride (bytes per row), u32 format, u64 frame number, u64
 * timestamp (microseconds, monotonic clock), u32 flags, u32 pixel bytes
 * (stride x height), u32 reserved, u32 reserved.  The pixels follow it
 * unless %LRG_FRAME_FLAG_PIXELS_IN_FD is set.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_FRAME_BODY_SIZE (48)

/**
 * LRG_FRAME_FLAG_PIXELS_IN_FD:
 *
 * Frame flag: the pixels are not in the body but in a sealed memfd passed
 * with SCM_RIGHTS on the message's first byte, at offset 0.  The receiver
 * owns the descriptor and closes it.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_FLAG_PIXELS_IN_FD ((guint32) 1u << 0)

/**
 * LRG_FRAME_CAPABILITY_MEMFD:
 *
 * Hello capability: this sender may pass frames in memfds.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_CAPABILITY_MEMFD ((guint32) 1u << 0)

/**
 * LRG_FRAME_FORMAT_RGBA8:
 *
 * The only pixel format of version 1: 8-bit red, green, blue, alpha per
 * pixel (straight alpha), rows top to bottom.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_FORMAT_RGBA8 (1)

/**
 * LRG_FRAME_MAX_DIMENSION:
 *
 * The largest width or height a reader accepts.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_MAX_DIMENSION (16384)

/**
 * LRG_FRAME_MAX_BODY_SIZE:
 *
 * The largest body a reader accepts (256 MiB); a bigger one is a
 * protocol error.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_MAX_BODY_SIZE ((guint32) 256u * 1024u * 1024u)

/**
 * LrgFrameMessageType:
 * @LRG_FRAME_MSG_HELLO: application to host, first message: capabilities
 *   and size
 * @LRG_FRAME_MSG_FRAME: application to host: one frame
 * @LRG_FRAME_MSG_POINTER_MOVE: host to application: f32 x, f32 y in frame
 *   pixels (8 bytes)
 * @LRG_FRAME_MSG_POINTER_BUTTON: host to application: u32 button (0 left,
 *   1 right, 2 middle, as #GrlMouseButton), u32 pressed (1 or 0), f32 x,
 *   f32 y (16 bytes)
 * @LRG_FRAME_MSG_SCROLL: host to application: f32 dx, f32 dy in wheel
 *   steps, positive dy away from the user (8 bytes)
 * @LRG_FRAME_MSG_KEY: host to application: u32 key (a #GrlKey, raylib's
 *   key code), u32 pressed, u32 modifiers (#LrgFrameModifier), u32 Unicode
 *   code point of the text typed, 0 for none (16 bytes)
 * @LRG_FRAME_MSG_RESIZE: host to application: u32 width, u32 height the
 *   host wants frames at (8 bytes)
 *
 * Message types.  A reader skips types it does not know (their bodies
 * are consumed), and ignores body bytes past the sizes above, so later
 * versions can grow messages.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_FRAME_MSG_HELLO          = 1,
	LRG_FRAME_MSG_FRAME          = 2,
	LRG_FRAME_MSG_POINTER_MOVE   = 16,
	LRG_FRAME_MSG_POINTER_BUTTON = 17,
	LRG_FRAME_MSG_SCROLL         = 18,
	LRG_FRAME_MSG_KEY            = 19,
	LRG_FRAME_MSG_RESIZE         = 20
} LrgFrameMessageType;

LRG_AVAILABLE_IN_ALL
GType lrg_frame_message_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FRAME_MESSAGE_TYPE (lrg_frame_message_type_get_type ())

/**
 * LrgFrameModifier:
 * @LRG_FRAME_MODIFIER_NONE: no modifier
 * @LRG_FRAME_MODIFIER_SHIFT: Shift
 * @LRG_FRAME_MODIFIER_CONTROL: Control
 * @LRG_FRAME_MODIFIER_ALT: Alt
 * @LRG_FRAME_MODIFIER_SUPER: Super (the logo key)
 *
 * Modifier bits of a key message.
 *
 * Since: 0.11.0
 */
typedef enum /*< flags >*/
{
	LRG_FRAME_MODIFIER_NONE    = 0,
	LRG_FRAME_MODIFIER_SHIFT   = 1 << 0,
	LRG_FRAME_MODIFIER_CONTROL = 1 << 1,
	LRG_FRAME_MODIFIER_ALT     = 1 << 2,
	LRG_FRAME_MODIFIER_SUPER   = 1 << 3
} LrgFrameModifier;

LRG_AVAILABLE_IN_ALL
GType lrg_frame_modifier_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FRAME_MODIFIER (lrg_frame_modifier_get_type ())

/**
 * LRG_FRAME_ERROR:
 *
 * Error domain of the frame protocol.
 *
 * Since: 0.11.0
 */
#define LRG_FRAME_ERROR (lrg_frame_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_frame_error_quark (void);

/**
 * LrgFrameError:
 * @LRG_FRAME_ERROR_IO: the socket failed (the peer went away, a system
 *   call failed)
 * @LRG_FRAME_ERROR_PROTOCOL: the peer sent something that is not this
 *   protocol: a bad magic or version, an oversized body, a frame that
 *   does not add up; the connection cannot be resynchronised
 * @LRG_FRAME_ERROR_CLOSED: the peer closed the connection
 * @LRG_FRAME_ERROR_INVALID: an argument cannot be sent (a size of 0, a
 *   stride smaller than a row)
 *
 * Errors of the frame protocol.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_FRAME_ERROR_IO,
	LRG_FRAME_ERROR_PROTOCOL,
	LRG_FRAME_ERROR_CLOSED,
	LRG_FRAME_ERROR_INVALID
} LrgFrameError;

LRG_AVAILABLE_IN_ALL
GType lrg_frame_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FRAME_ERROR (lrg_frame_error_get_type ())

G_END_DECLS
