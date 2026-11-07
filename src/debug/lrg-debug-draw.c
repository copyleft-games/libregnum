/* lrg-debug-draw.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDebugDraw: (stub, being written)
 */

#include "config.h"

#include "lrg-debug-draw.h"

struct _LrgDebugDraw
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgDebugDraw, lrg_debug_draw, G_TYPE_OBJECT)

static void
lrg_debug_draw_class_init (LrgDebugDrawClass *klass)
{
}

static void
lrg_debug_draw_init (LrgDebugDraw *self)
{
}

LrgDebugDraw *
lrg_debug_draw_new (void)
{
	return g_object_new (LRG_TYPE_DEBUG_DRAW, NULL);
}
