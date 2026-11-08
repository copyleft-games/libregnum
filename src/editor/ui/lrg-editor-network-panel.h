/* lrg-editor-network-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNetworkPanel: per-peer and per-message traffic from the
 * default #LrgNetStats. The panel does not dock itself; the shell (or a
 * test) adds it with lrg_editor_app_add_panel() under the id "network".
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../net/lrg-net-stats.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_NETWORK_PANEL_ID:
 *
 * Dock id of the network panel.
 *
 * Since: 0.9.0
 */
#define LRG_EDITOR_NETWORK_PANEL_ID "network"

#define LRG_TYPE_EDITOR_NETWORK_PANEL (lrg_editor_network_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorNetworkPanel, lrg_editor_network_panel, LRG, EDITOR_NETWORK_PANEL, LrgContainer)

/**
 * lrg_editor_network_panel_new:
 * @app: the editor application
 *
 * Creates the network panel for @app, or returns another reference to
 * the one already created for it. Registers the network.* IPC methods
 * and the network.show-panel / network.toggle / network.reset actions.
 * Does not dock itself.
 *
 * Returns: (transfer full): the panel
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNetworkPanel * lrg_editor_network_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_network_panel_find:
 * @app: an #LrgEditorApp
 *
 * Returns: (transfer none) (nullable): the panel created for @app, or %NULL
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNetworkPanel * lrg_editor_network_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_network_panel_get_stats:
 * @self: an #LrgEditorNetworkPanel
 *
 * The default #LrgNetStats the transports feed.
 *
 * Returns: (transfer none): the collector
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNetStats * lrg_editor_network_panel_get_stats (LrgEditorNetworkPanel *self);

G_END_DECLS
