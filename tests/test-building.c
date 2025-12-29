/* test-building.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the building module.
 */

#include <glib.h>
#include <locale.h>

#include "../src/building/lrg-building-def.h"
#include "../src/building/lrg-building-instance.h"
#include "../src/building/lrg-build-grid.h"
#include "../src/building/lrg-placement-system.h"

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

typedef struct
{
    LrgBuildingDef *house;
    LrgBuildingDef *factory;
} BuildingDefFixture;

static void
building_def_fixture_set_up (BuildingDefFixture *fixture,
                             gconstpointer       user_data)
{
    (void)user_data;

    /* Create a small house */
    fixture->house = lrg_building_def_new ("house");
    lrg_building_def_set_name (fixture->house, "House");
    lrg_building_def_set_size (fixture->house, 2, 2);
    lrg_building_def_set_category (fixture->house, LRG_BUILDING_CATEGORY_RESIDENTIAL);
    lrg_building_def_set_max_level (fixture->house, 3);
    lrg_building_def_set_buildable_on (fixture->house, LRG_TERRAIN_GRASS | LRG_TERRAIN_DIRT);

    /* Create a large factory */
    fixture->factory = lrg_building_def_new ("factory");
    lrg_building_def_set_name (fixture->factory, "Factory");
    lrg_building_def_set_size (fixture->factory, 4, 3);
    lrg_building_def_set_category (fixture->factory, LRG_BUILDING_CATEGORY_PRODUCTION);
    lrg_building_def_set_max_level (fixture->factory, 5);
    lrg_building_def_set_buildable_on (fixture->factory, LRG_TERRAIN_GRASS);
}

static void
building_def_fixture_tear_down (BuildingDefFixture *fixture,
                                gconstpointer       user_data)
{
    (void)user_data;

    g_clear_object (&fixture->house);
    g_clear_object (&fixture->factory);
}

typedef struct
{
    LrgBuildingDef      *def;
    LrgBuildingInstance *instance;
} BuildingInstanceFixture;

static void
building_instance_fixture_set_up (BuildingInstanceFixture *fixture,
                                  gconstpointer            user_data)
{
    (void)user_data;

    fixture->def = lrg_building_def_new ("test-building");
    lrg_building_def_set_name (fixture->def, "Test Building");
    lrg_building_def_set_size (fixture->def, 2, 2);
    lrg_building_def_set_max_level (fixture->def, 3);

    fixture->instance = lrg_building_instance_new (fixture->def, 5, 10);
}

static void
building_instance_fixture_tear_down (BuildingInstanceFixture *fixture,
                                     gconstpointer            user_data)
{
    (void)user_data;

    g_clear_object (&fixture->instance);
    g_clear_object (&fixture->def);
}

typedef struct
{
    LrgBuildGrid   *grid;
    LrgBuildingDef *small_def;
    LrgBuildingDef *large_def;
} BuildGridFixture;

static void
build_grid_fixture_set_up (BuildGridFixture *fixture,
                           gconstpointer     user_data)
{
    (void)user_data;

    fixture->grid = lrg_build_grid_new (16, 16, 32.0);
    lrg_build_grid_fill_terrain (fixture->grid, LRG_TERRAIN_GRASS);

    fixture->small_def = lrg_building_def_new ("small");
    lrg_building_def_set_size (fixture->small_def, 1, 1);
    lrg_building_def_set_buildable_on (fixture->small_def, LRG_TERRAIN_GRASS);

    fixture->large_def = lrg_building_def_new ("large");
    lrg_building_def_set_size (fixture->large_def, 3, 2);
    lrg_building_def_set_buildable_on (fixture->large_def, LRG_TERRAIN_GRASS);
}

static void
build_grid_fixture_tear_down (BuildGridFixture *fixture,
                              gconstpointer     user_data)
{
    (void)user_data;

    g_clear_object (&fixture->grid);
    g_clear_object (&fixture->small_def);
    g_clear_object (&fixture->large_def);
}

typedef struct
{
    LrgBuildGrid       *grid;
    LrgPlacementSystem *system;
    LrgBuildingDef     *def;
} PlacementFixture;

static void
placement_fixture_set_up (PlacementFixture *fixture,
                          gconstpointer     user_data)
{
    (void)user_data;

    fixture->grid = lrg_build_grid_new (16, 16, 32.0);
    lrg_build_grid_fill_terrain (fixture->grid, LRG_TERRAIN_GRASS);

    fixture->system = lrg_placement_system_new (fixture->grid);

    fixture->def = lrg_building_def_new ("test");
    lrg_building_def_set_size (fixture->def, 2, 2);
    lrg_building_def_set_buildable_on (fixture->def, LRG_TERRAIN_GRASS);
}

