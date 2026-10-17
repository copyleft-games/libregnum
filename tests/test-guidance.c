/* test-guidance.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the guidance module (LrgWaypoint, LrgWaypointIndicator,
 * the beacon's shape) and LrgQuestTracker.  Everything here is pure: no
 * window or graphics context.
 */

#include <glib.h>
#include <libregnum.h>
#include <math.h>
#include <string.h>

/* ==========================================================================
 * LrgWaypoint
 * ========================================================================== */

static void
test_waypoint_basics (void)
{
    g_autoptr(LrgWaypoint) waypoint = NULL;
    g_autoptr(LrgWaypoint) copy = NULL;
    gfloat position[3];
    gfloat from[3] = { 0.0f, 0.0f, 0.0f };
    guint8 r;
    guint8 a;

    waypoint = lrg_waypoint_new ("quest:1");
    g_assert_cmpstr (lrg_waypoint_get_id (waypoint), ==, "quest:1");
    g_assert_null (lrg_waypoint_get_label (waypoint));
    g_assert_cmpfloat (lrg_waypoint_get_radius (waypoint), ==, LRG_WAYPOINT_DEFAULT_RADIUS);

    lrg_waypoint_set_label (waypoint, "The Dirty Pit");
    lrg_waypoint_set_position (waypoint, 3.0f, 0.0f, 4.0f);
    lrg_waypoint_set_color (waypoint, 250, 200, 40, 128);
    lrg_waypoint_get_position (waypoint, position);
    g_assert_cmpfloat (position[0], ==, 3.0f);
    g_assert_cmpfloat (position[2], ==, 4.0f);
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_distance_from (waypoint, from), 5.0f, 1e-5);
    lrg_waypoint_get_color (waypoint, &r, NULL, NULL, &a);
    g_assert_cmpuint (r, ==, 250);
    g_assert_cmpuint (a, ==, 128);

    /* a copy is independent */
    copy = lrg_waypoint_copy (waypoint);
    lrg_waypoint_set_label (waypoint, "elsewhere");
    g_assert_cmpstr (lrg_waypoint_get_label (copy), ==, "The Dirty Pit");

    /* the radius never goes negative */
    lrg_waypoint_set_radius (waypoint, -3.0f);
    g_assert_cmpfloat (lrg_waypoint_get_radius (waypoint), ==, 0.0f);
}

static void
test_waypoint_heading (void)
{
    g_autoptr(LrgWaypoint) waypoint = NULL;
    gfloat from[3] = { 0.0f, 0.0f, 0.0f };

    waypoint = lrg_waypoint_new (NULL);
    /* +Z is 0, +X is 90, -Z is 180, -X is 270 */
    lrg_waypoint_set_position (waypoint, 0.0f, 0.0f, 10.0f);
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_heading_from (waypoint, from), 0.0f, 1e-4);
    lrg_waypoint_set_position (waypoint, 10.0f, 5.0f, 0.0f);
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_heading_from (waypoint, from), 90.0f, 1e-4);
    lrg_waypoint_set_position (waypoint, 0.0f, 0.0f, -10.0f);
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_heading_from (waypoint, from), 180.0f, 1e-4);
    lrg_waypoint_set_position (waypoint, -10.0f, 0.0f, 0.0f);
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_heading_from (waypoint, from), 270.0f, 1e-4);
    /* straight above: no heading */
    lrg_waypoint_set_position (waypoint, 0.0f, 9.0f, 0.0f);
    g_assert_cmpfloat (lrg_waypoint_heading_from (waypoint, from), ==, 0.0f);

    /* seen from a facing: left negative, right positive, wrapped */
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_relative_heading (90.0f, 0.0f), 90.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_relative_heading (10.0f, 350.0f), 20.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_relative_heading (350.0f, 10.0f), -20.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_relative_heading (180.0f, 0.0f), 180.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (lrg_waypoint_relative_heading (0.0f, 180.0f), 180.0f, 1e-4);
    g_assert_cmpfloat (lrg_waypoint_relative_heading (NAN, 0.0f), ==, 0.0f);
}

