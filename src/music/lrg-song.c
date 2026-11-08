/* lrg-song.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The song model, its editing API and the .rsong format (see lrg-song.h
 * and docs/modules/music/song.org).  Rendering is in lrg-song-render.c.
 *
 *   version: 1
 *   song:
 *     title: Neon Drive
 *     bpm: 108
 *     time_signature: 4/4
 *     sample_rate: 48000
 *     seed: 1985
 *     tempo:   [ {beat, bpm, ramp} ... ]
 *     markers: [ {beat, name} ... ]
 *   instruments:
 *     bass: { type: synth, source: synthwave-bass, params: { cutoff: 400 } }
 *   patterns:
 *     bass-a:
 *       length: 4
 *       notes: [ "0 A1 0.5 0.9", ... ]     (beat key length velocity)
 *       steps: 16
 *       lanes: { kick: "x...x...x...x..." }
 *   tracks:
 *     - id: bass
 *       instrument: bass
 *       bus: bass
 *       clips: [ { pattern: bass-a, start: 32, length: 64 } ]
 *       automation: [ { target: param.cutoff, points: [ "32 400", "96 2400" ] } ]
 *   automation: [ { target: bus.pad.volume, points: [...] } ]
 *   mixer: (the .rmixer schema)
 *
 * Block sequences are written one element per line; lists of numbers are
 * packed into one scalar per element ("beat key length velocity") so a
 * song stays readable in a diff.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lrg-song.h"
#include "lrg-song-private.h"
#include "lrg-synth.h"
#include "lrg-drum-machine.h"
#include "lrg-sampler.h"
#include "lrg-sound-font-instrument.h"
#include "lrg-foley-instrument.h"
#include "../audio/lrg-audio-yaml-private.h"
#include "../audio/lrg-audio-param-private.h"

G_DEFINE_QUARK (lrg-song-error-quark, lrg_song_error)

G_DEFINE_ENUM_TYPE (LrgSongTrackKind, lrg_song_track_kind,
	G_DEFINE_ENUM_VALUE (LRG_SONG_TRACK_INSTRUMENT, "instrument"),
	G_DEFINE_ENUM_VALUE (LRG_SONG_TRACK_AUDIO, "audio"))

G_DEFINE_ENUM_TYPE (LrgSongNormalize, lrg_song_normalize,
	G_DEFINE_ENUM_VALUE (LRG_SONG_NORMALIZE_NONE, "none"),
	G_DEFINE_ENUM_VALUE (LRG_SONG_NORMALIZE_PEAK, "peak"),
	G_DEFINE_ENUM_VALUE (LRG_SONG_NORMALIZE_LOUDNESS, "loudness"))

G_DEFINE_BOXED_TYPE (LrgSongRenderOptions, lrg_song_render_options, lrg_song_render_options_copy,
                     lrg_song_render_options_free)

G_DEFINE_FINAL_TYPE (LrgSong, lrg_song, G_TYPE_OBJECT)

#define DEFAULT_BPM  (120.0)
#define DEFAULT_RATE (48000)

enum
{
	PROP_0,
	PROP_TITLE,
	PROP_ARTIST,
	PROP_BPM,
	PROP_BEATS_PER_BAR,
	PROP_BEAT_UNIT,
	PROP_SAMPLE_RATE,
	PROP_SEED,
	PROP_LENGTH,
	PROP_LOOP_START,
	PROP_LOOP_END,
	PROP_BASE_PATH,
	N_PROPS
};

static GParamSpec *props[N_PROPS];

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Render options
 * ========================================================================== */

LrgSongRenderOptions *
lrg_song_render_options_new (void)
{
	LrgSongRenderOptions *o = g_new0 (LrgSongRenderOptions, 1);

	o->tail = -1.0;
	o->normalize = LRG_SONG_NORMALIZE_NONE;
	o->target = -14.0;
	o->true_peak_ceiling = -1.0;
	o->preroll = TRUE;
	return o;
}

LrgSongRenderOptions *
lrg_song_render_options_copy (const LrgSongRenderOptions *options)
{
	LrgSongRenderOptions *o;

	g_return_val_if_fail (options != NULL, NULL);
	o = g_new0 (LrgSongRenderOptions, 1);
	*o = *options;
	o->mute = g_strdupv (options->mute);
	o->solo = g_strdupv (options->solo);
	return o;
}

void
lrg_song_render_options_free (LrgSongRenderOptions *options)
{
	if (options == NULL)
		return;
	g_strfreev (options->mute);
	g_strfreev (options->solo);
	g_free (options);
}

/* ==========================================================================
 * Element lifetimes
 * ========================================================================== */

static void
lane_free (gpointer data)
{
	LrgSongLane *lane = data;

	g_free (lane->name);
	g_free (lane->steps);
	g_free (lane);
}

static LrgSongPattern *
pattern_new (const gchar *id,
             gdouble      length)
{
	LrgSongPattern *p = g_new0 (LrgSongPattern, 1);

	p->id = g_strdup (id);
	p->length = length;
	p->steps = 16;
	p->notes = g_array_new (FALSE, FALSE, sizeof (LrgSongNote));
	p->lanes = g_ptr_array_new_with_free_func (lane_free);
	return p;
}

static void
pattern_free (gpointer data)
{
	LrgSongPattern *p = data;

	g_free (p->id);
	g_array_unref (p->notes);
	g_ptr_array_unref (p->lanes);
	g_free (p);
}

static LrgSongPattern *
pattern_copy (const LrgSongPattern *src)
{
	LrgSongPattern *p = pattern_new (src->id, src->length);
	guint           i;

	p->steps = src->steps;
	g_array_append_vals (p->notes, src->notes->data, src->notes->len);
	for (i = 0; i < src->lanes->len; i++)
	{
		const LrgSongLane *l = g_ptr_array_index (src->lanes, i);
		LrgSongLane       *c = g_new0 (LrgSongLane, 1);

		c->name = g_strdup (l->name);
		c->steps = g_strdup (l->steps);
		g_ptr_array_add (p->lanes, c);
	}
	return p;
}

static LrgSongInstrument *
instrument_new (const gchar *id,
                const gchar *type,
                const gchar *source)
{
	LrgSongInstrument *in = g_new0 (LrgSongInstrument, 1);

	in->id = g_strdup (id);
	in->type = g_strdup (type);
	in->source = g_strdup (source);
	in->params = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	return in;
}

static void
instrument_free (gpointer data)
{
	LrgSongInstrument *in = data;

	g_free (in->id);
	g_free (in->type);
	g_free (in->source);
	g_hash_table_unref (in->params);
	g_free (in);
}

static LrgSongInstrument *
instrument_copy (const LrgSongInstrument *src)
{
	LrgSongInstrument *in = instrument_new (src->id, src->type, src->source);
	GHashTableIter     iter;
	gpointer           k, v;

	g_hash_table_iter_init (&iter, src->params);
	while (g_hash_table_iter_next (&iter, &k, &v))
		g_hash_table_insert (in->params, g_strdup (k), g_memdup2 (v, sizeof (gdouble)));
	return in;
}

static void
clip_clear (gpointer data)
{
	g_free (((LrgSongClip *) data)->source);
}

static LrgSongAutomation *
automation_new (const gchar *target)
{
	LrgSongAutomation *a = g_new0 (LrgSongAutomation, 1);

	a->target = g_strdup (target);
	a->points = g_array_new (FALSE, FALSE, sizeof (gdouble));
	return a;
}

static void
automation_free (gpointer data)
{
	LrgSongAutomation *a = data;

	g_free (a->target);
	g_array_unref (a->points);
	g_free (a);
}

static LrgSongAutomation *
automation_copy (const LrgSongAutomation *src)
{
	LrgSongAutomation *a = automation_new (src->target);

	a->step = src->step;
	g_array_append_vals (a->points, src->points->data, src->points->len);
	return a;
}

static LrgSongTrack *
track_new (const gchar      *id,
           LrgSongTrackKind  kind)
{
	LrgSongTrack *t = g_new0 (LrgSongTrack, 1);

	t->id = g_strdup (id);
	t->kind = kind;
	t->bus = g_strdup ("master");
	t->clips = g_array_new (FALSE, FALSE, sizeof (LrgSongClip));
	g_array_set_clear_func (t->clips, clip_clear);
	t->automation = g_ptr_array_new_with_free_func (automation_free);
	return t;
}

static void
track_free (gpointer data)
{
	LrgSongTrack *t = data;

	g_free (t->id);
	g_free (t->name);
	g_free (t->instrument);
	g_free (t->bus);
	g_array_unref (t->clips);
	g_ptr_array_unref (t->automation);
	g_free (t);
}

static LrgSongTrack *
track_copy (const LrgSongTrack *src)
{
	LrgSongTrack *t = track_new (src->id, src->kind);
	guint         i;

	t->name = g_strdup (src->name);
	t->instrument = g_strdup (src->instrument);
	g_free (t->bus);
	t->bus = g_strdup (src->bus);
	t->volume = src->volume;
	t->pan = src->pan;
	t->mute = src->mute;
	t->solo = src->solo;
	for (i = 0; i < src->clips->len; i++)
	{
		LrgSongClip c = g_array_index (src->clips, LrgSongClip, i);

		c.source = g_strdup (c.source);
		g_array_append_val (t->clips, c);
	}
	for (i = 0; i < src->automation->len; i++)
		g_ptr_array_add (t->automation, automation_copy (g_ptr_array_index (src->automation, i)));
	return t;
}

