/* lrg-midi-synth.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * General MIDI synthesizer: SoundFont 2 banks and Standard MIDI Files,
 * on the vendored, unmodified TinySoundFont / TinyMidiLoader (see
 * lrg-midi-synth-vendor.c and docs/modules/audio/midi-synth.org).
 *
 * Layers, from the bottom:
 *
 *   1. Validation (this file): the SoundFont's RIFF tree and every index
 *      of its preset / instrument / sample tables, and the MIDI file's
 *      chunks and events, are checked by our own bounds-checked readers
 *      before the vendored loaders see them.  The vendored code trusts
 *      those indices; a malformed file must fail with a GError, never
 *      read out of bounds.  The MIDI scan also extracts what
 *      TinyMidiLoader drops: the tempo map, the "loopStart"/"loopEnd"
 *      markers and the song's length.
 *   2. Songs: one synthesizer instance (a tsf_copy() sharing the bank's
 *      samples) and the song's events in milliseconds.  A song is
 *      rendered in blocks of BLOCK_FRAMES frames; before each block the
 *      events that are due are applied and the end is detected, after
 *      it the song time moves on and a loop wraps.
 *   3. Mixing: the current song and (while crossfading or fading out)
 *      the outgoing one, each with its own linear gain ramp, then the
 *      master volume ramp.
 *   4. The optional graylib audio stream fed by lrg_midi_synth_update().
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include <graylib.h>

#include "lrg-midi-synth.h"
#include "lrg-midi-synth-private.h"
#include "../lrg-log.h"

/* frames rendered between two event checks (TinySoundFont's own block) */
#define BLOCK_FRAMES        (64)

/* seconds a volume change takes */
#define VOLUME_RAMP_SECONDS (0.05)

/* a finished non-looping song stops waiting for its release tails */
#define TAIL_LIMIT_MS       (5000.0)

/* SoundFont 2 generator operators the validator follows */
#define SF2_GEN_INSTRUMENT  (41)
#define SF2_GEN_SAMPLE_ID   (53)

/* bytes per record of the nine "pdta" tables (SoundFont 2.01, 7.2 - 7.10) */
#define SF2_PHDR_SIZE       (38)
#define SF2_PBAG_SIZE       (4)
#define SF2_PMOD_SIZE       (10)
#define SF2_PGEN_SIZE       (4)
#define SF2_INST_SIZE       (22)
#define SF2_IBAG_SIZE       (4)
#define SF2_IMOD_SIZE       (10)
#define SF2_IGEN_SIZE       (4)
#define SF2_SHDR_SIZE       (46)

/* the nine tables, in a fixed order */
enum
{
    HYDRA_PHDR,
    HYDRA_PBAG,
    HYDRA_PMOD,
    HYDRA_PGEN,
    HYDRA_INST,
    HYDRA_IBAG,
    HYDRA_IMOD,
    HYDRA_IGEN,
    HYDRA_SHDR,
    HYDRA_COUNT
};

static const struct
{
    const gchar *id;
    guint        record_size;
} hydra_tables[HYDRA_COUNT] = {
    { "phdr", SF2_PHDR_SIZE },
    { "pbag", SF2_PBAG_SIZE },
    { "pmod", SF2_PMOD_SIZE },
    { "pgen", SF2_PGEN_SIZE },
    { "inst", SF2_INST_SIZE },
    { "ibag", SF2_IBAG_SIZE },
    { "imod", SF2_IMOD_SIZE },
    { "igen", SF2_IGEN_SIZE },
    { "shdr", SF2_SHDR_SIZE }
};

/* where one table lies in the file */
typedef struct
{
    const guint8 *data;
    guint32       count;
    gboolean      present;
} Sf2Table;

/* one tempo change of a MIDI file, in ticks */
typedef struct
{
    guint64 tick;
    guint32 usec_per_quarter;
} SmfTempo;

/* what the MIDI scan learnt about a song */
typedef struct
{
    guint    division;          /* ticks per quarter note */
    GArray  *tempos;            /* SmfTempo, sorted by tick */
    gint64   loop_start_tick;   /* -1 without a loopStart marker */
    gint64   loop_end_tick;     /* -1 without a loopEnd marker */
    guint64  end_tick;          /* the latest event or end of track */
} SmfScan;

/* one song being played */
typedef struct
{
    LrgMidiSynthVoices *voices;
    GArray             *events;      /* LrgMidiSynthEvent, by time */
    guint               cursor;      /* next event to apply */
    gdouble             time_ms;     /* song time */
    gdouble             length_ms;   /* the end (last event / end of track) */
    gdouble             loop_start_ms;
    gdouble             loop_end_ms;
    gboolean            loop;
    gboolean            finished;
    gdouble             tail_ms;     /* time spent after the last event */
    gfloat              gain;        /* crossfade gain, 0 .. 1 */
    gfloat              gain_target;
    gfloat              gain_step;   /* per frame, >= 0 */
} Song;

struct _LrgMidiSynth
{
    GObject           parent_instance;

    guint             sample_rate;
    LrgMidiSynthFont *font;          /* nullable */
    guint             preset_count;

    Song             *current;       /* nullable */
    Song             *outgoing;      /* nullable: fading out */

    gfloat            volume;        /* target */
    gfloat            volume_now;    /* ramped */
    gfloat            volume_step;   /* per frame */

    gfloat           *scratch;       /* 2 x BLOCK_FRAMES */
    gfloat           *stream_buffer; /* 2 x LRG_MIDI_SYNTH_STREAM_FRAMES */
    GrlAudioStream   *stream;        /* nullable */
};

