/* lrg-vr-hand.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Articulated hand tracking: the 26-joint skeleton OpenXR's
 * XR_EXT_hand_tracking reports, plus the pinch and aim a pointer needs.
 *
 * #LrgVRHandState is a plain value type: a backend fills one per hand per
 * frame (lrg_vr_service_get_hand_state()), and everything derived from it
 * -- pinch strength from the thumb/index distance, the pinch latch with
 * hysteresis, a fallback aim ray, palm orientation -- is pure arithmetic
 * here, so it is the same for every backend and testable without a
 * headset.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LrgVRHandJoint:
 * @LRG_VR_HAND_JOINT_PALM: centre of the palm
 * @LRG_VR_HAND_JOINT_WRIST: wrist
 * @LRG_VR_HAND_JOINT_THUMB_METACARPAL: thumb metacarpal
 * @LRG_VR_HAND_JOINT_THUMB_PROXIMAL: thumb proximal
 * @LRG_VR_HAND_JOINT_THUMB_DISTAL: thumb distal
 * @LRG_VR_HAND_JOINT_THUMB_TIP: thumb tip
 * @LRG_VR_HAND_JOINT_INDEX_METACARPAL: index metacarpal
 * @LRG_VR_HAND_JOINT_INDEX_PROXIMAL: index proximal
 * @LRG_VR_HAND_JOINT_INDEX_INTERMEDIATE: index intermediate
 * @LRG_VR_HAND_JOINT_INDEX_DISTAL: index distal
 * @LRG_VR_HAND_JOINT_INDEX_TIP: index tip
 * @LRG_VR_HAND_JOINT_MIDDLE_METACARPAL: middle metacarpal
 * @LRG_VR_HAND_JOINT_MIDDLE_PROXIMAL: middle proximal
 * @LRG_VR_HAND_JOINT_MIDDLE_INTERMEDIATE: middle intermediate
 * @LRG_VR_HAND_JOINT_MIDDLE_DISTAL: middle distal
 * @LRG_VR_HAND_JOINT_MIDDLE_TIP: middle tip
 * @LRG_VR_HAND_JOINT_RING_METACARPAL: ring metacarpal
 * @LRG_VR_HAND_JOINT_RING_PROXIMAL: ring proximal
 * @LRG_VR_HAND_JOINT_RING_INTERMEDIATE: ring intermediate
 * @LRG_VR_HAND_JOINT_RING_DISTAL: ring distal
 * @LRG_VR_HAND_JOINT_RING_TIP: ring tip
 * @LRG_VR_HAND_JOINT_LITTLE_METACARPAL: little metacarpal
 * @LRG_VR_HAND_JOINT_LITTLE_PROXIMAL: little proximal
 * @LRG_VR_HAND_JOINT_LITTLE_INTERMEDIATE: little intermediate
 * @LRG_VR_HAND_JOINT_LITTLE_DISTAL: little distal
 * @LRG_VR_HAND_JOINT_LITTLE_TIP: little tip
 *
 * The joints of a tracked hand, in the order and with the numbering of
 * OpenXR's XrHandJointEXT, so a backend copies arrays straight across.
 *
 * Each joint has a pose: +Z points back along the bone, away from the
 * finger tip; +Y points out of the back of the hand (the nail side); +X
 * completes a right-handed frame.  The palm's -Y is therefore the palm
 * normal.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VR_HAND_JOINT_PALM = 0,
    LRG_VR_HAND_JOINT_WRIST,
    LRG_VR_HAND_JOINT_THUMB_METACARPAL,
    LRG_VR_HAND_JOINT_THUMB_PROXIMAL,
    LRG_VR_HAND_JOINT_THUMB_DISTAL,
    LRG_VR_HAND_JOINT_THUMB_TIP,
    LRG_VR_HAND_JOINT_INDEX_METACARPAL,
    LRG_VR_HAND_JOINT_INDEX_PROXIMAL,
    LRG_VR_HAND_JOINT_INDEX_INTERMEDIATE,
    LRG_VR_HAND_JOINT_INDEX_DISTAL,
    LRG_VR_HAND_JOINT_INDEX_TIP,
    LRG_VR_HAND_JOINT_MIDDLE_METACARPAL,
    LRG_VR_HAND_JOINT_MIDDLE_PROXIMAL,
    LRG_VR_HAND_JOINT_MIDDLE_INTERMEDIATE,
    LRG_VR_HAND_JOINT_MIDDLE_DISTAL,
    LRG_VR_HAND_JOINT_MIDDLE_TIP,
    LRG_VR_HAND_JOINT_RING_METACARPAL,
    LRG_VR_HAND_JOINT_RING_PROXIMAL,
    LRG_VR_HAND_JOINT_RING_INTERMEDIATE,
    LRG_VR_HAND_JOINT_RING_DISTAL,
    LRG_VR_HAND_JOINT_RING_TIP,
    LRG_VR_HAND_JOINT_LITTLE_METACARPAL,
    LRG_VR_HAND_JOINT_LITTLE_PROXIMAL,
    LRG_VR_HAND_JOINT_LITTLE_INTERMEDIATE,
    LRG_VR_HAND_JOINT_LITTLE_DISTAL,
    LRG_VR_HAND_JOINT_LITTLE_TIP
} LrgVRHandJoint;

LRG_AVAILABLE_IN_ALL
GType lrg_vr_hand_joint_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VR_HAND_JOINT (lrg_vr_hand_joint_get_type ())

/**
 * LRG_VR_HAND_JOINT_COUNT:
 *
 * Number of joints in a hand skeleton (XR_HAND_JOINT_COUNT_EXT).
 */
