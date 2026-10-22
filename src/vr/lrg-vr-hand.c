/* lrg-vr-hand.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Hand-tracking value type and the pure arithmetic on it.  See
 * lrg-vr-hand.h.  Nothing here touches a runtime: backends fill the joint
 * arrays, then call _compute_aim() and _update_pinch() so every backend
 * derives aim and pinch the same way.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-vr-hand.h"

/**
 * SECTION:lrg-vr-hand
 * @title: LrgVRHandState
 * @short_description: Articulated hand tracking state
 *
 * #LrgVRHandState holds one tracked hand for one frame: the 26 joints of
 * the OpenXR hand skeleton (#LrgVRHandJoint), a pointer ray, and a pinch.
 * Get one from lrg_vr_service_get_hand_state().
 *
 * The pinch is a latch with hysteresis over a 0..1 strength: the runtime's
 * own strength where it reports one (XR_FB_hand_tracking_aim), otherwise
 * one derived from the distance between the thumb and index tips
 * (lrg_vr_hand_pinch_strength_from_distance()).  The latch closes at
 * %LRG_VR_PINCH_PRESS and opens at %LRG_VR_PINCH_RELEASE, so a hand held
 * at the threshold does not produce a stream of clicks.
 *
 * Since: 1.0
 */

G_DEFINE_BOXED_TYPE (LrgVRHandState, lrg_vr_hand_state,
                     lrg_vr_hand_state_copy, lrg_vr_hand_state_free)

GType
lrg_vr_hand_joint_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_VR_HAND_JOINT_PALM, "LRG_VR_HAND_JOINT_PALM", "palm" },
            { LRG_VR_HAND_JOINT_WRIST, "LRG_VR_HAND_JOINT_WRIST", "wrist" },
            { LRG_VR_HAND_JOINT_THUMB_METACARPAL, "LRG_VR_HAND_JOINT_THUMB_METACARPAL", "thumb-metacarpal" },
            { LRG_VR_HAND_JOINT_THUMB_PROXIMAL, "LRG_VR_HAND_JOINT_THUMB_PROXIMAL", "thumb-proximal" },
            { LRG_VR_HAND_JOINT_THUMB_DISTAL, "LRG_VR_HAND_JOINT_THUMB_DISTAL", "thumb-distal" },
            { LRG_VR_HAND_JOINT_THUMB_TIP, "LRG_VR_HAND_JOINT_THUMB_TIP", "thumb-tip" },
            { LRG_VR_HAND_JOINT_INDEX_METACARPAL, "LRG_VR_HAND_JOINT_INDEX_METACARPAL", "index-metacarpal" },
            { LRG_VR_HAND_JOINT_INDEX_PROXIMAL, "LRG_VR_HAND_JOINT_INDEX_PROXIMAL", "index-proximal" },
            { LRG_VR_HAND_JOINT_INDEX_INTERMEDIATE, "LRG_VR_HAND_JOINT_INDEX_INTERMEDIATE", "index-intermediate" },
            { LRG_VR_HAND_JOINT_INDEX_DISTAL, "LRG_VR_HAND_JOINT_INDEX_DISTAL", "index-distal" },
            { LRG_VR_HAND_JOINT_INDEX_TIP, "LRG_VR_HAND_JOINT_INDEX_TIP", "index-tip" },
            { LRG_VR_HAND_JOINT_MIDDLE_METACARPAL, "LRG_VR_HAND_JOINT_MIDDLE_METACARPAL", "middle-metacarpal" },
            { LRG_VR_HAND_JOINT_MIDDLE_PROXIMAL, "LRG_VR_HAND_JOINT_MIDDLE_PROXIMAL", "middle-proximal" },
            { LRG_VR_HAND_JOINT_MIDDLE_INTERMEDIATE, "LRG_VR_HAND_JOINT_MIDDLE_INTERMEDIATE", "middle-intermediate" },
            { LRG_VR_HAND_JOINT_MIDDLE_DISTAL, "LRG_VR_HAND_JOINT_MIDDLE_DISTAL", "middle-distal" },
            { LRG_VR_HAND_JOINT_MIDDLE_TIP, "LRG_VR_HAND_JOINT_MIDDLE_TIP", "middle-tip" },
            { LRG_VR_HAND_JOINT_RING_METACARPAL, "LRG_VR_HAND_JOINT_RING_METACARPAL", "ring-metacarpal" },
            { LRG_VR_HAND_JOINT_RING_PROXIMAL, "LRG_VR_HAND_JOINT_RING_PROXIMAL", "ring-proximal" },
            { LRG_VR_HAND_JOINT_RING_INTERMEDIATE, "LRG_VR_HAND_JOINT_RING_INTERMEDIATE", "ring-intermediate" },
            { LRG_VR_HAND_JOINT_RING_DISTAL, "LRG_VR_HAND_JOINT_RING_DISTAL", "ring-distal" },
            { LRG_VR_HAND_JOINT_RING_TIP, "LRG_VR_HAND_JOINT_RING_TIP", "ring-tip" },
            { LRG_VR_HAND_JOINT_LITTLE_METACARPAL, "LRG_VR_HAND_JOINT_LITTLE_METACARPAL", "little-metacarpal" },
            { LRG_VR_HAND_JOINT_LITTLE_PROXIMAL, "LRG_VR_HAND_JOINT_LITTLE_PROXIMAL", "little-proximal" },
            { LRG_VR_HAND_JOINT_LITTLE_INTERMEDIATE, "LRG_VR_HAND_JOINT_LITTLE_INTERMEDIATE", "little-intermediate" },
            { LRG_VR_HAND_JOINT_LITTLE_DISTAL, "LRG_VR_HAND_JOINT_LITTLE_DISTAL", "little-distal" },
            { LRG_VR_HAND_JOINT_LITTLE_TIP, "LRG_VR_HAND_JOINT_LITTLE_TIP", "little-tip" },
            { 0, NULL, NULL }
        };
        GType id = g_enum_register_static (g_intern_static_string ("LrgVRHandJoint"),
                                           values);
        g_once_init_leave (&type_id, id);
    }
    return type_id;
}

