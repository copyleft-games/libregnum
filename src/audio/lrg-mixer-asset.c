/* lrg-mixer-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMixerAsset: (stub, being written)
 */

#include "config.h"

#include "lrg-mixer-asset.h"

struct _LrgMixerAsset
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMixerAsset, lrg_mixer_asset, G_TYPE_OBJECT)

static void
lrg_mixer_asset_class_init (LrgMixerAssetClass *klass)
{
}

static void
lrg_mixer_asset_init (LrgMixerAsset *self)
{
}

LrgMixerAsset *
lrg_mixer_asset_new (void)
{
	return g_object_new (LRG_TYPE_MIXER_ASSET, NULL);
}
