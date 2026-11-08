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

/**
 * LrgAnimationInterpolation:
 * @LRG_ANIMATION_INTERPOLATION_STEP: hold each key's value until the next
 *   key (glTF "STEP")
 * @LRG_ANIMATION_INTERPOLATION_LINEAR: linear between keys; rotations use
 *   spherical linear interpolation along the shorter arc (glTF "LINEAR")
 * @LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE: cubic Hermite spline with an
 *   in-tangent and an out-tangent stored per key, scaled by the key
 *   interval (glTF "CUBICSPLINE"); rotations are renormalised
 *
 * How a channel (see lrg_animation_clip_set_track_channel()) is
 * interpolated between its keys.
 */
typedef enum
{
    LRG_ANIMATION_INTERPOLATION_STEP,
    LRG_ANIMATION_INTERPOLATION_LINEAR,
    LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE
} LrgAnimationInterpolation;

LRG_AVAILABLE_IN_ALL
GType lrg_animation_interpolation_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANIMATION_INTERPOLATION (lrg_animation_interpolation_get_type ())

/**
 * LrgAnimationChannelPath:
 * @LRG_ANIMATION_CHANNEL_TRANSLATION: the bone's local position (3 floats
 *   per value)
 * @LRG_ANIMATION_CHANNEL_ROTATION: the bone's local rotation quaternion
 *   x, y, z, w (4 floats per value)
 * @LRG_ANIMATION_CHANNEL_SCALE: the bone's local scale (3 floats per value)
 *
 * The bone pose component a track channel animates.
 */
typedef enum
{
    LRG_ANIMATION_CHANNEL_TRANSLATION,
    LRG_ANIMATION_CHANNEL_ROTATION,
    LRG_ANIMATION_CHANNEL_SCALE
} LrgAnimationChannelPath;

LRG_AVAILABLE_IN_ALL
GType lrg_animation_channel_path_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANIMATION_CHANNEL_PATH (lrg_animation_channel_path_get_type ())

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

/* ------------------------------------------------------------------------ */
/* Channels (per-component keys, as glTF stores them)                       */
/* ------------------------------------------------------------------------ */

/**
 * lrg_animation_clip_find_track:
 * @self: A #LrgAnimationClip
 * @bone_name: The bone name
 *
 * Finds the first track that animates @bone_name.
 *
 * Returns: the track index, or -1 when no track names the bone
 */
LRG_AVAILABLE_IN_ALL
gint                    lrg_animation_clip_find_track       (LrgAnimationClip *self,
                                                             const gchar      *bone_name);

/**
 * lrg_animation_clip_set_track_channel:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 * @path: Which pose component the channel animates
 * @interpolation: How values between keys are computed
 * @n_keys: Number of keys (at least 1)
 * @times: (array length=n_keys): key times in seconds, not decreasing
 * @values: (array) (element-type gfloat): the key values: 3 floats per key
 *   for translation and scale, 4 (x, y, z, w) for rotation; with
 *   %LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE three times as many, each key
 *   stored as in-tangent, value, out-tangent (the glTF layout)
 * @error: return location for a #GError
 *
 * Gives a track a channel: keys for one component of the bone pose with
 * their own times and interpolation, replacing a channel of the same
 * @path. A track with at least one channel is sampled from its channels
 * (components without a channel come from the track's base pose, see
 * lrg_animation_clip_set_track_base_pose()); its keyframes are then
 * ignored. The clip's duration grows to the last key time. Fails with
 * %G_IO_ERROR_INVALID_ARGUMENT for a bad track, no keys, decreasing or
 * non-finite times.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_clip_set_track_channel (LrgAnimationClip         *self,
                                                              guint                     track_index,
                                                              LrgAnimationChannelPath   path,
                                                              LrgAnimationInterpolation interpolation,
                                                              guint                     n_keys,
                                                              const gfloat             *times,
                                                              const gfloat             *values,
                                                              GError                  **error);

/**
 * lrg_animation_clip_get_track_channel:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 * @path: The pose component
 * @out_interpolation: (out) (optional): the channel's interpolation
 * @out_n_keys: (out) (optional): the number of keys
 * @out_times: (out) (optional) (transfer none) (array length=out_n_keys):
 *   the key times
 * @out_values: (out) (optional) (transfer none): the key values, laid out
 *   as lrg_animation_clip_set_track_channel() takes them
 *
 * Reads a channel back (the editor's curve view and the retargeter use
 * it).
 *
 * Returns: %TRUE when the track has a channel for @path
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_clip_get_track_channel (LrgAnimationClip           *self,
                                                              guint                       track_index,
                                                              LrgAnimationChannelPath     path,
                                                              LrgAnimationInterpolation  *out_interpolation,
                                                              guint                      *out_n_keys,
                                                              const gfloat              **out_times,
                                                              const gfloat              **out_values);

/**
 * lrg_animation_clip_track_has_channels:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 *
 * Returns: %TRUE when the track is sampled from channels rather than
 *   from keyframes
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_clip_track_has_channels (LrgAnimationClip *self,
                                                               guint             track_index);

/**
 * lrg_animation_clip_set_track_base_pose:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 * @pose: The pose whose components fill in for missing channels
 *
 * Sets the values used for the components a channel track does not
 * animate (the glTF importer stores the node's rest transform, so a
 * rotation-only track keeps the bone's rest position and scale). The
 * default is the identity pose.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_clip_set_track_base_pose (LrgAnimationClip  *self,
                                                                guint              track_index,
                                                                const LrgBonePose *pose);

/**
 * lrg_animation_clip_get_track_base_pose:
 * @self: A #LrgAnimationClip
 * @track_index: The track index
 * @out_pose: (out caller-allocates): the base pose
 *
 * Returns: %FALSE (and the identity) when @track_index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_clip_get_track_base_pose (LrgAnimationClip *self,
                                                                guint             track_index,
                                                                LrgBonePose      *out_pose);

/* ------------------------------------------------------------------------ */
/* Morph target weight channels                                             */
/* ------------------------------------------------------------------------ */

