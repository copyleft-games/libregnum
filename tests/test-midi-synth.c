/* test-midi-synth.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgMidiSynth.  Everything runs headless through
 * lrg_midi_synth_render(): a tiny synthetic SoundFont (one looped sine
 * sample, one preset) and synthetic Standard MIDI Files are written here
 * byte by byte, so no data file is needed.  The device case
 * (lrg_midi_synth_start_stream()) opens the audio device only when
 * LRG_TEST_AUDIO_DEVICE=1 is set.  The malformed-input cases (and the
 * mutation sweep) are meant to run under the sanitizers too
 * (make test-sanitize): every one must fail with a GError, never crash
 * or read out of bounds.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#define RATE            (44100)
#define SINE_PERIOD     (441)       /* one cycle of 100 Hz at 44100 Hz */
#define SINE_CYCLES     (10)
#define SINE_PADDING    (46)        /* SoundFont rule: 46 zero samples after each */
#define DIVISION        (480)       /* ticks per quarter; 120 bpm: 500 ms per quarter */

/* ==========================================================================
 * Byte writers
 * ========================================================================== */

static void
put_u8 (GByteArray *out,
        guint       value)
{
    guint8 b = (guint8)value;

    g_byte_array_append (out, &b, 1);
}

static void
put_u16le (GByteArray *out,
           guint       value)
{
    put_u8 (out, value & 0xff);
    put_u8 (out, (value >> 8) & 0xff);
}

static void
put_u32le (GByteArray *out,
           guint32     value)
{
    put_u16le (out, value & 0xffff);
    put_u16le (out, (value >> 16) & 0xffff);
}

static void
put_u32be (GByteArray *out,
           guint32     value)
{
    put_u8 (out, (value >> 24) & 0xff);
    put_u8 (out, (value >> 16) & 0xff);
    put_u8 (out, (value >> 8) & 0xff);
    put_u8 (out, value & 0xff);
}

static void
put_id (GByteArray  *out,
        const gchar *id)
{
    g_byte_array_append (out, (const guint8 *)id, 4);
}

/* a fixed-size, zero padded name field */
static void
put_name (GByteArray  *out,
          const gchar *name,
          guint        size)
{
    guint i;
    gsize len;

    len = strlen (name);
    for (i = 0; i < size; i++)
        put_u8 (out, i < len ? (guint)(guchar)name[i] : 0);
}

/* starts a RIFF chunk, returns the offset of its size field */
static guint
chunk_begin (GByteArray  *out,
             const gchar *id,
             const gchar *list_type)
{
    guint at;

    put_id (out, id);
    at = out->len;
    put_u32le (out, 0);
    if (list_type != NULL)
        put_id (out, list_type);
    return at;
}

static void
chunk_end (GByteArray *out,
           guint       size_at)
{
    guint32 size;

    size = (guint32)(out->len - size_at - 4);
    out->data[size_at] = size & 0xff;
    out->data[size_at + 1] = (size >> 8) & 0xff;
    out->data[size_at + 2] = (size >> 16) & 0xff;
    out->data[size_at + 3] = (size >> 24) & 0xff;
}

/* ==========================================================================
 * A synthetic SoundFont
 * ========================================================================== */

/*
 * build_sf2:
 * One preset ("Sine", bank 0 program 0) -> one instrument -> one sample:
 * SINE_CYCLES cycles of a 100 Hz sine, looped as a whole (sample mode 1),
 * root key 60, so every note sustains while held.
 */
