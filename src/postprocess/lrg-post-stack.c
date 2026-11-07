/* lrg-post-stack.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPostStack: (stub, being written)
 */

#include "config.h"

#include "lrg-post-stack.h"

struct _LrgPostStack
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgPostStack, lrg_post_stack, G_TYPE_OBJECT)

static void
lrg_post_stack_class_init (LrgPostStackClass *klass)
{
}

static void
lrg_post_stack_init (LrgPostStack *self)
{
}

LrgPostStack *
lrg_post_stack_new (void)
{
	return g_object_new (LRG_TYPE_POST_STACK, NULL);
}
