/* test-animation.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for animation module.
 */

#define LIBREGNUM_COMPILATION 1

#include <glib.h>
#include <glib-object.h>
#include <math.h>
#include "../src/libregnum.h"

/*
 * ============================================================================
 * Fixtures
 * ============================================================================
 */

/* Skeleton fixture */
typedef struct
{
    LrgSkeleton *skeleton;
    LrgBone *root;
    LrgBone *spine;
    LrgBone *head;
} SkeletonFixture;

static void
skeleton_fixture_set_up (SkeletonFixture *fixture,
                         gconstpointer    user_data)
{
    (void) user_data;

    fixture->skeleton = lrg_skeleton_new ();

    /* Create a simple 3-bone hierarchy: root -> spine -> head */
    fixture->root = lrg_bone_new ("root", 0);
    fixture->spine = lrg_bone_new ("spine", 1);
    fixture->head = lrg_bone_new ("head", 2);

    lrg_bone_set_parent_index (fixture->spine, 0);
    lrg_bone_set_parent_index (fixture->head, 1);

    lrg_skeleton_add_bone (fixture->skeleton, fixture->root);
    lrg_skeleton_add_bone (fixture->skeleton, fixture->spine);
    lrg_skeleton_add_bone (fixture->skeleton, fixture->head);
}

static void
skeleton_fixture_tear_down (SkeletonFixture *fixture,
                            gconstpointer    user_data)
{
    (void) user_data;
    g_clear_object (&fixture->skeleton);
    /* Bones are owned by skeleton, so no need to free them */
}

/* Animator fixture */
typedef struct
{
    LrgAnimator *animator;
    LrgSkeleton *skeleton;
    LrgAnimationClip *clip;
} AnimatorFixture;

static void
animator_fixture_set_up (AnimatorFixture *fixture,
                         gconstpointer    user_data)
{
    LrgBone *root = NULL;

    (void) user_data;

    /* Create skeleton */
    fixture->skeleton = lrg_skeleton_new ();
    root = lrg_bone_new ("root", 0);
    lrg_skeleton_add_bone (fixture->skeleton, root);

    /* Create clip */
    fixture->clip = lrg_animation_clip_new ("idle");
    lrg_animation_clip_set_duration (fixture->clip, 1.0f);

    /* Create animator */
    fixture->animator = lrg_animator_new (fixture->skeleton);
    lrg_animator_add_clip (fixture->animator, "idle", fixture->clip);
}

static void
animator_fixture_tear_down (AnimatorFixture *fixture,
                            gconstpointer    user_data)
{
    (void) user_data;
    g_clear_object (&fixture->animator);
    g_clear_object (&fixture->skeleton);
    g_clear_object (&fixture->clip);
}

/* State machine fixture */
typedef struct
{
    LrgAnimationStateMachine *machine;
    LrgAnimationState *idle_state;
    LrgAnimationState *walk_state;
} StateMachineFixture;

static void
state_machine_fixture_set_up (StateMachineFixture *fixture,
                              gconstpointer        user_data)
{
    (void) user_data;

    fixture->machine = lrg_animation_state_machine_new ();

    /* Create states */
    fixture->idle_state = lrg_animation_state_new ("idle");
    fixture->walk_state = lrg_animation_state_new ("walk");

    lrg_animation_state_machine_add_state (fixture->machine, fixture->idle_state);
    lrg_animation_state_machine_add_state (fixture->machine, fixture->walk_state);
    lrg_animation_state_machine_set_default_state (fixture->machine, "idle");
}

static void
state_machine_fixture_tear_down (StateMachineFixture *fixture,
                                 gconstpointer        user_data)
{
    (void) user_data;
    g_clear_object (&fixture->machine);
    /* States are owned by machine */
}

/*
 * ============================================================================
 * LrgBonePose Tests
 * ============================================================================
 */

static void
test_bone_pose_new (void)
{
    g_autoptr(LrgBonePose) pose = lrg_bone_pose_new ();

    g_assert_nonnull (pose);

    /* Should be identity transform */
    g_assert_cmpfloat_with_epsilon (pose->position_x, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pose->position_y, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pose->position_z, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pose->rotation_w, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pose->scale_x, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pose->scale_y, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pose->scale_z, 1.0f, 0.001f);
}

