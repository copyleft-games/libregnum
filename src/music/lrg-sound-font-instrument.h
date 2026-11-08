/* lrg-sound-font-instrument.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An instrument that plays one preset of a SoundFont (.sf2) bank through
 * the vendored TinySoundFont, note by note (where #LrgMidiSynth plays
 * whole MIDI files).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-instrument.h"

G_BEGIN_DECLS

#define LRG_TYPE_SOUND_FONT_INSTRUMENT (lrg_sound_font_instrument_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSoundFontInstrument, lrg_sound_font_instrument, LRG, SOUND_FONT_INSTRUMENT,
                      LrgInstrument)

/**
 * lrg_sound_font_instrument_new_from_bytes:
 * @sf2: the SoundFont file's bytes
 * @error: (nullable): return location for an error
 *
 * Checks and loads the bank (%LRG_MIDI_SYNTH_ERROR_INVALID on a bad
 * one).  The #LrgSoundFontInstrument:program property picks the preset
 * (General MIDI program number), #LrgSoundFontInstrument:drums the
 * percussion bank.
 *
 * Returns: (transfer full) (nullable): the instrument, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgSoundFontInstrument * lrg_sound_font_instrument_new_from_bytes (GBytes  *sf2,
                                                                   GError **error);

/**
 * lrg_sound_font_instrument_new_from_file:
 * @path: a .sf2 file
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the instrument, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgSoundFontInstrument * lrg_sound_font_instrument_new_from_file (const gchar  *path,
                                                                  GError      **error);

/**
 * lrg_sound_font_instrument_get_preset_count:
 * @self: an #LrgSoundFontInstrument
 *
 * Returns: the presets in the bank
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sound_font_instrument_get_preset_count (LrgSoundFontInstrument *self);

G_END_DECLS
