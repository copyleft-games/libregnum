/* lrg-canvas-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal canvas helpers shared within src/ui.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * _lrg_canvas_apply_pointer_transform:
 * @x: (inout): pointer X in window pixels, replaced by canvas units
 * @y: (inout): pointer Y in window pixels, replaced by canvas units
 *
 * Runs the process-wide pointer transform, if one is installed.
 */
void _lrg_canvas_apply_pointer_transform (gfloat *x,
                                          gfloat *y);

/*
 * Hooks for the UI inspector (lrg-ui-inspector.c). @self is an
 * LrgCanvas; the struct tag keeps this header free of lrg-canvas.h.
 *
 * _lrg_canvas_set_timing_enabled: measure step / layout / record
 *   durations with g_get_monotonic_time() (off by default).
 * _lrg_canvas_get_timings: the last measured durations in microseconds
 *   (record excludes layout).
 * _lrg_canvas_get_layers: appends the open popups (oldest first) and the
 *   overlay layer (bottom first) to the arrays, borrowed (no refs).
 */
struct _LrgCanvas;

void _lrg_canvas_set_timing_enabled (struct _LrgCanvas *self,
                                     gboolean           enabled);
void _lrg_canvas_get_timings        (struct _LrgCanvas *self,
                                     gint64            *step_us,
                                     gint64            *layout_us,
                                     gint64            *record_us);
void _lrg_canvas_get_layers         (struct _LrgCanvas *self,
                                     GPtrArray         *popups,
                                     GPtrArray         *overlays);

G_END_DECLS
