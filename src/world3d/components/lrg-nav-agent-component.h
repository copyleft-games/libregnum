/* lrg-nav-agent-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavAgentComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_NAV_AGENT_COMPONENT (lrg_nav_agent_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavAgentComponent, lrg_nav_agent_component, LRG, NAV_AGENT_COMPONENT, LrgComponent)

/**
 * lrg_nav_agent_component_new:
 *
 * Returns: (transfer full): a new #LrgNavAgentComponent
 */
LRG_AVAILABLE_IN_ALL
LrgNavAgentComponent * lrg_nav_agent_component_new (void);

G_END_DECLS
