/* lrg-breadcrumb.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBreadcrumb: A path of clickable segments that collapses to an ellipsis when narrow.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-breadcrumb.h"
#include "../lrg-log.h"

struct _LrgBreadcrumb
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgBreadcrumb, lrg_breadcrumb, LRG_TYPE_WIDGET)

static void
lrg_breadcrumb_class_init (LrgBreadcrumbClass *klass)
{
}

static void
lrg_breadcrumb_init (LrgBreadcrumb *self)
{
}

LrgBreadcrumb *
lrg_breadcrumb_new (void)
{
	return g_object_new (LRG_TYPE_BREADCRUMB, NULL);
}