static void
placement_fixture_tear_down (PlacementFixture *fixture,
                             gconstpointer     user_data)
{
    (void)user_data;

    g_clear_object (&fixture->system);
    g_clear_object (&fixture->grid);
    g_clear_object (&fixture->def);
}

/* ============================================================================
 * LrgBuildingDef Tests
 * ============================================================================ */

static void
test_building_def_new (BuildingDefFixture *fixture,
                       gconstpointer       user_data)
{
    (void)user_data;

    g_assert_nonnull (fixture->house);
    g_assert_cmpstr (lrg_building_def_get_id (fixture->house), ==, "house");
    g_assert_cmpstr (lrg_building_def_get_name (fixture->house), ==, "House");
}

static void
test_building_def_size (BuildingDefFixture *fixture,
                        gconstpointer       user_data)
{
    (void)user_data;

    g_assert_cmpint (lrg_building_def_get_width (fixture->house), ==, 2);
    g_assert_cmpint (lrg_building_def_get_height (fixture->house), ==, 2);
    g_assert_cmpint (lrg_building_def_get_width (fixture->factory), ==, 4);
    g_assert_cmpint (lrg_building_def_get_height (fixture->factory), ==, 3);
}

static void
test_building_def_category (BuildingDefFixture *fixture,
                            gconstpointer       user_data)
{
    (void)user_data;

    g_assert_cmpint (lrg_building_def_get_category (fixture->house), ==,
                     LRG_BUILDING_CATEGORY_RESIDENTIAL);
    g_assert_cmpint (lrg_building_def_get_category (fixture->factory), ==,
                     LRG_BUILDING_CATEGORY_PRODUCTION);
}

static void
test_building_def_buildable_on (BuildingDefFixture *fixture,
                                gconstpointer       user_data)
{
    (void)user_data;

    g_assert_true (lrg_building_def_get_buildable_on (fixture->house) &
                   LRG_TERRAIN_GRASS);
    g_assert_true (lrg_building_def_get_buildable_on (fixture->house) &
                   LRG_TERRAIN_DIRT);
    g_assert_false (lrg_building_def_get_buildable_on (fixture->house) &
                    LRG_TERRAIN_WATER);
}

static void
test_building_def_costs (void)
{
    g_autoptr(LrgBuildingDef) def = NULL;
    g_autoptr(LrgBuildCost) upgrade_cost = NULL;
    const LrgBuildCost *cost;

    def = lrg_building_def_new ("test");

    /* Set initial cost using simple API */
    lrg_building_def_set_cost_simple (def, "gold", 100.0);
    cost = lrg_building_def_get_cost (def);
    g_assert_nonnull (cost);
    g_assert_cmpfloat_with_epsilon (lrg_build_cost_get (cost, "gold"), 100.0, 0.001);

    /* Set upgrade cost using LrgBuildCost object */
    upgrade_cost = lrg_build_cost_new ();
    lrg_build_cost_set (upgrade_cost, "gold", 200.0);
    lrg_building_def_set_upgrade_cost (def, 2, upgrade_cost);
    cost = lrg_building_def_get_upgrade_cost (def, 2);
    g_assert_nonnull (cost);
    g_assert_cmpfloat_with_epsilon (lrg_build_cost_get (cost, "gold"), 200.0, 0.001);
}

/* ============================================================================
 * LrgBuildingInstance Tests
 * ============================================================================ */

static void
test_building_instance_new (BuildingInstanceFixture *fixture,
                            gconstpointer            user_data)
{
    (void)user_data;

    g_assert_nonnull (fixture->instance);
    g_assert_true (lrg_building_instance_get_definition (fixture->instance) == fixture->def);
    g_assert_cmpint (lrg_building_instance_get_grid_x (fixture->instance), ==, 5);
    g_assert_cmpint (lrg_building_instance_get_grid_y (fixture->instance), ==, 10);
}

static void
test_building_instance_position (BuildingInstanceFixture *fixture,
                                 gconstpointer            user_data)
{
    (void)user_data;

    lrg_building_instance_set_position (fixture->instance, 20, 30);

    g_assert_cmpint (lrg_building_instance_get_grid_x (fixture->instance), ==, 20);
    g_assert_cmpint (lrg_building_instance_get_grid_y (fixture->instance), ==, 30);
}

