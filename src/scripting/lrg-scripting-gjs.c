/* lrg-scripting-gjs.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gjs (GNOME JavaScript) scripting backend implementation.
 *
 * Gjs supports a single GjsContext per thread, so every LrgScriptingGjs
 * shares one runtime.  Contexts are isolated inside it:
 *
 *  - Each context gets a numeric LrgScriptHost bridge id and a private scope
 *    object in the runtime's `__lrg` prelude.
 *  - Loaded code runs inside a function whose `globalThis` parameter is that
 *    scope, under `with (globalThis)`, so top-level declarations stay local to
 *    the context, `globalThis.x = 1` writes the context scope, and names the
 *    host publishes (registered functions, globals, exposed objects) resolve
 *    as plain identifiers.  Everything else (imports, print, Math) falls
 *    through to the real global object.
 *  - Values cross between C and JavaScript as GVariants through
 *    LrgScriptHost, never spliced into source text.
 *
 * The prelude needs the Libregnum typelib.  Its directory is found from the
 * LRG_TYPELIB_PATH environment variable, next to the loaded libregnum
 * shared object (`../gir` or `girepository-1.0`), or through the usual
 * GI_TYPELIB_PATH.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "config.h"
#include "lrg-scripting-gjs.h"
#include "lrg-scripting-gi-private.h"
#include "lrg-script-host.h"
#include "../lrg-log.h"

#include <gjs/gjs.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <dlfcn.h>
#endif

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

    GjsContext     *context;      /* the shared runtime (a reference) */
    guint           host_id;      /* LrgScriptHost bridge id, 0 when idle */
};

G_DEFINE_FINAL_TYPE (LrgScriptingGjs, lrg_scripting_gjs, LRG_TYPE_SCRIPTING_GI)

/* The one runtime shared by every context on the main thread */
static GjsContext *shared_context = NULL;
static guint       shared_users = 0;
static gboolean    shared_bridge_ok = FALSE;
static gchar      *shared_bridge_error = NULL;

/*
 * The runtime prelude.  It owns one scope per bridge id and implements the
 * operations the C side requests.  Every entry point reports back through
 * script_host_set_result() as ("ok" | "missing" | "error", value), so script
 * exceptions never escape into Gjs's own uncaught-error logging.
 */
