/* test-nav-level.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavLevel on synthetic levels: walking round a wall, a door that is
 * open, closed (only door openers pass) and locked, sight blocked by
 * walls and by doors that are not open, the doors a path crosses, the
 * walk ray, the asynchronous bake and the failure paths.
 */

#include <libregnum.h>
#include <gio/gio.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Geometry                                                             */

static void
quad (GArray       *tris,
      const gfloat *a,
      const gfloat *b,
      const gfloat *c,
      const gfloat *d)
{
    g_array_append_vals (tris, a, 3);
    g_array_append_vals (tris, b, 3);
    g_array_append_vals (tris, c, 3);
    g_array_append_vals (tris, a, 3);
    g_array_append_vals (tris, c, 3);
    g_array_append_vals (tris, d, 3);
}

static void
floor_rect (GArray *tris,
            gfloat  x0,
            gfloat  z0,
            gfloat  x1,
            gfloat  z1)
{
    const gfloat a[3] = { x0, 0.0f, z0 };
    const gfloat b[3] = { x1, 0.0f, z0 };
    const gfloat c[3] = { x1, 0.0f, z1 };
    const gfloat d[3] = { x0, 0.0f, z1 };

    quad (tris, a, b, c, d);
}

static void
box (GArray *tris,
     gfloat  x0,
     gfloat  y0,
     gfloat  z0,
     gfloat  x1,
     gfloat  y1,
     gfloat  z1)
{
    const gfloat p[8][3] = {
        { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 },
        { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 },
    };

    quad (tris, p[0], p[1], p[2], p[3]);
    quad (tris, p[4], p[5], p[6], p[7]);
    quad (tris, p[0], p[1], p[5], p[4]);
    quad (tris, p[1], p[2], p[6], p[5]);
    quad (tris, p[2], p[3], p[7], p[6]);
    quad (tris, p[3], p[0], p[4], p[7]);
}

static const gfloat extents[3] = { 1.0f, 1.0f, 1.0f };

static gfloat
path_length (const gfloat *p,
             guint         n)
{
    gfloat len = 0.0f;
    guint i;

    for (i = 3; i < n; i += 3)
        len += sqrtf ((p[i] - p[i - 3]) * (p[i] - p[i - 3]) + (p[i + 2] - p[i - 1]) * (p[i + 2] - p[i - 1]));
    return len;
}

/* a 20 x 10 room, a wall at x = 10 from z = 0 to 8 (a gap at 8 .. 10) */
static LrgNavLevel *
wall_level (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(GError) error = NULL;
    LrgNavLevel *level = lrg_nav_level_new ();

    floor_rect (tris, 0, 0, 20, 10);
    box (tris, 9.8f, 0.0f, -0.5f, 10.2f, 3.0f, 8.0f);
    g_assert_cmpuint (lrg_nav_level_add_triangles (level, (const gfloat *) tris->data, tris->len), ==,
                      tris->len / 9);
    g_assert_true (lrg_nav_level_bake (level, &error));
    g_assert_no_error (error);
    return level;
}

/* two rooms joined by a doorway (z 4 .. 6 in the wall at x = 10) holding door 7 */
static LrgNavLevel *
door_level (LrgNavDoorState state)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    /* a door modelled as a slab 5 cm thick (thinner than a cell) */
    const gfloat door_min[3] = { 9.975f, 0.0f, 4.0f };
    const gfloat door_max[3] = { 10.025f, 2.5f, 6.0f };
    LrgNavLevel *level = lrg_nav_level_new ();

    floor_rect (tris, 0, 0, 20, 10);
    box (tris, 9.8f, 0.0f, -0.5f, 10.2f, 3.0f, 4.0f);
    box (tris, 9.8f, 0.0f, 6.0f, 10.2f, 3.0f, 10.5f);
    /* the lintel above the door */
    box (tris, 9.8f, 2.5f, 4.0f, 10.2f, 3.0f, 6.0f);
    lrg_nav_level_add_triangles (level, (const gfloat *) tris->data, tris->len);
    g_assert_true (lrg_nav_level_add_door (level, 7, door_min, door_max, state));
    return level;
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */

