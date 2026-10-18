/* lrg-midi-output.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Destinations for MIDI messages: the abstract #LrgMidiOutput, a
 * recording #LrgMidiOutputBuffer (tests, tools) and, in
 * lrg-midi-output-alsa.h, the system's ALSA sequencer.  #LrgMidiSequencer
 * plays Standard MIDI Files into any of them, so a game's music can go to
 * a hardware synthesizer or a software one (FluidSynth, a Roland SC-55
 * emulation) instead of #LrgMidiSynth.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_MIDI_OUTPUT_CHANNELS:
 *
 * MIDI channels per output.
 */
#define LRG_MIDI_OUTPUT_CHANNELS (16)

#define LRG_TYPE_MIDI_OUTPUT (lrg_midi_output_get_type ())
#define LRG_MIDI_OUTPUT_ERROR (lrg_midi_output_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgMidiOutput, lrg_midi_output, LRG, MIDI_OUTPUT, GObject)

/**
 * LrgMidiOutputError:
 * @LRG_MIDI_OUTPUT_ERROR_UNSUPPORTED: this libregnum was built without
 *   the backend (ALSA: Fedora `alsa-lib-devel`)
 * @LRG_MIDI_OUTPUT_ERROR_NOT_FOUND: no such port
 * @LRG_MIDI_OUTPUT_ERROR_FAILED: the backend refused (no sequencer
 *   device, the connection failed)
 *
 * Error codes of the MIDI outputs.
 */
typedef enum
{
    LRG_MIDI_OUTPUT_ERROR_UNSUPPORTED,
    LRG_MIDI_OUTPUT_ERROR_NOT_FOUND,
    LRG_MIDI_OUTPUT_ERROR_FAILED
} LrgMidiOutputError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_midi_output_error_quark (void);

/**
 * LrgMidiOutputClass:
 * @parent_class: the parent class
 * @send: delivers one complete MIDI message (status byte first, 1 .. 3
 *   bytes for channel and system common messages) @delay seconds from now
 *   (0: at once); returns %FALSE when it could not be delivered
 * @flush: drops every message scheduled for later and not yet delivered
 *
 * The class of a MIDI destination.
 */
struct _LrgMidiOutputClass
{
    GObjectClass parent_class;

