/* test-screenshot-frames.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for lrg_screenshot_write_png() (headless) and
 * lrg_screenshot_after_frames() and LrgFrameExport's begin()/end()
 * (with a display; skipped without one, and refused cleanly).  Both GPU
 * tests check the picture's orientation, which a GL read-back gets
 * upside down when nothing flips it.  Run them off the desktop with
 * build-aux/xrun.sh.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <raylib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static gboolean
have_display (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	return (display != NULL && *display != '\0') || (wayland != NULL && *wayland != '\0');
}

static gchar *
temp_png (const gchar *name)
{
	return g_build_filename (g_get_tmp_dir (), name, NULL);
}

static void
assert_pixel (GrlImage *image,
              gint      x,
              gint      y,
              guint8    r,
              guint8    g,
              guint8    b)
{
	g_autoptr(GrlColor) c = grl_image_get_pixel (image, x, y);

	g_assert_cmpuint (c->r, ==, r);
	g_assert_cmpuint (c->g, ==, g);
	g_assert_cmpuint (c->b, ==, b);
}

static void
test_write_png (void)
{
	g_autofree gchar *path = temp_png ("lrg-test-write.png");
	g_autoptr(GrlImage) back = NULL;
	g_autoptr(GError) error = NULL;
	/* 3x2 with a 16-byte stride (4 bytes of padding per row) */
	guint8 px[32];
	gint x;
	gint y;

	memset (px, 0xEE, sizeof (px));
	for (y = 0; y < 2; y++)
	{
		for (x = 0; x < 3; x++)
		{
			guint8 *p = px + y * 16 + x * 4;

			p[0] = (guint8) (x * 100);
			p[1] = (guint8) (y * 200);
			p[2] = 7;
			p[3] = 255;
		}
	}
	g_assert_true (lrg_screenshot_write_png (path, px, 3, 2, 16, &error));
	g_assert_no_error (error);

	back = grl_image_new_from_file (path);
	g_assert_nonnull (back);
	g_assert_cmpint (grl_image_get_width (back), ==, 3);
	g_assert_cmpint (grl_image_get_height (back), ==, 2);
	assert_pixel (back, 2, 0, 200, 0, 7);
	assert_pixel (back, 1, 1, 100, 200, 7);
	g_unlink (path);

	g_assert_false (lrg_screenshot_write_png (path, px, 3, 2, 8, &error));
	g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_INVALID_FORMAT);
	g_clear_error (&error);
	g_assert_false (lrg_screenshot_write_png ("/nonexistent-dir/x.png", px, 3, 2, 16, &error));
	g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_SAVE);
}

typedef struct
{
	guint  calls;
	guint  last_frame;
	gfloat delta;
} DrawLog;

/* red square top-left, blue bottom-right, green background on the
 * last of three frames only */
static void
draw_scene (guint    frame,
            gfloat   delta,
            gpointer user_data)
{
	DrawLog *log = user_data;

	log->calls++;
	log->last_frame = frame;
	log->delta = delta;
	if (frame == 2)
		ClearBackground ((Color) { 0, 255, 0, 255 });
	DrawRectangle (0, 0, 8, 8, (Color) { 255, 0, 0, 255 });
	DrawRectangle (56, 40, 8, 8, (Color) { 0, 0, 255, 255 });
}

static void
test_after_frames (void)
{
	g_autofree gchar *path = temp_png ("lrg-test-after-frames.png");
	g_autoptr(GrlImage) shot = NULL;
	g_autoptr(GError) error = NULL;
	DrawLog log = { 0, 0, 0.0f };

	if (!have_display ())
	{
		/* refused cleanly rather than aborting inside GLFW */
		g_assert_false (lrg_screenshot_after_frames (path, 3, 64, 48, draw_scene, &log, &error));
		g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_CAPTURE);
		g_assert_cmpuint (log.calls, ==, 0);
		g_test_skip ("no display: run under build-aux/xrun.sh for the GPU part");
		return;
	}

	g_assert_false (IsWindowReady ());
	if (!lrg_screenshot_after_frames (path, 3, 64, 48, draw_scene, &log, &error))
	{
		g_test_skip (error->message);
		return;
	}
	/* the hidden window it opened is closed again */
	g_assert_false (IsWindowReady ());
	g_assert_cmpuint (log.calls, ==, 3);
	g_assert_cmpuint (log.last_frame, ==, 2);
	g_assert_cmpfloat_with_epsilon (log.delta, 1.0f / 60.0f, 1e-6f);

	shot = grl_image_new_from_file (path);
	g_assert_nonnull (shot);
	g_assert_cmpint (grl_image_get_width (shot), ==, 64);
	g_assert_cmpint (grl_image_get_height (shot), ==, 48);
	assert_pixel (shot, 2, 2, 255, 0, 0);       /* top-left stays top-left */
	assert_pixel (shot, 60, 44, 0, 0, 255);
	assert_pixel (shot, 30, 20, 0, 255, 0);     /* the last frame is saved */
	g_unlink (path);
}

