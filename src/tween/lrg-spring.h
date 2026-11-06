/* lrg-spring.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Damped springs for UI motion.
 *
 * A spring chases a target value with physical, interruptible motion.
 * Unlike a tween it has no duration: retargeting mid-flight keeps the
 * current velocity, so hover/press/drag feedback never jumps. Steps use
 * the closed-form solution of the damped harmonic oscillator, so the
 * result is independent of the frame rate.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../accessibility/lrg-accessibility-settings.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPRING (lrg_spring_get_type ())

/**
 * LrgSpring:
 * @value: current value
 * @velocity: current velocity, in units per second
 * @target: value the spring is moving towards
 * @response: period of the undamped oscillation, in seconds; smaller
 *   is faster. Zero makes the spring jump straight to @target.
 * @damping: damping ratio; 1.0 is critically damped (fastest approach
 *   with no overshoot), below 1.0 overshoots, above 1.0 is sluggish
 *
 * A one-dimensional damped spring. It is a plain value struct: embed
 * it in your own structs and step it every frame with
 * lrg_spring_step().
 */
typedef struct
{
    gfloat value;
    gfloat velocity;
    gfloat target;
    gfloat response;
    gfloat damping;
} LrgSpring;

/**
 * LRG_SPRING_EPSILON:
 *
 * Distance and speed below which a spring counts as settled.
 */
#define LRG_SPRING_EPSILON (0.0005f)

LRG_AVAILABLE_IN_ALL
GType lrg_spring_get_type (void) G_GNUC_CONST;

/**
 * lrg_spring_new:
 * @value: initial value (also the initial target)
 * @motion: preset that sets the response and damping
 *
 * Allocates a spring on the heap. C code usually embeds an
 * #LrgSpring and calls lrg_spring_init() instead.
 *
 * Returns: (transfer full): a new #LrgSpring
 */
LRG_AVAILABLE_IN_ALL
LrgSpring * lrg_spring_new (gfloat    value,
                            LrgMotion motion);

/**
 * lrg_spring_copy:
 * @self: an #LrgSpring
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgSpring * lrg_spring_copy (const LrgSpring *self);

/**
 * lrg_spring_free:
 * @self: (nullable): a heap-allocated #LrgSpring
 */
LRG_AVAILABLE_IN_ALL
void lrg_spring_free (LrgSpring *self);

/**
 * lrg_spring_init:
 * @self: (out caller-allocates): spring to initialise
 * @value: initial value (also the initial target)
 * @motion: preset that sets the response and damping
 *
 * Puts the spring at rest at @value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spring_init (LrgSpring *self,
                      gfloat     value,
                      LrgMotion  motion);

/**
 * lrg_spring_init_full:
 * @self: (out caller-allocates): spring to initialise
 * @value: initial value (also the initial target)
 * @response: oscillation period in seconds (0 jumps instantly)
 * @damping: damping ratio (> 0)
 *
 * Puts the spring at rest at @value with explicit parameters.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spring_init_full (LrgSpring *self,
                           gfloat     value,
                           gfloat     response,
                           gfloat     damping);

/**
 * lrg_spring_set_motion:
 * @self: an #LrgSpring
 * @motion: the new preset
 *
 * Changes the response and damping without disturbing the value,
 * velocity or target.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spring_set_motion (LrgSpring *self,
                            LrgMotion  motion);

/**
 * lrg_spring_set_target:
 * @self: an #LrgSpring
 * @target: the new target
 *
 * Retargets the spring. The current velocity is kept, so motion
 * bends smoothly towards the new target instead of restarting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spring_set_target (LrgSpring *self,
                            gfloat     target);

/**
 * lrg_spring_snap:
 * @self: an #LrgSpring
 * @value: value to jump to
 *
 * Jumps to @value (and targets it) with zero velocity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spring_snap (LrgSpring *self,
                      gfloat     value);

/**
 * lrg_spring_step:
 * @self: an #LrgSpring
 * @delta_time: elapsed time in seconds
 *
 * Advances the spring by @delta_time, scaled by the global motion
 * speed (lrg_motion_set_speed()). When reduced motion is on, or the
 * response is zero, the spring jumps straight to its target. Once the
 * spring is within %LRG_SPRING_EPSILON of rest it snaps exactly onto
 * the target.
 *
 * Returns: %TRUE while the spring is still moving
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spring_step (LrgSpring *self,
                          gfloat     delta_time);

/**
 * lrg_spring_is_settled:
 * @self: an #LrgSpring
 *
 * Returns: %TRUE if the spring is at rest on its target
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spring_is_settled (const LrgSpring *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSpring, lrg_spring_free)

/* ==========================================================================
 * Motion presets and global motion settings
 * ========================================================================== */

/**
 * lrg_motion_get_params:
 * @motion: a preset
 * @response: (out) (optional): oscillation period in seconds
 * @damping: (out) (optional): damping ratio
 *
 * Looks up the spring parameters of a preset.
 */
LRG_AVAILABLE_IN_ALL
void lrg_motion_get_params (LrgMotion  motion,
                            gfloat    *response,
                            gfloat    *damping);

/**
 * lrg_motion_set_speed:
 * @speed: time multiplier (1.0 is normal, 0.5 is half speed)
 *
 * Scales every spring step in the process. Handy for slow-motion
 * debugging of transitions. Values are clamped to [0.01, 10].
 */
LRG_AVAILABLE_IN_ALL
void lrg_motion_set_speed (gfloat speed);

/**
 * lrg_motion_get_speed:
 *
 * Returns: the global motion time multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_motion_get_speed (void);

/**
 * lrg_motion_set_reduced:
 * @reduced: whether to disable motion
 *
 * When %TRUE every spring jumps straight to its target and appear
 * transitions are skipped, for players who are sensitive to motion.
 */
LRG_AVAILABLE_IN_ALL
void lrg_motion_set_reduced (gboolean reduced);

/**
 * lrg_motion_get_reduced:
 *
 * Returns: %TRUE if reduced motion is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_motion_get_reduced (void);

/**
 * lrg_motion_follow_accessibility:
 * @settings: (nullable): accessibility settings to follow, or %NULL
 *   to stop following
 *
 * Mirrors #LrgAccessibilitySettings:reduce-motion into
 * lrg_motion_set_reduced(), now and whenever the setting changes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_motion_follow_accessibility (LrgAccessibilitySettings *settings);

G_END_DECLS
