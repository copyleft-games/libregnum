/* lrg-song-render.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The song engine and offline rendering (see lrg-song.h,
 * lrg-song-private.h and docs/modules/music/song.org).
 *
 * Compiling a song for a rate turns it into:
 *   - a mixer built from the song's layout (clamp off);
 *   - per instrument track: an instrument played as a mixer source voice
 *     on the track's bus, and every note of every clip as a note-on and a
 *     note-off at an absolute frame (pattern clips repeat their pattern
 *     to fill the clip; note-offs sort before note-ons at the same frame);
 *   - per audio track: a clip player (a private LrgAudioSource) holding
 *     each clip's samples at the engine rate with its gain and fades;
 *   - every automation lane as (frame, value) points.
 *
 * Rendering goes in blocks of LRG_AUDIO_MIXER_BLOCK_FRAMES: the notes
 * falling in the block are scheduled on their instruments (which place
 * them on the exact sample), automation is applied (voice gains, pans,
 * bus gains and effect parameters ramp across the block; instrument
 * parameters are set at its start), then the mixer renders the block.
 * Seeking cuts every voice, restarts the instruments' clocks at the new
 * frame and re-sounds the notes that span it.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "lrg-song.h"
#include "lrg-song-private.h"
#include "../audio/lrg-audio-source.h"
#include "../audio/lrg-audio-analysis.h"
#include "../audio/lrg-audio-encoder.h"
#include "../audio/lrg-audio-dsp-private.h"

#define BLOCK      (LRG_AUDIO_MIXER_BLOCK_FRAMES)
#define CHUNK      (4096)
#define MAX_TAIL_S (15.0)
#define SILENCE    (3e-5)   /* about -90 dBFS */

/* ==========================================================================
 * Audio files
 * ========================================================================== */

LrgWaveData *
lrg_song_priv_load_audio (const gchar  *path,
                          guint         rate,
                          GError      **error)
{
	g_autoptr(LrgWaveData) wave = NULL;
	guint                  channels;

	wave = lrg_audio_decode_file (path, error);
	if (wave == NULL)
		return NULL;
	channels = lrg_wave_data_get_channels (wave);
	if (rate == 0)
		rate = lrg_wave_data_get_sample_rate (wave);
	if (rate == lrg_wave_data_get_sample_rate (wave) && channels == 2 && lrg_wave_data_get_sample_size (wave) == 32)
		return g_steal_pointer (&wave);
	return lrg_wave_data_convert (wave, rate, 32, 2);
}

/* ==========================================================================
 * The clip player: audio-file clips as one source voice
 * ========================================================================== */

typedef struct
{
	gfloat  *pcm;        /* stereo float at the engine rate (shared, not owned) */
	gsize    frames;
	guint64  start;      /* song frame of the clip's first sample */
	guint64  length;     /* frames it plays */
	gsize    offset;     /* first frame of @pcm it plays */
	gdouble  gain;
	guint64  fade_in;    /* frames */
	guint64  fade_out;
} PlayClip;

#define SONG_TYPE_CLIP_PLAYER (song_clip_player_get_type ())
G_DECLARE_FINAL_TYPE (SongClipPlayer, song_clip_player, SONG, CLIP_PLAYER, GObject)

struct _SongClipPlayer
{
	GObject parent_instance;
	GArray *clips;       /* PlayClip, sorted by start */
	guint64 pos;         /* song frame of the next sample */
};

static void song_clip_player_source_init (LrgAudioSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SongClipPlayer, song_clip_player, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_AUDIO_SOURCE, song_clip_player_source_init))

static gboolean
clip_player_render (LrgAudioSource *source,
                    gfloat         *out,
                    guint           frames,
                    guint           channels,
                    guint           sample_rate)
{
	SongClipPlayer *self = SONG_CLIP_PLAYER (source);
	guint64         from = self->pos;
	guint64         to = self->pos + frames;
	guint           i;

	(void) sample_rate;
	for (i = 0; i < self->clips->len; i++)
	{
		const PlayClip *c = &g_array_index (self->clips, PlayClip, i);
		guint64         a = MAX (from, c->start);
		guint64         b = MIN (to, c->start + c->length);
		guint64         f;

		if (c->start >= to)
			break;
		for (f = a; f < b; f++)
		{
			guint64 local = f - c->start;
			gsize   src = c->offset + (gsize) local;
			gdouble g = c->gain;

			if (src >= c->frames)
				break;
			if (c->fade_in > 0 && local < c->fade_in)
				g *= (gdouble) local / (gdouble) c->fade_in;
			if (c->fade_out > 0 && c->length - local <= c->fade_out)
				g *= (gdouble) (c->length - local) / (gdouble) c->fade_out;
			if (channels == 2)
			{
				out[(f - from) * 2] += (gfloat) (c->pcm[src * 2] * g);
				out[(f - from) * 2 + 1] += (gfloat) (c->pcm[src * 2 + 1] * g);
			}
			else
				out[f - from] += (gfloat) ((c->pcm[src * 2] + c->pcm[src * 2 + 1]) * 0.5 * g);
		}
	}
	self->pos = to;
	return TRUE;
}

