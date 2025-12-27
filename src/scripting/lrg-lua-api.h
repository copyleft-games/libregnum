/* lrg-lua-api.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built-in Lua API for libregnum.
 *
 * This file provides the registration of built-in globals and
 * functions that are exposed to Lua scripts: Engine, Registry, Log.
 */

#pragma once

#include <glib-object.h>
#include <lua.h>

#include "lrg-scripting-lua.h"

G_BEGIN_DECLS

/**
 * lrg_lua_api_register_all:
 * @L: the Lua state
 * @scripting: the scripting context
 *
 * Registers all built-in API globals in the Lua state.
 *
 * This includes:
 * - Engine: Access to the engine singleton
 * - Registry: Type registry for creating objects
 * - Log: Logging functions (debug, info, warning, error)
 */
void lrg_lua_api_register_all (lua_State       *L,
                               LrgScriptingLua *scripting);

/**
 * lrg_lua_api_register_engine:
 * @L: the Lua state
 * @scripting: the scripting context
 *
 * Registers the Engine global.
 *
 * The Engine global provides access to:
 * - Engine.state: Current engine state
 * - Engine.registry: Type registry
 * - Engine.data_loader: Data loader
 * - Engine.asset_manager: Asset manager
 *
 * Also allows connecting to engine signals:
 * ```lua
 * Engine:connect("pre-update", function(delta)
 *     -- called before each update
 * end)
 * ```
 */
void lrg_lua_api_register_engine (lua_State       *L,
                                  LrgScriptingLua *scripting);

/**
 * lrg_lua_api_register_registry:
 * @L: the Lua state
 * @scripting: the scripting context
 *
 * Registers the Registry global.
 *
 * The Registry table provides:
 * - Registry:create(type_name, [properties]): Create a new object
 * - Registry:is_registered(type_name): Check if type is registered
 * - Registry:get_types(): Get table of all registered types
 *
 * Example:
 * ```lua
 * local player = Registry:create("player", {
 *     name = "Hero",
 *     health = 100
 * })
 * ```
 */
void lrg_lua_api_register_registry (lua_State       *L,
                                    LrgScriptingLua *scripting);

/**
 * lrg_lua_api_register_log:
 * @L: the Lua state
 *
 * Registers the Log global.
 *
 * The Log table provides:
 * - Log.debug(message, ...): Log a debug message
 * - Log.info(message, ...): Log an info message
 * - Log.warning(message, ...): Log a warning message
 * - Log.error(message, ...): Log an error message
 *
 * Messages support printf-style formatting:
 * ```lua
 * Log.info("Player %s has %d health", player.name, player.health)
 * ```
 */
void lrg_lua_api_register_log (lua_State *L);

/**
 * lrg_lua_api_update_engine:
 * @L: the Lua state
 * @engine: (nullable): the new engine to expose
 *
 * Updates the Engine global to reference a different engine.
 *
 * This is called when lrg_scripting_lua_set_engine() is used
 * to change the engine reference.
 */
void lrg_lua_api_update_engine (lua_State *L,
                                LrgEngine *engine);

/**
 * lrg_lua_api_update_registry:
 * @L: the Lua state
 * @registry: (nullable): the new registry to expose
 *
 * Updates the Registry global to reference a different registry.
 *
 * This is called when lrg_scripting_lua_set_registry() is used
 * to change the registry reference.
 */
void lrg_lua_api_update_registry (lua_State   *L,
                                  LrgRegistry *registry);

G_END_DECLS
