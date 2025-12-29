/* lrg-animation-clip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation clip containing keyframe tracks and events.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-bone-pose.h"
#include "lrg-animation-keyframe.h"
#include "lrg-animation-event.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANIMATION_CLIP (lrg_animation_clip_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAnimationClip, lrg_animation_clip, LRG, ANIMATION_CLIP, GObject)

/**
 * LrgAnimationClipClass:
 * @parent_class: Parent class
 * @sample: Virtual method to sample the animation at a time
 *
 * Class structure for #LrgAnimationClip.
 */
struct _LrgAnimationClipClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*sample) (LrgAnimationClip *self,
                    gfloat            time,
                    GPtrArray        *out_poses);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_animation_clip_new:
 * @name: The clip name
 *
 * Creates a new animation clip.
 *
 * Returns: (transfer full): A new #LrgAnimationClip
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationClip *      lrg_animation_clip_new              (const gchar     *name);

/**
 * lrg_animation_clip_get_name:
 * @self: A #LrgAnimationClip
 *
 * Gets the clip name.
 *
 * Returns: (transfer none): The clip name
 */
LRG_AVAILABLE_IN_ALL
const gchar *           lrg_animation_clip_get_name         (LrgAnimationClip *self);

/**
 * lrg_animation_clip_get_duration:
 * @self: A #LrgAnimationClip
 *
 * Gets the clip duration in seconds.
 *
 * Returns: The duration
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_animation_clip_get_duration     (LrgAnimationClip *self);

/**
 * lrg_animation_clip_set_duration:
 * @self: A #LrgAnimationClip
 * @duration: The duration in seconds
 *
 * Sets the clip duration.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_clip_set_duration     (LrgAnimationClip *self,
                                                             gfloat            duration);

/**
 * lrg_animation_clip_get_loop_mode:
 * @self: A #LrgAnimationClip
 *
 * Gets the loop mode.
 *
 * Returns: The loop mode
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationLoopMode    lrg_animation_clip_get_loop_mode    (LrgAnimationClip *self);

/**
 * lrg_animation_clip_set_loop_mode:
 * @self: A #LrgAnimationClip
 * @mode: The loop mode
 *
 * Sets the loop mode.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_clip_set_loop_mode    (LrgAnimationClip    *self,
                                                             LrgAnimationLoopMode mode);

/**
 * lrg_animation_clip_add_track:
 * @self: A #LrgAnimationClip
 * @bone_name: The bone to animate
 *
 * Adds a new animation track for a bone.
 *
 * Returns: The track index
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_animation_clip_add_track        (LrgAnimationClip *self,
                                                             const gchar      *bone_name);

/**
 * lrg_animation_clip_get_track_count:
 * @self: A #LrgAnimationClip
 *
 * Gets the number of animation tracks.
 *
 * Returns: The track count
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_animation_clip_get_track_count  (LrgAnimationClip *self);

/**
 * lrg_animation_clip_get_track_bone_name:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 *
 * Gets the bone name for a track.
 *
 * Returns: (transfer none) (nullable): The bone name
 */
LRG_AVAILABLE_IN_ALL
const gchar *           lrg_animation_clip_get_track_bone_name (LrgAnimationClip *self,
                                                                 guint             track_index);

/**
 * lrg_animation_clip_add_keyframe:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 * @keyframe: The keyframe to add
 *
 * Adds a keyframe to a track.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_clip_add_keyframe     (LrgAnimationClip       *self,
                                                             guint                   track_index,
                                                             const LrgAnimationKeyframe *keyframe);

/**
 * lrg_animation_clip_get_keyframe_count:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 *
 * Gets the number of keyframes in a track.
 *
 * Returns: The keyframe count
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_animation_clip_get_keyframe_count (LrgAnimationClip *self,
                                                               guint             track_index);

/**
 * lrg_animation_clip_get_keyframe:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 * @keyframe_index: The keyframe index
 *
 * Gets a keyframe from a track.
 *
 * Returns: (transfer none) (nullable): The keyframe
 */
LRG_AVAILABLE_IN_ALL
const LrgAnimationKeyframe * lrg_animation_clip_get_keyframe (LrgAnimationClip *self,
                                                              guint             track_index,
                                                              guint             keyframe_index);

/**
 * lrg_animation_clip_add_event:
 * @self: A #LrgAnimationClip
 * @event: The event to add
 *
 * Adds an animation event.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_clip_add_event        (LrgAnimationClip       *self,
                                                             const LrgAnimationEvent *event);

/**
 * lrg_animation_clip_get_event_count:
 * @self: A #LrgAnimationClip
 *
 * Gets the number of events.
 *
 * Returns: The event count
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_animation_clip_get_event_count  (LrgAnimationClip *self);

/**
 * lrg_animation_clip_get_event:
 * @self: A #LrgAnimationClip
 * @index: The event index
 *
 * Gets an event by index.
 *
 * Returns: (transfer none) (nullable): The event
 */
LRG_AVAILABLE_IN_ALL
const LrgAnimationEvent * lrg_animation_clip_get_event      (LrgAnimationClip *self,
                                                             guint             index);

/**
 * lrg_animation_clip_get_events_in_range:
 * @self: A #LrgAnimationClip
 * @start_time: Start time in seconds
 * @end_time: End time in seconds
 *
 * Gets events that occur within a time range.
 *
 * Returns: (transfer container) (element-type LrgAnimationEvent): Events in range
 */
LRG_AVAILABLE_IN_ALL
GList *                 lrg_animation_clip_get_events_in_range (LrgAnimationClip *self,
                                                                 gfloat            start_time,
                                                                 gfloat            end_time);

/**
 * lrg_animation_clip_sample:
 * @self: A #LrgAnimationClip
 * @time: The time in seconds
 * @out_poses: (element-type LrgBonePose): Output array for sampled poses
 *
 * Samples the animation at a given time. The poses array should
 * have enough elements for all tracks.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_clip_sample           (LrgAnimationClip *self,
                                                             gfloat            time,
                                                             GPtrArray        *out_poses);

/**
 * lrg_animation_clip_sample_track:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 * @time: The time in seconds
 * @out_pose: (out): Output pose
 *
 * Samples a single track at a given time.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_clip_sample_track     (LrgAnimationClip *self,
                                                             guint             track_index,
                                                             gfloat            time,
                                                             LrgBonePose      *out_pose);

/**
 * lrg_animation_clip_calculate_smooth_tangents:
 * @self: A #LrgAnimationClip
 *
 * Recalculates smooth tangents for all keyframes in all tracks.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_clip_calculate_smooth_tangents (LrgAnimationClip *self);

G_END_DECLS