static const gchar gjs_prelude[] =
    "globalThis.__lrg = (function () {\n"
    "    'use strict';\n"
    "    const GLib = imports.gi.GLib;\n"
    "    imports.gi.versions.Libregnum = '1';\n"
    "    const Lrg = imports.gi.Libregnum;\n"
    "    const IDENT = /^[A-Za-z_$][\\w$]*$/;\n"
    "    const contexts = new Map();\n"
    "    function ctx(id) {\n"
    "        let c = contexts.get(id);\n"
    "        if (c === undefined) {\n"
    "            c = { scope: Object.create(null), resolvers: [] };\n"
    "            contexts.set(id, c);\n"
    "        }\n"
    "        return c;\n"
    "    }\n"
    "    function pack(v) {\n"
    "        switch (typeof v) {\n"
    "        case 'boolean': return new GLib.Variant('b', v);\n"
    "        case 'number':\n"
    "            if (Number.isSafeInteger(v)) return new GLib.Variant('x', v);\n"
    "            return new GLib.Variant('d', v);\n"
    "        case 'bigint': return new GLib.Variant('x', Number(v));\n"
    "        case 'string': return new GLib.Variant('s', v);\n"
    "        default: return new GLib.Variant('ms', null);\n"
    "        }\n"
    "    }\n"
    "    function args(id) {\n"
    "        const v = Lrg.script_host_take_args(id);\n"
    "        return v === null ? [] : v.recursiveUnpack();\n"
    "    }\n"
    "    function reply(id, status, value) {\n"
    "        Lrg.script_host_set_result(id, new GLib.Variant('(sv)', [status, pack(value)]));\n"
    "    }\n"
    "    const MISSING = {};\n"
    "    function lookup(c, name) {\n"
    "        if (typeof name !== 'string' || !IDENT.test(name)) return MISSING;\n"
    "        for (let i = c.resolvers.length - 1; i >= 0; i--) {\n"
    "            const r = c.resolvers[i](name);\n"
    "            if (r.found) return r.value;\n"
    "        }\n"
    "        return (name in c.scope) ? c.scope[name] : MISSING;\n"
    "    }\n"
    "    function guarded(id, body) {\n"
    "        try { body(); }\n"
    "        catch (e) {\n"
    "            let text = String(e);\n"
    "            if (e && e.stack) text += '\\n' + e.stack;\n"
    "            reply(id, 'error', text);\n"
    "        }\n"
    "    }\n"
    "    return Object.freeze({\n"
    "        scope(id) { return ctx(id).scope; },\n"
    "        load(id, factory) { const c = ctx(id); c.resolvers.push(factory(c.scope)); },\n"
    "        invoke(id) {\n"
    "            guarded(id, () => {\n"
    "                const [name, ...rest] = args(id);\n"
    "                const f = lookup(ctx(id), name);\n"
    "                if (typeof f !== 'function') { reply(id, 'missing', null); return; }\n"
    "                reply(id, 'ok', f(...rest));\n"
    "            });\n"
    "        },\n"
    "        has(id) {\n"
    "            guarded(id, () => {\n"
    "                const [name] = args(id);\n"
    "                reply(id, 'ok', typeof lookup(ctx(id), name) === 'function');\n"
    "            });\n"
    "        },\n"
    "        get(id) {\n"
    "            guarded(id, () => {\n"
    "                const [name] = args(id);\n"
    "                const v = lookup(ctx(id), name);\n"
    "                if (v === MISSING || v === undefined) reply(id, 'missing', null);\n"
    "                else reply(id, 'ok', v);\n"
    "            });\n"
    "        },\n"
    "        set(id) {\n"
    "            guarded(id, () => {\n"
    "                const [name, value] = args(id);\n"
    "                ctx(id).scope[name] = value;\n"
    "                reply(id, 'ok', null);\n"
    "            });\n"
    "        },\n"
    "        define(id) {\n"
    "            guarded(id, () => {\n"
    "                const [name] = args(id);\n"
    "                ctx(id).scope[name] = function (...a) {\n"
    "                    const r = Lrg.script_host_call(id, name,\n"
    "                        new GLib.Variant('av', a.map(pack)));\n"
    "                    return r === null ? undefined : r.recursiveUnpack();\n"
    "                };\n"
    "                reply(id, 'ok', null);\n"
    "            });\n"
    "        },\n"
    "        expose(id) {\n"
    "            guarded(id, () => {\n"
    "                const [name] = args(id);\n"
    "                ctx(id).scope[name] = Lrg.script_host_get_object(id, name);\n"
    "                reply(id, 'ok', null);\n"
    "            });\n"
    "        },\n"
    "        forget(id) { contexts.delete(id); },\n"
    "    });\n"
    "})();\n";

/* ==========================================================================
 * Forward Declarations
 * ========================================================================== */

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
static gboolean lrg_scripting_gjs_has_function       (LrgScripting    *scripting,
                                                      const gchar     *func_name);

/* ==========================================================================
 * Shared Runtime
 * ========================================================================== */

/*
 * Swallows Gjs's own logging of script errors; they arrive as GErrors.  It
 * must not log itself: logging from a log handler is fatal recursion.
 */
static void
lrg_scripting_gjs_silent_log_handler (const gchar    *log_domain,
                                      GLogLevelFlags  log_level,
                                      const gchar    *message,
                                      gpointer        user_data)
{
    (void)log_domain;
    (void)log_level;
    (void)message;
    (void)user_data;
}

/*
 * Evaluates @code in the shared runtime with Gjs's error logging silenced.
 * Script errors are reported only through @error.
 */
static gboolean
gjs_eval_quiet (const gchar  *code,
                gssize        length,
                const gchar  *filename,
                GError      **error)
{
    GLogLevelFlags old_fatal_mask;
    guint          old_handler;
    int            exit_status;
    gboolean       ok;

    exit_status = 0;
    old_fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK &
                                             ~(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING));
    old_handler = g_log_set_handler ("Gjs",
                                     G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
                                     lrg_scripting_gjs_silent_log_handler,
                                     NULL);

    ok = gjs_context_eval (shared_context, code, length, filename, &exit_status, error);

    g_log_remove_handler ("Gjs", old_handler);
    g_log_set_always_fatal (old_fatal_mask);

    return ok;
}

/*
 * Adds directories that may hold Libregnum-1.typelib to the default GI
 * repository before the prelude imports it: $LRG_TYPELIB_PATH, then the
 * build tree (`<libdir>/../gir`) and install layout
 * (`<libdir>/girepository-1.0`) next to the loaded libregnum object.
 */