static void
clip_player_reset (LrgAudioSource *source)
{
	SONG_CLIP_PLAYER (source)->pos = 0;
}

static void
song_clip_player_source_init (LrgAudioSourceInterface *iface)
{
	iface->render = clip_player_render;
	iface->reset = clip_player_reset;
}

static void
song_clip_player_finalize (GObject *object)
{
	g_array_unref (SONG_CLIP_PLAYER (object)->clips);
	G_OBJECT_CLASS (song_clip_player_parent_class)->finalize (object);
}

static void
song_clip_player_class_init (SongClipPlayerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = song_clip_player_finalize;
}

static void
song_clip_player_init (SongClipPlayer *self)
{
	self->clips = g_array_new (FALSE, FALSE, sizeof (PlayClip));
}

/* ==========================================================================
 * The engine
 * ========================================================================== */

typedef struct
{
	guint64 frame;
	gint    order;      /* 0 note-off, 1 note-on: offs first at a frame */
	gint    key;
	gdouble velocity;
} Ev;

typedef struct
{
	guint64 on;
	guint64 off;
	gint    key;
	gdouble velocity;
} Span;

typedef enum
{
	LANE_VOLUME,
	LANE_PAN,
	LANE_PARAM,
	LANE_BUS_VOLUME,
	LANE_EFFECT
} LaneKind;

typedef struct
{
	guint64 frame;
	gdouble value;
} Pt;

typedef struct
{
	LaneKind kind;
	gchar   *param;       /* PARAM: instrument property; EFFECT: effect property */
	gchar   *bus;
	guint    effect;      /* EFFECT: chain index */
	gboolean step;
	GArray  *pts;         /* Pt, sorted */
	gdouble  last;        /* PARAM: the value last set */
} Lane;

typedef struct
{
	gchar          *id;
	LrgInstrument  *instrument;   /* instrument tracks */
	SongClipPlayer *player;       /* audio tracks */
	guint64         voice;
	GArray         *events;       /* Ev */
	GArray         *spans;        /* Span */
	guint           cursor;
	gdouble         volume;       /* linear, the track's fader */
	gdouble         pan;
	gdouble         layer;        /* adaptive layer gain target */
	gdouble         layer_from;
	guint64         layer_total;  /* frames of the layer ramp */
	guint64         layer_done;
	GPtrArray      *lanes;        /* Lane */
	Lane           *volume_lane;
	gboolean        audible;
} ETrack;

struct _LrgSongEngine
{
	guint          rate;
	LrgSong       *song;          /* a private copy: the tempo map */
	LrgAudioMixer *mixer;
	GPtrArray     *tracks;        /* ETrack */
	GPtrArray     *lanes;         /* Lane, song-wide */
	GPtrArray     *pcm;           /* decoded audio, owned (gfloat *) */
	guint64        frame;
	guint64        origin;        /* the instruments' clock 0 */
	guint64        end;
};

static void
lane_free (gpointer data)
{
	Lane *l = data;

	g_free (l->param);
	g_free (l->bus);
	g_array_unref (l->pts);
	g_free (l);
}

static void
etrack_free (gpointer data)
{
	ETrack *t = data;

	g_free (t->id);
	g_clear_object (&t->instrument);
	g_clear_object (&t->player);
	g_array_unref (t->events);
	g_array_unref (t->spans);
	g_ptr_array_unref (t->lanes);
	g_free (t);
}

guint64
lrg_song_engine_beat_to_frame (LrgSongEngine *engine,
                               gdouble        beat)
{
	return (guint64) llround (lrg_song_beat_to_seconds (engine->song, beat) * engine->rate);
}

gdouble
lrg_song_engine_frame_to_beat (LrgSongEngine *engine,
                               guint64        frame)
{
	return lrg_song_seconds_to_beat (engine->song, (gdouble) frame / engine->rate);
}

static gint
ev_compare (gconstpointer a,
            gconstpointer b)
{
	const Ev *x = a;
	const Ev *y = b;

	if (x->frame != y->frame)
		return x->frame < y->frame ? -1 : 1;
	return x->order - y->order;
}

