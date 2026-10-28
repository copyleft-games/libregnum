/* test-world3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for World3D module: bounding boxes, spawn points, triggers, octree,
 * portals, sectors, levels, and portal system.
 */

#include <glib.h>
#include <libregnum.h>

/* =============================================================================
 * BoundingBox3D Tests
 * =============================================================================
 */

static void
test_bounding_box3d_new (void)
{
    g_autoptr(LrgBoundingBox3D) box = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 10.0f, 10.0f, 10.0f };

    box = lrg_bounding_box3d_new_from_vectors (&min, &max);

    g_assert_nonnull (box);
    g_assert_cmpfloat (box->min.x, ==, 0.0f);
    g_assert_cmpfloat (box->max.x, ==, 10.0f);
}

static void
test_bounding_box3d_from_center (void)
{
    g_autoptr(LrgBoundingBox3D) box = NULL;
    GrlVector3 center = { 5.0f, 5.0f, 5.0f };

    box = lrg_bounding_box3d_new_from_center (&center, 5.0f);

    g_assert_nonnull (box);
    g_assert_cmpfloat (box->min.x, ==, 0.0f);
    g_assert_cmpfloat (box->max.x, ==, 10.0f);
}

static void
test_bounding_box3d_contains_point (void)
{
    g_autoptr(LrgBoundingBox3D) box = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 10.0f, 10.0f, 10.0f };

    box = lrg_bounding_box3d_new_from_vectors (&min, &max);

    g_assert_true (lrg_bounding_box3d_contains_point_xyz (box, 5.0f, 5.0f, 5.0f));
    g_assert_false (lrg_bounding_box3d_contains_point_xyz (box, 15.0f, 5.0f, 5.0f));
}

static void
test_bounding_box3d_intersects (void)
{
    g_autoptr(LrgBoundingBox3D) box1 = NULL;
    g_autoptr(LrgBoundingBox3D) box2 = NULL;
    g_autoptr(LrgBoundingBox3D) box3 = NULL;
    GrlVector3 min1 = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max1 = { 10.0f, 10.0f, 10.0f };
    GrlVector3 min2 = { 5.0f, 5.0f, 5.0f };
    GrlVector3 max2 = { 15.0f, 15.0f, 15.0f };
    GrlVector3 min3 = { 20.0f, 20.0f, 20.0f };
    GrlVector3 max3 = { 30.0f, 30.0f, 30.0f };

    box1 = lrg_bounding_box3d_new_from_vectors (&min1, &max1);
    box2 = lrg_bounding_box3d_new_from_vectors (&min2, &max2);
    box3 = lrg_bounding_box3d_new_from_vectors (&min3, &max3);

    g_assert_true (lrg_bounding_box3d_intersects (box1, box2));
    g_assert_false (lrg_bounding_box3d_intersects (box1, box3));
}

static void
test_bounding_box3d_merge (void)
{
    g_autoptr(LrgBoundingBox3D) box1 = NULL;
    g_autoptr(LrgBoundingBox3D) box2 = NULL;
    g_autoptr(LrgBoundingBox3D) merged = NULL;
    GrlVector3 min1 = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max1 = { 5.0f, 5.0f, 5.0f };
    GrlVector3 min2 = { 3.0f, 3.0f, 3.0f };
    GrlVector3 max2 = { 10.0f, 10.0f, 10.0f };

    box1 = lrg_bounding_box3d_new_from_vectors (&min1, &max1);
    box2 = lrg_bounding_box3d_new_from_vectors (&min2, &max2);

    merged = lrg_bounding_box3d_merge (box1, box2);

    g_assert_nonnull (merged);
    g_assert_cmpfloat (merged->min.x, ==, 0.0f);
    g_assert_cmpfloat (merged->max.x, ==, 10.0f);
}

/* =============================================================================
 * SpawnPoint3D Tests
 * =============================================================================
 */

static void
test_spawn_point3d_new (void)
{
    g_autoptr(LrgSpawnPoint3D) spawn = NULL;

    spawn = lrg_spawn_point3d_new ("spawn1", 5.0f, 0.0f, 10.0f, LRG_SPAWN_TYPE_PLAYER);

    g_assert_nonnull (spawn);
    g_assert_cmpstr (lrg_spawn_point3d_get_id (spawn), ==, "spawn1");
    g_assert_cmpint (lrg_spawn_point3d_get_spawn_type (spawn), ==, LRG_SPAWN_TYPE_PLAYER);
}

