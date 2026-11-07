/* lrg-volumetric-fog.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVolumetricFog: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_VOLUMETRIC_FOG (lrg_volumetric_fog_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVolumetricFog, lrg_volumetric_fog, LRG, VOLUMETRIC_FOG, GObject)

/**
 * lrg_volumetric_fog_new:
 *
 * Returns: (transfer full): a new #LrgVolumetricFog
 */
LRG_AVAILABLE_IN_ALL
LrgVolumetricFog * lrg_volumetric_fog_new (void);

G_END_DECLS
