/* test-image-document.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgImageDocument / LrgImageLayer: layer management, compositing
 * (opacity, blend modes, offset, visibility), undo/redo, and export.  Pure
 * CPU / headless -- no display required.
 */

#include <libregnum.h>
#include <graylib.h>
#include <glib/gstdio.h>

static const GrlColor RED   = { 255, 0, 0, 255 };
static const GrlColor GREEN = { 0, 255, 0, 255 };
static const GrlColor BLUE  = { 0, 0, 255, 255 };
static const GrlColor WHITE = { 255, 255, 255, 255 };

static void
test_new (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (64, 48);

    g_assert_nonnull (doc);
    g_assert_cmpint (lrg_image_document_get_width (doc), ==, 64);
    g_assert_cmpint (lrg_image_document_get_height (doc), ==, 48);
    g_assert_cmpuint (lrg_image_document_get_n_layers (doc), ==, 1);
    g_assert_cmpuint (lrg_image_document_get_active_index (doc), ==, 0);
    g_assert_nonnull (lrg_image_document_get_active_layer (doc));
}

static void
test_layer_management (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (8, 8);

    g_assert_cmpuint (lrg_image_document_add_layer (doc, "Top"), ==, 1);
    g_assert_cmpuint (lrg_image_document_get_n_layers (doc), ==, 2);
    g_assert_cmpuint (lrg_image_document_get_active_index (doc), ==, 1);
    g_assert_cmpstr (lrg_image_layer_get_name (
                         lrg_image_document_get_layer (doc, 1)), ==, "Top");

    /* Reorder: move top (1) to bottom (0). */
    g_assert_true (lrg_image_document_move_layer (doc, 1, 0));
    g_assert_cmpstr (lrg_image_layer_get_name (
                         lrg_image_document_get_layer (doc, 0)), ==, "Top");

    /* Duplicate. */
    g_assert_cmpint (lrg_image_document_duplicate_layer (doc, 0), ==, 1);
    g_assert_cmpuint (lrg_image_document_get_n_layers (doc), ==, 3);

    /* Remove down to one; the last removal is refused. */
    g_assert_true (lrg_image_document_remove_layer (doc, 0));
    g_assert_true (lrg_image_document_remove_layer (doc, 0));
    g_assert_cmpuint (lrg_image_document_get_n_layers (doc), ==, 1);
    g_assert_false (lrg_image_document_remove_layer (doc, 0));
}

static void
assert_pixel (LrgImageDocument *doc, gint x, gint y,
              guint8 r, guint8 g, guint8 b, guint8 a, guint8 tol)
{
    GrlColor c = { 0, 0, 0, 0 };

    g_assert_true (lrg_image_document_get_pixel (doc, x, y, &c));
    g_assert_cmpint (ABS ((gint) c.r - (gint) r), <=, tol);
    g_assert_cmpint (ABS ((gint) c.g - (gint) g), <=, tol);
    g_assert_cmpint (ABS ((gint) c.b - (gint) b), <=, tol);
    g_assert_cmpint (ABS ((gint) c.a - (gint) a), <=, tol);
}

static void
test_flatten_single (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (4, 4);

    lrg_image_document_fill_active (doc, &RED);
    assert_pixel (doc, 0, 0, 255, 0, 0, 255, 0);
    assert_pixel (doc, 3, 3, 255, 0, 0, 255, 0);
}

static void
test_flatten_over (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (4, 4);

    /* Bottom red, top blue (opaque, OVER) -> blue wins. */
    lrg_image_document_fill_active (doc, &RED);
    lrg_image_document_add_layer (doc, "Top");
    lrg_image_document_fill_active (doc, &BLUE);
    assert_pixel (doc, 1, 1, 0, 0, 255, 255, 0);
}

static void
test_opacity (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (2, 2);

    lrg_image_document_fill_active (doc, &RED);
    lrg_image_layer_set_opacity (lrg_image_document_get_active_layer (doc),
                                 0.5f);
    lrg_image_document_mark_dirty (doc);
    /* Red over transparent at 50%: rgb stays red, alpha ~127. */
    assert_pixel (doc, 0, 0, 255, 0, 0, 127, 2);
}

static void
test_blend_add (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (2, 2);

    lrg_image_document_fill_active (doc, &RED);
    lrg_image_document_add_layer (doc, "Add");
    lrg_image_document_fill_active (doc, &GREEN);
    lrg_image_layer_set_blend_mode (lrg_image_document_get_active_layer (doc),
                                    GRL_IMAGE_BLEND_ADD);
    lrg_image_document_mark_dirty (doc);
    /* red + green = yellow. */
    assert_pixel (doc, 0, 0, 255, 255, 0, 255, 0);
}

