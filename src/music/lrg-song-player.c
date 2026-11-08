/* lrg-song-player.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Realtime song playback (see lrg-song-player.h and
 * docs/modules/music/player.org).
 *
 * Threads:
 *   - the caller (main thread) sends commands (play, seek, swap, layer,
 *     note, ...) through an async queue and reads published state;
 *   - the render side owns the compiled engine.  With an output it is a
 *     worker thread that keeps a ring buffer of LRG_SONG_PLAYER_RING_FRAMES
 *     full, one 256-frame block at a time; without one it is whoever calls
 *     lrg_song_player_render ();
 *   - the audio device's thread (graylib's stream callback) copies frames
 *     out of the ring and never blocks: an empty ring plays silence and
 *     counts an underrun.
 *
 * The ring is single-producer single-consumer with two monotonic frame
 * counters (atomic).  For every block written the song frame of its first
 * sample is kept, so the heard position is known whatever loops and jumps
 * happened in between.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>

#include <graylib.h>

#include "lrg-song-player.h"
#include "lrg-song-private.h"
#include "../audio/lrg-audio-dsp-private.h"

#define BLOCK        (LRG_AUDIO_MIXER_BLOCK_FRAMES)
#define CAP          (LRG_SONG_PLAYER_RING_FRAMES)
#define N_META       (CAP / BLOCK)
#define STREAM_BLOCK (1024)
#define END_TAIL_S   (2.0)

G_DEFINE_ENUM_TYPE (LrgSongQuantize, lrg_song_quantize,
	G_DEFINE_ENUM_VALUE (LRG_SONG_QUANTIZE_NOW, "now"),
	G_DEFINE_ENUM_VALUE (LRG_SONG_QUANTIZE_BEAT, "beat"),
	G_DEFINE_ENUM_VALUE (LRG_SONG_QUANTIZE_BAR, "bar"))

typedef enum
{
	CMD_PLAY,
	CMD_PAUSE,
	CMD_STOP,
	CMD_SEEK,
	CMD_LOOP,
	CMD_SWAP,
	CMD_SECTION,
	CMD_LAYER,
	CMD_STINGER,
	CMD_NOTE_ON,
	CMD_NOTE_OFF
} CmdType;

typedef struct
{
	CmdType        type;
	gdouble        a, b;       /* beats, gains, seconds, velocity */
	gdouble        quantum;    /* beats to round up to (0: now) */
	gint           track;
	gint           key;
	gboolean       flag;
	LrgSongEngine *engine;     /* CMD_SWAP */
	GArray        *notes;      /* CMD_STINGER: LrgSongNote */
} Cmd;

struct _LrgSongPlayer
{
	GObject        parent_instance;

	LrgSong       *song;           /* main thread: a snapshot for lookups */
	guint          rate;
	GAsyncQueue   *commands;       /* Cmd */

	/* the render side */
	LrgSongEngine *engine;
	gboolean       playing;
	gboolean       loop;
	gdouble        loop_start, loop_end;
	gboolean       has_jump;
	guint64        jump_at;
	gdouble        jump_to;

	/* published by the render side */
	GMutex         lock;
	gboolean       pub_playing;
	gint64         pub_frame;      /* headless: the next frame */
	gint64         meta[N_META];   /* song frame of each ring block's start, -1 silence */
	GHashTable    *meters;         /* bus -> gdouble[2] */

	/* the ring */
	gfloat        *ring;
	guint          wpos;           /* frames written (atomic) */
	guint          rpos;           /* frames read (atomic) */
	gint           underruns;      /* atomic */

	/* the output */
	GThread       *worker;
	gint           running;        /* atomic */
	GrlAudioStream *stream;
	gboolean       threaded;
	gfloat        *pump;
};

G_DEFINE_FINAL_TYPE (LrgSongPlayer, lrg_song_player, G_TYPE_OBJECT)

static void
cmd_free (gpointer data)
{
	Cmd *c = data;

	if (c->engine != NULL)
		lrg_song_engine_free (c->engine);
	if (c->notes != NULL)
		g_array_unref (c->notes);
	g_free (c);
}

static Cmd *
cmd_new (CmdType type)
{
	Cmd *c = g_new0 (Cmd, 1);

	c->type = type;
	c->track = -1;
	return c;
}

