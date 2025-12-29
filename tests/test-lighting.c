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
#include <math.h>

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

    /* Light Falloff Tests */
    g_test_add_func ("/lighting/light/falloff", test_light_falloff);

    /* Shadow Method Tests */
    g_test_add_func ("/lighting/light/shadow-method", test_shadow_method);

    return g_test_run ();
}
