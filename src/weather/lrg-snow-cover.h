/* lrg-snow-cover.h - snow that builds up, melts and takes tracks
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The amount of lying snow as one level (0 bare .. 1 deep), driven by
 * snowfall and warmth over time, plus the tracks walked into it: each
 * track is a dent of a radius and a depth that new snow fills and
 * melting snow fades.  Headless; #LrgGroundOverlay draws it.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_SNOW_COVER (lrg_snow_cover_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSnowCover, lrg_snow_cover, LRG, SNOW_COVER, GObject)

/**
 * LRG_SNOW_COVER_MAX_TRACKS:
 *
 * Tracks kept; a new track beyond this replaces the oldest.
 */
#define LRG_SNOW_COVER_MAX_TRACKS (64)

/**
 * lrg_snow_cover_new:
 *
 * Bare ground.  Heavy snowfall fills the level in 6 hours (rate 1/6
 * per hour), full warmth melts it in 12 (1/12 per hour); tracks fill
 * at 4 x the level's rate.
 *
 * Returns: (transfer full): a new #LrgSnowCover
 */
LRG_AVAILABLE_IN_ALL
LrgSnowCover *      lrg_snow_cover_new              (void);

/**
 * lrg_snow_cover_set_rates:
 * @self: an #LrgSnowCover
 * @build_per_hour: level gained per hour of full snowfall (>= 0)
 * @melt_per_hour: level lost per hour at full warmth (>= 0)
 *
 * Invalid values are ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_snow_cover_set_rates        (LrgSnowCover *self,
                                                     gdouble       build_per_hour,
                                                     gdouble       melt_per_hour);

/**
 * lrg_snow_cover_update:
 * @self: an #LrgSnowCover
 * @seconds: time that passed (game seconds)
 * @snowfall: how hard it snows, 0 .. 1
 * @warmth: how fast it melts, 0 (freezing) .. 1 (warm sun)
 *
 * level += (snowfall x build - warmth x melt) x hours, clamped to
 * 0 .. 1.  Every track loses depth by (snowfall x 4 x build + warmth x
 * melt) x hours and is dropped once filled; bare ground (level 0)
 * drops all tracks.  Long times (resting) integrate in one step: the
 * model is linear between updates.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_snow_cover_update           (LrgSnowCover *self,
                                                     gdouble       seconds,
                                                     gdouble       snowfall,
                                                     gdouble       warmth);

/**
 * lrg_snow_cover_get_level:
 * @self: an #LrgSnowCover
 *
 * Returns: the lying snow, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gdouble             lrg_snow_cover_get_level        (LrgSnowCover *self);

/**
 * lrg_snow_cover_set_level:
 * @self: an #LrgSnowCover
 * @level: the lying snow (clamped to 0 .. 1; a restored level)
 */
LRG_AVAILABLE_IN_ALL
void                lrg_snow_cover_set_level        (LrgSnowCover *self,
                                                     gdouble       level);

/**
 * lrg_snow_cover_stamp:
 * @self: an #LrgSnowCover
 * @x: absolute world x of the footprint, metres
 * @z: absolute world z
 * @radius: footprint radius, metres (> 0)
 * @depth: how deep, 0 .. 1 of the lying snow
 *
 * Walks a footprint into the snow.  Nothing happens on bare ground or
 * for invalid values; a stamp closer than half its radius to the newest
 * track deepens that track instead of adding one (standing still does
 * not fill the list).
 *
 * Returns: %TRUE when a track was added or deepened
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_snow_cover_stamp            (LrgSnowCover *self,
                                                     gdouble       x,
                                                     gdouble       z,
                                                     gdouble       radius,
                                                     gdouble       depth);

/**
 * lrg_snow_cover_get_track_count:
 * @self: an #LrgSnowCover
 *
 * Returns: the tracks kept, newest last
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_snow_cover_get_track_count  (LrgSnowCover *self);

/**
 * lrg_snow_cover_get_track:
 * @self: an #LrgSnowCover
 * @index: 0 (oldest) .. count - 1 (newest)
 * @x: (out) (optional): world x
 * @z: (out) (optional): world z
 * @radius: (out) (optional): radius
 * @depth: (out) (optional): depth left, 0 .. 1
 *
 * Returns: %TRUE when @index is a track
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_snow_cover_get_track        (LrgSnowCover *self,
                                                     guint         index,
                                                     gdouble      *x,
                                                     gdouble      *z,
                                                     gdouble      *radius,
                                                     gdouble      *depth);

/**
 * lrg_snow_cover_clear_tracks:
 * @self: an #LrgSnowCover
 */
LRG_AVAILABLE_IN_ALL
void                lrg_snow_cover_clear_tracks     (LrgSnowCover *self);

/**
 * lrg_snow_cover_get_depth_at:
 * @self: an #LrgSnowCover
 * @x: absolute world x
 * @z: absolute world z
 *
 * The snow at a point: the level, less the deepest track there (full
 * depth inside half the radius, easing to nothing at the radius).
 *
 * Returns: 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gdouble             lrg_snow_cover_get_depth_at     (LrgSnowCover *self,
                                                     gdouble       x,
                                                     gdouble       z);

G_END_DECLS
