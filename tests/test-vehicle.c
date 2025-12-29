/* test-vehicle.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for vehicle module.
 */

#include <glib.h>
#include <math.h>

#include "vehicle/lrg-wheel.h"
#include "vehicle/lrg-vehicle.h"
#include "vehicle/lrg-vehicle-controller.h"
#include "vehicle/lrg-vehicle-camera.h"
#include "vehicle/lrg-vehicle-audio.h"
#include "vehicle/lrg-road.h"
#include "vehicle/lrg-road-network.h"
#include "vehicle/lrg-traffic-agent.h"

/* ============================================================================
 * LrgWheel Tests
 * ============================================================================ */

static void
test_wheel_new (void)
{
    LrgWheel *wheel;

    wheel = lrg_wheel_new (1.0f, 0.0f, 2.0f, 0.3f);

    g_assert_nonnull (wheel);
    g_assert_cmpfloat (wheel->offset_x, ==, 1.0f);
    g_assert_cmpfloat (wheel->offset_y, ==, 0.0f);
    g_assert_cmpfloat (wheel->offset_z, ==, 2.0f);
    g_assert_cmpfloat (wheel->radius, ==, 0.3f);
    g_assert_false (wheel->is_grounded);

    lrg_wheel_free (wheel);
}

static void
test_wheel_copy (void)
{
    LrgWheel *wheel;
    LrgWheel *copy;

    wheel = lrg_wheel_new (1.0f, 0.5f, 2.0f, 0.35f);
    lrg_wheel_set_drive (wheel, TRUE);
    lrg_wheel_set_steering (wheel, TRUE);

    copy = lrg_wheel_copy (wheel);

    g_assert_nonnull (copy);
    g_assert_cmpfloat (copy->offset_x, ==, wheel->offset_x);
    g_assert_cmpfloat (copy->radius, ==, wheel->radius);
    g_assert_true (copy->is_drive_wheel);
    g_assert_true (copy->is_steering_wheel);

    lrg_wheel_free (wheel);
    lrg_wheel_free (copy);
}

static void
test_wheel_suspension (void)
{
    LrgWheel *wheel;

    wheel = lrg_wheel_new (0.0f, 0.0f, 0.0f, 0.3f);
    lrg_wheel_set_suspension (wheel, 0.5f, 60000.0f, 5000.0f);

    g_assert_cmpfloat (wheel->suspension_length, ==, 0.5f);
    g_assert_cmpfloat (wheel->suspension_stiffness, ==, 60000.0f);
    g_assert_cmpfloat (wheel->suspension_damping, ==, 5000.0f);

    lrg_wheel_free (wheel);
}

static void
test_wheel_update (void)
{
    LrgWheel *wheel;

    wheel = lrg_wheel_new (0.0f, 0.0f, 0.0f, 0.3f);

    /* Ground contact */
    lrg_wheel_update (wheel, 0.4f, 0.0f, 0.0f, 0.016f);

    g_assert_true (wheel->is_grounded);
    g_assert_cmpfloat (wheel->compression, >, 0.0f);

    /* No ground contact */
    lrg_wheel_update (wheel, 10.0f, 0.0f, 0.0f, 0.016f);

    g_assert_false (wheel->is_grounded);

    lrg_wheel_free (wheel);
}

static void
test_wheel_grip (void)
{
    LrgWheel *wheel;
    gfloat grip;

    wheel = lrg_wheel_new (0.0f, 0.0f, 0.0f, 0.3f);
    wheel->is_grounded = TRUE;
    wheel->slip_ratio = 0.15f;
    wheel->slip_angle = 0.1f;

    grip = lrg_wheel_calculate_grip (wheel);

    g_assert_cmpfloat (grip, >, 0.0f);
    g_assert_cmpfloat (grip, <=, 1.0f);

    lrg_wheel_free (wheel);
}

/* ============================================================================
 * LrgVehicle Tests
 * ============================================================================ */

typedef struct
{
    LrgVehicle *vehicle;
} VehicleFixture;

