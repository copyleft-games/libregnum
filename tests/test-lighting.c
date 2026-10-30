/* test-lighting.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the 2D lighting module.
 */

#include "../src/lrg-enums.h"
#include "../src/lighting/lrg-shadow-caster.h"
#include "../src/lighting/lrg-light2d.h"
#include "../src/lighting/lrg-point-light2d.h"
#include "../src/lighting/lrg-spot-light2d.h"
#include "../src/lighting/lrg-directional-light2d.h"
#include "../src/lighting/lrg-shadow-map.h"
#include "../src/lighting/lrg-lightmap.h"
#include "../src/lighting/lrg-light-probe.h"
#include "../src/lighting/lrg-lighting-manager.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <math.h>
#include <raylib.h>

/* ========================================================================== */
/*                          Shadow Edge Tests                                  */
/* ========================================================================== */

static void
test_shadow_edge_boxed (void)
{
    LrgShadowEdge *edge;
    LrgShadowEdge *copy;

    edge = lrg_shadow_edge_new (10.0f, 20.0f, 30.0f, 40.0f);

    g_assert_nonnull (edge);
    g_assert_cmpfloat (edge->x1, ==, 10.0f);
    g_assert_cmpfloat (edge->y1, ==, 20.0f);
    g_assert_cmpfloat (edge->x2, ==, 30.0f);
    g_assert_cmpfloat (edge->y2, ==, 40.0f);

    /* Test copy */
    copy = lrg_shadow_edge_copy (edge);
    g_assert_nonnull (copy);
    g_assert_cmpfloat (copy->x1, ==, 10.0f);
    g_assert_cmpfloat (copy->y1, ==, 20.0f);

    lrg_shadow_edge_free (edge);
    lrg_shadow_edge_free (copy);
}

/* ========================================================================== */
/*                          Point Light Tests                                  */
/* ========================================================================== */

static void
test_point_light_new (void)
{
    g_autoptr(LrgPointLight2D) light = NULL;

    light = lrg_point_light2d_new ();

    g_assert_nonnull (light);
    g_assert_true (LRG_IS_POINT_LIGHT2D (light));
    g_assert_true (LRG_IS_LIGHT2D (light));
}

static void
test_point_light_position (void)
{
    g_autoptr(LrgPointLight2D) light = NULL;
    gfloat x, y;

    light = lrg_point_light2d_new ();

    lrg_light2d_set_position (LRG_LIGHT2D (light), 100.0f, 200.0f);
    lrg_light2d_get_position (LRG_LIGHT2D (light), &x, &y);

    g_assert_cmpfloat (x, ==, 100.0f);
    g_assert_cmpfloat (y, ==, 200.0f);
}

static void
test_point_light_color (void)
{
    g_autoptr(LrgPointLight2D) light = NULL;
    guint8 r, g, b;

    light = lrg_point_light2d_new ();

    lrg_light2d_set_color (LRG_LIGHT2D (light), 255, 128, 64);
    lrg_light2d_get_color (LRG_LIGHT2D (light), &r, &g, &b);

    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 128);
    g_assert_cmpuint (b, ==, 64);
}

static void
test_point_light_intensity (void)
{
    g_autoptr(LrgPointLight2D) light = NULL;

    light = lrg_point_light2d_new ();

    lrg_light2d_set_intensity (LRG_LIGHT2D (light), 0.75f);
    g_assert_cmpfloat (lrg_light2d_get_intensity (LRG_LIGHT2D (light)), ==, 0.75f);
}

static void
test_point_light_radius (void)
{
    g_autoptr(LrgPointLight2D) light = NULL;

    light = lrg_point_light2d_new ();

    lrg_point_light2d_set_radius (light, 300.0f);
    g_assert_cmpfloat (lrg_point_light2d_get_radius (light), ==, 300.0f);

    lrg_point_light2d_set_inner_radius (light, 50.0f);
    g_assert_cmpfloat (lrg_point_light2d_get_inner_radius (light), ==, 50.0f);
}

