/* lrg-sequence.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequence: the `.rseq` cinematic sequence asset.
 *
 * A sequence is plain data: a length, a frame rate, a default play mode,
 * a list of entity bindings (GUIDs of #LrgEntity3D in an #LrgWorld3D, with
 * a display name for the editor) and a list of #LrgSequenceTrack s. Tracks
 * come in seven kinds (#LrgSequenceTrackKind): keyed transform and
 * property tracks hold #LrgSequenceCurve s (Bézier keys with tangents);
 * camera cut, audio, event, animation clip and sub-sequence tracks hold
 * timed #LrgSequenceSection s. #LrgSequencePlayer plays a sequence into a
 * world; #LrgSequenceComponent plays one from an entity.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-bone-pose.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Errors and enumerations
 * ========================================================================== */

/**
 * LRG_SEQUENCE_ERROR:
 *
 * Error domain for loading and saving `.rseq` sequences.
 */
#define LRG_SEQUENCE_ERROR (lrg_sequence_error_quark ())

/**
 * lrg_sequence_error_quark:
 *
 * Returns: the #GQuark of the #LRG_SEQUENCE_ERROR domain
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_sequence_error_quark (void);

/**
 * LrgSequenceError:
 * @LRG_SEQUENCE_ERROR_IO: the file could not be read or written
 * @LRG_SEQUENCE_ERROR_PARSE: the text is not valid YAML
 * @LRG_SEQUENCE_ERROR_INVALID: the YAML is not a sequence document
 * @LRG_SEQUENCE_ERROR_VERSION: the file was written by a newer format
 *
 * Error codes of the #LRG_SEQUENCE_ERROR domain.
 */
typedef enum
{
	LRG_SEQUENCE_ERROR_IO,
	LRG_SEQUENCE_ERROR_PARSE,
	LRG_SEQUENCE_ERROR_INVALID,
	LRG_SEQUENCE_ERROR_VERSION
} LrgSequenceError;

/**
 * LRG_SEQUENCE_FORMAT_VERSION:
 *
 * The `.rseq` format version this library writes (and the newest it
 * reads).
 */
#define LRG_SEQUENCE_FORMAT_VERSION (1)

/**
 * LrgSequenceTrackKind:
 * @LRG_SEQUENCE_TRACK_TRANSFORM: keyed location/rotation/scale of a bound
 *   entity, or of one bone of its skeleton
 * @LRG_SEQUENCE_TRACK_PROPERTY: one keyed numeric #GObject property of the
 *   bound entity or of one of its components
 * @LRG_SEQUENCE_TRACK_CAMERA_CUT: sections switching the world's active
 *   camera to a bound camera entity
 * @LRG_SEQUENCE_TRACK_AUDIO: sections starting a sound on the bound
 *   entity's audio source
 * @LRG_SEQUENCE_TRACK_EVENT: named events emitted as signals when the
 *   playhead crosses them
 * @LRG_SEQUENCE_TRACK_ANIMATION: animation clip sections sampled onto the
 *   bound entity's skeleton
 * @LRG_SEQUENCE_TRACK_SUBSEQUENCE: sections playing another sequence
 *
 * The kinds of #LrgSequenceTrack. The YAML names are the nicks
 * ("transform", "property", "camera-cut", "audio", "event", "animation",
 * "subsequence").
 */
typedef enum
{
	LRG_SEQUENCE_TRACK_TRANSFORM,
	LRG_SEQUENCE_TRACK_PROPERTY,
	LRG_SEQUENCE_TRACK_CAMERA_CUT,
	LRG_SEQUENCE_TRACK_AUDIO,
	LRG_SEQUENCE_TRACK_EVENT,
	LRG_SEQUENCE_TRACK_ANIMATION,
	LRG_SEQUENCE_TRACK_SUBSEQUENCE
} LrgSequenceTrackKind;

/**
 * lrg_sequence_track_kind_get_type:
 *
 * Returns: the #GType of #LrgSequenceTrackKind
 */
LRG_AVAILABLE_IN_ALL
GType lrg_sequence_track_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SEQUENCE_TRACK_KIND (lrg_sequence_track_kind_get_type ())

/**
 * LrgSequenceTangentMode:
 * @LRG_SEQUENCE_TANGENT_AUTO: smooth slope from the neighbouring keys
 *   (non-uniform Catmull-Rom, clamped so the curve never overshoots; flat
 *   at the first and last key and at local extrema)
 * @LRG_SEQUENCE_TANGENT_USER: one user slope used on both sides (aligned
 *   handles)
 * @LRG_SEQUENCE_TANGENT_BROKEN: independent incoming and outgoing user
 *   slopes
 * @LRG_SEQUENCE_TANGENT_LINEAR: the slope points straight at the
 *   neighbouring key on each side (a segment between two linear keys is a
 *   straight line)
 * @LRG_SEQUENCE_TANGENT_CONSTANT: the value is held until the next key (a
 *   step); the incoming side behaves as flat
 *
 * How a key's Bézier handles are found. Slopes are in value units per
 * second. Each segment is the cubic Bézier whose inner control points sit
 * one third of the segment duration along the two keys' slopes (the
 * Hermite form); handle weights are not supported. The YAML names are the
 * nicks ("auto", "user", "broken", "linear", "constant").
 */
