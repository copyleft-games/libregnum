/* lrg-song.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A song: a multitrack arrangement saved as .rsong YAML.  Instruments
 * (synthesizers, drum machines, samplers, SoundFonts, Foley patches) are
 * defined once by id; patterns hold notes and drum step lanes; tracks
 * place pattern clips (instrument tracks) or audio-file clips (audio
 * tracks) on a timeline in beats; automation moves volume, pan,
 * instrument and effect parameters; an embedded mixer layout (the .rmixer
 * schema) routes the tracks through buses and effects.  lrg_song_render()
 * mixes the song offline, sample-accurately and deterministically.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../audio/lrg-wave-data.h"
#include "../audio/lrg-mixer-asset.h"
#include "lrg-instrument.h"

G_BEGIN_DECLS

/**
 * LRG_SONG_FORMAT_VERSION:
 *
 * The `version` written in .rsong files.
 */
#define LRG_SONG_FORMAT_VERSION (1)

/**
 * LRG_SONG_MAX_BEATS:
 *
 * The longest song in beats (an hour at 240 BPM).
 */
#define LRG_SONG_MAX_BEATS (14400.0)

/**
 * LrgSongError:
 * @LRG_SONG_ERROR_PARSE: the text is not YAML
 * @LRG_SONG_ERROR_INVALID_VALUE: a key holds a wrong or out-of-range value
 * @LRG_SONG_ERROR_NOT_FOUND: a named instrument, pattern, track, bus,
 *   marker or file does not exist
 * @LRG_SONG_ERROR_IO: a file could not be read or written
 * @LRG_SONG_ERROR_RENDER: the song cannot be rendered
 * @LRG_SONG_ERROR_CANCELLED: the render was cancelled
 *
 * Errors of #LrgSong.
 */
typedef enum
{
	LRG_SONG_ERROR_PARSE,
	LRG_SONG_ERROR_INVALID_VALUE,
	LRG_SONG_ERROR_NOT_FOUND,
	LRG_SONG_ERROR_IO,
	LRG_SONG_ERROR_RENDER,
	LRG_SONG_ERROR_CANCELLED
} LrgSongError;

#define LRG_SONG_ERROR (lrg_song_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_song_error_quark (void);

/**
 * LrgSongTrackKind:
 * @LRG_SONG_TRACK_INSTRUMENT: plays pattern clips on an instrument
 * @LRG_SONG_TRACK_AUDIO: plays audio-file clips
 *
 * What a track's clips are.
 */
typedef enum
{
	LRG_SONG_TRACK_INSTRUMENT,
	LRG_SONG_TRACK_AUDIO
} LrgSongTrackKind;

LRG_AVAILABLE_IN_ALL
GType lrg_song_track_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SONG_TRACK_KIND (lrg_song_track_kind_get_type ())

/**
 * LrgSongNormalize:
 * @LRG_SONG_NORMALIZE_NONE: the mix as it comes out of the master bus
 * @LRG_SONG_NORMALIZE_PEAK: scaled so the sample peak is the target (dBFS)
 * @LRG_SONG_NORMALIZE_LOUDNESS: scaled so the integrated loudness is the
 *   target (LUFS), then turned down further if the true peak would pass
 *   the ceiling
 *
 * How lrg_song_render() sets the level of the result.
 */
typedef enum
{
	LRG_SONG_NORMALIZE_NONE,
	LRG_SONG_NORMALIZE_PEAK,
	LRG_SONG_NORMALIZE_LOUDNESS
} LrgSongNormalize;

LRG_AVAILABLE_IN_ALL
GType lrg_song_normalize_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SONG_NORMALIZE (lrg_song_normalize_get_type ())

/**
 * LrgSongRenderOptions:
 * @start_beat: where the result starts (the song still plays from 0 when
 *   @preroll is set, so held notes and effect tails are right)
 * @end_beat: where the music stops; 0 or less for the song's end
 * @tail: seconds rendered after the end for releases and reverbs; less
 *   than 0 renders until the master is silent (at most 15 s)
 * @normalize: how the level is set
 * @target: the peak (dBFS) or loudness (LUFS) target of @normalize
 * @true_peak_ceiling: the highest true peak (dBTP) loudness normalisation
 *   allows
 * @sample_rate: the output rate (0 for the song's)
 * @preroll: render from the song's start and drop what lies before
 *   @start_beat
 * @mute: (array zero-terminated=1) (nullable): track ids to silence
 * @solo: (array zero-terminated=1) (nullable): track ids to keep (every
 *   other track is silenced); %NULL or empty keeps them all
 *
 * Options of lrg_song_render(); lrg_song_render_options_new() fills the
 * defaults (whole song, automatic tail, no normalisation, -14 LUFS and
 * -1 dBTP targets, preroll).
 */
