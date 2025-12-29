/* test-trigger2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the 2D trigger system.
 */

#include <glib.h>
#include <glib-object.h>

#include "../src/lrg-enums.h"
#include "../src/trigger2d/lrg-trigger2d.h"
#include "../src/trigger2d/lrg-trigger-rect.h"
#include "../src/trigger2d/lrg-trigger-circle.h"
#include "../src/trigger2d/lrg-trigger-polygon.h"
#include "../src/trigger2d/lrg-trigger-event.h"
#include "../src/trigger2d/lrg-trigger-manager.h"

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgTriggerManager *manager;
    LrgTriggerRect    *rect_trigger;
    LrgTriggerCircle  *circle_trigger;
    LrgTriggerPolygon *polygon_trigger;
} TriggerFixture;

static void
trigger_fixture_set_up (TriggerFixture *fixture,
                        gconstpointer   user_data)
{
    (void)user_data;

    fixture->manager = lrg_trigger_manager_new ();

    /* Create a rectangle at (10, 10) with size 20x20 */
    fixture->rect_trigger = lrg_trigger_rect_new_with_id ("test_rect",
                                                          10.0f, 10.0f,
                                                          20.0f, 20.0f);

    /* Create a circle at (50, 50) with radius 10 */
    fixture->circle_trigger = lrg_trigger_circle_new_with_id ("test_circle",
                                                               50.0f, 50.0f,
                                                               10.0f);

    /* Create a triangle polygon */
    fixture->polygon_trigger = lrg_trigger_polygon_new_with_id ("test_polygon");
    lrg_trigger_polygon_add_vertex (fixture->polygon_trigger, 100.0f, 100.0f);
    lrg_trigger_polygon_add_vertex (fixture->polygon_trigger, 120.0f, 100.0f);
    lrg_trigger_polygon_add_vertex (fixture->polygon_trigger, 110.0f, 120.0f);
}

static void
trigger_fixture_tear_down (TriggerFixture *fixture,
                           gconstpointer   user_data)
{
    (void)user_data;

    g_clear_object (&fixture->polygon_trigger);
    g_clear_object (&fixture->circle_trigger);
    g_clear_object (&fixture->rect_trigger);
    g_clear_object (&fixture->manager);
}

/* ==========================================================================
 * Rectangle Trigger Tests
 * ========================================================================== */

static void
test_trigger_rect_creation (TriggerFixture *fixture,
                            gconstpointer   user_data)
{
    (void)user_data;

    g_assert_nonnull (fixture->rect_trigger);
    g_assert_true (LRG_IS_TRIGGER_RECT (fixture->rect_trigger));
    g_assert_true (LRG_IS_TRIGGER2D (fixture->rect_trigger));

    g_assert_cmpstr (lrg_trigger2d_get_id (LRG_TRIGGER2D (fixture->rect_trigger)),
                     ==, "test_rect");
}

static void
test_trigger_rect_properties (TriggerFixture *fixture,
                              gconstpointer   user_data)
{
    (void)user_data;

    g_assert_cmpfloat (lrg_trigger_rect_get_x (fixture->rect_trigger), ==, 10.0f);
    g_assert_cmpfloat (lrg_trigger_rect_get_y (fixture->rect_trigger), ==, 10.0f);
    g_assert_cmpfloat (lrg_trigger_rect_get_width (fixture->rect_trigger), ==, 20.0f);
    g_assert_cmpfloat (lrg_trigger_rect_get_height (fixture->rect_trigger), ==, 20.0f);

    /* Change properties */
    lrg_trigger_rect_set_x (fixture->rect_trigger, 15.0f);
    g_assert_cmpfloat (lrg_trigger_rect_get_x (fixture->rect_trigger), ==, 15.0f);

    lrg_trigger_rect_set_position (fixture->rect_trigger, 5.0f, 5.0f);
    g_assert_cmpfloat (lrg_trigger_rect_get_x (fixture->rect_trigger), ==, 5.0f);
    g_assert_cmpfloat (lrg_trigger_rect_get_y (fixture->rect_trigger), ==, 5.0f);
}

