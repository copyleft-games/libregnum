/* lrg-audio-bus.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A submix bus: voices (graylib sounds, sound aliases, music and audio
 * streams) routed into it are summed, run through the bus's effect chain
 * and mixed into the device output.  An effect send (a reverb only some
 * voices reach) or an insert (a voice filter a group of voices plays
 * through).
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
 * LRG_AUDIO_BUS_MAX_FRAMES:
 *
 * The frames a bus collects between two mixes (one device block): sends
 * past it in one block are dropped.  Far more than a device block.
 */
#define LRG_AUDIO_BUS_MAX_FRAMES (8192)

/**
 * LRG_AUDIO_BUS_MAX_LEVEL:
 *
 * The highest bus return and route level.
 */
#define LRG_AUDIO_BUS_MAX_LEVEL (4.0f)

/**
 * LrgAudioRoute:
 * @LRG_AUDIO_ROUTE_SEND: the voice stays in the device mix as it was and
 *   a copy goes into the bus (an effect send: a shared reverb)
 * @LRG_AUDIO_ROUTE_INSERT: the voice is taken out of the device mix and
 *   heard only through the bus (a filter on a group of voices)
 *
 * How a voice reaches a bus.  Either way the copy carries the voice's
 * own volume and pan (raylib's pan law) times the route's level.
 */
typedef enum
{
    LRG_AUDIO_ROUTE_SEND,
    LRG_AUDIO_ROUTE_INSERT
} LrgAudioRoute;

LRG_AVAILABLE_IN_ALL
GType lrg_audio_route_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_AUDIO_ROUTE (lrg_audio_route_get_type ())

#define LRG_TYPE_AUDIO_BUS (lrg_audio_bus_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAudioBus, lrg_audio_bus, LRG, AUDIO_BUS, GObject)

/**
 * lrg_audio_bus_new:
 *
 * Returns: (transfer full): a new, empty bus with a return level of 1,
 *   not on the device mix
 */
LRG_AVAILABLE_IN_ALL
LrgAudioBus * lrg_audio_bus_new (void);

/**
 * lrg_audio_bus_get_chain:
 * @self: an #LrgAudioBus
 *
 * The effects the bus's sum runs through, in order (an empty chain
 * passes it unchanged).  Add and remove effects on it at any time.
 *
 * Returns: (transfer none): the bus's effect chain
 */
LRG_AVAILABLE_IN_ALL
LrgAudioEffectChain * lrg_audio_bus_get_chain (LrgAudioBus *self);

