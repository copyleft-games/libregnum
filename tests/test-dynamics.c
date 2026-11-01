/* test-dynamics.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for LrgDynamicsWorld: box inertia, joint limits, sphere
 * contacts, the substep cap, sleep, freeze, eviction and bone-pose
 * composition. A collision-only solid is not a stand-in for any of these.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

static void
require (gboolean  ok,
         GError  **error)
{
    if (!ok)
        g_error ("%s", (error != NULL && *error != NULL) ? (*error)->message
                                                         : "dynamics call failed");
}

static guint
add_sphere (LrgDynamicsWorld    *world,
            LrgDynamicsBodyKind  kind,
            gfloat               mass,
            gfloat               radius,
            gfloat               x,
            gfloat               y,
            gfloat               z)
{
    g_autoptr(GError) error = NULL;
    gfloat position[3];
    guint id;

    id = G_MAXUINT;
    position[0] = x;
    position[1] = y;
    position[2] = z;
    require (lrg_dynamics_world_add_sphere (world, kind, mass, radius,
                                            position, NULL, &id, &error),
             &error);
    return id;
}

static void
rotate_y (const gfloat *q,
          gfloat       *out)
{
    gfloat up[3];
    gfloat tx;
    gfloat ty;
    gfloat tz;

    up[0] = 0.0f;
    up[1] = 1.0f;
    up[2] = 0.0f;
    tx = 2.0f * (q[1] * up[2] - q[2] * up[1]);
    ty = 2.0f * (q[2] * up[0] - q[0] * up[2]);
    tz = 2.0f * (q[0] * up[1] - q[1] * up[0]);
    out[0] = up[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = up[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = up[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static gfloat
axis_angle (LrgDynamicsWorld *world,
            guint             body)
{
    g_autoptr(GError) error = NULL;
    gfloat orientation[4];
    gfloat axis[3];
    gfloat cosine;

    require (lrg_dynamics_world_get_orientation (world, body, orientation, &error), &error);
    rotate_y (orientation, axis);
    cosine = axis[1];
    if (cosine > 1.0f)
        cosine = 1.0f;
    if (cosine < -1.0f)
        cosine = -1.0f;
    return acosf (cosine);
}

static void
test_box_inertia_and_torque (void)
{
    g_autoptr(LrgDynamicsWorld) world = NULL;
    g_autoptr(GError) error = NULL;
    gfloat inertia[3];
    gfloat angular[3];
    gfloat torque[3];
    gfloat origin[3];
    guint wide;
    guint cube;
    guint before;

    world = lrg_dynamics_world_new ();
    g_assert_nonnull (world);
    require (lrg_dynamics_world_set_step_limits (world, 0.1f, 1, &error), &error);
    origin[0] = 0.0f;
    origin[1] = 0.0f;
    origin[2] = 0.0f;
    before = 0;
    g_assert_false (lrg_dynamics_world_add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC,
                                                   -1.0f, 1.0f, origin, NULL,
                                                   &before, &error));
    g_assert_error (error, LRG_DYNAMICS_ERROR, LRG_DYNAMICS_ERROR_INVALID);
    g_assert_cmpuint (before, ==, 0);
    g_clear_error (&error);

    wide = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 12.0f, 0.2f, 0.0f, 0.0f, 0.0f);
    cube = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 12.0f, 0.2f, 10.0f, 0.0f, 0.0f);
    require (lrg_dynamics_world_set_box_inertia (world, wide, 1.0f, 2.0f, 3.0f, &error), &error);
    require (lrg_dynamics_world_set_box_inertia (world, cube, 1.0f, 1.0f, 1.0f, &error), &error);
    require (lrg_dynamics_world_get_inertia (world, wide, inertia, &error), &error);
    g_assert_cmpfloat_with_epsilon (inertia[0], 13.0f, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (inertia[1], 10.0f, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (inertia[2], 5.0f, 1.0e-4f);

    torque[0] = 0.0f;
    torque[1] = 2.0f;
    torque[2] = 0.0f;
    require (lrg_dynamics_world_apply_torque (world, wide, torque, &error), &error);
    require (lrg_dynamics_world_apply_torque (world, cube, torque, &error), &error);
    require (lrg_dynamics_world_step (world, 0.1f, NULL, &error), &error);
    require (lrg_dynamics_world_get_angular_velocity (world, wide, angular, &error), &error);
    /* Iyy is 10, so ωy = (1/10) * 2 * 0.1. The fixed step stays within 0.1 s. */
    g_assert_cmpfloat_with_epsilon (angular[0], 0.0f, 1.0e-5f);
    g_assert_cmpfloat_with_epsilon (angular[1], 0.02f, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (angular[2], 0.0f, 1.0e-5f);
    require (lrg_dynamics_world_get_angular_velocity (world, cube, angular, &error), &error);
    /* Iyy is 2, so the same torque spins five times faster. */
    g_assert_cmpfloat_with_epsilon (angular[1], 0.1f, 1.0e-4f);
}

