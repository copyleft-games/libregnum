/* test-world3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for World3D module: bounding boxes, spawn points, triggers, octree,
 * portals, sectors, levels, and portal system; and (0.9.0) the LrgWorld3D
 * runtime: GUID map, lookups, component world hooks, frame phases and the
 * fixed-step accumulator, coalesced transform signals, render-list
 * collection (layers, cull masks, frustum, categories, sorting), light and
 * camera registries, octree-backed spatial queries, and a regression test
 * proving 2D LrgGameObject components still work after the LrgComponent
 * class changes.
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

/* =============================================================================
 * LrgWorld3D / LrgEntity3D runtime tests (0.9.0)
 * =============================================================================
 */

#define W3_EPS (1e-4f)

/* A recording component used by the LrgWorld3D tests: logs every hook into
 * a shared GString, optionally reports bounds, and optionally submits one
 * render item per collect_render() call with configurable flags. */

#define TEST_TYPE_W3_COMPONENT (test_w3_component_get_type ())
G_DECLARE_FINAL_TYPE (TestW3Component, test_w3_component, TEST, W3_COMPONENT, LrgComponent)

struct _TestW3Component
{
	LrgComponent        parent_instance;
	gchar              *tag;
	GString            *log;          /* shared, not owned; may be NULL */
	gboolean            has_bounds;
	gfloat              half_extent;
	gboolean            render;
	LrgRenderItemFlags  flags;
	guint               fixed_calls;
	guint               update_calls;
	guint               late_calls;
	LrgEntity3D        *move_on_update;  /* weak */
	guint              *destroy_counter;
};

G_DEFINE_FINAL_TYPE (TestW3Component, test_w3_component, LRG_TYPE_COMPONENT)

static void
w3_log (TestW3Component *self,
        const gchar     *what)
{
	if (self->log != NULL)
		g_string_append_printf (self->log, "%s:%s;", what, self->tag);
}

static void
w3_update (LrgComponent *c,
           gfloat        dt)
{
	TestW3Component *self = TEST_W3_COMPONENT (c);

	self->update_calls++;
	w3_log (self, "U");

	/* Several writes in one phase must coalesce into one signal. */
	if (self->move_on_update != NULL)
	{
		lrg_entity3d_set_position_xyz (self->move_on_update, 1, 0, 0);
		lrg_entity3d_set_position_xyz (self->move_on_update, 2, 0, 0);
		lrg_entity3d_set_position_xyz (self->move_on_update, 3, 0, 0);
	}
}

static void
w3_fixed_update (LrgComponent *c,
                 gfloat        step)
{
	TestW3Component *self = TEST_W3_COMPONENT (c);

	self->fixed_calls++;
	w3_log (self, "F");
}

static void
w3_late_update (LrgComponent *c,
                gfloat        dt)
{
	TestW3Component *self = TEST_W3_COMPONENT (c);

	self->late_calls++;
	w3_log (self, "L");
}

static void
w3_world_entered (LrgComponent *c,
                  LrgWorld3D   *world)
{
	g_assert_true (lrg_component_get_world3d (c) == world);
	w3_log (TEST_W3_COMPONENT (c), "enter");
}

static void
w3_world_exited (LrgComponent *c,
                 LrgWorld3D   *world)
{
	w3_log (TEST_W3_COMPONENT (c), "exit");
}

static gboolean
w3_get_bounds (LrgComponent     *c,
               LrgBoundingBox3D *out)
{
	TestW3Component *self = TEST_W3_COMPONENT (c);
	gfloat h;

	if (!self->has_bounds)
		return FALSE;
	h = self->half_extent;
	out->min.x = out->min.y = out->min.z = -h;
	out->max.x = out->max.y = out->max.z = h;
	return TRUE;
}

static void
w3_destroy_counter (gpointer data)
{
	(*(guint *)data)++;
}

static void
w3_collect_render (LrgComponent  *c,
                   LrgRenderList *list)
{
	TestW3Component *self = TEST_W3_COMPONENT (c);
	LrgRenderItem item;
	LrgBoundingBox3D local;

	if (!self->render)
		return;

	lrg_render_item_init_for_entity (&item, lrg_component_get_entity (c), c);
	item.flags = self->flags;
	w3_get_bounds (c, &local);
	if (self->has_bounds)
		lrg_render_item_set_local_bounds (&item, &local);
	if (self->destroy_counter != NULL)
	{
		item.user_data = self->destroy_counter;
		item.user_data_destroy = w3_destroy_counter;
	}
	if (lrg_render_list_accepts (list, &item))
		lrg_render_list_add (list, &item);
}

static void
w3_finalize (GObject *object)
{
	g_free (TEST_W3_COMPONENT (object)->tag);
	G_OBJECT_CLASS (test_w3_component_parent_class)->finalize (object);
}

