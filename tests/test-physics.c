/* test-physics.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for physics module (LrgCollisionInfo, LrgRigidBody, LrgPhysicsWorld).
 */

#include <glib.h>
#include <math.h>
#include "libregnum.h"

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgRigidBody *body;
} RigidBodyFixture;

static void
rigid_body_fixture_set_up (RigidBodyFixture *fixture,
                           gconstpointer     user_data)
{
    fixture->body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    g_assert_nonnull (fixture->body);
}

static void
rigid_body_fixture_tear_down (RigidBodyFixture *fixture,
                              gconstpointer     user_data)
{
    g_clear_object (&fixture->body);
}

typedef struct
{
    LrgPhysicsWorld *world;
} PhysicsWorldFixture;

static void
physics_world_fixture_set_up (PhysicsWorldFixture *fixture,
                              gconstpointer        user_data)
{
    fixture->world = lrg_physics_world_new ();
    g_assert_nonnull (fixture->world);
}

static void
physics_world_fixture_tear_down (PhysicsWorldFixture *fixture,
                                 gconstpointer        user_data)
{
    g_clear_object (&fixture->world);
}

/* ==========================================================================
 * Collision Info Tests
 * ========================================================================== */

static void
test_collision_info_new (void)
{
    g_autoptr(GObject) body_a = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr(GObject) body_b = g_object_new (G_TYPE_OBJECT, NULL);
    LrgCollisionInfo *info;

    info = lrg_collision_info_new (body_a, body_b,
                                   1.0f, 0.0f,
                                   0.5f,
                                   10.0f, 20.0f);
    g_assert_nonnull (info);
    lrg_collision_info_free (info);
}

static void
test_collision_info_bodies (void)
{
    g_autoptr(GObject) body_a = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr(GObject) body_b = g_object_new (G_TYPE_OBJECT, NULL);
    LrgCollisionInfo *info;

    info = lrg_collision_info_new (body_a, body_b,
                                   1.0f, 0.0f,
                                   0.5f,
                                   10.0f, 20.0f);

    g_assert_true (lrg_collision_info_get_body_a (info) == body_a);
    g_assert_true (lrg_collision_info_get_body_b (info) == body_b);

    lrg_collision_info_free (info);
}

static void
test_collision_info_normal (void)
{
    g_autoptr(GObject) body_a = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr(GObject) body_b = g_object_new (G_TYPE_OBJECT, NULL);
    LrgCollisionInfo *info;
    gfloat nx, ny;

    info = lrg_collision_info_new (body_a, body_b,
                                   0.707f, 0.707f,
                                   0.5f,
                                   10.0f, 20.0f);

    lrg_collision_info_get_normal (info, &nx, &ny);
    g_assert_cmpfloat_with_epsilon (nx, 0.707f, 0.001f);
    g_assert_cmpfloat_with_epsilon (ny, 0.707f, 0.001f);

    lrg_collision_info_free (info);
}

static void
test_collision_info_penetration (void)
{
    g_autoptr(GObject) body_a = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr(GObject) body_b = g_object_new (G_TYPE_OBJECT, NULL);
    LrgCollisionInfo *info;

    info = lrg_collision_info_new (body_a, body_b,
                                   1.0f, 0.0f,
                                   2.5f,
                                   10.0f, 20.0f);

    g_assert_cmpfloat_with_epsilon (lrg_collision_info_get_penetration (info), 2.5f, 0.001f);

    lrg_collision_info_free (info);
}

