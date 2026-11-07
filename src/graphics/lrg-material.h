/* lrg-material.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMaterial: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_MATERIAL (lrg_material_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMaterial, lrg_material, LRG, MATERIAL, GObject)

/**
 * lrg_material_new:
 *
 * Returns: (transfer full): a new #LrgMaterial
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_material_new (void);

G_END_DECLS