static void
test_w3_component_class_init (TestW3ComponentClass *klass)
{
	LrgComponentClass *cc = LRG_COMPONENT_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = w3_finalize;
	cc->update = w3_update;
	cc->fixed_update = w3_fixed_update;
	cc->late_update = w3_late_update;
	cc->world_entered = w3_world_entered;
	cc->world_exited = w3_world_exited;
	cc->get_bounds = w3_get_bounds;
	cc->collect_render = w3_collect_render;
}

static void
test_w3_component_init (TestW3Component *self)
{
	self->half_extent = 0.5f;
}

static TestW3Component *
w3_new (const gchar *tag,
        GString     *log)
{
	TestW3Component *c = g_object_new (TEST_TYPE_W3_COMPONENT, NULL);

	c->tag = g_strdup (tag);
	c->log = log;
	return c;
}

/* Creates an entity with one TestW3Component and adds it to @parent (or
 * the world root).  Returns the (borrowed) entity; *out_comp gets the
 * (borrowed) component. */
static LrgEntity3D *
w3_spawn (LrgWorld3D       *world,
          LrgEntity3D      *parent,
          const gchar      *name,
          GString          *log,
          TestW3Component **out_comp)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new_with_name (name);
	g_autoptr(TestW3Component) c = w3_new (name, log);

	lrg_entity3d_add_component (e, LRG_COMPONENT (c));
	if (parent != NULL)
		lrg_entity3d_add_child (parent, e);
	else
		lrg_world3d_add_entity (world, e);
	if (out_comp != NULL)
		*out_comp = c;
	return e;
}

static void
w3_count0 (gpointer instance,
           gpointer user_data)
{
	(*(guint *)user_data)++;
}

static void
w3_count1 (gpointer instance,
           gpointer arg,
           gpointer user_data)
{
	(*(guint *)user_data)++;
}

/* --- GUID map, lookups, membership -------------------------------------- */

static void
test_world3d_guid_map (void)
{
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	g_autoptr(LrgEntity3D) a = lrg_entity3d_new_with_name ("a");
	g_autoptr(LrgEntity3D) b = lrg_entity3d_new_with_name ("b");
	guint added = 0;
	guint removed = 0;

	g_signal_connect (w, "entity-added", G_CALLBACK (w3_count1), &added);
	g_signal_connect (w, "entity-removed", G_CALLBACK (w3_count1), &removed);

	lrg_entity3d_set_guid (a, "A");
	lrg_entity3d_add_child (a, b);

	/* A subtree joins at once. */
	lrg_world3d_add_entity (w, a);
	g_assert_cmpuint (added, ==, 2);
	g_assert_cmpuint (lrg_world3d_get_entity_count (w), ==, 2);
	g_assert_true (lrg_entity3d_get_world (b) == w);
	g_assert_true (lrg_world3d_find_by_guid (w, "A") == a);
	g_assert_null (lrg_world3d_find_by_guid (w, "B"));

	/* A GUID set after joining is mapped too. */
	lrg_entity3d_set_guid (b, "B");
	g_assert_true (lrg_world3d_find_by_guid (w, "B") == b);

	/* Lookups by name and path. */
	g_assert_true (lrg_world3d_find_by_name (w, "b") == b);
	g_assert_true (lrg_world3d_find_by_path (w, "a/b") == b);

	/* Moving inside the world does not leave it. */
	lrg_entity3d_set_parent (b, lrg_world3d_get_root (w), TRUE);
	g_assert_cmpuint (removed, ==, 0);
	g_assert_true (lrg_world3d_find_by_guid (w, "B") == b);

	/* Removing leaves (and unmaps) the subtree. */
	g_assert_true (lrg_world3d_remove_entity (w, a));
	g_assert_cmpuint (removed, ==, 1);
	g_assert_null (lrg_world3d_find_by_guid (w, "A"));
	g_assert_null (lrg_entity3d_get_world (a));
	g_assert_false (lrg_world3d_remove_entity (w, a));
	g_assert_cmpuint (lrg_world3d_get_entity_count (w), ==, 1);

	/* Clear empties the world. */
	lrg_world3d_clear (w);
	g_assert_cmpuint (lrg_world3d_get_entity_count (w), ==, 0);
	g_assert_null (lrg_world3d_find_by_guid (w, "B"));
	g_assert_null (lrg_entity3d_get_world (b));
}

static void
test_world3d_queries_tags_components (void)
{
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	TestW3Component *c = NULL;
	LrgEntity3D *e1;
	LrgEntity3D *e2;
	g_autoptr(LrgEntity3D) plain = lrg_entity3d_new_with_name ("plain");
	g_autoptr(GPtrArray) tagged = NULL;
	g_autoptr(GPtrArray) with = NULL;
	g_autoptr(GPtrArray) comps = NULL;

	e1 = w3_spawn (w, NULL, "spawn1", NULL, &c);
	e2 = w3_spawn (w, e1, "spawn2", NULL, NULL);
	lrg_world3d_add_entity (w, plain);
	lrg_entity3d_add_tag (e2, "spawn");
	lrg_entity3d_add_tag (plain, "spawn");

	tagged = lrg_world3d_find_by_tag (w, "spawn");
	g_assert_cmpuint (tagged->len, ==, 2);
	g_assert_true (g_ptr_array_index (tagged, 0) == e2);   /* hierarchy order */

	with = lrg_world3d_find_with_component (w, TEST_TYPE_W3_COMPONENT);
	g_assert_cmpuint (with->len, ==, 2);
	g_assert_true (g_ptr_array_index (with, 0) == e1);

	comps = lrg_world3d_get_components_of_type (w, TEST_TYPE_W3_COMPONENT);
	g_assert_cmpuint (comps->len, ==, 2);
	g_assert_true (g_ptr_array_index (comps, 0) == c);
}