static void
vehicle_fixture_set_up (VehicleFixture *fixture,
                        gconstpointer   user_data)
{
    (void)user_data;

    fixture->vehicle = lrg_vehicle_new ();
}

static void
vehicle_fixture_tear_down (VehicleFixture *fixture,
                           gconstpointer   user_data)
{
    (void)user_data;

    g_clear_object (&fixture->vehicle);
}

static void
test_vehicle_new (VehicleFixture *fixture,
                  gconstpointer   user_data)
{
    (void)user_data;

    g_assert_nonnull (fixture->vehicle);
    g_assert_true (LRG_IS_VEHICLE (fixture->vehicle));
}

static void
test_vehicle_properties (VehicleFixture *fixture,
                         gconstpointer   user_data)
{
    (void)user_data;

    lrg_vehicle_set_mass (fixture->vehicle, 1500.0f);
    g_assert_cmpfloat (lrg_vehicle_get_mass (fixture->vehicle), ==, 1500.0f);

    lrg_vehicle_set_max_speed (fixture->vehicle, 60.0f);
    g_assert_cmpfloat (lrg_vehicle_get_max_speed (fixture->vehicle), ==, 60.0f);

    lrg_vehicle_set_drive_type (fixture->vehicle, LRG_DRIVE_TYPE_ALL);
    g_assert_cmpint (lrg_vehicle_get_drive_type (fixture->vehicle), ==, LRG_DRIVE_TYPE_ALL);
}

static void
test_vehicle_wheels (VehicleFixture *fixture,
                     gconstpointer   user_data)
{
    LrgWheel *wheel;
    guint count;

    (void)user_data;

    lrg_vehicle_setup_standard_wheels (fixture->vehicle, 2.5f, 1.6f, 0.35f);

    count = lrg_vehicle_get_wheel_count (fixture->vehicle);
    g_assert_cmpuint (count, ==, 4);

    wheel = lrg_vehicle_get_wheel (fixture->vehicle, 0);
    g_assert_nonnull (wheel);
    g_assert_cmpfloat (wheel->radius, ==, 0.35f);
}

static void
test_vehicle_position (VehicleFixture *fixture,
                       gconstpointer   user_data)
{
    gfloat x, y, z;

    (void)user_data;

    lrg_vehicle_set_position (fixture->vehicle, 10.0f, 0.5f, 20.0f);
    lrg_vehicle_get_position (fixture->vehicle, &x, &y, &z);

    g_assert_cmpfloat (x, ==, 10.0f);
    g_assert_cmpfloat (y, ==, 0.5f);
    g_assert_cmpfloat (z, ==, 20.0f);
}

static void
test_vehicle_input (VehicleFixture *fixture,
                    gconstpointer   user_data)
{
    (void)user_data;

    lrg_vehicle_set_throttle (fixture->vehicle, 0.8f);
    lrg_vehicle_set_brake (fixture->vehicle, 0.5f);
    lrg_vehicle_set_steering (fixture->vehicle, -0.3f);
    lrg_vehicle_set_handbrake (fixture->vehicle, TRUE);

    /* No assertion needed - just verify no crash */
    g_assert_true (TRUE);
}

static void
test_vehicle_update (VehicleFixture *fixture,
                     gconstpointer   user_data)
{
    gfloat speed_before;
    gfloat speed_after;
    gint i;

    (void)user_data;

    lrg_vehicle_setup_standard_wheels (fixture->vehicle, 2.5f, 1.6f, 0.35f);

    speed_before = lrg_vehicle_get_speed (fixture->vehicle);
    g_assert_cmpfloat (speed_before, ==, 0.0f);

    lrg_vehicle_set_throttle (fixture->vehicle, 1.0f);

    /* Simulate some frames */
    for (i = 0; i < 60; i++)
        lrg_vehicle_update (fixture->vehicle, 0.016f);

    speed_after = lrg_vehicle_get_speed (fixture->vehicle);
    g_assert_cmpfloat (speed_after, >, speed_before);
}

