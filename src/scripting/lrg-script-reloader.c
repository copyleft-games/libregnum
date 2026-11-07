/* lrg-script-reloader.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptReloader: (stub, being written)
 */

#include "config.h"

#include "lrg-script-reloader.h"

struct _LrgScriptReloader
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgScriptReloader, lrg_script_reloader, G_TYPE_OBJECT)

static void
lrg_script_reloader_class_init (LrgScriptReloaderClass *klass)
{
}

static void
lrg_script_reloader_init (LrgScriptReloader *self)
{
}

LrgScriptReloader *
lrg_script_reloader_new (void)
{
	return g_object_new (LRG_TYPE_SCRIPT_RELOADER, NULL);
}