typedef struct
{
	gdouble           start_beat;
	gdouble           end_beat;
	gdouble           tail;
	LrgSongNormalize  normalize;
	gdouble           target;
	gdouble           true_peak_ceiling;
	guint             sample_rate;
	gboolean          preroll;
	gchar           **mute;
	gchar           **solo;

	/*< private >*/
	gpointer _reserved[4];
} LrgSongRenderOptions;

LRG_AVAILABLE_IN_ALL
GType lrg_song_render_options_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SONG_RENDER_OPTIONS (lrg_song_render_options_get_type ())

/**
 * lrg_song_render_options_new:
 *
 * Returns: (transfer full): options with the defaults
 */
LRG_AVAILABLE_IN_ALL
LrgSongRenderOptions * lrg_song_render_options_new (void);

/**
 * lrg_song_render_options_copy:
 * @options: options
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgSongRenderOptions * lrg_song_render_options_copy (const LrgSongRenderOptions *options);

/**
 * lrg_song_render_options_free:
 * @options: options
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_render_options_free (LrgSongRenderOptions *options);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSongRenderOptions, lrg_song_render_options_free)

/**
 * LrgSongProgressFunc:
 * @fraction: 0 .. 1 of the render done
 * @user_data: the data given with the function
 *
 * Called from the rendering thread as the render goes.
 */
typedef void (*LrgSongProgressFunc) (gdouble  fraction,
                                     gpointer user_data);

#define LRG_TYPE_SONG (lrg_song_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSong, lrg_song, LRG, SONG, GObject)

/* ==========================================================================
 * Construction, files
 * ========================================================================== */

/**
 * lrg_song_new:
 *
 * An empty song at 120 BPM in 4/4, 48000 Hz, with a master bus only.
 * The song settings are properties: #LrgSong:title, #LrgSong:artist,
 * #LrgSong:bpm, #LrgSong:beats-per-bar, #LrgSong:beat-unit,
 * #LrgSong:sample-rate, #LrgSong:seed, #LrgSong:length,
 * #LrgSong:loop-start, #LrgSong:loop-end and #LrgSong:base-path.
 *
 * Returns: (transfer full): a new #LrgSong
 */
LRG_AVAILABLE_IN_ALL
LrgSong * lrg_song_new (void);

/**
 * lrg_song_new_from_data:
 * @data: .rsong YAML text
 * @length: length of @data, or -1 when NUL-terminated
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the song, or %NULL with @error set
 *   (%LRG_SONG_ERROR) naming the first wrong key by its YAML path
 */
LRG_AVAILABLE_IN_ALL
LrgSong * lrg_song_new_from_data (const gchar  *data,
                                  gssize        length,
                                  GError      **error);

/**
 * lrg_song_new_from_file:
 * @path: a .rsong file
 * @error: (nullable): return location for an error
 *
 * Loads a song; #LrgSong:base-path becomes the file's folder, against
 * which audio clips, samples, SoundFonts and Foley patches resolve.
 *
 * Returns: (transfer full) (nullable): the song, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgSong * lrg_song_new_from_file (const gchar  *path,
                                  GError      **error);

/**
 * lrg_song_to_data:
 * @self: an #LrgSong
 * @length: (out) (optional): the length of the text
 * @error: (nullable): return location for an error
 *
 * Writes the song as .rsong YAML in a stable order (keys sorted where
 * the order does not matter), so a song read and written again gives the
 * same text.
 *
 * Returns: (transfer full) (nullable): the text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_song_to_data (LrgSong  *self,
                          gsize    *length,
                          GError  **error);

/**
 * lrg_song_save_to_file:
 * @self: an #LrgSong
 * @path: the file to write
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_save_to_file (LrgSong      *self,
                                const gchar  *path,
                                GError      **error);

/**
 * lrg_song_copy:
 * @self: an #LrgSong
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgSong * lrg_song_copy (LrgSong *self);

/**
 * lrg_song_validate:
 * @self: an #LrgSong
 * @issues: (out) (optional) (transfer full) (element-type utf8): every
 *   problem found, as "path: message"
 *
 * Checks every reference: instruments of tracks, patterns and files of
 * clips, buses, automation targets, overlapping or out-of-song clips
 * (warnings start with "warning: ").
 *
 * Returns: %TRUE when nothing but warnings was found
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_validate (LrgSong    *self,
                            GPtrArray **issues);

/* ==========================================================================
 * Time
 * ========================================================================== */

