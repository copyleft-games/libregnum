/* lrg-scripting-gjs.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gjs (GNOME JavaScript) scripting backend.
 *
 * This class inherits from LrgScriptingGI and uses Gjs for native
 * GObject Introspection bindings in JavaScript. Scripts can directly access
 * the full Libregnum type system via `imports.gi.Libregnum`.
 *
 * Gjs is the JavaScript bindings for GNOME, using the SpiderMonkey engine
 * from Mozilla. It provides seamless access to GObject-based libraries
 * through GObject Introspection.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgScriptingGjs *gjs = lrg_scripting_gjs_new ();
 *
 * // Load the Libregnum typelib so scripts can import it
 * lrg_scripting_gi_require_libregnum (LRG_SCRIPTING_GI (gjs), &error);
 *
 * // Expose objects to scripts
 * lrg_scripting_gi_set_engine (LRG_SCRIPTING_GI (gjs), engine);
 * lrg_scripting_gi_expose_object (LRG_SCRIPTING_GI (gjs), "engine", G_OBJECT (engine), &error);
 *
 * // Load and run script
 * lrg_scripting_load_file (LRG_SCRIPTING (gjs), "scripts/main.js", &error);
 *
 * // Register update hook
 * lrg_scripting_gi_register_update_hook (LRG_SCRIPTING_GI (gjs), "game_update");
 * ]|
 *
 * JavaScript scripts use native GI bindings:
 * |[<!-- language="JavaScript" -->
 * const GLib = imports.gi.GLib;
 * const Libregnum = imports.gi.Libregnum;
 *
 * // Access exposed objects as globals
 * let registry = engine.get_registry();
 *
 * // Create objects via registry
 * let player = registry.create("player", { name: "Hero" });
 *
 * // Update hook receives delta time
 * function game_update(delta) {
 *     player.update(delta);
 * }
 * ]|
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "lrg-scripting-gi.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_GJS (lrg_scripting_gjs_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptingGjs, lrg_scripting_gjs, LRG, SCRIPTING_GJS, LrgScriptingGI)

/**
 * lrg_scripting_gjs_new:
 *
 * Creates a new Gjs-based JavaScript scripting context.
 *
 * The context initializes the Gjs interpreter (SpiderMonkey) and sets up
 * the GObject Introspection bindings. Use the inherited LrgScriptingGI
 * methods to configure the context.
 *
 * Gjs provides seamless access to all GI-exposed types, including
 * Libregnum, GLib, Gio, and more.
 *
 * Typical setup:
 * 1. Create context with lrg_scripting_gjs_new()
 * 2. Load Libregnum typelib with lrg_scripting_gi_require_libregnum()
 * 3. Expose engine with lrg_scripting_gi_expose_object()
 * 4. Add search paths with lrg_scripting_gi_add_search_path()
 * 5. Load scripts with lrg_scripting_load_file()
 * 6. Register update hooks with lrg_scripting_gi_register_update_hook()
 *
 * Returns: (transfer full): a new #LrgScriptingGjs
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingGjs * lrg_scripting_gjs_new (void);

G_END_DECLS
