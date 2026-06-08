/* lrg-reel-solid-clip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelSolidClip - a reel clip that fills the frame with a solid color.
 *
 * The entire canvas is cleared to the clip's color on every rendered frame.
 * This is the cheapest way to produce a colored background or title card.
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
#include "lrg-reel-clip.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_SOLID_CLIP (lrg_reel_solid_clip_get_type ())

/**
 * LrgReelSolidClip:
 *
 * A #LrgReelClip subclass that fills the entire canvas with a single color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelSolidClip, lrg_reel_solid_clip,
                      LRG, REEL_SOLID_CLIP, LrgReelClip)

/**
 * lrg_reel_solid_clip_new:
 * @color: the fill color.
 *
 * Creates a new #LrgReelSolidClip that fills each frame with @color.
 *
 * Returns: (transfer full): a new #LrgReelSolidClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelSolidClip *
lrg_reel_solid_clip_new (const GrlColor *color);

/**
 * lrg_reel_solid_clip_get_color:
 * @self: an #LrgReelSolidClip.
 * @out_color: (out caller-allocates): return location for the color.
 *
 * Copies the current fill color into @out_color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_solid_clip_get_color (LrgReelSolidClip *self,
                                GrlColor         *out_color);

/**
 * lrg_reel_solid_clip_set_color:
 * @self: an #LrgReelSolidClip.
 * @color: the new fill color.
 *
 * Sets the fill color used when rendering this clip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_solid_clip_set_color (LrgReelSolidClip *self,
                                const GrlColor   *color);

G_END_DECLS
