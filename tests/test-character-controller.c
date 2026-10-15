/* test-character-controller.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgCharacterController on synthetic geometry: floors,
 * walls and corners, stairs (climbable and too high), walkable and
 * steep ramps, ledges with and without ground snapping, falling without
 * tunnelling, ceilings, moving platforms (instances), terrain height
 * functions, depenetration and crouch clearance.
 */

#include <glib.h>
#include <glib-object.h>
#include <math.h>
#include <string.h>

#include "physics/lrg-collision-mesh.h"
#include "physics/lrg-character-controller.h"

#define RADIUS (0.35f)
#define HEIGHT (1.8f)
#define STEP   (0.4f)
#define EPS    (2e-3f)

/* ========================================================================== */
/*                               Geometry helpers                             */
/* ========================================================================== */

static void
push_tri (GArray *v,
          gfloat  ax, gfloat ay, gfloat az,
          gfloat  bx, gfloat by, gfloat bz,
          gfloat  cx, gfloat cy, gfloat cz)
{
    gfloat t[9];

    t[0] = ax; t[1] = ay; t[2] = az;
    t[3] = bx; t[4] = by; t[5] = bz;
    t[6] = cx; t[7] = cy; t[8] = cz;
    g_array_append_vals (v, t, 9);
}

static void
push_floor (GArray *v,
            gfloat  x0,
            gfloat  z0,
            gfloat  x1,
            gfloat  z1,
            gfloat  h)
{
    push_tri (v, x0, h, z0, x1, h, z0, x1, h, z1);
    push_tri (v, x0, h, z0, x1, h, z1, x0, h, z1);
}

static void
push_box (GArray *v,
          gfloat  x0, gfloat y0, gfloat z0,
          gfloat  x1, gfloat y1, gfloat z1)
{
    push_tri (v, x0, y0, z0, x1, y0, z0, x1, y0, z1);
    push_tri (v, x0, y0, z0, x1, y0, z1, x0, y0, z1);
    push_tri (v, x0, y1, z0, x1, y1, z1, x1, y1, z0);
    push_tri (v, x0, y1, z0, x0, y1, z1, x1, y1, z1);
    push_tri (v, x0, y0, z0, x0, y1, z1, x0, y1, z0);
    push_tri (v, x0, y0, z0, x0, y0, z1, x0, y1, z1);
    push_tri (v, x1, y0, z0, x1, y1, z0, x1, y1, z1);
    push_tri (v, x1, y0, z0, x1, y1, z1, x1, y0, z1);
    push_tri (v, x0, y0, z0, x1, y1, z0, x1, y0, z0);
    push_tri (v, x0, y0, z0, x0, y1, z0, x1, y1, z0);
    push_tri (v, x0, y0, z1, x1, y0, z1, x1, y1, z1);
    push_tri (v, x0, y0, z1, x1, y1, z1, x0, y1, z1);
}

/* A ramp rising along +X from (x0, 0) by angle degrees, to x1. */
static void
push_ramp (GArray *v,
           gfloat  x0,
           gfloat  x1,
           gfloat  z0,
           gfloat  z1,
           gfloat  degrees)
{
    gfloat top;

    top = (x1 - x0) * tanf (degrees * (gfloat) G_PI / 180.0f);
    push_tri (v, x0, 0, z0, x1, top, z0, x1, top, z1);
    push_tri (v, x0, 0, z0, x1, top, z1, x0, 0, z1);
}

static LrgCollisionMesh *
mesh_from (GArray *v)
{
    LrgCollisionMesh *mesh;
    g_autoptr(GError) error = NULL;

    mesh = lrg_collision_mesh_new ();
    g_assert_true (lrg_collision_mesh_set_triangles (mesh, (const gfloat *) v->data, v->len, NULL, &error));
    g_assert_no_error (error);
    return mesh;
}

static LrgCharacterController *
controller_for (LrgCollisionMesh *mesh)
{
    LrgCharacterController *cc;

    cc = lrg_character_controller_new (RADIUS, HEIGHT);
    lrg_character_controller_set_step_height (cc, STEP);
    lrg_character_controller_set_max_slope (cc, 45.0f);
    lrg_character_controller_set_world (cc, mesh);
    return cc;
}