/* every note of @track's clips as events and spans */
static void
compile_notes (LrgSongEngine *engine,
               LrgSong       *song,
               LrgSongTrack  *track,
               ETrack        *et)
{
	guint i, j;

	for (i = 0; i < track->clips->len; i++)
	{
		const LrgSongClip *clip = &g_array_index (track->clips, LrgSongClip, i);
		LrgSongPattern    *pattern = lrg_song_priv_pattern (song, clip->source);
		g_autoptr(GArray)  notes = NULL;
		gdouble            clen, end, rep;
		gdouble            vscale = lrg_dsp_db_to_lin (clip->gain);
		guint              reps = 0;

		if (clip->mute || pattern == NULL)
			continue;
		clen = lrg_song_priv_clip_length (song, track, clip);
		end = clip->start + clen;
		notes = lrg_song_priv_pattern_notes (pattern);
		for (rep = clip->start - clip->offset; rep < end && reps < 100000; rep += pattern->length, reps++)
			for (j = 0; j < notes->len; j++)
			{
				const LrgSongNote *n = &g_array_index (notes, LrgSongNote, j);
				gdouble            b = rep + n->beat;
				gdouble            off;
				gint               key = n->key + (gint) clip->transpose;
				Ev                 on_ev, off_ev;
				Span               span;

				if (n->beat >= pattern->length || b < clip->start || b >= end || key < 0 || key > 127)
					continue;
				off = MIN (b + n->length, end);
				on_ev.frame = lrg_song_engine_beat_to_frame (engine, b);
				on_ev.order = 1;
				on_ev.key = key;
				on_ev.velocity = CLAMP (n->velocity * vscale, 0.0, 1.0);
				off_ev.frame = MAX (lrg_song_engine_beat_to_frame (engine, off), on_ev.frame + 1);
				off_ev.order = 0;
				off_ev.key = key;
				off_ev.velocity = 0.0;
				g_array_append_val (et->events, on_ev);
				g_array_append_val (et->events, off_ev);
				span.on = on_ev.frame;
				span.off = off_ev.frame;
				span.key = key;
				span.velocity = on_ev.velocity;
				g_array_append_val (et->spans, span);
			}
	}
	g_array_sort (et->events, ev_compare);
}

/* the audio clips of @track into a clip player */
static gboolean
compile_audio (LrgSongEngine  *engine,
               LrgSong        *song,
               LrgSongTrack   *track,
               ETrack         *et,
               GHashTable     *cache,
               GError        **error)
{
	guint i;

	et->player = g_object_new (SONG_TYPE_CLIP_PLAYER, NULL);
	for (i = 0; i < track->clips->len; i++)
	{
		const LrgSongClip *clip = &g_array_index (track->clips, LrgSongClip, i);
		g_autofree gchar  *path = NULL;
		PlayClip           pc;
		LrgWaveData       *wave;
		guint64            start, stop;

		if (clip->mute)
			continue;
		path = lrg_song_priv_resolve (song, clip->source);
		wave = g_hash_table_lookup (cache, path);
		if (wave == NULL)
		{
			wave = lrg_song_priv_load_audio (path, engine->rate, error);
			if (wave == NULL)
				return FALSE;
			g_hash_table_insert (cache, g_strdup (path), wave);
		}
		memset (&pc, 0, sizeof pc);
		{
			gsize   count = 0;
			gfloat *pcm = lrg_wave_data_get_samples (wave, &count);

			g_ptr_array_add (engine->pcm, pcm);
			pc.pcm = pcm;
			pc.frames = count / 2;
		}
		start = lrg_song_engine_beat_to_frame (engine, clip->start);
		stop = lrg_song_engine_beat_to_frame (engine, clip->start + lrg_song_priv_clip_length (song, track, clip));
		pc.start = start;
		pc.length = stop > start ? stop - start : 0;
		pc.offset = (gsize) llround (clip->offset * engine->rate);
		pc.gain = lrg_dsp_db_to_lin (clip->gain);
		pc.fade_in = (guint64) llround (clip->fade_in * engine->rate);
		pc.fade_out = (guint64) llround (clip->fade_out * engine->rate);
		g_array_append_val (et->player->clips, pc);
	}
	return TRUE;
}

static Lane *
compile_lane (LrgSongEngine           *engine,
              const LrgSongAutomation *a,
              LaneKind                 kind)
{
	Lane *l = g_new0 (Lane, 1);
	guint i;

	l->kind = kind;
	l->step = a->step;
	l->last = NAN;
	l->pts = g_array_new (FALSE, FALSE, sizeof (Pt));
	for (i = 0; i + 1 < a->points->len; i += 2)
	{
		Pt p;

		p.frame = lrg_song_engine_beat_to_frame (engine, g_array_index (a->points, gdouble, i));
		p.value = g_array_index (a->points, gdouble, i + 1);
		g_array_append_val (l->pts, p);
	}
	return l;
}

