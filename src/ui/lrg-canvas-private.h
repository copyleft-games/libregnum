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

G_END_DECLS
