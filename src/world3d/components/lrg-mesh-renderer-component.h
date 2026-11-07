/* lrg-mesh-renderer-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMeshRendererComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_MESH_RENDERER_COMPONENT (lrg_mesh_renderer_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMeshRendererComponent, lrg_mesh_renderer_component, LRG, MESH_RENDERER_COMPONENT, GObject)

/**
 * lrg_mesh_renderer_component_new:
 *
 * Returns: (transfer full): a new #LrgMeshRendererComponent
 */
LRG_AVAILABLE_IN_ALL
LrgMeshRendererComponent * lrg_mesh_renderer_component_new (void);

G_END_DECLS
