/* lrg-scripting.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for scripting engine implementations.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-scripting.h"
#include "../lrg-log.h"

G_DEFINE_ABSTRACT_TYPE (LrgScripting, lrg_scripting, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scripting_class_init (LrgScriptingClass *klass)
{
    /*
     * Abstract class - all virtual methods must be implemented
     * by subclasses. We don't provide default implementations.
     */
    klass->load_file = NULL;
    klass->load_string = NULL;
    klass->call_function = NULL;
    klass->register_function = NULL;
    klass->get_global = NULL;
    klass->set_global = NULL;
    klass->reset = NULL;
}

static void
lrg_scripting_init (LrgScripting *self)
{
    /* Nothing to initialize in the abstract base class */
}

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
gboolean
lrg_scripting_load_file (LrgScripting  *self,
                         const gchar   *path,
                         GError       **error)
{
    LrgScriptingClass *klass;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    klass = LRG_SCRIPTING_GET_CLASS (self);

    if (klass->load_file == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Scripting backend does not implement load_file");
        return FALSE;
    }

    return klass->load_file (self, path, error);
}

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
gboolean
lrg_scripting_load_string (LrgScripting  *self,
                           const gchar   *name,
                           const gchar   *code,
                           GError       **error)
{
    LrgScriptingClass *klass;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    klass = LRG_SCRIPTING_GET_CLASS (self);

    if (klass->load_string == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Scripting backend does not implement load_string");
        return FALSE;
    }

    return klass->load_string (self, name, code, error);
}

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
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_scripting_call_function (LrgScripting  *self,
                             const gchar   *func_name,
                             GValue        *return_value,
                             guint          n_args,
                             const GValue  *args,
                             GError       **error)
{
    LrgScriptingClass *klass;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);
    g_return_val_if_fail (func_name != NULL, FALSE);

    klass = LRG_SCRIPTING_GET_CLASS (self);

    if (klass->call_function == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Scripting backend does not implement call_function");
        return FALSE;
    }

    return klass->call_function (self, func_name, return_value, n_args, args, error);
}

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
gboolean
lrg_scripting_register_function (LrgScripting           *self,
                                 const gchar            *name,
                                 LrgScriptingCFunction   func,
                                 gpointer                user_data,
                                 GError                **error)
{
    LrgScriptingClass *klass;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (func != NULL, FALSE);

    klass = LRG_SCRIPTING_GET_CLASS (self);

    if (klass->register_function == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Scripting backend does not implement register_function");
        return FALSE;
    }

    return klass->register_function (self, name, func, user_data, error);
}

/**
 * lrg_scripting_get_global:
 * @self: an #LrgScripting
 * @name: name of the global variable
 * @value: (out caller-allocates): location to store the value
 * @error: (nullable): return location for error
 *
 * Get a global variable from the script context.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_scripting_get_global (LrgScripting  *self,
                          const gchar   *name,
                          GValue        *value,
                          GError       **error)
{
    LrgScriptingClass *klass;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    klass = LRG_SCRIPTING_GET_CLASS (self);

    if (klass->get_global == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Scripting backend does not implement get_global");
        return FALSE;
    }

    return klass->get_global (self, name, value, error);
}

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
gboolean
lrg_scripting_set_global (LrgScripting  *self,
                          const gchar   *name,
                          const GValue  *value,
                          GError       **error)
{
    LrgScriptingClass *klass;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    klass = LRG_SCRIPTING_GET_CLASS (self);

    if (klass->set_global == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Scripting backend does not implement set_global");
        return FALSE;
    }

    return klass->set_global (self, name, value, error);
}

/**
 * lrg_scripting_reset:
 * @self: an #LrgScripting
 *
 * Reset the script context to a clean state.
 */
void
lrg_scripting_reset (LrgScripting *self)
{
    LrgScriptingClass *klass;

    g_return_if_fail (LRG_IS_SCRIPTING (self));

    klass = LRG_SCRIPTING_GET_CLASS (self);

    if (klass->reset != NULL)
    {
        klass->reset (self);
    }
}