static void
test_building_instance_rotation (BuildingInstanceFixture *fixture,
                                 gconstpointer            user_data)
{
    (void)user_data;

    g_assert_cmpint (lrg_building_instance_get_rotation (fixture->instance), ==,
                     LRG_ROTATION_0);

    lrg_building_instance_rotate_cw (fixture->instance);
    g_assert_cmpint (lrg_building_instance_get_rotation (fixture->instance), ==,
                     LRG_ROTATION_90);

    lrg_building_instance_rotate_cw (fixture->instance);
    g_assert_cmpint (lrg_building_instance_get_rotation (fixture->instance), ==,
                     LRG_ROTATION_180);

    lrg_building_instance_rotate_ccw (fixture->instance);
    g_assert_cmpint (lrg_building_instance_get_rotation (fixture->instance), ==,
                     LRG_ROTATION_90);
}

static void
test_building_instance_effective_size (BuildingInstanceFixture *fixture,
                                       gconstpointer            user_data)
{
    (void)user_data;

    /* Set asymmetric size */
    lrg_building_def_set_size (fixture->def, 3, 1);

    /* No rotation */
    lrg_building_instance_set_rotation (fixture->instance, LRG_ROTATION_0);
    g_assert_cmpint (lrg_building_instance_get_effective_width (fixture->instance), ==, 3);
    g_assert_cmpint (lrg_building_instance_get_effective_height (fixture->instance), ==, 1);

    /* 90 degrees - dimensions swapped */
    lrg_building_instance_set_rotation (fixture->instance, LRG_ROTATION_90);
    g_assert_cmpint (lrg_building_instance_get_effective_width (fixture->instance), ==, 1);
    g_assert_cmpint (lrg_building_instance_get_effective_height (fixture->instance), ==, 3);
}

static void
test_building_instance_upgrade (BuildingInstanceFixture *fixture,
                                gconstpointer            user_data)
{
    (void)user_data;

    g_assert_cmpint (lrg_building_instance_get_level (fixture->instance), ==, 1);
    g_assert_true (lrg_building_instance_can_upgrade (fixture->instance));

    g_assert_true (lrg_building_instance_upgrade (fixture->instance));
    g_assert_cmpint (lrg_building_instance_get_level (fixture->instance), ==, 2);

    g_assert_true (lrg_building_instance_upgrade (fixture->instance));
    g_assert_cmpint (lrg_building_instance_get_level (fixture->instance), ==, 3);

    /* At max level */
    g_assert_false (lrg_building_instance_can_upgrade (fixture->instance));
    g_assert_false (lrg_building_instance_upgrade (fixture->instance));
}

static void
test_building_instance_health (BuildingInstanceFixture *fixture,
                               gconstpointer            user_data)
{
    (void)user_data;

    lrg_building_instance_set_max_health (fixture->instance, 100.0);

    g_assert_cmpfloat_with_epsilon (lrg_building_instance_get_health (fixture->instance),
                                    100.0, 0.001);
    g_assert_cmpfloat_with_epsilon (lrg_building_instance_get_max_health (fixture->instance),
                                    100.0, 0.001);

    g_assert_false (lrg_building_instance_damage (fixture->instance, 30.0));
    g_assert_cmpfloat_with_epsilon (lrg_building_instance_get_health (fixture->instance),
                                    70.0, 0.001);

    lrg_building_instance_repair (fixture->instance, 20.0);
    g_assert_cmpfloat_with_epsilon (lrg_building_instance_get_health (fixture->instance),
                                    90.0, 0.001);
}

static void
test_building_instance_destroy (BuildingInstanceFixture *fixture,
                                gconstpointer            user_data)
{
    (void)user_data;

    lrg_building_instance_set_max_health (fixture->instance, 100.0);

    g_assert_false (lrg_building_instance_is_destroyed (fixture->instance));

    /* Should destroy when health reaches 0 */
    g_assert_true (lrg_building_instance_damage (fixture->instance, 150.0));
    g_assert_true (lrg_building_instance_is_destroyed (fixture->instance));
    g_assert_cmpfloat_with_epsilon (lrg_building_instance_get_health (fixture->instance),
                                    0.0, 0.001);
}

