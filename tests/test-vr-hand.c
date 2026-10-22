/* test-vr-hand.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Hand tracking: the joint math, the pinch latch's hysteresis, the
 * fallback aim ray, palm orientation, and the stub's deterministic fake
 * hands -- everything a backend derives from joints, without a headset.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

#define EPS 1e-4f

/* ------------------------------------------------------- pinch strength - */

static void
test_pinch_strength_from_distance (void)
{
    gfloat mid = 0.5f * (LRG_VR_PINCH_CLOSED_M + LRG_VR_PINCH_OPEN_M);

    g_assert_cmpfloat_with_epsilon (lrg_vr_hand_pinch_strength_from_distance (0.0f),
                                    1.0f, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_vr_hand_pinch_strength_from_distance (
                                        LRG_VR_PINCH_CLOSED_M), 1.0f, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_vr_hand_pinch_strength_from_distance (mid),
                                    0.5f, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_vr_hand_pinch_strength_from_distance (
                                        LRG_VR_PINCH_OPEN_M), 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_vr_hand_pinch_strength_from_distance (0.5f),
                                    0.0f, EPS);
    /* Unknown (a tip not tracked) is an open hand, never a click. */
    g_assert_cmpfloat_with_epsilon (lrg_vr_hand_pinch_strength_from_distance (-1.0f),
                                    0.0f, EPS);
}

static void
test_pinch_hysteresis (void)
{
    /* Strengths in the band between release and press keep the state, so
       a hand hovering at either threshold does not chatter. */
    static const struct { gfloat strength; gboolean expect; } seq[] = {
        { 0.10f, FALSE },
        { 0.80f, FALSE },   /* below press: still open */
        { 0.85f, TRUE },    /* press */
        { 0.70f, TRUE },    /* in the band: held */
        { 0.61f, TRUE },
        { 0.60f, FALSE },   /* release */
        { 0.70f, FALSE },   /* in the band: stays open */
        { 0.84f, FALSE },
        { 0.95f, TRUE },
    };
    gboolean latch = FALSE;
    guint i;

    g_assert_cmpfloat (LRG_VR_PINCH_RELEASE, <, LRG_VR_PINCH_PRESS);
    for (i = 0; i < G_N_ELEMENTS (seq); i++)
    {
        latch = lrg_vr_hand_pinch_update (latch, seq[i].strength);
        g_assert_cmpint (latch, ==, seq[i].expect);
    }
}

/* ---------------------------------------------------------- synthetic -- */

static void
test_synthetic_hand_geometry (void)
{
    LrgVRHandState st;
    gfloat x, y, z, pt[3];
    guint j;

    lrg_vr_hand_state_set_synthetic (&st, LRG_VR_HAND_RIGHT, 0.2f, -0.3f, -0.4f,
                                     0.025f, FALSE);
    g_assert_true (lrg_vr_hand_state_is_active (&st));
    for (j = 0; j < LRG_VR_HAND_JOINT_COUNT; j++)
    {
        g_assert_true (lrg_vr_hand_state_get_joint_position (&st, (LrgVRHandJoint) j,
                                                             NULL, NULL, NULL));
        g_assert_cmpfloat (lrg_vr_hand_state_get_joint_radius (&st, (LrgVRHandJoint) j),
                           >, 0.0f);
    }

    /* The wrist is where it was put; the fingers point away (-Z). */
    g_assert_true (lrg_vr_hand_state_get_joint_position (&st, LRG_VR_HAND_JOINT_WRIST,
                                                         &x, &y, &z));
    g_assert_cmpfloat_with_epsilon (x, 0.2f, EPS);
    g_assert_cmpfloat_with_epsilon (y, -0.3f, EPS);
    g_assert_cmpfloat_with_epsilon (z, -0.4f, EPS);
    lrg_vr_hand_state_get_joint_position (&st, LRG_VR_HAND_JOINT_MIDDLE_TIP, NULL, NULL, &z);
    g_assert_cmpfloat (z, <, -0.5f);

    /* The fingertips are exactly the asked-for distance apart. */
    g_assert_cmpfloat_with_epsilon (lrg_vr_hand_state_get_pinch_distance (&st),
                                    0.025f, EPS);
    g_assert_true (lrg_vr_hand_state_get_pinch_point (&st, pt));

    /* A right hand's thumb is on its left (-X) palm down; a left hand's
       on its right. */
    lrg_vr_hand_state_get_joint_position (&st, LRG_VR_HAND_JOINT_THUMB_PROXIMAL, &x,
                                          NULL, NULL);
    g_assert_cmpfloat (x, <, 0.2f);
    lrg_vr_hand_state_set_synthetic (&st, LRG_VR_HAND_LEFT, 0.0f, 0.0f, 0.0f,
                                     0.025f, FALSE);
    lrg_vr_hand_state_get_joint_position (&st, LRG_VR_HAND_JOINT_THUMB_PROXIMAL, &x,
                                          NULL, NULL);
    g_assert_cmpfloat (x, >, 0.0f);
}

