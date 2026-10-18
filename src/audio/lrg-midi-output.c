/* lrg-midi-output.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The abstract MIDI output and the recording buffer (see
 * lrg-midi-output.h and docs/modules/audio/midi-output.org).  The base
 * class validates every message (a status byte first, the length the
 * status implies, data bytes below 0x80) before a backend sees it, so
 * backends never parse untrusted bytes.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>

#include "lrg-midi-output.h"

/* CC numbers of the channel mode messages */
#define CC_SUSTAIN        (64)
#define CC_ALL_SOUND_OFF  (120)
#define CC_ALL_NOTES_OFF  (123)

typedef struct
{
    guint64 sent;
} LrgMidiOutputPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgMidiOutput, lrg_midi_output, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-midi-output-error-quark, lrg_midi_output_error)

static void
lrg_midi_output_real_flush (LrgMidiOutput *self)
{
    (void)self;
}

static void
lrg_midi_output_class_init (LrgMidiOutputClass *klass)
{
    klass->flush = lrg_midi_output_real_flush;
}

static void
lrg_midi_output_init (LrgMidiOutput *self)
{
    (void)self;
}

/* the length a status byte implies, 0 for one we do not send */
static gsize
message_length (guint8 status)
{
    switch (status & 0xF0)
    {
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
        return 3;
    case 0xC0:
    case 0xD0:
        return 2;
    default:
        break;
    }
    switch (status)
    {
    case 0xF1:
    case 0xF3:
        return 2;
    case 0xF2:
        return 3;
    case 0xF6:
    case 0xF8:
    case 0xFA:
    case 0xFB:
    case 0xFC:
    case 0xFE:
    case 0xFF:
        return 1;
    default:
        return 0;
    }
}

gboolean
lrg_midi_output_send (LrgMidiOutput *self,
                      const guint8  *message,
                      gsize          length,
                      gdouble        delay)
{
    LrgMidiOutputPrivate *priv;
    LrgMidiOutputClass   *klass;
    gsize                 i;

    g_return_val_if_fail (LRG_IS_MIDI_OUTPUT (self), FALSE);
    g_return_val_if_fail (message != NULL || length == 0, FALSE);

    if (length == 0 || (message[0] & 0x80) == 0 || message_length (message[0]) != length)
        return FALSE;
    for (i = 1; i < length; i++)
        if ((message[i] & 0x80) != 0)
            return FALSE;

    klass = LRG_MIDI_OUTPUT_GET_CLASS (self);
    if (klass->send == NULL || !klass->send (self, message, length, MAX (delay, 0.0)))
        return FALSE;
    priv = lrg_midi_output_get_instance_private (self);
    priv->sent++;
    return TRUE;
}

void
lrg_midi_output_flush (LrgMidiOutput *self)
{
    g_return_if_fail (LRG_IS_MIDI_OUTPUT (self));

    LRG_MIDI_OUTPUT_GET_CLASS (self)->flush (self);
}

static gboolean
send3 (LrgMidiOutput *self,
       guint8         status,
       guint8         data1,
       guint8         data2,
       gdouble        delay)
{
    guint8 message[3];

    message[0] = status;
    message[1] = data1 & 0x7F;
    message[2] = data2 & 0x7F;
    return lrg_midi_output_send (self, message, message_length (status), delay);
}

gboolean
lrg_midi_output_note_on (LrgMidiOutput *self,
                         guint8         channel,
                         guint8         note,
                         guint8         velocity,
                         gdouble        delay)
{
    g_return_val_if_fail (channel < LRG_MIDI_OUTPUT_CHANNELS, FALSE);

    return send3 (self, (guint8)(0x90 | channel), note, velocity, delay);
}

gboolean
lrg_midi_output_note_off (LrgMidiOutput *self,
                          guint8         channel,
                          guint8         note,
                          gdouble        delay)
{
    g_return_val_if_fail (channel < LRG_MIDI_OUTPUT_CHANNELS, FALSE);

    return send3 (self, (guint8)(0x80 | channel), note, 0, delay);
}

gboolean
lrg_midi_output_control_change (LrgMidiOutput *self,
                                guint8         channel,
                                guint8         control,
                                guint8         value,
                                gdouble        delay)
{
    g_return_val_if_fail (channel < LRG_MIDI_OUTPUT_CHANNELS, FALSE);

    return send3 (self, (guint8)(0xB0 | channel), control, value, delay);
}

gboolean
lrg_midi_output_program_change (LrgMidiOutput *self,
                                guint8         channel,
                                guint8         program,
                                gdouble        delay)
{
    g_return_val_if_fail (channel < LRG_MIDI_OUTPUT_CHANNELS, FALSE);

    return send3 (self, (guint8)(0xC0 | channel), program, 0, delay);
}

gboolean
lrg_midi_output_pitch_bend (LrgMidiOutput *self,
                            guint8         channel,
                            guint16        value,
                            gdouble        delay)
{
    g_return_val_if_fail (channel < LRG_MIDI_OUTPUT_CHANNELS, FALSE);

    value = MIN (value, (guint16)16383);
    return send3 (self, (guint8)(0xE0 | channel), (guint8)(value & 0x7F), (guint8)(value >> 7), delay);
}

void
lrg_midi_output_all_notes_off (LrgMidiOutput *self)
{
    guint8 channel;

    g_return_if_fail (LRG_IS_MIDI_OUTPUT (self));

    lrg_midi_output_flush (self);
    for (channel = 0; channel < LRG_MIDI_OUTPUT_CHANNELS; channel++)
    {
        (void)lrg_midi_output_control_change (self, channel, CC_SUSTAIN, 0, 0.0);
        (void)lrg_midi_output_control_change (self, channel, CC_ALL_NOTES_OFF, 0, 0.0);
        (void)lrg_midi_output_control_change (self, channel, CC_ALL_SOUND_OFF, 0, 0.0);
    }
}