typedef enum
{
	LRG_SEQUENCE_TANGENT_AUTO,
	LRG_SEQUENCE_TANGENT_USER,
	LRG_SEQUENCE_TANGENT_BROKEN,
	LRG_SEQUENCE_TANGENT_LINEAR,
	LRG_SEQUENCE_TANGENT_CONSTANT
} LrgSequenceTangentMode;

/**
 * lrg_sequence_tangent_mode_get_type:
 *
 * Returns: the #GType of #LrgSequenceTangentMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_sequence_tangent_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SEQUENCE_TANGENT_MODE (lrg_sequence_tangent_mode_get_type ())

/**
 * LrgSequencePlayMode:
 * @LRG_SEQUENCE_PLAY_ONCE: play to the end (or the start, backwards) and
 *   stop
 * @LRG_SEQUENCE_PLAY_LOOP: wrap from the end to the start
 * @LRG_SEQUENCE_PLAY_PING_PONG: reverse direction at either end
 *
 * What a player does at the end of a sequence. The YAML names are the
 * nicks ("once", "loop", "ping-pong").
 */
typedef enum
{
	LRG_SEQUENCE_PLAY_ONCE,
	LRG_SEQUENCE_PLAY_LOOP,
	LRG_SEQUENCE_PLAY_PING_PONG
} LrgSequencePlayMode;

/**
 * lrg_sequence_play_mode_get_type:
 *
 * Returns: the #GType of #LrgSequencePlayMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_sequence_play_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SEQUENCE_PLAY_MODE (lrg_sequence_play_mode_get_type ())

/**
 * LrgSequenceTransformChannel:
 * @LRG_SEQUENCE_CHANNEL_LOCATION_X: local X position ("location.x")
 * @LRG_SEQUENCE_CHANNEL_LOCATION_Y: local Y position ("location.y")
 * @LRG_SEQUENCE_CHANNEL_LOCATION_Z: local Z position ("location.z")
 * @LRG_SEQUENCE_CHANNEL_ROTATION_X: Euler X in radians ("rotation.x")
 * @LRG_SEQUENCE_CHANNEL_ROTATION_Y: Euler Y in radians ("rotation.y")
 * @LRG_SEQUENCE_CHANNEL_ROTATION_Z: Euler Z in radians ("rotation.z")
 * @LRG_SEQUENCE_CHANNEL_SCALE_X: local X scale ("scale.x")
 * @LRG_SEQUENCE_CHANNEL_SCALE_Y: local Y scale ("scale.y")
 * @LRG_SEQUENCE_CHANNEL_SCALE_Z: local Z scale ("scale.z")
 *
 * The nine curves of a transform track. On a bone track the values are
 * offsets from the bone's bind pose (location added, rotation applied
 * after the bind rotation, scale multiplied), the same convention as the
 * level timeline's bone channels.
 */
typedef enum
{
	LRG_SEQUENCE_CHANNEL_LOCATION_X,
	LRG_SEQUENCE_CHANNEL_LOCATION_Y,
	LRG_SEQUENCE_CHANNEL_LOCATION_Z,
	LRG_SEQUENCE_CHANNEL_ROTATION_X,
	LRG_SEQUENCE_CHANNEL_ROTATION_Y,
	LRG_SEQUENCE_CHANNEL_ROTATION_Z,
	LRG_SEQUENCE_CHANNEL_SCALE_X,
	LRG_SEQUENCE_CHANNEL_SCALE_Y,
	LRG_SEQUENCE_CHANNEL_SCALE_Z
} LrgSequenceTransformChannel;

/**
 * LRG_SEQUENCE_N_CHANNELS:
 *
 * The number of #LrgSequenceTransformChannel values.
 */
#define LRG_SEQUENCE_N_CHANNELS (9)

/**
 * lrg_sequence_transform_channel_get_type:
 *
 * Returns: the #GType of #LrgSequenceTransformChannel
 */
LRG_AVAILABLE_IN_ALL
GType lrg_sequence_transform_channel_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SEQUENCE_TRANSFORM_CHANNEL (lrg_sequence_transform_channel_get_type ())

/**
 * lrg_sequence_transform_channel_to_string:
 * @channel: a #LrgSequenceTransformChannel
 *
 * Returns: (nullable): the channel name ("location.x" ...), or %NULL for
 *   an invalid value
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_transform_channel_to_string (LrgSequenceTransformChannel channel);

/**
 * lrg_sequence_transform_channel_from_string:
 * @name: a channel name ("location.x" ... "scale.z")
 * @out_channel: (out): the channel
 *
 * Returns: whether @name is a transform channel
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_transform_channel_from_string (const gchar                 *name,
                                                     LrgSequenceTransformChannel *out_channel);

/* ==========================================================================
 * LrgSequenceKey and LrgSequenceSection (boxed)
 * ========================================================================== */

typedef struct _LrgSequenceKey LrgSequenceKey;