static void
test_spawn_point3d_properties (void)
{
    g_autoptr(LrgSpawnPoint3D) spawn = NULL;
    GValue value = G_VALUE_INIT;

    spawn = lrg_spawn_point3d_new ("spawn1", 0.0f, 0.0f, 0.0f, LRG_SPAWN_TYPE_ENEMY);

    lrg_spawn_point3d_set_entity_type (spawn, "goblin");
    g_assert_cmpstr (lrg_spawn_point3d_get_entity_type (spawn), ==, "goblin");

    g_value_init (&value, G_TYPE_INT);
    g_value_set_int (&value, 42);
    lrg_spawn_point3d_set_property (spawn, "level", &value);
    g_value_unset (&value);

    g_assert_true (lrg_spawn_point3d_has_property (spawn, "level"));
    g_assert_false (lrg_spawn_point3d_has_property (spawn, "health"));
}

/* =============================================================================
 * Trigger3D Tests
 * =============================================================================
 */

static void
test_trigger3d_new (void)
{
    g_autoptr(LrgTrigger3D) trigger = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 10.0f, 10.0f, 10.0f };

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    trigger = lrg_trigger3d_new ("trigger1", bounds, LRG_TRIGGER_TYPE_ENTER);

    g_assert_nonnull (trigger);
    g_assert_cmpstr (lrg_trigger3d_get_id (trigger), ==, "trigger1");
    g_assert_cmpint (lrg_trigger3d_get_trigger_type (trigger), ==, LRG_TRIGGER_TYPE_ENTER);
}

static void
test_trigger3d_test_point (void)
{
    g_autoptr(LrgTrigger3D) trigger = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 10.0f, 10.0f, 10.0f };

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    trigger = lrg_trigger3d_new ("trigger1", bounds, LRG_TRIGGER_TYPE_ENTER);

    g_assert_true (lrg_trigger3d_test_point_xyz (trigger, 5.0f, 5.0f, 5.0f));
    g_assert_false (lrg_trigger3d_test_point_xyz (trigger, 15.0f, 5.0f, 5.0f));

    lrg_trigger3d_set_enabled (trigger, FALSE);
    g_assert_false (lrg_trigger3d_test_point_xyz (trigger, 5.0f, 5.0f, 5.0f));
}

/* =============================================================================
 * Octree Tests
 * =============================================================================
 */

static void
test_octree_new (void)
{
    g_autoptr(LrgOctree) octree = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { -100.0f, -100.0f, -100.0f };
    GrlVector3 max = { 100.0f, 100.0f, 100.0f };

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    octree = lrg_octree_new (bounds);

    g_assert_nonnull (octree);
    g_assert_cmpuint (lrg_octree_get_object_count (octree), ==, 0);
}

static void
test_octree_insert (void)
{
    g_autoptr(LrgOctree) octree = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    g_autoptr(LrgBoundingBox3D) obj_bounds = NULL;
    GrlVector3 min = { -100.0f, -100.0f, -100.0f };
    GrlVector3 max = { 100.0f, 100.0f, 100.0f };
    GrlVector3 obj_min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 obj_max = { 5.0f, 5.0f, 5.0f };
    gint dummy_object = 42;

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    octree = lrg_octree_new (bounds);

    obj_bounds = lrg_bounding_box3d_new_from_vectors (&obj_min, &obj_max);
    g_assert_true (lrg_octree_insert (octree, &dummy_object, obj_bounds));
    g_assert_cmpuint (lrg_octree_get_object_count (octree), ==, 1);
}

