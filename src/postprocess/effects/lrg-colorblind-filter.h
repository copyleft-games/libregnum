/* lrg-colorblind-filter.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Colorblind accessibility filter.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-post-effect.h"
#include "../../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLORBLIND_FILTER (lrg_colorblind_filter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColorblindFilter, lrg_colorblind_filter, LRG, COLORBLIND_FILTER, LrgPostEffect)

/**
 * lrg_colorblind_filter_new:
 *
 * Creates a new colorblind filter.
 *
 * Returns: (transfer full): A new #LrgColorblindFilter
 */
LRG_AVAILABLE_IN_ALL
LrgColorblindFilter *   lrg_colorblind_filter_new           (void);

/**
 * lrg_colorblind_filter_new_with_type:
 * @filter_type: The colorblind type to simulate/correct
 *
 * Creates a new colorblind filter with a specific type.
 *
 * Returns: (transfer full): A new #LrgColorblindFilter
 */
LRG_AVAILABLE_IN_ALL
LrgColorblindFilter *   lrg_colorblind_filter_new_with_type (LrgColorblindType   filter_type);

/**
 * lrg_colorblind_filter_get_filter_type:
 * @self: A #LrgColorblindFilter
 *
 * Gets the current filter type.
 *
 * Returns: The #LrgColorblindType
 */
LRG_AVAILABLE_IN_ALL
LrgColorblindType       lrg_colorblind_filter_get_filter_type (LrgColorblindFilter *self);

/**
 * lrg_colorblind_filter_set_filter_type:
 * @self: A #LrgColorblindFilter
 * @filter_type: The colorblind type
 *
 * Sets the colorblind filter type.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_colorblind_filter_set_filter_type (LrgColorblindFilter *self,
                                                                LrgColorblindType    filter_type);

/**
 * lrg_colorblind_filter_get_mode:
 * @self: A #LrgColorblindFilter
 *
 * Gets the filter mode.
 *
 * Returns: The #LrgColorblindMode
 */
LRG_AVAILABLE_IN_ALL
LrgColorblindMode       lrg_colorblind_filter_get_mode      (LrgColorblindFilter *self);

/**
 * lrg_colorblind_filter_set_mode:
 * @self: A #LrgColorblindFilter
 * @mode: The filter mode
 *
 * Sets whether to simulate or correct colorblindness.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_colorblind_filter_set_mode      (LrgColorblindFilter *self,
                                                             LrgColorblindMode    mode);

/**
 * lrg_colorblind_filter_get_strength:
 * @self: A #LrgColorblindFilter
 *
 * Gets the filter strength.
 *
 * Returns: The strength (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_colorblind_filter_get_strength  (LrgColorblindFilter *self);

/**
 * lrg_colorblind_filter_set_strength:
 * @self: A #LrgColorblindFilter
 * @strength: The filter strength (0.0 to 1.0)
 *
 * Sets the filter strength.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_colorblind_filter_set_strength  (LrgColorblindFilter *self,
                                                             gfloat               strength);

G_END_DECLS
