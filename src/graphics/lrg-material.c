/* lrg-material.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMaterial: (stub, being written)
 */

#include "config.h"

#include "lrg-material.h"

struct _LrgMaterial
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMaterial, lrg_material, G_TYPE_OBJECT)

static void
lrg_material_class_init (LrgMaterialClass *klass)
{
}

static void
lrg_material_init (LrgMaterial *self)
{
}

LrgMaterial *
lrg_material_new (void)
{
	return g_object_new (LRG_TYPE_MATERIAL, NULL);
}
