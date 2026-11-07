/* lrg-nav-agent.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavAgent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_NAV_AGENT (lrg_nav_agent_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavAgent, lrg_nav_agent, LRG, NAV_AGENT, GObject)

/**
 * lrg_nav_agent_new:
 *
 * Returns: (transfer full): a new #LrgNavAgent
 */
LRG_AVAILABLE_IN_ALL
LrgNavAgent * lrg_nav_agent_new (void);

G_END_DECLS
