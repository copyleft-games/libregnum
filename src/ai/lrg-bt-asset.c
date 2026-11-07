/* lrg-bt-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBtAsset: (stub, being written)
 */

#include "config.h"

#include "lrg-bt-asset.h"

struct _LrgBtAsset
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgBtAsset, lrg_bt_asset, G_TYPE_OBJECT)

static void
lrg_bt_asset_class_init (LrgBtAssetClass *klass)
{
}

static void
lrg_bt_asset_init (LrgBtAsset *self)
{
}

LrgBtAsset *
lrg_bt_asset_new (void)
{
	return g_object_new (LRG_TYPE_BT_ASSET, NULL);
}
