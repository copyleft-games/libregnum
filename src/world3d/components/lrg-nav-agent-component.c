/* lrg-nav-agent-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavAgentComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-nav-agent-component.h"

struct _LrgNavAgentComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgNavAgentComponent, lrg_nav_agent_component, LRG_TYPE_COMPONENT)

static void
lrg_nav_agent_component_class_init (LrgNavAgentComponentClass *klass)
{
}

static void
lrg_nav_agent_component_init (LrgNavAgentComponent *self)
{
}

LrgNavAgentComponent *
lrg_nav_agent_component_new (void)
{
	return g_object_new (LRG_TYPE_NAV_AGENT_COMPONENT, NULL);
}
