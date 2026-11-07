/* lrg-timeline.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Keyframed property animation stored in a level.
 *
 * An #LrgTimeline is a named clip owned by an #LrgLevel. It holds
 * #LrgTimelineTrack s; each track animates one scalar channel of one node
 * (referenced by GUID) with an #LrgKeyframeCurve whose key times are in
 * seconds and whose per-key easing (#LrgEasingType) shapes the segment to
 * the next key.
 *
 * Channels are strings:
 *
 *  - "location.x|y|z", "rotation.x|y|z" (radians), "scale.x|y|z"
 *  - "color.r|g|b|a" (the visual's material colour, 0..1)
 *  - "roughness", "metallic" (the visual's material)
 *  - "param.NAME" (a numeric entry of the visual's param bag)
 *  - "bone.BONE.location|rotation|scale.x|y|z" (since 0.9.0): an offset
 *    from the bind pose of bone BONE of the node's skinned mesh (location
 *    added, Euler rotation in radians applied after the bind rotation,
 *    scale multiplied; see lrg_sequence_compose_bone_offset()). Bone names
 *    may contain dots. On an #LrgNode the offsets live in a runtime side
 *    table (lrg_timeline_get_bone_offset()), never in the saved document.
 *
 * Since 0.9.0 a key may also be a Bézier key
 * (lrg_timeline_track_set_key_tangents()): the segment that starts at it
 * is a cubic Bézier shaped by #LrgSequenceTangentMode handles instead of
 * the key's easing.
 *
 * Timelines are plain data: sampling and applying them is pure and
 * headless. Editing them in the editor goes through undoable commands
 * (see lrg_editor_command_new_timeline()).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "../tween/lrg-keyframe-curve.h"
#include "../animation/lrg-sequence.h"

G_BEGIN_DECLS

#define LRG_TYPE_TIMELINE_TRACK (lrg_timeline_track_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTimelineTrack, lrg_timeline_track, LRG, TIMELINE_TRACK, GObject)

#define LRG_TYPE_TIMELINE (lrg_timeline_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTimeline, lrg_timeline, LRG, TIMELINE, GObject)

/* ==========================================================================
 * Channels
 * ========================================================================== */

/**
 * lrg_timeline_channel_is_valid:
 * @channel: a channel name
 *
 * Returns: whether @channel names an animatable channel
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_channel_is_valid (const gchar *channel);

/**
 * lrg_timeline_channel_read:
 * @node: an #LrgNode
 * @channel: a channel name
 * @out_value: (out): the current value
 *
 * Reads a channel's current value from @node. Material channels read
 * the defaults (colour 1, roughness 0.5, metallic 0) when the visual has
 * no material.
 *
 * Returns: %FALSE if the channel is invalid or the node lacks the visual
 *   it needs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_channel_read (LrgNode     *node,
                                    const gchar *channel,
                                    gfloat      *out_value);

/**
 * lrg_timeline_channel_write:
 * @node: an #LrgNode
 * @channel: a channel name
 * @value: the value to write
 *
 * Writes a channel directly (not undoable; used for playback). Writing a
 * material channel creates the material if the visual has none.
 *
 * Returns: %FALSE if the channel is invalid or the node lacks a visual
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_channel_write (LrgNode     *node,
                                     const gchar *channel,
                                     gfloat       value);

/**
 * lrg_timeline_channel_get_bone:
 * @channel: a channel name
 * @out_bone: (out) (optional) (transfer full): the bone name
 * @out_channel: (out) (optional): the transform component
 *
 * Splits a bone channel ("bone.BONE.rotation.y") into its bone name and
 * transform component.
 *
 * Returns: whether @channel is a bone channel
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_channel_get_bone (const gchar                  *channel,
                                        gchar                       **out_bone,
                                        LrgSequenceTransformChannel  *out_channel);

/**
 * lrg_timeline_get_bone_offset:
 * @node: an #LrgNode
 * @bone: a bone name
 * @out_offset: (out caller-allocates) (array fixed-size=9): location xyz,
 *   Euler rotation xyz, scale xyz
 *
 * Reads the bind-pose offset bone channels wrote onto @node (playback and
 * preview only; not saved). Without one, @out_offset is the identity
 * offset (zeros, unit scale). A viewport poses the node's skinned mesh
 * with lrg_sequence_compose_bone_offset().
 *
 * Returns: whether @node holds an offset for @bone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_get_bone_offset (LrgNode     *node,
                                       const gchar *bone,
                                       gfloat      *out_offset);

/**
 * lrg_timeline_clear_bone_offsets:
 * @node: an #LrgNode
 *
 * Forgets every bone offset written onto @node (back to the bind pose).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_clear_bone_offsets (LrgNode *node);

/* ==========================================================================
 * LrgTimelineTrack
 * ========================================================================== */

