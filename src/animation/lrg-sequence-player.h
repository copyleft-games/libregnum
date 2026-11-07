/* lrg-sequence-player.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequencePlayer: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SEQUENCE_PLAYER (lrg_sequence_player_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSequencePlayer, lrg_sequence_player, LRG, SEQUENCE_PLAYER, GObject)

/**
 * lrg_sequence_player_new:
 *
 * Returns: (transfer full): a new #LrgSequencePlayer
 */
LRG_AVAILABLE_IN_ALL
LrgSequencePlayer * lrg_sequence_player_new (void);

G_END_DECLS