/**
 * LrgSequenceKey:
 * @time: key time in seconds
 * @value: key value
 * @tangent_mode: how the handles are found
 * @in_tangent: incoming slope (value per second), used by
 *   %LRG_SEQUENCE_TANGENT_USER and %LRG_SEQUENCE_TANGENT_BROKEN
 * @out_tangent: outgoing slope (value per second), used by
 *   %LRG_SEQUENCE_TANGENT_BROKEN (equal to @in_tangent for
 *   %LRG_SEQUENCE_TANGENT_USER)
 *
 * One key of an #LrgSequenceCurve.
 */
struct _LrgSequenceKey
{
	gfloat                 time;
	gfloat                 value;
	LrgSequenceTangentMode tangent_mode;
	gfloat                 in_tangent;
	gfloat                 out_tangent;
};

/**
 * lrg_sequence_key_get_type:
 *
 * Returns: the boxed #GType of #LrgSequenceKey
 */
LRG_AVAILABLE_IN_ALL
GType lrg_sequence_key_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SEQUENCE_KEY (lrg_sequence_key_get_type ())

/**
 * lrg_sequence_key_copy:
 * @key: an #LrgSequenceKey
 *
 * Returns: (transfer full): a copy of @key
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceKey * lrg_sequence_key_copy (const LrgSequenceKey *key);

/**
 * lrg_sequence_key_free:
 * @key: an #LrgSequenceKey from lrg_sequence_key_copy()
 *
 * Frees @key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_key_free (LrgSequenceKey *key);

typedef struct _LrgSequenceSection LrgSequenceSection;

/**
 * LrgSequenceSection:
 * @start: start time in the sequence, in seconds
 * @duration: length in seconds; 0 means "until the next section of the
 *   track, or the end of the sequence" (camera cuts and animation), "until
 *   the sound ends" (audio) or "a point in time" (events)
 * @name: what the section refers to: the camera entity GUID (camera cut),
 *   the sound ("bank:sound" or a file path; audio), the event name
 *   (event), the clip name (animation) or the sequence name or `.rseq`
 *   path (subsequence)
 * @payload: (nullable): free text passed with an event
 * @offset: time into the clip or sub-sequence at @start, in seconds
 * @rate: playback rate of the clip or sub-sequence (1 = real time)
 * @weight: blend weight of an animation clip (0..1) or volume of a sound
 * @loop: whether a clip or sub-sequence wraps inside the section
 *
 * A timed block of a camera cut, audio, event, animation or sub-sequence
 * track. Only the fields a kind uses are saved.
 */
struct _LrgSequenceSection
{
	gfloat    start;
	gfloat    duration;
	gchar    *name;
	gchar    *payload;
	gfloat    offset;
	gfloat    rate;
	gfloat    weight;
	gboolean  loop;
};

/**
 * lrg_sequence_section_get_type:
 *
 * Returns: the boxed #GType of #LrgSequenceSection
 */
LRG_AVAILABLE_IN_ALL
GType lrg_sequence_section_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SEQUENCE_SECTION (lrg_sequence_section_get_type ())

/**
 * lrg_sequence_section_new:
 * @start: start time in seconds
 * @name: (nullable): what the section refers to (see #LrgSequenceSection)
 *
 * Creates a section with duration 0, no payload, offset 0, rate 1,
 * weight 1 and no looping.
 *
 * Returns: (transfer full): a new #LrgSequenceSection
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceSection * lrg_sequence_section_new (gfloat       start,
                                               const gchar *name);

/**
 * lrg_sequence_section_copy:
 * @section: an #LrgSequenceSection
 *
 * Returns: (transfer full): a deep copy of @section
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceSection * lrg_sequence_section_copy (const LrgSequenceSection *section);

/**
 * lrg_sequence_section_free:
 * @section: an #LrgSequenceSection
 *
 * Frees @section and its strings.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_section_free (LrgSequenceSection *section);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSequenceSection, lrg_sequence_section_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSequenceKey, lrg_sequence_key_free)

/* ==========================================================================
 * LrgSequenceCurve
 * ========================================================================== */

#define LRG_TYPE_SEQUENCE_CURVE (lrg_sequence_curve_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSequenceCurve, lrg_sequence_curve, LRG, SEQUENCE_CURVE, GObject)

