/* lrg-expander.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgExpander: A header that reveals or hides its content with a spring.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-expander.h"
#include "../lrg-log.h"

struct _LrgExpander
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgExpander, lrg_expander, LRG_TYPE_CONTAINER)

static void
lrg_expander_class_init (LrgExpanderClass *klass)
{
}

static void
lrg_expander_init (LrgExpander *self)
{
}

LrgExpander *
lrg_expander_new (void)
{
	return g_object_new (LRG_TYPE_EXPANDER, NULL);
}
