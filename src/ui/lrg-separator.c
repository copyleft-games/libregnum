/* lrg-separator.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSeparator: A thin horizontal or vertical divider line.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-separator.h"
#include "../lrg-log.h"

struct _LrgSeparator
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSeparator, lrg_separator, LRG_TYPE_WIDGET)

static void
lrg_separator_class_init (LrgSeparatorClass *klass)
{
}

static void
lrg_separator_init (LrgSeparator *self)
{
}

LrgSeparator *
lrg_separator_new (void)
{
	return g_object_new (LRG_TYPE_SEPARATOR, NULL);
}