/* Walks n ticks of dt with a horizontal velocity and gravity. */
static void
walk (LrgCharacterController *cc,
      gfloat                 *feet,
      gfloat                  vx,
      gfloat                  vz,
      guint                   ticks,
      LrgCharacterMove       *last,
      guint                  *n_stepped)
{
    const gfloat dt = 1.0f / 60.0f;
    gfloat vy;
    guint i;

    vy = 0.0f;
    for (i = 0; i < ticks; i++)
    {
        gfloat m[3];

        vy -= 20.0f * dt;
        m[0] = vx * dt;
        m[1] = vy * dt;
        m[2] = vz * dt;
        g_assert_true (lrg_character_controller_move (cc, feet, m, TRUE, last));
        memcpy (feet, last->position, sizeof (gfloat) * 3);
        if (last->grounded)
            vy = 0.0f;
        if (last->stepped && n_stepped != NULL)
            (*n_stepped)++;
        if (g_getenv ("CC_TRACE") != NULL)
            g_printerr ("%u: %.4f %.4f %.4f g=%d w=%d s=%d\n", i, feet[0], feet[1], feet[2], last->grounded, last->hit_wall, last->stepped);
    }
}

/* ========================================================================== */
/*                                    Tests                                   */
/* ========================================================================== */

static void
test_properties (void)
{
    g_autoptr(LrgCharacterController) cc = lrg_character_controller_new (0.5f, 0.2f);

    /* height never below the sphere */
    g_assert_cmpfloat_with_epsilon (lrg_character_controller_get_height (cc), 1.0f, 1e-6f);
    lrg_character_controller_set_radius (cc, -1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_character_controller_get_radius (cc), 0.5f, 1e-6f);
    lrg_character_controller_set_max_slope (cc, 120.0f);
    g_assert_cmpfloat_with_epsilon (lrg_character_controller_get_max_slope (cc), 89.9f, 1e-4f);
    lrg_character_controller_set_skin_width (cc, 5.0f);
    g_assert_cmpfloat_with_epsilon (lrg_character_controller_get_skin_width (cc), 0.25f, 1e-6f);
    lrg_character_controller_set_step_height (cc, -1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_character_controller_get_step_height (cc),
                                    LRG_CHARACTER_CONTROLLER_DEFAULT_STEP_HEIGHT, 1e-6f);
    g_assert_null (lrg_character_controller_get_world (cc));
}

static void
test_free_motion (void)
{
    g_autoptr(LrgCharacterController) cc = lrg_character_controller_new (RADIUS, HEIGHT);
    gfloat feet[3] = { 1, 2, 3 };
    gfloat m[3] = { 0.5f, -1.0f, 0.25f };
    gfloat bad[3] = { NAN, 0, 0 };
    LrgCharacterMove mv;

    g_assert_true (lrg_character_controller_move (cc, feet, m, TRUE, &mv));
    g_assert_cmpfloat_with_epsilon (mv.position[0], 1.5f, 1e-6f);
    g_assert_cmpfloat_with_epsilon (mv.position[1], 1.0f, 1e-6f);
    g_assert_cmpfloat_with_epsilon (mv.position[2], 3.25f, 1e-6f);
    g_assert_false (mv.grounded);

    g_assert_false (lrg_character_controller_move (cc, feet, bad, TRUE, &mv));
    g_assert_cmpfloat (mv.position[0], ==, 1.0f);
}

static void
test_floor (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 0, 0.5f, 0 };
    LrgCharacterMove mv;

    /* a floor of many triangles: seams must not snag */
    {
        gint i;
        gint j;

        for (i = -10; i < 10; i++)
            for (j = -10; j < 10; j++)
                push_floor (v, (gfloat) i, (gfloat) j, (gfloat) i + 1, (gfloat) j + 1, 0);
    }
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    /* falls onto the floor and walks across seams */
    walk (cc, feet, 0, 0, 60, &mv, NULL);
    g_assert_true (mv.grounded);
    g_assert_cmpfloat_with_epsilon (feet[1], 0.0f, 0.02f);
    g_assert_cmpfloat_with_epsilon (mv.ground_normal[1], 1.0f, EPS);

    walk (cc, feet, 3.0f, 1.0f, 120, &mv, NULL);
    g_assert_true (mv.grounded);
    g_assert_false (mv.hit_wall);
    g_assert_cmpfloat_with_epsilon (feet[0], 6.0f, 0.01f);
    g_assert_cmpfloat_with_epsilon (feet[2], 2.0f, 0.01f);
    g_assert_cmpfloat_with_epsilon (feet[1], 0.0f, 0.02f);
}