static void
test_point_light_flicker (void)
{
    g_autoptr(LrgPointLight2D) light = NULL;

    light = lrg_point_light2d_new ();

    g_assert_false (lrg_point_light2d_get_flicker_enabled (light));

    lrg_point_light2d_set_flicker_enabled (light, TRUE);
    g_assert_true (lrg_point_light2d_get_flicker_enabled (light));

    lrg_point_light2d_set_flicker_amount (light, 0.3f);
    g_assert_cmpfloat (lrg_point_light2d_get_flicker_amount (light), ==, 0.3f);

    lrg_point_light2d_set_flicker_speed (light, 10.0f);
    g_assert_cmpfloat (lrg_point_light2d_get_flicker_speed (light), ==, 10.0f);
}

static void
test_point_light_enabled (void)
{
    g_autoptr(LrgPointLight2D) light = NULL;

    light = lrg_point_light2d_new ();

    g_assert_true (lrg_light2d_get_enabled (LRG_LIGHT2D (light)));

    lrg_light2d_set_enabled (LRG_LIGHT2D (light), FALSE);
    g_assert_false (lrg_light2d_get_enabled (LRG_LIGHT2D (light)));
}

/* ========================================================================== */
/*                          Spot Light Tests                                   */
/* ========================================================================== */

static void
test_spot_light_new (void)
{
    g_autoptr(LrgSpotLight2D) light = NULL;

    light = lrg_spot_light2d_new ();

    g_assert_nonnull (light);
    g_assert_true (LRG_IS_SPOT_LIGHT2D (light));
    g_assert_true (LRG_IS_LIGHT2D (light));
}

static void
test_spot_light_cone (void)
{
    g_autoptr(LrgSpotLight2D) light = NULL;

    light = lrg_spot_light2d_new ();

    /* Test angle (the outer/full cone angle) */
    lrg_spot_light2d_set_angle (light, 1.047f);  /* ~60 degrees in radians */
    g_assert_cmpfloat (fabs (lrg_spot_light2d_get_angle (light) - 1.047f), <, 0.01f);

    /* Test inner angle */
    lrg_spot_light2d_set_inner_angle (light, 0.524f);  /* ~30 degrees in radians */
    g_assert_cmpfloat (fabs (lrg_spot_light2d_get_inner_angle (light) - 0.524f), <, 0.01f);
}

static void
test_spot_light_direction (void)
{
    g_autoptr(LrgSpotLight2D) light = NULL;
    gfloat dir;

    light = lrg_spot_light2d_new ();

    /* Direction is an angle in radians */
    lrg_spot_light2d_set_direction (light, 0.785f);  /* ~45 degrees */
    dir = lrg_spot_light2d_get_direction (light);

    g_assert_cmpfloat (fabs (dir - 0.785f), <, 0.01f);
}

static void
test_spot_light_radius (void)
{
    g_autoptr(LrgSpotLight2D) light = NULL;

    light = lrg_spot_light2d_new ();

    lrg_spot_light2d_set_radius (light, 500.0f);
    g_assert_cmpfloat (lrg_spot_light2d_get_radius (light), ==, 500.0f);
}

/* ========================================================================== */
/*                      Directional Light Tests                                */
/* ========================================================================== */

static void
test_directional_light_new (void)
{
    g_autoptr(LrgDirectionalLight2D) light = NULL;

    light = lrg_directional_light2d_new ();

    g_assert_nonnull (light);
    g_assert_true (LRG_IS_DIRECTIONAL_LIGHT2D (light));
    g_assert_true (LRG_IS_LIGHT2D (light));
}

static void
test_directional_light_direction (void)
{
    g_autoptr(LrgDirectionalLight2D) light = NULL;
    gfloat dir;

    light = lrg_directional_light2d_new ();

    /* Direction is an angle in radians */
    lrg_directional_light2d_set_direction (light, 2.094f);  /* ~120 degrees */
    dir = lrg_directional_light2d_get_direction (light);

    g_assert_cmpfloat (fabs (dir - 2.094f), <, 0.01f);
}

static void
test_directional_light_shadow_length (void)
{
    g_autoptr(LrgDirectionalLight2D) light = NULL;

    light = lrg_directional_light2d_new ();

    lrg_directional_light2d_set_shadow_length (light, 200.0f);
    g_assert_cmpfloat (lrg_directional_light2d_get_shadow_length (light), ==, 200.0f);
}

