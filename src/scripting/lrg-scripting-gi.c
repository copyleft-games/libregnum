/* lrg-scripting-gi.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GObject Introspection-based scripting backend base class.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-scripting-gi.h"
#include "lrg-scripting-gi-private.h"
#include "../lrg-log.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"

G_DEFINE_TYPE_WITH_PRIVATE (LrgScriptingGI, lrg_scripting_gi, LRG_TYPE_SCRIPTING)

/* ==========================================================================
 * Private Helper Functions
 * ========================================================================== */

/*
 * Free a RegisteredCFunctionGI.
 */
static void
registered_func_free (gpointer data)
{
    RegisteredCFunctionGI *reg = (RegisteredCFunctionGI *)data;

    g_free (reg->name);
    g_free (reg);
}

/*
 * Rebuild the null-terminated search paths array.
 */
static void
rebuild_search_paths_null_term (LrgScriptingGIPrivate *priv)
{
    guint i;

    g_clear_pointer (&priv->search_paths_null_term, g_strfreev);

    if (priv->search_paths->len == 0)
    {
        priv->search_paths_null_term = g_new0 (gchar *, 1);
        return;
    }

    priv->search_paths_null_term = g_new0 (gchar *, priv->search_paths->len + 1);

    for (i = 0; i < priv->search_paths->len; i++)
    {
        priv->search_paths_null_term[i] = g_strdup (g_ptr_array_index (priv->search_paths, i));
    }
}

/* ==========================================================================
 * Private Data Access (for subclasses)
 * ========================================================================== */

/**
 * lrg_scripting_gi_get_private:
 * @self: an #LrgScriptingGI
 *
 * Gets the private data structure.
 *
 * Returns: (transfer none): the private data
 */
LrgScriptingGIPrivate *
lrg_scripting_gi_get_private (LrgScriptingGI *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), NULL);

    return lrg_scripting_gi_get_instance_private (self);
}

/**
 * lrg_scripting_gi_get_gi_repository:
 * @self: an #LrgScriptingGI
 *
 * Gets the GIRepository used by this scripting context.
 *
 * Returns: (transfer none): the GIRepository
 */
GIRepository *
lrg_scripting_gi_get_gi_repository (LrgScriptingGI *self)
{
    LrgScriptingGIPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), NULL);

    priv = lrg_scripting_gi_get_instance_private (self);

    return priv->gi_repository;
}

/**
 * lrg_scripting_gi_is_interpreter_initialized:
 * @self: an #LrgScriptingGI
 *
 * Checks if the interpreter has been initialized.
 *
 * Returns: %TRUE if initialized
 */
gboolean
lrg_scripting_gi_is_interpreter_initialized (LrgScriptingGI *self)
{
    LrgScriptingGIPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), FALSE);

    priv = lrg_scripting_gi_get_instance_private (self);

    return priv->interpreter_initialized;
}

/**
 * lrg_scripting_gi_set_interpreter_initialized:
 * @self: an #LrgScriptingGI
 * @initialized: the new state
 *
 * Sets the interpreter initialized state.
 */
void
lrg_scripting_gi_set_interpreter_initialized (LrgScriptingGI *self,
                                              gboolean        initialized)
{
    LrgScriptingGIPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPTING_GI (self));

    priv = lrg_scripting_gi_get_instance_private (self);
    priv->interpreter_initialized = initialized;
}

/**
 * lrg_scripting_gi_add_registered_function:
 * @self: an #LrgScriptingGI
 * @name: the function name
 * @func: the C function
 * @user_data: user data for the function
 *
 * Adds a registered C function to the tracking table.
 *
 * Returns: (transfer none): the registration data
 */