static void
marker_clear (gpointer data)
{
	g_free (((LrgSongMarker *) data)->name);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
changed (LrgSong *self)
{
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
lrg_song_finalize (GObject *object)
{
	LrgSong *self = LRG_SONG (object);

	g_free (self->title);
	g_free (self->artist);
	g_free (self->base_path);
	g_array_unref (self->tempo);
	g_array_unref (self->markers);
	g_ptr_array_unref (self->instruments);
	g_ptr_array_unref (self->patterns);
	g_ptr_array_unref (self->tracks);
	g_ptr_array_unref (self->automation);
	g_clear_object (&self->mixer);
	g_hash_table_unref (self->file_seconds);
	G_OBJECT_CLASS (lrg_song_parent_class)->finalize (object);
}

static void
lrg_song_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
	LrgSong *self = LRG_SONG (object);

	switch (prop_id)
	{
	case PROP_TITLE: g_value_set_string (value, self->title); break;
	case PROP_ARTIST: g_value_set_string (value, self->artist); break;
	case PROP_BPM: g_value_set_double (value, self->bpm); break;
	case PROP_BEATS_PER_BAR: g_value_set_uint (value, self->beats_per_bar); break;
	case PROP_BEAT_UNIT: g_value_set_uint (value, self->beat_unit); break;
	case PROP_SAMPLE_RATE: g_value_set_uint (value, self->rate); break;
	case PROP_SEED: g_value_set_uint (value, self->seed); break;
	case PROP_LENGTH: g_value_set_double (value, self->length); break;
	case PROP_LOOP_START: g_value_set_double (value, self->loop_start); break;
	case PROP_LOOP_END: g_value_set_double (value, self->loop_end); break;
	case PROP_BASE_PATH: g_value_set_string (value, self->base_path); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_song_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
	LrgSong *self = LRG_SONG (object);

	switch (prop_id)
	{
	case PROP_TITLE: g_free (self->title); self->title = g_value_dup_string (value); break;
	case PROP_ARTIST: g_free (self->artist); self->artist = g_value_dup_string (value); break;
	case PROP_BPM: self->bpm = g_value_get_double (value); break;
	case PROP_BEATS_PER_BAR: self->beats_per_bar = g_value_get_uint (value); break;
	case PROP_BEAT_UNIT: self->beat_unit = g_value_get_uint (value); break;
	case PROP_SAMPLE_RATE: self->rate = g_value_get_uint (value); break;
	case PROP_SEED: self->seed = g_value_get_uint (value); break;
	case PROP_LENGTH: self->length = g_value_get_double (value); break;
	case PROP_LOOP_START: self->loop_start = g_value_get_double (value); break;
	case PROP_LOOP_END: self->loop_end = g_value_get_double (value); break;
	case PROP_BASE_PATH: g_free (self->base_path); self->base_path = g_value_dup_string (value); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); return;
	}
	changed (self);
}

static void
lrg_song_class_init (LrgSongClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;

	object_class->finalize = lrg_song_finalize;
	object_class->get_property = lrg_song_get_property;
	object_class->set_property = lrg_song_set_property;

	/**
	 * LrgSong:title:
	 *
	 * The song's title (nullable).
	 */
	props[PROP_TITLE] = g_param_spec_string ("title", NULL, NULL, NULL, flags);

	/**
	 * LrgSong:artist:
	 *
	 * Who made it (nullable); written into exported files' tags.
	 */
	props[PROP_ARTIST] = g_param_spec_string ("artist", NULL, NULL, NULL, flags);

	/**
	 * LrgSong:bpm:
	 *
	 * The tempo at beat 0 (lrg_song_add_tempo_change() for changes).
	 */
	props[PROP_BPM] = g_param_spec_double ("bpm", NULL, NULL, 20.0, 400.0, DEFAULT_BPM, flags);

	/**
	 * LrgSong:beats-per-bar:
	 *
	 * The upper number of the time signature.
	 */
	props[PROP_BEATS_PER_BAR] = g_param_spec_uint ("beats-per-bar", NULL, NULL, 1, 32, 4, flags);

	/**
	 * LrgSong:beat-unit:
	 *
	 * The lower number of the time signature (1, 2, 4, 8 or 16).  A beat
	 * is always one quarter note for tempo purposes, as in MIDI files.
	 */
	props[PROP_BEAT_UNIT] = g_param_spec_uint ("beat-unit", NULL, NULL, 1, 16, 4, flags);

	/**
	 * LrgSong:sample-rate:
	 *
	 * The rate songs render at.
	 */
	props[PROP_SAMPLE_RATE] = g_param_spec_uint ("sample-rate", NULL, NULL, 8000, 192000, DEFAULT_RATE, flags);

	/**
	 * LrgSong:seed:
	 *
	 * Seeds anything random (instrument noise, humanising helpers).
	 */
	props[PROP_SEED] = g_param_spec_uint ("seed", NULL, NULL, 0, G_MAXUINT32, 1, flags);

	/**
	 * LrgSong:length:
	 *
	 * The length in beats; 0 ends the song at the end of its last clip.
	 */
	props[PROP_LENGTH] = g_param_spec_double ("length", NULL, NULL, 0.0, LRG_SONG_MAX_BEATS, 0.0, flags);

	/**
	 * LrgSong:loop-start:
	 *
	 * The first beat of the loop range (players loop it when it is not
	 * empty).
	 */
	props[PROP_LOOP_START] = g_param_spec_double ("loop-start", NULL, NULL, 0.0, LRG_SONG_MAX_BEATS, 0.0, flags);

	/**
	 * LrgSong:loop-end:
	 *
	 * The beat after the loop range.
	 */
	props[PROP_LOOP_END] = g_param_spec_double ("loop-end", NULL, NULL, 0.0, LRG_SONG_MAX_BEATS, 0.0, flags);

	/**
	 * LrgSong:base-path:
	 *
	 * The folder relative file names resolve against (nullable: the
	 * working directory).
	 */
	props[PROP_BASE_PATH] = g_param_spec_string ("base-path", NULL, NULL, NULL, flags);

	g_object_class_install_properties (object_class, N_PROPS, props);

	/**
	 * LrgSong::changed:
	 * @self: the song
	 *
	 * Emitted after any change.
	 */
	signals[SIGNAL_CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
	                                        NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_song_init (LrgSong *self)
{
	self->bpm = DEFAULT_BPM;
	self->beats_per_bar = 4;
	self->beat_unit = 4;
	self->rate = DEFAULT_RATE;
	self->seed = 1;
	self->tempo = g_array_new (FALSE, FALSE, sizeof (LrgSongTempo));
	self->markers = g_array_new (FALSE, FALSE, sizeof (LrgSongMarker));
	g_array_set_clear_func (self->markers, marker_clear);
	self->instruments = g_ptr_array_new_with_free_func (instrument_free);
	self->patterns = g_ptr_array_new_with_free_func (pattern_free);
	self->tracks = g_ptr_array_new_with_free_func (track_free);
	self->automation = g_ptr_array_new_with_free_func (automation_free);
	self->mixer = lrg_mixer_asset_new ();
	self->file_seconds = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

LrgSong *
lrg_song_new (void)
{
	return g_object_new (LRG_TYPE_SONG, NULL);
}

LrgSong *
lrg_song_copy (LrgSong *self)
{
	LrgSong *copy;
	guint    i;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);

	copy = lrg_song_new ();
	copy->title = g_strdup (self->title);
	copy->artist = g_strdup (self->artist);
	copy->bpm = self->bpm;
	copy->beats_per_bar = self->beats_per_bar;
	copy->beat_unit = self->beat_unit;
	copy->rate = self->rate;
	copy->seed = self->seed;
	copy->length = self->length;
	copy->loop_start = self->loop_start;
	copy->loop_end = self->loop_end;
	copy->base_path = g_strdup (self->base_path);
	g_array_append_vals (copy->tempo, self->tempo->data, self->tempo->len);
	for (i = 0; i < self->markers->len; i++)
	{
		LrgSongMarker m = g_array_index (self->markers, LrgSongMarker, i);

		m.name = g_strdup (m.name);
		g_array_append_val (copy->markers, m);
	}
	for (i = 0; i < self->instruments->len; i++)
		g_ptr_array_add (copy->instruments, instrument_copy (g_ptr_array_index (self->instruments, i)));
	for (i = 0; i < self->patterns->len; i++)
		g_ptr_array_add (copy->patterns, pattern_copy (g_ptr_array_index (self->patterns, i)));
	for (i = 0; i < self->tracks->len; i++)
		g_ptr_array_add (copy->tracks, track_copy (g_ptr_array_index (self->tracks, i)));
	for (i = 0; i < self->automation->len; i++)
		g_ptr_array_add (copy->automation, automation_copy (g_ptr_array_index (self->automation, i)));
	g_clear_object (&copy->mixer);
	copy->mixer = lrg_mixer_asset_copy (self->mixer);
	return copy;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* ids: letters, digits, '-', '_', '.', 1 .. 64 characters */
static gboolean
valid_id (const gchar *id)
{
	const gchar *p;

	if (id == NULL || *id == '\0' || strlen (id) > 64)
		return FALSE;
	for (p = id; *p != '\0'; p++)
		if (!g_ascii_isalnum (*p) && *p != '-' && *p != '_' && *p != '.')
			return FALSE;
	return TRUE;
}

LrgSongTrack *
lrg_song_priv_track (LrgSong     *self,
                     const gchar *id)
{
	guint i;

	if (id == NULL)
		return NULL;
	for (i = 0; i < self->tracks->len; i++)
		if (g_str_equal (((LrgSongTrack *) g_ptr_array_index (self->tracks, i))->id, id))
			return g_ptr_array_index (self->tracks, i);
	return NULL;
}

LrgSongPattern *
lrg_song_priv_pattern (LrgSong     *self,
                       const gchar *id)
{
	guint i;

	if (id == NULL)
		return NULL;
	for (i = 0; i < self->patterns->len; i++)
		if (g_str_equal (((LrgSongPattern *) g_ptr_array_index (self->patterns, i))->id, id))
			return g_ptr_array_index (self->patterns, i);
	return NULL;
}

LrgSongInstrument *
lrg_song_priv_instrument (LrgSong     *self,
                          const gchar *id)
{
	guint i;

	if (id == NULL)
		return NULL;
	for (i = 0; i < self->instruments->len; i++)
		if (g_str_equal (((LrgSongInstrument *) g_ptr_array_index (self->instruments, i))->id, id))
			return g_ptr_array_index (self->instruments, i);
	return NULL;
}

gchar *
lrg_song_priv_resolve (LrgSong     *self,
                       const gchar *rel)
{
	if (rel == NULL)
		return NULL;
	if (g_path_is_absolute (rel) || self->base_path == NULL)
		return g_strdup (rel);
	return g_build_filename (self->base_path, rel, NULL);
}

gint
lrg_song_priv_lane_key (const gchar *lane)
{
	gint key = lrg_drum_machine_piece_to_key (lane);

	return key >= 0 ? key : lrg_music_key_from_name (lane);
}

gdouble
lrg_song_priv_step_velocity (gchar c)
{
	switch (c)
	{
	case '.': case '-': return 0.0;
	case 'g': return 0.3;
	case 'o': return 0.5;
	case 'x': return 0.8;
	case 'X': return 1.0;
	default: return -1.0;
	}
}

static gint
note_compare (gconstpointer a,
              gconstpointer b)
{
	const LrgSongNote *na = a;
	const LrgSongNote *nb = b;

	if (na->beat != nb->beat)
		return na->beat < nb->beat ? -1 : 1;
	return na->key - nb->key;
}

GArray *
lrg_song_priv_pattern_notes (LrgSongPattern *pattern)
{
	GArray *notes = g_array_new (FALSE, FALSE, sizeof (LrgSongNote));
	gdouble step = pattern->length / (gdouble) MAX (pattern->steps, 1u);
	guint   i;

	g_array_append_vals (notes, pattern->notes->data, pattern->notes->len);
	for (i = 0; i < pattern->lanes->len; i++)
	{
		const LrgSongLane *lane = g_ptr_array_index (pattern->lanes, i);
		gint               key = lrg_song_priv_lane_key (lane->name);
		const gchar       *c;
		guint              s = 0;

		if (key < 0)
			continue;
		for (c = lane->steps; *c != '\0' && s < pattern->steps; c++)
		{
			gdouble vel = lrg_song_priv_step_velocity (*c);
			LrgSongNote n;

			if (vel < 0.0)
				continue;          /* spaces and bars */
			if (vel > 0.0)
			{
				n.beat = s * step;
				n.key = key;
				n.length = step;
				n.velocity = vel;
				g_array_append_val (notes, n);
			}
			s++;
		}
	}
	g_array_sort (notes, note_compare);
	return notes;
}

gdouble
lrg_song_priv_clip_length (LrgSong            *self,
                           const LrgSongTrack *track,
                           const LrgSongClip  *clip)
{
	if (clip->length > 0.0)
		return clip->length;
	if (track->kind == LRG_SONG_TRACK_INSTRUMENT)
	{
		LrgSongPattern *p = lrg_song_priv_pattern (self, clip->source);

		return p != NULL ? MAX (p->length - clip->offset, 0.0) : 0.0;
	}
	else
	{
		g_autofree gchar *path = lrg_song_priv_resolve (self, clip->source);
		gdouble          *cached;
		gdouble           seconds;
		gdouble           start_s;

		if (path == NULL)
			return 0.0;
		cached = g_hash_table_lookup (self->file_seconds, path);
		if (cached == NULL)
		{
			g_autoptr(LrgWaveData) wave = lrg_song_priv_load_audio (path, 0, NULL);
			gdouble                s = wave != NULL
			                           ? (gdouble) lrg_wave_data_get_frame_count (wave)
			                             / (gdouble) MAX (lrg_wave_data_get_sample_rate (wave), 1u)
			                           : 0.0;

			cached = g_memdup2 (&s, sizeof s);
			g_hash_table_insert (self->file_seconds, g_strdup (path), cached);
		}
		seconds = MAX (*cached - clip->offset, 0.0);
		start_s = lrg_song_beat_to_seconds (self, clip->start);
		return MAX (lrg_song_seconds_to_beat (self, start_s + seconds) - clip->start, 0.0);
	}
}

/* ==========================================================================
 * Time
 * ========================================================================== */

static gint
tempo_compare (gconstpointer a,
               gconstpointer b)
{
	gdouble x = ((const LrgSongTempo *) a)->beat;
	gdouble y = ((const LrgSongTempo *) b)->beat;

	return x < y ? -1 : x > y ? 1 : 0;
}

gboolean
lrg_song_add_tempo_change (LrgSong  *self,
                           gdouble   beat,
                           gdouble   bpm,
                           gboolean  ramp)
{
	LrgSongTempo t;
	guint        i;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	if (!isfinite (beat) || beat <= 0.0 || beat > LRG_SONG_MAX_BEATS || !isfinite (bpm) || bpm < 20.0
	    || bpm > 400.0)
		return FALSE;
	for (i = 0; i < self->tempo->len; i++)
		if (g_array_index (self->tempo, LrgSongTempo, i).beat == beat)
		{
			g_array_index (self->tempo, LrgSongTempo, i).bpm = bpm;
			g_array_index (self->tempo, LrgSongTempo, i).ramp = ramp;
			changed (self);
			return TRUE;
		}
	t.beat = beat;
	t.bpm = bpm;
	t.ramp = ramp ? TRUE : FALSE;
	g_array_append_val (self->tempo, t);
	g_array_sort (self->tempo, tempo_compare);
	changed (self);
	return TRUE;
}

void
lrg_song_clear_tempo_changes (LrgSong *self)
{
	g_return_if_fail (LRG_IS_SONG (self));
	g_array_set_size (self->tempo, 0);
	changed (self);
}

guint
lrg_song_get_n_tempo_changes (LrgSong *self)
{
	g_return_val_if_fail (LRG_IS_SONG (self), 0);
	return self->tempo->len;
}

gboolean
lrg_song_get_tempo_change (LrgSong  *self,
                           guint     index,
                           gdouble  *out_beat,
                           gdouble  *out_bpm,
                           gboolean *out_ramp)
{
	const LrgSongTempo *t;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	if (index >= self->tempo->len)
		return FALSE;
	t = &g_array_index (self->tempo, LrgSongTempo, index);
	if (out_beat != NULL)
		*out_beat = t->beat;
	if (out_bpm != NULL)
		*out_bpm = t->bpm;
	if (out_ramp != NULL)
		*out_ramp = t->ramp;
	return TRUE;
}

/*
 * The tempo map as segments: segment i runs from point i to point i + 1
 * (the last one forever).  Point 0 is (0, bpm).  A segment is a ramp when
 * the point that ends it is marked ramp: the tempo is then linear in beats
 * from the start's bpm to the end's.
 */
typedef struct
{
	gdouble b0, bpm0, b1, bpm1;
	gboolean ramp;
	gdouble s0;            /* seconds at b0 */
} Segment;

static guint
segments (LrgSong *self,
          Segment *out,
          guint    max)
{
	guint   n = 0;
	gdouble b = 0.0, bpm = self->bpm, s = 0.0;
	guint   i;

	for (i = 0; i <= self->tempo->len && n < max; i++)
	{
		Segment seg;

		seg.b0 = b;
		seg.bpm0 = bpm;
		seg.s0 = s;
		if (i < self->tempo->len)
		{
			const LrgSongTempo *t = &g_array_index (self->tempo, LrgSongTempo, i);

			seg.b1 = t->beat;
			seg.ramp = t->ramp;
			seg.bpm1 = t->ramp ? t->bpm : bpm;
			/* seconds over the segment */
			if (seg.ramp && seg.bpm1 != seg.bpm0)
				s += 60.0 * (seg.b1 - seg.b0) / (seg.bpm1 - seg.bpm0) * log (seg.bpm1 / seg.bpm0);
			else
				s += 60.0 * (seg.b1 - seg.b0) / seg.bpm0;
			b = t->beat;
			bpm = t->bpm;
		}
		else
		{
			seg.b1 = G_MAXDOUBLE;
			seg.bpm1 = bpm;
			seg.ramp = FALSE;
		}
		out[n++] = seg;
	}
	return n;
}

#define MAX_SEGMENTS (1024)

gdouble
lrg_song_get_bpm_at (LrgSong *self,
                     gdouble  beat)
{
	Segment segs[MAX_SEGMENTS];
	guint   n, i;

	g_return_val_if_fail (LRG_IS_SONG (self), DEFAULT_BPM);
	n = segments (self, segs, MAX_SEGMENTS);
	for (i = 0; i < n; i++)
		if (beat < segs[i].b1 || i == n - 1)
		{
			if (segs[i].ramp)
				return segs[i].bpm0 + (segs[i].bpm1 - segs[i].bpm0) * (beat - segs[i].b0) / (segs[i].b1 - segs[i].b0);
			return segs[i].bpm0;
		}
	return self->bpm;
}

gdouble
lrg_song_beat_to_seconds (LrgSong *self,
                          gdouble  beat)
{
	Segment segs[MAX_SEGMENTS];
	guint   n, i;

	g_return_val_if_fail (LRG_IS_SONG (self), 0.0);
	if (!(beat > 0.0))
		return 0.0;
	n = segments (self, segs, MAX_SEGMENTS);
	for (i = 0; i < n; i++)
	{
		const Segment *g = &segs[i];

		if (beat <= g->b1 || i == n - 1)
		{
			gdouble db = beat - g->b0;

			if (g->ramp && g->bpm1 != g->bpm0)
			{
				gdouble slope = (g->bpm1 - g->bpm0) / (g->b1 - g->b0);

				return g->s0 + 60.0 / slope * log ((g->bpm0 + slope * db) / g->bpm0);
			}
			return g->s0 + 60.0 * db / g->bpm0;
		}
	}
	return 0.0;
}

gdouble
lrg_song_seconds_to_beat (LrgSong *self,
                          gdouble  seconds)
{
	Segment segs[MAX_SEGMENTS];
	guint   n, i;

	g_return_val_if_fail (LRG_IS_SONG (self), 0.0);
	if (!(seconds > 0.0))
		return 0.0;
	n = segments (self, segs, MAX_SEGMENTS);
	for (i = 0; i < n; i++)
	{
		const Segment *g = &segs[i];
		gdouble        s1 = i + 1 < n ? segs[i + 1].s0 : G_MAXDOUBLE;

		if (seconds <= s1 || i == n - 1)
		{
			gdouble ds = seconds - g->s0;

			if (g->ramp && g->bpm1 != g->bpm0)
			{
				gdouble slope = (g->bpm1 - g->bpm0) / (g->b1 - g->b0);

				return g->b0 + g->bpm0 * (exp (ds * slope / 60.0) - 1.0) / slope;
			}
			return g->b0 + ds * g->bpm0 / 60.0;
		}
	}
	return 0.0;
}

gdouble
lrg_song_get_length_beats (LrgSong *self)
{
	gdouble end = 0.0;
	guint   i, j;

	g_return_val_if_fail (LRG_IS_SONG (self), 0.0);
	if (self->length > 0.0)
		return self->length;
	for (i = 0; i < self->tracks->len; i++)
	{
		LrgSongTrack *t = g_ptr_array_index (self->tracks, i);

		for (j = 0; j < t->clips->len; j++)
		{
			LrgSongClip *c = &g_array_index (t->clips, LrgSongClip, j);

			end = MAX (end, c->start + lrg_song_priv_clip_length (self, t, c));
		}
	}
	return end;
}

gdouble
lrg_song_get_duration (LrgSong *self)
{
	g_return_val_if_fail (LRG_IS_SONG (self), 0.0);
	return lrg_song_beat_to_seconds (self, lrg_song_get_length_beats (self));
}

static gint
marker_compare (gconstpointer a,
                gconstpointer b)
{
	gdouble x = ((const LrgSongMarker *) a)->beat;
	gdouble y = ((const LrgSongMarker *) b)->beat;

	return x < y ? -1 : x > y ? 1 : 0;
}

gboolean
lrg_song_add_marker (LrgSong     *self,
                     gdouble      beat,
                     const gchar *name)
{
	LrgSongMarker m;
	guint         i;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	if (name == NULL || *name == '\0' || !isfinite (beat) || beat < 0.0 || beat > LRG_SONG_MAX_BEATS)
		return FALSE;
	for (i = 0; i < self->markers->len; i++)
		if (g_str_equal (g_array_index (self->markers, LrgSongMarker, i).name, name))
		{
			g_array_index (self->markers, LrgSongMarker, i).beat = beat;
			g_array_sort (self->markers, marker_compare);
			changed (self);
			return TRUE;
		}
	m.beat = beat;
	m.name = g_strdup (name);
	g_array_append_val (self->markers, m);
	g_array_sort (self->markers, marker_compare);
	changed (self);
	return TRUE;
}

gboolean
lrg_song_remove_marker (LrgSong     *self,
                        const gchar *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	for (i = 0; name != NULL && i < self->markers->len; i++)
		if (g_str_equal (g_array_index (self->markers, LrgSongMarker, i).name, name))
		{
			g_array_remove_index (self->markers, i);
			changed (self);
			return TRUE;
		}
	return FALSE;
}

guint
lrg_song_get_n_markers (LrgSong *self)
{
	g_return_val_if_fail (LRG_IS_SONG (self), 0);
	return self->markers->len;
}

gboolean
lrg_song_get_marker (LrgSong      *self,
                     guint         index,
                     gdouble      *out_beat,
                     const gchar **out_name)
{
	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	if (index >= self->markers->len)
		return FALSE;
	if (out_beat != NULL)
		*out_beat = g_array_index (self->markers, LrgSongMarker, index).beat;
	if (out_name != NULL)
		*out_name = g_array_index (self->markers, LrgSongMarker, index).name;
	return TRUE;
}

gdouble
lrg_song_find_marker (LrgSong     *self,
                      const gchar *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_SONG (self), -1.0);
	for (i = 0; name != NULL && i < self->markers->len; i++)
		if (g_str_equal (g_array_index (self->markers, LrgSongMarker, i).name, name))
			return g_array_index (self->markers, LrgSongMarker, i).beat;
	return -1.0;
}

/* ==========================================================================
 * Instruments
 * ========================================================================== */

/* the GType of an instrument type name, or G_TYPE_INVALID */
static GType
instrument_gtype (const gchar *type)
{
	if (g_strcmp0 (type, "synth") == 0)
		return LRG_TYPE_SYNTH;
	if (g_strcmp0 (type, "drums") == 0)
		return LRG_TYPE_DRUM_MACHINE;
	if (g_strcmp0 (type, "sampler") == 0)
		return LRG_TYPE_SAMPLER;
	if (g_strcmp0 (type, "soundfont") == 0)
		return LRG_TYPE_SOUND_FONT_INSTRUMENT;
	if (g_strcmp0 (type, "foley") == 0)
		return LRG_TYPE_FOLEY_INSTRUMENT;
	return G_TYPE_INVALID;
}

/* sampler-only settings that are not instrument properties */
static gboolean
sampler_special (const gchar *param)
{
	return g_strcmp0 (param, "root-key") == 0 || g_strcmp0 (param, "loop-start") == 0
	       || g_strcmp0 (param, "loop-end") == 0;
}

static gboolean
type_has_param (const gchar *type,
                const gchar *param)
{
	GType                 t = instrument_gtype (type);
	g_autoptr(GTypeClass) klass = NULL;

	if (t == G_TYPE_INVALID || param == NULL)
		return FALSE;
	if (g_str_equal (type, "sampler") && sampler_special (param))
		return TRUE;
	klass = g_type_class_ref (t);
	return g_object_class_find_property (G_OBJECT_CLASS (klass), param) != NULL;
}

gboolean
lrg_song_set_instrument (LrgSong     *self,
                         const gchar *id,
                         const gchar *type,
                         const gchar *source)
{
	LrgSongInstrument *in;
	guint              i;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	if (!valid_id (id) || instrument_gtype (type) == G_TYPE_INVALID)
		return FALSE;
	in = instrument_new (id, type, source);
	for (i = 0; i < self->instruments->len; i++)
		if (g_str_equal (((LrgSongInstrument *) g_ptr_array_index (self->instruments, i))->id, id))
		{
			g_ptr_array_remove_index (self->instruments, i);
			g_ptr_array_insert (self->instruments, (gint) i, in);
			changed (self);
			return TRUE;
		}
	g_ptr_array_add (self->instruments, in);
	changed (self);
	return TRUE;
}

gboolean
lrg_song_remove_instrument (LrgSong     *self,
                            const gchar *id)
{
	LrgSongInstrument *in;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	in = lrg_song_priv_instrument (self, id);
	if (in == NULL)
		return FALSE;
	g_ptr_array_remove (self->instruments, in);
	changed (self);
	return TRUE;
}

/* the ids of @array (elements whose first member is a gchar *id), sorted */
static gchar **
sorted_ids (GPtrArray *array)
{
	GPtrArray *names = g_ptr_array_new ();
	guint      i;

	for (i = 0; i < array->len; i++)
		g_ptr_array_add (names, g_strdup (*(gchar **) g_ptr_array_index (array, i)));
	g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (names, FALSE);
}

gchar **
lrg_song_list_instruments (LrgSong *self)
{
	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	return sorted_ids (self->instruments);
}

const gchar *
lrg_song_get_instrument_type (LrgSong     *self,
                              const gchar *id)
{
	LrgSongInstrument *in;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	in = lrg_song_priv_instrument (self, id);
	return in != NULL ? in->type : NULL;
}

const gchar *
lrg_song_get_instrument_source (LrgSong     *self,
                                const gchar *id)
{
	LrgSongInstrument *in;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	in = lrg_song_priv_instrument (self, id);
	return in != NULL ? in->source : NULL;
}

gboolean
lrg_song_set_instrument_param (LrgSong     *self,
                               const gchar *id,
                               const gchar *param,
                               gdouble      value)
{
	LrgSongInstrument *in;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	in = lrg_song_priv_instrument (self, id);
	if (in == NULL || !isfinite (value) || !type_has_param (in->type, param))
		return FALSE;
	g_hash_table_insert (in->params, g_strdup (param), g_memdup2 (&value, sizeof value));
	changed (self);
	return TRUE;
}

gboolean
lrg_song_unset_instrument_param (LrgSong     *self,
                                 const gchar *id,
                                 const gchar *param)
{
	LrgSongInstrument *in;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	in = lrg_song_priv_instrument (self, id);
	if (in == NULL || param == NULL || !g_hash_table_remove (in->params, param))
		return FALSE;
	changed (self);
	return TRUE;
}

/* qsort compares pointers to elements; strings need one more hop */
static int
strv_cmp (const void *a,
          const void *b)
{
	return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

static gchar **
sorted_keys (GHashTable *table)
{
	g_autofree gpointer *keys = NULL;
	guint                n = 0;
	gchar              **result;
	guint                i;

	keys = g_hash_table_get_keys_as_array (table, &n);
	result = g_new0 (gchar *, n + 1);
	for (i = 0; i < n; i++)
		result[i] = g_strdup (keys[i]);
	return result;   /* callers sort with strv_cmp () */
}

gchar **
lrg_song_list_instrument_params (LrgSong     *self,
                                 const gchar *id)
{
	LrgSongInstrument *in;
	gchar            **keys;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	in = lrg_song_priv_instrument (self, id);
	if (in == NULL)
		return g_new0 (gchar *, 1);
	keys = sorted_keys (in->params);
	qsort (keys, g_strv_length (keys), sizeof (gchar *), strv_cmp);
	return keys;
}

gdouble
lrg_song_get_instrument_param (LrgSong     *self,
                               const gchar *id,
                               const gchar *param)
{
	LrgSongInstrument *in;
	gdouble           *v;

	g_return_val_if_fail (LRG_IS_SONG (self), NAN);
	in = lrg_song_priv_instrument (self, id);
	if (in == NULL || param == NULL)
		return NAN;
	v = g_hash_table_lookup (in->params, param);
	return v != NULL ? *v : NAN;
}

LrgInstrument *
lrg_song_create_instrument (LrgSong      *self,
                            const gchar  *id,
                            GError      **error)
{
	g_autoptr(LrgInstrument) inst = NULL;
	LrgSongInstrument       *in;
	g_autofree gchar        *path = NULL;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);

	in = lrg_song_priv_instrument (self, id);
	if (in == NULL)
	{
		g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_NOT_FOUND, "no instrument '%s'", id != NULL ? id : "");
		return NULL;
	}
	path = lrg_song_priv_resolve (self, in->source);
	if (g_str_equal (in->type, "synth"))
	{
		inst = LRG_INSTRUMENT (lrg_synth_new_preset (in->source != NULL ? in->source : "init"));
		if (inst == NULL)
		{
			g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_NOT_FOUND, "instrument '%s': no synth preset '%s'",
			             in->id, in->source);
			return NULL;
		}
	}
	else if (g_str_equal (in->type, "drums"))
	{
		LrgDrumKit kit = LRG_DRUM_KIT_808;

		if (g_strcmp0 (in->source, "909") == 0)
			kit = LRG_DRUM_KIT_909;
		else if (g_strcmp0 (in->source, "linn") == 0)
			kit = LRG_DRUM_KIT_LINN;
		else if (in->source != NULL && g_strcmp0 (in->source, "808") != 0)
		{
			g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_NOT_FOUND,
			             "instrument '%s': no drum kit '%s' (808, 909, linn)", in->id, in->source);
			return NULL;
		}
		inst = LRG_INSTRUMENT (lrg_drum_machine_new (kit));
	}
	else if (g_str_equal (in->type, "sampler"))
	{
		g_autoptr(LrgWaveData) wave = NULL;
		gdouble               *root = g_hash_table_lookup (in->params, "root-key");
		gdouble               *ls = g_hash_table_lookup (in->params, "loop-start");
		gdouble               *le = g_hash_table_lookup (in->params, "loop-end");
		LrgSampler            *sampler;

		if (path == NULL)
		{
			g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_NOT_FOUND, "instrument '%s': no sample file", in->id);
			return NULL;
		}
		wave = lrg_song_priv_load_audio (path, 0, error);
		if (wave == NULL)
			return NULL;
		sampler = lrg_sampler_new_from_wave (wave, root != NULL ? (gint) CLAMP (round (*root), 0, 127) : 60);
		if (sampler == NULL)
		{
			g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_RENDER, "instrument '%s': %s is empty", in->id, path);
			return NULL;
		}
		if (ls != NULL && le != NULL)
			lrg_sampler_set_zone_loop (sampler, 0, (gint64) *ls, (gint64) *le);
		inst = LRG_INSTRUMENT (sampler);
	}
	else if (g_str_equal (in->type, "soundfont"))
	{
		if (path == NULL)
		{
			g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_NOT_FOUND, "instrument '%s': no SoundFont file", in->id);
			return NULL;
		}
		inst = LRG_INSTRUMENT (lrg_sound_font_instrument_new_from_file (path, error));
		if (inst == NULL)
			return NULL;
	}
	else
	{
		g_autoptr(LrgFoleyPatch) patch = NULL;

		if (path == NULL)
		{
			g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_NOT_FOUND, "instrument '%s': no .rfoley file", in->id);
			return NULL;
		}
		patch = lrg_foley_patch_new_from_file (path, error);
		if (patch == NULL)
			return NULL;
		inst = LRG_INSTRUMENT (lrg_foley_instrument_new (patch));
	}

	/* overrides after the preset or kit, in a stable (sorted) order */
	{
		g_auto(GStrv) names = sorted_keys (in->params);
		guint         i;

		qsort (names, g_strv_length (names), sizeof (gchar *), strv_cmp);
		for (i = 0; names[i] != NULL; i++)
		{
			gdouble *value = g_hash_table_lookup (in->params, names[i]);

			if (g_str_equal (in->type, "sampler") && sampler_special (names[i]))
				continue;
			lrg_instrument_set_param (inst, names[i], *value);
		}
	}
	return g_steal_pointer (&inst);
}