static void
test_building_instance_user_data (BuildingInstanceFixture *fixture,
                                  gconstpointer            user_data)
{
    gint *data;
    gpointer retrieved;

    (void)user_data;

    data = g_new (gint, 1);
    *data = 42;

    lrg_building_instance_set_data (fixture->instance, "my-data", data, g_free);

    retrieved = lrg_building_instance_get_data (fixture->instance, "my-data");
    g_assert_nonnull (retrieved);
    g_assert_cmpint (*(gint *)retrieved, ==, 42);

    /* Overwrite */
    data = g_new (gint, 1);
    *data = 99;
    lrg_building_instance_set_data (fixture->instance, "my-data", data, g_free);

    retrieved = lrg_building_instance_get_data (fixture->instance, "my-data");
    g_assert_cmpint (*(gint *)retrieved, ==, 99);
}

static void
test_building_instance_contains_cell (BuildingInstanceFixture *fixture,
                                      gconstpointer            user_data)
{
    (void)user_data;

    /* Building at (5, 10) with size 2x2 */
    lrg_building_def_set_size (fixture->def, 2, 2);
    lrg_building_instance_set_position (fixture->instance, 5, 10);

    g_assert_true (lrg_building_instance_contains_cell (fixture->instance, 5, 10));
    g_assert_true (lrg_building_instance_contains_cell (fixture->instance, 6, 10));
    g_assert_true (lrg_building_instance_contains_cell (fixture->instance, 5, 11));
    g_assert_true (lrg_building_instance_contains_cell (fixture->instance, 6, 11));

    g_assert_false (lrg_building_instance_contains_cell (fixture->instance, 4, 10));
    g_assert_false (lrg_building_instance_contains_cell (fixture->instance, 7, 10));
    g_assert_false (lrg_building_instance_contains_cell (fixture->instance, 5, 9));
    g_assert_false (lrg_building_instance_contains_cell (fixture->instance, 5, 12));
}

/* ============================================================================
 * LrgBuildGrid Tests
 * ============================================================================ */

static void
test_build_grid_new (BuildGridFixture *fixture,
                     gconstpointer     user_data)
{
    (void)user_data;

    g_assert_nonnull (fixture->grid);
    g_assert_cmpint (lrg_build_grid_get_width (fixture->grid), ==, 16);
    g_assert_cmpint (lrg_build_grid_get_height (fixture->grid), ==, 16);
    g_assert_cmpfloat_with_epsilon (lrg_build_grid_get_cell_size (fixture->grid),
                                    32.0, 0.001);
}

static void
test_build_grid_get_cell (BuildGridFixture *fixture,
                          gconstpointer     user_data)
{
    LrgBuildCell *cell;

    (void)user_data;

    cell = lrg_build_grid_get_cell (fixture->grid, 5, 10);
    g_assert_nonnull (cell);
    g_assert_cmpint (cell->x, ==, 5);
    g_assert_cmpint (cell->y, ==, 10);

    /* Out of bounds */
    cell = lrg_build_grid_get_cell (fixture->grid, -1, 0);
    g_assert_null (cell);

    cell = lrg_build_grid_get_cell (fixture->grid, 100, 100);
    g_assert_null (cell);
}

static void
test_build_grid_terrain (BuildGridFixture *fixture,
                         gconstpointer     user_data)
{
    (void)user_data;

    g_assert_cmpint (lrg_build_grid_get_terrain (fixture->grid, 0, 0), ==,
                     LRG_TERRAIN_GRASS);

    lrg_build_grid_set_terrain (fixture->grid, 5, 5, LRG_TERRAIN_WATER);
    g_assert_cmpint (lrg_build_grid_get_terrain (fixture->grid, 5, 5), ==,
                     LRG_TERRAIN_WATER);

    /* Rect fill */
    lrg_build_grid_set_terrain_rect (fixture->grid, 10, 10, 3, 3, LRG_TERRAIN_ROAD);
    g_assert_cmpint (lrg_build_grid_get_terrain (fixture->grid, 11, 11), ==,
                     LRG_TERRAIN_ROAD);
}

static void
test_build_grid_blocked (BuildGridFixture *fixture,
                         gconstpointer     user_data)
{
    (void)user_data;

    g_assert_false (lrg_build_grid_is_blocked (fixture->grid, 5, 5));

    lrg_build_grid_set_blocked (fixture->grid, 5, 5, TRUE);
    g_assert_true (lrg_build_grid_is_blocked (fixture->grid, 5, 5));

    lrg_build_grid_set_blocked (fixture->grid, 5, 5, FALSE);
    g_assert_false (lrg_build_grid_is_blocked (fixture->grid, 5, 5));
}

