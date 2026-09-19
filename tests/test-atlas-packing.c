/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>
#include <gio/gio.h>

typedef struct
{
    gint x, y, w, h;
} Rect;

static LrgAtlasPacker *
new_packer (gint method, gint width, gint height)
{
    return g_object_new (LRG_TYPE_ATLAS_PACKER, "method", method, "max-width", width, "max-height",
                         height, "padding", 0, "power-of-two", FALSE, NULL);
}

static void
test_gap (gconstpointer data)
{
    gint method = GPOINTER_TO_INT (data);
    g_autoptr (LrgAtlasPacker) packer = new_packer (method, 10, 6);
    g_autoptr (GError) error = NULL;

    lrg_atlas_packer_add_image (packer, "large", 6, 6, NULL);
    lrg_atlas_packer_add_image (packer, "medium", 4, 4, NULL);
    lrg_atlas_packer_add_image (packer, "small", 4, 2, NULL);
    if (method == LRG_ATLAS_PACK_METHOD_SHELF)
    {
        g_assert_false (lrg_atlas_packer_pack (packer, &error));
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
        g_assert_false (lrg_atlas_packer_get_image_position (packer, "large", NULL, NULL, NULL));
    }
    else
    {
        g_assert_true (lrg_atlas_packer_pack (packer, &error));
        g_assert_no_error (error);
        g_assert_cmpfloat (lrg_atlas_packer_get_efficiency (packer), ==, 1);
    }
}

static void
test_rotation (gconstpointer data)
{
    g_autoptr (LrgAtlasPacker) packer = new_packer (GPOINTER_TO_INT (data), 4, 6);
    g_autoptr (LrgTextureAtlas) atlas = NULL;
    LrgAtlasRegion *region;
    gboolean rotated;

    lrg_atlas_packer_add_image (packer, "sprite", 6, 4, NULL);
    g_assert_false (lrg_atlas_packer_pack (packer, NULL));
    lrg_atlas_packer_set_allow_rotation (packer, TRUE);
    g_assert_true (lrg_atlas_packer_pack (packer, NULL));
    g_assert_true (lrg_atlas_packer_get_image_position (packer, "sprite", NULL, NULL, &rotated));
    g_assert_true (rotated);
    atlas = lrg_atlas_packer_create_atlas (packer, "rotated");
    region = lrg_texture_atlas_get_region (atlas, "sprite");
    g_assert_cmpint (lrg_atlas_region_get_width (region), ==, 4);
    g_assert_cmpint (lrg_atlas_region_get_height (region), ==, 6);
    g_assert_true (lrg_atlas_region_is_rotated (region));
}

static void
test_limits (gconstpointer data)
{
    g_autoptr (LrgAtlasPacker) packer = new_packer (GPOINTER_TO_INT (data), 100, 100);

    lrg_atlas_packer_add_image (packer, "sprite", 65, 1, NULL);
    g_assert_true (lrg_atlas_packer_pack (packer, NULL));
    g_object_set (packer, "power-of-two", TRUE, NULL);
    g_assert_cmpint (lrg_atlas_packer_get_packed_width (packer), ==, 0);
    g_assert_false (lrg_atlas_packer_get_image_position (packer, "sprite", NULL, NULL, NULL));
    g_assert_false (lrg_atlas_packer_pack (packer, NULL));
    lrg_atlas_packer_set_power_of_two (packer, FALSE);
    lrg_atlas_packer_set_padding (packer, G_MAXINT);
    g_assert_false (lrg_atlas_packer_pack (packer, NULL));
    lrg_atlas_packer_set_padding (packer, 0);
    lrg_atlas_packer_set_max_size (packer, 64, 100);
    g_assert_false (lrg_atlas_packer_pack (packer, NULL));
    lrg_atlas_packer_set_max_size (packer, 100, 100);
    g_assert_true (lrg_atlas_packer_pack (packer, NULL));
    lrg_atlas_packer_remove_image (packer, "sprite");
    g_assert_cmpint (lrg_atlas_packer_get_packed_width (packer), ==, 0);
}