static void
test_wall_slide (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 0, 0, 0 };
    LrgCharacterMove mv;

    push_floor (v, -10, -10, 10, 10, 0);
    push_box (v, 2, 0, -10, 3, 3, 10);   /* wall at x = 2 */
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    /* straight into the wall: stops at radius + skin */
    walk (cc, feet, 4.0f, 0.0f, 60, &mv, NULL);
    g_assert_true (mv.hit_wall);
    g_assert_cmpfloat (feet[0], <=, 2.0f - RADIUS);
    g_assert_cmpfloat (feet[0], >, 2.0f - RADIUS - 0.05f);
    g_assert_cmpfloat_with_epsilon (feet[1], 0.0f, 0.02f);

    /* diagonally: slides along it */
    walk (cc, feet, 3.0f, 3.0f, 60, &mv, NULL);
    g_assert_cmpfloat (feet[0], <=, 2.0f - RADIUS);
    g_assert_cmpfloat_with_epsilon (feet[2], 3.0f, 0.05f);
    g_assert_true (mv.grounded);
}

static void
test_corner (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 0, 0, 0 };
    LrgCharacterMove mv;

    push_floor (v, -10, -10, 10, 10, 0);
    push_box (v, 2, 0, -10, 3, 3, 10);   /* x = 2 */
    push_box (v, -10, 0, 2, 10, 3, 3);   /* z = 2 */
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    walk (cc, feet, 3.0f, 3.0f, 120, &mv, NULL);
    g_assert_cmpfloat (feet[0], <=, 2.0f - RADIUS);
    g_assert_cmpfloat (feet[2], <=, 2.0f - RADIUS);
    g_assert_cmpfloat (feet[0], >, 2.0f - RADIUS - 0.05f);
    g_assert_cmpfloat (feet[2], >, 2.0f - RADIUS - 0.05f);
}

/* Eight 0.3 high, 0.5 deep steps up along +X. */
static void
push_stairs (GArray *v,
             gfloat  rise)
{
    guint i;

    push_floor (v, -10, -10, 20, 10, 0);
    for (i = 0; i < 8; i++)
    {
        gfloat x0 = 1.0f + 0.5f * (gfloat) i;

        push_box (v, x0, 0, -2, 20, rise * (gfloat) (i + 1), 2);
    }
}

static void
test_stairs_up (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 0, 0, 0 };
    LrgCharacterMove mv;
    guint stepped;

    push_stairs (v, 0.3f);
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    stepped = 0;
    walk (cc, feet, 3.0f, 0.0f, 120, &mv, &stepped);
    g_assert_cmpuint (stepped, >=, 8);
    g_assert_true (mv.grounded);
    g_assert_cmpfloat_with_epsilon (feet[1], 2.4f, 0.02f);
    g_assert_cmpfloat (feet[0], >, 5.0f);

    /* and back down, snapping from tread to tread */
    walk (cc, feet, -3.0f, 0.0f, 120, &mv, NULL);
    g_assert_true (mv.grounded);
    g_assert_cmpfloat_with_epsilon (feet[1], 0.0f, 0.02f);
    g_assert_cmpfloat (feet[0], <, 0.5f);
}

static void
test_step_too_high (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 0, 0, 0 };
    LrgCharacterMove mv;
    guint stepped;

    push_stairs (v, 0.6f);
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    stepped = 0;
    walk (cc, feet, 3.0f, 0.0f, 120, &mv, &stepped);
    g_assert_cmpuint (stepped, ==, 0);
    g_assert_true (mv.hit_wall);
    g_assert_cmpfloat (feet[0], <, 1.0f - RADIUS + 0.01f);
    g_assert_cmpfloat_with_epsilon (feet[1], 0.0f, 0.02f);
}

