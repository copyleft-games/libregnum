/* lrg-frame-layout.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFrameLayout - named element boxes on a still frame, with checks.
 *
 * A frame (a poster, a thumbnail, a comic panel, one frame of a reel)
 * of a fixed size holds named rectangles: the title, the character, the
 * speech bubble, the footer.  Boxes are placed against a safe area with
 * the nine-point #LrgHudAnchor, beside other boxes (#LrgFrameSide,
 * #LrgFrameAlign), or explicitly.  lrg_frame_layout_validate() then
 * reports every overlap, every box that leaves the frame and every box
 * that leaves the safe area, so a generated scene can be tested before
 * anything is drawn; lrg_frame_layout_draw_debug() shows the boxes on
 * an #LrgImageCanvas.
 *
 * Everything is headless: no GL context is needed.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_FRAME_LAYOUT_ERROR:
 *
 * Error domain of lrg_frame_layout_validate().  Errors carry an
 * #LrgFrameLayoutError code.
 *
 * Since: 0.14
 */
#define LRG_FRAME_LAYOUT_ERROR (lrg_frame_layout_error_quark ())

/**
 * LrgFrameLayoutError:
 * @LRG_FRAME_LAYOUT_ERROR_OVERLAP: two boxes share area and the pair was
 *   not allowed with lrg_frame_layout_allow_overlap()
 * @LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME: a box reaches past an edge of
 *   the frame
 * @LRG_FRAME_LAYOUT_ERROR_OUT_OF_SAFE_AREA: a box lies inside the frame
 *   but reaches past an edge of the safe area
 * @LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX: a box is placed relative to a box
 *   that does not exist (or cannot be placed itself), so it has no
 *   rectangle
 *
 * Error codes of %LRG_FRAME_LAYOUT_ERROR.
 *
 * Since: 0.14
 */
typedef enum
{
	LRG_FRAME_LAYOUT_ERROR_OVERLAP,
	LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME,
	LRG_FRAME_LAYOUT_ERROR_OUT_OF_SAFE_AREA,
	LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX
} LrgFrameLayoutError;

#define LRG_TYPE_FRAME_LAYOUT_ERROR (lrg_frame_layout_error_get_type ())

/**
 * lrg_frame_layout_error_get_type:
 *
 * Returns: the #GType of #LrgFrameLayoutError
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
GType lrg_frame_layout_error_get_type (void) G_GNUC_CONST;

/**
 * lrg_frame_layout_error_quark:
 *
 * Returns: the error quark of %LRG_FRAME_LAYOUT_ERROR
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_frame_layout_error_quark (void);

#define LRG_TYPE_FRAME_LAYOUT (lrg_frame_layout_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFrameLayout, lrg_frame_layout, LRG, FRAME_LAYOUT, GObject)

/* ==========================================================================
 * Construction and the safe area
 * ========================================================================== */

/**
 * lrg_frame_layout_new:
 * @width: the frame's width in pixels
 * @height: the frame's height in pixels
 *
 * Creates an empty layout for a frame of @width x @height.  The safe area
 * starts as the whole frame.  Sizes below 1 (or not finite) become 1.
 *
 * Returns: (transfer full): a new #LrgFrameLayout
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgFrameLayout *
lrg_frame_layout_new (gfloat width,
                      gfloat height);

/**
 * lrg_frame_layout_get_width:
 * @self: an #LrgFrameLayout
 *
 * Returns: the frame's width
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_frame_layout_get_width (LrgFrameLayout *self);

/**
 * lrg_frame_layout_get_height:
 * @self: an #LrgFrameLayout
 *
 * Returns: the frame's height
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_frame_layout_get_height (LrgFrameLayout *self);

/**
 * lrg_frame_layout_set_safe_area_margins:
 * @self: an #LrgFrameLayout
 * @left: pixels kept free at the left edge
 * @top: pixels kept free at the top edge
 * @right: pixels kept free at the right edge
 * @bottom: pixels kept free at the bottom edge
 *
 * Sets the safe area to the frame minus the margins.  Negative or
 * non-finite margins count as 0; margins that together exceed the frame
 * leave a safe area of zero width (or height) where they meet.  Anchored
 * boxes follow the new safe area.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_frame_layout_set_safe_area_margins (LrgFrameLayout *self,
                                        gfloat          left,
                                        gfloat          top,
                                        gfloat          right,
                                        gfloat          bottom);

/**
 * lrg_frame_layout_set_safe_area_percent:
 * @self: an #LrgFrameLayout
 * @fraction: the share of the frame kept free at each edge, 0 .. 0.5
 *   (0.05 is the common 5 % title-safe margin)
 *
 * Sets the margins to @fraction of the frame's width (left and right)
 * and of its height (top and bottom).  @fraction is clamped into 0 .. 0.5.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_frame_layout_set_safe_area_percent (LrgFrameLayout *self,
                                        gfloat          fraction);

/**
 * lrg_frame_layout_get_safe_area:
 * @self: an #LrgFrameLayout
 * @out: (out caller-allocates): the safe area, in frame pixels
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_frame_layout_get_safe_area (LrgFrameLayout *self,
                                GrlRectangle   *out);

/* ==========================================================================
 * Boxes
 * ========================================================================== */

