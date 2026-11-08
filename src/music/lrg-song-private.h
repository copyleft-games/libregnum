/* lrg-song-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The data of an LrgSong and the render engine shared by the song
 * (lrg-song.c, the model and .rsong I/O), the offline renderer
 * (lrg-song-render.c), the realtime player (lrg-song-player.c) and MIDI
 * import/export (lrg-midi-file.c).  Not installed.
 */

#pragma once

#include <glib-object.h>
#include "lrg-song.h"
#include "../audio/lrg-audio-mixer.h"

G_BEGIN_DECLS

typedef struct
{
	gdouble beat;
	gint    key;
	gdouble length;
	gdouble velocity;
} LrgSongNote;

typedef struct
{
	gchar *name;       /* drum piece, alias or note */
	gchar *steps;
} LrgSongLane;

typedef struct
{
	gchar     *id;
	gdouble    length;
	guint      steps;
	GArray    *notes;  /* LrgSongNote, sorted by beat then key */
	GPtrArray *lanes;  /* LrgSongLane */
} LrgSongPattern;

typedef struct
{
	gchar      *id;
	gchar      *type;     /* synth, drums, sampler, soundfont, foley */
	gchar      *source;   /* preset, kit or file */
	GHashTable *params;   /* name -> gdouble* */
} LrgSongInstrument;

typedef struct
{
	gchar   *source;      /* pattern id or file */
	gdouble  start;
	gdouble  length;      /* 0: one pattern pass / the whole file */
	gdouble  offset;      /* beats into the pattern, seconds into the file */
	gdouble  transpose;
	gdouble  gain;        /* dB */
	gdouble  fade_in;     /* seconds */
	gdouble  fade_out;
	gboolean mute;
} LrgSongClip;

typedef struct
{
	gchar   *target;
	gboolean step;
	GArray  *points;      /* gdouble pairs: beat, value (sorted by beat) */
} LrgSongAutomation;

typedef struct
{
	gchar            *id;
	gchar            *name;
	LrgSongTrackKind  kind;
	gchar            *instrument;
	gchar            *bus;
	gdouble           volume;   /* dB */
	gdouble           pan;
	gboolean          mute;
	gboolean          solo;
	GArray           *clips;    /* LrgSongClip, sorted by start */
	GPtrArray        *automation;
} LrgSongTrack;

typedef struct
{
	gdouble  beat;
	gdouble  bpm;
	gboolean ramp;
} LrgSongTempo;

typedef struct
{
	gdouble  beat;
	gchar   *name;
} LrgSongMarker;

struct _LrgSong
{
	GObject        parent_instance;

	gchar         *title;
	gchar         *artist;
	gdouble        bpm;
	guint          beats_per_bar;
	guint          beat_unit;
	guint          rate;
	guint32        seed;
	gdouble        length;      /* 0: auto */
	gdouble        loop_start;
	gdouble        loop_end;
	gchar         *base_path;

	GArray        *tempo;       /* LrgSongTempo after beat 0, sorted */
	GArray        *markers;     /* LrgSongMarker, sorted by beat */
	GPtrArray     *instruments; /* LrgSongInstrument */
	GPtrArray     *patterns;    /* LrgSongPattern */
	GPtrArray     *tracks;      /* LrgSongTrack, in order */
	GPtrArray     *automation;  /* LrgSongAutomation, song-wide */
	LrgMixerAsset *mixer;

	GHashTable    *file_seconds; /* resolved path -> gdouble*: audio lengths */
};

/* lookups (NULL when absent) */
G_GNUC_INTERNAL LrgSongTrack      *lrg_song_priv_track      (LrgSong *self, const gchar *id);
G_GNUC_INTERNAL LrgSongPattern    *lrg_song_priv_pattern    (LrgSong *self, const gchar *id);
G_GNUC_INTERNAL LrgSongInstrument *lrg_song_priv_instrument (LrgSong *self, const gchar *id);

/* @rel resolved against the song's base path (absolute paths pass) */
G_GNUC_INTERNAL gchar *lrg_song_priv_resolve (LrgSong *self, const gchar *rel);

/* the key a drum or note lane plays, or -1 */
G_GNUC_INTERNAL gint lrg_song_priv_lane_key (const gchar *lane);

/* the velocity of a step character (0: rest, < 0: not a step) */
G_GNUC_INTERNAL gdouble lrg_song_priv_step_velocity (gchar c);

