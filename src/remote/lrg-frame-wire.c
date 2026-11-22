/* lrg-frame-wire.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The frame protocol's byte layer: encoding, the non-blocking writer
 * with its pending tail, and the buffering reader that validates
 * headers and pairs memfd frames with their descriptors.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_NET

#include "lrg-frame-wire-private.h"
#include "../lrg-log.h"

#include <errno.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

#define READ_CHUNK (65536)
#define MAX_FDS_PER_READ (16)

G_DEFINE_QUARK (lrg-frame-error-quark, lrg_frame_error)

GType
lrg_frame_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FRAME_ERROR_IO, "LRG_FRAME_ERROR_IO", "io" },
			{ LRG_FRAME_ERROR_PROTOCOL, "LRG_FRAME_ERROR_PROTOCOL", "protocol" },
			{ LRG_FRAME_ERROR_CLOSED, "LRG_FRAME_ERROR_CLOSED", "closed" },
			{ LRG_FRAME_ERROR_INVALID, "LRG_FRAME_ERROR_INVALID", "invalid" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgFrameError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_frame_message_type_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FRAME_MSG_HELLO, "LRG_FRAME_MSG_HELLO", "hello" },
			{ LRG_FRAME_MSG_FRAME, "LRG_FRAME_MSG_FRAME", "frame" },
			{ LRG_FRAME_MSG_POINTER_MOVE, "LRG_FRAME_MSG_POINTER_MOVE", "pointer-move" },
			{ LRG_FRAME_MSG_POINTER_BUTTON, "LRG_FRAME_MSG_POINTER_BUTTON", "pointer-button" },
			{ LRG_FRAME_MSG_SCROLL, "LRG_FRAME_MSG_SCROLL", "scroll" },
			{ LRG_FRAME_MSG_KEY, "LRG_FRAME_MSG_KEY", "key" },
			{ LRG_FRAME_MSG_RESIZE, "LRG_FRAME_MSG_RESIZE", "resize" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgFrameMessageType"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_frame_modifier_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_FRAME_MODIFIER_NONE, "LRG_FRAME_MODIFIER_NONE", "none" },
			{ LRG_FRAME_MODIFIER_SHIFT, "LRG_FRAME_MODIFIER_SHIFT", "shift" },
			{ LRG_FRAME_MODIFIER_CONTROL, "LRG_FRAME_MODIFIER_CONTROL", "control" },
			{ LRG_FRAME_MODIFIER_ALT, "LRG_FRAME_MODIFIER_ALT", "alt" },
			{ LRG_FRAME_MODIFIER_SUPER, "LRG_FRAME_MODIFIER_SUPER", "super" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgFrameModifier"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Encoding (explicitly little-endian, whatever the host)
 * ========================================================================== */

void
lrg_frame_put_u16 (guint8  *p,
                   guint16  v)
{
	p[0] = (guint8) (v & 0xFF);
	p[1] = (guint8) (v >> 8);
}

void
lrg_frame_put_u32 (guint8  *p,
                   guint32  v)
{
	p[0] = (guint8) (v & 0xFF);
	p[1] = (guint8) ((v >> 8) & 0xFF);
	p[2] = (guint8) ((v >> 16) & 0xFF);
	p[3] = (guint8) (v >> 24);
}

void
lrg_frame_put_u64 (guint8  *p,
                   guint64  v)
{
	lrg_frame_put_u32 (p, (guint32) (v & G_GUINT64_CONSTANT (0xFFFFFFFF)));
	lrg_frame_put_u32 (p + 4, (guint32) (v >> 32));
}

void
lrg_frame_put_f32 (guint8 *p,
                   gfloat  v)
{
	guint32 bits;

	memcpy (&bits, &v, sizeof (bits));
	lrg_frame_put_u32 (p, bits);
}

guint16
lrg_frame_get_u16 (const guint8 *p)
{
	return (guint16) (p[0] | (p[1] << 8));
}