/**
 * lrg_frame_layout_add_box:
 * @self: an #LrgFrameLayout
 * @name: the box's name
 * @anchor: the point of the safe area the same point of the box sits on
 * @offset_x: pixels to the right of the anchor (negative moves left)
 * @offset_y: pixels below the anchor (negative moves up)
 * @width: the box's width
 * @height: the box's height
 *
 * Adds a box placed against the safe area, as lrg_hud_layout_resolve()
 * places a HUD piece on its canvas: %LRG_HUD_ANCHOR_BOTTOM_RIGHT puts
 * the box's bottom-right corner on the safe area's bottom-right corner,
 * %LRG_HUD_ANCHOR_TOP centres it on the top edge.  The offsets move it
 * right and down whatever the anchor, so move in from a right or bottom
 * edge with negative offsets.  A box of the same name is replaced (it
 * keeps its place in the order of lrg_frame_layout_get_names()).
 * Negative or non-finite sizes count as 0 and non-finite offsets as 0.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_frame_layout_add_box (LrgFrameLayout *self,
                          const gchar    *name,
                          LrgHudAnchor    anchor,
                          gfloat          offset_x,
                          gfloat          offset_y,
                          gfloat          width,
                          gfloat          height);

/**
 * lrg_frame_layout_add_box_relative:
 * @self: an #LrgFrameLayout
 * @name: the box's name
 * @of: the name of the box to place it beside
 * @side: the side of @of it goes on
 * @gap: pixels between the two boxes (negative overlaps them)
 * @width: the box's width
 * @height: the box's height
 * @align: how it lines up with @of along that side
 *
 * Adds a box beside another: %LRG_FRAME_SIDE_RIGHT with
 * %LRG_FRAME_ALIGN_END puts its left edge @gap to the right of @of's
 * right edge, bottom edges level.  The position follows @of when @of is
 * moved or replaced.  When @of does not exist (or cannot be placed, or
 * the placements loop back to @name) the box has no rectangle and
 * lrg_frame_layout_validate() reports %LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX.
 * A box of the same name is replaced.  Sizes and @gap are treated as in
 * lrg_frame_layout_add_box().
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_frame_layout_add_box_relative (LrgFrameLayout *self,
                                   const gchar    *name,
                                   const gchar    *of,
                                   LrgFrameSide    side,
                                   gfloat          gap,
                                   gfloat          width,
                                   gfloat          height,
                                   LrgFrameAlign   align);

/**
 * lrg_frame_layout_set_box_rect:
 * @self: an #LrgFrameLayout
 * @name: the box's name
 * @rect: the box's rectangle, in frame pixels
 *
 * Adds a box at an explicit rectangle, or moves an existing box there
 * (its anchor or relative placement is dropped).  A negative width or
 * height counts as 0; a non-finite value as 0.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_frame_layout_set_box_rect (LrgFrameLayout     *self,
                               const gchar        *name,
                               const GrlRectangle *rect);

/**
 * lrg_frame_layout_get_box:
 * @self: an #LrgFrameLayout
 * @name: a box's name
 * @out: (out caller-allocates) (optional): the box's rectangle, in frame
 *   pixels; zeroed when %FALSE is returned
 *
 * Where a box lies now (relative boxes follow their targets).
 *
 * Returns: %TRUE when the box exists and can be placed; %FALSE for an
 *   unknown name or a box placed relative to a missing box
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_frame_layout_get_box (LrgFrameLayout *self,
                          const gchar    *name,
                          GrlRectangle   *out);

/**
 * lrg_frame_layout_remove_box:
 * @self: an #LrgFrameLayout
 * @name: a box's name
 *
 * Removes a box.  Boxes placed relative to it lose their rectangle and
 * are reported as %LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX until a box of that
 * name is added again.  Overlaps allowed for the name are kept.
 *
 * Returns: %TRUE when the box existed
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_frame_layout_remove_box (LrgFrameLayout *self,
                             const gchar    *name);

/**
 * lrg_frame_layout_get_names:
 * @self: an #LrgFrameLayout
 *
 * The boxes' names in the order they were first added.
 *
 * Returns: (transfer container) (element-type utf8): the names, owned by
 *   the layout and valid until the boxes change; free the array with
 *   g_ptr_array_unref()
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_frame_layout_get_names (LrgFrameLayout *self);

/**
 * lrg_frame_layout_allow_overlap:
 * @self: an #LrgFrameLayout
 * @a: a box's name
 * @b: another box's name
 *
 * Declares that boxes @a and @b are meant to overlap (a speech bubble's
 * tail over its speaker, a badge on a card), so lrg_frame_layout_validate()
 * does not report them.  The order of @a and @b does not matter, the
 * boxes need not exist yet, and the permission outlives removing or
 * replacing them.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_frame_layout_allow_overlap (LrgFrameLayout *self,
                                const gchar    *a,
                                const gchar    *b);

/**
 * lrg_frame_layout_set_backdrop:
 * @self: an #LrgFrameLayout
 * @name: a box's name
 * @backdrop: %TRUE to mark the box as a backdrop, %FALSE to unmark it
 *
 * Marks a box as a backdrop: a background or full-bleed image that lies
 * under everything else.  A backdrop overlaps no other box and may reach
 * past the safe area (up to the whole frame), so neither is reported;
 * leaving the frame still is (%LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME).
 * Like lrg_frame_layout_allow_overlap(), the mark is kept by name: the
 * box need not exist yet, and the mark outlives removing or replacing it.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_frame_layout_set_backdrop (LrgFrameLayout *self,
                               const gchar    *name,
                               gboolean        backdrop);

/**
 * lrg_frame_layout_get_backdrop:
 * @self: an #LrgFrameLayout
 * @name: a box's name
 *
 * Returns: %TRUE when @name is marked as a backdrop (whether or not a
 *   box of that name exists)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_frame_layout_get_backdrop (LrgFrameLayout *self,
                               const gchar    *name);

/* ==========================================================================
 * Checks
 * ========================================================================== */