static void
test_trigger_rect_point_test (TriggerFixture *fixture,
                              gconstpointer   user_data)
{
    LrgTrigger2D *trigger;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->rect_trigger);

    /* Inside tests */
    g_assert_true (lrg_trigger2d_test_point (trigger, 15.0f, 15.0f));  /* Center */
    g_assert_true (lrg_trigger2d_test_point (trigger, 10.0f, 10.0f));  /* Top-left corner */
    g_assert_true (lrg_trigger2d_test_point (trigger, 30.0f, 30.0f));  /* Bottom-right corner */
    g_assert_true (lrg_trigger2d_test_point (trigger, 20.0f, 20.0f));  /* Center */

    /* Outside tests */
    g_assert_false (lrg_trigger2d_test_point (trigger, 5.0f, 15.0f));   /* Left */
    g_assert_false (lrg_trigger2d_test_point (trigger, 35.0f, 15.0f));  /* Right */
    g_assert_false (lrg_trigger2d_test_point (trigger, 15.0f, 5.0f));   /* Top */
    g_assert_false (lrg_trigger2d_test_point (trigger, 15.0f, 35.0f));  /* Bottom */
}

static void
test_trigger_rect_bounds (TriggerFixture *fixture,
                          gconstpointer   user_data)
{
    LrgTrigger2D *trigger;
    gfloat x, y, w, h;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->rect_trigger);
    lrg_trigger2d_get_bounds (trigger, &x, &y, &w, &h);

    g_assert_cmpfloat (x, ==, 10.0f);
    g_assert_cmpfloat (y, ==, 10.0f);
    g_assert_cmpfloat (w, ==, 20.0f);
    g_assert_cmpfloat (h, ==, 20.0f);
}

static void
test_trigger_rect_center (TriggerFixture *fixture,
                          gconstpointer   user_data)
{
    gfloat cx, cy;

    (void)user_data;

    lrg_trigger_rect_get_center (fixture->rect_trigger, &cx, &cy);
    g_assert_cmpfloat (cx, ==, 20.0f);  /* 10 + 20/2 */
    g_assert_cmpfloat (cy, ==, 20.0f);  /* 10 + 20/2 */

    lrg_trigger_rect_set_center (fixture->rect_trigger, 50.0f, 50.0f);
    lrg_trigger_rect_get_center (fixture->rect_trigger, &cx, &cy);
    g_assert_cmpfloat (cx, ==, 50.0f);
    g_assert_cmpfloat (cy, ==, 50.0f);
}

static void
test_trigger_rect_shape (TriggerFixture *fixture,
                         gconstpointer   user_data)
{
    (void)user_data;

    g_assert_cmpint (lrg_trigger2d_get_shape (LRG_TRIGGER2D (fixture->rect_trigger)),
                     ==, LRG_TRIGGER2D_SHAPE_RECTANGLE);
}

/* ==========================================================================
 * Circle Trigger Tests
 * ========================================================================== */

static void
test_trigger_circle_creation (TriggerFixture *fixture,
                              gconstpointer   user_data)
{
    (void)user_data;

    g_assert_nonnull (fixture->circle_trigger);
    g_assert_true (LRG_IS_TRIGGER_CIRCLE (fixture->circle_trigger));
    g_assert_true (LRG_IS_TRIGGER2D (fixture->circle_trigger));

    g_assert_cmpstr (lrg_trigger2d_get_id (LRG_TRIGGER2D (fixture->circle_trigger)),
                     ==, "test_circle");
}

static void
test_trigger_circle_properties (TriggerFixture *fixture,
                                gconstpointer   user_data)
{
    (void)user_data;

    g_assert_cmpfloat (lrg_trigger_circle_get_center_x (fixture->circle_trigger), ==, 50.0f);
    g_assert_cmpfloat (lrg_trigger_circle_get_center_y (fixture->circle_trigger), ==, 50.0f);
    g_assert_cmpfloat (lrg_trigger_circle_get_radius (fixture->circle_trigger), ==, 10.0f);
    g_assert_cmpfloat (lrg_trigger_circle_get_diameter (fixture->circle_trigger), ==, 20.0f);

    lrg_trigger_circle_set_center (fixture->circle_trigger, 60.0f, 60.0f);
    g_assert_cmpfloat (lrg_trigger_circle_get_center_x (fixture->circle_trigger), ==, 60.0f);
    g_assert_cmpfloat (lrg_trigger_circle_get_center_y (fixture->circle_trigger), ==, 60.0f);
}

