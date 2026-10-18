/* lrg-midi-sequencer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Real-time playback of a Standard MIDI File into an LrgMidiOutput (see
 * lrg-midi-sequencer.h and docs/modules/audio/midi-output.org).
 *
 * The song is parsed by the synthesizer's validated reader
 * (lrg_midi_synth_parse_song(), lrg-midi-synth-private.h) into channel
 * events in milliseconds.  advance() moves the clock, then dispatches
 * from the cursor every event at or before clock + look-ahead with the
 * delay (event time - clock, 0 when already due).  With a loop the
 * dispatch stops at the loop end until the clock reaches it; then the
 * sounding notes are released, the clock wraps by the loop's length and
 * the cursor jumps to the loop start.  Volume is applied by scaling the
 * song's CC 7 values (the last raw value per channel is kept so a volume
 * change can resend them).
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-midi-sequencer.h"
#include "lrg-midi-synth.h"
#include "lrg-midi-synth-private.h"
#include "../lrg-log.h"

#define CC_VOLUME          (7)
#define CC_ALL_NOTES_OFF   (123)
#define GM_DEFAULT_VOLUME  (100)

/*
 * TinyMidiLoader stores event times as whole milliseconds, truncated,
 * while the loop markers are converted exactly: an event on a marker can
 * sit up to 1 ms before it.  The seam is matched with this tolerance.
 */
#define SEAM_TOLERANCE_MS  (1.0)

struct _LrgMidiSequencer
{
    GObject        parent_instance;

    LrgMidiOutput *output;          /* owned */
    GArray        *events;          /* LrgMidiSynthEvent, nullable */
    guint          cursor;          /* next event to send */
    gdouble        clock_ms;
    gdouble        length_ms;
    gdouble        loop_start_ms;
    gdouble        loop_end_ms;
    gboolean       loop;
    gboolean       playing;
    gboolean       finished;

    gfloat         volume;
    gdouble        lookahead;       /* seconds */
    guint8         cc_volume[LRG_MIDI_OUTPUT_CHANNELS];
    guint16        used;            /* channels the song touched */
};

G_DEFINE_FINAL_TYPE (LrgMidiSequencer, lrg_midi_sequencer, G_TYPE_OBJECT)

static void
lrg_midi_sequencer_dispose (GObject *object)
{
    LrgMidiSequencer *self = LRG_MIDI_SEQUENCER (object);

    if (self->output != NULL && self->playing)
        lrg_midi_output_all_notes_off (self->output);
    g_clear_object (&self->output);
    g_clear_pointer (&self->events, g_array_unref);

    G_OBJECT_CLASS (lrg_midi_sequencer_parent_class)->dispose (object);
}

static void
lrg_midi_sequencer_class_init (LrgMidiSequencerClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_midi_sequencer_dispose;
}

static void
lrg_midi_sequencer_init (LrgMidiSequencer *self)
{
    self->volume = 1.0f;
    memset (self->cc_volume, GM_DEFAULT_VOLUME, sizeof (self->cc_volume));
}

LrgMidiSequencer *
lrg_midi_sequencer_new (LrgMidiOutput *output)
{
    LrgMidiSequencer *self;

    g_return_val_if_fail (LRG_IS_MIDI_OUTPUT (output), NULL);

    self = g_object_new (LRG_TYPE_MIDI_SEQUENCER, NULL);
    self->output = g_object_ref (output);
    return self;
}

LrgMidiOutput *
lrg_midi_sequencer_get_output (LrgMidiSequencer *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SEQUENCER (self), NULL);

    return self->output;
}

/* the scaled CC 7 value */
static guint8
scaled_volume (LrgMidiSequencer *self,
               guint8            raw)
{
    return (guint8)CLAMP (lrintf ((gfloat)raw * self->volume), 0, 127);
}