/**
 * lrg_song_add_tempo_change:
 * @self: an #LrgSong
 * @beat: where the tempo is @bpm (above 0)
 * @bpm: 20 .. 400
 * @ramp: %TRUE glides linearly from the previous tempo to @bpm, %FALSE
 *   jumps to it at @beat
 *
 * Adds or replaces the tempo change at @beat.  #LrgSong:bpm is the tempo
 * at beat 0.
 *
 * Returns: %TRUE when the values are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_add_tempo_change (LrgSong  *self,
                                    gdouble   beat,
                                    gdouble   bpm,
                                    gboolean  ramp);

/**
 * lrg_song_clear_tempo_changes:
 * @self: an #LrgSong
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_clear_tempo_changes (LrgSong *self);

/**
 * lrg_song_get_n_tempo_changes:
 * @self: an #LrgSong
 *
 * Returns: the tempo changes after beat 0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_song_get_n_tempo_changes (LrgSong *self);

/**
 * lrg_song_get_tempo_change:
 * @self: an #LrgSong
 * @index: a change, by beat
 * @out_beat: (out) (optional): where it is
 * @out_bpm: (out) (optional): its tempo
 * @out_ramp: (out) (optional): whether it glides from the previous tempo
 *
 * Returns: %TRUE for a valid index
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_get_tempo_change (LrgSong  *self,
                                    guint     index,
                                    gdouble  *out_beat,
                                    gdouble  *out_bpm,
                                    gboolean *out_ramp);

/**
 * lrg_song_get_bpm_at:
 * @self: an #LrgSong
 * @beat: a beat
 *
 * Returns: the tempo there
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_get_bpm_at (LrgSong *self,
                             gdouble  beat);

/**
 * lrg_song_beat_to_seconds:
 * @self: an #LrgSong
 * @beat: a beat (0 or more)
 *
 * Integrates the tempo map exactly (ramps analytically).
 *
 * Returns: the time of @beat in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_beat_to_seconds (LrgSong *self,
                                  gdouble  beat);

/**
 * lrg_song_seconds_to_beat:
 * @self: an #LrgSong
 * @seconds: a time (0 or more)
 *
 * Returns: the beat playing at @seconds (the inverse of
 *   lrg_song_beat_to_seconds())
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_seconds_to_beat (LrgSong *self,
                                  gdouble  seconds);

/**
 * lrg_song_get_length_beats:
 * @self: an #LrgSong
 *
 * Returns: #LrgSong:length when set, otherwise the end of the last clip
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_get_length_beats (LrgSong *self);

/**
 * lrg_song_get_duration:
 * @self: an #LrgSong
 *
 * Returns: the length in seconds (without a render tail)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_get_duration (LrgSong *self);

/**
 * lrg_song_add_marker:
 * @self: an #LrgSong
 * @beat: where
 * @name: a unique name ("Intro", "Chorus")
 *
 * Adds a marker, or moves the marker of that name.
 *
 * Returns: %TRUE when the values are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_add_marker (LrgSong     *self,
                              gdouble      beat,
                              const gchar *name);

/**
 * lrg_song_remove_marker:
 * @self: an #LrgSong
 * @name: a marker name
 *
 * Returns: %TRUE when it existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_remove_marker (LrgSong     *self,
                                 const gchar *name);

/**
 * lrg_song_get_n_markers:
 * @self: an #LrgSong
 *
 * Returns: the markers
 */
LRG_AVAILABLE_IN_ALL
guint lrg_song_get_n_markers (LrgSong *self);

