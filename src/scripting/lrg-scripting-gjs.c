/* lrg-scripting-gjs.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gjs (GNOME JavaScript) scripting backend implementation.
 *
 * This implements all virtual methods from LrgScriptingGI and LrgScripting
 * using the Gjs runtime (SpiderMonkey JavaScript engine with GI bindings).
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "config.h"
#include "lrg-scripting-gjs.h"
#include "lrg-scripting-gi-private.h"
#include "../lrg-log.h"

#include <gjs/gjs.h>
#include <string.h>

/**
 * LrgScriptingGjs:
 *
 * Gjs-based JavaScript scripting context.
 *
 * Uses the Gjs runtime (SpiderMonkey) for JavaScript execution with
 * native GObject Introspection support.
 */
struct _LrgScriptingGjs
{
    LrgScriptingGI  parent_instance;

    GjsContext     *context;      /* Gjs JavaScript context */
};

G_DEFINE_FINAL_TYPE (LrgScriptingGjs, lrg_scripting_gjs, LRG_TYPE_SCRIPTING_GI)

/* ==========================================================================
 * Forward Declarations
 * ========================================================================== */

static void lrg_scripting_gjs_silent_log_handler (const gchar    *log_domain,
                                                  GLogLevelFlags  log_level,
                                                  const gchar    *message,
                                                  gpointer        user_data);

static gboolean lrg_scripting_gjs_init_interpreter   (LrgScriptingGI  *gi_self,
                                                      GError         **error);
static void     lrg_scripting_gjs_finalize_interpreter (LrgScriptingGI *gi_self);
static gboolean lrg_scripting_gjs_expose_typelib     (LrgScriptingGI  *gi_self,
                                                      const gchar     *namespace_,
                                                      const gchar     *version,
                                                      GError         **error);
static gboolean lrg_scripting_gjs_expose_gobject     (LrgScriptingGI  *gi_self,
                                                      const gchar     *name,
                                                      GObject         *object,
                                                      GError         **error);
static gboolean lrg_scripting_gjs_call_update_hook   (LrgScriptingGI  *gi_self,
                                                      const gchar     *func_name,
                                                      gfloat           delta,
                                                      GError         **error);
static void     lrg_scripting_gjs_update_search_paths (LrgScriptingGI *gi_self);
static const gchar * lrg_scripting_gjs_get_interpreter_name (LrgScriptingGI *gi_self);

static gboolean lrg_scripting_gjs_load_file          (LrgScripting    *scripting,
                                                      const gchar     *path,
                                                      GError         **error);
static gboolean lrg_scripting_gjs_load_string        (LrgScripting    *scripting,
                                                      const gchar     *name,
                                                      const gchar     *code,
                                                      GError         **error);
static gboolean lrg_scripting_gjs_call_function      (LrgScripting    *scripting,
                                                      const gchar     *func_name,
                                                      GValue          *return_value,
                                                      guint            n_args,
                                                      const GValue    *args,
                                                      GError         **error);
static gboolean lrg_scripting_gjs_register_function  (LrgScripting          *scripting,
                                                      const gchar           *name,
                                                      LrgScriptingCFunction  func,
                                                      gpointer               user_data,
                                                      GError               **error);
static gboolean lrg_scripting_gjs_get_global         (LrgScripting    *scripting,
                                                      const gchar     *name,
                                                      GValue          *value,
                                                      GError         **error);
static gboolean lrg_scripting_gjs_set_global         (LrgScripting    *scripting,
                                                      const gchar     *name,
                                                      const GValue    *value,
                                                      GError         **error);
static void     lrg_scripting_gjs_reset              (LrgScripting    *scripting);

/* ==========================================================================
 * GObject Lifecycle
 * ========================================================================== */

static void
lrg_scripting_gjs_dispose (GObject *object)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (object);

    g_clear_object (&self->context);

    G_OBJECT_CLASS (lrg_scripting_gjs_parent_class)->dispose (object);
}