static void
test_ramp_walkable (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { -1, 0, 0 };
    LrgCharacterMove mv;

    push_floor (v, -10, -10, 0, 10, 0);
    push_ramp (v, 0, 10, -10, 10, 30.0f);
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    walk (cc, feet, 3.0f, 0.0f, 90, &mv, NULL);
    g_assert_true (mv.grounded);
    g_assert_cmpfloat (feet[0], >, 2.0f);
    /* resting on the ramp: the sphere touches the plane, so the feet sit
     * r / cos (30) - r above the plane at their own x (plus the skin) */
    g_assert_cmpfloat_with_epsilon (feet[1],
                                    feet[0] * tanf ((gfloat) G_PI / 6.0f) +
                                    RADIUS / cosf ((gfloat) G_PI / 6.0f) - RADIUS, 0.03f);
    g_assert_cmpfloat_with_epsilon (mv.ground_normal[1], cosf ((gfloat) G_PI / 6.0f), EPS);

    /* standing still on it does not slide */
    {
        gfloat x;

        x = feet[0];
        walk (cc, feet, 0.0f, 0.0f, 60, &mv, NULL);
        g_assert_cmpfloat_with_epsilon (feet[0], x, 0.01f);
    }

    /* walking down keeps contact */
    walk (cc, feet, -3.0f, 0.0f, 30, &mv, NULL);
    g_assert_true (mv.grounded);
}

static void
test_ramp_too_steep (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { -1, 0, 0 };
    gfloat on_slope[3];
    LrgCharacterMove mv;

    push_floor (v, -10, -10, 10, 10, 0);
    push_ramp (v, 0, 4, -10, 10, 60.0f);
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    /* blocked where the bottom sphere meets the slope (x = 1.5 r / sin 60) */
    walk (cc, feet, 3.0f, 0.0f, 120, &mv, NULL);
    g_assert_true (mv.hit_wall);
    g_assert_cmpfloat (feet[1], <, 0.05f);
    g_assert_cmpfloat (feet[0], <, 0.62f);

    /* dropped onto the slope it slides down and is never grounded on it */
    on_slope[0] = 2.0f;
    on_slope[1] = 2.0f * tanf ((gfloat) G_PI / 3.0f) + 0.5f;
    on_slope[2] = 0.0f;
    walk (cc, on_slope, 0.0f, 0.0f, 10, &mv, NULL);
    g_assert_false (mv.grounded);
    walk (cc, on_slope, 0.0f, 0.0f, 240, &mv, NULL);
    g_assert_true (mv.grounded);
    g_assert_cmpfloat (on_slope[0], <, 0.62f);
    g_assert_cmpfloat_with_epsilon (on_slope[1], 0.0f, 0.05f);
}

static void
test_ledge_snap (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 0.5f, 0.2f, 0 };
    gfloat m[3] = { 1.0f, 0.0f, 0.0f };
    LrgCharacterMove mv;

    /* upper floor x < 1 at 0.2, lower floor x >= 1 at 0 */
    push_box (v, -5, -1, -5, 1, 0.2f, 5);
    push_floor (v, 1, -5, 10, 5, 0);
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    /* without snapping the capsule leaves the ground */
    g_assert_true (lrg_character_controller_move (cc, feet, m, FALSE, &mv));
    g_assert_false (mv.grounded);
    g_assert_cmpfloat_with_epsilon (mv.position[1], 0.2f, 0.02f);

    /* with snapping it follows the ground down */
    g_assert_true (lrg_character_controller_move (cc, feet, m, TRUE, &mv));
    g_assert_true (mv.grounded);
    g_assert_cmpfloat_with_epsilon (mv.position[1], 0.0f, 0.02f);
    g_assert_cmpfloat_with_epsilon (mv.position[0], 1.5f, 0.02f);
}

static void
test_fall_no_tunnelling (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 0, 100, 0 };
    gfloat m[3] = { 0, -500, 0 };
    LrgCharacterMove mv;

    /* a single-sided-looking thin floor */
    push_floor (v, -5, -5, 5, 5, 0);
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    g_assert_true (lrg_character_controller_move (cc, feet, m, FALSE, &mv));
    g_assert_true (mv.grounded);
    g_assert_cmpfloat_with_epsilon (mv.position[1], 0.0f, 0.02f);
    g_assert_cmpfloat (mv.position[1], >=, 0.0f);
}

