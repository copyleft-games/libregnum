/* lrg-frame-receiver.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFrameReceiver: validates and unpacks frames (inline or from a
 * memfd mapped read-only), keeps the newest, and encodes input events.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_NET

#include "lrg-frame-receiver.h"
#include "lrg-frame-wire-private.h"
#include "../lrg-log.h"

#include <string.h>

#ifdef G_OS_UNIX
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct _LrgFrameReceiver
{
	GObject         parent_instance;

	gint            fd;
	gboolean        own_fd;
	LrgFrameReader  reader;
	LrgFrameWriter  writer;

	gboolean        have_hello;
	guint32         capabilities;
	guint           hello_width;
	guint           hello_height;

	GByteArray     *pixels;
	guint           width;
	guint           height;
	guint64         frame_number;
	gboolean        in_memfd;
	gboolean        have_frame;
	guint64         received;
};

G_DEFINE_FINAL_TYPE (LrgFrameReceiver, lrg_frame_receiver, G_TYPE_OBJECT)

enum
{
	SIGNAL_FRAME,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_frame_receiver_finalize (GObject *object)
{
	LrgFrameReceiver *self = LRG_FRAME_RECEIVER (object);

	lrg_frame_reader_clear (&self->reader);
	lrg_frame_writer_clear (&self->writer);
	g_byte_array_unref (self->pixels);
#ifdef G_OS_UNIX
	if (self->own_fd && self->fd >= 0)
		close (self->fd);
#endif
	G_OBJECT_CLASS (lrg_frame_receiver_parent_class)->finalize (object);
}

static void
lrg_frame_receiver_class_init (LrgFrameReceiverClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_frame_receiver_finalize;

	/**
	 * LrgFrameReceiver::frame:
	 * @self: the receiver
	 * @frame_number: the frame's number
	 *
	 * Emitted for each frame received; read it with
	 * lrg_frame_receiver_get_frame().
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_FRAME] = g_signal_new ("frame", G_TYPE_FROM_CLASS (klass),
	                                      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                      G_TYPE_NONE, 1, G_TYPE_UINT64);
}

static void
lrg_frame_receiver_init (LrgFrameReceiver *self)
{
	self->fd = -1;
	self->pixels = g_byte_array_new ();
}

LrgFrameReceiver *
lrg_frame_receiver_new (gint     fd,
                        gboolean take_fd)
{
	LrgFrameReceiver *self;

	g_return_val_if_fail (fd >= 0, NULL);

	self = g_object_new (LRG_TYPE_FRAME_RECEIVER, NULL);
	self->fd = fd;
	self->own_fd = take_fd;
	lrg_frame_reader_init (&self->reader, fd);
	lrg_frame_writer_init (&self->writer, fd);
	return self;
}

/* Copies @height rows of @width x 4 bytes out of @src (with @stride). */
static void
store_rows (LrgFrameReceiver *self,
            const guint8     *src,
            guint             width,
            guint             height,
            guint             stride)
{
	gsize row = (gsize) width * 4;
	guint y;

	g_byte_array_set_size (self->pixels, (guint) (row * height));
	for (y = 0; y < height; y++)
		memcpy (self->pixels->data + row * y, src + (gsize) stride * y, row);
}

