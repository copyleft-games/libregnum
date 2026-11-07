/* lrg-audio-listener-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAudioListenerComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-audio-listener-component.h"

struct _LrgAudioListenerComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgAudioListenerComponent, lrg_audio_listener_component, G_TYPE_OBJECT)

static void
lrg_audio_listener_component_class_init (LrgAudioListenerComponentClass *klass)
{
}

static void
lrg_audio_listener_component_init (LrgAudioListenerComponent *self)
{
}

LrgAudioListenerComponent *
lrg_audio_listener_component_new (void)
{
	return g_object_new (LRG_TYPE_AUDIO_LISTENER_COMPONENT, NULL);
}
