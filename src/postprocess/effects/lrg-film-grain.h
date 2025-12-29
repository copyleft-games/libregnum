/* lrg-film-grain.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Film grain post-processing effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_FILM_GRAIN (lrg_film_grain_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFilmGrain, lrg_film_grain, LRG, FILM_GRAIN, LrgPostEffect)

/**
 * lrg_film_grain_new:
 *
 * Creates a new film grain effect.
 *
 * Returns: (transfer full): A new #LrgFilmGrain
 */
LRG_AVAILABLE_IN_ALL
LrgFilmGrain *      lrg_film_grain_new              (void);

/**
 * lrg_film_grain_get_intensity:
 * @self: A #LrgFilmGrain
 *
 * Gets the grain intensity.
 *
 * Returns: The intensity (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_film_grain_get_intensity    (LrgFilmGrain   *self);

/**
 * lrg_film_grain_set_intensity:
 * @self: A #LrgFilmGrain
 * @intensity: The intensity (0.0 to 1.0)
 *
 * Sets the grain intensity.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_film_grain_set_intensity    (LrgFilmGrain   *self,
                                                     gfloat          intensity);

/**
 * lrg_film_grain_get_size:
 * @self: A #LrgFilmGrain
 *
 * Gets the grain size.
 *
 * Returns: The grain size (1.0 to 5.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_film_grain_get_size         (LrgFilmGrain   *self);

/**
 * lrg_film_grain_set_size:
 * @self: A #LrgFilmGrain
 * @size: The grain size (1.0 to 5.0)
 *
 * Sets the grain size. Larger values create coarser grain.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_film_grain_set_size         (LrgFilmGrain   *self,
                                                     gfloat          size);

/**
 * lrg_film_grain_get_speed:
 * @self: A #LrgFilmGrain
 *
 * Gets the animation speed.
 *
 * Returns: The speed multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_film_grain_get_speed        (LrgFilmGrain   *self);

/**
 * lrg_film_grain_set_speed:
 * @self: A #LrgFilmGrain
 * @speed: The animation speed multiplier
 *
 * Sets how fast the grain animates.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_film_grain_set_speed        (LrgFilmGrain   *self,
                                                     gfloat          speed);

/**
 * lrg_film_grain_get_colored:
 * @self: A #LrgFilmGrain
 *
 * Gets whether color grain is enabled.
 *
 * Returns: %TRUE if colored grain is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_film_grain_get_colored      (LrgFilmGrain   *self);

/**
 * lrg_film_grain_set_colored:
 * @self: A #LrgFilmGrain
 * @colored: Whether to use colored grain
 *
 * Sets whether grain should be colored or monochrome.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_film_grain_set_colored      (LrgFilmGrain   *self,
                                                     gboolean        colored);

/**
 * lrg_film_grain_get_luminance_response:
 * @self: A #LrgFilmGrain
 *
 * Gets the luminance response.
 *
 * Returns: The luminance response (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_film_grain_get_luminance_response (LrgFilmGrain *self);

/**
 * lrg_film_grain_set_luminance_response:
 * @self: A #LrgFilmGrain
 * @response: The luminance response (0.0 to 1.0)
 *
 * Sets how much grain is affected by image brightness.
 * 0 = uniform grain, 1 = more grain in dark areas.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_film_grain_set_luminance_response (LrgFilmGrain *self,
                                                           gfloat        response);

/**
 * lrg_film_grain_update:
 * @self: A #LrgFilmGrain
 * @delta_time: Time since last update in seconds
 *
 * Updates the grain animation.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_film_grain_update           (LrgFilmGrain   *self,
                                                     gfloat          delta_time);

G_END_DECLS
