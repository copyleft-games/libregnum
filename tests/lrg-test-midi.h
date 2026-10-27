/* lrg-test-midi.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Test-only helpers that write a tiny SoundFont 2 bank and Standard MIDI
 * Files byte by byte, so the MIDI synthesizer's tests and the audio
 * benchmarks never depend on data files.  Header-only: every function is
 * static and only the file that includes it links it.
 */

#pragma once

#include <glib.h>
#include <math.h>
#include <string.h>

#define TEST_MIDI_RATE          (44100)
#define TEST_MIDI_SINE_PERIOD   (441)   /* one cycle of 100 Hz at 44100 Hz */
#define TEST_MIDI_SINE_CYCLES   (10)
#define TEST_MIDI_SINE_PADDING  (46)    /* SoundFont rule: 46 zero samples after each */
#define TEST_MIDI_DIVISION      (480)   /* ticks per quarter; 120 bpm: 500 ms per quarter */

/* ==========================================================================
 * Byte writers
 * ========================================================================== */

G_GNUC_UNUSED static void
test_midi_put_u8 (GByteArray *out,
                  guint       value)
{
    guint8 b = (guint8)value;

    g_byte_array_append (out, &b, 1);
}

G_GNUC_UNUSED static void
test_midi_put_u16le (GByteArray *out,
                     guint       value)
{
    test_midi_put_u8 (out, value & 0xff);
    test_midi_put_u8 (out, (value >> 8) & 0xff);
}

G_GNUC_UNUSED static void
test_midi_put_u32le (GByteArray *out,
                     guint32     value)
{
    test_midi_put_u16le (out, value & 0xffff);
    test_midi_put_u16le (out, (value >> 16) & 0xffff);
}

G_GNUC_UNUSED static void
test_midi_put_u32be (GByteArray *out,
                     guint32     value)
{
    test_midi_put_u8 (out, (value >> 24) & 0xff);
    test_midi_put_u8 (out, (value >> 16) & 0xff);
    test_midi_put_u8 (out, (value >> 8) & 0xff);
    test_midi_put_u8 (out, value & 0xff);
}

G_GNUC_UNUSED static void
test_midi_put_id (GByteArray  *out,
                  const gchar *id)
{
    g_byte_array_append (out, (const guint8 *)id, 4);
}

/* a fixed-size, zero padded name field */
G_GNUC_UNUSED static void
test_midi_put_name (GByteArray  *out,
                    const gchar *name,
                    guint        size)
{
    guint i;
    gsize len;

    len = strlen (name);
    for (i = 0; i < size; i++)
        test_midi_put_u8 (out, i < len ? (guint)(guchar)name[i] : 0);
}

/* starts a RIFF chunk, returns the offset of its size field */
G_GNUC_UNUSED static guint
test_midi_chunk_begin (GByteArray  *out,
                       const gchar *id,
                       const gchar *list_type)
{
    guint at;

    test_midi_put_id (out, id);
    at = out->len;
    test_midi_put_u32le (out, 0);
    if (list_type != NULL)
        test_midi_put_id (out, list_type);
    return at;
}

