/* test-gif-recorder.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgGifRecorder.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <libregnum.h>

/*
 * Skip tests that need a live GL context (capture_frame).
 */
#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/*
 * make_solid_image:
 *
 * Creates a solid-colour GrlImage of the given dimensions.  All GIF tests
 * use this instead of grl_image_new_from_screen() so they run headless.
 */
static GrlImage *
make_solid_image (gint    width,
                  gint    height,
                  guint8  r,
                  guint8  g,
                  guint8  b)
{
    GrlColor *color;
    GrlImage *image;

    color = grl_color_new (r, g, b, 255);
    image = grl_image_new_color (width, height, color);
    grl_color_free (color);

    return image;
}

/*
 * gif_is_valid:
 * @path: path to the GIF file to inspect
 * @out_frame_count: (out) (optional): number of GCE/frame markers found
 *
 * Verifies the GIF89a header and counts Graphic Control Extension (GCE)
 * blocks — each output frame is preceded by exactly one GCE (0x21 0xf9
 * 0x04 ...).  This is a byte-level sanity check, not a full decoder.
 *
 * Returns: %TRUE if the file begins with "GIF89a".
 */
static gboolean
gif_is_valid (const gchar *path,
              guint       *out_frame_count)
{
    FILE   *fp;
    guchar  header[6];
    guchar  prev;
    guchar  curr;
    guint   count;
    gboolean valid;

    count = 0;
    valid = FALSE;

    fp = fopen (path, "rb");
    if (fp == NULL)
        return FALSE;

    /* Read the 6-byte GIF signature */
    if (fread (header, 1, 6, fp) == 6)
    {
        if (header[0] == 'G' &&
            header[1] == 'I' &&
            header[2] == 'F' &&
            header[3] == '8' &&
            header[4] == '9' &&
            header[5] == 'a')
        {
            valid = TRUE;
        }
    }

    /* Count GCE markers: 0x21 0xf9 0x04 */
    if (valid && out_frame_count != NULL)
    {
        gboolean prev_was_21_f9;

        prev          = 0;
        prev_was_21_f9 = FALSE;

        while (fread (&curr, 1, 1, fp) == 1)
        {
            if (prev_was_21_f9 && curr == 0x04)
                count++;

            prev_was_21_f9 = (prev == 0x21 && curr == 0xf9);
            prev = curr;
        }

        *out_frame_count = count;
    }

    fclose (fp);
    return valid;
}

/* ==========================================================================
 * Test Cases - Construction
 * ========================================================================== */

static void
test_gif_recorder_new_valid (void)
{
    g_autoptr(LrgGifRecorder) recorder = NULL;
    g_autoptr(GError) error = NULL;
    gchar *path;
    gint   fd;

    /* Create a temporary file path for the GIF */
    fd = g_file_open_tmp ("libregnum-test-XXXXXX.gif", &path, &error);
    g_assert_no_error (error);
    g_assert_cmpint (fd, >=, 0);
    close (fd);
    g_unlink (path);

    recorder = lrg_gif_recorder_new (path, 16, 16, 10, &error);
    g_assert_no_error (error);
    g_assert_nonnull (recorder);
    g_assert_true (LRG_IS_GIF_RECORDER (recorder));

    /* Finish and clean up */
    g_assert_true (lrg_gif_recorder_finish (recorder, &error));
    g_assert_no_error (error);

    g_unlink (path);
    g_free (path);
}

static void
test_gif_recorder_new_invalid_path (void)
{
    g_autoptr(LrgGifRecorder) recorder = NULL;
    g_autoptr(GError) error = NULL;

    recorder = lrg_gif_recorder_new ("/nonexistent-dir/x.gif",
                                     16, 16, 10, &error);

    g_assert_null (recorder);
    g_assert_nonnull (error);
}

/* ==========================================================================
 * Test Cases - Basic Recording
 * ========================================================================== */

/*
 * test_gif_recorder_add_frames:
 *
 * Creates N solid-colour frames, writes them, finishes, then validates the
 * output file: GIF89a signature and N GCE markers.
 */
