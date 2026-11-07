/* lrg-skinned-sprite.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedSprite: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SKINNED_SPRITE (lrg_skinned_sprite_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkinnedSprite, lrg_skinned_sprite, LRG, SKINNED_SPRITE, GObject)

/**
 * lrg_skinned_sprite_new:
 *
 * Returns: (transfer full): a new #LrgSkinnedSprite
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedSprite * lrg_skinned_sprite_new (void);

G_END_DECLS