static void
test_ceiling (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 0, 0, 0 };
    gfloat m[3] = { 0, 2, 0 };
    LrgCharacterMove mv;

    push_floor (v, -5, -5, 5, 5, 0);
    push_floor (v, -5, -5, 5, 5, 2.5f);
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    g_assert_true (lrg_character_controller_move (cc, feet, m, FALSE, &mv));
    g_assert_true (mv.hit_ceiling);
    g_assert_cmpfloat (mv.position[1] + HEIGHT, <=, 2.5f);
    g_assert_cmpfloat (mv.position[1] + HEIGHT, >, 2.45f);

    /* crouch clearance: 1.8 fits, 2.6 does not */
    g_assert_true (lrg_character_controller_can_stand (cc, feet, 1.8f));
    g_assert_false (lrg_character_controller_can_stand (cc, feet, 2.6f));
}

static void
test_platform_instance (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(GArray) pv = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) world = NULL;
    g_autoptr(LrgCollisionMesh) platform = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat m[16];
    gfloat feet[3] = { 5, 1.5f, 0 };
    LrgCharacterMove mv;
    guint inst;

    push_floor (v, -10, -10, 10, 10, 0);
    world = mesh_from (v);
    push_box (pv, -1, 0, -1, 1, 1, 1);
    platform = mesh_from (pv);
    memset (m, 0, sizeof (m));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    m[12] = 5.0f;
    inst = lrg_collision_mesh_add_instance (world, platform, m, 777);
    cc = controller_for (world);

    walk (cc, feet, 0, 0, 30, &mv, NULL);
    g_assert_true (mv.grounded);
    g_assert_cmpuint (mv.ground_instance_id, ==, 777);
    g_assert_cmpfloat_with_epsilon (feet[1], 1.0f, 0.02f);

    /* the platform rises into the capsule: depenetration lifts it */
    m[13] = 0.3f;
    g_assert_true (lrg_collision_mesh_set_instance_transform (world, inst, m));
    walk (cc, feet, 0, 0, 1, &mv, NULL);
    g_assert_cmpfloat_with_epsilon (feet[1], 1.3f, 0.03f);
    g_assert_cmpuint (mv.ground_instance_id, ==, 777);

    /* stepping off onto the floor */
    walk (cc, feet, 3.0f, 0, 60, &mv, NULL);
    g_assert_true (mv.grounded);
    g_assert_cmpuint (mv.ground_instance_id, ==, 0);
    g_assert_cmpfloat_with_epsilon (feet[1], 0.0f, 0.02f);
}

static gboolean
slope_height (gfloat    x,
              gfloat    z,
              gfloat   *height,
              gpointer  user_data)
{
    gint *calls;

    calls = (gint *) user_data;
    (*calls)++;
    *height = 0.1f * x;
    return TRUE;
}

static void
count_destroy (gpointer data)
{
    gint *calls;

    calls = (gint *) data;
    *calls = -1000;
}

static void
test_terrain (void)
{
    g_autoptr(LrgCharacterController) cc = lrg_character_controller_new (RADIUS, HEIGHT);
    gfloat feet[3] = { 0, 3, 0 };
    LrgCharacterMove mv;
    gint calls;

    calls = 0;
    lrg_character_controller_set_step_height (cc, STEP);
    lrg_character_controller_set_height_func (cc, slope_height, &calls, count_destroy);

    walk (cc, feet, 0, 0, 60, &mv, NULL);
    g_assert_true (mv.grounded);
    g_assert_cmpfloat_with_epsilon (feet[1], 0.0f, 1e-4f);
    g_assert_cmpfloat (mv.ground_normal[0], <, 0.0f);

    walk (cc, feet, 3.0f, 0, 60, &mv, NULL);
    g_assert_true (mv.grounded);
    g_assert_cmpfloat_with_epsilon (feet[0], 3.0f, 0.01f);
    g_assert_cmpfloat_with_epsilon (feet[1], 0.3f, 0.01f);
    g_assert_cmpint (calls, >, 0);

    /* replacing the function releases the old data */
    lrg_character_controller_set_height_func (cc, NULL, NULL, NULL);
    g_assert_cmpint (calls, ==, -1000);
}

