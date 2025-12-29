/* lrg-animator.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation playback controller.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-skeleton.h"
#include "lrg-animation-clip.h"
#include "lrg-animation-event.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANIMATOR (lrg_animator_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAnimator, lrg_animator, LRG, ANIMATOR, GObject)

/**
 * LrgAnimatorClass:
 * @parent_class: Parent class
 * @update: Virtual method to update the animator
 * @event: Signal when an animation event fires
 *
 * Class structure for #LrgAnimator.
 */
struct _LrgAnimatorClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*update) (LrgAnimator *self,
                    gfloat       delta_time);

    /* Signals */
    void (*event)  (LrgAnimator           *self,
                    const LrgAnimationEvent *event);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_animator_new:
 * @skeleton: (nullable): The skeleton to animate
 *
 * Creates a new animator.
 *
 * Returns: (transfer full): A new #LrgAnimator
 */
LRG_AVAILABLE_IN_ALL
LrgAnimator *           lrg_animator_new                    (LrgSkeleton     *skeleton);

/**
 * lrg_animator_get_skeleton:
 * @self: A #LrgAnimator
 *
 * Gets the skeleton.
 *
 * Returns: (transfer none) (nullable): The skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *           lrg_animator_get_skeleton           (LrgAnimator     *self);

/**
 * lrg_animator_set_skeleton:
 * @self: A #LrgAnimator
 * @skeleton: (nullable): The skeleton to animate
 *
 * Sets the skeleton.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_set_skeleton           (LrgAnimator     *self,
                                                             LrgSkeleton     *skeleton);

/**
 * lrg_animator_add_clip:
 * @self: A #LrgAnimator
 * @name: The clip name for lookups
 * @clip: The animation clip
 *
 * Adds an animation clip.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_add_clip               (LrgAnimator      *self,
                                                             const gchar      *name,
                                                             LrgAnimationClip *clip);

/**
 * lrg_animator_remove_clip:
 * @self: A #LrgAnimator
 * @name: The clip name
 *
 * Removes an animation clip.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_remove_clip            (LrgAnimator     *self,
                                                             const gchar     *name);

/**
 * lrg_animator_get_clip:
 * @self: A #LrgAnimator
 * @name: The clip name
 *
 * Gets an animation clip by name.
 *
 * Returns: (transfer none) (nullable): The clip
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationClip *      lrg_animator_get_clip               (LrgAnimator     *self,
                                                             const gchar     *name);

/**
 * lrg_animator_play:
 * @self: A #LrgAnimator
 * @name: The clip name to play
 *
 * Plays an animation clip immediately.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_play                   (LrgAnimator     *self,
                                                             const gchar     *name);

/**
 * lrg_animator_crossfade:
 * @self: A #LrgAnimator
 * @name: The clip name to transition to
 * @duration: The crossfade duration in seconds
 *
 * Crossfades to a new animation clip.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_crossfade              (LrgAnimator     *self,
                                                             const gchar     *name,
                                                             gfloat           duration);

/**
 * lrg_animator_stop:
 * @self: A #LrgAnimator
 *
 * Stops playback.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_stop                   (LrgAnimator     *self);

/**
 * lrg_animator_pause:
 * @self: A #LrgAnimator
 *
 * Pauses playback.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_pause                  (LrgAnimator     *self);

/**
 * lrg_animator_resume:
 * @self: A #LrgAnimator
 *
 * Resumes playback.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_resume                 (LrgAnimator     *self);

/**
 * lrg_animator_get_state:
 * @self: A #LrgAnimator
 *
 * Gets the playback state.
 *
 * Returns: The playback state
 */
LRG_AVAILABLE_IN_ALL
LrgAnimatorState        lrg_animator_get_state              (LrgAnimator     *self);

/**
 * lrg_animator_get_current_clip:
 * @self: A #LrgAnimator
 *
 * Gets the current clip name.
 *
 * Returns: (transfer none) (nullable): The current clip name
 */
LRG_AVAILABLE_IN_ALL
const gchar *           lrg_animator_get_current_clip       (LrgAnimator     *self);

/**
 * lrg_animator_get_time:
 * @self: A #LrgAnimator
 *
 * Gets the current playback time.
 *
 * Returns: The time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_animator_get_time               (LrgAnimator     *self);

/**
 * lrg_animator_set_time:
 * @self: A #LrgAnimator
 * @time: The time in seconds
 *
 * Sets the playback time.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_set_time               (LrgAnimator     *self,
                                                             gfloat           time);

/**
 * lrg_animator_get_speed:
 * @self: A #LrgAnimator
 *
 * Gets the playback speed multiplier.
 *
 * Returns: The speed multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_animator_get_speed              (LrgAnimator     *self);

/**
 * lrg_animator_set_speed:
 * @self: A #LrgAnimator
 * @speed: The speed multiplier
 *
 * Sets the playback speed.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_set_speed              (LrgAnimator     *self,
                                                             gfloat           speed);

/**
 * lrg_animator_update:
 * @self: A #LrgAnimator
 * @delta_time: Time since last update
 *
 * Updates the animator and applies poses to the skeleton.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animator_update                 (LrgAnimator     *self,
                                                             gfloat           delta_time);

G_END_DECLS