RegisteredCFunctionGI *
lrg_scripting_gi_add_registered_function (LrgScriptingGI        *self,
                                          const gchar           *name,
                                          LrgScriptingCFunction  func,
                                          gpointer               user_data)
{
    LrgScriptingGIPrivate *priv;
    RegisteredCFunctionGI *reg;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (func != NULL, NULL);

    priv = lrg_scripting_gi_get_instance_private (self);

    reg = g_new0 (RegisteredCFunctionGI, 1);
    reg->scripting = self;  /* Weak reference */
    reg->func = func;
    reg->user_data = user_data;
    reg->name = g_strdup (name);

    g_hash_table_insert (priv->registered_funcs, g_strdup (name), reg);

    return reg;
}

/**
 * lrg_scripting_gi_get_registered_function:
 * @self: an #LrgScriptingGI
 * @name: the function name
 *
 * Gets a registered C function by name.
 *
 * Returns: (transfer none) (nullable): the registration data, or %NULL
 */
RegisteredCFunctionGI *
lrg_scripting_gi_get_registered_function (LrgScriptingGI *self,
                                          const gchar    *name)
{
    LrgScriptingGIPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_scripting_gi_get_instance_private (self);

    return g_hash_table_lookup (priv->registered_funcs, name);
}

/**
 * lrg_scripting_gi_clear_registered_functions:
 * @self: an #LrgScriptingGI
 *
 * Clears all registered C functions.
 */
void
lrg_scripting_gi_clear_registered_functions (LrgScriptingGI *self)
{
    LrgScriptingGIPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPTING_GI (self));

    priv = lrg_scripting_gi_get_instance_private (self);

    g_hash_table_remove_all (priv->registered_funcs);
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_scripting_gi_real_init_interpreter (LrgScriptingGI  *self,
                                        GError         **error)
{
    /* Default implementation does nothing - subclasses must override */
    (void)self;
    (void)error;

    return TRUE;
}

static void
lrg_scripting_gi_real_finalize_interpreter (LrgScriptingGI *self)
{
    /* Default implementation does nothing */
    (void)self;
}

static gboolean
lrg_scripting_gi_real_expose_typelib (LrgScriptingGI  *self,
                                      const gchar     *namespace_,
                                      const gchar     *version,
                                      GError         **error)
{
    /* Default implementation does nothing - subclasses must override */
    (void)self;
    (void)namespace_;
    (void)version;
    (void)error;

    return TRUE;
}

static gboolean
lrg_scripting_gi_real_expose_gobject (LrgScriptingGI  *self,
                                      const gchar     *name,
                                      GObject         *object,
                                      GError         **error)
{
    /* Default implementation does nothing - subclasses must override */
    (void)self;
    (void)name;
    (void)object;
    (void)error;

    return TRUE;
}

static gboolean
lrg_scripting_gi_real_call_update_hook (LrgScriptingGI  *self,
                                        const gchar     *func_name,
                                        gfloat           delta,
                                        GError         **error)
{
    GValue delta_value = G_VALUE_INIT;
    gboolean result;

    /*
     * Default implementation uses call_function from the base class.
     * Subclasses can override for more efficient implementations.
     */
    g_value_init (&delta_value, G_TYPE_FLOAT);
    g_value_set_float (&delta_value, delta);

    result = lrg_scripting_call_function (LRG_SCRIPTING (self),
                                          func_name,
                                          NULL,
                                          1,
                                          &delta_value,
                                          error);

    g_value_unset (&delta_value);

    return result;
}

static void
lrg_scripting_gi_real_update_search_paths (LrgScriptingGI *self)
{
    /* Default implementation does nothing - subclasses should override */
    (void)self;
}

static const gchar *
lrg_scripting_gi_real_get_interpreter_name (LrgScriptingGI *self)
{
    (void)self;

    return "GI";
}

/* ==========================================================================
 * Base Class Virtual Method Overrides
 * ========================================================================== */