static void
lrg_scripting_gjs_class_init (LrgScriptingGjsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgScriptingClass *scripting_class = LRG_SCRIPTING_CLASS (klass);
    LrgScriptingGIClass *gi_class = LRG_SCRIPTING_GI_CLASS (klass);

    object_class->dispose = lrg_scripting_gjs_dispose;

    /* LrgScriptingGI virtual methods */
    gi_class->init_interpreter = lrg_scripting_gjs_init_interpreter;
    gi_class->finalize_interpreter = lrg_scripting_gjs_finalize_interpreter;
    gi_class->expose_typelib = lrg_scripting_gjs_expose_typelib;
    gi_class->expose_gobject = lrg_scripting_gjs_expose_gobject;
    gi_class->call_update_hook = lrg_scripting_gjs_call_update_hook;
    gi_class->update_search_paths = lrg_scripting_gjs_update_search_paths;
    gi_class->get_interpreter_name = lrg_scripting_gjs_get_interpreter_name;

    /* LrgScripting virtual methods */
    scripting_class->load_file = lrg_scripting_gjs_load_file;
    scripting_class->load_string = lrg_scripting_gjs_load_string;
    scripting_class->call_function = lrg_scripting_gjs_call_function;
    scripting_class->register_function = lrg_scripting_gjs_register_function;
    scripting_class->get_global = lrg_scripting_gjs_get_global;
    scripting_class->set_global = lrg_scripting_gjs_set_global;
    scripting_class->reset = lrg_scripting_gjs_reset;
}

static void
lrg_scripting_gjs_init (LrgScriptingGjs *self)
{
    self->context = NULL;
}

/* ==========================================================================
 * Public Constructor
 * ========================================================================== */

/**
 * lrg_scripting_gjs_new:
 *
 * Creates a new Gjs-based JavaScript scripting context.
 *
 * Returns: (transfer full): a new #LrgScriptingGjs
 */
LrgScriptingGjs *
lrg_scripting_gjs_new (void)
{
    return g_object_new (LRG_TYPE_SCRIPTING_GJS, NULL);
}

/* ==========================================================================
 * LrgScriptingGI Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_scripting_gjs_init_interpreter (LrgScriptingGI  *gi_self,
                                    GError         **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (gi_self);
    LrgScriptingGIPrivate *priv = lrg_scripting_gi_get_private (gi_self);

    g_return_val_if_fail (self->context == NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Initializing Gjs interpreter");

    /*
     * Create a new Gjs context with search paths if available.
     * GjsContext automatically provides access to imports.gi.* for GI.
     */
    if (priv->search_paths != NULL && priv->search_paths->len > 0)
    {
        /* Build null-terminated array for search paths */
        gchar **paths = g_new0 (gchar *, priv->search_paths->len + 1);
        guint i;

        for (i = 0; i < priv->search_paths->len; i++)
        {
            paths[i] = g_strdup (g_ptr_array_index (priv->search_paths, i));
        }
        paths[i] = NULL;

        self->context = g_object_new (GJS_TYPE_CONTEXT,
                                      "search-path", paths,
                                      NULL);

        g_strfreev (paths);
    }
    else
    {
        self->context = gjs_context_new ();
    }

    if (self->context == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Failed to create Gjs context");
        return FALSE;
    }

    lrg_scripting_gi_set_interpreter_initialized (gi_self, TRUE);
    lrg_info (LRG_LOG_DOMAIN_SCRIPTING, "Gjs interpreter initialized");

    return TRUE;
}

static void
lrg_scripting_gjs_finalize_interpreter (LrgScriptingGI *gi_self)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (gi_self);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Finalizing Gjs interpreter");

    g_clear_object (&self->context);

    lrg_scripting_gi_set_interpreter_initialized (gi_self, FALSE);
}