/* ========================================================================== */
/*                          Shadow Map Tests                                   */
/* ========================================================================== */

static void
test_shadow_map_new (void)
{
    g_autoptr(LrgShadowMap) map = NULL;

    map = lrg_shadow_map_new (512, 512);

    g_assert_nonnull (map);
    g_assert_true (LRG_IS_SHADOW_MAP (map));
    g_assert_cmpuint (lrg_shadow_map_get_width (map), ==, 512);
    g_assert_cmpuint (lrg_shadow_map_get_height (map), ==, 512);
}

static void
test_shadow_map_resize (void)
{
    g_autoptr(LrgShadowMap) map = NULL;

    map = lrg_shadow_map_new (256, 256);

    lrg_shadow_map_resize (map, 512, 512);
    g_assert_cmpuint (lrg_shadow_map_get_width (map), ==, 512);
    g_assert_cmpuint (lrg_shadow_map_get_height (map), ==, 512);
}

/* ========================================================================== */
/*                          Lightmap Tests                                     */
/* ========================================================================== */

static void
test_lightmap_new (void)
{
    g_autoptr(LrgLightmap) lightmap = NULL;

    lightmap = lrg_lightmap_new (256, 256);

    g_assert_nonnull (lightmap);
    g_assert_true (LRG_IS_LIGHTMAP (lightmap));
    g_assert_cmpuint (lrg_lightmap_get_width (lightmap), ==, 256);
    g_assert_cmpuint (lrg_lightmap_get_height (lightmap), ==, 256);
}

static void
test_lightmap_pixel (void)
{
    g_autoptr(LrgLightmap) lightmap = NULL;
    guint8 r, g, b;

    lightmap = lrg_lightmap_new (64, 64);

    lrg_lightmap_set_pixel (lightmap, 10, 20, 255, 128, 64);
    lrg_lightmap_get_pixel (lightmap, 10, 20, &r, &g, &b);

    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 128);
    g_assert_cmpuint (b, ==, 64);
}

static void
test_lightmap_clear (void)
{
    g_autoptr(LrgLightmap) lightmap = NULL;
    guint8 r, g, b;

    lightmap = lrg_lightmap_new (64, 64);

    lrg_lightmap_set_pixel (lightmap, 10, 10, 255, 255, 255);
    lrg_lightmap_clear (lightmap, 50, 50, 50);
    lrg_lightmap_get_pixel (lightmap, 10, 10, &r, &g, &b);

    g_assert_cmpuint (r, ==, 50);
    g_assert_cmpuint (g, ==, 50);
    g_assert_cmpuint (b, ==, 50);
}

/* ========================================================================== */
/*                          Light Probe Tests                                  */
/* ========================================================================== */

static void
test_light_probe_new (void)
{
    g_autoptr(LrgLightProbe) probe = NULL;

    probe = lrg_light_probe_new ();

    g_assert_nonnull (probe);
    g_assert_true (LRG_IS_LIGHT_PROBE (probe));
}

static void
test_light_probe_position (void)
{
    g_autoptr(LrgLightProbe) probe = NULL;
    gfloat x, y;

    probe = lrg_light_probe_new ();

    lrg_light_probe_set_position (probe, 150.0f, 250.0f);
    lrg_light_probe_get_position (probe, &x, &y);

    g_assert_cmpfloat (x, ==, 150.0f);
    g_assert_cmpfloat (y, ==, 250.0f);
}

static void
test_light_probe_radius (void)
{
    g_autoptr(LrgLightProbe) probe = NULL;

    probe = lrg_light_probe_new ();

    g_assert_cmpfloat (lrg_light_probe_get_radius (probe), ==, 50.0f);

    lrg_light_probe_set_radius (probe, 100.0f);
    g_assert_cmpfloat (lrg_light_probe_get_radius (probe), ==, 100.0f);
}

