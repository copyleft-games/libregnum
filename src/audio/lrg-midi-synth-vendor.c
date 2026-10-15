/* lrg-midi-synth-vendor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The one translation unit that compiles the vendored, unmodified
 * TinySoundFont (tsf.h, MIT) and TinyMidiLoader (tml.h, zlib) by
 * Bernhard Schelling (https://github.com/schellingb/TinySoundFont; the
 * exact revision and the license texts are recorded in
 * docs/modules/audio/midi-synth.org and src/audio/vendor/).  Their
 * directory is on the -isystem path for this file only (Makefile), so
 * their own style (C++ comments, mixed declarations) does not trip the
 * project's warnings; the code below is ours and warning-clean.
 *
 * Everything the rest of libregnum needs goes through the small bridge
 * of lrg-midi-synth-private.h, so the vendored types stay private and
 * out of the introspection scan.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <glib.h>

/* no stdio: SoundFonts and songs are always loaded from memory */
#define TSF_NO_STDIO
#define TSF_IMPLEMENTATION
#include <tsf.h>

#define TML_NO_STDIO
#define TML_IMPLEMENTATION
#include <tml.h>

#include "lrg-midi-synth-private.h"

/* General MIDI: the tenth channel plays the percussion bank */
#define DRUM_CHANNEL   (9)
#define CHANNEL_COUNT  (16)

/* voices allocated up front, so rendering never reallocates */
#define MAX_VOICES     (128)

struct _LrgMidiSynthFont
{
    tsf *font;
};

struct _LrgMidiSynthVoices
{
    tsf *synth;
};

/*
 * clamp_regions:
 * Generator offsets (start/end/loop address offsets) are added to the
 * sample headers' positions by TinySoundFont without a bound, and its
 * linear interpolation reads one sample past the region's end.  Pull
 * every region inside [0, sample_count - 1) so no voice can read past
 * the sample pool, whatever the SoundFont says.
 */
static void
clamp_regions (tsf     *font,
               guint32  sample_count)
{
    guint32 last;
    gint    p;
    gint    r;

    last = sample_count > 0 ? sample_count - 1 : 0;
    for (p = 0; p < font->presetNum; p++)
    {
        struct tsf_preset *preset = &font->presets[p];

        for (r = 0; r < preset->regionNum; r++)
        {
            struct tsf_region *region = &preset->regions[r];

            if (region->end > last)
                region->end = last;
            if (region->offset > region->end)
                region->offset = region->end;
            if (region->loop_end > region->end)
                region->loop_end = region->end;
            if (region->loop_start > region->loop_end)
                region->loop_start = region->loop_end;
        }
    }
}

LrgMidiSynthFont *
lrg_midi_synth_font_load (const guint8 *data,
                          gsize         size,
                          guint32       sample_count,
                          guint         sample_rate)
{
    LrgMidiSynthFont *font;
    tsf              *loaded;

    if (data == NULL || size == 0 || size > (gsize)G_MAXINT)
        return NULL;

    loaded = tsf_load_memory (data, (int)size);
    if (loaded == NULL)
        return NULL;

    clamp_regions (loaded, sample_count);
    tsf_set_output (loaded, TSF_STEREO_INTERLEAVED, (int)sample_rate, 0.0f);

    font = g_new0 (LrgMidiSynthFont, 1);
    font->font = loaded;
    return font;
}

void
lrg_midi_synth_font_free (LrgMidiSynthFont *font)
{
    if (font == NULL)
        return;
    /* songs hold their own references (tsf_copy) to the shared data */
    tsf_close (font->font);
    g_free (font);
}

gint
lrg_midi_synth_font_get_preset_count (LrgMidiSynthFont *font)
{
    g_return_val_if_fail (font != NULL, 0);

    return tsf_get_presetcount (font->font);
}

LrgMidiSynthVoices *
lrg_midi_synth_voices_new (LrgMidiSynthFont *font)
{
    LrgMidiSynthVoices *voices;
    tsf                *copy;
    gint                channel;

    g_return_val_if_fail (font != NULL, NULL);

    copy = tsf_copy (font->font);
    if (copy == NULL)
        return NULL;

    /* fixed voice pool: no allocation while rendering */
    tsf_set_max_voices (copy, MAX_VOICES);

    /*
     * Create all 16 channels at once on General MIDI defaults: preset 0,
     * the drum bank on channel 10.  Program and bank changes of the song
     * follow through lrg_midi_synth_voices_apply().
     */
    for (channel = 0; channel < CHANNEL_COUNT; channel++)
        tsf_channel_set_presetnumber (copy, channel, 0, channel == DRUM_CHANNEL);

    voices = g_new0 (LrgMidiSynthVoices, 1);
    voices->synth = copy;
    return voices;
}

