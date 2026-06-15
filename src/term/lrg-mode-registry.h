/* lrg-mode-registry.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Maps string ids to #LrgSceneArrangement / #LrgPanelEnvironment GTypes so the
 * 3D surface can create modes by name, and so new modes (including ones written
 * in another library or from Elisp via GI) register without editing the surface.
 * The default registry comes pre-loaded with the built-in modes.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-scene-arrangement.h"
#include "lrg-panel-environment.h"

G_BEGIN_DECLS

#define LRG_TYPE_MODE_REGISTRY (lrg_mode_registry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgModeRegistry, lrg_mode_registry, LRG, MODE_REGISTRY, GObject)

LRG_AVAILABLE_IN_ALL
LrgModeRegistry * lrg_mode_registry_new (void);

/**
 * lrg_mode_registry_get_default:
 *
 * Returns: (transfer none): the process-wide registry, pre-loaded with the
 *   built-in arrangements (single-panel, per-window) and environments
 *   (void, workshop)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgModeRegistry * lrg_mode_registry_get_default (void);

/**
 * lrg_mode_registry_register_arrangement:
 * @self: a #LrgModeRegistry
 * @id: the mode id
 * @type: a GType implementing #LrgSceneArrangement
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_mode_registry_register_arrangement (LrgModeRegistry *self,
                                             const gchar     *id,
                                             GType            type);

/**
 * lrg_mode_registry_register_environment:
 * @self: a #LrgModeRegistry
 * @id: the mode id
 * @type: a GType implementing #LrgPanelEnvironment
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_mode_registry_register_environment (LrgModeRegistry *self,
                                             const gchar     *id,
                                             GType            type);

/**
 * lrg_mode_registry_create_arrangement:
 * @self: a #LrgModeRegistry
 * @id: a registered arrangement id
 *
 * Returns: (transfer full) (nullable): a new arrangement instance, or %NULL if
 *   @id is unknown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSceneArrangement * lrg_mode_registry_create_arrangement (LrgModeRegistry *self,
                                                           const gchar     *id);

/**
 * lrg_mode_registry_create_environment:
 * @self: a #LrgModeRegistry
 * @id: a registered environment id
 *
 * Returns: (transfer full) (nullable): a new environment instance, or %NULL if
 *   @id is unknown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPanelEnvironment * lrg_mode_registry_create_environment (LrgModeRegistry *self,
                                                           const gchar     *id);

LRG_AVAILABLE_IN_ALL
gboolean lrg_mode_registry_has_arrangement (LrgModeRegistry *self,
                                            const gchar     *id);

LRG_AVAILABLE_IN_ALL
gboolean lrg_mode_registry_has_environment (LrgModeRegistry *self,
                                            const gchar     *id);

G_END_DECLS