static gboolean
handle_frame (LrgFrameReceiver       *self,
              const LrgFrameMessage  *msg,
              GError                **error)
{
	const guint8 *b = msg->body;
	guint width;
	guint height;
	guint stride;
	guint format;
	guint flags;
	guint64 pixel_bytes;

	if (msg->body_size < LRG_FRAME_FRAME_BODY_SIZE)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
		             "a frame message of %u bytes is too short", msg->body_size);
		return FALSE;
	}
	width = lrg_frame_get_u32 (b);
	height = lrg_frame_get_u32 (b + 4);
	stride = lrg_frame_get_u32 (b + 8);
	format = lrg_frame_get_u32 (b + 12);
	flags = lrg_frame_get_u32 (b + 32);
	pixel_bytes = lrg_frame_get_u32 (b + 36);

	if (format != LRG_FRAME_FORMAT_RGBA8 || width == 0 || height == 0 ||
	    width > LRG_FRAME_MAX_DIMENSION || height > LRG_FRAME_MAX_DIMENSION ||
	    stride < width * 4u || pixel_bytes != (guint64) stride * height)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
		             "a %ux%u frame (stride %u, %" G_GUINT64_FORMAT " bytes, format %u) "
		             "does not add up", width, height, stride, pixel_bytes, format);
		return FALSE;
	}

	if (flags & LRG_FRAME_FLAG_PIXELS_IN_FD)
	{
#ifdef G_OS_UNIX
		struct stat st;
		gpointer map;

		if (msg->fd < 0 || fstat (msg->fd, &st) < 0 || (guint64) st.st_size < pixel_bytes)
		{
			g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
			             "the frame's memfd is missing or smaller than its pixels");
			return FALSE;
		}
		map = mmap (NULL, (gsize) pixel_bytes, PROT_READ, MAP_PRIVATE, msg->fd, 0);
		if (map == MAP_FAILED)
		{
			g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_IO, "mmap: %s",
			             g_strerror (errno));
			return FALSE;
		}
		store_rows (self, map, width, height, stride);
		munmap (map, (gsize) pixel_bytes);
		self->in_memfd = TRUE;
#else
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
		             "memfd frames need a Unix system");
		return FALSE;
#endif
	}
	else
	{
		if ((guint64) msg->body_size < LRG_FRAME_FRAME_BODY_SIZE + pixel_bytes)
		{
			g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL,
			             "an inline frame is shorter than its pixels");
			return FALSE;
		}
		store_rows (self, b + LRG_FRAME_FRAME_BODY_SIZE, width, height, stride);
		self->in_memfd = FALSE;
	}

	self->width = width;
	self->height = height;
	self->frame_number = lrg_frame_get_u64 (b + 16);
	self->have_frame = TRUE;
	self->received++;
	g_signal_emit (self, signals[SIGNAL_FRAME], 0, self->frame_number);
	return TRUE;
}

gboolean
lrg_frame_receiver_dispatch (LrgFrameReceiver  *self,
                             GError           **error)
{
	LrgFrameMessage msg;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!lrg_frame_writer_flush (&self->writer, error))
		return FALSE;
	if (!lrg_frame_reader_fill (&self->reader, error))
		return FALSE;

	while (lrg_frame_reader_next (&self->reader, &msg, &local))
	{
		gboolean ok = TRUE;

		if (msg.type == LRG_FRAME_MSG_HELLO && msg.body_size >= LRG_FRAME_HELLO_BODY_SIZE)
		{
			self->have_hello = TRUE;
			self->capabilities = lrg_frame_get_u32 (msg.body);
			self->hello_width = lrg_frame_get_u32 (msg.body + 4);
			self->hello_height = lrg_frame_get_u32 (msg.body + 8);
		}
		else if (msg.type == LRG_FRAME_MSG_FRAME)
			ok = handle_frame (self, &msg, &local);

#ifdef G_OS_UNIX
		if (msg.fd >= 0)
			close (msg.fd);
#endif
		if (!ok)
		{
			/* a frame that does not add up desynchronises nothing, but
			 * nothing after it can be trusted either */
			self->reader.broken = TRUE;
			g_propagate_error (error, g_steal_pointer (&local));
			return FALSE;
		}
	}
	if (local != NULL)
	{
		g_propagate_error (error, g_steal_pointer (&local));
		return FALSE;
	}
	if (self->reader.eof)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_CLOSED,
		             "the application closed the connection");
		return FALSE;
	}
	return TRUE;
}

gboolean
lrg_frame_receiver_get_hello (LrgFrameReceiver *self,
                              guint32          *capabilities,
                              guint            *width,
                              guint            *height)
{
	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), FALSE);

	if (!self->have_hello)
		return FALSE;
	if (capabilities != NULL)
		*capabilities = self->capabilities;
	if (width != NULL)
		*width = self->hello_width;
	if (height != NULL)
		*height = self->hello_height;
	return TRUE;
}

