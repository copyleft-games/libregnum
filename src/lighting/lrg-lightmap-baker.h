/* lrg-lightmap-baker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightmapBaker: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_LIGHTMAP_BAKER (lrg_lightmap_baker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightmapBaker, lrg_lightmap_baker, LRG, LIGHTMAP_BAKER, GObject)

/**
 * lrg_lightmap_baker_new:
 *
 * Returns: (transfer full): a new #LrgLightmapBaker
 */
LRG_AVAILABLE_IN_ALL
LrgLightmapBaker * lrg_lightmap_baker_new (void);

G_END_DECLS
