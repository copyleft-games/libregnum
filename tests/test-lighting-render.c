/* test-lighting-render.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GPU tests for the 2D lighting pass: LrgLightingManager renders the
 * visible lights into its light target, a texture covering the
 * viewport, and composites it over the scene.  Each test reads pixels
 * back in a hidden window and is skipped without a display (the
 * culling maths is covered headlessly in test-lighting.c).
 *
 * Checks are chosen to hold whichever way up the read-back is (render
 * textures are stored bottom-up): light centred in the target, left
 * versus right halves.
 */

#include <glib.h>
#include <libregnum.h>
#include <raylib.h>

#define TARGET_SIZE (64)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0'))
        return FALSE;

    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (8, 8, "lrg-lighting-render-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

#define SKIP_WITHOUT_GRAPHICS() \
    do { \
        if (!graphics_available) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/* A manager over a TARGET_SIZE square viewport at the origin, with a
 * dim grey ambient. */
static LrgLightingManager *
new_manager (void)
{
    LrgLightingManager *manager;

    manager = lrg_lighting_manager_new ();
    lrg_lighting_manager_set_viewport (manager, 0.0f, 0.0f,
                                       (gfloat) TARGET_SIZE, (gfloat) TARGET_SIZE);
    lrg_lighting_manager_set_ambient_color (manager, 40, 40, 40);
    lrg_lighting_manager_set_ambient_intensity (manager, 0.5f);
    return manager;
}

/* read_target:
 * Reads the manager's light target back as an RGBA8 image. */
static GrlImage *
read_target (LrgLightingManager *manager)
{
    GrlRenderTexture *target;
    g_autoptr(GrlTexture) texture = NULL;

    target = lrg_lighting_manager_get_light_target (manager);
    g_assert_nonnull (target);
    texture = grl_render_texture_get_texture (target);
    g_assert_nonnull (texture);
    return grl_texture_to_image (texture);
}

static guint
red_at (GrlImage *image,
        gint      x,
        gint      y)
{
    guint8 *pixels;

    pixels = grl_image_get_pixels (image, NULL);
    return pixels[(y * grl_image_get_width (image) + x) * 4];
}

static void
test_render_ambient_and_point (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgPointLight2D) light = NULL;
    g_autoptr(GrlImage) image = NULL;
    guint centre, corner;

    SKIP_WITHOUT_GRAPHICS ();

    manager = new_manager ();
    light = lrg_point_light2d_new ();
    lrg_light2d_set_position (LRG_LIGHT2D (light), 32.0f, 32.0f);
    lrg_light2d_set_color (LRG_LIGHT2D (light), 255, 0, 0);
    lrg_point_light2d_set_radius (light, 20.0f);
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (light));

    lrg_lighting_manager_render (manager);
    g_assert_cmpuint (lrg_lighting_manager_get_rendered_light_count (manager), ==, 1);
    g_assert_cmpuint (lrg_lighting_manager_get_light_texture_id (manager), !=, 0);

    image = read_target (manager);
    g_assert_cmpint (grl_image_get_width (image), ==, TARGET_SIZE);

    /* Corners: ambient only (40 * 0.5); centre: ambient plus the light */
    corner = red_at (image, 1, 1);
    centre = red_at (image, 32, 32);
    g_assert_cmpuint (corner, >=, 18);
    g_assert_cmpuint (corner, <=, 22);
    g_assert_cmpuint (centre, >, 200);

    /* Linear falloff: halfway out is dimmer than the centre, brighter
     * than the edge */
    g_assert_cmpuint (red_at (image, 42, 32), <, centre);
    g_assert_cmpuint (red_at (image, 42, 32), >, corner + 40);
}

static void
test_render_culled_light_not_drawn (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgPointLight2D) light = NULL;
    g_autoptr(GrlImage) image = NULL;

    SKIP_WITHOUT_GRAPHICS ();

    manager = new_manager ();
    light = lrg_point_light2d_new ();
    lrg_light2d_set_position (LRG_LIGHT2D (light), 500.0f, 32.0f);
    lrg_point_light2d_set_radius (light, 20.0f);
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (light));

    lrg_lighting_manager_render (manager);
    g_assert_cmpuint (lrg_lighting_manager_get_rendered_light_count (manager), ==, 0);

    image = read_target (manager);
    g_assert_cmpuint (red_at (image, 63, 32), <=, 22);
}

