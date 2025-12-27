/* lrg-scripting-python.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Python scripting backend implementation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "lrg-scripting.h"
#include "../core/lrg-registry.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_PYTHON (lrg_scripting_python_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptingPython, lrg_scripting_python, LRG, SCRIPTING_PYTHON, LrgScripting)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_scripting_python_new:
 *
 * Creates a new Python scripting context.
 *
 * The context is created with a fresh Python interpreter state.
 * Use lrg_scripting_python_set_registry() to enable registry-based
 * object creation from scripts.
 *
 * Returns: (transfer full): a new #LrgScriptingPython
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingPython * lrg_scripting_python_new (void);

/* ==========================================================================
 * Registry Integration
 * ========================================================================== */

/**
 * lrg_scripting_python_set_registry:
 * @self: an #LrgScriptingPython
 * @registry: (nullable): the #LrgRegistry for type lookups
 *
 * Sets the registry used to expose types to Python.
 *
 * When set, all registered types become available in Python via the
 * `Registry` object. Scripts can create objects using
 * `Registry.create("typename", prop=value, ...)`.
 *
 * Pass %NULL to disconnect the registry.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_python_set_registry (LrgScriptingPython *self,
                                        LrgRegistry        *registry);

/**
 * lrg_scripting_python_get_registry:
 * @self: an #LrgScriptingPython
 *
 * Gets the registry used for type lookups.
 *
 * Returns: (transfer none) (nullable): the #LrgRegistry, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
LrgRegistry * lrg_scripting_python_get_registry (LrgScriptingPython *self);

/* ==========================================================================
 * Script Search Paths
 * ========================================================================== */

/**
 * lrg_scripting_python_add_search_path:
 * @self: an #LrgScriptingPython
 * @path: (type filename): directory path to add to sys.path
 *
 * Adds a directory to the Python import search path (sys.path).
 *
 * This allows scripts to use `import` to load modules from
 * the specified directory.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_python_add_search_path (LrgScriptingPython *self,
                                           const gchar        *path);

/**
 * lrg_scripting_python_clear_search_paths:
 * @self: an #LrgScriptingPython
 *
 * Clears all custom search paths added via lrg_scripting_python_add_search_path().
 *
 * The default Python search paths are preserved.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_python_clear_search_paths (LrgScriptingPython *self);

/* ==========================================================================
 * Update Hooks
 * ========================================================================== */

/**
 * lrg_scripting_python_register_update_hook:
 * @self: an #LrgScriptingPython
 * @func_name: name of the Python function to call on update
 *
 * Registers a Python function to be called each frame.
 *
 * The function receives delta time (in seconds) as its only parameter:
 * ```python
 * def game_update(delta):
 *     # update logic here
 *     pass
 * ```
 *
 * Multiple hooks can be registered and will be called in order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_python_register_update_hook (LrgScriptingPython *self,
                                                const gchar        *func_name);

/**
 * lrg_scripting_python_unregister_update_hook:
 * @self: an #LrgScriptingPython
 * @func_name: name of the Python function to unregister
 *
 * Unregisters a previously registered update hook.
 *
 * Returns: %TRUE if the hook was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_python_unregister_update_hook (LrgScriptingPython *self,
                                                      const gchar        *func_name);

/**
 * lrg_scripting_python_clear_update_hooks:
 * @self: an #LrgScriptingPython
 *
 * Clears all registered update hooks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_python_clear_update_hooks (LrgScriptingPython *self);

/**
 * lrg_scripting_python_update:
 * @self: an #LrgScriptingPython
 * @delta: time since last frame in seconds
 *
 * Calls all registered update hooks with the given delta time.
 *
 * This is typically called from the engine's update loop. Errors
 * in individual hooks are logged but do not stop other hooks
 * from being called.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_python_update (LrgScriptingPython *self,
                                  gfloat              delta);

/* ==========================================================================
 * Engine Access
 * ========================================================================== */

/**
 * lrg_scripting_python_set_engine:
 * @self: an #LrgScriptingPython
 * @engine: (nullable): the #LrgEngine to expose to scripts
 *
 * Sets the engine instance to expose to Python as the `Engine` object.
 *
 * When set, scripts can access engine subsystems:
 * ```python
 * registry = Engine.registry
 * assets = Engine.asset_manager
 * ```
 *
 * Pass %NULL to disconnect the engine.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_python_set_engine (LrgScriptingPython *self,
                                      LrgEngine          *engine);

/**
 * lrg_scripting_python_get_engine:
 * @self: an #LrgScriptingPython
 *
 * Gets the engine instance exposed to Python.
 *
 * Returns: (transfer none) (nullable): the #LrgEngine, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
LrgEngine * lrg_scripting_python_get_engine (LrgScriptingPython *self);

G_END_DECLS
