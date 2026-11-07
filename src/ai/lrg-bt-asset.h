/* lrg-bt-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBtAsset: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_BT_ASSET (lrg_bt_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBtAsset, lrg_bt_asset, LRG, BT_ASSET, GObject)

/**
 * lrg_bt_asset_new:
 *
 * Returns: (transfer full): a new #LrgBtAsset
 */
LRG_AVAILABLE_IN_ALL
LrgBtAsset * lrg_bt_asset_new (void);

G_END_DECLS