static void
send (LrgSongPlayer *self,
      Cmd           *c)
{
	g_async_queue_push (self->commands, c);
}

/* ==========================================================================
 * The render side
 * ========================================================================== */

/* the beat @quantum rounds the current position up to */
static gdouble
quantized_beat (LrgSongPlayer *self,
                gdouble        quantum)
{
	gdouble now = lrg_song_engine_frame_to_beat (self->engine, lrg_song_engine_get_frame (self->engine));

	if (quantum <= 0.0)
		return now;
	return ceil (now / quantum - 1e-9) * quantum;
}

static void
apply (LrgSongPlayer *self,
       Cmd           *c)
{
	LrgSongEngine *e = self->engine;
	guint          i;

	switch (c->type)
	{
	case CMD_PLAY:
		self->playing = TRUE;
		break;
	case CMD_PAUSE:
		self->playing = FALSE;
		break;
	case CMD_STOP:
		self->playing = FALSE;
		self->has_jump = FALSE;
		lrg_song_engine_seek (e, lrg_song_engine_beat_to_frame (e, self->loop ? self->loop_start : 0.0));
		break;
	case CMD_SEEK:
		self->has_jump = FALSE;
		lrg_song_engine_seek (e, lrg_song_engine_beat_to_frame (e, MAX (c->a, 0.0)));
		break;
	case CMD_LOOP:
		self->loop = c->flag;
		self->loop_start = c->a;
		self->loop_end = c->b;
		break;
	case CMD_SWAP:
		{
			gdouble beat = lrg_song_engine_frame_to_beat (e, lrg_song_engine_get_frame (e));

			self->engine = c->engine;
			c->engine = e;          /* the old one is freed with the command */
			lrg_song_engine_seek (self->engine, lrg_song_engine_beat_to_frame (self->engine, beat));
		}
		break;
	case CMD_SECTION:
		self->jump_to = c->a;
		self->jump_at = lrg_song_engine_beat_to_frame (e, quantized_beat (self, c->quantum));
		self->has_jump = TRUE;
		break;
	case CMD_LAYER:
		lrg_song_engine_set_layer_gain (e, (guint) c->track, c->a, c->b);
		break;
	case CMD_STINGER:
		{
			gdouble start = quantized_beat (self, c->quantum);

			for (i = 0; i < c->notes->len; i++)
			{
				const LrgSongNote *n = &g_array_index (c->notes, LrgSongNote, i);
				guint64            at = lrg_song_engine_beat_to_frame (e, start + n->beat);
				guint64            off = lrg_song_engine_beat_to_frame (e, start + n->beat + n->length);

				lrg_song_engine_play_note (e, (guint) c->track, at, n->key, n->velocity, MAX (off, at + 1) - at);
			}
		}
		break;
	case CMD_NOTE_ON:
		lrg_song_engine_play_note (e, (guint) c->track, lrg_song_engine_get_frame (e), c->key, c->a, 0);
		break;
	case CMD_NOTE_OFF:
	default:
		lrg_song_engine_release_note (e, (guint) c->track, c->key);
		break;
	}
}

static void
apply_commands (LrgSongPlayer *self)
{
	Cmd *c;

	while ((c = g_async_queue_try_pop (self->commands)) != NULL)
	{
		apply (self, c);
		cmd_free (c);
	}
}

/* publishes the state and the meters after a block */
static void
publish (LrgSongPlayer *self)
{
	LrgAudioMixer *mixer = lrg_song_engine_get_mixer (self->engine);
	guint          i, n = lrg_audio_mixer_get_bus_count (mixer);

	g_mutex_lock (&self->lock);
	self->pub_playing = self->playing;
	self->pub_frame = (gint64) lrg_song_engine_get_frame (self->engine);
	for (i = 0; i < n; i++)
	{
		const gchar *bus = lrg_audio_mixer_get_bus_name (mixer, i);
		gdouble     *m = g_hash_table_lookup (self->meters, bus);

		if (m == NULL)
		{
			m = g_new0 (gdouble, 2);
			g_hash_table_insert (self->meters, g_strdup (bus), m);
		}
		lrg_audio_mixer_get_bus_meter (mixer, bus, -1, &m[0], &m[1]);
	}
	g_mutex_unlock (&self->lock);
}

