/* lrg-progress-bar.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Progress bar widget for displaying completion status.
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

#define LRG_TYPE_PROGRESS_BAR (lrg_progress_bar_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProgressBar, lrg_progress_bar, LRG, PROGRESS_BAR, LrgWidget)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_progress_bar_new:
 *
 * Creates a new progress bar widget.
 *
 * Returns: (transfer full): A new #LrgProgressBar
 */
LRG_AVAILABLE_IN_ALL
LrgProgressBar * lrg_progress_bar_new (void);

/* ==========================================================================
 * Value
 * ========================================================================== */

/**
 * lrg_progress_bar_get_value:
 * @self: an #LrgProgressBar
 *
 * Gets the current progress value.
 *
 * Returns: The current value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_progress_bar_get_value (LrgProgressBar *self);

/**
 * lrg_progress_bar_set_value:
 * @self: an #LrgProgressBar
 * @value: the progress value (0.0 to max)
 *
 * Sets the current progress value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_progress_bar_set_value (LrgProgressBar *self,
                                 gdouble         value);

/**
 * lrg_progress_bar_get_max:
 * @self: an #LrgProgressBar
 *
 * Gets the maximum value.
 *
 * Returns: The maximum value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_progress_bar_get_max (LrgProgressBar *self);

/**
 * lrg_progress_bar_set_max:
 * @self: an #LrgProgressBar
 * @max: the maximum value
 *
 * Sets the maximum value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_progress_bar_set_max (LrgProgressBar *self,
                               gdouble         max);

/**
 * lrg_progress_bar_get_fraction:
 * @self: an #LrgProgressBar
 *
 * Gets the progress fraction (value / max).
 *
 * Returns: The fraction (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_progress_bar_get_fraction (LrgProgressBar *self);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_progress_bar_get_show_text:
 * @self: an #LrgProgressBar
 *
 * Gets whether the percentage text is shown.
 *
 * Returns: %TRUE if text is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_progress_bar_get_show_text (LrgProgressBar *self);

/**
 * lrg_progress_bar_set_show_text:
 * @self: an #LrgProgressBar
 * @show: whether to show percentage text
 *
 * Sets whether to display the percentage text.
 */
LRG_AVAILABLE_IN_ALL
void lrg_progress_bar_set_show_text (LrgProgressBar *self,
                                     gboolean        show);

/**
 * lrg_progress_bar_get_orientation:
 * @self: an #LrgProgressBar
 *
 * Gets the progress bar orientation.
 *
 * Returns: The orientation
 */
LRG_AVAILABLE_IN_ALL
LrgOrientation lrg_progress_bar_get_orientation (LrgProgressBar *self);

/**
 * lrg_progress_bar_set_orientation:
 * @self: an #LrgProgressBar
 * @orientation: the orientation
 *
 * Sets the progress bar orientation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_progress_bar_set_orientation (LrgProgressBar *self,
                                       LrgOrientation  orientation);

/* ==========================================================================
 * Appearance
 * ========================================================================== */

/**
 * lrg_progress_bar_get_background_color:
 * @self: an #LrgProgressBar
 *
 * Gets the track background color.
 *
 * Returns: (transfer none): The background color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_progress_bar_get_background_color (LrgProgressBar *self);

/**
 * lrg_progress_bar_set_background_color:
 * @self: an #LrgProgressBar
 * @color: the background color
 *
 * Sets the track background color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_progress_bar_set_background_color (LrgProgressBar *self,
                                            const GrlColor *color);

/**
 * lrg_progress_bar_get_fill_color:
 * @self: an #LrgProgressBar
 *
 * Gets the progress fill color.
 *
 * Returns: (transfer none): The fill color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_progress_bar_get_fill_color (LrgProgressBar *self);

/**
 * lrg_progress_bar_set_fill_color:
 * @self: an #LrgProgressBar
 * @color: the fill color
 *
 * Sets the progress fill color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_progress_bar_set_fill_color (LrgProgressBar *self,
                                      const GrlColor *color);

/**
 * lrg_progress_bar_get_text_color:
 * @self: an #LrgProgressBar
 *
 * Gets the percentage text color.
 *
 * Returns: (transfer none): The text color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_progress_bar_get_text_color (LrgProgressBar *self);

/**
 * lrg_progress_bar_set_text_color:
 * @self: an #LrgProgressBar
 * @color: the text color
 *
 * Sets the percentage text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_progress_bar_set_text_color (LrgProgressBar *self,
                                      const GrlColor *color);

/**
 * lrg_progress_bar_get_corner_radius:
 * @self: an #LrgProgressBar
 *
 * Gets the corner radius.
 *
 * Returns: The corner radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_progress_bar_get_corner_radius (LrgProgressBar *self);

/**
 * lrg_progress_bar_set_corner_radius:
 * @self: an #LrgProgressBar
 * @radius: the corner radius
 *
 * Sets the corner radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_progress_bar_set_corner_radius (LrgProgressBar *self,
                                         gfloat          radius);

G_END_DECLS