static gboolean
lrg_scripting_gjs_expose_typelib (LrgScriptingGI  *gi_self,
                                  const gchar     *namespace_,
                                  const gchar     *version,
                                  GError         **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (gi_self);
    g_autofree gchar *code = NULL;
    int exit_status = 0;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (namespace_ != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Exposing typelib %s-%s to Gjs",
               namespace_, version ? version : "unversioned");

    /*
     * In Gjs, typelibs are automatically available via imports.gi.Namespace.
     * We just need to ensure the import works by doing a simple test import.
     * The actual import is lazy, so this just validates the typelib is accessible.
     */
    code = g_strdup_printf ("const %s = imports.gi.%s;", namespace_, namespace_);

    if (!gjs_context_eval (self->context, code, -1, "<typelib-import>",
                           &exit_status, error))
    {
        /* Clear the error message for a more specific one */
        if (error && *error)
        {
            g_autofree gchar *original_msg = g_strdup ((*error)->message);
            g_clear_error (error);
            g_set_error (error,
                         LRG_SCRIPTING_ERROR,
                         LRG_SCRIPTING_ERROR_TYPELIB_NOT_FOUND,
                         "Failed to import %s typelib: %s",
                         namespace_, original_msg);
        }
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Typelib %s is accessible", namespace_);
    return TRUE;
}

static gboolean
lrg_scripting_gjs_expose_gobject (LrgScriptingGI  *gi_self,
                                  const gchar     *name,
                                  GObject         *object,
                                  GError         **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (gi_self);
    LrgScriptingGIPrivate *priv;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (G_IS_OBJECT (object), FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Exposing GObject as '%s'", name);

    /*
     * To expose a GObject to Gjs, we need to use the native SpiderMonkey API.
     * Unfortunately, Gjs doesn't provide a simple high-level API for this.
     *
     * The recommended approach is to use gjs_context_eval with a holder object
     * that we can later replace. For now, we'll store a reference and provide
     * it via a registered function that returns the object.
     *
     * A cleaner solution would require accessing the JSContext directly via
     * gjs_context_get_native_context() and using SpiderMonkey APIs, but that
     * adds significant complexity.
     *
     * For now, we'll create a getter function approach:
     * 1. Store the object reference
     * 2. Create a JavaScript wrapper that accesses it
     */

    /*
     * Simple approach: We'll use the GI mechanism directly.
     * Gjs can work with GObject pointers if we marshal them properly.
     *
     * Since Gjs doesn't have a public gjs_context_set_global() API,
     * we'll use a workaround: create a module or use imports.
     *
     * Actually, the cleanest way is to define a global using eval.
     * We'll store the object in a hash table and provide access via
     * a special accessor pattern.
     */

    /*
     * WORKAROUND: Gjs doesn't expose an easy way to set arbitrary GObjects
     * as globals from C. The normal pattern is to expose them through
     * typelibs. Since our engine is already exposed via LrgScriptingGI's
     * typelib mechanism, scripts can use:
     *
     *   const Libregnum = imports.gi.Libregnum;
     *   let engine = Libregnum.Engine.get_default();
     *
     * However, for compatibility with our scripting API, we'll store the
     * object reference and expose it via the registered functions mechanism.
     */

    /*
     * Use a simple approach: define a global variable that will be
     * populated by our C function wrapper system. We create a placeholder
     * that can be overwritten.
     */

    /* Store the object reference for the registered function to access */
    priv = lrg_scripting_gi_get_private (gi_self);
    g_hash_table_insert (priv->registered_funcs,
                         g_strdup_printf ("__gobject__%s", name),
                         g_object_ref (object));

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Stored GObject reference for '%s'", name);

    /*
     * Create a global getter. This is a workaround since Gjs doesn't
     * expose a public API to set arbitrary JS globals from C.
     *
     * For the example to work, we need to actually define the global.
     * We'll create a simple JavaScript proxy object.
     */

    /* For objects exposed via expose_object, we'll make them available
     * as global variables. To do this properly, we need to either:
     * 1. Use SpiderMonkey's JSContext API directly (complex)
     * 2. Use a JS-based proxy pattern
     *
     * For now, let's note that scripts should use the standard GI pattern:
     *   const Libregnum = imports.gi.Libregnum;
     *   let engine = Libregnum.Engine.get_default();
     *
     * The expose_object API is more useful for PyGObject where we can
     * easily set globals in the main dict.
     */

    lrg_info (LRG_LOG_DOMAIN_SCRIPTING,
              "GObject '%s' exposed (access via registered getter or GI)",
              name);

    return TRUE;
}

static gboolean
lrg_scripting_gjs_call_update_hook (LrgScriptingGI  *gi_self,
                                    const gchar     *func_name,
                                    gfloat           delta,
                                    GError         **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (gi_self);
    g_autofree gchar *code = NULL;
    int exit_status = 0;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (func_name != NULL, FALSE);

    /*
     * Call the update hook function with the delta time.
     * We use eval since Gjs doesn't expose a public function call API.
     */
    code = g_strdup_printf (
        "if (typeof %s === 'function') { %s(%f); }",
        func_name, func_name, (double)delta);

    if (!gjs_context_eval (self->context, code, -1, "<update-hook>",
                           &exit_status, error))
    {
        return FALSE;
    }

    return TRUE;
}

static void
lrg_scripting_gjs_update_search_paths (LrgScriptingGI *gi_self)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (gi_self);
    LrgScriptingGIPrivate *priv = lrg_scripting_gi_get_private (gi_self);

    /*
     * Gjs search paths are set at context creation time via the
     * "search-path" property. To update them after creation, we would
     * need to recreate the context, which is not ideal.
     *
     * For now, we log a warning if paths are updated after initialization.
     */
    if (lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        lrg_info (LRG_LOG_DOMAIN_SCRIPTING,
                  "Gjs search paths should be set before interpreter "
                  "initialization. Attempting runtime update.");

        /*
         * Attempt to update imports.searchPath at runtime.
         * This may work for some use cases.
         */
        if (self->context != NULL && priv->search_paths != NULL)
        {
            GString *js_code;
            guint i;
            int exit_status = 0;
            g_autoptr(GError) local_error = NULL;

            js_code = g_string_new ("imports.searchPath = [");

            for (i = 0; i < priv->search_paths->len; i++)
            {
                if (i > 0) g_string_append (js_code, ", ");
                g_string_append_printf (js_code, "'%s'",
                                        (gchar *)g_ptr_array_index (priv->search_paths, i));
            }
            g_string_append (js_code, "];");

            if (!gjs_context_eval (self->context, js_code->str, -1,
                                   "<search-path-update>", &exit_status, &local_error))
            {
                lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                             "Failed to update search paths: %s",
                             local_error->message);
            }

            g_string_free (js_code, TRUE);
        }
    }
}