/*
 * produce:
 * Renders @frames frames into @out: commands first, then the engine up to
 * every jump, loop end and the song's end (each lands on its exact frame).
 * Returns the song frame of the first sample, or -1 for silence.
 */
static gint64
produce (LrgSongPlayer *self,
         gfloat        *out,
         guint          frames)
{
	gint64 first = -1;
	guint  done = 0;

	apply_commands (self);
	while (done < frames)
	{
		LrgSongEngine *e = self->engine;
		guint64        f = lrg_song_engine_get_frame (e);
		guint          n = frames - done;
		guint64        loop_end = self->loop ? lrg_song_engine_beat_to_frame (e, self->loop_end) : 0;

		if (!self->playing)
		{
			memset (out + (gsize) done * 2, 0, (gsize) n * 2 * sizeof (gfloat));
			break;
		}
		/* a jump or loop end due now happens before anything renders */
		if (self->has_jump && f >= self->jump_at)
		{
			self->has_jump = FALSE;
			lrg_song_engine_seek (e, lrg_song_engine_beat_to_frame (e, self->jump_to));
			continue;
		}
		if (self->loop && loop_end > 0 && f >= loop_end)
		{
			lrg_song_engine_seek (e, lrg_song_engine_beat_to_frame (e, self->loop_start));
			continue;
		}
		if (first < 0)
			first = (gint64) f;
		if (self->has_jump && self->jump_at > f)
			n = (guint) MIN ((guint64) n, self->jump_at - f);
		if (self->loop && loop_end > f)
			n = (guint) MIN ((guint64) n, loop_end - f);
		lrg_song_engine_render (e, out + (gsize) done * 2, n);
		done += n;
		f += n;
		if (self->has_jump && f >= self->jump_at)
		{
			self->has_jump = FALSE;
			lrg_song_engine_seek (e, lrg_song_engine_beat_to_frame (e, self->jump_to));
		}
		else if (self->loop && f >= loop_end && loop_end > 0)
			lrg_song_engine_seek (e, lrg_song_engine_beat_to_frame (e, self->loop_start));
		else if (!self->loop
		         && f >= lrg_song_engine_get_end_frame (e) + (guint64) (END_TAIL_S * lrg_song_engine_get_rate (e)))
			self->playing = FALSE;
	}
	publish (self);
	return first;
}

/* ==========================================================================
 * The ring, the worker and the device
 * ========================================================================== */

static gpointer
worker_main (gpointer data)
{
	LrgSongPlayer *self = data;
	gfloat         block[BLOCK * 2];

	while (g_atomic_int_get (&self->running))
	{
		guint w = (guint) g_atomic_int_get ((gint *) &self->wpos);
		guint r = (guint) g_atomic_int_get ((gint *) &self->rpos);
		guint at, first_part;
		gint64 frame;

		if (CAP - (w - r) < BLOCK)
		{
			g_usleep (1000);
			continue;
		}
		frame = produce (self, block, BLOCK);
		at = w % CAP;
		first_part = MIN ((guint) BLOCK, CAP - at);
		memcpy (self->ring + (gsize) at * 2, block, (gsize) first_part * 2 * sizeof (gfloat));
		if (first_part < BLOCK)
			memcpy (self->ring, block + first_part * 2, (gsize) (BLOCK - first_part) * 2 * sizeof (gfloat));
		g_mutex_lock (&self->lock);
		self->meta[(w / BLOCK) % N_META] = frame;
		g_mutex_unlock (&self->lock);
		g_atomic_int_set ((gint *) &self->wpos, (gint) (w + BLOCK));
	}
	return NULL;
}

/* copies @frames frames out of the ring (silence past what is there) */
static void
ring_pull (LrgSongPlayer *self,
           gfloat        *out,
           guint          frames)
{
	guint r = (guint) g_atomic_int_get ((gint *) &self->rpos);
	guint w = (guint) g_atomic_int_get ((gint *) &self->wpos);
	guint have = MIN (w - r, frames);
	guint i;

	for (i = 0; i < have; i++)
	{
		guint at = (r + i) % CAP;

		out[i * 2] = self->ring[at * 2];
		out[i * 2 + 1] = self->ring[at * 2 + 1];
	}
	if (have < frames)
	{
		memset (out + (gsize) have * 2, 0, (gsize) (frames - have) * 2 * sizeof (gfloat));
		g_atomic_int_inc (&self->underruns);
	}
	g_atomic_int_set ((gint *) &self->rpos, (gint) (r + have));
}

