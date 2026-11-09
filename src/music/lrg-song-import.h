/* lrg-song-import.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Turning a recording (WAV, FLAC, MP3, Ogg, Opus, M4A, ...) into an
 * #LrgSong: the audio itself on an audio track, resampled to the song's
 * rate and aligned to a detected tempo grid with sections as markers, and
 * optionally a transcription (drum steps, bass, melody, chords) on muted
 * instrument tracks to edit and play with the built-in instruments.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-song.h"

G_BEGIN_DECLS

/**
 * LrgSongImportOptions:
 * @sample_rate: the song's rate; the recording is resampled to it (0 for
 *   48000)
 * @bpm: the tempo, or 0 to detect it
 * @min_bpm: the slowest tempo detection considers (0 for 70)
 * @max_bpm: the fastest tempo detection considers (0 for 180)
 * @beats_per_bar: beats in a bar (0 for 4)
 * @sections: find sections and mark them
 * @section_bars: the shortest section in bars (0 for 4)
 * @transcribe: also estimate drums, bass, melody and chords as muted
 *   instrument tracks
 * @title: (nullable): the song's title (%NULL: the file name)
 * @artist: (nullable): the song's artist
 * @audio_dir: (nullable): the folder, relative to the output folder, that
 *   receives the aligned WAV (%NULL for "audio")
 *
 * Options of lrg_song_import_audio(); lrg_song_import_options_new() fills
 * the defaults.
 */
typedef struct
{
	guint     sample_rate;
	gdouble   bpm;
	gdouble   min_bpm;
	gdouble   max_bpm;
	guint     beats_per_bar;
	gboolean  sections;
	guint     section_bars;
	gboolean  transcribe;
	gchar    *title;
	gchar    *artist;
	gchar    *audio_dir;

	/*< private >*/
	gpointer _reserved[4];
} LrgSongImportOptions;

LRG_AVAILABLE_IN_ALL
GType lrg_song_import_options_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SONG_IMPORT_OPTIONS (lrg_song_import_options_get_type ())

/**
 * lrg_song_import_options_new:
 *
 * Returns: (transfer full): the default options (detect the tempo, find
 *   sections, no transcription, 48 kHz)
 */
LRG_AVAILABLE_IN_ALL
LrgSongImportOptions * lrg_song_import_options_new (void);

/**
 * lrg_song_import_options_copy:
 * @options: options
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgSongImportOptions * lrg_song_import_options_copy (const LrgSongImportOptions *options);

/**
 * lrg_song_import_options_free:
 * @options: options
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_import_options_free (LrgSongImportOptions *options);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSongImportOptions, lrg_song_import_options_free)

/**
 * LrgSongImportReport:
 * @bpm: the tempo used
 * @bpm_detected: %TRUE when it was detected, %FALSE when given
 * @confidence: how clearly the tempo stood out, 0 .. 1 (below about 0.3
 *   the grid is a guess: give the tempo)
 * @alternative_bpm: half or double @bpm when it fits the onsets nearly as
 *   well (doom at 66 or 132, metal at 150 or 75: the tempo octave is
 *   ambiguous), else 0; import again with that tempo if it feels right
 * @first_beat: seconds into the recording of its first downbeat
 * @lead_in: seconds of silence put before the recording so a bar starts
 *   at beat 0
 * @duration: the recording's length in seconds
 * @source_rate: the recording's own sample rate
 * @key: (nullable): the estimated key ("A minor")
 * @audio_file: the aligned WAV, relative to the output folder
 * @sections: the sections found
 * @drum_hits: drum steps transcribed
 * @bass_notes: bass notes transcribed
 * @melody_notes: melody notes transcribed
 * @chords: chords transcribed
 *
 * What lrg_song_import_audio() found.
 */
typedef struct
{
	gdouble   bpm;
	gboolean  bpm_detected;
	gdouble   confidence;
	gdouble   alternative_bpm;
	gdouble   first_beat;
	gdouble   lead_in;
	gdouble   duration;
	guint     source_rate;
	gchar    *key;
	gchar    *audio_file;
	guint     sections;
	guint     drum_hits;
	guint     bass_notes;
	guint     melody_notes;
	guint     chords;

	/*< private >*/
	gpointer _reserved[4];
} LrgSongImportReport;