/* ----------------------------------------------------------------- helpers */

static void
identity (gfloat *m)
{
    gint i;

    for (i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

static gboolean
joint_ok (const LrgVRHandState *s,
          gint                  joint)
{
    return joint >= 0 && joint < LRG_VR_HAND_JOINT_COUNT
           && (s->valid & (1u << joint)) != 0;
}

static const gfloat *
joint_pos (const LrgVRHandState *s,
           gint                  joint)
{
    return &s->positions[joint * 3];
}

static gfloat
v3_len (const gfloat *v)
{
    return sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static gboolean
v3_normalize (gfloat *v)
{
    gfloat l = v3_len (v);

    if (l < 1e-6f)
        return FALSE;
    v[0] /= l;
    v[1] /= l;
    v[2] /= l;
    return TRUE;
}

static void
v3_cross (const gfloat *a,
          const gfloat *b,
          gfloat       *out)
{
    gfloat x = a[1] * b[2] - a[2] * b[1];
    gfloat y = a[2] * b[0] - a[0] * b[2];
    gfloat z = a[0] * b[1] - a[1] * b[0];

    out[0] = x;
    out[1] = y;
    out[2] = z;
}

/* Rotate V by the unit quaternion Q (x y z w): v' = v + 2w(q x v) +
   2 q x (q x v). */
static void
quat_rotate (const gfloat *q,
             const gfloat *v,
             gfloat       *out)
{
    gfloat t[3], u[3];

    v3_cross (q, v, t);
    t[0] *= 2.0f;
    t[1] *= 2.0f;
    t[2] *= 2.0f;
    v3_cross (q, t, u);
    out[0] = v[0] + q[3] * t[0] + u[0];
    out[1] = v[1] + q[3] * t[1] + u[1];
    out[2] = v[2] + q[3] * t[2] + u[2];
}

/* A pose whose -Z axis is DIR (unit) and translation ORIGIN, with its +Y
   as near world up as DIR allows. */
static void
pose_from_ray (const gfloat *origin,
               const gfloat *dir,
               gfloat       *m)
{
    gfloat z[3], x[3], y[3];
    static const gfloat up[3] = { 0.0f, 1.0f, 0.0f };
    static const gfloat side[3] = { 1.0f, 0.0f, 0.0f };

    z[0] = -dir[0];
    z[1] = -dir[1];
    z[2] = -dir[2];
    v3_cross (up, z, x);
    if (!v3_normalize (x))
    {
        /* Pointing straight up or down: any horizontal right will do. */
        v3_cross (z, side, y);
        v3_normalize (y);
        v3_cross (y, z, x);
        v3_normalize (x);
    }
    v3_cross (z, x, y);

    m[0] = x[0];  m[1] = x[1];  m[2] = x[2];  m[3] = 0.0f;
    m[4] = y[0];  m[5] = y[1];  m[6] = y[2];  m[7] = 0.0f;
    m[8] = z[0];  m[9] = z[1];  m[10] = z[2]; m[11] = 0.0f;
    m[12] = origin[0];
    m[13] = origin[1];
    m[14] = origin[2];
    m[15] = 1.0f;
}

/* ------------------------------------------------------------- boxed type */

LrgVRHandState *
lrg_vr_hand_state_new (void)
{
    LrgVRHandState *self = g_new0 (LrgVRHandState, 1);

    lrg_vr_hand_state_reset (self);
    return self;
}

LrgVRHandState *
lrg_vr_hand_state_copy (const LrgVRHandState *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return g_memdup2 (self, sizeof *self);
}

void
lrg_vr_hand_state_free (LrgVRHandState *self)
{
    g_free (self);
}

void
lrg_vr_hand_state_reset (LrgVRHandState *self)
{
    gint j;

    g_return_if_fail (self != NULL);

    memset (self, 0, sizeof *self);
    for (j = 0; j < LRG_VR_HAND_JOINT_COUNT; j++)
        self->orientations[j * 4 + 3] = 1.0f;
    identity (self->aim);
}

/* ---------------------------------------------------------------- getters */

gboolean
lrg_vr_hand_state_is_active (const LrgVRHandState *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->active;
}

gboolean
lrg_vr_hand_state_get_joint_position (const LrgVRHandState *self,
                                      LrgVRHandJoint        joint,
                                      gfloat               *x,
                                      gfloat               *y,
                                      gfloat               *z)
{
    const gfloat *p;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail ((gint) joint >= 0
                          && (gint) joint < LRG_VR_HAND_JOINT_COUNT, FALSE);

    p = joint_pos (self, (gint) joint);
    if (x != NULL)
        *x = p[0];
    if (y != NULL)
        *y = p[1];
    if (z != NULL)
        *z = p[2];
    return self->active && joint_ok (self, (gint) joint);
}

gfloat
lrg_vr_hand_state_get_joint_radius (const LrgVRHandState *self,
                                    LrgVRHandJoint        joint)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    g_return_val_if_fail ((gint) joint >= 0
                          && (gint) joint < LRG_VR_HAND_JOINT_COUNT, 0.0f);
    return self->radii[joint];
}

gboolean
lrg_vr_hand_state_get_aim (const LrgVRHandState *self,
                           gfloat               *matrix)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (matrix != NULL, FALSE);

    if (!self->active || !self->aim_valid)
    {
        identity (matrix);
        return FALSE;
    }
    memcpy (matrix, self->aim, 16 * sizeof (gfloat));
    return TRUE;
}

gfloat
lrg_vr_hand_state_get_pinch_strength (const LrgVRHandState *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->active ? self->pinch_strength : 0.0f;
}

gboolean
lrg_vr_hand_state_is_pinched (const LrgVRHandState *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->active && self->pinched;
}

gfloat
lrg_vr_hand_state_get_pinch_distance (const LrgVRHandState *self)
{
    const gfloat *a, *b;
    gfloat d[3];

    g_return_val_if_fail (self != NULL, -1.0f);

    if (!self->active
        || !joint_ok (self, LRG_VR_HAND_JOINT_THUMB_TIP)
        || !joint_ok (self, LRG_VR_HAND_JOINT_INDEX_TIP))
        return -1.0f;
    a = joint_pos (self, LRG_VR_HAND_JOINT_THUMB_TIP);
    b = joint_pos (self, LRG_VR_HAND_JOINT_INDEX_TIP);
    d[0] = a[0] - b[0];
    d[1] = a[1] - b[1];
    d[2] = a[2] - b[2];
    return v3_len (d);
}

gboolean
lrg_vr_hand_state_get_pinch_point (const LrgVRHandState *self,
                                   gfloat               *point)
{
    const gfloat *a, *b;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (point != NULL, FALSE);

    if (!self->active
        || !joint_ok (self, LRG_VR_HAND_JOINT_THUMB_TIP)
        || !joint_ok (self, LRG_VR_HAND_JOINT_INDEX_TIP))
        return FALSE;
    a = joint_pos (self, LRG_VR_HAND_JOINT_THUMB_TIP);
    b = joint_pos (self, LRG_VR_HAND_JOINT_INDEX_TIP);
    point[0] = 0.5f * (a[0] + b[0]);
    point[1] = 0.5f * (a[1] + b[1]);
    point[2] = 0.5f * (a[2] + b[2]);
    return TRUE;
}

gboolean
lrg_vr_hand_state_get_palm_normal (const LrgVRHandState *self,
                                   gfloat               *normal)
{
    static const gfloat down[3] = { 0.0f, -1.0f, 0.0f };

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (normal != NULL, FALSE);

    if (!self->active || !joint_ok (self, LRG_VR_HAND_JOINT_PALM))
        return FALSE;
    /* The palm faces its joint's -Y. */
    quat_rotate (&self->orientations[LRG_VR_HAND_JOINT_PALM * 4], down, normal);
    return v3_normalize (normal);
}

gboolean
lrg_vr_hand_state_is_palm_up (const LrgVRHandState *self,
                              gfloat                threshold)
{
    gfloat n[3];

    g_return_val_if_fail (self != NULL, FALSE);

    if (!lrg_vr_hand_state_get_palm_normal (self, n))
        return FALSE;
    return n[1] >= threshold;
}

/* ------------------------------------------------------- derived: pinch -- */

gfloat
lrg_vr_hand_pinch_strength_from_distance (gfloat distance)
{
    gfloat s;

    if (distance < 0.0f)
        return 0.0f;
    if (distance <= LRG_VR_PINCH_CLOSED_M)
        return 1.0f;
    if (distance >= LRG_VR_PINCH_OPEN_M)
        return 0.0f;
    s = 1.0f - (distance - LRG_VR_PINCH_CLOSED_M)
               / (LRG_VR_PINCH_OPEN_M - LRG_VR_PINCH_CLOSED_M);
    return CLAMP (s, 0.0f, 1.0f);
}

gboolean
lrg_vr_hand_pinch_update (gboolean was_pinched,
                          gfloat   strength)
{
    if (!was_pinched && strength >= LRG_VR_PINCH_PRESS)
        return TRUE;
    if (was_pinched && strength <= LRG_VR_PINCH_RELEASE)
        return FALSE;
    return was_pinched;
}

gboolean
lrg_vr_hand_state_update_pinch (LrgVRHandState *self,
                                gboolean        was_pinched)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (!self->active)
    {
        self->pinch_strength = 0.0f;
        self->pinched = FALSE;
        return FALSE;
    }
    if (!self->aim_from_runtime)
        self->pinch_strength = lrg_vr_hand_pinch_strength_from_distance
            (lrg_vr_hand_state_get_pinch_distance (self));
    self->pinched = lrg_vr_hand_pinch_update (was_pinched, self->pinch_strength);
    return self->pinched;
}