static void
test_joint_linear_limit (void)
{
    g_autoptr(LrgDynamicsWorld) world = NULL;
    g_autoptr(GError) error = NULL;
    gfloat anchor[3];
    gfloat axis[3];
    gfloat position[3];
    gfloat force[3];
    guint left;
    guint right;
    guint joint;
    guint i;
    gfloat distance;

    world = lrg_dynamics_world_new ();
    require (lrg_dynamics_world_set_step_limits (world, 0.02f, 1, &error), &error);
    require (lrg_dynamics_world_set_sleep (world, 1.0e-8f, 1000, &error), &error);
    lrg_dynamics_world_set_iterations (world, 16);
    left = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 0.0f, 0.0f, 0.0f);
    right = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 2.0f, 0.0f, 0.0f);
    memset (anchor, 0, sizeof (anchor));
    axis[0] = 0.0f;
    axis[1] = 1.0f;
    axis[2] = 0.0f;
    joint = G_MAXUINT;
    require (lrg_dynamics_world_add_joint (world, left, right,
                                          anchor, axis, anchor, axis,
                                          2.0f, 2.0f, 0.0f, (gfloat) G_PI,
                                          &joint, &error),
             &error);
    force[0] = -80.0f;
    force[1] = 0.0f;
    force[2] = 0.0f;
    for (i = 0; i < 40; i++)
    {
        require (lrg_dynamics_world_apply_force (world, left, force, &error), &error);
        require (lrg_dynamics_world_step (world, 0.02f, NULL, &error), &error);
    }
    require (lrg_dynamics_world_get_position (world, left, position, &error), &error);
    distance = position[0];
    require (lrg_dynamics_world_get_position (world, right, position, &error), &error);
    distance = position[0] - distance;
    g_assert_cmpfloat_with_epsilon (distance, 2.0f, 0.08f);
}

static void
test_joint_angular_limit (void)
{
    g_autoptr(LrgDynamicsWorld) world = NULL;
    g_autoptr(GError) error = NULL;
    gfloat anchor[3];
    gfloat axis[3];
    gfloat torque[3];
    guint mover;
    guint pivot;
    guint joint;
    guint i;
    gfloat angle;

    world = lrg_dynamics_world_new ();
    require (lrg_dynamics_world_set_step_limits (world, 0.01f, 1, &error), &error);
    require (lrg_dynamics_world_set_sleep (world, 1.0e-8f, 1000, &error), &error);
    lrg_dynamics_world_set_iterations (world, 20);
    pivot = add_sphere (world, LRG_DYNAMICS_BODY_STATIC, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f);
    mover = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 3.0f, 0.0f, 0.0f);
    memset (anchor, 0, sizeof (anchor));
    axis[0] = 0.0f;
    axis[1] = 1.0f;
    axis[2] = 0.0f;
    joint = G_MAXUINT;
    require (lrg_dynamics_world_add_joint (world, mover, pivot,
                                          anchor, axis, anchor, axis,
                                          0.0f, 10.0f, 0.0f, 0.25f,
                                          &joint, &error),
             &error);
    torque[0] = 0.0f;
    torque[1] = 0.0f;
    torque[2] = 0.08f;
    for (i = 0; i < 80; i++)
    {
        require (lrg_dynamics_world_apply_torque (world, mover, torque, &error), &error);
        require (lrg_dynamics_world_step (world, 0.01f, NULL, &error), &error);
    }
    angle = axis_angle (world, mover);
    g_assert_cmpfloat (angle, >, 0.10f);
    g_assert_cmpfloat (angle, <, 0.40f);
}

