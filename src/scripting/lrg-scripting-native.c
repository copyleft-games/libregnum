/* lrg-scripting-native.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Native (shared-object) scripting backend.
 *
 * A context owns an ordered list of loaded "units" (GModules here, Crispy
 * scripts in the Crispy subclass).  Function calls resolve an exported
 * symbol, newest unit first, and invoke it through the LrgNativeFunction
 * ABI.  Host functions and globals live in tables on the context, which the
 * compiled code reaches through the public libregnum API.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include <gmodule.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "lrg-scripting-native.h"
#include "../lrg-enums.h"
#include "../lrg-log.h"

/* A loaded unit together with the way to resolve its symbols */
typedef struct
{
    gpointer               unit;
    LrgNativeSymbolLookup  lookup;
    GDestroyNotify         destroy;
} NativeUnit;

/* A host function registered with lrg_scripting_register_function() */
typedef struct
{
    LrgScriptingCFunction  func;
    gpointer               user_data;
} NativeHostFunction;

typedef struct
{
    GArray     *units;          /* NativeUnit, in load order */
    GHashTable *host_functions; /* name -> NativeHostFunction */
    GHashTable *globals;        /* name -> GValue */
    GPtrArray  *search_paths;   /* NULL-terminated array of gchar* */
} LrgScriptingNativePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgScriptingNative, lrg_scripting_native, LRG_TYPE_SCRIPTING)

/* The optional Crispy-style entry point a unit may export */
typedef int (*NativeMainFunc) (int argc, char **argv);

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Releases one unit through its destroy notify */
static void
native_unit_clear (gpointer data)
{
    NativeUnit *entry = data;

    if (entry->destroy != NULL && entry->unit != NULL)
    {
        entry->destroy (entry->unit);
    }
    entry->unit = NULL;
}

/* Frees a heap GValue stored in the globals table */
static void
native_value_free (gpointer data)
{
    GValue *value = data;

    if (G_IS_VALUE (value))
    {
        g_value_unset (value);
    }
    g_free (value);
}

/*
 * dlsym() on a module handle also searches its dependencies (libc, GLib,
 * libregnum), so "g_free" would resolve and be called through the native
 * ABI.  Only symbols defined by the unit's own file count.
 */
static gboolean
symbol_in_module (GModule  *module,
                  gpointer  symbol)
{
    Dl_info   info;
    gchar    *mine;
    gchar    *owner;
    gboolean  same;

    if (dladdr (symbol, &info) == 0 || info.dli_fname == NULL || g_module_name (module) == NULL)
    {
        return FALSE;
    }
    mine = realpath (g_module_name (module), NULL);
    owner = realpath (info.dli_fname, NULL);
    same = mine != NULL && owner != NULL && strcmp (mine, owner) == 0;
    free (mine);
    free (owner);
    return same;
}

/* Symbol lookup for plain GModule units */
static gpointer
native_module_lookup (gpointer     unit,
                      const gchar *symbol_name)
{
    gpointer symbol;

    symbol = NULL;
    if (!g_module_symbol ((GModule *)unit, symbol_name, &symbol) ||
        !symbol_in_module ((GModule *)unit, symbol))
    {
        return NULL;
    }

    return symbol;
}

/* Closes a GModule unit */
static void
native_module_close (gpointer unit)
{
    g_module_close ((GModule *)unit);
}

/*
 * Resolves @symbol_name across the loaded units.  Later units shadow earlier
 * ones so a reloaded or overriding unit wins.
 */