static void
test_palm_orientation (void)
{
    LrgVRHandState st;
    gfloat n[3];
    guint h;

    for (h = 0; h < 2; h++)
    {
        lrg_vr_hand_state_set_synthetic (&st, (LrgVRHand) h, 0, 0, -0.4f, 0.05f, FALSE);
        g_assert_true (lrg_vr_hand_state_get_palm_normal (&st, n));
        g_assert_cmpfloat_with_epsilon (n[1], -1.0f, EPS);
        g_assert_false (lrg_vr_hand_state_is_palm_up (&st, 0.7f));

        lrg_vr_hand_state_set_synthetic (&st, (LrgVRHand) h, 0, 0, -0.4f, 0.05f, TRUE);
        g_assert_true (lrg_vr_hand_state_get_palm_normal (&st, n));
        g_assert_cmpfloat_with_epsilon (n[1], 1.0f, EPS);
        g_assert_true (lrg_vr_hand_state_is_palm_up (&st, 0.7f));
    }

    /* An untracked hand is never palm-up. */
    lrg_vr_hand_state_reset (&st);
    g_assert_false (lrg_vr_hand_state_is_palm_up (&st, 0.0f));
    g_assert_false (lrg_vr_hand_state_get_palm_normal (&st, n));
}

static void
assert_orthonormal (const gfloat *m)
{
    guint a, b;

    for (a = 0; a < 3; a++)
        for (b = 0; b < 3; b++)
        {
            gfloat d = m[a * 4] * m[b * 4] + m[a * 4 + 1] * m[b * 4 + 1]
                       + m[a * 4 + 2] * m[b * 4 + 2];
            g_assert_cmpfloat_with_epsilon (d, a == b ? 1.0f : 0.0f, 1e-3f);
        }
    g_assert_cmpfloat_with_epsilon (m[15], 1.0f, EPS);
}