static void
lrg_scripting_gi_real_reset (LrgScripting *scripting)
{
    LrgScriptingGI *self = LRG_SCRIPTING_GI (scripting);
    LrgScriptingGIPrivate *priv = lrg_scripting_gi_get_instance_private (self);
    LrgScriptingGIClass *klass = LRG_SCRIPTING_GI_GET_CLASS (self);

    /* Clear update hooks */
    g_ptr_array_set_size (priv->update_hooks, 0);

    /* Clear registered functions */
    g_hash_table_remove_all (priv->registered_funcs);

    /* Clear loaded typelibs tracking */
    g_hash_table_remove_all (priv->loaded_typelibs);

    /* Let subclass finalize interpreter */
    if (klass->finalize_interpreter != NULL)
    {
        klass->finalize_interpreter (self);
    }

    priv->interpreter_initialized = FALSE;

    /* Re-initialize interpreter */
    if (klass->init_interpreter != NULL)
    {
        g_autoptr(GError) error = NULL;

        if (!klass->init_interpreter (self, &error))
        {
            const gchar *name = "Unknown";

            if (klass->get_interpreter_name != NULL)
            {
                name = klass->get_interpreter_name (self);
            }

            lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                         "%s interpreter reset failed: %s",
                         name,
                         error ? error->message : "(unknown error)");
        }
        else
        {
            priv->interpreter_initialized = TRUE;
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "GI script context reset");
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scripting_gi_finalize (GObject *object)
{
    LrgScriptingGI *self = LRG_SCRIPTING_GI (object);
    LrgScriptingGIPrivate *priv = lrg_scripting_gi_get_instance_private (self);
    LrgScriptingGIClass *klass = LRG_SCRIPTING_GI_GET_CLASS (self);

    /* Let subclass finalize interpreter */
    if (klass->finalize_interpreter != NULL)
    {
        klass->finalize_interpreter (self);
    }

    g_clear_pointer (&priv->update_hooks, g_ptr_array_unref);
    g_clear_pointer (&priv->search_paths, g_ptr_array_unref);
    g_clear_pointer (&priv->search_paths_null_term, g_strfreev);
    g_clear_pointer (&priv->registered_funcs, g_hash_table_unref);
    g_clear_pointer (&priv->loaded_typelibs, g_hash_table_unref);
    g_clear_object (&priv->gi_repository);

    G_OBJECT_CLASS (lrg_scripting_gi_parent_class)->finalize (object);
}

static void
lrg_scripting_gi_constructed (GObject *object)
{
    LrgScriptingGI *self = LRG_SCRIPTING_GI (object);
    LrgScriptingGIPrivate *priv = lrg_scripting_gi_get_instance_private (self);
    LrgScriptingGIClass *klass = LRG_SCRIPTING_GI_GET_CLASS (self);

    G_OBJECT_CLASS (lrg_scripting_gi_parent_class)->constructed (object);

    /* Initialize interpreter */
    if (klass->init_interpreter != NULL)
    {
        g_autoptr(GError) error = NULL;

        if (!klass->init_interpreter (self, &error))
        {
            const gchar *name = "Unknown";

            if (klass->get_interpreter_name != NULL)
            {
                name = klass->get_interpreter_name (self);
            }

            lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                         "%s interpreter initialization failed: %s",
                         name,
                         error ? error->message : "(unknown error)");
        }
        else
        {
            priv->interpreter_initialized = TRUE;
        }
    }
}

static void
lrg_scripting_gi_class_init (LrgScriptingGIClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgScriptingClass *scripting_class = LRG_SCRIPTING_CLASS (klass);

    object_class->finalize = lrg_scripting_gi_finalize;
    object_class->constructed = lrg_scripting_gi_constructed;

    /* Override base class reset */
    scripting_class->reset = lrg_scripting_gi_real_reset;

    /* Set default virtual method implementations */
    klass->init_interpreter = lrg_scripting_gi_real_init_interpreter;
    klass->finalize_interpreter = lrg_scripting_gi_real_finalize_interpreter;
    klass->expose_typelib = lrg_scripting_gi_real_expose_typelib;
    klass->expose_gobject = lrg_scripting_gi_real_expose_gobject;
    klass->call_update_hook = lrg_scripting_gi_real_call_update_hook;
    klass->update_search_paths = lrg_scripting_gi_real_update_search_paths;
    klass->get_interpreter_name = lrg_scripting_gi_real_get_interpreter_name;
}

