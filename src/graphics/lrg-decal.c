/* lrg-decal.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecal: (stub, being written)
 */

#include "config.h"

#include "lrg-decal.h"
#include "lrg-decal-glsl-private.h"

struct _LrgDecal
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgDecal, lrg_decal, G_TYPE_OBJECT)

static void
lrg_decal_class_init (LrgDecalClass *klass)
{
}

static void
lrg_decal_init (LrgDecal *self)
{
}

LrgDecal *
lrg_decal_new (void)
{
	return g_object_new (LRG_TYPE_DECAL, NULL);
}

struct _LrgDecalSet
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgDecalSet, lrg_decal_set, G_TYPE_OBJECT)

static void
lrg_decal_set_class_init (LrgDecalSetClass *klass)
{
}

static void
lrg_decal_set_init (LrgDecalSet *self)
{
}

LrgDecalSet *
lrg_decal_set_new (void)
{
	return g_object_new (LRG_TYPE_DECAL_SET, NULL);
}

/* stub binder (lrg-decal-glsl-private.h) */
void
lrg_decal_set_bind_private (LrgDecalSet *decals,
                            guint        shader_id,
                            gint        *texture_unit)
{
	(void)decals;
	(void)shader_id;
	(void)texture_unit;
}
