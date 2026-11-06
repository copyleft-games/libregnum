/* lrg-ui-inspector.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUIInspector: Inspects a canvas: widget tree, geometry, state, looks and timings, with pick mode.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-ui-inspector.h"
#include "../lrg-log.h"

struct _LrgUIInspector
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgUIInspector, lrg_ui_inspector, G_TYPE_OBJECT)

static void
lrg_ui_inspector_class_init (LrgUIInspectorClass *klass)
{
}

static void
lrg_ui_inspector_init (LrgUIInspector *self)
{
}

LrgUIInspector *
lrg_ui_inspector_new (void)
{
	return g_object_new (LRG_TYPE_UI_INSPECTOR, NULL);
}