#define LRG_VR_HAND_JOINT_COUNT 26

/**
 * LRG_VR_PINCH_PRESS:
 *
 * Pinch strength at or above which a released pinch latches closed.
 */
#define LRG_VR_PINCH_PRESS   0.85f

/**
 * LRG_VR_PINCH_RELEASE:
 *
 * Pinch strength at or below which a closed pinch latches open again.  The
 * gap to %LRG_VR_PINCH_PRESS is the hysteresis that keeps a pinch held at
 * the threshold from chattering into a stream of clicks.
 */
#define LRG_VR_PINCH_RELEASE 0.60f

/**
 * LRG_VR_PINCH_CLOSED_M:
 *
 * Thumb-tip to index-tip distance, in metres, that counts as a fully
 * closed pinch (strength 1).  Fingertip joints sit inside the finger, so
 * touching fingertips still measure about a centimetre apart.
 */
#define LRG_VR_PINCH_CLOSED_M 0.015f

/**
 * LRG_VR_PINCH_OPEN_M:
 *
 * Thumb-tip to index-tip distance, in metres, at and beyond which the
 * pinch strength is 0.
 */
#define LRG_VR_PINCH_OPEN_M   0.060f

#define LRG_TYPE_VR_HAND_STATE (lrg_vr_hand_state_get_type ())

/**
 * LrgVRHandState:
 * @active: the runtime is tracking this hand this frame
 * @valid: bit N set when joint N's position is valid
 * @positions: joint positions, x y z per joint, metres, tracking space
 * @orientations: joint orientations, quaternion x y z w per joint
 * @radii: joint radii, metres
 * @aim_valid: @aim holds a usable pointer ray
 * @aim: the pointer ray as a column-major 4x4 pose: its translation is the
 *   ray origin and its -Z axis the direction, like a controller aim pose
 * @aim_from_runtime: @aim and @pinch_strength came from the runtime
 *   (XR_FB_hand_tracking_aim) rather than from the joints
 * @pinch_strength: 0 (open) .. 1 (thumb and index together)
 * @pinched: the pinch latch, with hysteresis (see lrg_vr_hand_pinch_update())
 *
 * One hand's tracking state for one frame.  A plain value type: copy it by
 * assignment, keep it on the stack.
 *
 * Since: 1.0
 */
typedef struct _LrgVRHandState LrgVRHandState;

struct _LrgVRHandState
{
    gboolean active;
    guint32  valid;
    gfloat   positions[LRG_VR_HAND_JOINT_COUNT * 3];
    gfloat   orientations[LRG_VR_HAND_JOINT_COUNT * 4];
    gfloat   radii[LRG_VR_HAND_JOINT_COUNT];
    gboolean aim_valid;
    gfloat   aim[16];
    gboolean aim_from_runtime;
    gfloat   pinch_strength;
    gboolean pinched;
};

