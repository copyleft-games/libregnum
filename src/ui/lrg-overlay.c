/* lrg-overlay.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgOverlay: A stack of children drawn on top of each other, each placed by its own alignment and margin.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-overlay.h"
#include "../lrg-log.h"

struct _LrgOverlay
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgOverlay, lrg_overlay, LRG_TYPE_CONTAINER)

static void
lrg_overlay_class_init (LrgOverlayClass *klass)
{
}

static void
lrg_overlay_init (LrgOverlay *self)
{
}

LrgOverlay *
lrg_overlay_new (void)
{
	return g_object_new (LRG_TYPE_OVERLAY, NULL);
}
