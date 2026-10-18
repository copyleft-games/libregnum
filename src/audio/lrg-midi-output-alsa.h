/* lrg-midi-output-alsa.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A MIDI output to a port of the ALSA sequencer (Linux): a hardware
 * synthesizer behind a USB MIDI interface, or a software one such as
 * FluidSynth, TiMidity++ or Munt.  Built only when pkg-config finds
 * `alsa' (Fedora: alsa-lib-devel; ALSA=0 turns it off); otherwise the
 * type exists and lrg_midi_output_alsa_new() fails with
 * %LRG_MIDI_OUTPUT_ERROR_UNSUPPORTED.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-midi-output.h"

G_BEGIN_DECLS

#define LRG_TYPE_MIDI_OUTPUT_ALSA (lrg_midi_output_alsa_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMidiOutputAlsa, lrg_midi_output_alsa, LRG, MIDI_OUTPUT_ALSA, LrgMidiOutput)

/**
 * lrg_midi_output_alsa_is_available:
 *
 * Returns: %TRUE when this libregnum was built with ALSA
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_output_alsa_is_available (void);

/**
 * lrg_midi_output_alsa_new:
 * @port: the destination: "client:port" numbers ("128:0"), a client
 *   name or a prefix of one ("FLUID Synth", "TiMidity:0"), or "auto" for
 *   the first port that accepts output (software and hardware
 *   synthesizers before "Midi Through")
 * @error: return location for a #GError
 *
 * Opens a sequencer client named "libregnum", creates an output port
 * and connects it to @port.  Messages sent with a delay are scheduled on
 * the client's own real-time queue, so the timing does not depend on
 * when the game's frame runs.
 *
 * Returns: (transfer full) (nullable): the output, or %NULL with @error
 *   (%LRG_MIDI_OUTPUT_ERROR_UNSUPPORTED without ALSA,
 *   %LRG_MIDI_OUTPUT_ERROR_NOT_FOUND for an unknown port,
 *   %LRG_MIDI_OUTPUT_ERROR_FAILED when the sequencer refuses)
 */
LRG_AVAILABLE_IN_ALL
LrgMidiOutputAlsa * lrg_midi_output_alsa_new (const gchar  *port,
                                              GError      **error);

/**
 * lrg_midi_output_alsa_list_ports:
 * @error: return location for a #GError
 *
 * The sequencer's ports that accept output, as "client:port client
 * name:port name" lines (the first word is what lrg_midi_output_alsa_new()
 * takes).
 *
 * Returns: (transfer full) (nullable): the ports (possibly empty), or
 *   %NULL with @error when there is no sequencer
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_midi_output_alsa_list_ports (GError **error);

/**
 * lrg_midi_output_alsa_get_destination:
 * @self: an #LrgMidiOutputAlsa
 *
 * Returns: (transfer none): the connected port as "client:port"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_midi_output_alsa_get_destination (LrgMidiOutputAlsa *self);

G_END_DECLS
