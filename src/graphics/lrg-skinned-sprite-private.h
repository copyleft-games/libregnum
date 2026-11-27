/* lrg-skinned-sprite-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal helpers of LrgSkinnedSprite shared with LrgSkeleton2D.  Not
 * installed, not scanned for introspection.
 */

#pragma once

#include <glib-object.h>
#include "graphics/lrg-skinned-sprite.h"

G_BEGIN_DECLS

/*
 * _lrg_skinned_sprite_ensure_source:
 * @self: an #LrgSkinnedSprite
 * @base_dir: (nullable): directory a relative image path is resolved
 *   against, or %NULL for the current directory
 *
 * The lazy half of lrg_skinned_sprite_get_source_image(): when no CPU
 * source image is set and the sprite has an image path that was not
 * tried yet, loads it (relative to @base_dir) and remembers the attempt,
 * so a missing file warns once instead of once per frame.
 *
 * Returns: (transfer none) (nullable): the source image
 */
GrlImage * _lrg_skinned_sprite_ensure_source (LrgSkinnedSprite *self,
                                              const gchar      *base_dir);

/*
 * _lrg_skinned_sprite_peek_source_image:
 * @self: an #LrgSkinnedSprite
 *
 * The CPU source image as set, without the lazy load of
 * lrg_skinned_sprite_get_source_image().
 *
 * Returns: (transfer none) (nullable): the source image
 */
GrlImage * _lrg_skinned_sprite_peek_source_image (LrgSkinnedSprite *self);

/*
 * _lrg_skinned_sprite_draw_to_canvas_from:
 * @self: an #LrgSkinnedSprite
 * @canvas: the canvas drawn into
 * @tint: (nullable): colour multiplied into the texture
 * @base_dir: (nullable): directory a lazily loaded relative image path
 *   is resolved against
 *
 * lrg_skinned_sprite_draw_to_canvas() with the directory of the lazy
 * source load; lrg_skeleton2d_draw_to_canvas() passes the skeleton's
 * base directory.
 */
void _lrg_skinned_sprite_draw_to_canvas_from (LrgSkinnedSprite *self,
                                              LrgImageCanvas   *canvas,
                                              const GrlColor   *tint,
                                              const gchar      *base_dir);

G_END_DECLS