static gpointer
native_find_symbol (LrgScriptingNative *self,
                    const gchar        *symbol_name)
{
    LrgScriptingNativePrivate *priv;
    guint                      i;

    priv = lrg_scripting_native_get_instance_private (self);

    for (i = priv->units->len; i > 0; i--)
    {
        NativeUnit *entry;
        gpointer    symbol;

        entry = &g_array_index (priv->units, NativeUnit, i - 1);
        symbol = entry->lookup (entry->unit, symbol_name);
        if (symbol != NULL)
        {
            return symbol;
        }
    }

    return NULL;
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

/* Opens a prebuilt shared object and registers it as a unit */
static gboolean
lrg_scripting_native_load_file (LrgScripting  *scripting,
                                const gchar   *path,
                                GError       **error)
{
    LrgScriptingNative *self = LRG_SCRIPTING_NATIVE (scripting);
    GModule            *module;

    g_return_val_if_fail (path != NULL, FALSE);

    if (!g_module_supported ())
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD,
                     "Dynamic modules are not supported on this platform");
        return FALSE;
    }

    /*
     * BIND_LOCAL keeps each module's symbols private so two extensions can
     * export the same entry-point names without clashing.
     */
    module = g_module_open (path, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (module == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD,
                     "Failed to load '%s': %s", path, g_module_error ());
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loaded native module: %s", path);

    return lrg_scripting_native_add_unit (self, module, native_module_lookup,
                                          native_module_close, error);
}

/* Prebuilt objects have no source form */
static gboolean
lrg_scripting_native_load_string (LrgScripting  *scripting,
                                  const gchar   *name,
                                  const gchar   *code,
                                  GError       **error)
{
    (void)scripting;
    (void)code;

    g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD,
                 "Native modules cannot be loaded from source ('%s'); "
                 "use the Crispy backend to compile C source",
                 name != NULL ? name : "(unnamed)");
    return FALSE;
}

/* Calls an exported LrgNativeFunction */
static gboolean
lrg_scripting_native_call_function (LrgScripting  *scripting,
                                    const gchar   *func_name,
                                    GValue        *return_value,
                                    guint          n_args,
                                    const GValue  *args,
                                    GError       **error)
{
    LrgScriptingNative *self = LRG_SCRIPTING_NATIVE (scripting);
    LrgNativeFunction   func;
    GValue              discard = G_VALUE_INIT;
    GError             *local_error = NULL;
    gboolean            ok;

    g_return_val_if_fail (func_name != NULL, FALSE);

    func = (LrgNativeFunction)native_find_symbol (self, func_name);
    if (func == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Function '%s' not found", func_name);
        return FALSE;
    }

    /* The callee always gets somewhere to write; a discarded value is unset */
    ok = func (scripting, n_args, args,
               return_value != NULL ? return_value : &discard, &local_error);
    if (G_IS_VALUE (&discard))
    {
        g_value_unset (&discard);
    }

    if (!ok)
    {
        /* A failing callee may still have set the return value */
        if (return_value != NULL && G_IS_VALUE (return_value))
            g_value_unset (return_value);
        if (local_error == NULL)
        {
            g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                         "Function '%s' failed", func_name);
        }
        else
        {
            g_propagate_error (error, local_error);
        }
        return FALSE;
    }

    /* A success that still set an error is a script bug: report, don't leak */
    if (local_error != NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                     "Function '%s' succeeded but set an error: %s",
                     func_name, local_error->message);
        g_error_free (local_error);
    }

    return TRUE;
}

/* Publishes a host function for lrg_scripting_native_call_host() */
static gboolean
lrg_scripting_native_register_function (LrgScripting           *scripting,
                                        const gchar            *name,
                                        LrgScriptingCFunction   func,
                                        gpointer                user_data,
                                        GError                **error)
{
    LrgScriptingNative        *self = LRG_SCRIPTING_NATIVE (scripting);
    LrgScriptingNativePrivate *priv;
    NativeHostFunction        *host;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (func != NULL, FALSE);

    (void)error;

    priv = lrg_scripting_native_get_instance_private (self);

    host = g_new0 (NativeHostFunction, 1);
    host->func = func;
    host->user_data = user_data;
    g_hash_table_replace (priv->host_functions, g_strdup (name), host);

    return TRUE;
}

/* Copies a global out of the host-side table */
static gboolean
lrg_scripting_native_get_global (LrgScripting  *scripting,
                                 const gchar   *name,
                                 GValue        *value,
                                 GError       **error)
{
    LrgScriptingNative        *self = LRG_SCRIPTING_NATIVE (scripting);
    LrgScriptingNativePrivate *priv;
    GValue                    *stored;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    priv = lrg_scripting_native_get_instance_private (self);

    stored = g_hash_table_lookup (priv->globals, name);
    if (stored == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Global '%s' not found", name);
        return FALSE;
    }

    g_value_init (value, G_VALUE_TYPE (stored));
    g_value_copy (stored, value);
    return TRUE;
}