/**
 * lrg_sequence_curve_new:
 *
 * Returns: (transfer full): a new curve without keys
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceCurve * lrg_sequence_curve_new (void);

/**
 * lrg_sequence_curve_get_n_keys:
 * @self: an #LrgSequenceCurve
 *
 * Returns: the number of keys
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_curve_get_n_keys (LrgSequenceCurve *self);

/**
 * lrg_sequence_curve_get_key:
 * @self: an #LrgSequenceCurve
 * @index: a key index in time order
 * @out_key: (out caller-allocates): the key
 *
 * Returns: whether @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_curve_get_key (LrgSequenceCurve *self,
                                     guint             index,
                                     LrgSequenceKey   *out_key);

/**
 * lrg_sequence_curve_set_key:
 * @self: an #LrgSequenceCurve
 * @time: key time in seconds
 * @value: key value
 * @mode: tangent mode
 *
 * Adds a key, or updates the key within half a millisecond of @time
 * (keeping its user slopes). User slopes of a new key are 0.
 *
 * Returns: the key's index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_curve_set_key (LrgSequenceCurve       *self,
                                  gfloat                  time,
                                  gfloat                  value,
                                  LrgSequenceTangentMode  mode);

/**
 * lrg_sequence_curve_set_key_tangents:
 * @self: an #LrgSequenceCurve
 * @index: a key index
 * @mode: tangent mode
 * @in_tangent: incoming slope (value per second)
 * @out_tangent: outgoing slope; ignored (set to @in_tangent) unless @mode
 *   is %LRG_SEQUENCE_TANGENT_BROKEN
 *
 * Sets a key's handles.
 *
 * Returns: whether @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_curve_set_key_tangents (LrgSequenceCurve       *self,
                                              guint                   index,
                                              LrgSequenceTangentMode  mode,
                                              gfloat                  in_tangent,
                                              gfloat                  out_tangent);

/**
 * lrg_sequence_curve_insert_key:
 * @self: an #LrgSequenceCurve
 * @key: a full key
 *
 * Adds @key with its handles, replacing any key within half a millisecond
 * of its time.
 *
 * Returns: the key's index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_curve_insert_key (LrgSequenceCurve     *self,
                                     const LrgSequenceKey *key);

/**
 * lrg_sequence_curve_move_key:
 * @self: an #LrgSequenceCurve
 * @index: a key index
 * @time: the new time in seconds
 *
 * Moves a key in time, keeping its value and handles (a key already at
 * @time is replaced).
 *
 * Returns: the key's new index, or -1 if @index does not exist
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sequence_curve_move_key (LrgSequenceCurve *self,
                                  guint             index,
                                  gfloat            time);

/**
 * lrg_sequence_curve_remove_key:
 * @self: an #LrgSequenceCurve
 * @index: a key index
 *
 * Returns: whether a key was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_curve_remove_key (LrgSequenceCurve *self,
                                        guint             index);

/**
 * lrg_sequence_curve_find_key:
 * @self: an #LrgSequenceCurve
 * @time: a time in seconds
 * @tolerance: largest accepted distance in seconds
 *
 * Returns: the index of the key nearest @time within @tolerance, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sequence_curve_find_key (LrgSequenceCurve *self,
                                  gfloat            time,
                                  gfloat            tolerance);

/**
 * lrg_sequence_curve_clear:
 * @self: an #LrgSequenceCurve
 *
 * Removes every key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_curve_clear (LrgSequenceCurve *self);

/**
 * lrg_sequence_curve_get_slopes:
 * @self: an #LrgSequenceCurve
 * @index: a key index
 * @out_in: (out) (optional): the resolved incoming slope
 * @out_out: (out) (optional): the resolved outgoing slope
 *
 * Gets the slopes evaluation actually uses for a key (auto and linear
 * modes resolved against the neighbours, constant keys flat). An editor
 * draws the Bézier handles from these: the handle points sit at
 * (t ∓ dt/3, v ∓ slope * dt/3) for the neighbouring segment's dt.
 *
 * Returns: whether @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_curve_get_slopes (LrgSequenceCurve *self,
                                        guint             index,
                                        gfloat           *out_in,
                                        gfloat           *out_out);

/**
 * lrg_sequence_curve_evaluate:
 * @self: an #LrgSequenceCurve
 * @time: time in seconds
 *
 * Samples the curve: the first key's value before it, the last key's value
 * after it, 0 without keys.
 *
 * Returns: the value at @time
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sequence_curve_evaluate (LrgSequenceCurve *self,
                                    gfloat            time);

/**
 * lrg_sequence_curve_get_time_range:
 * @self: an #LrgSequenceCurve
 * @out_start: (out) (optional): first key time
 * @out_end: (out) (optional): last key time
 *
 * Returns: %FALSE when the curve has no keys
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_curve_get_time_range (LrgSequenceCurve *self,
                                            gfloat           *out_start,
                                            gfloat           *out_end);

/**
 * lrg_sequence_curve_copy:
 * @self: an #LrgSequenceCurve
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceCurve * lrg_sequence_curve_copy (LrgSequenceCurve *self);

/**
 * lrg_sequence_curve_equal:
 * @a: (nullable): an #LrgSequenceCurve
 * @b: (nullable): an #LrgSequenceCurve
 *
 * Returns: whether both are %NULL or hold identical keys
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_curve_equal (LrgSequenceCurve *a,
                                   LrgSequenceCurve *b);

/**
 * lrg_sequence_curve_hermite:
 * @t0: start key time
 * @v0: start key value
 * @out_slope: outgoing slope of the start key (value per second)
 * @t1: end key time (> @t0)
 * @v1: end key value
 * @in_slope: incoming slope of the end key (value per second)
 * @time: the time to evaluate, in [@t0, @t1]
 *
 * Evaluates one cubic segment: the Bézier with control points v0,
 * v0 + out_slope * dt / 3, v1 - in_slope * dt / 3, v1 (dt = t1 - t0),
 * parameterised uniformly in time. Shared by #LrgSequenceCurve and the
 * level timeline's Bézier keys.
 *
 * Returns: the value at @time
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sequence_curve_hermite (gfloat t0,
                                   gfloat v0,
                                   gfloat out_slope,
                                   gfloat t1,
                                   gfloat v1,
                                   gfloat in_slope,
                                   gfloat time);

/**
 * lrg_sequence_curve_auto_slope:
 * @has_prev: whether there is a previous key
 * @t_prev: previous key time
 * @v_prev: previous key value
 * @t: this key's time
 * @v: this key's value
 * @has_next: whether there is a next key
 * @t_next: next key time
 * @v_next: next key value
 *
 * The %LRG_SEQUENCE_TANGENT_AUTO slope of a key: 0 at the ends and at
 * local extrema, else the non-uniform Catmull-Rom slope
 * (v_next - v_prev) / (t_next - t_prev) limited to three times the smaller
 * neighbouring secant (so a monotone run of keys stays monotone).
 *
 * Returns: the slope in value units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sequence_curve_auto_slope (gboolean has_prev,
                                      gfloat   t_prev,
                                      gfloat   v_prev,
                                      gfloat   t,
                                      gfloat   v,
                                      gboolean has_next,
                                      gfloat   t_next,
                                      gfloat   v_next);

/* ==========================================================================
 * LrgSequenceTrack
 * ========================================================================== */

