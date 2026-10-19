/* test-sharpen.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for LrgSharpen: the CPU reference (a flat area is
 * unchanged, an edge in mid-grey gains contrast, a full-range edge is
 * left alone, more sharpness sharpens more, results stay in range), the
 * properties and the identity rule, and the shader source's uniforms.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

static void
test_flat_is_unchanged (void)
{
    const gfloat c[3] = { 0.4f, 0.5f, 0.6f };
    gfloat out[3];
    guint k;

    lrg_sharpen_pixel (1.0f, c, c, c, c, c, out);
    for (k = 0; k < 3; k++)
        g_assert_cmpfloat_with_epsilon (out[k], c[k], 1e-5f);
}

static void
test_edge_gains_contrast (void)
{
    const gfloat dark[3] = { 0.3f, 0.3f, 0.3f };
    const gfloat bright[3] = { 0.6f, 0.6f, 0.6f };
    gfloat soft[3];
    gfloat hard[3];

    /* a bright pixel with a dark neighbour above: pushed brighter */
    lrg_sharpen_pixel (0.0f, bright, dark, bright, bright, bright, soft);
    lrg_sharpen_pixel (1.0f, bright, dark, bright, bright, bright, hard);
    g_assert_cmpfloat (soft[0], >, bright[0]);
    g_assert_cmpfloat (hard[0], >, soft[0]);
    /* and a dark pixel with a bright neighbour: pushed darker */
    lrg_sharpen_pixel (1.0f, dark, bright, dark, dark, dark, hard);
    g_assert_cmpfloat (hard[0], <, dark[0]);
}

static void
test_full_range_edge_is_left_alone (void)
{
    const gfloat black[3] = { 0.0f, 0.0f, 0.0f };
    const gfloat white[3] = { 1.0f, 1.0f, 1.0f };
    gfloat out[3];

    /* the neighbourhood already touches white: no headroom, no ringing */
    lrg_sharpen_pixel (1.0f, white, black, white, white, white, out);
    g_assert_cmpfloat_with_epsilon (out[0], 1.0f, 1e-5f);
    /* and a black pixel next to white stays black */
    lrg_sharpen_pixel (1.0f, black, white, black, black, black, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.0f, 1e-5f);
}

static void
test_range_and_clamp (void)
{
    const gfloat a[3] = { 0.05f, 0.5f, 0.95f };
    const gfloat b[3] = { 0.9f, 0.1f, 0.2f };
    gfloat out[3];
    guint k;

    lrg_sharpen_pixel (7.0f, a, b, a, b, a, out);   /* sharpness clamps to 1 */
    for (k = 0; k < 3; k++)
    {
        g_assert_cmpfloat (out[k], >=, 0.0f);
        g_assert_cmpfloat (out[k], <=, 1.0f);
    }
}

static void
test_properties (void)
{
    g_autoptr(LrgSharpen) s = lrg_sharpen_new ();
    gfloat v = 0.0f;

    g_assert_cmpfloat_with_epsilon (lrg_sharpen_get_sharpness (s), 0.5f, 1e-6f);
    lrg_sharpen_set_sharpness (s, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_sharpen_get_sharpness (s), 1.0f, 1e-6f);
    g_object_set (s, "sharpness", 0.25f, NULL);
    g_object_get (s, "sharpness", &v, NULL);
    g_assert_cmpfloat_with_epsilon (v, 0.25f, 1e-6f);

    g_assert_false (lrg_sharpen_is_identity (s));
    lrg_post_effect_set_intensity (LRG_POST_EFFECT (s), 0.0f);
    g_assert_true (lrg_sharpen_is_identity (s));
}

static void
test_shader_source (void)
{
    const gchar *src = lrg_sharpen_get_fragment_shader ();

    g_assert_nonnull (strstr (src, "#version 330"));
    g_assert_nonnull (strstr (src, "uniform float sharpness"));
    g_assert_nonnull (strstr (src, "uniform float intensity"));
    g_assert_nonnull (strstr (src, "textureSize"));
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/sharpen/flat", test_flat_is_unchanged);
    g_test_add_func ("/sharpen/edge", test_edge_gains_contrast);
    g_test_add_func ("/sharpen/full-range", test_full_range_edge_is_left_alone);
    g_test_add_func ("/sharpen/range", test_range_and_clamp);
    g_test_add_func ("/sharpen/properties", test_properties);
    g_test_add_func ("/sharpen/shader-source", test_shader_source);
    return g_test_run ();
}
