/* lrg-retargeter.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRetargeter: (stub, being written)
 */

#include "config.h"

#include "lrg-retargeter.h"

struct _LrgRetargeter
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgRetargeter, lrg_retargeter, G_TYPE_OBJECT)

static void
lrg_retargeter_class_init (LrgRetargeterClass *klass)
{
}

static void
lrg_retargeter_init (LrgRetargeter *self)
{
}

LrgRetargeter *
lrg_retargeter_new (void)
{
	return g_object_new (LRG_TYPE_RETARGETER, NULL);
}