guint32
lrg_frame_get_u32 (const guint8 *p)
{
	return (guint32) p[0] | ((guint32) p[1] << 8) | ((guint32) p[2] << 16) |
	       ((guint32) p[3] << 24);
}

guint64
lrg_frame_get_u64 (const guint8 *p)
{
	return (guint64) lrg_frame_get_u32 (p) | ((guint64) lrg_frame_get_u32 (p + 4) << 32);
}

gfloat
lrg_frame_get_f32 (const guint8 *p)
{
	guint32 bits = lrg_frame_get_u32 (p);
	gfloat v;

	memcpy (&v, &bits, sizeof (v));
	return v;
}

void
lrg_frame_put_header (guint8              *out,
                      LrgFrameMessageType  type,
                      guint32              body_size)
{
	lrg_frame_put_u32 (out, LRG_FRAME_PROTOCOL_MAGIC);
	lrg_frame_put_u16 (out + 4, LRG_FRAME_PROTOCOL_VERSION);
	lrg_frame_put_u16 (out + 6, (guint16) type);
	lrg_frame_put_u32 (out + 8, body_size);
	lrg_frame_put_u32 (out + 12, 0);
}

/* ==========================================================================
 * Writer
 * ========================================================================== */

void
lrg_frame_writer_init (LrgFrameWriter *writer,
                       gint            fd)
{
	writer->fd = fd;
	writer->pending = g_byte_array_new ();
	writer->failed = FALSE;
}

void
lrg_frame_writer_clear (LrgFrameWriter *writer)
{
	g_clear_pointer (&writer->pending, g_byte_array_unref);
}

static void
set_errno_error (GError      **error,
                 gint          err,
                 const gchar  *what)
{
	if (err == EPIPE || err == ECONNRESET)
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_CLOSED,
		             "%s: the peer closed the connection", what);
	else
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_IO, "%s: %s", what,
		             g_strerror (err));
}

gboolean
lrg_frame_writer_flush (LrgFrameWriter  *writer,
                        GError         **error)
{
#ifdef G_OS_UNIX
	if (writer->failed)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_CLOSED, "the connection has failed");
		return FALSE;
	}
	while (writer->pending->len > 0)
	{
		gssize n = send (writer->fd, writer->pending->data, writer->pending->len,
		                 MSG_DONTWAIT | MSG_NOSIGNAL);

		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return TRUE;
			writer->failed = TRUE;
			set_errno_error (error, errno, "send");
			return FALSE;
		}
		g_byte_array_remove_range (writer->pending, 0, (guint) n);
	}
	return TRUE;
#else
	g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_IO,
	             "the frame protocol needs Unix domain sockets");
	return FALSE;
#endif
}

