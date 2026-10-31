/* lrg-chorus.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A three-tap chorus: copies of the input a few milliseconds late whose
 * delays sway with a slow oscillator, which thickens a voice or an
 * instrument (ghostly or many-throated voices).
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

/**
 * LRG_CHORUS_N_TAPS:
 *
 * The delayed copies the chorus mixes in; their oscillators are a third
 * of a cycle apart.
 */
#define LRG_CHORUS_N_TAPS (3)

/**
 * LRG_CHORUS_MAX_DELAY:
 *
 * The longest base delay in seconds (the swing goes up to twice it).
 */
#define LRG_CHORUS_MAX_DELAY (0.1f)

/**
 * LRG_CHORUS_MAX_RATE:
 *
 * The fastest oscillator in Hz.
 */
#define LRG_CHORUS_MAX_RATE (20.0f)

#define LRG_TYPE_CHORUS (lrg_chorus_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgChorus, lrg_chorus, LRG, CHORUS, LrgAudioEffect)

/**
 * lrg_chorus_new:
 *
 * Returns: (transfer full): a new #LrgChorus: 40 ms delay, 0.8 Hz,
 *   depth 0.03, no feedback, dry 1 and every tap at 0.5
 */
LRG_AVAILABLE_IN_ALL
LrgChorus * lrg_chorus_new (void);

/**
 * lrg_chorus_set_delay:
 * @self: an #LrgChorus
 * @delay: the base delay in seconds, 0 .. %LRG_CHORUS_MAX_DELAY (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_chorus_set_delay (LrgChorus *self,
                           gfloat     delay);

/**
 * lrg_chorus_get_delay:
 * @self: an #LrgChorus
 *
 * Returns: the base delay in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_chorus_get_delay (LrgChorus *self);

/**
 * lrg_chorus_set_rate:
 * @self: an #LrgChorus
 * @rate: the oscillator in Hz, 0 .. %LRG_CHORUS_MAX_RATE (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_chorus_set_rate (LrgChorus *self,
                          gfloat     rate);

/**
 * lrg_chorus_get_rate:
 * @self: an #LrgChorus
 *
 * Returns: the oscillator's rate in Hz
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_chorus_get_rate (LrgChorus *self);

/**
 * lrg_chorus_set_depth:
 * @self: an #LrgChorus
 * @depth: how far each tap's delay swings around the base delay, as a
 *   fraction of it, 0 .. 1 (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_chorus_set_depth (LrgChorus *self,
                           gfloat     depth);

/**
 * lrg_chorus_get_depth:
 * @self: an #LrgChorus
 *
 * Returns: the depth, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_chorus_get_depth (LrgChorus *self);

/**
 * lrg_chorus_set_feedback:
 * @self: an #LrgChorus
 * @feedback: how much of the taps' mean goes back into the line, 0 ..
 *   0.95 (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_chorus_set_feedback (LrgChorus *self,
                              gfloat     feedback);

/**
 * lrg_chorus_get_feedback:
 * @self: an #LrgChorus
 *
 * Returns: the feedback
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_chorus_get_feedback (LrgChorus *self);

/**
 * lrg_chorus_set_dry:
 * @self: an #LrgChorus
 * @dry: the direct signal's level, 0 .. 1 (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_chorus_set_dry (LrgChorus *self,
                         gfloat     dry);

/**
 * lrg_chorus_get_dry:
 * @self: an #LrgChorus
 *
 * Returns: the dry level
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_chorus_get_dry (LrgChorus *self);

/**
 * lrg_chorus_set_tap_wet:
 * @self: an #LrgChorus
 * @tap: 0 .. %LRG_CHORUS_N_TAPS - 1
 * @wet: the tap's level, 0 .. 1 (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_chorus_set_tap_wet (LrgChorus *self,
                             guint      tap,
                             gfloat     wet);

/**
 * lrg_chorus_get_tap_wet:
 * @self: an #LrgChorus
 * @tap: 0 .. %LRG_CHORUS_N_TAPS - 1
 *
 * Returns: the tap's level (0 for a tap past the last)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_chorus_get_tap_wet (LrgChorus *self,
                               guint      tap);

G_END_DECLS