static void
test_trigger_circle_point_test (TriggerFixture *fixture,
                                gconstpointer   user_data)
{
    LrgTrigger2D *trigger;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->circle_trigger);

    /* Inside tests */
    g_assert_true (lrg_trigger2d_test_point (trigger, 50.0f, 50.0f));  /* Center */
    g_assert_true (lrg_trigger2d_test_point (trigger, 55.0f, 50.0f));  /* Right of center */
    g_assert_true (lrg_trigger2d_test_point (trigger, 50.0f, 55.0f));  /* Below center */
    g_assert_true (lrg_trigger2d_test_point (trigger, 60.0f, 50.0f));  /* On edge (right) */

    /* Outside tests */
    g_assert_false (lrg_trigger2d_test_point (trigger, 35.0f, 50.0f));  /* Left */
    g_assert_false (lrg_trigger2d_test_point (trigger, 65.0f, 50.0f));  /* Right */
    g_assert_false (lrg_trigger2d_test_point (trigger, 50.0f, 35.0f));  /* Top */
    g_assert_false (lrg_trigger2d_test_point (trigger, 50.0f, 65.0f));  /* Bottom */
}

static void
test_trigger_circle_bounds (TriggerFixture *fixture,
                            gconstpointer   user_data)
{
    LrgTrigger2D *trigger;
    gfloat x, y, w, h;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->circle_trigger);
    lrg_trigger2d_get_bounds (trigger, &x, &y, &w, &h);

    g_assert_cmpfloat (x, ==, 40.0f);  /* 50 - 10 */
    g_assert_cmpfloat (y, ==, 40.0f);  /* 50 - 10 */
    g_assert_cmpfloat (w, ==, 20.0f);  /* diameter */
    g_assert_cmpfloat (h, ==, 20.0f);  /* diameter */
}

static void
test_trigger_circle_distance (TriggerFixture *fixture,
                              gconstpointer   user_data)
{
    gfloat dist;

    (void)user_data;

    /* Center should be -radius */
    dist = lrg_trigger_circle_distance_to_point (fixture->circle_trigger, 50.0f, 50.0f);
    g_assert_cmpfloat (dist, ==, -10.0f);

    /* On edge should be 0 */
    dist = lrg_trigger_circle_distance_to_point (fixture->circle_trigger, 60.0f, 50.0f);
    g_assert_cmpfloat (dist, ==, 0.0f);

    /* Outside */
    dist = lrg_trigger_circle_distance_to_point (fixture->circle_trigger, 70.0f, 50.0f);
    g_assert_cmpfloat (dist, ==, 10.0f);
}

static void
test_trigger_circle_shape (TriggerFixture *fixture,
                           gconstpointer   user_data)
{
    (void)user_data;

    g_assert_cmpint (lrg_trigger2d_get_shape (LRG_TRIGGER2D (fixture->circle_trigger)),
                     ==, LRG_TRIGGER2D_SHAPE_CIRCLE);
}

/* ==========================================================================
 * Polygon Trigger Tests
 * ========================================================================== */

static void
test_trigger_polygon_creation (TriggerFixture *fixture,
                               gconstpointer   user_data)
{
    (void)user_data;

    g_assert_nonnull (fixture->polygon_trigger);
    g_assert_true (LRG_IS_TRIGGER_POLYGON (fixture->polygon_trigger));
    g_assert_true (LRG_IS_TRIGGER2D (fixture->polygon_trigger));

    g_assert_cmpstr (lrg_trigger2d_get_id (LRG_TRIGGER2D (fixture->polygon_trigger)),
                     ==, "test_polygon");
}

static void
test_trigger_polygon_vertices (TriggerFixture *fixture,
                               gconstpointer   user_data)
{
    gfloat x, y;
    gboolean result;

    (void)user_data;

    g_assert_cmpuint (lrg_trigger_polygon_get_vertex_count (fixture->polygon_trigger), ==, 3);

    result = lrg_trigger_polygon_get_vertex (fixture->polygon_trigger, 0, &x, &y);
    g_assert_true (result);
    g_assert_cmpfloat (x, ==, 100.0f);
    g_assert_cmpfloat (y, ==, 100.0f);

    result = lrg_trigger_polygon_get_vertex (fixture->polygon_trigger, 1, &x, &y);
    g_assert_true (result);
    g_assert_cmpfloat (x, ==, 120.0f);
    g_assert_cmpfloat (y, ==, 100.0f);

    result = lrg_trigger_polygon_get_vertex (fixture->polygon_trigger, 2, &x, &y);
    g_assert_true (result);
    g_assert_cmpfloat (x, ==, 110.0f);
    g_assert_cmpfloat (y, ==, 120.0f);

    /* Invalid index */
    result = lrg_trigger_polygon_get_vertex (fixture->polygon_trigger, 5, &x, &y);
    g_assert_false (result);
}

