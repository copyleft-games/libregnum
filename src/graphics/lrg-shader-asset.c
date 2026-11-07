/* lrg-shader-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgShaderAsset: (stub, being written)
 */

#include "config.h"

#include "lrg-shader-asset.h"

struct _LrgShaderAsset
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgShaderAsset, lrg_shader_asset, G_TYPE_OBJECT)

static void
lrg_shader_asset_class_init (LrgShaderAssetClass *klass)
{
}

static void
lrg_shader_asset_init (LrgShaderAsset *self)
{
}

LrgShaderAsset *
lrg_shader_asset_new (void)
{
	return g_object_new (LRG_TYPE_SHADER_ASSET, NULL);
}