#define LRG_TYPE_SEQUENCE_TRACK (lrg_sequence_track_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSequenceTrack, lrg_sequence_track, LRG, SEQUENCE_TRACK, GObject)

/**
 * lrg_sequence_track_new:
 * @kind: the track kind
 * @name: (nullable): a display name
 *
 * Returns: (transfer full): a new, empty track
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceTrack * lrg_sequence_track_new (LrgSequenceTrackKind  kind,
                                           const gchar          *name);

/**
 * lrg_sequence_track_get_kind:
 * @self: an #LrgSequenceTrack
 *
 * Returns: the track kind
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceTrackKind lrg_sequence_track_get_kind (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_get_name:
 * @self: an #LrgSequenceTrack
 *
 * Returns: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_track_get_name (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_set_name:
 * @self: an #LrgSequenceTrack
 * @name: (nullable): a display name
 *
 * Renames the track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_track_set_name (LrgSequenceTrack *self,
                                  const gchar      *name);

/**
 * lrg_sequence_track_get_binding:
 * @self: an #LrgSequenceTrack
 *
 * Returns: (nullable): the GUID of the bound entity (transform, property,
 *   audio and animation tracks), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_track_get_binding (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_set_binding:
 * @self: an #LrgSequenceTrack
 * @guid: (nullable): an entity GUID (or a player binding-override name
 *   such as "self")
 *
 * Binds the track to an entity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_track_set_binding (LrgSequenceTrack *self,
                                     const gchar      *guid);

/**
 * lrg_sequence_track_get_muted:
 * @self: an #LrgSequenceTrack
 *
 * Returns: whether playback skips the track
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_track_get_muted (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_set_muted:
 * @self: an #LrgSequenceTrack
 * @muted: whether playback skips the track
 *
 * Mutes or unmutes the track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_track_set_muted (LrgSequenceTrack *self,
                                   gboolean          muted);

/**
 * lrg_sequence_track_get_bone:
 * @self: a transform #LrgSequenceTrack
 *
 * Returns: (nullable): the animated bone name, or %NULL when the track
 *   moves the entity itself
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_track_get_bone (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_set_bone:
 * @self: a transform #LrgSequenceTrack
 * @bone: (nullable): a bone name of the bound entity's skeleton
 *
 * Makes a transform track a bone track (values become bind-pose offsets,
 * see #LrgSequenceTransformChannel).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_track_set_bone (LrgSequenceTrack *self,
                                  const gchar      *bone);

/**
 * lrg_sequence_track_get_channel_curve:
 * @self: a transform #LrgSequenceTrack
 * @channel: a channel
 *
 * Returns: (transfer none) (nullable): the channel's curve, or %NULL when
 *   the channel is not animated
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceCurve * lrg_sequence_track_get_channel_curve (LrgSequenceTrack            *self,
                                                         LrgSequenceTransformChannel  channel);

/**
 * lrg_sequence_track_ensure_channel_curve:
 * @self: a transform #LrgSequenceTrack
 * @channel: a channel
 *
 * Returns: (transfer none): the channel's curve, created empty when missing
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceCurve * lrg_sequence_track_ensure_channel_curve (LrgSequenceTrack            *self,
                                                            LrgSequenceTransformChannel  channel);

/**
 * lrg_sequence_track_remove_channel_curve:
 * @self: a transform #LrgSequenceTrack
 * @channel: a channel
 *
 * Returns: whether the channel had a curve
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_track_remove_channel_curve (LrgSequenceTrack            *self,
                                                  LrgSequenceTransformChannel  channel);

/**
 * lrg_sequence_track_get_component_type:
 * @self: a property #LrgSequenceTrack
 *
 * Returns: (nullable): the #GType name of the animated component (e.g.
 *   "LrgLightComponent"), or %NULL to animate the entity itself
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_track_get_component_type (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_set_component_type:
 * @self: a property #LrgSequenceTrack
 * @type_name: (nullable): a component #GType name, or %NULL for the entity
 *
 * Sets which object of the bound entity the property lives on (the first
 * component of that type, subclasses included).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_track_set_component_type (LrgSequenceTrack *self,
                                            const gchar      *type_name);

/**
 * lrg_sequence_track_get_property_name:
 * @self: a property #LrgSequenceTrack
 *
 * Returns: (nullable): the animated property name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_track_get_property_name (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_set_property_name:
 * @self: a property #LrgSequenceTrack
 * @property: (nullable): a writable numeric, boolean or enum property
 *
 * Sets the animated property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_track_set_property_name (LrgSequenceTrack *self,
                                           const gchar      *property);

/**
 * lrg_sequence_track_get_curve:
 * @self: a property #LrgSequenceTrack
 *
 * Returns: (transfer none) (nullable): the property curve (%NULL for
 *   other kinds)
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceCurve * lrg_sequence_track_get_curve (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_add_section:
 * @self: a camera cut, audio, event, animation or sub-sequence track
 * @section: a section (copied)
 *
 * Inserts a section in start-time order (after sections with the same
 * start).
 *
 * Returns: the section's index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_track_add_section (LrgSequenceTrack         *self,
                                      const LrgSequenceSection *section);

/**
 * lrg_sequence_track_get_n_sections:
 * @self: an #LrgSequenceTrack
 *
 * Returns: the number of sections
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_track_get_n_sections (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_get_section:
 * @self: an #LrgSequenceTrack
 * @index: a section index in start order
 *
 * Returns: (transfer none) (nullable): the section, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const LrgSequenceSection * lrg_sequence_track_get_section (LrgSequenceTrack *self,
                                                           guint             index);

/**
 * lrg_sequence_track_remove_section:
 * @self: an #LrgSequenceTrack
 * @index: a section index
 *
 * Returns: whether a section was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_track_remove_section (LrgSequenceTrack *self,
                                            guint             index);

/**
 * lrg_sequence_track_get_section_end:
 * @self: an #LrgSequenceTrack
 * @index: a section index
 * @sequence_length: the owning sequence's length
 *
 * Gets where a section stops: start + duration, or with duration 0 the
 * next section's start (else @sequence_length). Events end where they
 * start.
 *
 * Returns: the end time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sequence_track_get_section_end (LrgSequenceTrack *self,
                                           guint             index,
                                           gfloat            sequence_length);

/**
 * lrg_sequence_track_find_section_at:
 * @self: an #LrgSequenceTrack
 * @time: a time in seconds
 * @sequence_length: the owning sequence's length
 *
 * Returns: the index of the last section active at @time (start <= time <
 *   end; the end of the sequence counts as inside), or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sequence_track_find_section_at (LrgSequenceTrack *self,
                                         gfloat            time,
                                         gfloat            sequence_length);

/**
 * lrg_sequence_track_add_event:
 * @self: an event track
 * @time: event time in seconds
 * @name: the event name
 * @payload: (nullable): free text passed to the signal
 *
 * Returns: the section index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_track_add_event (LrgSequenceTrack *self,
                                    gfloat            time,
                                    const gchar      *name,
                                    const gchar      *payload);

/**
 * lrg_sequence_track_add_camera_cut:
 * @self: a camera cut track
 * @time: cut time in seconds
 * @camera_guid: GUID of an entity with an #LrgCameraComponent
 *
 * Returns: the section index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_track_add_camera_cut (LrgSequenceTrack *self,
                                         gfloat            time,
                                         const gchar      *camera_guid);

/**
 * lrg_sequence_track_add_audio:
 * @self: an audio track
 * @time: start time in seconds
 * @sound: "bank:sound" or a sound file path
 * @volume: volume (section weight), 0..1
 * @duration: seconds to play before stopping, or 0 to let it finish
 *
 * Returns: the section index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_track_add_audio (LrgSequenceTrack *self,
                                    gfloat            time,
                                    const gchar      *sound,
                                    gfloat            volume,
                                    gfloat            duration);

/**
 * lrg_sequence_track_add_clip:
 * @self: an animation track
 * @start: start time in seconds
 * @duration: section length, or 0 for "until the next section"
 * @clip: the #LrgAnimationClip name
 * @offset: clip time at @start
 * @rate: clip playback rate
 * @loop: whether the clip wraps inside the section
 * @weight: blend weight (1 replaces the pose)
 *
 * Returns: the section index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_track_add_clip (LrgSequenceTrack *self,
                                   gfloat            start,
                                   gfloat            duration,
                                   const gchar      *clip,
                                   gfloat            offset,
                                   gfloat            rate,
                                   gboolean          loop,
                                   gfloat            weight);

/**
 * lrg_sequence_track_add_subsequence:
 * @self: a sub-sequence track
 * @start: start time in seconds
 * @duration: section length, or 0 for "until the next section"
 * @sequence: a sequence name registered on the player, or a `.rseq` path
 *   (relative paths resolve against the parent file's directory)
 * @offset: sub-sequence time at @start
 * @rate: sub-sequence playback rate
 * @loop: whether the sub-sequence wraps inside the section
 *
 * Returns: the section index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_track_add_subsequence (LrgSequenceTrack *self,
                                          gfloat            start,
                                          gfloat            duration,
                                          const gchar      *sequence,
                                          gfloat            offset,
                                          gfloat            rate,
                                          gboolean          loop);

/**
 * lrg_sequence_track_copy:
 * @self: an #LrgSequenceTrack
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceTrack * lrg_sequence_track_copy (LrgSequenceTrack *self);

/**
 * lrg_sequence_track_equal:
 * @a: an #LrgSequenceTrack
 * @b: an #LrgSequenceTrack
 *
 * Returns: whether both hold the same settings, curves and sections
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_track_equal (LrgSequenceTrack *a,
                                   LrgSequenceTrack *b);

/* ==========================================================================
 * LrgSequence
 * ========================================================================== */

