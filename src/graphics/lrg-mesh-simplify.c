/* lrg-mesh-simplify.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMeshSimplify: (stub, being written)
 */

#include "config.h"

#include "lrg-mesh-simplify.h"

struct _LrgMeshSimplify
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMeshSimplify, lrg_mesh_simplify, G_TYPE_OBJECT)

static void
lrg_mesh_simplify_class_init (LrgMeshSimplifyClass *klass)
{
}

static void
lrg_mesh_simplify_init (LrgMeshSimplify *self)
{
}

LrgMeshSimplify *
lrg_mesh_simplify_new (void)
{
	return g_object_new (LRG_TYPE_MESH_SIMPLIFY, NULL);
}
