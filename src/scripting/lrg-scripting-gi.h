/* lrg-scripting-gi.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GObject Introspection-based scripting backend base class.
 *
 * This is an intermediate derivable class between LrgScripting and
 * GI-based language implementations (Python via PyGObject, JavaScript
 * via Gjs, etc.). It provides common infrastructure and GI integration.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <girepository.h>
#include "lrg-scripting.h"
#include "../core/lrg-registry.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_GI (lrg_scripting_gi_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgScriptingGI, lrg_scripting_gi, LRG, SCRIPTING_GI, LrgScripting)

/**
 * LrgScriptingGIClass:
 * @parent_class: The parent class
 * @init_interpreter: Initialize the language interpreter (called once)
 * @finalize_interpreter: Finalize the language interpreter
 * @expose_typelib: Expose a loaded typelib to the interpreter
 * @expose_gobject: Expose a GObject instance to the interpreter as a global
 * @call_update_hook: Call a single update hook function with delta time
 * @update_search_paths: Update the interpreter's search paths after changes
 * @get_interpreter_name: Returns the interpreter name for logging
 *
 * Class structure for GI-based scripting backends.
 *
 * Subclasses must implement at minimum:
 * - init_interpreter: to set up the language runtime
 * - call_update_hook: to invoke update functions
 * - get_interpreter_name: for logging purposes
 *
 * The base class provides common infrastructure for:
 * - Registry and Engine integration (weak references)
 * - Update hook management
 * - Search path management
 * - GIRepository integration for typelib loading
 */
struct _LrgScriptingGIClass
{
    LrgScriptingClass parent_class;

    /*< public >*/

    /**
     * LrgScriptingGIClass::init_interpreter:
     * @self: an #LrgScriptingGI
     * @error: (nullable): return location for error
     *
     * Initialize the language interpreter.
     *
     * This is called once during object construction. Subclasses should
     * set up the interpreter runtime, import required modules, and
     * prepare the environment for script execution.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*init_interpreter) (LrgScriptingGI  *self,
                                  GError         **error);

    /**
     * LrgScriptingGIClass::finalize_interpreter:
     * @self: an #LrgScriptingGI
     *
     * Finalize the language interpreter.
     *
     * This is called during object disposal. Subclasses should clean up
     * interpreter resources. Note that some interpreters (like Python)
     * should not be fully finalized as it can cause issues.
     */
    void (*finalize_interpreter) (LrgScriptingGI *self);

    /**
     * LrgScriptingGIClass::expose_typelib:
     * @self: an #LrgScriptingGI
     * @namespace_: the typelib namespace (e.g., "Libregnum", "GLib")
     * @version: the typelib version (e.g., "1", "2.0")
     * @error: (nullable): return location for error
     *
     * Expose a loaded typelib to the interpreter.
     *
     * After the base class loads the typelib via GIRepository, this method
     * is called to make the types available in the scripting language.
     * For example, Python would call gi.require_version() and import.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*expose_typelib) (LrgScriptingGI  *self,
                                const gchar     *namespace_,
                                const gchar     *version,
                                GError         **error);

    /**
     * LrgScriptingGIClass::expose_gobject:
     * @self: an #LrgScriptingGI
     * @name: name to expose the object as
     * @object: (transfer none): the #GObject to expose
     * @error: (nullable): return location for error
     *
     * Expose a GObject instance to the interpreter as a named global.
     *
     * The object is wrapped using the language's GI bindings and made
     * available as a global variable with the given name.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*expose_gobject) (LrgScriptingGI  *self,
                                const gchar     *name,
                                GObject         *object,
                                GError         **error);

    /**
     * LrgScriptingGIClass::call_update_hook:
     * @self: an #LrgScriptingGI
     * @func_name: name of the function to call
     * @delta: time since last frame in seconds
     * @error: (nullable): return location for error
     *
     * Call a single update hook function.
     *
     * The base class iterates over registered hooks and calls this method
     * for each one. Subclasses implement the actual function invocation.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*call_update_hook) (LrgScriptingGI  *self,
                                  const gchar     *func_name,
                                  gfloat           delta,
                                  GError         **error);

    /**
     * LrgScriptingGIClass::update_search_paths:
     * @self: an #LrgScriptingGI
     *
     * Update the interpreter's search paths after changes.
     *
     * Called when search paths are added or cleared. Subclasses should
     * update their language-specific search path mechanism (e.g.,
     * sys.path for Python, package.path for Lua-like languages).
     */
    void (*update_search_paths) (LrgScriptingGI *self);

