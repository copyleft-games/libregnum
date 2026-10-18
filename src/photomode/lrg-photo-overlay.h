/* lrg-photo-overlay.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Photo overlays: frames and borders (baked into a capture) and
 * composition grids (preview only), with the geometry exposed so a
 * host can draw its own.
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
 * LRG_PHOTO_OVERLAY_MAX_GRID_LINES:
 *
 * The most lines lrg_photo_overlay_get_grid_lines() returns.
 *
 * Since: 1.0
 */
#define LRG_PHOTO_OVERLAY_MAX_GRID_LINES (8)

/**
 * lrg_photo_overlay_get_frame_borders:
 * @frame: the frame
 * @width: the picture's width in pixels
 * @height: the picture's height in pixels
 * @left: (out) (optional): the left border in pixels
 * @top: (out) (optional): the top border in pixels
 * @right: (out) (optional): the right border in pixels
 * @bottom: (out) (optional): the bottom border in pixels
 *
 * The widths of the four borders @frame draws over a @width x @height
 * picture: thin 1.2 % and wide 5 % of the shorter side; the polaroid
 * has the wide border with 16 % at the bottom; cinema leaves a 2.39:1
 * picture (bars above and below a narrower picture, beside a wider one).
 * %LRG_PHOTO_FRAME_NONE has none.  The frame covers the picture's
 * edges, it does not shrink it, so the preview and the photo match.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_overlay_get_frame_borders (LrgPhotoFrame frame,
                                     gint          width,
                                     gint          height,
                                     gint         *left,
                                     gint         *top,
                                     gint         *right,
                                     gint         *bottom);

/**
 * lrg_photo_overlay_get_picture_rect:
 * @frame: the frame
 * @width: the picture's width in pixels
 * @height: the picture's height in pixels
 * @out: (out caller-allocates): the rectangle inside the borders
 *
 * The part of the picture the frame leaves open.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_overlay_get_picture_rect (LrgPhotoFrame  frame,
                                    gint           width,
                                    gint           height,
                                    GrlRectangle  *out);

/**
 * lrg_photo_overlay_get_grid_lines:
 * @grid: the grid
 * @x: left of the area the grid covers
 * @y: top of the area
 * @width: width of the area
 * @height: height of the area
 * @out_lines: (out caller-allocates) (array length=max_lines): receives
 *   the lines as x1, y1, x2, y2 (four floats per line)
 * @max_lines: how many lines @out_lines has room for (at least
 *   %LRG_PHOTO_OVERLAY_MAX_GRID_LINES)
 *
 * The lines of @grid over an area: thirds are two vertical and two
 * horizontal at 1/3 and 2/3, golden the same at 0.382 and 0.618, centre a
 * cross, diagonals the two diagonals and the cross.
 *
 * Returns: the number of lines written (0 for %LRG_PHOTO_GRID_NONE)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_photo_overlay_get_grid_lines (LrgPhotoGrid  grid,
                                  gfloat        x,
                                  gfloat        y,
                                  gfloat        width,
                                  gfloat        height,
                                  gfloat       *out_lines,
                                  guint         max_lines);

/**
 * lrg_photo_overlay_draw_grid:
 * @grid: the grid
 * @width: the width of the view in pixels
 * @height: the height of the view in pixels
 * @area: (nullable): the part the grid covers (the open part of a frame),
 *   or %NULL for the whole view
 *
 * Draws the grid's lines in translucent white into the current target.
 * Preview only: a photo never carries the grid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_overlay_draw_grid (LrgPhotoGrid       grid,
                             gint               width,
                             gint               height,
                             const GrlRectangle *area);

/**
 * lrg_photo_overlay_draw_frame:
 * @frame: the frame
 * @width: the width of the view in pixels
 * @height: the height of the view in pixels
 * @red: the frame's colour, 0 to 255
 * @green: the frame's colour, 0 to 255
 * @blue: the frame's colour, 0 to 255
 *
 * Draws the frame's borders into the current target (the preview).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_overlay_draw_frame (LrgPhotoFrame frame,
                              gint          width,
                              gint          height,
                              guint8        red,
                              guint8        green,
                              guint8        blue);

/**
 * lrg_photo_overlay_apply_frame:
 * @frame: the frame
 * @image: the picture, painted in place
 * @red: the frame's colour, 0 to 255
 * @green: the frame's colour, 0 to 255
 * @blue: the frame's colour, 0 to 255
 *
 * Paints the frame's borders into @image on the CPU, the baked frame of
 * a captured photo.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_overlay_apply_frame (LrgPhotoFrame frame,
                               GrlImage     *image,
                               guint8        red,
                               guint8        green,
                               guint8        blue);

/**
 * lrg_photo_frame_get_label:
 * @frame: a frame
 *
 * Returns: (transfer none): the display name ("Polaroid")
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_photo_frame_get_label (LrgPhotoFrame frame);

/**
 * lrg_photo_grid_get_label:
 * @grid: a grid
 *
 * Returns: (transfer none): the display name ("Rule of thirds")
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_photo_grid_get_label (LrgPhotoGrid grid);

G_END_DECLS