static void
test_trigger_polygon_point_test (TriggerFixture *fixture,
                                 gconstpointer   user_data)
{
    LrgTrigger2D *trigger;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->polygon_trigger);

    /* Inside tests - center of triangle should be inside */
    g_assert_true (lrg_trigger2d_test_point (trigger, 110.0f, 107.0f));

    /* Outside tests */
    g_assert_false (lrg_trigger2d_test_point (trigger, 90.0f, 100.0f));   /* Left */
    g_assert_false (lrg_trigger2d_test_point (trigger, 130.0f, 100.0f));  /* Right */
    g_assert_false (lrg_trigger2d_test_point (trigger, 110.0f, 90.0f));   /* Top */
    g_assert_false (lrg_trigger2d_test_point (trigger, 110.0f, 130.0f));  /* Bottom */
}

static void
test_trigger_polygon_bounds (TriggerFixture *fixture,
                             gconstpointer   user_data)
{
    LrgTrigger2D *trigger;
    gfloat x, y, w, h;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->polygon_trigger);
    lrg_trigger2d_get_bounds (trigger, &x, &y, &w, &h);

    g_assert_cmpfloat (x, ==, 100.0f);
    g_assert_cmpfloat (y, ==, 100.0f);
    g_assert_cmpfloat (w, ==, 20.0f);   /* 120 - 100 */
    g_assert_cmpfloat (h, ==, 20.0f);   /* 120 - 100 */
}

static void
test_trigger_polygon_transform (TriggerFixture *fixture,
                                gconstpointer   user_data)
{
    gfloat x, y;

    (void)user_data;

    /* Translate */
    lrg_trigger_polygon_translate (fixture->polygon_trigger, 10.0f, 10.0f);

    lrg_trigger_polygon_get_vertex (fixture->polygon_trigger, 0, &x, &y);
    g_assert_cmpfloat (x, ==, 110.0f);
    g_assert_cmpfloat (y, ==, 110.0f);
}

static void
test_trigger_polygon_area (TriggerFixture *fixture,
                           gconstpointer   user_data)
{
    gfloat area;

    (void)user_data;

    /* Triangle: base=20, height=20, area = 0.5 * 20 * 20 = 200 */
    area = lrg_trigger_polygon_get_area (fixture->polygon_trigger);
    g_assert_cmpfloat (area, ==, 200.0f);
}

static void
test_trigger_polygon_validity (TriggerFixture *fixture,
                               gconstpointer   user_data)
{
    g_autoptr(LrgTriggerPolygon) empty = NULL;

    (void)user_data;

    g_assert_true (lrg_trigger_polygon_is_valid (fixture->polygon_trigger));

    /* Empty polygon is not valid */
    empty = lrg_trigger_polygon_new ();
    g_assert_false (lrg_trigger_polygon_is_valid (empty));

    /* 2 vertices is not valid */
    lrg_trigger_polygon_add_vertex (empty, 0.0f, 0.0f);
    lrg_trigger_polygon_add_vertex (empty, 10.0f, 10.0f);
    g_assert_false (lrg_trigger_polygon_is_valid (empty));

    /* 3 vertices is valid */
    lrg_trigger_polygon_add_vertex (empty, 20.0f, 0.0f);
    g_assert_true (lrg_trigger_polygon_is_valid (empty));
}

static void
test_trigger_polygon_shape (TriggerFixture *fixture,
                            gconstpointer   user_data)
{
    (void)user_data;

    g_assert_cmpint (lrg_trigger2d_get_shape (LRG_TRIGGER2D (fixture->polygon_trigger)),
                     ==, LRG_TRIGGER2D_SHAPE_POLYGON);
}

/* ==========================================================================
 * Base Trigger Tests
 * ========================================================================== */

