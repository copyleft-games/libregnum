/* lrg-sequence-player.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequencePlayer: plays an #LrgSequence into an #LrgWorld3D.
 *
 * The player owns the playhead (time, direction, play mode, rate) and
 * evaluates the sequence onto the world's entities: transform and property
 * tracks write values, camera cut tracks switch the world's active camera,
 * audio tracks start sounds on audio sources, animation tracks sample
 * clips onto skeletons, sub-sequence tracks recurse, and event tracks emit
 * the #LrgSequencePlayer::event signal when the playhead crosses them, in
 * either direction (playing, ping-pong, or scrubbing backwards).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-sequence.h"
#include "lrg-skeleton.h"
#include "lrg-animation-clip.h"
#include "../world3d/lrg-world3d.h"
#include "../world3d/lrg-entity3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_SEQUENCE_PLAYER (lrg_sequence_player_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSequencePlayer, lrg_sequence_player, LRG, SEQUENCE_PLAYER, GObject)

/**
 * LRG_SEQUENCE_MAX_DEPTH:
 *
 * How deep sub-sequences may nest before evaluation stops (guards against
 * a sequence that contains itself).
 */
#define LRG_SEQUENCE_MAX_DEPTH (8)

/**
 * lrg_sequence_player_new:
 * @sequence: (nullable): the sequence to play
 *
 * Creates a stopped player at time 0 whose play mode is the sequence's.
 *
 * Returns: (transfer full): a new #LrgSequencePlayer
 */
LRG_AVAILABLE_IN_ALL
LrgSequencePlayer * lrg_sequence_player_new (LrgSequence *sequence);

/**
 * lrg_sequence_player_get_sequence:
 * @self: an #LrgSequencePlayer
 *
 * Returns: (transfer none) (nullable): the sequence
 */
