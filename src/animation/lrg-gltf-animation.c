/* lrg-gltf-animation.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGltfAnimation: (stub, being written)
 */

#include "config.h"

#include "lrg-gltf-animation.h"

struct _LrgGltfAnimation
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgGltfAnimation, lrg_gltf_animation, G_TYPE_OBJECT)

static void
lrg_gltf_animation_class_init (LrgGltfAnimationClass *klass)
{
}

static void
lrg_gltf_animation_init (LrgGltfAnimation *self)
{
}

LrgGltfAnimation *
lrg_gltf_animation_new (void)
{
	return g_object_new (LRG_TYPE_GLTF_ANIMATION, NULL);
}