G_DEFINE_FINAL_TYPE (LrgMidiSynth, lrg_midi_synth, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-midi-synth-error-quark, lrg_midi_synth_error)

enum
{
    PROP_0,
    PROP_SAMPLE_RATE,
    PROP_VOLUME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Little-endian readers (callers check the bounds)
 * ========================================================================== */

static guint16
read_u16le (const guint8 *p)
{
    return (guint16)((guint16)p[0] | (guint16)((guint16)p[1] << 8));
}

static guint32
read_u32le (const guint8 *p)
{
    return (guint32)p[0] | ((guint32)p[1] << 8) |
           ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static guint32
read_u32be (const guint8 *p)
{
    return ((guint32)p[0] << 24) | ((guint32)p[1] << 16) |
           ((guint32)p[2] << 8) | (guint32)p[3];
}

/* a RIFF four-character code TinySoundFont accepts (first byte in ' '..'z') */
static gboolean
fourcc_is_valid (const guint8 *id)
{
    return id[0] > ' ' && id[0] < 'z';
}

/* ==========================================================================
 * SoundFont 2 validation
 * ========================================================================== */

/*
 * sf2_invalid:
 * Sets LRG_MIDI_SYNTH_ERROR_INVALID with a reason and returns FALSE.
 */
static gboolean
sf2_invalid (GError      **error,
             const gchar  *reason)
{
    g_set_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID,
                 "invalid SoundFont: %s", reason);
    return FALSE;
}

/*
 * sf2_walk_pdta:
 * Records the nine hydra tables of a "pdta" list body.  Every table must
 * appear once with a whole number of records; other chunks are skipped
 * as TinySoundFont skips them.
 */
static gboolean
sf2_walk_pdta (const guint8  *body,
               gsize          size,
               Sf2Table      *tables,
               GError       **error)
{
    gsize pos;

    pos = 0;
    while (size - pos >= 8)
    {
        const guint8 *id;
        guint32       chunk_size;
        guint         t;

        id = body + pos;
        chunk_size = read_u32le (body + pos + 4);
        if (!fourcc_is_valid (id))
            return sf2_invalid (error, "bad chunk id in pdta");
        if ((gsize)chunk_size > size - pos - 8)
            return sf2_invalid (error, "pdta chunk overruns its list");
        if (memcmp (id, "RIFF", 4) == 0)
            return sf2_invalid (error, "nested RIFF chunk");

        for (t = 0; t < HYDRA_COUNT; t++)
        {
            if (memcmp (id, hydra_tables[t].id, 4) != 0)
                continue;
            if (tables[t].present)
                return sf2_invalid (error, "duplicate pdta table");
            if (chunk_size % hydra_tables[t].record_size != 0)
                return sf2_invalid (error, "pdta table size is not a whole number of records");
            tables[t].present = TRUE;
            tables[t].data = body + pos + 8;
            tables[t].count = chunk_size / hydra_tables[t].record_size;
        }
        pos += 8 + (gsize)chunk_size;
    }
    return TRUE;
}

/*
 * sf2_walk_sdta:
 * Finds the sample pool: the first "smpl" chunk of at least one sample,
 * as TinySoundFont takes it.
 */
static gboolean
sf2_walk_sdta (const guint8  *body,
               gsize          size,
               guint32       *sample_count,
               GError       **error)
{
    gsize pos;

    pos = 0;
    while (size - pos >= 8)
    {
        const guint8 *id;
        guint32       chunk_size;

        id = body + pos;
        chunk_size = read_u32le (body + pos + 4);
        if (!fourcc_is_valid (id))
            return sf2_invalid (error, "bad chunk id in sdta");
        if ((gsize)chunk_size > size - pos - 8)
            return sf2_invalid (error, "sdta chunk overruns its list");
        if (memcmp (id, "RIFF", 4) == 0)
            return sf2_invalid (error, "nested RIFF chunk");
        if (memcmp (id, "smpl", 4) == 0 && *sample_count == 0 && chunk_size >= 2)
            *sample_count = chunk_size / 2;
        pos += 8 + (gsize)chunk_size;
    }
    return TRUE;
}

/*
 * sf2_check_ascending:
 * The bag/generator index field at @offset of every record of @table
 * must not decrease, and the last one (the terminal record's) must not
 * exceed @limit.  TinySoundFont walks [index[i], index[i + 1]) of each
 * record without checks, and reads the record after each bag, so these
 * two rules keep every access inside the next table.
 */
static gboolean
sf2_check_ascending (const Sf2Table  *table,
                     guint            record_size,
                     guint            offset,
                     guint32          limit,
                     const gchar     *what,
                     GError         **error)
{
    guint32 previous;
    guint32 i;

    previous = 0;
    for (i = 0; i < table->count; i++)
    {
        guint32 index;

        index = read_u16le (table->data + (gsize)i * record_size + offset);
        if (index < previous)
        {
            g_set_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID,
                         "invalid SoundFont: %s indices decrease", what);
            return FALSE;
        }
        previous = index;
    }
    if (previous > limit)
    {
        g_set_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID,
                     "invalid SoundFont: %s index %u out of range", what,
                     (guint)previous);
        return FALSE;
    }
    return TRUE;
}

/*
 * sf2_check_generators:
 * Generators that point into another table (a preset's instrument, an
 * instrument's sample) must point at a real record, never at the
 * terminal one TinySoundFont would read past.
 */