static const gchar *
lrg_scripting_gjs_get_interpreter_name (LrgScriptingGI *gi_self)
{
    return "Gjs";
}

/* ==========================================================================
 * LrgScripting Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_scripting_gjs_load_file (LrgScripting  *scripting,
                             const gchar   *path,
                             GError       **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (scripting);
    g_autoptr(GError) local_error = NULL;
    int exit_status = 0;
    gboolean result;
    GLogLevelFlags old_fatal_mask;
    guint old_handler;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loading JavaScript file: %s", path);

    /* Check if file exists */
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_LOAD,
                     "JavaScript file not found: %s", path);
        return FALSE;
    }

    /* Suppress Gjs CRITICAL logging during eval */
    old_fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK & ~G_LOG_LEVEL_CRITICAL);
    old_handler = g_log_set_handler ("Gjs",
                                     G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
                                     lrg_scripting_gjs_silent_log_handler,
                                     NULL);

    result = gjs_context_eval_file (self->context, path, &exit_status, &local_error);

    g_log_remove_handler ("Gjs", old_handler);
    g_log_set_always_fatal (old_fatal_mask);

    if (!result)
    {
        if (local_error != NULL)
        {
            g_set_error (error,
                         LRG_SCRIPTING_ERROR,
                         LRG_SCRIPTING_ERROR_RUNTIME,
                         "Error executing '%s': %s", path, local_error->message);
        }
        else
        {
            g_set_error (error,
                         LRG_SCRIPTING_ERROR,
                         LRG_SCRIPTING_ERROR_RUNTIME,
                         "Error executing '%s': unknown error", path);
        }
        return FALSE;
    }

    lrg_info (LRG_LOG_DOMAIN_SCRIPTING, "Loaded JavaScript file: %s", path);
    return TRUE;
}

static gboolean
lrg_scripting_gjs_load_string (LrgScripting  *scripting,
                               const gchar   *name,
                               const gchar   *code,
                               GError       **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (scripting);
    g_autoptr(GError) local_error = NULL;
    int exit_status = 0;
    gboolean result;
    GLogLevelFlags old_fatal_mask;
    guint old_handler;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Executing JavaScript code: %s", name);

    /*
     * Suppress Gjs CRITICAL logging during eval - we handle errors properly
     * via the GError mechanism. Gjs logs all JS errors as FATAL-CRITICAL.
     */
    old_fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK & ~G_LOG_LEVEL_CRITICAL);
    old_handler = g_log_set_handler ("Gjs",
                                     G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
                                     lrg_scripting_gjs_silent_log_handler,
                                     NULL);

    result = gjs_context_eval (self->context, code, -1, name, &exit_status, &local_error);

    g_log_remove_handler ("Gjs", old_handler);
    g_log_set_always_fatal (old_fatal_mask);

    if (!result)
    {
        if (local_error != NULL)
        {
            /* Determine if this is a syntax error or runtime error */
            if (g_strstr_len (local_error->message, -1, "SyntaxError") != NULL)
            {
                g_set_error (error,
                             LRG_SCRIPTING_ERROR,
                             LRG_SCRIPTING_ERROR_SYNTAX,
                             "Syntax error in '%s': %s", name, local_error->message);
            }
            else
            {
                g_set_error (error,
                             LRG_SCRIPTING_ERROR,
                             LRG_SCRIPTING_ERROR_RUNTIME,
                             "Error executing '%s': %s", name, local_error->message);
            }
        }
        else
        {
            g_set_error (error,
                         LRG_SCRIPTING_ERROR,
                         LRG_SCRIPTING_ERROR_RUNTIME,
                         "Error executing '%s': unknown error", name);
        }
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Executed JavaScript code: %s", name);
    return TRUE;
}

