/* lrg-audio-listener-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAudioListenerComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_AUDIO_LISTENER_COMPONENT (lrg_audio_listener_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAudioListenerComponent, lrg_audio_listener_component, LRG, AUDIO_LISTENER_COMPONENT, GObject)

/**
 * lrg_audio_listener_component_new:
 *
 * Returns: (transfer full): a new #LrgAudioListenerComponent
 */
LRG_AVAILABLE_IN_ALL
LrgAudioListenerComponent * lrg_audio_listener_component_new (void);

G_END_DECLS