/* Stores a copy of @value in the host-side table */
static gboolean
lrg_scripting_native_set_global (LrgScripting  *scripting,
                                 const gchar   *name,
                                 const GValue  *value,
                                 GError       **error)
{
    LrgScriptingNative        *self = LRG_SCRIPTING_NATIVE (scripting);
    LrgScriptingNativePrivate *priv;
    GValue                    *copy;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    if (!G_IS_VALUE (value))
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
                     "Global '%s' needs an initialized value", name);
        return FALSE;
    }

    priv = lrg_scripting_native_get_instance_private (self);

    copy = g_new0 (GValue, 1);
    g_value_init (copy, G_VALUE_TYPE (value));
    g_value_copy (value, copy);
    g_hash_table_replace (priv->globals, g_strdup (name), copy);

    return TRUE;
}

/* Unloads every unit and forgets host functions and globals */
static void
lrg_scripting_native_reset (LrgScripting *scripting)
{
    LrgScriptingNative        *self = LRG_SCRIPTING_NATIVE (scripting);
    LrgScriptingNativePrivate *priv;

    priv = lrg_scripting_native_get_instance_private (self);

    g_array_set_size (priv->units, 0);
    g_hash_table_remove_all (priv->host_functions);
    g_hash_table_remove_all (priv->globals);
}

/* Remembers a directory; compiled subclasses turn these into -I flags */
static void
lrg_scripting_native_add_search_path (LrgScripting *scripting,
                                      const gchar  *path)
{
    LrgScriptingNative        *self = LRG_SCRIPTING_NATIVE (scripting);
    LrgScriptingNativePrivate *priv;

    priv = lrg_scripting_native_get_instance_private (self);

    /* Keep the array NULL-terminated for the getter */
    g_ptr_array_remove_index (priv->search_paths, priv->search_paths->len - 1);
    g_ptr_array_add (priv->search_paths, g_strdup (path));
    g_ptr_array_add (priv->search_paths, NULL);
}

/* A function exists when some unit exports the symbol */
static gboolean
lrg_scripting_native_has_function (LrgScripting *scripting,
                                   const gchar  *func_name)
{
    return native_find_symbol (LRG_SCRIPTING_NATIVE (scripting), func_name) != NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scripting_native_finalize (GObject *object)
{
    LrgScriptingNative        *self = LRG_SCRIPTING_NATIVE (object);
    LrgScriptingNativePrivate *priv;

    priv = lrg_scripting_native_get_instance_private (self);

    g_clear_pointer (&priv->units, g_array_unref);
    g_clear_pointer (&priv->host_functions, g_hash_table_unref);
    g_clear_pointer (&priv->globals, g_hash_table_unref);
    g_clear_pointer (&priv->search_paths, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_scripting_native_parent_class)->finalize (object);
}

static void
lrg_scripting_native_class_init (LrgScriptingNativeClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    LrgScriptingClass *scripting_class = LRG_SCRIPTING_CLASS (klass);

    object_class->finalize = lrg_scripting_native_finalize;

    scripting_class->load_file = lrg_scripting_native_load_file;
    scripting_class->load_string = lrg_scripting_native_load_string;
    scripting_class->call_function = lrg_scripting_native_call_function;
    scripting_class->register_function = lrg_scripting_native_register_function;
    scripting_class->get_global = lrg_scripting_native_get_global;
    scripting_class->set_global = lrg_scripting_native_set_global;
    scripting_class->reset = lrg_scripting_native_reset;
    scripting_class->add_search_path = lrg_scripting_native_add_search_path;
    scripting_class->has_function = lrg_scripting_native_has_function;
}

static void
lrg_scripting_native_init (LrgScriptingNative *self)
{
    LrgScriptingNativePrivate *priv;

    priv = lrg_scripting_native_get_instance_private (self);

    priv->units = g_array_new (FALSE, TRUE, sizeof (NativeUnit));
    g_array_set_clear_func (priv->units, native_unit_clear);
    priv->host_functions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, g_free);
    priv->globals = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, native_value_free);
    priv->search_paths = g_ptr_array_new_with_free_func (g_free);
    g_ptr_array_add (priv->search_paths, NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_scripting_native_new:
 *
 * Creates a native scripting context that loads prebuilt shared objects.
 *
 * Returns: (transfer full): a new #LrgScriptingNative
 */
LrgScriptingNative *
lrg_scripting_native_new (void)
{
    return g_object_new (LRG_TYPE_SCRIPTING_NATIVE, NULL);
}

/**
 * lrg_scripting_native_call_host:
 * @scripting: the #LrgScripting context passed to the calling function
 * @name: a function registered with lrg_scripting_register_function()
 * @n_args: number of arguments
 * @args: (array length=n_args) (nullable): the arguments
 * @return_value: (out caller-allocates) (nullable): an uninitialized #GValue
 * @error: (nullable): return location for an error
 *
 * Calls a host function from compiled script code.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_scripting_native_call_host (LrgScripting  *scripting,
                                const gchar   *name,
                                guint          n_args,
                                const GValue  *args,
                                GValue        *return_value,
                                GError       **error)
{
    LrgScriptingNativePrivate *priv;
    NativeHostFunction        *host;
    GValue                     discard = G_VALUE_INIT;
    gboolean                   ok;

    g_return_val_if_fail (LRG_IS_SCRIPTING_NATIVE (scripting), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_scripting_native_get_instance_private (LRG_SCRIPTING_NATIVE (scripting));

    host = g_hash_table_lookup (priv->host_functions, name);
    if (host == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Host function '%s' is not registered", name);
        return FALSE;
    }

    ok = host->func (scripting, n_args, args,
                     return_value != NULL ? return_value : &discard,
                     host->user_data, error);
    if (G_IS_VALUE (&discard))
    {
        g_value_unset (&discard);
    }
    /* On failure the caller gets no value, even if the host set one */
    if (!ok && return_value != NULL && G_IS_VALUE (return_value))
        g_value_unset (return_value);

    return ok;
}

