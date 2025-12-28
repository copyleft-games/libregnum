/* lrg-scripting-gi-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private types and functions for GI-based scripting backends.
 *
 * This header provides access to the private instance data for
 * subclasses of LrgScriptingGI.
 */

#pragma once

#include <glib-object.h>
#include <girepository.h>
#include "lrg-scripting-gi.h"

G_BEGIN_DECLS

/**
 * RegisteredCFunctionGI:
 * @scripting: (weak): the scripting context
 * @func: the C function to call
 * @user_data: user data for the function
 * @name: the registered name
 *
 * Internal structure for tracking registered C functions.
 */
typedef struct
{
    LrgScriptingGI        *scripting;   /* Weak reference */
    LrgScriptingCFunction  func;        /* C function pointer */
    gpointer               user_data;   /* User data */
    gchar                 *name;        /* Function name */
} RegisteredCFunctionGI;

/**
 * LrgScriptingGIPrivate:
 * @registry: Type registry (weak reference)
 * @engine: Engine instance (weak reference)
 * @update_hooks: Array of function names to call on update
 * @search_paths: Array of custom search paths
 * @search_paths_null_term: Null-terminated version for get_search_paths()
 * @registered_funcs: Hash table of name -> RegisteredCFunctionGI
 * @loaded_typelibs: Hash table of namespace -> version (tracks loaded typelibs)
 * @gi_repository: The GIRepository instance
 * @interpreter_initialized: Whether the interpreter has been initialized
 *
 * Private instance data for LrgScriptingGI.
 *
 * Subclasses can access this via lrg_scripting_gi_get_private().
 */
typedef struct _LrgScriptingGIPrivate
{
    LrgRegistry  *registry;
    LrgEngine    *engine;
    GPtrArray    *update_hooks;
    GPtrArray    *search_paths;
    gchar       **search_paths_null_term;
    GHashTable   *registered_funcs;
    GHashTable   *loaded_typelibs;
    GIRepository *gi_repository;
    gboolean      interpreter_initialized;
} LrgScriptingGIPrivate;

/**
 * lrg_scripting_gi_get_private:
 * @self: an #LrgScriptingGI
 *
 * Gets the private data structure.
 *
 * This allows subclasses to access the common infrastructure data
 * (registry, engine, hooks, paths, etc.).
 *
 * Returns: (transfer none): the private data
 */
LrgScriptingGIPrivate * lrg_scripting_gi_get_private (LrgScriptingGI *self);

/**
 * lrg_scripting_gi_get_gi_repository:
 * @self: an #LrgScriptingGI
 *
 * Gets the GIRepository used by this scripting context.
 *
 * Returns: (transfer none): the GIRepository
 */
GIRepository * lrg_scripting_gi_get_gi_repository (LrgScriptingGI *self);

/**
 * lrg_scripting_gi_is_interpreter_initialized:
 * @self: an #LrgScriptingGI
 *
 * Checks if the interpreter has been initialized.
 *
 * Returns: %TRUE if initialized
 */
gboolean lrg_scripting_gi_is_interpreter_initialized (LrgScriptingGI *self);

/**
 * lrg_scripting_gi_set_interpreter_initialized:
 * @self: an #LrgScriptingGI
 * @initialized: the new state
 *
 * Sets the interpreter initialized state.
 *
 * This should be called by subclasses after successful interpreter
 * initialization.
 */
void lrg_scripting_gi_set_interpreter_initialized (LrgScriptingGI *self,
                                                   gboolean        initialized);

/**
 * lrg_scripting_gi_add_registered_function:
 * @self: an #LrgScriptingGI
 * @name: the function name
 * @func: the C function
 * @user_data: user data for the function
 *
 * Adds a registered C function to the tracking table.
 *
 * This is called by subclasses when implementing register_function.
 *
 * Returns: (transfer none): the registration data (owned by the hash table)
 */
RegisteredCFunctionGI * lrg_scripting_gi_add_registered_function (LrgScriptingGI        *self,
                                                                  const gchar           *name,
                                                                  LrgScriptingCFunction  func,
                                                                  gpointer               user_data);

/**
 * lrg_scripting_gi_get_registered_function:
 * @self: an #LrgScriptingGI
 * @name: the function name
 *
 * Gets a registered C function by name.
 *
 * Returns: (transfer none) (nullable): the registration data, or %NULL
 */
RegisteredCFunctionGI * lrg_scripting_gi_get_registered_function (LrgScriptingGI *self,
                                                                  const gchar    *name);

/**
 * lrg_scripting_gi_clear_registered_functions:
 * @self: an #LrgScriptingGI
 *
 * Clears all registered C functions.
 *
 * This is called during reset.
 */
void lrg_scripting_gi_clear_registered_functions (LrgScriptingGI *self);

G_END_DECLS
