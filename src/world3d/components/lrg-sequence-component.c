/* lrg-sequence-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequenceComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-sequence-component.h"

struct _LrgSequenceComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSequenceComponent, lrg_sequence_component, LRG_TYPE_COMPONENT)

static void
lrg_sequence_component_class_init (LrgSequenceComponentClass *klass)
{
}

static void
lrg_sequence_component_init (LrgSequenceComponent *self)
{
}

LrgSequenceComponent *
lrg_sequence_component_new (void)
{
	return g_object_new (LRG_TYPE_SEQUENCE_COMPONENT, NULL);
}