#define LRG_TYPE_SEQUENCE (lrg_sequence_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSequence, lrg_sequence, LRG, SEQUENCE, GObject)

/**
 * lrg_sequence_new:
 *
 * Creates an empty, unnamed five-second sequence at 30 frames per second
 * that plays once.
 *
 * Returns: (transfer full): a new #LrgSequence
 */
LRG_AVAILABLE_IN_ALL
LrgSequence * lrg_sequence_new (void);

/**
 * lrg_sequence_new_from_timeline:
 * @timeline: a level #LrgTimeline
 * @fps: frame rate of the new sequence, used to bake eased segments
 *
 * Converts a level timeline into a sequence so it can play on the 3D
 * runtime (#LrgWorld3D entities share their node's GUID). Location,
 * rotation and scale channels become transform tracks (one per node),
 * bone channels become bone transform tracks, and the other channels
 * (colour, roughness, metallic, params) are skipped. Bézier keys keep
 * their tangents, linear keys become %LRG_SEQUENCE_TANGENT_LINEAR keys and
 * any other easing is baked into linear keys at @fps. The length, loop
 * flag (%LRG_SEQUENCE_PLAY_LOOP) and name are copied.
 *
 * Returns: (transfer full): a new #LrgSequence
 */
LRG_AVAILABLE_IN_ALL
LrgSequence * lrg_sequence_new_from_timeline (LrgTimeline *timeline,
                                              gfloat       fps);

