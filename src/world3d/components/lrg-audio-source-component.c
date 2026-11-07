/* lrg-audio-source-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAudioSourceComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-audio-source-component.h"

struct _LrgAudioSourceComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgAudioSourceComponent, lrg_audio_source_component, G_TYPE_OBJECT)

static void
lrg_audio_source_component_class_init (LrgAudioSourceComponentClass *klass)
{
}

static void
lrg_audio_source_component_init (LrgAudioSourceComponent *self)
{
}

LrgAudioSourceComponent *
lrg_audio_source_component_new (void)
{
	return g_object_new (LRG_TYPE_AUDIO_SOURCE_COMPONENT, NULL);
}
