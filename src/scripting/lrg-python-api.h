/* lrg-python-api.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built-in Python API for libregnum.
 *
 * This file provides the registration of built-in globals and
 * functions that are exposed to Python scripts: Engine, Registry, Log.
 */

#pragma once

#include <glib-object.h>

/* Python.h must be included before any standard headers */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "lrg-scripting-python.h"

G_BEGIN_DECLS

/**
 * lrg_python_api_register_all:
 * @scripting: the scripting context
 *
 * Registers all built-in API globals in Python.
 *
 * This includes:
 * - Engine: Access to the engine singleton
 * - Registry: Type registry for creating objects
 * - Log: Logging functions (debug, info, warning, error)
 */
void lrg_python_api_register_all (LrgScriptingPython *scripting);

/**
 * lrg_python_api_register_engine:
 * @scripting: the scripting context
 *
 * Registers the Engine global.
 *
 * The Engine global provides access to:
 * - Engine.state: Current engine state
 * - Engine.registry: Type registry
 * - Engine.data_loader: Data loader
 * - Engine.asset_manager: Asset manager
 */
void lrg_python_api_register_engine (LrgScriptingPython *scripting);

/**
 * lrg_python_api_register_registry:
 * @scripting: the scripting context
 *
 * Registers the Registry global.
 *
 * The Registry object provides:
 * - Registry.create(type_name, **props): Create a new object
 * - Registry.is_registered(type_name): Check if type is registered
 * - Registry.get_types(): Get list of all registered types
 *
 * Example:
 * ```python
 * player = Registry.create("player", name="Hero", health=100)
 * ```
 */
void lrg_python_api_register_registry (LrgScriptingPython *scripting);

/**
 * lrg_python_api_register_log:
 * @scripting: the scripting context
 *
 * Registers the Log global.
 *
 * The Log object provides:
 * - Log.debug(message): Log a debug message
 * - Log.info(message): Log an info message
 * - Log.warning(message): Log a warning message
 * - Log.error(message): Log an error message
 *
 * Example:
 * ```python
 * Log.info("Player spawned")
 * Log.debug(f"Position: {x}, {y}")
 * ```
 */
void lrg_python_api_register_log (LrgScriptingPython *scripting);

/**
 * lrg_python_api_update_engine:
 * @scripting: the scripting context
 * @engine: (nullable): the new engine to expose
 *
 * Updates the Engine global to reference a different engine.
 *
 * This is called when lrg_scripting_python_set_engine() is used
 * to change the engine reference.
 */
void lrg_python_api_update_engine (LrgScriptingPython *scripting,
                                   LrgEngine          *engine);

/**
 * lrg_python_api_update_registry:
 * @scripting: the scripting context
 * @registry: (nullable): the new registry to expose
 *
 * Updates the Registry global to reference a different registry.
 *
 * This is called when lrg_scripting_python_set_registry() is used
 * to change the registry reference.
 */
void lrg_python_api_update_registry (LrgScriptingPython *scripting,
                                     LrgRegistry        *registry);

G_END_DECLS
