/* lrg-environment-cockpit.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "cockpit" environment: an enclosed grid room (floor + back + side walls)
 * around the panels, for the ambient situational-awareness look.  Live
 * dashboards (the gnuseye globe, etc.) as wall textures are a documented
 * follow-up; the room shell is drawn here.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ENVIRONMENT_COCKPIT (lrg_environment_cockpit_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEnvironmentCockpit, lrg_environment_cockpit, LRG, ENVIRONMENT_COCKPIT, GObject)

LRG_AVAILABLE_IN_ALL
LrgEnvironmentCockpit * lrg_environment_cockpit_new (void);

/**
 * lrg_environment_cockpit_set_back_texture:
 * @self: a #LrgEnvironmentCockpit
 * @texture: (nullable): a live texture (e.g. a libregnum view's FBO colour
 *   attachment — the gnuseye globe, a dashboard) to show on the back wall, or
 *   %NULL to clear it (grid only)
 *
 * Displays @texture as a large quad on the cockpit's back wall.  The embedder
 * refreshes it each present from the live view FBO.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_environment_cockpit_set_back_texture (LrgEnvironmentCockpit *self,
                                               GrlTexture            *texture);

G_END_DECLS