static void
test_blend_multiply (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (2, 2);

    lrg_image_document_fill_active (doc, &WHITE);
    lrg_image_document_add_layer (doc, "Mul");
    lrg_image_document_fill_active (doc, &RED);
    lrg_image_layer_set_blend_mode (lrg_image_document_get_active_layer (doc),
                                    GRL_IMAGE_BLEND_MULTIPLY);
    lrg_image_document_mark_dirty (doc);
    /* white * red = red. */
    assert_pixel (doc, 0, 0, 255, 0, 0, 255, 0);
}

static void
test_visibility (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (2, 2);

    lrg_image_document_fill_active (doc, &RED);
    lrg_image_document_add_layer (doc, "Hidden");
    lrg_image_document_fill_active (doc, &BLUE);
    lrg_image_layer_set_visible (lrg_image_document_get_active_layer (doc),
                                 FALSE);
    lrg_image_document_mark_dirty (doc);
    /* Top hidden -> red shows through. */
    assert_pixel (doc, 0, 0, 255, 0, 0, 255, 0);
}

static void
test_offset (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (4, 4);
    g_autoptr(GrlImage) patch = grl_image_new_color (2, 2, &RED);
    guint idx;

    idx = lrg_image_document_add_layer_for_image (doc, patch, "Patch");
    lrg_image_layer_set_offset (lrg_image_document_get_layer (doc, idx), 1, 1);
    lrg_image_document_mark_dirty (doc);

    /* The 2x2 red patch sits at (1,1)..(2,2). */
    assert_pixel (doc, 0, 0, 0, 0, 0, 0, 0);     /* transparent */
    assert_pixel (doc, 1, 1, 255, 0, 0, 255, 0); /* red */
    assert_pixel (doc, 2, 2, 255, 0, 0, 255, 0); /* red */
    assert_pixel (doc, 3, 3, 0, 0, 0, 0, 0);     /* transparent */
}

static void
test_undo_redo (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (2, 2);

    lrg_image_document_fill_active (doc, &RED);
    assert_pixel (doc, 0, 0, 255, 0, 0, 255, 0);
    g_assert_false (lrg_image_document_can_undo (doc));

    /* Snapshot the red state, then paint blue. */
    lrg_image_document_push_undo (doc);
    lrg_image_document_fill_active (doc, &BLUE);
    assert_pixel (doc, 0, 0, 0, 0, 255, 255, 0);
    g_assert_true (lrg_image_document_can_undo (doc));

    g_assert_true (lrg_image_document_undo (doc));
    assert_pixel (doc, 0, 0, 255, 0, 0, 255, 0);  /* back to red */
    g_assert_true (lrg_image_document_can_redo (doc));

    g_assert_true (lrg_image_document_redo (doc));
    assert_pixel (doc, 0, 0, 0, 0, 255, 255, 0);  /* blue again */

    g_assert_false (lrg_image_document_redo (doc)); /* nothing left */
}

static void
test_get_pixel_bounds (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (4, 4);
    GrlColor c;

    g_assert_false (lrg_image_document_get_pixel (doc, -1, 0, &c));
    g_assert_false (lrg_image_document_get_pixel (doc, 4, 0, &c));
    g_assert_false (lrg_image_document_get_pixel (doc, 0, 4, &c));
    g_assert_true (lrg_image_document_get_pixel (doc, 0, 0, &c));
}

static void
test_export_roundtrip (void)
{
    g_autoptr(LrgImageDocument) doc = lrg_image_document_new (4, 4);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    g_autoptr(GrlImage) reloaded = NULL;
    g_autoptr(GrlColor) c = NULL;

    dir = g_dir_make_tmp ("lrg-imgdoc-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "out.png", NULL);

    lrg_image_document_fill_active (doc, &GREEN);
    g_assert_true (lrg_image_document_export (doc, path, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    reloaded = grl_image_new_from_file (path);
    g_assert_nonnull (reloaded);
    g_assert_cmpint (grl_image_get_width (reloaded), ==, 4);
    c = grl_image_get_pixel (reloaded, 0, 0);
    g_assert_cmpint (c->g, >=, 250);
    g_assert_cmpint (c->r, <=, 5);

    g_unlink (path);
    g_rmdir (dir);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/imagedoc/new", test_new);
    g_test_add_func ("/imagedoc/layers", test_layer_management);
    g_test_add_func ("/imagedoc/flatten-single", test_flatten_single);
    g_test_add_func ("/imagedoc/flatten-over", test_flatten_over);
    g_test_add_func ("/imagedoc/opacity", test_opacity);
    g_test_add_func ("/imagedoc/blend-add", test_blend_add);
    g_test_add_func ("/imagedoc/blend-multiply", test_blend_multiply);
    g_test_add_func ("/imagedoc/visibility", test_visibility);
    g_test_add_func ("/imagedoc/offset", test_offset);
    g_test_add_func ("/imagedoc/undo-redo", test_undo_redo);
    g_test_add_func ("/imagedoc/get-pixel-bounds", test_get_pixel_bounds);
    g_test_add_func ("/imagedoc/export-roundtrip", test_export_roundtrip);

    return g_test_run ();
}