static void
test_build_grid_is_area_free (BuildGridFixture *fixture,
                              gconstpointer     user_data)
{
    (void)user_data;

    g_assert_true (lrg_build_grid_is_area_free (fixture->grid, 0, 0, 3, 3));

    /* Block one cell */
    lrg_build_grid_set_blocked (fixture->grid, 1, 1, TRUE);
    g_assert_false (lrg_build_grid_is_area_free (fixture->grid, 0, 0, 3, 3));

    /* Area outside blocked cell */
    g_assert_true (lrg_build_grid_is_area_free (fixture->grid, 5, 5, 3, 3));
}

static void
test_build_grid_can_place (BuildGridFixture *fixture,
                           gconstpointer     user_data)
{
    (void)user_data;

    /* Should be placeable on grass */
    g_assert_true (lrg_build_grid_can_place (fixture->grid, fixture->small_def,
                                             0, 0, LRG_ROTATION_0));

    /* Block the cell */
    lrg_build_grid_set_blocked (fixture->grid, 0, 0, TRUE);
    g_assert_false (lrg_build_grid_can_place (fixture->grid, fixture->small_def,
                                              0, 0, LRG_ROTATION_0));

    /* Wrong terrain */
    lrg_build_grid_set_blocked (fixture->grid, 5, 5, FALSE);
    lrg_build_grid_set_terrain (fixture->grid, 5, 5, LRG_TERRAIN_WATER);
    g_assert_false (lrg_build_grid_can_place (fixture->grid, fixture->small_def,
                                              5, 5, LRG_ROTATION_0));
}

static void
test_build_grid_coordinate_conversion (BuildGridFixture *fixture,
                                       gconstpointer     user_data)
{
    gint    cell_x;
    gint    cell_y;
    gdouble world_x;
    gdouble world_y;

    (void)user_data;

    /* World to cell */
    lrg_build_grid_world_to_cell (fixture->grid, 50.0, 100.0, &cell_x, &cell_y);
    g_assert_cmpint (cell_x, ==, 1);
    g_assert_cmpint (cell_y, ==, 3);

    /* Cell to world (center) */
    lrg_build_grid_cell_to_world (fixture->grid, 5, 10, &world_x, &world_y);
    g_assert_cmpfloat_with_epsilon (world_x, 176.0, 0.001);  /* (5 + 0.5) * 32 */
    g_assert_cmpfloat_with_epsilon (world_y, 336.0, 0.001);  /* (10 + 0.5) * 32 */
}

static void
test_build_grid_place_building (BuildGridFixture *fixture,
                                gconstpointer     user_data)
{
    g_autoptr(LrgBuildingInstance) building = NULL;
    g_autoptr(LrgBuildingInstance) building2 = NULL;

    (void)user_data;

    building = lrg_building_instance_new (fixture->small_def, 5, 5);

    g_assert_true (lrg_build_grid_place_building (fixture->grid, building));

    /* Cell should now have building */
    g_assert_true (lrg_build_grid_get_building_at (fixture->grid, 5, 5) == building);

    /* Area should no longer be free */
    g_assert_false (lrg_build_grid_is_area_free (fixture->grid, 5, 5, 1, 1));

    /* Cannot place another building there - expect warning */
    building2 = lrg_building_instance_new (fixture->small_def, 5, 5);
    g_test_expect_message ("Libregnum-Building", G_LOG_LEVEL_WARNING,
                           "Cannot place building at (5, 5)");
    g_assert_false (lrg_build_grid_place_building (fixture->grid, building2));
    g_test_assert_expected_messages ();
}

static void
test_build_grid_remove_building (BuildGridFixture *fixture,
                                 gconstpointer     user_data)
{
    g_autoptr(LrgBuildingInstance) building = NULL;

    (void)user_data;

    building = lrg_building_instance_new (fixture->small_def, 5, 5);
    lrg_build_grid_place_building (fixture->grid, building);

    g_assert_true (lrg_build_grid_remove_building (fixture->grid, building));

    /* Cell should be free again */
    g_assert_null (lrg_build_grid_get_building_at (fixture->grid, 5, 5));
    g_assert_true (lrg_build_grid_is_area_free (fixture->grid, 5, 5, 1, 1));
}

static void
test_build_grid_get_all_buildings (BuildGridFixture *fixture,
                                   gconstpointer     user_data)
{
    g_autoptr(LrgBuildingInstance) b1 = NULL;
    g_autoptr(LrgBuildingInstance) b2 = NULL;
    g_autoptr(GPtrArray) buildings = NULL;

    (void)user_data;

    b1 = lrg_building_instance_new (fixture->small_def, 0, 0);
    b2 = lrg_building_instance_new (fixture->small_def, 5, 5);

    lrg_build_grid_place_building (fixture->grid, b1);
    lrg_build_grid_place_building (fixture->grid, b2);

    buildings = lrg_build_grid_get_all_buildings (fixture->grid);
    g_assert_cmpuint (buildings->len, ==, 2);
}