static gboolean
sf2_check_generators (const Sf2Table  *table,
                      guint16          operator_id,
                      guint32          limit,
                      const gchar     *what,
                      GError         **error)
{
    guint32 i;

    for (i = 0; i < table->count; i++)
    {
        const guint8 *record;

        record = table->data + (gsize)i * SF2_PGEN_SIZE;
        if (read_u16le (record) != operator_id)
            continue;
        if ((guint32)read_u16le (record + 2) >= limit)
        {
            g_set_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID,
                         "invalid SoundFont: %s %u out of range", what,
                         (guint)read_u16le (record + 2));
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * sf2_validate:
 * The whole structure check; on success @sample_count is the size of
 * the sample pool in samples.
 */
static gboolean
sf2_validate (const guint8  *data,
              gsize          size,
              guint32       *sample_count,
              GError       **error)
{
    Sf2Table tables[HYDRA_COUNT];
    gsize    riff_end;
    gsize    pos;
    guint32  riff_size;
    guint    t;

    memset (tables, 0, sizeof (tables));
    *sample_count = 0;

    /* 1. the RIFF "sfbk" header and its declared size */
    if (size < 12 || memcmp (data, "RIFF", 4) != 0 || memcmp (data + 8, "sfbk", 4) != 0)
        return sf2_invalid (error, "not a RIFF sfbk file");
    riff_size = read_u32le (data + 4);
    if (riff_size < 4 || (gsize)riff_size > size - 8)
        return sf2_invalid (error, "the RIFF size exceeds the file");
    riff_end = 8 + (gsize)riff_size;

    /* 2. the top level lists: pdta (tables), sdta (samples), others skipped */
    pos = 12;
    while (riff_end - pos >= 8)
    {
        const guint8 *id;
        guint32       chunk_size;

        id = data + pos;
        chunk_size = read_u32le (data + pos + 4);
        if (!fourcc_is_valid (id))
            return sf2_invalid (error, "bad chunk id");
        if ((gsize)chunk_size > riff_end - pos - 8)
            return sf2_invalid (error, "a chunk overruns the file");
        if (memcmp (id, "RIFF", 4) == 0)
            return sf2_invalid (error, "nested RIFF chunk");
        if (memcmp (id, "LIST", 4) == 0)
        {
            const guint8 *list_id;

            if (chunk_size < 4)
                return sf2_invalid (error, "LIST chunk without a type");
            list_id = data + pos + 8;
            if (!fourcc_is_valid (list_id))
                return sf2_invalid (error, "bad LIST type");
            if (memcmp (list_id, "pdta", 4) == 0 &&
                !sf2_walk_pdta (data + pos + 12, chunk_size - 4, tables, error))
                return FALSE;
            if (memcmp (list_id, "sdta", 4) == 0 &&
                !sf2_walk_sdta (data + pos + 12, chunk_size - 4, sample_count, error))
                return FALSE;
        }
        pos += 8 + (gsize)chunk_size;
    }

    /* 3. every table, each with its terminal record; a sample pool */
    for (t = 0; t < HYDRA_COUNT; t++)
    {
        if (!tables[t].present || tables[t].count == 0)
        {
            g_set_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID,
                         "invalid SoundFont: missing or empty %s table",
                         hydra_tables[t].id);
            return FALSE;
        }
    }
    if (tables[HYDRA_PHDR].count < 2 || tables[HYDRA_INST].count < 2 ||
        tables[HYDRA_SHDR].count < 2)
        return sf2_invalid (error, "no presets, instruments or samples");
    if (*sample_count < 2)
        return sf2_invalid (error, "no sample data");

    /* 4. the index chains preset -> bag -> generator -> instrument -> ... */
    if (!sf2_check_ascending (&tables[HYDRA_PHDR], SF2_PHDR_SIZE, 24,
                              tables[HYDRA_PBAG].count - 1, "preset bag", error) ||
        !sf2_check_ascending (&tables[HYDRA_PBAG], SF2_PBAG_SIZE, 0,
                              tables[HYDRA_PGEN].count, "preset generator", error) ||
        !sf2_check_ascending (&tables[HYDRA_INST], SF2_INST_SIZE, 20,
                              tables[HYDRA_IBAG].count - 1, "instrument bag", error) ||
        !sf2_check_ascending (&tables[HYDRA_IBAG], SF2_IBAG_SIZE, 0,
                              tables[HYDRA_IGEN].count, "instrument generator", error) ||
        !sf2_check_generators (&tables[HYDRA_PGEN], SF2_GEN_INSTRUMENT,
                               tables[HYDRA_INST].count - 1, "instrument", error) ||
        !sf2_check_generators (&tables[HYDRA_IGEN], SF2_GEN_SAMPLE_ID,
                               tables[HYDRA_SHDR].count - 1, "sample", error))
        return FALSE;

    return TRUE;
}

/* ==========================================================================
 * Standard MIDI File scan
 * ========================================================================== */

static gboolean
smf_invalid (GError      **error,
             const gchar  *reason)
{
    g_set_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID,
                 "invalid MIDI file: %s", reason);
    return FALSE;
}

/* a variable-length quantity of at most four bytes */
static gboolean
smf_read_varlen (const guint8 *data,
                 gsize         size,
                 gsize        *pos,
                 guint32      *out)
{
    guint32 value;
    guint   i;

    value = 0;
    for (i = 0; i < 4; i++)
    {
        guint8 c;

        if (*pos >= size)
            return FALSE;
        c = data[(*pos)++];
        value = (value << 7) | (guint32)(c & 0x7f);
        if ((c & 0x80) == 0)
        {
            *out = value;
            return TRUE;
        }
    }
    return FALSE;
}