static void
test_sphere_contacts (void)
{
    g_autoptr(LrgDynamicsWorld) pair = NULL;
    g_autoptr(LrgDynamicsWorld) ground = NULL;
    g_autoptr(GError) error = NULL;
    gfloat normal_up[3];
    gfloat position[3];
    gfloat velocity[3];
    gfloat angular[3];
    gfloat point[3];
    gfloat normal[3];
    gfloat depth;
    guint left;
    guint right;
    guint sphere;
    guint plane;
    guint steps;
    LrgDynamicsContactKind kind;
    guint body_a;
    guint body_b;
    gfloat distance;

    pair = lrg_dynamics_world_new ();
    require (lrg_dynamics_world_set_step_limits (pair, 1.0f / 60.0f, 1, &error), &error);
    left = add_sphere (pair, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    right = add_sphere (pair, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 1.0f, 1.5f, 0.0f, 0.0f);
    require (lrg_dynamics_world_step (pair, 1.0f / 60.0f, &steps, &error), &error);
    g_assert_cmpuint (steps, ==, 1);
    g_assert_cmpuint (lrg_dynamics_world_get_contact_count (pair), >=, 1);
    require (lrg_dynamics_world_get_contact (pair, 0, &kind, &body_a, &body_b,
                                            point, normal, &depth, &error),
             &error);
    g_assert_cmpint (kind, ==, LRG_DYNAMICS_CONTACT_SPHERE_SPHERE);
    g_assert_cmpfloat (depth, >, 0.4f);
    for (steps = 0; steps < 30; steps++)
        require (lrg_dynamics_world_step (pair, 1.0f / 60.0f, NULL, &error), &error);
    require (lrg_dynamics_world_get_position (pair, left, position, &error), &error);
    distance = position[0];
    require (lrg_dynamics_world_get_position (pair, right, position, &error), &error);
    distance = fabsf (position[0] - distance);
    g_assert_cmpfloat (distance, >, 1.95f);
    g_assert_cmpfloat (distance, <, 2.6f);

    ground = lrg_dynamics_world_new ();
    require (lrg_dynamics_world_set_step_limits (ground, 1.0f / 60.0f, 1, &error), &error);
    normal_up[0] = 0.0f;
    normal_up[1] = 1.0f;
    normal_up[2] = 0.0f;
    velocity[0] = 0.0f;
    velocity[1] = -10.0f;
    velocity[2] = 0.0f;
    require (lrg_dynamics_world_set_gravity (ground, velocity, &error), &error);
    require (lrg_dynamics_world_set_material (ground, 0.0f, 0.8f, &error), &error);
    plane = G_MAXUINT;
    require (lrg_dynamics_world_add_plane (ground, normal_up, 0.0f, &plane, &error), &error);
    sphere = add_sphere (ground, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.5f, 0.0f, 0.5f, 0.0f);
    velocity[0] = 2.0f;
    velocity[1] = 0.0f;
    velocity[2] = 0.0f;
    require (lrg_dynamics_world_set_linear_velocity (ground, sphere, velocity, &error), &error);
    require (lrg_dynamics_world_step (ground, 1.0f / 60.0f, NULL, &error), &error);
    g_assert_cmpuint (lrg_dynamics_world_get_contact_count (ground), >=, 1);
    require (lrg_dynamics_world_get_contact (ground, 0, &kind, &body_a, &body_b,
                                            point, normal, &depth, &error),
             &error);
    g_assert_cmpint (kind, ==, LRG_DYNAMICS_CONTACT_SPHERE_PLANE);
    g_assert_cmpuint (body_b, ==, plane);
    for (steps = 0; steps < 90; steps++)
        require (lrg_dynamics_world_step (ground, 1.0f / 60.0f, NULL, &error), &error);
    require (lrg_dynamics_world_get_position (ground, sphere, position, &error), &error);
    require (lrg_dynamics_world_get_linear_velocity (ground, sphere, velocity, &error), &error);
    require (lrg_dynamics_world_get_angular_velocity (ground, sphere, angular, &error), &error);
    g_assert_cmpfloat (position[1], >, 0.35f);
    g_assert_cmpfloat (position[1], <, 0.7f);
    /*
     * Coulomb friction stops the slip and leaves the sphere rolling.
     * I = (2/5)mr², so v_roll = v0 / (1 + 2/5) = 2 / 1.4. A frictionless
     * sphere would still be at 2, and a stopped one would not be spinning.
     */
    g_assert_cmpfloat (fabsf (velocity[0] - (2.0f / 1.4f)), <, 0.08f);
    g_assert_cmpfloat (fabsf (angular[2] * 0.5f + velocity[0]), <, 0.15f);
}

static void
test_box_extents_are_not_contacts (void)
{
    g_autoptr(LrgDynamicsWorld) world = NULL;
    g_autoptr(GError) error = NULL;
    gfloat position[3];
    guint a;
    guint b;

    world = lrg_dynamics_world_new ();
    require (lrg_dynamics_world_set_step_limits (world, 0.05f, 1, &error), &error);
    a = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 0.0f, 0.0f, 0.0f);
    b = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 3.0f, 0.0f, 0.0f);
    require (lrg_dynamics_world_set_box_inertia (world, a, 2.0f, 2.0f, 2.0f, &error), &error);
    require (lrg_dynamics_world_set_box_inertia (world, b, 2.0f, 2.0f, 2.0f, &error), &error);
    require (lrg_dynamics_world_step (world, 0.05f, NULL, &error), &error);
    g_assert_cmpuint (lrg_dynamics_world_get_contact_count (world), ==, 0);
    require (lrg_dynamics_world_get_position (world, a, position, &error), &error);
    g_assert_cmpfloat_with_epsilon (position[0], 0.0f, 1.0e-5f);
    require (lrg_dynamics_world_get_position (world, b, position, &error), &error);
    g_assert_cmpfloat_with_epsilon (position[0], 3.0f, 1.0e-5f);
}

