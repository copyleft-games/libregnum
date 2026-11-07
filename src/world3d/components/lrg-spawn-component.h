/* lrg-spawn-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpawnComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SPAWN_COMPONENT (lrg_spawn_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpawnComponent, lrg_spawn_component, LRG, SPAWN_COMPONENT, GObject)

/**
 * lrg_spawn_component_new:
 *
 * Returns: (transfer full): a new #LrgSpawnComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSpawnComponent * lrg_spawn_component_new (void);

G_END_DECLS