/*
 * lrg_song_priv_pattern_notes:
 * Every note of @pattern: its notes, then its lanes as one-step notes;
 * sorted by beat.  Returns a new GArray of LrgSongNote.
 */
G_GNUC_INTERNAL GArray *lrg_song_priv_pattern_notes (LrgSongPattern *pattern);

/* the clip's length in beats with 0 resolved (pattern pass, file length) */
G_GNUC_INTERNAL gdouble lrg_song_priv_clip_length (LrgSong            *self,
                                                   const LrgSongTrack *track,
                                                   const LrgSongClip  *clip);

/* decodes an audio file as stereo float at @rate (cached by nothing) */
G_GNUC_INTERNAL LrgWaveData *lrg_song_priv_load_audio (const gchar  *path,
                                                       guint         rate,
                                                       GError      **error);

/* ==========================================================================
 * Engine: a compiled song that renders block by block
 * ========================================================================== */

typedef struct _LrgSongEngine LrgSongEngine;

/*
 * lrg_song_engine_new:
 * Compiles @song (read only during the call) for @rate: instruments,
 * audio clips, every note and automation point in frames, and a mixer
 * built from the song's layout with the clamp off.  @mute and @solo name
 * tracks to silence or keep.  The engine can then be used from any one
 * thread.
 */
G_GNUC_INTERNAL LrgSongEngine *lrg_song_engine_new (LrgSong             *song,
                                                    guint                rate,
                                                    const gchar * const *mute,
                                                    const gchar * const *solo,
                                                    GError             **error);

G_GNUC_INTERNAL void lrg_song_engine_free (LrgSongEngine *engine);

/* jumps to @frame: every voice cut, the next notes from there on */
G_GNUC_INTERNAL void lrg_song_engine_seek (LrgSongEngine *engine,
                                           guint64        frame);

G_GNUC_INTERNAL guint64 lrg_song_engine_get_frame (LrgSongEngine *engine);

/* the frame where the music (the song length) ends */
G_GNUC_INTERNAL guint64 lrg_song_engine_get_end_frame (LrgSongEngine *engine);

/* writes @frames stereo frames to @out (overwriting) */
G_GNUC_INTERNAL void lrg_song_engine_render (LrgSongEngine *engine,
                                             gfloat        *out,
                                             guint          frames);

G_GNUC_INTERNAL LrgAudioMixer *lrg_song_engine_get_mixer (LrgSongEngine *engine);

/* the engine's rate and the song's frame <-> beat mapping at that rate */
G_GNUC_INTERNAL guint   lrg_song_engine_get_rate (LrgSongEngine *engine);
G_GNUC_INTERNAL gdouble lrg_song_engine_frame_to_beat (LrgSongEngine *engine,
                                                       guint64        frame);
G_GNUC_INTERNAL guint64 lrg_song_engine_beat_to_frame (LrgSongEngine *engine,
                                                       gdouble        beat);

/* the index of track @id in the engine, or -1 */
G_GNUC_INTERNAL gint lrg_song_engine_find_track (LrgSongEngine *engine,
                                                 const gchar   *id);

/* an extra gain on a track (adaptive layers), ramped over @seconds */
G_GNUC_INTERNAL void lrg_song_engine_set_layer_gain (LrgSongEngine *engine,
                                                     guint          track,
                                                     gdouble        gain,
                                                     gdouble        seconds);

/*
 * lrg_song_engine_play_note:
 * Plays @key on track @track's instrument from song frame @at for
 * @length frames (0: until a note-off), outside the song's own notes
 * (stingers, previews).  Frames before the current one play at once.
 */
G_GNUC_INTERNAL void lrg_song_engine_play_note (LrgSongEngine *engine,
                                                guint          track,
                                                guint64        at,
                                                gint           key,
                                                gdouble        velocity,
                                                guint64        length);

/* releases @key on track @track's instrument now */
G_GNUC_INTERNAL void lrg_song_engine_release_note (LrgSongEngine *engine,
                                                   guint          track,
                                                   gint           key);

/* the instrument of track @track (NULL for audio tracks), for live notes */
G_GNUC_INTERNAL LrgInstrument *lrg_song_engine_get_instrument (LrgSongEngine *engine,
                                                               guint          track);

G_END_DECLS