/* ==========================================================================
 * Patterns
 * ========================================================================== */

gboolean
lrg_song_add_pattern (LrgSong     *self,
                      const gchar *id,
                      gdouble      length_beats)
{
	LrgSongPattern *p;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	if (!valid_id (id) || !isfinite (length_beats) || length_beats < 0.25 || length_beats > 1024.0)
		return FALSE;
	p = lrg_song_priv_pattern (self, id);
	if (p != NULL)
	{
		p->length = length_beats;
		g_array_set_size (p->notes, 0);
		g_ptr_array_set_size (p->lanes, 0);
	}
	else
		g_ptr_array_add (self->patterns, pattern_new (id, length_beats));
	changed (self);
	return TRUE;
}

gboolean
lrg_song_remove_pattern (LrgSong     *self,
                         const gchar *id)
{
	LrgSongPattern *p;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	p = lrg_song_priv_pattern (self, id);
	if (p == NULL)
		return FALSE;
	g_ptr_array_remove (self->patterns, p);
	changed (self);
	return TRUE;
}

gchar **
lrg_song_list_patterns (LrgSong *self)
{
	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	return sorted_ids (self->patterns);
}

gdouble
lrg_song_get_pattern_length (LrgSong     *self,
                             const gchar *id)
{
	LrgSongPattern *p;

	g_return_val_if_fail (LRG_IS_SONG (self), 0.0);
	p = lrg_song_priv_pattern (self, id);
	return p != NULL ? p->length : 0.0;
}

