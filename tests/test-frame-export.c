/* test-frame-export.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the frame protocol in one process, over a socketpair:
 * LrgFrameExport -> LrgFrameReceiver frames (memfd and inline, a padded
 * stride), the memfd's seals, LrgFrameReceiver -> LrgInputInject input
 * into an LrgInputSoftware, dropping frames under back-pressure and
 * reassembling partial writes, unknown types, protocol errors and a
 * closed peer.
 */

#include <glib.h>
#include <libregnum.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct
{
	gint fds[2];
} Pair;

static void
pair_open (Pair *pair)
{
	g_assert_cmpint (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair->fds), ==, 0);
}

static void
pair_close (Pair *pair)
{
	if (pair->fds[0] >= 0)
		close (pair->fds[0]);
	if (pair->fds[1] >= 0)
		close (pair->fds[1]);
}

/* A recognisable pattern with a padded stride. */
static guint8 *
make_pattern (guint  width,
              guint  height,
              guint  stride,
              guint8 seed)
{
	guint8 *px = g_malloc0 ((gsize) stride * height);
	guint x;
	guint y;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			guint8 *p = px + (gsize) y * stride + x * 4;

			p[0] = (guint8) (x * 7 + seed);
			p[1] = (guint8) (y * 13);
			p[2] = (guint8) (x ^ y);
			p[3] = 255;
		}
	}
	return px;
}

static void
assert_same_pixels (const guint8 *packed,
                    const guint8 *strided,
                    guint         width,
                    guint         height,
                    guint         stride)
{
	guint y;

	for (y = 0; y < height; y++)
		g_assert_cmpmem (packed + (gsize) y * width * 4, width * 4,
		                 strided + (gsize) y * stride, width * 4);
}