guint64
lrg_midi_output_get_sent_count (LrgMidiOutput *self)
{
    LrgMidiOutputPrivate *priv;

    g_return_val_if_fail (LRG_IS_MIDI_OUTPUT (self), 0);

    priv = lrg_midi_output_get_instance_private (self);
    return priv->sent;
}

/* ==========================================================================
 * LrgMidiOutputBuffer
 * ========================================================================== */

typedef struct
{
    gdouble time;
    guint8  bytes[3];
    guint8  length;
} BufferedMessage;

struct _LrgMidiOutputBuffer
{
    LrgMidiOutput parent_instance;

    GArray       *messages;   /* BufferedMessage, in the order sent */
    gdouble       clock;
    guint         flushes;
};

G_DEFINE_FINAL_TYPE (LrgMidiOutputBuffer, lrg_midi_output_buffer, LRG_TYPE_MIDI_OUTPUT)

static gboolean
lrg_midi_output_buffer_send (LrgMidiOutput *output,
                             const guint8  *message,
                             gsize          length,
                             gdouble        delay)
{
    LrgMidiOutputBuffer *self = LRG_MIDI_OUTPUT_BUFFER (output);
    BufferedMessage      entry;

    memset (&entry, 0, sizeof (entry));
    entry.time = self->clock + delay;
    entry.length = (guint8)length;
    memcpy (entry.bytes, message, length);
    g_array_append_val (self->messages, entry);
    return TRUE;
}

/* what a real output has not played yet: due after the clock */
static void
lrg_midi_output_buffer_flush (LrgMidiOutput *output)
{
    LrgMidiOutputBuffer *self = LRG_MIDI_OUTPUT_BUFFER (output);
    guint                i;

    self->flushes++;
    i = 0;
    while (i < self->messages->len)
    {
        if (g_array_index (self->messages, BufferedMessage, i).time > self->clock)
            g_array_remove_index (self->messages, i);
        else
            i++;
    }
}

static void
lrg_midi_output_buffer_finalize (GObject *object)
{
    LrgMidiOutputBuffer *self = LRG_MIDI_OUTPUT_BUFFER (object);

    g_array_unref (self->messages);

    G_OBJECT_CLASS (lrg_midi_output_buffer_parent_class)->finalize (object);
}

static void
lrg_midi_output_buffer_class_init (LrgMidiOutputBufferClass *klass)
{
    GObjectClass       *object_class = G_OBJECT_CLASS (klass);
    LrgMidiOutputClass *output_class = LRG_MIDI_OUTPUT_CLASS (klass);

    object_class->finalize = lrg_midi_output_buffer_finalize;
    output_class->send = lrg_midi_output_buffer_send;
    output_class->flush = lrg_midi_output_buffer_flush;
}

static void
lrg_midi_output_buffer_init (LrgMidiOutputBuffer *self)
{
    self->messages = g_array_new (FALSE, TRUE, sizeof (BufferedMessage));
}

LrgMidiOutputBuffer *
lrg_midi_output_buffer_new (void)
{
    return g_object_new (LRG_TYPE_MIDI_OUTPUT_BUFFER, NULL);
}

void
lrg_midi_output_buffer_set_clock (LrgMidiOutputBuffer *self,
                                  gdouble              seconds)
{
    g_return_if_fail (LRG_IS_MIDI_OUTPUT_BUFFER (self));

    self->clock = seconds;
}

guint
lrg_midi_output_buffer_get_count (LrgMidiOutputBuffer *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_OUTPUT_BUFFER (self), 0);

    return self->messages->len;
}

guint
lrg_midi_output_buffer_get_message (LrgMidiOutputBuffer *self,
                                    guint                index,
                                    guint8              *out_status,
                                    guint8              *out_data1,
                                    guint8              *out_data2,
                                    gdouble             *out_time)
{
    const BufferedMessage *entry;

    g_return_val_if_fail (LRG_IS_MIDI_OUTPUT_BUFFER (self), 0);

    if (index >= self->messages->len)
        return 0;
    entry = &g_array_index (self->messages, BufferedMessage, index);
    if (out_status != NULL)
        *out_status = entry->bytes[0];
    if (out_data1 != NULL)
        *out_data1 = entry->length > 1 ? entry->bytes[1] : 0;
    if (out_data2 != NULL)
        *out_data2 = entry->length > 2 ? entry->bytes[2] : 0;
    if (out_time != NULL)
        *out_time = entry->time;
    return entry->length;
}

guint
lrg_midi_output_buffer_count_status (LrgMidiOutputBuffer *self,
                                     guint8               kind)
{
    guint count;
    guint i;

    g_return_val_if_fail (LRG_IS_MIDI_OUTPUT_BUFFER (self), 0);

    count = 0;
    for (i = 0; i < self->messages->len; i++)
    {
        const BufferedMessage *entry;
        guint8                 status;

        entry = &g_array_index (self->messages, BufferedMessage, i);
        status = entry->bytes[0] & 0xF0;
        if (status == 0x90 && entry->length == 3 && entry->bytes[2] == 0)
            status = 0x80;
        if (kind == 0 || status == (kind & 0xF0))
            count++;
    }
    return count;
}

guint
lrg_midi_output_buffer_get_flush_count (LrgMidiOutputBuffer *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_OUTPUT_BUFFER (self), 0);

    return self->flushes;
}

void
lrg_midi_output_buffer_clear (LrgMidiOutputBuffer *self)
{
    g_return_if_fail (LRG_IS_MIDI_OUTPUT_BUFFER (self));

    g_array_set_size (self->messages, 0);
    self->flushes = 0;
}