/**
 * lrg_scripting_native_add_unit: (skip)
 * @self: an #LrgScriptingNative
 * @unit: (transfer full): the loaded unit
 * @lookup: resolves exported symbols in @unit
 * @destroy: (nullable): releases @unit on reset or finalize
 * @error: (nullable): return location for an error
 *
 * For subclasses: registers a loaded unit and runs its optional main().
 *
 * Returns: %TRUE when the unit was added and its optional main() succeeded
 */
gboolean
lrg_scripting_native_add_unit (LrgScriptingNative     *self,
                               gpointer                unit,
                               LrgNativeSymbolLookup   lookup,
                               GDestroyNotify          destroy,
                               GError                **error)
{
    LrgScriptingNativePrivate *priv;
    NativeUnit                 entry;
    NativeMainFunc             main_func;
    gint                       status;

    g_return_val_if_fail (LRG_IS_SCRIPTING_NATIVE (self), FALSE);
    g_return_val_if_fail (unit != NULL, FALSE);
    g_return_val_if_fail (lookup != NULL, FALSE);

    priv = lrg_scripting_native_get_instance_private (self);

    entry.unit = unit;
    entry.lookup = lookup;
    entry.destroy = destroy;
    g_array_append_val (priv->units, entry);

    /* Crispy convention: an exported main() runs once, like a script body */
    main_func = (NativeMainFunc)lookup (unit, "main");
    if (main_func == NULL)
    {
        return TRUE;
    }

    status = main_func (0, NULL);
    if (status != 0)
    {
        /* The failed unit must not stay resolvable */
        g_array_set_size (priv->units, priv->units->len - 1);
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                     "Script main() returned %d", status);
        return FALSE;
    }

    return TRUE;
}

/**
 * lrg_scripting_native_get_search_paths:
 * @self: an #LrgScriptingNative
 *
 * Gets the directories added with lrg_scripting_add_search_path().
 *
 * Returns: (transfer none) (array zero-terminated=1): the directories
 */
const gchar * const *
lrg_scripting_native_get_search_paths (LrgScriptingNative *self)
{
    LrgScriptingNativePrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING_NATIVE (self), NULL);

    priv = lrg_scripting_native_get_instance_private (self);
    return (const gchar * const *)priv->search_paths->pdata;
}
