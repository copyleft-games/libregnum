/* lrg-shader-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgShaderAsset: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SHADER_ASSET (lrg_shader_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgShaderAsset, lrg_shader_asset, LRG, SHADER_ASSET, GObject)

/**
 * lrg_shader_asset_new:
 *
 * Returns: (transfer full): a new #LrgShaderAsset
 */
LRG_AVAILABLE_IN_ALL
LrgShaderAsset * lrg_shader_asset_new (void);

G_END_DECLS