/**
 * lrg_sequence_new_from_data:
 * @data: `.rseq` YAML text
 * @length: length of @data, or -1 if nul-terminated
 * @base_dir: (nullable): directory relative sub-sequence paths resolve
 *   against
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the sequence, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgSequence * lrg_sequence_new_from_data (const gchar  *data,
                                          gssize        length,
                                          const gchar  *base_dir,
                                          GError      **error);

/**
 * lrg_sequence_new_from_file:
 * @path: a `.rseq` file
 * @error: return location for a #GError
 *
 * Loads a sequence; its base directory becomes the file's directory.
 *
 * Returns: (transfer full) (nullable): the sequence, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgSequence * lrg_sequence_new_from_file (const gchar  *path,
                                          GError      **error);

/**
 * lrg_sequence_to_data:
 * @self: an #LrgSequence
 * @error: return location for a #GError
 *
 * Serializes the sequence. The output is byte stable: saving what was
 * loaded from a saved file reproduces it exactly.
 *
 * Returns: (transfer full) (nullable): the YAML text, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_sequence_to_data (LrgSequence  *self,
                              GError      **error);

/**
 * lrg_sequence_save_to_file:
 * @self: an #LrgSequence
 * @path: destination `.rseq` file
 * @error: return location for a #GError
 *
 * Returns: whether the file was written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_save_to_file (LrgSequence  *self,
                                    const gchar  *path,
                                    GError      **error);

/**
 * lrg_sequence_get_base_dir:
 * @self: an #LrgSequence
 *
 * Returns: (nullable): the directory relative sub-sequence paths resolve
 *   against
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_get_base_dir (LrgSequence *self);

/**
 * lrg_sequence_set_base_dir:
 * @self: an #LrgSequence
 * @base_dir: (nullable): a directory
 *
 * Sets where relative sub-sequence paths resolve.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_set_base_dir (LrgSequence *self,
                                const gchar *base_dir);

/**
 * lrg_sequence_get_name:
 * @self: an #LrgSequence
 *
 * Returns: (nullable): the name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_get_name (LrgSequence *self);

/**
 * lrg_sequence_set_name:
 * @self: an #LrgSequence
 * @name: (nullable): a name
 *
 * Sets the name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_set_name (LrgSequence *self,
                            const gchar *name);

/**
 * lrg_sequence_get_length:
 * @self: an #LrgSequence
 *
 * Returns: the length in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sequence_get_length (LrgSequence *self);

/**
 * lrg_sequence_set_length:
 * @self: an #LrgSequence
 * @length: length in seconds (> 0)
 *
 * Sets the length (keys and sections past it are kept but not reached).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_set_length (LrgSequence *self,
                              gfloat       length);

/**
 * lrg_sequence_get_fps:
 * @self: an #LrgSequence
 *
 * Returns: the display/snapping frame rate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sequence_get_fps (LrgSequence *self);

/**
 * lrg_sequence_set_fps:
 * @self: an #LrgSequence
 * @fps: frames per second (> 0)
 *
 * Sets the frame rate used for snapping, frame display and timeline
 * conversion (evaluation itself is continuous).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_set_fps (LrgSequence *self,
                           gfloat       fps);

/**
 * lrg_sequence_frame_to_time:
 * @self: an #LrgSequence
 * @frame: a frame number
 *
 * Returns: the time of @frame in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sequence_frame_to_time (LrgSequence *self,
                                   gint         frame);

/**
 * lrg_sequence_time_to_frame:
 * @self: an #LrgSequence
 * @time: a time in seconds
 *
 * Returns: the nearest frame number
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sequence_time_to_frame (LrgSequence *self,
                                 gfloat       time);

/**
 * lrg_sequence_get_play_mode:
 * @self: an #LrgSequence
 *
 * Returns: the default play mode a player starts with
 */
