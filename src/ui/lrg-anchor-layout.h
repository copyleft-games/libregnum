/* lrg-anchor-layout.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnchorLayout: children placed freely by anchors, offsets and a pivot.
 *
 * The layout is the libregnum take on Slate's SConstraintCanvas and
 * Godot's anchors: each child has a pair of anchors per axis (fractions
 * of the layout's content rectangle). When the two anchors of an axis
 * coincide the child is pinned to that point: the offsets give its
 * position and size and the alignment (pivot) says which point of the
 * child sits on the anchor. When they differ the child stretches
 * between them and the offsets are margins from the anchors. Children
 * may lie partly or wholly outside the layout. This is the container
 * for game HUDs: health top-left, minimap top-right, the interaction
 * prompt bottom-centre, all surviving any window size.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgAnchorPreset
 * ========================================================================== */

/**
 * LrgAnchorPreset:
 * @LRG_ANCHOR_PRESET_TOP_LEFT: pinned to the top-left corner
 * @LRG_ANCHOR_PRESET_TOP: pinned to the middle of the top edge
 * @LRG_ANCHOR_PRESET_TOP_RIGHT: pinned to the top-right corner
 * @LRG_ANCHOR_PRESET_LEFT: pinned to the middle of the left edge
 * @LRG_ANCHOR_PRESET_CENTER: pinned to the centre
 * @LRG_ANCHOR_PRESET_RIGHT: pinned to the middle of the right edge
 * @LRG_ANCHOR_PRESET_BOTTOM_LEFT: pinned to the bottom-left corner
 * @LRG_ANCHOR_PRESET_BOTTOM: pinned to the middle of the bottom edge
 * @LRG_ANCHOR_PRESET_BOTTOM_RIGHT: pinned to the bottom-right corner
 * @LRG_ANCHOR_PRESET_TOP_STRETCH: along the top edge, full width
 * @LRG_ANCHOR_PRESET_MIDDLE_STRETCH: a horizontal band through the
 *   middle, full width
 * @LRG_ANCHOR_PRESET_BOTTOM_STRETCH: along the bottom edge, full width
 * @LRG_ANCHOR_PRESET_LEFT_STRETCH: along the left edge, full height
 * @LRG_ANCHOR_PRESET_CENTER_STRETCH: a vertical band through the
 *   centre, full height
 * @LRG_ANCHOR_PRESET_RIGHT_STRETCH: along the right edge, full height
 * @LRG_ANCHOR_PRESET_FILL: stretched over the whole layout
 *
 * Common anchor setups. Pinned presets also set the alignment (pivot)
 * to the same point, so a TOP_RIGHT child hugs the corner with its own
 * top-right corner; stretched presets align the unstretched axis the
 * same way (TOP_STRETCH: top edge on the top edge).
 */
typedef enum
{
	LRG_ANCHOR_PRESET_TOP_LEFT,
	LRG_ANCHOR_PRESET_TOP,
	LRG_ANCHOR_PRESET_TOP_RIGHT,
	LRG_ANCHOR_PRESET_LEFT,
	LRG_ANCHOR_PRESET_CENTER,
	LRG_ANCHOR_PRESET_RIGHT,
	LRG_ANCHOR_PRESET_BOTTOM_LEFT,
	LRG_ANCHOR_PRESET_BOTTOM,
	LRG_ANCHOR_PRESET_BOTTOM_RIGHT,
	LRG_ANCHOR_PRESET_TOP_STRETCH,
	LRG_ANCHOR_PRESET_MIDDLE_STRETCH,
	LRG_ANCHOR_PRESET_BOTTOM_STRETCH,
	LRG_ANCHOR_PRESET_LEFT_STRETCH,
	LRG_ANCHOR_PRESET_CENTER_STRETCH,
	LRG_ANCHOR_PRESET_RIGHT_STRETCH,
	LRG_ANCHOR_PRESET_FILL
} LrgAnchorPreset;

LRG_AVAILABLE_IN_ALL
GType lrg_anchor_preset_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANCHOR_PRESET (lrg_anchor_preset_get_type ())

/* ==========================================================================
 * LrgAnchorData
 * ========================================================================== */

#define LRG_TYPE_ANCHOR_DATA (lrg_anchor_data_get_type ())

