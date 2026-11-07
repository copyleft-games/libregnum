/* lrg-scripting.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for scripting engine implementations.
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

#define LRG_TYPE_SCRIPTING (lrg_scripting_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgScripting, lrg_scripting, LRG, SCRIPTING, GObject)

/**
 * LrgScriptingCFunction:
 * @scripting: the scripting context
 * @n_args: number of arguments passed
 * @args: (array length=n_args): array of argument values
 * @return_value: (out): location to store return value
 * @user_data: user data passed to register_function
 * @error: return location for error
 *
 * Callback signature for C functions exposed to scripts.
 *
 * The callback receives the arguments passed from the script and should
 * set the return value if the function returns something. If an error
 * occurs, set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
typedef gboolean (*LrgScriptingCFunction) (LrgScripting  *scripting,
                                           guint          n_args,
                                           const GValue  *args,
                                           GValue        *return_value,
                                           gpointer       user_data,
                                           GError       **error);

/**
 * LrgScriptingClass:
 * @parent_class: The parent class
 * @load_file: Load and execute a script from a file
 * @load_string: Load and execute a script from a string
 * @call_function: Call a function defined in the script
 * @register_function: Register a C function callable from scripts
 * @get_global: Get a global variable from the script context
 * @set_global: Set a global variable in the script context
 * @reset: Reset the script context to a clean state
 *
 * Abstract class for scripting engine implementations.
 *
 * Subclasses must implement all virtual methods to provide
 * a complete scripting backend.
 */
struct _LrgScriptingClass
{
    GObjectClass parent_class;

    /*< public >*/

    /**
     * LrgScriptingClass::load_file:
     * @self: an #LrgScripting
     * @path: path to the script file
     * @error: (nullable): return location for error
     *
     * Load and execute a script from a file.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*load_file) (LrgScripting  *self,
                           const gchar   *path,
                           GError       **error);

    /**
     * LrgScriptingClass::load_string:
     * @self: an #LrgScripting
     * @name: name to identify the script (for error messages)
     * @code: the script source code
     * @error: (nullable): return location for error
     *
     * Load and execute a script from a string.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*load_string) (LrgScripting  *self,
                             const gchar   *name,
                             const gchar   *code,
                             GError       **error);

    /**
     * LrgScriptingClass::call_function:
     * @self: an #LrgScripting
     * @func_name: name of the function to call
     * @return_value: (out) (nullable): location to store return value
     * @n_args: number of arguments
     * @args: (array length=n_args) (nullable): array of arguments
     * @error: (nullable): return location for error
     *
     * Call a function defined in the script.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*call_function) (LrgScripting  *self,
                               const gchar   *func_name,
                               GValue        *return_value,
                               guint          n_args,
                               const GValue  *args,
                               GError       **error);

    /**
     * LrgScriptingClass::register_function:
     * @self: an #LrgScripting
     * @name: name to expose the function as
     * @func: the C function to call
     * @user_data: user data to pass to the function
     * @error: (nullable): return location for error
     *
     * Register a C function that can be called from scripts.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*register_function) (LrgScripting           *self,
                                   const gchar            *name,
                                   LrgScriptingCFunction   func,
                                   gpointer                user_data,
                                   GError                **error);

    /**
     * LrgScriptingClass::get_global:
     * @self: an #LrgScripting
     * @name: name of the global variable
     * @value: (out): location to store the value
     * @error: (nullable): return location for error
     *
     * Get a global variable from the script context.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*get_global) (LrgScripting  *self,
                            const gchar   *name,
                            GValue        *value,
                            GError       **error);

    /**
     * LrgScriptingClass::set_global:
     * @self: an #LrgScripting
     * @name: name of the global variable
     * @value: the value to set
     * @error: (nullable): return location for error
     *
     * Set a global variable in the script context.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*set_global) (LrgScripting  *self,
                            const gchar   *name,
                            const GValue  *value,
                            GError       **error);

    /**
     * LrgScriptingClass::reset:
     * @self: an #LrgScripting
     *
     * Reset the script context to a clean state.
     *
     * This clears all loaded scripts, global variables, and
     * registered functions.
     */
    void (*reset) (LrgScripting *self);

    /**
     * LrgScriptingClass::add_search_path:
     * @self: an #LrgScripting
     * @path: a directory
     *
     * Add a directory the backend searches when a script imports or includes
     * another file: Lua `package.path`, Python `sys.path`, the Gjs search
     * path, or a C include directory (`-I`) for compiled backends.
     *
     * Since: 0.2
     */
    void (*add_search_path) (LrgScripting *self,
                             const gchar  *path);