static void
test_octree_query_box (void)
{
    g_autoptr(LrgOctree) octree = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    g_autoptr(LrgBoundingBox3D) obj_bounds = NULL;
    g_autoptr(LrgBoundingBox3D) query_bounds = NULL;
    g_autoptr(GPtrArray) results = NULL;
    GrlVector3 min = { -100.0f, -100.0f, -100.0f };
    GrlVector3 max = { 100.0f, 100.0f, 100.0f };
    GrlVector3 obj_min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 obj_max = { 5.0f, 5.0f, 5.0f };
    GrlVector3 query_min = { -10.0f, -10.0f, -10.0f };
    GrlVector3 query_max = { 10.0f, 10.0f, 10.0f };
    gint dummy_object = 42;

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    octree = lrg_octree_new (bounds);

    obj_bounds = lrg_bounding_box3d_new_from_vectors (&obj_min, &obj_max);
    lrg_octree_insert (octree, &dummy_object, obj_bounds);

    query_bounds = lrg_bounding_box3d_new_from_vectors (&query_min, &query_max);
    results = lrg_octree_query_box (octree, query_bounds);

    g_assert_nonnull (results);
    g_assert_cmpuint (results->len, ==, 1);
}

static void
test_octree_remove (void)
{
    g_autoptr(LrgOctree) octree = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    g_autoptr(LrgBoundingBox3D) obj_bounds = NULL;
    GrlVector3 min = { -100.0f, -100.0f, -100.0f };
    GrlVector3 max = { 100.0f, 100.0f, 100.0f };
    GrlVector3 obj_min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 obj_max = { 5.0f, 5.0f, 5.0f };
    gint dummy_object = 42;

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    octree = lrg_octree_new (bounds);

    obj_bounds = lrg_bounding_box3d_new_from_vectors (&obj_min, &obj_max);
    lrg_octree_insert (octree, &dummy_object, obj_bounds);
    g_assert_cmpuint (lrg_octree_get_object_count (octree), ==, 1);

    g_assert_true (lrg_octree_remove (octree, &dummy_object));
    g_assert_cmpuint (lrg_octree_get_object_count (octree), ==, 0);
}

/* =============================================================================
 * Portal Tests
 * =============================================================================
 */

static void
test_portal_new (void)
{
    g_autoptr(LrgPortal) portal = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 5.0f, 0.0f, 0.0f };
    GrlVector3 max = { 5.0f, 10.0f, 10.0f };

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    portal = lrg_portal_new ("portal1", bounds, "sector_a", "sector_b");

    g_assert_nonnull (portal);
    g_assert_cmpstr (lrg_portal_get_id (portal), ==, "portal1");
    g_assert_cmpstr (lrg_portal_get_sector_a (portal), ==, "sector_a");
    g_assert_cmpstr (lrg_portal_get_sector_b (portal), ==, "sector_b");
}

static void
test_portal_get_other_sector (void)
{
    g_autoptr(LrgPortal) portal = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 1.0f, 10.0f, 10.0f };

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    portal = lrg_portal_new ("portal1", bounds, "sector_a", "sector_b");

    g_assert_cmpstr (lrg_portal_get_other_sector (portal, "sector_a"), ==, "sector_b");
    g_assert_cmpstr (lrg_portal_get_other_sector (portal, "sector_b"), ==, "sector_a");
    g_assert_null (lrg_portal_get_other_sector (portal, "sector_c"));
}

/* =============================================================================
 * Sector Tests
 * =============================================================================
 */

static void
test_sector_new (void)
{
    g_autoptr(LrgSector) sector = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 100.0f, 50.0f, 100.0f };

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    sector = lrg_sector_new ("sector1", bounds);

    g_assert_nonnull (sector);
    g_assert_cmpstr (lrg_sector_get_id (sector), ==, "sector1");
    g_assert_cmpuint (lrg_sector_get_portal_count (sector), ==, 0);
}

static void
test_sector_portals (void)
{
    g_autoptr(LrgSector) sector = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 100.0f, 50.0f, 100.0f };

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    sector = lrg_sector_new ("sector1", bounds);

    lrg_sector_add_portal (sector, "portal1");
    lrg_sector_add_portal (sector, "portal2");

    g_assert_cmpuint (lrg_sector_get_portal_count (sector), ==, 2);
    g_assert_true (lrg_sector_has_portal (sector, "portal1"));
    g_assert_true (lrg_sector_has_portal (sector, "portal2"));
    g_assert_false (lrg_sector_has_portal (sector, "portal3"));

    g_assert_true (lrg_sector_remove_portal (sector, "portal1"));
    g_assert_cmpuint (lrg_sector_get_portal_count (sector), ==, 1);
}

