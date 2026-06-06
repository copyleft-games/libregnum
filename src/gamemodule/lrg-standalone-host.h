/* lrg-standalone-host.h - LrgGameHost that owns a real window + main loop
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_STANDALONE_HOST (lrg_standalone_host_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgStandaloneHost, lrg_standalone_host, LRG, STANDALONE_HOST, GObject)

/**
 * lrg_standalone_host_new:
 * @game: the #LrgGameTemplate whose window configuration to use
 * @error: (nullable): return location for error
 *
 * Creates a standalone host: it starts the engine (refcounted), creates and
 * configures a real window from @game's properties (title, size, fps, vsync,
 * resize, fullscreen), and registers the window with the engine. This is the
 * host used by lrg_game_template_run() and the shim launcher.
 *
 * Returns: (transfer full) (nullable): a new #LrgStandaloneHost, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStandaloneHost *
lrg_standalone_host_new (LrgGameTemplate  *game,
                         GError          **error);

/**
 * lrg_standalone_host_teardown:
 * @self: an #LrgStandaloneHost
 *
 * Disconnects the window from the engine, releases the window, and shuts the
 * engine down (refcounted). Safe to call more than once. Call this after the
 * game's shutdown and before dropping the last reference to the host.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_standalone_host_teardown (LrgStandaloneHost *self);

G_END_DECLS