static GBytes *
build_sf2 (void)
{
    GByteArray *out;
    guint riff;
    guint list;
    guint sub;
    guint i;
    guint32 n_samples;

    n_samples = SINE_PERIOD * SINE_CYCLES;
    out = g_byte_array_new ();
    riff = chunk_begin (out, "RIFF", "sfbk");

    /* INFO: the version, which TinySoundFont skips */
    list = chunk_begin (out, "LIST", "INFO");
    sub = chunk_begin (out, "ifil", NULL);
    put_u16le (out, 2);
    put_u16le (out, 1);
    chunk_end (out, sub);
    chunk_end (out, list);

    /* sdta: the sine and its padding */
    list = chunk_begin (out, "LIST", "sdta");
    sub = chunk_begin (out, "smpl", NULL);
    for (i = 0; i < n_samples; i++)
    {
        gdouble v;

        v = sin (2.0 * G_PI * (gdouble)(i % SINE_PERIOD) / (gdouble)SINE_PERIOD);
        put_u16le (out, (guint)(guint16)(gint16)(v * 26000.0));
    }
    for (i = 0; i < SINE_PADDING; i++)
        put_u16le (out, 0);
    chunk_end (out, sub);
    chunk_end (out, list);

    /* pdta: the nine tables, each with its terminal record */
    list = chunk_begin (out, "LIST", "pdta");

    sub = chunk_begin (out, "phdr", NULL);
    put_name (out, "Sine", 20); put_u16le (out, 0); put_u16le (out, 0); put_u16le (out, 0);
    put_u32le (out, 0); put_u32le (out, 0); put_u32le (out, 0);
    put_name (out, "EOP", 20); put_u16le (out, 0); put_u16le (out, 0); put_u16le (out, 1);
    put_u32le (out, 0); put_u32le (out, 0); put_u32le (out, 0);
    chunk_end (out, sub);

    sub = chunk_begin (out, "pbag", NULL);
    put_u16le (out, 0); put_u16le (out, 0);
    put_u16le (out, 1); put_u16le (out, 0);
    chunk_end (out, sub);

    sub = chunk_begin (out, "pmod", NULL);
    for (i = 0; i < 10; i++)
        put_u8 (out, 0);
    chunk_end (out, sub);

    sub = chunk_begin (out, "pgen", NULL);
    put_u16le (out, 41); put_u16le (out, 0);    /* instrument 0 */
    put_u16le (out, 0); put_u16le (out, 0);     /* terminal */
    chunk_end (out, sub);

    sub = chunk_begin (out, "inst", NULL);
    put_name (out, "SineInst", 20); put_u16le (out, 0);
    put_name (out, "EOI", 20); put_u16le (out, 1);
    chunk_end (out, sub);

    sub = chunk_begin (out, "ibag", NULL);
    put_u16le (out, 0); put_u16le (out, 0);
    put_u16le (out, 2); put_u16le (out, 0);
    chunk_end (out, sub);

    sub = chunk_begin (out, "imod", NULL);
    for (i = 0; i < 10; i++)
        put_u8 (out, 0);
    chunk_end (out, sub);

    sub = chunk_begin (out, "igen", NULL);
    put_u16le (out, 54); put_u16le (out, 1);    /* sampleModes: loop */
    put_u16le (out, 53); put_u16le (out, 0);    /* sampleID 0 */
    put_u16le (out, 0); put_u16le (out, 0);     /* terminal */
    chunk_end (out, sub);

    sub = chunk_begin (out, "shdr", NULL);
    put_name (out, "Sine", 20);
    put_u32le (out, 0); put_u32le (out, n_samples);
    put_u32le (out, 0); put_u32le (out, n_samples);
    put_u32le (out, RATE);
    put_u8 (out, 60); put_u8 (out, 0); put_u16le (out, 0); put_u16le (out, 1);
    put_name (out, "EOS", 20);
    for (i = 0; i < 5; i++)
        put_u32le (out, 0);
    put_u8 (out, 0); put_u8 (out, 0); put_u16le (out, 0); put_u16le (out, 0);
    chunk_end (out, sub);

    chunk_end (out, list);
    chunk_end (out, riff);
    return g_byte_array_free_to_bytes (out);
}

/* the offset of a four-character code in @bytes (first match) */
static gsize
find_id (GBytes      *bytes,
         const gchar *id)
{
    const guint8 *data;
    gsize size;
    gsize i;

    data = g_bytes_get_data (bytes, &size);
    for (i = 0; i + 4 <= size; i++)
    {
        if (memcmp (data + i, id, 4) == 0)
            return i;
    }
    g_assert_not_reached ();
    return 0;
}

/* a copy of @bytes with the little-endian u16 at @offset replaced */
static GBytes *
patch_u16 (GBytes *bytes,
           gsize   offset,
           guint   value)
{
    guint8 *copy;
    gsize size;

    copy = g_memdup2 (g_bytes_get_data (bytes, &size), g_bytes_get_size (bytes));
    copy[offset] = value & 0xff;
    copy[offset + 1] = (value >> 8) & 0xff;
    return g_bytes_new_take (copy, size);
}

/* ==========================================================================
 * Synthetic MIDI files
 * ========================================================================== */

static void
put_varlen (GByteArray *out,
            guint32     value)
{
    guint8 buf[4];
    gint n;

    n = 0;
    buf[n++] = value & 0x7f;
    while ((value >>= 7) != 0)
        buf[n++] = (guint8)(0x80 | (value & 0x7f));
    while (n > 0)
        put_u8 (out, buf[--n]);
}

/* an event at @delta ticks: up to three status/data bytes */
static void
put_event (GByteArray *track,
           guint32     delta,
           guint       status,
           gint        data1,
           gint        data2)
{
    put_varlen (track, delta);
    put_u8 (track, status);
    if (data1 >= 0)
        put_u8 (track, (guint)data1);
    if (data2 >= 0)
        put_u8 (track, (guint)data2);
}

static void
put_meta (GByteArray  *track,
          guint32      delta,
          guint        type,
          const guint8 *data,
          guint        length)
{
    put_varlen (track, delta);
    put_u8 (track, 0xff);
    put_u8 (track, type);
    put_varlen (track, length);
    if (length > 0)
        g_byte_array_append (track, data, length);
}

static void
put_marker (GByteArray  *track,
            guint32      delta,
            const gchar *text)
{
    put_meta (track, delta, 0x06, (const guint8 *)text, (guint)strlen (text));
}

static void
put_end (GByteArray *track,
         guint32     delta)
{
    put_meta (track, delta, 0x2f, NULL, 0);
}