void
lrg_midi_synth_voices_free (LrgMidiSynthVoices *voices)
{
    if (voices == NULL)
        return;
    tsf_close (voices->synth);
    g_free (voices);
}

void
lrg_midi_synth_voices_apply (LrgMidiSynthVoices      *voices,
                             const LrgMidiSynthEvent *event)
{
    tsf  *synth;
    gint  channel;

    g_return_if_fail (voices != NULL);
    g_return_if_fail (event != NULL);

    synth = voices->synth;
    channel = (gint)(event->channel & 0x0f);

    /* the dispatch of TinySoundFont's own MIDI player example */
    switch (event->type)
    {
    case LRG_MIDI_SYNTH_EVENT_PROGRAM_CHANGE:
        tsf_channel_set_presetnumber (synth, channel, (gint)event->data1,
                                      channel == DRUM_CHANNEL);
        break;
    case LRG_MIDI_SYNTH_EVENT_NOTE_ON:
        tsf_channel_note_on (synth, channel, (gint)event->data1,
                             (gfloat)event->data2 / 127.0f);
        break;
    case LRG_MIDI_SYNTH_EVENT_NOTE_OFF:
        tsf_channel_note_off (synth, channel, (gint)event->data1);
        break;
    case LRG_MIDI_SYNTH_EVENT_PITCH_BEND:
        tsf_channel_set_pitchwheel (synth, channel, (gint)event->bend);
        break;
    case LRG_MIDI_SYNTH_EVENT_CONTROL_CHANGE:
        tsf_channel_midi_control (synth, channel, (gint)event->data1,
                                  (gint)event->data2);
        break;
    default:
        /* key and channel pressure: TinySoundFont has no aftertouch */
        break;
    }
}

void
lrg_midi_synth_voices_release_all (LrgMidiSynthVoices *voices)
{
    g_return_if_fail (voices != NULL);

    tsf_note_off_all (voices->synth);
}

gint
lrg_midi_synth_voices_get_active (LrgMidiSynthVoices *voices)
{
    g_return_val_if_fail (voices != NULL, 0);

    return tsf_active_voice_count (voices->synth);
}

void
lrg_midi_synth_voices_render (LrgMidiSynthVoices *voices,
                              gfloat             *frames,
                              guint               n_frames)
{
    g_return_if_fail (voices != NULL);
    g_return_if_fail (frames != NULL || n_frames == 0);

    if (n_frames == 0)
        return;
    tsf_render_float (voices->synth, frames, (int)n_frames, 0);
}

GArray *
lrg_midi_synth_events_load (const guint8 *data,
                            gsize         size)
{
    tml_message *first;
    tml_message *message;
    GArray      *events;

    if (data == NULL || size == 0 || size > (gsize)G_MAXINT)
        return NULL;

    first = tml_load_memory (data, (int)size);
    events = g_array_new (FALSE, TRUE, sizeof (LrgMidiSynthEvent));
    if (first == NULL)
    {
        /* a valid file without channel messages loads as NULL too */
        return events;
    }

    /* the linked list is already ordered by time; keep channel messages */
    for (message = first; message != NULL; message = message->next)
    {
        LrgMidiSynthEvent event;

        switch (message->type)
        {
        case TML_NOTE_OFF:
        case TML_NOTE_ON:
        case TML_KEY_PRESSURE:
        case TML_CONTROL_CHANGE:
        case TML_PROGRAM_CHANGE:
        case TML_CHANNEL_PRESSURE:
        case TML_PITCH_BEND:
            break;
        default:
            continue;
        }

        memset (&event, 0, sizeof (event));
        event.time_ms = (guint32)message->time;
        event.type = (guint8)message->type;
        event.channel = (guint8)(message->channel & 0x0f);
        if (message->type == TML_PITCH_BEND)
        {
            event.bend = (guint16)(message->pitch_bend & 0x3fff);
        }
        else
        {
            event.data1 = (guint8)(message->key & 0x7f);
            event.data2 = (guint8)(message->velocity & 0x7f);
        }
        g_array_append_val (events, event);
    }

    tml_free (first);
    return events;
}