static void
gjs_add_typelib_paths (void)
{
    g_autoptr(GIRepository) repository = NULL;
    const gchar            *env;

    repository = gi_repository_dup_default ();

    env = g_getenv ("LRG_TYPELIB_PATH");
    if (env != NULL && env[0] != '\0')
    {
        g_auto(GStrv) dirs = g_strsplit (env, G_SEARCHPATH_SEPARATOR_S, -1);
        guint         i;

        for (i = 0; dirs[i] != NULL; i++)
        {
            if (dirs[i][0] != '\0')
            {
                gi_repository_prepend_search_path (repository, dirs[i]);
            }
        }
    }

#ifdef G_OS_UNIX
    {
        Dl_info info;

        if (dladdr ((gpointer)lrg_script_host_take_args, &info) != 0 &&
            info.dli_fname != NULL)
        {
            g_autofree gchar *libdir = g_path_get_dirname (info.dli_fname);
            g_autofree gchar *build_gir = g_build_filename (libdir, "..", "gir", NULL);
            g_autofree gchar *installed = g_build_filename (libdir, "girepository-1.0", NULL);

            if (g_file_test (installed, G_FILE_TEST_IS_DIR))
            {
                gi_repository_prepend_search_path (repository, installed);
            }
            if (g_file_test (build_gir, G_FILE_TEST_IS_DIR))
            {
                gi_repository_prepend_search_path (repository, build_gir);
            }
        }
    }
#endif
}

/*
 * Takes a reference on the shared runtime, creating it (and installing the
 * prelude) for the first user.
 */
static gboolean
gjs_shared_acquire (GError **error)
{
    g_autoptr(GError) local_error = NULL;

    if (shared_context != NULL)
    {
        shared_users++;
        return TRUE;
    }

    shared_context = gjs_context_new ();
    if (shared_context == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "Failed to create Gjs context");
        return FALSE;
    }
    shared_users = 1;

    /* The bridge is optional: plain script evaluation works without it */
    gjs_add_typelib_paths ();
    g_clear_pointer (&shared_bridge_error, g_free);
    shared_bridge_ok = gjs_eval_quiet (gjs_prelude, -1, "<lrg-prelude>", &local_error);
    if (!shared_bridge_ok)
    {
        shared_bridge_error = g_strdup (local_error != NULL ? local_error->message
                                                            : "unknown error");
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                     "Gjs host bridge unavailable (Libregnum typelib not found?): %s",
                     shared_bridge_error);
    }

    return TRUE;
}

/* Drops a reference; the last user tears the runtime down */
static void
gjs_shared_release (void)
{
    g_return_if_fail (shared_users > 0);

    shared_users--;
    if (shared_users == 0)
    {
        g_clear_object (&shared_context);
        shared_bridge_ok = FALSE;
    }
}

/* Reports a missing bridge as a typelib error */
static gboolean
gjs_require_bridge (GError **error)
{
    if (shared_bridge_ok)
    {
        return TRUE;
    }

    g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPELIB_NOT_FOUND,
                 "The Gjs host bridge needs the Libregnum typelib "
                 "(set LRG_TYPELIB_PATH or GI_TYPELIB_PATH): %s",
                 shared_bridge_error != NULL ? shared_bridge_error : "not initialized");
    return FALSE;
}

/*
 * Runs one prelude operation (`__lrg.<op>(<id>)`) with @args stashed, and
 * returns the status string and value it replied with.  @value_out may be
 * NULL.  Returns FALSE with @error set for evaluation failures and script
 * exceptions; a "missing" status is returned to the caller to interpret.
 */