/**
 * lrg_timeline_track_new:
 * @node_guid: GUID of the animated node
 * @channel: the animated channel
 *
 * Returns: (transfer full): a new, empty track
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineTrack * lrg_timeline_track_new (const gchar *node_guid,
                                           const gchar *channel);

/**
 * lrg_timeline_track_get_node_guid:
 * @self: an #LrgTimelineTrack
 *
 * Returns: the animated node's GUID
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_timeline_track_get_node_guid (LrgTimelineTrack *self);

/**
 * lrg_timeline_track_get_channel:
 * @self: an #LrgTimelineTrack
 *
 * Returns: the animated channel
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_timeline_track_get_channel (LrgTimelineTrack *self);

/**
 * lrg_timeline_track_get_curve:
 * @self: an #LrgTimelineTrack
 *
 * Returns: (transfer none): the keyframe curve (key times in seconds)
 */
LRG_AVAILABLE_IN_ALL
LrgKeyframeCurve * lrg_timeline_track_get_curve (LrgTimelineTrack *self);

/**
 * lrg_timeline_track_get_n_keys:
 * @self: an #LrgTimelineTrack
 *
 * Returns: the number of keys
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_track_get_n_keys (LrgTimelineTrack *self);

/**
 * lrg_timeline_track_get_key:
 * @self: an #LrgTimelineTrack
 * @index: key index in time order
 * @time: (out) (optional): key time in seconds
 * @value: (out) (optional): key value
 * @easing: (out) (optional): easing to the next key
 *
 * Returns: whether @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_track_get_key (LrgTimelineTrack *self,
                                     guint             index,
                                     gfloat           *time,
                                     gfloat           *value,
                                     LrgEasingType    *easing);

/**
 * lrg_timeline_track_set_key:
 * @self: an #LrgTimelineTrack
 * @time: key time in seconds (>= 0)
 * @value: key value
 * @easing: easing to the next key
 *
 * Adds a key, replacing any key within half a millisecond of @time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_track_set_key (LrgTimelineTrack *self,
                                 gfloat            time,
                                 gfloat            value,
                                 LrgEasingType     easing);

/**
 * lrg_timeline_track_set_key_tangents:
 * @self: an #LrgTimelineTrack
 * @index: key index
 * @mode: tangent mode
 * @in_tangent: incoming slope (value per second; user/broken modes)
 * @out_tangent: outgoing slope (broken mode; else @in_tangent is used)
 *
 * Makes a key a Bézier key: the segment from it to the next key follows
 * the cubic Bézier of the two keys' handles (the next key contributes its
 * incoming slope when it is a Bézier key, else the straight secant), and
 * its easing is ignored. Constant keys hold their value. Re-keying the
 * same time with lrg_timeline_track_set_key() keeps the handles.
 *
 * Returns: whether @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_track_set_key_tangents (LrgTimelineTrack       *self,
                                              guint                   index,
                                              LrgSequenceTangentMode  mode,
                                              gfloat                  in_tangent,
                                              gfloat                  out_tangent);

/**
 * lrg_timeline_track_get_key_tangents:
 * @self: an #LrgTimelineTrack
 * @index: key index
 * @out_mode: (out) (optional): tangent mode
 * @out_in: (out) (optional): incoming user slope
 * @out_out: (out) (optional): outgoing user slope
 *
 * Returns: %TRUE when the key is a Bézier key, %FALSE when it is an eased
 *   key (or @index does not exist)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_track_get_key_tangents (LrgTimelineTrack       *self,
                                              guint                   index,
                                              LrgSequenceTangentMode *out_mode,
                                              gfloat                 *out_in,
                                              gfloat                 *out_out);

/**
 * lrg_timeline_track_clear_key_tangents:
 * @self: an #LrgTimelineTrack
 * @index: key index
 *
 * Turns a Bézier key back into an eased key.
 *
 * Returns: whether the key was a Bézier key
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_track_clear_key_tangents (LrgTimelineTrack *self,
                                                guint             index);

/**
 * lrg_timeline_track_move_key:
 * @self: an #LrgTimelineTrack
 * @index: key index
 * @time: new time in seconds (>= 0)
 *
 * Moves a key, keeping its value, easing and Bézier handles (a key
 * already at @time is replaced).
 *
 * Returns: the key's new index, or -1 if @index does not exist
 */
