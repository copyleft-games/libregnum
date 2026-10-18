/* lrg-reverb.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An algorithmic room reverb: eight damped feedback comb filters in
 * parallel and four all-pass diffusers in series per channel (the
 * Schroeder-Moorer design as popularised by Jezar's public-domain
 * "Freeverb"; written here from that published description), with a
 * pre-delay and named room presets.
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
 * LRG_REVERB_MAX_PRE_DELAY:
 *
 * The longest pre-delay in seconds.
 */
#define LRG_REVERB_MAX_PRE_DELAY (0.25f)

/**
 * LrgReverbPreset:
 * @LRG_REVERB_PRESET_OFF: no reverb (dry signal only)
 * @LRG_REVERB_PRESET_ROOM: a small furnished room
 * @LRG_REVERB_PRESET_STONE_ROOM: a bare stone chamber
 * @LRG_REVERB_PRESET_CAVE: a large cave, long dark tail
 * @LRG_REVERB_PRESET_QUARRY: a vast open pit, late echoes
 * @LRG_REVERB_PRESET_HALL: a great hall
 * @LRG_REVERB_PRESET_UNDERWATER: dense, dull and close
 *
 * Starting points named after the classic environmental-audio presets
 * (the values are libregnum's own); lrg_reverb_set_preset() loads one.
 */
typedef enum
{
    LRG_REVERB_PRESET_OFF,
    LRG_REVERB_PRESET_ROOM,
    LRG_REVERB_PRESET_STONE_ROOM,
    LRG_REVERB_PRESET_CAVE,
    LRG_REVERB_PRESET_QUARRY,
    LRG_REVERB_PRESET_HALL,
    LRG_REVERB_PRESET_UNDERWATER
} LrgReverbPreset;

LRG_AVAILABLE_IN_ALL
GType lrg_reverb_preset_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_REVERB_PRESET (lrg_reverb_preset_get_type ())

#define LRG_TYPE_REVERB (lrg_reverb_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReverb, lrg_reverb, LRG, REVERB, LrgAudioEffect)

/**
 * lrg_reverb_new:
 *
 * A reverb with the %LRG_REVERB_PRESET_ROOM settings.  Its delay lines
 * are sized on the first block of each sample rate.
 *
 * Returns: (transfer full): a new #LrgReverb
 */
LRG_AVAILABLE_IN_ALL
LrgReverb * lrg_reverb_new (void);

/**
 * lrg_reverb_set_preset:
 * @self: an #LrgReverb
 * @preset: the room to load
 *
 * Sets room size, damping, wet, dry, width and pre-delay together.  The
 * wet and dry levels glide to their new values (no click); the tail of
 * the previous room keeps ringing out.
 */
LRG_AVAILABLE_IN_ALL
void lrg_reverb_set_preset (LrgReverb       *self,
                            LrgReverbPreset  preset);

/**
 * lrg_reverb_get_preset:
 * @self: an #LrgReverb
 *
 * Returns: the last preset loaded (the parameters may have been changed
 *   since)
 */
LRG_AVAILABLE_IN_ALL
LrgReverbPreset lrg_reverb_get_preset (LrgReverb *self);

/**
 * lrg_reverb_set_room_size:
 * @self: an #LrgReverb
 * @room_size: 0 (tiny, short tail) .. 1 (huge, long tail)
 */
LRG_AVAILABLE_IN_ALL
void lrg_reverb_set_room_size (LrgReverb *self,
                               gfloat     room_size);

/**
 * lrg_reverb_get_room_size:
 * @self: an #LrgReverb
 *
 * Returns: the room size, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_reverb_get_room_size (LrgReverb *self);

/**
 * lrg_reverb_set_damping:
 * @self: an #LrgReverb
 * @damping: 0 (bright walls) .. 1 (high frequencies die fast)
 */
LRG_AVAILABLE_IN_ALL
void lrg_reverb_set_damping (LrgReverb *self,
                             gfloat     damping);

/**
 * lrg_reverb_get_damping:
 * @self: an #LrgReverb
 *
 * Returns: the damping, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_reverb_get_damping (LrgReverb *self);

/**
 * lrg_reverb_set_wet:
 * @self: an #LrgReverb
 * @wet: level of the reverberated signal, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
void lrg_reverb_set_wet (LrgReverb *self,
                         gfloat     wet);

/**
 * lrg_reverb_get_wet:
 * @self: an #LrgReverb
 *
 * Returns: the wet level (the target the output glides to)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_reverb_get_wet (LrgReverb *self);

/**
 * lrg_reverb_set_dry:
 * @self: an #LrgReverb
 * @dry: level of the original signal, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
void lrg_reverb_set_dry (LrgReverb *self,
                         gfloat     dry);

/**
 * lrg_reverb_get_dry:
 * @self: an #LrgReverb
 *
 * Returns: the dry level (the target the output glides to)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_reverb_get_dry (LrgReverb *self);

/**
 * lrg_reverb_set_width:
 * @self: an #LrgReverb
 * @width: stereo width of the tail, 0 (mono) .. 1 (fully decorrelated)
 */
LRG_AVAILABLE_IN_ALL
void lrg_reverb_set_width (LrgReverb *self,
                           gfloat     width);

/**
 * lrg_reverb_get_width:
 * @self: an #LrgReverb
 *
 * Returns: the stereo width, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_reverb_get_width (LrgReverb *self);

/**
 * lrg_reverb_set_pre_delay:
 * @self: an #LrgReverb
 * @seconds: delay before the tail starts, 0 .. %LRG_REVERB_MAX_PRE_DELAY
 */
LRG_AVAILABLE_IN_ALL
void lrg_reverb_set_pre_delay (LrgReverb *self,
                               gfloat     seconds);

/**
 * lrg_reverb_get_pre_delay:
 * @self: an #LrgReverb
 *
 * Returns: the pre-delay in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_reverb_get_pre_delay (LrgReverb *self);

G_END_DECLS
