/* lrg-foley.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Synthesized foley: short sound clips made by code, no recorded
 * material, so a game has footsteps, armour clinks and rain out of the
 * box (and a pack can still replace any of them).  See
 * docs/modules/audio/foley.org.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_FOLEY_DEFAULT_RATE:
 *
 * The sample rate used when a caller passes 0.
 */
#define LRG_FOLEY_DEFAULT_RATE (44100)

/**
 * LrgFoleyKind:
 * @LRG_FOLEY_FOOTSTEP_DIRT: a soft thud on packed earth
 * @LRG_FOLEY_FOOTSTEP_STONE: a hard heel with a short ring
 * @LRG_FOLEY_FOOTSTEP_WOOD: a hollow knock on boards
 * @LRG_FOLEY_FOOTSTEP_GRASS: a brushing step
 * @LRG_FOLEY_FOOTSTEP_GRAVEL: a crunch of loose stones
 * @LRG_FOLEY_FOOTSTEP_MUD: a wet squelch
 * @LRG_FOLEY_FOOTSTEP_SNOW: a dry crunch
 * @LRG_FOLEY_FOOTSTEP_WATER: a splash with bubbles
 * @LRG_FOLEY_ARMOR_CLINK: a metallic clink of worn armour
 * @LRG_FOLEY_RAIN_LOOP: four seconds of steady rain that loops without a seam
 *
 * What lrg_foley_synthesize() makes.
 */
typedef enum
{
    LRG_FOLEY_FOOTSTEP_DIRT,
    LRG_FOLEY_FOOTSTEP_STONE,
    LRG_FOLEY_FOOTSTEP_WOOD,
    LRG_FOLEY_FOOTSTEP_GRASS,
    LRG_FOLEY_FOOTSTEP_GRAVEL,
    LRG_FOLEY_FOOTSTEP_MUD,
    LRG_FOLEY_FOOTSTEP_SNOW,
    LRG_FOLEY_FOOTSTEP_WATER,
    LRG_FOLEY_ARMOR_CLINK,
    LRG_FOLEY_RAIN_LOOP
} LrgFoleyKind;

LRG_AVAILABLE_IN_ALL
GType lrg_foley_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FOLEY_KIND (lrg_foley_kind_get_type ())

/**
 * lrg_foley_get_duration:
 * @kind: a kind
 *
 * Returns: the clip's length in seconds (0 for an unknown kind)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_foley_get_duration (LrgFoleyKind kind);

/**
 * lrg_foley_synthesize:
 * @kind: what to make
 * @variant: which one: every variant of a kind is a different take
 *   (other pitches, grains, ring), the same variant is always the same
 *   clip
 * @sample_rate: samples per second, 8000 .. 96000; 0 for
 *   %LRG_FOLEY_DEFAULT_RATE
 * @error: return location for a #GError
 *
 * Synthesizes one clip as a complete WAV image: 16-bit signed mono PCM,
 * peak-normalized to about 0.8 and with a fade at both ends (the rain
 * loop's ends meet instead).
 *
 * Returns: (transfer full): the WAV file's bytes, or %NULL with
 *   %G_IO_ERROR_INVALID_ARGUMENT for an unknown kind or a sample rate out
 *   of range
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_foley_synthesize (LrgFoleyKind   kind,
                              guint          variant,
                              guint          sample_rate,
                              GError       **error);

G_END_DECLS
