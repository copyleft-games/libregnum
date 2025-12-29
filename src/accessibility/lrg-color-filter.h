/* lrg-color-filter.h - Abstract base class for color filters
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_COLOR_FILTER_H
#define LRG_COLOR_FILTER_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLOR_FILTER (lrg_color_filter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgColorFilter, lrg_color_filter, LRG, COLOR_FILTER, GObject)

/**
 * LrgColorFilterClass:
 * @parent_class: the parent class
 * @get_matrix: Get the 4x4 color transformation matrix
 * @get_name: Get the filter display name
 *
 * The virtual function table for #LrgColorFilter.
 *
 * Subclasses must implement get_matrix() to provide their
 * color transformation matrix.
 */
struct _LrgColorFilterClass
{
    GObjectClass parent_class;

    /*< public >*/

    /**
     * LrgColorFilterClass::get_matrix:
     * @self: an #LrgColorFilter
     * @matrix: (out) (array fixed-size=16): output 4x4 matrix (row-major)
     *
     * Gets the 4x4 color transformation matrix.
     * The matrix is applied to RGBA color vectors.
     */
    void (*get_matrix) (LrgColorFilter *self,
                        gfloat          matrix[16]);

    /**
     * LrgColorFilterClass::get_name:
     * @self: an #LrgColorFilter
     *
     * Gets the display name of this filter.
     *
     * Returns: (transfer none): The filter name
     */
    const gchar *(*get_name) (LrgColorFilter *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_color_filter_get_matrix:
 * @self: an #LrgColorFilter
 * @matrix: (out) (array fixed-size=16): output 4x4 matrix
 *
 * Gets the 4x4 color transformation matrix (row-major order).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_color_filter_get_matrix (LrgColorFilter *self,
                             gfloat          matrix[16]);

/**
 * lrg_color_filter_get_name:
 * @self: an #LrgColorFilter
 *
 * Gets the display name of this filter.
 *
 * Returns: (transfer none): The filter name
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_color_filter_get_name (LrgColorFilter *self);

/**
 * lrg_color_filter_get_intensity:
 * @self: an #LrgColorFilter
 *
 * Gets the filter intensity (0.0 = off, 1.0 = full).
 *
 * Returns: The intensity value
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_color_filter_get_intensity (LrgColorFilter *self);

/**
 * lrg_color_filter_set_intensity:
 * @self: an #LrgColorFilter
 * @intensity: the intensity (0.0 to 1.0)
 *
 * Sets the filter intensity.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_color_filter_set_intensity (LrgColorFilter *self,
                                gfloat          intensity);

/**
 * lrg_color_filter_is_enabled:
 * @self: an #LrgColorFilter
 *
 * Gets whether the filter is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_color_filter_is_enabled (LrgColorFilter *self);

/**
 * lrg_color_filter_set_enabled:
 * @self: an #LrgColorFilter
 * @enabled: whether to enable the filter
 *
 * Sets whether the filter is enabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_color_filter_set_enabled (LrgColorFilter *self,
                              gboolean        enabled);

G_END_DECLS

#endif /* LRG_COLOR_FILTER_H */