static void
test_round_a_wall (void)
{
    g_autoptr(LrgNavLevel) level = wall_level ();
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *p = NULL;
    const gfloat a[3] = { 5.0f, 0.0f, 2.0f };
    const gfloat b[3] = { 15.0f, 0.0f, 2.0f };
    const gfloat eye_a[3] = { 5.0f, 1.5f, 2.0f };
    const gfloat eye_b[3] = { 15.0f, 1.5f, 2.0f };
    const gfloat eye_c[3] = { 5.0f, 1.5f, 9.2f };
    const gfloat eye_d[3] = { 15.0f, 1.5f, 9.2f };
    const gfloat c[3] = { 5.0f, 0.0f, 9.0f };
    const gfloat d[3] = { 15.0f, 0.0f, 9.0f };
    LrgCollisionHit hit;
    gfloat fraction;
    guint n;
    guint i;
    gboolean beyond_gap;

    g_assert_true (lrg_nav_level_is_baked (level));
    g_assert_cmpuint (lrg_nav_level_get_triangle_count (level), ==, 14);
    p = lrg_nav_level_find_path (level, a, b, extents, LRG_NAV_DOOR_STATE_OPEN, 0, &n, &error);
    g_assert_no_error (error);
    g_assert_nonnull (p);
    /* round the wall's end at z = 8: half again the straight 10 */
    g_assert_cmpfloat (path_length (p, n), >, 15.0f);
    beyond_gap = FALSE;
    for (i = 0; i < n; i += 3)
        if (p[i + 2] > 8.0f)
            beyond_gap = TRUE;
    g_assert_true (beyond_gap);
    /* the straight walk and the straight look meet the wall */
    g_assert_false (lrg_nav_level_can_walk (level, a, b, extents, &fraction));
    g_assert_cmpfloat (fraction, <, 0.5f);
    g_assert_false (lrg_nav_level_has_line_of_sight (level, eye_a, eye_b, &hit));
    g_assert_cmpuint (hit.instance_id, ==, 0);
    g_assert_cmpfloat (fabsf (hit.position[0] - 9.8f), <, 0.01f);
    /* through the gap: walkable and seen */
    g_assert_true (lrg_nav_level_can_walk (level, c, d, extents, &fraction));
    g_assert_cmpfloat (fraction, ==, 1.0f);
    g_assert_true (lrg_nav_level_has_line_of_sight (level, eye_c, eye_d, NULL));
    /* a goal 3 m under the floor in the same X and Z is no straight walk */
    {
        const gfloat below[3] = { 5.0f, -3.0f, 9.0f };

        g_assert_false (lrg_nav_level_can_walk (level, c, below, extents, &fraction));
    }
    /* a point on itself is always seen */
    g_assert_true (lrg_nav_level_has_line_of_sight (level, eye_a, eye_a, NULL));
}