static void
test_sector_contains_point (void)
{
    g_autoptr(LrgSector) sector = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 100.0f, 50.0f, 100.0f };

    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    sector = lrg_sector_new ("sector1", bounds);

    g_assert_true (lrg_sector_contains_point_xyz (sector, 50.0f, 25.0f, 50.0f));
    g_assert_false (lrg_sector_contains_point_xyz (sector, 150.0f, 25.0f, 50.0f));
}

/* =============================================================================
 * Level3D Tests
 * =============================================================================
 */

static void
test_level3d_new (void)
{
    g_autoptr(LrgLevel3D) level = NULL;

    level = lrg_level3d_new ("test_level");

    g_assert_nonnull (level);
    g_assert_cmpstr (lrg_level3d_get_id (level), ==, "test_level");
    g_assert_cmpuint (lrg_level3d_get_spawn_point_count (level), ==, 0);
    g_assert_cmpuint (lrg_level3d_get_trigger_count (level), ==, 0);
}

static void
test_level3d_spawn_points (void)
{
    g_autoptr(LrgLevel3D) level = NULL;
    g_autoptr(LrgSpawnPoint3D) spawn = NULL;

    level = lrg_level3d_new ("test_level");
    spawn = lrg_spawn_point3d_new ("spawn1", 0.0f, 0.0f, 0.0f, LRG_SPAWN_TYPE_PLAYER);

    lrg_level3d_add_spawn_point (level, spawn);

    g_assert_cmpuint (lrg_level3d_get_spawn_point_count (level), ==, 1);
    g_assert_nonnull (lrg_level3d_get_spawn_point (level, "spawn1"));
    g_assert_null (lrg_level3d_get_spawn_point (level, "nonexistent"));

    g_assert_true (lrg_level3d_remove_spawn_point (level, "spawn1"));
    g_assert_cmpuint (lrg_level3d_get_spawn_point_count (level), ==, 0);
}

static void
test_level3d_triggers (void)
{
    g_autoptr(LrgLevel3D) level = NULL;
    g_autoptr(LrgTrigger3D) trigger = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 10.0f, 10.0f, 10.0f };

    level = lrg_level3d_new ("test_level");
    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    trigger = lrg_trigger3d_new ("trigger1", bounds, LRG_TRIGGER_TYPE_ENTER);

    lrg_level3d_add_trigger (level, trigger);

    g_assert_cmpuint (lrg_level3d_get_trigger_count (level), ==, 1);
    g_assert_nonnull (lrg_level3d_get_trigger (level, "trigger1"));

    g_assert_true (lrg_level3d_remove_trigger (level, "trigger1"));
    g_assert_cmpuint (lrg_level3d_get_trigger_count (level), ==, 0);
}

static void
test_level3d_check_triggers (void)
{
    g_autoptr(LrgLevel3D) level = NULL;
    g_autoptr(LrgTrigger3D) trigger = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    g_autoptr(GPtrArray) triggered = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 10.0f, 10.0f, 10.0f };
    GrlVector3 inside = { 5.0f, 5.0f, 5.0f };
    GrlVector3 outside = { 50.0f, 50.0f, 50.0f };

    level = lrg_level3d_new ("test_level");
    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    trigger = lrg_trigger3d_new ("trigger1", bounds, LRG_TRIGGER_TYPE_ENTER);

    lrg_level3d_add_trigger (level, trigger);

    triggered = lrg_level3d_check_triggers (level, &inside);
    g_assert_cmpuint (triggered->len, ==, 1);

    g_ptr_array_unref (triggered);
    triggered = lrg_level3d_check_triggers (level, &outside);
    g_assert_cmpuint (triggered->len, ==, 0);
}

/* =============================================================================
 * PortalSystem Tests
 * =============================================================================
 */

static void
test_portal_system_new (void)
{
    g_autoptr(LrgPortalSystem) system = NULL;

    system = lrg_portal_system_new ();

    g_assert_nonnull (system);
    g_assert_cmpuint (lrg_portal_system_get_sector_count (system), ==, 0);
    g_assert_cmpuint (lrg_portal_system_get_portal_count (system), ==, 0);
}