static gint
compare_tempo (gconstpointer a,
               gconstpointer b)
{
    const SmfTempo *ta = a;
    const SmfTempo *tb = b;

    if (ta->tick < tb->tick)
        return -1;
    return ta->tick > tb->tick ? 1 : 0;
}

/*
 * smf_scan_track:
 * Walks one track's events with running status.  Delta times above
 * 2^20 ticks count as 0 (TinyMidiLoader's rule for broken files).
 * Records tempo changes, the loop markers and the track's last tick.
 */
static gboolean
smf_scan_track (const guint8  *track,
                gsize          size,
                SmfScan       *scan,
                GError       **error)
{
    guint64 tick;
    gsize   pos;
    gint    status;

    tick = 0;
    pos = 0;
    status = -1;
    while (pos < size)
    {
        guint32 delta;
        gint    byte;

        if (!smf_read_varlen (track, size, &pos, &delta))
            return smf_invalid (error, "bad delta time");
        if (delta & 0xfff00000u)
            delta = 0;
        tick += delta;
        if (pos >= size)
            return smf_invalid (error, "event without a status");

        byte = track[pos];
        if (byte & 0x80)
            pos++;
        else if (status < 0)
            return smf_invalid (error, "running status without a status");
        else
            byte = status;

        if (byte == 0xff)
        {
            /* meta event: type, length, data */
            guint8  type;
            guint32 length;

            if (pos >= size)
                return smf_invalid (error, "truncated meta event");
            type = track[pos++];
            if (!smf_read_varlen (track, size, &pos, &length) ||
                (gsize)length > size - pos)
                return smf_invalid (error, "truncated meta event");
            if (type == 0x2f)
            {
                if (length != 0)
                    return smf_invalid (error, "end of track with data");
                if (tick > scan->end_tick)
                    scan->end_tick = tick;
                return TRUE;
            }
            if (type == 0x51)
            {
                SmfTempo tempo;

                if (length != 3)
                    return smf_invalid (error, "tempo event of a wrong length");
                tempo.tick = tick;
                tempo.usec_per_quarter = ((guint32)track[pos] << 16) |
                                         ((guint32)track[pos + 1] << 8) |
                                         (guint32)track[pos + 2];
                g_array_append_val (scan->tempos, tempo);
            }
            if (type == 0x06)
            {
                if (length == 9 && g_ascii_strncasecmp ((const gchar *)track + pos, "loopStart", 9) == 0 &&
                    scan->loop_start_tick < 0)
                    scan->loop_start_tick = (gint64)tick;
                if (length == 7 && g_ascii_strncasecmp ((const gchar *)track + pos, "loopEnd", 7) == 0 &&
                    scan->loop_end_tick < 0)
                    scan->loop_end_tick = (gint64)tick;
            }
            pos += length;
            status = -1;
        }
        else if (byte == 0xf0 || byte == 0xf7)
        {
            /* SysEx: length and data, ignored */
            guint32 length;

            if (!smf_read_varlen (track, size, &pos, &length) ||
                (gsize)length > size - pos)
                return smf_invalid (error, "truncated SysEx event");
            pos += length;
            status = -1;
        }
        else if (byte >= 0xf0)
        {
            return smf_invalid (error, "system message in a track");
        }
        else
        {
            /* channel message: one data byte for program / channel pressure */
            gsize needed;

            status = byte;
            needed = ((byte & 0xf0) == 0xc0 || (byte & 0xf0) == 0xd0) ? 1 : 2;
            if (size - pos < needed)
                return smf_invalid (error, "truncated channel message");
            pos += needed;
        }
        if (tick > scan->end_tick)
            scan->end_tick = tick;
    }
    return TRUE;
}

/*
 * smf_scan:
 * Checks the header and every track of a Standard MIDI File and fills
 * @scan.  Format 0 and 1 (2 is accepted as TinyMidiLoader accepts it,
 * playing the tracks together); SMPTE time division is refused.
 */
static gboolean
smf_scan (const guint8  *data,
          gsize          size,
          SmfScan       *scan,
          GError       **error)
{
    guint  format;
    guint  n_tracks;
    guint  division;
    guint  track;
    gsize  pos;

    scan->tempos = g_array_new (FALSE, FALSE, sizeof (SmfTempo));
    scan->loop_start_tick = -1;
    scan->loop_end_tick = -1;
    scan->end_tick = 0;
    scan->division = 0;

    if (size < 14 || memcmp (data, "MThd", 4) != 0 || read_u32be (data + 4) != 6)
        return smf_invalid (error, "no MThd header");
    format = ((guint)data[8] << 8) | data[9];
    n_tracks = ((guint)data[10] << 8) | data[11];
    division = ((guint)data[12] << 8) | data[13];
    if (format > 2)
        return smf_invalid (error, "unknown format");
    if (n_tracks == 0)
        return smf_invalid (error, "no tracks");
    if (division == 0 || (division & 0x8000) != 0)
        return smf_invalid (error, "unsupported time division");
    scan->division = division;

    pos = 14;
    for (track = 0; track < n_tracks; track++)
    {
        guint32 length;

        if (size - pos < 8 || memcmp (data + pos, "MTrk", 4) != 0)
            return smf_invalid (error, "missing MTrk chunk");
        length = read_u32be (data + pos + 4);
        if (length > 0x7fffffffu || (gsize)length > size - pos - 8)
            return smf_invalid (error, "track overruns the file");
        if (!smf_scan_track (data + pos + 8, length, scan, error))
            return FALSE;
        pos += 8 + (gsize)length;
    }

    g_array_sort (scan->tempos, compare_tempo);
    return TRUE;
}

