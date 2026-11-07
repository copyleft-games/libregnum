/* lrg-mesh-renderer-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMeshRendererComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-mesh-renderer-component.h"

struct _LrgMeshRendererComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMeshRendererComponent, lrg_mesh_renderer_component, G_TYPE_OBJECT)

static void
lrg_mesh_renderer_component_class_init (LrgMeshRendererComponentClass *klass)
{
}

static void
lrg_mesh_renderer_component_init (LrgMeshRendererComponent *self)
{
}

LrgMeshRendererComponent *
lrg_mesh_renderer_component_new (void)
{
	return g_object_new (LRG_TYPE_MESH_RENDERER_COMPONENT, NULL);
}
