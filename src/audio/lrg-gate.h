/* lrg-gate.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A noise gate: below the threshold the signal is turned down by the
 * range.  After a reverb it cuts the tail short, the 1980s gated snare.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-audio-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_GATE (lrg_gate_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGate, lrg_gate, LRG, GATE, LrgAudioEffect)

/**
 * lrg_gate_new:
 * @threshold_db: the opening level in dBFS (-90 .. 0)
 *
 * Creates the effect; every parameter is also a GObject property (#LrgGate:threshold, #LrgGate:attack, #LrgGate:hold, #LrgGate:release, #LrgGate:range).
 * Out-of-range arguments are clamped.
 *
 * Returns: (transfer full): a new #LrgGate
 */
LRG_AVAILABLE_IN_ALL
LrgGate * lrg_gate_new (gdouble threshold_db);

G_END_DECLS