/* one song event to the output */
static void
send_event (LrgMidiSequencer        *self,
            const LrgMidiSynthEvent *event,
            gdouble                  delay)
{
    guint8 channel;

    channel = event->channel & 0x0F;
    self->used |= (guint16)(1u << channel);
    switch (event->type)
    {
    case LRG_MIDI_SYNTH_EVENT_NOTE_OFF:
        (void)lrg_midi_output_note_off (self->output, channel, event->data1, delay);
        break;
    case LRG_MIDI_SYNTH_EVENT_NOTE_ON:
        (void)lrg_midi_output_note_on (self->output, channel, event->data1, event->data2, delay);
        break;
    case LRG_MIDI_SYNTH_EVENT_KEY_PRESSURE:
    {
        guint8 message[3];

        message[0] = (guint8)(0xA0 | channel);
        message[1] = event->data1 & 0x7F;
        message[2] = event->data2 & 0x7F;
        (void)lrg_midi_output_send (self->output, message, 3, delay);
        break;
    }
    case LRG_MIDI_SYNTH_EVENT_CONTROL_CHANGE:
        if (event->data1 == CC_VOLUME)
        {
            self->cc_volume[channel] = event->data2 & 0x7F;
            (void)lrg_midi_output_control_change (self->output, channel, CC_VOLUME,
                                                  scaled_volume (self, self->cc_volume[channel]), delay);
        }
        else
        {
            (void)lrg_midi_output_control_change (self->output, channel, event->data1, event->data2, delay);
        }
        break;
    case LRG_MIDI_SYNTH_EVENT_PROGRAM_CHANGE:
        (void)lrg_midi_output_program_change (self->output, channel, event->data1, delay);
        break;
    case LRG_MIDI_SYNTH_EVENT_CHANNEL_PRESSURE:
    {
        guint8 message[2];

        message[0] = (guint8)(0xD0 | channel);
        message[1] = event->data1 & 0x7F;
        (void)lrg_midi_output_send (self->output, message, 2, delay);
        break;
    }
    case LRG_MIDI_SYNTH_EVENT_PITCH_BEND:
        (void)lrg_midi_output_pitch_bend (self->output, channel, event->bend, delay);
        break;
    default:
        break;
    }
}

/* releases the notes of the channels in use (a loop's seam) */
static void
release_notes (LrgMidiSequencer *self)
{
    guint8 channel;

    for (channel = 0; channel < LRG_MIDI_OUTPUT_CHANNELS; channel++)
        if ((self->used & (1u << channel)) != 0)
            (void)lrg_midi_output_control_change (self->output, channel, CC_ALL_NOTES_OFF, 0, 0.0);
}

/* the index of the first event at or after @time_ms (with the seam tolerance) */
static guint
find_event (LrgMidiSequencer *self,
            gdouble           time_ms)
{
    guint low;
    guint high;

    low = 0;
    high = self->events->len;
    while (low < high)
    {
        guint mid;

        mid = low + (high - low) / 2;
        if ((gdouble)g_array_index (self->events, LrgMidiSynthEvent, mid).time_ms < time_ms - SEAM_TOLERANCE_MS)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}

/*
 * dispatch_until:
 * Sends every event before @limit_ms (and at it when @inclusive),
 * delayed relative to the clock.  A loop's end is exclusive: an event
 * on the seam belongs to the bars after the loop.
 */
static void
dispatch_until (LrgMidiSequencer *self,
                gdouble           limit_ms,
                gboolean          inclusive)
{
    while (self->cursor < self->events->len)
    {
        const LrgMidiSynthEvent *event;
        gdouble                  delay;

        event = &g_array_index (self->events, LrgMidiSynthEvent, self->cursor);
        if ((gdouble)event->time_ms > limit_ms ||
            (!inclusive && (gdouble)event->time_ms >= limit_ms))
            break;
        delay = MAX ((gdouble)event->time_ms - self->clock_ms, 0.0) / 1000.0;
        send_event (self, event, delay);
        self->cursor++;
    }
}

gboolean
lrg_midi_sequencer_play (LrgMidiSequencer  *self,
                         GBytes            *smf,
                         gboolean           loop,
                         GError           **error)
{
    GArray *events;
    gdouble length_ms;
    gdouble loop_start_ms;
    gdouble loop_end_ms;

    g_return_val_if_fail (LRG_IS_MIDI_SEQUENCER (self), FALSE);
    g_return_val_if_fail (smf != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!lrg_midi_synth_parse_song (smf, &events, &length_ms, &loop_start_ms, &loop_end_ms, error))
        return FALSE;

    if (self->playing)
        lrg_midi_output_all_notes_off (self->output);
    g_clear_pointer (&self->events, g_array_unref);
    self->events = events;
    self->cursor = 0;
    self->clock_ms = 0.0;
    self->length_ms = length_ms;
    self->loop_start_ms = loop_start_ms;
    self->loop_end_ms = loop_end_ms;
    self->loop = loop && loop_end_ms > loop_start_ms;
    self->playing = TRUE;
    self->finished = FALSE;
    self->used = 0;
    memset (self->cc_volume, GM_DEFAULT_VOLUME, sizeof (self->cc_volume));

    /* the events at time 0 (program changes, volumes) go out at once */
    lrg_midi_sequencer_advance (self, 0.0);

    lrg_debug (LRG_LOG_DOMAIN_AUDIO, "MIDI sequencer: %u events, %.1f s, loop %s",
               events->len, length_ms / 1000.0, self->loop ? "on" : "off");
    return TRUE;
}

void
lrg_midi_sequencer_stop (LrgMidiSequencer *self)
{
    g_return_if_fail (LRG_IS_MIDI_SEQUENCER (self));

    if (self->events == NULL)
        return;
    lrg_midi_output_all_notes_off (self->output);
    g_clear_pointer (&self->events, g_array_unref);
    self->playing = FALSE;
    self->finished = FALSE;
    self->clock_ms = 0.0;
}

void
lrg_midi_sequencer_advance (LrgMidiSequencer *self,
                            gdouble           seconds)
{
    g_return_if_fail (LRG_IS_MIDI_SEQUENCER (self));

    if (!self->playing || self->events == NULL || !(seconds >= 0.0))
        return;

    self->clock_ms += seconds * 1000.0;
    for (;;)
    {
        gdouble horizon;

        horizon = self->clock_ms + self->lookahead * 1000.0;
        if (!self->loop)
        {
            dispatch_until (self, horizon, TRUE);
            break;
        }
        if (horizon < self->loop_end_ms - SEAM_TOLERANCE_MS)
            dispatch_until (self, horizon, TRUE);
        else
            dispatch_until (self, self->loop_end_ms - SEAM_TOLERANCE_MS, FALSE);
        if (self->clock_ms < self->loop_end_ms)
            break;

        /* the seam: the events up to the end went out above */
        release_notes (self);
        self->clock_ms = self->loop_start_ms +
                         fmod (self->clock_ms - self->loop_end_ms, self->loop_end_ms - self->loop_start_ms);
        self->cursor = find_event (self, self->loop_start_ms);
    }

    if (!self->loop && self->cursor >= self->events->len && self->clock_ms >= self->length_ms)
    {
        release_notes (self);
        self->playing = FALSE;
        self->finished = TRUE;
    }
}

gboolean
lrg_midi_sequencer_is_playing (LrgMidiSequencer *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SEQUENCER (self), FALSE);

    return self->playing;
}