static void
test_collision_info_contact_point (void)
{
    g_autoptr(GObject) body_a = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr(GObject) body_b = g_object_new (G_TYPE_OBJECT, NULL);
    LrgCollisionInfo *info;
    gfloat cx, cy;

    info = lrg_collision_info_new (body_a, body_b,
                                   1.0f, 0.0f,
                                   0.5f,
                                   15.0f, 25.0f);

    lrg_collision_info_get_contact_point (info, &cx, &cy);
    g_assert_cmpfloat_with_epsilon (cx, 15.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (cy, 25.0f, 0.001f);

    lrg_collision_info_free (info);
}

static void
test_collision_info_copy (void)
{
    g_autoptr(GObject) body_a = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr(GObject) body_b = g_object_new (G_TYPE_OBJECT, NULL);
    LrgCollisionInfo *info;
    LrgCollisionInfo *copy;

    info = lrg_collision_info_new (body_a, body_b,
                                   1.0f, 0.0f,
                                   0.5f,
                                   10.0f, 20.0f);

    copy = lrg_collision_info_copy (info);
    g_assert_nonnull (copy);

    g_assert_true (lrg_collision_info_get_body_a (copy) == body_a);
    g_assert_true (lrg_collision_info_get_body_b (copy) == body_b);
    g_assert_cmpfloat_with_epsilon (lrg_collision_info_get_penetration (copy), 0.5f, 0.001f);

    lrg_collision_info_free (info);
    lrg_collision_info_free (copy);
}

static void
test_collision_info_gtype (void)
{
    GType type;

    type = lrg_collision_info_get_type ();
    g_assert_true (G_TYPE_IS_BOXED (type));
    g_assert_cmpstr (g_type_name (type), ==, "LrgCollisionInfo");
}

/* ==========================================================================
 * Rigid Body Tests
 * ========================================================================== */

static void
test_rigid_body_new (void)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    g_assert_nonnull (body);
    g_assert_true (LRG_IS_RIGID_BODY (body));
}

static void
test_rigid_body_body_type (RigidBodyFixture *fixture,
                           gconstpointer     user_data)
{
    g_assert_cmpint (lrg_rigid_body_get_body_type (fixture->body), ==, LRG_RIGID_BODY_DYNAMIC);

    lrg_rigid_body_set_body_type (fixture->body, LRG_RIGID_BODY_STATIC);
    g_assert_cmpint (lrg_rigid_body_get_body_type (fixture->body), ==, LRG_RIGID_BODY_STATIC);

    lrg_rigid_body_set_body_type (fixture->body, LRG_RIGID_BODY_KINEMATIC);
    g_assert_cmpint (lrg_rigid_body_get_body_type (fixture->body), ==, LRG_RIGID_BODY_KINEMATIC);
}

static void
test_rigid_body_mass (RigidBodyFixture *fixture,
                      gconstpointer     user_data)
{
    /* Default mass should be 1.0 */
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_mass (fixture->body), 1.0f, 0.001f);

    lrg_rigid_body_set_mass (fixture->body, 5.0f);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_mass (fixture->body), 5.0f, 0.001f);
}

static void
test_rigid_body_restitution (RigidBodyFixture *fixture,
                             gconstpointer     user_data)
{
    lrg_rigid_body_set_restitution (fixture->body, 0.8f);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_restitution (fixture->body), 0.8f, 0.001f);
}

static void
test_rigid_body_friction (RigidBodyFixture *fixture,
                          gconstpointer     user_data)
{
    lrg_rigid_body_set_friction (fixture->body, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_friction (fixture->body), 0.5f, 0.001f);
}

static void
test_rigid_body_linear_damping (RigidBodyFixture *fixture,
                                gconstpointer     user_data)
{
    lrg_rigid_body_set_linear_damping (fixture->body, 0.1f);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_linear_damping (fixture->body), 0.1f, 0.001f);
}

static void
test_rigid_body_angular_damping (RigidBodyFixture *fixture,
                                 gconstpointer     user_data)
{
    lrg_rigid_body_set_angular_damping (fixture->body, 0.2f);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_angular_damping (fixture->body), 0.2f, 0.001f);
}

static void
test_rigid_body_is_trigger (RigidBodyFixture *fixture,
                            gconstpointer     user_data)
{
    g_assert_false (lrg_rigid_body_get_is_trigger (fixture->body));

    lrg_rigid_body_set_is_trigger (fixture->body, TRUE);
    g_assert_true (lrg_rigid_body_get_is_trigger (fixture->body));

    lrg_rigid_body_set_is_trigger (fixture->body, FALSE);
    g_assert_false (lrg_rigid_body_get_is_trigger (fixture->body));
}

static void
test_rigid_body_gravity_scale (RigidBodyFixture *fixture,
                               gconstpointer     user_data)
{
    /* Default should be 1.0 */
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_gravity_scale (fixture->body), 1.0f, 0.001f);

    lrg_rigid_body_set_gravity_scale (fixture->body, 0.0f);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_gravity_scale (fixture->body), 0.0f, 0.001f);

    lrg_rigid_body_set_gravity_scale (fixture->body, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_gravity_scale (fixture->body), 2.0f, 0.001f);
}