/*
 * smf_tick_to_ms:
 * The tempo map as TinyMidiLoader applies it: 120 bpm until the first
 * tempo event, each segment at its tempo.
 */
static gdouble
smf_tick_to_ms (const SmfScan *scan,
                guint64        tick)
{
    gdouble ms_per_tick;
    gdouble ms;
    guint64 last;
    guint   i;

    ms_per_tick = 500000.0 / (1000.0 * (gdouble)scan->division);
    ms = 0.0;
    last = 0;
    for (i = 0; i < scan->tempos->len; i++)
    {
        const SmfTempo *tempo;

        tempo = &g_array_index (scan->tempos, SmfTempo, i);
        if (tempo->tick > tick)
            break;
        ms += (gdouble)(tempo->tick - last) * ms_per_tick;
        last = tempo->tick;
        ms_per_tick = (gdouble)tempo->usec_per_quarter / (1000.0 * (gdouble)scan->division);
    }
    return ms + (gdouble)(tick - last) * ms_per_tick;
}

/* ==========================================================================
 * Songs
 * ========================================================================== */

static void
song_free (Song *song)
{
    if (song == NULL)
        return;
    lrg_midi_synth_voices_free (song->voices);
    if (song->events != NULL)
        g_array_unref (song->events);
    g_free (song);
}