LRG_AVAILABLE_IN_ALL
LrgSequence * lrg_sequence_player_get_sequence (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_set_sequence:
 * @self: an #LrgSequencePlayer
 * @sequence: (nullable): a sequence
 *
 * Replaces the sequence: stops, rewinds and takes the new sequence's
 * play mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_set_sequence (LrgSequencePlayer *self,
                                       LrgSequence       *sequence);

/**
 * lrg_sequence_player_get_world:
 * @self: an #LrgSequencePlayer
 *
 * Returns: (transfer none) (nullable): the world bindings resolve in
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3D * lrg_sequence_player_get_world (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_set_world:
 * @self: an #LrgSequencePlayer
 * @world: (nullable): the world to drive (held weakly)
 *
 * Sets the world whose entities the tracks' GUIDs resolve to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_set_world (LrgSequencePlayer *self,
                                    LrgWorld3D        *world);

/**
 * lrg_sequence_player_play:
 * @self: an #LrgSequencePlayer
 *
 * Starts (or resumes) playback and evaluates the current time. A
 * once-mode player at its end rewinds first.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_play (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_pause:
 * @self: an #LrgSequencePlayer
 *
 * Stops advancing; the pose, camera and sounds stay.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_pause (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_stop:
 * @self: an #LrgSequencePlayer
 *
 * Stops playback, stops the sounds the player started, hands the world's
 * camera back (lrg_world3d_set_active_camera() with %NULL, i.e. priority
 * order) and rewinds to 0 without evaluating (entities keep their last
 * pose).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_stop (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_is_playing:
 * @self: an #LrgSequencePlayer
 *
 * Returns: whether the playhead advances in lrg_sequence_player_advance()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_player_is_playing (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_advance:
 * @self: an #LrgSequencePlayer
 * @delta: elapsed real time in seconds
 *
 * Moves the playhead by @delta times the rate (when playing), handling
 * the end per play mode, emits the events and starts the sounds crossed on
 * the way, then evaluates. Call once per frame (#LrgSequenceComponent
 * does).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_advance (LrgSequencePlayer *self,
                                  gfloat             delta);

/**
 * lrg_sequence_player_scrub:
 * @self: an #LrgSequencePlayer
 * @time: the new time in seconds (clamped to the sequence)
 *
 * Moves the playhead like a user dragging it: events between the old and
 * the new time are emitted (in reverse order, with reverse = %TRUE, when
 * moving backwards), sounds are not started, then the sequence is
 * evaluated at @time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_scrub (LrgSequencePlayer *self,
                                gfloat             time);

/**
 * lrg_sequence_player_get_time:
 * @self: an #LrgSequencePlayer
 *
 * Returns: the playhead in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sequence_player_get_time (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_set_time:
 * @self: an #LrgSequencePlayer
 * @time: the new time in seconds (clamped to the sequence)
 *
 * Jumps without emitting events and evaluates. An event exactly at @time
 * fires on the next advance or scrub away from it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_set_time (LrgSequencePlayer *self,
                                   gfloat             time);

/**
 * lrg_sequence_player_evaluate:
 * @self: an #LrgSequencePlayer
 *
 * Applies the sequence at the current time (no events, no sounds).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_evaluate (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_get_rate:
 * @self: an #LrgSequencePlayer
 *
 * Returns: the playback rate (1 = real time; negative plays backwards)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sequence_player_get_rate (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_set_rate:
 * @self: an #LrgSequencePlayer
 * @rate: the playback rate
 *
 * Sets the playback rate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_set_rate (LrgSequencePlayer *self,
                                   gfloat             rate);

/**
 * lrg_sequence_player_get_play_mode:
 * @self: an #LrgSequencePlayer
 *
 * Returns: the play mode
 */
LRG_AVAILABLE_IN_ALL
LrgSequencePlayMode lrg_sequence_player_get_play_mode (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_set_play_mode:
 * @self: an #LrgSequencePlayer
 * @mode: once, loop or ping-pong
 *
 * Overrides the sequence's play mode for this player.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_set_play_mode (LrgSequencePlayer   *self,
                                        LrgSequencePlayMode  mode);

/**
 * lrg_sequence_player_get_direction:
 * @self: an #LrgSequencePlayer
 *
 * Returns: 1 while the ping-pong playhead runs forwards, -1 backwards
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sequence_player_get_direction (LrgSequencePlayer *self);

/**
 * lrg_sequence_player_set_binding_override:
 * @self: an #LrgSequencePlayer
 * @binding: a GUID or a placeholder name used as a track binding
 * @entity: (nullable): the entity to use instead (held weakly), or %NULL
 *   to remove the override
 *
 * Redirects a binding, e.g. to play one sequence on several spawned
 * actors. #LrgSequenceComponent overrides "self" with its entity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_set_binding_override (LrgSequencePlayer *self,
                                               const gchar       *binding,
                                               LrgEntity3D       *entity);

/**
 * lrg_sequence_player_resolve_binding:
 * @self: an #LrgSequencePlayer
 * @binding: a track binding
 *
 * Returns: (transfer none) (nullable): the override, else the world's
 *   entity with that GUID, else %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_sequence_player_resolve_binding (LrgSequencePlayer *self,
                                                   const gchar       *binding);

/**
 * lrg_sequence_player_bind_skeleton:
 * @self: an #LrgSequencePlayer
 * @binding: a track binding
 * @skeleton: (nullable): the skeleton animation and bone tracks of that
 *   binding pose, or %NULL to unbind
 *
 * Gives a binding a skeleton. Animation clip sections are sampled onto it
 * and bone tracks offset its bones, then its world poses are
 * recalculated; the caller uploads the palette (for example
 * lrg_skinned_mesh_apply_skeleton()). An #LrgSequencePlayer::animation-sample
 * handler returning %TRUE replaces this default path (for example to
 * drive an animator component).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_bind_skeleton (LrgSequencePlayer *self,
                                        const gchar       *binding,
                                        LrgSkeleton       *skeleton);

/**
 * lrg_sequence_player_get_skeleton:
 * @self: an #LrgSequencePlayer
 * @binding: a track binding
 *
 * Returns: (transfer none) (nullable): the bound skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton * lrg_sequence_player_get_skeleton (LrgSequencePlayer *self,
                                                const gchar       *binding);

/**
 * lrg_sequence_player_add_clip:
 * @self: an #LrgSequencePlayer
 * @clip: an animation clip, found by animation sections through its name
 *
 * Registers a clip (replacing one of the same name).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_add_clip (LrgSequencePlayer *self,
                                   LrgAnimationClip  *clip);

/**
 * lrg_sequence_player_load_clips:
 * @self: an #LrgSequencePlayer
 * @path: (type filename): a .gltf or .glb file
 * @error: return location for a #GError
 *
 * Registers every animation of a glTF file (lrg_gltf_load_clips()).
 *
 * Returns: the number of clips registered, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sequence_player_load_clips (LrgSequencePlayer  *self,
                                     const gchar        *path,
                                     GError            **error);

/**
 * lrg_sequence_player_get_clip:
 * @self: an #LrgSequencePlayer
 * @name: a clip name
 *
 * Returns: (transfer none) (nullable): the registered clip
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationClip * lrg_sequence_player_get_clip (LrgSequencePlayer *self,
                                                 const gchar       *name);

/**
 * lrg_sequence_player_add_sequence:
 * @self: an #LrgSequencePlayer
 * @name: the name sub-sequence sections use
 * @sequence: the sequence
 *
 * Registers a sub-sequence by name (checked before `.rseq` paths).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_player_add_sequence (LrgSequencePlayer *self,
                                       const gchar       *name,
                                       LrgSequence       *sequence);

/**
 * lrg_sequence_player_get_camera_binding:
 * @self: an #LrgSequencePlayer
 *
 * Returns: (nullable): the binding of the camera the current cut made
 *   active, or %NULL when the player does not own the camera
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_player_get_camera_binding (LrgSequencePlayer *self);

G_END_DECLS