static void
test_trigger_enabled (TriggerFixture *fixture,
                      gconstpointer   user_data)
{
    LrgTrigger2D *trigger;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->rect_trigger);

    /* Default is enabled */
    g_assert_true (lrg_trigger2d_is_enabled (trigger));

    lrg_trigger2d_set_enabled (trigger, FALSE);
    g_assert_false (lrg_trigger2d_is_enabled (trigger));

    lrg_trigger2d_set_enabled (trigger, TRUE);
    g_assert_true (lrg_trigger2d_is_enabled (trigger));
}

static void
test_trigger_one_shot (TriggerFixture *fixture,
                       gconstpointer   user_data)
{
    LrgTrigger2D *trigger;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->rect_trigger);

    /* Default is not one-shot */
    g_assert_false (lrg_trigger2d_is_one_shot (trigger));
    g_assert_false (lrg_trigger2d_has_fired (trigger));

    lrg_trigger2d_set_one_shot (trigger, TRUE);
    g_assert_true (lrg_trigger2d_is_one_shot (trigger));

    /* Reset should clear fired state */
    lrg_trigger2d_reset (trigger);
    g_assert_false (lrg_trigger2d_has_fired (trigger));
}

static void
test_trigger_cooldown (TriggerFixture *fixture,
                       gconstpointer   user_data)
{
    LrgTrigger2D *trigger;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->rect_trigger);

    /* Default cooldown is 0 */
    g_assert_cmpfloat (lrg_trigger2d_get_cooldown (trigger), ==, 0.0f);
    g_assert_false (lrg_trigger2d_is_on_cooldown (trigger));

    /* Set cooldown */
    lrg_trigger2d_set_cooldown (trigger, 1.0f);
    g_assert_cmpfloat (lrg_trigger2d_get_cooldown (trigger), ==, 1.0f);
}

static void
test_trigger_collision_layers (TriggerFixture *fixture,
                               gconstpointer   user_data)
{
    LrgTrigger2D *trigger;

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->rect_trigger);

    /* Default layer is 1, mask is G_MAXUINT32 (collide with all layers) */
    g_assert_cmpuint (lrg_trigger2d_get_collision_layer (trigger), ==, 1);
    g_assert_cmpuint (lrg_trigger2d_get_collision_mask (trigger), ==, G_MAXUINT32);

    /* Set custom layer */
    lrg_trigger2d_set_collision_layer (trigger, 0x02);
    lrg_trigger2d_set_collision_mask (trigger, 0x04);

    g_assert_cmpuint (lrg_trigger2d_get_collision_layer (trigger), ==, 0x02);
    g_assert_cmpuint (lrg_trigger2d_get_collision_mask (trigger), ==, 0x04);

    /* Test collision compatibility */
    g_assert_true (lrg_trigger2d_can_collide_with (trigger, 0x04));   /* Matches mask */
    g_assert_false (lrg_trigger2d_can_collide_with (trigger, 0x08));  /* Doesn't match */
    g_assert_false (lrg_trigger2d_can_collide_with (trigger, 0x01));  /* Doesn't match */
}

/* ==========================================================================
 * Trigger Event Tests
 * ========================================================================== */

static void
test_trigger_event_creation (void)
{
    g_autoptr(LrgTriggerEvent) event = NULL;
    gpointer dummy_entity = GINT_TO_POINTER (0x12345);

    event = lrg_trigger_event_new (LRG_TRIGGER2D_EVENT_ENTER,
                                   dummy_entity,
                                   100.0f, 200.0f);

    g_assert_nonnull (event);
    g_assert_cmpint (lrg_trigger_event_get_event_type (event), ==, LRG_TRIGGER2D_EVENT_ENTER);
    g_assert_true (lrg_trigger_event_get_entity (event) == dummy_entity);
    g_assert_cmpfloat (lrg_trigger_event_get_x (event), ==, 100.0f);
    g_assert_cmpfloat (lrg_trigger_event_get_y (event), ==, 200.0f);
}