LRG_AVAILABLE_IN_ALL
GType lrg_song_import_report_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SONG_IMPORT_REPORT (lrg_song_import_report_get_type ())

/**
 * lrg_song_import_report_copy:
 * @report: a report
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgSongImportReport * lrg_song_import_report_copy (const LrgSongImportReport *report);

/**
 * lrg_song_import_report_free:
 * @report: a report
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_import_report_free (LrgSongImportReport *report);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSongImportReport, lrg_song_import_report_free)

/**
 * lrg_song_import_audio:
 * @audio_path: a recording: anything lrg_audio_decode_file() reads
 * @out_dir: the folder the song will be saved in; the aligned WAV is
 *   written below it (#LrgSongImportOptions:audio_dir) and the song's
 *   #LrgSong:base-path is set to it
 * @options: (nullable): options (%NULL for the defaults)
 * @out_report: (out) (optional) (transfer full): what was found
 * @error: (nullable): return location for an error
 *
 * Makes a song of a recording.  The recording is decoded, resampled to
 * the song's rate with a windowed-sinc filter, and its tempo, beat phase
 * and downbeats are found (or the tempo given is used); silence is put in
 * front so that a bar starts at beat 0, and the result is written as a
 * WAV and played by one clip on the audio track "original".  Rendering
 * the song therefore gives back the recording (after the lead-in), and
 * every change (cutting, moving, layering instruments, effects,
 * automation) lines up with its bars.  Sections become markers.
 *
 * With @options->transcribe, the recording is also estimated as music:
 * drum steps (kick, snare, hat) from onsets per band, a bass line and a
 * melody from pitch tracking, and chords from the harmony, each on its
 * own muted instrument track (drums, bass, melody, chords) with patterns
 * shared by identical bars.  Estimates are approximate: dense mixes
 * transcribe less well than sparse ones.
 *
 * The song is not saved: save it with lrg_song_save_to_file() in
 * @out_dir.
 *
 * Returns: (transfer full) (nullable): the song
 */
LRG_AVAILABLE_IN_ALL
LrgSong * lrg_song_import_audio (const gchar                 *audio_path,
                                 const gchar                 *out_dir,
                                 const LrgSongImportOptions  *options,
                                 LrgSongImportReport        **out_report,
                                 GError                     **error);

/**
 * lrg_music_detect_tempo:
 * @samples: (array length=n_samples): interleaved samples
 * @n_samples: the number of samples (frames x @channels)
 * @channels: 1 .. 8
 * @sample_rate: their rate
 * @min_bpm: the slowest tempo considered (0 for 70)
 * @max_bpm: the fastest tempo considered (0 for 180)
 * @out_bpm: (out): the tempo
 * @out_first_beat: (out) (optional): seconds to the first beat
 * @out_confidence: (out) (optional): 0 .. 1
 *
 * Finds the tempo of music from its onsets: the period whose comb of
 * beats lines up best with them, refined over the whole recording.
 *
 * Returns: %FALSE when there is too little to go on (shorter than about
 *   four seconds, or silent)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_music_detect_tempo (const gfloat *samples,
                                 gsize         n_samples,
                                 guint         channels,
                                 guint         sample_rate,
                                 gdouble       min_bpm,
                                 gdouble       max_bpm,
                                 gdouble      *out_bpm,
                                 gdouble      *out_first_beat,
                                 gdouble      *out_confidence);

/**
 * lrg_music_detect_key:
 * @samples: (array length=n_samples): interleaved samples
 * @n_samples: the number of samples (frames x @channels)
 * @channels: 1 .. 8
 * @sample_rate: their rate
 *
 * Estimates the key from the balance of the twelve pitch classes
 * (Krumhansl-Schmuckler profiles).
 *
 * Returns: (transfer full) (nullable): "C major", "F# minor", ...; %NULL
 *   for silence
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_music_detect_key (const gfloat *samples,
                              gsize         n_samples,
                              guint         channels,
                              guint         sample_rate);

G_END_DECLS