LRG_AVAILABLE_IN_ALL
LrgSequencePlayMode lrg_sequence_get_play_mode (LrgSequence *self);

/**
 * lrg_sequence_set_play_mode:
 * @self: an #LrgSequence
 * @mode: the default play mode
 *
 * Sets the default play mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_set_play_mode (LrgSequence         *self,
                                 LrgSequencePlayMode  mode);

/**
 * lrg_sequence_add_binding:
 * @self: an #LrgSequence
 * @guid: an entity GUID
 * @name: (nullable): a display name for the editor
 *
 * Declares (or renames) a binding. Bindings document which entities the
 * sequence drives; tracks reference them by GUID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_add_binding (LrgSequence *self,
                               const gchar *guid,
                               const gchar *name);

/**
 * lrg_sequence_remove_binding:
 * @self: an #LrgSequence
 * @guid: an entity GUID
 *
 * Returns: whether the binding existed (tracks keep their GUID)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_remove_binding (LrgSequence *self,
                                      const gchar *guid);

/**
 * lrg_sequence_get_n_bindings:
 * @self: an #LrgSequence
 *
 * Returns: the number of bindings
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_get_n_bindings (LrgSequence *self);

/**
 * lrg_sequence_get_binding:
 * @self: an #LrgSequence
 * @index: a binding index
 * @out_guid: (out) (optional) (transfer none): the GUID
 * @out_name: (out) (optional) (transfer none) (nullable): the display name
 *
 * Returns: whether @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_get_binding (LrgSequence  *self,
                                   guint         index,
                                   const gchar **out_guid,
                                   const gchar **out_name);

/**
 * lrg_sequence_get_binding_name:
 * @self: an #LrgSequence
 * @guid: an entity GUID
 *
 * Returns: (nullable): the binding's display name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_get_binding_name (LrgSequence *self,
                                             const gchar *guid);

/**
 * lrg_sequence_add_track:
 * @self: an #LrgSequence
 * @track: (transfer none): a track (a reference is taken)
 *
 * Appends a track. Tracks evaluate in order, so a later track wins when
 * two drive the same value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_add_track (LrgSequence      *self,
                             LrgSequenceTrack *track);

/**
 * lrg_sequence_get_n_tracks:
 * @self: an #LrgSequence
 *
 * Returns: the number of tracks
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sequence_get_n_tracks (LrgSequence *self);

/**
 * lrg_sequence_get_track:
 * @self: an #LrgSequence
 * @index: a track index
 *
 * Returns: (transfer none) (nullable): the track, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceTrack * lrg_sequence_get_track (LrgSequence *self,
                                           guint        index);

/**
 * lrg_sequence_find_track:
 * @self: an #LrgSequence
 * @name: a track name
 *
 * Returns: (transfer none) (nullable): the first track called @name
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceTrack * lrg_sequence_find_track (LrgSequence *self,
                                            const gchar *name);

/**
 * lrg_sequence_remove_track:
 * @self: an #LrgSequence
 * @index: a track index
 *
 * Returns: whether a track was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_remove_track (LrgSequence *self,
                                    guint        index);

/**
 * lrg_sequence_copy:
 * @self: an #LrgSequence
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgSequence * lrg_sequence_copy (LrgSequence *self);

/**
 * lrg_sequence_equal:
 * @a: (nullable): an #LrgSequence
 * @b: (nullable): an #LrgSequence
 *
 * Returns: whether both are %NULL or hold the same settings, bindings and
 *   tracks
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_equal (LrgSequence *a,
                             LrgSequence *b);

/**
 * lrg_sequence_compose_bone_offset:
 * @bind: the bone's bind (rest) pose
 * @offset: (array fixed-size=9): location xyz, Euler rotation xyz
 *   (radians), scale xyz
 * @out_pose: (out caller-allocates): the resulting local pose
 *
 * Applies a bind-pose offset, the value convention of bone tracks in
 * sequences and level timelines: position = bind + location, rotation =
 * bind * euler(rotation), scale = bind * scale. A zero offset with unit
 * scale gives the bind pose.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_compose_bone_offset (const LrgBonePose *bind,
                                       const gfloat      *offset,
                                       LrgBonePose       *out_pose);

G_END_DECLS
