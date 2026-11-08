/* lrg-midi-file.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MIDI import and export of songs (see lrg-midi-file.h and
 * docs/modules/music/song.org).
 *
 * The reader is a small bounds-checked SMF parser (header, tracks,
 * running status, meta and system-exclusive events skipped by their
 * lengths).  Beats are quarter notes, so ticks / division are beats
 * whatever the tempo; the tempo map becomes the song's.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-midi-file.h"
#include "lrg-song-private.h"
#include "lrg-drum-machine.h"

#define PPQ (480)

/* ==========================================================================
 * Reading
 * ========================================================================== */

typedef struct
{
	const guint8 *p;
	const guint8 *end;
} Reader;

static gboolean
read_u (Reader  *r,
        guint    n,
        guint32 *out)
{
	guint i;

	if ((gsize) (r->end - r->p) < n)
		return FALSE;
	*out = 0;
	for (i = 0; i < n; i++)
		*out = (*out << 8) | r->p[i];
	r->p += n;
	return TRUE;
}

static gboolean
read_varlen (Reader  *r,
             guint32 *out)
{
	guint i;

	*out = 0;
	for (i = 0; i < 4; i++)
	{
		guint8 b;

		if (r->p >= r->end)
			return FALSE;
		b = *r->p++;
		*out = (*out << 7) | (b & 0x7F);
		if (!(b & 0x80))
			return TRUE;
	}
	return FALSE;
}

typedef struct
{
	guint64 tick;
	guint32 uspq;     /* microseconds per quarter */
} TempoEv;

typedef struct
{
	guint64 on, off;
	gint    key;
	gdouble velocity;
} NoteEv;

typedef struct
{
	GArray *notes;     /* NoteEv */
	gint    program;   /* -1: none */
	gchar  *name;
	gint64  open[128]; /* index into notes of the sounding key, or -1 */
} Channel;

typedef struct
{
	guint64  tick;
	gchar   *text;
} MarkerEv;

static gboolean
parse_fail (GError      **error,
            const gchar  *what)
{
	g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_PARSE, "not a valid MIDI file: %s", what);
	return FALSE;
}

/* the synth preset nearest a General MIDI program */
static const gchar *
preset_for_program (gint program)
{
	if (program < 0)
		return "init";
	if (program < 8)
		return "bell";                  /* pianos */
	if (program < 16)
		return "bell";                  /* chromatic percussion */
	if (program < 24)
		return "organ";
	if (program < 32)
		return "arp-pluck";             /* guitars */
	if (program < 40)
		return "synthwave-bass";
	if (program < 56)
		return "strings";
	if (program < 64)
		return "brass-stab";
	if (program < 80)
		return "synthwave-lead";        /* reeds, pipes */
	if (program < 88)
		return "synthwave-lead";
	if (program < 96)
		return "synthwave-pad";
	if (program < 104)
		return "noise-riser";           /* synth effects */
	return "init";
}

