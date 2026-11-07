/* lrg-water-body.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaterBody: (stub, being written)
 */

#include "config.h"

#include "lrg-water-body.h"

struct _LrgWaterBody
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgWaterBody, lrg_water_body, G_TYPE_OBJECT)

static void
lrg_water_body_class_init (LrgWaterBodyClass *klass)
{
}

static void
lrg_water_body_init (LrgWaterBody *self)
{
}

LrgWaterBody *
lrg_water_body_new (void)
{
	return g_object_new (LRG_TYPE_WATER_BODY, NULL);
}
