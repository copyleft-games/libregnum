/* lrg-splitter.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSplitter: Two panes separated by a draggable divider.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPLITTER (lrg_splitter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSplitter, lrg_splitter, LRG, SPLITTER, LrgContainer)

/**
 * lrg_splitter_new:
 * @orientation: %LRG_ORIENTATION_HORIZONTAL for panes side by side
 *   (vertical handle), %LRG_ORIENTATION_VERTICAL for panes stacked
 *
 * Creates an empty splitter at half / half (default fraction 0.5).
 *
 * Returns: (transfer full): a new #LrgSplitter
 */
LRG_AVAILABLE_IN_ALL
LrgSplitter * lrg_splitter_new (LrgOrientation orientation);

/**
 * lrg_splitter_get_orientation:
 * @self: an #LrgSplitter
 *
 * Returns: the direction the panes are laid out in
 */
LRG_AVAILABLE_IN_ALL
LrgOrientation lrg_splitter_get_orientation (LrgSplitter *self);

/**
 * lrg_splitter_set_orientation:
 * @self: an #LrgSplitter
 * @orientation: the new orientation
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_set_orientation (LrgSplitter    *self,
                                   LrgOrientation  orientation);

/**
 * lrg_splitter_set_start_child:
 * @self: an #LrgSplitter
 * @child: (nullable): the left / top pane
 *
 * Replaces the start pane.
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_set_start_child (LrgSplitter *self,
                                   LrgWidget   *child);

/**
 * lrg_splitter_get_start_child:
 * @self: an #LrgSplitter
 *
 * Returns: (transfer none) (nullable): the start pane
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_splitter_get_start_child (LrgSplitter *self);

/**
 * lrg_splitter_set_end_child:
 * @self: an #LrgSplitter
 * @child: (nullable): the right / bottom pane
 *
 * Replaces the end pane.
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_set_end_child (LrgSplitter *self,
                                 LrgWidget   *child);

/**
 * lrg_splitter_get_end_child:
 * @self: an #LrgSplitter
 *
 * Returns: (transfer none) (nullable): the end pane
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_splitter_get_end_child (LrgSplitter *self);

/**
 * lrg_splitter_get_position:
 * @self: an #LrgSplitter
 *
 * Returns: the size of the start pane the splitter is heading for
 *   (clamped by the minimum sizes); during an animation the displayed
 *   size lags behind, see lrg_splitter_get_displayed_position()
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_splitter_get_position (LrgSplitter *self);

/**
 * lrg_splitter_set_position:
 * @self: an #LrgSplitter
 * @position: size of the start pane in canvas units
 * @animate: glide there with a spring
 *
 * Fixes the start pane size in units: it keeps its size when the
 * splitter is resized.
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_set_position (LrgSplitter *self,
                                gfloat       position,
                                gboolean     animate);

/**
 * lrg_splitter_get_displayed_position:
 * @self: an #LrgSplitter
 *
 * Returns: the start pane size currently laid out (the spring value)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_splitter_get_displayed_position (LrgSplitter *self);

/**
 * lrg_splitter_get_fraction:
 * @self: an #LrgSplitter
 *
 * Returns: the start pane's share of the space (0..1)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_splitter_get_fraction (LrgSplitter *self);

/**
 * lrg_splitter_set_fraction:
 * @self: an #LrgSplitter
 * @fraction: share of the space for the start pane, 0..1
 * @animate: glide there with a spring
 *
 * Splits proportionally: the share is kept when the splitter is
 * resized.
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_set_fraction (LrgSplitter *self,
                                gfloat       fraction,
                                gboolean     animate);

/**
 * lrg_splitter_get_default_fraction:
 * @self: an #LrgSplitter
 *
 * Returns: the fraction a double click on the handle returns to
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_splitter_get_default_fraction (LrgSplitter *self);

/**
 * lrg_splitter_set_default_fraction:
 * @self: an #LrgSplitter
 * @fraction: fraction restored by lrg_splitter_reset() and double
 *   clicks (default 0.5)
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_set_default_fraction (LrgSplitter *self,
                                        gfloat       fraction);

/**
 * lrg_splitter_reset:
 * @self: an #LrgSplitter
 * @animate: glide there with a spring
 *
 * Returns to the default fraction.
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_reset (LrgSplitter *self,
                         gboolean     animate);

/**
 * lrg_splitter_set_min_sizes:
 * @self: an #LrgSplitter
 * @start: smallest size of the start pane
 * @end: smallest size of the end pane
 *
 * When both do not fit, the start pane's minimum wins.
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_set_min_sizes (LrgSplitter *self,
                                 gfloat       start,
                                 gfloat       end);

/**
 * lrg_splitter_get_min_sizes:
 * @self: an #LrgSplitter
 * @start: (out) (optional): smallest start pane size
 * @end: (out) (optional): smallest end pane size
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_get_min_sizes (LrgSplitter *self,
                                 gfloat      *start,
                                 gfloat      *end);

/**
 * lrg_splitter_get_handle_size:
 * @self: an #LrgSplitter
 *
 * Returns: thickness of the handle (its grab zone)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_splitter_get_handle_size (LrgSplitter *self);

/**
 * lrg_splitter_set_handle_size:
 * @self: an #LrgSplitter
 * @size: thickness of the handle between the panes (default 8)
 */
LRG_AVAILABLE_IN_ALL
void lrg_splitter_set_handle_size (LrgSplitter *self,
                                   gfloat       size);

/**
 * lrg_splitter_get_handle:
 * @self: an #LrgSplitter
 *
 * The handle is a focusable child widget: keyboard and gamepad focus
 * land on it rather than on the panes.
 *
 * Returns: (transfer none): the handle widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_splitter_get_handle (LrgSplitter *self);

/**
 * lrg_splitter_get_dragging:
 * @self: an #LrgSplitter
 *
 * Returns: %TRUE while the handle is being dragged
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_splitter_get_dragging (LrgSplitter *self);

G_END_DECLS
