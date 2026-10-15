/* lrg-midi-synth.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A General MIDI synthesizer: Standard MIDI Files rendered through a
 * SoundFont 2 bank.
 *
 * LrgMidiSynth plays one song at a time (two while crossfading) through
 * the vendored, unmodified TinySoundFont and TinyMidiLoader single-header
 * libraries.  Rendering is headless: lrg_midi_synth_render() fills a
 * buffer of interleaved stereo floats without an audio device, which is
 * what the tests use.  In a running game lrg_midi_synth_start_stream()
 * opens a graylib audio stream and lrg_midi_synth_update(), called every
 * frame, keeps it fed.
 *
 * Songs loop at their end, or between "loopStart" / "loopEnd" marker
 * meta events (the convention of libADLMIDI and of converted HMI songs)
 * when the file has them.  A new song can crossfade from the one
 * playing; the volume changes smoothly.
 *
 * Applications can probe for this API at compile time with
 * `#ifdef LRG_AUDIO_HAS_MIDI_SYNTH`.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_AUDIO_HAS_MIDI_SYNTH:
 *
 * Defined when this libregnum provides #LrgMidiSynth, so applications
 * can probe for the API at compile time.
 */
#define LRG_AUDIO_HAS_MIDI_SYNTH (1)

/**
 * LRG_MIDI_SYNTH_DEFAULT_SAMPLE_RATE:
 *
 * The usual output rate in Hz.
 */
#define LRG_MIDI_SYNTH_DEFAULT_SAMPLE_RATE (44100)

/**
 * LRG_MIDI_SYNTH_STREAM_FRAMES:
 *
 * Frames rendered per stream buffer (the stream is double buffered):
 * about 93 ms at 44100 Hz.
 */
#define LRG_MIDI_SYNTH_STREAM_FRAMES (4096)

#define LRG_TYPE_MIDI_SYNTH (lrg_midi_synth_get_type ())
#define LRG_MIDI_SYNTH_ERROR (lrg_midi_synth_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMidiSynth, lrg_midi_synth, LRG, MIDI_SYNTH, GObject)

/**
 * LrgMidiSynthError:
 * @LRG_MIDI_SYNTH_ERROR_INVALID: the SoundFont or MIDI data is malformed
 * @LRG_MIDI_SYNTH_ERROR_NO_SOUNDFONT: playing needs a SoundFont first
 * @LRG_MIDI_SYNTH_ERROR_NO_DEVICE: the audio device is not open
 *
 * Error codes of #LrgMidiSynth.
 */
typedef enum
{
    LRG_MIDI_SYNTH_ERROR_INVALID,
    LRG_MIDI_SYNTH_ERROR_NO_SOUNDFONT,
    LRG_MIDI_SYNTH_ERROR_NO_DEVICE
} LrgMidiSynthError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_midi_synth_error_quark (void);

/* ==========================================================================
 * Construction and SoundFonts
 * ========================================================================== */

/**
 * lrg_midi_synth_new:
 * @sample_rate: output rate in Hz (8000 .. 192000; 44100 typical)
 *
 * Creates a synthesizer producing interleaved stereo at @sample_rate.
 * It has no SoundFont yet: load one before playing.
 *
 * Returns: (transfer full): a new #LrgMidiSynth
 */
LRG_AVAILABLE_IN_ALL
LrgMidiSynth * lrg_midi_synth_new (guint sample_rate);

/**
 * lrg_midi_synth_load_soundfont:
 * @self: an #LrgMidiSynth
 * @path: (type filename): a SoundFont 2 file (.sf2)
 * @error: (nullable): return location for a #GError
 *
 * Reads @path and loads it as lrg_midi_synth_load_soundfont_bytes()
 * does.
 *
 * Returns: %TRUE on success; %FALSE with a %G_IO_ERROR when the file
 *   cannot be read, or %LRG_MIDI_SYNTH_ERROR_INVALID
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_synth_load_soundfont (LrgMidiSynth  *self,
                                        const gchar   *path,
                                        GError       **error);

/**
 * lrg_midi_synth_load_soundfont_bytes:
 * @self: an #LrgMidiSynth
 * @sf2: the SoundFont 2 file contents
 * @error: (nullable): return location for a #GError
 *
 * Checks the whole RIFF structure and every index of the preset,
 * instrument and sample tables before handing the bank to
 * TinySoundFont, so a malformed file fails cleanly.  On success the new
 * bank replaces the old one and the songs playing stop at once.  On
 * failure nothing changes.
 *
 * Returns: %TRUE on success; %FALSE with %LRG_MIDI_SYNTH_ERROR_INVALID
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_synth_load_soundfont_bytes (LrgMidiSynth  *self,
                                              GBytes        *sf2,
                                              GError       **error);

/**
 * lrg_midi_synth_has_soundfont:
 * @self: an #LrgMidiSynth
 *
 * Returns: %TRUE once a SoundFont is loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_synth_has_soundfont (LrgMidiSynth *self);

/**
 * lrg_midi_synth_get_preset_count:
 * @self: an #LrgMidiSynth
 *
 * Returns: the presets (instruments) of the loaded SoundFont, 0 without
 */
LRG_AVAILABLE_IN_ALL
guint lrg_midi_synth_get_preset_count (LrgMidiSynth *self);

/**
 * lrg_midi_synth_get_sample_rate:
 * @self: an #LrgMidiSynth
 *
 * Returns: the output rate in Hz
 */
LRG_AVAILABLE_IN_ALL
guint lrg_midi_synth_get_sample_rate (LrgMidiSynth *self);