static void
test_light_probe_sample (void)
{
    g_autoptr(LrgLightProbe) probe = NULL;
    g_autoptr(LrgPointLight2D) light = NULL;
    g_autoptr(GPtrArray) lights = NULL;
    guint8 r, g, b;
    gfloat intensity;

    probe = lrg_light_probe_new ();
    light = lrg_point_light2d_new ();

    /* Place light at origin */
    lrg_light2d_set_position (LRG_LIGHT2D (light), 0.0f, 0.0f);
    lrg_light2d_set_color (LRG_LIGHT2D (light), 255, 200, 150);
    lrg_light2d_set_intensity (LRG_LIGHT2D (light), 1.0f);

    /* Place probe nearby */
    lrg_light_probe_set_position (probe, 10.0f, 10.0f);
    lrg_light_probe_set_radius (probe, 100.0f);

    /* Create lights array */
    lights = g_ptr_array_new ();
    g_ptr_array_add (lights, light);

    /* Sample */
    lrg_light_probe_sample (probe, lights);

    lrg_light_probe_get_color (probe, &r, &g, &b);
    intensity = lrg_light_probe_get_intensity (probe);

    /* Should have sampled the light */
    g_assert_cmpfloat (intensity, >, 0.0f);
    g_assert_cmpuint (r, >, 0);
}

/* ========================================================================== */
/*                      Lighting Manager Tests                                 */
/* ========================================================================== */

static void
test_lighting_manager_new (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;

    manager = lrg_lighting_manager_new ();

    g_assert_nonnull (manager);
    g_assert_true (LRG_IS_LIGHTING_MANAGER (manager));
}

static void
test_lighting_manager_add_remove_light (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgPointLight2D) light = NULL;

    manager = lrg_lighting_manager_new ();
    light = lrg_point_light2d_new ();

    g_assert_cmpuint (lrg_lighting_manager_get_light_count (manager), ==, 0);

    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (light));
    g_assert_cmpuint (lrg_lighting_manager_get_light_count (manager), ==, 1);

    /* Adding same light again should not increase count */
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (light));
    g_assert_cmpuint (lrg_lighting_manager_get_light_count (manager), ==, 1);

    lrg_lighting_manager_remove_light (manager, LRG_LIGHT2D (light));
    g_assert_cmpuint (lrg_lighting_manager_get_light_count (manager), ==, 0);
}

static void
test_lighting_manager_ambient (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    guint8 r, g, b;

    manager = lrg_lighting_manager_new ();

    lrg_lighting_manager_set_ambient_color (manager, 100, 100, 120);
    lrg_lighting_manager_get_ambient_color (manager, &r, &g, &b);

    g_assert_cmpuint (r, ==, 100);
    g_assert_cmpuint (g, ==, 100);
    g_assert_cmpuint (b, ==, 120);

    lrg_lighting_manager_set_ambient_intensity (manager, 0.5f);
    g_assert_cmpfloat (lrg_lighting_manager_get_ambient_intensity (manager), ==, 0.5f);
}

static void
test_lighting_manager_shadows (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;

    manager = lrg_lighting_manager_new ();

    g_assert_true (lrg_lighting_manager_get_shadows_enabled (manager));

    lrg_lighting_manager_set_shadows_enabled (manager, FALSE);
    g_assert_false (lrg_lighting_manager_get_shadows_enabled (manager));

    lrg_lighting_manager_set_default_shadow_method (manager, LRG_SHADOW_METHOD_GEOMETRY);
    g_assert_cmpint (lrg_lighting_manager_get_default_shadow_method (manager),
                     ==, LRG_SHADOW_METHOD_GEOMETRY);
}

static void
test_lighting_manager_blend_mode (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;

    manager = lrg_lighting_manager_new ();

    g_assert_cmpint (lrg_lighting_manager_get_blend_mode (manager),
                     ==, LRG_LIGHT_BLEND_MULTIPLY);

    lrg_lighting_manager_set_blend_mode (manager, LRG_LIGHT_BLEND_ADDITIVE);
    g_assert_cmpint (lrg_lighting_manager_get_blend_mode (manager),
                     ==, LRG_LIGHT_BLEND_ADDITIVE);
}

static void
test_lighting_manager_lightmap (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgLightmap) lightmap = NULL;

    manager = lrg_lighting_manager_new ();
    lightmap = lrg_lightmap_new (128, 128);

    g_assert_null (lrg_lighting_manager_get_lightmap (manager));

    lrg_lighting_manager_set_lightmap (manager, lightmap);
    g_assert_true (lrg_lighting_manager_get_lightmap (manager) == lightmap);

    lrg_lighting_manager_set_lightmap (manager, NULL);
    g_assert_null (lrg_lighting_manager_get_lightmap (manager));
}