LrgFrameSendResult
lrg_frame_writer_send (LrgFrameWriter  *writer,
                       const guint8    *head,
                       gsize            head_len,
                       const guint8    *tail,
                       gsize            tail_len,
                       gint             pass_fd,
                       gboolean         droppable,
                       GError         **error)
{
#ifdef G_OS_UNIX
	struct msghdr msg;
	struct iovec iov[2];
	union
	{
		struct cmsghdr align;
		gchar          buf[CMSG_SPACE (sizeof (gint))];
	} control;
	gsize total = head_len + tail_len;
	gssize n;

	if (!lrg_frame_writer_flush (writer, error))
		return LRG_FRAME_SEND_FAILED;

	/* earlier bytes are still queued: a frame is not worth waiting for */
	if (writer->pending->len > 0)
	{
		if (droppable || pass_fd >= 0)
			return LRG_FRAME_SEND_DROPPED;
		g_byte_array_append (writer->pending, head, (guint) head_len);
		if (tail_len > 0)
			g_byte_array_append (writer->pending, tail, (guint) tail_len);
		return LRG_FRAME_SEND_OK;
	}

	memset (&msg, 0, sizeof (msg));
	iov[0].iov_base = (gpointer) head;
	iov[0].iov_len = head_len;
	iov[1].iov_base = (gpointer) tail;
	iov[1].iov_len = tail_len;
	msg.msg_iov = iov;
	msg.msg_iovlen = (tail_len > 0) ? 2 : 1;
	if (pass_fd >= 0)
	{
		struct cmsghdr *cmsg;

		memset (&control, 0, sizeof (control));
		msg.msg_control = control.buf;
		msg.msg_controllen = sizeof (control.buf);
		cmsg = CMSG_FIRSTHDR (&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN (sizeof (gint));
		memcpy (CMSG_DATA (cmsg), &pass_fd, sizeof (gint));
	}

	do
		n = sendmsg (writer->fd, &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
	while (n < 0 && errno == EINTR);

	if (n < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			if (droppable)
				return LRG_FRAME_SEND_DROPPED;
			g_byte_array_append (writer->pending, head, (guint) head_len);
			if (tail_len > 0)
				g_byte_array_append (writer->pending, tail, (guint) tail_len);
			return LRG_FRAME_SEND_OK;
		}
		writer->failed = TRUE;
		set_errno_error (error, errno, "sendmsg");
		return LRG_FRAME_SEND_FAILED;
	}

	/* a partial write: the descriptor went with the first byte; queue the rest */
	if ((gsize) n < total)
	{
		if ((gsize) n < head_len)
		{
			g_byte_array_append (writer->pending, head + n, (guint) (head_len - (gsize) n));
			if (tail_len > 0)
				g_byte_array_append (writer->pending, tail, (guint) tail_len);
		}
		else
			g_byte_array_append (writer->pending, tail + ((gsize) n - head_len),
			                     (guint) (total - (gsize) n));
	}
	return LRG_FRAME_SEND_OK;
#else
	g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_IO,
	             "the frame protocol needs Unix domain sockets");
	return LRG_FRAME_SEND_FAILED;
#endif
}

/* ==========================================================================
 * Reader
 * ========================================================================== */

static void
close_fd (gpointer data)
{
#ifdef G_OS_UNIX
	close (GPOINTER_TO_INT (data));
#endif
}

void
lrg_frame_reader_init (LrgFrameReader *reader,
                       gint            fd)
{
	reader->fd = fd;
	reader->buf = g_byte_array_new ();
	reader->start = 0;
	reader->fds = g_queue_new ();
	reader->eof = FALSE;
	reader->broken = FALSE;
}

void
lrg_frame_reader_clear (LrgFrameReader *reader)
{
	g_clear_pointer (&reader->buf, g_byte_array_unref);
	if (reader->fds != NULL)
	{
		/* descriptors are stored as fd + 1 so 0 is not NULL */
		while (!g_queue_is_empty (reader->fds))
			close_fd (GINT_TO_POINTER (GPOINTER_TO_INT (g_queue_pop_head (reader->fds)) - 1));
		g_queue_free (reader->fds);
		reader->fds = NULL;
	}
}