/* ==========================================================================
 * Playback
 * ========================================================================== */

/**
 * lrg_midi_synth_play:
 * @self: an #LrgMidiSynth
 * @smf: a Standard MIDI File (format 0 or 1, ticks per quarter note)
 * @loop: loop the song: at its end, or between its "loopStart" and
 *   "loopEnd" marker meta events when it has them (a "loopStart" alone
 *   loops from there to the end)
 * @fade_in: seconds the new song fades in; the song playing (if any)
 *   fades out over the same time (a crossfade).  0 switches at once.
 * @error: (nullable): return location for a #GError
 *
 * Starts a song.  The file is checked completely (header, every track's
 * events, running status, meta and SysEx lengths) before anything
 * changes, so a malformed song leaves the current one playing.
 *
 * Returns: %TRUE when the song plays; %FALSE with
 *   %LRG_MIDI_SYNTH_ERROR_NO_SOUNDFONT or %LRG_MIDI_SYNTH_ERROR_INVALID
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_synth_play (LrgMidiSynth  *self,
                              GBytes        *smf,
                              gboolean       loop,
                              gdouble        fade_in,
                              GError       **error);

/**
 * lrg_midi_synth_stop:
 * @self: an #LrgMidiSynth
 * @fade_out: seconds the song fades out; 0 silences it at once
 *
 * Stops the song.  It is no longer "playing" from this call on, even
 * while it fades.
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_synth_stop (LrgMidiSynth *self,
                          gdouble       fade_out);

/**
 * lrg_midi_synth_is_playing:
 * @self: an #LrgMidiSynth
 *
 * Returns: %TRUE while a song plays (looping, or not finished yet)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_synth_is_playing (LrgMidiSynth *self);

/**
 * lrg_midi_synth_is_finished:
 * @self: an #LrgMidiSynth
 *
 * A song started without @loop ends once its last event played and its
 * notes died away (or five seconds after its last event).
 *
 * Returns: %TRUE when the current song ended; cleared by
 *   lrg_midi_synth_play() and lrg_midi_synth_stop()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_synth_is_finished (LrgMidiSynth *self);

/**
 * lrg_midi_synth_set_volume:
 * @self: an #LrgMidiSynth
 * @volume: 0 .. 1 (clamped)
 *
 * Sets the output volume.  The change is applied as a short ramp, so
 * moving a slider never clicks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_synth_set_volume (LrgMidiSynth *self,
                                gfloat        volume);

/**
 * lrg_midi_synth_get_volume:
 * @self: an #LrgMidiSynth
 *
 * Returns: the volume last set (the ramp's target)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_midi_synth_get_volume (LrgMidiSynth *self);

/**
 * lrg_midi_synth_get_position:
 * @self: an #LrgMidiSynth
 *
 * Returns: seconds into the current song (wrapping at its loop), 0
 *   without one
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_midi_synth_get_position (LrgMidiSynth *self);

/**
 * lrg_midi_synth_get_loop_points:
 * @self: an #LrgMidiSynth
 * @loop_start: (out) (optional): seconds where the loop starts
 * @loop_end: (out) (optional): seconds where it ends
 *
 * The loop of the current song: its markers, or the whole song.
 *
 * Returns: %TRUE when the current song loops
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_synth_get_loop_points (LrgMidiSynth *self,
                                         gdouble      *loop_start,
                                         gdouble      *loop_end);

/**
 * lrg_midi_synth_render:
 * @self: an #LrgMidiSynth
 * @frames: (out caller-allocates) (array): room for 2 x @n_frames floats,
 *   written as interleaved stereo (left, right, ...)
 * @n_frames: frames to render
 *
 * Advances the songs by @n_frames and writes their mix (crossfade and
 * volume applied).  Needs no audio device.  Silence without a song or a
 * SoundFont.
 *
 * Returns: the frames written (@n_frames)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_midi_synth_render (LrgMidiSynth *self,
                             gfloat       *frames,
                             guint         n_frames);

/* ==========================================================================
 * Audio device stream
 * ========================================================================== */

/**
 * lrg_midi_synth_start_stream:
 * @self: an #LrgMidiSynth
 * @error: (nullable): return location for a #GError
 *
 * Opens a 32-bit float stereo graylib audio stream of
 * %LRG_MIDI_SYNTH_STREAM_FRAMES frames per buffer and starts it.  Needs
 * the audio device (grl_audio_device_init_audio()).  Calling it again
 * while the stream runs does nothing.  It sets raylib's process-wide
 * default stream buffer size to %LRG_MIDI_SYNTH_STREAM_FRAMES (raylib
 * cannot report the previous value), which streams opened later inherit.
 *
 * Returns: %TRUE when the stream runs; %FALSE with
 *   %LRG_MIDI_SYNTH_ERROR_NO_DEVICE
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_synth_start_stream (LrgMidiSynth  *self,
                                      GError       **error);

/**
 * lrg_midi_synth_update:
 * @self: an #LrgMidiSynth
 *
 * Per frame: renders a buffer for each stream buffer the device has
 * consumed.  Does nothing without a stream.
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_synth_update (LrgMidiSynth *self);

/**
 * lrg_midi_synth_stop_stream:
 * @self: an #LrgMidiSynth
 *
 * Stops and closes the stream (the songs keep their positions).
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_synth_stop_stream (LrgMidiSynth *self);

/**
 * lrg_midi_synth_has_stream:
 * @self: an #LrgMidiSynth
 *
 * Returns: %TRUE while the audio stream runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_synth_has_stream (LrgMidiSynth *self);

G_END_DECLS