static void
test_bounded_step (void)
{
    g_autoptr(LrgDynamicsWorld) world = NULL;
    g_autoptr(GError) error = NULL;
    gfloat gravity[3];
    gfloat position[3];
    gfloat velocity[3];
    guint id;
    guint substeps;

    world = lrg_dynamics_world_new ();
    gravity[0] = 0.0f;
    gravity[1] = -10.0f;
    gravity[2] = 0.0f;
    require (lrg_dynamics_world_set_gravity (world, gravity, &error), &error);
    require (lrg_dynamics_world_set_step_limits (world, 0.1f, 2, &error), &error);
    id = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 0.0f, 0.0f, 0.0f);
    require (lrg_dynamics_world_step (world, 1.0f, &substeps, &error), &error);
    g_assert_cmpuint (substeps, ==, 2);
    g_assert_cmpfloat_with_epsilon (lrg_dynamics_world_get_dropped_time (world), 0.8f, 1.0e-4f);
    require (lrg_dynamics_world_get_position (world, id, position, &error), &error);
    require (lrg_dynamics_world_get_linear_velocity (world, id, velocity, &error), &error);
    g_assert_cmpfloat_with_epsilon (position[1], -0.3f, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (velocity[1], -2.0f, 1.0e-4f);
}

static void
test_sleep_and_freeze (void)
{
    g_autoptr(LrgDynamicsWorld) sleeper = NULL;
    g_autoptr(LrgDynamicsWorld) frozen_world = NULL;
    g_autoptr(GError) error = NULL;
    gfloat gravity[3];
    gfloat force[3];
    gfloat position[3];
    gboolean sleeping;
    guint id;
    guint substeps;

    sleeper = lrg_dynamics_world_new ();
    require (lrg_dynamics_world_set_step_limits (sleeper, 0.01f, 4, &error), &error);
    require (lrg_dynamics_world_set_sleep (sleeper, 1.0e-4f, 4, &error), &error);
    id = add_sphere (sleeper, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 0.0f, 1.0f, 0.0f);
    require (lrg_dynamics_world_step (sleeper, 0.04f, &substeps, &error), &error);
    g_assert_cmpuint (substeps, ==, 4);
    require (lrg_dynamics_world_get_sleeping (sleeper, id, &sleeping, &error), &error);
    g_assert_true (sleeping);
    require (lrg_dynamics_world_get_position (sleeper, id, position, &error), &error);
    g_assert_cmpfloat_with_epsilon (position[1], 1.0f, 1.0e-6f);
    force[0] = 10.0f;
    force[1] = 0.0f;
    force[2] = 0.0f;
    require (lrg_dynamics_world_apply_force (sleeper, id, force, &error), &error);
    require (lrg_dynamics_world_get_sleeping (sleeper, id, &sleeping, &error), &error);
    g_assert_false (sleeping);
    require (lrg_dynamics_world_set_step_limits (sleeper, 0.01f, 1, &error), &error);
    require (lrg_dynamics_world_step (sleeper, 0.01f, NULL, &error), &error);
    require (lrg_dynamics_world_get_position (sleeper, id, position, &error), &error);
    g_assert_cmpfloat (position[0], >, 0.0f);

    frozen_world = lrg_dynamics_world_new ();
    gravity[0] = 0.0f;
    gravity[1] = -10.0f;
    gravity[2] = 0.0f;
    require (lrg_dynamics_world_set_gravity (frozen_world, gravity, &error), &error);
    require (lrg_dynamics_world_set_step_limits (frozen_world, 0.1f, 1, &error), &error);
    id = add_sphere (frozen_world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.2f, 0.0f, 2.0f, 0.0f);
    require (lrg_dynamics_world_set_frozen (frozen_world, id, TRUE, &error), &error);
    require (lrg_dynamics_world_step (frozen_world, 0.1f, NULL, &error), &error);
    require (lrg_dynamics_world_step (frozen_world, 0.1f, NULL, &error), &error);
    require (lrg_dynamics_world_get_position (frozen_world, id, position, &error), &error);
    g_assert_cmpfloat_with_epsilon (position[1], 2.0f, 1.0e-5f);
    g_assert_false (lrg_dynamics_world_apply_force (frozen_world, id, force, &error));
    g_assert_error (error, LRG_DYNAMICS_ERROR, LRG_DYNAMICS_ERROR_STATE);
    g_clear_error (&error);
    require (lrg_dynamics_world_set_frozen (frozen_world, id, FALSE, &error), &error);
    require (lrg_dynamics_world_step (frozen_world, 0.1f, NULL, &error), &error);
    require (lrg_dynamics_world_get_position (frozen_world, id, position, &error), &error);
    g_assert_cmpfloat (position[1], <, 1.95f);
}

