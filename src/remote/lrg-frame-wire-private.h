/* lrg-frame-wire-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The frame protocol's byte layer, shared by LrgFrameExport,
 * LrgFrameReceiver and LrgInputInject: little-endian encoding, a
 * non-blocking writer that keeps the unsent tail of a message (and can
 * drop frames rather than queue them), and a reader that buffers bytes
 * and SCM_RIGHTS descriptors and hands out whole, validated messages.
 */

#pragma once

#include "lrg-frame-protocol.h"

G_BEGIN_DECLS

/* ---- encoding ---------------------------------------------------------- */

G_GNUC_INTERNAL void lrg_frame_put_u16 (guint8 *p, guint16 v);
G_GNUC_INTERNAL void lrg_frame_put_u32 (guint8 *p, guint32 v);
G_GNUC_INTERNAL void lrg_frame_put_u64 (guint8 *p, guint64 v);
G_GNUC_INTERNAL void lrg_frame_put_f32 (guint8 *p, gfloat v);
G_GNUC_INTERNAL guint16 lrg_frame_get_u16 (const guint8 *p);
G_GNUC_INTERNAL guint32 lrg_frame_get_u32 (const guint8 *p);
G_GNUC_INTERNAL guint64 lrg_frame_get_u64 (const guint8 *p);
G_GNUC_INTERNAL gfloat lrg_frame_get_f32 (const guint8 *p);

/* Writes a header for @type and @body_size into @out (16 bytes). */
G_GNUC_INTERNAL void lrg_frame_put_header (guint8              *out,
                                           LrgFrameMessageType  type,
                                           guint32              body_size);

/* ---- writer ------------------------------------------------------------ */

typedef enum
{
	LRG_FRAME_SEND_OK,       /* sent, or the rest queued for flush() */
	LRG_FRAME_SEND_DROPPED,  /* droppable and the socket was busy: not sent */
	LRG_FRAME_SEND_FAILED    /* the connection failed (error set) */
} LrgFrameSendResult;

typedef struct
{
	gint        fd;
	GByteArray *pending;     /* the unsent tail of earlier messages */
	gboolean    failed;
} LrgFrameWriter;

G_GNUC_INTERNAL void lrg_frame_writer_init (LrgFrameWriter *writer,
                                            gint            fd);
G_GNUC_INTERNAL void lrg_frame_writer_clear (LrgFrameWriter *writer);
G_GNUC_INTERNAL gboolean lrg_frame_writer_flush (LrgFrameWriter  *writer,
                                                 GError         **error);
/*
 * Sends one message (header and body in one buffer, or header+fixed
 * body in @head and the pixels in @tail).  @pass_fd >= 0 is sent with
 * the first byte and is not closed here.  A @droppable message is
 * dropped whole when earlier bytes are still queued or the socket takes
 * none of it; otherwise the unsent rest is queued.
 */
G_GNUC_INTERNAL LrgFrameSendResult lrg_frame_writer_send (LrgFrameWriter  *writer,
                                                          const guint8    *head,
                                                          gsize            head_len,
                                                          const guint8    *tail,
                                                          gsize            tail_len,
                                                          gint             pass_fd,
                                                          gboolean         droppable,
                                                          GError         **error);

/* ---- reader ------------------------------------------------------------ */

typedef struct
{
	gint        fd;
	GByteArray *buf;
	gsize       start;       /* first unconsumed byte of buf */
	GQueue     *fds;         /* received descriptors, oldest first */
	gboolean    eof;
	gboolean    broken;      /* a protocol error: nothing more is read */
} LrgFrameReader;

typedef struct
{
	LrgFrameMessageType type;
	const guint8       *body;     /* valid until the next reader call */
	guint32             body_size;
	gint                fd;       /* -1, or a descriptor the caller now owns */
} LrgFrameMessage;

G_GNUC_INTERNAL void lrg_frame_reader_init (LrgFrameReader *reader,
                                            gint            fd);
G_GNUC_INTERNAL void lrg_frame_reader_clear (LrgFrameReader *reader);
/* Reads whatever the socket has now without blocking.  FALSE on error. */
G_GNUC_INTERNAL gboolean lrg_frame_reader_fill (LrgFrameReader  *reader,
                                                GError         **error);
/*
 * Takes the next whole message.  Returns FALSE with no error when none
 * is complete yet, FALSE with an error on a protocol violation (the
 * reader is then broken).
 */
G_GNUC_INTERNAL gboolean lrg_frame_reader_next (LrgFrameReader   *reader,
                                                LrgFrameMessage  *message,
                                                GError          **error);

G_END_DECLS