/**
 * lrg_animation_clip_add_weights_channel:
 * @self: A #LrgAnimationClip
 * @target_name: the morph target group the weights drive (the glTF
 *   importer uses the animated node's name, which is also the group name
 *   of that node's targets in #LrgMorphTargets)
 * @n_weights: weights per key (the number of morph targets, at least 1)
 * @interpolation: how values between keys are computed
 * @n_keys: number of keys (at least 1)
 * @times: (array length=n_keys): key times in seconds, not decreasing
 * @values: (array) (element-type gfloat): @n_weights floats per key
 *   (three times as many for cubic spline: in-tangents, values,
 *   out-tangents, each @n_weights wide)
 * @error: return location for a #GError
 *
 * Adds a channel of morph target weights. The clip's duration grows to
 * the last key time.
 *
 * Returns: the channel index, or -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint                    lrg_animation_clip_add_weights_channel (LrgAnimationClip         *self,
                                                                const gchar              *target_name,
                                                                guint                     n_weights,
                                                                LrgAnimationInterpolation interpolation,
                                                                guint                     n_keys,
                                                                const gfloat             *times,
                                                                const gfloat             *values,
                                                                GError                  **error);

/**
 * lrg_animation_clip_get_weights_channel_count:
 * @self: A #LrgAnimationClip
 *
 * Returns: the number of morph weight channels
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_animation_clip_get_weights_channel_count (LrgAnimationClip *self);

/**
 * lrg_animation_clip_get_weights_channel_target:
 * @self: A #LrgAnimationClip
 * @channel: the weight channel index
 *
 * Returns: (transfer none) (nullable): the morph target group the
 *   channel drives, %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *           lrg_animation_clip_get_weights_channel_target (LrgAnimationClip *self,
                                                                       guint             channel);

/**
 * lrg_animation_clip_get_weights_channel_width:
 * @self: A #LrgAnimationClip
 * @channel: the weight channel index
 *
 * Returns: weights per key, 0 when out of range
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_animation_clip_get_weights_channel_width (LrgAnimationClip *self,
                                                                      guint             channel);

/**
 * lrg_animation_clip_sample_weights:
 * @self: A #LrgAnimationClip
 * @channel: the weight channel index
 * @time: The time in seconds (the clip's loop mode applies)
 * @out_weights: (array) (out caller-allocates) (element-type gfloat):
 *   lrg_animation_clip_get_weights_channel_width() floats
 *
 * Samples a morph weight channel.
 *
 * Returns: %FALSE when @channel is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_clip_sample_weights   (LrgAnimationClip *self,
                                                             guint             channel,
                                                             gfloat            time,
                                                             gfloat           *out_weights);

G_END_DECLS