static void
test_vehicle_health (VehicleFixture *fixture,
                     gconstpointer   user_data)
{
    gboolean destroyed;

    (void)user_data;

    lrg_vehicle_set_max_health (fixture->vehicle, 100.0f);
    g_assert_cmpfloat (lrg_vehicle_get_health (fixture->vehicle), ==, 100.0f);

    destroyed = lrg_vehicle_damage (fixture->vehicle, 50.0f);
    g_assert_false (destroyed);
    g_assert_cmpfloat (lrg_vehicle_get_health (fixture->vehicle), ==, 50.0f);

    lrg_vehicle_repair (fixture->vehicle, 30.0f);
    g_assert_cmpfloat (lrg_vehicle_get_health (fixture->vehicle), ==, 80.0f);

    destroyed = lrg_vehicle_damage (fixture->vehicle, 100.0f);
    g_assert_true (destroyed);
    g_assert_true (lrg_vehicle_is_destroyed (fixture->vehicle));
}

static void
test_vehicle_occupancy (VehicleFixture *fixture,
                        gconstpointer   user_data)
{
    gboolean entered;

    (void)user_data;

    g_assert_false (lrg_vehicle_is_occupied (fixture->vehicle));

    entered = lrg_vehicle_enter (fixture->vehicle);
    g_assert_true (entered);
    g_assert_true (lrg_vehicle_is_occupied (fixture->vehicle));

    /* Can't enter again */
    entered = lrg_vehicle_enter (fixture->vehicle);
    g_assert_false (entered);

    lrg_vehicle_exit (fixture->vehicle);
    g_assert_false (lrg_vehicle_is_occupied (fixture->vehicle));
}

/* ============================================================================
 * LrgVehicleController Tests
 * ============================================================================ */

static void
test_controller_new (void)
{
    LrgVehicleController *controller;

    controller = lrg_vehicle_controller_new ();

    g_assert_nonnull (controller);
    g_assert_true (LRG_IS_VEHICLE_CONTROLLER (controller));
    g_assert_null (lrg_vehicle_controller_get_vehicle (controller));

    g_object_unref (controller);
}

static void
test_controller_vehicle (void)
{
    LrgVehicleController *controller;
    LrgVehicle *vehicle;

    controller = lrg_vehicle_controller_new ();
    vehicle = lrg_vehicle_new ();

    lrg_vehicle_controller_set_vehicle (controller, vehicle);
    g_assert_true (lrg_vehicle_controller_get_vehicle (controller) == vehicle);

    g_object_unref (vehicle);
    g_object_unref (controller);
}

static void
test_controller_sensitivity (void)
{
    LrgVehicleController *controller;

    controller = lrg_vehicle_controller_new ();

    lrg_vehicle_controller_set_throttle_sensitivity (controller, 1.5f);
    g_assert_cmpfloat (lrg_vehicle_controller_get_throttle_sensitivity (controller), ==, 1.5f);

    lrg_vehicle_controller_set_steering_sensitivity (controller, 0.8f);
    g_assert_cmpfloat (lrg_vehicle_controller_get_steering_sensitivity (controller), ==, 0.8f);

    g_object_unref (controller);
}

static void
test_controller_update (void)
{
    LrgVehicleController *controller;
    LrgVehicle *vehicle;

    controller = lrg_vehicle_controller_new ();
    vehicle = lrg_vehicle_new ();
    lrg_vehicle_setup_standard_wheels (vehicle, 2.5f, 1.6f, 0.35f);

    lrg_vehicle_controller_set_vehicle (controller, vehicle);
    lrg_vehicle_controller_set_throttle_input (controller, 0.5f);
    lrg_vehicle_controller_set_steering_input (controller, 0.3f);

    lrg_vehicle_controller_update (controller, 0.016f);

    /* Vehicle should have received input */
    g_assert_true (TRUE);

    g_object_unref (vehicle);
    g_object_unref (controller);
}

/* ============================================================================
 * LrgRoad Tests
 * ============================================================================ */

static void
test_road_new (void)
{
    LrgRoad *road;

    road = lrg_road_new ("road1");

    g_assert_nonnull (road);
    g_assert_cmpstr (lrg_road_get_id (road), ==, "road1");
    g_assert_cmpuint (lrg_road_get_waypoint_count (road), ==, 0);

    lrg_road_free (road);
}