/* a format 1 file of the given tracks (consumed) */
static GBytes *
build_smf (GByteArray **tracks,
           guint        n_tracks)
{
    GByteArray *out;
    guint i;

    out = g_byte_array_new ();
    put_id (out, "MThd");
    put_u32be (out, 6);
    put_u8 (out, 0); put_u8 (out, 1);
    put_u8 (out, 0); put_u8 (out, n_tracks);
    put_u8 (out, DIVISION >> 8); put_u8 (out, DIVISION & 0xff);
    for (i = 0; i < n_tracks; i++)
    {
        put_id (out, "MTrk");
        put_u32be (out, tracks[i]->len);
        g_byte_array_append (out, tracks[i]->data, tracks[i]->len);
        g_byte_array_unref (tracks[i]);
    }
    return g_byte_array_free_to_bytes (out);
}

/*
 * build_note_song:
 * One note of @key held for @quarters quarter notes (500 ms each), with
 * optional loop markers at @loop_start / @loop_end quarters (-1 none).
 */
static GBytes *
build_note_song (guint key,
                 guint quarters,
                 gint  loop_start,
                 gint  loop_end)
{
    GByteArray *tracks[2];
    guint32 last;

    /* track 0: tempo and markers */
    tracks[0] = g_byte_array_new ();
    {
        const guint8 tempo[3] = { 0x07, 0xa1, 0x20 };   /* 500000 us */

        put_meta (tracks[0], 0, 0x51, tempo, 3);
    }
    last = 0;
    if (loop_start >= 0)
    {
        put_marker (tracks[0], (guint32)loop_start * DIVISION - last, "loopStart");
        last = (guint32)loop_start * DIVISION;
    }
    if (loop_end >= 0)
    {
        put_marker (tracks[0], (guint32)loop_end * DIVISION - last, "loopEnd");
        last = (guint32)loop_end * DIVISION;
    }
    put_end (tracks[0], quarters * DIVISION > last ? quarters * DIVISION - last : 0);

    /* track 1: the note (running status for the note off) */
    tracks[1] = g_byte_array_new ();
    put_event (tracks[1], 0, 0xc0, 0, -1);
    put_event (tracks[1], 0, 0xb0, 7, 127);
    put_event (tracks[1], 0, 0x90, (gint)key, 127);
    put_varlen (tracks[1], quarters * DIVISION);
    put_u8 (tracks[1], (guint)key);     /* running status 0x90, velocity 0 */
    put_u8 (tracks[1], 0);
    put_end (tracks[1], 0);

    return build_smf (tracks, 2);
}

/* a valid song with no notes at all, @quarters long */
static GBytes *
build_silent_song (guint quarters)
{
    GByteArray *tracks[1];

    tracks[0] = g_byte_array_new ();
    put_event (tracks[0], 0, 0xc0, 0, -1);
    put_end (tracks[0], quarters * DIVISION);
    return build_smf (tracks, 1);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgMidiSynth *
new_synth_with_font (void)
{
    g_autoptr(GBytes) sf2 = NULL;
    g_autoptr(GError) error = NULL;
    LrgMidiSynth *synth;

    synth = lrg_midi_synth_new (RATE);
    sf2 = build_sf2 ();
    g_assert_true (lrg_midi_synth_load_soundfont_bytes (synth, sf2, &error));
    g_assert_no_error (error);
    return synth;
}

/* renders @seconds and returns the RMS of the left channel */
static gdouble
render_rms (LrgMidiSynth *synth,
            gdouble       seconds)
{
    g_autofree gfloat *buffer = NULL;
    guint frames;
    guint i;
    gdouble sum;

    frames = (guint)(seconds * RATE);
    buffer = g_new0 (gfloat, 2 * (gsize)frames);
    g_assert_cmpuint (lrg_midi_synth_render (synth, buffer, frames), ==, frames);
    sum = 0.0;
    for (i = 0; i < frames; i++)
        sum += (gdouble)buffer[2 * i] * (gdouble)buffer[2 * i];
    return frames > 0 ? sqrt (sum / (gdouble)frames) : 0.0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

static void
test_new_defaults (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    gfloat buffer[2 * 128];
    guint i;
    gdouble start;
    gdouble end;

    synth = lrg_midi_synth_new (RATE);
    g_assert_true (LRG_IS_MIDI_SYNTH (synth));
    g_assert_cmpuint (lrg_midi_synth_get_sample_rate (synth), ==, RATE);
    g_assert_false (lrg_midi_synth_has_soundfont (synth));
    g_assert_cmpuint (lrg_midi_synth_get_preset_count (synth), ==, 0);
    g_assert_false (lrg_midi_synth_is_playing (synth));
    g_assert_false (lrg_midi_synth_is_finished (synth));
    g_assert_false (lrg_midi_synth_has_stream (synth));
    g_assert_cmpfloat (lrg_midi_synth_get_volume (synth), ==, 1.0f);
    g_assert_cmpfloat (lrg_midi_synth_get_position (synth), ==, 0.0);
    g_assert_false (lrg_midi_synth_get_loop_points (synth, &start, &end));

    /* silence without a song */
    for (i = 0; i < G_N_ELEMENTS (buffer); i++)
        buffer[i] = 1.0f;
    g_assert_cmpuint (lrg_midi_synth_render (synth, buffer, 128), ==, 128);
    for (i = 0; i < G_N_ELEMENTS (buffer); i++)
        g_assert_cmpfloat (buffer[i], ==, 0.0f);

    /* the volume is clamped and a property */
    lrg_midi_synth_set_volume (synth, 3.0f);
    g_assert_cmpfloat (lrg_midi_synth_get_volume (synth), ==, 1.0f);
    lrg_midi_synth_set_volume (synth, -1.0f);
    g_assert_cmpfloat (lrg_midi_synth_get_volume (synth), ==, 0.0f);
    g_object_set (synth, "volume", 0.25f, NULL);
    g_assert_cmpfloat (lrg_midi_synth_get_volume (synth), ==, 0.25f);
}

static void
test_play_needs_soundfont (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) song = NULL;
    g_autoptr(GError) error = NULL;

    synth = lrg_midi_synth_new (RATE);
    song = build_note_song (60, 1, -1, -1);
    g_assert_false (lrg_midi_synth_play (synth, song, FALSE, 0.0, &error));
    g_assert_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_NO_SOUNDFONT);
    g_assert_false (lrg_midi_synth_is_playing (synth));
}