/* the value of @l at @frame */
static gdouble
lane_at (const Lane *l,
         guint64     frame)
{
	const Pt *p = (const Pt *) l->pts->data;
	guint     n = l->pts->len;
	guint     lo = 0, hi = n;

	if (n == 0)
		return 0.0;
	if (frame <= p[0].frame)
		return p[0].value;
	if (frame >= p[n - 1].frame)
		return p[n - 1].value;
	while (lo + 1 < hi)
	{
		guint mid = (lo + hi) / 2;

		if (p[mid].frame <= frame)
			lo = mid;
		else
			hi = mid;
	}
	if (l->step || p[lo + 1].frame == p[lo].frame)
		return p[lo].value;
	return p[lo].value + (p[lo + 1].value - p[lo].value) * (gdouble) (frame - p[lo].frame)
	                     / (gdouble) (p[lo + 1].frame - p[lo].frame);
}

static gboolean
in_list (const gchar * const *list,
         const gchar         *id)
{
	return list != NULL && g_strv_contains (list, id);
}

LrgSongEngine *
lrg_song_engine_new (LrgSong             *song,
                     guint                rate,
                     const gchar * const *mute,
                     const gchar * const *solo,
                     GError             **error)
{
	LrgSongEngine        *engine;
	g_autoptr(GHashTable) cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	gboolean              any_solo = solo != NULL && solo[0] != NULL;
	gboolean              track_solo = FALSE;
	guint                 i, j;

	g_return_val_if_fail (LRG_IS_SONG (song), NULL);

	engine = g_new0 (LrgSongEngine, 1);
	engine->rate = rate != 0 ? rate : song->rate;
	engine->song = lrg_song_copy (song);
	engine->tracks = g_ptr_array_new_with_free_func (etrack_free);
	engine->lanes = g_ptr_array_new_with_free_func (lane_free);
	engine->pcm = g_ptr_array_new_with_free_func (g_free);
	engine->mixer = lrg_mixer_asset_create_mixer (song->mixer, engine->rate, 2, error);
	if (engine->mixer == NULL)
	{
		lrg_song_engine_free (engine);
		return NULL;
	}
	lrg_audio_mixer_set_clip (engine->mixer, FALSE);
	engine->end = lrg_song_engine_beat_to_frame (engine, lrg_song_get_length_beats (song));

	for (i = 0; i < song->tracks->len; i++)
		if (((LrgSongTrack *) g_ptr_array_index (song->tracks, i))->solo)
			track_solo = TRUE;

	for (i = 0; i < song->tracks->len; i++)
	{
		LrgSongTrack *track = g_ptr_array_index (song->tracks, i);
		ETrack       *et = g_new0 (ETrack, 1);

		et->id = g_strdup (track->id);
		et->events = g_array_new (FALSE, FALSE, sizeof (Ev));
		et->spans = g_array_new (FALSE, FALSE, sizeof (Span));
		et->lanes = g_ptr_array_new_with_free_func (lane_free);
		et->volume = lrg_dsp_db_to_lin (track->volume);
		et->pan = track->pan;
		et->layer = 1.0;
		et->layer_from = 1.0;
		et->audible = !track->mute && !in_list (mute, track->id)
		              && (any_solo ? in_list (solo, track->id) : (!track_solo || track->solo));
		g_ptr_array_add (engine->tracks, et);
		if (!et->audible)
			continue;
		if (!lrg_audio_mixer_has_bus (engine->mixer, track->bus))
		{
			g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_NOT_FOUND, "track '%s': no bus '%s' in the mixer",
			             track->id, track->bus);
			lrg_song_engine_free (engine);
			return NULL;
		}

		/* the sound source */
		if (track->kind == LRG_SONG_TRACK_INSTRUMENT)
		{
			et->instrument = lrg_song_create_instrument (song, track->instrument, error);
			if (et->instrument == NULL)
			{
				g_prefix_error (error, "track '%s': ", track->id);
				lrg_song_engine_free (engine);
				return NULL;
			}
			compile_notes (engine, song, track, et);
			et->voice = lrg_audio_mixer_play_source (engine->mixer, LRG_AUDIO_SOURCE (et->instrument), track->bus,
			                                         MIN (et->volume, 4.0));
		}
		else
		{
			if (!compile_audio (engine, song, track, et, cache, error))
			{
				g_prefix_error (error, "track '%s': ", track->id);
				lrg_song_engine_free (engine);
				return NULL;
			}
			et->voice = lrg_audio_mixer_play_source (engine->mixer, LRG_AUDIO_SOURCE (et->player), track->bus,
			                                         MIN (et->volume, 4.0));
		}
		lrg_audio_mixer_set_voice_pan (engine->mixer, et->voice, CLAMP (et->pan, -1.0, 1.0), 0.0);

		/* its automation */
		for (j = 0; j < track->automation->len; j++)
		{
			const LrgSongAutomation *a = g_ptr_array_index (track->automation, j);
			Lane                    *l;

			if (g_str_equal (a->target, "volume"))
			{
				l = compile_lane (engine, a, LANE_VOLUME);
				et->volume_lane = l;
			}
			else if (g_str_equal (a->target, "pan"))
				l = compile_lane (engine, a, LANE_PAN);
			else if (g_str_has_prefix (a->target, "param.") && et->instrument != NULL)
			{
				l = compile_lane (engine, a, LANE_PARAM);
				l->param = g_strdup (a->target + 6);
			}
			else
				continue;
			g_ptr_array_add (et->lanes, l);
		}
	}

	/* song-wide automation: bus volumes and effect parameters */
	for (i = 0; i < song->automation->len; i++)
	{
		const LrgSongAutomation *a = g_ptr_array_index (song->automation, i);
		g_auto(GStrv)            parts = g_strsplit (a->target, ".", 4);
		Lane                    *l;

		if (g_str_equal (parts[0], "bus") && lrg_audio_mixer_has_bus (engine->mixer, parts[1]))
		{
			l = compile_lane (engine, a, LANE_BUS_VOLUME);
			l->bus = g_strdup (parts[1]);
			g_ptr_array_add (engine->lanes, l);
		}
		else if (g_str_equal (parts[0], "effect") && g_strv_length (parts) == 4)
		{
			g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (song->mixer, parts[1]);
			guint         k;

			for (k = 0; ids != NULL && ids[k] != NULL; k++)
				if (g_str_equal (ids[k], parts[2]))
				{
					l = compile_lane (engine, a, LANE_EFFECT);
					l->bus = g_strdup (parts[1]);
					l->effect = k;
					l->param = g_strdup (parts[3]);
					g_ptr_array_add (engine->lanes, l);
					break;
				}
		}
	}
	return engine;
}

