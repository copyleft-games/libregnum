/* lrg-skinned-sprite.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedSprite: (stub, being written)
 */

#include "config.h"

#include "lrg-skinned-sprite.h"

struct _LrgSkinnedSprite
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSkinnedSprite, lrg_skinned_sprite, G_TYPE_OBJECT)

static void
lrg_skinned_sprite_class_init (LrgSkinnedSpriteClass *klass)
{
}

static void
lrg_skinned_sprite_init (LrgSkinnedSprite *self)
{
}

LrgSkinnedSprite *
lrg_skinned_sprite_new (void)
{
	return g_object_new (LRG_TYPE_SKINNED_SPRITE, NULL);
}
