/* lrg-nav-agent.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavAgent: (stub, being written)
 */

#include "config.h"

#include "lrg-nav-agent.h"

struct _LrgNavAgent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgNavAgent, lrg_nav_agent, G_TYPE_OBJECT)

static void
lrg_nav_agent_class_init (LrgNavAgentClass *klass)
{
}

static void
lrg_nav_agent_init (LrgNavAgent *self)
{
}

LrgNavAgent *
lrg_nav_agent_new (void)
{
	return g_object_new (LRG_TYPE_NAV_AGENT, NULL);
}
