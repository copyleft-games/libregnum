/* lrg-lod-group.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLodGroup: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_LOD_GROUP (lrg_lod_group_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLodGroup, lrg_lod_group, LRG, LOD_GROUP, GObject)

/**
 * lrg_lod_group_new:
 *
 * Returns: (transfer full): a new #LrgLodGroup
 */
LRG_AVAILABLE_IN_ALL
LrgLodGroup * lrg_lod_group_new (void);

G_END_DECLS