G_GNUC_UNUSED static void
test_midi_chunk_end (GByteArray *out,
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
 * test_midi_build_sf2:
 * One preset ("Sine", bank 0 program 0) -> one instrument -> one sample:
 * TEST_MIDI_SINE_CYCLES cycles of a 100 Hz sine, looped as a whole (sample mode 1),
 * root key 60, so every note sustains while held.
 */
G_GNUC_UNUSED static GBytes *
test_midi_build_sf2 (void)
{
    GByteArray *out;
    guint riff;
    guint list;
    guint sub;
    guint i;
    guint32 n_samples;

    n_samples = TEST_MIDI_SINE_PERIOD * TEST_MIDI_SINE_CYCLES;
    out = g_byte_array_new ();
    riff = test_midi_chunk_begin (out, "RIFF", "sfbk");

    /* INFO: the version, which TinySoundFont skips */
    list = test_midi_chunk_begin (out, "LIST", "INFO");
    sub = test_midi_chunk_begin (out, "ifil", NULL);
    test_midi_put_u16le (out, 2);
    test_midi_put_u16le (out, 1);
    test_midi_chunk_end (out, sub);
    test_midi_chunk_end (out, list);

    /* sdta: the sine and its padding */
    list = test_midi_chunk_begin (out, "LIST", "sdta");
    sub = test_midi_chunk_begin (out, "smpl", NULL);
    for (i = 0; i < n_samples; i++)
    {
        gdouble v;

        v = sin (2.0 * G_PI * (gdouble)(i % TEST_MIDI_SINE_PERIOD) / (gdouble)TEST_MIDI_SINE_PERIOD);
        test_midi_put_u16le (out, (guint)(guint16)(gint16)(v * 26000.0));
    }
    for (i = 0; i < TEST_MIDI_SINE_PADDING; i++)
        test_midi_put_u16le (out, 0);
    test_midi_chunk_end (out, sub);
    test_midi_chunk_end (out, list);

    /* pdta: the nine tables, each with its terminal record */
    list = test_midi_chunk_begin (out, "LIST", "pdta");

    sub = test_midi_chunk_begin (out, "phdr", NULL);
    test_midi_put_name (out, "Sine", 20); test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 0);
    test_midi_put_u32le (out, 0); test_midi_put_u32le (out, 0); test_midi_put_u32le (out, 0);
    test_midi_put_name (out, "EOP", 20); test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 1);
    test_midi_put_u32le (out, 0); test_midi_put_u32le (out, 0); test_midi_put_u32le (out, 0);
    test_midi_chunk_end (out, sub);

    sub = test_midi_chunk_begin (out, "pbag", NULL);
    test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 0);
    test_midi_put_u16le (out, 1); test_midi_put_u16le (out, 0);
    test_midi_chunk_end (out, sub);

    sub = test_midi_chunk_begin (out, "pmod", NULL);
    for (i = 0; i < 10; i++)
        test_midi_put_u8 (out, 0);
    test_midi_chunk_end (out, sub);

    sub = test_midi_chunk_begin (out, "pgen", NULL);
    test_midi_put_u16le (out, 41); test_midi_put_u16le (out, 0);    /* instrument 0 */
    test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 0);     /* terminal */
    test_midi_chunk_end (out, sub);

    sub = test_midi_chunk_begin (out, "inst", NULL);
    test_midi_put_name (out, "SineInst", 20); test_midi_put_u16le (out, 0);
    test_midi_put_name (out, "EOI", 20); test_midi_put_u16le (out, 1);
    test_midi_chunk_end (out, sub);

    sub = test_midi_chunk_begin (out, "ibag", NULL);
    test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 0);
    test_midi_put_u16le (out, 2); test_midi_put_u16le (out, 0);
    test_midi_chunk_end (out, sub);

    sub = test_midi_chunk_begin (out, "imod", NULL);
    for (i = 0; i < 10; i++)
        test_midi_put_u8 (out, 0);
    test_midi_chunk_end (out, sub);

    sub = test_midi_chunk_begin (out, "igen", NULL);
    test_midi_put_u16le (out, 54); test_midi_put_u16le (out, 1);    /* sampleModes: loop */
    test_midi_put_u16le (out, 53); test_midi_put_u16le (out, 0);    /* sampleID 0 */
    test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 0);     /* terminal */
    test_midi_chunk_end (out, sub);

    sub = test_midi_chunk_begin (out, "shdr", NULL);
    test_midi_put_name (out, "Sine", 20);
    test_midi_put_u32le (out, 0); test_midi_put_u32le (out, n_samples);
    test_midi_put_u32le (out, 0); test_midi_put_u32le (out, n_samples);
    test_midi_put_u32le (out, TEST_MIDI_RATE);
    test_midi_put_u8 (out, 60); test_midi_put_u8 (out, 0); test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 1);
    test_midi_put_name (out, "EOS", 20);
    for (i = 0; i < 5; i++)
        test_midi_put_u32le (out, 0);
    test_midi_put_u8 (out, 0); test_midi_put_u8 (out, 0); test_midi_put_u16le (out, 0); test_midi_put_u16le (out, 0);
    test_midi_chunk_end (out, sub);

    test_midi_chunk_end (out, list);
    test_midi_chunk_end (out, riff);
    return g_byte_array_free_to_bytes (out);
}

/* ==========================================================================
 * Synthetic MIDI files
 * ========================================================================== */

G_GNUC_UNUSED static void
test_midi_put_varlen (GByteArray *out,
                      guint32     value)
{
    guint8 buf[4];
    gint n;

    n = 0;
    buf[n++] = value & 0x7f;
    while ((value >>= 7) != 0)
        buf[n++] = (guint8)(0x80 | (value & 0x7f));
    while (n > 0)
        test_midi_put_u8 (out, buf[--n]);
}

/* an event at @delta ticks: up to three status/data bytes */
G_GNUC_UNUSED static void
test_midi_put_event (GByteArray *track,
                     guint32     delta,
                     guint       status,
                     gint        data1,
                     gint        data2)
{
    test_midi_put_varlen (track, delta);
    test_midi_put_u8 (track, status);
    if (data1 >= 0)
        test_midi_put_u8 (track, (guint)data1);
    if (data2 >= 0)
        test_midi_put_u8 (track, (guint)data2);
}