static void
test_road_waypoints (void)
{
    LrgRoad *road;
    const LrgRoadWaypoint *wp;

    road = lrg_road_new ("road1");

    lrg_road_add_waypoint (road, 0.0f, 0.0f, 0.0f, 5.0f, 30.0f);
    lrg_road_add_waypoint (road, 10.0f, 0.0f, 10.0f, 5.0f, 30.0f);
    lrg_road_add_waypoint (road, 20.0f, 0.0f, 20.0f, 5.0f, 30.0f);

    g_assert_cmpuint (lrg_road_get_waypoint_count (road), ==, 3);

    wp = lrg_road_get_waypoint (road, 1);
    g_assert_nonnull (wp);
    g_assert_cmpfloat (wp->x, ==, 10.0f);

    lrg_road_free (road);
}

static void
test_road_interpolate (void)
{
    LrgRoad *road;
    gfloat x, y, z;
    gboolean result;

    road = lrg_road_new ("road1");

    lrg_road_add_waypoint (road, 0.0f, 0.0f, 0.0f, 5.0f, 30.0f);
    lrg_road_add_waypoint (road, 100.0f, 0.0f, 0.0f, 5.0f, 30.0f);

    result = lrg_road_interpolate (road, 0.5f, &x, &y, &z);

    g_assert_true (result);
    g_assert_cmpfloat (fabs (x - 50.0f), <, 0.01f);

    lrg_road_free (road);
}

static void
test_road_length (void)
{
    LrgRoad *road;
    gfloat length;

    road = lrg_road_new ("road1");

    lrg_road_add_waypoint (road, 0.0f, 0.0f, 0.0f, 5.0f, 30.0f);
    lrg_road_add_waypoint (road, 100.0f, 0.0f, 0.0f, 5.0f, 30.0f);

    length = lrg_road_get_length (road);
    g_assert_cmpfloat (fabs (length - 100.0f), <, 0.01f);

    lrg_road_free (road);
}

static void
test_road_nearest (void)
{
    LrgRoad *road;
    gfloat t, dist;
    gboolean result;

    road = lrg_road_new ("road1");

    lrg_road_add_waypoint (road, 0.0f, 0.0f, 0.0f, 5.0f, 30.0f);
    lrg_road_add_waypoint (road, 100.0f, 0.0f, 0.0f, 5.0f, 30.0f);

    result = lrg_road_find_nearest_point (road, 50.0f, 10.0f, 0.0f, &t, &dist);

    g_assert_true (result);
    g_assert_cmpfloat (fabs (t - 0.5f), <, 0.01f);
    g_assert_cmpfloat (fabs (dist - 10.0f), <, 0.01f);

    lrg_road_free (road);
}

/* ============================================================================
 * LrgRoadNetwork Tests
 * ============================================================================ */

typedef struct
{
    LrgRoadNetwork *network;
} NetworkFixture;

static void
network_fixture_set_up (NetworkFixture *fixture,
                        gconstpointer   user_data)
{
    LrgRoad *road1;
    LrgRoad *road2;

    (void)user_data;

    fixture->network = lrg_road_network_new ();

    road1 = lrg_road_new ("road1");
    lrg_road_add_waypoint (road1, 0.0f, 0.0f, 0.0f, 5.0f, 30.0f);
    lrg_road_add_waypoint (road1, 100.0f, 0.0f, 0.0f, 5.0f, 30.0f);
    lrg_road_network_add_road (fixture->network, road1);

    road2 = lrg_road_new ("road2");
    lrg_road_add_waypoint (road2, 100.0f, 0.0f, 0.0f, 5.0f, 30.0f);
    lrg_road_add_waypoint (road2, 100.0f, 0.0f, 100.0f, 5.0f, 30.0f);
    lrg_road_network_add_road (fixture->network, road2);

    lrg_road_network_connect (fixture->network, "road1", TRUE, "road2", FALSE);
}

static void
network_fixture_tear_down (NetworkFixture *fixture,
                           gconstpointer   user_data)
{
    (void)user_data;

    g_clear_object (&fixture->network);
}

