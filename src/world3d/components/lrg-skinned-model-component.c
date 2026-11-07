/* lrg-skinned-model-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedModelComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-skinned-model-component.h"

struct _LrgSkinnedModelComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSkinnedModelComponent, lrg_skinned_model_component, G_TYPE_OBJECT)

static void
lrg_skinned_model_component_class_init (LrgSkinnedModelComponentClass *klass)
{
}

static void
lrg_skinned_model_component_init (LrgSkinnedModelComponent *self)
{
}

LrgSkinnedModelComponent *
lrg_skinned_model_component_new (void)
{
	return g_object_new (LRG_TYPE_SKINNED_MODEL_COMPONENT, NULL);
}