static void
test_aim_from_head (void)
{
    gfloat head[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    LrgVRHandState st;
    gfloat m[16], tx, ty, tz, ix, iy, iz;

    lrg_vr_hand_state_set_synthetic (&st, LRG_VR_HAND_RIGHT, 0.2f, -0.3f, -0.4f,
                                     0.05f, FALSE);
    g_assert_false (lrg_vr_hand_state_get_aim (&st, m));   /* not derived yet */
    g_assert_true (lrg_vr_hand_state_compute_aim (&st, LRG_VR_HAND_RIGHT, head));
    g_assert_true (lrg_vr_hand_state_get_aim (&st, m));
    g_assert_false (st.aim_from_runtime);
    assert_orthonormal (m);

    /* Origin: between the thumb and index knuckles. */
    lrg_vr_hand_state_get_joint_position (&st, LRG_VR_HAND_JOINT_THUMB_PROXIMAL,
                                          &tx, &ty, &tz);
    lrg_vr_hand_state_get_joint_position (&st, LRG_VR_HAND_JOINT_INDEX_PROXIMAL,
                                          &ix, &iy, &iz);
    g_assert_cmpfloat_with_epsilon (m[12], 0.5f * (tx + ix), EPS);
    g_assert_cmpfloat_with_epsilon (m[13], 0.5f * (ty + iy), EPS);
    g_assert_cmpfloat_with_epsilon (m[14], 0.5f * (tz + iz), EPS);

    /* Direction (-Z): away from the right shoulder -- forward, and no
       farther left than straight ahead. */
    g_assert_cmpfloat (-m[10], <, -0.5f);
    g_assert_cmpfloat (-m[8], >, -0.05f);

    /* Pinching does not move the origin: the knuckles stay put. */
    {
        gfloat m2[16];

        lrg_vr_hand_state_set_synthetic (&st, LRG_VR_HAND_RIGHT, 0.2f, -0.3f, -0.4f,
                                         0.0f, FALSE);
        lrg_vr_hand_state_compute_aim (&st, LRG_VR_HAND_RIGHT, head);
        lrg_vr_hand_state_get_aim (&st, m2);
        g_assert_cmpfloat_with_epsilon (m2[12], m[12], EPS);
        g_assert_cmpfloat_with_epsilon (m2[13], m[13], EPS);
        g_assert_cmpfloat_with_epsilon (m2[14], m[14], EPS);
    }
}

static void
test_aim_without_head (void)
{
    LrgVRHandState st;
    gfloat m[16];

    /* Without a head the palm's forward axis is the ray: straight ahead. */
    lrg_vr_hand_state_set_synthetic (&st, LRG_VR_HAND_LEFT, 0, 0, -0.4f, 0.05f, FALSE);
    g_assert_true (lrg_vr_hand_state_compute_aim (&st, LRG_VR_HAND_LEFT, NULL));
    lrg_vr_hand_state_get_aim (&st, m);
    assert_orthonormal (m);
    g_assert_cmpfloat_with_epsilon (-m[8], 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (-m[9], 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (-m[10], -1.0f, EPS);

    /* An aim the runtime supplied is never replaced. */
    st.aim_from_runtime = TRUE;
    st.aim_valid = TRUE;
    st.aim[12] = 42.0f;
    g_assert_true (lrg_vr_hand_state_compute_aim (&st, LRG_VR_HAND_LEFT, NULL));
    g_assert_cmpfloat (st.aim[12], ==, 42.0f);
}

static void
test_update_pinch (void)
{
    LrgVRHandState st;

    lrg_vr_hand_state_set_synthetic (&st, LRG_VR_HAND_RIGHT, 0, 0, -0.4f, 0.01f, FALSE);
    g_assert_true (lrg_vr_hand_state_update_pinch (&st, FALSE));
    g_assert_cmpfloat_with_epsilon (lrg_vr_hand_state_get_pinch_strength (&st), 1.0f, EPS);
    g_assert_true (lrg_vr_hand_state_is_pinched (&st));

    /* The runtime's strength wins over the fingertip distance. */
    st.aim_from_runtime = TRUE;
    st.pinch_strength = 0.1f;
    g_assert_false (lrg_vr_hand_state_update_pinch (&st, TRUE));

    /* An inactive hand is open whatever came before. */
    lrg_vr_hand_state_reset (&st);
    g_assert_false (lrg_vr_hand_state_update_pinch (&st, TRUE));
    g_assert_cmpfloat (lrg_vr_hand_state_get_pinch_strength (&st), ==, 0.0f);
}

/* ------------------------------------------------------------ skeleton -- */

static void
test_joint_parents (void)
{
    guint j;

    g_assert_cmpint (lrg_vr_hand_joint_parent (LRG_VR_HAND_JOINT_WRIST), ==, -1);
    g_assert_cmpint (lrg_vr_hand_joint_parent (LRG_VR_HAND_JOINT_INDEX_TIP), ==,
                     LRG_VR_HAND_JOINT_INDEX_DISTAL);
    g_assert_cmpint (lrg_vr_hand_joint_parent (LRG_VR_HAND_JOINT_THUMB_PROXIMAL), ==,
                     LRG_VR_HAND_JOINT_THUMB_METACARPAL);
    g_assert_cmpint (lrg_vr_hand_joint_parent (LRG_VR_HAND_JOINT_RING_METACARPAL), ==,
                     LRG_VR_HAND_JOINT_WRIST);

    /* Every joint reaches the wrist, in fewer steps than there are joints
       (no cycles). */
    for (j = 0; j < LRG_VR_HAND_JOINT_COUNT; j++)
    {
        gint at = (gint) j;
        guint steps = 0;

        while (at != LRG_VR_HAND_JOINT_WRIST && steps < LRG_VR_HAND_JOINT_COUNT)
        {
            at = lrg_vr_hand_joint_parent ((LrgVRHandJoint) at);
            g_assert_cmpint (at, >=, 0);
            steps++;
        }
        g_assert_cmpint (at, ==, LRG_VR_HAND_JOINT_WRIST);
    }
}

static void
test_types (void)
{
    g_autoptr(LrgVRHandState) a = lrg_vr_hand_state_new ();
    g_autoptr(LrgVRHandState) b = NULL;
    GEnumClass *klass;
    GEnumValue *v;

    g_assert_false (lrg_vr_hand_state_is_active (a));
    lrg_vr_hand_state_set_synthetic (a, LRG_VR_HAND_LEFT, 1, 2, 3, 0.03f, TRUE);
    b = lrg_vr_hand_state_copy (a);
    g_assert_true (lrg_vr_hand_state_is_active (b));
    g_assert_cmpfloat_with_epsilon (lrg_vr_hand_state_get_pinch_distance (b), 0.03f, EPS);
    g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_VR_HAND_STATE));

    klass = g_type_class_ref (LRG_TYPE_VR_HAND_JOINT);
    g_assert_cmpuint (klass->n_values, ==, LRG_VR_HAND_JOINT_COUNT);
    v = g_enum_get_value (klass, LRG_VR_HAND_JOINT_INDEX_TIP);
    g_assert_cmpstr (v->value_nick, ==, "index-tip");
    g_type_class_unref (klass);
}

/* ------------------------------------------------------- the stub's hands */

static void
test_stub_hands (void)
{
    /* Frames of fingertip distances and the latch each must leave; the
       strength is 1 - (d - 0.015) / 0.045. */
    static const struct { gfloat d; gboolean pinched; } frames[] = {
        { 0.050f, FALSE },   /* 0.22 */
        { 0.020f, TRUE },    /* 0.89: closes */
        { 0.030f, TRUE },    /* 0.67: held in the band */
        { 0.035f, FALSE },   /* 0.56: opens */
        { 0.030f, FALSE },   /* 0.67: stays open */
        { 0.018f, TRUE },    /* 0.93: closes again */
    };
    g_autoptr(LrgVRStub) stub = lrg_vr_stub_new ();
    LrgVRService *svc = LRG_VR_SERVICE (stub);
    LrgVRHandState st;
    gfloat m[16];
    guint i;

    /* No fake hands: unsupported, and the state comes back reset. */
    g_assert_false (lrg_vr_service_is_hand_tracking_supported (svc));
    st.active = TRUE;
    g_assert_false (lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_RIGHT, &st));
    g_assert_false (st.active);

    for (i = 0; i < G_N_ELEMENTS (frames); i++)
    {
        lrg_vr_stub_set_hand (stub, LRG_VR_HAND_RIGHT, 0.2f, -0.3f, -0.4f,
                              frames[i].d, FALSE);
        g_assert_true (lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_RIGHT, &st));
        g_assert_cmpint (lrg_vr_hand_state_is_pinched (&st), ==, frames[i].pinched);
    }
    g_assert_true (lrg_vr_service_is_hand_tracking_supported (svc));
    /* The stub derives an aim, as a backend does each frame. */
    g_assert_true (lrg_vr_hand_state_get_aim (&st, m));
    /* The other hand was never given a pose. */
    g_assert_false (lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_LEFT, &st));

    /* A hand that leaves view is inactive, and comes back open. */
    lrg_vr_stub_clear_hand (stub, LRG_VR_HAND_RIGHT);
    g_assert_false (lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_RIGHT, &st));
    lrg_vr_stub_set_hand (stub, LRG_VR_HAND_RIGHT, 0, 0, -0.4f, 0.030f, FALSE);
    lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_RIGHT, &st);
    g_assert_false (lrg_vr_hand_state_is_pinched (&st));

    /* Palm up through the service. */
    lrg_vr_stub_set_hand (stub, LRG_VR_HAND_LEFT, -0.2f, -0.3f, -0.4f, 0.05f, TRUE);
    lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_LEFT, &st);
    g_assert_true (lrg_vr_hand_state_is_palm_up (&st, 0.7f));
}

