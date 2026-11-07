/* lrg-nav-bake.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavBake: (stub, being written)
 */

#include "config.h"

#include "lrg-nav-bake.h"

struct _LrgNavBake
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgNavBake, lrg_nav_bake, G_TYPE_OBJECT)

static void
lrg_nav_bake_class_init (LrgNavBakeClass *klass)
{
}

static void
lrg_nav_bake_init (LrgNavBake *self)
{
}

LrgNavBake *
lrg_nav_bake_new (void)
{
	return g_object_new (LRG_TYPE_NAV_BAKE, NULL);
}
