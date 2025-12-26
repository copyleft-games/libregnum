/* lrg-panel.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Container widget with styled background.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_PANEL (lrg_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPanel, lrg_panel, LRG, PANEL, LrgContainer)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_panel_new:
 *
 * Creates a new panel widget.
 *
 * Returns: (transfer full): A new #LrgPanel
 */
LRG_AVAILABLE_IN_ALL
LrgPanel * lrg_panel_new (void);

/* ==========================================================================
 * Background
 * ========================================================================== */

/**
 * lrg_panel_get_background_color:
 * @self: an #LrgPanel
 *
 * Gets the panel's background color.
 *
 * Returns: (transfer none): The background color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_panel_get_background_color (LrgPanel *self);

/**
 * lrg_panel_set_background_color:
 * @self: an #LrgPanel
 * @color: the background color
 *
 * Sets the panel's background color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_panel_set_background_color (LrgPanel       *self,
                                     const GrlColor *color);

/* ==========================================================================
 * Border
 * ========================================================================== */

/**
 * lrg_panel_get_border_color:
 * @self: an #LrgPanel
 *
 * Gets the panel's border color.
 *
 * Returns: (transfer none) (nullable): The border color, or %NULL if no border
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_panel_get_border_color (LrgPanel *self);

/**
 * lrg_panel_set_border_color:
 * @self: an #LrgPanel
 * @color: (nullable): the border color, or %NULL for no border
 *
 * Sets the panel's border color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_panel_set_border_color (LrgPanel       *self,
                                 const GrlColor *color);

/**
 * lrg_panel_get_border_width:
 * @self: an #LrgPanel
 *
 * Gets the panel's border width.
 *
 * Returns: The border width in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_panel_get_border_width (LrgPanel *self);

/**
 * lrg_panel_set_border_width:
 * @self: an #LrgPanel
 * @width: the border width in pixels
 *
 * Sets the panel's border width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_panel_set_border_width (LrgPanel *self,
                                 gfloat    width);

/* ==========================================================================
 * Corner Radius
 * ========================================================================== */

/**
 * lrg_panel_get_corner_radius:
 * @self: an #LrgPanel
 *
 * Gets the panel's corner radius for rounded corners.
 *
 * Returns: The corner radius in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_panel_get_corner_radius (LrgPanel *self);

/**
 * lrg_panel_set_corner_radius:
 * @self: an #LrgPanel
 * @radius: the corner radius in pixels
 *
 * Sets the panel's corner radius for rounded corners.
 */
LRG_AVAILABLE_IN_ALL
void lrg_panel_set_corner_radius (LrgPanel *self,
                                  gfloat    radius);

G_END_DECLS
