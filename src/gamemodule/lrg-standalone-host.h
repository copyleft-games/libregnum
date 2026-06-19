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
#include "../lrg-enums.h"

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

/**
 * lrg_standalone_host_fullscreen_target:
 * @mode: the desired #LrgFullscreenMode
 * @monitor_width: the target monitor's width in pixels
 * @monitor_height: the target monitor's height in pixels
 * @out_width: (out): window width to request before toggling
 * @out_height: (out): window height to request before toggling
 *
 * Computes the window size that must be applied BEFORE toggling exclusive
 * fullscreen. raylib's fullscreen toggle keeps the current framebuffer size, so
 * an exclusive-fullscreen window must first be sized to the monitor or the scene
 * renders into a small rectangle in the corner. For %LRG_FULLSCREEN_FULLSCREEN
 * (with a valid monitor size) this returns the monitor size; for windowed and
 * borderless modes it returns %FALSE (no pre-resize needed — borderless sizes
 * itself, windowed keeps the requested size).
 *
 * This is the pure decision behind the host's fullscreen setup, exposed so it
 * can be unit-tested without opening a window.
 *
 * Returns: %TRUE if the window should be resized to (@out_width, @out_height).
 *
 * Since: 0.2
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_standalone_host_fullscreen_target (LrgFullscreenMode  mode,
                                       gint               monitor_width,
                                       gint               monitor_height,
                                       gint              *out_width,
                                       gint              *out_height);

G_END_DECLS
