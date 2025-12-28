/* lrg-scripting-pygobject.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * PyGObject-based Python scripting backend.
 *
 * This class inherits from LrgScriptingGI and uses PyGObject for native
 * GObject Introspection bindings in Python. Scripts can directly access
 * the full Libregnum type system via `from gi.repository import Libregnum`.
 *
 * Compare to LrgScriptingPython which uses direct Python C API wrappers
 * and does not require PyGObject. Use this class when you need full
 * access to Libregnum's GI-exposed API from Python scripts.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgScriptingPyGObject *pygobj = lrg_scripting_pygobject_new ();
 *
 * // Load the Libregnum typelib so scripts can import it
 * lrg_scripting_gi_require_libregnum (LRG_SCRIPTING_GI (pygobj), &error);
 *
 * // Expose objects to scripts
 * lrg_scripting_gi_set_engine (LRG_SCRIPTING_GI (pygobj), engine);
 * lrg_scripting_gi_expose_object (LRG_SCRIPTING_GI (pygobj), "engine", G_OBJECT (engine), &error);
 *
 * // Load and run script
 * lrg_scripting_run_file (LRG_SCRIPTING (pygobj), "scripts/main.py", &error);
 *
 * // Register update hook
 * lrg_scripting_gi_register_update_hook (LRG_SCRIPTING_GI (pygobj), "game_update");
 * ]|
 *
 * Python scripts use native GI bindings:
 * |[<!-- language="Python" -->
 * from gi.repository import Libregnum
 *
 * # Access exposed objects as globals
 * registry = engine.get_registry()
 *
 * # Create objects via registry
 * player = registry.create("player", name="Hero")
 *
 * # Update hook receives delta time
 * def game_update(delta):
 *     player.update(delta)
 * ]|
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "lrg-scripting-gi.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_PYGOBJECT (lrg_scripting_pygobject_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptingPyGObject, lrg_scripting_pygobject, LRG, SCRIPTING_PYGOBJECT, LrgScriptingGI)

/**
 * lrg_scripting_pygobject_new:
 *
 * Creates a new PyGObject-based Python scripting context.
 *
 * The context initializes the Python interpreter and sets up the PyGObject
 * bindings for GObject Introspection support. Use the inherited
 * LrgScriptingGI methods to configure the context.
 *
 * Unlike #LrgScriptingPython (which uses direct Python C API wrappers),
 * this class allows scripts to use native PyGObject bindings to access
 * all GI-exposed types in Libregnum.
 *
 * Typical setup:
 * 1. Create context with lrg_scripting_pygobject_new()
 * 2. Load Libregnum typelib with lrg_scripting_gi_require_libregnum()
 * 3. Expose engine with lrg_scripting_gi_expose_object()
 * 4. Add search paths with lrg_scripting_gi_add_search_path()
 * 5. Load scripts with lrg_scripting_run_file()
 * 6. Register update hooks with lrg_scripting_gi_register_update_hook()
 *
 * Returns: (transfer full): a new #LrgScriptingPyGObject
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingPyGObject * lrg_scripting_pygobject_new (void);

G_END_DECLS
