/* lrg-depth-prepass.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDepthPrepass: (stub, being written)
 */

#include "config.h"

#include "lrg-depth-prepass.h"

struct _LrgDepthPrepass
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgDepthPrepass, lrg_depth_prepass, G_TYPE_OBJECT)

static void
lrg_depth_prepass_class_init (LrgDepthPrepassClass *klass)
{
}

static void
lrg_depth_prepass_init (LrgDepthPrepass *self)
{
}

LrgDepthPrepass *
lrg_depth_prepass_new (void)
{
	return g_object_new (LRG_TYPE_DEPTH_PREPASS, NULL);
}