/**
 * lrg_frame_layout_get_problems:
 * @self: an #LrgFrameLayout
 *
 * Every problem of the layout as one line of text each, in the order
 * lrg_frame_layout_validate() lists them: first each box in the order of
 * lrg_frame_layout_get_names() (unknown relative target, out of frame, else out of the safe area),
 * then each overlapping pair.  Boxes that only touch along an edge or at
 * a corner do not overlap, and zero-size boxes and backdrops
 * (lrg_frame_layout_set_backdrop()) overlap nothing.
 *
 * Returns: (transfer full) (element-type utf8): the problems (empty when
 *   the layout is clean)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_frame_layout_get_problems (LrgFrameLayout *self);

/**
 * lrg_frame_layout_validate:
 * @self: an #LrgFrameLayout
 * @error: (nullable): return location for a #GError
 *
 * Checks the layout.  On failure the error's message lists every problem
 * (one per line, with the boxes' names and rectangles, as
 * lrg_frame_layout_get_problems()) and its code is the first problem's
 * #LrgFrameLayoutError.  Use it in a test as
 * `g_assert_no_error` after `lrg_frame_layout_validate (layout, &error)`.
 *
 * Returns: %TRUE when there is no problem
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_frame_layout_validate (LrgFrameLayout  *self,
                           GError         **error);

/**
 * lrg_frame_layout_draw_debug:
 * @self: an #LrgFrameLayout
 * @canvas: the canvas to draw on (frame pixel = canvas pixel)
 *
 * Draws the layout over @canvas: the safe area as a thin yellow outline,
 * every box as a green outline with its name in a label at its top-left
 * corner, backdrops (lrg_frame_layout_set_backdrop()) under the rest as a
 * thin grey dashed outline, and boxes with a problem in red with a
 * thicker outline; the shared area of an overlapping pair is shaded
 * translucent red.  The
 * canvas' transform is ignored; its clip rectangle applies.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_frame_layout_draw_debug (LrgFrameLayout *self,
                             LrgImageCanvas *canvas);

G_END_DECLS
