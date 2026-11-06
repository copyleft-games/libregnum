/* lrg-rating.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRating: A row of stars (or other shapes) for a 0..N rating, with halves.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-rating.h"
#include "../lrg-log.h"

struct _LrgRating
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgRating, lrg_rating, LRG_TYPE_WIDGET)

static void
lrg_rating_class_init (LrgRatingClass *klass)
{
}

static void
lrg_rating_init (LrgRating *self)
{
}

LrgRating *
lrg_rating_new (void)
{
	return g_object_new (LRG_TYPE_RATING, NULL);
}
