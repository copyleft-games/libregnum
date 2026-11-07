/* lrg-mixer-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMixerAsset: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_MIXER_ASSET (lrg_mixer_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMixerAsset, lrg_mixer_asset, LRG, MIXER_ASSET, GObject)

/**
 * lrg_mixer_asset_new:
 *
 * Returns: (transfer full): a new #LrgMixerAsset
 */
LRG_AVAILABLE_IN_ALL
LrgMixerAsset * lrg_mixer_asset_new (void);

G_END_DECLS