static void
test_render_scrolled_viewport (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgPointLight2D) light = NULL;
    g_autoptr(GrlImage) image = NULL;

    SKIP_WITHOUT_GRAPHICS ();

    /* The light pass draws in world units: a light at world (1032, 532)
     * lands in the middle of a viewport starting at (1000, 500). */
    manager = new_manager ();
    lrg_lighting_manager_set_viewport (manager, 1000.0f, 500.0f,
                                       (gfloat) TARGET_SIZE, (gfloat) TARGET_SIZE);
    light = lrg_point_light2d_new ();
    lrg_light2d_set_position (LRG_LIGHT2D (light), 1032.0f, 532.0f);
    lrg_point_light2d_set_radius (light, 10.0f);
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (light));

    lrg_lighting_manager_render (manager);
    image = read_target (manager);
    g_assert_cmpuint (red_at (image, 32, 32), >, 200);
    g_assert_cmpuint (red_at (image, 5, 32), <=, 22);
}

static void
test_render_spot_and_directional (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgSpotLight2D) spot = NULL;
    g_autoptr(LrgDirectionalLight2D) sun = NULL;
    g_autoptr(GrlImage) image = NULL;
    guint lit, dark, filled;

    SKIP_WITHOUT_GRAPHICS ();

    /* Spot pointing along +X from the centre: right lit, left not */
    manager = new_manager ();
    spot = lrg_spot_light2d_new ();
    lrg_light2d_set_position (LRG_LIGHT2D (spot), 32.0f, 32.0f);
    lrg_spot_light2d_set_radius (spot, 30.0f);
    lrg_spot_light2d_set_direction (spot, 0.0f);
    lrg_spot_light2d_set_angle (spot, 60.0f);
    lrg_spot_light2d_set_inner_angle (spot, 30.0f);
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (spot));

    lrg_lighting_manager_render (manager);
    image = read_target (manager);
    lit = red_at (image, 40, 32);
    dark = red_at (image, 24, 32);
    g_assert_cmpuint (lit, >, dark + 60);
    g_assert_cmpuint (dark, <=, 22);
    g_clear_object (&image);

    /* A directional light lifts every pixel */
    sun = lrg_directional_light2d_new ();
    lrg_light2d_set_intensity (LRG_LIGHT2D (sun), 0.25f);
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (sun));
    lrg_lighting_manager_render (manager);
    image = read_target (manager);
    filled = red_at (image, 2, 2);
    g_assert_cmpuint (filled, >=, 20 + 60);
    g_assert_cmpuint (red_at (image, 61, 61), ==, filled);
}

static void
test_render_lightmap (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgLightmap) lightmap = NULL;
    g_autoptr(GrlImage) image = NULL;

    SKIP_WITHOUT_GRAPHICS ();

    /* A 2x1 lightmap stretched over the viewport: bright left, black right */
    manager = new_manager ();
    lightmap = lrg_lightmap_new (2, 1);
    lrg_lightmap_set_pixel (lightmap, 0, 0, 200, 200, 200);
    lrg_lightmap_set_pixel (lightmap, 1, 0, 0, 0, 0);
    lrg_lighting_manager_set_lightmap (manager, lightmap);
    lrg_lighting_manager_set_lightmap_bounds (manager, 0.0f, 0.0f,
                                              (gfloat) TARGET_SIZE, (gfloat) TARGET_SIZE);

    lrg_lighting_manager_render (manager);
    g_assert_false (lrg_lightmap_is_dirty (lightmap));
    g_assert_nonnull (lrg_lightmap_get_texture (lightmap));

    image = read_target (manager);
    /* bilinear filtering blends the middle; the outer columns are clean */
    g_assert_cmpuint (red_at (image, 1, 32), >, 180);
    g_assert_cmpuint (red_at (image, 62, 32), <=, 30);
}

