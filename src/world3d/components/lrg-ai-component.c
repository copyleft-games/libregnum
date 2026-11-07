/* lrg-ai-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAiComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-ai-component.h"

struct _LrgAiComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgAiComponent, lrg_ai_component, LRG_TYPE_COMPONENT)

static void
lrg_ai_component_class_init (LrgAiComponentClass *klass)
{
}

static void
lrg_ai_component_init (LrgAiComponent *self)
{
}

LrgAiComponent *
lrg_ai_component_new (void)
{
	return g_object_new (LRG_TYPE_AI_COMPONENT, NULL);
}
