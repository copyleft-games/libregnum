/* lrg-sequence-player.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequencePlayer: (stub, being written)
 */

#include "config.h"

#include "lrg-sequence-player.h"

struct _LrgSequencePlayer
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSequencePlayer, lrg_sequence_player, G_TYPE_OBJECT)

static void
lrg_sequence_player_class_init (LrgSequencePlayerClass *klass)
{
}

static void
lrg_sequence_player_init (LrgSequencePlayer *self)
{
}

LrgSequencePlayer *
lrg_sequence_player_new (void)
{
	return g_object_new (LRG_TYPE_SEQUENCE_PLAYER, NULL);
}