LRG_AVAILABLE_IN_ALL
GType lrg_vr_hand_state_get_type (void) G_GNUC_CONST;

/**
 * lrg_vr_hand_state_new:
 *
 * Returns: (transfer full): a new, inactive hand state
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRHandState * lrg_vr_hand_state_new (void);

/**
 * lrg_vr_hand_state_copy:
 * @self: a #LrgVRHandState
 *
 * Returns: (transfer full): a copy of @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRHandState * lrg_vr_hand_state_copy (const LrgVRHandState *self);

/**
 * lrg_vr_hand_state_free:
 * @self: (nullable): a #LrgVRHandState
 *
 * Frees a state made by lrg_vr_hand_state_new() or _copy().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_hand_state_free (LrgVRHandState *self);

/**
 * lrg_vr_hand_state_reset:
 * @self: a #LrgVRHandState
 *
 * Makes @self inactive: no valid joints, no aim, an open pinch, identity
 * orientations.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_hand_state_reset (LrgVRHandState *self);

/**
 * lrg_vr_hand_state_is_active:
 * @self: a #LrgVRHandState
 *
 * Returns: %TRUE while the runtime tracks this hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_state_is_active (const LrgVRHandState *self);

/**
 * lrg_vr_hand_state_get_joint_position:
 * @self: a #LrgVRHandState
 * @joint: which joint
 * @x: (out) (optional): x, metres
 * @y: (out) (optional): y, metres
 * @z: (out) (optional): z, metres
 *
 * Returns: %TRUE when @joint's position is valid this frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_state_get_joint_position (const LrgVRHandState *self,
                                               LrgVRHandJoint        joint,
                                               gfloat               *x,
                                               gfloat               *y,
                                               gfloat               *z);

/**
 * lrg_vr_hand_state_get_joint_radius:
 * @self: a #LrgVRHandState
 * @joint: which joint
 *
 * Returns: @joint's radius in metres (0 when unknown)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_hand_state_get_joint_radius (const LrgVRHandState *self,
                                           LrgVRHandJoint        joint);

/**
 * lrg_vr_hand_state_get_aim:
 * @self: a #LrgVRHandState
 * @matrix: (out caller-allocates) (array fixed-size=16): the aim pose
 *
 * Returns: %TRUE when the hand has a usable aim ray; @matrix is identity
 *   otherwise
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_state_get_aim (const LrgVRHandState *self,
                                    gfloat               *matrix);

/**
 * lrg_vr_hand_state_get_pinch_strength:
 * @self: a #LrgVRHandState
 *
 * Returns: 0 (open) .. 1 (closed)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_hand_state_get_pinch_strength (const LrgVRHandState *self);

/**
 * lrg_vr_hand_state_is_pinched:
 * @self: a #LrgVRHandState
 *
 * Returns: the pinch latch (closed with hysteresis)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_state_is_pinched (const LrgVRHandState *self);

/**
 * lrg_vr_hand_state_get_pinch_distance:
 * @self: a #LrgVRHandState
 *
 * Returns: the thumb-tip to index-tip distance in metres, or -1 when
 *   either tip is not tracked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_hand_state_get_pinch_distance (const LrgVRHandState *self);

/**
 * lrg_vr_hand_state_get_pinch_point:
 * @self: a #LrgVRHandState
 * @point: (out caller-allocates) (array fixed-size=3): the point between
 *   thumb tip and index tip, metres
 *
 * Returns: %TRUE when both tips are tracked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_state_get_pinch_point (const LrgVRHandState *self,
                                            gfloat               *point);

/**
 * lrg_vr_hand_state_get_palm_normal:
 * @self: a #LrgVRHandState
 * @normal: (out caller-allocates) (array fixed-size=3): unit vector out of
 *   the palm (the palm joint's -Y), tracking space
 *
 * Returns: %TRUE when the palm joint is tracked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_state_get_palm_normal (const LrgVRHandState *self,
                                            gfloat               *normal);

/**
 * lrg_vr_hand_state_is_palm_up:
 * @self: a #LrgVRHandState
 * @threshold: cosine of the largest angle between the palm normal and
 *   straight up that still counts (0.7 is about 45 degrees)
 *
 * Tracking space has +Y up (OpenXR LOCAL and STAGE spaces both do).
 *
 * Returns: %TRUE when the hand is tracked with its palm facing up
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_state_is_palm_up (const LrgVRHandState *self,
                                       gfloat                threshold);

/**
 * lrg_vr_hand_state_compute_aim:
 * @self: a #LrgVRHandState
 * @hand: which hand @self is
 * @head: (nullable) (array fixed-size=16): the head pose, or %NULL
 *
 * Fills @self's aim from its joints, for runtimes without
 * XR_FB_hand_tracking_aim.  The ray starts between the thumb and index
 * knuckles -- a point that barely moves as the fingers pinch, so clicking
 * does not jog the pointer -- and points away from an estimated shoulder
 * (a little below and to @hand's side of @head), the way an outstretched
 * arm points.  Without @head it follows the palm's forward axis.
 *
 * Leaves an aim the runtime supplied (@aim_from_runtime) alone.
 *
 * Returns: %TRUE when @self now has an aim
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_state_compute_aim (LrgVRHandState *self,
                                        LrgVRHand       hand,
                                        const gfloat   *head);

/**
 * lrg_vr_hand_state_update_pinch:
 * @self: a #LrgVRHandState
 * @was_pinched: the latch from the previous frame
 *
 * Derives @self's pinch: the strength from the fingertip distance (unless
 * the runtime supplied one), then the latch from that strength and
 * @was_pinched with lrg_vr_hand_pinch_update().  An inactive hand is open.
 *
 * Returns: the new latch (also stored in @self)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_state_update_pinch (LrgVRHandState *self,
                                         gboolean        was_pinched);

/**
 * lrg_vr_hand_pinch_strength_from_distance:
 * @distance: thumb-tip to index-tip distance in metres (negative: unknown)
 *
 * Maps a fingertip distance onto 0..1: 1 at %LRG_VR_PINCH_CLOSED_M or
 * nearer, 0 at %LRG_VR_PINCH_OPEN_M or farther (or when unknown), linear
 * in between.
 *
 * Returns: the pinch strength
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_hand_pinch_strength_from_distance (gfloat distance);

/**
 * lrg_vr_hand_pinch_update:
 * @was_pinched: the latch so far
 * @strength: this frame's pinch strength
 *
 * The pinch latch: an open pinch closes at %LRG_VR_PINCH_PRESS, a closed
 * one opens at %LRG_VR_PINCH_RELEASE, and anything between keeps the
 * previous state.
 *
 * Returns: the new latch
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_hand_pinch_update (gboolean was_pinched,
                                   gfloat   strength);

/**
 * lrg_vr_hand_state_set_synthetic:
 * @self: a #LrgVRHandState
 * @hand: which hand to lay out
 * @x: wrist x, metres
 * @y: wrist y, metres
 * @z: wrist z, metres
 * @pinch_distance: thumb-tip to index-tip distance to pose, metres
 * @palm_up: %TRUE to turn the palm up, %FALSE for palm down
 *
 * Lays out a deterministic, anatomically plausible hand: wrist at
 * (@x, @y, @z), fingers pointing along -Z (away from a viewer at the
 * origin looking down -Z), every joint valid, with the thumb and index
 * tips @pinch_distance apart.  The aim and pinch are left for
 * lrg_vr_hand_state_compute_aim() and _update_pinch(), as a real backend
 * would leave them.  Used by #LrgVRStub's fake hands and by tests.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_hand_state_set_synthetic (LrgVRHandState *self,
                                      LrgVRHand       hand,
                                      gfloat          x,
                                      gfloat          y,
                                      gfloat          z,
                                      gfloat          pinch_distance,
                                      gboolean        palm_up);

/**
 * lrg_vr_hand_joint_parent:
 * @joint: a joint
 *
 * The joint a bone runs to @joint from, for drawing a skeleton: each
 * finger chains back to its metacarpal, metacarpals and the palm to the
 * wrist.
 *
 * Returns: the parent joint, or -1 for the wrist (the root)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_vr_hand_joint_parent (LrgVRHandJoint joint);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgVRHandState, lrg_vr_hand_state_free)

G_END_DECLS