static void
test_network_new (void)
{
    LrgRoadNetwork *network;

    network = lrg_road_network_new ();

    g_assert_nonnull (network);
    g_assert_true (LRG_IS_ROAD_NETWORK (network));
    g_assert_cmpuint (lrg_road_network_get_road_count (network), ==, 0);

    g_object_unref (network);
}

static void
test_network_add_road (NetworkFixture *fixture,
                       gconstpointer   user_data)
{
    (void)user_data;

    g_assert_cmpuint (lrg_road_network_get_road_count (fixture->network), ==, 2);
    g_assert_nonnull (lrg_road_network_get_road (fixture->network, "road1"));
    g_assert_nonnull (lrg_road_network_get_road (fixture->network, "road2"));
}

static void
test_network_connections (NetworkFixture *fixture,
                          gconstpointer   user_data)
{
    GList *connections;

    (void)user_data;

    connections = lrg_road_network_get_connections (fixture->network, "road1", TRUE);

    g_assert_nonnull (connections);
    g_assert_cmpuint (g_list_length (connections), ==, 1);
    g_assert_cmpstr (connections->data, ==, "road2");

    g_list_free (connections);
}

static void
test_network_route (NetworkFixture *fixture,
                    gconstpointer   user_data)
{
    GList *route;
    gboolean found;

    (void)user_data;

    found = lrg_road_network_find_route (fixture->network,
                                         "road1", 0.0f,
                                         "road2", 1.0f,
                                         &route);

    g_assert_true (found);
    g_assert_nonnull (route);
    g_assert_cmpuint (g_list_length (route), ==, 2);

    g_list_free (route);
}

static void
test_network_nearest (NetworkFixture *fixture,
                      gconstpointer   user_data)
{
    const gchar *road_id;
    gfloat t, dist;
    gboolean found;

    (void)user_data;

    found = lrg_road_network_get_nearest_road (fixture->network,
                                               50.0f, 0.0f, 0.0f,
                                               &road_id, &t, &dist);

    g_assert_true (found);
    g_assert_cmpstr (road_id, ==, "road1");
    g_assert_cmpfloat (fabs (t - 0.5f), <, 0.01f);
}

/* ============================================================================
 * LrgTrafficAgent Tests
 * ============================================================================ */

static void
test_traffic_agent_new (void)
{
    LrgTrafficAgent *agent;

    agent = lrg_traffic_agent_new ();

    g_assert_nonnull (agent);
    g_assert_true (LRG_IS_TRAFFIC_AGENT (agent));
    g_assert_cmpint (lrg_traffic_agent_get_state (agent), ==, LRG_TRAFFIC_STATE_IDLE);

    g_object_unref (agent);
}

static void
test_traffic_agent_behavior (void)
{
    LrgTrafficAgent *agent;

    agent = lrg_traffic_agent_new ();

    lrg_traffic_agent_set_behavior (agent, LRG_TRAFFIC_BEHAVIOR_AGGRESSIVE);
    g_assert_cmpint (lrg_traffic_agent_get_behavior (agent), ==,
                     LRG_TRAFFIC_BEHAVIOR_AGGRESSIVE);

    lrg_traffic_agent_set_max_speed (agent, 50.0f);
    g_assert_cmpfloat (lrg_traffic_agent_get_max_speed (agent), ==, 50.0f);

    g_object_unref (agent);
}

static void
test_traffic_agent_start_stop (void)
{
    LrgTrafficAgent *agent;

    agent = lrg_traffic_agent_new ();

    g_assert_false (lrg_traffic_agent_is_active (agent));

    lrg_traffic_agent_start (agent);
    g_assert_true (lrg_traffic_agent_is_active (agent));
    g_assert_cmpint (lrg_traffic_agent_get_state (agent), ==, LRG_TRAFFIC_STATE_DRIVING);

    lrg_traffic_agent_stop (agent);
    g_assert_false (lrg_traffic_agent_is_active (agent));

    g_object_unref (agent);
}

