/* lrg-island.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgIsland: A pill-shaped panel that morphs between a compact and an expanded size.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_ISLAND (lrg_island_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIsland, lrg_island, LRG, ISLAND, LrgContainer)

/**
 * lrg_island_new:
 *
 * Returns: (transfer full): a new #LrgIsland
 */
LRG_AVAILABLE_IN_ALL
LrgIsland * lrg_island_new (void);

G_END_DECLS