static gboolean
parse_track (Reader     *r,
             Channel    *channels,
             GArray     *tempos,
             GArray     *markers,
             guint      *num,
             guint      *den,
             gchar     **title,
             GError    **error)
{
	guint64      tick = 0;
	guint8       status = 0;
	g_autofree gchar *track_name = NULL;
	gint         first_channel = -1;

	while (r->p < r->end)
	{
		guint32 delta, len;
		guint8  b;

		if (!read_varlen (r, &delta))
			return parse_fail (error, "truncated delta time");
		tick += delta;
		if (r->p >= r->end)
			return parse_fail (error, "truncated event");
		b = *r->p;
		if (b == 0xFF)
		{
			guint8 type;

			r->p++;
			if (r->p >= r->end)
				return parse_fail (error, "truncated meta event");
			type = *r->p++;
			if (!read_varlen (r, &len) || (gsize) (r->end - r->p) < len)
				return parse_fail (error, "truncated meta event");
			if (type == 0x51 && len == 3)
			{
				TempoEv t;

				t.tick = tick;
				t.uspq = ((guint32) r->p[0] << 16) | ((guint32) r->p[1] << 8) | r->p[2];
				if (t.uspq > 0)
					g_array_append_val (tempos, t);
			}
			else if (type == 0x58 && len >= 2 && *num == 0)
			{
				*num = r->p[0];
				*den = 1u << MIN (r->p[1], 4);
			}
			else if (type == 0x06 || type == 0x03)
			{
				g_autofree gchar *raw = g_strndup ((const gchar *) r->p, len);
				gchar            *text = g_utf8_make_valid (raw, -1);

				if (type == 0x06)
				{
					MarkerEv m;

					m.tick = tick;
					m.text = text;
					g_array_append_val (markers, m);
				}
				else
				{
					g_free (track_name);
					track_name = text;
				}
			}
			else if (type == 0x2F)
			{
				r->p += len;
				break;
			}
			r->p += len;
			continue;
		}
		if (b == 0xF0 || b == 0xF7)
		{
			r->p++;
			if (!read_varlen (r, &len) || (gsize) (r->end - r->p) < len)
				return parse_fail (error, "truncated system exclusive event");
			r->p += len;
			continue;
		}
		if (b & 0x80)
		{
			status = b;
			r->p++;
		}
		else if (status == 0)
			return parse_fail (error, "running status without a status");
		{
			guint8   kind = status & 0xF0;
			guint    ch = status & 0x0F;
			guint    n = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
			guint8   d1, d2 = 0;
			Channel *c = &channels[ch];

			if ((gsize) (r->end - r->p) < n)
				return parse_fail (error, "truncated channel event");
			d1 = r->p[0] & 0x7F;
			if (n == 2)
				d2 = r->p[1] & 0x7F;
			r->p += n;
			if (kind == 0xC0 && c->program < 0)
				c->program = d1;
			if (kind == 0x90 && d2 > 0)
			{
				NoteEv note;

				/* a key struck again ends the previous note */
				if (c->open[d1] >= 0)
					g_array_index (c->notes, NoteEv, c->open[d1]).off = tick;
				note.on = tick;
				note.off = G_MAXUINT64;
				note.key = d1;
				note.velocity = d2 / 127.0;
				g_array_append_val (c->notes, note);
				c->open[d1] = c->notes->len - 1;
				if (first_channel < 0)
					first_channel = (gint) ch;
			}
			else if (kind == 0x80 || (kind == 0x90 && d2 == 0))
			{
				if (c->open[d1] >= 0)
				{
					g_array_index (c->notes, NoteEv, c->open[d1]).off = tick;
					c->open[d1] = -1;
				}
			}
		}
	}

	/* the track name names its first channel, or the song */
	if (track_name != NULL)
	{
		if (first_channel >= 0 && channels[first_channel].name == NULL)
			channels[first_channel].name = g_steal_pointer (&track_name);
		else if (*title == NULL)
			*title = g_steal_pointer (&track_name);
	}
	return TRUE;
}

/* letters, digits and '-' from @name, else @fallback */
static gchar *
make_id (const gchar *name,
         const gchar *fallback)
{
	GString     *s = g_string_new (NULL);
	const gchar *p;

	for (p = name; p != NULL && *p != '\0' && s->len < 40; p++)
	{
		if (g_ascii_isalnum (*p))
			g_string_append_c (s, g_ascii_tolower (*p));
		else if (s->len > 0 && s->str[s->len - 1] != '-')
			g_string_append_c (s, '-');
	}
	while (s->len > 0 && s->str[s->len - 1] == '-')
		g_string_truncate (s, s->len - 1);
	if (s->len == 0)
		g_string_assign (s, fallback);
	return g_string_free (s, FALSE);
}