/**
 * lrg_song_get_marker:
 * @self: an #LrgSong
 * @index: a marker, by beat
 * @out_beat: (out) (optional): where
 * @out_name: (out) (optional) (transfer none): its name
 *
 * Returns: %TRUE for a valid index
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_get_marker (LrgSong      *self,
                              guint         index,
                              gdouble      *out_beat,
                              const gchar **out_name);

/**
 * lrg_song_find_marker:
 * @self: an #LrgSong
 * @name: a marker name
 *
 * Returns: its beat, or -1 when there is none
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_find_marker (LrgSong     *self,
                              const gchar *name);

/* ==========================================================================
 * Instruments
 * ========================================================================== */

/**
 * lrg_song_set_instrument:
 * @self: an #LrgSong
 * @id: the instrument id (letters, digits, '-', '_', '.')
 * @type: "synth", "drums", "sampler", "soundfont" or "foley"
 * @source: (nullable): the synth preset, the drum kit ("808", "909",
 *   "linn"), or the file of a sampler (audio), SoundFont (.sf2) or Foley
 *   patch (.rfoley), relative to #LrgSong:base-path
 *
 * Defines or replaces an instrument; its parameter overrides are cleared.
 *
 * Returns: %TRUE when @id and @type are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_set_instrument (LrgSong     *self,
                                  const gchar *id,
                                  const gchar *type,
                                  const gchar *source);

/**
 * lrg_song_remove_instrument:
 * @self: an #LrgSong
 * @id: an instrument id
 *
 * Returns: %TRUE when it existed (tracks that used it keep its id and fail
 *   validation)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_remove_instrument (LrgSong     *self,
                                     const gchar *id);

/**
 * lrg_song_list_instruments:
 * @self: an #LrgSong
 *
 * Returns: (transfer full) (array zero-terminated=1): the ids, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_song_list_instruments (LrgSong *self);

/**
 * lrg_song_get_instrument_type:
 * @self: an #LrgSong
 * @id: an instrument id
 *
 * Returns: (transfer none) (nullable): its type
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_song_get_instrument_type (LrgSong     *self,
                                            const gchar *id);

/**
 * lrg_song_get_instrument_source:
 * @self: an #LrgSong
 * @id: an instrument id
 *
 * Returns: (transfer none) (nullable): its preset, kit or file
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_song_get_instrument_source (LrgSong     *self,
                                              const gchar *id);

/**
 * lrg_song_set_instrument_param:
 * @self: an #LrgSong
 * @id: an instrument id
 * @param: a parameter of the instrument (lrg_instrument_set_param())
 * @value: its value (clamped when the instrument is built)
 *
 * Stores a parameter override (applied after the preset or kit).
 *
 * Returns: %TRUE when the instrument exists and the instrument type has
 *   the parameter
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_set_instrument_param (LrgSong     *self,
                                        const gchar *id,
                                        const gchar *param,
                                        gdouble      value);

/**
 * lrg_song_unset_instrument_param:
 * @self: an #LrgSong
 * @id: an instrument id
 * @param: a parameter
 *
 * Returns: %TRUE when the override existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_unset_instrument_param (LrgSong     *self,
                                          const gchar *id,
                                          const gchar *param);

/**
 * lrg_song_list_instrument_params:
 * @self: an #LrgSong
 * @id: an instrument id
 *
 * Returns: (transfer full) (array zero-terminated=1): the overridden
 *   parameters, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_song_list_instrument_params (LrgSong     *self,
                                          const gchar *id);

/**
 * lrg_song_get_instrument_param:
 * @self: an #LrgSong
 * @id: an instrument id
 * @param: a parameter
 *
 * Returns: the override, or NaN when there is none
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_get_instrument_param (LrgSong     *self,
                                       const gchar *id,
                                       const gchar *param);

/**
 * lrg_song_create_instrument:
 * @self: an #LrgSong
 * @id: an instrument id
 * @error: (nullable): return location for an error
 *
 * Builds a new instance of the instrument with its overrides (a song
 * render builds its own; the editor plays previews on these).
 *
 * Returns: (transfer full) (nullable): the instrument
 */
LRG_AVAILABLE_IN_ALL
LrgInstrument * lrg_song_create_instrument (LrgSong      *self,
                                            const gchar  *id,
                                            GError      **error);