static void
test_build_grid_out_of_bounds (BuildGridFixture *fixture,
                               gconstpointer     user_data)
{
    g_autoptr(LrgBuildingInstance) building = NULL;

    (void)user_data;

    /* Try to place building at edge that would overflow */
    building = lrg_building_instance_new (fixture->large_def, 14, 15);

    /* 3x2 at (14, 15) would need cells (14-16, 15-16), but grid is 16x16 */
    g_assert_false (lrg_build_grid_can_place (fixture->grid, fixture->large_def,
                                              14, 15, LRG_ROTATION_0));
}

/* ============================================================================
 * LrgPlacementSystem Tests
 * ============================================================================ */

static void
test_placement_start (PlacementFixture *fixture,
                      gconstpointer     user_data)
{
    (void)user_data;

    g_assert_cmpint (lrg_placement_system_get_state (fixture->system), ==,
                     LRG_PLACEMENT_STATE_IDLE);
    g_assert_false (lrg_placement_system_is_placing (fixture->system));

    g_assert_true (lrg_placement_system_start_placement (fixture->system, fixture->def));

    g_assert_cmpint (lrg_placement_system_get_state (fixture->system), ==,
                     LRG_PLACEMENT_STATE_PLACING);
    g_assert_true (lrg_placement_system_is_placing (fixture->system));
    g_assert_true (lrg_placement_system_get_current_definition (fixture->system) ==
                   fixture->def);
}

static void
test_placement_cancel (PlacementFixture *fixture,
                       gconstpointer     user_data)
{
    (void)user_data;

    lrg_placement_system_start_placement (fixture->system, fixture->def);
    lrg_placement_system_cancel (fixture->system);

    g_assert_cmpint (lrg_placement_system_get_state (fixture->system), ==,
                     LRG_PLACEMENT_STATE_IDLE);
    g_assert_false (lrg_placement_system_is_placing (fixture->system));
}

static void
test_placement_update_position (PlacementFixture *fixture,
                                gconstpointer     user_data)
{
    gint grid_x;
    gint grid_y;

    (void)user_data;

    lrg_placement_system_start_placement (fixture->system, fixture->def);

    /* World coordinates at cell (3, 4) */
    lrg_placement_system_update_position (fixture->system, 100.0, 140.0);

    lrg_placement_system_get_grid_position (fixture->system, &grid_x, &grid_y);
    g_assert_cmpint (grid_x, ==, 3);
    g_assert_cmpint (grid_y, ==, 4);
}

static void
test_placement_rotate (PlacementFixture *fixture,
                       gconstpointer     user_data)
{
    (void)user_data;

    lrg_placement_system_start_placement (fixture->system, fixture->def);

    g_assert_cmpint (lrg_placement_system_get_rotation (fixture->system), ==,
                     LRG_ROTATION_0);

    lrg_placement_system_rotate_cw (fixture->system);
    g_assert_cmpint (lrg_placement_system_get_rotation (fixture->system), ==,
                     LRG_ROTATION_90);

    lrg_placement_system_rotate_ccw (fixture->system);
    g_assert_cmpint (lrg_placement_system_get_rotation (fixture->system), ==,
                     LRG_ROTATION_0);
}

static void
test_placement_is_valid (PlacementFixture *fixture,
                         gconstpointer     user_data)
{
    (void)user_data;

    lrg_placement_system_start_placement (fixture->system, fixture->def);
    lrg_placement_system_set_grid_position (fixture->system, 5, 5);

    /* Should be valid on grass */
    g_assert_true (lrg_placement_system_is_valid (fixture->system));

    /* Block the area */
    lrg_build_grid_set_blocked (fixture->grid, 5, 5, TRUE);
    /* Move away then back to force validity recheck (position change optimization) */
    lrg_placement_system_set_grid_position (fixture->system, 6, 6);
    lrg_placement_system_set_grid_position (fixture->system, 5, 5);
    g_assert_false (lrg_placement_system_is_valid (fixture->system));
}