    /**
     * LrgScriptingClass::has_function:
     * @self: an #LrgScripting
     * @func_name: a function name
     *
     * Check whether the loaded scripts define a callable @func_name.  Hosts
     * use this for optional entry points without provoking an error.
     *
     * Returns: %TRUE when @func_name can be called
     *
     * Since: 0.2
     */
    gboolean (*has_function) (LrgScripting *self,
                              const gchar  *func_name);

    /**
     * LrgScriptingClass::reload_file:
     * @self: an #LrgScripting
     * @path: a script file this context loaded
     * @error: (nullable): return location for error
     *
     * Re-execute @path in the live context (hot reload).  Implementations
     * call the #LRG_SCRIPT_HOOK_BEFORE_RELOAD hook, run the new code, then
     * #LRG_SCRIPT_HOOK_RELOAD with the saved state, and keep the old code
     * when the new code fails to compile or run.  The default
     * implementation does this through call_function() and load_file()
     * with the state as a #GValue (no rollback of a half-run file).
     *
     * Returns: %TRUE on success
     *
     * Since: 0.9
     */
    gboolean (*reload_file) (LrgScripting  *self,
                             const gchar   *path,
                             GError       **error);

    /*< private >*/
    gpointer _reserved[5];
};

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_scripting_load_file:
 * @self: an #LrgScripting
 * @path: (type filename): path to the script file
 * @error: (nullable): return location for error
 *
 * Load and execute a script from a file.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_load_file (LrgScripting  *self,
                                  const gchar   *path,
                                  GError       **error);