static void
test_world3d_component_world_hooks (void)
{
	/* The log outlives the world: world teardown runs world_exited hooks
	 * that still write to it. */
	g_autoptr(GString) log = g_string_new (NULL);
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new ();
	g_autoptr(TestW3Component) c1 = w3_new ("1", log);
	g_autoptr(TestW3Component) c2 = w3_new ("2", log);

	/* Attached before joining: world_entered on join. */
	lrg_entity3d_add_component (e, LRG_COMPONENT (c1));
	g_assert_cmpstr (log->str, ==, "");
	lrg_world3d_add_entity (w, e);
	g_assert_cmpstr (log->str, ==, "enter:1;");

	/* Attached while in the world: world_entered immediately. */
	lrg_entity3d_add_component (e, LRG_COMPONENT (c2));
	g_assert_cmpstr (log->str, ==, "enter:1;enter:2;");

	/* Removing a component exits it. */
	lrg_entity3d_remove_component (e, LRG_COMPONENT (c2));
	g_assert_cmpstr (log->str, ==, "enter:1;enter:2;exit:2;");

	/* Listener is cleared when its entity leaves. */
	lrg_world3d_set_audio_listener (w, e);
	g_assert_true (lrg_world3d_get_audio_listener (w) == e);
	lrg_world3d_remove_entity (w, e);
	g_assert_cmpstr (log->str, ==, "enter:1;enter:2;exit:2;exit:1;");
	g_assert_null (lrg_world3d_get_audio_listener (w));
}

/* --- Phases ------------------------------------------------------------- */

static void
test_world3d_phase_order (void)
{
	/* The log outlives the world: world teardown runs world_exited hooks
	 * that still write to it. */
	g_autoptr(GString) log = g_string_new (NULL);
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	TestW3Component *cp = NULL;
	TestW3Component *cc = NULL;
	TestW3Component *ci = NULL;
	LrgEntity3D *p;
	LrgEntity3D *inactive;

	p = w3_spawn (w, NULL, "p", log, &cp);
	w3_spawn (w, p, "c", log, &cc);
	inactive = w3_spawn (w, NULL, "i", log, &ci);
	lrg_entity3d_set_active (inactive, FALSE);
	g_string_truncate (log, 0);

	lrg_world3d_set_fixed_step (w, 0.1f);

	/* step = fixed (0..n) -> update -> late, parents before children. */
	lrg_world3d_step (w, 0.1f);
	g_assert_cmpstr (log->str, ==, "F:p;F:c;U:p;U:c;L:p;L:c;");
	g_assert_cmpuint (ci->update_calls, ==, 0);

	/* Disabled components are skipped in every phase. */
	g_string_truncate (log, 0);
	lrg_component_set_enabled (LRG_COMPONENT (cc), FALSE);
	lrg_world3d_step (w, 0.1f);
	g_assert_cmpstr (log->str, ==, "F:p;U:p;L:p;");

	/* Paused: step does nothing; direct phase calls still run. */
	g_string_truncate (log, 0);
	lrg_world3d_set_paused (w, TRUE);
	lrg_world3d_step (w, 0.1f);
	g_assert_cmpstr (log->str, ==, "");
	lrg_world3d_update (w, 0.1f);
	g_assert_cmpstr (log->str, ==, "U:p;");
	(void)cp;
}