LRG_AVAILABLE_IN_ALL
gint lrg_timeline_track_move_key (LrgTimelineTrack *self,
                                  guint             index,
                                  gfloat            time);

/**
 * lrg_timeline_track_find_key:
 * @self: an #LrgTimelineTrack
 * @time: a time in seconds
 * @tolerance: largest accepted distance in seconds
 *
 * Returns: the index of the key nearest @time within @tolerance, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_timeline_track_find_key (LrgTimelineTrack *self,
                                  gfloat            time,
                                  gfloat            tolerance);

/**
 * lrg_timeline_track_remove_key:
 * @self: an #LrgTimelineTrack
 * @index: key index
 *
 * Returns: whether a key was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_track_remove_key (LrgTimelineTrack *self,
                                        guint             index);

/**
 * lrg_timeline_track_sample:
 * @self: an #LrgTimelineTrack
 * @time: time in seconds
 * @out_value: (out): the sampled value
 *
 * Samples the curve (clamped to the first/last key). Segments starting
 * at a Bézier key follow its handles; the others use the key's easing.
 *
 * Returns: %FALSE when the track has no keys
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_track_sample (LrgTimelineTrack *self,
                                    gfloat            time,
                                    gfloat           *out_value);

/**
 * lrg_timeline_track_copy:
 * @self: an #LrgTimelineTrack
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineTrack * lrg_timeline_track_copy (LrgTimelineTrack *self);

/**
 * lrg_timeline_track_equal:
 * @a: an #LrgTimelineTrack
 * @b: an #LrgTimelineTrack
 *
 * Returns: whether both target the same channel with identical keys
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_track_equal (LrgTimelineTrack *a,
                                   LrgTimelineTrack *b);

/* ==========================================================================
 * LrgTimeline
 * ========================================================================== */

/**
 * lrg_timeline_new:
 * @name: the timeline name (unique within a level)
 *
 * Creates an empty two-second, non-looping timeline.
 *
 * Returns: (transfer full): a new #LrgTimeline
 */
LRG_AVAILABLE_IN_ALL
LrgTimeline * lrg_timeline_new (const gchar *name);