static void
lrg_scripting_gi_init (LrgScriptingGI *self)
{
    LrgScriptingGIPrivate *priv = lrg_scripting_gi_get_instance_private (self);

    priv->registry = NULL;
    priv->engine = NULL;
    priv->update_hooks = g_ptr_array_new_with_free_func (g_free);
    priv->search_paths = g_ptr_array_new_with_free_func (g_free);
    priv->search_paths_null_term = g_new0 (gchar *, 1);
    priv->registered_funcs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                     g_free, registered_func_free);
    priv->loaded_typelibs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free, g_free);
    priv->gi_repository = g_irepository_get_default ();
    g_object_ref (priv->gi_repository);
    priv->interpreter_initialized = FALSE;
}

/* ==========================================================================
 * Public API: Registry Integration
 * ========================================================================== */

/**
 * lrg_scripting_gi_set_registry:
 * @self: an #LrgScriptingGI
 * @registry: (nullable): the #LrgRegistry for type lookups
 *
 * Sets the registry used to expose types to scripts.
 */
void
lrg_scripting_gi_set_registry (LrgScriptingGI *self,
                               LrgRegistry    *registry)
{
    LrgScriptingGIPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPTING_GI (self));
    g_return_if_fail (registry == NULL || LRG_IS_REGISTRY (registry));

    priv = lrg_scripting_gi_get_instance_private (self);
    priv->registry = registry;  /* Weak reference */
}

/**
 * lrg_scripting_gi_get_registry:
 * @self: an #LrgScriptingGI
 *
 * Gets the registry used for type lookups.
 *
 * Returns: (transfer none) (nullable): the #LrgRegistry, or %NULL if not set
 */
LrgRegistry *
lrg_scripting_gi_get_registry (LrgScriptingGI *self)
{
    LrgScriptingGIPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), NULL);

    priv = lrg_scripting_gi_get_instance_private (self);

    return priv->registry;
}

/* ==========================================================================
 * Public API: Engine Integration
 * ========================================================================== */

/**
 * lrg_scripting_gi_set_engine:
 * @self: an #LrgScriptingGI
 * @engine: (nullable): the #LrgEngine to expose to scripts
 *
 * Sets the engine instance to expose to scripts.
 */
void
lrg_scripting_gi_set_engine (LrgScriptingGI *self,
                             LrgEngine      *engine)
{
    LrgScriptingGIPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPTING_GI (self));
    g_return_if_fail (engine == NULL || LRG_IS_ENGINE (engine));

    priv = lrg_scripting_gi_get_instance_private (self);
    priv->engine = engine;  /* Weak reference */
}

/**
 * lrg_scripting_gi_get_engine:
 * @self: an #LrgScriptingGI
 *
 * Gets the engine instance exposed to scripts.
 *
 * Returns: (transfer none) (nullable): the #LrgEngine, or %NULL if not set
 */
LrgEngine *
lrg_scripting_gi_get_engine (LrgScriptingGI *self)
{
    LrgScriptingGIPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), NULL);

    priv = lrg_scripting_gi_get_instance_private (self);

    return priv->engine;
}

/* ==========================================================================
 * Public API: Search Paths
 * ========================================================================== */

/**
 * lrg_scripting_gi_add_search_path:
 * @self: an #LrgScriptingGI
 * @path: (type filename): directory path to add
 *
 * Adds a directory to the script search path.
 */