static void
test_world3d_fixed_accumulator (void)
{
	g_autoptr(LrgWorld3D) w1 = lrg_world3d_new ();
	g_autoptr(LrgWorld3D) w2 = lrg_world3d_new ();
	TestW3Component *c1 = NULL;
	TestW3Component *c2 = NULL;
	guint i;
	guint total1 = 0;
	guint total2 = 0;

	w3_spawn (w1, NULL, "a", NULL, &c1);
	w3_spawn (w2, NULL, "b", NULL, &c2);
	lrg_world3d_set_fixed_step (w1, 0.02f);
	lrg_world3d_set_fixed_step (w2, 0.02f);

	/* Same simulated second, different frame pacing: identical substep
	 * counts (determinism), allowing for float rounding of the total. */
	for (i = 0; i < 60; i++)
		total1 += lrg_world3d_fixed_update (w1, 1.0f / 60.0f);
	for (i = 0; i < 25; i++)
		total2 += lrg_world3d_fixed_update (w2, i % 2 == 0 ? 0.03f : 0.05f);
	total2 += lrg_world3d_fixed_update (w2, 0.0f);
	g_assert_cmpuint (c1->fixed_calls, ==, total1);
	g_assert_cmpuint (c2->fixed_calls, ==, total2);
	g_assert_cmpint (ABS ((gint)total1 - 50), <=, 1);
	g_assert_cmpint (ABS ((gint)total2 - 50), <=, 1);
	g_assert_cmpfloat_with_epsilon (lrg_world3d_get_fixed_time (w1),
	                                (gdouble)total1 * 0.02, 1e-4);

	/* Alpha stays in [0, 1). */
	g_assert_cmpfloat (lrg_world3d_get_fixed_alpha (w1), >=, 0.0f);
	g_assert_cmpfloat (lrg_world3d_get_fixed_alpha (w1), <, 1.0f);

	/* A long hitch is capped at max-substeps and the excess dropped. */
	lrg_world3d_set_max_substeps (w1, 4);
	g_assert_cmpuint (lrg_world3d_fixed_update (w1, 1.0f), ==, 4);
	g_assert_cmpfloat (lrg_world3d_get_fixed_alpha (w1), <, 1.0f);
	g_assert_cmpuint (lrg_world3d_fixed_update (w1, 0.0f), ==, 0);

	/* Below one step: nothing runs, the time accumulates. */
	g_assert_cmpuint (lrg_world3d_fixed_update (w2, 0.001f), ==, 0);
}

static void
test_world3d_transform_coalesced (void)
{
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	TestW3Component *c = NULL;
	LrgEntity3D *mover;
	LrgEntity3D *driver;
	guint n = 0;

	mover = w3_spawn (w, NULL, "mover", NULL, NULL);
	driver = w3_spawn (w, NULL, "driver", NULL, &c);
	c->move_on_update = mover;
	(void)driver;

	g_signal_connect (mover, "transform-changed", G_CALLBACK (w3_count0), &n);

	/* Three writes during update -> one emission at the flush. */
	lrg_world3d_update (w, 0.016f);
	g_assert_cmpuint (n, ==, 1);

	/* Writes outside a phase are queued until the next flush (here the
	 * render collection). */
	lrg_entity3d_set_position_xyz (mover, 9, 9, 9);
	lrg_entity3d_set_position_xyz (mover, 8, 8, 8);
	g_assert_cmpuint (n, ==, 1);
	{
		g_autoptr(LrgRenderList) list = lrg_render_list_new ();

		lrg_world3d_collect_render (w, NULL, list);
	}
	g_assert_cmpuint (n, ==, 2);
}

/* --- Render list collection --------------------------------------------- */

static LrgEntity3D *
w3_spawn_drawable (LrgWorld3D        *w,
                   const gchar       *name,
                   gfloat             x,
                   gfloat             z,
                   LrgRenderItemFlags flags,
                   guint              layer)
{
	TestW3Component *c = NULL;
	LrgEntity3D *e;

	e = w3_spawn (w, NULL, name, NULL, &c);
	c->render = TRUE;
	c->has_bounds = TRUE;
	c->flags = flags;
	lrg_entity3d_set_position_xyz (e, x, 0, z);
	lrg_entity3d_set_layer (e, layer);
	return e;
}

static gboolean
w3_list_has (LrgRenderList     *list,
             LrgRenderCategory  cat,
             LrgEntity3D       *e)
{
	guint i;

	for (i = 0; i < lrg_render_list_get_n_items (list, cat); i++)
	{
		if (lrg_render_list_get_item (list, cat, i)->entity == e)
			return TRUE;
	}
	return FALSE;
}

static void
replay_count (const LrgRenderItem *item,
              LrgRenderCategory    category,
              gpointer             user_data)
{
	(*(guint *)user_data)++;
}