static void
test_simulated_stub_hands (void)
{
    g_autoptr(LrgVRStub) sim = lrg_vr_stub_new_simulated ();
    LrgVRService *svc = LRG_VR_SERVICE (sim);
    g_autoptr(GError) error = NULL;
    gfloat head[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    gfloat a[16], b[16];
    LrgVRHandState st;

    /* Before the session starts a simulated headset shows no hands. */
    lrg_vr_stub_set_hand (sim, LRG_VR_HAND_RIGHT, 0.2f, -0.3f, -0.4f, 0.05f, FALSE);
    g_assert_true (lrg_vr_service_is_hand_tracking_supported (svc));
    g_assert_false (lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_RIGHT, &st));

    g_assert_true (lrg_vr_service_initialize (svc, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_RIGHT, &st));
    g_assert_true (lrg_vr_hand_state_get_aim (&st, a));

    /* The aim comes from the headset's pose: a head moved 1 m right of the
       hand points the ray to the left. */
    head[12] = 1.2f;
    lrg_vr_stub_set_hmd_pose (sim, head);
    lrg_vr_stub_set_hand (sim, LRG_VR_HAND_RIGHT, 0.2f, -0.3f, -0.4f, 0.05f, FALSE);
    lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_RIGHT, &st);
    lrg_vr_hand_state_get_aim (&st, b);
    g_assert_cmpfloat_with_epsilon (b[12], a[12], EPS);   /* same origin */
    g_assert_cmpfloat (-b[8], <, -a[8]);                  /* swung left */

    /* Quitting ends the session, and with it the hands. */
    lrg_vr_stub_request_quit (sim);
    g_assert_false (lrg_vr_service_get_hand_state (svc, LRG_VR_HAND_RIGHT, &st));
    g_assert_false (st.active);
}

