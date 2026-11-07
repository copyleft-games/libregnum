/* lrg-mesh-simplify.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMeshSimplify: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_MESH_SIMPLIFY (lrg_mesh_simplify_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMeshSimplify, lrg_mesh_simplify, LRG, MESH_SIMPLIFY, GObject)

/**
 * lrg_mesh_simplify_new:
 *
 * Returns: (transfer full): a new #LrgMeshSimplify
 */
LRG_AVAILABLE_IN_ALL
LrgMeshSimplify * lrg_mesh_simplify_new (void);

G_END_DECLS