static void
test_world3d_render_categories (void)
{
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();
	LrgEntity3D *opaque;
	LrgEntity3D *cutout;
	LrgEntity3D *glass_near;
	LrgEntity3D *glass_far;
	LrgEntity3D *hidden;
	LrgEntity3D *ui;
	guint replayed = 0;
	guint destroyed = 0;
	TestW3Component *dc = NULL;

	opaque = w3_spawn_drawable (w, "opaque", 0, 0, LRG_RENDER_ITEM_FLAG_CAST_SHADOW, 0);
	cutout = w3_spawn_drawable (w, "cutout", 1, 0, LRG_RENDER_ITEM_FLAG_CUTOUT, 0);
	glass_near = w3_spawn_drawable (w, "glass_near", 0, 2,
	                                LRG_RENDER_ITEM_FLAG_TRANSPARENT | LRG_RENDER_ITEM_FLAG_CUTOUT, 0);
	glass_far = w3_spawn_drawable (w, "glass_far", 0, 20, LRG_RENDER_ITEM_FLAG_TRANSPARENT, 0);
	hidden = w3_spawn_drawable (w, "hidden", 3, 0, 0, 0);
	lrg_entity3d_set_visible (hidden, FALSE);
	ui = w3_spawn_drawable (w, "ui", 4, 0, 0, LRG_RENDER_LAYERS_UI);
	dc = TEST_W3_COMPONENT (lrg_entity3d_get_component (opaque, TEST_TYPE_W3_COMPONENT));
	dc->destroy_counter = &destroyed;

	/* No camera: everything visible is collected, no frustum. */
	lrg_world3d_collect_render (w, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_total_items (list), ==, 5);
	g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, opaque));
	g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_CUTOUT, cutout));
	g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_TRANSPARENT, glass_near));
	g_assert_false (w3_list_has (list, LRG_RENDER_CATEGORY_CUTOUT, glass_near));
	g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_SHADOW_CASTER, opaque));
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_SHADOW_CASTER), ==, 1);
	g_assert_false (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, hidden));
	g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, ui));

	/* Transparent sorted back to front from the eye (origin). */
	g_assert_true (lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_TRANSPARENT, 0)->entity == glass_far);
	g_assert_true (lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_TRANSPARENT, 1)->entity == glass_near);

	/* Replay is repeatable. */
	lrg_render_list_foreach (list, LRG_RENDER_CATEGORY_TRANSPARENT, replay_count, &replayed);
	lrg_render_list_foreach (list, LRG_RENDER_CATEGORY_TRANSPARENT, replay_count, &replayed);
	g_assert_cmpuint (replayed, ==, 4);

	/* Camera cull mask drops the UI layer; the list is cleared first and
	 * user data from the previous frame is released. */
	{
		g_autoptr(LrgCamera3D) cam = lrg_camera3d_new ();

		lrg_camera_set_cull_mask (LRG_CAMERA (cam),
		                          LRG_RENDER_LAYERS_MASK_ALL & ~LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_UI));
		lrg_world3d_set_frustum_culling (w, FALSE);
		lrg_world3d_collect_render (w, LRG_CAMERA (cam), list);
		g_assert_cmpuint (destroyed, ==, 1);
		g_assert_false (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, ui));
		g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, opaque));
		g_assert_cmpuint (lrg_render_list_get_cull_mask (list), ==,
		                  LRG_RENDER_LAYERS_MASK_ALL & ~LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_UI));
	}
	lrg_render_list_clear (list);
	g_assert_cmpuint (destroyed, ==, 2);
}

static void
test_world3d_render_frustum (void)
{
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();
	g_autoptr(LrgCamera3D) cam = lrg_camera3d_new ();
	LrgEntity3D *front;
	LrgEntity3D *behind;
	LrgEntity3D *behind_caster;
	LrgEntity3D *behind_nocull;
	gfloat vp[16];
	gfloat eye[3];

	/* Camera at the origin looking down -Z. */
	lrg_camera3d_set_position_xyz (cam, 0, 0, 0);
	lrg_camera3d_set_target_xyz (cam, 0, 0, -1);
	lrg_camera3d_set_aspect (cam, 1.0f);

	front = w3_spawn_drawable (w, "front", 0, -10, 0, 0);
	behind = w3_spawn_drawable (w, "behind", 0, 10, 0, 0);
	behind_caster = w3_spawn_drawable (w, "caster", 1, 10, LRG_RENDER_ITEM_FLAG_CAST_SHADOW, 0);
	behind_nocull = w3_spawn_drawable (w, "nocull", 2, 10, LRG_RENDER_ITEM_FLAG_NO_CULL, 0);

	lrg_world3d_collect_render (w, LRG_CAMERA (cam), list);
	g_assert_true (lrg_render_list_get_view_projection (list, vp));
	lrg_render_list_get_eye (list, eye);
	g_assert_cmpfloat_with_epsilon (eye[2], 0.0f, W3_EPS);

	g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, front));
	g_assert_false (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, behind));
	g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, behind_nocull));

	/* Off-screen casters still shadow: SHADOW_CASTER only. */
	g_assert_false (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, behind_caster));
	g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_SHADOW_CASTER, behind_caster));

	/* Culling off: everything is in. */
	lrg_world3d_set_frustum_culling (w, FALSE);
	lrg_world3d_collect_render (w, LRG_CAMERA (cam), list);
	g_assert_false (lrg_render_list_get_view_projection (list, vp));
	g_assert_true (w3_list_has (list, LRG_RENDER_CATEGORY_OPAQUE, behind));
}

