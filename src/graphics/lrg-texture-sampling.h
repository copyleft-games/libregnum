/* lrg-texture-sampling.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Texture sampling control beyond graylib's filters: mipmapped point
 * sampling (GL_NEAREST_MIPMAP_LINEAR), which keeps low-resolution art
 * crisp up close while distant surfaces average out instead of
 * shimmering.
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
 * lrg_texture_sampling_uses_mipmaps:
 * @sampling: an #LrgTextureSampling
 *
 * Returns: %TRUE when @sampling reads mipmap levels
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_texture_sampling_uses_mipmaps (LrgTextureSampling sampling);

/**
 * lrg_texture_set_sampling:
 * @texture: a #GrlTexture with GPU storage
 * @sampling: how to sample it
 *
 * Sets the minification and magnification filters; the mipmapped modes
 * generate the texture's mipmap chain first (once, on the GPU). Needs a
 * graphics context.
 *
 * Returns: %FALSE (and changes nothing) without a context or for a
 *   texture without GPU storage
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_texture_set_sampling (GrlTexture         *texture,
                          LrgTextureSampling  sampling);

G_END_DECLS
