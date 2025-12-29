/* lrg-vignette.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Vignette post-processing effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_VIGNETTE (lrg_vignette_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVignette, lrg_vignette, LRG, VIGNETTE, LrgPostEffect)

/**
 * lrg_vignette_new:
 *
 * Creates a new vignette effect with default settings.
 *
 * Returns: (transfer full): A new #LrgVignette
 */
LRG_AVAILABLE_IN_ALL
LrgVignette *       lrg_vignette_new                (void);

/**
 * lrg_vignette_get_intensity:
 * @self: A #LrgVignette
 *
 * Gets the vignette intensity.
 *
 * Returns: The intensity (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_vignette_get_intensity      (LrgVignette    *self);

/**
 * lrg_vignette_set_intensity:
 * @self: A #LrgVignette
 * @intensity: The intensity (0.0 to 1.0)
 *
 * Sets the vignette intensity.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_vignette_set_intensity      (LrgVignette    *self,
                                                     gfloat          intensity);

/**
 * lrg_vignette_get_radius:
 * @self: A #LrgVignette
 *
 * Gets the inner radius where vignette starts.
 *
 * Returns: The radius (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_vignette_get_radius         (LrgVignette    *self);

/**
 * lrg_vignette_set_radius:
 * @self: A #LrgVignette
 * @radius: The inner radius (0.0 to 1.0)
 *
 * Sets the inner radius where vignette starts.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_vignette_set_radius         (LrgVignette    *self,
                                                     gfloat          radius);

/**
 * lrg_vignette_get_smoothness:
 * @self: A #LrgVignette
 *
 * Gets the smoothness of the vignette edge.
 *
 * Returns: The smoothness (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_vignette_get_smoothness     (LrgVignette    *self);

/**
 * lrg_vignette_set_smoothness:
 * @self: A #LrgVignette
 * @smoothness: The smoothness (0.0 to 1.0)
 *
 * Sets the smoothness of the vignette edge.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_vignette_set_smoothness     (LrgVignette    *self,
                                                     gfloat          smoothness);

/**
 * lrg_vignette_get_roundness:
 * @self: A #LrgVignette
 *
 * Gets the roundness (1.0 = circular, 0.0 = follows screen aspect).
 *
 * Returns: The roundness (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_vignette_get_roundness      (LrgVignette    *self);

/**
 * lrg_vignette_set_roundness:
 * @self: A #LrgVignette
 * @roundness: The roundness (0.0 to 1.0)
 *
 * Sets the roundness of the vignette shape.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_vignette_set_roundness      (LrgVignette    *self,
                                                     gfloat          roundness);

/**
 * lrg_vignette_get_color:
 * @self: A #LrgVignette
 * @r: (out) (optional): Red component
 * @g: (out) (optional): Green component
 * @b: (out) (optional): Blue component
 *
 * Gets the vignette color.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_vignette_get_color          (LrgVignette    *self,
                                                     gfloat         *r,
                                                     gfloat         *g,
                                                     gfloat         *b);

/**
 * lrg_vignette_set_color:
 * @self: A #LrgVignette
 * @r: Red component (0.0 to 1.0)
 * @g: Green component (0.0 to 1.0)
 * @b: Blue component (0.0 to 1.0)
 *
 * Sets the vignette color. Default is black (0, 0, 0).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_vignette_set_color          (LrgVignette    *self,
                                                     gfloat          r,
                                                     gfloat          g,
                                                     gfloat          b);

G_END_DECLS