static void
test_traffic_agent_obstacles (void)
{
    LrgTrafficAgent *agent;

    agent = lrg_traffic_agent_new ();

    lrg_traffic_agent_add_obstacle (agent, 10.0f, 0.0f, 10.0f, 2.0f);
    lrg_traffic_agent_add_obstacle (agent, 20.0f, 0.0f, 20.0f, 3.0f);

    /* Verify no crash */
    g_assert_true (TRUE);

    lrg_traffic_agent_clear_obstacles (agent);

    g_object_unref (agent);
}

/* ============================================================================
 * LrgVehicleCamera Tests
 * ============================================================================ */

static void
test_vehicle_camera_new (void)
{
    LrgVehicleCamera *camera;

    camera = lrg_vehicle_camera_new ();

    g_assert_nonnull (camera);
    g_assert_true (LRG_IS_VEHICLE_CAMERA (camera));
    g_assert_cmpint (lrg_vehicle_camera_get_mode (camera), ==, LRG_VEHICLE_CAMERA_FOLLOW);

    g_object_unref (camera);
}

static void
test_vehicle_camera_modes (void)
{
    LrgVehicleCamera *camera;

    camera = lrg_vehicle_camera_new ();

    lrg_vehicle_camera_set_mode (camera, LRG_VEHICLE_CAMERA_HOOD);
    g_assert_cmpint (lrg_vehicle_camera_get_mode (camera), ==, LRG_VEHICLE_CAMERA_HOOD);

    lrg_vehicle_camera_cycle_mode (camera);
    g_assert_cmpint (lrg_vehicle_camera_get_mode (camera), ==, LRG_VEHICLE_CAMERA_COCKPIT);

    g_object_unref (camera);
}

static void
test_vehicle_camera_settings (void)
{
    LrgVehicleCamera *camera;

    camera = lrg_vehicle_camera_new ();

    lrg_vehicle_camera_set_follow_distance (camera, 10.0f);
    g_assert_cmpfloat (lrg_vehicle_camera_get_follow_distance (camera), ==, 10.0f);

    lrg_vehicle_camera_set_follow_height (camera, 5.0f);
    g_assert_cmpfloat (lrg_vehicle_camera_get_follow_height (camera), ==, 5.0f);

    lrg_vehicle_camera_set_smoothing (camera, 0.9f);
    g_assert_cmpfloat (lrg_vehicle_camera_get_smoothing (camera), ==, 0.9f);

    g_object_unref (camera);
}

/* ============================================================================
 * LrgVehicleAudio Tests
 * ============================================================================ */

static void
test_vehicle_audio_new (void)
{
    LrgVehicleAudio *audio;

    audio = lrg_vehicle_audio_new ();

    g_assert_nonnull (audio);
    g_assert_true (LRG_IS_VEHICLE_AUDIO (audio));
    g_assert_false (lrg_vehicle_audio_is_playing (audio));

    g_object_unref (audio);
}

static void
test_vehicle_audio_volume (void)
{
    LrgVehicleAudio *audio;

    audio = lrg_vehicle_audio_new ();

    lrg_vehicle_audio_set_master_volume (audio, 0.8f);
    g_assert_cmpfloat (lrg_vehicle_audio_get_master_volume (audio), ==, 0.8f);

    g_object_unref (audio);
}

