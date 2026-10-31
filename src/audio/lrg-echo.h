/* lrg-echo.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A feedback delay (echo): repeats of the input every delay seconds,
 * each quieter by the feedback; voice doubling, canyons, radio voices.
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
 * LRG_ECHO_MIN_DELAY:
 *
 * The shortest delay in seconds (one millisecond).
 */
#define LRG_ECHO_MIN_DELAY (0.001f)

/**
 * LRG_ECHO_MAX_DELAY:
 *
 * The longest delay in seconds.
 */
#define LRG_ECHO_MAX_DELAY (5.0f)

#define LRG_TYPE_ECHO (lrg_echo_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEcho, lrg_echo, LRG, ECHO, LrgAudioEffect)

/**
 * lrg_echo_new:
 * @delay: seconds between repeats (clamped to %LRG_ECHO_MIN_DELAY ..
 *   %LRG_ECHO_MAX_DELAY)
 * @feedback: how much of each repeat comes back in the next, 0 .. 1
 *
 * Returns: (transfer full): a new #LrgEcho with dry and wet at 1
 */
LRG_AVAILABLE_IN_ALL
LrgEcho * lrg_echo_new (gfloat delay,
                        gfloat feedback);

/**
 * lrg_echo_set_delay:
 * @self: an #LrgEcho
 * @delay: seconds, clamped to %LRG_ECHO_MIN_DELAY .. %LRG_ECHO_MAX_DELAY
 *
 * Sets the time between repeats.  A longer delay than the line holds
 * regrows the line on the next block (and forgets the repeats in it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_echo_set_delay (LrgEcho *self,
                         gfloat   delay);

/**
 * lrg_echo_get_delay:
 * @self: an #LrgEcho
 *
 * Returns: the delay in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_echo_get_delay (LrgEcho *self);

/**
 * lrg_echo_set_feedback:
 * @self: an #LrgEcho
 * @feedback: 0 (one repeat) .. 1 (repeats forever), clamped; values just
 *   under 1 ring for a long time
 *
 * Sets how much of each repeat is fed back into the line.
 */
LRG_AVAILABLE_IN_ALL
void lrg_echo_set_feedback (LrgEcho *self,
                            gfloat   feedback);

/**
 * lrg_echo_get_feedback:
 * @self: an #LrgEcho
 *
 * Returns: the feedback, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_echo_get_feedback (LrgEcho *self);

/**
 * lrg_echo_set_dry:
 * @self: an #LrgEcho
 * @dry: the level of the direct signal, 0 .. 1 (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_echo_set_dry (LrgEcho *self,
                       gfloat   dry);

/**
 * lrg_echo_get_dry:
 * @self: an #LrgEcho
 *
 * Returns: the dry level
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_echo_get_dry (LrgEcho *self);

/**
 * lrg_echo_set_wet:
 * @self: an #LrgEcho
 * @wet: the level of the repeats, 0 .. 1 (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_echo_set_wet (LrgEcho *self,
                       gfloat   wet);

/**
 * lrg_echo_get_wet:
 * @self: an #LrgEcho
 *
 * Returns: the wet level
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_echo_get_wet (LrgEcho *self);

G_END_DECLS
