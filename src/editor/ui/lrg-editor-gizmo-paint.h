/* lrg-editor-gizmo-paint.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Editor viewport overlays recorded into an #LrgPaintList: the transform
 * gizmo, its drag readout, the orientation axes, the box-select
 * rectangle, the stats chip, the tile hover outline, the play-mode frame,
 * missing-asset boxes and the active-viewport outline.
 *
 * Every function paints in canvas coordinates. Viewport-local pixels
 * (the coordinates #LrgEditorView, #LrgEditorGizmo and
 * #LrgEditorViewportNav use for hit testing) map to the canvas as
 * (@origin_x + u * @scale, @origin_y + v * @scale), so what is drawn and
 * what is hit always come from the same geometry.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../lrg-enums.h"
#include "../../paint/lrg-paint-list.h"
#include "../../ui/lrg-theme.h"
#include "../lrg-editor-math.h"
#include "../lrg-editor-gizmo.h"
#include "../lrg-editor-view-state.h"

G_BEGIN_DECLS

/**
 * lrg_editor_gizmo_paint_transform:
 * @list: the paint list
 * @gizmo: the transform gizmo (origin, axes, hovered and active handle)
 * @tool: the editor tool; %LRG_EDITOR_TOOL_SELECT paints nothing
 * @view: the camera the gizmo is seen through, sized to the viewport
 *   image in pixels
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints the transform gizmo of @tool: rotation rings plus the screen
 * ring (rotate), arms with arrow heads and the three plane squares
 * (translate) or arms with boxes (scale), and the centre handle. The
 * hovered handle (when not dragging) and the dragged handle are
 * highlighted, the others dim slightly while a drag runs. Nothing is
 * painted when the gizmo is hidden (no selection) or behind the camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_transform (LrgPaintList        *list,
                                       LrgEditorGizmo      *gizmo,
                                       LrgEditorTool        tool,
                                       const LrgEditorView *view,
                                       gfloat               origin_x,
                                       gfloat               origin_y,
                                       gfloat               scale);

/**
 * lrg_editor_gizmo_paint_format_delta:
 * @gizmo: the transform gizmo
 * @tool: the editor tool
 *
 * The drag readout text: "Move  dx 1.25  dy 0  dz -0.5",
 * "Rotate  45.0 deg" or "Scale  x1.50 y1.50 z1.50".
 *
 * Returns: (transfer full) (nullable): the text, or %NULL when no drag
 *   is running
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_gizmo_paint_format_delta (LrgEditorGizmo *gizmo,
                                             LrgEditorTool   tool);

/**
 * lrg_editor_gizmo_paint_drag_readout:
 * @list: the paint list
 * @theme: the theme (font, colours, metrics)
 * @gizmo: the transform gizmo
 * @tool: the editor tool
 * @x: pointer x in canvas coordinates
 * @y: pointer y in canvas coordinates
 *
 * Paints lrg_editor_gizmo_paint_format_delta() on a small rounded chip
 * just below and right of the pointer while a gizmo drag runs.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_drag_readout (LrgPaintList   *list,
                                          LrgTheme       *theme,
                                          LrgEditorGizmo *gizmo,
                                          LrgEditorTool   tool,
                                          gfloat          x,
                                          gfloat          y);

/**
 * lrg_editor_gizmo_paint_axes:
 * @list: the paint list
 * @theme: the theme
 * @camera: the viewport camera; its view size places the gizmo at
 *   (%LRG_EDITOR_NAV_GIZMO_MARGIN, height - %LRG_EDITOR_NAV_GIZMO_MARGIN)
 * @hot: the hovered end (0..5) or %LRG_EDITOR_NAV_AXIS_HOT_DISC, or -1
 * @lit: brighten the disc (hovered or being dragged)
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints the orientation gizmo with the geometry of
 * lrg_editor_viewport_nav_get_gizmo_end(): a translucent disc, the
 * +X +Y +Z ends as labelled discs on lines and the negative ends as
 * smaller discs, far ends first. Nothing for a 2D camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_axes (LrgPaintList              *list,
                                  LrgTheme                  *theme,
                                  const LrgEditorViewCamera *camera,
                                  gint                       hot,
                                  gboolean                   lit,
                                  gfloat                     origin_x,
                                  gfloat                     origin_y,
                                  gfloat                     scale);

/**
 * lrg_editor_gizmo_paint_box_select:
 * @list: the paint list
 * @theme: the theme
 * @x: left edge, canvas coordinates
 * @y: top edge
 * @width: width
 * @height: height
 *
 * Paints the box-select rectangle: a translucent accent fill with a
 * crisp accent border.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_box_select (LrgPaintList *list,
                                        LrgTheme     *theme,
                                        gfloat        x,
                                        gfloat        y,
                                        gfloat        width,
                                        gfloat        height);

/**
 * lrg_editor_gizmo_paint_chip:
 * @list: the paint list
 * @theme: the theme
 * @text: the text (one or more lines)
 * @x: anchor x, canvas coordinates
 * @y: top edge
 * @align_right: %TRUE when @x is the chip's right edge
 * @accent: (nullable): text colour, %NULL for the theme's text colour
 *
 * Paints a text chip on a glass background with a soft shadow (the
 * stats overlay, hints).
 *
 * Returns: the chip's height in canvas units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_gizmo_paint_chip (LrgPaintList   *list,
                                    LrgTheme       *theme,
                                    const gchar    *text,
                                    gfloat          x,
                                    gfloat          y,
                                    gboolean        align_right,
                                    const GrlColor *accent);

/**
 * lrg_editor_gizmo_paint_tile_hover:
 * @list: the paint list
 * @theme: the theme
 * @view: the viewport camera, sized to the viewport image
 * @node: a tilemap node (its "tile_size" visual param sizes the cells)
 * @cell_x: hovered column
 * @cell_y: hovered row
 * @erase: paint the eraser colour instead of the accent
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Outlines the hovered cell of a tilemap (projected through @view) and
 * tints it faintly.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_tile_hover (LrgPaintList        *list,
                                        LrgTheme            *theme,
                                        const LrgEditorView *view,
                                        LrgNode             *node,
                                        gint                 cell_x,
                                        gint                 cell_y,
                                        gboolean             erase,
                                        gfloat               origin_x,
                                        gfloat               origin_y,
                                        gfloat               scale);

/**
 * lrg_editor_gizmo_paint_play_frame:
 * @list: the paint list
 * @theme: the theme
 * @x: viewport left, canvas coordinates
 * @y: viewport top
 * @width: viewport width
 * @height: viewport height
 * @paused: label "PAUSED" instead of "PLAYING"
 *
 * Paints the play-mode frame (a success-coloured, or warning-coloured
 * while paused, border) and a pill label at the bottom centre with the
 * play keys.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_play_frame (LrgPaintList *list,
                                        LrgTheme     *theme,
                                        gfloat        x,
                                        gfloat        y,
                                        gfloat        width,
                                        gfloat        height,
                                        gboolean      paused);

/**
 * lrg_editor_gizmo_paint_missing_asset:
 * @list: the paint list
 * @theme: the theme
 * @view: the viewport camera, sized to the viewport image
 * @node: a node whose asset could not be found
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints a magenta box around the screen bounds of @node with a small
 * "missing: file" tag, so broken references stand out in every render
 * mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_missing_asset (LrgPaintList        *list,
                                           LrgTheme            *theme,
                                           const LrgEditorView *view,
                                           LrgNode             *node,
                                           gfloat               origin_x,
                                           gfloat               origin_y,
                                           gfloat               scale);

/**
 * lrg_editor_gizmo_paint_active_outline:
 * @list: the paint list
 * @theme: the theme
 * @x: viewport left, canvas coordinates
 * @y: viewport top
 * @width: viewport width
 * @height: viewport height
 * @strength: 0..1, the outline's opacity (a spring in the panel)
 *
 * Paints the accent outline of the active viewport of a split layout.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_active_outline (LrgPaintList *list,
                                            LrgTheme     *theme,
                                            gfloat        x,
                                            gfloat        y,
                                            gfloat        width,
                                            gfloat        height,
                                            gfloat        strength);

G_END_DECLS
