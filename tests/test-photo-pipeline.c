/* test-photo-pipeline.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Display-free tests of the photo mode pipeline: the look, the depth of
 * field, the overlays, PNG metadata, the orbit camera and its limits, and
 * the photo mode state machine (world contract, capture, timer) against
 * a mock host.  Nothing here needs a window.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define NEAR_F(a, b, eps) g_assert_cmpfloat_with_epsilon ((a), (b), (eps))

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* a w x h image filled with one colour */
static GrlImage *
make_image (gint    w,
            gint    h,
            guint8  r,
            guint8  g,
            guint8  b)
{
    g_autoptr(GrlColor) color = grl_color_new (r, g, b, 255);
    GrlImage *image = grl_image_new_color (w, h, color);

    g_assert_nonnull (image);
    return image;
}

static void
get_px (GrlImage *image,
        gint      x,
        gint      y,
        guint8   *rgb)
{
    gsize size;
    guint8 *pixels = grl_image_get_pixels (image, &size);
    gint w = grl_image_get_width (image);

    rgb[0] = pixels[((gsize)y * (gsize)w + (gsize)x) * 4 + 0];
    rgb[1] = pixels[((gsize)y * (gsize)w + (gsize)x) * 4 + 1];
    rgb[2] = pixels[((gsize)y * (gsize)w + (gsize)x) * 4 + 2];
}

static void
set_px (GrlImage *image,
        gint      x,
        gint      y,
        guint8    v)
{
    gsize size;
    guint8 *pixels = grl_image_get_pixels (image, &size);
    gint w = grl_image_get_width (image);

    pixels[((gsize)y * (gsize)w + (gsize)x) * 4 + 0] = v;
    pixels[((gsize)y * (gsize)w + (gsize)x) * 4 + 1] = v;
    pixels[((gsize)y * (gsize)w + (gsize)x) * 4 + 2] = v;
}

static gchar *
make_temp_dir (void)
{
    GError *error = NULL;
    gchar *dir = g_dir_make_tmp ("lrg-photo-XXXXXX", &error);

    g_assert_no_error (error);
    return dir;
}

/* removes a directory of files made by a test */
static void
remove_dir (const gchar *dir)
{
    GDir *handle = g_dir_open (dir, 0, NULL);
    const gchar *name;

    if (handle != NULL)
    {
        while ((name = g_dir_read_name (handle)) != NULL)
        {
            g_autofree gchar *path = g_build_filename (dir, name, NULL);

            g_remove (path);
        }
        g_dir_close (handle);
    }
    g_rmdir (dir);
}

/* ==========================================================================
 * Enums
 * ========================================================================== */

static void
test_enums (void)
{
    GEnumClass *klass;

    klass = g_type_class_ref (LRG_TYPE_PHOTO_PRESET);
    g_assert_cmpint (klass->n_values, ==, LRG_PHOTO_PRESET_COUNT);
    g_assert_cmpstr (g_enum_get_value (klass, LRG_PHOTO_PRESET_NOIR)->value_nick, ==, "noir");
    g_type_class_unref (klass);

    klass = g_type_class_ref (LRG_TYPE_PHOTO_FRAME);
    g_assert_cmpstr (g_enum_get_value (klass, LRG_PHOTO_FRAME_POLAROID)->value_nick, ==, "polaroid");
    g_type_class_unref (klass);

    klass = g_type_class_ref (LRG_TYPE_PHOTO_GRID);
    g_assert_cmpstr (g_enum_get_value (klass, LRG_PHOTO_GRID_GOLDEN)->value_nick, ==, "golden");
    g_type_class_unref (klass);

    klass = g_type_class_ref (LRG_TYPE_PHOTO_CAMERA_MODE);
    g_assert_cmpstr (g_enum_get_value (klass, LRG_PHOTO_CAMERA_MODE_ORBIT)->value_nick, ==, "orbit");
    g_type_class_unref (klass);

    g_assert_cmpstr (lrg_photo_preset_get_label (LRG_PHOTO_PRESET_SEPIA), ==, "Sepia");
    g_assert_cmpstr (lrg_photo_frame_get_label (LRG_PHOTO_FRAME_CINEMA), ==, "Cinema bars");
    g_assert_cmpstr (lrg_photo_grid_get_label (LRG_PHOTO_GRID_THIRDS), ==, "Rule of thirds");
}

/* ==========================================================================
 * LrgPhotoLook
 * ========================================================================== */

static void
test_look_defaults_and_clamping (void)
{
    g_autoptr(LrgPhotoLook) look = lrg_photo_look_new ();

    g_assert_true (lrg_photo_look_is_identity (look));
    g_assert_false (lrg_photo_look_has_finish (look));
    g_assert_cmpint (lrg_photo_look_get_preset (look), ==, LRG_PHOTO_PRESET_NONE);

    /* out of range values are clamped; garbage becomes the lower bound */
    lrg_photo_look_set_exposure (look, 99.0f);
    NEAR_F (lrg_photo_look_get_exposure (look), 3.0f, 1e-6);
    lrg_photo_look_set_contrast (look, -5.0f);
    NEAR_F (lrg_photo_look_get_contrast (look), 0.25f, 1e-6);
    lrg_photo_look_set_vignette (look, NAN);
    NEAR_F (lrg_photo_look_get_vignette (look), 0.0f, 1e-6);
    lrg_photo_look_set_grain (look, 2.0f);
    NEAR_F (lrg_photo_look_get_grain (look), 1.0f, 1e-6);
    g_assert_false (lrg_photo_look_is_identity (look));
    g_assert_true (lrg_photo_look_has_finish (look));

    lrg_photo_look_reset (look);
    g_assert_true (lrg_photo_look_is_identity (look));
}

static void
test_look_presets (void)
{
    g_autoptr(LrgPhotoLook) look = lrg_photo_look_new_for_preset (LRG_PHOTO_PRESET_NOIR);
    g_autoptr(LrgColorLut) lut = lrg_color_lut_new_identity (4);
    gint i;

    g_assert_cmpint (lrg_photo_look_get_preset (look), ==, LRG_PHOTO_PRESET_NOIR);
    NEAR_F (lrg_photo_look_get_saturation (look), 0.0f, 1e-6);
    g_assert_cmpfloat (lrg_photo_look_get_contrast (look), >, 1.0f);
    g_assert_true (lrg_photo_look_has_finish (look));

    /* every preset applies; only NONE is the identity */
    lrg_photo_look_set_lut (look, lut);
    for (i = 0; i < LRG_PHOTO_PRESET_COUNT; i++)
    {
        lrg_photo_look_apply_preset (look, (LrgPhotoPreset)i);
        g_assert_cmpint (lrg_photo_look_get_preset (look), ==, i);
        /* the table survives a preset */
        g_assert_true (lrg_photo_look_get_lut (look) == lut);
    }
    lrg_photo_look_apply_preset (look, LRG_PHOTO_PRESET_NONE);
    lrg_photo_look_set_lut (look, NULL);
    g_assert_true (lrg_photo_look_is_identity (look));

    /* the preset property applies the preset too */
    g_object_set (look, "preset", LRG_PHOTO_PRESET_VIVID, NULL);
    g_assert_cmpfloat (lrg_photo_look_get_saturation (look), >, 1.2f);
}

static void
test_look_grade_matches_color_grade (void)
{
    g_autoptr(LrgPhotoLook) look = lrg_photo_look_new ();
    g_autoptr(LrgColorGrade) grade = lrg_color_grade_new ();
    gfloat in[3] = { 0.8f, 0.4f, 0.2f };
    gfloat a[3];
    gfloat b[3];
    gint i;

    lrg_photo_look_set_exposure (look, 0.5f);
    lrg_photo_look_set_contrast (look, 1.3f);
    lrg_photo_look_set_saturation (look, 0.7f);
    lrg_photo_look_set_temperature (look, 0.4f);
    lrg_photo_look_set_lift (look, 0.05f);
    lrg_photo_look_set_gamma (look, 1.2f);

    lrg_photo_look_apply_rgb (look, in, a);
    lrg_photo_look_configure_grade (look, grade);
    lrg_color_grade_apply_rgb (grade, in, b);
    for (i = 0; i < 3; i++)
        NEAR_F (a[i], b[i], 1e-6);
    /* and it did something */
    g_assert_cmpfloat (fabsf (a[0] - in[0]) + fabsf (a[1] - in[1]) + fabsf (a[2] - in[2]), >, 0.05f);
}

static void
test_look_vignette (void)
{
    g_autoptr(LrgPhotoLook) look = lrg_photo_look_new ();
    gfloat prev;
    gfloat u;

    /* off: no change anywhere */
    NEAR_F (lrg_photo_look_get_vignette_factor (look, 0.0f, 0.0f, 1.5f), 1.0f, 1e-6);

    lrg_photo_look_set_vignette (look, 0.6f);
    lrg_photo_look_set_vignette_radius (look, 0.4f);
    /* the middle is untouched, the corner takes the full strength */
    NEAR_F (lrg_photo_look_get_vignette_factor (look, 0.5f, 0.5f, 1.5f), 1.0f, 1e-6);
    NEAR_F (lrg_photo_look_get_vignette_factor (look, 0.0f, 0.0f, 1.5f), 0.4f, 1e-5);
    NEAR_F (lrg_photo_look_get_vignette_factor (look, 1.0f, 1.0f, 1.5f), 0.4f, 1e-5);

    /* it darkens monotonically towards the corner along the diagonal */
    prev = 1.0f;
    for (u = 0.5f; u >= 0.0f; u -= 0.05f)
    {
        gfloat f = lrg_photo_look_get_vignette_factor (look, u, u, 1.5f);

        g_assert_cmpfloat (f, <=, prev + 1e-6f);
        prev = f;
    }
    /* a nonsense aspect does not blow up */
    g_assert_true (isfinite (lrg_photo_look_get_vignette_factor (look, 0.1f, 0.1f, 0.0f)));
}