/**
 * LrgAnchorData:
 * @min_x: left anchor, fraction of the layout width (0..1, may exceed)
 * @min_y: top anchor, fraction of the layout height
 * @max_x: right anchor; equal to @min_x pins the child horizontally
 * @max_y: bottom anchor; equal to @min_y pins the child vertically
 * @left: pinned: X offset of the pivot from the anchor; stretched:
 *   distance of the left edge from the left anchor
 * @top: pinned: Y offset of the pivot from the anchor; stretched:
 *   distance of the top edge from the top anchor
 * @right: pinned: width (<= 0 or @auto_size: the preferred width);
 *   stretched: distance of the right edge from the right anchor,
 *   measured inwards
 * @bottom: pinned: height (<= 0 or @auto_size: the preferred height);
 *   stretched: distance of the bottom edge from the bottom anchor,
 *   measured inwards
 * @align_x: pivot, which point of the child sits on a pinned anchor
 *   (0 left edge, 0.5 centre, 1 right edge)
 * @align_y: vertical pivot (0 top, 1 bottom)
 * @auto_size: size pinned axes from the child's preferred size,
 *   ignoring @right / @bottom
 * @z_order: paint and hit-test order; higher is on top, equal values
 *   keep the order the children were added in
 *
 * Placement of one child of an #LrgAnchorLayout. The offsets follow
 * Slate's convention: with coincident anchors they are position and
 * size, with separate anchors they are margins.
 */
typedef struct
{
	gfloat   min_x;
	gfloat   min_y;
	gfloat   max_x;
	gfloat   max_y;
	gfloat   left;
	gfloat   top;
	gfloat   right;
	gfloat   bottom;
	gfloat   align_x;
	gfloat   align_y;
	gboolean auto_size;
	gint     z_order;
} LrgAnchorData;

LRG_AVAILABLE_IN_ALL
GType lrg_anchor_data_get_type (void) G_GNUC_CONST;

/**
 * lrg_anchor_data_init:
 * @self: (out caller-allocates): data to fill
 *
 * Fills @self with the defaults: pinned to the top-left corner, no
 * offsets, pivot at the child's top-left, auto-size on, z-order 0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_data_init (LrgAnchorData *self);

/**
 * lrg_anchor_data_copy:
 * @self: an #LrgAnchorData
 *
 * Returns: (transfer full): a heap copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgAnchorData * lrg_anchor_data_copy (const LrgAnchorData *self);

/**
 * lrg_anchor_data_free:
 * @self: (nullable): a heap-allocated #LrgAnchorData
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_data_free (LrgAnchorData *self);

/**
 * lrg_anchor_data_set_preset:
 * @self: an #LrgAnchorData
 * @preset: the preset to apply
 *
 * Sets the anchors and alignment of @self from @preset. Offsets,
 * auto-size and z-order are left alone, so a preset can be applied
 * after the offsets.
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_data_set_preset (LrgAnchorData   *self,
                                 LrgAnchorPreset  preset);

/**
 * lrg_anchor_compute_rect:
 * @data: placement of the child
 * @width: width of the area the anchors refer to
 * @height: height of that area
 * @preferred_width: the child's preferred width (used by pinned axes
 *   when auto-sizing or when the size offset is <= 0)
 * @preferred_height: the child's preferred height
 * @x: (out): left edge of the child, relative to the area
 * @y: (out): top edge of the child
 * @out_width: (out): width of the child
 * @out_height: (out): height of the child
 *
 * The anchor math on its own, left to right. #LrgAnchorLayout calls
 * it for each child; exposed for tools and tests.
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_compute_rect (const LrgAnchorData *data,
                              gfloat               width,
                              gfloat               height,
                              gfloat               preferred_width,
                              gfloat               preferred_height,
                              gfloat              *x,
                              gfloat              *y,
                              gfloat              *out_width,
                              gfloat              *out_height);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgAnchorData, lrg_anchor_data_free)

/* ==========================================================================
 * LrgAnchorLayout
 * ========================================================================== */

#define LRG_TYPE_ANCHOR_LAYOUT (lrg_anchor_layout_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAnchorLayout, lrg_anchor_layout, LRG, ANCHOR_LAYOUT, LrgContainer)

/**
 * lrg_anchor_layout_new:
 *
 * Creates an empty anchor layout. It is pointer-transparent: clicks
 * that hit none of its children fall through to what lies beneath,
 * which is what a HUD laid over the game wants.
 *
 * Returns: (transfer full): a new #LrgAnchorLayout
 */
LRG_AVAILABLE_IN_ALL
LrgAnchorLayout * lrg_anchor_layout_new (void);