static void
test_world3d_render_item_helpers (void)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new ();
	LrgRenderItem item;
	LrgBoundingBox3D local = { { -1, -1, -1 }, { 1, 1, 1 } };
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();

	lrg_render_item_init (&item);
	g_assert_cmpint (item.kind, ==, LRG_RENDER_ITEM_PRIMITIVE);
	g_assert_cmpint (item.mesh_index, ==, -1);
	g_assert_cmpint (lrg_render_item_get_category (&item), ==, LRG_RENDER_CATEGORY_OPAQUE);
	g_assert_true (item.bounds.min.x > item.bounds.max.x);   /* unknown */

	lrg_entity3d_set_position_xyz (e, 10, 0, 0);
	lrg_entity3d_set_scale_xyz (e, 2, 2, 2);
	lrg_entity3d_set_layer (e, 5);
	lrg_render_item_init_for_entity (&item, e, NULL);
	g_assert_cmpuint (item.layer, ==, 5);
	g_assert_cmpfloat_with_epsilon (item.world_matrix[12], 10.0f, W3_EPS);
	lrg_render_item_set_local_bounds (&item, &local);
	g_assert_cmpfloat_with_epsilon (item.bounds.min.x, 8.0f, W3_EPS);
	g_assert_cmpfloat_with_epsilon (item.bounds.max.x, 12.0f, W3_EPS);

	/* Opaque items sort by key first, then near to far. */
	{
		gfloat eye[3] = { 0, 0, 0 };
		LrgRenderItem a;
		LrgRenderItem b;
		LrgRenderItem c;

		lrg_render_list_set_view (list, eye, LRG_RENDER_LAYERS_MASK_ALL, NULL);
		lrg_render_item_init (&a);
		lrg_render_item_init (&b);
		lrg_render_item_init (&c);
		a.world_matrix[12] = 5.0f;  a.sort_key = 2;
		b.world_matrix[12] = 50.0f; b.sort_key = 1;
		c.world_matrix[12] = 1.0f;  c.sort_key = 2;
		lrg_render_list_add (list, &a);
		lrg_render_list_add (list, &b);
		lrg_render_list_add (list, &c);
		lrg_render_list_sort (list);
		g_assert_cmpfloat (lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 0)->world_matrix[12], ==, 50.0f);
		g_assert_cmpfloat (lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 1)->world_matrix[12], ==, 1.0f);
		g_assert_cmpfloat (lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 2)->world_matrix[12], ==, 5.0f);
		g_assert_cmpfloat_with_epsilon (lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 2)->view_depth, 5.0f, W3_EPS);
	}

	/* Boxed type round trip. */
	{
		LrgRenderList *ref = g_boxed_copy (LRG_TYPE_RENDER_LIST, list);

		g_assert_true (ref == list);
		g_boxed_free (LRG_TYPE_RENDER_LIST, ref);
	}
}

/* --- Lights, cameras ---------------------------------------------------- */

static void
test_world3d_lights (void)
{
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	g_autoptr(LrgEntity3D) src1 = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D) src2 = lrg_entity3d_new ();
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();
	g_autoptr(GPtrArray) lights = NULL;
	g_autoptr(LrgLight3DDesc) heap = lrg_light3d_desc_new ();
	g_autoptr(LrgLight3DDesc) copy = NULL;
	LrgLight3DDesc d;

	g_assert_true (heap->enabled);
	g_assert_cmpuint (heap->layer_mask, ==, LRG_RENDER_LAYERS_MASK_ALL);
	copy = lrg_light3d_desc_copy (heap);
	g_assert_cmpfloat (copy->range, ==, heap->range);

	lrg_light3d_desc_init (&d);
	d.kind = LRG_LIGHT3D_DIRECTIONAL;
	d.intensity = 2.0f;
	lrg_world3d_register_light (w, G_OBJECT (src1), &d);
	d.kind = LRG_LIGHT3D_SPOT;
	d.layer_mask = LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_WATER);
	lrg_world3d_register_light (w, G_OBJECT (src2), &d);
	g_assert_cmpuint (lrg_world3d_get_n_lights (w), ==, 2);

	/* Re-registering updates in place. */
	d.kind = LRG_LIGHT3D_POINT;
	d.intensity = 3.0f;
	d.layer_mask = LRG_RENDER_LAYERS_MASK_ALL;
	lrg_world3d_register_light (w, G_OBJECT (src1), &d);
	lights = lrg_world3d_get_lights (w);
	g_assert_cmpuint (lights->len, ==, 2);
	g_assert_cmpint (((LrgLight3DDesc *)g_ptr_array_index (lights, 0))->kind, ==, LRG_LIGHT3D_POINT);
	g_assert_true (((LrgLight3DDesc *)g_ptr_array_index (lights, 0))->source == G_OBJECT (src1));

	/* Collection filters by the camera mask. */
	{
		g_autoptr(LrgCamera3D) cam = lrg_camera3d_new ();

		lrg_camera_set_cull_mask (LRG_CAMERA (cam), LRG_RENDER_LAYERS_BIT (0));
		lrg_world3d_collect_render (w, LRG_CAMERA (cam), list);
		g_assert_cmpuint (lrg_render_list_get_n_lights (list), ==, 1);
		g_assert_cmpfloat (lrg_render_list_get_light (list, 0)->intensity, ==, 3.0f);
	}

	g_assert_true (lrg_world3d_unregister_light (w, G_OBJECT (src1)));
	g_assert_false (lrg_world3d_unregister_light (w, G_OBJECT (src1)));
	g_assert_cmpuint (lrg_world3d_get_n_lights (w), ==, 1);
}