/* the index of the first event at or after @time_ms */
static guint
song_find_event (const Song *song,
                 gdouble     time_ms)
{
    guint low;
    guint high;

    low = 0;
    high = song->events->len;
    while (low < high)
    {
        guint mid;

        mid = low + (high - low) / 2;
        if ((gdouble)g_array_index (song->events, LrgMidiSynthEvent, mid).time_ms < time_ms)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}

/* sets a linear ramp of the song's gain to @target over @seconds */
static void
song_fade (Song   *song,
           gfloat  target,
           gdouble seconds,
           guint   sample_rate)
{
    gdouble frames;

    song->gain_target = target;
    frames = seconds * (gdouble)sample_rate;
    if (frames < 1.0)
    {
        song->gain = target;
        song->gain_step = 0.0f;
        return;
    }
    song->gain_step = (gfloat)(fabs ((gdouble)target - (gdouble)song->gain) / frames);
}

/* the loop's end: its marker, else the song's end */
static gboolean
song_loop_range (const Song *song,
                 gdouble    *start,
                 gdouble    *end)
{
    if (!song->loop || song->loop_end_ms <= song->loop_start_ms)
        return FALSE;
    *start = song->loop_start_ms;
    *end = song->loop_end_ms;
    return TRUE;
}

/* applies every event due at or before @until_ms */
static void
song_apply_until (Song    *song,
                  gdouble  until_ms)
{
    while (song->cursor < song->events->len &&
           (gdouble)g_array_index (song->events, LrgMidiSynthEvent, song->cursor).time_ms <= until_ms)
    {
        lrg_midi_synth_voices_apply (song->voices,
                                     &g_array_index (song->events, LrgMidiSynthEvent, song->cursor));
        song->cursor++;
    }
}

/*
 * song_wrap:
 * After the song time moved: a looping song past its loop end applies
 * the events up to the end (controllers and program changes must not be
 * lost), releases the sounding notes and continues at the same offset
 * after the loop start; the events from the loop start on play again.
 */
static void
song_wrap (Song *song)
{
    gdouble loop_start;
    gdouble loop_end;

    if (!song_loop_range (song, &loop_start, &loop_end) || song->time_ms < loop_end)
        return;

    song_apply_until (song, loop_end);
    lrg_midi_synth_voices_release_all (song->voices);
    song->time_ms = loop_start + fmod (song->time_ms - loop_end, loop_end - loop_start);
    song->cursor = song_find_event (song, loop_start);
}

/*
 * song_advance:
 * Before a block: applies the events that are due and detects the end
 * of a song that does not loop (its last event played, every note died
 * away, or TAIL_LIMIT_MS passed since).
 */
static void
song_advance (Song *song)
{
    song_apply_until (song, song->time_ms);

    if (song->loop || song->finished || song->cursor < song->events->len)
        return;
    if (song->time_ms < song->length_ms)
        return;
    if (lrg_midi_synth_voices_get_active (song->voices) == 0 || song->tail_ms >= TAIL_LIMIT_MS)
    {
        lrg_midi_synth_voices_release_all (song->voices);
        song->finished = TRUE;
    }
}

/*
 * song_mix:
 * Renders @n_frames (<= BLOCK_FRAMES) of @song into @scratch and adds
 * them to @out with the song's gain ramp, then moves the song time (and
 * wraps a loop).  Returns FALSE when the song faded out completely (its
 * owner frees it).
 */
static gboolean
song_mix (Song   *song,
          gfloat *scratch,
          gfloat *out,
          guint   n_frames,
          guint   sample_rate)
{
    gdouble block_ms;
    guint   i;

    song_advance (song);
    lrg_midi_synth_voices_render (song->voices, scratch, n_frames);

    for (i = 0; i < n_frames; i++)
    {
        out[2 * i] += scratch[2 * i] * song->gain;
        out[2 * i + 1] += scratch[2 * i + 1] * song->gain;

        /* the crossfade ramp, one step per frame */
        if (song->gain < song->gain_target)
            song->gain = MIN (song->gain + song->gain_step, song->gain_target);
        else if (song->gain > song->gain_target)
            song->gain = MAX (song->gain - song->gain_step, song->gain_target);
    }

    block_ms = (gdouble)n_frames * 1000.0 / (gdouble)sample_rate;
    if (!song->finished)
    {
        song->time_ms += block_ms;
        song_wrap (song);
    }
    if (song->cursor >= song->events->len && !song->loop)
        song->tail_ms += block_ms;

    return !(song->gain_target <= 0.0f && song->gain <= 0.0f);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_midi_synth_finalize (GObject *object)
{
    LrgMidiSynth *self = LRG_MIDI_SYNTH (object);

    lrg_midi_synth_stop_stream (self);
    g_clear_pointer (&self->current, song_free);
    g_clear_pointer (&self->outgoing, song_free);
    g_clear_pointer (&self->font, lrg_midi_synth_font_free);
    g_clear_pointer (&self->scratch, g_free);
    g_clear_pointer (&self->stream_buffer, g_free);

    G_OBJECT_CLASS (lrg_midi_synth_parent_class)->finalize (object);
}

static void
lrg_midi_synth_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgMidiSynth *self = LRG_MIDI_SYNTH (object);

    switch (prop_id)
    {
    case PROP_SAMPLE_RATE:
        g_value_set_uint (value, self->sample_rate);
        break;
    case PROP_VOLUME:
        g_value_set_float (value, self->volume);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_midi_synth_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgMidiSynth *self = LRG_MIDI_SYNTH (object);

    switch (prop_id)
    {
    case PROP_SAMPLE_RATE:
        self->sample_rate = g_value_get_uint (value);
        break;
    case PROP_VOLUME:
        lrg_midi_synth_set_volume (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_midi_synth_constructed (GObject *object)
{
    LrgMidiSynth *self = LRG_MIDI_SYNTH (object);

    G_OBJECT_CLASS (lrg_midi_synth_parent_class)->constructed (object);

    /* the volume ramp takes VOLUME_RAMP_SECONDS from silence to full */
    self->volume_step = (gfloat)(1.0 / (VOLUME_RAMP_SECONDS * (gdouble)self->sample_rate));
}

static void
lrg_midi_synth_class_init (LrgMidiSynthClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_midi_synth_finalize;
    object_class->get_property = lrg_midi_synth_get_property;
    object_class->set_property = lrg_midi_synth_set_property;
    object_class->constructed = lrg_midi_synth_constructed;

    /**
     * LrgMidiSynth:sample-rate:
     *
     * Output rate in Hz.
     */
    properties[PROP_SAMPLE_RATE] =
        g_param_spec_uint ("sample-rate", "Sample Rate", "Output rate in Hz",
                           8000, 192000, LRG_MIDI_SYNTH_DEFAULT_SAMPLE_RATE,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgMidiSynth:volume:
     *
     * Output volume, 0 .. 1 (applied as a short ramp).
     */
    properties[PROP_VOLUME] =
        g_param_spec_float ("volume", "Volume", "Output volume",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_midi_synth_init (LrgMidiSynth *self)
{
    self->sample_rate = LRG_MIDI_SYNTH_DEFAULT_SAMPLE_RATE;
    self->volume = 1.0f;
    self->volume_now = 1.0f;
    self->scratch = g_new0 (gfloat, 2 * BLOCK_FRAMES);
}

/* ==========================================================================
 * Construction and SoundFonts
 * ========================================================================== */

/*
 * lrg_midi_synth_new:
 * Documented in the header.
 */
LrgMidiSynth *
lrg_midi_synth_new (guint sample_rate)
{
    g_return_val_if_fail (sample_rate >= 8000 && sample_rate <= 192000, NULL);

    return g_object_new (LRG_TYPE_MIDI_SYNTH, "sample-rate", sample_rate, NULL);
}

/*
 * lrg_midi_synth_load_soundfont:
 * Reads the file through GIO (errors in G_IO_ERROR) and loads it.
 */
gboolean
lrg_midi_synth_load_soundfont (LrgMidiSynth  *self,
                               const gchar   *path,
                               GError       **error)
{
    g_autoptr(GFile)  file = NULL;
    g_autoptr(GBytes) bytes = NULL;

    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    file = g_file_new_for_path (path);
    bytes = g_file_load_bytes (file, NULL, NULL, error);
    if (bytes == NULL)
        return FALSE;
    return lrg_midi_synth_load_soundfont_bytes (self, bytes, error);
}

/*
 * lrg_midi_synth_load_soundfont_bytes:
 * Validate, load, then swap: the old bank and its songs go only when
 * the new bank loaded.
 */
gboolean
lrg_midi_synth_load_soundfont_bytes (LrgMidiSynth  *self,
                                     GBytes        *sf2,
                                     GError       **error)
{
    LrgMidiSynthFont *font;
    const guint8     *data;
    gsize             size;
    guint32           sample_count;

    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), FALSE);
    g_return_val_if_fail (sf2 != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    data = g_bytes_get_data (sf2, &size);
    if (data == NULL || size == 0)
        return sf2_invalid (error, "empty file");
    if (size > (gsize)G_MAXINT)
        return sf2_invalid (error, "file too large");
    if (!sf2_validate (data, size, &sample_count, error))
        return FALSE;

    font = lrg_midi_synth_font_load (data, size, sample_count, self->sample_rate);
    if (font == NULL)
        return sf2_invalid (error, "TinySoundFont rejected the bank");

    g_clear_pointer (&self->current, song_free);
    g_clear_pointer (&self->outgoing, song_free);
    g_clear_pointer (&self->font, lrg_midi_synth_font_free);
    self->font = font;
    self->preset_count = (guint)MAX (lrg_midi_synth_font_get_preset_count (font), 0);

    lrg_debug (LRG_LOG_DOMAIN_AUDIO, "MIDI synth: SoundFont with %u presets, %u samples",
               self->preset_count, (guint)sample_count);
    return TRUE;
}

gboolean
lrg_midi_synth_has_soundfont (LrgMidiSynth *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), FALSE);

    return self->font != NULL;
}

guint
lrg_midi_synth_get_preset_count (LrgMidiSynth *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), 0);

    return self->font != NULL ? self->preset_count : 0;
}

guint
lrg_midi_synth_get_sample_rate (LrgMidiSynth *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), 0);

    return self->sample_rate;
}

/* ==========================================================================
 * Playback
 * ========================================================================== */

/*
 * lrg_midi_synth_play:
 * 1. scan the file (errors leave everything as it was);
 * 2. load its events and make a synthesizer instance;
 * 3. the loop range from the markers (or the whole song);
 * 4. swap: with a fade the current song becomes the outgoing one (an
 *    older outgoing song is dropped), without one everything else stops.
 */
gboolean
lrg_midi_synth_play (LrgMidiSynth  *self,
                     GBytes        *smf,
                     gboolean       loop,
                     gdouble        fade_in,
                     GError       **error)
{
    SmfScan       scan;
    Song         *song;
    GArray       *events;
    const guint8 *data;
    gsize         size;
    gboolean      ok;

    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), FALSE);
    g_return_val_if_fail (smf != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->font == NULL)
    {
        g_set_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_NO_SOUNDFONT,
                     "no SoundFont loaded");
        return FALSE;
    }

    /* 1. scan */
    data = g_bytes_get_data (smf, &size);
    memset (&scan, 0, sizeof (scan));
    if (data == NULL || size == 0)
        ok = smf_invalid (error, "empty file");
    else if (size > (gsize)G_MAXINT)
        ok = smf_invalid (error, "file too large");
    else
        ok = smf_scan (data, size, &scan, error);
    if (!ok)
    {
        if (scan.tempos != NULL)
            g_array_unref (scan.tempos);
        return FALSE;
    }

    /* 2. events and voices */
    events = lrg_midi_synth_events_load (data, size);
    if (events == NULL)
    {
        g_array_unref (scan.tempos);
        return smf_invalid (error, "TinyMidiLoader rejected the file");
    }

    song = g_new0 (Song, 1);
    song->events = events;
    song->voices = lrg_midi_synth_voices_new (self->font);
    song->loop = loop;
    song->gain = 1.0f;
    song->gain_target = 1.0f;

    /* 3. the song's end and its loop */
    song->length_ms = smf_tick_to_ms (&scan, scan.end_tick);
    if (events->len > 0)
        song->length_ms = MAX (song->length_ms,
                               (gdouble)g_array_index (events, LrgMidiSynthEvent, events->len - 1).time_ms);
    song->loop_start_ms = scan.loop_start_tick >= 0 ?
                          smf_tick_to_ms (&scan, (guint64)scan.loop_start_tick) : 0.0;
    song->loop_end_ms = scan.loop_end_tick >= 0 ?
                        smf_tick_to_ms (&scan, (guint64)scan.loop_end_tick) : song->length_ms;
    if (song->loop_end_ms <= song->loop_start_ms)
    {
        /* markers out of order: loop the whole song */
        song->loop_start_ms = 0.0;
        song->loop_end_ms = song->length_ms;
    }
    g_array_unref (scan.tempos);

    if (song->voices == NULL)
    {
        song_free (song);
        return smf_invalid (error, "no synthesizer voices");
    }

    /* 4. swap */
    g_clear_pointer (&self->outgoing, song_free);
    if (fade_in > 0.0 && self->current != NULL)
    {
        self->outgoing = self->current;
        song_fade (self->outgoing, 0.0f, fade_in, self->sample_rate);
        song->gain = 0.0f;
        song_fade (song, 1.0f, fade_in, self->sample_rate);
    }
    else
    {
        g_clear_pointer (&self->current, song_free);
    }
    self->current = song;

    lrg_debug (LRG_LOG_DOMAIN_AUDIO,
               "MIDI synth: song of %u events, %.1f s, loop %s (%.1f .. %.1f s)",
               events->len, song->length_ms / 1000.0, loop ? "on" : "off",
               song->loop_start_ms / 1000.0, song->loop_end_ms / 1000.0);
    return TRUE;
}