gboolean
lrg_song_set_pattern_length (LrgSong     *self,
                             const gchar *id,
                             gdouble      length_beats)
{
	LrgSongPattern *p;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	p = lrg_song_priv_pattern (self, id);
	if (p == NULL || !isfinite (length_beats))
		return FALSE;
	p->length = CLAMP (length_beats, 0.25, 1024.0);
	changed (self);
	return TRUE;
}

gint
lrg_song_pattern_add_note (LrgSong     *self,
                           const gchar *id,
                           gdouble      beat,
                           gint         key,
                           gdouble      length,
                           gdouble      velocity)
{
	LrgSongPattern *p;
	LrgSongNote     n;
	guint           i;

	g_return_val_if_fail (LRG_IS_SONG (self), -1);
	p = lrg_song_priv_pattern (self, id);
	if (p == NULL || !isfinite (beat) || beat < 0.0 || beat > 1024.0 || key < 0 || key > 127 || !isfinite (length)
	    || length <= 0.0 || !isfinite (velocity))
		return -1;
	n.beat = beat;
	n.key = key;
	n.length = MIN (length, 1024.0);
	n.velocity = CLAMP (velocity, 0.0, 1.0);
	for (i = 0; i < p->notes->len; i++)
		if (note_compare (&n, &g_array_index (p->notes, LrgSongNote, i)) < 0)
			break;
	g_array_insert_val (p->notes, i, n);
	changed (self);
	return (gint) i;
}

gboolean
lrg_song_pattern_remove_note (LrgSong     *self,
                              const gchar *id,
                              guint        index)
{
	LrgSongPattern *p;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	p = lrg_song_priv_pattern (self, id);
	if (p == NULL || index >= p->notes->len)
		return FALSE;
	g_array_remove_index (p->notes, index);
	changed (self);
	return TRUE;
}

gboolean
lrg_song_pattern_clear_notes (LrgSong     *self,
                              const gchar *id)
{
	LrgSongPattern *p;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	p = lrg_song_priv_pattern (self, id);
	if (p == NULL)
		return FALSE;
	g_array_set_size (p->notes, 0);
	changed (self);
	return TRUE;
}

guint
lrg_song_pattern_get_n_notes (LrgSong     *self,
                              const gchar *id)
{
	LrgSongPattern *p;

	g_return_val_if_fail (LRG_IS_SONG (self), 0);
	p = lrg_song_priv_pattern (self, id);
	return p != NULL ? p->notes->len : 0;
}

gboolean
lrg_song_pattern_get_note (LrgSong     *self,
                           const gchar *id,
                           guint        index,
                           gdouble     *out_beat,
                           gint        *out_key,
                           gdouble     *out_length,
                           gdouble     *out_velocity)
{
	LrgSongPattern    *p;
	const LrgSongNote *n;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	p = lrg_song_priv_pattern (self, id);
	if (p == NULL || index >= p->notes->len)
		return FALSE;
	n = &g_array_index (p->notes, LrgSongNote, index);
	if (out_beat != NULL)
		*out_beat = n->beat;
	if (out_key != NULL)
		*out_key = n->key;
	if (out_length != NULL)
		*out_length = n->length;
	if (out_velocity != NULL)
		*out_velocity = n->velocity;
	return TRUE;
}

gboolean
lrg_song_pattern_set_steps (LrgSong     *self,
                            const gchar *id,
                            guint        steps)
{
	LrgSongPattern *p;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	p = lrg_song_priv_pattern (self, id);
	if (p == NULL || steps == 0 || steps > 256)
		return FALSE;
	p->steps = steps;
	changed (self);
	return TRUE;
}

guint
lrg_song_pattern_get_steps (LrgSong     *self,
                            const gchar *id)
{
	LrgSongPattern *p;

	g_return_val_if_fail (LRG_IS_SONG (self), 0);
	p = lrg_song_priv_pattern (self, id);
	return p != NULL ? p->steps : 0;
}

static gboolean
valid_steps (const gchar *steps)
{
	const gchar *c;

	for (c = steps; *c != '\0'; c++)
		if (lrg_song_priv_step_velocity (*c) < 0.0 && *c != ' ' && *c != '|')
			return FALSE;
	return TRUE;
}

gboolean
lrg_song_pattern_set_lane (LrgSong     *self,
                           const gchar *id,
                           const gchar *lane,
                           const gchar *steps)
{
	LrgSongPattern *p;
	guint           i;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	p = lrg_song_priv_pattern (self, id);
	if (p == NULL || lane == NULL || lrg_song_priv_lane_key (lane) < 0)
		return FALSE;
	if (steps != NULL && !valid_steps (steps))
		return FALSE;
	for (i = 0; i < p->lanes->len; i++)
	{
		LrgSongLane *l = g_ptr_array_index (p->lanes, i);

		if (g_str_equal (l->name, lane))
		{
			if (steps == NULL)
				g_ptr_array_remove_index (p->lanes, i);
			else
			{
				g_free (l->steps);
				l->steps = g_strdup (steps);
			}
			changed (self);
			return TRUE;
		}
	}
	if (steps == NULL)
		return FALSE;
	{
		LrgSongLane *l = g_new0 (LrgSongLane, 1);

		l->name = g_strdup (lane);
		l->steps = g_strdup (steps);
		g_ptr_array_add (p->lanes, l);
	}
	changed (self);
	return TRUE;
}

const gchar *
lrg_song_pattern_get_lane (LrgSong     *self,
                           const gchar *id,
                           const gchar *lane)
{
	LrgSongPattern *p;
	guint           i;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	p = lrg_song_priv_pattern (self, id);
	for (i = 0; p != NULL && lane != NULL && i < p->lanes->len; i++)
		if (g_str_equal (((LrgSongLane *) g_ptr_array_index (p->lanes, i))->name, lane))
			return ((LrgSongLane *) g_ptr_array_index (p->lanes, i))->steps;
	return NULL;
}

gchar **
lrg_song_pattern_list_lanes (LrgSong     *self,
                             const gchar *id)
{
	LrgSongPattern *p;
	gchar         **names;
	guint           i;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	p = lrg_song_priv_pattern (self, id);
	if (p == NULL)
		return g_new0 (gchar *, 1);
	names = g_new0 (gchar *, p->lanes->len + 1);
	for (i = 0; i < p->lanes->len; i++)
		names[i] = g_strdup (((LrgSongLane *) g_ptr_array_index (p->lanes, i))->name);
	return names;
}

/* ==========================================================================
 * Tracks and clips
 * ========================================================================== */

gboolean
lrg_song_add_track (LrgSong          *self,
                    const gchar      *id,
                    LrgSongTrackKind  kind,
                    const gchar      *instrument)
{
	LrgSongTrack *t;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	if (!valid_id (id) || lrg_song_priv_track (self, id) != NULL || (guint) kind > LRG_SONG_TRACK_AUDIO)
		return FALSE;
	t = track_new (id, kind);
	t->instrument = g_strdup (instrument);
	g_ptr_array_add (self->tracks, t);
	changed (self);
	return TRUE;
}

gboolean
lrg_song_remove_track (LrgSong     *self,
                       const gchar *id)
{
	LrgSongTrack *t;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	t = lrg_song_priv_track (self, id);
	if (t == NULL)
		return FALSE;
	g_ptr_array_remove (self->tracks, t);
	changed (self);
	return TRUE;
}

gboolean
lrg_song_move_track (LrgSong     *self,
                     const gchar *id,
                     guint        index)
{
	LrgSongTrack *t;
	guint         from = 0;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);

	t = lrg_song_priv_track (self, id);
	if (t == NULL || index >= self->tracks->len || !g_ptr_array_find (self->tracks, t, &from))
		return FALSE;
	if (from != index)
	{
		g_ptr_array_steal_index (self->tracks, from);
		g_ptr_array_insert (self->tracks, (gint) index, t);
		changed (self);
	}
	return TRUE;
}

gchar **
lrg_song_list_tracks (LrgSong *self)
{
	gchar **ids;
	guint   i;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	ids = g_new0 (gchar *, self->tracks->len + 1);
	for (i = 0; i < self->tracks->len; i++)
		ids[i] = g_strdup (((LrgSongTrack *) g_ptr_array_index (self->tracks, i))->id);
	return ids;
}

LrgSongTrackKind
lrg_song_get_track_kind (LrgSong     *self,
                         const gchar *id)
{
	LrgSongTrack *t;

	g_return_val_if_fail (LRG_IS_SONG (self), LRG_SONG_TRACK_INSTRUMENT);
	t = lrg_song_priv_track (self, id);
	return t != NULL ? t->kind : LRG_SONG_TRACK_INSTRUMENT;
}

gboolean
lrg_song_set_track (LrgSong     *self,
                    const gchar *id,
                    const gchar *field,
                    const gchar *text,
                    gdouble      value)
{
	LrgSongTrack *t;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	t = lrg_song_priv_track (self, id);
	if (t == NULL || field == NULL)
		return FALSE;
	if (g_str_equal (field, "name"))
	{
		g_free (t->name);
		t->name = g_strdup (text);
	}
	else if (g_str_equal (field, "instrument"))
	{
		g_free (t->instrument);
		t->instrument = g_strdup (text);
	}
	else if (g_str_equal (field, "bus"))
	{
		if (text == NULL || *text == '\0')
			return FALSE;
		g_free (t->bus);
		t->bus = g_strdup (text);
	}
	else if (!isfinite (value))
		return FALSE;
	else if (g_str_equal (field, "volume"))
		t->volume = CLAMP (value, -144.0, 12.0);
	else if (g_str_equal (field, "pan"))
		t->pan = CLAMP (value, -1.0, 1.0);
	else if (g_str_equal (field, "mute"))
		t->mute = value != 0.0;
	else if (g_str_equal (field, "solo"))
		t->solo = value != 0.0;
	else
		return FALSE;
	changed (self);
	return TRUE;
}

const gchar *
lrg_song_get_track_text (LrgSong     *self,
                         const gchar *id,
                         const gchar *field)
{
	LrgSongTrack *t;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	t = lrg_song_priv_track (self, id);
	if (t == NULL || field == NULL)
		return NULL;
	if (g_str_equal (field, "name"))
		return t->name;
	if (g_str_equal (field, "instrument"))
		return t->instrument;
	if (g_str_equal (field, "bus"))
		return t->bus;
	return NULL;
}

gdouble
lrg_song_get_track_value (LrgSong     *self,
                          const gchar *id,
                          const gchar *field)
{
	LrgSongTrack *t;

	g_return_val_if_fail (LRG_IS_SONG (self), NAN);
	t = lrg_song_priv_track (self, id);
	if (t == NULL || field == NULL)
		return NAN;
	if (g_str_equal (field, "volume"))
		return t->volume;
	if (g_str_equal (field, "pan"))
		return t->pan;
	if (g_str_equal (field, "mute"))
		return t->mute ? 1.0 : 0.0;
	if (g_str_equal (field, "solo"))
		return t->solo ? 1.0 : 0.0;
	return NAN;
}

static gint
clip_compare (gconstpointer a,
              gconstpointer b)
{
	gdouble x = ((const LrgSongClip *) a)->start;
	gdouble y = ((const LrgSongClip *) b)->start;

	return x < y ? -1 : x > y ? 1 : 0;
}

/* stable re-sort: equal starts keep their order */
static void
sort_clips (LrgSongTrack *t)
{
	guint i, j;

	for (i = 1; i < t->clips->len; i++)
	{
		LrgSongClip c = g_array_index (t->clips, LrgSongClip, i);

		for (j = i; j > 0 && clip_compare (&g_array_index (t->clips, LrgSongClip, j - 1), &c) > 0; j--)
			g_array_index (t->clips, LrgSongClip, j) = g_array_index (t->clips, LrgSongClip, j - 1);
		g_array_index (t->clips, LrgSongClip, j) = c;
	}
}

gint
lrg_song_add_clip (LrgSong     *self,
                   const gchar *track,
                   const gchar *source,
                   gdouble      start,
                   gdouble      length)
{
	LrgSongTrack *t;
	LrgSongClip   c;
	guint         i;

	g_return_val_if_fail (LRG_IS_SONG (self), -1);
	t = lrg_song_priv_track (self, track);
	if (t == NULL || source == NULL || *source == '\0' || !isfinite (start) || start < 0.0
	    || start > LRG_SONG_MAX_BEATS || !isfinite (length) || length < 0.0 || length > LRG_SONG_MAX_BEATS)
		return -1;
	memset (&c, 0, sizeof c);
	c.source = g_strdup (source);
	c.start = start;
	c.length = length;
	for (i = 0; i < t->clips->len; i++)
		if (start < g_array_index (t->clips, LrgSongClip, i).start)
			break;
	g_array_insert_val (t->clips, i, c);
	changed (self);
	return (gint) i;
}