static gboolean
gjs_bridge_op (LrgScriptingGjs  *self,
               const gchar      *op,
               GVariant         *args,
               gchar           **status_out,
               GVariant        **value_out,
               GError          **error)
{
    g_autoptr(GVariant) result = NULL;
    g_autofree gchar   *code = NULL;
    g_autofree gchar   *status = NULL;
    GVariant           *value = NULL;

    if (!gjs_require_bridge (error))
    {
        return FALSE;
    }

    /* Only a numeric id is ever formatted into source */
    lrg_script_host_stash_args (self->host_id, args);
    code = g_strdup_printf ("__lrg.%s(%u);", op, self->host_id);
    if (!gjs_eval_quiet (code, -1, "<lrg-bridge>", error))
    {
        GVariant *left = lrg_script_host_take_args (self->host_id);

        /* Do not leave arguments behind for the next operation */
        if (left != NULL)
        {
            g_variant_unref (left);
        }
        return FALSE;
    }

    result = lrg_script_host_steal_result (self->host_id);
    if (result == NULL || !g_variant_is_of_type (result, G_VARIANT_TYPE ("(sv)")))
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "Gjs bridge operation '%s' returned no result", op);
        return FALSE;
    }

    g_variant_get (result, "(sv)", &status, &value);
    if (g_strcmp0 (status, "error") == 0)
    {
        const gchar *text = g_variant_is_of_type (value, G_VARIANT_TYPE_STRING)
                            ? g_variant_get_string (value, NULL) : "unknown error";

        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                     "%s", text);
        g_variant_unref (value);
        return FALSE;
    }

    if (status_out != NULL)
    {
        *status_out = g_steal_pointer (&status);
    }
    if (value_out != NULL)
    {
        *value_out = value;
    }
    else
    {
        g_variant_unref (value);
    }
    return TRUE;
}

/* Builds an `av` from one name and optional GValue arguments */
static GVariant *
gjs_pack_args (const gchar  *name,
               guint         n_args,
               const GValue *args)
{
    GVariantBuilder builder;
    guint           i;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("av"));
    if (name != NULL)
    {
        g_variant_builder_add (&builder, "v", g_variant_new_string (name));
    }
    for (i = 0; i < n_args; i++)
    {
        g_autoptr(GVariant) packed = lrg_script_host_value_to_variant (&args[i]);

        g_variant_builder_add (&builder, "v", packed);
    }

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/* Accepts the identifier forms the prelude can resolve */
static gboolean
gjs_valid_identifier (const gchar *name)
{
    const gchar *p;

    if (name == NULL || name[0] == '\0' ||
        !(g_ascii_isalpha (name[0]) || name[0] == '_' || name[0] == '$'))
    {
        return FALSE;
    }
    for (p = name + 1; *p != '\0'; p++)
    {
        if (!(g_ascii_isalnum (*p) || *p == '_' || *p == '$'))
        {
            return FALSE;
        }
    }
    return TRUE;
}

/* ==========================================================================
 * GObject Lifecycle
 * ========================================================================== */

static void
lrg_scripting_gjs_dispose (GObject *object)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (object);

    if (self->context != NULL)
    {
        lrg_scripting_gjs_finalize_interpreter (LRG_SCRIPTING_GI (self));
    }

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
    scripting_class->has_function = lrg_scripting_gjs_has_function;
}

static void
lrg_scripting_gjs_init (LrgScriptingGjs *self)
{
    self->context = NULL;
    self->host_id = 0;
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

    g_return_val_if_fail (self->context == NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Initializing Gjs interpreter");

    if (!gjs_shared_acquire (error))
    {
        return FALSE;
    }

    self->context = g_object_ref (shared_context);
    self->host_id = lrg_script_host_register (LRG_SCRIPTING (self));
    lrg_scripting_gi_set_interpreter_initialized (gi_self, TRUE);

    /* Paths added before initialization apply now */
    lrg_scripting_gjs_update_search_paths (gi_self);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Gjs interpreter initialized (bridge %u)",
               self->host_id);
    return TRUE;
}

static void
lrg_scripting_gjs_finalize_interpreter (LrgScriptingGI *gi_self)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (gi_self);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Finalizing Gjs interpreter");

    if (self->context == NULL)
    {
        return;
    }

    /* Drop this context's scope before the runtime may go away */
    if (shared_bridge_ok && self->host_id != 0)
    {
        g_autofree gchar *code = g_strdup_printf ("__lrg.forget(%u);", self->host_id);
        g_autoptr(GError) local_error = NULL;

        if (!gjs_eval_quiet (code, -1, "<lrg-forget>", &local_error))
        {
            lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Gjs forget failed: %s",
                       local_error->message);
        }
    }
    if (self->host_id != 0)
    {
        lrg_script_host_unregister (self->host_id);
        self->host_id = 0;
    }

    g_clear_object (&self->context);
    gjs_shared_release ();

    lrg_scripting_gi_set_interpreter_initialized (gi_self, FALSE);
}