/*
 * lrg_midi_synth_stop:
 * The song fades as the outgoing one; an older outgoing song goes now.
 */
void
lrg_midi_synth_stop (LrgMidiSynth *self,
                     gdouble       fade_out)
{
    g_return_if_fail (LRG_IS_MIDI_SYNTH (self));

    g_clear_pointer (&self->outgoing, song_free);
    if (self->current == NULL)
        return;
    if (fade_out > 0.0)
    {
        self->outgoing = self->current;
        self->current = NULL;
        song_fade (self->outgoing, 0.0f, fade_out, self->sample_rate);
        return;
    }
    g_clear_pointer (&self->current, song_free);
}

gboolean
lrg_midi_synth_is_playing (LrgMidiSynth *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), FALSE);

    return self->current != NULL && !self->current->finished;
}

gboolean
lrg_midi_synth_is_finished (LrgMidiSynth *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), FALSE);

    return self->current != NULL && self->current->finished;
}

void
lrg_midi_synth_set_volume (LrgMidiSynth *self,
                           gfloat        volume)
{
    g_return_if_fail (LRG_IS_MIDI_SYNTH (self));

    volume = CLAMP (volume, 0.0f, 1.0f);
    if (volume == self->volume)
        return;
    self->volume = volume;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VOLUME]);
}