gboolean
lrg_frame_reader_fill (LrgFrameReader  *reader,
                       GError         **error)
{
#ifdef G_OS_UNIX
	if (reader->broken)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
		             "the connection had a protocol error");
		return FALSE;
	}
	/* drop consumed bytes before growing */
	if (reader->start > 0)
	{
		g_byte_array_remove_range (reader->buf, 0, (guint) reader->start);
		reader->start = 0;
	}

	while (!reader->eof)
	{
		struct msghdr msg;
		struct iovec iov;
		union
		{
			struct cmsghdr align;
			gchar          buf[CMSG_SPACE (sizeof (gint) * MAX_FDS_PER_READ)];
		} control;
		struct cmsghdr *cmsg;
		guint old_len = reader->buf->len;
		gssize n;

		g_byte_array_set_size (reader->buf, old_len + READ_CHUNK);
		memset (&msg, 0, sizeof (msg));
		iov.iov_base = reader->buf->data + old_len;
		iov.iov_len = READ_CHUNK;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = control.buf;
		msg.msg_controllen = sizeof (control.buf);

		do
			n = recvmsg (reader->fd, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
		while (n < 0 && errno == EINTR);

		if (n < 0)
		{
			g_byte_array_set_size (reader->buf, old_len);
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return TRUE;
			set_errno_error (error, errno, "recvmsg");
			return FALSE;
		}
		g_byte_array_set_size (reader->buf, old_len + (guint) n);
		if (n == 0)
			reader->eof = TRUE;

		for (cmsg = CMSG_FIRSTHDR (&msg); cmsg != NULL; cmsg = CMSG_NXTHDR (&msg, cmsg))
		{
			if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
			{
				gsize count = (cmsg->cmsg_len - CMSG_LEN (0)) / sizeof (gint);
				gsize i;

				for (i = 0; i < count; i++)
				{
					gint fd;

					memcpy (&fd, CMSG_DATA (cmsg) + i * sizeof (gint), sizeof (gint));
					g_queue_push_tail (reader->fds, GINT_TO_POINTER (fd + 1));
				}
			}
		}
		if (msg.msg_flags & MSG_CTRUNC)
		{
			/* descriptors were lost: frames can no longer be paired with them */
			reader->broken = TRUE;
			g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
			             "too many descriptors in one read");
			return FALSE;
		}
	}
	return TRUE;
#else
	g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_IO,
	             "the frame protocol needs Unix domain sockets");
	return FALSE;
#endif
}

gboolean
lrg_frame_reader_next (LrgFrameReader   *reader,
                       LrgFrameMessage  *message,
                       GError          **error)
{
	const guint8 *p;
	gsize avail;
	guint32 magic;
	guint16 version;
	guint16 type;
	guint32 body_size;

	if (reader->broken)
		return FALSE;
	avail = reader->buf->len - reader->start;
	if (avail < LRG_FRAME_HEADER_SIZE)
		return FALSE;

	p = reader->buf->data + reader->start;
	magic = lrg_frame_get_u32 (p);
	version = lrg_frame_get_u16 (p + 4);
	type = lrg_frame_get_u16 (p + 6);
	body_size = lrg_frame_get_u32 (p + 8);

	if (magic != LRG_FRAME_PROTOCOL_MAGIC)
	{
		reader->broken = TRUE;
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
		             "bad magic 0x%08x (not the frame protocol)", magic);
		return FALSE;
	}
	if (version != LRG_FRAME_PROTOCOL_VERSION)
	{
		reader->broken = TRUE;
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
		             "protocol version %u, this side speaks %u", version,
		             LRG_FRAME_PROTOCOL_VERSION);
		return FALSE;
	}
	if (body_size > LRG_FRAME_MAX_BODY_SIZE)
	{
		reader->broken = TRUE;
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
		             "a %u byte message is larger than the protocol allows", body_size);
		return FALSE;
	}
	if (avail < LRG_FRAME_HEADER_SIZE + (gsize) body_size)
		return FALSE;

	message->type = (LrgFrameMessageType) type;
	message->body = p + LRG_FRAME_HEADER_SIZE;
	message->body_size = body_size;
	message->fd = -1;

	if (type == LRG_FRAME_MSG_FRAME && body_size >= LRG_FRAME_FRAME_BODY_SIZE &&
	    (lrg_frame_get_u32 (message->body + 32) & LRG_FRAME_FLAG_PIXELS_IN_FD))
	{
		if (g_queue_is_empty (reader->fds))
		{
			reader->broken = TRUE;
			g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
			             "a frame in a memfd arrived without its descriptor");
			return FALSE;
		}
		message->fd = GPOINTER_TO_INT (g_queue_pop_head (reader->fds)) - 1;
	}

	reader->start += LRG_FRAME_HEADER_SIZE + body_size;
	return TRUE;
}