static void
test_eviction (void)
{
    g_autoptr(LrgDynamicsWorld) world = NULL;
    g_autoptr(GError) error = NULL;
    gfloat minimum[3];
    gfloat maximum[3];
    gfloat velocity[3];
    gfloat position[3];
    guint leaving;
    guint staying;
    guint evicted[4];
    gboolean active;
    guint count;

    world = lrg_dynamics_world_new ();
    require (lrg_dynamics_world_set_step_limits (world, 0.1f, 1, &error), &error);
    minimum[0] = -1.0f;
    minimum[1] = -1.0f;
    minimum[2] = -1.0f;
    maximum[0] = 1.0f;
    maximum[1] = 1.0f;
    maximum[2] = 1.0f;
    require (lrg_dynamics_world_set_bounds (world, minimum, maximum, &error), &error);
    leaving = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.1f, 0.0f, 0.0f, 0.0f);
    staying = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.1f, 0.2f, 0.0f, 0.0f);
    velocity[0] = 20.0f;
    velocity[1] = 0.0f;
    velocity[2] = 0.0f;
    require (lrg_dynamics_world_set_linear_velocity (world, leaving, velocity, &error), &error);
    require (lrg_dynamics_world_step (world, 0.1f, NULL, &error), &error);
    count = lrg_dynamics_world_get_evicted (world, evicted, 4);
    g_assert_cmpuint (count, ==, 1);
    g_assert_cmpuint (evicted[0], ==, leaving);
    require (lrg_dynamics_world_body_is_active (world, leaving, &active, &error), &error);
    g_assert_false (active);
    require (lrg_dynamics_world_body_is_active (world, staying, &active, &error), &error);
    g_assert_true (active);
    require (lrg_dynamics_world_get_position (world, leaving, position, &error), &error);
    g_assert_cmpfloat (position[0], >, 1.0f);
    position[0] = 50.0f;
    require (lrg_dynamics_world_step (world, 0.1f, NULL, &error), &error);
    require (lrg_dynamics_world_get_position (world, leaving, position, &error), &error);
    g_assert_cmpfloat (position[0], <, 5.0f);
    g_assert_cmpuint (lrg_dynamics_world_get_evicted (world, NULL, 0), ==, 0);
    require (lrg_dynamics_world_get_position (world, staying, position, &error), &error);
    g_assert_cmpfloat_with_epsilon (position[0], 0.2f, 1.0e-4f);
}