gfloat
lrg_midi_synth_get_volume (LrgMidiSynth *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), 0.0f);

    return self->volume;
}

gdouble
lrg_midi_synth_get_position (LrgMidiSynth *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), 0.0);

    return self->current != NULL ? self->current->time_ms / 1000.0 : 0.0;
}

gboolean
lrg_midi_synth_get_loop_points (LrgMidiSynth *self,
                                gdouble      *loop_start,
                                gdouble      *loop_end)
{
    gdouble start;
    gdouble end;

    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), FALSE);

    if (self->current == NULL || !song_loop_range (self->current, &start, &end))
    {
        if (loop_start != NULL)
            *loop_start = 0.0;
        if (loop_end != NULL)
            *loop_end = 0.0;
        return FALSE;
    }
    if (loop_start != NULL)
        *loop_start = start / 1000.0;
    if (loop_end != NULL)
        *loop_end = end / 1000.0;
    return TRUE;
}

/*
 * lrg_midi_synth_render:
 * Block by block: clear, mix the current and the outgoing song, apply
 * the master volume ramp.  A fully faded outgoing song is freed.
 */
guint
lrg_midi_synth_render (LrgMidiSynth *self,
                       gfloat       *frames,
                       guint         n_frames)
{
    guint done;

    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), 0);
    g_return_val_if_fail (frames != NULL || n_frames == 0, 0);

    done = 0;
    while (done < n_frames)
    {
        gfloat *out;
        guint   block;
        guint   i;

        block = MIN (n_frames - done, (guint)BLOCK_FRAMES);
        out = frames + 2 * (gsize)done;
        memset (out, 0, sizeof (gfloat) * 2 * block);

        /* the current song only ever fades in: it is never dropped here */
        if (self->current != NULL)
            (void)song_mix (self->current, self->scratch, out, block, self->sample_rate);
        if (self->outgoing != NULL &&
            !song_mix (self->outgoing, self->scratch, out, block, self->sample_rate))
            g_clear_pointer (&self->outgoing, song_free);

        for (i = 0; i < block; i++)
        {
            if (self->volume_now < self->volume)
                self->volume_now = MIN (self->volume_now + self->volume_step, self->volume);
            else if (self->volume_now > self->volume)
                self->volume_now = MAX (self->volume_now - self->volume_step, self->volume);
            out[2 * i] *= self->volume_now;
            out[2 * i + 1] *= self->volume_now;
        }
        done += block;
    }
    return n_frames;
}

/* ==========================================================================
 * Audio device stream
 * ========================================================================== */

/*
 * lrg_midi_synth_start_stream:
 * raylib sizes a stream's two buffers from a process-wide default, which
 * is set to LRG_MIDI_SYNTH_STREAM_FRAMES so every buffer we write fills
 * one exactly (a shorter write would be padded with silence: audible
 * gaps).  raylib cannot report the previous default, so it stays at that
 * size for streams created later; it equals LrgProceduralAudio's write
 * size (4096 frames), which suits those streams as well.
 */
gboolean
lrg_midi_synth_start_stream (LrgMidiSynth  *self,
                             GError       **error)
{
    GrlAudioDevice *device;

    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->stream != NULL)
        return TRUE;

    device = grl_audio_device_get_default ();
    if (device == NULL || !grl_audio_device_is_ready (device))
    {
        g_set_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_NO_DEVICE,
                     "the audio device is not open");
        return FALSE;
    }

    grl_audio_stream_set_default_buffer_size (LRG_MIDI_SYNTH_STREAM_FRAMES);
    self->stream = grl_audio_stream_new (self->sample_rate, 32, 2);
    if (self->stream == NULL)
    {
        g_set_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_NO_DEVICE,
                     "could not open an audio stream");
        return FALSE;
    }

    if (self->stream_buffer == NULL)
        self->stream_buffer = g_new0 (gfloat, 2 * LRG_MIDI_SYNTH_STREAM_FRAMES);
    grl_audio_stream_play (self->stream);
    return TRUE;
}

/*
 * lrg_midi_synth_update:
 * At most both buffers per call (after a long frame both may be free).
 */
void
lrg_midi_synth_update (LrgMidiSynth *self)
{
    guint i;

    g_return_if_fail (LRG_IS_MIDI_SYNTH (self));

    if (self->stream == NULL)
        return;
    for (i = 0; i < 2 && grl_audio_stream_is_processed (self->stream); i++)
    {
        lrg_midi_synth_render (self, self->stream_buffer, LRG_MIDI_SYNTH_STREAM_FRAMES);
        grl_audio_stream_update (self->stream, self->stream_buffer,
                                 LRG_MIDI_SYNTH_STREAM_FRAMES);
    }
}

void
lrg_midi_synth_stop_stream (LrgMidiSynth *self)
{
    g_return_if_fail (LRG_IS_MIDI_SYNTH (self));

    if (self->stream == NULL)
        return;
    if (grl_audio_stream_is_playing (self->stream))
        grl_audio_stream_stop (self->stream);
    g_clear_object (&self->stream);
}

gboolean
lrg_midi_synth_has_stream (LrgMidiSynth *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SYNTH (self), FALSE);

    return self->stream != NULL;
}