static void
test_load_soundfont (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) sf2 = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *missing = NULL;

    synth = new_synth_with_font ();
    g_assert_true (lrg_midi_synth_has_soundfont (synth));
    g_assert_cmpuint (lrg_midi_synth_get_preset_count (synth), ==, 1);

    /* from a file */
    dir = g_dir_make_tmp ("lrg-midi-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "sine.sf2", NULL);
    sf2 = build_sf2 ();
    g_assert_true (g_file_set_contents (path, g_bytes_get_data (sf2, NULL),
                                        (gssize)g_bytes_get_size (sf2), &error));
    g_assert_true (lrg_midi_synth_load_soundfont (synth, path, &error));
    g_assert_no_error (error);

    /* a missing file is a GIO error and changes nothing */
    missing = g_build_filename (dir, "missing.sf2", NULL);
    g_assert_false (lrg_midi_synth_load_soundfont (synth, missing, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_true (lrg_midi_synth_has_soundfont (synth));

    g_unlink (path);
    g_rmdir (dir);
}

/* a held note renders a sine; its end makes a non-looping song finish */
static void
test_render_note (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) song = NULL;
    g_autoptr(GError) error = NULL;
    gdouble rms;
    guint i;

    synth = new_synth_with_font ();
    song = build_note_song (60, 1, -1, -1);    /* 0.5 s */
    g_assert_true (lrg_midi_synth_play (synth, song, FALSE, 0.0, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_midi_synth_is_playing (synth));
    g_assert_false (lrg_midi_synth_get_loop_points (synth, NULL, NULL));

    rms = render_rms (synth, 0.25);
    g_assert_cmpfloat (rms, >, 0.05);
    g_assert_cmpfloat_with_epsilon (lrg_midi_synth_get_position (synth), 0.25, 0.002);
    g_assert_false (lrg_midi_synth_is_finished (synth));

    /* past the note's end and its release: finished, silent */
    for (i = 0; i < 20 && !lrg_midi_synth_is_finished (synth); i++)
        (void)render_rms (synth, 0.1);
    g_assert_true (lrg_midi_synth_is_finished (synth));
    g_assert_false (lrg_midi_synth_is_playing (synth));
    g_assert_cmpfloat (render_rms (synth, 0.1), <, 1e-4);

    /* stop clears the finished state */
    lrg_midi_synth_stop (synth, 0.0);
    g_assert_false (lrg_midi_synth_is_finished (synth));
}

/* loopStart/loopEnd markers wrap the position */
static void
test_loop_markers (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) song = NULL;
    g_autoptr(GError) error = NULL;
    gdouble start;
    gdouble end;

    synth = new_synth_with_font ();
    /* 3 quarters (1.5 s), loop between 1 and 2 quarters (0.5 .. 1.0 s) */
    song = build_note_song (60, 3, 1, 2);
    g_assert_true (lrg_midi_synth_play (synth, song, TRUE, 0.0, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_midi_synth_get_loop_points (synth, &start, &end));
    g_assert_cmpfloat_with_epsilon (start, 0.5, 1e-6);
    g_assert_cmpfloat_with_epsilon (end, 1.0, 1e-6);

    (void)render_rms (synth, 1.25);
    g_assert_cmpfloat_with_epsilon (lrg_midi_synth_get_position (synth), 0.75, 0.003);

    /* keeps looping: never past the loop end, never finished */
    (void)render_rms (synth, 3.1);
    g_assert_cmpfloat (lrg_midi_synth_get_position (synth), >=, 0.5);
    g_assert_cmpfloat (lrg_midi_synth_get_position (synth), <, 1.0);
    g_assert_true (lrg_midi_synth_is_playing (synth));
    g_assert_false (lrg_midi_synth_is_finished (synth));
}

/* without markers a looping song loops at its end; loopStart alone loops to the end */
static void
test_loop_whole_song (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) song = NULL;
    g_autoptr(GBytes) half = NULL;
    g_autoptr(GError) error = NULL;
    gdouble start;
    gdouble end;

    synth = new_synth_with_font ();
    song = build_note_song (60, 2, -1, -1);    /* 1.0 s */
    g_assert_true (lrg_midi_synth_play (synth, song, TRUE, 0.0, &error));
    g_assert_true (lrg_midi_synth_get_loop_points (synth, &start, &end));
    g_assert_cmpfloat_with_epsilon (start, 0.0, 1e-6);
    g_assert_cmpfloat_with_epsilon (end, 1.0, 1e-6);
    (void)render_rms (synth, 1.4);
    g_assert_cmpfloat_with_epsilon (lrg_midi_synth_get_position (synth), 0.4, 0.003);
    /* the note plays again after the wrap */
    g_assert_cmpfloat (render_rms (synth, 0.2), >, 0.05);

    half = build_note_song (60, 2, 1, -1);     /* loop 0.5 .. 1.0 s */
    g_assert_true (lrg_midi_synth_play (synth, half, TRUE, 0.0, &error));
    g_assert_true (lrg_midi_synth_get_loop_points (synth, &start, &end));
    g_assert_cmpfloat_with_epsilon (start, 0.5, 1e-6);
    g_assert_cmpfloat_with_epsilon (end, 1.0, 1e-6);
    (void)render_rms (synth, 1.2);
    g_assert_cmpfloat_with_epsilon (lrg_midi_synth_get_position (synth), 0.7, 0.003);
}

/*
 * A crossfade mixes both songs: from a loud song to a silent one the
 * level falls along the fade, from a silent one to a loud one it rises,
 * and between two loud songs it never drops out.
 */
static void
test_crossfade (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) loud = NULL;
    g_autoptr(GBytes) silent = NULL;
    g_autoptr(GError) error = NULL;
    gdouble full;
    gdouble first;
    gdouble second;
    gdouble after;

    synth = new_synth_with_font ();
    loud = build_note_song (60, 16, -1, -1);
    silent = build_silent_song (16);

    /* loud -> silent: the old song fades out over 1 s */
    g_assert_true (lrg_midi_synth_play (synth, loud, TRUE, 0.0, &error));
    full = render_rms (synth, 0.2);
    g_assert_true (lrg_midi_synth_play (synth, silent, TRUE, 1.0, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_midi_synth_is_playing (synth));
    first = render_rms (synth, 0.5);
    second = render_rms (synth, 0.5);
    after = render_rms (synth, 0.2);
    g_assert_cmpfloat (first, >, 0.5 * full);
    g_assert_cmpfloat (first, <, full);
    g_assert_cmpfloat (second, <, first);
    g_assert_cmpfloat (second, >, 0.05 * full);
    g_assert_cmpfloat (after, <, 1e-4);

    /* silent -> loud: the new song fades in over 1 s */
    g_assert_true (lrg_midi_synth_play (synth, loud, TRUE, 1.0, &error));
    first = render_rms (synth, 0.5);
    second = render_rms (synth, 0.5);
    after = render_rms (synth, 0.2);
    g_assert_cmpfloat (first, <, second);
    g_assert_cmpfloat (second, <, after);
    g_assert_cmpfloat_with_epsilon (after, full, 0.05 * full);

    /* loud -> loud: both play during the fade, the level never collapses */
    g_assert_true (lrg_midi_synth_play (synth, loud, TRUE, 1.0, &error));
    first = render_rms (synth, 0.5);
    second = render_rms (synth, 0.5);
    g_assert_cmpfloat (first, >, 0.3 * full);
    g_assert_cmpfloat (second, >, 0.3 * full);

    /* an immediate switch replaces both songs */
    g_assert_true (lrg_midi_synth_play (synth, silent, FALSE, 0.0, &error));
    g_assert_cmpfloat (render_rms (synth, 0.1), <, 1e-4);
}

static void
test_stop_fade (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) loud = NULL;
    g_autoptr(GError) error = NULL;
    gdouble full;
    gdouble fading;

    synth = new_synth_with_font ();
    loud = build_note_song (60, 16, -1, -1);
    g_assert_true (lrg_midi_synth_play (synth, loud, TRUE, 0.0, &error));
    full = render_rms (synth, 0.2);

    lrg_midi_synth_stop (synth, 0.5);
    g_assert_false (lrg_midi_synth_is_playing (synth));
    fading = render_rms (synth, 0.25);
    g_assert_cmpfloat (fading, >, 0.1 * full);
    g_assert_cmpfloat (fading, <, full);
    (void)render_rms (synth, 0.3);
    g_assert_cmpfloat (render_rms (synth, 0.1), <, 1e-4);

    /* stopping at once */
    g_assert_true (lrg_midi_synth_play (synth, loud, TRUE, 0.0, &error));
    (void)render_rms (synth, 0.1);
    lrg_midi_synth_stop (synth, 0.0);
    g_assert_cmpfloat (render_rms (synth, 0.1), <, 1e-4);
    g_assert_cmpfloat (lrg_midi_synth_get_position (synth), ==, 0.0);
}