static void
test_world3d_cameras (void)
{
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	g_autoptr(LrgCamera3D) a = lrg_camera3d_new ();
	g_autoptr(LrgCamera3D) b = lrg_camera3d_new ();
	g_autoptr(LrgCamera3D) forced = lrg_camera3d_new ();
	g_autoptr(GPtrArray) cams = NULL;

	g_assert_null (lrg_world3d_get_active_camera (w));
	lrg_world3d_register_camera (w, LRG_CAMERA (a), 0);
	lrg_world3d_register_camera (w, LRG_CAMERA (b), 0);
	g_assert_true (lrg_world3d_get_active_camera (w) == LRG_CAMERA (a));   /* tie: first */
	lrg_world3d_register_camera (w, LRG_CAMERA (b), 10);
	g_assert_true (lrg_world3d_get_active_camera (w) == LRG_CAMERA (b));
	cams = lrg_world3d_get_cameras (w);
	g_assert_cmpuint (cams->len, ==, 2);

	lrg_world3d_set_active_camera (w, LRG_CAMERA (forced));
	g_assert_true (lrg_world3d_get_active_camera (w) == LRG_CAMERA (forced));
	lrg_world3d_set_active_camera (w, NULL);
	g_assert_true (lrg_world3d_get_active_camera (w) == LRG_CAMERA (b));

	g_assert_true (lrg_world3d_unregister_camera (w, LRG_CAMERA (b)));
	g_assert_false (lrg_world3d_unregister_camera (w, LRG_CAMERA (b)));
	g_assert_true (lrg_world3d_get_active_camera (w) == LRG_CAMERA (a));
}

/* --- Spatial queries ---------------------------------------------------- */

static void
test_world3d_spatial_queries (void)
{
	g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
	TestW3Component *c = NULL;
	LrgEntity3D *near;
	LrgEntity3D *far;
	LrgEntity3D *outside;
	LrgEntity3D *pointlike;
	g_autoptr(GPtrArray) hits = NULL;
	g_autoptr(LrgBoundingBox3D) box = lrg_bounding_box3d_new (-2, -2, -2, 2, 2, 2);
	gfloat origin[3] = { 0, 0, 0 };

	near = w3_spawn (w, NULL, "near", NULL, &c);
	c->has_bounds = TRUE;
	lrg_world3d_invalidate_bounds (w, near);
	far = w3_spawn (w, NULL, "far", NULL, &c);
	c->has_bounds = TRUE;
	lrg_entity3d_set_position_xyz (far, 50, 0, 0);
	outside = w3_spawn (w, NULL, "outside", NULL, &c);
	c->has_bounds = TRUE;
	lrg_entity3d_set_position_xyz (outside, 100000, 0, 0);
	pointlike = w3_spawn (w, NULL, "point", NULL, NULL);
	lrg_entity3d_set_position_xyz (pointlike, 1, 1, 1);

	hits = lrg_world3d_query_box (w, box);
	g_assert_cmpuint (hits->len, ==, 2);
	g_assert_true (g_ptr_array_find (hits, near, NULL));
	g_assert_true (g_ptr_array_find (hits, pointlike, NULL));
	g_clear_pointer (&hits, g_ptr_array_unref);

	/* Moving re-indexes lazily. */
	lrg_entity3d_set_position_xyz (far, 1, 0, 0);
	hits = lrg_world3d_query_sphere (w, origin, 1.0f);
	g_assert_true (g_ptr_array_find (hits, far, NULL));
	g_clear_pointer (&hits, g_ptr_array_unref);

	/* Moving a parent moves the indexed bounds of its children. */
	lrg_entity3d_set_parent (near, far, FALSE);
	lrg_entity3d_set_position_xyz (far, -30, 0, 0);
	hits = lrg_world3d_query_box (w, box);
	g_assert_false (g_ptr_array_find (hits, near, NULL));
	g_assert_false (g_ptr_array_find (hits, far, NULL));
	g_clear_pointer (&hits, g_ptr_array_unref);

	/* Far outside the octree bounds: still found. */
	{
		gfloat at[3] = { 100000, 0, 0 };

		hits = lrg_world3d_query_sphere (w, at, 1.0f);
		g_assert_cmpuint (hits->len, ==, 1);
		g_assert_true (g_ptr_array_index (hits, 0) == outside);
		g_clear_pointer (&hits, g_ptr_array_unref);
	}

	/* Inactive entities are not reported. */
	lrg_entity3d_set_active (pointlike, FALSE);
	hits = lrg_world3d_query_box (w, box);
	g_assert_false (g_ptr_array_find (hits, pointlike, NULL));
	g_clear_pointer (&hits, g_ptr_array_unref);

	g_assert_nonnull (lrg_world3d_get_octree (w));
	/* LrgOctree keeps out-of-bounds objects in its root node, so all four
	 * entities are indexed (the overflow list only holds rejects). */
	g_assert_cmpuint (lrg_octree_get_object_count (lrg_world3d_get_octree (w)), ==, 4);
}