static void
test_portal_system_sectors (void)
{
    g_autoptr(LrgPortalSystem) system = NULL;
    g_autoptr(LrgSector) sector = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 100.0f, 50.0f, 100.0f };

    system = lrg_portal_system_new ();
    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    sector = lrg_sector_new ("sector1", bounds);

    lrg_portal_system_add_sector (system, sector);

    g_assert_cmpuint (lrg_portal_system_get_sector_count (system), ==, 1);
    g_assert_nonnull (lrg_portal_system_get_sector (system, "sector1"));

    g_assert_true (lrg_portal_system_remove_sector (system, "sector1"));
    g_assert_cmpuint (lrg_portal_system_get_sector_count (system), ==, 0);
}

static void
test_portal_system_visibility (void)
{
    g_autoptr(LrgPortalSystem) system = NULL;
    g_autoptr(LrgSector) sector1 = NULL;
    g_autoptr(LrgSector) sector2 = NULL;
    g_autoptr(LrgPortal) portal = NULL;
    g_autoptr(LrgBoundingBox3D) bounds1 = NULL;
    g_autoptr(LrgBoundingBox3D) bounds2 = NULL;
    g_autoptr(LrgBoundingBox3D) portal_bounds = NULL;
    GrlVector3 min1 = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max1 = { 100.0f, 50.0f, 100.0f };
    GrlVector3 min2 = { 100.0f, 0.0f, 0.0f };
    GrlVector3 max2 = { 200.0f, 50.0f, 100.0f };
    GrlVector3 portal_min = { 100.0f, 0.0f, 40.0f };
    GrlVector3 portal_max = { 100.0f, 50.0f, 60.0f };
    GrlVector3 camera_pos = { 50.0f, 25.0f, 50.0f };

    system = lrg_portal_system_new ();

    bounds1 = lrg_bounding_box3d_new_from_vectors (&min1, &max1);
    sector1 = lrg_sector_new ("sector1", bounds1);
    lrg_sector_add_portal (sector1, "portal1");

    bounds2 = lrg_bounding_box3d_new_from_vectors (&min2, &max2);
    sector2 = lrg_sector_new ("sector2", bounds2);
    lrg_sector_add_portal (sector2, "portal1");

    portal_bounds = lrg_bounding_box3d_new_from_vectors (&portal_min, &portal_max);
    portal = lrg_portal_new ("portal1", portal_bounds, "sector1", "sector2");

    lrg_portal_system_add_sector (system, sector1);
    lrg_portal_system_add_sector (system, sector2);
    lrg_portal_system_add_portal (system, portal);

    lrg_portal_system_update (system, &camera_pos);

    g_assert_cmpstr (lrg_portal_system_get_current_sector (system), ==, "sector1");
    g_assert_true (lrg_portal_system_is_sector_visible (system, "sector1"));
}

static void
count_visibility_changed (LrgPortalSystem *system,
                          gpointer         user_data)
{
    guint *count = user_data;

    (*count)++;
}