static void
test_vehicle_audio_playback (void)
{
    LrgVehicleAudio *audio;

    audio = lrg_vehicle_audio_new ();

    lrg_vehicle_audio_start (audio);
    g_assert_true (lrg_vehicle_audio_is_playing (audio));

    lrg_vehicle_audio_stop (audio);
    g_assert_false (lrg_vehicle_audio_is_playing (audio));

    g_object_unref (audio);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgWheel tests */
    g_test_add_func ("/vehicle/wheel/new", test_wheel_new);
    g_test_add_func ("/vehicle/wheel/copy", test_wheel_copy);
    g_test_add_func ("/vehicle/wheel/suspension", test_wheel_suspension);
    g_test_add_func ("/vehicle/wheel/update", test_wheel_update);
    g_test_add_func ("/vehicle/wheel/grip", test_wheel_grip);

    /* LrgVehicle tests */
    g_test_add ("/vehicle/vehicle/new", VehicleFixture, NULL,
                vehicle_fixture_set_up, test_vehicle_new, vehicle_fixture_tear_down);
    g_test_add ("/vehicle/vehicle/properties", VehicleFixture, NULL,
                vehicle_fixture_set_up, test_vehicle_properties, vehicle_fixture_tear_down);
    g_test_add ("/vehicle/vehicle/wheels", VehicleFixture, NULL,
                vehicle_fixture_set_up, test_vehicle_wheels, vehicle_fixture_tear_down);
    g_test_add ("/vehicle/vehicle/position", VehicleFixture, NULL,
                vehicle_fixture_set_up, test_vehicle_position, vehicle_fixture_tear_down);
    g_test_add ("/vehicle/vehicle/input", VehicleFixture, NULL,
                vehicle_fixture_set_up, test_vehicle_input, vehicle_fixture_tear_down);
    g_test_add ("/vehicle/vehicle/update", VehicleFixture, NULL,
                vehicle_fixture_set_up, test_vehicle_update, vehicle_fixture_tear_down);
    g_test_add ("/vehicle/vehicle/health", VehicleFixture, NULL,
                vehicle_fixture_set_up, test_vehicle_health, vehicle_fixture_tear_down);
    g_test_add ("/vehicle/vehicle/occupancy", VehicleFixture, NULL,
                vehicle_fixture_set_up, test_vehicle_occupancy, vehicle_fixture_tear_down);

    /* LrgVehicleController tests */
    g_test_add_func ("/vehicle/controller/new", test_controller_new);
    g_test_add_func ("/vehicle/controller/vehicle", test_controller_vehicle);
    g_test_add_func ("/vehicle/controller/sensitivity", test_controller_sensitivity);
    g_test_add_func ("/vehicle/controller/update", test_controller_update);

    /* LrgRoad tests */
    g_test_add_func ("/vehicle/road/new", test_road_new);
    g_test_add_func ("/vehicle/road/waypoints", test_road_waypoints);
    g_test_add_func ("/vehicle/road/interpolate", test_road_interpolate);
    g_test_add_func ("/vehicle/road/length", test_road_length);
    g_test_add_func ("/vehicle/road/nearest", test_road_nearest);

    /* LrgRoadNetwork tests */
    g_test_add_func ("/vehicle/network/new", test_network_new);
    g_test_add ("/vehicle/network/add-road", NetworkFixture, NULL,
                network_fixture_set_up, test_network_add_road, network_fixture_tear_down);
    g_test_add ("/vehicle/network/connections", NetworkFixture, NULL,
                network_fixture_set_up, test_network_connections, network_fixture_tear_down);
    g_test_add ("/vehicle/network/route", NetworkFixture, NULL,
                network_fixture_set_up, test_network_route, network_fixture_tear_down);
    g_test_add ("/vehicle/network/nearest", NetworkFixture, NULL,
                network_fixture_set_up, test_network_nearest, network_fixture_tear_down);

    /* LrgTrafficAgent tests */
    g_test_add_func ("/vehicle/traffic-agent/new", test_traffic_agent_new);
    g_test_add_func ("/vehicle/traffic-agent/behavior", test_traffic_agent_behavior);
    g_test_add_func ("/vehicle/traffic-agent/start-stop", test_traffic_agent_start_stop);
    g_test_add_func ("/vehicle/traffic-agent/obstacles", test_traffic_agent_obstacles);

    /* LrgVehicleCamera tests */
    g_test_add_func ("/vehicle/camera/new", test_vehicle_camera_new);
    g_test_add_func ("/vehicle/camera/modes", test_vehicle_camera_modes);
    g_test_add_func ("/vehicle/camera/settings", test_vehicle_camera_settings);

    /* LrgVehicleAudio tests */
    g_test_add_func ("/vehicle/audio/new", test_vehicle_audio_new);
    g_test_add_func ("/vehicle/audio/volume", test_vehicle_audio_volume);
    g_test_add_func ("/vehicle/audio/playback", test_vehicle_audio_playback);

    return g_test_run ();
}
