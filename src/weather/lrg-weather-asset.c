/* lrg-weather-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWeatherAsset: (stub, being written)
 */

#include "config.h"

#include "lrg-weather-asset.h"

struct _LrgWeatherAsset
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgWeatherAsset, lrg_weather_asset, G_TYPE_OBJECT)

static void
lrg_weather_asset_class_init (LrgWeatherAssetClass *klass)
{
}

static void
lrg_weather_asset_init (LrgWeatherAsset *self)
{
}

LrgWeatherAsset *
lrg_weather_asset_new (void)
{
	return g_object_new (LRG_TYPE_WEATHER_ASSET, NULL);
}
