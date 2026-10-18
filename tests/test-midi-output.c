/* test-midi-output.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the MIDI outputs and LrgMidiSequencer.  Songs are
 * synthetic Standard MIDI Files written here byte by byte; the sink is
 * the recording LrgMidiOutputBuffer, so the event stream (what, and
 * when) is checked without a sequencer device.  The ALSA output is only
 * checked to fail cleanly for a port that cannot exist (or to report
 * that the build has no ALSA).
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <libregnum.h>

#define DIVISION (480)   /* ticks per quarter; 120 bpm: 500 ms per quarter */

/* ==========================================================================
 * Synthetic MIDI files
 * ========================================================================== */

static void
put_u8 (GByteArray *out,
        guint       value)
{
    guint8 b;

    b = (guint8)value;
    g_byte_array_append (out, &b, 1);
}

static void
put_u32be (GByteArray *out,
           guint32     value)
{
    put_u8 (out, value >> 24);
    put_u8 (out, (value >> 16) & 0xff);
    put_u8 (out, (value >> 8) & 0xff);
    put_u8 (out, value & 0xff);
}

static void
put_varlen (GByteArray *out,
            guint32     value)
{
    guint8 buf[4];
    gint   n;

    n = 0;
    buf[n++] = value & 0x7f;
    while ((value >>= 7) != 0)
        buf[n++] = (guint8)(0x80 | (value & 0x7f));
    while (n > 0)
        put_u8 (out, buf[--n]);
}

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
put_meta (GByteArray   *track,
          guint32       delta,
          guint         type,
          const guint8 *data,
          guint         length)
{
    put_varlen (track, delta);
    put_u8 (track, 0xff);
    put_u8 (track, type);
    put_varlen (track, length);
    if (length > 0)
        g_byte_array_append (track, data, length);
}

/*
 * build_song:
 * Channel 2: program 5, volume 100, then @notes notes of one quarter
 * each (keys 60, 61, ...), note off at the end of each quarter.  Loop
 * markers at @loop_start / @loop_end quarters when >= 0.
 */
static GBytes *
build_song (guint notes,
            gint  loop_start,
            gint  loop_end)
{
    GByteArray   *out;
    GByteArray   *track;
    const guint8  tempo[3] = { 0x07, 0xa1, 0x20 };
    guint         i;
    guint32       now;
    guint32       last;

    track = g_byte_array_new ();
    put_meta (track, 0, 0x51, tempo, 3);
    put_event (track, 0, 0xc2, 5, -1);
    put_event (track, 0, 0xb2, 7, 100);
    now = 0;
    last = 0;
    for (i = 0; i <= notes; i++)
    {
        guint32 start;

        start = i * DIVISION;
        if (loop_end >= 0 && (guint)loop_end == i)
        {
            put_meta (track, start - last, 0x06, (const guint8 *)"loopEnd", 7);
            last = start;
        }
        if (loop_start >= 0 && (guint)loop_start == i)
        {
            put_meta (track, start - last, 0x06, (const guint8 *)"loopStart", 9);
            last = start;
        }
        if (i == notes)
            break;
        put_event (track, start - last, 0x92, (gint)(60 + i), 90);
        last = start;
        now = start + DIVISION;
        put_event (track, now - last, 0x82, (gint)(60 + i), 0);
        last = now;
    }
    put_meta (track, 0, 0x2f, NULL, 0);

    out = g_byte_array_new ();
    g_byte_array_append (out, (const guint8 *)"MThd", 4);
    put_u32be (out, 6);
    put_u8 (out, 0); put_u8 (out, 0);
    put_u8 (out, 0); put_u8 (out, 1);
    put_u8 (out, DIVISION >> 8); put_u8 (out, DIVISION & 0xff);
    g_byte_array_append (out, (const guint8 *)"MTrk", 4);
    put_u32be (out, track->len);
    g_byte_array_append (out, track->data, track->len);
    g_byte_array_unref (track);
    return g_byte_array_free_to_bytes (out);
}

/* the number of note ons (velocity > 0) recorded */
static guint
note_ons (LrgMidiOutputBuffer *buffer)
{
    return lrg_midi_output_buffer_count_status (buffer, 0x90);
}