void
lrg_scripting_gi_add_search_path (LrgScriptingGI *self,
                                  const gchar    *path)
{
    LrgScriptingGIPrivate *priv;
    LrgScriptingGIClass *klass;

    g_return_if_fail (LRG_IS_SCRIPTING_GI (self));
    g_return_if_fail (path != NULL);

    priv = lrg_scripting_gi_get_instance_private (self);
    klass = LRG_SCRIPTING_GI_GET_CLASS (self);

    g_ptr_array_add (priv->search_paths, g_strdup (path));
    rebuild_search_paths_null_term (priv);

    /* Notify subclass */
    if (klass->update_search_paths != NULL)
    {
        klass->update_search_paths (self);
    }
}

/**
 * lrg_scripting_gi_clear_search_paths:
 * @self: an #LrgScriptingGI
 *
 * Clears all custom search paths.
 */
void
lrg_scripting_gi_clear_search_paths (LrgScriptingGI *self)
{
    LrgScriptingGIPrivate *priv;
    LrgScriptingGIClass *klass;

    g_return_if_fail (LRG_IS_SCRIPTING_GI (self));

    priv = lrg_scripting_gi_get_instance_private (self);
    klass = LRG_SCRIPTING_GI_GET_CLASS (self);

    g_ptr_array_set_size (priv->search_paths, 0);
    rebuild_search_paths_null_term (priv);

    /* Notify subclass */
    if (klass->update_search_paths != NULL)
    {
        klass->update_search_paths (self);
    }
}

/**
 * lrg_scripting_gi_get_search_paths:
 * @self: an #LrgScriptingGI
 *
 * Gets the list of custom search paths.
 *
 * Returns: (transfer none) (array zero-terminated=1): the search paths
 */
const gchar * const *
lrg_scripting_gi_get_search_paths (LrgScriptingGI *self)
{
    LrgScriptingGIPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), NULL);

    priv = lrg_scripting_gi_get_instance_private (self);

    return (const gchar * const *)priv->search_paths_null_term;
}

/* ==========================================================================
 * Public API: Update Hooks
 * ========================================================================== */

/**
 * lrg_scripting_gi_register_update_hook:
 * @self: an #LrgScriptingGI
 * @func_name: name of the script function to call on update
 *
 * Registers a script function to be called each frame.
 */
void
lrg_scripting_gi_register_update_hook (LrgScriptingGI *self,
                                       const gchar    *func_name)
{
    LrgScriptingGIPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPTING_GI (self));
    g_return_if_fail (func_name != NULL);

    priv = lrg_scripting_gi_get_instance_private (self);

    g_ptr_array_add (priv->update_hooks, g_strdup (func_name));

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Registered update hook: %s", func_name);
}

/**
 * lrg_scripting_gi_unregister_update_hook:
 * @self: an #LrgScriptingGI
 * @func_name: name of the script function to unregister
 *
 * Unregisters a previously registered update hook.
 *
 * Returns: %TRUE if the hook was found and removed
 */
gboolean
lrg_scripting_gi_unregister_update_hook (LrgScriptingGI *self,
                                         const gchar    *func_name)
{
    LrgScriptingGIPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), FALSE);
    g_return_val_if_fail (func_name != NULL, FALSE);

    priv = lrg_scripting_gi_get_instance_private (self);

    for (i = 0; i < priv->update_hooks->len; i++)
    {
        const gchar *name = g_ptr_array_index (priv->update_hooks, i);

        if (g_strcmp0 (name, func_name) == 0)
        {
            g_ptr_array_remove_index (priv->update_hooks, i);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_scripting_gi_clear_update_hooks:
 * @self: an #LrgScriptingGI
 *
 * Clears all registered update hooks.
 */
void
lrg_scripting_gi_clear_update_hooks (LrgScriptingGI *self)
{
    LrgScriptingGIPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPTING_GI (self));

    priv = lrg_scripting_gi_get_instance_private (self);

    g_ptr_array_set_size (priv->update_hooks, 0);
}

/**
 * lrg_scripting_gi_update:
 * @self: an #LrgScriptingGI
 * @delta: time since last frame in seconds
 *
 * Calls all registered update hooks with the given delta time.
 */
void
lrg_scripting_gi_update (LrgScriptingGI *self,
                         gfloat          delta)
{
    LrgScriptingGIPrivate *priv;
    LrgScriptingGIClass *klass;
    guint i;

    g_return_if_fail (LRG_IS_SCRIPTING_GI (self));

    priv = lrg_scripting_gi_get_instance_private (self);
    klass = LRG_SCRIPTING_GI_GET_CLASS (self);

    if (!priv->interpreter_initialized)
    {
        return;
    }

    for (i = 0; i < priv->update_hooks->len; i++)
    {
        const gchar *func_name = g_ptr_array_index (priv->update_hooks, i);
        g_autoptr(GError) error = NULL;

        if (klass->call_update_hook != NULL)
        {
            if (!klass->call_update_hook (self, func_name, delta, &error))
            {
                lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                             "Update hook '%s' error: %s",
                             func_name,
                             error ? error->message : "(unknown)");
            }
        }
    }
}