static void
test_export_render_texture (void)
{
	g_autoptr(GrlWindow) window = NULL;
	g_autoptr(LrgFrameExport) export = NULL;
	g_autoptr(LrgFrameReceiver) receiver = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 *px;
	gint fds[2];
	guint w;
	guint h;

	if (!have_display ())
	{
		g_autoptr(LrgFrameExport) headless = NULL;

		g_assert_cmpint (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), ==, 0);
		headless = lrg_frame_export_new (fds[0], 8, 8, TRUE);
		g_assert_false (lrg_frame_export_begin (headless));
		g_assert_false (lrg_frame_export_end (headless, &error));
		g_assert_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_INVALID);
		close (fds[1]);
		g_test_skip ("no display: run under build-aux/xrun.sh for the GPU part");
		return;
	}

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	window = grl_window_new (32, 32, "lrg-frame-export-test");
	if (window == NULL || !grl_window_is_ready (window))
	{
		g_test_skip ("could not open a window");
		return;
	}

	g_assert_cmpint (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), ==, 0);
	export = lrg_frame_export_new (fds[0], 64, 48, TRUE);
	receiver = lrg_frame_receiver_new (fds[1], TRUE);

	BeginDrawing ();
	g_assert_true (lrg_frame_export_begin (export));
	ClearBackground ((Color) { 0, 255, 0, 255 });
	DrawRectangle (0, 0, 8, 8, (Color) { 255, 0, 0, 255 });
	g_assert_true (lrg_frame_export_end (export, &error));
	g_assert_no_error (error);
	EndDrawing ();
	g_assert_nonnull (lrg_frame_export_get_texture (export));

	g_assert_true (lrg_frame_receiver_dispatch (receiver, &error));
	px = lrg_frame_receiver_get_frame (receiver, &w, &h, NULL, NULL);
	g_assert_nonnull (px);
	g_assert_cmpuint (w, ==, 64);
	g_assert_cmpuint (h, ==, 48);
	/* row 0 is the top: the red square */
	g_assert_cmpuint (px[(2 * 64 + 2) * 4 + 0], ==, 255);
	g_assert_cmpuint (px[(2 * 64 + 2) * 4 + 1], ==, 0);
	g_assert_cmpuint (px[(40 * 64 + 40) * 4 + 1], ==, 255);

	/* a resize takes effect on the next frame */
	lrg_frame_export_set_size (export, 16, 12);
	BeginDrawing ();
	g_assert_true (lrg_frame_export_begin (export));
	ClearBackground ((Color) { 0, 0, 255, 255 });
	g_assert_true (lrg_frame_export_end (export, &error));
	EndDrawing ();
	g_assert_true (lrg_frame_receiver_dispatch (receiver, &error));
	px = lrg_frame_receiver_get_frame (receiver, &w, &h, NULL, NULL);
	g_assert_cmpuint (w, ==, 16);
	g_assert_cmpuint (h, ==, 12);
	g_assert_cmpuint (px[2], ==, 255);

	/* with a window already open the helper leaves it open */
	{
		g_autofree gchar *path = temp_png ("lrg-test-after-frames-2.png");
		DrawLog log = { 0, 0, 0.0f };

		g_assert_true (lrg_screenshot_after_frames (path, 1, 16, 16, draw_scene, &log, &error));
		g_assert_no_error (error);
		g_assert_true (IsWindowReady ());
		g_unlink (path);
	}

	g_clear_object (&export);
	g_clear_object (&receiver);
	g_clear_object (&window);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/screenshot/write-png", test_write_png);
	g_test_add_func ("/screenshot/after-frames", test_after_frames);
	g_test_add_func ("/screenshot/export-render-texture", test_export_render_texture);

	return g_test_run ();
}