static void
test_placement_confirm (PlacementFixture *fixture,
                        gconstpointer     user_data)
{
    LrgBuildingInstance *building;

    (void)user_data;

    lrg_placement_system_start_placement (fixture->system, fixture->def);
    lrg_placement_system_set_grid_position (fixture->system, 5, 5);

    building = lrg_placement_system_confirm (fixture->system);

    g_assert_nonnull (building);
    g_assert_cmpint (lrg_building_instance_get_grid_x (building), ==, 5);
    g_assert_cmpint (lrg_building_instance_get_grid_y (building), ==, 5);

    /* Should exit placement mode */
    g_assert_false (lrg_placement_system_is_placing (fixture->system));

    /* Building should be on grid */
    g_assert_true (lrg_build_grid_get_building_at (fixture->grid, 5, 5) == building);
}

static void
test_placement_confirm_invalid (PlacementFixture *fixture,
                                gconstpointer     user_data)
{
    LrgBuildingInstance *building;

    (void)user_data;

    lrg_placement_system_start_placement (fixture->system, fixture->def);

    /* Block the placement location */
    lrg_build_grid_set_blocked (fixture->grid, 0, 0, TRUE);
    lrg_placement_system_set_grid_position (fixture->system, 0, 0);

    /* Expect warnings when attempting to place on blocked cell */
    g_test_expect_message ("Libregnum-Building", G_LOG_LEVEL_WARNING,
                           "Cannot place building at (0, 0)");
    g_test_expect_message ("Libregnum-Building", G_LOG_LEVEL_WARNING,
                           "Failed to place building on grid");
    building = lrg_placement_system_confirm (fixture->system);
    g_test_assert_expected_messages ();

    g_assert_null (building);
    /* Should still be in placement mode */
    g_assert_true (lrg_placement_system_is_placing (fixture->system));
}

static void
test_placement_demolish (PlacementFixture *fixture,
                         gconstpointer     user_data)
{
    g_autoptr(LrgBuildingInstance) building = NULL;
    LrgBuildingInstance *demolished;

    (void)user_data;

    /* Place a building first */
    building = lrg_building_instance_new (fixture->def, 5, 5);
    lrg_build_grid_place_building (fixture->grid, building);

    lrg_placement_system_start_demolition (fixture->system);
    g_assert_true (lrg_placement_system_is_demolishing (fixture->system));

    demolished = lrg_placement_system_demolish_at (fixture->system, 5, 5);
    g_assert_true (demolished == building);

    /* Building should be removed from grid */
    g_assert_null (lrg_build_grid_get_building_at (fixture->grid, 5, 5));
}

/* ============================================================================
 * LrgBuildCell Tests
 * ============================================================================ */

static void
test_build_cell_new (void)
{
    LrgBuildCell *cell;

    cell = lrg_build_cell_new (5, 10);

    g_assert_nonnull (cell);
    g_assert_cmpint (cell->x, ==, 5);
    g_assert_cmpint (cell->y, ==, 10);
    g_assert_cmpint (cell->terrain, ==, LRG_TERRAIN_GRASS);
    g_assert_null (cell->building);
    g_assert_false (cell->blocked);

    lrg_build_cell_free (cell);
}

static void
test_build_cell_copy (void)
{
    LrgBuildCell *cell;
    LrgBuildCell *copy;

    cell = lrg_build_cell_new (3, 7);
    cell->terrain = LRG_TERRAIN_WATER;
    cell->blocked = TRUE;

    copy = lrg_build_cell_copy (cell);

    g_assert_nonnull (copy);
    g_assert_cmpint (copy->x, ==, 3);
    g_assert_cmpint (copy->y, ==, 7);
    g_assert_cmpint (copy->terrain, ==, LRG_TERRAIN_WATER);
    g_assert_true (copy->blocked);

    lrg_build_cell_free (cell);
    lrg_build_cell_free (copy);
}