static void
test_waypoint_reached (void)
{
    g_autoptr(LrgWaypoint) waypoint = NULL;
    gfloat near_spot[3] = { 1.0f, 0.5f, 0.0f };
    gfloat far_spot[3] = { 3.0f, 0.0f, 0.0f };
    gfloat above[3] = { 0.0f, 9.0f, 0.0f };

    waypoint = lrg_waypoint_new (NULL);
    g_assert_true (lrg_waypoint_is_reached (waypoint, near_spot));
    g_assert_false (lrg_waypoint_is_reached (waypoint, far_spot));
    /* a floor far above is not the spot */
    g_assert_false (lrg_waypoint_is_reached (waypoint, above));
}

static void
test_waypoint_format (void)
{
    static const struct
    {
        gfloat       distance;
        const gchar *text;
    } cases[] = {
        { 0.0f,      "0 m" },
        { 7.4f,      "7 m" },
        { 312.6f,    "313 m" },
        { 999.4f,    "999 m" },
        { 1000.0f,   "1.0 km" },
        { 1449.0f,   "1.4 km" },
        { 9940.0f,   "9.9 km" },
        { 27300.0f,  "27 km" },
        { -5.0f,     "0 m" },
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (cases); i++)
    {
        g_autofree gchar *text = lrg_waypoint_format_distance (cases[i].distance);

        g_assert_cmpstr (text, ==, cases[i].text);
    }
    {
        g_autofree gchar *text = lrg_waypoint_format_distance (INFINITY);

        g_assert_cmpstr (text, ==, "0 m");
    }
}

/* ==========================================================================
 * LrgWaypointIndicator
 * ========================================================================== */

/* a camera at the origin looking along -Z (raylib's default forward) */
static const gfloat eye[3] = { 0.0f, 0.0f, 0.0f };
static const gfloat target[3] = { 0.0f, 0.0f, -1.0f };
static const gfloat up[3] = { 0.0f, 1.0f, 0.0f };