/* ==========================================================================
 * Outputs
 * ========================================================================== */

/* the helpers build the right bytes; malformed messages are refused */
static void
test_output_messages (void)
{
    g_autoptr(LrgMidiOutputBuffer) buffer = NULL;
    LrgMidiOutput                 *output;
    guint8                         status;
    guint8                         d1;
    guint8                         d2;
    gdouble                        time;
    const guint8                   bad_data[3] = { 0x90, 0x80, 0x10 };
    const guint8                   no_status[2] = { 0x40, 0x40 };
    const guint8                   short_cc[2] = { 0xB0, 0x07 };
    const guint8                   clock[1] = { 0xF8 };

    buffer = lrg_midi_output_buffer_new ();
    output = LRG_MIDI_OUTPUT (buffer);

    lrg_midi_output_buffer_set_clock (buffer, 2.0);
    g_assert_true (lrg_midi_output_note_on (output, 3, 64, 100, 0.0));
    g_assert_true (lrg_midi_output_note_off (output, 3, 64, 0.25));
    g_assert_true (lrg_midi_output_control_change (output, 15, 7, 90, 0.0));
    g_assert_true (lrg_midi_output_program_change (output, 9, 42, 0.0));
    g_assert_true (lrg_midi_output_pitch_bend (output, 0, 8192 + 100, 0.0));
    g_assert_true (lrg_midi_output_send (output, clock, 1, 0.0));
    g_assert_cmpuint (lrg_midi_output_buffer_get_count (buffer), ==, 6);
    g_assert_cmpuint (lrg_midi_output_get_sent_count (output), ==, 6);

    g_assert_cmpuint (lrg_midi_output_buffer_get_message (buffer, 0, &status, &d1, &d2, &time), ==, 3);
    g_assert_cmpuint (status, ==, 0x93);
    g_assert_cmpuint (d1, ==, 64);
    g_assert_cmpuint (d2, ==, 100);
    g_assert_cmpfloat (time, ==, 2.0);
    lrg_midi_output_buffer_get_message (buffer, 1, &status, NULL, NULL, &time);
    g_assert_cmpuint (status, ==, 0x83);
    g_assert_cmpfloat (time, ==, 2.25);
    g_assert_cmpuint (lrg_midi_output_buffer_get_message (buffer, 3, &status, &d1, NULL, NULL), ==, 2);
    g_assert_cmpuint (status, ==, 0xC9);
    g_assert_cmpuint (d1, ==, 42);
    lrg_midi_output_buffer_get_message (buffer, 4, &status, &d1, &d2, NULL);
    g_assert_cmpuint (status, ==, 0xE0);
    g_assert_cmpuint ((guint)d1 | ((guint)d2 << 7), ==, 8292);
    g_assert_cmpuint (lrg_midi_output_buffer_get_message (buffer, 99, NULL, NULL, NULL, NULL), ==, 0);

    /* refused: a data byte with the high bit, no status, wrong length */
    g_assert_false (lrg_midi_output_send (output, bad_data, 3, 0.0));
    g_assert_false (lrg_midi_output_send (output, no_status, 2, 0.0));
    g_assert_false (lrg_midi_output_send (output, short_cc, 2, 0.0));
    g_assert_cmpuint (lrg_midi_output_buffer_get_count (buffer), ==, 6);

    g_assert_cmpuint (lrg_midi_output_buffer_count_status (buffer, 0x80), ==, 1);
    g_assert_cmpuint (lrg_midi_output_buffer_count_status (buffer, 0), ==, 6);

    /* a flush drops what is not due yet; all notes off covers 16 channels */
    lrg_midi_output_all_notes_off (output);
    g_assert_cmpuint (lrg_midi_output_buffer_get_flush_count (buffer), ==, 1);
    g_assert_cmpuint (lrg_midi_output_buffer_count_status (buffer, 0x80), ==, 0);
    g_assert_cmpuint (lrg_midi_output_buffer_count_status (buffer, 0xB0), ==, 1 + 3 * 16);

    lrg_midi_output_buffer_clear (buffer);
    g_assert_cmpuint (lrg_midi_output_buffer_get_count (buffer), ==, 0);
    g_assert_cmpuint (lrg_midi_output_buffer_get_flush_count (buffer), ==, 0);
}