    /*< public >*/
    gboolean (*send)  (LrgMidiOutput *self,
                       const guint8  *message,
                       gsize          length,
                       gdouble        delay);
    void     (*flush) (LrgMidiOutput *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_midi_output_send:
 * @self: an #LrgMidiOutput
 * @message: (array length=length): one MIDI message
 * @length: its bytes (1 .. 3; a message without a status byte first is
 *   refused)
 * @delay: seconds from now, >= 0
 *
 * Returns: %TRUE when the message was accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_output_send (LrgMidiOutput *self,
                               const guint8  *message,
                               gsize          length,
                               gdouble        delay);

/**
 * lrg_midi_output_flush:
 * @self: an #LrgMidiOutput
 *
 * Drops the messages scheduled for later.
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_output_flush (LrgMidiOutput *self);

/**
 * lrg_midi_output_note_on:
 * @self: an #LrgMidiOutput
 * @channel: 0 .. 15
 * @note: 0 .. 127
 * @velocity: 0 .. 127 (0 is a note off by convention)
 * @delay: seconds from now
 *
 * Returns: %TRUE when accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_output_note_on (LrgMidiOutput *self,
                                  guint8         channel,
                                  guint8         note,
                                  guint8         velocity,
                                  gdouble        delay);

/**
 * lrg_midi_output_note_off:
 * @self: an #LrgMidiOutput
 * @channel: 0 .. 15
 * @note: 0 .. 127
 * @delay: seconds from now
 *
 * Returns: %TRUE when accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_output_note_off (LrgMidiOutput *self,
                                   guint8         channel,
                                   guint8         note,
                                   gdouble        delay);

/**
 * lrg_midi_output_control_change:
 * @self: an #LrgMidiOutput
 * @channel: 0 .. 15
 * @control: 0 .. 127
 * @value: 0 .. 127
 * @delay: seconds from now
 *
 * Returns: %TRUE when accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_output_control_change (LrgMidiOutput *self,
                                         guint8         channel,
                                         guint8         control,
                                         guint8         value,
                                         gdouble        delay);

/**
 * lrg_midi_output_program_change:
 * @self: an #LrgMidiOutput
 * @channel: 0 .. 15
 * @program: 0 .. 127
 * @delay: seconds from now
 *
 * Returns: %TRUE when accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_output_program_change (LrgMidiOutput *self,
                                         guint8         channel,
                                         guint8         program,
                                         gdouble        delay);

/**
 * lrg_midi_output_pitch_bend:
 * @self: an #LrgMidiOutput
 * @channel: 0 .. 15
 * @value: 0 .. 16383 (8192 is centred)
 * @delay: seconds from now
 *
 * Returns: %TRUE when accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_output_pitch_bend (LrgMidiOutput *self,
                                     guint8         channel,
                                     guint16        value,
                                     gdouble        delay);

/**
 * lrg_midi_output_all_notes_off:
 * @self: an #LrgMidiOutput
 *
 * Silences the destination at once: drops the scheduled messages, then
 * sends Sustain off, All Notes Off and All Sound Off on every channel.
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_output_all_notes_off (LrgMidiOutput *self);

/**
 * lrg_midi_output_get_sent_count:
 * @self: an #LrgMidiOutput
 *
 * Returns: the messages accepted so far
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_midi_output_get_sent_count (LrgMidiOutput *self);

/* ==========================================================================
 * The recording output
 * ========================================================================== */

#define LRG_TYPE_MIDI_OUTPUT_BUFFER (lrg_midi_output_buffer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMidiOutputBuffer, lrg_midi_output_buffer, LRG, MIDI_OUTPUT_BUFFER, LrgMidiOutput)

/**
 * lrg_midi_output_buffer_new:
 *
 * An output that keeps every message it is sent, with the time it was
 * due: the buffer's clock (lrg_midi_output_buffer_set_clock()) plus the
 * message's delay.  A flush drops the recorded messages due after the
 * clock, as a real output drops what it has not played yet.
 *
 * Returns: (transfer full): a new, empty buffer
 */
LRG_AVAILABLE_IN_ALL
LrgMidiOutputBuffer * lrg_midi_output_buffer_new (void);

/**
 * lrg_midi_output_buffer_set_clock:
 * @self: an #LrgMidiOutputBuffer
 * @seconds: what "now" is for the messages sent from here on
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_output_buffer_set_clock (LrgMidiOutputBuffer *self,
                                       gdouble              seconds);

/**
 * lrg_midi_output_buffer_get_count:
 * @self: an #LrgMidiOutputBuffer
 *
 * Returns: the messages recorded
 */
LRG_AVAILABLE_IN_ALL
guint lrg_midi_output_buffer_get_count (LrgMidiOutputBuffer *self);

/**
 * lrg_midi_output_buffer_get_message:
 * @self: an #LrgMidiOutputBuffer
 * @index: 0 .. lrg_midi_output_buffer_get_count() - 1
 * @out_status: (out) (optional): the status byte
 * @out_data1: (out) (optional): the first data byte (0 without)
 * @out_data2: (out) (optional): the second data byte (0 without)
 * @out_time: (out) (optional): when it was due (seconds)
 *
 * Returns: the message's length in bytes, 0 when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
guint lrg_midi_output_buffer_get_message (LrgMidiOutputBuffer *self,
                                          guint                index,
                                          guint8              *out_status,
                                          guint8              *out_data1,
                                          guint8              *out_data2,
                                          gdouble             *out_time);

/**
 * lrg_midi_output_buffer_count_status:
 * @self: an #LrgMidiOutputBuffer
 * @kind: a status kind (0x80 note off .. 0xE0 pitch bend; the channel
 *   nibble is ignored), or 0 for every message
 *
 * Returns: the recorded messages of that kind (a note on with velocity
 *   0 counts as a note off)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_midi_output_buffer_count_status (LrgMidiOutputBuffer *self,
                                           guint8               kind);

/**
 * lrg_midi_output_buffer_get_flush_count:
 * @self: an #LrgMidiOutputBuffer
 *
 * Returns: how many times the output was flushed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_midi_output_buffer_get_flush_count (LrgMidiOutputBuffer *self);

/**
 * lrg_midi_output_buffer_clear:
 * @self: an #LrgMidiOutputBuffer
 *
 * Forgets the recorded messages and the flush count.
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_output_buffer_clear (LrgMidiOutputBuffer *self);

G_END_DECLS