static void
test_frames_round_trip (void)
{
	Pair pair;
	g_autoptr(LrgFrameExport) export = NULL;
	g_autoptr(LrgFrameReceiver) receiver = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree guint8 *a = NULL;
	g_autofree guint8 *b = NULL;
	const guint8 *got;
	guint32 caps = 0;
	guint w = 0;
	guint h = 0;
	guint64 number = 99;
	gboolean in_memfd = FALSE;

	pair_open (&pair);
	export = lrg_frame_export_new (pair.fds[0], 32, 16, FALSE);
	receiver = lrg_frame_receiver_new (pair.fds[1], FALSE);

	g_assert_true (lrg_frame_receiver_dispatch (receiver, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_frame_receiver_get_hello (receiver, &caps, &w, &h));
	g_assert_cmpuint (w, ==, 32);
	g_assert_cmpuint (h, ==, 16);
	g_assert_true (caps & LRG_FRAME_CAPABILITY_MEMFD);
	g_assert_false (lrg_frame_receiver_has_frame (receiver));
	g_assert_null (lrg_frame_receiver_get_frame (receiver, NULL, NULL, NULL, NULL));

	/* in a memfd, with a padded stride that arrives tightly packed */
	g_assert_true (lrg_frame_export_get_use_memfd (export));
	a = make_pattern (32, 16, 136, 1);
	g_assert_true (lrg_frame_export_send_pixels (export, a, 32, 16, 136, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_frame_receiver_dispatch (receiver, &error));
	got = lrg_frame_receiver_get_frame (receiver, &w, &h, &number, &in_memfd);
	g_assert_nonnull (got);
	g_assert_cmpuint (w, ==, 32);
	g_assert_cmpuint (h, ==, 16);
	g_assert_cmpuint (number, ==, 0);
	g_assert_true (in_memfd);
	assert_same_pixels (got, a, 32, 16, 136);

	/* inline */
	lrg_frame_export_set_use_memfd (export, FALSE);
	b = make_pattern (20, 10, 80, 9);
	g_assert_true (lrg_frame_export_send_pixels (export, b, 20, 10, 80, &error));
	g_assert_true (lrg_frame_receiver_dispatch (receiver, &error));
	g_assert_no_error (error);
	got = lrg_frame_receiver_get_frame (receiver, &w, &h, &number, &in_memfd);
	g_assert_cmpuint (w, ==, 20);
	g_assert_cmpuint (number, ==, 1);
	g_assert_false (in_memfd);
	assert_same_pixels (got, b, 20, 10, 80);
	g_assert_cmpuint (lrg_frame_receiver_get_frames_received (receiver), ==, 2);
	g_assert_cmpuint (lrg_frame_export_get_sent_frames (export), ==, 2);
	g_assert_cmpuint (lrg_frame_export_get_frame_number (export), ==, 2);

	/* bad arguments are refused before anything is sent */
	g_assert_false (lrg_frame_export_send_pixels (export, b, 20, 10, 40, &error));
	g_assert_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_INVALID);
	g_clear_error (&error);
	g_assert_cmpuint (lrg_frame_export_get_frame_number (export), ==, 2);

	g_clear_object (&export);
	g_clear_object (&receiver);
	pair_close (&pair);
}

/* Reads one message by hand and checks the memfd that came with it. */
static void
test_memfd_is_sealed (void)
{
	Pair pair;
	g_autoptr(LrgFrameExport) export = NULL;
	g_autofree guint8 *px = make_pattern (8, 8, 32, 3);
	guint8 buf[256];
	union
	{
		struct cmsghdr align;
		gchar          buf[CMSG_SPACE (sizeof (gint) * 4)];
	} control;
	struct msghdr msg;
	struct iovec iov;
	struct cmsghdr *cmsg;
	gint fd = -1;
	gint seals;
	gssize n;

	pair_open (&pair);
	export = lrg_frame_export_new (pair.fds[0], 8, 8, FALSE);
	g_assert_true (lrg_frame_export_send_pixels (export, px, 8, 8, 32, NULL));

	/* the hello (32 bytes) then the frame (64 bytes) with its fd */
	memset (&msg, 0, sizeof (msg));
	iov.iov_base = buf;
	iov.iov_len = sizeof (buf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control.buf;
	msg.msg_controllen = sizeof (control.buf);
	n = recvmsg (pair.fds[1], &msg, MSG_CMSG_CLOEXEC);
	if (n == 32)
	{
		msg.msg_controllen = sizeof (control.buf);
		n = recvmsg (pair.fds[1], &msg, MSG_CMSG_CLOEXEC);
	}
	else
		g_assert_cmpint (n, ==, 96);
	for (cmsg = CMSG_FIRSTHDR (&msg); cmsg != NULL; cmsg = CMSG_NXTHDR (&msg, cmsg))
		if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
			memcpy (&fd, CMSG_DATA (cmsg), sizeof (gint));
	g_assert_cmpint (fd, >=, 0);

	/* the header says what the doc says */
	g_assert_cmpint (memcmp (buf + (n == 96 ? 32 : 0), "LFRM", 4), ==, 0);

	seals = fcntl (fd, F_GET_SEALS);
	g_assert_cmpint (seals, >=, 0);
	g_assert_true (seals & F_SEAL_WRITE);
	g_assert_true (seals & F_SEAL_SHRINK);
	g_assert_true (seals & F_SEAL_GROW);
	g_assert_cmpint (write (fd, "x", 1), <, 0);
	g_assert_cmpint (lseek (fd, 0, SEEK_END), ==, 8 * 32);
	close (fd);

	g_clear_object (&export);
	pair_close (&pair);
}

typedef struct
{
	guint motion;
	guint buttons;
	guint scrolls;
	guint keys;
	guint resizes;
	guint last_codepoint;
} InputLog;

static void
on_motion (LrgInputInject *inject, gfloat x, gfloat y, gpointer data)
{
	((InputLog *) data)->motion++;
}

static void
on_button (LrgInputInject *inject, guint button, gboolean pressed, gfloat x, gfloat y,
           gpointer data)
{
	((InputLog *) data)->buttons++;
}

static void
on_scroll (LrgInputInject *inject, gfloat dx, gfloat dy, gpointer data)
{
	((InputLog *) data)->scrolls++;
}

static void
on_key (LrgInputInject *inject, guint key, gboolean pressed, guint mods, guint codepoint,
        gpointer data)
{
	((InputLog *) data)->keys++;
	((InputLog *) data)->last_codepoint = codepoint;
}

static void
on_resize (LrgInputInject *inject, guint w, guint h, gpointer data)
{
	((InputLog *) data)->resizes++;
}

static void
test_input_round_trip (void)
{
	Pair pair;
	g_autoptr(LrgFrameReceiver) host = NULL;
	g_autoptr(LrgInputInject) inject = NULL;
	g_autoptr(LrgInputSoftware) software = lrg_input_software_new ();
	g_autoptr(GError) error = NULL;
	InputLog log = { 0, 0, 0, 0, 0, 0 };
	gfloat x;
	gfloat y;
	guint w;
	guint h;
	guint8 junk[24];

	pair_open (&pair);
	host = lrg_frame_receiver_new (pair.fds[1], FALSE);
	inject = lrg_input_inject_new (pair.fds[0]);
	lrg_input_inject_set_target (inject, software);
	g_assert_true (lrg_input_inject_get_target (inject) == software);
	g_signal_connect (inject, "pointer-motion", G_CALLBACK (on_motion), &log);
	g_signal_connect (inject, "button", G_CALLBACK (on_button), &log);
	g_signal_connect (inject, "scroll", G_CALLBACK (on_scroll), &log);
	g_signal_connect (inject, "key", G_CALLBACK (on_key), &log);
	g_signal_connect (inject, "resize", G_CALLBACK (on_resize), &log);

	g_assert_true (lrg_input_inject_dispatch (inject, &error));  /* nothing yet */
	g_assert_false (lrg_input_inject_get_size (inject, NULL, NULL));

	g_assert_true (lrg_frame_receiver_send_pointer_move (host, 12.5f, 40.0f, &error));
	g_assert_true (lrg_frame_receiver_send_button (host, GRL_MOUSE_BUTTON_LEFT, TRUE, 13.0f, 41.0f, &error));

	/* an unknown message type in between is skipped, body and all */
	memset (junk, 0xAB, sizeof (junk));
	memcpy (junk, "LFRM", 4);
	junk[4] = 1; junk[5] = 0;     /* version 1 */
	junk[6] = 99; junk[7] = 0;    /* type 99 */
	junk[8] = 8; junk[9] = 0; junk[10] = 0; junk[11] = 0;
	junk[12] = junk[13] = junk[14] = junk[15] = 0;
	g_assert_cmpint (write (pair.fds[1], junk, sizeof (junk)), ==, (gssize) sizeof (junk));

	g_assert_true (lrg_frame_receiver_send_scroll (host, 0.0f, 2.0f, &error));
	g_assert_true (lrg_frame_receiver_send_scroll (host, 1.0f, -0.5f, &error));
	g_assert_true (lrg_frame_receiver_send_key (host, GRL_KEY_A, TRUE,
	                                            LRG_FRAME_MODIFIER_SHIFT, 'A', &error));
	g_assert_true (lrg_frame_receiver_send_resize (host, 640, 360, &error));
	g_assert_true (lrg_frame_receiver_send_resize (host, 0, 360, &error));   /* ignored */
	g_assert_no_error (error);

	g_assert_true (lrg_input_inject_dispatch (inject, &error));
	g_assert_no_error (error);

	g_assert_cmpuint (log.motion, ==, 1);
	g_assert_cmpuint (log.buttons, ==, 1);
	g_assert_cmpuint (log.scrolls, ==, 2);
	g_assert_cmpuint (log.keys, ==, 1);
	g_assert_cmpuint (log.last_codepoint, ==, 'A');
	g_assert_cmpuint (log.resizes, ==, 1);

	lrg_input_inject_get_pointer (inject, &x, &y);
	g_assert_cmpfloat (x, ==, 13.0f);
	g_assert_cmpfloat (y, ==, 41.0f);
	g_assert_true (lrg_input_inject_is_button_down (inject, 0));
	g_assert_false (lrg_input_inject_is_button_down (inject, 1));
	g_assert_false (lrg_input_inject_is_button_down (inject, 99));
	lrg_input_inject_take_scroll (inject, &x, &y);
	g_assert_cmpfloat (x, ==, 1.0f);
	g_assert_cmpfloat (y, ==, 1.5f);
	lrg_input_inject_take_scroll (inject, &x, &y);
	g_assert_cmpfloat (y, ==, 0.0f);
	g_assert_true (lrg_input_inject_get_size (inject, &w, &h));
	g_assert_cmpuint (w, ==, 640);
	g_assert_cmpuint (h, ==, 360);
	g_assert_cmpuint (lrg_input_inject_get_modifiers (inject), ==, LRG_FRAME_MODIFIER_SHIFT);

	/* the software input source saw it too */
	g_assert_true (lrg_input_is_key_down (LRG_INPUT (software), GRL_KEY_A));
	g_assert_true (lrg_input_is_mouse_button_down (LRG_INPUT (software), GRL_MOUSE_BUTTON_LEFT));
	lrg_input_get_mouse_position (LRG_INPUT (software), &x, &y);
	g_assert_cmpfloat (x, ==, 13.0f);
	g_assert_cmpfloat (y, ==, 41.0f);

	/* releases */
	g_assert_true (lrg_frame_receiver_send_key (host, GRL_KEY_A, FALSE, 0, 0, &error));
	g_assert_true (lrg_frame_receiver_send_button (host, GRL_MOUSE_BUTTON_LEFT, FALSE, 13.0f, 41.0f, &error));
	g_assert_true (lrg_input_inject_dispatch (inject, &error));
	g_assert_false (lrg_input_is_key_down (LRG_INPUT (software), GRL_KEY_A));
	g_assert_false (lrg_input_inject_is_button_down (inject, 0));

	/* the host goes away */
	g_clear_object (&host);
	close (pair.fds[1]);
	pair.fds[1] = -1;
	g_assert_false (lrg_input_inject_dispatch (inject, &error));
	g_assert_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_CLOSED);

	g_clear_object (&inject);
	pair_close (&pair);
}

static void
test_backpressure (void)
{
	Pair pair;
	g_autoptr(LrgFrameExport) export = NULL;
	g_autoptr(LrgFrameReceiver) receiver = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree guint8 *px = make_pattern (128, 128, 512, 5);
	gint small = 16384;
	guint64 last = 0;
	guint i;

	pair_open (&pair);
	g_assert_cmpint (setsockopt (pair.fds[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof (small)), ==, 0);
	export = lrg_frame_export_new (pair.fds[0], 128, 128, FALSE);
	lrg_frame_export_set_use_memfd (export, FALSE);
	receiver = lrg_frame_receiver_new (pair.fds[1], FALSE);

	/* 64 KiB frames into a 16 KiB socket nobody reads: one is partly
	 * written, the rest are dropped, none block */
	for (i = 0; i < 20; i++)
		g_assert_true (lrg_frame_export_send_pixels (export, px, 128, 128, 512, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_frame_export_get_frame_number (export), ==, 20);
	g_assert_cmpuint (lrg_frame_export_get_dropped_frames (export), >, 0);
	g_assert_cmpuint (lrg_frame_export_get_sent_frames (export) +
	                  lrg_frame_export_get_dropped_frames (export), ==, 20);

	/* the host catches up: partial writes are reassembled, nothing torn */
	for (i = 0; i < 1000 && lrg_frame_receiver_get_frames_received (receiver) <
	                        lrg_frame_export_get_sent_frames (export); i++)
	{
		g_assert_true (lrg_frame_receiver_dispatch (receiver, &error));
		g_assert_true (lrg_frame_export_flush (export, &error));
		if (lrg_frame_receiver_has_frame (receiver))
		{
			guint64 n;
			const guint8 *got = lrg_frame_receiver_get_frame (receiver, NULL, NULL, &n, NULL);

			g_assert_cmpuint (n, >=, last);
			last = n;
			assert_same_pixels (got, px, 128, 128, 512);
		}
	}
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_frame_receiver_get_frames_received (receiver), ==,
	                  lrg_frame_export_get_sent_frames (export));

	/* once drained, frames flow again */
	g_assert_true (lrg_frame_export_send_pixels (export, px, 128, 128, 512, &error));
	for (i = 0; i < 1000 && last != 20; i++)
	{
		g_assert_true (lrg_frame_receiver_dispatch (receiver, &error));
		g_assert_true (lrg_frame_export_flush (export, &error));
		lrg_frame_receiver_get_frame (receiver, NULL, NULL, &last, NULL);
	}
	g_assert_cmpuint (last, ==, 20);

	g_clear_object (&export);
	g_clear_object (&receiver);
	pair_close (&pair);
}

static void
expect_protocol_error (const guint8 *bytes,
                       gsize         len)
{
	Pair pair;
	g_autoptr(LrgFrameReceiver) receiver = NULL;
	g_autoptr(GError) error = NULL;

	pair_open (&pair);
	receiver = lrg_frame_receiver_new (pair.fds[1], FALSE);
	g_assert_cmpint (write (pair.fds[0], bytes, len), ==, (gssize) len);
	g_assert_false (lrg_frame_receiver_dispatch (receiver, &error));
	g_assert_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_PROTOCOL);
	/* the connection stays broken */
	g_clear_error (&error);
	g_assert_false (lrg_frame_receiver_dispatch (receiver, &error));
	g_assert_nonnull (error);
	g_clear_object (&receiver);
	pair_close (&pair);
}

static void
test_protocol_errors (void)
{
	guint8 m[16 + 48];
	Pair pair;
	g_autoptr(LrgFrameReceiver) receiver = NULL;
	g_autoptr(GError) error = NULL;

	/* not the protocol at all */
	expect_protocol_error ((const guint8 *) "GET / HTTP/1.1\r\n\r\n", 18);

	/* a version from the future */
	memset (m, 0, sizeof (m));
	memcpy (m, "LFRM", 4);
	m[4] = 2;
	m[6] = LRG_FRAME_MSG_HELLO;
	m[8] = 16;
	expect_protocol_error (m, 32);

	/* an absurd body size */
	m[4] = 1;
	m[8] = 0; m[9] = 0; m[10] = 0; m[11] = 0x20;   /* 512 MiB */
	expect_protocol_error (m, 16);

	/* a memfd frame with no descriptor */
	memset (m, 0, sizeof (m));
	memcpy (m, "LFRM", 4);
	m[4] = 1;
	m[6] = LRG_FRAME_MSG_FRAME;
	m[8] = 48;
	m[16] = 2; m[20] = 2; m[24] = 8; m[28] = 1;    /* 2x2, stride 8, RGBA8 */
	m[16 + 32] = 1;                                /* PIXELS_IN_FD */
	m[16 + 36] = 16;
	expect_protocol_error (m, sizeof (m));

	/* an inline frame whose numbers do not add up */
	m[16 + 32] = 0;
	m[16 + 36] = 99;
	expect_protocol_error (m, sizeof (m));

	/* the application closes */
	pair_open (&pair);
	receiver = lrg_frame_receiver_new (pair.fds[1], FALSE);
	close (pair.fds[0]);
	pair.fds[0] = -1;
	g_assert_false (lrg_frame_receiver_dispatch (receiver, &error));
	g_assert_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_CLOSED);
	g_clear_object (&receiver);
	pair_close (&pair);
}

static void
test_send_after_close (void)
{
	Pair pair;
	g_autoptr(LrgFrameExport) export = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree guint8 *px = make_pattern (4, 4, 16, 0);

	pair_open (&pair);
	export = lrg_frame_export_new (pair.fds[0], 4, 4, TRUE);    /* takes the fd */
	close (pair.fds[1]);
	pair.fds[1] = -1;
	pair.fds[0] = -1;
	/* no SIGPIPE: MSG_NOSIGNAL; a clean error instead */
	g_assert_false (lrg_frame_export_send_pixels (export, px, 4, 4, 16, &error));
	g_assert_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_CLOSED);
	g_clear_error (&error);
	g_assert_false (lrg_frame_export_flush (export, &error));
	g_assert_nonnull (error);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/frame-export/frames-round-trip", test_frames_round_trip);
	g_test_add_func ("/frame-export/memfd-is-sealed", test_memfd_is_sealed);
	g_test_add_func ("/frame-export/input-round-trip", test_input_round_trip);
	g_test_add_func ("/frame-export/backpressure", test_backpressure);
	g_test_add_func ("/frame-export/protocol-errors", test_protocol_errors);
	g_test_add_func ("/frame-export/send-after-close", test_send_after_close);

	return g_test_run ();
}