/* ==========================================================================
 * Patterns
 * ========================================================================== */

/**
 * lrg_song_add_pattern:
 * @self: an #LrgSong
 * @id: the pattern id
 * @length_beats: its length (0.25 .. 1024)
 *
 * Adds an empty pattern, or empties and resizes an existing one.
 *
 * Returns: %TRUE when the values are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_add_pattern (LrgSong     *self,
                               const gchar *id,
                               gdouble      length_beats);

/**
 * lrg_song_remove_pattern:
 * @self: an #LrgSong
 * @id: a pattern id
 *
 * Returns: %TRUE when it existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_remove_pattern (LrgSong     *self,
                                  const gchar *id);

/**
 * lrg_song_list_patterns:
 * @self: an #LrgSong
 *
 * Returns: (transfer full) (array zero-terminated=1): the ids, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_song_list_patterns (LrgSong *self);

/**
 * lrg_song_get_pattern_length:
 * @self: an #LrgSong
 * @id: a pattern id
 *
 * Returns: its length in beats, or 0 when there is none
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_get_pattern_length (LrgSong     *self,
                                     const gchar *id);

/**
 * lrg_song_set_pattern_length:
 * @self: an #LrgSong
 * @id: a pattern id
 * @length_beats: 0.25 .. 1024 (notes past it stay but do not play)
 *
 * Returns: %TRUE when the pattern exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_set_pattern_length (LrgSong     *self,
                                      const gchar *id,
                                      gdouble      length_beats);

/**
 * lrg_song_pattern_add_note:
 * @self: an #LrgSong
 * @id: a pattern id
 * @beat: the start within the pattern
 * @key: the MIDI key (0 .. 127)
 * @length: in beats (above 0)
 * @velocity: 0 .. 1
 *
 * Adds a note; notes are kept sorted by beat, then key.
 *
 * Returns: the note's index, or -1 for bad values
 */
LRG_AVAILABLE_IN_ALL
gint lrg_song_pattern_add_note (LrgSong     *self,
                                const gchar *id,
                                gdouble      beat,
                                gint         key,
                                gdouble      length,
                                gdouble      velocity);

/**
 * lrg_song_pattern_remove_note:
 * @self: an #LrgSong
 * @id: a pattern id
 * @index: a note index
 *
 * Returns: %TRUE when the note existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_pattern_remove_note (LrgSong     *self,
                                       const gchar *id,
                                       guint        index);

/**
 * lrg_song_pattern_clear_notes:
 * @self: an #LrgSong
 * @id: a pattern id
 *
 * Removes every note (not the lanes).
 *
 * Returns: %TRUE when the pattern exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_pattern_clear_notes (LrgSong     *self,
                                       const gchar *id);

/**
 * lrg_song_pattern_get_n_notes:
 * @self: an #LrgSong
 * @id: a pattern id
 *
 * Returns: the notes (not counting lanes)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_song_pattern_get_n_notes (LrgSong     *self,
                                    const gchar *id);

/**
 * lrg_song_pattern_get_note:
 * @self: an #LrgSong
 * @id: a pattern id
 * @index: a note index
 * @out_beat: (out) (optional): start
 * @out_key: (out) (optional): key
 * @out_length: (out) (optional): length in beats
 * @out_velocity: (out) (optional): velocity
 *
 * Returns: %TRUE for a valid note
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_pattern_get_note (LrgSong     *self,
                                    const gchar *id,
                                    guint        index,
                                    gdouble     *out_beat,
                                    gint        *out_key,
                                    gdouble     *out_length,
                                    gdouble     *out_velocity);

/**
 * lrg_song_pattern_set_steps:
 * @self: an #LrgSong
 * @id: a pattern id
 * @steps: steps over the pattern's length (1 .. 256; 16 for sixteenths of
 *   a 4-beat pattern)
 *
 * Returns: %TRUE when the pattern exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_pattern_set_steps (LrgSong     *self,
                                     const gchar *id,
                                     guint        steps);

/**
 * lrg_song_pattern_get_steps:
 * @self: an #LrgSong
 * @id: a pattern id
 *
 * Returns: the step count (0 when there is no pattern)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_song_pattern_get_steps (LrgSong     *self,
                                  const gchar *id);

/**
 * lrg_song_pattern_set_lane:
 * @self: an #LrgSong
 * @id: a pattern id
 * @lane: what the lane plays: a drum piece ("kick", "sd", ...) or a note
 *   ("C3", "36")
 * @steps: (nullable): one character per step: `.` or `-` rest, `g` ghost
 *   (0.3), `o` soft (0.5), `x` normal (0.8), `X` accent (1.0); spaces and
 *   `|` are ignored; %NULL removes the lane
 *
 * Sets a step-sequencer lane.  Steps past the step count are ignored.
 *
 * Returns: %TRUE when the pattern exists and @lane and @steps are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_pattern_set_lane (LrgSong     *self,
                                    const gchar *id,
                                    const gchar *lane,
                                    const gchar *steps);

/**
 * lrg_song_pattern_get_lane:
 * @self: an #LrgSong
 * @id: a pattern id
 * @lane: a lane name
 *
 * Returns: (transfer none) (nullable): its steps
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_song_pattern_get_lane (LrgSong     *self,
                                         const gchar *id,
                                         const gchar *lane);

/**
 * lrg_song_pattern_list_lanes:
 * @self: an #LrgSong
 * @id: a pattern id
 *
 * Returns: (transfer full) (array zero-terminated=1): the lane names in
 *   the order they were added
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_song_pattern_list_lanes (LrgSong     *self,
                                      const gchar *id);

/* ==========================================================================
 * Tracks and clips
 * ========================================================================== */