static void
test_look_grain (void)
{
    g_autoptr(LrgPhotoLook) look = lrg_photo_look_new ();
    gdouble sum = 0.0;
    gfloat lo = 1.0f;
    gfloat hi = -1.0f;
    gint x;
    gint y;
    gboolean differs = FALSE;

    NEAR_F (lrg_photo_look_get_grain_value (look, 3, 4, 1), 0.0f, 1e-9);

    lrg_photo_look_set_grain (look, 1.0f);
    for (y = 0; y < 64; y++)
    {
        for (x = 0; x < 64; x++)
        {
            gfloat v = lrg_photo_look_get_grain_value (look, x, y, 7);

            sum += v;
            lo = MIN (lo, v);
            hi = MAX (hi, v);
            /* the same arguments give the same grain */
            g_assert_true (v == lrg_photo_look_get_grain_value (look, x, y, 7));
            if (v != lrg_photo_look_get_grain_value (look, x, y, 8))
                differs = TRUE;
        }
    }
    /* bounded by plus and minus 0.2, centred on zero, new seed new noise */
    g_assert_cmpfloat (lo, >=, -0.2001f);
    g_assert_cmpfloat (hi, <=, 0.2001f);
    g_assert_cmpfloat (fabs (sum / 4096.0), <, 0.02);
    g_assert_cmpfloat (hi - lo, >, 0.3f);
    g_assert_true (differs);
}

static void
test_look_apply_image (void)
{
    g_autoptr(LrgPhotoLook) look = lrg_photo_look_new ();
    g_autoptr(GrlImage) image = make_image (32, 16, 200, 100, 50);
    g_autoptr(GrlImage) before = NULL;
    guint8 px[3];
    guint8 corner[3];
    guint8 mid[3];

    /* the identity changes nothing, even with a seed */
    before = grl_image_copy (image);
    lrg_photo_look_apply_image (look, image, 5);
    get_px (image, 3, 3, px);
    g_assert_cmpint (px[0], ==, 200);
    g_assert_cmpint (px[1], ==, 100);
    g_assert_cmpint (px[2], ==, 50);

    /* noir: monochrome */
    lrg_photo_look_apply_preset (look, LRG_PHOTO_PRESET_NOIR);
    lrg_photo_look_set_grain (look, 0.0f);
    lrg_photo_look_set_vignette (look, 0.0f);
    lrg_photo_look_apply_image (look, image, 5);
    get_px (image, 8, 8, px);
    g_assert_cmpint (px[0], ==, px[1]);
    g_assert_cmpint (px[1], ==, px[2]);

    /* a vignette on a flat grey picture: dark corners, untouched middle */
    g_clear_object (&image);
    image = make_image (64, 32, 128, 128, 128);
    lrg_photo_look_reset (look);
    lrg_photo_look_set_vignette (look, 0.8f);
    lrg_photo_look_set_vignette_radius (look, 0.3f);
    lrg_photo_look_apply_image (look, image, 0);
    get_px (image, 0, 0, corner);
    get_px (image, 32, 16, mid);
    g_assert_cmpint (mid[0], ==, 128);
    g_assert_cmpint (corner[0], <, 60);

    /* exposure up brightens */
    g_clear_object (&image);
    image = make_image (4, 4, 64, 64, 64);
    lrg_photo_look_reset (look);
    lrg_photo_look_set_exposure (look, 1.0f);
    lrg_photo_look_apply_image (look, image, 0);
    get_px (image, 1, 1, px);
    g_assert_cmpint (px[0], >, 120);
}

static void
test_look_lut (void)
{
    g_autoptr(LrgPhotoLook) look = lrg_photo_look_new ();
    g_autoptr(LrgColorLut) lut = lrg_color_lut_new_identity (5);
    g_autoptr(LrgColorGrade) grade = lrg_color_grade_new ();

    g_assert_null (lrg_photo_look_get_lut (look));
    lrg_photo_look_set_lut (look, lut);
    g_assert_true (lrg_photo_look_get_lut (look) == lut);
    /* a table makes the look non-identity until its strength is 0 */
    g_assert_false (lrg_photo_look_is_identity (look));
    lrg_photo_look_set_lut_strength (look, 0.0f);
    g_assert_true (lrg_photo_look_is_identity (look));
    lrg_photo_look_set_lut_strength (look, 1.0f);
    lrg_photo_look_configure_grade (look, grade);
    g_assert_true (lrg_color_grade_get_lut (grade, NULL) == lut);
    lrg_photo_look_set_lut (look, NULL);
    g_assert_null (lrg_photo_look_get_lut (look));
}

static void
test_look_no_window (void)
{
    g_autoptr(LrgPhotoLook) look = lrg_photo_look_new ();
    g_autoptr(GrlTexture) unused = NULL;

    /* without a window the shader cannot compile; releasing is harmless */
    lrg_photo_look_set_vignette (look, 0.5f);
    lrg_photo_look_release_gpu (look);
    lrg_photo_look_release_gpu (look);
}

/* ==========================================================================
 * LrgPhotoDof
 * ========================================================================== */

static void
test_dof_defaults_and_clamping (void)
{
    g_autoptr(LrgPhotoDof) dof = lrg_photo_dof_new ();

    g_assert_false (lrg_photo_dof_get_enabled (dof));
    NEAR_F (lrg_photo_dof_get_aperture (dof), 4.0f, 1e-6);
    lrg_photo_dof_set_aperture (dof, 0.1f);
    NEAR_F (lrg_photo_dof_get_aperture (dof), 1.0f, 1e-6);
    lrg_photo_dof_set_aperture (dof, 500.0f);
    NEAR_F (lrg_photo_dof_get_aperture (dof), 32.0f, 1e-6);
    lrg_photo_dof_set_focus_distance (dof, -3.0f);
    NEAR_F (lrg_photo_dof_get_focus_distance (dof), 0.1f, 1e-6);
    lrg_photo_dof_set_fov (dof, 500.0f);
    NEAR_F (lrg_photo_dof_get_fov (dof), 150.0f, 1e-6);
    /* a 24 mm sensor at 90 degrees is a 12 mm lens */
    lrg_photo_dof_set_fov (dof, 90.0f);
    NEAR_F (lrg_photo_dof_get_focal_length (dof), 12.0f, 1e-3);
}

static void
test_dof_coc (void)
{
    g_autoptr(LrgPhotoDof) dof = lrg_photo_dof_new ();
    gfloat at_focus;
    gfloat near_blur;
    gfloat far_blur;
    gfloat wide;
    gfloat narrow;

    lrg_photo_dof_set_enabled (dof, TRUE);
    lrg_photo_dof_set_focus_distance (dof, 10.0f);
    lrg_photo_dof_set_aperture (dof, 2.0f);

    /* nothing blurs at the focus distance, both sides blur, further more */
    at_focus = lrg_photo_dof_get_coc (dof, 10.0f, 1080);
    near_blur = lrg_photo_dof_get_coc (dof, 5.0f, 1080);
    far_blur = lrg_photo_dof_get_coc (dof, 40.0f, 1080);
    NEAR_F (at_focus, 0.0f, 1e-6);
    g_assert_cmpfloat (near_blur, >, 0.5f);
    g_assert_cmpfloat (far_blur, >, near_blur * 0.5f);
    g_assert_cmpfloat (lrg_photo_dof_get_coc (dof, 20.0f, 1080), >,
                       lrg_photo_dof_get_coc (dof, 12.0f, 1080));

    /* a wider aperture (smaller f-number) blurs more */
    lrg_photo_dof_set_aperture (dof, 1.4f);
    wide = lrg_photo_dof_get_coc (dof, 30.0f, 1080);
    lrg_photo_dof_set_aperture (dof, 16.0f);
    narrow = lrg_photo_dof_get_coc (dof, 30.0f, 1080);
    g_assert_cmpfloat (wide, >, narrow);

    /* the blur is limited, and scales with the picture's height */
    lrg_photo_dof_set_aperture (dof, 1.0f);
    lrg_photo_dof_set_strength (dof, 64.0f);
    lrg_photo_dof_set_max_blur (dof, 10.0f);
    NEAR_F (lrg_photo_dof_get_coc (dof, 500.0f, 1080), 10.0f, 1e-4);
    NEAR_F (lrg_photo_dof_get_coc (dof, 500.0f, 2160), 20.0f, 1e-3);

    /* a telephoto (narrow view) blurs more than a wide angle */
    lrg_photo_dof_set_strength (dof, 8.0f);
    lrg_photo_dof_set_max_blur (dof, 64.0f);
    lrg_photo_dof_set_aperture (dof, 4.0f);
    lrg_photo_dof_set_fov (dof, 90.0f);
    wide = lrg_photo_dof_get_coc (dof, 30.0f, 1080);
    lrg_photo_dof_set_fov (dof, 20.0f);
    narrow = lrg_photo_dof_get_coc (dof, 30.0f, 1080);
    g_assert_cmpfloat (narrow, >, wide);

    /* bad distances do not blow up */
    g_assert_true (isfinite (lrg_photo_dof_get_coc (dof, 0.0f, 1080)));
    g_assert_true (isfinite (lrg_photo_dof_get_coc (dof, -4.0f, 1080)));
}