gboolean
lrg_frame_receiver_has_frame (LrgFrameReceiver *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), FALSE);
	return self->have_frame;
}

const guint8 *
lrg_frame_receiver_get_frame (LrgFrameReceiver *self,
                              guint            *width,
                              guint            *height,
                              guint64          *frame_number,
                              gboolean         *in_memfd)
{
	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), NULL);

	if (!self->have_frame)
		return NULL;
	if (width != NULL)
		*width = self->width;
	if (height != NULL)
		*height = self->height;
	if (frame_number != NULL)
		*frame_number = self->frame_number;
	if (in_memfd != NULL)
		*in_memfd = self->in_memfd;
	return self->pixels->data;
}

guint64
lrg_frame_receiver_get_frames_received (LrgFrameReceiver *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), 0);
	return self->received;
}

static gboolean
send_message (LrgFrameReceiver     *self,
              LrgFrameMessageType   type,
              const guint8         *body,
              guint32               body_size,
              GError              **error)
{
	guint8 buf[LRG_FRAME_HEADER_SIZE + 32];

	g_assert (body_size <= 32);
	lrg_frame_put_header (buf, type, body_size);
	memcpy (buf + LRG_FRAME_HEADER_SIZE, body, body_size);
	return lrg_frame_writer_send (&self->writer, buf, LRG_FRAME_HEADER_SIZE + body_size,
	                              NULL, 0, -1, FALSE, error) != LRG_FRAME_SEND_FAILED;
}

gboolean
lrg_frame_receiver_send_pointer_move (LrgFrameReceiver  *self,
                                      gfloat             x,
                                      gfloat             y,
                                      GError           **error)
{
	guint8 body[8];

	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), FALSE);
	lrg_frame_put_f32 (body, x);
	lrg_frame_put_f32 (body + 4, y);
	return send_message (self, LRG_FRAME_MSG_POINTER_MOVE, body, sizeof (body), error);
}

gboolean
lrg_frame_receiver_send_button (LrgFrameReceiver  *self,
                                guint              button,
                                gboolean           pressed,
                                gfloat             x,
                                gfloat             y,
                                GError           **error)
{
	guint8 body[16];

	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), FALSE);
	lrg_frame_put_u32 (body, button);
	lrg_frame_put_u32 (body + 4, pressed ? 1u : 0u);
	lrg_frame_put_f32 (body + 8, x);
	lrg_frame_put_f32 (body + 12, y);
	return send_message (self, LRG_FRAME_MSG_POINTER_BUTTON, body, sizeof (body), error);
}

gboolean
lrg_frame_receiver_send_scroll (LrgFrameReceiver  *self,
                                gfloat             dx,
                                gfloat             dy,
                                GError           **error)
{
	guint8 body[8];

	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), FALSE);
	lrg_frame_put_f32 (body, dx);
	lrg_frame_put_f32 (body + 4, dy);
	return send_message (self, LRG_FRAME_MSG_SCROLL, body, sizeof (body), error);
}

gboolean
lrg_frame_receiver_send_key (LrgFrameReceiver  *self,
                             guint              key,
                             gboolean           pressed,
                             guint              modifiers,
                             gunichar           codepoint,
                             GError           **error)
{
	guint8 body[16];

	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), FALSE);
	lrg_frame_put_u32 (body, key);
	lrg_frame_put_u32 (body + 4, pressed ? 1u : 0u);
	lrg_frame_put_u32 (body + 8, modifiers);
	lrg_frame_put_u32 (body + 12, codepoint);
	return send_message (self, LRG_FRAME_MSG_KEY, body, sizeof (body), error);
}

gboolean
lrg_frame_receiver_send_resize (LrgFrameReceiver  *self,
                                guint              width,
                                guint              height,
                                GError           **error)
{
	guint8 body[8];

	g_return_val_if_fail (LRG_IS_FRAME_RECEIVER (self), FALSE);
	lrg_frame_put_u32 (body, width);
	lrg_frame_put_u32 (body + 4, height);
	return send_message (self, LRG_FRAME_MSG_RESIZE, body, sizeof (body), error);
}
