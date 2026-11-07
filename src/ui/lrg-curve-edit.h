/* lrg-curve-edit.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCurveEdit: an LrgKeyframeCurve preview and editor.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../tween/lrg-keyframe-curve.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_CURVE_EDIT (lrg_curve_edit_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCurveEdit, lrg_curve_edit, LRG, CURVE_EDIT, LrgWidget)

/**
 * lrg_curve_edit_new:
 *
 * Creates a curve editor in compact mode: a small preview of the
 * curve; a click (or Enter) opens a full editor in a popup. In full
 * mode (lrg_curve_edit_set_compact() with %FALSE) the widget is the
 * editor itself: drag a key to move it in time and value, click empty
 * space to add a key, Delete removes the selected key, the wheel or
 * E / Shift+E over the selected key cycles its easing to the next key
 * (#LrgKeyframeCurve stores an easing per key rather than tangents),
 * Escape during a drag puts the keys back.
 *
 * Returns: (transfer full): a new #LrgCurveEdit editing a linear 0..1
 *   ramp
 */
LRG_AVAILABLE_IN_ALL
LrgCurveEdit * lrg_curve_edit_new (void);

/**
 * lrg_curve_edit_get_curve:
 * @self: an #LrgCurveEdit
 *
 * Returns: (transfer none): the edited curve (the widget's own copy;
 *   copy it to keep a snapshot)
 */
LRG_AVAILABLE_IN_ALL
LrgKeyframeCurve * lrg_curve_edit_get_curve (LrgCurveEdit *self);

/**
 * lrg_curve_edit_set_curve:
 * @self: an #LrgCurveEdit
 * @curve: (nullable): the curve to show (copied), %NULL for a linear
 *   0..1 ramp
 *
 * Replaces the curve without emitting #LrgCurveEdit::curve-changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_curve_edit_set_curve (LrgCurveEdit     *self,
                               LrgKeyframeCurve *curve);

/**
 * lrg_curve_edit_get_compact:
 * @self: an #LrgCurveEdit
 *
 * Returns: %TRUE for the preview that opens a popup editor
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_curve_edit_get_compact (LrgCurveEdit *self);

/**
 * lrg_curve_edit_set_compact:
 * @self: an #LrgCurveEdit
 * @compact: %TRUE for a preview that opens a popup editor, %FALSE to
 *   edit in place
 *
 * Switches between the preview and the in-place editor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_curve_edit_set_compact (LrgCurveEdit *self,
                                 gboolean      compact);

/**
 * lrg_curve_edit_get_selected:
 * @self: an #LrgCurveEdit
 *
 * Returns: the selected key index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_curve_edit_get_selected (LrgCurveEdit *self);

/**
 * lrg_curve_edit_set_selected:
 * @self: an #LrgCurveEdit
 * @index: a key index, or -1 for none
 *
 * Selects a key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_curve_edit_set_selected (LrgCurveEdit *self,
                                  gint          index);

/**
 * lrg_curve_edit_set_value_range:
 * @self: an #LrgCurveEdit
 * @min: lowest value shown
 * @max: highest value shown
 *
 * Sets the value range of the view. Keys outside it widen the view.
 * The default is 0..1.
 */
LRG_AVAILABLE_IN_ALL
void lrg_curve_edit_set_value_range (LrgCurveEdit *self,
                                     gfloat        min,
                                     gfloat        max);

/**
 * lrg_curve_edit_get_value_range:
 * @self: an #LrgCurveEdit
 * @min: (out) (optional): lowest value shown, keys included
 * @max: (out) (optional): highest value shown, keys included
 *
 * Gets the value range currently shown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_curve_edit_get_value_range (LrgCurveEdit *self,
                                     gfloat       *min,
                                     gfloat       *max);

/**
 * lrg_curve_edit_get_graph_rect:
 * @self: an #LrgCurveEdit
 * @rect: (out caller-allocates): the world rectangle of the graph
 *
 * For tests and tutorials: where time 0..1 and the value range map to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_curve_edit_get_graph_rect (LrgCurveEdit *self,
                                    GrlRectangle *rect);

/**
 * lrg_curve_edit_get_key_position:
 * @self: an #LrgCurveEdit
 * @index: a key index
 * @x: (out) (optional): world X of the key
 * @y: (out) (optional): world Y of the key
 *
 * For tests and tutorials: where a key is drawn.
 *
 * Returns: %TRUE if @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_curve_edit_get_key_position (LrgCurveEdit *self,
                                          guint         index,
                                          gfloat       *x,
                                          gfloat       *y);

/**
 * lrg_curve_edit_open_popup:
 * @self: a compact #LrgCurveEdit on an #LrgCanvas
 *
 * Opens the full editor in a popup below the preview. Everything done
 * in it is one gesture, ended when the popup closes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_curve_edit_open_popup (LrgCurveEdit *self);

/**
 * lrg_curve_edit_get_popup_editor:
 * @self: an #LrgCurveEdit
 *
 * Returns: (transfer none) (nullable): the full editor in the open
 *   popup, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgCurveEdit * lrg_curve_edit_get_popup_editor (LrgCurveEdit *self);

G_END_DECLS