static void
test_lighting_manager_viewport (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;

    manager = lrg_lighting_manager_new ();

    /* Should not crash */
    lrg_lighting_manager_set_viewport (manager, 0.0f, 0.0f, 1920.0f, 1080.0f);
}

static void
test_lighting_manager_update (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgPointLight2D) light = NULL;

    manager = lrg_lighting_manager_new ();
    light = lrg_point_light2d_new ();

    lrg_point_light2d_set_flicker_enabled (light, TRUE);
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (light));

    /* Should not crash */
    lrg_lighting_manager_update (manager, 0.016f);
    lrg_lighting_manager_update (manager, 0.016f);
}

/* ========================================================================== */
/*                          Light Falloff Tests                                */
/* ========================================================================== */

static void
test_light_falloff (void)
{
    g_autoptr(LrgPointLight2D) light = NULL;

    light = lrg_point_light2d_new ();

    /* Default should be quadratic */
    g_assert_cmpint (lrg_light2d_get_falloff (LRG_LIGHT2D (light)),
                     ==, LRG_LIGHT_FALLOFF_QUADRATIC);

    lrg_light2d_set_falloff (LRG_LIGHT2D (light), LRG_LIGHT_FALLOFF_LINEAR);
    g_assert_cmpint (lrg_light2d_get_falloff (LRG_LIGHT2D (light)),
                     ==, LRG_LIGHT_FALLOFF_LINEAR);

    lrg_light2d_set_falloff (LRG_LIGHT2D (light), LRG_LIGHT_FALLOFF_NONE);
    g_assert_cmpint (lrg_light2d_get_falloff (LRG_LIGHT2D (light)),
                     ==, LRG_LIGHT_FALLOFF_NONE);
}

/* ========================================================================== */
/*                          Shadow Method Tests                                */
/* ========================================================================== */

static void
test_shadow_method (void)
{
    g_autoptr(LrgPointLight2D) light = NULL;

    light = lrg_point_light2d_new ();

    /* Default should be geometry */
    g_assert_cmpint (lrg_light2d_get_shadow_method (LRG_LIGHT2D (light)),
                     ==, LRG_SHADOW_METHOD_GEOMETRY);

    lrg_light2d_set_shadow_method (LRG_LIGHT2D (light), LRG_SHADOW_METHOD_RAY_CAST);
    g_assert_cmpint (lrg_light2d_get_shadow_method (LRG_LIGHT2D (light)),
                     ==, LRG_SHADOW_METHOD_RAY_CAST);
}

/* ========================================================================== */
/*                          Main Entry Point                                   */
/* ========================================================================== */

/* ==========================================================================
 * Lightmap file I/O (headless: image decode/encode needs no window)
 * ========================================================================== */