/* visibility-changed fires when the visible set changes, not every update. */
static void
test_portal_system_visibility_changed (void)
{
    g_autoptr(LrgPortalSystem) system = lrg_portal_system_new ();
    g_autoptr(LrgSector) sector1 = NULL;
    g_autoptr(LrgSector) sector2 = NULL;
    g_autoptr(LrgPortal) portal = NULL;
    g_autoptr(LrgBoundingBox3D) bounds1 = NULL;
    g_autoptr(LrgBoundingBox3D) bounds2 = NULL;
    g_autoptr(LrgBoundingBox3D) portal_bounds = NULL;
    GrlVector3 min1 = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max1 = { 100.0f, 50.0f, 100.0f };
    GrlVector3 min2 = { 100.0f, 0.0f, 0.0f };
    GrlVector3 max2 = { 200.0f, 50.0f, 100.0f };
    GrlVector3 portal_min = { 100.0f, 0.0f, 40.0f };
    GrlVector3 portal_max = { 100.0f, 50.0f, 60.0f };
    GrlVector3 normal = { -1.0f, 0.0f, 0.0f };
    GrlVector3 inside = { 50.0f, 25.0f, 50.0f };
    GrlVector3 inside_moved = { 60.0f, 25.0f, 40.0f };
    GrlVector3 beyond = { 150.0f, 25.0f, 50.0f };
    GrlVector3 nowhere = { -50.0f, 25.0f, 50.0f };
    guint changes = 0;

    bounds1 = lrg_bounding_box3d_new_from_vectors (&min1, &max1);
    sector1 = lrg_sector_new ("sector1", bounds1);
    lrg_sector_add_portal (sector1, "portal1");
    bounds2 = lrg_bounding_box3d_new_from_vectors (&min2, &max2);
    sector2 = lrg_sector_new ("sector2", bounds2);
    lrg_sector_add_portal (sector2, "portal1");
    portal_bounds = lrg_bounding_box3d_new_from_vectors (&portal_min, &portal_max);
    /* Faces sector1: seen from sector1, not from sector2. */
    portal = lrg_portal_new_with_normal ("portal1", portal_bounds, "sector1", "sector2", &normal);
    lrg_portal_system_add_sector (system, sector1);
    lrg_portal_system_add_sector (system, sector2);
    lrg_portal_system_add_portal (system, portal);
    g_signal_connect (system, "visibility-changed",
                      G_CALLBACK (count_visibility_changed), &changes);

    lrg_portal_system_update (system, &inside);
    g_assert_cmpuint (changes, ==, 1);
    g_assert_cmpuint (lrg_portal_system_get_visible_sector_count (system), ==, 2);
    g_assert_true (lrg_portal_system_is_sector_visible (system, "sector2"));

    /* Same set, still or moving within the sector: no signal. */
    lrg_portal_system_update (system, &inside);
    lrg_portal_system_update (system, &inside_moved);
    g_assert_cmpuint (changes, ==, 1);

    /* Into sector2, whose side of the portal sees nothing else. */
    lrg_portal_system_update (system, &beyond);
    g_assert_cmpuint (changes, ==, 2);
    g_assert_cmpuint (lrg_portal_system_get_visible_sector_count (system), ==, 1);
    g_assert_false (lrg_portal_system_is_sector_visible (system, "sector1"));

    /* Back, then out of every sector (once), then still out (no change). */
    lrg_portal_system_update (system, &inside);
    g_assert_cmpuint (changes, ==, 3);
    lrg_portal_system_update (system, &nowhere);
    lrg_portal_system_update (system, &nowhere);
    g_assert_cmpuint (changes, ==, 4);
    g_assert_cmpuint (lrg_portal_system_get_visible_sector_count (system), ==, 0);
}

static void
test_portal_system_clear (void)
{
    g_autoptr(LrgPortalSystem) system = NULL;
    g_autoptr(LrgSector) sector = NULL;
    g_autoptr(LrgBoundingBox3D) bounds = NULL;
    GrlVector3 min = { 0.0f, 0.0f, 0.0f };
    GrlVector3 max = { 100.0f, 50.0f, 100.0f };

    system = lrg_portal_system_new ();
    bounds = lrg_bounding_box3d_new_from_vectors (&min, &max);
    sector = lrg_sector_new ("sector1", bounds);

    lrg_portal_system_add_sector (system, sector);
    g_assert_cmpuint (lrg_portal_system_get_sector_count (system), ==, 1);

    lrg_portal_system_clear (system);
    g_assert_cmpuint (lrg_portal_system_get_sector_count (system), ==, 0);
    g_assert_cmpuint (lrg_portal_system_get_portal_count (system), ==, 0);
}

/* =============================================================================
 * Main
 * =============================================================================
 */

/* Reference traversal for test_portal_system_topology_cache(), straight
 * off the public lookups, as the system did before its topology cache. */
static void
reference_traverse (LrgPortalSystem  *system,
                    const gchar      *sector_id,
                    const GrlVector3 *camera,
                    guint             depth,
                    guint             max_depth,
                    GHashTable       *visited,
                    GPtrArray        *order)
{
    const LrgSector *sector;
    g_autoptr(GPtrArray) portal_ids = NULL;
    guint i;

    if (depth >= max_depth || g_hash_table_contains (visited, sector_id))
        return;
    sector = lrg_portal_system_get_sector (system, sector_id);
    if (sector == NULL)
        return;
    g_hash_table_add (visited, g_strdup (sector_id));
    g_ptr_array_add (order, g_strdup (sector_id));

    portal_ids = lrg_sector_get_portal_ids (sector);
    for (i = 0; i < portal_ids->len; i++)
    {
        const LrgPortal *portal = lrg_portal_system_get_portal (system,
                                                               portal_ids->pdata[i]);
        const gchar *other;

        if (portal == NULL || !lrg_portal_is_visible_from (portal, camera))
            continue;
        other = lrg_portal_get_other_sector (portal, sector_id);
        if (other != NULL)
            reference_traverse (system, other, camera, depth + 1, max_depth,
                                visited, order);
    }
}