LrgSong *
lrg_song_new_from_midi (GBytes  *smf,
                        GError **error)
{
	g_autoptr(LrgSong) song = NULL;
	g_autoptr(GArray)  tempos = g_array_new (FALSE, FALSE, sizeof (TempoEv));
	g_autoptr(GArray)  markers = g_array_new (FALSE, FALSE, sizeof (MarkerEv));
	Channel            channels[16];
	g_autofree gchar  *title = NULL;
	Reader             r;
	gsize              size = 0;
	guint32            v, len;
	guint              format, ntracks, division, t, ch, num = 0, den = 4;
	guint64            last_tick = 0;
	gboolean           ok = TRUE;
	guint              i;

	g_return_val_if_fail (smf != NULL, NULL);

	r.p = g_bytes_get_data (smf, &size);
	r.end = r.p + size;
	if (size < 14 || memcmp (r.p, "MThd", 4) != 0)
	{
		parse_fail (error, "no MThd header");
		return NULL;
	}
	r.p += 4;
	if (!read_u (&r, 4, &len) || len < 6 || (gsize) (r.end - r.p) < len)
	{
		parse_fail (error, "bad header length");
		return NULL;
	}
	read_u (&r, 2, &v);
	format = v;
	read_u (&r, 2, &v);
	ntracks = v;
	read_u (&r, 2, &v);
	division = v;
	r.p += len - 6;
	if (format > 1 || division == 0 || (division & 0x8000))
	{
		g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_PARSE,
		             "only format 0 and 1 MIDI files with ticks per quarter note are read");
		return NULL;
	}

	memset (channels, 0, sizeof channels);
	for (ch = 0; ch < 16; ch++)
	{
		channels[ch].notes = g_array_new (FALSE, FALSE, sizeof (NoteEv));
		channels[ch].program = -1;
		for (i = 0; i < 128; i++)
			channels[ch].open[i] = -1;
	}

	/* every track chunk (unknown chunks are skipped) */
	for (t = 0; t < ntracks && ok && r.p < r.end;)
	{
		Reader tr;

		if ((gsize) (r.end - r.p) < 8)
		{
			ok = parse_fail (error, "truncated track header");
			break;
		}
		{
			gboolean is_track = memcmp (r.p, "MTrk", 4) == 0;

			r.p += 4;
			read_u (&r, 4, &len);
			if ((gsize) (r.end - r.p) < len)
			{
				ok = parse_fail (error, "truncated track");
				break;
			}
			tr.p = r.p;
			tr.end = r.p + len;
			r.p += len;
			if (!is_track)
				continue;
		}
		ok = parse_track (&tr, channels, tempos, markers, &num, &den, &title, error);
		t++;
	}

	if (ok)
	{
		song = lrg_song_new ();
		g_object_set (song, "title", title, NULL);
		if (num > 0)
			g_object_set (song, "beats-per-bar", MIN (num, 32u), "beat-unit", den, NULL);

		/* tempo: the first at tick 0 is the song's, later ones changes */
		for (i = 0; i < tempos->len; i++)
		{
			const TempoEv *te = &g_array_index (tempos, TempoEv, i);
			gdouble        bpm = CLAMP (60000000.0 / te->uspq, 20.0, 400.0);
			gdouble        beat = (gdouble) te->tick / division;

			if (te->tick == 0)
				g_object_set (song, "bpm", bpm, NULL);
			else
				lrg_song_add_tempo_change (song, beat, bpm, FALSE);
		}
		for (i = 0; i < markers->len; i++)
		{
			const MarkerEv *m = &g_array_index (markers, MarkerEv, i);

			if (m->text != NULL && *m->text != '\0')
				lrg_song_add_marker (song, (gdouble) m->tick / division, m->text);
		}

		/* the last tick anything sounds */
		for (ch = 0; ch < 16; ch++)
			for (i = 0; i < channels[ch].notes->len; i++)
			{
				NoteEv *n = &g_array_index (channels[ch].notes, NoteEv, i);

				last_tick = MAX (last_tick, n->on);
				if (n->off != G_MAXUINT64)
					last_tick = MAX (last_tick, n->off);
			}

		/* one pattern, instrument and track per channel with notes */
		for (ch = 0; ch < 16; ch++)
		{
			Channel          *c = &channels[ch];
			g_autofree gchar *fallback = g_strdup_printf ("ch%u", ch + 1);
			g_autofree gchar *id = NULL;
			gdouble           bar = song->beats_per_bar * 4.0 / song->beat_unit;
			gdouble           end_beat = 0.0;

			if (c->notes->len == 0)
				continue;
			id = make_id (c->name, fallback);
			if (lrg_song_priv_track (song, id) != NULL)
			{
				g_free (id);
				id = g_strdup_printf ("%s-%u", fallback, ch + 1);
			}
			for (i = 0; i < c->notes->len; i++)
			{
				NoteEv *n = &g_array_index (c->notes, NoteEv, i);

				if (n->off == G_MAXUINT64)
					n->off = MAX (last_tick, n->on + 1);
				end_beat = MAX (end_beat, (gdouble) n->off / division);
			}
			end_beat = MAX (ceil (end_beat / bar) * bar, 1.0);
			lrg_song_add_pattern (song, id, MIN (end_beat, 1024.0));
			for (i = 0; i < c->notes->len; i++)
			{
				NoteEv *n = &g_array_index (c->notes, NoteEv, i);

				lrg_song_pattern_add_note (song, id, (gdouble) n->on / division, n->key,
				                           MAX ((gdouble) (n->off - n->on) / division, 1.0 / 64.0), n->velocity);
			}
			if (ch == 9)
				lrg_song_set_instrument (song, id, "drums", "808");
			else
				lrg_song_set_instrument (song, id, "synth", preset_for_program (c->program));
			lrg_song_add_track (song, id, LRG_SONG_TRACK_INSTRUMENT, id);
			if (c->name != NULL)
				lrg_song_set_track (song, id, "name", c->name, 0);
			lrg_song_add_clip (song, id, id, 0, 0);
		}
	}

	for (ch = 0; ch < 16; ch++)
	{
		g_array_unref (channels[ch].notes);
		g_free (channels[ch].name);
	}
	for (i = 0; i < markers->len; i++)
		g_free (g_array_index (markers, MarkerEv, i).text);
	return ok ? g_steal_pointer (&song) : NULL;
}