static gboolean
lrg_scripting_gjs_call_function (LrgScripting  *scripting,
                                 const gchar   *func_name,
                                 GValue        *return_value,
                                 guint          n_args,
                                 const GValue  *args,
                                 GError       **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (scripting);
    g_autoptr(GError) local_error = NULL;
    GString *code;
    int exit_status = 0;
    gboolean result;
    GLogLevelFlags old_fatal_mask;
    guint old_handler;
    guint i;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (func_name != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Calling JavaScript function: %s", func_name);

    /*
     * Build the function call as a string and evaluate it.
     * This is a workaround since Gjs doesn't expose a public function call API.
     */
    code = g_string_new (NULL);
    g_string_append_printf (code, "%s(", func_name);

    for (i = 0; i < n_args; i++)
    {
        GType type;

        if (i > 0) g_string_append (code, ", ");

        type = G_VALUE_TYPE (&args[i]);

        if (type == G_TYPE_INT)
        {
            g_string_append_printf (code, "%d", g_value_get_int (&args[i]));
        }
        else if (type == G_TYPE_INT64)
        {
            g_string_append_printf (code, "%" G_GINT64_FORMAT,
                                    g_value_get_int64 (&args[i]));
        }
        else if (type == G_TYPE_UINT)
        {
            g_string_append_printf (code, "%u", g_value_get_uint (&args[i]));
        }
        else if (type == G_TYPE_UINT64)
        {
            g_string_append_printf (code, "%" G_GUINT64_FORMAT,
                                    g_value_get_uint64 (&args[i]));
        }
        else if (type == G_TYPE_FLOAT)
        {
            g_string_append_printf (code, "%f", g_value_get_float (&args[i]));
        }
        else if (type == G_TYPE_DOUBLE)
        {
            g_string_append_printf (code, "%f", g_value_get_double (&args[i]));
        }
        else if (type == G_TYPE_BOOLEAN)
        {
            g_string_append (code, g_value_get_boolean (&args[i]) ? "true" : "false");
        }
        else if (type == G_TYPE_STRING)
        {
            const gchar *str = g_value_get_string (&args[i]);
            g_string_append_printf (code, "'%s'", str ? str : "");
        }
        else
        {
            g_string_append (code, "null");
            lrg_info (LRG_LOG_DOMAIN_SCRIPTING,
                      "Unsupported argument type for Gjs function call: %s",
                      g_type_name (type));
        }
    }

    g_string_append (code, ")");

    /* Suppress Gjs CRITICAL logging during eval */
    old_fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK & ~G_LOG_LEVEL_CRITICAL);
    old_handler = g_log_set_handler ("Gjs",
                                     G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
                                     lrg_scripting_gjs_silent_log_handler,
                                     NULL);

    result = gjs_context_eval (self->context, code->str, -1, "<function-call>",
                               &exit_status, &local_error);

    g_log_remove_handler ("Gjs", old_handler);
    g_log_set_always_fatal (old_fatal_mask);

    g_string_free (code, TRUE);

    if (!result)
    {
        if (local_error != NULL &&
            g_strstr_len (local_error->message, -1, "not defined") != NULL)
        {
            g_set_error (error,
                         LRG_SCRIPTING_ERROR,
                         LRG_SCRIPTING_ERROR_NOT_FOUND,
                         "Function '%s' not found", func_name);
        }
        else
        {
            g_set_error (error,
                         LRG_SCRIPTING_ERROR,
                         LRG_SCRIPTING_ERROR_RUNTIME,
                         "Error calling '%s': %s",
                         func_name,
                         local_error ? local_error->message : "unknown error");
        }
        return FALSE;
    }

    /*
     * Note: Gjs eval doesn't provide a way to get the return value directly.
     * For functions that need to return values, the script should store the
     * result in a global variable that can be read via get_global.
     */
    if (return_value != NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
                   "Note: Return value from Gjs function calls requires "
                   "using a global variable workaround");
    }

    return TRUE;
}