/* graylib's stream callback, on the audio device's thread */
static void
stream_fill (gpointer buffer,
             guint    n_frames,
             gpointer user_data)
{
	ring_pull (user_data, buffer, n_frames);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_song_player_finalize (GObject *object)
{
	LrgSongPlayer *self = LRG_SONG_PLAYER (object);

	lrg_song_player_stop_output (self);
	g_async_queue_unref (self->commands);
	g_clear_pointer (&self->engine, lrg_song_engine_free);
	g_clear_object (&self->song);
	g_hash_table_unref (self->meters);
	g_mutex_clear (&self->lock);
	g_free (self->ring);
	g_free (self->pump);
	G_OBJECT_CLASS (lrg_song_player_parent_class)->finalize (object);
}

static void
lrg_song_player_class_init (LrgSongPlayerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_song_player_finalize;
}

static void
lrg_song_player_init (LrgSongPlayer *self)
{
	guint i;

	g_mutex_init (&self->lock);
	self->commands = g_async_queue_new_full (cmd_free);
	self->meters = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->ring = g_new0 (gfloat, (gsize) CAP * 2);
	for (i = 0; i < N_META; i++)
		self->meta[i] = -1;
}

LrgSongPlayer *
lrg_song_player_new (LrgSong  *song,
                     GError  **error)
{
	g_autoptr(LrgSongPlayer) self = NULL;

	g_return_val_if_fail (LRG_IS_SONG (song), NULL);

	self = g_object_new (LRG_TYPE_SONG_PLAYER, NULL);
	self->song = lrg_song_copy (song);
	self->rate = song->rate;
	self->engine = lrg_song_engine_new (song, self->rate, NULL, NULL, error);
	if (self->engine == NULL)
		return NULL;
	self->loop = song->loop_end > song->loop_start;
	self->loop_start = song->loop_start;
	self->loop_end = song->loop_end;
	publish (self);
	return g_steal_pointer (&self);
}

gboolean
lrg_song_player_set_song (LrgSongPlayer  *self,
                          LrgSong        *song,
                          GError        **error)
{
	LrgSongEngine *engine;
	Cmd           *c;

	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	g_return_val_if_fail (LRG_IS_SONG (song), FALSE);

	engine = lrg_song_engine_new (song, self->rate, NULL, NULL, error);
	if (engine == NULL)
		return FALSE;
	g_clear_object (&self->song);
	self->song = lrg_song_copy (song);
	c = cmd_new (CMD_SWAP);
	c->engine = engine;
	send (self, c);
	return TRUE;
}

void
lrg_song_player_play (LrgSongPlayer *self)
{
	g_return_if_fail (LRG_IS_SONG_PLAYER (self));
	send (self, cmd_new (CMD_PLAY));
	g_mutex_lock (&self->lock);
	self->pub_playing = TRUE;
	g_mutex_unlock (&self->lock);
}

void
lrg_song_player_pause (LrgSongPlayer *self)
{
	g_return_if_fail (LRG_IS_SONG_PLAYER (self));
	send (self, cmd_new (CMD_PAUSE));
	g_mutex_lock (&self->lock);
	self->pub_playing = FALSE;
	g_mutex_unlock (&self->lock);
}

void
lrg_song_player_stop (LrgSongPlayer *self)
{
	g_return_if_fail (LRG_IS_SONG_PLAYER (self));
	send (self, cmd_new (CMD_STOP));
	g_mutex_lock (&self->lock);
	self->pub_playing = FALSE;
	g_mutex_unlock (&self->lock);
}

gboolean
lrg_song_player_is_playing (LrgSongPlayer *self)
{
	gboolean playing;

	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	g_mutex_lock (&self->lock);
	playing = self->pub_playing;
	g_mutex_unlock (&self->lock);
	return playing;
}

void
lrg_song_player_seek (LrgSongPlayer *self,
                      gdouble        beat)
{
	Cmd *c;

	g_return_if_fail (LRG_IS_SONG_PLAYER (self));
	if (!isfinite (beat))
		return;
	c = cmd_new (CMD_SEEK);
	c->a = beat;
	send (self, c);
}

gdouble
lrg_song_player_get_beat (LrgSongPlayer *self)
{
	gint64 frame;

	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), 0.0);
	g_mutex_lock (&self->lock);
	if (self->worker != NULL)
	{
		guint  r = (guint) g_atomic_int_get ((gint *) &self->rpos);
		gint64 start = self->meta[(r / BLOCK) % N_META];

		frame = start >= 0 ? start + (gint64) (r % BLOCK) : self->pub_frame;
	}
	else
		frame = self->pub_frame;
	g_mutex_unlock (&self->lock);
	return lrg_song_seconds_to_beat (self->song, (gdouble) MAX (frame, 0) / self->rate);
}