/* Checks find_sector_at() and the visible list against the references for
 * a batch of random camera positions. */
static void
check_portal_system (LrgPortalSystem *system,
                     GRand           *rng)
{
    guint round;

    for (round = 0; round < 50; round++)
    {
        g_autoptr(GPtrArray) sectors = lrg_portal_system_get_sectors (system);
        g_autoptr(GPtrArray) visible = NULL;
        g_autoptr(GPtrArray) order = g_ptr_array_new_with_free_func (g_free);
        g_autoptr(GHashTable) visited = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                               g_free, NULL);
        const LrgSector *expected = NULL;
        GrlVector3 camera;
        guint i;

        camera.x = (gfloat) g_rand_double_range (rng, -5.0, 65.0);
        camera.y = 5.0f;
        camera.z = (gfloat) g_rand_double_range (rng, -5.0, 65.0);

        /* First containing sector in table order */
        for (i = 0; i < sectors->len && expected == NULL; i++)
            if (lrg_sector_contains_point (sectors->pdata[i], &camera))
                expected = sectors->pdata[i];
        g_assert_true (lrg_portal_system_find_sector_at (system, &camera) == expected);

        lrg_portal_system_update (system, &camera);
        if (expected != NULL)
            reference_traverse (system, lrg_sector_get_id (expected), &camera, 0,
                                lrg_portal_system_get_max_portal_depth (system),
                                visited, order);
        visible = lrg_portal_system_get_visible_sectors (system);
        g_assert_cmpuint (visible->len, ==, order->len);
        for (i = 0; i < order->len; i++)
            g_assert_cmpstr (lrg_sector_get_id (visible->pdata[i]), ==, order->pdata[i]);
    }
}

/* The traversal runs over a cached, index-resolved copy of the topology;
 * with overlapping sectors (first match wins), dangling portal and sector
 * IDs, and edits between updates it must match the reference exactly. */