static void
test_skeleton_pose (void)
{
    g_autoptr(LrgDynamicsWorld) world = NULL;
    g_autoptr(LrgBonePose) root_local = NULL;
    g_autoptr(LrgBonePose) child_local = NULL;
    g_autoptr(LrgBonePose) grand_local = NULL;
    g_autoptr(LrgBonePose) identity = NULL;
    g_autoptr(LrgBonePose) world0 = NULL;
    g_autoptr(LrgBonePose) world1 = NULL;
    g_autoptr(LrgBonePose) world2 = NULL;
    g_autoptr(LrgBonePose) multiplied = NULL;
    g_autoptr(GError) error = NULL;
    LrgBonePose synced_local;
    LrgBonePose synced_world;
    gfloat position[3];
    gfloat orientation[4];
    gfloat half;
    guint root;
    guint child;
    guint grand;
    gfloat gap;

    half = sqrtf (0.5f);
    identity = lrg_bone_pose_new ();
    root_local = lrg_bone_pose_new_with_values (0.0f, 0.0f, 0.0f,
                                                0.0f, half, 0.0f, half,
                                                1.0f, 1.0f, 1.0f);
    child_local = lrg_bone_pose_new_with_values (1.0f, 0.0f, 0.0f,
                                                 0.0f, 0.0f, 0.0f, 1.0f,
                                                 1.0f, 1.0f, 1.0f);
    grand_local = lrg_bone_pose_new_with_values (1.0f, 0.0f, 0.0f,
                                                 0.0f, 0.0f, 0.0f, 1.0f,
                                                 1.0f, 1.0f, 1.0f);
    world0 = lrg_bone_pose_compose (identity, root_local);
    world1 = lrg_bone_pose_compose (world0, child_local);
    world2 = lrg_bone_pose_compose (world1, grand_local);
    multiplied = lrg_bone_pose_multiply (world1, grand_local);
    gap = fabsf (multiplied->position_x - world2->position_x) +
          fabsf (multiplied->position_z - world2->position_z);
    g_assert_cmpfloat (gap, >, 0.5f);

    world = lrg_dynamics_world_new ();
    root = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.05f, 0.0f, 0.0f, 0.0f);
    child = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.05f, 0.0f, 0.0f, 0.0f);
    grand = add_sphere (world, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f, 0.05f, 0.0f, 0.0f, 0.0f);
    position[0] = world0->position_x;
    position[1] = world0->position_y;
    position[2] = world0->position_z;
    orientation[0] = world0->rotation_x;
    orientation[1] = world0->rotation_y;
    orientation[2] = world0->rotation_z;
    orientation[3] = world0->rotation_w;
    require (lrg_dynamics_world_set_pose (world, root, position, orientation, &error), &error);
    position[0] = world1->position_x;
    position[1] = world1->position_y;
    position[2] = world1->position_z;
    orientation[0] = world1->rotation_x;
    orientation[1] = world1->rotation_y;
    orientation[2] = world1->rotation_z;
    orientation[3] = world1->rotation_w;
    require (lrg_dynamics_world_set_pose (world, child, position, orientation, &error), &error);
    position[0] = world2->position_x;
    position[1] = world2->position_y;
    position[2] = world2->position_z;
    orientation[0] = world2->rotation_x;
    orientation[1] = world2->rotation_y;
    orientation[2] = world2->rotation_z;
    orientation[3] = world2->rotation_w;
    require (lrg_dynamics_world_set_pose (world, grand, position, orientation, &error), &error);

    memset (&synced_local, 0, sizeof (synced_local));
    synced_local.rotation_w = 1.0f;
    synced_local.scale_x = 2.0f;
    synced_local.scale_y = 2.0f;
    synced_local.scale_z = 2.0f;
    memset (&synced_world, 0, sizeof (synced_world));
    require (lrg_dynamics_world_write_bone_pose (world, root, NULL,
                                                &synced_local, &synced_world, &error),
             &error);
    g_assert_cmpfloat_with_epsilon (synced_local.scale_x, 2.0f, 1.0e-5f);
    g_assert_cmpfloat_with_epsilon (synced_world.position_x, world0->position_x, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (synced_world.scale_x, 2.0f, 1.0e-4f);

    memset (&synced_local, 0, sizeof (synced_local));
    synced_local.rotation_w = 1.0f;
    synced_local.scale_x = 1.0f;
    synced_local.scale_y = 1.0f;
    synced_local.scale_z = 1.0f;
    require (lrg_dynamics_world_write_bone_pose (world, child, &synced_world,
                                                &synced_local, &synced_world, &error),
             &error);
    /* The child call above used the root world, whose scale is 2. Rebuild
     * the chain at unit scale so the grandchild matches compose(). */
    memset (&synced_local, 0, sizeof (synced_local));
    synced_local.rotation_w = 1.0f;
    synced_local.scale_x = 1.0f;
    synced_local.scale_y = 1.0f;
    synced_local.scale_z = 1.0f;
    require (lrg_dynamics_world_write_bone_pose (world, root, NULL,
                                                &synced_local, &synced_world, &error),
             &error);
    g_assert_cmpfloat_with_epsilon (synced_world.position_x, world0->position_x, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (synced_world.position_y, world0->position_y, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (synced_world.position_z, world0->position_z, 1.0e-4f);
    memset (&synced_local, 0, sizeof (synced_local));
    synced_local.rotation_w = 1.0f;
    synced_local.scale_x = 1.0f;
    synced_local.scale_y = 1.0f;
    synced_local.scale_z = 1.0f;
    require (lrg_dynamics_world_write_bone_pose (world, child, &synced_world,
                                                &synced_local, &synced_world, &error),
             &error);
    g_assert_cmpfloat_with_epsilon (synced_world.position_x, world1->position_x, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (synced_world.position_z, world1->position_z, 1.0e-4f);
    memset (&synced_local, 0, sizeof (synced_local));
    synced_local.rotation_w = 1.0f;
    synced_local.scale_x = 1.0f;
    synced_local.scale_y = 1.0f;
    synced_local.scale_z = 1.0f;
    require (lrg_dynamics_world_write_bone_pose (world, grand, &synced_world,
                                                &synced_local, &synced_world, &error),
             &error);
    g_assert_cmpfloat_with_epsilon (synced_world.position_x, world2->position_x, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (synced_world.position_y, world2->position_y, 1.0e-4f);
    g_assert_cmpfloat_with_epsilon (synced_world.position_z, world2->position_z, 1.0e-4f);
    g_assert_cmpfloat (fabsf (synced_world.position_x - multiplied->position_x) +
                       fabsf (synced_world.position_z - multiplied->position_z), >, 0.5f);
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/dynamics/box-inertia", test_box_inertia_and_torque);
    g_test_add_func ("/dynamics/joint-linear-limit", test_joint_linear_limit);
    g_test_add_func ("/dynamics/joint-angular-limit", test_joint_angular_limit);
    g_test_add_func ("/dynamics/sphere-contacts", test_sphere_contacts);
    g_test_add_func ("/dynamics/box-extents-are-not-contacts", test_box_extents_are_not_contacts);
    g_test_add_func ("/dynamics/bounded-step", test_bounded_step);
    g_test_add_func ("/dynamics/sleep-and-freeze", test_sleep_and_freeze);
    g_test_add_func ("/dynamics/eviction", test_eviction);
    g_test_add_func ("/dynamics/skeleton-pose", test_skeleton_pose);
    return g_test_run ();
}
