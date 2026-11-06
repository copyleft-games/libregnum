/* lrg-size-box.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSizeBox: one child with size overrides, limits and an aspect ratio.
 *
 * A size box constrains a single child: a fixed width and / or height
 * (overrides), minimum and maximum sizes, and a fixed aspect ratio. The
 * aspect ratio fits the largest rectangle of that ratio inside the
 * space the box has, positioned by the child's halign / valign
 * (centred for FILL / CENTER) - the letterboxing a game viewport or a
 * video needs.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_SIZE_BOX (lrg_size_box_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSizeBox, lrg_size_box, LRG, SIZE_BOX, LrgContainer)

/**
 * lrg_size_box_new:
 *
 * Creates an empty size box without constraints (it behaves like a
 * plain single-child bin until a constraint is set). Every size
 * setting uses a negative value for "unset".
 *
 * Returns: (transfer full): a new #LrgSizeBox
 */
LRG_AVAILABLE_IN_ALL
LrgSizeBox * lrg_size_box_new (void);

/**
 * lrg_size_box_get_child:
 * @self: an #LrgSizeBox
 *
 * Returns: (transfer none) (nullable): the child (the first child
 *   when more were added through the container API)
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_size_box_get_child (LrgSizeBox *self);

/**
 * lrg_size_box_set_child:
 * @self: an #LrgSizeBox
 * @child: (nullable): the new child
 *
 * Replaces the child (removing every existing child).
 */
LRG_AVAILABLE_IN_ALL
void lrg_size_box_set_child (LrgSizeBox *self,
                             LrgWidget  *child);

/**
 * lrg_size_box_get_width_override:
 * @self: an #LrgSizeBox
 *
 * Returns: the fixed child width, or -1 when unset
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_size_box_get_width_override (LrgSizeBox *self);

/**
 * lrg_size_box_set_width_override:
 * @self: an #LrgSizeBox
 * @width: fixed width of the child, < 0 to unset
 *
 * Gives the child exactly @width (excluding its margin) regardless of
 * its preferred size and of the box's own width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_size_box_set_width_override (LrgSizeBox *self,
                                      gfloat      width);

/**
 * lrg_size_box_get_height_override:
 * @self: an #LrgSizeBox
 *
 * Returns: the fixed child height, or -1 when unset
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_size_box_get_height_override (LrgSizeBox *self);

/**
 * lrg_size_box_set_height_override:
 * @self: an #LrgSizeBox
 * @height: fixed height of the child, < 0 to unset
 *
 * Gives the child exactly @height (excluding its margin).
 */
LRG_AVAILABLE_IN_ALL
void lrg_size_box_set_height_override (LrgSizeBox *self,
                                       gfloat      height);

/**
 * lrg_size_box_get_min_width:
 * @self: an #LrgSizeBox
 *
 * Returns: the minimum child width, or -1 when unset
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_size_box_get_min_width (LrgSizeBox *self);

/**
 * lrg_size_box_set_min_width:
 * @self: an #LrgSizeBox
 * @width: minimum width of the child, < 0 to unset
 */
LRG_AVAILABLE_IN_ALL
void lrg_size_box_set_min_width (LrgSizeBox *self,
                                 gfloat      width);

/**
 * lrg_size_box_get_min_height:
 * @self: an #LrgSizeBox
 *
 * Returns: the minimum child height, or -1 when unset
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_size_box_get_min_height (LrgSizeBox *self);

/**
 * lrg_size_box_set_min_height:
 * @self: an #LrgSizeBox
 * @height: minimum height of the child, < 0 to unset
 */
LRG_AVAILABLE_IN_ALL
void lrg_size_box_set_min_height (LrgSizeBox *self,
                                  gfloat      height);

/**
 * lrg_size_box_get_max_width:
 * @self: an #LrgSizeBox
 *
 * Returns: the maximum child width, or -1 when unset
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_size_box_get_max_width (LrgSizeBox *self);

/**
 * lrg_size_box_set_max_width:
 * @self: an #LrgSizeBox
 * @width: maximum width of the child, < 0 to unset
 */
LRG_AVAILABLE_IN_ALL
void lrg_size_box_set_max_width (LrgSizeBox *self,
                                 gfloat      width);

/**
 * lrg_size_box_get_max_height:
 * @self: an #LrgSizeBox
 *
 * Returns: the maximum child height, or -1 when unset
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_size_box_get_max_height (LrgSizeBox *self);

/**
 * lrg_size_box_set_max_height:
 * @self: an #LrgSizeBox
 * @height: maximum height of the child, < 0 to unset
 */
LRG_AVAILABLE_IN_ALL
void lrg_size_box_set_max_height (LrgSizeBox *self,
                                  gfloat      height);

/**
 * lrg_size_box_get_aspect_ratio:
 * @self: an #LrgSizeBox
 *
 * Returns: the width / height ratio, or 0 when unset
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_size_box_get_aspect_ratio (LrgSizeBox *self);

/**
 * lrg_size_box_set_aspect_ratio:
 * @self: an #LrgSizeBox
 * @ratio: width / height (16.0 / 9.0 for widescreen), <= 0 to unset
 *
 * Keeps the child at @ratio: it gets the largest rectangle of that
 * ratio that fits the space left by the other constraints, placed by
 * its halign / valign (START, CENTER or END; FILL centres).
 */
LRG_AVAILABLE_IN_ALL
void lrg_size_box_set_aspect_ratio (LrgSizeBox *self,
                                    gfloat      ratio);

G_END_DECLS