static gboolean
lrg_scripting_gjs_expose_typelib (LrgScriptingGI  *gi_self,
                                  const gchar     *namespace_,
                                  const gchar     *version,
                                  GError         **error)
{
    LrgScriptingGjs  *self = LRG_SCRIPTING_GJS (gi_self);
    g_autofree gchar *code = NULL;
    g_autoptr(GError) local_error = NULL;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (namespace_ != NULL, FALSE);

    /* The namespace is formatted into source, so it must be an identifier */
    if (!gjs_valid_identifier (namespace_))
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPELIB_NOT_FOUND,
                     "Invalid typelib namespace '%s'", namespace_);
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Exposing typelib %s-%s to Gjs",
               namespace_, version ? version : "unversioned");

    /* Typelibs are reached as imports.gi.<Namespace>; validate the import */
    code = g_strdup_printf ("imports.gi.%s;", namespace_);
    if (!gjs_eval_quiet (code, -1, "<typelib-import>", &local_error))
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPELIB_NOT_FOUND,
                     "Failed to import %s typelib: %s", namespace_,
                     local_error != NULL ? local_error->message : "unknown error");
        return FALSE;
    }

    return TRUE;
}

static gboolean
lrg_scripting_gjs_expose_gobject (LrgScriptingGI  *gi_self,
                                  const gchar     *name,
                                  GObject         *object,
                                  GError         **error)
{
    LrgScriptingGjs     *self = LRG_SCRIPTING_GJS (gi_self);
    g_autoptr(GVariant)  args = NULL;

    g_return_val_if_fail (self->context != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (G_IS_OBJECT (object), FALSE);

    /* Publish through the bridge, then bind the name in this scope */
    lrg_script_host_add_object (self->host_id, name, object);
    args = gjs_pack_args (name, 0, NULL);
    return gjs_bridge_op (self, "expose", args, NULL, NULL, error);
}

static gboolean
lrg_scripting_gjs_call_update_hook (LrgScriptingGI  *gi_self,
                                    const gchar     *func_name,
                                    gfloat           delta,
                                    GError         **error)
{
    GValue   arg = G_VALUE_INIT;
    gboolean ok;

    g_return_val_if_fail (func_name != NULL, FALSE);

    /* A hook that is not (yet) defined is skipped, as before */
    if (!lrg_scripting_gjs_has_function (LRG_SCRIPTING (gi_self), func_name))
    {
        return TRUE;
    }

    g_value_init (&arg, G_TYPE_DOUBLE);
    g_value_set_double (&arg, (gdouble)delta);
    ok = lrg_scripting_gjs_call_function (LRG_SCRIPTING (gi_self), func_name,
                                          NULL, 1, &arg, error);
    g_value_unset (&arg);
    return ok;
}

static void
lrg_scripting_gjs_update_search_paths (LrgScriptingGI *gi_self)
{
    LrgScriptingGjs       *self = LRG_SCRIPTING_GJS (gi_self);
    LrgScriptingGIPrivate *priv = lrg_scripting_gi_get_private (gi_self);
    GVariantBuilder        builder;
    g_autoptr(GVariant)    args = NULL;
    g_autoptr(GError)      local_error = NULL;
    g_autofree gchar      *code = NULL;
    guint                  path_id;
    guint                  i;

    if (self->context == NULL || !shared_bridge_ok ||
        priv->search_paths == NULL || priv->search_paths->len == 0)
    {
        return;
    }

    /* imports.searchPath belongs to the shared runtime */
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("av"));
    for (i = 0; i < priv->search_paths->len; i++)
    {
        g_variant_builder_add (&builder, "v",
                               g_variant_new_string (g_ptr_array_index (priv->search_paths, i)));
    }
    args = g_variant_ref_sink (g_variant_builder_end (&builder));

    /* A temporary bridge slot carries the paths, so they are never quoted */
    path_id = lrg_script_host_register (LRG_SCRIPTING (self));
    lrg_script_host_stash_args (path_id, args);
    code = g_strdup_printf (
        "(function () {"
        " const p = imports.gi.Libregnum.script_host_take_args(%u);"
        " if (p !== null)"
        "  for (const d of p.recursiveUnpack().reverse())"
        "   if (!imports.searchPath.includes(d)) imports.searchPath.unshift(d);"
        " })();", path_id);
    if (!gjs_eval_quiet (code, -1, "<lrg-search-path>", &local_error))
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING, "Failed to update Gjs search paths: %s",
                     local_error->message);
    }
    lrg_script_host_unregister (path_id);
}

