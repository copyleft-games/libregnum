/* lrg-frame-export.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFrameExport: offscreen rendering into a render texture, read-back,
 * and frames sent as sealed memfds (Linux) or inline, dropping rather
 * than queueing when the host is behind.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_NET

#include "lrg-frame-export.h"
#include "lrg-frame-wire-private.h"
#include "../lrg-log.h"

#include <raylib.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/mman.h>
#define LRG_HAVE_MEMFD 1
#if defined(__ANDROID__) && __ANDROID_API__ < 30
/* bionic declares memfd_create() from API 30 (libregnum's minimum is 29);
 * the system call itself is in every kernel Android 10 ships (3.17+) */
#include <sys/syscall.h>
static int
lrg_memfd_create (const char   *name,
                  unsigned int  flags)
{
	return (int) syscall (__NR_memfd_create, name, flags);
}
#define memfd_create lrg_memfd_create
#endif
#endif

struct _LrgFrameExport
{
	GObject           parent_instance;

	gint              fd;
	gboolean          own_fd;
	LrgFrameWriter    writer;
	gboolean          use_memfd;
	guint             width;
	guint             height;
	GrlRenderTexture *target;
	gboolean          drawing;
	guint64           frame_number;
	guint64           sent;
	guint64           dropped;
};

G_DEFINE_FINAL_TYPE (LrgFrameExport, lrg_frame_export, G_TYPE_OBJECT)

static void
lrg_frame_export_finalize (GObject *object)
{
	LrgFrameExport *self = LRG_FRAME_EXPORT (object);

	lrg_frame_writer_clear (&self->writer);
	g_clear_object (&self->target);
#ifdef G_OS_UNIX
	if (self->own_fd && self->fd >= 0)
		close (self->fd);
#endif
	G_OBJECT_CLASS (lrg_frame_export_parent_class)->finalize (object);
}

static void
lrg_frame_export_class_init (LrgFrameExportClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_frame_export_finalize;
}

static void
lrg_frame_export_init (LrgFrameExport *self)
{
	self->fd = -1;
#ifdef LRG_HAVE_MEMFD
	self->use_memfd = TRUE;
#endif
}

LrgFrameExport *
lrg_frame_export_new (gint     fd,
                      guint    width,
                      guint    height,
                      gboolean take_fd)
{
	LrgFrameExport *self;
	guint8 hello[LRG_FRAME_HEADER_SIZE + LRG_FRAME_HELLO_BODY_SIZE];
	g_autoptr(GError) error = NULL;

	g_return_val_if_fail (fd >= 0, NULL);
	g_return_val_if_fail (width > 0 && height > 0, NULL);

	self = g_object_new (LRG_TYPE_FRAME_EXPORT, NULL);
	self->fd = fd;
	self->own_fd = take_fd;
	self->width = width;
	self->height = height;
	lrg_frame_writer_init (&self->writer, fd);

	/* the hello is never dropped: it is queued if the socket is full */
	lrg_frame_put_header (hello, LRG_FRAME_MSG_HELLO, LRG_FRAME_HELLO_BODY_SIZE);
#ifdef LRG_HAVE_MEMFD
	lrg_frame_put_u32 (hello + 16, LRG_FRAME_CAPABILITY_MEMFD);
#else
	lrg_frame_put_u32 (hello + 16, 0);
#endif
	lrg_frame_put_u32 (hello + 20, width);
	lrg_frame_put_u32 (hello + 24, height);
	lrg_frame_put_u32 (hello + 28, 0);
	if (lrg_frame_writer_send (&self->writer, hello, sizeof (hello), NULL, 0, -1, FALSE,
	                           &error) == LRG_FRAME_SEND_FAILED)
		lrg_debug (LRG_LOG_DOMAIN_NET, "frame export: hello not sent: %s", error->message);
	return self;
}

void
lrg_frame_export_set_use_memfd (LrgFrameExport *self,
                                gboolean        use_memfd)
{
	g_return_if_fail (LRG_IS_FRAME_EXPORT (self));
#ifdef LRG_HAVE_MEMFD
	self->use_memfd = use_memfd;
#else
	self->use_memfd = FALSE;
#endif
}

