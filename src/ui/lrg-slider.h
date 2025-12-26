/* lrg-slider.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Slider widget for selecting numeric values.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_SLIDER (lrg_slider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSlider, lrg_slider, LRG, SLIDER, LrgWidget)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_slider_new:
 *
 * Creates a new slider widget with default range (0 to 100).
 *
 * Returns: (transfer full): A new #LrgSlider
 */
LRG_AVAILABLE_IN_ALL
LrgSlider * lrg_slider_new (void);

/**
 * lrg_slider_new_with_range:
 * @min: minimum value
 * @max: maximum value
 * @step: step increment
 *
 * Creates a new slider widget with the specified range.
 *
 * Returns: (transfer full): A new #LrgSlider
 */
LRG_AVAILABLE_IN_ALL
LrgSlider * lrg_slider_new_with_range (gdouble min,
                                       gdouble max,
                                       gdouble step);

/* ==========================================================================
 * Value
 * ========================================================================== */

/**
 * lrg_slider_get_value:
 * @self: an #LrgSlider
 *
 * Gets the current slider value.
 *
 * Returns: The current value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_slider_get_value (LrgSlider *self);

/**
 * lrg_slider_set_value:
 * @self: an #LrgSlider
 * @value: the new value
 *
 * Sets the current slider value. The value is clamped to the
 * valid range and snapped to the step increment.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_value (LrgSlider *self,
                           gdouble    value);

/* ==========================================================================
 * Range
 * ========================================================================== */

/**
 * lrg_slider_get_min:
 * @self: an #LrgSlider
 *
 * Gets the minimum value.
 *
 * Returns: The minimum value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_slider_get_min (LrgSlider *self);

/**
 * lrg_slider_set_min:
 * @self: an #LrgSlider
 * @min: the minimum value
 *
 * Sets the minimum value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_min (LrgSlider *self,
                         gdouble    min);

/**
 * lrg_slider_get_max:
 * @self: an #LrgSlider
 *
 * Gets the maximum value.
 *
 * Returns: The maximum value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_slider_get_max (LrgSlider *self);

/**
 * lrg_slider_set_max:
 * @self: an #LrgSlider
 * @max: the maximum value
 *
 * Sets the maximum value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_max (LrgSlider *self,
                         gdouble    max);

/**
 * lrg_slider_get_step:
 * @self: an #LrgSlider
 *
 * Gets the step increment.
 *
 * Returns: The step increment
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_slider_get_step (LrgSlider *self);

/**
 * lrg_slider_set_step:
 * @self: an #LrgSlider
 * @step: the step increment
 *
 * Sets the step increment.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_step (LrgSlider *self,
                          gdouble    step);

/**
 * lrg_slider_set_range:
 * @self: an #LrgSlider
 * @min: the minimum value
 * @max: the maximum value
 *
 * Sets the value range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_range (LrgSlider *self,
                           gdouble    min,
                           gdouble    max);

/**
 * lrg_slider_get_fraction:
 * @self: an #LrgSlider
 *
 * Gets the current value as a fraction of the range (0.0 to 1.0).
 *
 * Returns: The fraction
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_slider_get_fraction (LrgSlider *self);

/* ==========================================================================
 * Orientation
 * ========================================================================== */

/**
 * lrg_slider_get_orientation:
 * @self: an #LrgSlider
 *
 * Gets the slider orientation.
 *
 * Returns: The orientation
 */
LRG_AVAILABLE_IN_ALL
LrgOrientation lrg_slider_get_orientation (LrgSlider *self);

/**
 * lrg_slider_set_orientation:
 * @self: an #LrgSlider
 * @orientation: the orientation
 *
 * Sets the slider orientation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_orientation (LrgSlider      *self,
                                 LrgOrientation  orientation);

/* ==========================================================================
 * Appearance
 * ========================================================================== */

/**
 * lrg_slider_get_track_color:
 * @self: an #LrgSlider
 *
 * Gets the track background color.
 *
 * Returns: (transfer none): The track color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_slider_get_track_color (LrgSlider *self);

/**
 * lrg_slider_set_track_color:
 * @self: an #LrgSlider
 * @color: the track color
 *
 * Sets the track background color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_track_color (LrgSlider      *self,
                                 const GrlColor *color);

/**
 * lrg_slider_get_fill_color:
 * @self: an #LrgSlider
 *
 * Gets the filled portion color.
 *
 * Returns: (transfer none): The fill color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_slider_get_fill_color (LrgSlider *self);

/**
 * lrg_slider_set_fill_color:
 * @self: an #LrgSlider
 * @color: the fill color
 *
 * Sets the filled portion color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_fill_color (LrgSlider      *self,
                                const GrlColor *color);

/**
 * lrg_slider_get_handle_color:
 * @self: an #LrgSlider
 *
 * Gets the handle color.
 *
 * Returns: (transfer none): The handle color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_slider_get_handle_color (LrgSlider *self);

/**
 * lrg_slider_set_handle_color:
 * @self: an #LrgSlider
 * @color: the handle color
 *
 * Sets the handle color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_handle_color (LrgSlider      *self,
                                  const GrlColor *color);

/**
 * lrg_slider_get_handle_size:
 * @self: an #LrgSlider
 *
 * Gets the handle diameter.
 *
 * Returns: The handle size in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_slider_get_handle_size (LrgSlider *self);

/**
 * lrg_slider_set_handle_size:
 * @self: an #LrgSlider
 * @size: the handle diameter in pixels
 *
 * Sets the handle diameter.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_handle_size (LrgSlider *self,
                                 gfloat     size);

/**
 * lrg_slider_get_track_thickness:
 * @self: an #LrgSlider
 *
 * Gets the track thickness.
 *
 * Returns: The track thickness in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_slider_get_track_thickness (LrgSlider *self);

/**
 * lrg_slider_set_track_thickness:
 * @self: an #LrgSlider
 * @thickness: the track thickness in pixels
 *
 * Sets the track thickness.
 */
LRG_AVAILABLE_IN_ALL
void lrg_slider_set_track_thickness (LrgSlider *self,
                                     gfloat     thickness);

G_END_DECLS