static void
test_openxr_without_session (void)
{
    g_autoptr(LrgOpenXRService) xr = lrg_openxr_service_new ("test");
    LrgVRHandState st;

    /* Never initialised: no hands, no crash. */
    g_assert_false (lrg_vr_service_is_hand_tracking_supported (LRG_VR_SERVICE (xr)));
    g_assert_false (lrg_vr_service_get_hand_state (LRG_VR_SERVICE (xr),
                                                   LRG_VR_HAND_LEFT, &st));
    g_assert_false (lrg_openxr_service_has_hand_aim (xr));
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/vr/hand/pinch-strength", test_pinch_strength_from_distance);
    g_test_add_func ("/vr/hand/pinch-hysteresis", test_pinch_hysteresis);
    g_test_add_func ("/vr/hand/synthetic-geometry", test_synthetic_hand_geometry);
    g_test_add_func ("/vr/hand/palm-orientation", test_palm_orientation);
    g_test_add_func ("/vr/hand/aim-from-head", test_aim_from_head);
    g_test_add_func ("/vr/hand/aim-without-head", test_aim_without_head);
    g_test_add_func ("/vr/hand/update-pinch", test_update_pinch);
    g_test_add_func ("/vr/hand/joint-parents", test_joint_parents);
    g_test_add_func ("/vr/hand/types", test_types);
    g_test_add_func ("/vr/hand/stub", test_stub_hands);
    g_test_add_func ("/vr/hand/simulated-stub", test_simulated_stub_hands);
    g_test_add_func ("/vr/hand/openxr-without-session", test_openxr_without_session);

    return g_test_run ();
}