/**
 * lrg_song_add_track:
 * @self: an #LrgSong
 * @id: a new track id
 * @kind: what its clips are
 * @instrument: (nullable): the instrument an instrument track plays
 *
 * Appends a track at 0 dB, centred, on the master bus.
 *
 * Returns: %TRUE when @id is valid and new
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_add_track (LrgSong          *self,
                             const gchar      *id,
                             LrgSongTrackKind  kind,
                             const gchar      *instrument);

/**
 * lrg_song_remove_track:
 * @self: an #LrgSong
 * @id: a track id
 *
 * Returns: %TRUE when it existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_remove_track (LrgSong     *self,
                                const gchar *id);

/**
 * lrg_song_move_track:
 * @self: an #LrgSong
 * @id: a track id
 * @index: its new position
 *
 * Returns: %TRUE when the track exists and @index is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_move_track (LrgSong     *self,
                              const gchar *id,
                              guint        index);

/**
 * lrg_song_list_tracks:
 * @self: an #LrgSong
 *
 * Returns: (transfer full) (array zero-terminated=1): the ids in order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_song_list_tracks (LrgSong *self);

/**
 * lrg_song_get_track_kind:
 * @self: an #LrgSong
 * @id: a track id
 *
 * Returns: its kind (%LRG_SONG_TRACK_INSTRUMENT for an unknown track)
 */
LRG_AVAILABLE_IN_ALL
LrgSongTrackKind lrg_song_get_track_kind (LrgSong     *self,
                                          const gchar *id);

/**
 * lrg_song_set_track:
 * @self: an #LrgSong
 * @id: a track id
 * @field: "name", "instrument" or "bus" (text), "volume" (dB), "pan"
 *   (-1 .. 1), "mute" or "solo" (0 / 1)
 * @text: (nullable): the value of a text field
 * @value: the value of a numeric field (clamped)
 *
 * Sets one track setting.
 *
 * Returns: %TRUE for a known track and field
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_set_track (LrgSong     *self,
                             const gchar *id,
                             const gchar *field,
                             const gchar *text,
                             gdouble      value);

/**
 * lrg_song_get_track_text:
 * @self: an #LrgSong
 * @id: a track id
 * @field: "name", "instrument" or "bus"
 *
 * Returns: (transfer none) (nullable): the value
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_song_get_track_text (LrgSong     *self,
                                       const gchar *id,
                                       const gchar *field);

/**
 * lrg_song_get_track_value:
 * @self: an #LrgSong
 * @id: a track id
 * @field: "volume", "pan", "mute" or "solo"
 *
 * Returns: the value, or NaN for an unknown track or field
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_get_track_value (LrgSong     *self,
                                  const gchar *id,
                                  const gchar *field);

/**
 * lrg_song_add_clip:
 * @self: an #LrgSong
 * @track: a track id
 * @source: a pattern id (instrument track) or an audio file (audio track,
 *   relative to #LrgSong:base-path)
 * @start: the beat it starts on
 * @length: its length in beats (a pattern repeats to fill it; 0 for one
 *   pass of the pattern, or the whole file)
 *
 * Adds a clip; a track's clips are kept sorted by start.
 *
 * Returns: the clip's index, or -1 for an unknown track or bad values
 */