static void
test_doors (void)
{
    g_autoptr(LrgNavLevel) level = door_level (LRG_NAV_DOOR_STATE_CLOSED);
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *p = NULL;
    g_autofree guint32 *tags = NULL;
    const gfloat a[3] = { 5.0f, 0.0f, 5.0f };
    const gfloat b[3] = { 15.0f, 0.0f, 5.0f };
    const gfloat eye_a[3] = { 5.0f, 1.5f, 5.0f };
    const gfloat eye_b[3] = { 15.0f, 1.5f, 5.0f };
    LrgCollisionHit hit;
    guint n;
    guint n_doors;

    g_assert_cmpuint (lrg_nav_level_get_door_count (level), ==, 1);
    g_assert_true (lrg_nav_level_bake (level, &error));
    g_assert_no_error (error);

    /* closed: only a door opener walks through; nobody sees through */
    p = lrg_nav_level_find_path (level, a, b, extents, LRG_NAV_DOOR_STATE_OPEN, 0, &n, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_null (p);
    g_assert_cmpuint (n, ==, 0);
    g_clear_error (&error);
    p = lrg_nav_level_find_path (level, a, b, extents, LRG_NAV_DOOR_STATE_CLOSED, 0, &n, &error);
    g_assert_no_error (error);
    g_assert_nonnull (p);
    g_assert_cmpfloat (path_length (p, n), <, 11.0f);
    tags = lrg_nav_level_find_doors_on_path (level, p, n, &n_doors);
    g_assert_cmpuint (n_doors, ==, 1);
    g_assert_cmpuint (tags[0], ==, 7);
    g_clear_pointer (&p, g_free);
    g_assert_false (lrg_nav_level_can_walk (level, a, b, extents, NULL));
    g_assert_false (lrg_nav_level_has_line_of_sight (level, eye_a, eye_b, &hit));
    g_assert_cmpuint (hit.instance_id, ==, 7);

    /* open: everybody walks and sees through, no rebake */
    g_assert_true (lrg_nav_level_set_door_state (level, 7, LRG_NAV_DOOR_STATE_OPEN));
    g_assert_cmpint (lrg_nav_level_get_door_state (level, 7), ==, LRG_NAV_DOOR_STATE_OPEN);
    p = lrg_nav_level_find_path (level, a, b, extents, LRG_NAV_DOOR_STATE_OPEN, 0, &n, &error);
    g_assert_no_error (error);
    g_assert_nonnull (p);
    g_clear_pointer (&p, g_free);
    g_assert_true (lrg_nav_level_can_walk (level, a, b, extents, NULL));
    g_assert_true (lrg_nav_level_has_line_of_sight (level, eye_a, eye_b, NULL));

    /* locked: a door opener stops, an agent that unlocks passes */
    g_assert_true (lrg_nav_level_set_door_state (level, 7, LRG_NAV_DOOR_STATE_LOCKED));
    p = lrg_nav_level_find_path (level, a, b, extents, LRG_NAV_DOOR_STATE_CLOSED, 0, &n, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_null (p);
    g_clear_error (&error);
    p = lrg_nav_level_find_path (level, a, b, extents, LRG_NAV_DOOR_STATE_LOCKED, 0, &n, &error);
    g_assert_no_error (error);
    g_assert_nonnull (p);
    g_clear_pointer (&p, g_free);
    g_assert_false (lrg_nav_level_can_walk (level, a, b, extents, NULL));
    g_assert_false (lrg_nav_level_has_line_of_sight (level, eye_a, eye_b, NULL));
    /* sealed: unlocking is not enough */
    g_assert_true (lrg_nav_level_set_door_state (level, 7, LRG_NAV_DOOR_STATE_SEALED));
    p = lrg_nav_level_find_path (level, a, b, extents, LRG_NAV_DOOR_STATE_LOCKED, 0, &n, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_null (p);
    g_clear_error (&error);
    g_assert_true (lrg_nav_level_set_door_state (level, 7, LRG_NAV_DOOR_STATE_LOCKED));

    /* a door that is not there */
    g_assert_false (lrg_nav_level_set_door_state (level, 8, LRG_NAV_DOOR_STATE_OPEN));
    g_assert_cmpint (lrg_nav_level_get_door_state (level, 8), ==, LRG_NAV_DOOR_STATE_OPEN);

    /* a rebake keeps the states */
    g_assert_true (lrg_nav_level_bake (level, &error));
    g_assert_no_error (error);
    g_assert_false (lrg_nav_level_has_line_of_sight (level, eye_a, eye_b, NULL));
    p = lrg_nav_level_find_path (level, a, b, extents, LRG_NAV_DOOR_STATE_CLOSED, 0, &n, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error (&error);
}

static void
on_baked (GObject      *source,
          GAsyncResult *result,
          gpointer      user_data)
{
    gboolean *done = (gboolean *) user_data;
    g_autoptr(GError) error = NULL;

    g_assert_true (lrg_nav_level_bake_finish (LRG_NAV_LEVEL (source), result, &error));
    g_assert_no_error (error);
    *done = TRUE;
}

static void
test_async (void)
{
    g_autoptr(LrgNavLevel) level = door_level (LRG_NAV_DOOR_STATE_CLOSED);
    g_autoptr(GError) error = NULL;
    const gfloat eye_a[3] = { 5.0f, 1.5f, 5.0f };
    const gfloat eye_b[3] = { 15.0f, 1.5f, 5.0f };
    const gfloat a[3] = { 5.0f, 0.0f, 5.0f };
    const gfloat b[3] = { 15.0f, 0.0f, 5.0f };
    g_autofree gfloat *p = NULL;
    gboolean done = FALSE;
    guint n;

    lrg_nav_level_bake_async (level, NULL, on_baked, &done);
    /* while it runs: unbaked answers, a second bake is refused, states are kept */
    g_assert_false (lrg_nav_level_is_baked (level));
    g_assert_true (lrg_nav_level_has_line_of_sight (level, eye_a, eye_b, NULL));
    g_assert_false (lrg_nav_level_bake (level, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PENDING);
    g_clear_error (&error);
    g_assert_true (lrg_nav_level_set_door_state (level, 7, LRG_NAV_DOOR_STATE_LOCKED));
    while (!done)
        g_main_context_iteration (NULL, TRUE);
    g_assert_true (lrg_nav_level_is_baked (level));
    /* the lock set during the bake holds */
    g_assert_false (lrg_nav_level_has_line_of_sight (level, eye_a, eye_b, NULL));
    p = lrg_nav_level_find_path (level, a, b, extents, LRG_NAV_DOOR_STATE_CLOSED, 0, &n, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_failures (void)
{
    g_autoptr(LrgNavLevel) level = lrg_nav_level_new ();
    g_autoptr(GError) error = NULL;
    const gfloat lo[3] = { 0.0f, 0.0f, 0.0f };
    const gfloat hi[3] = { 1.0f, 2.0f, 1.0f };
    const gfloat nan_point[3] = { NAN, 0.0f, 0.0f };
    const gfloat bad_tri[9] = { 0, 0, 0, NAN, 0, 0, 0, 0, 1 };
    const gfloat a[3] = { 0.5f, 0.0f, 0.5f };
    g_autofree gfloat *p = NULL;
    guint n = 9;

    /* unbaked: no path, no walk, everything in sight */
    g_assert_false (lrg_nav_level_is_baked (level));
    p = lrg_nav_level_find_path (level, a, a, extents, LRG_NAV_DOOR_STATE_CLOSED, 0, &n, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_cmpuint (n, ==, 0);
    g_clear_error (&error);
    g_assert_false (lrg_nav_level_can_walk (level, a, a, extents, NULL));
    g_assert_true (lrg_nav_level_has_line_of_sight (level, lo, hi, NULL));
    /* bad doors and triangles */
    g_assert_false (lrg_nav_level_add_door (level, 0, lo, hi, LRG_NAV_DOOR_STATE_OPEN));
    g_assert_false (lrg_nav_level_add_door (level, 3, hi, lo, LRG_NAV_DOOR_STATE_OPEN));
    g_assert_false (lrg_nav_level_add_door (level, 3, nan_point, hi, LRG_NAV_DOOR_STATE_OPEN));
    g_assert_cmpuint (lrg_nav_level_add_triangles (level, bad_tri, 9), ==, 0);
    /* nothing to bake */
    g_assert_false (lrg_nav_level_bake (level, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_false (lrg_nav_level_is_baked (level));
}

/* a lower floor (x 0 .. 10, y 0) below a ledge (a solid block x 10 .. 20,
 * top at y 4) reached only by a ladder at x = 10 */
static LrgNavLevel *
ladder_level (guint *out_ladder)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(GError) error = NULL;
    const gfloat foot[3] = { 9.6f, 0.0f, 5.0f };
    const gfloat top[3] = { 10.4f, 4.0f, 5.0f };
    LrgNavLevel *level = lrg_nav_level_new ();

    floor_rect (tris, 0, 0, 10, 10);
    box (tris, 10.0f, 0.0f, 0.0f, 20.0f, 4.0f, 10.0f);
    lrg_nav_level_add_triangles (level, (const gfloat *) tris->data, tris->len);
    *out_ladder = lrg_nav_level_add_ladder (level, foot, top);
    g_assert_true (lrg_nav_level_bake (level, &error));
    g_assert_no_error (error);
    return level;
}

static void
test_ladders (void)
{
    g_autoptr(LrgNavLevel) level = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *p = NULL;
    const gfloat a[3] = { 2.0f, 0.0f, 5.0f };
    const gfloat b[3] = { 15.0f, 4.0f, 5.0f };
    gboolean climbed;
    guint ladder;
    guint n;
    guint i;

    level = ladder_level (&ladder);
    g_assert_cmpuint (ladder, ==, 0);
    g_assert_cmpuint (lrg_nav_level_get_ladder_count (level), ==, 1);
    g_assert_true (lrg_nav_level_get_ladder_connected (level, 0));
    g_assert_false (lrg_nav_level_get_ladder_connected (level, 1));

    /* a climber goes up the ladder: one leg of the path is the ladder's */
    p = lrg_nav_level_find_path_full (level, a, b, extents, LRG_NAV_DOOR_STATE_OPEN, TRUE, FALSE, 0,
                                      &n, &error);
    g_assert_no_error (error);
    g_assert_nonnull (p);
    climbed = FALSE;
    for (i = 3; i + 3 <= n; i += 3)
        climbed = climbed || lrg_nav_level_is_ladder_leg (level, p + i - 3, p + i);
    g_assert_true (climbed);
    g_assert_cmpfloat (fabsf (p[n - 2] - 4.0f), <, 0.3f);
    g_clear_pointer (&p, g_free);
    /* and down again (the link goes both ways); the plain query climbs */
    p = lrg_nav_level_find_path (level, b, a, extents, LRG_NAV_DOOR_STATE_OPEN, 0, &n, &error);
    g_assert_no_error (error);
    g_assert_nonnull (p);
    g_clear_pointer (&p, g_free);

    /* an agent that does not climb has no way up */
    p = lrg_nav_level_find_path_full (level, a, b, extents, LRG_NAV_DOOR_STATE_SEALED, FALSE, TRUE, 0,
                                      &n, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_null (p);
    g_clear_error (&error);
    /* a ladder is never a straight walk */
    g_assert_false (lrg_nav_level_can_walk (level, a, b, extents, NULL));
    g_assert_false (lrg_nav_level_is_ladder_leg (level, a, b));
}

/* a pit (x 10 .. 14, floor at y -3) between two banks (y 0) full of water
 * up to y -0.2 */
static LrgNavLevel *
pool_level (gboolean with_water)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(GError) error = NULL;
    const gfloat pit_a[3] = { 10.0f, -3.0f, 0.0f };
    const gfloat pit_b[3] = { 14.0f, -3.0f, 0.0f };
    const gfloat pit_c[3] = { 14.0f, -3.0f, 10.0f };
    const gfloat pit_d[3] = { 10.0f, -3.0f, 10.0f };
    const gfloat water_min[3] = { 10.0f, -3.5f, 0.0f };
    const gfloat water_max[3] = { 14.0f, -0.2f, 10.0f };
    LrgNavLevel *level = lrg_nav_level_new ();

    box (tris, 0.0f, -3.0f, 0.0f, 10.0f, 0.0f, 10.0f);
    box (tris, 14.0f, -3.0f, 0.0f, 24.0f, 0.0f, 10.0f);
    quad (tris, pit_a, pit_b, pit_c, pit_d);
    lrg_nav_level_add_triangles (level, (const gfloat *) tris->data, tris->len);
    if (with_water)
        g_assert_true (lrg_nav_level_add_water (level, water_min, water_max));
    g_assert_true (lrg_nav_level_bake (level, &error));
    g_assert_no_error (error);
    return level;
}

static void
test_water (void)
{
    g_autoptr(LrgNavLevel) level = NULL;
    g_autoptr(LrgNavLevel) dry = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *p = NULL;
    const gfloat a[3] = { 5.0f, 0.0f, 5.0f };
    const gfloat b[3] = { 19.0f, 0.0f, 5.0f };
    const gfloat swimming[3] = { 12.0f, -1.0f, 5.0f };
    const gfloat eye_a[3] = { 5.0f, 1.5f, 5.0f };
    const gfloat eye_b[3] = { 19.0f, 1.5f, 5.0f };
    gfloat surface = 0.0f;
    guint n;

    /* without water the pit cannot be crossed */
    dry = pool_level (FALSE);
    p = lrg_nav_level_find_path (dry, a, b, extents, LRG_NAV_DOOR_STATE_OPEN, 0, &n, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error (&error);

    level = pool_level (TRUE);
    g_assert_cmpuint (lrg_nav_level_get_water_count (level), ==, 1);
    /* a swimmer crosses at the surface and climbs out on the far bank */
    p = lrg_nav_level_find_path_full (level, a, b, extents, LRG_NAV_DOOR_STATE_OPEN, FALSE, TRUE, 0,
                                      &n, &error);
    g_assert_no_error (error);
    g_assert_nonnull (p);
    g_assert_cmpfloat (path_length (p, n), <, 16.0f);
    g_clear_pointer (&p, g_free);
    /* one that does not swim stays on its bank */
    p = lrg_nav_level_find_path_full (level, a, b, extents, LRG_NAV_DOOR_STATE_SEALED, TRUE, FALSE, 0,
                                      &n, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_null (p);
    g_clear_error (&error);

    /* where a walker swims */
    g_assert_true (lrg_nav_level_in_water (level, swimming, &surface));
    g_assert_cmpfloat (surface, ==, -0.2f);
    g_assert_false (lrg_nav_level_in_water (level, a, NULL));
    /* the surface hides nothing */
    g_assert_true (lrg_nav_level_has_line_of_sight (level, eye_a, eye_b, NULL));
}

static void
test_ladder_water_failures (void)
{
    g_autoptr(LrgNavLevel) level = lrg_nav_level_new ();
    const gfloat lo[3] = { 0.0f, 0.0f, 0.0f };
    const gfloat hi[3] = { 1.0f, 2.0f, 1.0f };
    const gfloat flat[3] = { 1.0f, 0.0f, 1.0f };
    const gfloat nan_point[3] = { NAN, 0.0f, 0.0f };

    /* a ladder must go up and be finite */
    g_assert_cmpuint (lrg_nav_level_add_ladder (level, hi, lo), ==, G_MAXUINT);
    g_assert_cmpuint (lrg_nav_level_add_ladder (level, lo, flat), ==, G_MAXUINT);
    g_assert_cmpuint (lrg_nav_level_add_ladder (level, nan_point, hi), ==, G_MAXUINT);
    g_assert_cmpuint (lrg_nav_level_get_ladder_count (level), ==, 0);
    /* water must be a box */
    g_assert_false (lrg_nav_level_add_water (level, hi, lo));
    g_assert_false (lrg_nav_level_add_water (level, lo, flat));
    g_assert_false (lrg_nav_level_add_water (level, nan_point, hi));
    g_assert_cmpuint (lrg_nav_level_get_water_count (level), ==, 0);
    /* the reserved tags are no doors */
    g_assert_false (lrg_nav_level_add_door (level, LRG_NAV_LEVEL_LADDER_TAG, lo, hi,
                                            LRG_NAV_DOOR_STATE_OPEN));
    g_assert_false (lrg_nav_level_add_door (level, LRG_NAV_LEVEL_WATER_TAG, lo, hi,
                                            LRG_NAV_DOOR_STATE_OPEN));
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/nav-level/round-a-wall", test_round_a_wall);
    g_test_add_func ("/nav-level/doors", test_doors);
    g_test_add_func ("/nav-level/async", test_async);
    g_test_add_func ("/nav-level/failures", test_failures);
    g_test_add_func ("/nav-level/ladders", test_ladders);
    g_test_add_func ("/nav-level/water", test_water);
    g_test_add_func ("/nav-level/ladder-water-failures", test_ladder_water_failures);

    return g_test_run ();
}