/**
 * lrg_audio_bus_add_effect:
 * @self: an #LrgAudioBus
 * @effect: an effect
 *
 * Appends @effect to the bus's chain (lrg_audio_effect_chain_add()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_bus_add_effect (LrgAudioBus    *self,
                               LrgAudioEffect *effect);

/**
 * lrg_audio_bus_set_level:
 * @self: an #LrgAudioBus
 * @level: the return level the bus's output is mixed in at, 0 ..
 *   %LRG_AUDIO_BUS_MAX_LEVEL (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_bus_set_level (LrgAudioBus *self,
                              gfloat       level);

/**
 * lrg_audio_bus_get_level:
 * @self: an #LrgAudioBus
 *
 * Returns: the return level
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_audio_bus_get_level (LrgAudioBus *self);

/**
 * lrg_audio_bus_route:
 * @self: an #LrgAudioBus
 * @voice: a #GrlSound (a sound or an alias), #GrlMusic or #GrlAudioStream
 * @route: how the voice reaches the bus
 * @level: the route's level, 0 .. %LRG_AUDIO_BUS_MAX_LEVEL (clamped)
 *
 * Routes @voice into the bus through a graylib processor on the voice.
 * A voice already routed here gets the new @route and @level.  The
 * route ends with lrg_audio_bus_unroute(), when the voice is disposed or
 * when the bus is.
 *
 * A voice's processors run in the order its routes were made, and an
 * insert silences the voice for every processor after it: route a voice
 * to its sends before its insert.
 *
 * The bus holds no reference on @voice.
 *
 * Returns: %TRUE when @voice is routed here; %FALSE for an object of
 *   another type or when graylib has no processor slot left
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_bus_route (LrgAudioBus   *self,
                              GObject       *voice,
                              LrgAudioRoute  route,
                              gfloat         level);

/**
 * lrg_audio_bus_unroute:
 * @self: an #LrgAudioBus
 * @voice: a voice routed here
 *
 * Ends a route: once this returns the voice no longer reaches the bus
 * (an inserted voice is heard in the device mix again).
 *
 * Returns: %TRUE when @voice was routed here
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_bus_unroute (LrgAudioBus *self,
                                GObject     *voice);

/**
 * lrg_audio_bus_is_routed:
 * @self: an #LrgAudioBus
 * @voice: a voice
 *
 * Returns: whether @voice is routed into this bus
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_bus_is_routed (LrgAudioBus *self,
                                  GObject     *voice);

/**
 * lrg_audio_bus_get_route_level:
 * @self: an #LrgAudioBus
 * @voice: a voice
 * @out_route: (out) (optional): the route's kind
 *
 * Returns: the route's level, or -1 when @voice is not routed here
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_audio_bus_get_route_level (LrgAudioBus   *self,
                                      GObject       *voice,
                                      LrgAudioRoute *out_route);

/**
 * lrg_audio_bus_get_n_routes:
 * @self: an #LrgAudioBus
 *
 * Returns: how many voices are routed here
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_bus_get_n_routes (LrgAudioBus *self);

/**
 * lrg_audio_bus_mix: (skip)
 * @self: an #LrgAudioBus
 * @out: interleaved stereo frames the bus's output is added to
 * @n_frames: frames in @out
 * @sample_rate: the rate the effects run at (Hz)
 *
 * One block of the bus: takes what its voices sent since the last mix,
 * runs the chain over it (also when nothing was sent, so tails ring
 * out) and adds it to @out times the return level.  An attached bus is
 * mixed by the device mix on its own; this is for offline rendering and
 * tests of a bus that is not attached.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_bus_mix (LrgAudioBus *self,
                        gfloat      *out,
                        guint        n_frames,
                        guint        sample_rate);

/**
 * lrg_audio_bus_reset:
 * @self: an #LrgAudioBus
 *
 * Drops what the voices sent and resets the chain's effects (tails,
 * delay lines).
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_bus_reset (LrgAudioBus *self);

/**
 * lrg_audio_bus_attach_mix:
 * @self: an #LrgAudioBus
 * @sample_rate: the device's rate in Hz, or 0 to assume
 *   %LRG_AUDIO_EFFECT_DEFAULT_SAMPLE_RATE and measure it (as
 *   lrg_audio_effect_chain_attach_mix() does)
 *
 * Mixes the bus into the device output from now on, before an effect
 * chain on the final mix runs.  Several buses can be attached at once.
 *
 * Returns: %FALSE without an open audio device
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_bus_attach_mix (LrgAudioBus *self,
                                   guint        sample_rate);

/**
 * lrg_audio_bus_detach_mix:
 * @self: an #LrgAudioBus
 *
 * Takes the bus off the device output (its inserted voices fall silent
 * until it is attached again or they are unrouted).  Disposing an
 * attached bus detaches it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_bus_detach_mix (LrgAudioBus *self);

/**
 * lrg_audio_bus_is_attached:
 * @self: an #LrgAudioBus
 *
 * Returns: whether the bus is on the device output
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_bus_is_attached (LrgAudioBus *self);

/**
 * lrg_audio_bus_get_mix_rate:
 * @self: an #LrgAudioBus
 * @out_measured: (out) (optional): whether the rate was measured
 *
 * Returns: the rate the attached bus runs at, 0 when not attached
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_bus_get_mix_rate (LrgAudioBus *self,
                                  gboolean    *out_measured);

G_END_DECLS