    /**
     * LrgScriptingGIClass::get_interpreter_name:
     * @self: an #LrgScriptingGI
     *
     * Get the interpreter name for logging.
     *
     * Returns: (transfer none): the interpreter name (e.g., "Python", "Gjs")
     */
    const gchar * (*get_interpreter_name) (LrgScriptingGI *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Registry Integration
 * ========================================================================== */

/**
 * lrg_scripting_gi_set_registry:
 * @self: an #LrgScriptingGI
 * @registry: (nullable): the #LrgRegistry for type lookups
 *
 * Sets the registry used to expose types to scripts.
 *
 * When set, registered types become available via the registry's
 * create functionality. The registry is held as a weak reference.
 *
 * Pass %NULL to disconnect the registry.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_gi_set_registry (LrgScriptingGI *self,
                                    LrgRegistry    *registry);

/**
 * lrg_scripting_gi_get_registry:
 * @self: an #LrgScriptingGI
 *
 * Gets the registry used for type lookups.
 *
 * Returns: (transfer none) (nullable): the #LrgRegistry, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
LrgRegistry * lrg_scripting_gi_get_registry (LrgScriptingGI *self);

/* ==========================================================================
 * Engine Integration
 * ========================================================================== */

/**
 * lrg_scripting_gi_set_engine:
 * @self: an #LrgScriptingGI
 * @engine: (nullable): the #LrgEngine to expose to scripts
 *
 * Sets the engine instance to expose to scripts.
 *
 * When set, scripts can access engine subsystems. The engine is held
 * as a weak reference.
 *
 * Pass %NULL to disconnect the engine.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_gi_set_engine (LrgScriptingGI *self,
                                  LrgEngine      *engine);

/**
 * lrg_scripting_gi_get_engine:
 * @self: an #LrgScriptingGI
 *
 * Gets the engine instance exposed to scripts.
 *
 * Returns: (transfer none) (nullable): the #LrgEngine, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
LrgEngine * lrg_scripting_gi_get_engine (LrgScriptingGI *self);

/* ==========================================================================
 * Search Paths
 * ========================================================================== */

/**
 * lrg_scripting_gi_add_search_path:
 * @self: an #LrgScriptingGI
 * @path: (type filename): directory path to add
 *
 * Adds a directory to the script search path.
 *
 * This allows scripts to import/require modules from the specified
 * directory. The actual mechanism depends on the language.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_gi_add_search_path (LrgScriptingGI *self,
                                       const gchar    *path);

/**
 * lrg_scripting_gi_clear_search_paths:
 * @self: an #LrgScriptingGI
 *
 * Clears all custom search paths.
 *
 * The default language-specific search paths are preserved.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_gi_clear_search_paths (LrgScriptingGI *self);

/**
 * lrg_scripting_gi_get_search_paths:
 * @self: an #LrgScriptingGI
 *
 * Gets the list of custom search paths.
 *
 * Returns: (transfer none) (array zero-terminated=1): the search paths
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_scripting_gi_get_search_paths (LrgScriptingGI *self);

/* ==========================================================================
 * Update Hooks
 * ========================================================================== */

/**
 * lrg_scripting_gi_register_update_hook:
 * @self: an #LrgScriptingGI
 * @func_name: name of the script function to call on update
 *
 * Registers a script function to be called each frame.
 *
 * The function receives delta time (in seconds) as its only parameter.
 * Multiple hooks can be registered and will be called in order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_gi_register_update_hook (LrgScriptingGI *self,
                                            const gchar    *func_name);

/**
 * lrg_scripting_gi_unregister_update_hook:
 * @self: an #LrgScriptingGI
 * @func_name: name of the script function to unregister
 *
 * Unregisters a previously registered update hook.
 *
 * Returns: %TRUE if the hook was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_gi_unregister_update_hook (LrgScriptingGI *self,
                                                  const gchar    *func_name);

/**
 * lrg_scripting_gi_clear_update_hooks:
 * @self: an #LrgScriptingGI
 *
 * Clears all registered update hooks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_gi_clear_update_hooks (LrgScriptingGI *self);

/**
 * lrg_scripting_gi_update:
 * @self: an #LrgScriptingGI
 * @delta: time since last frame in seconds
 *
 * Calls all registered update hooks with the given delta time.
 *
 * This is typically called from the engine's update loop. Errors
 * in individual hooks are logged but do not stop other hooks
 * from being called.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_gi_update (LrgScriptingGI *self,
                              gfloat          delta);

/* ==========================================================================
 * GI-Specific: Typelib Loading
 * ========================================================================== */

/**
 * lrg_scripting_gi_require_typelib:
 * @self: an #LrgScriptingGI
 * @namespace_: the typelib namespace (e.g., "GLib", "Gio")
 * @version: the typelib version (e.g., "2.0")
 * @error: (nullable): return location for error
 *
 * Loads a typelib and exposes it to the interpreter.
 *
 * This uses GIRepository to load the typelib, then calls the
 * expose_typelib virtual method to make it available in the
 * scripting language.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_gi_require_typelib (LrgScriptingGI  *self,
                                           const gchar     *namespace_,
                                           const gchar     *version,
                                           GError         **error);

/**
 * lrg_scripting_gi_require_libregnum:
 * @self: an #LrgScriptingGI
 * @error: (nullable): return location for error
 *
 * Loads the Libregnum typelib and exposes it to the interpreter.
 *
 * This is a convenience function equivalent to:
 * `lrg_scripting_gi_require_typelib(self, "Libregnum", "1", error)`
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_gi_require_libregnum (LrgScriptingGI  *self,
                                             GError         **error);

/* ==========================================================================
 * GI-Specific: GObject Exposure
 * ========================================================================== */

/**
 * lrg_scripting_gi_expose_object:
 * @self: an #LrgScriptingGI
 * @name: name to expose the object as
 * @object: (transfer none): the #GObject to expose
 * @error: (nullable): return location for error
 *
 * Exposes a GObject instance to scripts as a named global.
 *
 * The object is wrapped using the language's native GI bindings
 * and made available as a global variable with the given name.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_gi_expose_object (LrgScriptingGI  *self,
                                         const gchar     *name,
                                         GObject         *object,
                                         GError         **error);

/* ==========================================================================
 * Registered Functions Tracking
 * ========================================================================== */

/**
 * lrg_scripting_gi_has_registered_function:
 * @self: an #LrgScriptingGI
 * @name: function name to check
 *
 * Checks if a C function with the given name is registered.
 *
 * Returns: %TRUE if the function is registered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_gi_has_registered_function (LrgScriptingGI *self,
                                                   const gchar    *name);

G_END_DECLS
