/* lrg-sequence.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequence: (stub, being written)
 */

#include "config.h"

#include "lrg-sequence.h"

struct _LrgSequence
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSequence, lrg_sequence, G_TYPE_OBJECT)

static void
lrg_sequence_class_init (LrgSequenceClass *klass)
{
}

static void
lrg_sequence_init (LrgSequence *self)
{
}

LrgSequence *
lrg_sequence_new (void)
{
	return g_object_new (LRG_TYPE_SEQUENCE, NULL);
}
