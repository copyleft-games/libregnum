/* lrg-ui-loader.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUILoader: Builds widget trees from YAML documents with components and hot reload.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-ui-loader.h"
#include "../lrg-log.h"

struct _LrgUILoader
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgUILoader, lrg_ui_loader, G_TYPE_OBJECT)

static void
lrg_ui_loader_class_init (LrgUILoaderClass *klass)
{
}

static void
lrg_ui_loader_init (LrgUILoader *self)
{
}

LrgUILoader *
lrg_ui_loader_new (void)
{
	return g_object_new (LRG_TYPE_UI_LOADER, NULL);
}