/* the volume scales the output after a short ramp */
static void
test_volume (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) loud = NULL;
    g_autoptr(GError) error = NULL;
    gdouble full;
    gdouble half;

    synth = new_synth_with_font ();
    loud = build_note_song (60, 16, -1, -1);
    g_assert_true (lrg_midi_synth_play (synth, loud, TRUE, 0.0, &error));
    (void)render_rms (synth, 0.1);
    full = render_rms (synth, 0.2);

    lrg_midi_synth_set_volume (synth, 0.5f);
    (void)render_rms (synth, 0.1);              /* the ramp */
    half = render_rms (synth, 0.2);
    g_assert_cmpfloat_with_epsilon (half, 0.5 * full, 0.02 * full);

    lrg_midi_synth_set_volume (synth, 0.0f);
    (void)render_rms (synth, 0.1);
    g_assert_cmpfloat (render_rms (synth, 0.1), <, 1e-6);
}

/* every way a SoundFont can be broken fails cleanly and changes nothing */
static void
test_malformed_soundfont (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) good = NULL;
    const guint8 *data;
    gsize size;
    guint i;

    synth = lrg_midi_synth_new (RATE);
    good = build_sf2 ();
    data = g_bytes_get_data (good, &size);

    /* cases built from the good file */
    {
        GBytes *cases[12];
        guint n;

        n = 0;
        cases[n++] = g_bytes_new_static ("", 0);
        cases[n++] = g_bytes_new_static ("RIFF\x04\x00\x00\x00WAVE", 12);
        cases[n++] = g_bytes_new (data, 40);                                   /* truncated */
        cases[n++] = g_bytes_new (data, size - 1);                             /* RIFF size > file */
        /* a preset bag index past the bag table */
        cases[n++] = patch_u16 (good, find_id (good, "phdr") + 8 + 38 + 24, 9);
        /* the instrument generator points at the terminal instrument */
        cases[n++] = patch_u16 (good, find_id (good, "pgen") + 8 + 2, 1);
        /* a sample id past the sample table */
        cases[n++] = patch_u16 (good, find_id (good, "igen") + 8 + 4 + 2, 7);
        /* an instrument generator index past the generator table */
        cases[n++] = patch_u16 (good, find_id (good, "ibag") + 8 + 4, 4);
        /* decreasing instrument generator indices */
        cases[n++] = patch_u16 (good, find_id (good, "ibag") + 8, 3);
        /* a table renamed away: missing */
        {
            guint8 *copy;

            copy = g_memdup2 (data, size);
            memcpy (copy + find_id (good, "shdr"), "shdx", 4);
            cases[n++] = g_bytes_new_take (copy, size);
        }
        /* a chunk size that is not a whole number of records */
        {
            guint8 *copy;
            gsize at;

            copy = g_memdup2 (data, size);
            at = find_id (good, "pbag") + 4;
            copy[at] = (guint8)(copy[at] - 1);
            cases[n++] = g_bytes_new_take (copy, size);
        }
        /* a pdta subchunk overrunning its list */
        {
            guint8 *copy;
            gsize at;

            copy = g_memdup2 (data, size);
            at = find_id (good, "shdr") + 4;
            copy[at + 1] = 0x7f;
            cases[n++] = g_bytes_new_take (copy, size);
        }
        g_assert_cmpuint (n, <=, G_N_ELEMENTS (cases));

        for (i = 0; i < n; i++)
        {
            g_autoptr(GError) error = NULL;

            g_test_message ("malformed SoundFont case %u", i);
            g_assert_false (lrg_midi_synth_load_soundfont_bytes (synth, cases[i], &error));
            g_assert_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID);
            g_assert_false (lrg_midi_synth_has_soundfont (synth));
            g_bytes_unref (cases[i]);
        }
    }

    /* a good bank stays loaded after a bad one */
    {
        g_autoptr(GError) error = NULL;
        g_autoptr(GBytes) bad = g_bytes_new_static ("nonsense", 8);

        g_assert_true (lrg_midi_synth_load_soundfont_bytes (synth, good, &error));
        g_assert_false (lrg_midi_synth_load_soundfont_bytes (synth, bad, &error));
        g_assert_true (lrg_midi_synth_has_soundfont (synth));
    }
}