static void
test_portal_system_topology_cache (void)
{
    g_autoptr(LrgPortalSystem) system = lrg_portal_system_new ();
    g_autoptr(GRand) rng = g_rand_new_with_seed (7);
    guint x;
    guint z;

    lrg_portal_system_set_max_portal_depth (system, 6);
    for (z = 0; z < 6; z++)
        for (x = 0; x < 6; x++)
        {
            g_autofree gchar *id = g_strdup_printf ("s-%u-%u", x, z);
            g_autoptr(LrgBoundingBox3D) bounds = NULL;
            g_autoptr(LrgSector) sector = NULL;
            guint k;

            /* 12-unit cells on a 10-unit pitch: neighbours overlap */
            bounds = lrg_bounding_box3d_new ((gfloat) x * 10.0f, 0.0f, (gfloat) z * 10.0f,
                                             (gfloat) x * 10.0f + 12.0f, 10.0f,
                                             (gfloat) z * 10.0f + 12.0f);
            sector = lrg_sector_new (id, bounds);
            for (k = 0; k < 4; k++)
            {
                g_autofree gchar *pid = NULL;
                guint nx = (k == 0 && x < 5) ? x + 1 : (k == 1 && x > 0) ? x - 1 : x;
                guint nz = (k == 2 && z < 5) ? z + 1 : (k == 3 && z > 0) ? z - 1 : z;
                g_autofree gchar *other = g_strdup_printf ("s-%u-%u", nx, nz);
                g_autoptr(LrgPortal) portal = NULL;
                g_autoptr(LrgBoundingBox3D) pb = NULL;
                GrlVector3 normal;

                pid = g_strdup_printf ("p-%u-%u-%u", x, z, k);
                lrg_sector_add_portal (sector, pid);
                /* Every fifth portal ID is left dangling */
                if (g_rand_int_range (rng, 0, 5) == 0)
                    continue;
                normal.x = (gfloat) g_rand_double_range (rng, -1.0, 1.0);
                normal.y = 0.0f;
                normal.z = (gfloat) g_rand_double_range (rng, -1.0, 1.0);
                pb = lrg_bounding_box3d_new ((gfloat) nx * 10.0f, 0.0f, (gfloat) nz * 10.0f,
                                             (gfloat) nx * 10.0f + 2.0f, 10.0f,
                                             (gfloat) nz * 10.0f + 2.0f);
                portal = lrg_portal_new_with_normal (pid, pb, id,
                                                     g_rand_int_range (rng, 0, 8) == 0 ?
                                                     "nowhere" : other, &normal);
                lrg_portal_system_add_portal (system, portal);
            }
            lrg_portal_system_add_sector (system, sector);
        }

    check_portal_system (system, rng);

    /* Edits invalidate the cache */
    g_assert_true (lrg_portal_system_remove_sector (system, "s-2-2"));
    g_assert_true (lrg_portal_system_remove_sector (system, "s-0-0"));
    check_portal_system (system, rng);
    lrg_portal_system_remove_portal (system, "p-1-1-0");
    lrg_portal_system_remove_portal (system, "p-3-3-2");
    check_portal_system (system, rng);
    {
        g_autoptr(LrgBoundingBox3D) bounds = lrg_bounding_box3d_new (0.0f, 0.0f, 0.0f,
                                                                     60.0f, 10.0f, 60.0f);
        g_autoptr(LrgSector) big = lrg_sector_new ("s-2-2", bounds);

        lrg_sector_add_portal (big, "p-2-1-2");
        lrg_portal_system_add_sector (system, big);
    }
    check_portal_system (system, rng);
    lrg_portal_system_clear (system);
    check_portal_system (system, rng);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* BoundingBox3D tests */
    g_test_add_func ("/world3d/bounding-box3d/new", test_bounding_box3d_new);
    g_test_add_func ("/world3d/bounding-box3d/from-center", test_bounding_box3d_from_center);
    g_test_add_func ("/world3d/bounding-box3d/contains-point", test_bounding_box3d_contains_point);
    g_test_add_func ("/world3d/bounding-box3d/intersects", test_bounding_box3d_intersects);
    g_test_add_func ("/world3d/bounding-box3d/merge", test_bounding_box3d_merge);

    /* SpawnPoint3D tests */
    g_test_add_func ("/world3d/spawn-point3d/new", test_spawn_point3d_new);
    g_test_add_func ("/world3d/spawn-point3d/properties", test_spawn_point3d_properties);

    /* Trigger3D tests */
    g_test_add_func ("/world3d/trigger3d/new", test_trigger3d_new);
    g_test_add_func ("/world3d/trigger3d/test-point", test_trigger3d_test_point);

    /* Octree tests */
    g_test_add_func ("/world3d/octree/new", test_octree_new);
    g_test_add_func ("/world3d/octree/insert", test_octree_insert);
    g_test_add_func ("/world3d/octree/query-box", test_octree_query_box);
    g_test_add_func ("/world3d/octree/remove", test_octree_remove);

    /* Portal tests */
    g_test_add_func ("/world3d/portal/new", test_portal_new);
    g_test_add_func ("/world3d/portal/get-other-sector", test_portal_get_other_sector);

    /* Sector tests */
    g_test_add_func ("/world3d/sector/new", test_sector_new);
    g_test_add_func ("/world3d/sector/portals", test_sector_portals);
    g_test_add_func ("/world3d/sector/contains-point", test_sector_contains_point);

    /* Level3D tests */
    g_test_add_func ("/world3d/level3d/new", test_level3d_new);
    g_test_add_func ("/world3d/level3d/spawn-points", test_level3d_spawn_points);
    g_test_add_func ("/world3d/level3d/triggers", test_level3d_triggers);
    g_test_add_func ("/world3d/level3d/check-triggers", test_level3d_check_triggers);

    /* PortalSystem tests */
    g_test_add_func ("/world3d/portal-system/new", test_portal_system_new);
    g_test_add_func ("/world3d/portal-system/sectors", test_portal_system_sectors);
    g_test_add_func ("/world3d/portal-system/visibility", test_portal_system_visibility);
    g_test_add_func ("/world3d/portal-system/visibility-changed", test_portal_system_visibility_changed);
    g_test_add_func ("/world3d/portal-system/clear", test_portal_system_clear);
    g_test_add_func ("/world3d/portal-system/topology-cache", test_portal_system_topology_cache);

    return g_test_run ();
}