gboolean
lrg_song_player_set_loop (LrgSongPlayer *self,
                          gboolean       enabled,
                          gdouble        start,
                          gdouble        end)
{
	Cmd *c;

	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	if (enabled && (!isfinite (start) || !isfinite (end) || start < 0.0 || end <= start))
		return FALSE;
	c = cmd_new (CMD_LOOP);
	c->flag = enabled;
	c->a = start;
	c->b = end;
	send (self, c);
	return TRUE;
}

gboolean
lrg_song_player_start_output (LrgSongPlayer  *self,
                              GError        **error)
{
	GrlAudioDevice *device;

	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);

	if (self->stream != NULL)
		return TRUE;
	device = grl_audio_device_get_default ();
	if (device == NULL || !grl_audio_device_is_ready (device))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "the audio device is not open");
		return FALSE;
	}
	grl_audio_stream_set_default_buffer_size (STREAM_BLOCK);
	self->stream = grl_audio_stream_new (self->rate, 32, 2);
	if (self->stream == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "could not open an audio stream");
		return FALSE;
	}
	g_atomic_int_set ((gint *) &self->wpos, 0);
	g_atomic_int_set ((gint *) &self->rpos, 0);
	g_atomic_int_set (&self->underruns, 0);
	g_atomic_int_set (&self->running, 1);
	self->worker = g_thread_new ("lrg-song-player", worker_main, self);
	grl_audio_stream_set_callback (self->stream, stream_fill, self, NULL);
	self->threaded = grl_audio_stream_has_callback (self->stream);
	if (!self->threaded && self->pump == NULL)
		self->pump = g_new0 (gfloat, STREAM_BLOCK * 2);
	grl_audio_stream_play (self->stream);
	return TRUE;
}

void
lrg_song_player_stop_output (LrgSongPlayer *self)
{
	g_return_if_fail (LRG_IS_SONG_PLAYER (self));

	if (self->stream != NULL)
	{
		grl_audio_stream_set_callback (self->stream, NULL, NULL, NULL);
		if (grl_audio_stream_is_playing (self->stream))
			grl_audio_stream_stop (self->stream);
		g_clear_object (&self->stream);
	}
	if (self->worker != NULL)
	{
		g_atomic_int_set (&self->running, 0);
		g_thread_join (self->worker);
		self->worker = NULL;
	}
}

gboolean
lrg_song_player_has_output (LrgSongPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	return self->stream != NULL;
}

void
lrg_song_player_update (LrgSongPlayer *self)
{
	guint i;

	g_return_if_fail (LRG_IS_SONG_PLAYER (self));
	if (self->stream == NULL || self->threaded)
		return;
	for (i = 0; i < 2 && grl_audio_stream_is_processed (self->stream); i++)
	{
		ring_pull (self, self->pump, STREAM_BLOCK);
		grl_audio_stream_update (self->stream, self->pump, STREAM_BLOCK);
	}
}

gboolean
lrg_song_player_render (LrgSongPlayer *self,
                        gfloat        *out,
                        guint          frames)
{
	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	g_return_val_if_fail (out != NULL || frames == 0, FALSE);

	if (self->worker != NULL)
		return FALSE;
	produce (self, out, frames);
	return TRUE;
}

/* the index of track @id in the song (= in the engine), or -1 */
static gint
track_index (LrgSongPlayer *self,
             const gchar   *id)
{
	guint i;

	for (i = 0; id != NULL && i < self->song->tracks->len; i++)
		if (g_str_equal (((LrgSongTrack *) g_ptr_array_index (self->song->tracks, i))->id, id))
			return (gint) i;
	return -1;
}

