/* lrg-chart-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private header for LrgChart - internal functions for subclasses.
 */

#pragma once

#include "lrg-chart.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Internal Functions for Subclasses
 * ========================================================================== */

/**
 * lrg_chart_set_animation_progress:
 * @self: an #LrgChart
 * @progress: the animation progress (0.0 to 1.0)
 *
 * Sets the animation progress. Called internally during animation.
 * Subclasses may call this when implementing custom animation updates.
 */
void
lrg_chart_set_animation_progress (LrgChart *self,
                                  gfloat    progress);

/**
 * lrg_chart_is_animating:
 * @self: an #LrgChart
 *
 * Checks if the chart is currently animating.
 *
 * Returns: %TRUE if animating
 */
gboolean
lrg_chart_is_animating (LrgChart *self);

/**
 * lrg_chart_is_layout_dirty:
 * @self: an #LrgChart
 *
 * Checks if the layout needs to be rebuilt.
 *
 * Returns: %TRUE if layout is dirty
 */
gboolean
lrg_chart_is_layout_dirty (LrgChart *self);

/**
 * lrg_chart_mark_layout_dirty:
 * @self: an #LrgChart
 *
 * Marks the layout as needing rebuild.
 */
void
lrg_chart_mark_layout_dirty (LrgChart *self);

G_END_DECLS
