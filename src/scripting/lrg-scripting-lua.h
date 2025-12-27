/* lrg-scripting-lua.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LuaJIT scripting backend implementation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "lrg-scripting.h"
#include "../core/lrg-registry.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_LUA (lrg_scripting_lua_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptingLua, lrg_scripting_lua, LRG, SCRIPTING_LUA, LrgScripting)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_scripting_lua_new:
 *
 * Creates a new LuaJIT scripting context.
 *
 * The context is created with a fresh Lua state and the standard
 * libraries loaded. Use lrg_scripting_lua_set_registry() to enable
 * registry-based object creation from scripts.
 *
 * Returns: (transfer full): a new #LrgScriptingLua
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingLua * lrg_scripting_lua_new (void);

/* ==========================================================================
 * Registry Integration
 * ========================================================================== */

/**
 * lrg_scripting_lua_set_registry:
 * @self: an #LrgScriptingLua
 * @registry: (nullable): the #LrgRegistry for type lookups
 *
 * Sets the registry used to expose types to Lua.
 *
 * When set, all registered types become available in Lua via the
 * `Registry` global table. Scripts can create objects using
 * `Registry:create("typename", {properties})`.
 *
 * Pass %NULL to disconnect the registry.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_lua_set_registry (LrgScriptingLua *self,
                                     LrgRegistry     *registry);

/**
 * lrg_scripting_lua_get_registry:
 * @self: an #LrgScriptingLua
 *
 * Gets the registry used for type lookups.
 *
 * Returns: (transfer none) (nullable): the #LrgRegistry, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
LrgRegistry * lrg_scripting_lua_get_registry (LrgScriptingLua *self);

/* ==========================================================================
 * Script Search Paths
 * ========================================================================== */

/**
 * lrg_scripting_lua_add_search_path:
 * @self: an #LrgScriptingLua
 * @path: (type filename): directory path to add to search paths
 *
 * Adds a directory to the Lua package search path.
 *
 * This allows scripts to use `require()` to load modules from
 * the specified directory. The path is added with the pattern
 * "path/?.lua" to package.path.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_lua_add_search_path (LrgScriptingLua *self,
                                        const gchar     *path);

/**
 * lrg_scripting_lua_clear_search_paths:
 * @self: an #LrgScriptingLua
 *
 * Clears all custom search paths added via lrg_scripting_lua_add_search_path().
 *
 * The default Lua search paths are preserved.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_lua_clear_search_paths (LrgScriptingLua *self);

/* ==========================================================================
 * Update Hooks
 * ========================================================================== */

/**
 * lrg_scripting_lua_register_update_hook:
 * @self: an #LrgScriptingLua
 * @func_name: name of the Lua function to call on update
 *
 * Registers a Lua function to be called each frame.
 *
 * The function receives delta time (in seconds) as its only parameter:
 * ```lua
 * function game_update(delta)
 *     -- update logic here
 * end
 * ```
 *
 * Multiple hooks can be registered and will be called in order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_lua_register_update_hook (LrgScriptingLua *self,
                                             const gchar     *func_name);

/**
 * lrg_scripting_lua_unregister_update_hook:
 * @self: an #LrgScriptingLua
 * @func_name: name of the Lua function to unregister
 *
 * Unregisters a previously registered update hook.
 *
 * Returns: %TRUE if the hook was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_lua_unregister_update_hook (LrgScriptingLua *self,
                                                   const gchar     *func_name);

/**
 * lrg_scripting_lua_clear_update_hooks:
 * @self: an #LrgScriptingLua
 *
 * Clears all registered update hooks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_lua_clear_update_hooks (LrgScriptingLua *self);

/**
 * lrg_scripting_lua_update:
 * @self: an #LrgScriptingLua
 * @delta: time since last frame in seconds
 *
 * Calls all registered update hooks with the given delta time.
 *
 * This is typically called from the engine's update loop. Errors
 * in individual hooks are logged but do not stop other hooks
 * from being called.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_lua_update (LrgScriptingLua *self,
                               gfloat           delta);

/* ==========================================================================
 * Engine Access
 * ========================================================================== */

/**
 * lrg_scripting_lua_set_engine:
 * @self: an #LrgScriptingLua
 * @engine: (nullable): the #LrgEngine to expose to scripts
 *
 * Sets the engine instance to expose to Lua as the `Engine` global.
 *
 * When set, scripts can access engine subsystems:
 * ```lua
 * local registry = Engine.registry
 * local assets = Engine.asset_manager
 * ```
 *
 * Pass %NULL to disconnect the engine.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_lua_set_engine (LrgScriptingLua *self,
                                   LrgEngine       *engine);

/**
 * lrg_scripting_lua_get_engine:
 * @self: an #LrgScriptingLua
 *
 * Gets the engine instance exposed to Lua.
 *
 * Returns: (transfer none) (nullable): the #LrgEngine, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
LrgEngine * lrg_scripting_lua_get_engine (LrgScriptingLua *self);

G_END_DECLS