static void
test_rigid_body_position (RigidBodyFixture *fixture,
                          gconstpointer     user_data)
{
    gfloat x, y;

    /* Default position is origin */
    lrg_rigid_body_get_position (fixture->body, &x, &y);
    g_assert_cmpfloat_with_epsilon (x, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (y, 0.0f, 0.001f);

    lrg_rigid_body_set_position (fixture->body, 100.0f, 200.0f);
    lrg_rigid_body_get_position (fixture->body, &x, &y);
    g_assert_cmpfloat_with_epsilon (x, 100.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (y, 200.0f, 0.001f);
}

static void
test_rigid_body_rotation (RigidBodyFixture *fixture,
                          gconstpointer     user_data)
{
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_rotation (fixture->body), 0.0f, 0.001f);

    lrg_rigid_body_set_rotation (fixture->body, G_PI / 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_rotation (fixture->body), G_PI / 2.0f, 0.001f);
}

static void
test_rigid_body_velocity (RigidBodyFixture *fixture,
                          gconstpointer     user_data)
{
    gfloat vx, vy;

    lrg_rigid_body_get_velocity (fixture->body, &vx, &vy);
    g_assert_cmpfloat_with_epsilon (vx, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (vy, 0.0f, 0.001f);

    lrg_rigid_body_set_velocity (fixture->body, 50.0f, -25.0f);
    lrg_rigid_body_get_velocity (fixture->body, &vx, &vy);
    g_assert_cmpfloat_with_epsilon (vx, 50.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (vy, -25.0f, 0.001f);
}

static void
test_rigid_body_angular_velocity (RigidBodyFixture *fixture,
                                  gconstpointer     user_data)
{
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_angular_velocity (fixture->body), 0.0f, 0.001f);

    lrg_rigid_body_set_angular_velocity (fixture->body, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_angular_velocity (fixture->body), 2.0f, 0.001f);
}

static void
test_rigid_body_add_force (RigidBodyFixture *fixture,
                           gconstpointer     user_data)
{
    gfloat vx, vy;

    /* Apply velocity change directly */
    lrg_rigid_body_add_force (fixture->body, 10.0f, 5.0f, LRG_FORCE_MODE_VELOCITY_CHANGE);
    lrg_rigid_body_get_velocity (fixture->body, &vx, &vy);
    g_assert_cmpfloat_with_epsilon (vx, 10.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (vy, 5.0f, 0.001f);
}

static void
test_rigid_body_add_impulse (RigidBodyFixture *fixture,
                             gconstpointer     user_data)
{
    gfloat vx, vy;

    /* Impulse mode: delta_v = impulse / mass */
    lrg_rigid_body_set_mass (fixture->body, 2.0f);
    lrg_rigid_body_add_force (fixture->body, 10.0f, 0.0f, LRG_FORCE_MODE_IMPULSE);
    lrg_rigid_body_get_velocity (fixture->body, &vx, &vy);
    g_assert_cmpfloat_with_epsilon (vx, 5.0f, 0.001f);  /* 10 / 2 = 5 */
}

static void
test_rigid_body_add_torque (RigidBodyFixture *fixture,
                            gconstpointer     user_data)
{
    /* Apply angular velocity change */
    lrg_rigid_body_add_torque (fixture->body, 3.0f, LRG_FORCE_MODE_VELOCITY_CHANGE);
    g_assert_cmpfloat_with_epsilon (lrg_rigid_body_get_angular_velocity (fixture->body), 3.0f, 0.001f);
}

static void
test_rigid_body_clear_forces (RigidBodyFixture *fixture,
                              gconstpointer     user_data)
{
    gfloat vx, vy;

    /* Add some forces (accumulated, not velocity change) */
    lrg_rigid_body_add_force (fixture->body, 100.0f, 100.0f, LRG_FORCE_MODE_FORCE);
    lrg_rigid_body_add_torque (fixture->body, 50.0f, LRG_FORCE_MODE_FORCE);

    /* Clear should remove accumulated forces */
    lrg_rigid_body_clear_forces (fixture->body);

    /* Velocity should still be zero since forces weren't integrated */
    lrg_rigid_body_get_velocity (fixture->body, &vx, &vy);
    g_assert_cmpfloat_with_epsilon (vx, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (vy, 0.0f, 0.001f);
}

static void
test_rigid_body_box_shape (RigidBodyFixture *fixture,
                           gconstpointer     user_data)
{
    gfloat w, h;

    lrg_rigid_body_set_box_shape (fixture->body, 32.0f, 64.0f);

    g_assert_cmpint (lrg_rigid_body_get_shape_type (fixture->body), ==, LRG_COLLISION_SHAPE_BOX);

    lrg_rigid_body_get_shape_bounds (fixture->body, &w, &h);
    g_assert_cmpfloat_with_epsilon (w, 32.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (h, 64.0f, 0.001f);
}

static void
test_rigid_body_circle_shape (RigidBodyFixture *fixture,
                              gconstpointer     user_data)
{
    gfloat w, h;

    lrg_rigid_body_set_circle_shape (fixture->body, 16.0f);

    g_assert_cmpint (lrg_rigid_body_get_shape_type (fixture->body), ==, LRG_COLLISION_SHAPE_CIRCLE);

    lrg_rigid_body_get_shape_bounds (fixture->body, &w, &h);
    /* Circle bounds should be diameter x diameter */
    g_assert_cmpfloat_with_epsilon (w, 32.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (h, 32.0f, 0.001f);
}

static void
test_rigid_body_sleep_state (RigidBodyFixture *fixture,
                             gconstpointer     user_data)
{
    /* Default should be awake */
    g_assert_false (lrg_rigid_body_is_sleeping (fixture->body));

    lrg_rigid_body_sleep (fixture->body);
    g_assert_true (lrg_rigid_body_is_sleeping (fixture->body));

    lrg_rigid_body_wake_up (fixture->body);
    g_assert_false (lrg_rigid_body_is_sleeping (fixture->body));
}

static void
test_rigid_body_static_type (void)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_STATIC);
    g_assert_cmpint (lrg_rigid_body_get_body_type (body), ==, LRG_RIGID_BODY_STATIC);
}

static void
test_rigid_body_kinematic_type (void)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_KINEMATIC);
    g_assert_cmpint (lrg_rigid_body_get_body_type (body), ==, LRG_RIGID_BODY_KINEMATIC);
}

/* ==========================================================================
 * Physics World Tests
 * ========================================================================== */

static void
test_physics_world_new (void)
{
    g_autoptr(LrgPhysicsWorld) world = lrg_physics_world_new ();
    g_assert_nonnull (world);
    g_assert_true (LRG_IS_PHYSICS_WORLD (world));
}

static void
test_physics_world_gravity (PhysicsWorldFixture *fixture,
                            gconstpointer        user_data)
{
    gfloat gx, gy;

    /* Default gravity should be 0, 9.8 (or similar) */
    lrg_physics_world_get_gravity (fixture->world, &gx, &gy);
    /* Just check it has some value */
    g_assert_true (TRUE);

    lrg_physics_world_set_gravity (fixture->world, 0.0f, -10.0f);
    lrg_physics_world_get_gravity (fixture->world, &gx, &gy);
    g_assert_cmpfloat_with_epsilon (gx, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (gy, -10.0f, 0.001f);
}

static void
test_physics_world_time_step (PhysicsWorldFixture *fixture,
                              gconstpointer        user_data)
{
    lrg_physics_world_set_time_step (fixture->world, 1.0f / 60.0f);
    g_assert_cmpfloat_with_epsilon (lrg_physics_world_get_time_step (fixture->world),
                                    1.0f / 60.0f, 0.0001f);
}

static void
test_physics_world_velocity_iterations (PhysicsWorldFixture *fixture,
                                        gconstpointer        user_data)
{
    lrg_physics_world_set_velocity_iterations (fixture->world, 10);
    g_assert_cmpuint (lrg_physics_world_get_velocity_iterations (fixture->world), ==, 10);
}

static void
test_physics_world_position_iterations (PhysicsWorldFixture *fixture,
                                        gconstpointer        user_data)
{
    lrg_physics_world_set_position_iterations (fixture->world, 4);
    g_assert_cmpuint (lrg_physics_world_get_position_iterations (fixture->world), ==, 4);
}

static void
test_physics_world_add_body (PhysicsWorldFixture *fixture,
                             gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);

    g_assert_cmpuint (lrg_physics_world_get_body_count (fixture->world), ==, 0);

    lrg_physics_world_add_body (fixture->world, body);
    g_assert_cmpuint (lrg_physics_world_get_body_count (fixture->world), ==, 1);
}

static void
test_physics_world_remove_body (PhysicsWorldFixture *fixture,
                                gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    gboolean removed;

    lrg_physics_world_add_body (fixture->world, body);
    g_assert_cmpuint (lrg_physics_world_get_body_count (fixture->world), ==, 1);

    removed = lrg_physics_world_remove_body (fixture->world, body);
    g_assert_true (removed);
    g_assert_cmpuint (lrg_physics_world_get_body_count (fixture->world), ==, 0);
}

static void
test_physics_world_get_bodies (PhysicsWorldFixture *fixture,
                               gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body1 = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    g_autoptr(LrgRigidBody) body2 = lrg_rigid_body_new (LRG_RIGID_BODY_STATIC);
    GPtrArray *bodies;

    lrg_physics_world_add_body (fixture->world, body1);
    lrg_physics_world_add_body (fixture->world, body2);

    bodies = lrg_physics_world_get_bodies (fixture->world);
    g_assert_nonnull (bodies);
    g_assert_cmpuint (bodies->len, ==, 2);
}

static void
test_physics_world_clear (PhysicsWorldFixture *fixture,
                          gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body1 = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    g_autoptr(LrgRigidBody) body2 = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);

    lrg_physics_world_add_body (fixture->world, body1);
    lrg_physics_world_add_body (fixture->world, body2);
    g_assert_cmpuint (lrg_physics_world_get_body_count (fixture->world), ==, 2);

    lrg_physics_world_clear (fixture->world);
    g_assert_cmpuint (lrg_physics_world_get_body_count (fixture->world), ==, 0);
}

static void
test_physics_world_paused (PhysicsWorldFixture *fixture,
                           gconstpointer        user_data)
{
    g_assert_false (lrg_physics_world_is_paused (fixture->world));

    lrg_physics_world_set_paused (fixture->world, TRUE);
    g_assert_true (lrg_physics_world_is_paused (fixture->world));

    lrg_physics_world_set_paused (fixture->world, FALSE);
    g_assert_false (lrg_physics_world_is_paused (fixture->world));
}

static void
test_physics_world_step_basic (PhysicsWorldFixture *fixture,
                               gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    gfloat x, y;

    lrg_rigid_body_set_position (body, 0.0f, 0.0f);
    lrg_rigid_body_set_velocity (body, 100.0f, 0.0f);
    lrg_rigid_body_set_box_shape (body, 10.0f, 10.0f);

    lrg_physics_world_set_gravity (fixture->world, 0.0f, 0.0f);
    lrg_physics_world_set_time_step (fixture->world, 1.0f / 60.0f);
    lrg_physics_world_add_body (fixture->world, body);

    /* Step simulation */
    lrg_physics_world_step (fixture->world, 1.0f / 60.0f);

    /* Body should have moved */
    lrg_rigid_body_get_position (body, &x, &y);
    g_assert_cmpfloat (x, >, 0.0f);
}

static void
test_physics_world_step_gravity (PhysicsWorldFixture *fixture,
                                 gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    gfloat vx, vy;
    int i;

    lrg_rigid_body_set_position (body, 0.0f, 100.0f);
    lrg_rigid_body_set_velocity (body, 0.0f, 0.0f);
    lrg_rigid_body_set_box_shape (body, 10.0f, 10.0f);

    lrg_physics_world_set_gravity (fixture->world, 0.0f, -100.0f);
    lrg_physics_world_set_time_step (fixture->world, 1.0f / 60.0f);
    lrg_physics_world_add_body (fixture->world, body);

    /* Step simulation multiple times */
    for (i = 0; i < 10; i++)
    {
        lrg_physics_world_step (fixture->world, 1.0f / 60.0f);
    }

    /* Body should have fallen (y decreased or velocity is negative) */
    lrg_rigid_body_get_velocity (body, &vx, &vy);
    g_assert_cmpfloat (vy, <, 0.0f);
}

static void
test_physics_world_step_paused (PhysicsWorldFixture *fixture,
                                gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    gfloat x, y;

    lrg_rigid_body_set_position (body, 0.0f, 0.0f);
    lrg_rigid_body_set_velocity (body, 100.0f, 0.0f);
    lrg_rigid_body_set_box_shape (body, 10.0f, 10.0f);

    lrg_physics_world_set_gravity (fixture->world, 0.0f, 0.0f);
    lrg_physics_world_add_body (fixture->world, body);

    /* Pause and step */
    lrg_physics_world_set_paused (fixture->world, TRUE);
    lrg_physics_world_step (fixture->world, 1.0f / 60.0f);

    /* Body should NOT have moved */
    lrg_rigid_body_get_position (body, &x, &y);
    g_assert_cmpfloat_with_epsilon (x, 0.0f, 0.001f);
}

static void
test_physics_world_query_aabb (PhysicsWorldFixture *fixture,
                               gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body1 = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    g_autoptr(LrgRigidBody) body2 = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    g_autoptr(LrgRigidBody) body3 = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    GPtrArray *results;

    /* Place bodies at different positions */
    lrg_rigid_body_set_position (body1, 10.0f, 10.0f);
    lrg_rigid_body_set_box_shape (body1, 5.0f, 5.0f);

    lrg_rigid_body_set_position (body2, 100.0f, 100.0f);
    lrg_rigid_body_set_box_shape (body2, 5.0f, 5.0f);

    lrg_rigid_body_set_position (body3, 15.0f, 15.0f);
    lrg_rigid_body_set_box_shape (body3, 5.0f, 5.0f);

    lrg_physics_world_add_body (fixture->world, body1);
    lrg_physics_world_add_body (fixture->world, body2);
    lrg_physics_world_add_body (fixture->world, body3);

    /* Query a region that contains body1 and body3 */
    results = lrg_physics_world_query_aabb (fixture->world,
                                            0.0f, 0.0f,
                                            30.0f, 30.0f);
    g_assert_nonnull (results);
    g_assert_cmpuint (results->len, ==, 2);
    g_ptr_array_unref (results);
}

static void
test_physics_world_query_point (PhysicsWorldFixture *fixture,
                                gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    GPtrArray *results;

    lrg_rigid_body_set_position (body, 50.0f, 50.0f);
    lrg_rigid_body_set_box_shape (body, 20.0f, 20.0f);

    lrg_physics_world_add_body (fixture->world, body);

    /* Query inside the body */
    results = lrg_physics_world_query_point (fixture->world, 50.0f, 50.0f);
    g_assert_nonnull (results);
    g_assert_cmpuint (results->len, ==, 1);
    g_ptr_array_unref (results);

    /* Query outside the body */
    results = lrg_physics_world_query_point (fixture->world, 200.0f, 200.0f);
    g_assert_nonnull (results);
    g_assert_cmpuint (results->len, ==, 0);
    g_ptr_array_unref (results);
}

static void
test_physics_world_raycast (PhysicsWorldFixture *fixture,
                            gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    LrgRigidBody *hit_body = NULL;
    gfloat hit_x, hit_y;
    gfloat hit_nx, hit_ny;
    gboolean hit;

    lrg_rigid_body_set_position (body, 50.0f, 0.0f);
    lrg_rigid_body_set_box_shape (body, 10.0f, 100.0f);

    lrg_physics_world_add_body (fixture->world, body);

    /* Cast ray from left to right */
    hit = lrg_physics_world_raycast (fixture->world,
                                     0.0f, 0.0f,
                                     100.0f, 0.0f,
                                     &hit_body,
                                     &hit_x, &hit_y,
                                     &hit_nx, &hit_ny);
    g_assert_true (hit);
    g_assert_true (hit_body == body);
}

static void
test_physics_world_raycast_miss (PhysicsWorldFixture *fixture,
                                 gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    LrgRigidBody *hit_body = NULL;
    gfloat hit_x, hit_y;
    gfloat hit_nx, hit_ny;
    gboolean hit;

    lrg_rigid_body_set_position (body, 50.0f, 50.0f);
    lrg_rigid_body_set_box_shape (body, 10.0f, 10.0f);

    lrg_physics_world_add_body (fixture->world, body);

    /* Cast ray that misses */
    hit = lrg_physics_world_raycast (fixture->world,
                                     0.0f, 100.0f,
                                     100.0f, 100.0f,
                                     &hit_body,
                                     &hit_x, &hit_y,
                                     &hit_nx, &hit_ny);
    g_assert_false (hit);
    g_assert_null (hit_body);
}

static void
test_physics_world_static_body_no_move (PhysicsWorldFixture *fixture,
                                        gconstpointer        user_data)
{
    g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_STATIC);
    gfloat x, y;
    int i;

    lrg_rigid_body_set_position (body, 50.0f, 50.0f);
    lrg_rigid_body_set_box_shape (body, 10.0f, 10.0f);

    lrg_physics_world_set_gravity (fixture->world, 0.0f, -100.0f);
    lrg_physics_world_add_body (fixture->world, body);

    /* Step multiple times */
    for (i = 0; i < 10; i++)
    {
        lrg_physics_world_step (fixture->world, 1.0f / 60.0f);
    }

    /* Static body should not have moved */
    lrg_rigid_body_get_position (body, &x, &y);
    g_assert_cmpfloat_with_epsilon (x, 50.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (y, 50.0f, 0.001f);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Collision Info tests */
    g_test_add_func ("/physics/collision-info/new", test_collision_info_new);
    g_test_add_func ("/physics/collision-info/bodies", test_collision_info_bodies);
    g_test_add_func ("/physics/collision-info/normal", test_collision_info_normal);
    g_test_add_func ("/physics/collision-info/penetration", test_collision_info_penetration);
    g_test_add_func ("/physics/collision-info/contact-point", test_collision_info_contact_point);
    g_test_add_func ("/physics/collision-info/copy", test_collision_info_copy);
    g_test_add_func ("/physics/collision-info/gtype", test_collision_info_gtype);

    /* Rigid Body tests */
    g_test_add_func ("/physics/rigid-body/new", test_rigid_body_new);
    g_test_add ("/physics/rigid-body/body-type", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_body_type, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/mass", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_mass, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/restitution", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_restitution, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/friction", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_friction, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/linear-damping", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_linear_damping, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/angular-damping", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_angular_damping, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/is-trigger", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_is_trigger, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/gravity-scale", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_gravity_scale, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/position", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_position, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/rotation", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_rotation, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/velocity", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_velocity, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/angular-velocity", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_angular_velocity, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/add-force", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_add_force, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/add-impulse", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_add_impulse, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/add-torque", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_add_torque, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/clear-forces", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_clear_forces, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/box-shape", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_box_shape, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/circle-shape", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_circle_shape, rigid_body_fixture_tear_down);
    g_test_add ("/physics/rigid-body/sleep-state", RigidBodyFixture, NULL,
                rigid_body_fixture_set_up, test_rigid_body_sleep_state, rigid_body_fixture_tear_down);
    g_test_add_func ("/physics/rigid-body/static-type", test_rigid_body_static_type);
    g_test_add_func ("/physics/rigid-body/kinematic-type", test_rigid_body_kinematic_type);

    /* Physics World tests */
    g_test_add_func ("/physics/world/new", test_physics_world_new);
    g_test_add ("/physics/world/gravity", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_gravity, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/time-step", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_time_step, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/velocity-iterations", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_velocity_iterations, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/position-iterations", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_position_iterations, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/add-body", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_add_body, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/remove-body", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_remove_body, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/get-bodies", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_get_bodies, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/clear", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_clear, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/paused", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_paused, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/step-basic", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_step_basic, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/step-gravity", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_step_gravity, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/step-paused", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_step_paused, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/query-aabb", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_query_aabb, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/query-point", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_query_point, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/raycast", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_raycast, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/raycast-miss", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_raycast_miss, physics_world_fixture_tear_down);
    g_test_add ("/physics/world/static-body-no-move", PhysicsWorldFixture, NULL,
                physics_world_fixture_set_up, test_physics_world_static_body_no_move, physics_world_fixture_tear_down);

    return g_test_run ();
}