static void
test_lightmap_save_load_roundtrip (void)
{
    g_autoptr(LrgLightmap) lightmap = NULL;
    g_autoptr(LrgLightmap) loaded = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    guint8 r, g, b;
    guint x, y;

    dir = g_dir_make_tmp ("lrg-lightmap-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "baked.png", NULL);

    /* A gradient plus one marked texel, so orientation is checked too */
    lightmap = lrg_lightmap_new (16, 8);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 16; x++)
            lrg_lightmap_set_pixel (lightmap, x, y, (guint8) (x * 16), (guint8) (y * 32), 7);
    lrg_lightmap_set_pixel (lightmap, 15, 0, 1, 2, 3);

    g_assert_true (lrg_lightmap_save (lightmap, path, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));

    loaded = lrg_lightmap_load (path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (loaded);
    g_assert_cmpuint (lrg_lightmap_get_width (loaded), ==, 16);
    g_assert_cmpuint (lrg_lightmap_get_height (loaded), ==, 8);

    lrg_lightmap_get_pixel (loaded, 3, 5, &r, &g, &b);
    g_assert_cmpuint (r, ==, 48);
    g_assert_cmpuint (g, ==, 160);
    g_assert_cmpuint (b, ==, 7);

    lrg_lightmap_get_pixel (loaded, 15, 0, &r, &g, &b);
    g_assert_cmpuint (r, ==, 1);
    g_assert_cmpuint (g, ==, 2);
    g_assert_cmpuint (b, ==, 3);

    g_unlink (path);
    g_rmdir (dir);
}

static void
test_lightmap_load_errors (void)
{
    g_autoptr(LrgLightmap) loaded = NULL;
    g_autoptr(LrgLightmap) lightmap = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *missing = NULL;
    g_autofree gchar *garbage = NULL;
    g_autofree gchar *unwritable = NULL;

    dir = g_dir_make_tmp ("lrg-lightmap-XXXXXX", NULL);
    missing = g_build_filename (dir, "missing.png", NULL);
    garbage = g_build_filename (dir, "garbage.png", NULL);
    unwritable = g_build_filename (dir, "no-such-dir", "out.png", NULL);

    loaded = lrg_lightmap_load (missing, &error);
    g_assert_null (loaded);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error (&error);

    g_assert_true (g_file_set_contents (garbage, "not an image", -1, NULL));
    loaded = lrg_lightmap_load (garbage, &error);
    g_assert_null (loaded);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);

    lightmap = lrg_lightmap_new (4, 4);
    g_assert_false (lrg_lightmap_save (lightmap, unwritable, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_clear_error (&error);

    g_unlink (garbage);
    g_rmdir (dir);
}

static void
test_lightmap_upload_headless (void)
{
    g_autoptr(LrgLightmap) lightmap = NULL;

    if (IsWindowReady ())
    {
        g_test_skip ("A window is open; this checks the headless path");
        return;
    }

    lightmap = lrg_lightmap_new (4, 4);
    g_assert_true (lrg_lightmap_is_dirty (lightmap));

    /* No GL context: nothing is uploaded and the pixels stay pending */
    lrg_lightmap_upload (lightmap);
    g_assert_true (lrg_lightmap_is_dirty (lightmap));
    g_assert_null (lrg_lightmap_get_texture (lightmap));
    g_assert_cmpuint (lrg_lightmap_get_texture_id (lightmap), ==, 0);
}

/* ==========================================================================
 * Lighting manager culling (headless maths)
 * ========================================================================== */

static LrgLight2D *
add_point_light (LrgLightingManager *manager,
                 gfloat              x,
                 gfloat              y,
                 gfloat              radius)
{
    g_autoptr(LrgPointLight2D) light = NULL;

    light = lrg_point_light2d_new ();
    lrg_light2d_set_position (LRG_LIGHT2D (light), x, y);
    lrg_point_light2d_set_radius (light, radius);
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (light));
    return LRG_LIGHT2D (light);  /* the manager holds a reference */
}

static void
test_lighting_manager_culling (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgDirectionalLight2D) sun = NULL;
    g_autoptr(GPtrArray) visible = NULL;
    LrgLight2D *inside;
    LrgLight2D *outside;
    LrgLight2D *edge;
    LrgLight2D *disabled;

    manager = lrg_lighting_manager_new ();
    lrg_lighting_manager_set_viewport (manager, 0.0f, 0.0f, 800.0f, 600.0f);

    inside = add_point_light (manager, 400.0f, 300.0f, 50.0f);
    outside = add_point_light (manager, 2000.0f, 300.0f, 100.0f);
    /* centre off-screen, but the radius reaches 10 units into view */
    edge = add_point_light (manager, -90.0f, 300.0f, 100.0f);
    disabled = add_point_light (manager, 100.0f, 100.0f, 50.0f);
    lrg_light2d_set_enabled (disabled, FALSE);

    sun = lrg_directional_light2d_new ();
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (sun));

    visible = lrg_lighting_manager_get_visible_lights (manager);
    g_assert_cmpuint (visible->len, ==, 3);
    g_assert_true (g_ptr_array_find (visible, inside, NULL));
    g_assert_true (g_ptr_array_find (visible, edge, NULL));
    g_assert_true (g_ptr_array_find (visible, sun, NULL));
    g_assert_false (g_ptr_array_find (visible, outside, NULL));
    g_assert_false (g_ptr_array_find (visible, disabled, NULL));
    g_assert_cmpuint (lrg_lighting_manager_get_visible_light_count (manager), ==, 3);

    /* Scrolling the viewport changes the set */
    lrg_lighting_manager_set_viewport (manager, 1500.0f, 0.0f, 800.0f, 600.0f);
    g_assert_cmpuint (lrg_lighting_manager_get_visible_light_count (manager), ==, 2);

    /* render() culls even without a window, and reports what it drew */
    lrg_lighting_manager_render (manager);
    g_assert_cmpuint (lrg_lighting_manager_get_rendered_light_count (manager), ==, 2);
}