/* the horizontal contrast of a checker of 2 pixel columns */
static gdouble
column_contrast (GrlImage *image)
{
    gint w = grl_image_get_width (image);
    gint h = grl_image_get_height (image);
    gdouble total = 0.0;
    gint x;
    gint y;

    for (y = 0; y < h; y++)
    {
        for (x = 1; x < w; x++)
        {
            guint8 a[3];
            guint8 b[3];

            get_px (image, x - 1, y, a);
            get_px (image, x, y, b);
            total += fabs ((gdouble)a[0] - (gdouble)b[0]);
        }
    }
    return total / (gdouble)(w * h);
}

/* the blur limit scales with the picture's height, so these pictures are
 * tall enough (540 pixels: a 8 pixel radius at max-blur 16) to blur visibly */
#define DOF_W (96)
#define DOF_H (540)

static void
test_dof_apply_image (void)
{
    g_autoptr(LrgPhotoDof) dof = lrg_photo_dof_new ();
    g_autoptr(GrlImage) sharp = make_image (DOF_W, DOF_H, 0, 0, 0);
    g_autoptr(GrlImage) blurred = NULL;
    g_autoptr(GrlImage) off = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *far_plane = g_new (gfloat, DOF_W * DOF_H);
    g_autofree gfloat *focus_plane = g_new (gfloat, DOF_W * DOF_H);
    gdouble before;
    gint x;
    gint y;
    gint i;

    /* a striped picture */
    for (y = 0; y < DOF_H; y++)
        for (x = 0; x < DOF_W; x++)
            set_px (sharp, x, y, (x / 2) % 2 ? 255 : 0);
    before = column_contrast (sharp);
    for (i = 0; i < DOF_W * DOF_H; i++)
    {
        far_plane[i] = 80.0f;
        focus_plane[i] = 10.0f;
    }
    lrg_photo_dof_set_focus_distance (dof, 10.0f);
    lrg_photo_dof_set_aperture (dof, 1.4f);
    lrg_photo_dof_set_strength (dof, 16.0f);
    lrg_photo_dof_set_max_blur (dof, 16.0f);

    /* off: untouched, whatever the depth */
    off = grl_image_copy (sharp);
    g_assert_true (lrg_photo_dof_apply_image (dof, off, far_plane, DOF_W * DOF_H, &error));
    g_assert_no_error (error);
    NEAR_F (column_contrast (off), before, 1e-9);

    /* on, in focus: untouched */
    lrg_photo_dof_set_enabled (dof, TRUE);
    g_clear_object (&off);
    off = grl_image_copy (sharp);
    g_assert_true (lrg_photo_dof_apply_image (dof, off, focus_plane, DOF_W * DOF_H, &error));
    NEAR_F (column_contrast (off), before, 1e-9);

    /* on, far from the focus: the stripes wash out */
    blurred = grl_image_copy (sharp);
    g_assert_true (lrg_photo_dof_apply_image (dof, blurred, far_plane, DOF_W * DOF_H, &error));
    g_assert_no_error (error);
    g_assert_cmpfloat (column_contrast (blurred), <, before * 0.5);

    /* too few distances is an error and leaves the picture alone */
    g_clear_object (&off);
    off = grl_image_copy (sharp);
    g_assert_false (lrg_photo_dof_apply_image (dof, off, far_plane, 10, &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    NEAR_F (column_contrast (off), before, 1e-9);
}

static void
test_dof_sharp_subject_in_blurred_scene (void)
{
    g_autoptr(LrgPhotoDof) dof = lrg_photo_dof_new ();
    g_autoptr(GrlImage) image = make_image (DOF_W, DOF_H, 0, 0, 0);
    g_autofree gfloat *depth = g_new (gfloat, DOF_W * DOF_H);
    g_autoptr(GError) error = NULL;
    guint8 px[3];
    gint x;
    gint y;

    /* a white square (the subject, in focus) on a black far background */
    for (y = 0; y < DOF_H; y++)
    {
        for (x = 0; x < DOF_W; x++)
        {
            gboolean subject = x >= 30 && x < 66 && y >= 250 && y < 290;

            set_px (image, x, y, subject ? 255 : 0);
            depth[y * DOF_W + x] = subject ? 10.0f : 100.0f;
        }
    }
    lrg_photo_dof_set_enabled (dof, TRUE);
    lrg_photo_dof_set_focus_distance (dof, 10.0f);
    lrg_photo_dof_set_aperture (dof, 1.4f);
    lrg_photo_dof_set_strength (dof, 16.0f);
    lrg_photo_dof_set_max_blur (dof, 16.0f);
    g_assert_true (lrg_photo_dof_apply_image (dof, image, depth, DOF_W * DOF_H, &error));
    g_assert_no_error (error);

    /* the subject's middle stays white: it is sharp and does not take the
     * background's dark in */
    get_px (image, 48, 270, px);
    g_assert_cmpint (px[0], ==, 255);
    /* just outside the subject the blurred background is only a little
     * lit (the sharp white bleeds in only a little), far away it is black */
    get_px (image, 4, 4, px);
    g_assert_cmpint (px[0], ==, 0);
    get_px (image, 28, 270, px);
    g_assert_cmpint (px[0], <, 200);
}

/* ==========================================================================
 * Overlays
 * ========================================================================== */

static void
test_overlay_borders (void)
{
    gint l;
    gint t;
    gint r;
    gint b;
    GrlRectangle rect;

    lrg_photo_overlay_get_frame_borders (LRG_PHOTO_FRAME_NONE, 1920, 1080, &l, &t, &r, &b);
    g_assert_true (l == 0 && t == 0 && r == 0 && b == 0);

    lrg_photo_overlay_get_frame_borders (LRG_PHOTO_FRAME_THIN, 1920, 1080, &l, &t, &r, &b);
    g_assert_cmpint (l, ==, 13);
    g_assert_true (l == t && t == r && r == b);

    lrg_photo_overlay_get_frame_borders (LRG_PHOTO_FRAME_WIDE, 1920, 1080, &l, &t, &r, &b);
    g_assert_cmpint (l, ==, 54);

    /* the polaroid's thick edge is the bottom */
    lrg_photo_overlay_get_frame_borders (LRG_PHOTO_FRAME_POLAROID, 1000, 1000, &l, &t, &r, &b);
    g_assert_cmpint (l, ==, 50);
    g_assert_cmpint (b, ==, 160);

    /* cinema: 1920 / 2.39 = 803 tall, bars above and below */
    lrg_photo_overlay_get_frame_borders (LRG_PHOTO_FRAME_CINEMA, 1920, 1080, &l, &t, &r, &b);
    g_assert_cmpint (l, ==, 0);
    g_assert_cmpint (t, ==, (1080 - 803) / 2);
    g_assert_cmpint (t, ==, b);
    /* wider than 2.39: bars at the sides */
    lrg_photo_overlay_get_frame_borders (LRG_PHOTO_FRAME_CINEMA, 3000, 1000, &l, &t, &r, &b);
    g_assert_cmpint (t, ==, 0);
    g_assert_cmpint (l, ==, (3000 - 2390) / 2);

    lrg_photo_overlay_get_picture_rect (LRG_PHOTO_FRAME_POLAROID, 1000, 1000, &rect);
    NEAR_F (rect.x, 50.0f, 1e-6);
    NEAR_F (rect.width, 900.0f, 1e-6);
    NEAR_F (rect.height, 1000.0f - 50.0f - 160.0f, 1e-6);

    /* tiny pictures never lose everything */
    lrg_photo_overlay_get_frame_borders (LRG_PHOTO_FRAME_WIDE, 3, 3, &l, &t, &r, &b);
    g_assert_cmpint (l + r, <, 3);
    g_assert_cmpint (t + b, <, 3);
}

static void
test_overlay_grid (void)
{
    gfloat lines[LRG_PHOTO_OVERLAY_MAX_GRID_LINES * 4];
    guint n;

    g_assert_cmpuint (lrg_photo_overlay_get_grid_lines (LRG_PHOTO_GRID_NONE, 0, 0, 100, 60, lines,
                                                         LRG_PHOTO_OVERLAY_MAX_GRID_LINES), ==, 0);

    /* thirds: two vertical at 1/3 and 2/3, two horizontal */
    n = lrg_photo_overlay_get_grid_lines (LRG_PHOTO_GRID_THIRDS, 10, 20, 90, 60, lines,
                                          LRG_PHOTO_OVERLAY_MAX_GRID_LINES);
    g_assert_cmpuint (n, ==, 4);
    NEAR_F (lines[0], 40.0f, 1e-4);     /* x1 of the first vertical */
    NEAR_F (lines[1], 20.0f, 1e-4);     /* from the top ... */
    NEAR_F (lines[3], 80.0f, 1e-4);     /* ... to the bottom */
    NEAR_F (lines[4], 70.0f, 1e-4);     /* the second vertical */
    NEAR_F (lines[9], 40.0f, 1e-4);     /* the first horizontal at 1/3 of 60 */

    n = lrg_photo_overlay_get_grid_lines (LRG_PHOTO_GRID_GOLDEN, 0, 0, 1000, 1000, lines,
                                          LRG_PHOTO_OVERLAY_MAX_GRID_LINES);
    g_assert_cmpuint (n, ==, 4);
    NEAR_F (lines[0], 381.966f, 0.01);
    NEAR_F (lines[4], 618.034f, 0.01);

    g_assert_cmpuint (lrg_photo_overlay_get_grid_lines (LRG_PHOTO_GRID_CENTER, 0, 0, 100, 100, lines,
                                                         LRG_PHOTO_OVERLAY_MAX_GRID_LINES), ==, 2);
    g_assert_cmpuint (lrg_photo_overlay_get_grid_lines (LRG_PHOTO_GRID_DIAGONALS, 0, 0, 100, 100, lines,
                                                         LRG_PHOTO_OVERLAY_MAX_GRID_LINES), ==, 4);

    /* drawing a grid without a window is harmless (nothing to draw into) */
    lrg_photo_overlay_draw_grid (LRG_PHOTO_GRID_NONE, 100, 100, NULL);
}

static void
test_overlay_apply_frame (void)
{
    g_autoptr(GrlImage) image = make_image (100, 100, 10, 20, 30);
    guint8 px[3];

    lrg_photo_overlay_apply_frame (LRG_PHOTO_FRAME_NONE, image, 255, 255, 255);
    get_px (image, 0, 0, px);
    g_assert_cmpint (px[0], ==, 10);

    lrg_photo_overlay_apply_frame (LRG_PHOTO_FRAME_POLAROID, image, 250, 240, 230);
    /* the edge and the thick bottom are the frame's colour, the middle is not */
    get_px (image, 0, 0, px);
    g_assert_cmpint (px[0], ==, 250);
    g_assert_cmpint (px[1], ==, 240);
    get_px (image, 50, 95, px);
    g_assert_cmpint (px[2], ==, 230);
    get_px (image, 50, 50, px);
    g_assert_cmpint (px[0], ==, 10);
    g_assert_cmpint (px[2], ==, 30);
    /* the polaroid's bottom border is wider than the top */
    get_px (image, 50, 10, px);
    g_assert_cmpint (px[0], ==, 10);
    get_px (image, 50, 3, px);
    g_assert_cmpint (px[0], ==, 250);
}

/* ==========================================================================
 * Screenshot metadata
 * ========================================================================== */

static void
test_screenshot_metadata_roundtrip (void)
{
    g_autoptr(GrlImage) image = make_image (24, 16, 200, 10, 10);
    g_autoptr(LrgScreenshot) shot = lrg_screenshot_new_from_image (image);
    g_autoptr(GError) error = NULL;
    g_autoptr(GHashTable) table = NULL;
    g_autoptr(GrlImage) loaded = NULL;
    g_autofree gchar *dir = make_temp_dir ();
    g_autofree gchar *path = g_build_filename (dir, "meta.png", NULL);
    g_auto(GStrv) keys = NULL;

    lrg_screenshot_set_metadata (shot, "Title", "A test photo");
    lrg_screenshot_set_metadata (shot, "Camera.Position", "1.0 2.0 3.0");
    lrg_screenshot_set_metadata (shot, "Gone", "soon");
    lrg_screenshot_set_metadata (shot, "Gone", NULL);
    lrg_screenshot_set_metadata (shot, "Title", "A better title \xc3\xa9");

    g_assert_cmpstr (lrg_screenshot_get_metadata (shot, "Title"), ==, "A better title \xc3\xa9");
    g_assert_null (lrg_screenshot_get_metadata (shot, "Gone"));
    keys = lrg_screenshot_get_metadata_keys (shot);
    g_assert_cmpuint (g_strv_length (keys), ==, 2);
    g_assert_cmpstr (keys[0], ==, "Title");
    g_assert_cmpstr (keys[1], ==, "Camera.Position");

    g_assert_true (lrg_screenshot_save_png (shot, path, &error));
    g_assert_no_error (error);

    /* the text comes back, and the picture still decodes */
    table = lrg_screenshot_read_png_metadata (path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (table);
    g_assert_cmpuint (g_hash_table_size (table), ==, 2);
    g_assert_cmpstr (g_hash_table_lookup (table, "Camera.Position"), ==, "1.0 2.0 3.0");
    g_assert_cmpstr (g_hash_table_lookup (table, "Title"), ==, "A better title \xc3\xa9");
    loaded = grl_image_new_from_file (path);
    g_assert_nonnull (loaded);
    g_assert_cmpint (grl_image_get_width (loaded), ==, 24);
    g_assert_cmpint (grl_image_get_height (loaded), ==, 16);

    remove_dir (dir);
}

static void
test_screenshot_metadata_none_and_failures (void)
{
    g_autoptr(GrlImage) image = make_image (8, 8, 1, 2, 3);
    g_autoptr(LrgScreenshot) shot = lrg_screenshot_new_from_image (image);
    g_autoptr(GError) error = NULL;
    g_autoptr(GHashTable) table = NULL;
    g_autofree gchar *dir = make_temp_dir ();
    g_autofree gchar *path = g_build_filename (dir, "plain.png", NULL);
    g_autofree gchar *bad = g_build_filename (dir, "bad.png", NULL);
    g_autofree gchar *contents = NULL;
    gsize length = 0;

    /* no metadata: a plain PNG with an empty table */
    g_assert_true (lrg_screenshot_save_png (shot, path, &error));
    table = lrg_screenshot_read_png_metadata (path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_hash_table_size (table), ==, 0);
    g_clear_pointer (&table, g_hash_table_unref);

    /* a flipped byte breaks a chunk's CRC */
    lrg_screenshot_set_metadata (shot, "Key", "value");
    g_assert_true (lrg_screenshot_save_png (shot, path, &error));
    g_assert_true (g_file_get_contents (path, &contents, &length, &error));
    contents[40] ^= 0x55;
    g_assert_true (g_file_set_contents (bad, contents, (gssize)length, &error));
    table = lrg_screenshot_read_png_metadata (bad, &error);
    g_assert_null (table);
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_INVALID_FORMAT);
    g_clear_error (&error);

    /* not a PNG, and a missing file */
    g_assert_true (g_file_set_contents (bad, "hello world, not a png", -1, &error));
    table = lrg_screenshot_read_png_metadata (bad, &error);
    g_assert_null (table);
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_INVALID_FORMAT);
    g_clear_error (&error);
    g_assert_null (lrg_screenshot_read_png_metadata ("/nonexistent/dir/x.png", &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_FAILED);
    g_clear_error (&error);

    /* saving into a missing directory fails with an error, not a crash */
    g_assert_false (lrg_screenshot_save_png (shot, "/nonexistent/dir/x.png", &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_SAVE);
    g_clear_error (&error);

    remove_dir (dir);
}

/* ==========================================================================
 * Camera controller: orbit, limits, collision, roll
 * ========================================================================== */

/* a wall: nothing may pass z = 5; the camera stops 0.5 short of it */
static gboolean
wall_collision (const gfloat *from,
                const gfloat *to,
                gfloat        radius,
                gfloat       *out,
                gpointer      user_data)
{
    gint *calls = user_data;

    (*calls)++;
    (void)from;
    if (to[2] <= 5.0f - radius)
        return FALSE;
    out[0] = to[0];
    out[1] = to[1];
    out[2] = 5.0f - radius;
    return TRUE;
}

static void
settle (LrgPhotoCameraController *c)
{
    gint i;

    for (i = 0; i < 60; i++)
        lrg_photo_camera_controller_update (c, 0.1f);
}

static void
test_camera_roll_sets_up_vector (void)
{
    g_autoptr(LrgPhotoCameraController) c = lrg_photo_camera_controller_new ();
    g_autoptr(GrlVector3) up = NULL;

    lrg_photo_camera_controller_update (c, 0.016f);
    up = lrg_camera3d_get_up (lrg_photo_camera_controller_get_camera (c));
    NEAR_F (up->y, 1.0f, 1e-4);
    g_clear_pointer (&up, grl_vector3_free);

    /* rolled a quarter turn the up vector lies on the horizon */
    lrg_photo_camera_controller_set_roll (c, 90.0f);
    lrg_photo_camera_controller_update (c, 0.016f);
    up = lrg_camera3d_get_up (lrg_photo_camera_controller_get_camera (c));
    NEAR_F (up->y, 0.0f, 1e-4);
    g_assert_cmpfloat (fabsf (up->x) + fabsf (up->z), >, 0.99f);
}

static void
test_camera_from_camera_keeps_direction (void)
{
    g_autoptr(LrgCamera3D) source = lrg_camera3d_new ();
    g_autoptr(GrlVector3) pos = grl_vector3_new (0.0f, 2.0f, 0.0f);
    g_autoptr(GrlVector3) aim = grl_vector3_new (10.0f, 2.0f, 0.0f);
    g_autoptr(LrgPhotoCameraController) c = NULL;

    lrg_camera3d_set_position (source, pos);
    lrg_camera3d_set_target (source, aim);
    c = lrg_photo_camera_controller_new_from_camera (source);
    /* looking along +x is yaw 90 degrees, level */
    NEAR_F (lrg_photo_camera_controller_get_yaw (c), 90.0f, 1e-3);
    NEAR_F (lrg_photo_camera_controller_get_pitch (c), 0.0f, 1e-3);
    /* reset goes back to that view, not to yaw 0 */
    lrg_photo_camera_controller_rotate (c, 40.0f, 10.0f);
    lrg_photo_camera_controller_reset (c);
    NEAR_F (lrg_photo_camera_controller_get_yaw (c), 90.0f, 1e-3);
}

static void
test_camera_strafe_right_is_right (void)
{
    g_autoptr(LrgPhotoCameraController) c = lrg_photo_camera_controller_new ();
    g_autoptr(GrlVector3) p = NULL;
    g_autoptr(GrlVector3) up = NULL;
    g_autoptr(GrlVector3) start = grl_vector3_new (0.0f, 0.0f, 0.0f);
    gfloat fwd[3] = { 0.0f, 0.0f, 1.0f };
    gfloat yaw;

    /* right is forward x up: at yaw 0 (looking along +z) it is -x, and it
     * turns with the yaw */
    lrg_photo_camera_controller_set_smoothing (c, 0.0f);
    for (yaw = 0.0f; yaw < 360.0f; yaw += 45.0f)
    {
        gfloat rad = yaw * (gfloat)G_PI / 180.0f;
        gfloat want_x;
        gfloat want_z;

        lrg_photo_camera_controller_set_yaw (c, yaw);
        lrg_photo_camera_controller_set_position (c, start);
        lrg_photo_camera_controller_move_right (c, 1.0f);
        lrg_photo_camera_controller_update (c, 1.0f);
        p = lrg_photo_camera_controller_get_position (c);
        fwd[0] = sinf (rad);
        fwd[2] = cosf (rad);
        /* forward x up = (-fwd.z, 0, fwd.x) */
        want_x = -fwd[2] * 10.0f;
        want_z = fwd[0] * 10.0f;
        NEAR_F (p->x, want_x, 1e-3);
        NEAR_F (p->z, want_z, 1e-3);
        g_clear_pointer (&p, grl_vector3_free);
    }
    (void)up;
}

static void
test_camera_orbit (void)
{
    g_autoptr(LrgPhotoCameraController) c = lrg_photo_camera_controller_new ();
    g_autoptr(GrlVector3) before = NULL;
    g_autoptr(GrlVector3) after = NULL;
    gfloat start_distance;

    g_assert_cmpint (lrg_photo_camera_controller_get_mode (c), ==, LRG_PHOTO_CAMERA_MODE_FREE);
    before = lrg_photo_camera_controller_get_position (c);

    /* focusing on a point: the camera stays, the distance is the gap */
    lrg_photo_camera_controller_focus_on (c, 0.0f, 10.0f, 0.0f);
    g_assert_cmpint (lrg_photo_camera_controller_get_mode (c), ==, LRG_PHOTO_CAMERA_MODE_ORBIT);
    after = lrg_photo_camera_controller_get_position (c);
    NEAR_F (after->x, before->x, 1e-3);
    NEAR_F (after->z, before->z, 1e-3);
    NEAR_F (lrg_photo_camera_controller_get_orbit_distance (c), 10.0f, 1e-3);
    start_distance = lrg_photo_camera_controller_get_orbit_distance (c);

    /* orbiting keeps the distance to the subject */
    lrg_photo_camera_controller_set_smoothing (c, 0.0f);
    lrg_photo_camera_controller_orbit (c, 60.0f, 5.0f);
    settle (c);
    {
        g_autoptr(GrlVector3) p = lrg_photo_camera_controller_get_position (c);
        gfloat d = sqrtf (p->x * p->x + (p->y - 10.0f) * (p->y - 10.0f) + p->z * p->z);

        NEAR_F (d, start_distance, 0.05);
    }

    /* zoom in: closer, never inside the minimum */
    lrg_photo_camera_controller_zoom (c, 0.2f);
    settle (c);
    g_assert_cmpfloat (lrg_photo_camera_controller_get_orbit_distance (c), <, start_distance);
    lrg_photo_camera_controller_zoom (c, 1000.0f);
    settle (c);
    NEAR_F (lrg_photo_camera_controller_get_orbit_distance (c), 0.5f, 1e-3);

    /* back to the free camera: the camera stays where it is */
    {
        g_autoptr(GrlVector3) p1 = lrg_photo_camera_controller_get_position (c);
        g_autoptr(GrlVector3) p2 = NULL;

        lrg_photo_camera_controller_set_mode (c, LRG_PHOTO_CAMERA_MODE_FREE);
        p2 = lrg_photo_camera_controller_get_position (c);
        NEAR_F (p1->x, p2->x, 1e-3);
        NEAR_F (p1->y, p2->y, 1e-3);
        NEAR_F (p1->z, p2->z, 1e-3);
    }
}

static void
test_camera_max_distance (void)
{
    g_autoptr(LrgPhotoCameraController) c = lrg_photo_camera_controller_new ();
    gint i;

    lrg_photo_camera_controller_set_smoothing (c, 0.0f);
    lrg_photo_camera_controller_set_anchor (c, 0.0f, 10.0f, 10.0f);
    lrg_photo_camera_controller_set_max_distance (c, 20.0f);

    /* fly far: the camera stops at the sphere's edge */
    for (i = 0; i < 40; i++)
    {
        lrg_photo_camera_controller_move_forward (c, -5.0f);
        lrg_photo_camera_controller_update (c, 0.1f);
    }
    g_assert_cmpfloat (lrg_photo_camera_controller_get_distance_to_anchor (c), <=, 20.001f);
    g_assert_cmpfloat (lrg_photo_camera_controller_get_distance_to_anchor (c), >, 19.0f);

    /* and flying back needs no unwinding: one step the other way moves in */
    lrg_photo_camera_controller_move_forward (c, 1.0f);
    lrg_photo_camera_controller_update (c, 0.1f);
    g_assert_cmpfloat (lrg_photo_camera_controller_get_distance_to_anchor (c), <, 19.0f);

    /* no limit: free to go on */
    lrg_photo_camera_controller_set_max_distance (c, 0.0f);
    for (i = 0; i < 40; i++)
    {
        lrg_photo_camera_controller_move_forward (c, -5.0f);
        lrg_photo_camera_controller_update (c, 0.1f);
    }
    g_assert_cmpfloat (lrg_photo_camera_controller_get_distance_to_anchor (c), >, 100.0f);
}

static void
test_camera_collision (void)
{
    g_autoptr(LrgPhotoCameraController) c = lrg_photo_camera_controller_new ();
    gint calls = 0;
    gint i;

    lrg_photo_camera_controller_set_smoothing (c, 0.0f);
    /* facing +z (yaw 0), at z = 10: walk "backwards" into... the wall is at
     * z = 5, so start nearer */
    {
        g_autoptr(GrlVector3) p = grl_vector3_new (0.0f, 2.0f, 0.0f);

        lrg_photo_camera_controller_set_position (c, p);
    }
    lrg_photo_camera_controller_set_collision_func (c, wall_collision, &calls, NULL);
    lrg_photo_camera_controller_set_collision_radius (c, 0.5f);

    /* collision off (the default): the camera goes through the wall */
    g_assert_false (lrg_photo_camera_controller_get_collision_enabled (c));
    for (i = 0; i < 20; i++)
    {
        lrg_photo_camera_controller_move_forward (c, 1.0f);
        lrg_photo_camera_controller_update (c, 0.1f);
    }
    {
        g_autoptr(GrlVector3) p = lrg_photo_camera_controller_get_position (c);

        g_assert_cmpfloat (p->z, >, 5.0f);
        g_assert_cmpint (calls, ==, 0);
    }

    /* back to the start, collision on: it stops short of the wall */
    {
        g_autoptr(GrlVector3) p = grl_vector3_new (0.0f, 2.0f, 0.0f);

        lrg_photo_camera_controller_set_position (c, p);
    }
    lrg_photo_camera_controller_set_collision_enabled (c, TRUE);
    for (i = 0; i < 20; i++)
    {
        lrg_photo_camera_controller_move_forward (c, 1.0f);
        lrg_photo_camera_controller_update (c, 0.1f);
    }
    {
        g_autoptr(GrlVector3) p = lrg_photo_camera_controller_get_position (c);

        g_assert_cmpfloat (p->z, <=, 4.5f + 1e-4f);
        g_assert_cmpfloat (p->z, >, 4.0f);
        g_assert_cmpint (calls, >, 0);
    }

    /* enabled without a function: nothing blocks */
    lrg_photo_camera_controller_set_collision_func (c, NULL, NULL, NULL);
    for (i = 0; i < 20; i++)
    {
        lrg_photo_camera_controller_move_forward (c, 1.0f);
        lrg_photo_camera_controller_update (c, 0.1f);
    }
    {
        g_autoptr(GrlVector3) p = lrg_photo_camera_controller_get_position (c);

        g_assert_cmpfloat (p->z, >, 5.0f);
    }
}

static gint destroyed = 0;

static void
count_destroy (gpointer data)
{
    destroyed++;
    (void)data;
}

static void
test_camera_collision_destroy_notify (void)
{
    LrgPhotoCameraController *c = lrg_photo_camera_controller_new ();
    gint calls = 0;

    destroyed = 0;
    lrg_photo_camera_controller_set_collision_func (c, wall_collision, &calls, count_destroy);
    /* replacing the function releases the old data */
    lrg_photo_camera_controller_set_collision_func (c, wall_collision, &calls, count_destroy);
    g_assert_cmpint (destroyed, ==, 1);
    g_object_unref (c);
    g_assert_cmpint (destroyed, ==, 2);
}

static void
test_camera_orbit_respects_max_distance (void)
{
    g_autoptr(LrgPhotoCameraController) c = lrg_photo_camera_controller_new ();
    gint i;

    lrg_photo_camera_controller_set_smoothing (c, 0.0f);
    lrg_photo_camera_controller_set_anchor (c, 0.0f, 0.0f, 0.0f);
    lrg_photo_camera_controller_focus_on (c, 0.0f, 0.0f, 50.0f);
    lrg_photo_camera_controller_set_max_distance (c, 30.0f);
    for (i = 0; i < 30; i++)
    {
        lrg_photo_camera_controller_orbit (c, 20.0f, 0.0f);
        lrg_photo_camera_controller_move_right (c, 3.0f);
        lrg_photo_camera_controller_update (c, 0.1f);
        g_assert_cmpfloat (lrg_photo_camera_controller_get_distance_to_anchor (c), <=, 30.01f);
    }
}

/* ==========================================================================
 * Photo mode: a mock host
 * ========================================================================== */

#define TYPE_MOCK_HOST (mock_host_get_type ())
G_DECLARE_FINAL_TYPE (MockHost, mock_host, TEST, MOCK_HOST, GObject)

struct _MockHost
{
    GObject parent_instance;

    gboolean can_freeze;
    gboolean frozen;
    gboolean hud;
    gfloat hour;
    gboolean hour_overridden;
    gboolean has_clock;
    gchar *weather;
    gboolean weather_overridden;
    gint freeze_calls;
    gint hud_calls;
};

static const gchar * const mock_weathers[] = { "sunny", "rain", "snow", NULL };

static gboolean
mock_freeze (LrgPhotoHost *host,
             gboolean      freeze)
{
    MockHost *self = (MockHost *)host;

    self->freeze_calls++;
    if (!self->can_freeze)
        return FALSE;
    self->frozen = freeze;
    return freeze;
}

static void
mock_hud (LrgPhotoHost *host,
          gboolean      visible)
{
    MockHost *self = (MockHost *)host;

    self->hud = visible;
    self->hud_calls++;
}

static gboolean
mock_get_hour (LrgPhotoHost *host,
               gfloat       *hour)
{
    MockHost *self = (MockHost *)host;

    if (!self->has_clock)
        return FALSE;
    *hour = self->hour;
    return TRUE;
}

static void
mock_set_hour (LrgPhotoHost *host,
               gfloat        hour)
{
    MockHost *self = (MockHost *)host;

    self->hour = hour;
    self->hour_overridden = TRUE;
}

static void
mock_clear_hour (LrgPhotoHost *host)
{
    MockHost *self = (MockHost *)host;

    self->hour = 12.0f;
    self->hour_overridden = FALSE;
}

static const gchar *
mock_get_weather (LrgPhotoHost *host)
{
    return ((MockHost *)host)->weather;
}

static const gchar * const *
mock_list_weathers (LrgPhotoHost *host)
{
    (void)host;
    return mock_weathers;
}

static void
mock_set_weather (LrgPhotoHost *host,
                  const gchar  *weather)
{
    MockHost *self = (MockHost *)host;

    g_free (self->weather);
    self->weather = g_strdup (weather);
    self->weather_overridden = TRUE;
}

static void
mock_clear_weather (LrgPhotoHost *host)
{
    MockHost *self = (MockHost *)host;

    g_free (self->weather);
    self->weather = g_strdup ("sunny");
    self->weather_overridden = FALSE;
}

static gboolean
mock_anchor (LrgPhotoHost *host,
             gfloat       *xyz)
{
    (void)host;
    xyz[0] = 100.0f;
    xyz[1] = 5.0f;
    xyz[2] = -40.0f;
    return TRUE;
}

static void
mock_describe (LrgPhotoHost *host,
               GHashTable   *metadata)
{
    (void)host;
    g_hash_table_replace (metadata, g_strdup ("Game"), g_strdup ("Mock Game 1.2"));
    g_hash_table_replace (metadata, g_strdup ("Location"), g_strdup ("Sentinel"));
}

static void
mock_host_iface_init (LrgPhotoHostInterface *iface)
{
    iface->freeze_world = mock_freeze;
    iface->set_hud_visible = mock_hud;
    iface->get_time_of_day = mock_get_hour;
    iface->set_time_of_day = mock_set_hour;
    iface->clear_time_of_day = mock_clear_hour;
    iface->get_weather = mock_get_weather;
    iface->list_weathers = mock_list_weathers;
    iface->set_weather = mock_set_weather;
    iface->clear_weather = mock_clear_weather;
    iface->get_anchor = mock_anchor;
    iface->describe = mock_describe;
}

G_DEFINE_TYPE_WITH_CODE (MockHost, mock_host, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PHOTO_HOST, mock_host_iface_init))

static void
mock_host_finalize (GObject *object)
{
    g_free (((MockHost *)object)->weather);
    G_OBJECT_CLASS (mock_host_parent_class)->finalize (object);
}

static void
mock_host_class_init (MockHostClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = mock_host_finalize;
}

static void
mock_host_init (MockHost *self)
{
    self->can_freeze = TRUE;
    self->hud = TRUE;
    self->hour = 12.0f;
    self->has_clock = TRUE;
    self->weather = g_strdup ("sunny");
}

/* a host that implements nothing at all */
#define TYPE_BARE_HOST (bare_host_get_type ())
G_DECLARE_FINAL_TYPE (BareHost, bare_host, TEST, BARE_HOST, GObject)
struct _BareHost { GObject parent_instance; };
static void bare_host_iface_init (LrgPhotoHostInterface *iface) { (void)iface; }
G_DEFINE_TYPE_WITH_CODE (BareHost, bare_host, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PHOTO_HOST, bare_host_iface_init))
static void bare_host_class_init (BareHostClass *klass) { (void)klass; }
static void bare_host_init (BareHost *self) { (void)self; }

static LrgPhotoMode *
new_mode (const gchar *dir)
{
    LrgPhotoMode *mode = g_object_new (LRG_TYPE_PHOTO_MODE, NULL);

    lrg_photo_mode_set_screenshot_directory (mode, dir);
    return mode;
}

static void
test_mode_world_contract (void)
{
    g_autoptr(LrgPhotoMode) mode = new_mode ("/tmp");
    g_autoptr(MockHost) host = g_object_new (TYPE_MOCK_HOST, NULL);
    g_autoptr(GError) error = NULL;
    gfloat ax;
    gfloat ay;
    gfloat az;

    lrg_photo_mode_set_host (mode, LRG_PHOTO_HOST (host));
    g_assert_true (lrg_photo_mode_get_host (mode) == LRG_PHOTO_HOST (host));
    g_assert_true (lrg_photo_mode_get_pause_world (mode));
    g_assert_false (lrg_photo_mode_is_world_frozen (mode));

    g_assert_true (lrg_photo_mode_enter (mode, NULL, &error));
    g_assert_no_error (error);
    /* the world is frozen, the HUD is gone, the camera anchors at the player */
    g_assert_true (host->frozen);
    g_assert_true (lrg_photo_mode_is_world_frozen (mode));
    g_assert_false (host->hud);
    g_assert_false (lrg_photo_mode_get_hud_visible (mode));
    lrg_photo_camera_controller_get_anchor (lrg_photo_mode_get_camera_controller (mode), &ax, &ay, &az);
    NEAR_F (ax, 100.0f, 1e-4);
    NEAR_F (az, -40.0f, 1e-4);

    /* showing the HUD again while in photo mode goes through the host */
    lrg_photo_mode_set_hud_visible (mode, TRUE);
    g_assert_true (host->hud);
    lrg_photo_mode_set_hud_visible (mode, FALSE);
    g_assert_false (host->hud);

    /* entering twice is an error */
    g_assert_false (lrg_photo_mode_enter (mode, NULL, &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_ALREADY_ACTIVE);
    g_clear_error (&error);

    /* leaving puts everything back */
    lrg_photo_mode_exit (mode);
    g_assert_false (host->frozen);
    g_assert_true (host->hud);
    g_assert_false (lrg_photo_mode_is_world_frozen (mode));
    g_assert_false (lrg_photo_mode_is_active (mode));
}

static void
test_mode_online_host_refuses_freeze (void)
{
    g_autoptr(LrgPhotoMode) mode = new_mode ("/tmp");
    g_autoptr(MockHost) host = g_object_new (TYPE_MOCK_HOST, NULL);
    gint calls_after_enter;

    host->can_freeze = FALSE;
    lrg_photo_mode_set_host (mode, LRG_PHOTO_HOST (host));
    g_assert_true (lrg_photo_mode_enter (mode, NULL, NULL));

    /* asked, refused: the camera is detached and the world runs on */
    g_assert_cmpint (host->freeze_calls, ==, 1);
    g_assert_false (lrg_photo_mode_is_world_frozen (mode));
    g_assert_false (host->hud);
    calls_after_enter = host->freeze_calls;

    /* leaving does not "unfreeze" a world that was never frozen */
    lrg_photo_mode_exit (mode);
    g_assert_cmpint (host->freeze_calls, ==, calls_after_enter);
    g_assert_true (host->hud);
}

static void
test_mode_pause_world_off (void)
{
    g_autoptr(LrgPhotoMode) mode = new_mode ("/tmp");
    g_autoptr(MockHost) host = g_object_new (TYPE_MOCK_HOST, NULL);

    lrg_photo_mode_set_host (mode, LRG_PHOTO_HOST (host));
    lrg_photo_mode_set_pause_world (mode, FALSE);
    g_assert_true (lrg_photo_mode_enter (mode, NULL, NULL));
    g_assert_cmpint (host->freeze_calls, ==, 0);
    g_assert_false (host->frozen);
    lrg_photo_mode_exit (mode);
    g_assert_cmpint (host->freeze_calls, ==, 0);
}

static void
test_mode_without_host (void)
{
    g_autoptr(LrgPhotoMode) mode = new_mode ("/tmp");
    g_autoptr(BareHost) bare = g_object_new (TYPE_BARE_HOST, NULL);
    GError *error = NULL;

    /* no host: everything still works, the overrides just say no */
    g_assert_true (lrg_photo_mode_enter (mode, NULL, &error));
    g_assert_false (lrg_photo_mode_set_time_of_day (mode, 18.0f));
    g_assert_false (lrg_photo_mode_set_weather (mode, "rain"));
    g_assert_null (lrg_photo_mode_list_weathers (mode));
    lrg_photo_mode_exit (mode);

    /* a host that implements nothing is the same: the calls are safe and
     * the world stays unfrozen */
    lrg_photo_mode_set_host (mode, LRG_PHOTO_HOST (bare));
    g_assert_true (lrg_photo_mode_enter (mode, NULL, &error));
    g_assert_no_error (error);
    g_assert_false (lrg_photo_mode_is_world_frozen (mode));
    g_assert_false (lrg_photo_mode_set_time_of_day (mode, 18.0f));
    g_assert_null (lrg_photo_mode_list_weathers (mode));
    lrg_photo_mode_exit (mode);
}

static void
test_mode_time_and_weather (void)
{
    g_autoptr(LrgPhotoMode) mode = new_mode ("/tmp");
    g_autoptr(MockHost) host = g_object_new (TYPE_MOCK_HOST, NULL);
    const gchar * const *known;
    gfloat hour = 0.0f;

    lrg_photo_mode_set_host (mode, LRG_PHOTO_HOST (host));

    /* not active: refused */
    g_assert_false (lrg_photo_mode_set_time_of_day (mode, 20.0f));
    g_assert_false (lrg_photo_mode_set_weather (mode, "rain"));

    g_assert_true (lrg_photo_mode_enter (mode, NULL, NULL));
    g_assert_true (lrg_photo_mode_get_time_of_day (mode, &hour));
    NEAR_F (hour, 12.0f, 1e-6);

    /* hours wrap round the day */
    g_assert_true (lrg_photo_mode_set_time_of_day (mode, 27.5f));
    NEAR_F (host->hour, 3.5f, 1e-5);
    g_assert_true (lrg_photo_mode_set_time_of_day (mode, -2.0f));
    NEAR_F (host->hour, 22.0f, 1e-5);
    g_assert_false (lrg_photo_mode_set_time_of_day (mode, NAN));
    g_assert_true (lrg_photo_mode_has_time_override (mode));

    /* only listed weathers */
    known = lrg_photo_mode_list_weathers (mode);
    g_assert_cmpstr (known[1], ==, "rain");
    g_assert_false (lrg_photo_mode_set_weather (mode, "meteor shower"));
    g_assert_false (host->weather_overridden);
    g_assert_true (lrg_photo_mode_set_weather (mode, "snow"));
    g_assert_cmpstr (host->weather, ==, "snow");
    g_assert_cmpstr (lrg_photo_mode_get_weather_override (mode), ==, "snow");

    /* clearing one leaves the other */
    lrg_photo_mode_clear_weather (mode);
    g_assert_false (host->weather_overridden);
    g_assert_true (host->hour_overridden);
    g_assert_true (lrg_photo_mode_set_weather (mode, "rain"));

    /* exit reverts what is still overridden */
    lrg_photo_mode_exit (mode);
    g_assert_false (host->hour_overridden);
    g_assert_false (host->weather_overridden);
    g_assert_cmpstr (host->weather, ==, "sunny");
    g_assert_false (lrg_photo_mode_has_time_override (mode));
    g_assert_null (lrg_photo_mode_get_weather_override (mode));
}

static void
test_mode_time_without_clock (void)
{
    g_autoptr(LrgPhotoMode) mode = new_mode ("/tmp");
    g_autoptr(MockHost) host = g_object_new (TYPE_MOCK_HOST, NULL);

    host->has_clock = FALSE;
    lrg_photo_mode_set_host (mode, LRG_PHOTO_HOST (host));
    g_assert_true (lrg_photo_mode_enter (mode, NULL, NULL));
    g_assert_false (lrg_photo_mode_set_time_of_day (mode, 6.0f));
    g_assert_false (host->hour_overridden);
    lrg_photo_mode_exit (mode);
}

static void
test_mode_capture_size (void)
{
    g_autoptr(LrgPhotoMode) mode = new_mode ("/tmp");
    gint w;
    gint h;

    lrg_photo_mode_set_capture_scale (mode, 4);
    g_assert_cmpuint (lrg_photo_mode_get_capture_size (mode, 1920, 1080, &w, &h), ==, 4);
    g_assert_cmpint (w, ==, 7680);
    g_assert_cmpint (h, ==, 4320);

    /* the longest side limit lowers the scale */
    lrg_photo_mode_set_max_capture_side (mode, 4096);
    g_assert_cmpuint (lrg_photo_mode_get_capture_size (mode, 1920, 1080, &w, &h), ==, 2);
    g_assert_cmpint (w, ==, 3840);
    g_assert_cmpint (h, ==, 2160);
    /* a window already beyond it still gets 1x */
    lrg_photo_mode_set_max_capture_side (mode, 256);
    g_assert_cmpuint (lrg_photo_mode_get_capture_size (mode, 1920, 1080, &w, &h), ==, 1);
    g_assert_cmpint (w, ==, 1920);

    /* properties clamp */
    lrg_photo_mode_set_capture_scale (mode, 100);
    g_assert_cmpuint (lrg_photo_mode_get_capture_scale (mode), ==, 8);
    lrg_photo_mode_set_capture_scale (mode, 0);
    g_assert_cmpuint (lrg_photo_mode_get_capture_scale (mode), ==, 1);
}

static gint capture_signals = 0;
static guint capture_scale_seen = 0;

static void
on_capture_requested (LrgPhotoMode *mode,
                      guint         scale,
                      gpointer      data)
{
    (void)mode;
    (void)data;
    capture_signals++;
    capture_scale_seen = scale;
}

static void
test_mode_capture_state_machine (void)
{
    g_autofree gchar *dir = make_temp_dir ();
    g_autofree gchar *sub = g_build_filename (dir, "pics", "librefall", NULL);
    g_autoptr(LrgPhotoMode) mode = new_mode (sub);
    g_autoptr(MockHost) host = g_object_new (TYPE_MOCK_HOST, NULL);
    g_autoptr(GrlImage) image = make_image (120, 80, 90, 120, 150);
    g_autoptr(GError) error = NULL;
    g_autoptr(GHashTable) meta = NULL;
    g_autoptr(GrlImage) loaded = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *path2 = NULL;
    guint8 px[3];

    capture_signals = 0;
    g_signal_connect (mode, "capture-requested", G_CALLBACK (on_capture_requested), NULL);
    lrg_photo_mode_set_host (mode, LRG_PHOTO_HOST (host));
    lrg_photo_mode_set_filename_prefix (mode, "photo");
    lrg_photo_mode_set_extra_metadata (mode, "Save", "Slot 3");

    /* not active: refused; finishing without a request: refused */
    g_assert_false (lrg_photo_mode_request_capture (mode, &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_NOT_ACTIVE);
    g_clear_error (&error);
    g_assert_true (lrg_photo_mode_enter (mode, NULL, NULL));
    g_assert_null (lrg_photo_mode_finish_capture (mode, image, FALSE, &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_NOT_ACTIVE);
    g_clear_error (&error);

    /* a request: pending, the signal carries the scale; a second is busy */
    lrg_photo_mode_set_capture_scale (mode, 2);
    lrg_photo_mode_set_frame (mode, LRG_PHOTO_FRAME_WIDE);
    lrg_photo_mode_set_frame_color (mode, 0xff0000u);
    lrg_photo_look_apply_preset (lrg_photo_mode_get_look (mode), LRG_PHOTO_PRESET_NOIR);
    lrg_photo_dof_set_enabled (lrg_photo_mode_get_dof (mode), TRUE);
    lrg_photo_mode_set_time_of_day (mode, 18.25f);
    g_assert_true (lrg_photo_mode_request_capture (mode, &error));
    g_assert_no_error (error);
    g_assert_cmpint (capture_signals, ==, 1);
    g_assert_cmpuint (capture_scale_seen, ==, 2);
    g_assert_true (lrg_photo_mode_is_capture_pending (mode));
    g_assert_false (lrg_photo_mode_request_capture (mode, &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_BUSY);
    g_clear_error (&error);

    /* finishing: a PNG in the (made) directory with frame and metadata */
    path = lrg_photo_mode_finish_capture (mode, image, TRUE, &error);
    g_assert_no_error (error);
    g_assert_nonnull (path);
    g_assert_false (lrg_photo_mode_is_capture_pending (mode));
    g_assert_true (g_str_has_prefix (path, sub));
    g_assert_nonnull (strstr (path, "/photo_"));
    g_assert_true (g_str_has_suffix (path, ".png"));
    g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
    g_assert_cmpstr (lrg_photo_mode_get_last_path (mode), ==, path);

    loaded = grl_image_new_from_file (path);
    g_assert_nonnull (loaded);
    g_assert_cmpint (grl_image_get_width (loaded), ==, 120);
    g_assert_cmpint (grl_image_get_height (loaded), ==, 80);
    get_px (loaded, 1, 1, px);          /* the red frame */
    g_assert_cmpint (px[0], ==, 255);
    g_assert_cmpint (px[1], ==, 0);
    get_px (loaded, 60, 40, px);        /* the noir middle: grey */
    g_assert_cmpint (px[0], ==, px[1]);
    g_assert_cmpint (px[1], ==, px[2]);

    meta = lrg_screenshot_read_png_metadata (path, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (g_hash_table_lookup (meta, "Game"), ==, "Mock Game 1.2");
    g_assert_cmpstr (g_hash_table_lookup (meta, "Save"), ==, "Slot 3");
    g_assert_cmpstr (g_hash_table_lookup (meta, "Look.Preset"), ==, "noir");
    g_assert_cmpstr (g_hash_table_lookup (meta, "Camera.Mode"), ==, "free");
    g_assert_cmpstr (g_hash_table_lookup (meta, "World.TimeOfDay"), ==, "18.25");
    g_assert_cmpstr (g_hash_table_lookup (meta, "Photo.Size"), ==, "120x80");
    g_assert_cmpstr (g_hash_table_lookup (meta, "Photo.CaptureScale"), ==, "2");
    g_assert_nonnull (g_hash_table_lookup (meta, "Photo.Aperture"));
    g_assert_nonnull (g_hash_table_lookup (meta, "Camera.Position"));
    g_assert_nonnull (g_hash_table_lookup (meta, "Creation Time"));
    /* the caller's picture was not painted on */
    get_px (image, 1, 1, px);
    g_assert_cmpint (px[0], ==, 90);

    /* a second photo never overwrites the first */
    g_assert_true (lrg_photo_mode_request_capture (mode, &error));
    path2 = lrg_photo_mode_finish_capture (mode, image, FALSE, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (path, !=, path2);

    /* an aborted capture is no longer pending; exiting forgets one too */
    g_assert_true (lrg_photo_mode_request_capture (mode, &error));
    lrg_photo_mode_abort_capture (mode);
    g_assert_false (lrg_photo_mode_is_capture_pending (mode));
    g_assert_true (lrg_photo_mode_request_capture (mode, &error));
    lrg_photo_mode_exit (mode);
    g_assert_false (lrg_photo_mode_is_capture_pending (mode));

    remove_dir (sub);
    {
        g_autofree gchar *pics = g_build_filename (dir, "pics", NULL);

        g_rmdir (pics);
    }
    remove_dir (dir);
}

static void
test_mode_finish_capture_failures (void)
{
    g_autofree gchar *dir = make_temp_dir ();
    g_autofree gchar *blocker = g_build_filename (dir, "afile", NULL);
    g_autofree gchar *under = g_build_filename (blocker, "sub", NULL);
    g_autoptr(LrgPhotoMode) mode = new_mode (under);
    g_autoptr(GrlImage) image = make_image (8, 8, 1, 1, 1);
    g_autoptr(GError) error = NULL;

    /* the directory cannot be made: a file is in the way */
    g_assert_true (g_file_set_contents (blocker, "x", -1, NULL));
    g_assert_true (lrg_photo_mode_enter (mode, NULL, NULL));
    g_assert_true (lrg_photo_mode_request_capture (mode, NULL));
    g_assert_null (lrg_photo_mode_finish_capture (mode, image, FALSE, &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_SAVE);
    /* the request is over: the host can try again */
    g_assert_false (lrg_photo_mode_is_capture_pending (mode));
    g_assert_true (lrg_photo_mode_request_capture (mode, NULL));

    /* a bad prefix is replaced by the default */
    lrg_photo_mode_set_filename_prefix (mode, "../evil");
    g_assert_cmpstr (lrg_photo_mode_get_filename_prefix (mode), ==, "screenshot");

    lrg_photo_mode_exit (mode);
    remove_dir (dir);
}

static gint ticks[8];
static gint n_ticks = 0;

static void
on_tick (LrgPhotoMode *mode,
         guint         seconds,
         gpointer      data)
{
    (void)mode;
    (void)data;
    if (n_ticks < 8)
        ticks[n_ticks++] = (gint)seconds;
}

static void
test_mode_timer (void)
{
    g_autoptr(LrgPhotoMode) mode = new_mode ("/tmp");
    g_autoptr(GError) error = NULL;

    capture_signals = 0;
    n_ticks = 0;
    g_signal_connect (mode, "capture-requested", G_CALLBACK (on_capture_requested), NULL);
    g_signal_connect (mode, "timer-tick", G_CALLBACK (on_tick), NULL);

    /* not active */
    g_assert_false (lrg_photo_mode_shoot (mode, &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_NOT_ACTIVE);
    g_clear_error (&error);
    g_assert_true (lrg_photo_mode_enter (mode, NULL, NULL));

    /* no timer: the shutter captures at once */
    g_assert_true (lrg_photo_mode_shoot (mode, &error));
    g_assert_cmpint (capture_signals, ==, 1);
    lrg_photo_mode_abort_capture (mode);

    /* a 3 second timer: counts 3, 2, 1, 0 and then asks for the capture */
    lrg_photo_mode_set_timer_seconds (mode, 3.0f);
    g_assert_true (lrg_photo_mode_shoot (mode, &error));
    g_assert_true (lrg_photo_mode_is_timer_running (mode));
    g_assert_cmpint (capture_signals, ==, 1);
    g_assert_false (lrg_photo_mode_shoot (mode, &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_BUSY);
    g_clear_error (&error);

    lrg_photo_mode_update (mode, 1.0f);
    lrg_photo_mode_update (mode, 1.0f);
    g_assert_cmpint (capture_signals, ==, 1);
    NEAR_F (lrg_photo_mode_get_timer_remaining (mode), 1.0f, 1e-4);
    lrg_photo_mode_update (mode, 1.5f);
    g_assert_false (lrg_photo_mode_is_timer_running (mode));
    g_assert_cmpint (capture_signals, ==, 2);
    g_assert_true (lrg_photo_mode_is_capture_pending (mode));
    g_assert_cmpint (n_ticks, >=, 4);
    g_assert_cmpint (ticks[0], ==, 3);
    g_assert_cmpint (ticks[n_ticks - 1], ==, 0);
    lrg_photo_mode_abort_capture (mode);

    /* cancelling stops it without a capture */
    g_assert_true (lrg_photo_mode_shoot (mode, &error));
    lrg_photo_mode_cancel_timer (mode);
    lrg_photo_mode_update (mode, 10.0f);
    g_assert_cmpint (capture_signals, ==, 2);
    NEAR_F (lrg_photo_mode_get_timer_remaining (mode), 0.0f, 1e-9);

    /* leaving mid-countdown drops it */
    g_assert_true (lrg_photo_mode_shoot (mode, &error));
    lrg_photo_mode_exit (mode);
    g_assert_false (lrg_photo_mode_is_timer_running (mode));

    /* the delay clamps */
    lrg_photo_mode_set_timer_seconds (mode, 1000.0f);
    NEAR_F (lrg_photo_mode_get_timer_seconds (mode), 60.0f, 1e-6);
    lrg_photo_mode_set_timer_seconds (mode, -4.0f);
    NEAR_F (lrg_photo_mode_get_timer_seconds (mode), 0.0f, 1e-6);
}

static void
test_mode_dof_follows_camera_fov (void)
{
    g_autoptr(LrgPhotoMode) mode = new_mode ("/tmp");

    g_assert_true (lrg_photo_mode_enter (mode, NULL, NULL));
    lrg_photo_camera_controller_set_fov (lrg_photo_mode_get_camera_controller (mode), 30.0f);
    lrg_photo_mode_update (mode, 0.016f);
    NEAR_F (lrg_photo_dof_get_fov (lrg_photo_mode_get_dof (mode)), 30.0f, 1e-4);
    lrg_photo_mode_exit (mode);
}

static void
test_depth_target_needs_window (void)
{
    GError *error = NULL;

    /* without a window the target cannot be made: an error, not a crash */
    g_assert_null (lrg_photo_depth_target_new (64, 64, &error));
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_FAILED);
    g_clear_error (&error);
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/photo/enums", test_enums);
    g_test_add_func ("/photo/look/defaults-and-clamping", test_look_defaults_and_clamping);
    g_test_add_func ("/photo/look/presets", test_look_presets);
    g_test_add_func ("/photo/look/grade-matches-color-grade", test_look_grade_matches_color_grade);
    g_test_add_func ("/photo/look/vignette", test_look_vignette);
    g_test_add_func ("/photo/look/grain", test_look_grain);
    g_test_add_func ("/photo/look/apply-image", test_look_apply_image);
    g_test_add_func ("/photo/look/lut", test_look_lut);
    g_test_add_func ("/photo/look/no-window", test_look_no_window);
    g_test_add_func ("/photo/dof/defaults-and-clamping", test_dof_defaults_and_clamping);
    g_test_add_func ("/photo/dof/coc", test_dof_coc);
    g_test_add_func ("/photo/dof/apply-image", test_dof_apply_image);
    g_test_add_func ("/photo/dof/sharp-subject", test_dof_sharp_subject_in_blurred_scene);
    g_test_add_func ("/photo/dof/depth-target-needs-window", test_depth_target_needs_window);
    g_test_add_func ("/photo/overlay/borders", test_overlay_borders);
    g_test_add_func ("/photo/overlay/grid", test_overlay_grid);
    g_test_add_func ("/photo/overlay/apply-frame", test_overlay_apply_frame);
    g_test_add_func ("/photo/screenshot/metadata-roundtrip", test_screenshot_metadata_roundtrip);
    g_test_add_func ("/photo/screenshot/metadata-failures", test_screenshot_metadata_none_and_failures);
    g_test_add_func ("/photo/camera/roll", test_camera_roll_sets_up_vector);
    g_test_add_func ("/photo/camera/from-camera", test_camera_from_camera_keeps_direction);
    g_test_add_func ("/photo/camera/strafe-right", test_camera_strafe_right_is_right);
    g_test_add_func ("/photo/camera/orbit", test_camera_orbit);
    g_test_add_func ("/photo/camera/max-distance", test_camera_max_distance);
    g_test_add_func ("/photo/camera/collision", test_camera_collision);
    g_test_add_func ("/photo/camera/collision-notify", test_camera_collision_destroy_notify);
    g_test_add_func ("/photo/camera/orbit-max-distance", test_camera_orbit_respects_max_distance);
    g_test_add_func ("/photo/mode/world-contract", test_mode_world_contract);
    g_test_add_func ("/photo/mode/online-host", test_mode_online_host_refuses_freeze);
    g_test_add_func ("/photo/mode/pause-world-off", test_mode_pause_world_off);
    g_test_add_func ("/photo/mode/without-host", test_mode_without_host);
    g_test_add_func ("/photo/mode/time-and-weather", test_mode_time_and_weather);
    g_test_add_func ("/photo/mode/time-without-clock", test_mode_time_without_clock);
    g_test_add_func ("/photo/mode/capture-size", test_mode_capture_size);
    g_test_add_func ("/photo/mode/capture-state-machine", test_mode_capture_state_machine);
    g_test_add_func ("/photo/mode/capture-failures", test_mode_finish_capture_failures);
    g_test_add_func ("/photo/mode/timer", test_mode_timer);
    g_test_add_func ("/photo/mode/dof-follows-fov", test_mode_dof_follows_camera_fov);

    return g_test_run ();
}