static void
test_composite_multiply (void)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgPointLight2D) light = NULL;
    g_autoptr(GrlRenderTexture) scene = NULL;
    g_autoptr(GrlTexture) texture = NULL;
    g_autoptr(GrlImage) image = NULL;
    g_autoptr(GrlColor) white = NULL;

    SKIP_WITHOUT_GRAPHICS ();

    manager = new_manager ();
    light = lrg_point_light2d_new ();
    lrg_light2d_set_position (LRG_LIGHT2D (light), 32.0f, 32.0f);
    lrg_point_light2d_set_radius (light, 16.0f);
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (light));
    lrg_lighting_manager_render (manager);

    /* A white "scene", then the lighting multiplied over it.  The
     * viewport is at the origin at scene scale, so no camera needed. */
    scene = grl_render_texture_new (TARGET_SIZE, TARGET_SIZE);
    white = grl_color_new (255, 255, 255, 255);
    grl_render_texture_begin (scene);
    grl_draw_clear_background (white);
    lrg_lighting_manager_composite (manager);
    grl_render_texture_end (scene);

    texture = grl_render_texture_get_texture (scene);
    image = grl_texture_to_image (texture);
    /* white x ambient in the corner, white x (ambient + light) centred */
    g_assert_cmpuint (red_at (image, 1, 1), <=, 22);
    g_assert_cmpuint (red_at (image, 32, 32), >, 200);
}

/*
 * 2D shadows: a red point light in the middle and a wall (x 40..44, full
 * height, so the check holds whichever way up the read-back is) to its
 * right.  Left of the light is lit, behind the wall only the ambient
 * remains, the wall itself stays lit (occluders are not self-shadowing by
 * default).  Run hard (1 sample) and soft (8 samples).
 */
static void
run_shadow_wall (gfloat softness)
{
    g_autoptr(LrgLightingManager) manager = NULL;
    g_autoptr(LrgPointLight2D) light = NULL;
    g_autoptr(LrgShadowOccluder2D) wall = NULL;
    g_autoptr(GrlImage) image = NULL;
    guint behind;
    guint front;

    manager = new_manager ();
    light = lrg_point_light2d_new ();
    lrg_light2d_set_position (LRG_LIGHT2D (light), 32.0f, 32.0f);
    lrg_light2d_set_color (LRG_LIGHT2D (light), 255, 0, 0);
    lrg_light2d_set_shadow_softness (LRG_LIGHT2D (light), softness);
    lrg_light2d_set_shadow_samples (LRG_LIGHT2D (light), 8);
    lrg_light2d_set_falloff (LRG_LIGHT2D (light), LRG_LIGHT_FALLOFF_NONE);
    lrg_point_light2d_set_radius (light, 30.0f);
    lrg_lighting_manager_add_light (manager, LRG_LIGHT2D (light));

    wall = lrg_shadow_occluder2d_new ();
    lrg_shadow_occluder2d_add_rect (wall, 40.0f, -8.0f, 4.0f, 80.0f);
    lrg_lighting_manager_add_shadow_caster (manager, LRG_SHADOW_CASTER (wall));

    lrg_lighting_manager_render (manager);
    g_assert_cmpuint (lrg_lighting_manager_get_shadowed_light_count (manager), ==, 1);

    image = read_target (manager);
    front = red_at (image, 24, 32);
    behind = red_at (image, 54, 32);
    g_assert_cmpuint (front, >, 200);
    g_assert_cmpuint (behind, >=, 18);
    g_assert_cmpuint (behind, <=, 24);
    g_assert_cmpuint (red_at (image, 42, 32), >, 200);
}

static void
test_render_shadow_hard (void)
{
    SKIP_WITHOUT_GRAPHICS ();
    run_shadow_wall (0.0f);
}

static void
test_render_shadow_soft (void)
{
    SKIP_WITHOUT_GRAPHICS ();
    run_shadow_wall (0.3f);
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);

    graphics_available = init_graphics_context ();

    g_test_add_func ("/lighting-render/pass/ambient-and-point", test_render_ambient_and_point);
    g_test_add_func ("/lighting-render/pass/culled-light", test_render_culled_light_not_drawn);
    g_test_add_func ("/lighting-render/pass/scrolled-viewport", test_render_scrolled_viewport);
    g_test_add_func ("/lighting-render/pass/spot-and-directional", test_render_spot_and_directional);
    g_test_add_func ("/lighting-render/pass/lightmap", test_render_lightmap);
    g_test_add_func ("/lighting-render/composite/multiply", test_composite_multiply);
    g_test_add_func ("/lighting-render/shadow/hard", test_render_shadow_hard);
    g_test_add_func ("/lighting-render/shadow/soft", test_render_shadow_soft);

    result = g_test_run ();

    g_clear_object (&test_window);
    return result;
}
