/* lrg-reel-loader.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Data-driven reels: build an #LrgReel from a YAML description, with clip types
 * resolved through an #LrgRegistry.
 *
 * The YAML shape is:
 *
 *   id: intro
 *   width: 1920
 *   height: 1080
 *   fps: 30
 *   duration-in-frames: 90
 *   clips:
 *     - type: solid-clip
 *       color: "#202830ff"
 *     - type: text-clip
 *       text: Hello
 *       font-size: 64
 *       text-x: 40
 *
 * Clip member keys map to the clip's GObject properties (kebab-case);
 * "#rrggbb"/"#rrggbbaa" strings are parsed into #GrlColor properties and enum
 * properties accept their nick.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * lrg_reel_register_types:
 * @registry: an #LrgRegistry.
 *
 * Registers the built-in reel clip types under their kebab-case names
 * (`solid-clip`, `gradient-clip`, `image-clip`, `text-clip`, `shape-clip`,
 * `caption-clip`, `video-clip`) so they can be named in YAML reels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_register_types (LrgRegistry *registry);

/**
 * lrg_reel_load_yaml:
 * @path: (type filename): path to a YAML reel file.
 * @registry: (nullable): an #LrgRegistry for clip type names; if %NULL, a
 *   temporary registry with the built-in types is used.
 * @error: (nullable): return location for a #GError.
 *
 * Returns: (transfer full) (nullable): the built #LrgReel, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReel *
lrg_reel_load_yaml (const gchar  *path,
                    LrgRegistry  *registry,
                    GError      **error);

/**
 * lrg_reel_load_yaml_string:
 * @yaml: the YAML reel document text.
 * @registry: (nullable): an #LrgRegistry, or %NULL for the built-in types.
 * @error: (nullable): return location for a #GError.
 *
 * Returns: (transfer full) (nullable): the built #LrgReel, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReel *
lrg_reel_load_yaml_string (const gchar  *yaml,
                           LrgRegistry  *registry,
                           GError      **error);

G_END_DECLS
