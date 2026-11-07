/* lrg-audio-listener-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAudioListenerComponent: the ears of a 3D world.  Becomes the
 * #LrgWorld3D audio listener and feeds the entity's world position,
 * forward (-Z) and up into lrg_audio_manager_set_listener() every late
 * update.  Needs no audio device.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_AUDIO_LISTENER_COMPONENT (lrg_audio_listener_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAudioListenerComponent, lrg_audio_listener_component,
                      LRG, AUDIO_LISTENER_COMPONENT, LrgComponent)

/**
 * lrg_audio_listener_component_new:
 *
 * Creates a listener feeding the default audio manager
 * (lrg_audio_manager_get_default()).
 *
 * Returns: (transfer full): a new #LrgAudioListenerComponent
 */
LRG_AVAILABLE_IN_ALL
LrgAudioListenerComponent * lrg_audio_listener_component_new (void);

/**
 * lrg_audio_listener_component_get_audio_manager:
 * @self: an #LrgAudioListenerComponent
 *
 * Returns: (transfer none) (nullable): the explicit audio manager, %NULL
 *   for the default one
 */
LRG_AVAILABLE_IN_ALL
LrgAudioManager * lrg_audio_listener_component_get_audio_manager (LrgAudioListenerComponent *self);

/**
 * lrg_audio_listener_component_set_audio_manager:
 * @self: an #LrgAudioListenerComponent
 * @manager: (nullable): an audio manager, %NULL for the default one
 *
 * Sets the audio manager whose listener the component moves.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_listener_component_set_audio_manager (LrgAudioListenerComponent *self,
                                                     LrgAudioManager           *manager);

/**
 * lrg_audio_listener_component_sync:
 * @self: an #LrgAudioListenerComponent
 *
 * Pushes the entity's world position, forward and up to the audio
 * manager's listener now (late_update() does this every frame).  Does
 * nothing while disabled or without an entity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_listener_component_sync (LrgAudioListenerComponent *self);

G_END_DECLS