gboolean
lrg_frame_export_get_use_memfd (LrgFrameExport *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), FALSE);
	return self->use_memfd;
}

void
lrg_frame_export_set_size (LrgFrameExport *self,
                           guint           width,
                           guint           height)
{
	g_return_if_fail (LRG_IS_FRAME_EXPORT (self));
	g_return_if_fail (width > 0 && height > 0);

	self->width = MIN (width, (guint) LRG_FRAME_MAX_DIMENSION);
	self->height = MIN (height, (guint) LRG_FRAME_MAX_DIMENSION);
}

void
lrg_frame_export_get_size (LrgFrameExport *self,
                           guint          *width,
                           guint          *height)
{
	g_return_if_fail (LRG_IS_FRAME_EXPORT (self));
	if (width != NULL)
		*width = self->width;
	if (height != NULL)
		*height = self->height;
}

GrlRenderTexture *
lrg_frame_export_get_texture (LrgFrameExport *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), NULL);
	return self->target;
}

gboolean
lrg_frame_export_flush (LrgFrameExport  *self,
                        GError         **error)
{
	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), FALSE);
	return lrg_frame_writer_flush (&self->writer, error);
}

guint64
lrg_frame_export_get_frame_number (LrgFrameExport *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), 0);
	return self->frame_number;
}

guint64
lrg_frame_export_get_sent_frames (LrgFrameExport *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), 0);
	return self->sent;
}

guint64
lrg_frame_export_get_dropped_frames (LrgFrameExport *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), 0);
	return self->dropped;
}

#ifdef LRG_HAVE_MEMFD
/*
 * Copies the rows into a new memfd and seals it, so the host can map it
 * without the sender ever changing it underneath.  write() rather than a
 * shared mapping: F_SEAL_WRITE is refused while a writable mapping
 * exists.
 */
static gint
make_frame_memfd (const guint8  *rgba,
                  guint          height,
                  guint          stride,
                  GError       **error)
{
	gsize size = (gsize) stride * height;
	gsize done = 0;
	gint fd;

	fd = memfd_create ("lrg-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (fd < 0)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_IO, "memfd_create: %s",
		             g_strerror (errno));
		return -1;
	}
	while (done < size)
	{
		gssize n = write (fd, rgba + done, size - done);

		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
		{
			g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_IO, "writing a frame memfd: %s",
			             g_strerror (errno));
			close (fd);
			return -1;
		}
		done += (gsize) n;
	}
	if (fcntl (fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) < 0)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_IO, "sealing a frame memfd: %s",
		             g_strerror (errno));
		close (fd);
		return -1;
	}
	return fd;
}
#endif

gboolean
lrg_frame_export_send_pixels (LrgFrameExport  *self,
                              const guint8    *rgba,
                              guint            width,
                              guint            height,
                              guint            stride,
                              GError         **error)
{
	guint8 head[LRG_FRAME_HEADER_SIZE + LRG_FRAME_FRAME_BODY_SIZE];
	guint8 *body = head + LRG_FRAME_HEADER_SIZE;
	guint64 pixel_bytes;
	gboolean in_fd = FALSE;
	gint fd = -1;
	LrgFrameSendResult result;

	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), FALSE);
	g_return_val_if_fail (rgba != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	pixel_bytes = (guint64) stride * height;
	if (width == 0 || height == 0 || width > LRG_FRAME_MAX_DIMENSION ||
	    height > LRG_FRAME_MAX_DIMENSION || stride < width * 4u ||
	    pixel_bytes > LRG_FRAME_MAX_BODY_SIZE - LRG_FRAME_FRAME_BODY_SIZE)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_INVALID,
		             "cannot send a %ux%u frame with stride %u", width, height, stride);
		return FALSE;
	}

	/* a frame queued behind a busy socket would only be late: drop it */
	if (!lrg_frame_writer_flush (&self->writer, error))
		return FALSE;
	if (self->writer.pending->len > 0)
	{
		self->frame_number++;
		self->dropped++;
		return TRUE;
	}

