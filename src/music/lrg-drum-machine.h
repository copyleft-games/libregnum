/* lrg-drum-machine.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A synthesized drum machine in the analogue tradition: kick, snare,
 * clap, rim, closed/pedal/open hats, three toms, crash, ride and cowbell,
 * all made from oscillators, noise and filters (no samples).  Notes use
 * the General MIDI drum map (36 kick, 38 snare, 42 closed hat, ...).
 * Every piece has `<piece>-tune`, `-decay`, `-tone`, `-snap`, `-level`
 * and `-pan` parameters; a kit (808, 909, Linn) sets them all.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-instrument.h"

G_BEGIN_DECLS

/**
 * LrgDrumKit:
 * @LRG_DRUM_KIT_808: boomy long kick, snappy snare, metallic hats
 * @LRG_DRUM_KIT_909: punchy clicky kick, noisy snare, bright noise hats
 * @LRG_DRUM_KIT_LINN: tight, dry, shorter pieces with gated toms
 *
 * Starting sounds for an #LrgDrumMachine: lrg_drum_machine_set_kit()
 * resets every piece parameter and the synthesis character.
 */
typedef enum
{
	LRG_DRUM_KIT_808,
	LRG_DRUM_KIT_909,
	LRG_DRUM_KIT_LINN
} LrgDrumKit;

LRG_AVAILABLE_IN_ALL
GType lrg_drum_kit_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DRUM_KIT (lrg_drum_kit_get_type ())

#define LRG_TYPE_DRUM_MACHINE (lrg_drum_machine_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDrumMachine, lrg_drum_machine, LRG, DRUM_MACHINE, LrgInstrument)

/**
 * lrg_drum_machine_new:
 * @kit: the starting kit
 *
 * Returns: (transfer full): a new #LrgDrumMachine
 */
LRG_AVAILABLE_IN_ALL
LrgDrumMachine * lrg_drum_machine_new (LrgDrumKit kit);

/**
 * lrg_drum_machine_set_kit:
 * @self: an #LrgDrumMachine
 * @kit: a kit
 *
 * Resets every piece parameter to the kit's values (also the
 * #LrgDrumMachine:kit property).
 */
LRG_AVAILABLE_IN_ALL
void lrg_drum_machine_set_kit (LrgDrumMachine *self,
                               LrgDrumKit      kit);

/**
 * lrg_drum_machine_get_kit:
 * @self: an #LrgDrumMachine
 *
 * Returns: the kit last set
 */
LRG_AVAILABLE_IN_ALL
LrgDrumKit lrg_drum_machine_get_kit (LrgDrumMachine *self);

/**
 * lrg_drum_machine_list_pieces:
 *
 * Returns: (transfer full) (array zero-terminated=1): the piece names in
 *   kit order ("kick", "snare", "clap", "rim", "closed-hat",
 *   "pedal-hat", "open-hat", "low-tom", "mid-tom", "high-tom", "crash",
 *   "ride", "cowbell")
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_drum_machine_list_pieces (void);

/**
 * lrg_drum_machine_piece_to_key:
 * @piece: a piece name, or a short alias ("bd", "sd", "cp", "rs", "ch",
 *   "ph", "oh", "lt", "mt", "ht", "cy", "rd", "cb", "hh" for the closed
 *   hat)
 *
 * Returns: the General MIDI key that plays it, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_drum_machine_piece_to_key (const gchar *piece);

/**
 * lrg_drum_machine_key_to_piece:
 * @key: a MIDI key
 *
 * Returns: (transfer none) (nullable): the piece the key plays, or %NULL
 *   for a key no piece answers
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_drum_machine_key_to_piece (gint key);

G_END_DECLS