/**
 * lrg_timeline_get_name:
 * @self: an #LrgTimeline
 *
 * Returns: the name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_timeline_get_name (LrgTimeline *self);

/**
 * lrg_timeline_set_name:
 * @self: an #LrgTimeline
 * @name: the new name
 *
 * Renames the timeline.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_set_name (LrgTimeline *self,
                            const gchar *name);

/**
 * lrg_timeline_get_length:
 * @self: an #LrgTimeline
 *
 * Returns: the playback length in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_get_length (LrgTimeline *self);

/**
 * lrg_timeline_set_length:
 * @self: an #LrgTimeline
 * @length: playback length in seconds (> 0)
 *
 * Sets the playback length (keys past it are kept but never reached).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_set_length (LrgTimeline *self,
                              gfloat       length);

/**
 * lrg_timeline_get_loop:
 * @self: an #LrgTimeline
 *
 * Returns: whether playback wraps at the end
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_get_loop (LrgTimeline *self);

/**
 * lrg_timeline_set_loop:
 * @self: an #LrgTimeline
 * @loop: whether playback wraps at the end
 *
 * Sets looping.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_set_loop (LrgTimeline *self,
                            gboolean     loop);

/**
 * lrg_timeline_get_autoplay:
 * @self: an #LrgTimeline
 *
 * Returns: whether the timeline starts playing when the level runs
 *   (play-in-editor)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_get_autoplay (LrgTimeline *self);

/**
 * lrg_timeline_set_autoplay:
 * @self: an #LrgTimeline
 * @autoplay: whether it plays when the level runs
 *
 * Sets autoplay.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_set_autoplay (LrgTimeline *self,
                                gboolean     autoplay);

/**
 * lrg_timeline_get_n_tracks:
 * @self: an #LrgTimeline
 *
 * Returns: the number of tracks
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_get_n_tracks (LrgTimeline *self);

/**
 * lrg_timeline_get_track:
 * @self: an #LrgTimeline
 * @index: a track index
 *
 * Returns: (transfer none) (nullable): the track, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineTrack * lrg_timeline_get_track (LrgTimeline *self,
                                           guint        index);

/**
 * lrg_timeline_find_track:
 * @self: an #LrgTimeline
 * @node_guid: a node GUID
 * @channel: a channel
 *
 * Returns: the index of the track animating @channel of @node_guid, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_timeline_find_track (LrgTimeline *self,
                              const gchar *node_guid,
                              const gchar *channel);

/**
 * lrg_timeline_ensure_track:
 * @self: an #LrgTimeline
 * @node_guid: a node GUID
 * @channel: a valid channel
 *
 * Returns the track for (@node_guid, @channel), appending an empty one
 * when there is none.
 *
 * Returns: (transfer none): the track
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineTrack * lrg_timeline_ensure_track (LrgTimeline *self,
                                              const gchar *node_guid,
                                              const gchar *channel);

/**
 * lrg_timeline_remove_track:
 * @self: an #LrgTimeline
 * @index: a track index
 *
 * Returns: whether a track was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_remove_track (LrgTimeline *self,
                                    guint        index);

/**
 * lrg_timeline_add_track:
 * @self: an #LrgTimeline
 * @track: (transfer none): a track
 *
 * Appends @track (a reference is taken).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_add_track (LrgTimeline      *self,
                             LrgTimelineTrack *track);

/**
 * lrg_timeline_wrap_time:
 * @self: an #LrgTimeline
 * @time: a playback time in seconds
 *
 * Maps a running playback time onto the clip: wraps it when looping,
 * clamps it to [0, length] otherwise.
 *
 * Returns: the clip time
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_wrap_time (LrgTimeline *self,
                               gfloat       time);

/**
 * lrg_timeline_apply:
 * @self: an #LrgTimeline
 * @root: the level root (tracks find their node by GUID beneath it)
 * @time: clip time in seconds
 *
 * Samples every track at @time and writes the values onto the nodes
 * (not undoable: this is playback/preview).
 *
 * Returns: the number of channels written
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_apply (LrgTimeline *self,
                          LrgNode     *root,
                          gfloat       time);

/**
 * lrg_timeline_copy:
 * @self: an #LrgTimeline
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgTimeline * lrg_timeline_copy (LrgTimeline *self);

/**
 * lrg_timeline_equal:
 * @a: (nullable): an #LrgTimeline
 * @b: (nullable): an #LrgTimeline
 *
 * Returns: whether both are %NULL or hold the same settings and tracks
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_equal (LrgTimeline *a,
                             LrgTimeline *b);

G_END_DECLS
