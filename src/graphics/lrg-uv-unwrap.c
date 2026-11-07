/* lrg-uv-unwrap.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUvUnwrap: (stub, being written)
 */

#include "config.h"

#include "lrg-uv-unwrap.h"

struct _LrgUvUnwrap
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgUvUnwrap, lrg_uv_unwrap, G_TYPE_OBJECT)

static void
lrg_uv_unwrap_class_init (LrgUvUnwrapClass *klass)
{
}

static void
lrg_uv_unwrap_init (LrgUvUnwrap *self)
{
}

LrgUvUnwrap *
lrg_uv_unwrap_new (void)
{
	return g_object_new (LRG_TYPE_UV_UNWRAP, NULL);
}