LrgSong *
lrg_song_new_from_midi_file (const gchar  *path,
                             GError      **error)
{
	g_autoptr(GBytes)  bytes = NULL;
	g_autoptr(GError)  local = NULL;
	g_autofree gchar  *text = NULL;
	gsize              length = 0;
	LrgSong           *song;

	g_return_val_if_fail (path != NULL, NULL);
	if (!g_file_get_contents (path, &text, &length, &local))
	{
		g_set_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_IO, "%s: %s", path, local->message);
		return NULL;
	}
	bytes = g_bytes_new_take (g_steal_pointer (&text), length);
	song = lrg_song_new_from_midi (bytes, error);
	if (song != NULL)
	{
		g_autofree gchar *dir = g_path_get_dirname (path);

		g_object_set (song, "base-path", dir, NULL);
	}
	return song;
}

/* ==========================================================================
 * Writing
 * ========================================================================== */

typedef struct
{
	guint64 tick;
	gint    order;      /* meta 0, off 1, on 2 */
	guint8  data[8];
	guint   len;
} OutEv;

static gint
out_compare (gconstpointer a,
             gconstpointer b)
{
	const OutEv *x = a;
	const OutEv *y = b;

	if (x->tick != y->tick)
		return x->tick < y->tick ? -1 : 1;
	return x->order - y->order;
}

static void
put_varlen (GByteArray *b,
            guint32     v)
{
	guint8 tmp[5];
	gint   n = 0;

	tmp[n++] = v & 0x7F;
	while ((v >>= 7) > 0)
		tmp[n++] = (guint8) ((v & 0x7F) | 0x80);
	while (n > 0)
		g_byte_array_append (b, &tmp[--n], 1);
}

static void
put_be32 (GByteArray *b,
          guint32     v)
{
	guint8 tmp[4] = { (guint8) (v >> 24), (guint8) (v >> 16), (guint8) (v >> 8), (guint8) v };

	g_byte_array_append (b, tmp, 4);
}

static void
put_be16 (GByteArray *b,
          guint16     v)
{
	guint8 tmp[2] = { (guint8) (v >> 8), (guint8) v };

	g_byte_array_append (b, tmp, 2);
}

static void
add_meta (GArray      *events,
          guint64      tick,
          guint8       type,
          const guint8 *data,
          guint        len)
{
	OutEv e;

	/* meta events longer than the inline buffer are only text: truncate */
	memset (&e, 0, sizeof e);
	e.tick = tick;
	e.order = 0;
	e.data[0] = 0xFF;
	e.data[1] = type;
	e.data[2] = (guint8) MIN (len, 5u);
	memcpy (e.data + 3, data, MIN (len, 5u));
	e.len = 3 + MIN (len, 5u);
	g_array_append_val (events, e);
}

/* writes @events (sorted here) as an MTrk chunk named @name */
static void
write_track (GByteArray  *out,
             GArray      *events,
             const gchar *name)
{
	g_autoptr(GByteArray) b = g_byte_array_new ();
	guint64               tick = 0;
	guint                 i;

	if (name != NULL)
	{
		gsize n = MIN (strlen (name), (gsize) 127);
		guint8 hdr[3] = { 0xFF, 0x03, (guint8) n };

		put_varlen (b, 0);
		g_byte_array_append (b, hdr, 3);
		g_byte_array_append (b, (const guint8 *) name, (guint) n);
	}
	g_array_sort (events, out_compare);
	for (i = 0; i < events->len; i++)
	{
		const OutEv *e = &g_array_index (events, OutEv, i);

		put_varlen (b, (guint32) (e->tick - tick));
		tick = e->tick;
		g_byte_array_append (b, e->data, e->len);
	}
	{
		guint8 eot[3] = { 0xFF, 0x2F, 0x00 };

		put_varlen (b, 0);
		g_byte_array_append (b, eot, 3);
	}
	g_byte_array_append (out, (const guint8 *) "MTrk", 4);
	put_be32 (out, b->len);
	g_byte_array_append (out, b->data, b->len);
}