static const gchar *
lrg_scripting_gjs_get_interpreter_name (LrgScriptingGI *gi_self)
{
    (void)gi_self;
    return "Gjs";
}

/* ==========================================================================
 * LrgScripting Virtual Method Implementations
 * ========================================================================== */

/*
 * Evaluates @code as a new unit of this context: wrapped in a function whose
 * `globalThis` is the context scope, returning a resolver for its
 * top-level bindings.  The wrapper prefix shares the first line with the
 * code so reported line numbers stay correct.
 */
static gboolean
gjs_load_unit (LrgScriptingGjs  *self,
               const gchar      *label,
               const gchar      *code,
               GError          **error)
{
    g_autoptr(GString) wrapped = NULL;
    g_autoptr(GError)  local_error = NULL;

    if (self->context == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "Gjs interpreter not initialized");
        return FALSE;
    }

    wrapped = g_string_new (NULL);
    if (shared_bridge_ok)
    {
        g_string_append_printf (wrapped,
            "__lrg.load(%u, function (globalThis) { with (globalThis) { return (function () { ",
            self->host_id);
        g_string_append (wrapped, code);
        g_string_append (wrapped,
            "\n;return function (__lrg_n) {"
            " try { return { found: true, value: eval(__lrg_n) }; }"
            " catch (__lrg_e) { if (__lrg_e instanceof ReferenceError) return { found: false }; throw __lrg_e; }"
            " }; })(); } });\n");
    }
    else
    {
        /* Without the bridge the code runs directly in the shared global */
        g_string_append (wrapped, code);
    }

    if (!gjs_eval_quiet (wrapped->str, (gssize)wrapped->len, label, &local_error))
    {
        const gchar *message = local_error != NULL ? local_error->message : "unknown error";

        if (strstr (message, "SyntaxError") != NULL)
        {
            g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_SYNTAX,
                         "Syntax error in '%s': %s", label, message);
        }
        else
        {
            g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                         "Error executing '%s': %s", label, message);
        }
        return FALSE;
    }

    return TRUE;
}

static gboolean
lrg_scripting_gjs_load_file (LrgScripting  *scripting,
                             const gchar   *path,
                             GError       **error)
{
    LrgScriptingGjs   *self = LRG_SCRIPTING_GJS (scripting);
    g_autofree gchar  *code = NULL;
    g_autoptr(GError)  local_error = NULL;

    g_return_val_if_fail (path != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loading JavaScript file: %s", path);

    if (!g_file_get_contents (path, &code, NULL, &local_error))
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD,
                     "JavaScript file not found or unreadable: %s (%s)", path,
                     local_error->message);
        return FALSE;
    }

    /* A leading #! line is not JavaScript */
    if (code[0] == '#' && code[1] == '!')
    {
        code[0] = '/';
        code[1] = '/';
    }

    return gjs_load_unit (self, path, code, error);
}

static gboolean
lrg_scripting_gjs_load_string (LrgScripting  *scripting,
                               const gchar   *name,
                               const gchar   *code,
                               GError       **error)
{
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Executing JavaScript code: %s", name);

    return gjs_load_unit (LRG_SCRIPTING_GJS (scripting), name, code, error);
}

static gboolean
lrg_scripting_gjs_call_function (LrgScripting  *scripting,
                                 const gchar   *func_name,
                                 GValue        *return_value,
                                 guint          n_args,
                                 const GValue  *args,
                                 GError       **error)
{
    LrgScriptingGjs     *self = LRG_SCRIPTING_GJS (scripting);
    g_autoptr(GVariant)  packed = NULL;
    g_autoptr(GVariant)  value = NULL;
    g_autofree gchar    *status = NULL;

    g_return_val_if_fail (func_name != NULL, FALSE);

    if (self->context == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "Gjs interpreter not initialized");
        return FALSE;
    }

    packed = gjs_pack_args (func_name, n_args, args);
    if (!gjs_bridge_op (self, "invoke", packed, &status, &value, error))
    {
        return FALSE;
    }

    if (g_strcmp0 (status, "missing") == 0)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Function '%s' not found", func_name);
        return FALSE;
    }

    /* "Nothing" leaves the return location unset, like the other backends */
    if (return_value != NULL &&
        !(g_variant_is_of_type (value, G_VARIANT_TYPE_MAYBE) &&
          g_variant_n_children (value) == 0))
    {
        if (!lrg_script_host_variant_to_value (value, return_value))
        {
            g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
                         "Cannot convert the return value of '%s'", func_name);
            return FALSE;
        }
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
    LrgScriptingGjs     *self = LRG_SCRIPTING_GJS (scripting);
    LrgScriptingGI      *gi_self = LRG_SCRIPTING_GI (scripting);
    g_autoptr(GVariant)  args = NULL;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (func != NULL, FALSE);

    if (self->context == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "Gjs interpreter not initialized");
        return FALSE;
    }
    if (!gjs_valid_identifier (name))
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "'%s' is not a valid JavaScript identifier", name);
        return FALSE;
    }

    /* Track in the GI base (for has_registered_function) and the bridge */
    lrg_scripting_gi_add_registered_function (gi_self, name, func, user_data);
    lrg_script_host_add_function (self->host_id, name, func, user_data);

    args = gjs_pack_args (name, 0, NULL);
    if (!gjs_bridge_op (self, "define", args, NULL, NULL, error))
    {
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Registered C function for Gjs: %s", name);
    return TRUE;
}