void
lrg_song_engine_free (LrgSongEngine *engine)
{
	if (engine == NULL)
		return;
	g_clear_object (&engine->mixer);
	g_clear_object (&engine->song);
	g_clear_pointer (&engine->tracks, g_ptr_array_unref);
	g_clear_pointer (&engine->lanes, g_ptr_array_unref);
	g_clear_pointer (&engine->pcm, g_ptr_array_unref);
	g_free (engine);
}

guint64
lrg_song_engine_get_frame (LrgSongEngine *engine)
{
	return engine->frame;
}

guint64
lrg_song_engine_get_end_frame (LrgSongEngine *engine)
{
	return engine->end;
}

guint
lrg_song_engine_get_rate (LrgSongEngine *engine)
{
	return engine->rate;
}

LrgAudioMixer *
lrg_song_engine_get_mixer (LrgSongEngine *engine)
{
	return engine->mixer;
}

gint
lrg_song_engine_find_track (LrgSongEngine *engine,
                            const gchar   *id)
{
	guint i;

	for (i = 0; id != NULL && i < engine->tracks->len; i++)
		if (g_str_equal (((ETrack *) g_ptr_array_index (engine->tracks, i))->id, id))
			return (gint) i;
	return -1;
}

LrgInstrument *
lrg_song_engine_get_instrument (LrgSongEngine *engine,
                                guint          track)
{
	if (track >= engine->tracks->len)
		return NULL;
	return ((ETrack *) g_ptr_array_index (engine->tracks, track))->instrument;
}

void
lrg_song_engine_set_layer_gain (LrgSongEngine *engine,
                                guint          track,
                                gdouble        gain,
                                gdouble        seconds)
{
	ETrack *t;

	if (track >= engine->tracks->len || !isfinite (gain))
		return;
	t = g_ptr_array_index (engine->tracks, track);
	t->layer_from = t->layer_total > t->layer_done
	                ? t->layer_from + (t->layer - t->layer_from) * (gdouble) t->layer_done / (gdouble) t->layer_total
	                : t->layer;
	t->layer = CLAMP (gain, 0.0, 4.0);
	t->layer_total = seconds > 0.0 ? (guint64) llround (seconds * engine->rate) : 0;
	t->layer_done = 0;
}