static gdouble
quantum (LrgSongPlayer   *self,
         LrgSongQuantize  q)
{
	switch (q)
	{
	case LRG_SONG_QUANTIZE_BEAT: return 1.0;
	case LRG_SONG_QUANTIZE_BAR: return self->song->beats_per_bar * 4.0 / self->song->beat_unit;
	case LRG_SONG_QUANTIZE_NOW:
	default: return 0.0;
	}
}

gboolean
lrg_song_player_queue_section (LrgSongPlayer   *self,
                               const gchar     *marker,
                               LrgSongQuantize  quantize)
{
	gdouble beat;
	Cmd    *c;

	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	beat = lrg_song_find_marker (self->song, marker);
	if (beat < 0.0)
		return FALSE;
	c = cmd_new (CMD_SECTION);
	c->a = beat;
	c->quantum = quantum (self, quantize);
	send (self, c);
	return TRUE;
}

gboolean
lrg_song_player_set_layer (LrgSongPlayer *self,
                           const gchar   *track,
                           gdouble        gain_db,
                           gdouble        seconds)
{
	gint index;
	Cmd *c;

	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	index = track_index (self, track);
	if (index < 0 || !isfinite (gain_db) || !isfinite (seconds))
		return FALSE;
	c = cmd_new (CMD_LAYER);
	c->track = index;
	c->a = lrg_dsp_db_to_lin (CLAMP (gain_db, -144.0, 12.0));
	c->b = MAX (seconds, 0.0);
	send (self, c);
	return TRUE;
}

gboolean
lrg_song_player_play_stinger (LrgSongPlayer   *self,
                              const gchar     *track,
                              const gchar     *pattern,
                              LrgSongQuantize  quantize)
{
	LrgSongPattern *p;
	gint            index;
	Cmd            *c;

	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	index = track_index (self, track);
	p = lrg_song_priv_pattern (self->song, pattern);
	if (index < 0 || p == NULL
	    || ((LrgSongTrack *) g_ptr_array_index (self->song->tracks, index))->kind != LRG_SONG_TRACK_INSTRUMENT)
		return FALSE;
	c = cmd_new (CMD_STINGER);
	c->track = index;
	c->quantum = quantum (self, quantize);
	c->notes = lrg_song_priv_pattern_notes (p);
	send (self, c);
	return TRUE;
}

static gboolean
live_note (LrgSongPlayer *self,
           const gchar   *track,
           gint           key,
           gdouble        velocity,
           gboolean       on)
{
	gint index;
	Cmd *c;

	index = track_index (self, track);
	if (index < 0 || key < 0 || key > 127
	    || ((LrgSongTrack *) g_ptr_array_index (self->song->tracks, index))->kind != LRG_SONG_TRACK_INSTRUMENT)
		return FALSE;
	c = cmd_new (on ? CMD_NOTE_ON : CMD_NOTE_OFF);
	c->track = index;
	c->key = key;
	c->a = CLAMP (velocity, 0.0, 1.0);
	send (self, c);
	return TRUE;
}

gboolean
lrg_song_player_note_on (LrgSongPlayer *self,
                         const gchar   *track,
                         gint           key,
                         gdouble        velocity)
{
	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	return live_note (self, track, key, velocity, TRUE);
}

gboolean
lrg_song_player_note_off (LrgSongPlayer *self,
                          const gchar   *track,
                          gint           key)
{
	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	return live_note (self, track, key, 0.0, FALSE);
}

gboolean
lrg_song_player_get_meter (LrgSongPlayer *self,
                           const gchar   *bus,
                           gdouble       *out_peak,
                           gdouble       *out_rms)
{
	gdouble *m;

	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), FALSE);
	g_mutex_lock (&self->lock);
	m = bus != NULL ? g_hash_table_lookup (self->meters, bus) : NULL;
	if (m != NULL)
	{
		if (out_peak != NULL)
			*out_peak = m[0];
		if (out_rms != NULL)
			*out_rms = m[1];
	}
	g_mutex_unlock (&self->lock);
	return m != NULL;
}

guint
lrg_song_player_get_underruns (LrgSongPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SONG_PLAYER (self), 0);
	return (guint) g_atomic_int_get (&self->underruns);
}