/* every way a MIDI file can be broken fails cleanly; the song keeps playing */
static void
test_malformed_midi (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GBytes) good = NULL;
    g_autoptr(GError) play_error = NULL;
    const guint8 *data;
    gsize size;
    GPtrArray *cases;
    guint i;

    synth = new_synth_with_font ();
    good = build_note_song (60, 4, -1, -1);
    data = g_bytes_get_data (good, &size);
    g_assert_true (lrg_midi_synth_play (synth, good, TRUE, 0.0, &play_error));

    cases = g_ptr_array_new_with_free_func ((GDestroyNotify)g_bytes_unref);
    g_ptr_array_add (cases, g_bytes_new_static ("", 0));
    g_ptr_array_add (cases, g_bytes_new_static ("RIFF0000", 8));
    g_ptr_array_add (cases, g_bytes_new (data, 13));                 /* short header */
    g_ptr_array_add (cases, g_bytes_new (data, size - 3));           /* truncated track */
    {
        guint8 *copy;

        /* SMPTE time division */
        copy = g_memdup2 (data, size);
        copy[12] = 0xe7;
        g_ptr_array_add (cases, g_bytes_new_take (copy, size));
        /* no tracks */
        copy = g_memdup2 (data, size);
        copy[10] = 0;
        copy[11] = 0;
        g_ptr_array_add (cases, g_bytes_new_take (copy, size));
        /* format 3 */
        copy = g_memdup2 (data, size);
        copy[9] = 3;
        g_ptr_array_add (cases, g_bytes_new_take (copy, size));
        /* a track length past the file */
        copy = g_memdup2 (data, size);
        copy[14 + 4] = 0x40;
        g_ptr_array_add (cases, g_bytes_new_take (copy, size));
    }
    {
        GByteArray *tracks[1];

        /* running status before any status byte */
        tracks[0] = g_byte_array_new ();
        put_varlen (tracks[0], 0);
        put_u8 (tracks[0], 0x3c);
        put_u8 (tracks[0], 0x40);
        put_end (tracks[0], 0);
        g_ptr_array_add (cases, build_smf (tracks, 1));

        /* a tempo event of a wrong length */
        tracks[0] = g_byte_array_new ();
        put_meta (tracks[0], 0, 0x51, (const guint8 *)"\x07\xa1", 2);
        put_end (tracks[0], 0);
        g_ptr_array_add (cases, build_smf (tracks, 1));

        /* a variable-length quantity of five bytes */
        tracks[0] = g_byte_array_new ();
        put_u8 (tracks[0], 0x81); put_u8 (tracks[0], 0x81); put_u8 (tracks[0], 0x81);
        put_u8 (tracks[0], 0x81); put_u8 (tracks[0], 0x01);
        put_u8 (tracks[0], 0x90); put_u8 (tracks[0], 0x3c); put_u8 (tracks[0], 0x40);
        g_ptr_array_add (cases, build_smf (tracks, 1));

        /* a SysEx overrunning its track */
        tracks[0] = g_byte_array_new ();
        put_varlen (tracks[0], 0);
        put_u8 (tracks[0], 0xf0);
        put_varlen (tracks[0], 200);
        put_u8 (tracks[0], 0x7f);
        g_ptr_array_add (cases, build_smf (tracks, 1));

        /* a channel message cut short */
        tracks[0] = g_byte_array_new ();
        put_varlen (tracks[0], 0);
        put_u8 (tracks[0], 0x90);
        put_u8 (tracks[0], 0x3c);
        g_ptr_array_add (cases, build_smf (tracks, 1));
    }

    for (i = 0; i < cases->len; i++)
    {
        g_autoptr(GError) error = NULL;

        g_test_message ("malformed MIDI case %u", i);
        g_assert_false (lrg_midi_synth_play (synth, g_ptr_array_index (cases, i), TRUE, 1.0, &error));
        g_assert_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID);
        g_assert_true (lrg_midi_synth_is_playing (synth));
    }
    g_ptr_array_unref (cases);
    g_assert_cmpfloat (render_rms (synth, 0.1), >, 0.05);
}