/**
 * lrg_scripting_load_string:
 * @self: an #LrgScripting
 * @name: name to identify the script (for error messages)
 * @code: the script source code
 * @error: (nullable): return location for error
 *
 * Load and execute a script from a string.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_load_string (LrgScripting  *self,
                                    const gchar   *name,
                                    const gchar   *code,
                                    GError       **error);

/**
 * lrg_scripting_call_function:
 * @self: an #LrgScripting
 * @func_name: name of the function to call
 * @return_value: (out) (optional) (nullable): location to store return value
 * @n_args: number of arguments
 * @args: (array length=n_args) (nullable): array of arguments
 * @error: (nullable): return location for error
 *
 * Call a function defined in the script.
 *
 * If @return_value is not %NULL, it will be initialized and set to
 * the function's return value. The caller is responsible for calling
 * g_value_unset() on it when done.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_call_function (LrgScripting  *self,
                                      const gchar   *func_name,
                                      GValue        *return_value,
                                      guint          n_args,
                                      const GValue  *args,
                                      GError       **error);

/**
 * lrg_scripting_register_function:
 * @self: an #LrgScripting
 * @name: name to expose the function as
 * @func: (scope notified) (closure user_data): the C function to call
 * @user_data: user data to pass to the function
 * @error: (nullable): return location for error
 *
 * Register a C function that can be called from scripts.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_register_function (LrgScripting           *self,
                                          const gchar            *name,
                                          LrgScriptingCFunction   func,
                                          gpointer                user_data,
                                          GError                **error);

/**
 * lrg_scripting_get_global:
 * @self: an #LrgScripting
 * @name: name of the global variable
 * @value: (out caller-allocates): location to store the value
 * @error: (nullable): return location for error
 *
 * Get a global variable from the script context.
 *
 * The @value parameter should be an uninitialized #GValue. On success,
 * it will be initialized with the appropriate type and value.
 * The caller is responsible for calling g_value_unset() when done.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_get_global (LrgScripting  *self,
                                   const gchar   *name,
                                   GValue        *value,
                                   GError       **error);

/**
 * lrg_scripting_set_global:
 * @self: an #LrgScripting
 * @name: name of the global variable
 * @value: the value to set
 * @error: (nullable): return location for error
 *
 * Set a global variable in the script context.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_set_global (LrgScripting  *self,
                                   const gchar   *name,
                                   const GValue  *value,
                                   GError       **error);

/**
 * lrg_scripting_reset:
 * @self: an #LrgScripting
 *
 * Reset the script context to a clean state.
 *
 * This clears all loaded scripts, global variables, and
 * registered functions, returning the scripting context
 * to its initial state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_reset (LrgScripting *self);

/**
 * lrg_scripting_add_search_path:
 * @self: an #LrgScripting
 * @path: (type filename): a directory
 *
 * Add a directory the backend searches when a script imports or includes
 * another file.  Backends without an import mechanism ignore it.
 *
 * Since: 0.2
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_add_search_path (LrgScripting *self,
                                    const gchar  *path);

/**
 * lrg_scripting_has_function:
 * @self: an #LrgScripting
 * @func_name: a function name
 *
 * Check whether the loaded scripts define a callable @func_name.
 *
 * Returns: %TRUE when @func_name can be called; %FALSE when it is missing,
 *   not callable, or the backend cannot tell
 *
 * Since: 0.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_has_function (LrgScripting *self,
                                     const gchar  *func_name);

/**
 * lrg_scripting_reload_file:
 * @self: an #LrgScripting
 * @path: (type filename): a script file
 * @error: (nullable): return location for error
 *
 * Hot-reloads @path in this live context: calls the script's
 * `on_before_reload()` hook (if any), re-executes the file, then calls
 * `on_reload(old_state)` (if any).  When the new code fails to compile (or,
 * for Lua and Python, to run its top level) the old definitions are kept
 * and the error carries the failing file and line (see
 * lrg_scripting_get_error_location()).  Most callers use
 * #LrgScriptReloader instead.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_reload_file (LrgScripting  *self,
                                    const gchar   *path,
                                    GError       **error);

/**
 * lrg_scripting_get_loaded_files:
 * @self: an #LrgScripting
 *
 * Lists the files lrg_scripting_load_file() executed in this context
 * since the last reset, as canonical absolute paths in load order.
 *
 * Returns: (transfer full) (array zero-terminated=1) (element-type filename):
 *   the loaded files
 *
 * Since: 0.9
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_scripting_get_loaded_files (LrgScripting *self);

/**
 * lrg_scripting_has_loaded_file:
 * @self: an #LrgScripting
 * @path: (type filename): a path (canonicalized before comparing)
 *
 * Returns: whether this context loaded @path since the last reset
 *
 * Since: 0.9
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_has_loaded_file (LrgScripting *self,
                                        const gchar  *path);

/**
 * lrg_scripting_get_search_paths:
 * @self: an #LrgScripting
 *
 * Lists the directories added with lrg_scripting_add_search_path(), as
 * canonical absolute paths.
 *
 * Returns: (transfer full) (array zero-terminated=1) (element-type filename):
 *   the search paths
 *
 * Since: 0.9
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_scripting_get_search_paths (LrgScripting *self);

/**
 * lrg_scripting_get_error_location:
 * @self: an #LrgScripting
 * @file: (out) (optional) (transfer full) (type filename): the file of the
 *   last load or reload error
 * @line: (out) (optional): its 1-based line (0 when unknown)
 *
 * Returns where the most recent load, reload or call error of this context
 * happened, when the backend could tell.
 *
 * Returns: %TRUE when a location is known
 *
 * Since: 0.9
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_get_error_location (LrgScripting  *self,
                                           gchar        **file,
                                           gint          *line);

/**
 * lrg_scripting_list_instances:
 *
 * Lists every live #LrgScripting in the process (any thread), oldest
 * first.  The debugger's auto-attach and the reloader's watch-all use it.
 *
 * Returns: (transfer full) (element-type LrgScripting): the contexts
 *
 * Since: 0.9
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_scripting_list_instances (void);

/* ==========================================================================
 * Library-internal helpers (backends, debugger, reloader).  Only visible to
 * files that define LRG_SCRIPTING_PRIVATE before including this header; the
 * GIR scanner and applications never do.
 * ========================================================================== */

#ifdef LRG_SCRIPTING_PRIVATE

/* called (with the new context) after every LrgScripting is constructed */
typedef void (*LrgScriptingInstanceHook) (LrgScripting *scripting,
                                          gpointer      user_data);

G_GNUC_INTERNAL void           lrg_scripting_priv_add_instance_hook    (LrgScriptingInstanceHook  hook,
                                                                        gpointer                  user_data);
G_GNUC_INTERNAL void           lrg_scripting_priv_remove_instance_hook (LrgScriptingInstanceHook  hook,
                                                                        gpointer                  user_data);
/* the context executing on this thread (innermost), or NULL; no ref */
G_GNUC_INTERNAL LrgScripting * lrg_scripting_priv_get_current          (void);
G_GNUC_INTERNAL void           lrg_scripting_priv_push_current         (LrgScripting             *self);
G_GNUC_INTERNAL void           lrg_scripting_priv_pop_current          (LrgScripting             *self);
/* records where the last error happened (file may be NULL to clear) */
G_GNUC_INTERNAL void           lrg_scripting_priv_set_error_location   (LrgScripting             *self,
                                                                        const gchar              *file,
                                                                        gint                      line);
/* adds @path to the loaded-files list (canonicalized) */
G_GNUC_INTERNAL void           lrg_scripting_priv_note_loaded          (LrgScripting             *self,
                                                                        const gchar              *path);

#endif /* LRG_SCRIPTING_PRIVATE */

G_END_DECLS
