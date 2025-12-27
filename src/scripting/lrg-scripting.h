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

    /*< private >*/
    gpointer _reserved[8];
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
 * @func: (scope notified): the C function to call
 * @user_data: (closure func): user data to pass to the function
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

G_END_DECLS