/* the General MIDI program of a synth preset */
static gint
program_for_preset (const gchar *preset)
{
	static const struct { const gchar *preset; gint program; } map[] = {
		{ "synthwave-bass", 38 }, { "sub-bass", 38 }, { "acid-bass", 38 }, { "synthwave-pad", 89 },
		{ "synthwave-lead", 81 }, { "arp-pluck", 84 }, { "brass-stab", 62 }, { "bell", 14 },
		{ "strings", 50 }, { "noise-riser", 98 }, { "organ", 16 },
	};
	guint i;

	for (i = 0; preset != NULL && i < G_N_ELEMENTS (map); i++)
		if (g_str_equal (map[i].preset, preset))
			return map[i].program;
	return 80;
}

static guint64
to_tick (gdouble beat)
{
	return (guint64) llround (MAX (beat, 0.0) * PPQ);
}

GBytes *
lrg_song_to_midi (LrgSong  *self,
                  GError  **error)
{
	g_autoptr(GByteArray) out = g_byte_array_new ();
	g_autoptr(GArray)     tempo_events = g_array_new (FALSE, FALSE, sizeof (OutEv));
	guint                 n_tracks = 1;
	guint                 next_channel = 0;
	guint                 i, j, k;

	g_return_val_if_fail (LRG_IS_SONG (self), NULL);
	(void) error;

	/* the tempo track: tempo map (ramps as sixteenth steps), meter */
	{
		gdouble end = MAX (lrg_song_get_length_beats (self), 1.0);
		gdouble beat;
		gdouble last_bpm = -1.0;

		for (beat = 0.0; beat < end; beat += 0.25)
		{
			gdouble bpm = lrg_song_get_bpm_at (self, beat);

			if (fabs (bpm - last_bpm) > 1e-6)
			{
				guint32 uspq = (guint32) llround (60000000.0 / bpm);
				guint8  d[3] = { (guint8) (uspq >> 16), (guint8) (uspq >> 8), (guint8) uspq };

				add_meta (tempo_events, to_tick (beat), 0x51, d, 3);
				last_bpm = bpm;
			}
		}
		{
			guint8 ts[4] = { (guint8) self->beats_per_bar, (guint8) (g_bit_nth_lsf (self->beat_unit, -1)), 24, 8 };

			add_meta (tempo_events, 0, 0x58, ts, 4);
		}
	}
	for (i = 0; i < self->tracks->len; i++)
		if (((LrgSongTrack *) g_ptr_array_index (self->tracks, i))->kind == LRG_SONG_TRACK_INSTRUMENT)
			n_tracks++;

	g_byte_array_append (out, (const guint8 *) "MThd", 4);
	put_be32 (out, 6);
	put_be16 (out, 1);
	put_be16 (out, (guint16) n_tracks);
	put_be16 (out, PPQ);

	/* markers go into the tempo track as text events; written inline here
	 * because names can be longer than an OutEv holds */
	{
		g_autoptr(GByteArray) b = g_byte_array_new ();
		guint64               tick = 0;
		guint                 m = 0;

		g_array_sort (tempo_events, out_compare);
		if (self->title != NULL)
		{
			gsize  n = MIN (strlen (self->title), (gsize) 127);
			guint8 hdr[3] = { 0xFF, 0x03, (guint8) n };

			put_varlen (b, 0);
			g_byte_array_append (b, hdr, 3);
			g_byte_array_append (b, (const guint8 *) self->title, (guint) n);
		}
		for (k = 0; k <= tempo_events->len; k++)
		{
			/* interleave markers by tick */
			while (m < self->markers->len
			       && (k == tempo_events->len
			           || to_tick (g_array_index (self->markers, LrgSongMarker, m).beat)
			              <= g_array_index (tempo_events, OutEv, k).tick))
			{
				const LrgSongMarker *mk = &g_array_index (self->markers, LrgSongMarker, m);
				guint64              mt = to_tick (mk->beat);
				gsize                n = MIN (strlen (mk->name), (gsize) 127);
				guint8               hdr[3] = { 0xFF, 0x06, (guint8) n };

				put_varlen (b, (guint32) (mt - tick));
				tick = mt;
				g_byte_array_append (b, hdr, 3);
				g_byte_array_append (b, (const guint8 *) mk->name, (guint) n);
				m++;
			}
			if (k < tempo_events->len)
			{
				const OutEv *e = &g_array_index (tempo_events, OutEv, k);

				put_varlen (b, (guint32) (e->tick - tick));
				tick = e->tick;
				g_byte_array_append (b, e->data, e->len);
			}
		}
		{
			guint8 eot[3] = { 0xFF, 0x2F, 0x00 };

			put_varlen (b, 0);
			g_byte_array_append (b, eot, 3);
		}
		g_byte_array_append (out, (const guint8 *) "MTrk", 4);
		put_be32 (out, b->len);
		g_byte_array_append (out, b->data, b->len);
	}

	/* one track per instrument track */
	for (i = 0; i < self->tracks->len; i++)
	{
		LrgSongTrack      *t = g_ptr_array_index (self->tracks, i);
		LrgSongInstrument *in;
		g_autoptr(GArray)  events = g_array_new (FALSE, FALSE, sizeof (OutEv));
		guint8             channel;

		if (t->kind != LRG_SONG_TRACK_INSTRUMENT)
			continue;
		in = lrg_song_priv_instrument (self, t->instrument);
		if (in != NULL && g_str_equal (in->type, "drums"))
			channel = 9;
		else
		{
			channel = (guint8) (next_channel % 15);
			if (channel >= 9)
				channel++;
			next_channel++;
		}
		if (channel != 9)
		{
			OutEv pc;

			memset (&pc, 0, sizeof pc);
			pc.order = 0;
			pc.data[0] = (guint8) (0xC0 | channel);
			pc.data[1] = (guint8) program_for_preset (in != NULL && g_str_equal (in->type, "synth") ? in->source : NULL);
			pc.len = 2;
			g_array_append_val (events, pc);
		}

		/* the clips' notes in beats, as the engine places them */
		for (j = 0; j < t->clips->len; j++)
		{
			const LrgSongClip *clip = &g_array_index (t->clips, LrgSongClip, j);
			LrgSongPattern    *p = lrg_song_priv_pattern (self, clip->source);
			g_autoptr(GArray)  notes = NULL;
			gdouble            clen, end, rep;
			guint              reps = 0;

			if (clip->mute || p == NULL)
				continue;
			clen = lrg_song_priv_clip_length (self, t, clip);
			end = clip->start + clen;
			notes = lrg_song_priv_pattern_notes (p);
			for (rep = clip->start - clip->offset; rep < end && reps < 100000; rep += p->length, reps++)
				for (k = 0; k < notes->len; k++)
				{
					const LrgSongNote *n = &g_array_index (notes, LrgSongNote, k);
					gdouble            b = rep + n->beat;
					gint               key = n->key + (gint) clip->transpose;
					OutEv              on, off;

					if (n->beat >= p->length || b < clip->start || b >= end || key < 0 || key > 127)
						continue;
					memset (&on, 0, sizeof on);
					memset (&off, 0, sizeof off);
					on.tick = to_tick (b);
					on.order = 2;
					on.data[0] = (guint8) (0x90 | channel);
					on.data[1] = (guint8) key;
					on.data[2] = (guint8) CLAMP (lround (n->velocity * 127.0), 1, 127);
					on.len = 3;
					off.tick = MAX (to_tick (MIN (b + n->length, end)), on.tick + 1);
					off.order = 1;
					off.data[0] = (guint8) (0x80 | channel);
					off.data[1] = (guint8) key;
					off.data[2] = 64;
					off.len = 3;
					g_array_append_val (events, on);
					g_array_append_val (events, off);
				}
		}
		write_track (out, events, t->name != NULL ? t->name : t->id);
	}
	return g_byte_array_free_to_bytes (g_steal_pointer (&out));
}

gboolean
lrg_song_save_midi (LrgSong      *self,
                    const gchar  *path,
                    GError      **error)
{
	g_autoptr(GBytes) bytes = NULL;
	gsize             size = 0;
	const gchar      *data;

	g_return_val_if_fail (LRG_IS_SONG (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	bytes = lrg_song_to_midi (self, error);
	if (bytes == NULL)
		return FALSE;
	data = g_bytes_get_data (bytes, &size);
	return g_file_set_contents (path, data, (gssize) size, error);
}