void
lrg_song_engine_seek (LrgSongEngine *engine,
                      guint64        frame)
{
	guint i, j;

	engine->frame = frame;
	engine->origin = frame;
	for (i = 0; i < engine->tracks->len; i++)
	{
		ETrack *t = g_ptr_array_index (engine->tracks, i);

		if (t->player != NULL)
			t->player->pos = frame;
		if (t->instrument == NULL)
			continue;
		lrg_instrument_reset (t->instrument);
		t->cursor = 0;
		while (t->cursor < t->events->len && g_array_index (t->events, Ev, t->cursor).frame < frame)
			t->cursor++;

		/* notes that span the seek point sound from it */
		for (j = 0; j < t->spans->len; j++)
		{
			const Span *s = &g_array_index (t->spans, Span, j);

			if (s->on < frame && s->off > frame)
				lrg_instrument_schedule (t->instrument, 0, LRG_INSTRUMENT_EVENT_NOTE_ON, s->key, s->velocity);
		}
		for (j = 0; j < t->lanes->len; j++)
			((Lane *) g_ptr_array_index (t->lanes, j))->last = NAN;
	}
}

/* the fader gain of @t at @frame (automation or fader) times its layer */
static gdouble
track_gain (ETrack  *t,
            guint64  frame,
            guint64  advance)
{
	gdouble base = t->volume_lane != NULL ? lrg_dsp_db_to_lin (lane_at (t->volume_lane, frame)) : t->volume;
	gdouble layer = t->layer;

	if (t->layer_total > t->layer_done)
	{
		guint64 done = MIN (t->layer_done + advance, t->layer_total);

		layer = t->layer_from + (t->layer - t->layer_from) * (gdouble) done / (gdouble) t->layer_total;
	}
	return CLAMP (base * layer, 0.0, 4.0);
}

static void
render_block (LrgSongEngine *engine,
              gfloat        *out,
              guint          n)
{
	guint64           start = engine->frame;
	guint64           stop = start + n;
	gdouble           seconds = (gdouble) n / engine->rate;
	g_autofree gfloat *block = NULL;
	gsize             count = 0;
	guint             i, j;

	for (i = 0; i < engine->tracks->len; i++)
	{
		ETrack *t = g_ptr_array_index (engine->tracks, i);

		if (!t->audible)
			continue;

		/* notes due in this block, on their exact frame */
		if (t->instrument != NULL)
			while (t->cursor < t->events->len && g_array_index (t->events, Ev, t->cursor).frame < stop)
			{
				const Ev *e = &g_array_index (t->events, Ev, t->cursor);

				lrg_instrument_schedule (t->instrument, e->frame - engine->origin,
				                         e->order == 1 ? LRG_INSTRUMENT_EVENT_NOTE_ON : LRG_INSTRUMENT_EVENT_NOTE_OFF,
				                         e->key, e->velocity);
				t->cursor++;
			}

		/* the fader (automation, layers) ramps across the block */
		if (t->volume_lane != NULL || t->layer_total > t->layer_done || t->layer != 1.0)
			lrg_audio_mixer_set_voice_gain (engine->mixer, t->voice, track_gain (t, stop, n), seconds);
		if (t->layer_total > t->layer_done)
			t->layer_done = MIN (t->layer_done + n, t->layer_total);

		for (j = 0; j < t->lanes->len; j++)
		{
			Lane   *l = g_ptr_array_index (t->lanes, j);
			gdouble v;

			switch (l->kind)
			{
			case LANE_PAN:
				lrg_audio_mixer_set_voice_pan (engine->mixer, t->voice, CLAMP (lane_at (l, stop), -1.0, 1.0),
				                               seconds);
				break;
			case LANE_PARAM:
				v = lane_at (l, start);
				if (v != l->last)
				{
					lrg_instrument_set_param (t->instrument, l->param, v);
					l->last = v;
				}
				break;
			default:
				break;
			}
		}
	}

	for (i = 0; i < engine->lanes->len; i++)
	{
		Lane   *l = g_ptr_array_index (engine->lanes, i);
		gdouble v = lane_at (l, stop);

		if (l->kind == LANE_BUS_VOLUME)
			lrg_audio_mixer_ramp_bus_gain (engine->mixer, l->bus, CLAMP (lrg_dsp_db_to_lin (v), 0.0, 4.0), seconds);
		else if (l->kind == LANE_EFFECT && v != l->last)
		{
			lrg_audio_mixer_ramp_effect_param (engine->mixer, l->bus, l->effect, l->param, v, seconds);
			l->last = v;
		}
	}

	block = lrg_audio_mixer_render (engine->mixer, n, &count);
	if (block != NULL && count == (gsize) n * 2)
		memcpy (out, block, count * sizeof (gfloat));
	else
		memset (out, 0, (gsize) n * 2 * sizeof (gfloat));
	engine->frame = stop;
}

void
lrg_song_engine_render (LrgSongEngine *engine,
                        gfloat        *out,
                        guint          frames)
{
	guint done = 0;

	while (done < frames)
	{
		guint n = MIN (frames - done, (guint) BLOCK);

		render_block (engine, out + (gsize) done * 2, n);
		done += n;
	}
}

