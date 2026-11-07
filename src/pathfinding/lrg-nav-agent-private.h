/* lrg-nav-agent-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal LrgNavAgent hooks for LrgNavAgentComponent.  Not installed,
 * not introspected.
 */

#pragma once

#include "lrg-nav-agent.h"

G_BEGIN_DECLS

/*
 * _lrg_nav_agent_follow_position:
 * Moves the agent to @position (snapped to the mesh when near it)
 * without stopping it or replanning: the body that carries the agent
 * (a dynamic rigid body) moved it.
 */
void _lrg_nav_agent_follow_position (LrgNavAgent  *self,
                                     const gfloat *position);

G_END_DECLS