static void
test_randomized (gconstpointer data)
{
    g_autoptr (GRand) random = g_rand_new_with_seed (20260918);
    guint run, i, j;

    for (run = 0; run < 20; run++)
    {
        g_autoptr (LrgAtlasPacker) packer = new_packer (GPOINTER_TO_INT (data), 256, 256);
        Rect rects[64];

        lrg_atlas_packer_set_allow_rotation (packer, TRUE);
        lrg_atlas_packer_set_padding (packer, 2);
        for (i = 0; i < 64; i++)
        {
            g_autofree gchar *name = g_strdup_printf ("sprite-%u", i);

            rects[i].w = g_rand_int_range (random, 2, 25);
            rects[i].h = g_rand_int_range (random, 2, 25);
            lrg_atlas_packer_add_image (packer, name, rects[i].w, rects[i].h, NULL);
        }
        g_assert_true (lrg_atlas_packer_pack (packer, NULL));
        for (i = 0; i < 64; i++)
        {
            g_autofree gchar *name = g_strdup_printf ("sprite-%u", i);
            gboolean rotated;

            g_assert_true (lrg_atlas_packer_get_image_position (packer, name, &rects[i].x,
                                                                &rects[i].y, &rotated));
            if (rotated)
            {
                gint swap = rects[i].w;

                rects[i].w = rects[i].h;
                rects[i].h = swap;
            }
            rects[i].w += 2;
            rects[i].h += 2;
            g_assert_cmpint (rects[i].x, >=, 0);
            g_assert_cmpint (rects[i].y, >=, 0);
            g_assert_cmpint (rects[i].x + rects[i].w, <=,
                             lrg_atlas_packer_get_packed_width (packer));
            g_assert_cmpint (rects[i].y + rects[i].h, <=,
                             lrg_atlas_packer_get_packed_height (packer));
            for (j = 0; j < i; j++)
                g_assert_true (rects[i].x >= rects[j].x + rects[j].w
                               || rects[j].x >= rects[i].x + rects[i].w
                               || rects[i].y >= rects[j].y + rects[j].h
                               || rects[j].y >= rects[i].y + rects[i].h);
        }
        /* Repacking is deterministic. */
        g_assert_true (lrg_atlas_packer_pack (packer, NULL));
        for (i = 0; i < 64; i++)
        {
            g_autofree gchar *name = g_strdup_printf ("sprite-%u", i);
            gint x, y;

            lrg_atlas_packer_get_image_position (packer, name, &x, &y, NULL);
            g_assert_cmpint (x, ==, rects[i].x);
            g_assert_cmpint (y, ==, rects[i].y);
        }
    }
}

static void
test_benchmark (gconstpointer data)
{
    g_autoptr (GRand) random = g_rand_new_with_seed (42);
    g_autoptr (LrgAtlasPacker) packer = new_packer (GPOINTER_TO_INT (data), 1024, 1024);
    gint64 started;
    guint i;

    lrg_atlas_packer_set_allow_rotation (packer, TRUE);
    lrg_atlas_packer_set_padding (packer, 1);
    for (i = 0; i < 256; i++)
    {
        g_autofree gchar *name = g_strdup_printf ("sprite-%u", i);

        lrg_atlas_packer_add_image (packer, name, g_rand_int_range (random, 8, 65),
                                    g_rand_int_range (random, 8, 65), NULL);
    }
    started = g_get_monotonic_time ();
    g_assert_true (lrg_atlas_packer_pack (packer, NULL));
    g_test_message ("method=%d 256 sprites: %dx%d, efficiency=%.3f, time=%" G_GINT64_FORMAT " us",
                    GPOINTER_TO_INT (data), lrg_atlas_packer_get_packed_width (packer),
                    lrg_atlas_packer_get_packed_height (packer),
                    lrg_atlas_packer_get_efficiency (packer), g_get_monotonic_time () - started);
}

int
main (int argc, char **argv)
{
    gint method;

    g_test_init (&argc, &argv, NULL);
    for (method = 0; method < 3; method++)
    {
        g_autofree gchar *gap = g_strdup_printf ("/packing/%d/gap", method);
        g_autofree gchar *rotation = g_strdup_printf ("/packing/%d/rotation", method);
        g_autofree gchar *limits = g_strdup_printf ("/packing/%d/limits", method);
        g_autofree gchar *random = g_strdup_printf ("/packing/%d/random", method);
        g_autofree gchar *benchmark = g_strdup_printf ("/packing/%d/benchmark", method);

        g_test_add_data_func (gap, GINT_TO_POINTER (method), test_gap);
        g_test_add_data_func (rotation, GINT_TO_POINTER (method), test_rotation);
        g_test_add_data_func (limits, GINT_TO_POINTER (method), test_limits);
        g_test_add_data_func (random, GINT_TO_POINTER (method), test_randomized);
        g_test_add_data_func (benchmark, GINT_TO_POINTER (method), test_benchmark);
    }
    return g_test_run ();
}