static void
test_lighting_manager_viewport_from_camera (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgCamera2D) camera = NULL;
    gfloat x, y, w, h;

    manager = lrg_lighting_manager_new ();
    camera = lrg_camera2d_new ();

    /* Centred camera at 2x zoom: shows half the screen size in world units */
    lrg_camera2d_set_target_xy (camera, 100.0f, 50.0f);
    lrg_camera2d_set_offset_xy (camera, 400.0f, 300.0f);
    lrg_camera2d_set_zoom (camera, 2.0f);
    lrg_camera2d_set_rotation (camera, 0.0f);

    lrg_lighting_manager_set_viewport_from_camera (manager, camera, 800.0f, 600.0f);
    lrg_lighting_manager_get_viewport (manager, &x, &y, &w, &h);
    g_assert_cmpfloat_with_epsilon (x, -100.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (y, -100.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (w, 400.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (h, 300.0f, 1e-3);

    /* A quarter turn swaps the box's sides around the same centre */
    lrg_camera2d_set_rotation (camera, 90.0f);
    lrg_lighting_manager_set_viewport_from_camera (manager, camera, 800.0f, 600.0f);
    lrg_lighting_manager_get_viewport (manager, &x, &y, &w, &h);
    g_assert_cmpfloat_with_epsilon (w, 300.0f, 1e-2);
    g_assert_cmpfloat_with_epsilon (h, 400.0f, 1e-2);
    g_assert_cmpfloat_with_epsilon (x + w * 0.5f, 100.0f, 1e-2);
    g_assert_cmpfloat_with_epsilon (y + h * 0.5f, 50.0f, 1e-2);

    /* A light just outside the camera's view is culled, one inside kept */
    lrg_camera2d_set_rotation (camera, 0.0f);
    lrg_lighting_manager_set_viewport_from_camera (manager, camera, 800.0f, 600.0f);
    add_point_light (manager, 350.0f, 50.0f, 20.0f);   /* x - r > 300 */
    add_point_light (manager, 290.0f, 50.0f, 20.0f);
    g_assert_cmpuint (lrg_lighting_manager_get_visible_light_count (manager), ==, 1);
}

static void
test_lighting_manager_lightmap_bounds (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgLightmap) lightmap = NULL;
    gfloat x, y, w, h;

    manager = lrg_lighting_manager_new ();

    /* Without a lightmap the resolved size is 0 */
    lrg_lighting_manager_get_lightmap_bounds (manager, &x, &y, &w, &h);
    g_assert_cmpfloat (w, ==, 0.0f);
    g_assert_cmpfloat (h, ==, 0.0f);

    /* Default: origin, one world unit per texel */
    lightmap = lrg_lightmap_new (32, 16);
    lrg_lighting_manager_set_lightmap (manager, lightmap);
    lrg_lighting_manager_get_lightmap_bounds (manager, &x, &y, &w, &h);
    g_assert_cmpfloat (x, ==, 0.0f);
    g_assert_cmpfloat (y, ==, 0.0f);
    g_assert_cmpfloat (w, ==, 32.0f);
    g_assert_cmpfloat (h, ==, 16.0f);

    lrg_lighting_manager_set_lightmap_bounds (manager, -10.0f, 5.0f, 640.0f, 320.0f);
    lrg_lighting_manager_get_lightmap_bounds (manager, &x, &y, &w, &h);
    g_assert_cmpfloat (x, ==, -10.0f);
    g_assert_cmpfloat (y, ==, 5.0f);
    g_assert_cmpfloat (w, ==, 640.0f);
    g_assert_cmpfloat (h, ==, 320.0f);

    /* No window: compositing is a safe no-op */
    lrg_lighting_manager_composite (manager);
    g_assert_null (lrg_lighting_manager_get_light_target (manager));
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Shadow Edge Tests */
    g_test_add_func ("/lighting/shadow-edge/boxed", test_shadow_edge_boxed);

    /* Point Light Tests */
    g_test_add_func ("/lighting/point-light/new", test_point_light_new);
    g_test_add_func ("/lighting/point-light/position", test_point_light_position);
    g_test_add_func ("/lighting/point-light/color", test_point_light_color);
    g_test_add_func ("/lighting/point-light/intensity", test_point_light_intensity);
    g_test_add_func ("/lighting/point-light/radius", test_point_light_radius);
    g_test_add_func ("/lighting/point-light/flicker", test_point_light_flicker);
    g_test_add_func ("/lighting/point-light/enabled", test_point_light_enabled);

    /* Spot Light Tests */
    g_test_add_func ("/lighting/spot-light/new", test_spot_light_new);
    g_test_add_func ("/lighting/spot-light/cone", test_spot_light_cone);
    g_test_add_func ("/lighting/spot-light/direction", test_spot_light_direction);
    g_test_add_func ("/lighting/spot-light/radius", test_spot_light_radius);

    /* Directional Light Tests */
    g_test_add_func ("/lighting/directional-light/new", test_directional_light_new);
    g_test_add_func ("/lighting/directional-light/direction", test_directional_light_direction);
    g_test_add_func ("/lighting/directional-light/shadow-length", test_directional_light_shadow_length);

    /* Shadow Map Tests */
    g_test_add_func ("/lighting/shadow-map/new", test_shadow_map_new);
    g_test_add_func ("/lighting/shadow-map/resize", test_shadow_map_resize);

    /* Lightmap Tests */
    g_test_add_func ("/lighting/lightmap/new", test_lightmap_new);
    g_test_add_func ("/lighting/lightmap/pixel", test_lightmap_pixel);
    g_test_add_func ("/lighting/lightmap/clear", test_lightmap_clear);
    g_test_add_func ("/lighting/lightmap/save-load-roundtrip", test_lightmap_save_load_roundtrip);
    g_test_add_func ("/lighting/lightmap/load-errors", test_lightmap_load_errors);
    g_test_add_func ("/lighting/lightmap/upload-headless", test_lightmap_upload_headless);

    /* Light Probe Tests */
    g_test_add_func ("/lighting/light-probe/new", test_light_probe_new);
    g_test_add_func ("/lighting/light-probe/position", test_light_probe_position);
    g_test_add_func ("/lighting/light-probe/radius", test_light_probe_radius);
    g_test_add_func ("/lighting/light-probe/sample", test_light_probe_sample);

    /* Lighting Manager Tests */
    g_test_add_func ("/lighting/manager/new", test_lighting_manager_new);
    g_test_add_func ("/lighting/manager/add-remove-light", test_lighting_manager_add_remove_light);
    g_test_add_func ("/lighting/manager/ambient", test_lighting_manager_ambient);
    g_test_add_func ("/lighting/manager/shadows", test_lighting_manager_shadows);
    g_test_add_func ("/lighting/manager/blend-mode", test_lighting_manager_blend_mode);
    g_test_add_func ("/lighting/manager/lightmap", test_lighting_manager_lightmap);
    g_test_add_func ("/lighting/manager/viewport", test_lighting_manager_viewport);
    g_test_add_func ("/lighting/manager/update", test_lighting_manager_update);
    g_test_add_func ("/lighting/manager/culling", test_lighting_manager_culling);
    g_test_add_func ("/lighting/manager/viewport-from-camera", test_lighting_manager_viewport_from_camera);
    g_test_add_func ("/lighting/manager/lightmap-bounds", test_lighting_manager_lightmap_bounds);

    /* Light Falloff Tests */
    g_test_add_func ("/lighting/light/falloff", test_light_falloff);

    /* Shadow Method Tests */
    g_test_add_func ("/lighting/light/shadow-method", test_shadow_method);

    return g_test_run ();
}