LRG_AVAILABLE_IN_ALL
gint lrg_song_add_clip (LrgSong     *self,
                        const gchar *track,
                        const gchar *source,
                        gdouble      start,
                        gdouble      length);

/**
 * lrg_song_remove_clip:
 * @self: an #LrgSong
 * @track: a track id
 * @index: a clip index
 *
 * Returns: %TRUE when the clip existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_remove_clip (LrgSong     *self,
                               const gchar *track,
                               guint        index);

/**
 * lrg_song_get_n_clips:
 * @self: an #LrgSong
 * @track: a track id
 *
 * Returns: the track's clips
 */
LRG_AVAILABLE_IN_ALL
guint lrg_song_get_n_clips (LrgSong     *self,
                            const gchar *track);

/**
 * lrg_song_set_clip:
 * @self: an #LrgSong
 * @track: a track id
 * @index: a clip index
 * @field: "start", "length", "offset" (beats into the pattern, or seconds
 *   into the file), "transpose" (semitones), "gain" (dB), "fade-in",
 *   "fade-out" (seconds), "mute" (0 / 1)
 * @value: the value (clamped)
 *
 * Changing "start" re-sorts the clips; the moved clip's new index is
 * returned by lrg_song_find_clip().
 *
 * Returns: %TRUE for a known clip and field
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_set_clip (LrgSong     *self,
                            const gchar *track,
                            guint        index,
                            const gchar *field,
                            gdouble      value);

/**
 * lrg_song_set_clip_source:
 * @self: an #LrgSong
 * @track: a track id
 * @index: a clip index
 * @source: a pattern id or file
 *
 * Returns: %TRUE for a known clip
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_set_clip_source (LrgSong     *self,
                                   const gchar *track,
                                   guint        index,
                                   const gchar *source);

/**
 * lrg_song_get_clip:
 * @self: an #LrgSong
 * @track: a track id
 * @index: a clip index
 * @out_source: (out) (optional) (transfer none): its pattern or file
 * @out_start: (out) (optional): its start beat
 * @out_length: (out) (optional): its length in beats (resolved: never 0)
 *
 * Returns: %TRUE for a known clip
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_get_clip (LrgSong      *self,
                            const gchar  *track,
                            guint         index,
                            const gchar **out_source,
                            gdouble      *out_start,
                            gdouble      *out_length);

/**
 * lrg_song_get_clip_value:
 * @self: an #LrgSong
 * @track: a track id
 * @index: a clip index
 * @field: a field of lrg_song_set_clip()
 *
 * Returns: the value, or NaN
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_get_clip_value (LrgSong     *self,
                                 const gchar *track,
                                 guint        index,
                                 const gchar *field);

/**
 * lrg_song_find_clip:
 * @self: an #LrgSong
 * @track: a track id
 * @beat: a beat
 *
 * Returns: the index of the last clip that plays at @beat, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_song_find_clip (LrgSong     *self,
                         const gchar *track,
                         gdouble      beat);

/* ==========================================================================
 * Automation
 * ========================================================================== */