/* --- 2D regression ------------------------------------------------------ */

typedef struct
{
	LrgComponent parent_instance;
} Test2DComponent;

typedef struct
{
	LrgComponentClass parent_class;
} Test2DComponentClass;

static GType test_2d_component_get_type (void);
G_DEFINE_TYPE (Test2DComponent, test_2d_component, LRG_TYPE_COMPONENT)

static guint test_2d_attached;
static guint test_2d_detached;
static guint test_2d_updates;

static void
t2d_attached (LrgComponent  *self,
              LrgGameObject *owner)
{
	g_assert_true (lrg_component_get_owner (self) == owner);
	test_2d_attached++;
}

static void
t2d_detached (LrgComponent *self)
{
	test_2d_detached++;
}

static void
t2d_update (LrgComponent *self,
            gfloat        dt)
{
	test_2d_updates++;
}

static void
test_2d_component_class_init (Test2DComponentClass *klass)
{
	LrgComponentClass *cc = LRG_COMPONENT_CLASS (klass);

	cc->attached = t2d_attached;
	cc->detached = t2d_detached;
	cc->update = t2d_update;
}

static void
test_2d_component_init (Test2DComponent *self)
{
}

static void
test_world3d_2d_component_regression (void)
{
	g_autoptr(LrgGameObject) go = lrg_game_object_new ();
	LrgComponent *c;

	/* The 0.9.0 class layout keeps the 2D vfunc offsets. */
	g_assert_cmpuint (G_STRUCT_OFFSET (LrgComponentClass, attached), ==, sizeof (GObjectClass));
	g_assert_cmpuint (sizeof (LrgComponentClass), ==,
	                  sizeof (GObjectClass) + 11 * sizeof (gpointer));

	test_2d_attached = test_2d_detached = test_2d_updates = 0;
	c = g_object_new (test_2d_component_get_type (), NULL);
	lrg_game_object_add_component (go, c);
	g_assert_cmpuint (test_2d_attached, ==, 1);
	g_assert_true (lrg_component_get_owner (c) == go);
	g_assert_null (lrg_component_get_entity (c));
	g_assert_true (lrg_component_get_owner_object (c) == G_OBJECT (go));
	g_assert_null (lrg_component_get_world3d (c));

	lrg_component_update (c, 0.016f);
	g_assert_cmpuint (test_2d_updates, ==, 1);

	/* The new hooks are harmless no-ops on 2D components. */
	lrg_component_fixed_update (c, 0.02f);
	lrg_component_late_update (c, 0.016f);
	{
		LrgBoundingBox3D b;

		g_assert_false (lrg_component_get_bounds (c, &b));
	}

	lrg_game_object_remove_component (go, c);
	g_assert_cmpuint (test_2d_detached, ==, 1);
	g_assert_null (lrg_component_get_owner (c));
	g_object_unref (c);

	/* A 2D component on a 3D entity: update runs, 2D hooks do not. */
	{
		g_autoptr(LrgWorld3D) w = lrg_world3d_new ();
		g_autoptr(LrgEntity3D) e = lrg_entity3d_new ();
		LrgComponent *c2 = g_object_new (test_2d_component_get_type (), NULL);

		lrg_entity3d_add_component (e, c2);
		g_object_unref (c2);
		lrg_world3d_add_entity (w, e);
		lrg_world3d_update (w, 0.016f);
		g_assert_cmpuint (test_2d_updates, ==, 2);
		g_assert_cmpuint (test_2d_attached, ==, 1);
	}
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

    g_test_add_func ("/world3d/world/guid-map", test_world3d_guid_map);
    g_test_add_func ("/world3d/world/queries-tags-components", test_world3d_queries_tags_components);
    g_test_add_func ("/world3d/world/component-world-hooks", test_world3d_component_world_hooks);
    g_test_add_func ("/world3d/world/phase-order", test_world3d_phase_order);
    g_test_add_func ("/world3d/world/fixed-accumulator", test_world3d_fixed_accumulator);
    g_test_add_func ("/world3d/world/transform-coalesced", test_world3d_transform_coalesced);
    g_test_add_func ("/world3d/world/render-categories", test_world3d_render_categories);
    g_test_add_func ("/world3d/world/render-frustum", test_world3d_render_frustum);
    g_test_add_func ("/world3d/world/render-item-helpers", test_world3d_render_item_helpers);
    g_test_add_func ("/world3d/world/lights", test_world3d_lights);
    g_test_add_func ("/world3d/world/cameras", test_world3d_cameras);
    g_test_add_func ("/world3d/world/spatial-queries", test_world3d_spatial_queries);
    g_test_add_func ("/world3d/world/2d-component-regression", test_world3d_2d_component_regression);

    return g_test_run ();
}