/*
 * Mutation sweep: truncations and byte flips of the good files either
 * load (and render) or fail with a GError.  Run under the sanitizers it
 * shows the validators keep the vendored loaders in bounds.
 */
static void
test_mutations (void)
{
    g_autoptr(GBytes) sf2 = NULL;
    g_autoptr(GBytes) smf = NULL;
    g_autoptr(GRand) rand = NULL;
    g_autoptr(LrgMidiSynth) player = NULL;
    const guint8 *sf2_data;
    const guint8 *smf_data;
    gsize sf2_size;
    gsize smf_size;
    guint i;
    guint rounds;
    gfloat buffer[2 * 256];

    /* LRG_TEST_MUTATIONS raises the count for a longer local sweep */
    rounds = 300;
    if (g_getenv ("LRG_TEST_MUTATIONS") != NULL)
        rounds = (guint)MAX (g_ascii_strtoull (g_getenv ("LRG_TEST_MUTATIONS"), NULL, 10), 1);

    sf2 = build_sf2 ();
    smf = build_note_song (64, 2, 1, 2);
    sf2_data = g_bytes_get_data (sf2, &sf2_size);
    smf_data = g_bytes_get_data (smf, &smf_size);
    rand = g_rand_new_with_seed (0x5f2u);
    player = new_synth_with_font ();

    for (i = 0; i < rounds; i++)
    {
        g_autoptr(LrgMidiSynth) synth = NULL;
        g_autoptr(GBytes) bytes = NULL;
        g_autoptr(GError) error = NULL;
        guint8 *copy;
        gsize size;
        guint flips;
        guint f;

        /* a SoundFont: mostly the small structural part is hit */
        size = (i % 7 == 0) ? (gsize)g_rand_int_range (rand, 0, (gint32)sf2_size) : sf2_size;
        copy = g_memdup2 (sf2_data, sf2_size);
        flips = (guint)g_rand_int_range (rand, 1, 4);
        for (f = 0; f < flips && size > 0; f++)
        {
            gsize at;

            at = (i % 2 == 0) ? (gsize)g_rand_int_range (rand, 0, (gint32)MIN (size, 64)) :
                                sf2_size - 1 - (gsize)g_rand_int_range (rand, 0, 600);
            if (at < size)
                copy[at] = (guint8)g_rand_int_range (rand, 0, 256);
        }
        bytes = g_bytes_new_take (copy, size);
        synth = lrg_midi_synth_new (RATE);
        if (lrg_midi_synth_load_soundfont_bytes (synth, bytes, &error))
        {
            g_autoptr(GError) play_error = NULL;

            if (lrg_midi_synth_play (synth, smf, TRUE, 0.0, &play_error))
                (void)lrg_midi_synth_render (synth, buffer, 256);
        }
        else
        {
            g_assert_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID);
        }
    }

    for (i = 0; i < rounds; i++)
    {
        g_autoptr(GBytes) bytes = NULL;
        g_autoptr(GError) error = NULL;
        guint8 *copy;
        gsize size;
        gsize at;

        size = (i % 5 == 0) ? (gsize)g_rand_int_range (rand, 0, (gint32)smf_size) : smf_size;
        copy = g_memdup2 (smf_data, smf_size);
        at = (gsize)g_rand_int_range (rand, 0, (gint32)smf_size);
        if (at < size)
            copy[at] = (guint8)g_rand_int_range (rand, 0, 256);
        bytes = g_bytes_new_take (copy, size);
        if (lrg_midi_synth_play (player, bytes, (i & 1) != 0, (i & 2) != 0 ? 0.01 : 0.0, &error))
            (void)lrg_midi_synth_render (player, buffer, 256);
        else
            g_assert_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID);
    }
}

