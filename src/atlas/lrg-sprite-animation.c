/* lrg-sprite-animation.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpriteAnimation: (stub, being written)
 */

#include "config.h"

#include "lrg-sprite-animation.h"

struct _LrgSpriteAnimation
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSpriteAnimation, lrg_sprite_animation, G_TYPE_OBJECT)

static void
lrg_sprite_animation_class_init (LrgSpriteAnimationClass *klass)
{
}

static void
lrg_sprite_animation_init (LrgSpriteAnimation *self)
{
}

LrgSpriteAnimation *
lrg_sprite_animation_new (void)
{
	return g_object_new (LRG_TYPE_SPRITE_ANIMATION, NULL);
}