/**
 * lrg_song_set_automation:
 * @self: an #LrgSong
 * @track: (nullable): a track id, or %NULL for song-wide targets
 * @target: on a track "volume" (dB), "pan" or "param.<name>" (an
 *   instrument parameter); song-wide "bus.<bus>.volume" (dB) or
 *   "effect.<bus>.<effect id>.<property>"
 * @points: (array length=n_values): beat, value, beat, value, ...
 * @n_values: the number of doubles in @points (even)
 * @step: %TRUE holds each value until the next point, %FALSE moves
 *   linearly
 *
 * Sets (or with no points removes) an automation lane.  Points are sorted
 * by beat; before the first point the lane holds the first value, after
 * the last the last value.
 *
 * Returns: %TRUE for a known track and a valid target
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_set_automation (LrgSong       *self,
                                  const gchar   *track,
                                  const gchar   *target,
                                  const gdouble *points,
                                  gsize          n_values,
                                  gboolean       step);

/**
 * lrg_song_list_automation:
 * @self: an #LrgSong
 * @track: (nullable): a track id, or %NULL for song-wide lanes
 *
 * Returns: (transfer full) (array zero-terminated=1): the targets
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_song_list_automation (LrgSong     *self,
                                   const gchar *track);

/**
 * lrg_song_get_automation_value:
 * @self: an #LrgSong
 * @track: (nullable): a track id, or %NULL
 * @target: a target
 * @beat: a beat
 *
 * Returns: the lane's value at @beat, or NaN when there is no lane
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_get_automation_value (LrgSong     *self,
                                       const gchar *track,
                                       const gchar *target,
                                       gdouble      beat);

/**
 * lrg_song_get_automation_points:
 * @self: an #LrgSong
 * @track: (nullable): a track id, or %NULL
 * @target: a target
 * @n_values: (out): the number of doubles returned
 *
 * Returns: (transfer full) (array length=n_values) (nullable): beat,
 *   value, ... or %NULL when there is no lane
 */
LRG_AVAILABLE_IN_ALL
gdouble * lrg_song_get_automation_points (LrgSong     *self,
                                          const gchar *track,
                                          const gchar *target,
                                          gsize       *n_values);

/* ==========================================================================
 * Mixer
 * ========================================================================== */

/**
 * lrg_song_get_mixer:
 * @self: an #LrgSong
 *
 * The song's mixer layout (the .rmixer schema, saved under `mixer:`).
 * Edit it in place; tracks name its buses.
 *
 * Returns: (transfer none): the layout
 */
LRG_AVAILABLE_IN_ALL
LrgMixerAsset * lrg_song_get_mixer (LrgSong *self);

/**
 * lrg_song_set_mixer:
 * @self: an #LrgSong
 * @mixer: a layout (copied)
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_set_mixer (LrgSong       *self,
                         LrgMixerAsset *mixer);

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/**
 * lrg_song_render:
 * @self: an #LrgSong
 * @options: (nullable): options (%NULL for the defaults)
 * @cancellable: (nullable): cancels the render
 * @progress: (scope call) (nullable): called as the render goes
 * @user_data: data for @progress
 * @error: (nullable): return location for an error
 *
 * Mixes the song offline: builds every instrument and the mixer, plays
 * every clip at its exact sample, applies the automation every
 * %LRG_AUDIO_MIXER_BLOCK_FRAMES frames, and renders the master bus
 * without clipping, then normalises as asked.  Rendering the same song
 * with the same options gives the same samples.  May run on any thread
 * (the song must not change meanwhile; render a lrg_song_copy() to keep
 * editing).
 *
 * Returns: (transfer full) (nullable): stereo 32-bit float #LrgWaveData
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_song_render (LrgSong                    *self,
                               const LrgSongRenderOptions *options,
                               GCancellable               *cancellable,
                               LrgSongProgressFunc         progress,
                               gpointer                    user_data,
                               GError                    **error);

/**
 * lrg_song_render_stems:
 * @self: an #LrgSong
 * @options: (nullable): options; their solo list is replaced per stem
 * @cancellable: (nullable): cancels the render
 * @progress: (scope call) (nullable): called as the render goes
 * @user_data: data for @progress
 * @out_names: (out) (transfer full) (array zero-terminated=1): the track
 *   id of each stem
 * @error: (nullable): return location for an error
 *
 * Renders every track alone (soloed, through its buses and effects).
 * Normalisation, when asked, uses the gain the full mix would get, so the
 * stems add up to the mix.
 *
 * Returns: (transfer full) (element-type LrgWaveData) (nullable): one wave
 *   per track
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_song_render_stems (LrgSong                    *self,
                                   const LrgSongRenderOptions *options,
                                   GCancellable               *cancellable,
                                   LrgSongProgressFunc         progress,
                                   gpointer                    user_data,
                                   gchar                    ***out_names,
                                   GError                    **error);

G_END_DECLS