/**
 * lrg_anchor_layout_add:
 * @self: an #LrgAnchorLayout
 * @child: the widget to add
 * @preset: where to anchor it
 * @offset_x: offset from the anchor (pinned) or margin (stretched)
 * @offset_y: vertical offset or margin
 *
 * Convenience: adds @child with @preset and offsets (@offset_x,
 * @offset_y) on both near and far sides of stretched axes, sized from
 * its preferred size on pinned axes. Pinned children on the right or
 * bottom move inwards for positive offsets, so a TOP_RIGHT child with
 * offset (16, 16) sits 16 units in from the corner.
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_layout_add (LrgAnchorLayout *self,
                            LrgWidget       *child,
                            LrgAnchorPreset  preset,
                            gfloat           offset_x,
                            gfloat           offset_y);

/**
 * lrg_anchor_layout_get_child_data:
 * @self: an #LrgAnchorLayout
 * @child: a child of @self
 * @data: (out caller-allocates): the child's placement
 *
 * Copies the placement of @child into @data (the defaults of
 * lrg_anchor_data_init() if it was never set).
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_layout_get_child_data (LrgAnchorLayout *self,
                                       LrgWidget       *child,
                                       LrgAnchorData   *data);

/**
 * lrg_anchor_layout_set_child_data:
 * @self: an #LrgAnchorLayout
 * @child: a child of @self
 * @data: the new placement
 *
 * Replaces the whole placement of @child and lays the layout out
 * again (re-sorting children when the z-order changed).
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_layout_set_child_data (LrgAnchorLayout     *self,
                                       LrgWidget           *child,
                                       const LrgAnchorData *data);

/**
 * lrg_anchor_layout_set_child_preset:
 * @self: an #LrgAnchorLayout
 * @child: a child of @self
 * @preset: anchors and alignment to apply
 *
 * Applies @preset to @child (see lrg_anchor_data_set_preset()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_layout_set_child_preset (LrgAnchorLayout *self,
                                         LrgWidget       *child,
                                         LrgAnchorPreset  preset);

/**
 * lrg_anchor_layout_set_child_anchors:
 * @self: an #LrgAnchorLayout
 * @child: a child of @self
 * @min_x: left anchor (fraction of the width)
 * @min_y: top anchor (fraction of the height)
 * @max_x: right anchor
 * @max_y: bottom anchor
 *
 * Sets the anchors of @child. Equal min and max pin that axis; a max
 * greater than the min stretches the child between them. A max below
 * the min is treated as equal to it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_layout_set_child_anchors (LrgAnchorLayout *self,
                                          LrgWidget       *child,
                                          gfloat           min_x,
                                          gfloat           min_y,
                                          gfloat           max_x,
                                          gfloat           max_y);

/**
 * lrg_anchor_layout_set_child_offsets:
 * @self: an #LrgAnchorLayout
 * @child: a child of @self
 * @left: pinned: X position; stretched: left margin
 * @top: pinned: Y position; stretched: top margin
 * @right: pinned: width; stretched: right margin
 * @bottom: pinned: height; stretched: bottom margin
 *
 * Sets the offsets of @child (see #LrgAnchorData for their meaning).
 * Giving a pinned axis a size > 0 only takes effect with auto-size
 * off (lrg_anchor_layout_set_child_auto_size()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_layout_set_child_offsets (LrgAnchorLayout *self,
                                          LrgWidget       *child,
                                          gfloat           left,
                                          gfloat           top,
                                          gfloat           right,
                                          gfloat           bottom);

/**
 * lrg_anchor_layout_set_child_alignment:
 * @self: an #LrgAnchorLayout
 * @child: a child of @self
 * @align_x: horizontal pivot (0..1)
 * @align_y: vertical pivot (0..1)
 *
 * Sets which point of @child sits on its pinned anchor: (0, 0) its
 * top-left corner, (0.5, 0.5) its centre, (1, 1) its bottom-right.
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_layout_set_child_alignment (LrgAnchorLayout *self,
                                            LrgWidget       *child,
                                            gfloat           align_x,
                                            gfloat           align_y);

/**
 * lrg_anchor_layout_set_child_auto_size:
 * @self: an #LrgAnchorLayout
 * @child: a child of @self
 * @auto_size: size pinned axes from the preferred size
 *
 * With auto-size on (the default) a pinned child gets its preferred
 * size and tracks it as its content changes; turn it off to give it a
 * fixed size through the right / bottom offsets.
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_layout_set_child_auto_size (LrgAnchorLayout *self,
                                            LrgWidget       *child,
                                            gboolean         auto_size);

/**
 * lrg_anchor_layout_set_child_z_order:
 * @self: an #LrgAnchorLayout
 * @child: a child of @self
 * @z_order: stacking order, higher on top
 *
 * Moves @child in the stacking order. Children are kept sorted by
 * z-order in the container's child list, so painting and hit-testing
 * agree: the topmost painted child is the one that receives clicks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_anchor_layout_set_child_z_order (LrgAnchorLayout *self,
                                          LrgWidget       *child,
                                          gint             z_order);

/**
 * lrg_anchor_layout_get_child_z_order:
 * @self: an #LrgAnchorLayout
 * @child: a child of @self
 *
 * Returns: the z-order of @child
 */
LRG_AVAILABLE_IN_ALL
gint lrg_anchor_layout_get_child_z_order (LrgAnchorLayout *self,
                                          LrgWidget       *child);

G_END_DECLS
