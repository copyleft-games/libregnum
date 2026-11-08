/* lrg-sampler.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A multi-zone sampler: recordings (#LrgWaveData) mapped over key and
 * velocity ranges, pitched from their root key with cubic interpolation,
 * optionally looped, shaped by an ADSR.  Several zones over the same
 * range alternate (round robin), so repeated notes do not sound
 * identical.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../audio/lrg-wave-data.h"
#include "lrg-instrument.h"

G_BEGIN_DECLS

#define LRG_TYPE_SAMPLER (lrg_sampler_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSampler, lrg_sampler, LRG, SAMPLER, LrgInstrument)

/**
 * lrg_sampler_new:
 *
 * An empty sampler.  Its parameters are the properties `attack`,
 * `decay`, `sustain`, `release` (seconds / level), `one-shot` (1: play
 * each sample to its end, ignoring note-offs), `velocity` (how much
 * velocity changes the level) and `pitch-track` (1: a semitone per key,
 * 0: every key plays at the root pitch).
 *
 * Returns: (transfer full): a new #LrgSampler
 */
LRG_AVAILABLE_IN_ALL
LrgSampler * lrg_sampler_new (void);

/**
 * lrg_sampler_new_from_wave:
 * @wave: a recording
 * @root_key: the key at which it plays unpitched
 *
 * A sampler with one zone over every key.
 *
 * Returns: (transfer full) (nullable): the sampler, or %NULL for an empty
 *   or invalid @wave
 */
LRG_AVAILABLE_IN_ALL
LrgSampler * lrg_sampler_new_from_wave (LrgWaveData *wave,
                                        gint         root_key);

/**
 * lrg_sampler_add_zone:
 * @self: an #LrgSampler
 * @wave: a recording (copied, converted to float)
 * @root_key: the key at which it plays unpitched (0 .. 127)
 * @low_key: the lowest key of the zone
 * @high_key: the highest key of the zone
 *
 * Adds a zone over every velocity.  Zones are matched in the order they
 * were added; several matches alternate round robin.
 *
 * Returns: the zone index, or -1 for an empty or invalid @wave or keys
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sampler_add_zone (LrgSampler  *self,
                           LrgWaveData *wave,
                           gint         root_key,
                           gint         low_key,
                           gint         high_key);

/**
 * lrg_sampler_set_zone_velocity:
 * @self: an #LrgSampler
 * @zone: a zone index
 * @low: the lowest velocity (0 .. 1) the zone answers
 * @high: the highest velocity
 *
 * Returns: %TRUE for a known zone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sampler_set_zone_velocity (LrgSampler *self,
                                        guint       zone,
                                        gdouble     low,
                                        gdouble     high);

/**
 * lrg_sampler_set_zone_loop:
 * @self: an #LrgSampler
 * @zone: a zone index
 * @start: the first frame of the loop, or -1 for no loop
 * @end: the frame after the loop (exclusive)
 *
 * While the note is held the zone loops [@start, @end); after the
 * release it plays on past @end.
 *
 * Returns: %TRUE for a known zone and a valid range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sampler_set_zone_loop (LrgSampler *self,
                                    guint       zone,
                                    gint64      start,
                                    gint64      end);

/**
 * lrg_sampler_set_zone_gain:
 * @self: an #LrgSampler
 * @zone: a zone index
 * @gain_db: the zone's level in dB
 * @tune_cents: fine tuning in cents
 *
 * Returns: %TRUE for a known zone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sampler_set_zone_gain (LrgSampler *self,
                                    guint       zone,
                                    gdouble     gain_db,
                                    gdouble     tune_cents);

/**
 * lrg_sampler_get_n_zones:
 * @self: an #LrgSampler
 *
 * Returns: the number of zones
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sampler_get_n_zones (LrgSampler *self);

/**
 * lrg_sampler_clear_zones:
 * @self: an #LrgSampler
 *
 * Silences the sampler and removes every zone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sampler_clear_zones (LrgSampler *self);

G_END_DECLS