static void
test_trigger_event_types (void)
{
    g_autoptr(LrgTriggerEvent) enter_event = NULL;
    g_autoptr(LrgTriggerEvent) stay_event = NULL;
    g_autoptr(LrgTriggerEvent) exit_event = NULL;

    enter_event = lrg_trigger_event_new (LRG_TRIGGER2D_EVENT_ENTER, NULL, 0, 0);
    stay_event = lrg_trigger_event_new (LRG_TRIGGER2D_EVENT_STAY, NULL, 0, 0);
    exit_event = lrg_trigger_event_new (LRG_TRIGGER2D_EVENT_EXIT, NULL, 0, 0);

    g_assert_true (lrg_trigger_event_is_enter (enter_event));
    g_assert_false (lrg_trigger_event_is_stay (enter_event));
    g_assert_false (lrg_trigger_event_is_exit (enter_event));

    g_assert_false (lrg_trigger_event_is_enter (stay_event));
    g_assert_true (lrg_trigger_event_is_stay (stay_event));
    g_assert_false (lrg_trigger_event_is_exit (stay_event));

    g_assert_false (lrg_trigger_event_is_enter (exit_event));
    g_assert_false (lrg_trigger_event_is_stay (exit_event));
    g_assert_true (lrg_trigger_event_is_exit (exit_event));
}

static void
test_trigger_event_copy (void)
{
    g_autoptr(LrgTriggerEvent) original = NULL;
    g_autoptr(LrgTriggerEvent) copy = NULL;

    original = lrg_trigger_event_new (LRG_TRIGGER2D_EVENT_STAY,
                                      GINT_TO_POINTER (0x42),
                                      50.0f, 75.0f);

    copy = lrg_trigger_event_copy (original);

    g_assert_nonnull (copy);
    g_assert_true (copy != original);
    g_assert_cmpint (lrg_trigger_event_get_event_type (copy), ==, LRG_TRIGGER2D_EVENT_STAY);
    g_assert_true (lrg_trigger_event_get_entity (copy) == GINT_TO_POINTER (0x42));
    g_assert_cmpfloat (lrg_trigger_event_get_x (copy), ==, 50.0f);
    g_assert_cmpfloat (lrg_trigger_event_get_y (copy), ==, 75.0f);
}

/* ==========================================================================
 * Trigger Manager Tests
 * ========================================================================== */

static void
test_manager_creation (TriggerFixture *fixture,
                       gconstpointer   user_data)
{
    (void)user_data;

    g_assert_nonnull (fixture->manager);
    g_assert_true (LRG_IS_TRIGGER_MANAGER (fixture->manager));
    g_assert_cmpuint (lrg_trigger_manager_get_trigger_count (fixture->manager), ==, 0);
}

static void
test_manager_add_remove_trigger (TriggerFixture *fixture,
                                 gconstpointer   user_data)
{
    gboolean result;

    (void)user_data;

    /* Add triggers */
    lrg_trigger_manager_add_trigger (fixture->manager,
                                     LRG_TRIGGER2D (fixture->rect_trigger));
    g_assert_cmpuint (lrg_trigger_manager_get_trigger_count (fixture->manager), ==, 1);

    lrg_trigger_manager_add_trigger (fixture->manager,
                                     LRG_TRIGGER2D (fixture->circle_trigger));
    g_assert_cmpuint (lrg_trigger_manager_get_trigger_count (fixture->manager), ==, 2);

    /* Get by ID */
    g_assert_true (lrg_trigger_manager_get_trigger (fixture->manager, "test_rect") ==
                   LRG_TRIGGER2D (fixture->rect_trigger));
    g_assert_true (lrg_trigger_manager_get_trigger (fixture->manager, "test_circle") ==
                   LRG_TRIGGER2D (fixture->circle_trigger));
    g_assert_null (lrg_trigger_manager_get_trigger (fixture->manager, "nonexistent"));

    /* Remove by trigger */
    result = lrg_trigger_manager_remove_trigger (fixture->manager,
                                                 LRG_TRIGGER2D (fixture->rect_trigger));
    g_assert_true (result);
    g_assert_cmpuint (lrg_trigger_manager_get_trigger_count (fixture->manager), ==, 1);

    /* Remove by ID */
    result = lrg_trigger_manager_remove_trigger_by_id (fixture->manager, "test_circle");
    g_assert_true (result);
    g_assert_cmpuint (lrg_trigger_manager_get_trigger_count (fixture->manager), ==, 0);

    /* Remove nonexistent */
    result = lrg_trigger_manager_remove_trigger_by_id (fixture->manager, "nonexistent");
    g_assert_false (result);
}