gboolean
lrg_song_remove_clip (LrgSong     *self,
                      const gchar *track,
                      guint        index)
{
	LrgSongTrack *t;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	t = lrg_song_priv_track (self, track);
	if (t == NULL || index >= t->clips->len)
		return FALSE;
	g_array_remove_index (t->clips, index);
	changed (self);
	return TRUE;
}

guint
lrg_song_get_n_clips (LrgSong     *self,
                      const gchar *track)
{
	LrgSongTrack *t;

	g_return_val_if_fail (LRG_IS_SONG (self), 0);
	t = lrg_song_priv_track (self, track);
	return t != NULL ? t->clips->len : 0;
}

gboolean
lrg_song_set_clip (LrgSong     *self,
                   const gchar *track,
                   guint        index,
                   const gchar *field,
                   gdouble      value)
{
	LrgSongTrack *t;
	LrgSongClip  *c;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	t = lrg_song_priv_track (self, track);
	if (t == NULL || index >= t->clips->len || field == NULL || !isfinite (value))
		return FALSE;
	c = &g_array_index (t->clips, LrgSongClip, index);
	if (g_str_equal (field, "start"))
	{
		c->start = CLAMP (value, 0.0, LRG_SONG_MAX_BEATS);
		sort_clips (t);
	}
	else if (g_str_equal (field, "length"))
		c->length = CLAMP (value, 0.0, LRG_SONG_MAX_BEATS);
	else if (g_str_equal (field, "offset"))
		c->offset = CLAMP (value, 0.0, LRG_SONG_MAX_BEATS);
	else if (g_str_equal (field, "transpose"))
		c->transpose = CLAMP (round (value), -48.0, 48.0);
	else if (g_str_equal (field, "gain"))
		c->gain = CLAMP (value, -144.0, 24.0);
	else if (g_str_equal (field, "fade-in") || g_str_equal (field, "fade_in"))
		c->fade_in = CLAMP (value, 0.0, 600.0);
	else if (g_str_equal (field, "fade-out") || g_str_equal (field, "fade_out"))
		c->fade_out = CLAMP (value, 0.0, 600.0);
	else if (g_str_equal (field, "mute"))
		c->mute = value != 0.0;
	else
		return FALSE;
	changed (self);
	return TRUE;
}

gboolean
lrg_song_set_clip_source (LrgSong     *self,
                          const gchar *track,
                          guint        index,
                          const gchar *source)
{
	LrgSongTrack *t;
	LrgSongClip  *c;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	t = lrg_song_priv_track (self, track);
	if (t == NULL || index >= t->clips->len || source == NULL || *source == '\0')
		return FALSE;
	c = &g_array_index (t->clips, LrgSongClip, index);
	g_free (c->source);
	c->source = g_strdup (source);
	changed (self);
	return TRUE;
}

gboolean
lrg_song_get_clip (LrgSong      *self,
                   const gchar  *track,
                   guint         index,
                   const gchar **out_source,
                   gdouble      *out_start,
                   gdouble      *out_length)
{
	LrgSongTrack *t;
	LrgSongClip  *c;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	t = lrg_song_priv_track (self, track);
	if (t == NULL || index >= t->clips->len)
		return FALSE;
	c = &g_array_index (t->clips, LrgSongClip, index);
	if (out_source != NULL)
		*out_source = c->source;
	if (out_start != NULL)
		*out_start = c->start;
	if (out_length != NULL)
		*out_length = lrg_song_priv_clip_length (self, t, c);
	return TRUE;
}

gdouble
lrg_song_get_clip_value (LrgSong     *self,
                         const gchar *track,
                         guint        index,
                         const gchar *field)
{
	LrgSongTrack *t;
	LrgSongClip  *c;

	g_return_val_if_fail (LRG_IS_SONG (self), NAN);
	t = lrg_song_priv_track (self, track);
	if (t == NULL || index >= t->clips->len || field == NULL)
		return NAN;
	c = &g_array_index (t->clips, LrgSongClip, index);
	if (g_str_equal (field, "start"))
		return c->start;
	if (g_str_equal (field, "length"))
		return c->length;
	if (g_str_equal (field, "offset"))
		return c->offset;
	if (g_str_equal (field, "transpose"))
		return c->transpose;
	if (g_str_equal (field, "gain"))
		return c->gain;
	if (g_str_equal (field, "fade-in") || g_str_equal (field, "fade_in"))
		return c->fade_in;
	if (g_str_equal (field, "fade-out") || g_str_equal (field, "fade_out"))
		return c->fade_out;
	if (g_str_equal (field, "mute"))
		return c->mute ? 1.0 : 0.0;
	return NAN;
}

gint
lrg_song_find_clip (LrgSong     *self,
                    const gchar *track,
                    gdouble      beat)
{
	LrgSongTrack *t;
	gint          found = -1;
	guint         i;

	g_return_val_if_fail (LRG_IS_SONG (self), -1);
	t = lrg_song_priv_track (self, track);
	for (i = 0; t != NULL && i < t->clips->len; i++)
	{
		LrgSongClip *c = &g_array_index (t->clips, LrgSongClip, i);

		if (beat >= c->start && beat < c->start + lrg_song_priv_clip_length (self, t, c))
			found = (gint) i;
	}
	return found;
}

/* ==========================================================================
 * Automation
 * ========================================================================== */

static gboolean
valid_target (const gchar *target,
              gboolean     on_track)
{
	if (target == NULL)
		return FALSE;
	if (on_track)
		return g_str_equal (target, "volume") || g_str_equal (target, "pan")
		       || (g_str_has_prefix (target, "param.") && target[6] != '\0');
	if (g_str_has_prefix (target, "bus."))
		return g_str_has_suffix (target, ".volume") && strlen (target) > strlen ("bus..volume");
	if (g_str_has_prefix (target, "effect."))
	{
		g_auto(GStrv) parts = g_strsplit (target, ".", 4);

		return g_strv_length (parts) == 4 && *parts[1] != '\0' && *parts[2] != '\0' && *parts[3] != '\0';
	}
	return FALSE;
}

static GPtrArray *
automation_list (LrgSong     *self,
                 const gchar *track,
                 gboolean    *ok)
{
	LrgSongTrack *t;

	*ok = TRUE;
	if (track == NULL)
		return self->automation;
	t = lrg_song_priv_track (self, track);
	if (t == NULL)
	{
		*ok = FALSE;
		return NULL;
	}
	return t->automation;
}

static LrgSongAutomation *
find_lane (GPtrArray   *list,
           const gchar *target)
{
	guint i;

	for (i = 0; list != NULL && target != NULL && i < list->len; i++)
		if (g_str_equal (((LrgSongAutomation *) g_ptr_array_index (list, i))->target, target))
			return g_ptr_array_index (list, i);
	return NULL;
}

gboolean
lrg_song_set_automation (LrgSong       *self,
                         const gchar   *track,
                         const gchar   *target,
                         const gdouble *points,
                         gsize          n_values,
                         gboolean       step)
{
	GPtrArray         *list;
	LrgSongAutomation *lane;
	gboolean           ok;
	gsize              i;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	list = automation_list (self, track, &ok);
	if (!ok || !valid_target (target, track != NULL) || n_values % 2 != 0 || (n_values > 0 && points == NULL))
		return FALSE;
	for (i = 0; i < n_values; i++)
		if (!isfinite (points[i]) || (i % 2 == 0 && (points[i] < 0.0 || points[i] > LRG_SONG_MAX_BEATS)))
			return FALSE;
	lane = find_lane (list, target);
	if (n_values == 0)
	{
		if (lane != NULL)
			g_ptr_array_remove (list, lane);
		changed (self);
		return TRUE;
	}
	if (lane == NULL)
	{
		lane = automation_new (target);
		g_ptr_array_add (list, lane);
	}
	lane->step = step ? TRUE : FALSE;
	g_array_set_size (lane->points, 0);

	/* insertion by beat, stable */
	for (i = 0; i < n_values; i += 2)
	{
		guint j = lane->points->len;

		while (j >= 2 && g_array_index (lane->points, gdouble, j - 2) > points[i])
			j -= 2;
		g_array_insert_vals (lane->points, j, &points[i], 2);
	}
	changed (self);
	return TRUE;
}

gchar **
lrg_song_list_automation (LrgSong     *self,
                          const gchar *track)
{
	GPtrArray *list;
	gboolean   ok;
	gchar    **names;
	guint      i;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	list = automation_list (self, track, &ok);
	if (!ok)
		return g_new0 (gchar *, 1);
	names = g_new0 (gchar *, list->len + 1);
	for (i = 0; i < list->len; i++)
		names[i] = g_strdup (((LrgSongAutomation *) g_ptr_array_index (list, i))->target);
	return names;
}

/* the value of @lane at @beat */
static gdouble
lane_value (const LrgSongAutomation *lane,
            gdouble                  beat)
{
	const gdouble *p = (const gdouble *) lane->points->data;
	guint          n = lane->points->len / 2;
	guint          i;

	if (n == 0)
		return NAN;
	if (beat <= p[0])
		return p[1];
	for (i = 1; i < n; i++)
		if (beat < p[i * 2])
		{
			gdouble b0 = p[(i - 1) * 2], v0 = p[(i - 1) * 2 + 1];
			gdouble b1 = p[i * 2], v1 = p[i * 2 + 1];

			if (lane->step || b1 <= b0)
				return v0;
			return v0 + (v1 - v0) * (beat - b0) / (b1 - b0);
		}
	return p[(n - 1) * 2 + 1];
}

gdouble
lrg_song_get_automation_value (LrgSong     *self,
                               const gchar *track,
                               const gchar *target,
                               gdouble      beat)
{
	GPtrArray         *list;
	LrgSongAutomation *lane;
	gboolean           ok;

	g_return_val_if_fail (LRG_IS_SONG (self), NAN);
	list = automation_list (self, track, &ok);
	lane = ok ? find_lane (list, target) : NULL;
	return lane != NULL ? lane_value (lane, beat) : NAN;
}

gdouble *
lrg_song_get_automation_points (LrgSong     *self,
                                const gchar *track,
                                const gchar *target,
                                gsize       *n_values)
{
	GPtrArray         *list;
	LrgSongAutomation *lane;
	gboolean           ok;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	g_return_val_if_fail (n_values != NULL, NULL);
	*n_values = 0;
	list = automation_list (self, track, &ok);
	lane = ok ? find_lane (list, target) : NULL;
	if (lane == NULL)
		return NULL;
	*n_values = lane->points->len;
	return g_memdup2 (lane->points->data, lane->points->len * sizeof (gdouble));
}

/* ==========================================================================
 * Mixer
 * ========================================================================== */

LrgMixerAsset *
lrg_song_get_mixer (LrgSong *self)
{
	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	return self->mixer;
}

void
lrg_song_set_mixer (LrgSong       *self,
                    LrgMixerAsset *mixer)
{
	g_return_if_fail (LRG_IS_SONG (self));
	g_return_if_fail (LRG_IS_MIXER_ASSET (mixer));

	g_clear_object (&self->mixer);
	self->mixer = lrg_mixer_asset_copy (mixer);
	changed (self);
}

/* ==========================================================================
 * Validation
 * ========================================================================== */

gboolean
lrg_song_validate (LrgSong    *self,
                   GPtrArray **issues)
{
	g_autoptr(GPtrArray) list = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GError)    merr = NULL;
	gboolean             ok = TRUE;
	guint                i, j;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);

#define ISSUE(...) G_STMT_START { g_ptr_array_add (list, g_strdup_printf (__VA_ARGS__)); ok = FALSE; } G_STMT_END
#define WARN(...) G_STMT_START { g_autofree gchar *w_ = g_strdup_printf (__VA_ARGS__); \
	g_ptr_array_add (list, g_strconcat ("warning: ", w_, NULL)); } G_STMT_END

	if (!lrg_mixer_asset_validate (self->mixer, &merr))
		ISSUE ("mixer: %s", merr->message);

	for (i = 0; i < self->instruments->len; i++)
	{
		LrgSongInstrument *in = g_ptr_array_index (self->instruments, i);
		g_autoptr(GError)  ierr = NULL;
		g_autoptr(LrgInstrument) inst = lrg_song_create_instrument (self, in->id, &ierr);

		if (inst == NULL)
			ISSUE ("instruments.%s: %s", in->id, ierr->message);
	}
	for (i = 0; i < self->patterns->len; i++)
	{
		LrgSongPattern *p = g_ptr_array_index (self->patterns, i);

		for (j = 0; j < p->notes->len; j++)
			if (g_array_index (p->notes, LrgSongNote, j).beat >= p->length)
				WARN ("patterns.%s.notes[%u]: starts after the pattern's end", p->id, j);
		for (j = 0; j < p->lanes->len; j++)
		{
			LrgSongLane *l = g_ptr_array_index (p->lanes, j);

			if (lrg_song_priv_lane_key (l->name) < 0)
				ISSUE ("patterns.%s.lanes.%s: not a drum piece or note", p->id, l->name);
		}
	}
	for (i = 0; i < self->tracks->len; i++)
	{
		LrgSongTrack *t = g_ptr_array_index (self->tracks, i);
		gdouble       prev_end = -1.0;

		if (!lrg_mixer_asset_has_bus (self->mixer, t->bus))
			ISSUE ("tracks[%u].bus: no bus '%s' in the mixer", i, t->bus);
		if (t->kind == LRG_SONG_TRACK_INSTRUMENT && lrg_song_priv_instrument (self, t->instrument) == NULL)
			ISSUE ("tracks[%u].instrument: no instrument '%s'", i, t->instrument != NULL ? t->instrument : "");
		for (j = 0; j < t->clips->len; j++)
		{
			LrgSongClip *c = &g_array_index (t->clips, LrgSongClip, j);
			gdouble      len;

			if (t->kind == LRG_SONG_TRACK_INSTRUMENT && lrg_song_priv_pattern (self, c->source) == NULL)
				ISSUE ("tracks[%u].clips[%u].pattern: no pattern '%s'", i, j, c->source);
			if (t->kind == LRG_SONG_TRACK_AUDIO)
			{
				g_autofree gchar *path = lrg_song_priv_resolve (self, c->source);

				if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
					ISSUE ("tracks[%u].clips[%u].file: no file '%s'", i, j, path);
			}
			len = lrg_song_priv_clip_length (self, t, c);
			if (c->start < prev_end - 1e-9)
				WARN ("tracks[%u].clips[%u]: overlaps the clip before it", i, j);
			prev_end = MAX (prev_end, c->start + len);
		}
		for (j = 0; j < t->automation->len; j++)
		{
			LrgSongAutomation *a = g_ptr_array_index (t->automation, j);

			if (g_str_has_prefix (a->target, "param.") && t->kind == LRG_SONG_TRACK_INSTRUMENT)
			{
				LrgSongInstrument *in = lrg_song_priv_instrument (self, t->instrument);

				if (in != NULL && !type_has_param (in->type, a->target + 6))
					ISSUE ("tracks[%u].automation[%u].target: a %s has no parameter '%s'", i, j, in->type,
					       a->target + 6);
			}
		}
	}
	for (i = 0; i < self->automation->len; i++)
	{
		LrgSongAutomation *a = g_ptr_array_index (self->automation, i);
		g_auto(GStrv)      parts = g_strsplit (a->target, ".", 4);

		if (!lrg_mixer_asset_has_bus (self->mixer, parts[1]))
			ISSUE ("automation[%u].target: no bus '%s'", i, parts[1]);
		else if (g_str_equal (parts[0], "effect"))
		{
			g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (self->mixer, parts[1]);

			if (ids == NULL || !g_strv_contains ((const gchar * const *) ids, parts[2]))
				ISSUE ("automation[%u].target: bus '%s' has no effect '%s'", i, parts[1], parts[2]);
		}
	}
