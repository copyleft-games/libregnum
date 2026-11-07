/* lrg-rule-tile.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRuleTile: (stub, being written)
 */

#include "config.h"

#include "lrg-rule-tile.h"

struct _LrgRuleTile
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgRuleTile, lrg_rule_tile, G_TYPE_OBJECT)

static void
lrg_rule_tile_class_init (LrgRuleTileClass *klass)
{
}

static void
lrg_rule_tile_init (LrgRuleTile *self)
{
}

LrgRuleTile *
lrg_rule_tile_new (void)
{
	return g_object_new (LRG_TYPE_RULE_TILE, NULL);
}