static void
test_build_cell_is_free (void)
{
    LrgBuildCell *cell;

    cell = lrg_build_cell_new (0, 0);

    g_assert_true (lrg_build_cell_is_free (cell));

    cell->blocked = TRUE;
    g_assert_false (lrg_build_cell_is_free (cell));

    lrg_build_cell_free (cell);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int
main (int   argc,
      char *argv[])
{
    setlocale (LC_ALL, "");

    g_test_init (&argc, &argv, NULL);

    /* LrgBuildingDef tests */
    g_test_add ("/building/def/new", BuildingDefFixture, NULL,
                building_def_fixture_set_up,
                test_building_def_new,
                building_def_fixture_tear_down);
    g_test_add ("/building/def/size", BuildingDefFixture, NULL,
                building_def_fixture_set_up,
                test_building_def_size,
                building_def_fixture_tear_down);
    g_test_add ("/building/def/category", BuildingDefFixture, NULL,
                building_def_fixture_set_up,
                test_building_def_category,
                building_def_fixture_tear_down);
    g_test_add ("/building/def/buildable-on", BuildingDefFixture, NULL,
                building_def_fixture_set_up,
                test_building_def_buildable_on,
                building_def_fixture_tear_down);
    g_test_add_func ("/building/def/costs", test_building_def_costs);

    /* LrgBuildingInstance tests */
    g_test_add ("/building/instance/new", BuildingInstanceFixture, NULL,
                building_instance_fixture_set_up,
                test_building_instance_new,
                building_instance_fixture_tear_down);
    g_test_add ("/building/instance/position", BuildingInstanceFixture, NULL,
                building_instance_fixture_set_up,
                test_building_instance_position,
                building_instance_fixture_tear_down);
    g_test_add ("/building/instance/rotation", BuildingInstanceFixture, NULL,
                building_instance_fixture_set_up,
                test_building_instance_rotation,
                building_instance_fixture_tear_down);
    g_test_add ("/building/instance/effective-size", BuildingInstanceFixture, NULL,
                building_instance_fixture_set_up,
                test_building_instance_effective_size,
                building_instance_fixture_tear_down);
    g_test_add ("/building/instance/upgrade", BuildingInstanceFixture, NULL,
                building_instance_fixture_set_up,
                test_building_instance_upgrade,
                building_instance_fixture_tear_down);
    g_test_add ("/building/instance/health", BuildingInstanceFixture, NULL,
                building_instance_fixture_set_up,
                test_building_instance_health,
                building_instance_fixture_tear_down);
    g_test_add ("/building/instance/destroy", BuildingInstanceFixture, NULL,
                building_instance_fixture_set_up,
                test_building_instance_destroy,
                building_instance_fixture_tear_down);
    g_test_add ("/building/instance/user-data", BuildingInstanceFixture, NULL,
                building_instance_fixture_set_up,
                test_building_instance_user_data,
                building_instance_fixture_tear_down);
    g_test_add ("/building/instance/contains-cell", BuildingInstanceFixture, NULL,
                building_instance_fixture_set_up,
                test_building_instance_contains_cell,
                building_instance_fixture_tear_down);

    /* LrgBuildGrid tests */
    g_test_add ("/building/grid/new", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_new,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/get-cell", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_get_cell,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/terrain", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_terrain,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/blocked", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_blocked,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/is-area-free", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_is_area_free,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/can-place", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_can_place,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/coordinate-conversion", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_coordinate_conversion,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/place-building", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_place_building,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/remove-building", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_remove_building,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/get-all-buildings", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_get_all_buildings,
                build_grid_fixture_tear_down);
    g_test_add ("/building/grid/out-of-bounds", BuildGridFixture, NULL,
                build_grid_fixture_set_up,
                test_build_grid_out_of_bounds,
                build_grid_fixture_tear_down);

    /* LrgPlacementSystem tests */
    g_test_add ("/building/placement/start", PlacementFixture, NULL,
                placement_fixture_set_up,
                test_placement_start,
                placement_fixture_tear_down);
    g_test_add ("/building/placement/cancel", PlacementFixture, NULL,
                placement_fixture_set_up,
                test_placement_cancel,
                placement_fixture_tear_down);
    g_test_add ("/building/placement/update-position", PlacementFixture, NULL,
                placement_fixture_set_up,
                test_placement_update_position,
                placement_fixture_tear_down);
    g_test_add ("/building/placement/rotate", PlacementFixture, NULL,
                placement_fixture_set_up,
                test_placement_rotate,
                placement_fixture_tear_down);
    g_test_add ("/building/placement/is-valid", PlacementFixture, NULL,
                placement_fixture_set_up,
                test_placement_is_valid,
                placement_fixture_tear_down);
    g_test_add ("/building/placement/confirm", PlacementFixture, NULL,
                placement_fixture_set_up,
                test_placement_confirm,
                placement_fixture_tear_down);
    g_test_add ("/building/placement/confirm-invalid", PlacementFixture, NULL,
                placement_fixture_set_up,
                test_placement_confirm_invalid,
                placement_fixture_tear_down);
    g_test_add ("/building/placement/demolish", PlacementFixture, NULL,
                placement_fixture_set_up,
                test_placement_demolish,
                placement_fixture_tear_down);

    /* LrgBuildCell tests */
    g_test_add_func ("/building/cell/new", test_build_cell_new);
    g_test_add_func ("/building/cell/copy", test_build_cell_copy);
    g_test_add_func ("/building/cell/is-free", test_build_cell_is_free);

    return g_test_run ();
}