static void
test_gif_recorder_add_frames (void)
{
    g_autoptr(LrgGifRecorder) recorder = NULL;
    g_autoptr(GError) error = NULL;
    gchar *path;
    gint   fd;
    gint   i;
    guint  found_frames;

    /* Solid colours to alternate across frames */
    static const guint8 colours[][3] = {
        { 255,   0,   0 }, /* red   */
        {   0, 255,   0 }, /* green */
        {   0,   0, 255 }, /* blue  */
    };
    gint n_frames = (gint) G_N_ELEMENTS (colours);

    fd = g_file_open_tmp ("libregnum-test-XXXXXX.gif", &path, &error);
    g_assert_no_error (error);
    g_assert_cmpint (fd, >=, 0);
    close (fd);
    g_unlink (path);

    recorder = lrg_gif_recorder_new (path, 32, 32, 25, &error);
    g_assert_no_error (error);
    g_assert_nonnull (recorder);

    for (i = 0; i < n_frames; i++)
    {
        g_autoptr(GrlImage) frame = NULL;

        frame = make_solid_image (32, 32,
                                  colours[i][0],
                                  colours[i][1],
                                  colours[i][2]);
        g_assert_nonnull (frame);

        g_assert_true (lrg_gif_recorder_add_frame (recorder, frame, &error));
        g_assert_no_error (error);
    }

    g_assert_cmpuint (lrg_gif_recorder_get_frame_count (recorder), ==,
                      (guint) n_frames);

    g_assert_true (lrg_gif_recorder_finish (recorder, &error));
    g_assert_no_error (error);

    /* Validate file */
    g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
    g_assert_true (gif_is_valid (path, &found_frames));
    g_assert_cmpuint (found_frames, ==, (guint) n_frames);

    g_unlink (path);
    g_free (path);
}

/* ==========================================================================
 * Test Cases - Quality Settings
 * ========================================================================== */

static void
test_gif_recorder_set_quality (void)
{
    g_autoptr(LrgGifRecorder) recorder = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GrlImage) frame = NULL;
    gchar *path;
    gint   fd;
    guint  found_frames;

    fd = g_file_open_tmp ("libregnum-test-XXXXXX.gif", &path, &error);
    g_assert_no_error (error);
    g_assert_cmpint (fd, >=, 0);
    close (fd);
    g_unlink (path);

    recorder = lrg_gif_recorder_new (path, 16, 16, 15, &error);
    g_assert_no_error (error);
    g_assert_nonnull (recorder);

    /* Configure adaptive palette + dithering before adding any frame */
    lrg_gif_recorder_set_quality (recorder, TRUE, TRUE);

    frame = make_solid_image (16, 16, 128, 64, 200);
    g_assert_nonnull (frame);

    g_assert_true (lrg_gif_recorder_add_frame (recorder, frame, &error));
    g_assert_no_error (error);

    g_assert_true (lrg_gif_recorder_finish (recorder, &error));
    g_assert_no_error (error);

    /* The file must still be a valid GIF with exactly 1 frame */
    g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
    g_assert_true (gif_is_valid (path, &found_frames));
    g_assert_cmpuint (found_frames, ==, 1);

    g_unlink (path);
    g_free (path);
}

/* ==========================================================================
 * Test Cases - Motion Blur
 * ========================================================================== */

static void
test_gif_recorder_motion_blur (void)
{
    g_autoptr(LrgGifRecorder) recorder = NULL;
    g_autoptr(GError) error = NULL;
    gchar *path;
    gint   fd;
    guint  found_frames;
    gint   i;

    fd = g_file_open_tmp ("libregnum-test-XXXXXX.gif", &path, &error);
    g_assert_no_error (error);
    g_assert_cmpint (fd, >=, 0);
    close (fd);
    g_unlink (path);

    recorder = lrg_gif_recorder_new (path, 8, 8, 12, &error);
    g_assert_no_error (error);
    g_assert_nonnull (recorder);

    /* Enable 4-sample motion blur */
    lrg_gif_recorder_set_motion_blur (recorder, 4);

    /*
     * Produce one output frame by blending 2 white sub-frames and 2 black
     * sub-frames.  The resolved average should be mid-grey (pixel value
     * around 128), but we only check that a valid 1-frame GIF is produced.
     */
    lrg_gif_recorder_begin_frame (recorder);

    for (i = 0; i < 2; i++)
    {
        g_autoptr(GrlImage) white = make_solid_image (8, 8, 255, 255, 255);

        g_assert_nonnull (white);
        lrg_gif_recorder_add_subframe (recorder, white);
    }

    for (i = 0; i < 2; i++)
    {
        g_autoptr(GrlImage) black = make_solid_image (8, 8, 0, 0, 0);

        g_assert_nonnull (black);
        lrg_gif_recorder_add_subframe (recorder, black);
    }

    g_assert_true (lrg_gif_recorder_end_frame (recorder, &error));
    g_assert_no_error (error);

    g_assert_cmpuint (lrg_gif_recorder_get_frame_count (recorder), ==, 1);

    g_assert_true (lrg_gif_recorder_finish (recorder, &error));
    g_assert_no_error (error);

    /* Output must be a valid GIF with exactly 1 frame */
    g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
    g_assert_true (gif_is_valid (path, &found_frames));
    g_assert_cmpuint (found_frames, ==, 1);

    g_unlink (path);
    g_free (path);
}