static void
test_manager_entity_tracking (TriggerFixture *fixture,
                              gconstpointer   user_data)
{
    gpointer entity1 = GINT_TO_POINTER (0x100);
    gpointer entity2 = GINT_TO_POINTER (0x200);

    (void)user_data;

    /* Register entities */
    lrg_trigger_manager_register_entity (fixture->manager, entity1, 0x01);
    lrg_trigger_manager_register_entity (fixture->manager, entity2, 0x02);

    /* Set positions */
    lrg_trigger_manager_set_entity_position (fixture->manager, entity1, 20.0f, 20.0f);
    lrg_trigger_manager_set_entity_position (fixture->manager, entity2, 50.0f, 50.0f);

    /* Unregister */
    lrg_trigger_manager_unregister_entity (fixture->manager, entity1);
    lrg_trigger_manager_unregister_entity (fixture->manager, entity2);
}

static void
test_manager_check_point (TriggerFixture *fixture,
                          gconstpointer   user_data)
{
    g_autoptr(GPtrArray) results = NULL;

    (void)user_data;

    /* Add triggers */
    lrg_trigger_manager_add_trigger (fixture->manager,
                                     LRG_TRIGGER2D (fixture->rect_trigger));
    lrg_trigger_manager_add_trigger (fixture->manager,
                                     LRG_TRIGGER2D (fixture->circle_trigger));

    /* Check point inside rect */
    results = lrg_trigger_manager_check_point (fixture->manager, 20.0f, 20.0f, 0x01);
    g_assert_cmpuint (results->len, ==, 1);
    g_assert_true (g_ptr_array_index (results, 0) == fixture->rect_trigger);
    g_clear_pointer (&results, g_ptr_array_unref);

    /* Check point inside circle */
    results = lrg_trigger_manager_check_point (fixture->manager, 50.0f, 50.0f, 0x01);
    g_assert_cmpuint (results->len, ==, 1);
    g_assert_true (g_ptr_array_index (results, 0) == fixture->circle_trigger);
    g_clear_pointer (&results, g_ptr_array_unref);

    /* Check point outside both */
    results = lrg_trigger_manager_check_point (fixture->manager, 200.0f, 200.0f, 0x01);
    g_assert_cmpuint (results->len, ==, 0);
}

static void
test_manager_debug_mode (TriggerFixture *fixture,
                         gconstpointer   user_data)
{
    (void)user_data;

    g_assert_false (lrg_trigger_manager_is_debug_enabled (fixture->manager));

    lrg_trigger_manager_set_debug_enabled (fixture->manager, TRUE);
    g_assert_true (lrg_trigger_manager_is_debug_enabled (fixture->manager));

    lrg_trigger_manager_set_debug_enabled (fixture->manager, FALSE);
    g_assert_false (lrg_trigger_manager_is_debug_enabled (fixture->manager));
}

static void
test_manager_clear (TriggerFixture *fixture,
                    gconstpointer   user_data)
{
    (void)user_data;

    /* Add triggers */
    lrg_trigger_manager_add_trigger (fixture->manager,
                                     LRG_TRIGGER2D (fixture->rect_trigger));
    lrg_trigger_manager_add_trigger (fixture->manager,
                                     LRG_TRIGGER2D (fixture->circle_trigger));
    g_assert_cmpuint (lrg_trigger_manager_get_trigger_count (fixture->manager), ==, 2);

    /* Clear */
    lrg_trigger_manager_clear (fixture->manager);
    g_assert_cmpuint (lrg_trigger_manager_get_trigger_count (fixture->manager), ==, 0);
}

/* ==========================================================================
 * Signal Tracking Helper
 * ========================================================================== */

typedef struct
{
    gint enter_count;
    gint stay_count;
    gint exit_count;
    LrgTrigger2D *last_trigger;
} SignalCounter;

static void
on_triggered_signal (LrgTrigger2D          *trigger,
                     LrgTrigger2DEventType  event_type,
                     gpointer               entity,
                     gpointer               user_data)
{
    SignalCounter *counter = user_data;

    (void)entity;

    counter->last_trigger = trigger;

    switch (event_type)
    {
    case LRG_TRIGGER2D_EVENT_ENTER:
        counter->enter_count++;
        break;
    case LRG_TRIGGER2D_EVENT_STAY:
        counter->stay_count++;
        break;
    case LRG_TRIGGER2D_EVENT_EXIT:
        counter->exit_count++;
        break;
    }
}