static gboolean
lrg_scripting_gjs_get_global (LrgScripting  *scripting,
                              const gchar   *name,
                              GValue        *value,
                              GError       **error)
{
    LrgScriptingGjs     *self = LRG_SCRIPTING_GJS (scripting);
    g_autoptr(GVariant)  args = NULL;
    g_autoptr(GVariant)  result = NULL;
    g_autofree gchar    *status = NULL;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    if (self->context == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "Gjs interpreter not initialized");
        return FALSE;
    }

    args = gjs_pack_args (name, 0, NULL);
    if (!gjs_bridge_op (self, "get", args, &status, &result, error))
    {
        return FALSE;
    }

    if (g_strcmp0 (status, "missing") == 0)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Global '%s' not found", name);
        return FALSE;
    }

    /* Functions and objects exist but have no scalar form */
    if (g_variant_is_of_type (result, G_VARIANT_TYPE_MAYBE) &&
        g_variant_n_children (result) == 0)
    {
        g_value_init (value, G_TYPE_POINTER);
        g_value_set_pointer (value, NULL);
        return TRUE;
    }

    if (!lrg_script_host_variant_to_value (result, value))
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
                     "Cannot convert global '%s'", name);
        return FALSE;
    }

    return TRUE;
}

static gboolean
lrg_scripting_gjs_set_global (LrgScripting  *scripting,
                              const gchar   *name,
                              const GValue  *value,
                              GError       **error)
{
    LrgScriptingGjs     *self = LRG_SCRIPTING_GJS (scripting);
    g_autoptr(GVariant)  args = NULL;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    if (self->context == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "Gjs interpreter not initialized");
        return FALSE;
    }

    args = gjs_pack_args (name, 1, value);
    return gjs_bridge_op (self, "set", args, NULL, NULL, error);
}

static gboolean
lrg_scripting_gjs_has_function (LrgScripting *scripting,
                                const gchar  *func_name)
{
    LrgScriptingGjs     *self = LRG_SCRIPTING_GJS (scripting);
    g_autoptr(GVariant)  args = NULL;
    g_autoptr(GVariant)  result = NULL;
    g_autoptr(GError)    local_error = NULL;

    if (self->context == NULL || !shared_bridge_ok)
    {
        return FALSE;
    }

    args = gjs_pack_args (func_name, 0, NULL);
    if (!gjs_bridge_op (self, "has", args, NULL, &result, &local_error))
    {
        return FALSE;
    }

    return g_variant_is_of_type (result, G_VARIANT_TYPE_BOOLEAN) &&
           g_variant_get_boolean (result);
}

static void
lrg_scripting_gjs_reset (LrgScripting *scripting)
{
    LrgScriptingGjs *self = LRG_SCRIPTING_GJS (scripting);
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (scripting);
    g_autoptr(GError) error = NULL;

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Resetting Gjs scripting context");

    /* Drop this context's scope and bridge slot */
    if (self->context != NULL)
    {
        lrg_scripting_gjs_finalize_interpreter (gi_self);
    }

    lrg_scripting_gi_clear_registered_functions (gi_self);

    /* Reinitialize with a fresh scope */
    if (!lrg_scripting_gjs_init_interpreter (gi_self, &error))
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                     "Failed to reinitialize Gjs interpreter: %s",
                     error->message);
    }
}