static void
test_indicator_on_screen (void)
{
    LrgWaypointIndicator ind;
    gfloat ahead[3] = { 0.0f, 0.0f, -10.0f };
    gfloat right[3] = { 2.0f, 0.0f, -10.0f };
    gfloat above[3] = { 0.0f, 2.0f, -10.0f };

    /* straight ahead: the centre */
    g_assert_true (lrg_waypoint_indicator_compute (&ind, eye, target, up, 60.0f, 320.0f, 200.0f,
                                                   8.0f, ahead));
    g_assert_true (ind.on_screen);
    g_assert_false (ind.behind);
    g_assert_cmpfloat_with_epsilon (ind.x, 160.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (ind.y, 100.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (ind.depth, 10.0f, 1e-4);

    /* to the right (+X) is right on the screen, at the perspective's x */
    g_assert_true (lrg_waypoint_indicator_compute (&ind, eye, target, up, 60.0f, 320.0f, 200.0f,
                                                   8.0f, right));
    g_assert_true (ind.on_screen);
    {
        gdouble expect;

        expect = (2.0 / (10.0 * tan (30.0 * G_PI / 180.0) * 1.6) + 1.0) * 0.5 * 320.0;
        g_assert_cmpfloat_with_epsilon (ind.x, (gfloat) expect, 1e-2);
    }
    g_assert_cmpfloat_with_epsilon (ind.y, 100.0f, 1e-3);

    /* above is up on the screen (smaller y) */
    g_assert_true (lrg_waypoint_indicator_compute (&ind, eye, target, up, 60.0f, 320.0f, 200.0f,
                                                   8.0f, above));
    g_assert_true (ind.on_screen);
    g_assert_cmpfloat (ind.y, <, 100.0f);
    g_assert_cmpfloat_with_epsilon (ind.x, 160.0f, 1e-3);
}

static void
test_indicator_edges (void)
{
    LrgWaypointIndicator ind;
    gfloat far_right[3] = { 50.0f, 0.0f, -10.0f };
    gfloat behind_left[3] = { -5.0f, 0.0f, 10.0f };
    gfloat straight_behind[3] = { 0.0f, 0.0f, 10.0f };

    /* far to the right: pinned to the right edge less the margin */
    g_assert_true (lrg_waypoint_indicator_compute (&ind, eye, target, up, 60.0f, 320.0f, 200.0f,
                                                   8.0f, far_right));
    g_assert_false (ind.on_screen);
    g_assert_false (ind.behind);
    g_assert_cmpfloat_with_epsilon (ind.x, 312.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (ind.y, 100.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (ind.angle, 0.0f, 1e-4);

    /* behind and to the left: the left edge */
    g_assert_true (lrg_waypoint_indicator_compute (&ind, eye, target, up, 60.0f, 320.0f, 200.0f,
                                                   8.0f, behind_left));
    g_assert_false (ind.on_screen);
    g_assert_true (ind.behind);
    g_assert_cmpfloat (ind.depth, <, 0.0f);
    g_assert_cmpfloat_with_epsilon (ind.x, 8.0f, 1e-3);

    /* straight behind: the bottom edge, pointing down */
    g_assert_true (lrg_waypoint_indicator_compute (&ind, eye, target, up, 60.0f, 320.0f, 200.0f,
                                                   8.0f, straight_behind));
    g_assert_true (ind.behind);
    g_assert_cmpfloat_with_epsilon (ind.y, 192.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (ind.x, 160.0f, 1e-3);
    g_assert_cmpfloat_with_epsilon (ind.angle, (gfloat) (G_PI / 2.0), 1e-4);
}

static void
test_indicator_degenerate (void)
{
    LrgWaypointIndicator ind;
    gfloat point[3] = { 0.0f, 0.0f, -10.0f };
    gfloat vertical_target[3] = { 0.0f, 5.0f, 0.0f };

    g_assert_false (lrg_waypoint_indicator_compute (&ind, eye, eye, up, 60.0f, 320.0f, 200.0f,
                                                    0.0f, point));
    g_assert_false (lrg_waypoint_indicator_compute (&ind, eye, vertical_target, up, 60.0f, 320.0f,
                                                    200.0f, 0.0f, point));
    g_assert_false (lrg_waypoint_indicator_compute (&ind, eye, target, up, 0.0f, 320.0f, 200.0f,
                                                    0.0f, point));
    g_assert_false (lrg_waypoint_indicator_compute (&ind, eye, target, up, 60.0f, 0.0f, 200.0f,
                                                    0.0f, point));
    g_assert_false (ind.on_screen);
}

static void
test_indicator_camera (void)
{
    g_autoptr(LrgCamera3D) camera = NULL;
    LrgWaypointIndicator ind;
    gfloat point[3] = { 10.0f, 1.0f, 0.0f };

    /* a camera looking along +X sees a point on +X at the centre */
    camera = lrg_camera3d_new ();
    lrg_camera3d_set_position_xyz (camera, 0.0f, 1.0f, 0.0f);
    lrg_camera3d_set_target_xyz (camera, 1.0f, 1.0f, 0.0f);
    lrg_camera3d_set_up_xyz (camera, 0.0f, 1.0f, 0.0f);
    lrg_camera3d_set_fovy (camera, 70.0f);
    g_assert_true (lrg_waypoint_indicator_compute_for_camera (&ind, camera, 640.0f, 360.0f, 0.0f,
                                                              point));
    g_assert_true (ind.on_screen);
    g_assert_cmpfloat_with_epsilon (ind.x, 320.0f, 1e-2);
    g_assert_cmpfloat_with_epsilon (ind.y, 180.0f, 1e-2);
}

/* ==========================================================================
 * LrgBeacon
 * ========================================================================== */

static void
test_beacon_shape (void)
{
    LrgBeaconStyle style;
    LrgBeaconShape shape;
    gfloat foot[3] = { 0.0f, 0.0f, 0.0f };
    gfloat close_eye[3] = { 1.0f, 0.0f, 0.0f };
    gfloat mid_eye[3] = { 3.75f, 0.0f, 0.0f };
    gfloat far_eye[3] = { 1000.0f, 0.0f, 0.0f };
    gfloat very_far_eye[3] = { 100000.0f, 0.0f, 0.0f };
    gfloat bad_eye[3] = { NAN, 0.0f, 0.0f };

    lrg_beacon_style_init (&style);

    /* standing at it: nothing to draw */
    g_assert_false (lrg_beacon_compute (&style, close_eye, foot, 0.0, &shape));
    g_assert_cmpfloat (shape.alpha, ==, 0.0f);
    g_assert_cmpfloat_with_epsilon (shape.distance, 1.0f, 1e-5);

    /* half way through the fade: half the opacity */
    g_assert_true (lrg_beacon_compute (&style, mid_eye, foot, 0.0, &shape));
    g_assert_cmpfloat_with_epsilon (shape.alpha, style.alpha * 0.5f, 1e-4);

    /* far: full opacity, a wider column, the marker over the foot */
    g_assert_true (lrg_beacon_compute (NULL, far_eye, foot, 0.0, &shape));
    g_assert_cmpfloat_with_epsilon (shape.alpha, style.alpha, 1e-5);
    g_assert_cmpfloat_with_epsilon (shape.radius, style.min_radius + 1000.0f * style.radius_per_unit,
                                    1e-4);
    g_assert_cmpfloat (shape.height, ==, style.height);
    g_assert_cmpfloat (shape.marker[0], ==, 0.0f);
    g_assert_cmpfloat (shape.marker[1], >, style.marker_lift);

    /* very far: the widest column */
    g_assert_true (lrg_beacon_compute (&style, very_far_eye, foot, 0.0, &shape));
    g_assert_cmpfloat (shape.radius, ==, style.max_radius);

    /* the marker bobs: a quarter period lifts it by the amplitude */
    {
        LrgBeaconShape later;

        g_assert_true (lrg_beacon_compute (&style, far_eye, foot, 0.0, &shape));
        g_assert_true (lrg_beacon_compute (&style, far_eye, foot, style.bob_period * 0.25, &later));
        g_assert_cmpfloat_with_epsilon (later.marker[1] - shape.marker[1], style.bob_amplitude, 1e-4);
    }

    /* not finite: refused */
    g_assert_false (lrg_beacon_compute (&style, bad_eye, foot, 0.0, &shape));
}

/* ==========================================================================
 * LrgQuestTracker
 * ========================================================================== */

typedef struct
{
    guint    count;
    gchar   *last_id;
    gboolean last_tracked;
} Changes;

static void
on_changed (LrgQuestTracker *tracker,
            const gchar     *quest_id,
            gboolean         tracked,
            gpointer         user_data)
{
    Changes *changes = (Changes *) user_data;

    changes->count++;
    g_free (changes->last_id);
    changes->last_id = g_strdup (quest_id);
    changes->last_tracked = tracked;
}

static void
test_tracker_track (void)
{
    g_autoptr(LrgQuestTracker) tracker = NULL;
    g_auto(GStrv) ids = NULL;
    Changes changes = { 0, NULL, FALSE };

    tracker = lrg_quest_tracker_new (3);
    g_signal_connect (tracker, "changed", G_CALLBACK (on_changed), &changes);
    g_assert_cmpuint (lrg_quest_tracker_get_max_tracked (tracker), ==, 3);

    g_assert_true (lrg_quest_tracker_track (tracker, "a"));
    g_assert_cmpuint (changes.count, ==, 1);
    g_assert_cmpstr (changes.last_id, ==, "a");
    g_assert_true (changes.last_tracked);
    g_assert_true (lrg_quest_tracker_track (tracker, "b"));
    g_assert_true (lrg_quest_tracker_track (tracker, "c"));
    /* full, repeated, empty */
    g_assert_false (lrg_quest_tracker_track (tracker, "d"));
    g_assert_false (lrg_quest_tracker_track (tracker, "a"));
    g_assert_false (lrg_quest_tracker_track (tracker, ""));
    g_assert_false (lrg_quest_tracker_track (tracker, NULL));
    g_assert_cmpuint (changes.count, ==, 3);

    g_assert_true (lrg_quest_tracker_is_tracked (tracker, "b"));
    g_assert_cmpint (lrg_quest_tracker_get_index (tracker, "c"), ==, 2);
    g_assert_cmpint (lrg_quest_tracker_get_index (tracker, "x"), ==, -1);

    /* untrack makes room; the signal names the quest */
    g_assert_true (lrg_quest_tracker_untrack (tracker, "b"));
    g_assert_cmpstr (changes.last_id, ==, "b");
    g_assert_false (changes.last_tracked);
    g_assert_false (lrg_quest_tracker_untrack (tracker, "b"));
    g_assert_true (lrg_quest_tracker_set_tracked (tracker, "d", TRUE));
    ids = lrg_quest_tracker_get_ids (tracker);
    g_assert_cmpuint (g_strv_length (ids), ==, 3);
    g_assert_cmpstr (ids[0], ==, "a");
    g_assert_cmpstr (ids[1], ==, "c");
    g_assert_cmpstr (ids[2], ==, "d");

    /* move to the front */
    g_assert_true (lrg_quest_tracker_move (tracker, "d", 0));
    g_assert_cmpint (lrg_quest_tracker_get_index (tracker, "d"), ==, 0);
    g_assert_null (changes.last_id);
    g_assert_false (lrg_quest_tracker_move (tracker, "zz", 0));
    g_assert_true (lrg_quest_tracker_move (tracker, "d", 99));
    g_assert_cmpint (lrg_quest_tracker_get_index (tracker, "d"), ==, 2);

    /* a lower limit drops the latest */
    lrg_quest_tracker_set_max_tracked (tracker, 1);
    g_assert_cmpuint (lrg_quest_tracker_get_count (tracker), ==, 1);
    g_assert_true (lrg_quest_tracker_is_tracked (tracker, "a"));

    lrg_quest_tracker_clear (tracker);
    g_assert_cmpuint (lrg_quest_tracker_get_count (tracker), ==, 0);
    g_free (changes.last_id);
}

static void
test_tracker_variant (void)
{
    g_autoptr(LrgQuestTracker) tracker = NULL;
    g_autoptr(LrgQuestTracker) restored = NULL;
    g_autoptr(GVariant) state = NULL;
    g_autoptr(GVariant) wrong = NULL;
    g_autoptr(GVariant) messy = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *messy_ids[] = { "x", "", "y", "x", "z", "w", NULL };

    tracker = lrg_quest_tracker_new (0);
    g_assert_cmpuint (lrg_quest_tracker_get_max_tracked (tracker), ==, LRG_QUEST_TRACKER_DEFAULT_MAX);
    lrg_quest_tracker_track (tracker, "12");
    lrg_quest_tracker_track (tracker, "7");
    state = g_variant_ref_sink (lrg_quest_tracker_to_variant (tracker));
    g_assert_cmpstr (g_variant_get_type_string (state), ==, "as");

    restored = lrg_quest_tracker_new (3);
    g_assert_true (lrg_quest_tracker_from_variant (restored, state, &error));
    g_assert_no_error (error);
    g_assert_cmpint (lrg_quest_tracker_get_index (restored, "7"), ==, 1);

    /* the wrong type is refused, nothing changes */
    wrong = g_variant_ref_sink (g_variant_new_int32 (3));
    g_assert_false (lrg_quest_tracker_from_variant (restored, wrong, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_assert_cmpuint (lrg_quest_tracker_get_count (restored), ==, 2);
    g_clear_error (&error);

    /* empty and repeated ids skipped, the limit kept */
    messy = g_variant_ref_sink (g_variant_new_strv (messy_ids, -1));
    g_assert_true (lrg_quest_tracker_from_variant (restored, messy, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_quest_tracker_get_count (restored), ==, 3);
    g_assert_cmpint (lrg_quest_tracker_get_index (restored, "z"), ==, 2);
    g_assert_false (lrg_quest_tracker_is_tracked (restored, "w"));
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/guidance/waypoint/basics", test_waypoint_basics);
    g_test_add_func ("/guidance/waypoint/heading", test_waypoint_heading);
    g_test_add_func ("/guidance/waypoint/reached", test_waypoint_reached);
    g_test_add_func ("/guidance/waypoint/format", test_waypoint_format);
    g_test_add_func ("/guidance/indicator/on-screen", test_indicator_on_screen);
    g_test_add_func ("/guidance/indicator/edges", test_indicator_edges);
    g_test_add_func ("/guidance/indicator/degenerate", test_indicator_degenerate);
    g_test_add_func ("/guidance/indicator/camera", test_indicator_camera);
    g_test_add_func ("/guidance/beacon/shape", test_beacon_shape);
    g_test_add_func ("/quest/tracker/track", test_tracker_track);
    g_test_add_func ("/quest/tracker/variant", test_tracker_variant);

    return g_test_run ();
}