/* without the audio device the stream refuses; with it (opt-in) it runs */
static void
test_stream (void)
{
    g_autoptr(LrgMidiSynth) synth = NULL;
    g_autoptr(GError) error = NULL;
    GrlAudioDevice *device;

    synth = new_synth_with_font ();
    if (g_strcmp0 (g_getenv ("LRG_TEST_AUDIO_DEVICE"), "1") != 0)
    {
        /* headless: no stream; feeding and stopping are no-ops */
        g_assert_false (lrg_midi_synth_has_stream (synth));
        lrg_midi_synth_update (synth);
        lrg_midi_synth_stop_stream (synth);
        g_assert_false (lrg_midi_synth_has_stream (synth));
        g_test_skip ("set LRG_TEST_AUDIO_DEVICE=1 to open the audio device");
        return;
    }
    device = grl_audio_device_get_default ();
    if (device == NULL || !grl_audio_device_is_ready (device))
    {
        g_assert_false (lrg_midi_synth_start_stream (synth, &error));
        g_assert_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_NO_DEVICE);
        g_test_skip ("no audio device");
        return;
    }

    {
        g_autoptr(GBytes) song = NULL;
        guint i;

        song = build_note_song (60, 4, -1, -1);
        g_assert_true (lrg_midi_synth_start_stream (synth, &error));
        g_assert_no_error (error);
        g_assert_true (lrg_midi_synth_start_stream (synth, &error));    /* idempotent */
        g_assert_true (lrg_midi_synth_play (synth, song, TRUE, 0.0, &error));
        for (i = 0; i < 30; i++)
        {
            lrg_midi_synth_update (synth);
            g_usleep (10000);
        }
        g_assert_true (lrg_midi_synth_get_position (synth) > 0.0);
        lrg_midi_synth_stop_stream (synth);
        g_assert_false (lrg_midi_synth_has_stream (synth));
        grl_audio_device_close (device);
    }
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/midi-synth/new-defaults", test_new_defaults);
    g_test_add_func ("/midi-synth/play-needs-soundfont", test_play_needs_soundfont);
    g_test_add_func ("/midi-synth/load-soundfont", test_load_soundfont);
    g_test_add_func ("/midi-synth/render-note", test_render_note);
    g_test_add_func ("/midi-synth/loop-markers", test_loop_markers);
    g_test_add_func ("/midi-synth/loop-whole-song", test_loop_whole_song);
    g_test_add_func ("/midi-synth/crossfade", test_crossfade);
    g_test_add_func ("/midi-synth/stop-fade", test_stop_fade);
    g_test_add_func ("/midi-synth/volume", test_volume);
    g_test_add_func ("/midi-synth/malformed-soundfont", test_malformed_soundfont);
    g_test_add_func ("/midi-synth/malformed-midi", test_malformed_midi);
    g_test_add_func ("/midi-synth/mutations", test_mutations);
    g_test_add_func ("/midi-synth/stream", test_stream);

    return g_test_run ();
}
