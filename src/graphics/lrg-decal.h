/* lrg-decal.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecal: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_DECAL (lrg_decal_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDecal, lrg_decal, LRG, DECAL, GObject)

/**
 * lrg_decal_new:
 *
 * Returns: (transfer full): a new #LrgDecal
 */
LRG_AVAILABLE_IN_ALL
LrgDecal * lrg_decal_new (void);

#define LRG_TYPE_DECAL_SET (lrg_decal_set_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDecalSet, lrg_decal_set, LRG, DECAL_SET, GObject)

/**
 * lrg_decal_set_new:
 *
 * Returns: (transfer full): a new #LrgDecalSet
 */
LRG_AVAILABLE_IN_ALL
LrgDecalSet * lrg_decal_set_new (void);

G_END_DECLS
