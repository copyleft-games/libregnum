/* lrg-splitter.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSplitter: Two panes separated by a draggable divider.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-splitter.h"
#include "../lrg-log.h"

struct _LrgSplitter
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSplitter, lrg_splitter, LRG_TYPE_CONTAINER)

static void
lrg_splitter_class_init (LrgSplitterClass *klass)
{
}

static void
lrg_splitter_init (LrgSplitter *self)
{
}

LrgSplitter *
lrg_splitter_new (void)
{
	return g_object_new (LRG_TYPE_SPLITTER, NULL);
}