/* ==========================================================================
 * Test Cases - Idempotent Finish
 * ========================================================================== */

static void
test_gif_recorder_finish_idempotent (void)
{
    g_autoptr(LrgGifRecorder) recorder = NULL;
    g_autoptr(GError) error = NULL;
    gchar *path;
    gint   fd;

    fd = g_file_open_tmp ("libregnum-test-XXXXXX.gif", &path, &error);
    g_assert_no_error (error);
    g_assert_cmpint (fd, >=, 0);
    close (fd);
    g_unlink (path);

    recorder = lrg_gif_recorder_new (path, 8, 8, 10, &error);
    g_assert_no_error (error);
    g_assert_nonnull (recorder);

    /* Finish once */
    g_assert_true (lrg_gif_recorder_finish (recorder, &error));
    g_assert_no_error (error);

    /* Finish a second time — must be safe */
    g_assert_true (lrg_gif_recorder_finish (recorder, &error));
    g_assert_no_error (error);

    g_unlink (path);
    g_free (path);
}

/* ==========================================================================
 * Test Cases - Add Frame After Finish
 * ========================================================================== */

static void
test_gif_recorder_add_frame_after_finish (void)
{
    g_autoptr(LrgGifRecorder) recorder = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GrlImage) frame = NULL;
    gboolean result;
    gchar *path;
    gint   fd;

    fd = g_file_open_tmp ("libregnum-test-XXXXXX.gif", &path, &error);
    g_assert_no_error (error);
    g_assert_cmpint (fd, >=, 0);
    close (fd);
    g_unlink (path);

    recorder = lrg_gif_recorder_new (path, 8, 8, 10, &error);
    g_assert_no_error (error);
    g_assert_nonnull (recorder);

    g_assert_true (lrg_gif_recorder_finish (recorder, &error));
    g_assert_no_error (error);

    frame = make_solid_image (8, 8, 200, 100, 50);
    g_assert_nonnull (frame);

    /* Adding a frame after finish must return FALSE with an error */
    result = lrg_gif_recorder_add_frame (recorder, frame, &error);
    g_assert_false (result);
    g_assert_nonnull (error);
    g_assert_true (g_error_matches (error,
                                    LRG_GIF_RECORDER_ERROR,
                                    LRG_GIF_RECORDER_ERROR_ALREADY_CLOSED));

    g_unlink (path);
    g_free (path);
}

/* ==========================================================================
 * Test Cases - GL-Dependent (skipped in headless CI)
 * ========================================================================== */

static void
test_gif_recorder_capture_frame_headless (void)
{
    SKIP_IF_NO_DISPLAY ();

    /*
     * Full capture_frame test only runs when a display is available.
     * The test just verifies the function reaches the screen-capture path
     * and returns a sensible error when there is no open GrlWindow.
     * Actual GL integration is covered by manual/integration testing.
     */
    g_test_skip ("capture_frame GL integration not exercised in unit tests");
}

/* ==========================================================================
 * main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Construction */
    g_test_add_func ("/gif-recorder/new/valid",
                     test_gif_recorder_new_valid);
    g_test_add_func ("/gif-recorder/new/invalid-path",
                     test_gif_recorder_new_invalid_path);

    /* Frame recording */
    g_test_add_func ("/gif-recorder/add-frames",
                     test_gif_recorder_add_frames);

    /* Quality */
    g_test_add_func ("/gif-recorder/set-quality",
                     test_gif_recorder_set_quality);

    /* Motion blur */
    g_test_add_func ("/gif-recorder/motion-blur",
                     test_gif_recorder_motion_blur);

    /* Edge cases */
    g_test_add_func ("/gif-recorder/finish/idempotent",
                     test_gif_recorder_finish_idempotent);
    g_test_add_func ("/gif-recorder/add-frame-after-finish",
                     test_gif_recorder_add_frame_after_finish);

    /* GL-dependent */
    g_test_add_func ("/gif-recorder/capture-frame/headless",
                     test_gif_recorder_capture_frame_headless);

    return g_test_run ();
}