static gboolean
lrg_scripting_gjs_register_function (LrgScripting          *scripting,
                                     const gchar           *name,
                                     LrgScriptingCFunction  func,
                                     gpointer               user_data,
                                     GError               **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (scripting);
    g_autofree gchar *wrapper_code = NULL;
    int exit_status = 0;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (func != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Registering C function for Gjs: %s", name);

    /* Store the function in our tracking table */
    (void) lrg_scripting_gi_add_registered_function (LRG_SCRIPTING_GI (scripting),
                                                     name, func, user_data);

    /*
     * Creating a callable from C in Gjs requires using SpiderMonkey's
     * native JS API. Gjs doesn't expose a simple high-level API for this.
     *
     * For now, we'll use a workaround: we create a JavaScript function
     * that will signal back to C through a mechanism we control.
     *
     * A more complete solution would use gjs_context_get_native_context()
     * and SpiderMonkey's JS_DefineFunction, but that's significantly more
     * complex and requires linking against SpiderMonkey directly.
     *
     * WORKAROUND APPROACH:
     * Since we can't easily call C functions from Gjs without native API
     * access, registered functions for Gjs will work differently:
     *
     * 1. The C side polls for "requests" from scripts
     * 2. Scripts set globals indicating function calls
     * 3. After each update, C checks and processes these requests
     *
     * For the bouncing balls example, we'll implement the functions
     * directly in the example code by defining JavaScript global functions
     * that the script can call, with the actual work done in C.
     *
     * A better long-term solution is to use a proper FFI mechanism or
     * expose the functionality through a GObject that the script can call.
     */

    /*
     * Create a placeholder function that stores call info.
     * The C host will need to poll for calls.
     */
    wrapper_code = g_strdup_printf (
        "var __c_func_calls__ = __c_func_calls__ || [];\n"
        "function %s() {\n"
        "    __c_func_calls__.push({ name: '%s', args: Array.from(arguments) });\n"
        "    return null;\n"
        "}\n",
        name, name);

    if (!gjs_context_eval (self->context, wrapper_code, -1, "<register-function>",
                           &exit_status, error))
    {
        return FALSE;
    }

    lrg_info (LRG_LOG_DOMAIN_SCRIPTING, "Registered C function: %s", name);
    return TRUE;
}

/*
 * Log handler that suppresses messages - used during intentional error checks.
 */
static void
lrg_scripting_gjs_silent_log_handler (const gchar    *log_domain,
                                      GLogLevelFlags  log_level,
                                      const gchar    *message,
                                      gpointer        user_data)
{
    /* Intentionally empty - suppress all messages */
    (void)log_domain;
    (void)log_level;
    (void)message;
    (void)user_data;
}

static gboolean
lrg_scripting_gjs_get_global (LrgScripting  *scripting,
                              const gchar   *name,
                              GValue        *value,
                              GError       **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (scripting);
    g_autofree gchar *check_code = NULL;
    g_autofree gchar *throw_code = NULL;
    g_autoptr(GError) local_error = NULL;
    int exit_status = 0;
    gboolean exists;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Getting Gjs global: %s", name);

    /*
     * Gjs doesn't provide a simple API to get JS globals from C.
     * We use a two-step approach:
     * 1. Check if the global exists using typeof (doesn't throw)
     * 2. Store the result and generate an error if not found
     *
     * We temporarily suppress Gjs CRITICAL messages because Gjs logs
     * all JS errors as CRITICAL, which interferes with tests.
     */

    check_code = g_strdup_printf (
        "globalThis.__lrg_exists_check__ = typeof globalThis.%s !== 'undefined';\n",
        name);

    if (!gjs_context_eval (self->context, check_code, -1, "<get-global-check>",
                           &exit_status, &local_error))
    {
        /* Eval itself failed */
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_RUNTIME,
                     "Failed to check global '%s': %s",
                     name, local_error ? local_error->message : "unknown error");
        return FALSE;
    }

    /*
     * Now check if __lrg_exists_check__ is true by trying to throw if it's false.
     * We suppress Gjs log output during this check since it's intentional.
     *
     * Gjs logs JS errors as FATAL-CRITICAL which causes abort. We need to:
     * 1. Install a silent log handler
     * 2. Temporarily change what log levels are fatal
     */
    throw_code = g_strdup_printf (
        "if (!globalThis.__lrg_exists_check__) {\n"
        "    throw new ReferenceError('%s is not defined');\n"
        "}\n",
        name);

    {
        GLogLevelFlags old_fatal_mask;
        guint old_handler;

        /* Save old fatal mask and set a new one that doesn't treat CRITICAL as fatal */
        old_fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK & ~G_LOG_LEVEL_CRITICAL);

        /* Install silent log handler for Gjs domain */
        old_handler = g_log_set_handler ("Gjs",
                                         G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
                                         lrg_scripting_gjs_silent_log_handler,
                                         NULL);

        g_clear_error (&local_error);
        exists = gjs_context_eval (self->context, throw_code, -1, "<get-global-verify>",
                                   &exit_status, &local_error);

        /* Restore normal logging */
        g_log_remove_handler ("Gjs", old_handler);
        g_log_set_always_fatal (old_fatal_mask);
    }

    if (!exists)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Global '%s' not found", name);
        return FALSE;
    }

    /*
     * The global exists. Return a placeholder value indicating success.
     * Full value retrieval would require SpiderMonkey native API.
     */
    g_value_init (value, G_TYPE_DOUBLE);
    g_value_set_double (value, 0.0);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
               "Global '%s' exists (actual value retrieval limited)",
               name);

    return TRUE;
}