/* ALSA: an impossible port fails with a GError, never crashes */
static void
test_output_alsa (void)
{
    g_autoptr(LrgMidiOutputAlsa) alsa = NULL;
    g_autoptr(GError)            error = NULL;

    alsa = lrg_midi_output_alsa_new ("no-such-midi-port-in-this-test:99", &error);
    g_assert_null (alsa);
    g_assert_nonnull (error);
    g_assert_true (error->domain == LRG_MIDI_OUTPUT_ERROR);
    if (!lrg_midi_output_alsa_is_available ())
        g_assert_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_UNSUPPORTED);
    else
        g_assert_true (error->code == LRG_MIDI_OUTPUT_ERROR_NOT_FOUND ||
                       error->code == LRG_MIDI_OUTPUT_ERROR_FAILED);
    g_clear_error (&error);

    alsa = lrg_midi_output_alsa_new ("", &error);
    g_assert_null (alsa);
    g_assert_nonnull (error);
}

/* ==========================================================================
 * Sequencer
 * ========================================================================== */

/* the events come out in time: frame by frame, each at its due time */
static void
test_sequencer_timing (void)
{
    g_autoptr(LrgMidiOutputBuffer) buffer = NULL;
    g_autoptr(LrgMidiSequencer)    seq = NULL;
    g_autoptr(GBytes)              song = NULL;
    g_autoptr(GError)              error = NULL;
    gdouble                        t;
    guint                          i;
    guint                          on_index;

    buffer = lrg_midi_output_buffer_new ();
    seq = lrg_midi_sequencer_new (LRG_MIDI_OUTPUT (buffer));
    g_assert_true (lrg_midi_sequencer_get_output (seq) == LRG_MIDI_OUTPUT (buffer));
    song = build_song (4, -1, -1);
    g_assert_true (lrg_midi_sequencer_play (seq, song, FALSE, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_midi_sequencer_is_playing (seq));
    g_assert_cmpfloat_with_epsilon (lrg_midi_sequencer_get_length (seq), 2.0, 1e-6);

    /* time 0: program, volume and the first note */
    g_assert_cmpuint (lrg_midi_output_buffer_count_status (buffer, 0xC0), ==, 1);
    g_assert_cmpuint (lrg_midi_output_buffer_count_status (buffer, 0xB0), ==, 1);
    g_assert_cmpuint (note_ons (buffer), ==, 1);

    /* 60 Hz frames for 2.5 s */
    t = 0.0;
    for (i = 0; i < 150; i++)
    {
        t += 1.0 / 60.0;
        lrg_midi_output_buffer_set_clock (buffer, t);
        lrg_midi_sequencer_advance (seq, 1.0 / 60.0);
    }
    g_assert_cmpuint (note_ons (buffer), ==, 4);
    g_assert_cmpuint (lrg_midi_output_buffer_count_status (buffer, 0x80), ==, 4);
    g_assert_false (lrg_midi_sequencer_is_playing (seq));
    g_assert_true (lrg_midi_sequencer_is_finished (seq));

    /* the third note on (key 62) was due at 1.0 s: sent within a frame */
    on_index = 0;
    for (i = 0; i < lrg_midi_output_buffer_get_count (buffer); i++)
    {
        guint8  status;
        guint8  key;
        gdouble when;

        lrg_midi_output_buffer_get_message (buffer, i, &status, &key, NULL, &when);
        if ((status & 0xF0) == 0x90 && key == 62)
        {
            g_assert_cmpuint (status & 0x0F, ==, 2);
            g_assert_cmpfloat (when, >=, 1.0 - 1e-9);
            g_assert_cmpfloat (when, <=, 1.0 + 1.0 / 60.0 + 1e-9);
            on_index = i;
        }
    }
    g_assert_cmpuint (on_index, >, 0);
}

/* with a look-ahead the events go out early, stamped with their exact time */
static void
test_sequencer_lookahead (void)
{
    g_autoptr(LrgMidiOutputBuffer) buffer = NULL;
    g_autoptr(LrgMidiSequencer)    seq = NULL;
    g_autoptr(GBytes)              song = NULL;
    guint                          i;
    gboolean                       found;

    buffer = lrg_midi_output_buffer_new ();
    seq = lrg_midi_sequencer_new (LRG_MIDI_OUTPUT (buffer));
    lrg_midi_sequencer_set_lookahead (seq, 0.6);
    g_assert_cmpfloat (lrg_midi_sequencer_get_lookahead (seq), ==, 0.6);
    lrg_midi_sequencer_set_lookahead (seq, 5.0);
    g_assert_cmpfloat (lrg_midi_sequencer_get_lookahead (seq), ==, LRG_MIDI_SEQUENCER_MAX_LOOKAHEAD);
    lrg_midi_sequencer_set_lookahead (seq, 0.6);
    song = build_song (4, -1, -1);
    g_assert_true (lrg_midi_sequencer_play (seq, song, FALSE, NULL));

    /* at clock 0 the second note (0.5 s) is already handed over */
    g_assert_cmpuint (note_ons (buffer), ==, 2);
    found = FALSE;
    for (i = 0; i < lrg_midi_output_buffer_get_count (buffer); i++)
    {
        guint8  status;
        guint8  key;
        gdouble when;

        lrg_midi_output_buffer_get_message (buffer, i, &status, &key, NULL, &when);
        if ((status & 0xF0) == 0x90 && key == 61)
        {
            g_assert_cmpfloat_with_epsilon (when, 0.5, 1e-6);
            found = TRUE;
        }
    }
    g_assert_true (found);

    /* stopping drops the scheduled ones and silences the output */
    lrg_midi_sequencer_stop (seq);
    g_assert_false (lrg_midi_sequencer_is_playing (seq));
    g_assert_cmpuint (note_ons (buffer), ==, 1);
    g_assert_cmpuint (lrg_midi_output_buffer_get_flush_count (buffer), ==, 1);
    g_assert_cmpfloat (lrg_midi_sequencer_get_position (seq), ==, 0.0);
}

/* a loop wraps at its marker, releasing notes, and replays from its start */
static void
test_sequencer_loop (void)
{
    g_autoptr(LrgMidiOutputBuffer) buffer = NULL;
    g_autoptr(LrgMidiSequencer)    seq = NULL;
    g_autoptr(GBytes)              song = NULL;
    guint                          i;
    guint                          key61;

    buffer = lrg_midi_output_buffer_new ();
    seq = lrg_midi_sequencer_new (LRG_MIDI_OUTPUT (buffer));
    /* four notes, loop over notes 1 .. 2 (0.5 s .. 1.5 s) */
    song = build_song (4, 1, 3);
    g_assert_true (lrg_midi_sequencer_play (seq, song, TRUE, NULL));
    {
        gdouble start;
        gdouble end;

        g_assert_true (lrg_midi_sequencer_get_loop_points (seq, &start, &end));
        g_assert_cmpfloat_with_epsilon (start, 0.5, 1e-6);
        g_assert_cmpfloat_with_epsilon (end, 1.5, 1e-6);
    }
    for (i = 0; i < 300; i++)
        lrg_midi_sequencer_advance (seq, 0.01);   /* 3 s */
    g_assert_true (lrg_midi_sequencer_is_playing (seq));
    g_assert_false (lrg_midi_sequencer_is_finished (seq));
    /* clock 3.0 s: 0.5 + (2.5 mod 1.0) = 1.0 s */
    g_assert_cmpfloat_with_epsilon (lrg_midi_sequencer_get_position (seq), 1.0, 1e-6);

    key61 = 0;
    for (i = 0; i < lrg_midi_output_buffer_get_count (buffer); i++)
    {
        guint8 status;
        guint8 d1;

        lrg_midi_output_buffer_get_message (buffer, i, &status, &d1, NULL, NULL);
        if ((status & 0xF0) == 0x90 && d1 == 61)
            key61++;
        g_assert_false ((status & 0xF0) == 0x90 && d1 == 63);   /* after the loop */
    }
    /* 0.5, 1.5 (wrap) and 2.5 (wrap) */
    g_assert_cmpuint (key61, ==, 3);
    /* every wrap released the channel: CC 123 on channel 2 */
    {
        guint all_off = 0;

        for (i = 0; i < lrg_midi_output_buffer_get_count (buffer); i++)
        {
            guint8 status;
            guint8 d1;

            lrg_midi_output_buffer_get_message (buffer, i, &status, &d1, NULL, NULL);
            if (status == 0xB2 && d1 == 123)
                all_off++;
        }
        g_assert_cmpuint (all_off, ==, 2);
    }
}

/* the volume scales CC 7 and is resent when it changes */
static void
test_sequencer_volume (void)
{
    g_autoptr(LrgMidiOutputBuffer) buffer = NULL;
    g_autoptr(LrgMidiSequencer)    seq = NULL;
    g_autoptr(GBytes)              song = NULL;
    guint8                         status;
    guint8                         d1;
    guint8                         d2;
    guint                          n;

    buffer = lrg_midi_output_buffer_new ();
    seq = lrg_midi_sequencer_new (LRG_MIDI_OUTPUT (buffer));
    lrg_midi_sequencer_set_volume (seq, 0.5f);
    g_assert_cmpfloat (lrg_midi_sequencer_get_volume (seq), ==, 0.5f);
    song = build_song (2, -1, -1);
    g_assert_true (lrg_midi_sequencer_play (seq, song, FALSE, NULL));
    /* the song's CC 7 = 100 went out as 50 */
    lrg_midi_output_buffer_get_message (buffer, 1, &status, &d1, &d2, NULL);
    g_assert_cmpuint (status, ==, 0xB2);
    g_assert_cmpuint (d1, ==, 7);
    g_assert_cmpuint (d2, ==, 50);

    n = lrg_midi_output_buffer_get_count (buffer);
    lrg_midi_sequencer_set_volume (seq, 1.0f);
    g_assert_cmpuint (lrg_midi_output_buffer_get_count (buffer), ==, n + 1);
    lrg_midi_output_buffer_get_message (buffer, n, &status, &d1, &d2, NULL);
    g_assert_cmpuint (status, ==, 0xB2);
    g_assert_cmpuint (d2, ==, 100);
}

/* a malformed file fails and the current song keeps playing */
static void
test_sequencer_malformed (void)
{
    g_autoptr(LrgMidiOutputBuffer) buffer = NULL;
    g_autoptr(LrgMidiSequencer)    seq = NULL;
    g_autoptr(GBytes)              song = NULL;
    g_autoptr(GBytes)              junk = NULL;
    g_autoptr(GBytes)              cut = NULL;
    g_autoptr(GError)              error = NULL;

    buffer = lrg_midi_output_buffer_new ();
    seq = lrg_midi_sequencer_new (LRG_MIDI_OUTPUT (buffer));
    song = build_song (3, -1, -1);
    g_assert_true (lrg_midi_sequencer_play (seq, song, TRUE, NULL));

    junk = g_bytes_new_static ("MThd but not really", 19);
    g_assert_false (lrg_midi_sequencer_play (seq, junk, FALSE, &error));
    g_assert_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID);
    g_clear_error (&error);
    cut = g_bytes_new_from_bytes (song, 0, g_bytes_get_size (song) - 7);
    g_assert_false (lrg_midi_sequencer_play (seq, cut, FALSE, &error));
    g_assert_nonnull (error);
    g_assert_true (lrg_midi_sequencer_is_playing (seq));
    g_assert_cmpfloat_with_epsilon (lrg_midi_sequencer_get_length (seq), 1.5, 1e-6);

    /* a negative step is ignored */
    lrg_midi_sequencer_advance (seq, -1.0);
    g_assert_cmpfloat (lrg_midi_sequencer_get_position (seq), ==, 0.0);
}

gint
main (gint   argc,
      gchar *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/midi-output/messages", test_output_messages);
    g_test_add_func ("/midi-output/alsa", test_output_alsa);
    g_test_add_func ("/midi-output/sequencer/timing", test_sequencer_timing);
    g_test_add_func ("/midi-output/sequencer/lookahead", test_sequencer_lookahead);
    g_test_add_func ("/midi-output/sequencer/loop", test_sequencer_loop);
    g_test_add_func ("/midi-output/sequencer/volume", test_sequencer_volume);
    g_test_add_func ("/midi-output/sequencer/malformed", test_sequencer_malformed);

    return g_test_run ();
}
