/* lrg-vr-draw.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Drawing helpers for VR views: a textured quad standing in the world
 * (a UI panel showing a render texture of the game's 2D interface).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * lrg_vr_draw_quad:
 * @texture: the texture to show (a render texture's, typically)
 * @centre: (array fixed-size=3): the quad's centre, world space
 * @right: (array fixed-size=3): half the quad's width along the
 *   viewer's right
 * @up: (array fixed-size=3): half its height upwards
 * @flip_v: %TRUE for a render texture, stored bottom up: its top row
 *   goes to the quad's top edge
 * @tint: (nullable): the colour to modulate with, %NULL for white
 *
 * Draws a textured quad with corners centre +- right +- up into the
 * current camera (both faces, depth tested as the caller set it).  VR
 * games show their 2D interface this way: drawn into a render texture
 * each frame, then this quad in front of the player in every eye.
 * Flushes the batch it drew into.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_draw_quad (GrlTexture     *texture,
                       const gfloat   *centre,
                       const gfloat   *right,
                       const gfloat   *up,
                       gboolean        flip_v,
                       const GrlColor *tint);

G_END_DECLS