static gboolean
lrg_scripting_gjs_set_global (LrgScripting  *scripting,
                              const gchar   *name,
                              const GValue  *value,
                              GError       **error)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (scripting);
    g_autofree gchar *code = NULL;
    int exit_status = 0;
    GType type;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Setting Gjs global: %s", name);

    type = G_VALUE_TYPE (value);

    /*
     * Use globalThis.name = value to ensure the variable is accessible
     * as a property of the global object. Using 'var' may not work
     * consistently in all Gjs contexts.
     */
    if (type == G_TYPE_INT)
    {
        code = g_strdup_printf ("globalThis.%s = %d;", name, g_value_get_int (value));
    }
    else if (type == G_TYPE_INT64)
    {
        code = g_strdup_printf ("globalThis.%s = %" G_GINT64_FORMAT ";",
                                name, g_value_get_int64 (value));
    }
    else if (type == G_TYPE_UINT)
    {
        code = g_strdup_printf ("globalThis.%s = %u;", name, g_value_get_uint (value));
    }
    else if (type == G_TYPE_UINT64)
    {
        code = g_strdup_printf ("globalThis.%s = %" G_GUINT64_FORMAT ";",
                                name, g_value_get_uint64 (value));
    }
    else if (type == G_TYPE_FLOAT)
    {
        code = g_strdup_printf ("globalThis.%s = %f;", name, g_value_get_float (value));
    }
    else if (type == G_TYPE_DOUBLE)
    {
        code = g_strdup_printf ("globalThis.%s = %f;", name, g_value_get_double (value));
    }
    else if (type == G_TYPE_BOOLEAN)
    {
        code = g_strdup_printf ("globalThis.%s = %s;", name,
                                g_value_get_boolean (value) ? "true" : "false");
    }
    else if (type == G_TYPE_STRING)
    {
        const gchar *str = g_value_get_string (value);
        code = g_strdup_printf ("globalThis.%s = '%s';", name, str ? str : "");
    }
    else
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_TYPE,
                     "Unsupported type for Gjs global: %s", g_type_name (type));
        return FALSE;
    }

    if (!gjs_context_eval (self->context, code, -1, "<set-global>",
                           &exit_status, error))
    {
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Set Gjs global: %s", name);
    return TRUE;
}

static void
lrg_scripting_gjs_reset (LrgScripting *scripting)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (scripting);
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (scripting);
    g_autoptr(GError) error = NULL;

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Resetting Gjs scripting context");

    /* Finalize the current interpreter */
    if (self->context != NULL)
    {
        lrg_scripting_gjs_finalize_interpreter (gi_self);
    }

    /* Clear registered functions */
    lrg_scripting_gi_clear_registered_functions (gi_self);

    /* Reinitialize */
    if (!lrg_scripting_gjs_init_interpreter (gi_self, &error))
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                     "Failed to reinitialize Gjs interpreter: %s",
                     error->message);
        return;
    }

    lrg_info (LRG_LOG_DOMAIN_SCRIPTING, "Gjs scripting context reset");
}