G_GNUC_UNUSED static void
test_midi_put_meta (GByteArray  *track,
                    guint32      delta,
                    guint        type,
                    const guint8 *data,
                    guint        length)
{
    test_midi_put_varlen (track, delta);
    test_midi_put_u8 (track, 0xff);
    test_midi_put_u8 (track, type);
    test_midi_put_varlen (track, length);
    if (length > 0)
        g_byte_array_append (track, data, length);
}

G_GNUC_UNUSED static void
test_midi_put_marker (GByteArray  *track,
                      guint32      delta,
                      const gchar *text)
{
    test_midi_put_meta (track, delta, 0x06, (const guint8 *)text, (guint)strlen (text));
}

G_GNUC_UNUSED static void
test_midi_put_end (GByteArray *track,
                   guint32     delta)
{
    test_midi_put_meta (track, delta, 0x2f, NULL, 0);
}

/* a format 1 file of the given tracks (consumed) */
G_GNUC_UNUSED static GBytes *
test_midi_build_smf (GByteArray **tracks,
                     guint        n_tracks)
{
    GByteArray *out;
    guint i;

    out = g_byte_array_new ();
    test_midi_put_id (out, "MThd");
    test_midi_put_u32be (out, 6);
    test_midi_put_u8 (out, 0); test_midi_put_u8 (out, 1);
    test_midi_put_u8 (out, 0); test_midi_put_u8 (out, n_tracks);
    test_midi_put_u8 (out, TEST_MIDI_DIVISION >> 8); test_midi_put_u8 (out, TEST_MIDI_DIVISION & 0xff);
    for (i = 0; i < n_tracks; i++)
    {
        test_midi_put_id (out, "MTrk");
        test_midi_put_u32be (out, tracks[i]->len);
        g_byte_array_append (out, tracks[i]->data, tracks[i]->len);
        g_byte_array_unref (tracks[i]);
    }
    return g_byte_array_free_to_bytes (out);
}


/*
 * test_midi_build_busy_song:
 * A dense song for load tests: on @n_channels melodic channels (the drum
 * channel 10 is skipped) a four-note chord changes every eighth note
 * for @quarters quarter notes, so about 4 x @n_channels voices sound at
 * once (plus their release tails).  Loops cleanly as a whole.
 */
G_GNUC_UNUSED static GBytes *
test_midi_build_busy_song (guint n_channels,
                           guint quarters)
{
    GByteArray *tracks[2];
    guint       step;
    guint       steps;
    guint       c;
    guint       n;

    /* track 0: the tempo (120 bpm) */
    tracks[0] = g_byte_array_new ();
    {
        const guint8 tempo[3] = { 0x07, 0xa1, 0x20 };   /* 500000 us */

        test_midi_put_meta (tracks[0], 0, 0x51, tempo, 3);
    }
    test_midi_put_end (tracks[0], quarters * TEST_MIDI_DIVISION);

    /* track 1: every channel's chords, all on one track */
    tracks[1] = g_byte_array_new ();
    n_channels = MIN (n_channels, 15u);
    for (c = 0; c < n_channels; c++)
    {
        guint channel = c < 9 ? c : c + 1;

        test_midi_put_event (tracks[1], 0, 0xc0 | channel, 0, -1);
        test_midi_put_event (tracks[1], 0, 0xb0 | channel, 7, 100);
    }
    steps = quarters * 2;
    for (step = 0; step < steps; step++)
    {
        guint32 delta = step == 0 ? 0 : TEST_MIDI_DIVISION / 2;

        for (c = 0; c < n_channels; c++)
        {
            guint channel = c < 9 ? c : c + 1;
            guint root = 36 + ((step * 5 + c * 7) % 36);

            for (n = 0; n < 4; n++)
            {
                /* the previous chord off, then this one on */
                if (step > 0)
                {
                    guint old_root = 36 + (((step - 1) * 5 + c * 7) % 36);

                    test_midi_put_event (tracks[1], delta, 0x80 | channel, (gint)(old_root + n * 4), 0);
                    delta = 0;
                }
                test_midi_put_event (tracks[1], delta, 0x90 | channel, (gint)(root + n * 4), 90);
                delta = 0;
            }
        }
    }
    for (c = 0; c < n_channels; c++)
    {
        guint channel = c < 9 ? c : c + 1;
        guint32 delta = c == 0 ? TEST_MIDI_DIVISION / 2 : 0;
        guint root = 36 + (((steps - 1) * 5 + c * 7) % 36);

        for (n = 0; n < 4; n++)
        {
            test_midi_put_event (tracks[1], delta, 0x80 | channel, (gint)(root + n * 4), 0);
            delta = 0;
        }
    }
    test_midi_put_end (tracks[1], 0);

    return test_midi_build_smf (tracks, 2);
}