#undef ISSUE
#undef WARN
	if (issues != NULL)
		*issues = g_steal_pointer (&list);
	return ok;
}

/* ==========================================================================
 * .rsong reading
 * ========================================================================== */

static gboolean
read_points (LrgAudioYamlCtx *ctx,
             YamlNode        *node,
             const gchar     *path,
             GArray          *out)
{
	YamlSequence *seq = lrg_audio_yaml_seq (ctx, node, path);
	guint         i, n;

	if (seq == NULL)
		return FALSE;
	n = yaml_sequence_get_length (seq);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *p = g_strdup_printf ("%s[%u]", path, i);
		const gchar      *text = lrg_audio_yaml_scalar (yaml_sequence_get_element (seq, i));
		g_auto(GStrv)     parts = NULL;
		gdouble           beat, value;

		if (text == NULL)
			return lrg_audio_yaml_fail (ctx, p, "expected \"beat value\"");
		parts = g_strsplit_set (text, " \t", -1);
		{
			/* drop empty pieces from repeated blanks */
			guint   k, m = 0;
			gdouble v[2];

			for (k = 0; parts[k] != NULL; k++)
			{
				if (*parts[k] == '\0')
					continue;
				if (m >= 2 || !lrg_audio_yaml_parse_double (parts[k], &v[m]))
					return lrg_audio_yaml_fail (ctx, p, "expected \"beat value\"");
				m++;
			}
			if (m != 2)
				return lrg_audio_yaml_fail (ctx, p, "expected \"beat value\"");
			beat = v[0];
			value = v[1];
		}
		if (beat < 0.0 || beat > LRG_SONG_MAX_BEATS)
			return lrg_audio_yaml_fail (ctx, p, "beat %g is out of range", beat);
		g_array_append_val (out, beat);
		g_array_append_val (out, value);
	}
	return TRUE;
}

static gboolean
read_automation_list (LrgAudioYamlCtx *ctx,
                      LrgSong         *self,
                      YamlNode        *node,
                      const gchar     *path,
                      const gchar     *track)
{
	YamlSequence *seq = lrg_audio_yaml_seq (ctx, node, path);
	guint         i, n;

	if (seq == NULL)
		return FALSE;
	n = yaml_sequence_get_length (seq);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *p = g_strdup_printf ("%s[%u]", path, i);
		g_autofree gchar *tp = g_strconcat (p, ".target", NULL);
		g_autofree gchar *pp = g_strconcat (p, ".points", NULL);
		g_autoptr(GArray) points = g_array_new (FALSE, FALSE, sizeof (gdouble));
		YamlMapping      *m = lrg_audio_yaml_map (ctx, yaml_sequence_get_element (seq, i), p);
		const gchar      *target = NULL;
		const gchar      *curve = "linear";
		YamlNode         *cn;

		if (m == NULL || !lrg_audio_yaml_string (ctx, yaml_mapping_get_member (m, "target"), tp, &target))
			return FALSE;
		if (!valid_target (target, track != NULL))
			return lrg_audio_yaml_fail (ctx, tp, "'%s' is not an automation target", target);
		if ((cn = yaml_mapping_get_member (m, "curve")) != NULL)
		{
			g_autofree gchar *cp = g_strconcat (p, ".curve", NULL);

			if (!lrg_audio_yaml_string (ctx, cn, cp, &curve))
				return FALSE;
			if (!g_str_equal (curve, "linear") && !g_str_equal (curve, "step"))
				return lrg_audio_yaml_fail (ctx, cp, "expected linear or step");
		}
		if (!read_points (ctx, yaml_mapping_get_member (m, "points"), pp, points))
			return FALSE;
		lrg_song_set_automation (self, track, target, (const gdouble *) points->data, points->len,
		                         g_str_equal (curve, "step"));
	}
	return TRUE;
}

static gboolean
read_song_meta (LrgAudioYamlCtx *ctx,
                LrgSong         *self,
                YamlMapping     *m)
{
	YamlNode    *n;
	const gchar *text = NULL;
	gint64       iv = 0;

	if ((n = yaml_mapping_get_member (m, "title")) != NULL)
	{
		if (!lrg_audio_yaml_string (ctx, n, "song.title", &text))
			return FALSE;
		g_free (self->title);
		self->title = g_strdup (text);
	}
	if ((n = yaml_mapping_get_member (m, "artist")) != NULL)
	{
		if (!lrg_audio_yaml_string (ctx, n, "song.artist", &text))
			return FALSE;
		g_free (self->artist);
		self->artist = g_strdup (text);
	}
	if ((n = yaml_mapping_get_member (m, "bpm")) != NULL
	    && !lrg_audio_yaml_number (ctx, n, "song.bpm", 20.0, 400.0, &self->bpm))
		return FALSE;
	if ((n = yaml_mapping_get_member (m, "time_signature")) != NULL)
	{
		guint num = 0, den = 0;

		if (!lrg_audio_yaml_string (ctx, n, "song.time_signature", &text))
			return FALSE;
		if (sscanf (text, "%u/%u", &num, &den) != 2 || num < 1 || num > 32
		    || (den != 1 && den != 2 && den != 4 && den != 8 && den != 16))
			return lrg_audio_yaml_fail (ctx, "song.time_signature", "expected beats/unit such as 4/4 or 6/8");
		self->beats_per_bar = num;
		self->beat_unit = den;
	}
	if ((n = yaml_mapping_get_member (m, "sample_rate")) != NULL)
	{
		if (!lrg_audio_yaml_int (ctx, n, "song.sample_rate", 8000, 192000, &iv))
			return FALSE;
		self->rate = (guint) iv;
	}
	if ((n = yaml_mapping_get_member (m, "seed")) != NULL)
	{
		if (!lrg_audio_yaml_int (ctx, n, "song.seed", 0, G_MAXUINT32, &iv))
			return FALSE;
		self->seed = (guint32) iv;
	}
	if ((n = yaml_mapping_get_member (m, "length")) != NULL
	    && !lrg_audio_yaml_number (ctx, n, "song.length", 0.0, LRG_SONG_MAX_BEATS, &self->length))
		return FALSE;
	if ((n = yaml_mapping_get_member (m, "loop")) != NULL)
	{
		YamlMapping *lm = lrg_audio_yaml_map (ctx, n, "song.loop");

		if (lm == NULL)
			return FALSE;
		if (!lrg_audio_yaml_number (ctx, yaml_mapping_get_member (lm, "start"), "song.loop.start", 0.0,
		                            LRG_SONG_MAX_BEATS, &self->loop_start)
		    || !lrg_audio_yaml_number (ctx, yaml_mapping_get_member (lm, "end"), "song.loop.end", 0.0,
		                               LRG_SONG_MAX_BEATS, &self->loop_end))
			return FALSE;
	}
	if ((n = yaml_mapping_get_member (m, "tempo")) != NULL)
	{
		YamlSequence *seq = lrg_audio_yaml_seq (ctx, n, "song.tempo");
		guint         i, len;

		if (seq == NULL)
			return FALSE;
		len = yaml_sequence_get_length (seq);
		for (i = 0; i < len; i++)
		{
			g_autofree gchar *p = g_strdup_printf ("song.tempo[%u]", i);
			g_autofree gchar *bp = g_strconcat (p, ".beat", NULL);
			g_autofree gchar *tp = g_strconcat (p, ".bpm", NULL);
			YamlMapping      *tm = lrg_audio_yaml_map (ctx, yaml_sequence_get_element (seq, i), p);
			gdouble           beat = 0, bpm = 0;
			gboolean          ramp = FALSE;
			YamlNode         *rn;

			if (tm == NULL
			    || !lrg_audio_yaml_number (ctx, yaml_mapping_get_member (tm, "beat"), bp, 1e-9, LRG_SONG_MAX_BEATS,
			                               &beat)
			    || !lrg_audio_yaml_number (ctx, yaml_mapping_get_member (tm, "bpm"), tp, 20.0, 400.0, &bpm))
				return FALSE;
			if ((rn = yaml_mapping_get_member (tm, "ramp")) != NULL)
			{
				g_autofree gchar *rp = g_strconcat (p, ".ramp", NULL);

				if (!lrg_audio_yaml_bool (ctx, rn, rp, &ramp))
					return FALSE;
			}
			lrg_song_add_tempo_change (self, beat, bpm, ramp);
		}
	}
	if ((n = yaml_mapping_get_member (m, "markers")) != NULL)
	{
		YamlSequence *seq = lrg_audio_yaml_seq (ctx, n, "song.markers");
		guint         i, len;

		if (seq == NULL)
			return FALSE;
		len = yaml_sequence_get_length (seq);
		for (i = 0; i < len; i++)
		{
			g_autofree gchar *p = g_strdup_printf ("song.markers[%u]", i);
			g_autofree gchar *bp = g_strconcat (p, ".beat", NULL);
			g_autofree gchar *np = g_strconcat (p, ".name", NULL);
			YamlMapping      *mm = lrg_audio_yaml_map (ctx, yaml_sequence_get_element (seq, i), p);
			gdouble           beat = 0;
			const gchar      *name = NULL;

			if (mm == NULL
			    || !lrg_audio_yaml_number (ctx, yaml_mapping_get_member (mm, "beat"), bp, 0.0, LRG_SONG_MAX_BEATS,
			                               &beat)
			    || !lrg_audio_yaml_string (ctx, yaml_mapping_get_member (mm, "name"), np, &name))
				return FALSE;
			if (*name == '\0')
				return lrg_audio_yaml_fail (ctx, np, "a marker needs a name");
			lrg_song_add_marker (self, beat, name);
		}
	}
	return TRUE;
}

static gboolean
read_instruments (LrgAudioYamlCtx *ctx,
                  LrgSong         *self,
                  YamlMapping     *m)
{
	g_autoptr(GList) keys = yaml_mapping_get_members (m);
	GList           *l;

	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *id = l->data;
		g_autofree gchar *p = g_strconcat ("instruments.", id, NULL);
		g_autofree gchar *tp = g_strconcat (p, ".type", NULL);
		YamlMapping      *im = lrg_audio_yaml_map (ctx, yaml_mapping_get_member (m, id), p);
		const gchar      *type = NULL;
		const gchar      *source = NULL;
		YamlNode         *n;

		if (im == NULL || !lrg_audio_yaml_string (ctx, yaml_mapping_get_member (im, "type"), tp, &type))
			return FALSE;
		if (!valid_id (id))
			return lrg_audio_yaml_fail (ctx, p, "'%s' is not a valid id (letters, digits, - _ .)", id);
		if (instrument_gtype (type) == G_TYPE_INVALID)
			return lrg_audio_yaml_fail (ctx, tp, "unknown instrument type '%s' (synth, drums, sampler, soundfont, foley)",
			                            type);
		if ((n = yaml_mapping_get_member (im, "source")) != NULL)
		{
			g_autofree gchar *sp = g_strconcat (p, ".source", NULL);

			if (!lrg_audio_yaml_string (ctx, n, sp, &source))
				return FALSE;
		}
		lrg_song_set_instrument (self, id, type, source);
		if ((n = yaml_mapping_get_member (im, "params")) != NULL)
		{
			g_autofree gchar *pp = g_strconcat (p, ".params", NULL);
			YamlMapping      *pm = lrg_audio_yaml_map (ctx, n, pp);
			g_autoptr(GList)  pkeys = NULL;
			GList            *k;

			if (pm == NULL)
				return FALSE;
			pkeys = yaml_mapping_get_members (pm);
			for (k = pkeys; k != NULL; k = k->next)
			{
				g_autofree gchar *kp = g_strconcat (pp, ".", (const gchar *) k->data, NULL);
				gdouble           v = 0;

				if (!lrg_audio_yaml_number (ctx, yaml_mapping_get_member (pm, k->data), kp, -G_MAXDOUBLE, G_MAXDOUBLE,
				                            &v))
					return FALSE;
				if (!lrg_song_set_instrument_param (self, id, k->data, v))
					return lrg_audio_yaml_fail (ctx, kp, "a %s has no parameter '%s'", type,
					                            (const gchar *) k->data);
			}
		}
	}
	return TRUE;
}

/* "beat key length velocity" (velocity optional, 0.8) */
static gboolean
parse_note (const gchar *text,
            LrgSongNote *out)
{
	g_auto(GStrv) parts = g_strsplit_set (text, " \t", -1);
	const gchar  *f[4] = { NULL, NULL, NULL, NULL };
	guint         i, n = 0;

	for (i = 0; parts[i] != NULL; i++)
		if (*parts[i] != '\0')
		{
			if (n >= 4)
				return FALSE;
			f[n++] = parts[i];
		}
	if (n < 3)
		return FALSE;
	out->key = lrg_music_key_from_name (f[1]);
	out->velocity = 0.8;
	if (out->key < 0 || !lrg_audio_yaml_parse_double (f[0], &out->beat)
	    || !lrg_audio_yaml_parse_double (f[2], &out->length)
	    || (f[3] != NULL && !lrg_audio_yaml_parse_double (f[3], &out->velocity)))
		return FALSE;
	return out->beat >= 0.0 && out->beat <= 1024.0 && out->length > 0.0 && out->velocity >= 0.0
	       && out->velocity <= 1.0;
}

