/* test-graphics.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the graphics module (LrgDrawable, LrgCamera, LrgRenderer).
 *
 * Note: Full window and rendering tests require a graphics context.
 * These tests focus on the type system, properties, and interfaces
 * that can be tested without creating an actual window.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgCamera2D *camera2d;
    LrgCamera3D *camera3d;
} CameraFixture;

static void
camera_fixture_set_up (CameraFixture *fixture,
                       gconstpointer  user_data)
{
    fixture->camera2d = lrg_camera2d_new ();
    fixture->camera3d = lrg_camera3d_new ();
    g_assert_nonnull (fixture->camera2d);
    g_assert_nonnull (fixture->camera3d);
}

static void
camera_fixture_tear_down (CameraFixture *fixture,
                          gconstpointer  user_data)
{
    g_clear_object (&fixture->camera2d);
    g_clear_object (&fixture->camera3d);
}

/* ==========================================================================
 * Mock Drawable for Interface Testing
 * ========================================================================== */

#define TEST_TYPE_DRAWABLE (test_drawable_get_type ())
G_DECLARE_FINAL_TYPE (TestDrawable, test_drawable, TEST, DRAWABLE, GObject)

struct _TestDrawable
{
    GObject parent_instance;
    gint    draw_count;
    gfloat  last_delta;
};

static void test_drawable_interface_init (LrgDrawableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestDrawable, test_drawable, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DRAWABLE,
                                                test_drawable_interface_init))

static void
test_drawable_draw_impl (LrgDrawable *drawable,
                         gfloat       delta)
{
    TestDrawable *self = TEST_DRAWABLE (drawable);
    self->draw_count++;
    self->last_delta = delta;
}

static void
test_drawable_get_bounds_impl (LrgDrawable  *drawable,
                               GrlRectangle *out_bounds)
{
    out_bounds->x = 10.0f;
    out_bounds->y = 20.0f;
    out_bounds->width = 100.0f;
    out_bounds->height = 50.0f;
}

static void
test_drawable_interface_init (LrgDrawableInterface *iface)
{
    iface->draw = test_drawable_draw_impl;
    iface->get_bounds = test_drawable_get_bounds_impl;
}

static void
test_drawable_init (TestDrawable *self)
{
    self->draw_count = 0;
    self->last_delta = 0.0f;
}

static void
test_drawable_class_init (TestDrawableClass *klass)
{
}

static TestDrawable *
test_drawable_new (void)
{
    return g_object_new (TEST_TYPE_DRAWABLE, NULL);
}

/* ==========================================================================
 * Test Cases - LrgDrawable Interface
 * ========================================================================== */

static void
test_drawable_type (void)
{
    GType type;

    type = LRG_TYPE_DRAWABLE;
    g_assert_true (G_TYPE_IS_INTERFACE (type));
    g_assert_true (g_type_is_a (type, G_TYPE_INTERFACE));
}

static void
test_drawable_mock_implements (void)
{
    g_autoptr(TestDrawable) drawable = test_drawable_new ();

    g_assert_nonnull (drawable);
    g_assert_true (LRG_IS_DRAWABLE (drawable));
}

static void
test_drawable_draw (void)
{
    g_autoptr(TestDrawable) drawable = test_drawable_new ();

    g_assert_cmpint (drawable->draw_count, ==, 0);

    lrg_drawable_draw (LRG_DRAWABLE (drawable), 0.016f);
    g_assert_cmpint (drawable->draw_count, ==, 1);
    g_assert_cmpfloat_with_epsilon (drawable->last_delta, 0.016f, 0.0001f);

    lrg_drawable_draw (LRG_DRAWABLE (drawable), 0.033f);
    g_assert_cmpint (drawable->draw_count, ==, 2);
    g_assert_cmpfloat_with_epsilon (drawable->last_delta, 0.033f, 0.0001f);
}