static void
test_bone_pose_copy (void)
{
    g_autoptr(LrgBonePose) pose = NULL;
    g_autoptr(LrgBonePose) copy = NULL;

    pose = lrg_bone_pose_new ();
    lrg_bone_pose_set_position (pose, 1.0f, 2.0f, 3.0f);

    copy = lrg_bone_pose_copy (pose);

    g_assert_nonnull (copy);
    g_assert_cmpfloat_with_epsilon (copy->position_x, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (copy->position_y, 2.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (copy->position_z, 3.0f, 0.001f);
}

static void
test_bone_pose_identity (void)
{
    g_autoptr(LrgBonePose) pose = lrg_bone_pose_new ();
    lrg_bone_pose_set_position (pose, 5.0f, 5.0f, 5.0f);
    lrg_bone_pose_set_uniform_scale (pose, 2.0f);

    lrg_bone_pose_set_identity (pose);

    g_assert_cmpfloat_with_epsilon (pose->position_x, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pose->scale_x, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (pose->rotation_w, 1.0f, 0.001f);
}

static void
test_bone_pose_lerp (void)
{
    g_autoptr(LrgBonePose) a = NULL;
    g_autoptr(LrgBonePose) b = NULL;
    g_autoptr(LrgBonePose) result = NULL;

    a = lrg_bone_pose_new ();
    b = lrg_bone_pose_new ();

    lrg_bone_pose_set_position (a, 0.0f, 0.0f, 0.0f);
    lrg_bone_pose_set_position (b, 10.0f, 20.0f, 30.0f);

    result = lrg_bone_pose_lerp (a, b, 0.5f);

    g_assert_cmpfloat_with_epsilon (result->position_x, 5.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (result->position_y, 10.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (result->position_z, 15.0f, 0.001f);
}

static void
test_bone_pose_equal (void)
{
    g_autoptr(LrgBonePose) a = lrg_bone_pose_new ();
    g_autoptr(LrgBonePose) b = lrg_bone_pose_new ();

    g_assert_true (lrg_bone_pose_equal (a, b));

    lrg_bone_pose_set_position (a, 1.0f, 0.0f, 0.0f);
    g_assert_false (lrg_bone_pose_equal (a, b));
}

/*
 * ============================================================================
 * LrgBone Tests
 * ============================================================================
 */

static void
test_bone_new (void)
{
    g_autoptr(LrgBone) bone = lrg_bone_new ("test_bone", 5);

    g_assert_nonnull (bone);
    g_assert_cmpstr (lrg_bone_get_name (bone), ==, "test_bone");
    g_assert_cmpint (lrg_bone_get_index (bone), ==, 5);
}

static void
test_bone_parent (void)
{
    g_autoptr(LrgBone) root = lrg_bone_new ("root", 0);
    g_autoptr(LrgBone) child = lrg_bone_new ("child", 1);

    g_assert_true (lrg_bone_is_root (root));
    g_assert_cmpint (lrg_bone_get_parent_index (root), ==, -1);

    lrg_bone_set_parent_index (child, 0);
    g_assert_false (lrg_bone_is_root (child));
    g_assert_cmpint (lrg_bone_get_parent_index (child), ==, 0);
}

static void
test_bone_bind_pose (void)
{
    g_autoptr(LrgBone) bone = NULL;
    g_autoptr(LrgBonePose) pose = NULL;
    const LrgBonePose *bind = NULL;

    bone = lrg_bone_new ("bone", 0);
    pose = lrg_bone_pose_new ();

    lrg_bone_pose_set_position (pose, 1.0f, 2.0f, 3.0f);
    lrg_bone_set_bind_pose (bone, pose);

    bind = lrg_bone_get_bind_pose (bone);
    g_assert_cmpfloat_with_epsilon (bind->position_x, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (bind->position_y, 2.0f, 0.001f);
}

static void
test_bone_length (void)
{
    g_autoptr(LrgBone) bone = lrg_bone_new ("bone", 0);

    lrg_bone_set_length (bone, 5.0f);
    g_assert_cmpfloat_with_epsilon (lrg_bone_get_length (bone), 5.0f, 0.001f);
}

/*
 * ============================================================================
 * LrgSkeleton Tests
 * ============================================================================
 */

static void
test_skeleton_new (void)
{
    g_autoptr(LrgSkeleton) skeleton = lrg_skeleton_new ();

    g_assert_nonnull (skeleton);
    g_assert_cmpuint (lrg_skeleton_get_bone_count (skeleton), ==, 0);
}

static void
test_skeleton_add_bone (SkeletonFixture *fixture,
                        gconstpointer    user_data)
{
    (void) user_data;

    g_assert_cmpuint (lrg_skeleton_get_bone_count (fixture->skeleton), ==, 3);
}

static void
test_skeleton_find_bone (SkeletonFixture *fixture,
                         gconstpointer    user_data)
{
    LrgBone *found = NULL;

    (void) user_data;

    found = lrg_skeleton_get_bone (fixture->skeleton, 1);
    g_assert_nonnull (found);
    g_assert_cmpstr (lrg_bone_get_name (found), ==, "spine");

    found = lrg_skeleton_get_bone_by_name (fixture->skeleton, "head");
    g_assert_nonnull (found);
    g_assert_cmpint (lrg_bone_get_index (found), ==, 2);
}

static void
test_skeleton_root_bones (SkeletonFixture *fixture,
                          gconstpointer    user_data)
{
    GList *roots = NULL;
    LrgBone *root = NULL;

    (void) user_data;

    roots = lrg_skeleton_get_root_bones (fixture->skeleton);
    g_assert_cmpuint (g_list_length (roots), ==, 1);

    root = roots->data;
    g_assert_cmpstr (lrg_bone_get_name (root), ==, "root");
    g_list_free (roots);
}

static void
test_skeleton_children (SkeletonFixture *fixture,
                        gconstpointer    user_data)
{
    GList *children = NULL;
    LrgBone *child = NULL;

    (void) user_data;

    children = lrg_skeleton_get_children (fixture->skeleton, fixture->root);
    g_assert_cmpuint (g_list_length (children), ==, 1);

    child = children->data;
    g_assert_cmpstr (lrg_bone_get_name (child), ==, "spine");
    g_list_free (children);
}

static void
test_skeleton_reset_to_bind (SkeletonFixture *fixture,
                             gconstpointer    user_data)
{
    g_autoptr(LrgBonePose) pose = NULL;
    const LrgBonePose *local = NULL;

    (void) user_data;
    pose = lrg_bone_pose_new ();

    lrg_bone_pose_set_position (pose, 100.0f, 100.0f, 100.0f);
    lrg_skeleton_set_pose (fixture->skeleton, 0, pose);

    lrg_skeleton_reset_to_bind (fixture->skeleton);

    local = lrg_bone_get_local_pose (fixture->root);
    g_assert_cmpfloat_with_epsilon (local->position_x, 0.0f, 0.001f);
}

/*
 * ============================================================================
 * LrgAnimationClip Tests
 * ============================================================================
 */

static void
test_clip_new (void)
{
    g_autoptr(LrgAnimationClip) clip = lrg_animation_clip_new ("walk");

    g_assert_nonnull (clip);
    g_assert_cmpstr (lrg_animation_clip_get_name (clip), ==, "walk");
}

static void
test_clip_duration (void)
{
    g_autoptr(LrgAnimationClip) clip = lrg_animation_clip_new ("test");

    lrg_animation_clip_set_duration (clip, 2.5f);
    g_assert_cmpfloat_with_epsilon (lrg_animation_clip_get_duration (clip), 2.5f, 0.001f);
}

static void
test_clip_loop_mode (void)
{
    g_autoptr(LrgAnimationClip) clip = lrg_animation_clip_new ("test");

    lrg_animation_clip_set_loop_mode (clip, LRG_ANIMATION_LOOP_REPEAT);
    g_assert_cmpint (lrg_animation_clip_get_loop_mode (clip), ==, LRG_ANIMATION_LOOP_REPEAT);

    lrg_animation_clip_set_loop_mode (clip, LRG_ANIMATION_LOOP_PINGPONG);
    g_assert_cmpint (lrg_animation_clip_get_loop_mode (clip), ==, LRG_ANIMATION_LOOP_PINGPONG);
}

static void
test_clip_add_track (void)
{
    g_autoptr(LrgAnimationClip) clip = lrg_animation_clip_new ("test");

    guint track0 = lrg_animation_clip_add_track (clip, "bone1");
    guint track1 = lrg_animation_clip_add_track (clip, "bone2");

    g_assert_cmpuint (track0, ==, 0);
    g_assert_cmpuint (track1, ==, 1);
    g_assert_cmpuint (lrg_animation_clip_get_track_count (clip), ==, 2);
    g_assert_cmpstr (lrg_animation_clip_get_track_bone_name (clip, 0), ==, "bone1");
}

/*
 * ============================================================================
 * LrgAnimator Tests
 * ============================================================================
 */

static void
test_animator_new (void)
{
    g_autoptr(LrgAnimator) animator = lrg_animator_new (NULL);

    g_assert_nonnull (animator);
    g_assert_cmpint (lrg_animator_get_state (animator), ==, LRG_ANIMATOR_STOPPED);
}

static void
test_animator_play (AnimatorFixture *fixture,
                    gconstpointer    user_data)
{
    (void) user_data;

    lrg_animator_play (fixture->animator, "idle");

    g_assert_cmpint (lrg_animator_get_state (fixture->animator), ==, LRG_ANIMATOR_PLAYING);
    g_assert_cmpstr (lrg_animator_get_current_clip (fixture->animator), ==, "idle");
}

static void
test_animator_pause (AnimatorFixture *fixture,
                     gconstpointer    user_data)
{
    (void) user_data;

    lrg_animator_play (fixture->animator, "idle");
    lrg_animator_pause (fixture->animator);

    g_assert_cmpint (lrg_animator_get_state (fixture->animator), ==, LRG_ANIMATOR_PAUSED);
}

static void
test_animator_stop (AnimatorFixture *fixture,
                    gconstpointer    user_data)
{
    (void) user_data;

    lrg_animator_play (fixture->animator, "idle");
    lrg_animator_stop (fixture->animator);

    g_assert_cmpint (lrg_animator_get_state (fixture->animator), ==, LRG_ANIMATOR_STOPPED);
}

static void
test_animator_speed (AnimatorFixture *fixture,
                     gconstpointer    user_data)
{
    (void) user_data;

    lrg_animator_set_speed (fixture->animator, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_animator_get_speed (fixture->animator), 2.0f, 0.001f);
}

static void
test_animator_time (AnimatorFixture *fixture,
                    gconstpointer    user_data)
{
    (void) user_data;

    lrg_animator_play (fixture->animator, "idle");
    lrg_animator_set_time (fixture->animator, 0.5f);

    g_assert_cmpfloat_with_epsilon (lrg_animator_get_time (fixture->animator), 0.5f, 0.001f);
}

/*
 * ============================================================================
 * LrgAnimationStateMachine Tests
 * ============================================================================
 */

static void
test_state_machine_new (void)
{
    g_autoptr(LrgAnimationStateMachine) machine = lrg_animation_state_machine_new ();

    g_assert_nonnull (machine);
    g_assert_false (lrg_animation_state_machine_is_running (machine));
}

static void
test_state_machine_add_state (StateMachineFixture *fixture,
                              gconstpointer        user_data)
{
    LrgAnimationState *state = NULL;
    GList *states = NULL;

    (void) user_data;

    state = lrg_animation_state_machine_get_state (fixture->machine, "idle");
    g_assert_nonnull (state);

    states = lrg_animation_state_machine_get_states (fixture->machine);
    g_assert_cmpuint (g_list_length (states), ==, 2);
    g_list_free (states);
}

static void
test_state_machine_default_state (StateMachineFixture *fixture,
                                  gconstpointer        user_data)
{
    const gchar *default_state = NULL;

    (void) user_data;

    default_state = lrg_animation_state_machine_get_default_state (fixture->machine);
    g_assert_cmpstr (default_state, ==, "idle");
}

static void
test_state_machine_parameters (StateMachineFixture *fixture,
                               gconstpointer        user_data)
{
    (void) user_data;

    /* Float parameter */
    lrg_animation_state_machine_set_float (fixture->machine, "speed", 2.5f);
    g_assert_cmpfloat_with_epsilon (
        lrg_animation_state_machine_get_float (fixture->machine, "speed"),
        2.5f, 0.001f);

    /* Bool parameter */
    lrg_animation_state_machine_set_bool (fixture->machine, "grounded", TRUE);
    g_assert_true (lrg_animation_state_machine_get_bool (fixture->machine, "grounded"));
}

static void
test_state_machine_start_stop (StateMachineFixture *fixture,
                               gconstpointer        user_data)
{
    const gchar *current = NULL;

    (void) user_data;

    lrg_animation_state_machine_start (fixture->machine);
    g_assert_true (lrg_animation_state_machine_is_running (fixture->machine));

    current = lrg_animation_state_machine_get_current_state_name (fixture->machine);
    g_assert_cmpstr (current, ==, "idle");

    lrg_animation_state_machine_stop (fixture->machine);
    g_assert_false (lrg_animation_state_machine_is_running (fixture->machine));
}

static void
test_state_machine_force_state (StateMachineFixture *fixture,
                                gconstpointer        user_data)
{
    const gchar *current = NULL;

    (void) user_data;

    lrg_animation_state_machine_start (fixture->machine);
    lrg_animation_state_machine_force_state (fixture->machine, "walk");

    current = lrg_animation_state_machine_get_current_state_name (fixture->machine);
    g_assert_cmpstr (current, ==, "walk");
}

/*
 * ============================================================================
 * LrgIKSolver Tests
 * ============================================================================
 */

static void
test_ik_solver_fabrik_new (void)
{
    g_autoptr(LrgIKSolverFABRIK) solver = lrg_ik_solver_fabrik_new ();

    g_assert_nonnull (solver);
    g_assert_true (LRG_IS_IK_SOLVER (solver));
    g_assert_true (lrg_ik_solver_supports_chain_length (LRG_IK_SOLVER (solver), 5));
}

static void
test_ik_solver_ccd_new (void)
{
    g_autoptr(LrgIKSolverCCD) solver = lrg_ik_solver_ccd_new ();

    g_assert_nonnull (solver);
    g_assert_true (LRG_IS_IK_SOLVER (solver));
}

static void
test_ik_solver_two_bone_new (void)
{
    g_autoptr(LrgIKSolverTwoBone) solver = lrg_ik_solver_two_bone_new ();

    g_assert_nonnull (solver);
    g_assert_true (lrg_ik_solver_supports_chain_length (LRG_IK_SOLVER (solver), 2));
    g_assert_false (lrg_ik_solver_supports_chain_length (LRG_IK_SOLVER (solver), 3));
}

static void
test_ik_solver_look_at_new (void)
{
    g_autoptr(LrgIKSolverLookAt) solver = lrg_ik_solver_look_at_new ();

    g_assert_nonnull (solver);
    g_assert_true (lrg_ik_solver_supports_chain_length (LRG_IK_SOLVER (solver), 1));
}

static void
test_ik_solver_look_at_up_vector (void)
{
    g_autoptr(LrgIKSolverLookAt) solver = lrg_ik_solver_look_at_new ();
    gfloat x, y, z;

    lrg_ik_solver_look_at_set_up_vector (solver, 0.0f, 1.0f, 0.0f);
    lrg_ik_solver_look_at_get_up_vector (solver, &x, &y, &z);

    g_assert_cmpfloat_with_epsilon (x, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (y, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (z, 0.0f, 0.001f);
}

/*
 * ============================================================================
 * Main
 * ============================================================================
 */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgBonePose tests */
    g_test_add_func ("/animation/bone-pose/new", test_bone_pose_new);
    g_test_add_func ("/animation/bone-pose/copy", test_bone_pose_copy);
    g_test_add_func ("/animation/bone-pose/identity", test_bone_pose_identity);
    g_test_add_func ("/animation/bone-pose/lerp", test_bone_pose_lerp);
    g_test_add_func ("/animation/bone-pose/equal", test_bone_pose_equal);

    /* LrgBone tests */
    g_test_add_func ("/animation/bone/new", test_bone_new);
    g_test_add_func ("/animation/bone/parent", test_bone_parent);
    g_test_add_func ("/animation/bone/bind-pose", test_bone_bind_pose);
    g_test_add_func ("/animation/bone/length", test_bone_length);

    /* LrgSkeleton tests */
    g_test_add_func ("/animation/skeleton/new", test_skeleton_new);
    g_test_add ("/animation/skeleton/add-bone", SkeletonFixture, NULL,
                skeleton_fixture_set_up, test_skeleton_add_bone, skeleton_fixture_tear_down);
    g_test_add ("/animation/skeleton/find-bone", SkeletonFixture, NULL,
                skeleton_fixture_set_up, test_skeleton_find_bone, skeleton_fixture_tear_down);
    g_test_add ("/animation/skeleton/root-bones", SkeletonFixture, NULL,
                skeleton_fixture_set_up, test_skeleton_root_bones, skeleton_fixture_tear_down);
    g_test_add ("/animation/skeleton/children", SkeletonFixture, NULL,
                skeleton_fixture_set_up, test_skeleton_children, skeleton_fixture_tear_down);
    g_test_add ("/animation/skeleton/reset-to-bind", SkeletonFixture, NULL,
                skeleton_fixture_set_up, test_skeleton_reset_to_bind, skeleton_fixture_tear_down);

    /* LrgAnimationClip tests */
    g_test_add_func ("/animation/clip/new", test_clip_new);
    g_test_add_func ("/animation/clip/duration", test_clip_duration);
    g_test_add_func ("/animation/clip/loop-mode", test_clip_loop_mode);
    g_test_add_func ("/animation/clip/add-track", test_clip_add_track);

    /* LrgAnimator tests */
    g_test_add_func ("/animation/animator/new", test_animator_new);
    g_test_add ("/animation/animator/play", AnimatorFixture, NULL,
                animator_fixture_set_up, test_animator_play, animator_fixture_tear_down);
    g_test_add ("/animation/animator/pause", AnimatorFixture, NULL,
                animator_fixture_set_up, test_animator_pause, animator_fixture_tear_down);
    g_test_add ("/animation/animator/stop", AnimatorFixture, NULL,
                animator_fixture_set_up, test_animator_stop, animator_fixture_tear_down);
    g_test_add ("/animation/animator/speed", AnimatorFixture, NULL,
                animator_fixture_set_up, test_animator_speed, animator_fixture_tear_down);
    g_test_add ("/animation/animator/time", AnimatorFixture, NULL,
                animator_fixture_set_up, test_animator_time, animator_fixture_tear_down);

    /* LrgAnimationStateMachine tests */
    g_test_add_func ("/animation/state-machine/new", test_state_machine_new);
    g_test_add ("/animation/state-machine/add-state", StateMachineFixture, NULL,
                state_machine_fixture_set_up, test_state_machine_add_state, state_machine_fixture_tear_down);
    g_test_add ("/animation/state-machine/default-state", StateMachineFixture, NULL,
                state_machine_fixture_set_up, test_state_machine_default_state, state_machine_fixture_tear_down);
    g_test_add ("/animation/state-machine/parameters", StateMachineFixture, NULL,
                state_machine_fixture_set_up, test_state_machine_parameters, state_machine_fixture_tear_down);
    g_test_add ("/animation/state-machine/start-stop", StateMachineFixture, NULL,
                state_machine_fixture_set_up, test_state_machine_start_stop, state_machine_fixture_tear_down);
    g_test_add ("/animation/state-machine/force-state", StateMachineFixture, NULL,
                state_machine_fixture_set_up, test_state_machine_force_state, state_machine_fixture_tear_down);

    /* LrgIKSolver tests */
    g_test_add_func ("/animation/ik-solver/fabrik/new", test_ik_solver_fabrik_new);
    g_test_add_func ("/animation/ik-solver/ccd/new", test_ik_solver_ccd_new);
    g_test_add_func ("/animation/ik-solver/two-bone/new", test_ik_solver_two_bone_new);
    g_test_add_func ("/animation/ik-solver/look-at/new", test_ik_solver_look_at_new);
    g_test_add_func ("/animation/ik-solver/look-at/up-vector", test_ik_solver_look_at_up_vector);

    return g_test_run ();
}
