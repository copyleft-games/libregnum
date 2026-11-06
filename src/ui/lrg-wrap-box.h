/* lrg-wrap-box.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWrapBox: A box that flows children into lines, wrapping when a line is full.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_WRAP_BOX (lrg_wrap_box_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWrapBox, lrg_wrap_box, LRG, WRAP_BOX, LrgContainer)

/**
 * LrgWrapJustify:
 * @LRG_WRAP_JUSTIFY_START: lines are packed at the start
 * @LRG_WRAP_JUSTIFY_CENTER: lines are centred
 * @LRG_WRAP_JUSTIFY_END: lines are packed at the end
 * @LRG_WRAP_JUSTIFY_SPACE_BETWEEN: the free space of each line is
 *   shared between its items (first at the start, last at the end; a
 *   single item stays at the start)
 *
 * How the items of a line share the line's leftover length.
 */
typedef enum
{
	LRG_WRAP_JUSTIFY_START,
	LRG_WRAP_JUSTIFY_CENTER,
	LRG_WRAP_JUSTIFY_END,
	LRG_WRAP_JUSTIFY_SPACE_BETWEEN
} LrgWrapJustify;

LRG_AVAILABLE_IN_ALL
GType lrg_wrap_justify_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WRAP_JUSTIFY (lrg_wrap_justify_get_type ())

/**
 * lrg_wrap_box_new:
 *
 * Creates an empty horizontal wrap box (items left to right, wrapping
 * downwards) with 8 units between items and lines.
 *
 * Returns: (transfer full): a new #LrgWrapBox
 */
LRG_AVAILABLE_IN_ALL
LrgWrapBox * lrg_wrap_box_new (void);

/**
 * lrg_wrap_box_get_orientation:
 * @self: an #LrgWrapBox
 *
 * Returns: the flow direction
 */
LRG_AVAILABLE_IN_ALL
LrgOrientation lrg_wrap_box_get_orientation (LrgWrapBox *self);

/**
 * lrg_wrap_box_set_orientation:
 * @self: an #LrgWrapBox
 * @orientation: %LRG_ORIENTATION_HORIZONTAL (rows, wrapping at the
 *   width) or %LRG_ORIENTATION_VERTICAL (columns, wrapping at the
 *   height)
 */
LRG_AVAILABLE_IN_ALL
void lrg_wrap_box_set_orientation (LrgWrapBox     *self,
                                   LrgOrientation  orientation);

/**
 * lrg_wrap_box_get_line_spacing:
 * @self: an #LrgWrapBox
 *
 * Returns: gap between lines
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_wrap_box_get_line_spacing (LrgWrapBox *self);

/**
 * lrg_wrap_box_set_line_spacing:
 * @self: an #LrgWrapBox
 * @spacing: gap between lines (the gap between items of a line is the
 *   container's #LrgContainer:spacing)
 */
LRG_AVAILABLE_IN_ALL
void lrg_wrap_box_set_line_spacing (LrgWrapBox *self,
                                    gfloat      spacing);

/**
 * lrg_wrap_box_get_justify:
 * @self: an #LrgWrapBox
 *
 * Returns: how lines share their free length
 */
LRG_AVAILABLE_IN_ALL
LrgWrapJustify lrg_wrap_box_get_justify (LrgWrapBox *self);

/**
 * lrg_wrap_box_set_justify:
 * @self: an #LrgWrapBox
 * @justify: start, center, end or space-between
 */
LRG_AVAILABLE_IN_ALL
void lrg_wrap_box_set_justify (LrgWrapBox     *self,
                               LrgWrapJustify  justify);

/**
 * lrg_wrap_box_get_item_align:
 * @self: an #LrgWrapBox
 *
 * Returns: alignment of items across their line
 */
LRG_AVAILABLE_IN_ALL
LrgAlign lrg_wrap_box_get_item_align (LrgWrapBox *self);

/**
 * lrg_wrap_box_set_item_align:
 * @self: an #LrgWrapBox
 * @align: where items sit across a line taller (or wider) than they
 *   are: start, center, end, or fill (stretched to the line)
 */
LRG_AVAILABLE_IN_ALL
void lrg_wrap_box_set_item_align (LrgWrapBox *self,
                                  LrgAlign    align);

/**
 * lrg_wrap_box_get_n_lines:
 * @self: an #LrgWrapBox
 *
 * Returns: number of lines in the last layout
 */
LRG_AVAILABLE_IN_ALL
guint lrg_wrap_box_get_n_lines (LrgWrapBox *self);

G_END_DECLS