/* --------------------------------------------------------- derived: aim -- */

/* Shoulder offset from the head, metres: to the hand's side, and down. */
#define LRG_VR_SHOULDER_SIDE 0.18f
#define LRG_VR_SHOULDER_DROP 0.20f

gboolean
lrg_vr_hand_state_compute_aim (LrgVRHandState *self,
                               LrgVRHand       hand,
                               const gfloat   *head)
{
    gfloat origin[3], dir[3];
    gint i;

    g_return_val_if_fail (self != NULL, FALSE);

    if (self->aim_from_runtime)
        return self->aim_valid;
    self->aim_valid = FALSE;
    if (!self->active)
        return FALSE;

    /* Origin: between the thumb and index knuckles; the palm if those are
       not tracked. */
    if (joint_ok (self, LRG_VR_HAND_JOINT_THUMB_PROXIMAL)
        && joint_ok (self, LRG_VR_HAND_JOINT_INDEX_PROXIMAL))
    {
        const gfloat *a = joint_pos (self, LRG_VR_HAND_JOINT_THUMB_PROXIMAL);
        const gfloat *b = joint_pos (self, LRG_VR_HAND_JOINT_INDEX_PROXIMAL);

        for (i = 0; i < 3; i++)
            origin[i] = 0.5f * (a[i] + b[i]);
    }
    else if (joint_ok (self, LRG_VR_HAND_JOINT_PALM))
        memcpy (origin, joint_pos (self, LRG_VR_HAND_JOINT_PALM), sizeof origin);
    else
        return FALSE;

    if (head != NULL)
    {
        /* From an estimated shoulder through the hand.  The head's right
           axis, levelled, finds the side; the head's own pitch and roll
           must not move the shoulder. */
        gfloat right[3], shoulder[3];
        gfloat side = hand == LRG_VR_HAND_RIGHT ? LRG_VR_SHOULDER_SIDE
                                                : -LRG_VR_SHOULDER_SIDE;

        right[0] = head[0];
        right[1] = 0.0f;
        right[2] = head[2];
        if (!v3_normalize (right))
        {
            right[0] = 1.0f;
            right[2] = 0.0f;
        }
        shoulder[0] = head[12] + right[0] * side;
        shoulder[1] = head[13] - LRG_VR_SHOULDER_DROP;
        shoulder[2] = head[14] + right[2] * side;
        for (i = 0; i < 3; i++)
            dir[i] = origin[i] - shoulder[i];
        if (v3_normalize (dir))
        {
            pose_from_ray (origin, dir, self->aim);
            self->aim_valid = TRUE;
            return TRUE;
        }
    }

    /* No head (or the hand is at the shoulder): the palm's forward axis. */
    if (joint_ok (self, LRG_VR_HAND_JOINT_PALM))
    {
        static const gfloat fwd[3] = { 0.0f, 0.0f, -1.0f };

        quat_rotate (&self->orientations[LRG_VR_HAND_JOINT_PALM * 4], fwd, dir);
        if (v3_normalize (dir))
        {
            pose_from_ray (origin, dir, self->aim);
            self->aim_valid = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}

/* ---------------------------------------------------------- skeleton ---- */

gint
lrg_vr_hand_joint_parent (LrgVRHandJoint joint)
{
    switch (joint)
    {
    case LRG_VR_HAND_JOINT_WRIST:
        return -1;
    case LRG_VR_HAND_JOINT_PALM:
    case LRG_VR_HAND_JOINT_THUMB_METACARPAL:
    case LRG_VR_HAND_JOINT_INDEX_METACARPAL:
    case LRG_VR_HAND_JOINT_MIDDLE_METACARPAL:
    case LRG_VR_HAND_JOINT_RING_METACARPAL:
    case LRG_VR_HAND_JOINT_LITTLE_METACARPAL:
        return LRG_VR_HAND_JOINT_WRIST;
    default:
        if ((gint) joint > 0 && (gint) joint < LRG_VR_HAND_JOINT_COUNT)
            return (gint) joint - 1;
        return -1;
    }
}

/* The synthetic hand, palm down, in its own frame: wrist at the origin,
   fingers along -Z, back of the hand +Y, X positive to the little-finger
   side of a RIGHT hand (mirrored for the left).  Metres.  The thumb tip
   is placed separately, from the pinch distance. */
static const gfloat synth_right[LRG_VR_HAND_JOINT_COUNT][3] = {
    {  0.000f,  0.000f, -0.045f },   /* palm */
    {  0.000f,  0.000f,  0.000f },   /* wrist */
    { -0.020f, -0.010f, -0.020f },   /* thumb metacarpal */
    { -0.042f, -0.016f, -0.045f },   /* thumb proximal */
    { -0.052f, -0.020f, -0.075f },   /* thumb distal */
    {  0.000f,  0.000f,  0.000f },   /* thumb tip: from the pinch */
    { -0.022f,  0.000f, -0.010f },   /* index metacarpal */
    { -0.024f,  0.000f, -0.090f },   /* index proximal */
    { -0.026f, -0.012f, -0.128f },   /* index intermediate */
    { -0.027f, -0.026f, -0.148f },   /* index distal */
    { -0.028f, -0.040f, -0.160f },   /* index tip */
    { -0.004f,  0.000f, -0.010f },   /* middle metacarpal */
    { -0.004f,  0.000f, -0.094f },   /* middle proximal */
    { -0.004f, -0.008f, -0.140f },   /* middle intermediate */
    { -0.004f, -0.016f, -0.166f },   /* middle distal */
    { -0.004f, -0.024f, -0.188f },   /* middle tip */
    {  0.014f,  0.000f, -0.010f },   /* ring metacarpal */
    {  0.016f,  0.000f, -0.088f },   /* ring proximal */
    {  0.018f, -0.008f, -0.130f },   /* ring intermediate */
    {  0.019f, -0.016f, -0.155f },   /* ring distal */
    {  0.020f, -0.024f, -0.175f },   /* ring tip */
    {  0.030f,  0.000f, -0.010f },   /* little metacarpal */
    {  0.034f,  0.000f, -0.078f },   /* little proximal */
    {  0.036f, -0.008f, -0.108f },   /* little intermediate */
    {  0.037f, -0.014f, -0.126f },   /* little distal */
    {  0.038f, -0.020f, -0.142f },   /* little tip */
};

void
lrg_vr_hand_state_set_synthetic (LrgVRHandState *self,
                                 LrgVRHand       hand,
                                 gfloat          x,
                                 gfloat          y,
                                 gfloat          z,
                                 gfloat          pinch_distance,
                                 gboolean        palm_up)
{
    /* The thumb tip sits PINCH_DISTANCE from the index tip, toward the
       thumb side and a little up and back. */
    static const gfloat thumb_dir[3] = { -0.80f, 0.30f, 0.52f };
    gfloat mirror = hand == LRG_VR_HAND_LEFT ? -1.0f : 1.0f;
    gfloat flip = palm_up ? -1.0f : 1.0f;
    gfloat dir[3], local[3];
    gint j, i;

    g_return_if_fail (self != NULL);

    lrg_vr_hand_state_reset (self);
    self->active = TRUE;
    self->valid = (1u << LRG_VR_HAND_JOINT_COUNT) - 1u;

    memcpy (dir, thumb_dir, sizeof dir);
    v3_normalize (dir);
    if (pinch_distance < 0.0f)
        pinch_distance = 0.0f;

    for (j = 0; j < LRG_VR_HAND_JOINT_COUNT; j++)
    {
        if (j == LRG_VR_HAND_JOINT_THUMB_TIP)
            for (i = 0; i < 3; i++)
                local[i] = synth_right[LRG_VR_HAND_JOINT_INDEX_TIP][i]
                           + dir[i] * pinch_distance;
        else
            memcpy (local, synth_right[j], sizeof local);

        /* Mirror for the left hand (X), then turn palm up: half a turn
           about the forearm (Z) negates X and Y. */
        local[0] *= mirror * flip;
        local[1] *= flip;

        self->positions[j * 3 + 0] = x + local[0];
        self->positions[j * 3 + 1] = y + local[1];
        self->positions[j * 3 + 2] = z + local[2];

        /* Palm down: every joint's frame is the world's (+Y out of the
           back of the hand, +Z back toward the wrist).  Palm up: half a
           turn about Z, quaternion (0, 0, 1, 0). */
        self->orientations[j * 4 + 0] = 0.0f;
        self->orientations[j * 4 + 1] = 0.0f;
        self->orientations[j * 4 + 2] = palm_up ? 1.0f : 0.0f;
        self->orientations[j * 4 + 3] = palm_up ? 0.0f : 1.0f;

        self->radii[j] = (j == LRG_VR_HAND_JOINT_PALM || j == LRG_VR_HAND_JOINT_WRIST)
                             ? 0.012f : 0.008f;
    }
}
