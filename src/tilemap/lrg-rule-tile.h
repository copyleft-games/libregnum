/* lrg-rule-tile.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRuleTile: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_RULE_TILE (lrg_rule_tile_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRuleTile, lrg_rule_tile, LRG, RULE_TILE, GObject)

/**
 * lrg_rule_tile_new:
 *
 * Returns: (transfer full): a new #LrgRuleTile
 */
LRG_AVAILABLE_IN_ALL
LrgRuleTile * lrg_rule_tile_new (void);

G_END_DECLS