/* ==========================================================================
 * Offline rendering
 * ========================================================================== */

static gboolean
cancelled (GCancellable  *cancellable,
           GError       **error)
{
	if (cancellable != NULL && g_cancellable_is_cancelled (cancellable))
	{
		g_set_error_literal (error, LRG_SONG_ERROR, LRG_SONG_ERROR_CANCELLED, "the render was cancelled");
		return TRUE;
	}
	return FALSE;
}

/* the raw (unnormalised) render as interleaved stereo float */
static GArray *
render_raw (LrgSong                    *self,
            const LrgSongRenderOptions *o,
            guint                      *out_rate,
            GCancellable               *cancellable,
            LrgSongProgressFunc         progress,
            gpointer                    user_data,
            gdouble                     progress_from,
            gdouble                     progress_span,
            GError                    **error)
{
	LrgSongEngine *engine;
	GArray        *out;
	gfloat        *chunk;
	guint          rate;
	guint64        from, start, end, f;
	gdouble        total;

	rate = o->sample_rate != 0 ? o->sample_rate : self->rate;
	if (rate < 8000 || rate > 192000)
	{
		g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_INVALID_VALUE, "sample rate %u is out of range", rate);
		return NULL;
	}
	engine = lrg_song_engine_new (self, rate, (const gchar * const *) o->mute, (const gchar * const *) o->solo,
	                              error);
	if (engine == NULL)
		return NULL;

	start = o->start_beat > 0.0 ? lrg_song_engine_beat_to_frame (engine, o->start_beat) : 0;
	end = o->end_beat > 0.0 ? lrg_song_engine_beat_to_frame (engine, o->end_beat) : engine->end;
	if (end <= start)
	{
		g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_RENDER, "nothing to render: the range is empty");
		lrg_song_engine_free (engine);
		return NULL;
	}
	from = 0;
	if (!o->preroll && start > 0)
	{
		lrg_song_engine_seek (engine, start);
		from = start;
	}
	total = (gdouble) (end - from) + (o->tail < 0.0 ? 2.0 * rate : o->tail * rate);
	out = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), (guint) MIN ((end - start) * 2, (guint64) G_MAXUINT32));
	chunk = g_new (gfloat, CHUNK * 2);

	/* the music */
	for (f = from; f < end;)
	{
		guint n = (guint) MIN ((guint64) CHUNK, end - f);

		if (cancelled (cancellable, error))
			goto fail;
		lrg_song_engine_render (engine, chunk, n);
		if (f + n > start)
		{
			guint skip = f < start ? (guint) (start - f) : 0;

			g_array_append_vals (out, chunk + skip * 2, (n - skip) * 2);
		}
		f += n;
		if (progress != NULL)
			progress (progress_from + progress_span * MIN ((gdouble) (f - from) / total, 1.0), user_data);
	}

	/* the tail: fixed, or until a second of silence (at most MAX_TAIL_S) */
	{
		guint64 tail_max = (guint64) llround ((o->tail < 0.0 ? MAX_TAIL_S : o->tail) * rate);
		guint64 quiet = 0;
		guint64 done = 0;

		while (done < tail_max)
		{
			guint   n = (guint) MIN ((guint64) CHUNK, tail_max - done);
			gdouble peak = 0.0;
			guint   k;

			if (cancelled (cancellable, error))
				goto fail;
			lrg_song_engine_render (engine, chunk, n);
			for (k = 0; k < n * 2; k++)
				peak = MAX (peak, fabs ((gdouble) chunk[k]));
			g_array_append_vals (out, chunk, n * 2);
			done += n;
			if (o->tail < 0.0)
			{
				quiet = peak < SILENCE ? quiet + n : 0;
				if (quiet >= rate)
				{
					/* drop the silence but a tenth of a second */
					guint64 drop = (quiet - rate / 10) * 2;

					g_array_set_size (out, out->len - (guint) MIN ((guint64) out->len, drop));
					break;
				}
			}
		}
	}
	if (progress != NULL)
		progress (progress_from + progress_span, user_data);
	g_free (chunk);
	lrg_song_engine_free (engine);
	*out_rate = rate;
	return out;

fail:
	g_free (chunk);
	g_array_unref (out);
	lrg_song_engine_free (engine);
	return NULL;
}

/* the gain @o's normalisation gives @samples */
static gdouble
normalize_gain (const LrgSongRenderOptions *o,
                const gfloat               *samples,
                gsize                       n,
                guint                       rate)
{
	g_autoptr(LrgAudioAnalysis) a = NULL;

	if (o->normalize == LRG_SONG_NORMALIZE_NONE || n == 0)
		return 1.0;
	a = lrg_audio_analyze (samples, n, 2, rate);
	if (a == NULL)
		return 1.0;
	return lrg_audio_analysis_normalize_gain (a, o->normalize == LRG_SONG_NORMALIZE_LOUDNESS, o->target,
	                                          o->true_peak_ceiling);
}