static void
test_drawable_get_bounds (void)
{
    g_autoptr(TestDrawable) drawable = test_drawable_new ();
    GrlRectangle bounds;

    lrg_drawable_get_bounds (LRG_DRAWABLE (drawable), &bounds);

    g_assert_cmpfloat_with_epsilon (bounds.x, 10.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (bounds.y, 20.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (bounds.width, 100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (bounds.height, 50.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - LrgCamera Base
 * ========================================================================== */

static void
test_camera_type (void)
{
    GType type;

    type = LRG_TYPE_CAMERA;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (G_TYPE_IS_ABSTRACT (type));
}

static void
test_camera2d_is_camera (CameraFixture *fixture,
                         gconstpointer  user_data)
{
    g_assert_true (LRG_IS_CAMERA (fixture->camera2d));
    g_assert_true (LRG_IS_CAMERA2D (fixture->camera2d));
}

static void
test_camera3d_is_camera (CameraFixture *fixture,
                         gconstpointer  user_data)
{
    g_assert_true (LRG_IS_CAMERA (fixture->camera3d));
    g_assert_true (LRG_IS_CAMERA3D (fixture->camera3d));
}

/* ==========================================================================
 * Test Cases - LrgCamera2D
 * ========================================================================== */

static void
test_camera2d_type (void)
{
    GType type;

    type = LRG_TYPE_CAMERA2D;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA));
}

static void
test_camera2d_new (void)
{
    g_autoptr(LrgCamera2D) camera = lrg_camera2d_new ();

    g_assert_nonnull (camera);
    g_assert_true (LRG_IS_CAMERA2D (camera));
}

static void
test_camera2d_default_values (CameraFixture *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(GrlVector2) offset = NULL;
    g_autoptr(GrlVector2) target = NULL;
    gfloat rotation;
    gfloat zoom;

    offset = lrg_camera2d_get_offset (fixture->camera2d);
    target = lrg_camera2d_get_target (fixture->camera2d);
    rotation = lrg_camera2d_get_rotation (fixture->camera2d);
    zoom = lrg_camera2d_get_zoom (fixture->camera2d);

    /* Default offset is 0,0 */
    g_assert_cmpfloat_with_epsilon (offset->x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (offset->y, 0.0f, 0.0001f);

    /* Default target is 0,0 */
    g_assert_cmpfloat_with_epsilon (target->x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (target->y, 0.0f, 0.0001f);

    /* Default rotation is 0 */
    g_assert_cmpfloat_with_epsilon (rotation, 0.0f, 0.0001f);

    /* Default zoom is 1 */
    g_assert_cmpfloat_with_epsilon (zoom, 1.0f, 0.0001f);
}

static void
test_camera2d_set_offset (CameraFixture *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(GrlVector2) offset = NULL;

    lrg_camera2d_set_offset_xy (fixture->camera2d, 100.0f, 200.0f);
    offset = lrg_camera2d_get_offset (fixture->camera2d);

    g_assert_cmpfloat_with_epsilon (offset->x, 100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (offset->y, 200.0f, 0.0001f);
}

static void
test_camera2d_set_target (CameraFixture *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(GrlVector2) target = NULL;

    lrg_camera2d_set_target_xy (fixture->camera2d, 50.0f, 75.0f);
    target = lrg_camera2d_get_target (fixture->camera2d);

    g_assert_cmpfloat_with_epsilon (target->x, 50.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (target->y, 75.0f, 0.0001f);
}

static void
test_camera2d_set_rotation (CameraFixture *fixture,
                            gconstpointer  user_data)
{
    gfloat rotation;

    lrg_camera2d_set_rotation (fixture->camera2d, 45.0f);
    rotation = lrg_camera2d_get_rotation (fixture->camera2d);

    g_assert_cmpfloat_with_epsilon (rotation, 45.0f, 0.0001f);
}

static void
test_camera2d_set_zoom (CameraFixture *fixture,
                        gconstpointer  user_data)
{
    gfloat zoom;

    lrg_camera2d_set_zoom (fixture->camera2d, 2.0f);
    zoom = lrg_camera2d_get_zoom (fixture->camera2d);

    g_assert_cmpfloat_with_epsilon (zoom, 2.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - LrgCamera3D
 * ========================================================================== */

static void
test_camera3d_type (void)
{
    GType type;

    type = LRG_TYPE_CAMERA3D;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA));
}

static void
test_camera3d_new (void)
{
    g_autoptr(LrgCamera3D) camera = lrg_camera3d_new ();

    g_assert_nonnull (camera);
    g_assert_true (LRG_IS_CAMERA3D (camera));
}

static void
test_camera3d_default_values (CameraFixture *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(GrlVector3) position = NULL;
    g_autoptr(GrlVector3) target = NULL;
    g_autoptr(GrlVector3) up = NULL;
    gfloat fovy;
    LrgProjectionType projection;

    position = lrg_camera3d_get_position (fixture->camera3d);
    target = lrg_camera3d_get_target (fixture->camera3d);
    up = lrg_camera3d_get_up (fixture->camera3d);
    fovy = lrg_camera3d_get_fovy (fixture->camera3d);
    projection = lrg_camera3d_get_projection (fixture->camera3d);

    /* Default position is 0,10,10 */
    g_assert_cmpfloat_with_epsilon (position->x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (position->y, 10.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (position->z, 10.0f, 0.0001f);

    /* Default target is 0,0,0 */
    g_assert_cmpfloat_with_epsilon (target->x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (target->y, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (target->z, 0.0f, 0.0001f);

    /* Default up is 0,1,0 */
    g_assert_cmpfloat_with_epsilon (up->x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (up->y, 1.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (up->z, 0.0f, 0.0001f);

    /* Default fovy is 45 degrees */
    g_assert_cmpfloat_with_epsilon (fovy, 45.0f, 0.0001f);

    /* Default projection is perspective */
    g_assert_cmpint (projection, ==, LRG_PROJECTION_PERSPECTIVE);
}

static void
test_camera3d_set_position (CameraFixture *fixture,
                            gconstpointer  user_data)
{
    g_autoptr(GrlVector3) position = NULL;

    lrg_camera3d_set_position_xyz (fixture->camera3d, 5.0f, 10.0f, 15.0f);
    position = lrg_camera3d_get_position (fixture->camera3d);

    g_assert_cmpfloat_with_epsilon (position->x, 5.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (position->y, 10.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (position->z, 15.0f, 0.0001f);
}

static void
test_camera3d_set_target (CameraFixture *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(GrlVector3) target = NULL;

    lrg_camera3d_set_target_xyz (fixture->camera3d, 1.0f, 2.0f, 3.0f);
    target = lrg_camera3d_get_target (fixture->camera3d);

    g_assert_cmpfloat_with_epsilon (target->x, 1.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (target->y, 2.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (target->z, 3.0f, 0.0001f);
}

static void
test_camera3d_set_fovy (CameraFixture *fixture,
                        gconstpointer  user_data)
{
    gfloat fovy;

    lrg_camera3d_set_fovy (fixture->camera3d, 60.0f);
    fovy = lrg_camera3d_get_fovy (fixture->camera3d);

    g_assert_cmpfloat_with_epsilon (fovy, 60.0f, 0.0001f);
}

static void
test_camera3d_set_projection (CameraFixture *fixture,
                              gconstpointer  user_data)
{
    LrgProjectionType projection;

    /* Test orthographic */
    lrg_camera3d_set_projection (fixture->camera3d, LRG_PROJECTION_ORTHOGRAPHIC);
    projection = lrg_camera3d_get_projection (fixture->camera3d);
    g_assert_cmpint (projection, ==, LRG_PROJECTION_ORTHOGRAPHIC);

    /* Test perspective */
    lrg_camera3d_set_projection (fixture->camera3d, LRG_PROJECTION_PERSPECTIVE);
    projection = lrg_camera3d_get_projection (fixture->camera3d);
    g_assert_cmpint (projection, ==, LRG_PROJECTION_PERSPECTIVE);
}

/* ==========================================================================
 * Test Cases - LrgCameraIsometric
 * ========================================================================== */

static void
test_camera_isometric_type (void)
{
    GType type;

    type = LRG_TYPE_CAMERA_ISOMETRIC;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA3D));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA));
}

static void
test_camera_isometric_new (void)
{
    g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();

    g_assert_nonnull (camera);
    g_assert_true (LRG_IS_CAMERA_ISOMETRIC (camera));
    g_assert_true (LRG_IS_CAMERA3D (camera));
    g_assert_true (LRG_IS_CAMERA (camera));
}

static void
test_camera_isometric_default_values (void)
{
    g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();
    gfloat tile_width;
    gfloat tile_height;
    gfloat height_scale;
    gfloat zoom;
    LrgProjectionType projection;

    tile_width = lrg_camera_isometric_get_tile_width (camera);
    tile_height = lrg_camera_isometric_get_tile_height (camera);
    height_scale = lrg_camera_isometric_get_height_scale (camera);
    zoom = lrg_camera_isometric_get_zoom (camera);
    projection = lrg_camera3d_get_projection (LRG_CAMERA3D (camera));

    /* Default tile size is 64x32 */
    g_assert_cmpfloat_with_epsilon (tile_width, 64.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (tile_height, 32.0f, 0.0001f);

    /* Default height scale is 0.5 */
    g_assert_cmpfloat_with_epsilon (height_scale, 0.5f, 0.0001f);

    /* Default zoom is 1.0 */
    g_assert_cmpfloat_with_epsilon (zoom, 1.0f, 0.0001f);

    /* Isometric cameras are always orthographic */
    g_assert_cmpint (projection, ==, LRG_PROJECTION_ORTHOGRAPHIC);
}

static void
test_camera_isometric_set_tile_width (void)
{
    g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();
    gfloat tile_width;

    lrg_camera_isometric_set_tile_width (camera, 128.0f);
    tile_width = lrg_camera_isometric_get_tile_width (camera);

    g_assert_cmpfloat_with_epsilon (tile_width, 128.0f, 0.0001f);
}

static void
test_camera_isometric_set_tile_height (void)
{
    g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();
    gfloat tile_height;

    lrg_camera_isometric_set_tile_height (camera, 64.0f);
    tile_height = lrg_camera_isometric_get_tile_height (camera);

    g_assert_cmpfloat_with_epsilon (tile_height, 64.0f, 0.0001f);
}

static void
test_camera_isometric_set_height_scale (void)
{
    g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();
    gfloat height_scale;

    lrg_camera_isometric_set_height_scale (camera, 0.75f);
    height_scale = lrg_camera_isometric_get_height_scale (camera);

    g_assert_cmpfloat_with_epsilon (height_scale, 0.75f, 0.0001f);
}

static void
test_camera_isometric_set_zoom (void)
{
    g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();
    gfloat zoom;

    lrg_camera_isometric_set_zoom (camera, 2.0f);
    zoom = lrg_camera_isometric_get_zoom (camera);

    g_assert_cmpfloat_with_epsilon (zoom, 2.0f, 0.0001f);
}

static void
test_camera_isometric_focus_on (void)
{
    g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();
    g_autoptr(GrlVector3) target = NULL;

    /* Focus on a specific world position */
    lrg_camera_isometric_focus_on (camera, 10.0f, 5.0f, 20.0f);
    target = lrg_camera3d_get_target (LRG_CAMERA3D (camera));

    /* Target should be updated */
    g_assert_cmpfloat_with_epsilon (target->x, 10.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (target->y, 5.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (target->z, 20.0f, 0.0001f);
}

static void
test_camera_isometric_tile_conversion (void)
{
    g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();
    gint tile_x;
    gint tile_y;
    gfloat world_x;
    gfloat world_z;

    /* Set specific tile dimensions */
    lrg_camera_isometric_set_tile_width (camera, 64.0f);
    lrg_camera_isometric_set_tile_height (camera, 32.0f);

    /* Test tile_to_world conversion */
    lrg_camera_isometric_tile_to_world (camera, 2, 3, &world_x, &world_z);

    /* Tile (2,3) should map to a specific world position */
    /* With 64x32 tiles: X = 2 * 32 + 16 = 80, Z = 3 * 32 + 16 = 112 */
    g_assert_cmpfloat_with_epsilon (world_x, 80.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (world_z, 112.0f, 0.0001f);

    /* Test world_to_tile conversion (round-trip) */
    lrg_camera_isometric_world_to_tile (camera, world_x, 0.0f, world_z, &tile_x, &tile_y);
    g_assert_cmpint (tile_x, ==, 2);
    g_assert_cmpint (tile_y, ==, 3);
}

/* ==========================================================================
 * Test Cases - LrgCameraTopDown
 * ========================================================================== */

static void
test_camera_topdown_type (void)
{
    GType type;

    type = LRG_TYPE_CAMERA_TOPDOWN;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA2D));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA));
}

static void
test_camera_topdown_new (void)
{
    g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();

    g_assert_nonnull (camera);
    g_assert_true (LRG_IS_CAMERA_TOPDOWN (camera));
    g_assert_true (LRG_IS_CAMERA2D (camera));
    g_assert_true (LRG_IS_CAMERA (camera));
}

static void
test_camera_topdown_default_values (void)
{
    g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();
    gfloat follow_speed;
    gfloat deadzone;
    gboolean bounds_enabled;

    follow_speed = lrg_camera_topdown_get_follow_speed (camera);
    deadzone = lrg_camera_topdown_get_deadzone_radius (camera);
    bounds_enabled = lrg_camera_topdown_get_bounds_enabled (camera);

    g_assert_cmpfloat_with_epsilon (follow_speed, 5.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (deadzone, 20.0f, 0.0001f);
    g_assert_false (bounds_enabled);
}

static void
test_camera_topdown_set_follow_speed (void)
{
    g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();
    gfloat speed;

    lrg_camera_topdown_set_follow_speed (camera, 10.0f);
    speed = lrg_camera_topdown_get_follow_speed (camera);

    g_assert_cmpfloat_with_epsilon (speed, 10.0f, 0.0001f);
}

static void
test_camera_topdown_set_deadzone (void)
{
    g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();
    gfloat radius;

    lrg_camera_topdown_set_deadzone_radius (camera, 50.0f);
    radius = lrg_camera_topdown_get_deadzone_radius (camera);

    g_assert_cmpfloat_with_epsilon (radius, 50.0f, 0.0001f);
}

static void
test_camera_topdown_bounds (void)
{
    g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();
    gfloat min_x, min_y, max_x, max_y;
    gboolean enabled;

    lrg_camera_topdown_set_bounds (camera, 0.0f, 0.0f, 1000.0f, 800.0f);
    lrg_camera_topdown_set_bounds_enabled (camera, TRUE);

    enabled = lrg_camera_topdown_get_bounds_enabled (camera);
    lrg_camera_topdown_get_bounds (camera, &min_x, &min_y, &max_x, &max_y);

    g_assert_true (enabled);
    g_assert_cmpfloat_with_epsilon (min_x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (min_y, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (max_x, 1000.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (max_y, 800.0f, 0.0001f);
}

static void
test_camera_topdown_follow (void)
{
    g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();
    g_autoptr(GrlVector2) target = NULL;

    /* Follow at 100,200 */
    lrg_camera_topdown_follow (camera, 100.0f, 200.0f, 1.0f);
    target = lrg_camera2d_get_target (LRG_CAMERA2D (camera));

    /* After 1 second with high delta, should have moved close to target */
    g_assert_cmpfloat (target->x, >=, 50.0f);
    g_assert_cmpfloat (target->y, >=, 100.0f);
}

static void
test_camera_topdown_shake (void)
{
    g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();

    g_assert_false (lrg_camera_topdown_is_shaking (camera));

    lrg_camera_topdown_shake (camera, 10.0f, 0.5f);
    g_assert_true (lrg_camera_topdown_is_shaking (camera));

    lrg_camera_topdown_stop_shake (camera);
    g_assert_false (lrg_camera_topdown_is_shaking (camera));
}

static void
test_camera_topdown_update_shake (void)
{
    g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();

    /* Start shake with 0.5 second duration */
    lrg_camera_topdown_shake (camera, 10.0f, 0.5f);
    g_assert_true (lrg_camera_topdown_is_shaking (camera));

    /* Manual update should decrement timer - still shaking after 0.25s */
    lrg_camera_topdown_update_shake (camera, 0.25f);
    g_assert_true (lrg_camera_topdown_is_shaking (camera));

    /* After full duration (0.25 + 0.3 = 0.55s > 0.5s), shake should stop */
    lrg_camera_topdown_update_shake (camera, 0.3f);
    g_assert_false (lrg_camera_topdown_is_shaking (camera));
}

/* ==========================================================================
 * Test Cases - LrgCameraSideOn
 * ========================================================================== */

static void
test_camera_sideon_type (void)
{
    GType type;

    type = LRG_TYPE_CAMERA_SIDEON;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA2D));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA));
}

static void
test_camera_sideon_new (void)
{
    g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();

    g_assert_nonnull (camera);
    g_assert_true (LRG_IS_CAMERA_SIDEON (camera));
    g_assert_true (LRG_IS_CAMERA2D (camera));
}

static void
test_camera_sideon_default_values (void)
{
    g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();
    gfloat speed_x, speed_y;
    gfloat dz_width, dz_height;
    gfloat lookahead_distance;
    gfloat vertical_bias;

    speed_x = lrg_camera_sideon_get_follow_speed_x (camera);
    speed_y = lrg_camera_sideon_get_follow_speed_y (camera);
    dz_width = lrg_camera_sideon_get_deadzone_width (camera);
    dz_height = lrg_camera_sideon_get_deadzone_height (camera);
    lookahead_distance = lrg_camera_sideon_get_lookahead_distance (camera);
    vertical_bias = lrg_camera_sideon_get_vertical_bias (camera);

    g_assert_cmpfloat_with_epsilon (speed_x, 8.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (speed_y, 4.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (dz_width, 100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (dz_height, 150.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lookahead_distance, 100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (vertical_bias, 0.3f, 0.0001f);
}

static void
test_camera_sideon_set_follow_speed (void)
{
    g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();
    gfloat speed_x, speed_y;

    lrg_camera_sideon_set_follow_speed_x (camera, 12.0f);
    lrg_camera_sideon_set_follow_speed_y (camera, 6.0f);

    speed_x = lrg_camera_sideon_get_follow_speed_x (camera);
    speed_y = lrg_camera_sideon_get_follow_speed_y (camera);

    g_assert_cmpfloat_with_epsilon (speed_x, 12.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (speed_y, 6.0f, 0.0001f);
}

static void
test_camera_sideon_set_deadzone (void)
{
    g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();
    gfloat width, height;

    lrg_camera_sideon_set_deadzone (camera, 200.0f, 300.0f);

    width = lrg_camera_sideon_get_deadzone_width (camera);
    height = lrg_camera_sideon_get_deadzone_height (camera);

    g_assert_cmpfloat_with_epsilon (width, 200.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (height, 300.0f, 0.0001f);
}

static void
test_camera_sideon_set_lookahead (void)
{
    g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();
    gfloat distance, speed;

    lrg_camera_sideon_set_lookahead_distance (camera, 150.0f);
    lrg_camera_sideon_set_lookahead_speed (camera, 5.0f);

    distance = lrg_camera_sideon_get_lookahead_distance (camera);
    speed = lrg_camera_sideon_get_lookahead_speed (camera);

    g_assert_cmpfloat_with_epsilon (distance, 150.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (speed, 5.0f, 0.0001f);
}

static void
test_camera_sideon_set_vertical_bias (void)
{
    g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();
    gfloat bias;

    lrg_camera_sideon_set_vertical_bias (camera, 0.5f);
    bias = lrg_camera_sideon_get_vertical_bias (camera);

    g_assert_cmpfloat_with_epsilon (bias, 0.5f, 0.0001f);
}

static void
test_camera_sideon_bounds (void)
{
    g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();
    gfloat min_x, min_y, max_x, max_y;
    gboolean enabled;

    lrg_camera_sideon_set_bounds (camera, 0.0f, 0.0f, 3200.0f, 1800.0f);
    lrg_camera_sideon_set_bounds_enabled (camera, TRUE);

    enabled = lrg_camera_sideon_get_bounds_enabled (camera);
    lrg_camera_sideon_get_bounds (camera, &min_x, &min_y, &max_x, &max_y);

    g_assert_true (enabled);
    g_assert_cmpfloat_with_epsilon (max_x, 3200.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (max_y, 1800.0f, 0.0001f);
}

static void
test_camera_sideon_shake (void)
{
    g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();

    g_assert_false (lrg_camera_sideon_is_shaking (camera));

    lrg_camera_sideon_shake (camera, 8.0f, 0.3f);
    g_assert_true (lrg_camera_sideon_is_shaking (camera));

    lrg_camera_sideon_stop_shake (camera);
    g_assert_false (lrg_camera_sideon_is_shaking (camera));
}

static void
test_camera_sideon_follow (void)
{
    g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();
    g_autoptr(GrlVector2) target = NULL;

    /* Follow at 200,100 with high delta time for noticeable movement */
    lrg_camera_sideon_follow (camera, 200.0f, 100.0f, 1.0f);
    target = lrg_camera2d_get_target (LRG_CAMERA2D (camera));

    /* After 1 second with high delta, camera should have moved toward target */
    g_assert_cmpfloat (target->x, >=, 50.0f);
}

/* ==========================================================================
 * Test Cases - LrgCameraFirstPerson
 * ========================================================================== */

static void
test_camera_firstperson_type (void)
{
    GType type;

    type = LRG_TYPE_CAMERA_FIRSTPERSON;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA3D));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA));
}

static void
test_camera_firstperson_new (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();

    g_assert_nonnull (camera);
    g_assert_true (LRG_IS_CAMERA_FIRSTPERSON (camera));
    g_assert_true (LRG_IS_CAMERA3D (camera));
}

static void
test_camera_firstperson_default_values (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gfloat pitch, yaw;
    gfloat sens_x, sens_y;
    gfloat eye_height;
    gboolean head_bob;

    pitch = lrg_camera_firstperson_get_pitch (camera);
    yaw = lrg_camera_firstperson_get_yaw (camera);
    sens_x = lrg_camera_firstperson_get_sensitivity_x (camera);
    sens_y = lrg_camera_firstperson_get_sensitivity_y (camera);
    eye_height = lrg_camera_firstperson_get_eye_height (camera);
    head_bob = lrg_camera_firstperson_get_head_bob_enabled (camera);

    g_assert_cmpfloat_with_epsilon (pitch, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (yaw, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (sens_x, 0.1f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (sens_y, 0.1f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (eye_height, 1.7f, 0.0001f);
    g_assert_false (head_bob);
}

static void
test_camera_firstperson_set_pitch (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gfloat pitch;

    lrg_camera_firstperson_set_pitch (camera, 45.0f);
    pitch = lrg_camera_firstperson_get_pitch (camera);

    g_assert_cmpfloat_with_epsilon (pitch, 45.0f, 0.0001f);
}

static void
test_camera_firstperson_pitch_clamping (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gfloat pitch;

    /* Try to set beyond limits */
    lrg_camera_firstperson_set_pitch (camera, 100.0f);
    pitch = lrg_camera_firstperson_get_pitch (camera);
    g_assert_cmpfloat (pitch, <=, 89.0f);

    lrg_camera_firstperson_set_pitch (camera, -100.0f);
    pitch = lrg_camera_firstperson_get_pitch (camera);
    g_assert_cmpfloat (pitch, >=, -89.0f);
}

static void
test_camera_firstperson_set_yaw (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gfloat yaw;

    lrg_camera_firstperson_set_yaw (camera, 180.0f);
    yaw = lrg_camera_firstperson_get_yaw (camera);

    g_assert_cmpfloat_with_epsilon (yaw, 180.0f, 0.0001f);
}

static void
test_camera_firstperson_yaw_wrapping (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gfloat yaw;

    /* Yaw should wrap around 360 */
    lrg_camera_firstperson_set_yaw (camera, 400.0f);
    yaw = lrg_camera_firstperson_get_yaw (camera);
    g_assert_cmpfloat (yaw, <, 360.0f);
    g_assert_cmpfloat (yaw, >=, 0.0f);
    g_assert_cmpfloat_with_epsilon (yaw, 40.0f, 0.0001f);
}

static void
test_camera_firstperson_rotate (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gfloat pitch, yaw;

    lrg_camera_firstperson_set_sensitivity_x (camera, 1.0f);
    lrg_camera_firstperson_set_sensitivity_y (camera, 1.0f);

    /* Note: Y is inverted for natural feel - positive delta_y looks DOWN (negative pitch) */
    lrg_camera_firstperson_rotate (camera, 10.0f, 5.0f);
    pitch = lrg_camera_firstperson_get_pitch (camera);
    yaw = lrg_camera_firstperson_get_yaw (camera);

    g_assert_cmpfloat_with_epsilon (pitch, -5.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (yaw, 10.0f, 0.0001f);
}

static void
test_camera_firstperson_set_body_position (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();

    /* Just verify it doesn't crash */
    lrg_camera_firstperson_set_body_position (camera, 10.0f, 0.0f, 20.0f);
}

static void
test_camera_firstperson_set_sensitivity (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gfloat sens_x, sens_y;

    lrg_camera_firstperson_set_sensitivity_x (camera, 0.2f);
    lrg_camera_firstperson_set_sensitivity_y (camera, 0.15f);

    sens_x = lrg_camera_firstperson_get_sensitivity_x (camera);
    sens_y = lrg_camera_firstperson_get_sensitivity_y (camera);

    g_assert_cmpfloat_with_epsilon (sens_x, 0.2f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (sens_y, 0.15f, 0.0001f);
}

static void
test_camera_firstperson_set_eye_height (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gfloat height;

    lrg_camera_firstperson_set_eye_height (camera, 1.8f);
    height = lrg_camera_firstperson_get_eye_height (camera);

    g_assert_cmpfloat_with_epsilon (height, 1.8f, 0.0001f);
}

static void
test_camera_firstperson_head_bob (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gboolean enabled;

    lrg_camera_firstperson_set_head_bob_enabled (camera, TRUE);
    enabled = lrg_camera_firstperson_get_head_bob_enabled (camera);
    g_assert_true (enabled);

    lrg_camera_firstperson_set_head_bob (camera, 12.0f, 0.08f, 0.03f);
    lrg_camera_firstperson_update_head_bob (camera, TRUE, 0.016f);
}

static void
test_camera_firstperson_pitch_limits (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    gfloat min_pitch, max_pitch;

    lrg_camera_firstperson_set_pitch_limits (camera, -45.0f, 60.0f);
    lrg_camera_firstperson_get_pitch_limits (camera, &min_pitch, &max_pitch);

    g_assert_cmpfloat_with_epsilon (min_pitch, -45.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (max_pitch, 60.0f, 0.0001f);
}

static void
test_camera_firstperson_direction_vectors (void)
{
    g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
    g_autoptr(GrlVector3) forward = NULL;
    g_autoptr(GrlVector3) right = NULL;
    g_autoptr(GrlVector3) look = NULL;

    lrg_camera_firstperson_set_yaw (camera, 0.0f);
    lrg_camera_firstperson_set_pitch (camera, 0.0f);

    forward = lrg_camera_firstperson_get_forward (camera);
    right = lrg_camera_firstperson_get_right (camera);
    look = lrg_camera_firstperson_get_look_direction (camera);

    g_assert_nonnull (forward);
    g_assert_nonnull (right);
    g_assert_nonnull (look);

    /* Forward should have Y=0 (horizontal) */
    g_assert_cmpfloat_with_epsilon (forward->y, 0.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - LrgCameraThirdPerson
 * ========================================================================== */

static void
test_camera_thirdperson_type (void)
{
    GType type;

    type = LRG_TYPE_CAMERA_THIRDPERSON;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA3D));
    g_assert_true (g_type_is_a (type, LRG_TYPE_CAMERA));
}

static void
test_camera_thirdperson_new (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();

    g_assert_nonnull (camera);
    g_assert_true (LRG_IS_CAMERA_THIRDPERSON (camera));
    g_assert_true (LRG_IS_CAMERA3D (camera));
}

static void
test_camera_thirdperson_default_values (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat distance, pitch, yaw;
    gfloat height_offset, shoulder_offset;
    gfloat sens_x, sens_y;
    gboolean collision_enabled;

    distance = lrg_camera_thirdperson_get_distance (camera);
    pitch = lrg_camera_thirdperson_get_pitch (camera);
    yaw = lrg_camera_thirdperson_get_yaw (camera);
    height_offset = lrg_camera_thirdperson_get_height_offset (camera);
    shoulder_offset = lrg_camera_thirdperson_get_shoulder_offset (camera);
    sens_x = lrg_camera_thirdperson_get_sensitivity_x (camera);
    sens_y = lrg_camera_thirdperson_get_sensitivity_y (camera);
    collision_enabled = lrg_camera_thirdperson_get_collision_enabled (camera);

    g_assert_cmpfloat_with_epsilon (distance, 5.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (pitch, 15.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (yaw, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (height_offset, 1.5f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (shoulder_offset, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (sens_x, 0.15f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (sens_y, 0.15f, 0.0001f);
    g_assert_true (collision_enabled);
}

static void
test_camera_thirdperson_set_distance (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat distance;

    lrg_camera_thirdperson_set_distance (camera, 8.0f);
    distance = lrg_camera_thirdperson_get_distance (camera);

    g_assert_cmpfloat_with_epsilon (distance, 8.0f, 0.0001f);
}

static void
test_camera_thirdperson_distance_limits (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat min_dist, max_dist;

    lrg_camera_thirdperson_set_distance_limits (camera, 2.0f, 15.0f);
    lrg_camera_thirdperson_get_distance_limits (camera, &min_dist, &max_dist);

    g_assert_cmpfloat_with_epsilon (min_dist, 2.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (max_dist, 15.0f, 0.0001f);
}

static void
test_camera_thirdperson_set_pitch (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat pitch;

    lrg_camera_thirdperson_set_pitch (camera, 30.0f);
    pitch = lrg_camera_thirdperson_get_pitch (camera);

    g_assert_cmpfloat_with_epsilon (pitch, 30.0f, 0.0001f);
}

static void
test_camera_thirdperson_pitch_clamping (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat pitch;

    /* Default limits are -30 to 60 */
    lrg_camera_thirdperson_set_pitch (camera, 80.0f);
    pitch = lrg_camera_thirdperson_get_pitch (camera);
    g_assert_cmpfloat (pitch, <=, 60.0f);
}

static void
test_camera_thirdperson_set_yaw (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat yaw;

    lrg_camera_thirdperson_set_yaw (camera, 90.0f);
    yaw = lrg_camera_thirdperson_get_yaw (camera);

    g_assert_cmpfloat_with_epsilon (yaw, 90.0f, 0.0001f);
}

static void
test_camera_thirdperson_yaw_wrapping (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat yaw;

    lrg_camera_thirdperson_set_yaw (camera, 370.0f);
    yaw = lrg_camera_thirdperson_get_yaw (camera);

    g_assert_cmpfloat (yaw, <, 360.0f);
    g_assert_cmpfloat_with_epsilon (yaw, 10.0f, 0.0001f);
}

static void
test_camera_thirdperson_orbit (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat pitch, yaw;

    lrg_camera_thirdperson_set_sensitivity_x (camera, 1.0f);
    lrg_camera_thirdperson_set_sensitivity_y (camera, 1.0f);

    lrg_camera_thirdperson_orbit (camera, 20.0f, 10.0f);

    yaw = lrg_camera_thirdperson_get_yaw (camera);
    pitch = lrg_camera_thirdperson_get_pitch (camera);

    g_assert_cmpfloat_with_epsilon (yaw, 20.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (pitch, 25.0f, 0.0001f); /* 15 default + 10 */
}

static void
test_camera_thirdperson_set_offsets (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat height, shoulder;

    lrg_camera_thirdperson_set_height_offset (camera, 2.0f);
    lrg_camera_thirdperson_set_shoulder_offset (camera, 0.8f);

    height = lrg_camera_thirdperson_get_height_offset (camera);
    shoulder = lrg_camera_thirdperson_get_shoulder_offset (camera);

    g_assert_cmpfloat_with_epsilon (height, 2.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (shoulder, 0.8f, 0.0001f);
}

static void
test_camera_thirdperson_set_sensitivity (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat sens_x, sens_y;

    lrg_camera_thirdperson_set_sensitivity_x (camera, 0.2f);
    lrg_camera_thirdperson_set_sensitivity_y (camera, 0.1f);

    sens_x = lrg_camera_thirdperson_get_sensitivity_x (camera);
    sens_y = lrg_camera_thirdperson_get_sensitivity_y (camera);

    g_assert_cmpfloat_with_epsilon (sens_x, 0.2f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (sens_y, 0.1f, 0.0001f);
}

static void
test_camera_thirdperson_set_smoothing (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat orbit_smooth, follow_smooth;

    lrg_camera_thirdperson_set_orbit_smoothing (camera, 12.0f);
    lrg_camera_thirdperson_set_follow_smoothing (camera, 15.0f);

    orbit_smooth = lrg_camera_thirdperson_get_orbit_smoothing (camera);
    follow_smooth = lrg_camera_thirdperson_get_follow_smoothing (camera);

    g_assert_cmpfloat_with_epsilon (orbit_smooth, 12.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (follow_smooth, 15.0f, 0.0001f);
}

static void
test_camera_thirdperson_pitch_limits (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat min_pitch, max_pitch;

    lrg_camera_thirdperson_set_pitch_limits (camera, -45.0f, 45.0f);
    lrg_camera_thirdperson_get_pitch_limits (camera, &min_pitch, &max_pitch);

    g_assert_cmpfloat_with_epsilon (min_pitch, -45.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (max_pitch, 45.0f, 0.0001f);
}

static void
test_camera_thirdperson_follow (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();

    /* Just verify follow doesn't crash */
    lrg_camera_thirdperson_follow (camera, 10.0f, 0.0f, 20.0f, 0.016f);
    lrg_camera_thirdperson_follow (camera, 12.0f, 0.0f, 22.0f, 0.016f);
}

static void
test_camera_thirdperson_snap_to_target (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();

    /* Just verify snap doesn't crash */
    lrg_camera_thirdperson_snap_to_target (camera, 50.0f, 0.0f, 100.0f);
}

static void
test_camera_thirdperson_collision_settings (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat radius;
    guint32 layers;
    gboolean enabled;

    lrg_camera_thirdperson_set_collision_enabled (camera, FALSE);
    enabled = lrg_camera_thirdperson_get_collision_enabled (camera);
    g_assert_false (enabled);

    lrg_camera_thirdperson_set_collision_radius (camera, 0.5f);
    radius = lrg_camera_thirdperson_get_collision_radius (camera);
    g_assert_cmpfloat_with_epsilon (radius, 0.5f, 0.0001f);

    lrg_camera_thirdperson_set_collision_layers (camera, 0xFF);
    layers = lrg_camera_thirdperson_get_collision_layers (camera);
    g_assert_cmpuint (layers, ==, 0xFF);
}

static void
test_camera_thirdperson_direction_vectors (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    g_autoptr(GrlVector3) forward = NULL;
    g_autoptr(GrlVector3) right = NULL;

    forward = lrg_camera_thirdperson_get_forward (camera);
    right = lrg_camera_thirdperson_get_right (camera);

    g_assert_nonnull (forward);
    g_assert_nonnull (right);

    /* Forward and right should have Y=0 (horizontal movement) */
    g_assert_cmpfloat_with_epsilon (forward->y, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (right->y, 0.0f, 0.0001f);
}

static void
test_camera_thirdperson_actual_distance (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gfloat actual;

    /* Without collision callback, actual distance equals desired */
    lrg_camera_thirdperson_set_distance (camera, 7.0f);
    lrg_camera_thirdperson_snap_to_target (camera, 0.0f, 0.0f, 0.0f);

    actual = lrg_camera_thirdperson_get_actual_distance (camera);
    g_assert_cmpfloat_with_epsilon (actual, 7.0f, 0.0001f);
}

/* Collision callback for testing - returns collision at 50% distance */
static gboolean
test_collision_callback (LrgCameraThirdPerson *camera,
                         gfloat                start_x,
                         gfloat                start_y,
                         gfloat                start_z,
                         gfloat                end_x,
                         gfloat                end_y,
                         gfloat                end_z,
                         gfloat                radius,
                         guint32               layers,
                         gfloat               *out_hit_distance,
                         gpointer              user_data)
{
    gint *call_count = (gint *)user_data;
    (*call_count)++;

    /* Report collision at 50% of the distance */
    *out_hit_distance = 0.5f;
    return TRUE;
}

static void
test_camera_thirdperson_collision_callback (void)
{
    g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();
    gint call_count = 0;
    gfloat actual;

    /* Set up collision callback */
    lrg_camera_thirdperson_set_collision_callback (camera,
                                                    test_collision_callback,
                                                    &call_count, NULL);

    /* Configure distance and snap to target - this should invoke callback */
    lrg_camera_thirdperson_set_distance (camera, 10.0f);
    lrg_camera_thirdperson_snap_to_target (camera, 0.0f, 0.0f, 0.0f);

    /* Callback should have been called and distance reduced to 50% */
    g_assert_cmpint (call_count, >, 0);
    actual = lrg_camera_thirdperson_get_actual_distance (camera);
    g_assert_cmpfloat (actual, <, 10.0f);
}

/* ==========================================================================
 * Test Cases - LrgWindow (Abstract)
 * ========================================================================== */

static void
test_window_type (void)
{
    GType type;

    type = LRG_TYPE_WINDOW;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (G_TYPE_IS_ABSTRACT (type));
}

static void
test_grl_window_type (void)
{
    GType type;

    type = LRG_TYPE_GRL_WINDOW;
    g_assert_true (G_TYPE_IS_OBJECT (type));
    g_assert_true (g_type_is_a (type, LRG_TYPE_WINDOW));
}

/* ==========================================================================
 * Test Cases - LrgRenderer
 * ========================================================================== */

static void
test_renderer_type (void)
{
    GType type;

    type = LRG_TYPE_RENDERER;
    g_assert_true (G_TYPE_IS_OBJECT (type));
}

/* ==========================================================================
 * Test Cases - Enums
 * ========================================================================== */

static void
test_render_layer_enum (void)
{
    GType type;

    type = lrg_render_layer_get_type ();
    g_assert_true (G_TYPE_IS_ENUM (type));

    /* Verify enum values */
    g_assert_cmpint (LRG_RENDER_LAYER_BACKGROUND, ==, 0);
    g_assert_cmpint (LRG_RENDER_LAYER_WORLD, ==, 1);
    g_assert_cmpint (LRG_RENDER_LAYER_EFFECTS, ==, 2);
    g_assert_cmpint (LRG_RENDER_LAYER_UI, ==, 3);
    g_assert_cmpint (LRG_RENDER_LAYER_DEBUG, ==, 4);
}

static void
test_projection_type_enum (void)
{
    GType type;

    type = lrg_projection_type_get_type ();
    g_assert_true (G_TYPE_IS_ENUM (type));

    /* Verify enum values */
    g_assert_cmpint (LRG_PROJECTION_PERSPECTIVE, ==, 0);
    g_assert_cmpint (LRG_PROJECTION_ORTHOGRAPHIC, ==, 1);
}

/* ==========================================================================
 * Test Cases - Engine Integration
 * ========================================================================== */

static void
test_engine_no_window_by_default (void)
{
    LrgEngine *engine;
    LrgWindow *window;
    LrgRenderer *renderer;

    engine = lrg_engine_get_default ();
    g_assert_nonnull (engine);

    window = lrg_engine_get_window (engine);
    g_assert_null (window);

    renderer = lrg_engine_get_renderer (engine);
    g_assert_null (renderer);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    /* LrgDrawable interface tests */
    g_test_add_func ("/graphics/drawable/type", test_drawable_type);
    g_test_add_func ("/graphics/drawable/mock-implements", test_drawable_mock_implements);
    g_test_add_func ("/graphics/drawable/draw", test_drawable_draw);
    g_test_add_func ("/graphics/drawable/get-bounds", test_drawable_get_bounds);

    /* LrgCamera base tests */
    g_test_add_func ("/graphics/camera/type", test_camera_type);
    g_test_add ("/graphics/camera/camera2d-is-camera", CameraFixture, NULL,
                camera_fixture_set_up, test_camera2d_is_camera, camera_fixture_tear_down);
    g_test_add ("/graphics/camera/camera3d-is-camera", CameraFixture, NULL,
                camera_fixture_set_up, test_camera3d_is_camera, camera_fixture_tear_down);

    /* LrgCamera2D tests */
    g_test_add_func ("/graphics/camera2d/type", test_camera2d_type);
    g_test_add_func ("/graphics/camera2d/new", test_camera2d_new);
    g_test_add ("/graphics/camera2d/default-values", CameraFixture, NULL,
                camera_fixture_set_up, test_camera2d_default_values, camera_fixture_tear_down);
    g_test_add ("/graphics/camera2d/set-offset", CameraFixture, NULL,
                camera_fixture_set_up, test_camera2d_set_offset, camera_fixture_tear_down);
    g_test_add ("/graphics/camera2d/set-target", CameraFixture, NULL,
                camera_fixture_set_up, test_camera2d_set_target, camera_fixture_tear_down);
    g_test_add ("/graphics/camera2d/set-rotation", CameraFixture, NULL,
                camera_fixture_set_up, test_camera2d_set_rotation, camera_fixture_tear_down);
    g_test_add ("/graphics/camera2d/set-zoom", CameraFixture, NULL,
                camera_fixture_set_up, test_camera2d_set_zoom, camera_fixture_tear_down);

    /* LrgCamera3D tests */
    g_test_add_func ("/graphics/camera3d/type", test_camera3d_type);
    g_test_add_func ("/graphics/camera3d/new", test_camera3d_new);
    g_test_add ("/graphics/camera3d/default-values", CameraFixture, NULL,
                camera_fixture_set_up, test_camera3d_default_values, camera_fixture_tear_down);
    g_test_add ("/graphics/camera3d/set-position", CameraFixture, NULL,
                camera_fixture_set_up, test_camera3d_set_position, camera_fixture_tear_down);
    g_test_add ("/graphics/camera3d/set-target", CameraFixture, NULL,
                camera_fixture_set_up, test_camera3d_set_target, camera_fixture_tear_down);
    g_test_add ("/graphics/camera3d/set-fovy", CameraFixture, NULL,
                camera_fixture_set_up, test_camera3d_set_fovy, camera_fixture_tear_down);
    g_test_add ("/graphics/camera3d/set-projection", CameraFixture, NULL,
                camera_fixture_set_up, test_camera3d_set_projection, camera_fixture_tear_down);

    /* LrgCameraIsometric tests */
    g_test_add_func ("/graphics/camera-isometric/type", test_camera_isometric_type);
    g_test_add_func ("/graphics/camera-isometric/new", test_camera_isometric_new);
    g_test_add_func ("/graphics/camera-isometric/default-values", test_camera_isometric_default_values);
    g_test_add_func ("/graphics/camera-isometric/set-tile-width", test_camera_isometric_set_tile_width);
    g_test_add_func ("/graphics/camera-isometric/set-tile-height", test_camera_isometric_set_tile_height);
    g_test_add_func ("/graphics/camera-isometric/set-height-scale", test_camera_isometric_set_height_scale);
    g_test_add_func ("/graphics/camera-isometric/set-zoom", test_camera_isometric_set_zoom);
    g_test_add_func ("/graphics/camera-isometric/focus-on", test_camera_isometric_focus_on);
    g_test_add_func ("/graphics/camera-isometric/tile-conversion", test_camera_isometric_tile_conversion);

    /* LrgCameraTopDown tests */
    g_test_add_func ("/graphics/camera-topdown/type", test_camera_topdown_type);
    g_test_add_func ("/graphics/camera-topdown/new", test_camera_topdown_new);
    g_test_add_func ("/graphics/camera-topdown/default-values", test_camera_topdown_default_values);
    g_test_add_func ("/graphics/camera-topdown/set-follow-speed", test_camera_topdown_set_follow_speed);
    g_test_add_func ("/graphics/camera-topdown/set-deadzone", test_camera_topdown_set_deadzone);
    g_test_add_func ("/graphics/camera-topdown/bounds", test_camera_topdown_bounds);
    g_test_add_func ("/graphics/camera-topdown/follow", test_camera_topdown_follow);
    g_test_add_func ("/graphics/camera-topdown/shake", test_camera_topdown_shake);
    g_test_add_func ("/graphics/camera-topdown/update-shake", test_camera_topdown_update_shake);

    /* LrgCameraSideOn tests */
    g_test_add_func ("/graphics/camera-sideon/type", test_camera_sideon_type);
    g_test_add_func ("/graphics/camera-sideon/new", test_camera_sideon_new);
    g_test_add_func ("/graphics/camera-sideon/default-values", test_camera_sideon_default_values);
    g_test_add_func ("/graphics/camera-sideon/set-follow-speed", test_camera_sideon_set_follow_speed);
    g_test_add_func ("/graphics/camera-sideon/set-deadzone", test_camera_sideon_set_deadzone);
    g_test_add_func ("/graphics/camera-sideon/set-lookahead", test_camera_sideon_set_lookahead);
    g_test_add_func ("/graphics/camera-sideon/set-vertical-bias", test_camera_sideon_set_vertical_bias);
    g_test_add_func ("/graphics/camera-sideon/bounds", test_camera_sideon_bounds);
    g_test_add_func ("/graphics/camera-sideon/shake", test_camera_sideon_shake);
    g_test_add_func ("/graphics/camera-sideon/follow", test_camera_sideon_follow);

    /* LrgCameraFirstPerson tests */
    g_test_add_func ("/graphics/camera-firstperson/type", test_camera_firstperson_type);
    g_test_add_func ("/graphics/camera-firstperson/new", test_camera_firstperson_new);
    g_test_add_func ("/graphics/camera-firstperson/default-values", test_camera_firstperson_default_values);
    g_test_add_func ("/graphics/camera-firstperson/set-pitch", test_camera_firstperson_set_pitch);
    g_test_add_func ("/graphics/camera-firstperson/pitch-clamping", test_camera_firstperson_pitch_clamping);
    g_test_add_func ("/graphics/camera-firstperson/set-yaw", test_camera_firstperson_set_yaw);
    g_test_add_func ("/graphics/camera-firstperson/yaw-wrapping", test_camera_firstperson_yaw_wrapping);
    g_test_add_func ("/graphics/camera-firstperson/rotate", test_camera_firstperson_rotate);
    g_test_add_func ("/graphics/camera-firstperson/set-body-position", test_camera_firstperson_set_body_position);
    g_test_add_func ("/graphics/camera-firstperson/set-sensitivity", test_camera_firstperson_set_sensitivity);
    g_test_add_func ("/graphics/camera-firstperson/set-eye-height", test_camera_firstperson_set_eye_height);
    g_test_add_func ("/graphics/camera-firstperson/head-bob", test_camera_firstperson_head_bob);
    g_test_add_func ("/graphics/camera-firstperson/pitch-limits", test_camera_firstperson_pitch_limits);
    g_test_add_func ("/graphics/camera-firstperson/direction-vectors", test_camera_firstperson_direction_vectors);

    /* LrgCameraThirdPerson tests */
    g_test_add_func ("/graphics/camera-thirdperson/type", test_camera_thirdperson_type);
    g_test_add_func ("/graphics/camera-thirdperson/new", test_camera_thirdperson_new);
    g_test_add_func ("/graphics/camera-thirdperson/default-values", test_camera_thirdperson_default_values);
    g_test_add_func ("/graphics/camera-thirdperson/set-distance", test_camera_thirdperson_set_distance);
    g_test_add_func ("/graphics/camera-thirdperson/distance-limits", test_camera_thirdperson_distance_limits);
    g_test_add_func ("/graphics/camera-thirdperson/set-pitch", test_camera_thirdperson_set_pitch);
    g_test_add_func ("/graphics/camera-thirdperson/pitch-clamping", test_camera_thirdperson_pitch_clamping);
    g_test_add_func ("/graphics/camera-thirdperson/set-yaw", test_camera_thirdperson_set_yaw);
    g_test_add_func ("/graphics/camera-thirdperson/yaw-wrapping", test_camera_thirdperson_yaw_wrapping);
    g_test_add_func ("/graphics/camera-thirdperson/orbit", test_camera_thirdperson_orbit);
    g_test_add_func ("/graphics/camera-thirdperson/set-offsets", test_camera_thirdperson_set_offsets);
    g_test_add_func ("/graphics/camera-thirdperson/set-sensitivity", test_camera_thirdperson_set_sensitivity);
    g_test_add_func ("/graphics/camera-thirdperson/set-smoothing", test_camera_thirdperson_set_smoothing);
    g_test_add_func ("/graphics/camera-thirdperson/pitch-limits", test_camera_thirdperson_pitch_limits);
    g_test_add_func ("/graphics/camera-thirdperson/follow", test_camera_thirdperson_follow);
    g_test_add_func ("/graphics/camera-thirdperson/snap-to-target", test_camera_thirdperson_snap_to_target);
    g_test_add_func ("/graphics/camera-thirdperson/collision-settings", test_camera_thirdperson_collision_settings);
    g_test_add_func ("/graphics/camera-thirdperson/direction-vectors", test_camera_thirdperson_direction_vectors);
    g_test_add_func ("/graphics/camera-thirdperson/actual-distance", test_camera_thirdperson_actual_distance);
    g_test_add_func ("/graphics/camera-thirdperson/collision-callback", test_camera_thirdperson_collision_callback);

    /* LrgWindow tests */
    g_test_add_func ("/graphics/window/type", test_window_type);
    g_test_add_func ("/graphics/window/grl-window-type", test_grl_window_type);

    /* LrgRenderer tests */
    g_test_add_func ("/graphics/renderer/type", test_renderer_type);

    /* Enum tests */
    g_test_add_func ("/graphics/enums/render-layer", test_render_layer_enum);
    g_test_add_func ("/graphics/enums/projection-type", test_projection_type_enum);

    /* Engine integration tests */
    g_test_add_func ("/graphics/engine/no-window-by-default", test_engine_no_window_by_default);

    return g_test_run ();
}
