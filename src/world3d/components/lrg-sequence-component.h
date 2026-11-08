/* lrg-sequence-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequenceComponent: plays an #LrgSequence from an #LrgEntity3D.
 *
 * The component owns an #LrgSequencePlayer bound to the entity's world.
 * It loads its sequence from #LrgSequenceComponent:sequence-path when the
 * entity enters a world (or uses one set in code), starts it there when
 * #LrgSequenceComponent:autoplay is set, advances it in update, and stops
 * it when the entity leaves the world. The binding "self" resolves to the
 * owning entity, so one sequence asset can drive whichever entity carries
 * it (prefabs). Registry name: "sequence".
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"
#include "../../animation/lrg-sequence.h"
#include "../../animation/lrg-sequence-player.h"

G_BEGIN_DECLS

#define LRG_TYPE_SEQUENCE_COMPONENT (lrg_sequence_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSequenceComponent, lrg_sequence_component, LRG, SEQUENCE_COMPONENT, LrgComponent)

/**
 * lrg_sequence_component_new:
 *
 * Creates a component without a sequence that autoplays.
 *
 * Returns: (transfer full): a new #LrgSequenceComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceComponent * lrg_sequence_component_new (void);

/**
 * lrg_sequence_component_new_with_sequence:
 * @sequence: the sequence to play
 *
 * Returns: (transfer full): a new autoplaying #LrgSequenceComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSequenceComponent * lrg_sequence_component_new_with_sequence (LrgSequence *sequence);

/**
 * lrg_sequence_component_get_player:
 * @self: an #LrgSequenceComponent
 *
 * Returns: (transfer none): the player (connect to its signals, scrub it,
 *   bind skeletons and clips through it)
 */
LRG_AVAILABLE_IN_ALL
LrgSequencePlayer * lrg_sequence_component_get_player (LrgSequenceComponent *self);

/**
 * lrg_sequence_component_get_sequence:
 * @self: an #LrgSequenceComponent
 *
 * Returns: (transfer none) (nullable): the sequence
 */
LRG_AVAILABLE_IN_ALL
LrgSequence * lrg_sequence_component_get_sequence (LrgSequenceComponent *self);

/**
 * lrg_sequence_component_set_sequence:
 * @self: an #LrgSequenceComponent
 * @sequence: (nullable): a sequence
 *
 * Plays @sequence (replacing a path-loaded one).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_component_set_sequence (LrgSequenceComponent *self,
                                          LrgSequence          *sequence);

/**
 * lrg_sequence_component_get_sequence_path:
 * @self: an #LrgSequenceComponent
 *
 * Returns: (nullable): the `.rseq` path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sequence_component_get_sequence_path (LrgSequenceComponent *self);

/**
 * lrg_sequence_component_set_sequence_path:
 * @self: an #LrgSequenceComponent
 * @path: (nullable): a `.rseq` file
 *
 * Sets the file to load. It is loaded now when the entity is already in a
 * world, else when it enters one; a load failure is logged and leaves the
 * component without a sequence.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_component_set_sequence_path (LrgSequenceComponent *self,
                                               const gchar          *path);

/**
 * lrg_sequence_component_get_autoplay:
 * @self: an #LrgSequenceComponent
 *
 * Returns: whether the sequence starts when the entity enters a world
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sequence_component_get_autoplay (LrgSequenceComponent *self);

/**
 * lrg_sequence_component_set_autoplay:
 * @self: an #LrgSequenceComponent
 * @autoplay: whether to start on entering a world
 *
 * Sets autoplay.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_component_set_autoplay (LrgSequenceComponent *self,
                                          gboolean              autoplay);

/**
 * lrg_sequence_component_play:
 * @self: an #LrgSequenceComponent
 *
 * Shortcut for lrg_sequence_player_play() on the component's player.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_component_play (LrgSequenceComponent *self);

/**
 * lrg_sequence_component_stop:
 * @self: an #LrgSequenceComponent
 *
 * Shortcut for lrg_sequence_player_stop() on the component's player.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sequence_component_stop (LrgSequenceComponent *self);

G_END_DECLS