static gboolean
read_patterns (LrgAudioYamlCtx *ctx,
               LrgSong         *self,
               YamlMapping     *m)
{
	g_autoptr(GList) keys = yaml_mapping_get_members (m);
	GList           *l;

	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *id = l->data;
		g_autofree gchar *p = g_strconcat ("patterns.", id, NULL);
		g_autofree gchar *lp = g_strconcat (p, ".length", NULL);
		YamlMapping      *pm = lrg_audio_yaml_map (ctx, yaml_mapping_get_member (m, id), p);
		gdouble           length = 4.0;
		YamlNode         *n;

		if (pm == NULL)
			return FALSE;
		if (!valid_id (id))
			return lrg_audio_yaml_fail (ctx, p, "'%s' is not a valid id", id);
		if ((n = yaml_mapping_get_member (pm, "length")) != NULL
		    && !lrg_audio_yaml_number (ctx, n, lp, 0.25, 1024.0, &length))
			return FALSE;
		lrg_song_add_pattern (self, id, length);
		if ((n = yaml_mapping_get_member (pm, "steps")) != NULL)
		{
			g_autofree gchar *sp = g_strconcat (p, ".steps", NULL);
			gint64            steps = 16;

			if (!lrg_audio_yaml_int (ctx, n, sp, 1, 256, &steps))
				return FALSE;
			lrg_song_pattern_set_steps (self, id, (guint) steps);
		}
		if ((n = yaml_mapping_get_member (pm, "notes")) != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
		{
			g_autofree gchar *np = g_strconcat (p, ".notes", NULL);
			YamlSequence     *seq = lrg_audio_yaml_seq (ctx, n, np);
			guint             i, len;

			if (seq == NULL)
				return FALSE;
			len = yaml_sequence_get_length (seq);
			for (i = 0; i < len; i++)
			{
				g_autofree gchar *ep = g_strdup_printf ("%s[%u]", np, i);
				const gchar      *text = lrg_audio_yaml_scalar (yaml_sequence_get_element (seq, i));
				LrgSongNote       note;

				if (text == NULL || !parse_note (text, &note))
					return lrg_audio_yaml_fail (ctx, ep, "expected \"beat key length [velocity]\" such as \"0 A2 0.5 0.9\"");
				lrg_song_pattern_add_note (self, id, note.beat, note.key, note.length, note.velocity);
			}
		}
		if ((n = yaml_mapping_get_member (pm, "lanes")) != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
		{
			g_autofree gchar *lap = g_strconcat (p, ".lanes", NULL);
			YamlMapping      *lm = lrg_audio_yaml_map (ctx, n, lap);
			g_autoptr(GList)  lanes = NULL;
			GList            *k;

			if (lm == NULL)
				return FALSE;
			lanes = yaml_mapping_get_members (lm);
			for (k = lanes; k != NULL; k = k->next)
			{
				g_autofree gchar *kp = g_strconcat (lap, ".", (const gchar *) k->data, NULL);
				const gchar      *steps = NULL;

				if (!lrg_audio_yaml_string (ctx, yaml_mapping_get_member (lm, k->data), kp, &steps))
					return FALSE;
				if (lrg_song_priv_lane_key (k->data) < 0)
					return lrg_audio_yaml_fail (ctx, kp, "'%s' is not a drum piece or note",
					                            (const gchar *) k->data);
				if (!lrg_song_pattern_set_lane (self, id, k->data, steps))
					return lrg_audio_yaml_fail (ctx, kp, "steps are . - g o x X (spaces and | ignored)");
			}
		}
	}
	return TRUE;
}

static gboolean
read_clip (LrgAudioYamlCtx *ctx,
           LrgSong         *self,
           LrgSongTrack    *t,
           YamlNode        *node,
           const gchar     *path)
{
	YamlMapping *m = lrg_audio_yaml_map (ctx, node, path);
	const gchar *source = NULL;
	const gchar *key = t->kind == LRG_SONG_TRACK_INSTRUMENT ? "pattern" : "file";
	gdouble      start = 0, length = 0;
	gint         index;
	YamlNode    *n;
	static const gchar *fields[] = { "offset", "transpose", "gain", "fade_in", "fade_out", "mute" };
	guint        i;

	if (m == NULL)
		return FALSE;
	{
		g_autofree gchar *sp = g_strconcat (path, ".", key, NULL);
		g_autofree gchar *bp = g_strconcat (path, ".start", NULL);

		if (!lrg_audio_yaml_string (ctx, yaml_mapping_get_member (m, key), sp, &source)
		    || !lrg_audio_yaml_number (ctx, yaml_mapping_get_member (m, "start"), bp, 0.0, LRG_SONG_MAX_BEATS,
		                               &start))
			return FALSE;
		if (*source == '\0')
			return lrg_audio_yaml_fail (ctx, sp, "empty");
	}
	if ((n = yaml_mapping_get_member (m, "length")) != NULL)
	{
		g_autofree gchar *lp = g_strconcat (path, ".length", NULL);

		if (!lrg_audio_yaml_number (ctx, n, lp, 0.0, LRG_SONG_MAX_BEATS, &length))
			return FALSE;
	}
	index = lrg_song_add_clip (self, t->id, source, start, length);
	for (i = 0; i < G_N_ELEMENTS (fields); i++)
	{
		g_autofree gchar *fp = g_strconcat (path, ".", fields[i], NULL);
		gdouble           v = 0;

		if ((n = yaml_mapping_get_member (m, fields[i])) == NULL)
			continue;
		if (g_str_equal (fields[i], "mute"))
		{
			gboolean b = FALSE;

			if (!lrg_audio_yaml_bool (ctx, n, fp, &b))
				return FALSE;
			v = b ? 1.0 : 0.0;
		}
		else if (!lrg_audio_yaml_number (ctx, n, fp, -144.0, LRG_SONG_MAX_BEATS, &v))
			return FALSE;
		lrg_song_set_clip (self, t->id, (guint) index, fields[i], v);
	}
	return TRUE;
}

static gboolean
read_tracks (LrgAudioYamlCtx *ctx,
             LrgSong         *self,
             YamlSequence    *seq)
{
	guint i, n = yaml_sequence_get_length (seq);

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *p = g_strdup_printf ("tracks[%u]", i);
		g_autofree gchar *ip = g_strconcat (p, ".id", NULL);
		YamlMapping      *m = lrg_audio_yaml_map (ctx, yaml_sequence_get_element (seq, i), p);
		const gchar      *id = NULL;
		const gchar      *text = NULL;
		LrgSongTrackKind  kind = LRG_SONG_TRACK_INSTRUMENT;
		LrgSongTrack     *t;
		YamlNode         *node;

		if (m == NULL || !lrg_audio_yaml_string (ctx, yaml_mapping_get_member (m, "id"), ip, &id))
			return FALSE;
		if (!valid_id (id))
			return lrg_audio_yaml_fail (ctx, ip, "'%s' is not a valid id", id);
		if (lrg_song_priv_track (self, id) != NULL)
			return lrg_audio_yaml_fail (ctx, ip, "a track '%s' is defined twice", id);
		if ((node = yaml_mapping_get_member (m, "kind")) != NULL)
		{
			g_autofree gchar *kp = g_strconcat (p, ".kind", NULL);

			if (!lrg_audio_yaml_string (ctx, node, kp, &text))
				return FALSE;
			if (g_str_equal (text, "audio"))
				kind = LRG_SONG_TRACK_AUDIO;
			else if (!g_str_equal (text, "instrument"))
				return lrg_audio_yaml_fail (ctx, kp, "expected instrument or audio");
		}
		lrg_song_add_track (self, id, kind, NULL);
		t = lrg_song_priv_track (self, id);
		if ((node = yaml_mapping_get_member (m, "name")) != NULL)
		{
			g_autofree gchar *np = g_strconcat (p, ".name", NULL);

			if (!lrg_audio_yaml_string (ctx, node, np, &text))
				return FALSE;
			t->name = g_strdup (text);
		}
		if ((node = yaml_mapping_get_member (m, "instrument")) != NULL)
		{
			g_autofree gchar *np = g_strconcat (p, ".instrument", NULL);

			if (!lrg_audio_yaml_string (ctx, node, np, &text))
				return FALSE;
			t->instrument = g_strdup (text);
		}
		if ((node = yaml_mapping_get_member (m, "bus")) != NULL)
		{
			g_autofree gchar *np = g_strconcat (p, ".bus", NULL);

			if (!lrg_audio_yaml_string (ctx, node, np, &text))
				return FALSE;
			g_free (t->bus);
			t->bus = g_strdup (text);
		}
		if ((node = yaml_mapping_get_member (m, "volume")) != NULL)
		{
			g_autofree gchar *np = g_strconcat (p, ".volume", NULL);

			if (!lrg_audio_yaml_number (ctx, node, np, -144.0, 12.0, &t->volume))
				return FALSE;
		}
		if ((node = yaml_mapping_get_member (m, "pan")) != NULL)
		{
			g_autofree gchar *np = g_strconcat (p, ".pan", NULL);

			if (!lrg_audio_yaml_number (ctx, node, np, -1.0, 1.0, &t->pan))
				return FALSE;
		}
		if ((node = yaml_mapping_get_member (m, "mute")) != NULL)
		{
			g_autofree gchar *np = g_strconcat (p, ".mute", NULL);

			if (!lrg_audio_yaml_bool (ctx, node, np, &t->mute))
				return FALSE;
		}
		if ((node = yaml_mapping_get_member (m, "solo")) != NULL)
		{
			g_autofree gchar *np = g_strconcat (p, ".solo", NULL);

			if (!lrg_audio_yaml_bool (ctx, node, np, &t->solo))
				return FALSE;
		}
		if ((node = yaml_mapping_get_member (m, "clips")) != NULL && yaml_node_get_node_type (node) != YAML_NODE_NULL)
		{
			g_autofree gchar *cp = g_strconcat (p, ".clips", NULL);
			YamlSequence     *cs = lrg_audio_yaml_seq (ctx, node, cp);
			guint             j, len;

			if (cs == NULL)
				return FALSE;
			len = yaml_sequence_get_length (cs);
			for (j = 0; j < len; j++)
			{
				g_autofree gchar *ep = g_strdup_printf ("%s[%u]", cp, j);

				if (!read_clip (ctx, self, t, yaml_sequence_get_element (cs, j), ep))
					return FALSE;
			}
		}
		if ((node = yaml_mapping_get_member (m, "automation")) != NULL
		    && yaml_node_get_node_type (node) != YAML_NODE_NULL)
		{
			g_autofree gchar *ap = g_strconcat (p, ".automation", NULL);

			if (!read_automation_list (ctx, self, node, ap, t->id))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
read_song (LrgAudioYamlCtx *ctx,
           LrgSong         *self,
           YamlNode        *root)
{
	YamlMapping *m = lrg_audio_yaml_map (ctx, root, "");
	YamlNode    *n;

	if (m == NULL)
		return FALSE;
	if ((n = yaml_mapping_get_member (m, "version")) != NULL)
	{
		gint64 v = 0;

		if (!lrg_audio_yaml_int (ctx, n, "version", 1, LRG_SONG_FORMAT_VERSION, &v))
			return FALSE;
	}
	if ((n = yaml_mapping_get_member (m, "song")) != NULL)
	{
		YamlMapping *sm = lrg_audio_yaml_map (ctx, n, "song");

		if (sm == NULL || !read_song_meta (ctx, self, sm))
			return FALSE;
	}

	/* the mixer first: tracks and automation name its buses */
	if ((n = yaml_mapping_get_member (m, "mixer")) != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		g_autofree gchar         *text = lrg_audio_yaml_to_data (n, NULL, NULL);
		g_autoptr(GError)         merr = NULL;
		g_autoptr(LrgMixerAsset)  mixer = NULL;

		if (text == NULL)
			return lrg_audio_yaml_fail (ctx, "mixer", "cannot be read");
		mixer = lrg_mixer_asset_new_from_data (text, -1, &merr);
		if (mixer == NULL)
			return lrg_audio_yaml_fail (ctx, "mixer", "%s", merr->message);
		g_clear_object (&self->mixer);
		self->mixer = g_steal_pointer (&mixer);
	}
	if ((n = yaml_mapping_get_member (m, "instruments")) != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		YamlMapping *im = lrg_audio_yaml_map (ctx, n, "instruments");

		if (im == NULL || !read_instruments (ctx, self, im))
			return FALSE;
	}
	if ((n = yaml_mapping_get_member (m, "patterns")) != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		YamlMapping *pm = lrg_audio_yaml_map (ctx, n, "patterns");

		if (pm == NULL || !read_patterns (ctx, self, pm))
			return FALSE;
	}
	if ((n = yaml_mapping_get_member (m, "tracks")) != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		YamlSequence *ts = lrg_audio_yaml_seq (ctx, n, "tracks");

		if (ts == NULL || !read_tracks (ctx, self, ts))
			return FALSE;
	}
	if ((n = yaml_mapping_get_member (m, "automation")) != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL
	    && !read_automation_list (ctx, self, n, "automation", NULL))
		return FALSE;
	return TRUE;
}

LrgSong *
lrg_song_new_from_data (const gchar  *data,
                        gssize        length,
                        GError      **error)
{
	g_autoptr(LrgSong)    self = NULL;
	g_autoptr(YamlParser) parser = NULL;
	LrgAudioYamlCtx       ctx = { "<data>", 0, LRG_SONG_ERROR_PARSE, LRG_SONG_ERROR_INVALID_VALUE, error };
	YamlNode             *root;

	g_return_val_if_fail (data != NULL, NULL);
	ctx.domain = LRG_SONG_ERROR;
	root = lrg_audio_yaml_parse (&ctx, data, length, &parser);
	if (root == NULL)
		return NULL;
	self = lrg_song_new ();
	g_signal_handlers_block_matched (self, G_SIGNAL_MATCH_ID, signals[SIGNAL_CHANGED], 0, NULL, NULL, NULL);
	if (!read_song (&ctx, self, root))
		return NULL;
	g_signal_handlers_unblock_matched (self, G_SIGNAL_MATCH_ID, signals[SIGNAL_CHANGED], 0, NULL, NULL, NULL);
	return g_steal_pointer (&self);
}

LrgSong *
lrg_song_new_from_file (const gchar  *path,
                        GError      **error)
{
	g_autoptr(LrgSong)    self = NULL;
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError)     local = NULL;
	g_autofree gchar     *text = NULL;
	gsize                 length = 0;
	LrgAudioYamlCtx       ctx = { path, 0, LRG_SONG_ERROR_PARSE, LRG_SONG_ERROR_INVALID_VALUE, error };
	YamlNode             *root;

	g_return_val_if_fail (path != NULL, NULL);
	ctx.domain = LRG_SONG_ERROR;
	if (!g_file_get_contents (path, &text, &length, &local))
	{
		g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_IO, "%s: %s", path, local->message);
		return NULL;
	}
	root = lrg_audio_yaml_parse (&ctx, text, (gssize) length, &parser);
	if (root == NULL)
		return NULL;
	self = lrg_song_new ();
	{
		g_autofree gchar *dir = g_path_get_dirname (path);

		self->base_path = g_canonicalize_filename (dir, NULL);
	}
	if (!read_song (&ctx, self, root))
		return NULL;
	return g_steal_pointer (&self);
}

/* ==========================================================================
 * .rsong writing
 * ========================================================================== */

static YamlNode *
points_node (const LrgSongAutomation *a)
{
	YamlSequence *seq;
	YamlNode     *node = lrg_audio_yaml_new_seq (&seq);
	guint         i;

	for (i = 0; i + 1 < a->points->len; i += 2)
	{
		g_autofree gchar *b = lrg_audio_yaml_format_number (g_array_index (a->points, gdouble, i));
		g_autofree gchar *v = lrg_audio_yaml_format_number (g_array_index (a->points, gdouble, i + 1));
		g_autofree gchar *text = g_strconcat (b, " ", v, NULL);

		lrg_audio_yaml_append (seq, yaml_node_new_scalar (text, YAML_SCALAR_STYLE_PLAIN));
	}
	return node;
}

static YamlNode *
automation_node (GPtrArray *list)
{
	YamlSequence *seq;
	YamlNode     *node = lrg_audio_yaml_new_seq (&seq);
	guint         i;

	for (i = 0; i < list->len; i++)
	{
		const LrgSongAutomation *a = g_ptr_array_index (list, i);
		YamlMapping             *m;
		YamlNode                *el = lrg_audio_yaml_new_map (&m);

		lrg_audio_yaml_set (m, "target", lrg_audio_yaml_str (a->target));
		if (a->step)
			lrg_audio_yaml_set (m, "curve", lrg_audio_yaml_str ("step"));
		lrg_audio_yaml_set (m, "points", points_node (a));
		lrg_audio_yaml_append (seq, el);
	}
	return node;
}

gchar *
lrg_song_to_data (LrgSong  *self,
                  gsize    *length,
                  GError  **error)
{
	g_autoptr(YamlNode) root = NULL;
	YamlMapping        *map;
	YamlMapping        *sm;
	guint               i, j;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);

	root = lrg_audio_yaml_new_map (&map);
	lrg_audio_yaml_set (map, "version", lrg_audio_yaml_int_node (LRG_SONG_FORMAT_VERSION));

	/* song */
	lrg_audio_yaml_set (map, "song", lrg_audio_yaml_new_map (&sm));
	if (self->title != NULL)
		lrg_audio_yaml_set (sm, "title", lrg_audio_yaml_str (self->title));
	if (self->artist != NULL)
		lrg_audio_yaml_set (sm, "artist", lrg_audio_yaml_str (self->artist));
	lrg_audio_yaml_set (sm, "bpm", lrg_audio_yaml_num (self->bpm));
	{
		g_autofree gchar *ts = g_strdup_printf ("%u/%u", self->beats_per_bar, self->beat_unit);

		lrg_audio_yaml_set (sm, "time_signature", lrg_audio_yaml_str (ts));
	}
	lrg_audio_yaml_set (sm, "sample_rate", lrg_audio_yaml_int_node (self->rate));
	lrg_audio_yaml_set (sm, "seed", lrg_audio_yaml_int_node (self->seed));
	if (self->length > 0.0)
		lrg_audio_yaml_set (sm, "length", lrg_audio_yaml_num (self->length));
	if (self->loop_end > self->loop_start)
	{
		YamlMapping *lm;

		lrg_audio_yaml_set (sm, "loop", lrg_audio_yaml_new_map (&lm));
		lrg_audio_yaml_set (lm, "start", lrg_audio_yaml_num (self->loop_start));
		lrg_audio_yaml_set (lm, "end", lrg_audio_yaml_num (self->loop_end));
	}
	if (self->tempo->len > 0)
	{
		YamlSequence *seq;

		lrg_audio_yaml_set (sm, "tempo", lrg_audio_yaml_new_seq (&seq));
		for (i = 0; i < self->tempo->len; i++)
		{
			const LrgSongTempo *t = &g_array_index (self->tempo, LrgSongTempo, i);
			YamlMapping        *tm;
			YamlNode           *el = lrg_audio_yaml_new_map (&tm);

			lrg_audio_yaml_set (tm, "beat", lrg_audio_yaml_num (t->beat));
			lrg_audio_yaml_set (tm, "bpm", lrg_audio_yaml_num (t->bpm));
			if (t->ramp)
				lrg_audio_yaml_set (tm, "ramp", lrg_audio_yaml_bool_node (TRUE));
			lrg_audio_yaml_append (seq, el);
		}
	}
	if (self->markers->len > 0)
	{
		YamlSequence *seq;

		lrg_audio_yaml_set (sm, "markers", lrg_audio_yaml_new_seq (&seq));
		for (i = 0; i < self->markers->len; i++)
		{
			const LrgSongMarker *mk = &g_array_index (self->markers, LrgSongMarker, i);
			YamlMapping         *mm;
			YamlNode            *el = lrg_audio_yaml_new_map (&mm);

			lrg_audio_yaml_set (mm, "beat", lrg_audio_yaml_num (mk->beat));
			lrg_audio_yaml_set (mm, "name", lrg_audio_yaml_str (mk->name));
			lrg_audio_yaml_append (seq, el);
		}
	}

	/* instruments, sorted by id */
	if (self->instruments->len > 0)
	{
		g_auto(GStrv) ids = lrg_song_list_instruments (self);
		YamlMapping  *im;

		lrg_audio_yaml_set (map, "instruments", lrg_audio_yaml_new_map (&im));
		for (i = 0; ids != NULL && ids[i] != NULL; i++)
		{
			LrgSongInstrument *in = lrg_song_priv_instrument (self, ids[i]);
			YamlMapping       *m;
			YamlNode          *el;

			if (in == NULL)
				continue;
			el = lrg_audio_yaml_new_map (&m);
			lrg_audio_yaml_set (m, "type", lrg_audio_yaml_str (in->type));
			if (in->source != NULL)
				lrg_audio_yaml_set (m, "source", lrg_audio_yaml_str (in->source));
			if (g_hash_table_size (in->params) > 0)
			{
				g_auto(GStrv) names = lrg_song_list_instrument_params (self, ids[i]);
				YamlMapping  *pm;

				lrg_audio_yaml_set (m, "params", lrg_audio_yaml_new_map (&pm));
				for (j = 0; names != NULL && names[j] != NULL; j++)
					lrg_audio_yaml_set (pm, names[j],
					                    lrg_audio_yaml_num (*(gdouble *) g_hash_table_lookup (in->params, names[j])));
			}
			lrg_audio_yaml_set (im, ids[i], el);
		}
	}

	/* patterns, sorted by id */
	if (self->patterns->len > 0)
	{
		g_auto(GStrv) ids = lrg_song_list_patterns (self);
		YamlMapping  *pm;

		lrg_audio_yaml_set (map, "patterns", lrg_audio_yaml_new_map (&pm));
		for (i = 0; ids != NULL && ids[i] != NULL; i++)
		{
			LrgSongPattern *p = lrg_song_priv_pattern (self, ids[i]);
			YamlMapping    *m;
			YamlNode       *el;

			if (p == NULL)
				continue;
			el = lrg_audio_yaml_new_map (&m);
			lrg_audio_yaml_set (m, "length", lrg_audio_yaml_num (p->length));
			if (p->notes->len > 0)
			{
				YamlSequence *seq;

				lrg_audio_yaml_set (m, "notes", lrg_audio_yaml_new_seq (&seq));
				for (j = 0; j < p->notes->len; j++)
				{
					const LrgSongNote *n = &g_array_index (p->notes, LrgSongNote, j);
					g_autofree gchar  *b = lrg_audio_yaml_format_number (n->beat);
					g_autofree gchar  *k = lrg_music_key_to_name (n->key);
					g_autofree gchar  *l = lrg_audio_yaml_format_number (n->length);
					g_autofree gchar  *v = lrg_audio_yaml_format_number (n->velocity);
					g_autofree gchar  *text = g_strjoin (" ", b, k, l, v, NULL);

					lrg_audio_yaml_append (seq, yaml_node_new_scalar (text, YAML_SCALAR_STYLE_PLAIN));
				}
			}
			if (p->lanes->len > 0)
			{
				YamlMapping *lm;

				lrg_audio_yaml_set (m, "steps", lrg_audio_yaml_int_node (p->steps));
				lrg_audio_yaml_set (m, "lanes", lrg_audio_yaml_new_map (&lm));
				for (j = 0; j < p->lanes->len; j++)
				{
					const LrgSongLane *l = g_ptr_array_index (p->lanes, j);
					YamlNode          *sn = yaml_node_new_scalar (l->steps, YAML_SCALAR_STYLE_DOUBLE_QUOTED);

					lrg_audio_yaml_set (lm, l->name, sn);
				}
			}
			else if (p->steps != 16)
				lrg_audio_yaml_set (m, "steps", lrg_audio_yaml_int_node (p->steps));
			lrg_audio_yaml_set (pm, ids[i], el);
		}
	}

	/* tracks, in order */
	if (self->tracks->len > 0)
	{
		YamlSequence *seq;

		lrg_audio_yaml_set (map, "tracks", lrg_audio_yaml_new_seq (&seq));
		for (i = 0; i < self->tracks->len; i++)
		{
			const LrgSongTrack *t = g_ptr_array_index (self->tracks, i);
			YamlMapping        *m;
			YamlNode           *el = lrg_audio_yaml_new_map (&m);

			lrg_audio_yaml_set (m, "id", lrg_audio_yaml_str (t->id));
			if (t->kind == LRG_SONG_TRACK_AUDIO)
				lrg_audio_yaml_set (m, "kind", lrg_audio_yaml_str ("audio"));
			if (t->name != NULL)
				lrg_audio_yaml_set (m, "name", lrg_audio_yaml_str (t->name));
			if (t->instrument != NULL)
				lrg_audio_yaml_set (m, "instrument", lrg_audio_yaml_str (t->instrument));
			lrg_audio_yaml_set (m, "bus", lrg_audio_yaml_str (t->bus));
			if (t->volume != 0.0)
				lrg_audio_yaml_set (m, "volume", lrg_audio_yaml_num (t->volume));
			if (t->pan != 0.0)
				lrg_audio_yaml_set (m, "pan", lrg_audio_yaml_num (t->pan));
			if (t->mute)
				lrg_audio_yaml_set (m, "mute", lrg_audio_yaml_bool_node (TRUE));
			if (t->solo)
				lrg_audio_yaml_set (m, "solo", lrg_audio_yaml_bool_node (TRUE));
			if (t->clips->len > 0)
			{
				YamlSequence *cs;

				lrg_audio_yaml_set (m, "clips", lrg_audio_yaml_new_seq (&cs));
				for (j = 0; j < t->clips->len; j++)
				{
					const LrgSongClip *c = &g_array_index (t->clips, LrgSongClip, j);
					YamlMapping       *cm;
					YamlNode          *cel = lrg_audio_yaml_new_map (&cm);

					lrg_audio_yaml_set (cm, t->kind == LRG_SONG_TRACK_AUDIO ? "file" : "pattern",
					                    lrg_audio_yaml_str (c->source));
					lrg_audio_yaml_set (cm, "start", lrg_audio_yaml_num (c->start));
					if (c->length > 0.0)
						lrg_audio_yaml_set (cm, "length", lrg_audio_yaml_num (c->length));
					if (c->offset != 0.0)
						lrg_audio_yaml_set (cm, "offset", lrg_audio_yaml_num (c->offset));
					if (c->transpose != 0.0)
						lrg_audio_yaml_set (cm, "transpose", lrg_audio_yaml_num (c->transpose));
					if (c->gain != 0.0)
						lrg_audio_yaml_set (cm, "gain", lrg_audio_yaml_num (c->gain));
					if (c->fade_in != 0.0)
						lrg_audio_yaml_set (cm, "fade_in", lrg_audio_yaml_num (c->fade_in));
					if (c->fade_out != 0.0)
						lrg_audio_yaml_set (cm, "fade_out", lrg_audio_yaml_num (c->fade_out));
					if (c->mute)
						lrg_audio_yaml_set (cm, "mute", lrg_audio_yaml_bool_node (TRUE));
					lrg_audio_yaml_append (cs, cel);
				}
			}
			if (t->automation->len > 0)
				lrg_audio_yaml_set (m, "automation", automation_node (t->automation));
			lrg_audio_yaml_append (seq, el);
		}
	}
	if (self->automation->len > 0)
		lrg_audio_yaml_set (map, "automation", automation_node (self->automation));

	/* the mixer, through its own writer */
	{
		g_autofree gchar     *mtext = lrg_mixer_asset_to_data (self->mixer, NULL, error);
		g_autoptr(YamlParser) mp = NULL;
		LrgAudioYamlCtx       ctx = { "mixer", LRG_SONG_ERROR, LRG_SONG_ERROR_RENDER, LRG_SONG_ERROR_RENDER, error };
		YamlNode             *mroot;

		if (mtext == NULL)
			return NULL;
		mroot = lrg_audio_yaml_parse (&ctx, mtext, -1, &mp);
		if (mroot == NULL)
			return NULL;
		yaml_mapping_set_member (map, "mixer", mroot);
	}
	return lrg_audio_yaml_to_data (root, length, error);
}

gboolean
lrg_song_save_to_file (LrgSong      *self,
                       const gchar  *path,
                       GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;
	gsize             length = 0;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_song_to_data (self, &length, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, (gssize) length, &local))
	{
		g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_IO, "%s: %s", path, local->message);
		return FALSE;
	}
	return TRUE;
}