gboolean
lrg_midi_sequencer_is_finished (LrgMidiSequencer *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SEQUENCER (self), FALSE);

    return self->finished;
}

gdouble
lrg_midi_sequencer_get_position (LrgMidiSequencer *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SEQUENCER (self), 0.0);

    return self->events != NULL ? self->clock_ms / 1000.0 : 0.0;
}

gdouble
lrg_midi_sequencer_get_length (LrgMidiSequencer *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SEQUENCER (self), 0.0);

    return self->events != NULL ? self->length_ms / 1000.0 : 0.0;
}

gboolean
lrg_midi_sequencer_get_loop_points (LrgMidiSequencer *self,
                                    gdouble          *loop_start,
                                    gdouble          *loop_end)
{
    gboolean looping;

    g_return_val_if_fail (LRG_IS_MIDI_SEQUENCER (self), FALSE);

    looping = self->events != NULL && self->loop;
    if (loop_start != NULL)
        *loop_start = looping ? self->loop_start_ms / 1000.0 : 0.0;
    if (loop_end != NULL)
        *loop_end = looping ? self->loop_end_ms / 1000.0 : 0.0;
    return looping;
}

void
lrg_midi_sequencer_set_volume (LrgMidiSequencer *self,
                               gfloat            volume)
{
    guint8 channel;

    g_return_if_fail (LRG_IS_MIDI_SEQUENCER (self));

    volume = CLAMP (volume, 0.0f, 1.0f);
    if (volume == self->volume)
        return;
    self->volume = volume;
    if (!self->playing)
        return;
    for (channel = 0; channel < LRG_MIDI_OUTPUT_CHANNELS; channel++)
        if ((self->used & (1u << channel)) != 0)
            (void)lrg_midi_output_control_change (self->output, channel, CC_VOLUME,
                                                  scaled_volume (self, self->cc_volume[channel]), 0.0);
}

gfloat
lrg_midi_sequencer_get_volume (LrgMidiSequencer *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SEQUENCER (self), 0.0f);

    return self->volume;
}

void
lrg_midi_sequencer_set_lookahead (LrgMidiSequencer *self,
                                  gdouble           seconds)
{
    g_return_if_fail (LRG_IS_MIDI_SEQUENCER (self));

    self->lookahead = CLAMP (seconds, 0.0, LRG_MIDI_SEQUENCER_MAX_LOOKAHEAD);
}

gdouble
lrg_midi_sequencer_get_lookahead (LrgMidiSequencer *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_SEQUENCER (self), 0.0);

    return self->lookahead;
}
