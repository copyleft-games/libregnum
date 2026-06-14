/* lrg-render-mode.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Render-mode selector for the terminal/display surface family.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgRenderMode:
 * @LRG_RENDER_MODE_2D: Faithful flat 2D (orthographic, pixel-exact). The only
 *   mode implemented today.
 * @LRG_RENDER_MODE_3D: Windows/text as planes in a perspective scene. Reserved
 *   for a future #Lrg3DSurface; selecting it currently errors politely.
 * @LRG_RENDER_MODE_3DVR: Stereo per-eye + head/controller tracking. Reserved for
 *   a future #LrgVRSurface; selecting it currently errors politely.
 *
 * Selects which concrete #LrgFrameSurface subclass backs a frame. The seam
 * exists so 3D and VR are additive (a new subclass + a new value) rather than a
 * rewrite of the redisplay glue, which only ever talks to the #LrgFrameSurface
 * base vtable.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_RENDER_MODE_2D = 0,
    LRG_RENDER_MODE_3D = 1,
    LRG_RENDER_MODE_3DVR = 2
} LrgRenderMode;

LRG_AVAILABLE_IN_ALL
GType lrg_render_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RENDER_MODE (lrg_render_mode_get_type ())

/**
 * lrg_render_mode_to_string:
 * @mode: a #LrgRenderMode
 *
 * Returns the canonical flag token for @mode ("2d", "3d", "3dvr").
 *
 * Returns: (transfer none): a static string; never %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_render_mode_to_string (LrgRenderMode mode);

/**
 * lrg_render_mode_from_string:
 * @str: (nullable): a flag token ("2d", "3d", "3dvr"); %NULL or unknown -> 2d
 * @out_mode: (out): return location for the parsed mode
 *
 * Parses a render-mode token.
 *
 * Returns: %TRUE if @str was a recognised token, %FALSE otherwise (in which case
 *   @out_mode is set to %LRG_RENDER_MODE_2D)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_render_mode_from_string (const gchar   *str,
                                      LrgRenderMode *out_mode);

G_END_DECLS