/* ==========================================================================
 * Public API: GI-Specific Typelib Loading
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
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_scripting_gi_require_typelib (LrgScriptingGI  *self,
                                  const gchar     *namespace_,
                                  const gchar     *version,
                                  GError         **error)
{
    LrgScriptingGIPrivate *priv;
    LrgScriptingGIClass *klass;
    GITypelib *typelib;
    gchar *key;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), FALSE);
    g_return_val_if_fail (namespace_ != NULL, FALSE);
    g_return_val_if_fail (version != NULL, FALSE);

    priv = lrg_scripting_gi_get_instance_private (self);
    klass = LRG_SCRIPTING_GI_GET_CLASS (self);

    /* Check if already loaded */
    key = g_strdup_printf ("%s-%s", namespace_, version);
    if (g_hash_table_contains (priv->loaded_typelibs, key))
    {
        g_free (key);
        return TRUE;
    }

    /* Load the typelib via GIRepository */
    typelib = g_irepository_require (priv->gi_repository,
                                     namespace_,
                                     version,
                                     0,
                                     error);
    if (typelib == NULL)
    {
        g_free (key);
        return FALSE;
    }

    /* Track it */
    g_hash_table_insert (priv->loaded_typelibs, key, g_strdup (version));

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
               "Loaded typelib: %s-%s",
               namespace_, version);

    /* Let subclass expose it to the interpreter */
    if (klass->expose_typelib != NULL)
    {
        return klass->expose_typelib (self, namespace_, version, error);
    }

    return TRUE;
}

/**
 * lrg_scripting_gi_require_libregnum:
 * @self: an #LrgScriptingGI
 * @error: (nullable): return location for error
 *
 * Loads the Libregnum typelib and exposes it to the interpreter.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_scripting_gi_require_libregnum (LrgScriptingGI  *self,
                                    GError         **error)
{
    return lrg_scripting_gi_require_typelib (self, "Libregnum", "1", error);
}

/* ==========================================================================
 * Public API: GObject Exposure
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
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_scripting_gi_expose_object (LrgScriptingGI  *self,
                                const gchar     *name,
                                GObject         *object,
                                GError         **error)
{
    LrgScriptingGIClass *klass;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (G_IS_OBJECT (object), FALSE);

    klass = LRG_SCRIPTING_GI_GET_CLASS (self);

    if (klass->expose_gobject != NULL)
    {
        return klass->expose_gobject (self, name, object, error);
    }

    return TRUE;
}

/* ==========================================================================
 * Public API: Registered Functions Tracking
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
gboolean
lrg_scripting_gi_has_registered_function (LrgScriptingGI *self,
                                          const gchar    *name)
{
    LrgScriptingGIPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING_GI (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_scripting_gi_get_instance_private (self);

    return g_hash_table_contains (priv->registered_funcs, name);
}