#ifdef LRG_HAVE_MEMFD
	if (self->use_memfd)
	{
		fd = make_frame_memfd (rgba, height, stride, error);
		if (fd < 0)
			return FALSE;
		in_fd = TRUE;
	}
#endif

	lrg_frame_put_header (head, LRG_FRAME_MSG_FRAME,
	                      LRG_FRAME_FRAME_BODY_SIZE + (in_fd ? 0u : (guint32) pixel_bytes));
	lrg_frame_put_u32 (body + 0, width);
	lrg_frame_put_u32 (body + 4, height);
	lrg_frame_put_u32 (body + 8, stride);
	lrg_frame_put_u32 (body + 12, LRG_FRAME_FORMAT_RGBA8);
	lrg_frame_put_u64 (body + 16, self->frame_number);
	lrg_frame_put_u64 (body + 24, (guint64) g_get_monotonic_time ());
	lrg_frame_put_u32 (body + 32, in_fd ? LRG_FRAME_FLAG_PIXELS_IN_FD : 0u);
	lrg_frame_put_u32 (body + 36, (guint32) pixel_bytes);
	lrg_frame_put_u32 (body + 40, 0);
	lrg_frame_put_u32 (body + 44, 0);

	result = lrg_frame_writer_send (&self->writer, head, sizeof (head),
	                                in_fd ? NULL : rgba, in_fd ? 0 : (gsize) pixel_bytes,
	                                fd, TRUE, error);
#ifdef G_OS_UNIX
	/* the kernel holds its own reference to a passed descriptor */
	if (fd >= 0)
		close (fd);
#endif
	self->frame_number++;
	switch (result)
	{
	case LRG_FRAME_SEND_OK:
		self->sent++;
		return TRUE;
	case LRG_FRAME_SEND_DROPPED:
		self->dropped++;
		return TRUE;
	case LRG_FRAME_SEND_FAILED:
	default:
		return FALSE;
	}
}

gboolean
lrg_frame_export_send_image (LrgFrameExport  *self,
                             GrlImage        *image,
                             GError         **error)
{
	const guint8 *pixels;
	gint w;
	gint h;

	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), FALSE);
	g_return_val_if_fail (GRL_IS_IMAGE (image), FALSE);

	pixels = grl_image_get_pixels (image, NULL);
	w = grl_image_get_width (image);
	h = grl_image_get_height (image);
	if (pixels == NULL || w <= 0 || h <= 0)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_INVALID, "the image is empty");
		return FALSE;
	}
	return lrg_frame_export_send_pixels (self, pixels, (guint) w, (guint) h, (guint) w * 4u, error);
}

gboolean
lrg_frame_export_begin (LrgFrameExport *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), FALSE);

	if (!IsWindowReady ())
		return FALSE;
	if (self->target == NULL ||
	    (guint) grl_render_texture_get_width (self->target) != self->width ||
	    (guint) grl_render_texture_get_height (self->target) != self->height)
	{
		g_clear_object (&self->target);
		self->target = grl_render_texture_new ((gint) self->width, (gint) self->height);
		if (self->target == NULL || !grl_render_texture_is_valid (self->target))
		{
			g_clear_object (&self->target);
			return FALSE;
		}
	}
	grl_render_texture_begin (self->target);
	self->drawing = TRUE;
	return TRUE;
}

gboolean
lrg_frame_export_end (LrgFrameExport  *self,
                      GError         **error)
{
	g_autoptr(GrlImage) image = NULL;

	g_return_val_if_fail (LRG_IS_FRAME_EXPORT (self), FALSE);

	if (!self->drawing)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_INVALID,
		             "lrg_frame_export_end() without a successful begin()");
		return FALSE;
	}
	self->drawing = FALSE;
	grl_render_texture_end (self->target);

	/* GL textures are stored bottom row first */
	image = grl_texture_to_image (grl_render_texture_get_texture (self->target));
	if (image == NULL)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_IO, "could not read the frame back");
		return FALSE;
	}
	grl_image_flip_vertical (image);
	return lrg_frame_export_send_image (self, image, error);
}