static void
test_trigger_signals (TriggerFixture *fixture,
                      gconstpointer   user_data)
{
    LrgTrigger2D  *trigger;
    SignalCounter  counter = { 0 };
    gpointer       entity = GINT_TO_POINTER (0x42);

    (void)user_data;

    trigger = LRG_TRIGGER2D (fixture->rect_trigger);

    /* Connect signal */
    g_signal_connect (trigger, "triggered",
                      G_CALLBACK (on_triggered_signal), &counter);

    /* Add trigger and entity to manager */
    lrg_trigger_manager_add_trigger (fixture->manager, trigger);
    lrg_trigger_manager_register_entity (fixture->manager, entity, 0x01);

    /* Move entity into trigger */
    lrg_trigger_manager_set_entity_position (fixture->manager, entity, 20.0f, 20.0f);
    lrg_trigger_manager_update (fixture->manager, 0.016f);

    g_assert_cmpint (counter.enter_count, ==, 1);
    g_assert_cmpint (counter.stay_count, ==, 0);
    g_assert_cmpint (counter.exit_count, ==, 0);

    /* Stay in trigger */
    lrg_trigger_manager_set_entity_position (fixture->manager, entity, 21.0f, 21.0f);
    lrg_trigger_manager_update (fixture->manager, 0.016f);

    g_assert_cmpint (counter.enter_count, ==, 1);
    g_assert_cmpint (counter.stay_count, ==, 1);
    g_assert_cmpint (counter.exit_count, ==, 0);

    /* Move entity out of trigger */
    lrg_trigger_manager_set_entity_position (fixture->manager, entity, 100.0f, 100.0f);
    lrg_trigger_manager_update (fixture->manager, 0.016f);

    g_assert_cmpint (counter.enter_count, ==, 1);
    g_assert_cmpint (counter.stay_count, ==, 1);
    g_assert_cmpint (counter.exit_count, ==, 1);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Rectangle tests */
    g_test_add ("/trigger2d/rect/creation", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_rect_creation, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/rect/properties", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_rect_properties, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/rect/point-test", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_rect_point_test, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/rect/bounds", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_rect_bounds, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/rect/center", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_rect_center, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/rect/shape", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_rect_shape, trigger_fixture_tear_down);

    /* Circle tests */
    g_test_add ("/trigger2d/circle/creation", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_circle_creation, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/circle/properties", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_circle_properties, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/circle/point-test", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_circle_point_test, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/circle/bounds", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_circle_bounds, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/circle/distance", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_circle_distance, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/circle/shape", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_circle_shape, trigger_fixture_tear_down);

    /* Polygon tests */
    g_test_add ("/trigger2d/polygon/creation", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_polygon_creation, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/polygon/vertices", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_polygon_vertices, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/polygon/point-test", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_polygon_point_test, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/polygon/bounds", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_polygon_bounds, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/polygon/transform", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_polygon_transform, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/polygon/area", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_polygon_area, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/polygon/validity", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_polygon_validity, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/polygon/shape", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_polygon_shape, trigger_fixture_tear_down);

    /* Base trigger tests */
    g_test_add ("/trigger2d/base/enabled", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_enabled, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/base/one-shot", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_one_shot, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/base/cooldown", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_cooldown, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/base/collision-layers", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_collision_layers, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/base/signals", TriggerFixture, NULL,
                trigger_fixture_set_up, test_trigger_signals, trigger_fixture_tear_down);

    /* Event tests */
    g_test_add_func ("/trigger2d/event/creation", test_trigger_event_creation);
    g_test_add_func ("/trigger2d/event/types", test_trigger_event_types);
    g_test_add_func ("/trigger2d/event/copy", test_trigger_event_copy);

    /* Manager tests */
    g_test_add ("/trigger2d/manager/creation", TriggerFixture, NULL,
                trigger_fixture_set_up, test_manager_creation, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/manager/add-remove", TriggerFixture, NULL,
                trigger_fixture_set_up, test_manager_add_remove_trigger, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/manager/entity-tracking", TriggerFixture, NULL,
                trigger_fixture_set_up, test_manager_entity_tracking, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/manager/check-point", TriggerFixture, NULL,
                trigger_fixture_set_up, test_manager_check_point, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/manager/debug-mode", TriggerFixture, NULL,
                trigger_fixture_set_up, test_manager_debug_mode, trigger_fixture_tear_down);
    g_test_add ("/trigger2d/manager/clear", TriggerFixture, NULL,
                trigger_fixture_set_up, test_manager_clear, trigger_fixture_tear_down);

    return g_test_run ();
}
