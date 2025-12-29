/* lrg-fxaa.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * FXAA anti-aliasing post-processing effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_FXAA (lrg_fxaa_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFxaa, lrg_fxaa, LRG, FXAA, LrgPostEffect)

/**
 * lrg_fxaa_new:
 *
 * Creates a new FXAA effect.
 *
 * Returns: (transfer full): A new #LrgFxaa
 */
LRG_AVAILABLE_IN_ALL
LrgFxaa *           lrg_fxaa_new                    (void);

/**
 * lrg_fxaa_get_quality:
 * @self: A #LrgFxaa
 *
 * Gets the FXAA quality preset.
 *
 * Returns: The quality level
 */
LRG_AVAILABLE_IN_ALL
LrgFxaaQuality      lrg_fxaa_get_quality            (LrgFxaa        *self);

/**
 * lrg_fxaa_set_quality:
 * @self: A #LrgFxaa
 * @quality: The quality level
 *
 * Sets the FXAA quality preset.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_fxaa_set_quality            (LrgFxaa        *self,
                                                     LrgFxaaQuality  quality);

/**
 * lrg_fxaa_get_subpixel_quality:
 * @self: A #LrgFxaa
 *
 * Gets the subpixel quality.
 *
 * Returns: The subpixel quality (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_fxaa_get_subpixel_quality   (LrgFxaa        *self);

/**
 * lrg_fxaa_set_subpixel_quality:
 * @self: A #LrgFxaa
 * @quality: Subpixel quality (0.0 to 1.0)
 *
 * Sets the subpixel quality. Higher values result in
 * more anti-aliasing but can cause blurriness.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_fxaa_set_subpixel_quality   (LrgFxaa        *self,
                                                     gfloat          quality);

/**
 * lrg_fxaa_get_edge_threshold:
 * @self: A #LrgFxaa
 *
 * Gets the edge detection threshold.
 *
 * Returns: The edge threshold
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_fxaa_get_edge_threshold     (LrgFxaa        *self);

/**
 * lrg_fxaa_set_edge_threshold:
 * @self: A #LrgFxaa
 * @threshold: Edge threshold (0.0 to 0.5)
 *
 * Sets the edge detection threshold. Lower values detect
 * more edges but may affect non-edge pixels.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_fxaa_set_edge_threshold     (LrgFxaa        *self,
                                                     gfloat          threshold);

/**
 * lrg_fxaa_get_edge_threshold_min:
 * @self: A #LrgFxaa
 *
 * Gets the minimum edge threshold.
 *
 * Returns: The minimum edge threshold
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_fxaa_get_edge_threshold_min (LrgFxaa        *self);

/**
 * lrg_fxaa_set_edge_threshold_min:
 * @self: A #LrgFxaa
 * @threshold: Minimum edge threshold (0.0 to 0.1)
 *
 * Sets the minimum edge threshold. Pixels below this
 * brightness are not anti-aliased.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_fxaa_set_edge_threshold_min (LrgFxaa        *self,
                                                     gfloat          threshold);

G_END_DECLS
