/* lrg-engine.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Engine singleton - the central hub for all engine subsystems.
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

#define LRG_TYPE_ENGINE (lrg_engine_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgEngine, lrg_engine, LRG, ENGINE, GObject)

/**
 * LrgEngineClass:
 * @parent_class: The parent class
 * @startup: Virtual method called during engine startup
 * @shutdown: Virtual method called during engine shutdown
 * @update: Virtual method called each frame with delta time
 *
 * The class structure for #LrgEngine.
 *
 * Subclasses can override these virtual methods to customize
 * engine behavior.
 */
struct _LrgEngineClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*startup)  (LrgEngine *self);
    void (*shutdown) (LrgEngine *self);
    void (*update)   (LrgEngine *self,
                      gfloat     delta);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_engine_get_default:
 *
 * Gets the default engine instance. Creates it if it doesn't exist.
 *
 * This is the primary way to access the engine singleton.
 *
 * Returns: (transfer none): The default #LrgEngine instance
 */
LRG_AVAILABLE_IN_ALL
LrgEngine * lrg_engine_get_default (void);

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

/**
 * lrg_engine_startup:
 * @self: an #LrgEngine
 * @error: (nullable): return location for error
 *
 * Starts up the engine and all subsystems.
 *
 * This must be called before using the engine. It initializes
 * all subsystems (registry, data loader, asset manager, etc.).
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_engine_startup (LrgEngine  *self,
                             GError    **error);

/**
 * lrg_engine_shutdown:
 * @self: an #LrgEngine
 *
 * Shuts down the engine and all subsystems.
 *
 * After calling this, the engine cannot be used until
 * lrg_engine_startup() is called again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_engine_shutdown (LrgEngine *self);

/**
 * lrg_engine_update:
 * @self: an #LrgEngine
 * @delta: time since last frame in seconds
 *
 * Updates the engine for one frame.
 *
 * This should be called from the game loop to update all
 * engine systems.
 */
LRG_AVAILABLE_IN_ALL
void lrg_engine_update (LrgEngine *self,
                        gfloat     delta);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_engine_get_state:
 * @self: an #LrgEngine
 *
 * Gets the current engine state.
 *
 * Returns: The current #LrgEngineState
 */
LRG_AVAILABLE_IN_ALL
LrgEngineState lrg_engine_get_state (LrgEngine *self);

/**
 * lrg_engine_is_running:
 * @self: an #LrgEngine
 *
 * Checks if the engine is in the running state.
 *
 * Returns: %TRUE if the engine is running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_engine_is_running (LrgEngine *self);

/* ==========================================================================
 * Subsystem Access
 * ========================================================================== */

/**
 * lrg_engine_get_registry:
 * @self: an #LrgEngine
 *
 * Gets the engine's type registry.
 *
 * The registry maps string names to GTypes for data-driven
 * instantiation.
 *
 * Returns: (transfer none): The #LrgRegistry
 */
LRG_AVAILABLE_IN_ALL
LrgRegistry * lrg_engine_get_registry (LrgEngine *self);

/**
 * lrg_engine_get_data_loader:
 * @self: an #LrgEngine
 *
 * Gets the engine's data loader.
 *
 * The data loader handles loading YAML files and converting
 * them to GObjects.
 *
 * Returns: (transfer none): The #LrgDataLoader
 */
LRG_AVAILABLE_IN_ALL
LrgDataLoader * lrg_engine_get_data_loader (LrgEngine *self);

/**
 * lrg_engine_get_asset_manager:
 * @self: an #LrgEngine
 *
 * Gets the engine's asset manager.
 *
 * The asset manager handles loading and caching of game assets
 * (textures, fonts, sounds, music) with mod overlay support.
 *
 * Returns: (transfer none): The #LrgAssetManager
 */
LRG_AVAILABLE_IN_ALL
LrgAssetManager * lrg_engine_get_asset_manager (LrgEngine *self);

/* ==========================================================================
 * Version Functions (implementations from lrg-version.h.in)
 * ========================================================================== */

/* These are declared in lrg-version.h.in but implemented in lrg-engine.c */

G_END_DECLS
