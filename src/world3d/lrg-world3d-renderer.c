/* lrg-world3d-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorld3DRenderer: (stub, being written)
 */

#include "config.h"

#include "lrg-world3d-renderer.h"

struct _LrgWorld3DRenderer
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgWorld3DRenderer, lrg_world3d_renderer, G_TYPE_OBJECT)

static void
lrg_world3d_renderer_class_init (LrgWorld3DRendererClass *klass)
{
}

static void
lrg_world3d_renderer_init (LrgWorld3DRenderer *self)
{
}

LrgWorld3DRenderer *
lrg_world3d_renderer_new (void)
{
	return g_object_new (LRG_TYPE_WORLD3D_RENDERER, NULL);
}