static LrgWaveData *
to_wave (GArray  *samples,
         guint    rate,
         gdouble  gain)
{
	guint i;

	if (gain != 1.0)
		for (i = 0; i < samples->len; i++)
			g_array_index (samples, gfloat, i) = (gfloat) (g_array_index (samples, gfloat, i) * gain);
	return lrg_wave_data_new_from_samples (rate, 32, 2, (const guint8 *) samples->data,
	                                       samples->len * sizeof (gfloat));
}

LrgWaveData *
lrg_song_render (LrgSong                    *self,
                 const LrgSongRenderOptions *options,
                 GCancellable               *cancellable,
                 LrgSongProgressFunc         progress,
                 gpointer                    user_data,
                 GError                    **error)
{
	g_autoptr(LrgSongRenderOptions) defaults = NULL;
	g_autoptr(GArray)               raw = NULL;
	guint                           rate = 0;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (options == NULL)
		options = defaults = lrg_song_render_options_new ();
	raw = render_raw (self, options, &rate, cancellable, progress, user_data, 0.0, 1.0, error);
	if (raw == NULL)
		return NULL;
	return to_wave (raw, rate, normalize_gain (options, (const gfloat *) raw->data, raw->len, rate));
}

GPtrArray *
lrg_song_render_stems (LrgSong                    *self,
                       const LrgSongRenderOptions *options,
                       GCancellable               *cancellable,
                       LrgSongProgressFunc         progress,
                       gpointer                    user_data,
                       gchar                    ***out_names,
                       GError                    **error)
{
	g_autoptr(LrgSongRenderOptions) o = NULL;
	g_autoptr(GPtrArray)            stems = g_ptr_array_new_with_free_func (g_object_unref);
	g_autoptr(GPtrArray)            names = g_ptr_array_new_with_free_func (g_free);
	gdouble                         gain = 1.0;
	guint                           i, n_stems = 0, done = 0;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	g_return_val_if_fail (out_names != NULL, NULL);

	*out_names = NULL;
	o = options != NULL ? lrg_song_render_options_copy (options) : lrg_song_render_options_new ();
	g_clear_pointer (&o->solo, g_strfreev);
	for (i = 0; i < self->tracks->len; i++)
	{
		LrgSongTrack *t = g_ptr_array_index (self->tracks, i);

		if (!t->mute && !in_list ((const gchar * const *) o->mute, t->id))
			n_stems++;
	}
	if (n_stems == 0)
	{
		g_set_error_literal (error, LRG_SONG_ERROR, LRG_SONG_ERROR_RENDER, "the song has no audible track");
		return NULL;
	}

	/* the mix's normalisation gain, so the stems add up to it */
	if (o->normalize != LRG_SONG_NORMALIZE_NONE)
	{
		guint             rate = 0;
		g_autoptr(GArray) mix = render_raw (self, o, &rate, cancellable, progress, user_data, 0.0,
		                                    1.0 / (n_stems + 1), error);

		if (mix == NULL)
			return NULL;
		gain = normalize_gain (o, (const gfloat *) mix->data, mix->len, rate);
		done = 1;
	}

	for (i = 0; i < self->tracks->len; i++)
	{
		LrgSongTrack     *t = g_ptr_array_index (self->tracks, i);
		g_autoptr(GArray) raw = NULL;
		guint             rate = 0;
		gchar            *solo[2] = { t->id, NULL };
		gchar           **keep = o->solo;

		if (t->mute || in_list ((const gchar * const *) o->mute, t->id))
			continue;
		o->solo = solo;
		/* a fixed tail so every stem has the length of the mix */
		if (o->tail < 0.0)
			o->tail = MAX_TAIL_S / 3.0;
		raw = render_raw (self, o, &rate, cancellable, progress, user_data,
		                  (gdouble) done / (n_stems + (o->normalize != LRG_SONG_NORMALIZE_NONE ? 1 : 0)),
		                  1.0 / (n_stems + (o->normalize != LRG_SONG_NORMALIZE_NONE ? 1 : 0)), error);
		o->solo = keep;
		if (raw == NULL)
			return NULL;
		g_ptr_array_add (stems, to_wave (raw, rate, gain));
		g_ptr_array_add (names, g_strdup (t->id));
		done++;
	}
	g_ptr_array_add (names, NULL);
	*out_names = (gchar **) g_ptr_array_free (g_steal_pointer (&names), FALSE);
	return g_steal_pointer (&stems);
}