static void
test_depenetration (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 1.8f, 0, 0 };
    gfloat none[3] = { 0, 0, 0 };
    LrgCharacterMove mv;

    push_floor (v, -10, -10, 10, 10, 0);
    push_box (v, 2, 0, -10, 3, 3, 10);
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    /* starts 0.15 inside the wall: pushed back out */
    g_assert_true (lrg_character_controller_move (cc, feet, none, TRUE, &mv));
    g_assert_cmpfloat (mv.position[0], <=, 2.0f - RADIUS + 1e-4f);
    g_assert_cmpfloat (mv.position[0], >, 2.0f - RADIUS - 0.02f);
    g_assert_true (mv.grounded);
}

/*
 * Random walks through a room full of boxes, stairs and ramps: the
 * capsule never ends inside geometry, never leaves the room and stays
 * finite.
 */
static void
test_random_walk (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCharacterController) cc = NULL;
    gfloat feet[3] = { 0, 0, 0 };
    guint32 state;
    guint i;

    push_floor (v, -10, -10, 10, 10, 0);
    push_box (v, -11, -1, -11, -10, 5, 11);
    push_box (v, 10, -1, -11, 11, 5, 11);
    push_box (v, -11, -1, -11, 11, 5, -10);
    push_box (v, -11, -1, 10, 11, 5, 11);
    push_box (v, 2, 0, 2, 3, 2, 3);
    push_box (v, -4, 0, 1, -3, 0.3f, 4);
    push_box (v, -4, 0, 1.5f, -3.5f, 0.6f, 4);
    push_box (v, -2, 0, -6, 4, 0.25f, -5);
    push_ramp (v, 5, 9, -9, -6, 25.0f);
    push_ramp (v, -9, -6, -9, -6, 55.0f);
    mesh = mesh_from (v);
    cc = controller_for (mesh);

    state = 12345u;
    for (i = 0; i < 40; i++)
    {
        LrgCharacterMove mv;
        gfloat vx;
        gfloat vz;

        state = state * 1103515245u + 12345u;
        vx = ((gfloat) ((state >> 8) % 1000u) / 500.0f - 1.0f) * 6.0f;
        state = state * 1103515245u + 12345u;
        vz = ((gfloat) ((state >> 8) % 1000u) / 500.0f - 1.0f) * 6.0f;
        walk (cc, feet, vx, vz, 30, &mv, NULL);

        g_assert_true (isfinite (feet[0]) && isfinite (feet[1]) && isfinite (feet[2]));
        g_assert_cmpfloat (fabsf (feet[0]), <, 10.0f);
        g_assert_cmpfloat (fabsf (feet[2]), <, 10.0f);
        g_assert_cmpfloat (feet[1], >, -0.05f);
        g_assert_false (lrg_collision_mesh_overlap_capsule (mesh, feet, RADIUS - 0.005f, HEIGHT));
    }
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/character-controller/properties", test_properties);
    g_test_add_func ("/character-controller/free-motion", test_free_motion);
    g_test_add_func ("/character-controller/floor", test_floor);
    g_test_add_func ("/character-controller/wall-slide", test_wall_slide);
    g_test_add_func ("/character-controller/corner", test_corner);
    g_test_add_func ("/character-controller/stairs-up", test_stairs_up);
    g_test_add_func ("/character-controller/step-too-high", test_step_too_high);
    g_test_add_func ("/character-controller/ramp-walkable", test_ramp_walkable);
    g_test_add_func ("/character-controller/ramp-too-steep", test_ramp_too_steep);
    g_test_add_func ("/character-controller/ledge-snap", test_ledge_snap);
    g_test_add_func ("/character-controller/fall-no-tunnelling", test_fall_no_tunnelling);
    g_test_add_func ("/character-controller/ceiling", test_ceiling);
    g_test_add_func ("/character-controller/platform-instance", test_platform_instance);
    g_test_add_func ("/character-controller/terrain", test_terrain);
    g_test_add_func ("/character-controller/depenetration", test_depenetration);
    g_test_add_func ("/character-controller/random-walk", test_random_walk);

    return g_test_run ();
}
