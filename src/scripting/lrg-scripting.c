/* lrg-scripting.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for scripting engine implementations.
 *
 * Besides dispatching the virtual methods, the base class keeps the
 * bookkeeping the 0.9 hot reloader and script debugger need from every
 * backend: a process-wide registry of live contexts (with creation hooks
 * for the debugger's auto-attach and the reloader's watch-all), the list of
 * files each context loaded, its search paths, the location of its last
 * error, and a per-thread stack of the contexts currently executing.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING
#define LRG_SCRIPTING_PRIVATE

#include "lrg-scripting.h"
#include "lrg-script-reloader.h"
#include "../lrg-log.h"

typedef struct
{
    GPtrArray *loaded_files;  /* canonical paths (gchar *), load order */
    GPtrArray *search_paths;  /* canonical paths (gchar *) */
    gchar     *error_file;    /* location of the last error, or NULL */
    gint       error_line;
    GMutex     lock;          /* guards the arrays (read from other threads) */
} LrgScriptingPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgScripting, lrg_scripting, G_TYPE_OBJECT)

/* ==========================================================================
 * Process-wide registry of live contexts
 * ========================================================================== */

typedef struct
{
    LrgScriptingInstanceHook hook;
    gpointer                 user_data;
} InstanceHook;

static GMutex     instances_lock;
static GPtrArray *instances = NULL;       /* LrgScripting * (no ref) */
static GArray    *instance_hooks = NULL;  /* InstanceHook */
static GArray    *path_hooks = NULL;      /* PathHook */

typedef struct
{
    LrgScriptingPathHook hook;
    gpointer             user_data;
} PathHook;

/* per-thread stack of executing contexts (innermost last) */
static GPrivate current_stack = G_PRIVATE_INIT ((GDestroyNotify) g_ptr_array_unref);

/* canonicalize a script path the same way everywhere (strip Lua's "@") */
static gchar *
canonical_path (const gchar *path)
{
    if (path[0] == '@')
        path++;
    return g_canonicalize_filename (path, NULL);
}

/*
 * lrg_scripting_priv_add_instance_hook:
 * @hook: called after every context is constructed
 * @user_data: data for @hook
 *
 * Registers a creation hook (the debugger's auto-attach, the reloader's
 * watch-all).  Hooks run on the constructing thread without the registry
 * lock held.
 */
void
lrg_scripting_priv_add_instance_hook (LrgScriptingInstanceHook hook,
                                      gpointer                 user_data)
{
    InstanceHook entry;

    entry.hook = hook;
    entry.user_data = user_data;
    g_mutex_lock (&instances_lock);
    if (instance_hooks == NULL)
        instance_hooks = g_array_new (FALSE, FALSE, sizeof (InstanceHook));
    g_array_append_val (instance_hooks, entry);
    g_mutex_unlock (&instances_lock);
}

/*
 * lrg_scripting_priv_remove_instance_hook:
 * @hook: a hook given to lrg_scripting_priv_add_instance_hook()
 * @user_data: its data
 *
 * Unregisters a creation hook (first match).
 */
void
lrg_scripting_priv_remove_instance_hook (LrgScriptingInstanceHook hook,
                                         gpointer                 user_data)
{
    guint i;

    g_mutex_lock (&instances_lock);
    for (i = 0; instance_hooks != NULL && i < instance_hooks->len; i++)
    {
        InstanceHook *entry = &g_array_index (instance_hooks, InstanceHook, i);

        if (entry->hook == hook && entry->user_data == user_data)
        {
            g_array_remove_index (instance_hooks, i);
            break;
        }
    }
    g_mutex_unlock (&instances_lock);
}

/* copy the path hooks so a hook may add or remove hooks */
static GArray *
copy_path_hooks (void)
{
    GArray *hooks = NULL;

    g_mutex_lock (&instances_lock);
    if (path_hooks != NULL && path_hooks->len > 0)
    {
        hooks = g_array_sized_new (FALSE, FALSE, sizeof (PathHook), path_hooks->len);
        g_array_append_vals (hooks, path_hooks->data, path_hooks->len);
    }
    g_mutex_unlock (&instances_lock);
    return hooks;
}

static void
fire_path_hooks (LrgScripting *self,
                 const gchar  *path)
{
    g_autoptr(GArray) hooks = NULL;
    guint i;

    hooks = copy_path_hooks ();
    for (i = 0; hooks != NULL && i < hooks->len; i++)
    {
        PathHook *entry = &g_array_index (hooks, PathHook, i);

        entry->hook (self, path, entry->user_data);
    }
}

/*
 * lrg_scripting_priv_add_path_hook:
 * @hook: called with a canonical path a context loaded or will search
 * @user_data: data for @hook
 */
void
lrg_scripting_priv_add_path_hook (LrgScriptingPathHook hook,
                                  gpointer             user_data)
{
    PathHook entry;

    entry.hook = hook;
    entry.user_data = user_data;
    g_mutex_lock (&instances_lock);
    if (path_hooks == NULL)
        path_hooks = g_array_new (FALSE, FALSE, sizeof (PathHook));
    g_array_append_val (path_hooks, entry);
    g_mutex_unlock (&instances_lock);
}

/*
 * lrg_scripting_priv_remove_path_hook:
 * @hook: a hook given to lrg_scripting_priv_add_path_hook()
 * @user_data: its data
 */
void
lrg_scripting_priv_remove_path_hook (LrgScriptingPathHook hook,
                                     gpointer             user_data)
{
    guint i;

    g_mutex_lock (&instances_lock);
    for (i = 0; path_hooks != NULL && i < path_hooks->len; i++)
    {
        PathHook *entry = &g_array_index (path_hooks, PathHook, i);

        if (entry->hook == hook && entry->user_data == user_data)
        {
            g_array_remove_index (path_hooks, i);
            break;
        }
    }
    g_mutex_unlock (&instances_lock);
}

/*
 * lrg_scripting_priv_get_current:
 *
 * Returns: (transfer none) (nullable): the innermost context executing
 *   script code on this thread
 */
LrgScripting *
lrg_scripting_priv_get_current (void)
{
    GPtrArray *stack = g_private_get (&current_stack);

    if (stack == NULL || stack->len == 0)
        return NULL;
    return g_ptr_array_index (stack, stack->len - 1);
}

/*
 * lrg_scripting_priv_push_current:
 * @self: the context about to execute script code
 *
 * Marks @self as executing on this thread (nesting allowed).
 */
void
lrg_scripting_priv_push_current (LrgScripting *self)
{
    GPtrArray *stack = g_private_get (&current_stack);

    if (stack == NULL)
    {
        stack = g_ptr_array_new ();
        g_private_set (&current_stack, stack);
    }
    g_ptr_array_add (stack, self);
}

/*
 * lrg_scripting_priv_pop_current:
 * @self: the context that finished executing
 *
 * Pops @self from this thread's executing stack.
 */
void
lrg_scripting_priv_pop_current (LrgScripting *self)
{
    GPtrArray *stack = g_private_get (&current_stack);

    if (stack == NULL || stack->len == 0)
        return;
    if (g_ptr_array_index (stack, stack->len - 1) == self)
        g_ptr_array_set_size (stack, stack->len - 1);
}

/*
 * lrg_scripting_priv_set_error_location:
 * @self: an #LrgScripting
 * @file: (nullable): the failing file, or %NULL to clear
 * @line: the failing line (0 when unknown)
 *
 * Records where the last error happened (backends call this when they
 * parse "file:line:" out of an interpreter message).
 */
void
lrg_scripting_priv_set_error_location (LrgScripting *self,
                                       const gchar  *file,
                                       gint          line)
{
    LrgScriptingPrivate *priv = lrg_scripting_get_instance_private (self);

    g_mutex_lock (&priv->lock);
    g_clear_pointer (&priv->error_file, g_free);
    if (file != NULL)
        priv->error_file = canonical_path (file);
    priv->error_line = file != NULL ? line : 0;
    g_mutex_unlock (&priv->lock);
}

/*
 * lrg_scripting_priv_note_loaded:
 * @self: an #LrgScripting
 * @path: a file the context executed
 *
 * Adds @path (canonicalized) to the loaded-files list once.
 */
void
lrg_scripting_priv_note_loaded (LrgScripting *self,
                                const gchar  *path)
{
    LrgScriptingPrivate *priv = lrg_scripting_get_instance_private (self);
    g_autofree gchar *canon = canonical_path (path);
    const gchar *stored = NULL;
    guint i;

    g_mutex_lock (&priv->lock);
    for (i = 0; i < priv->loaded_files->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (priv->loaded_files, i), canon) == 0)
        {
            g_mutex_unlock (&priv->lock);
            return;
        }
    }
    stored = canon;
    g_ptr_array_add (priv->loaded_files, g_steal_pointer (&canon));
    g_mutex_unlock (&priv->lock);

    fire_path_hooks (self, stored);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

/* register the finished context and run the creation hooks */
static void
lrg_scripting_constructed (GObject *object)
{
    LrgScripting *self = LRG_SCRIPTING (object);
    g_autoptr(GArray) hooks = NULL;
    guint i;

    G_OBJECT_CLASS (lrg_scripting_parent_class)->constructed (object);

    g_mutex_lock (&instances_lock);
    if (instances == NULL)
        instances = g_ptr_array_new ();
    g_ptr_array_add (instances, self);
    /* copy the hooks so a hook may add or remove hooks */
    if (instance_hooks != NULL && instance_hooks->len > 0)
    {
        hooks = g_array_sized_new (FALSE, FALSE, sizeof (InstanceHook), instance_hooks->len);
        g_array_append_vals (hooks, instance_hooks->data, instance_hooks->len);
    }
    g_mutex_unlock (&instances_lock);

    for (i = 0; hooks != NULL && i < hooks->len; i++)
    {
        InstanceHook *entry = &g_array_index (hooks, InstanceHook, i);

        entry->hook (self, entry->user_data);
    }
}

static void
lrg_scripting_finalize (GObject *object)
{
    LrgScripting *self = LRG_SCRIPTING (object);
    LrgScriptingPrivate *priv = lrg_scripting_get_instance_private (self);

    g_mutex_lock (&instances_lock);
    if (instances != NULL)
        g_ptr_array_remove (instances, self);
    g_mutex_unlock (&instances_lock);

    g_clear_pointer (&priv->loaded_files, g_ptr_array_unref);
    g_clear_pointer (&priv->search_paths, g_ptr_array_unref);
    g_clear_pointer (&priv->error_file, g_free);
    g_mutex_clear (&priv->lock);

    G_OBJECT_CLASS (lrg_scripting_parent_class)->finalize (object);
}

/*
 * Default reload: hand the state over as a GValue through call_function()
 * and re-run the file with load_file().  Backends with a native state
 * representation (Lua, Python) override this with a rollback-capable
 * version.
 */
static gboolean
lrg_scripting_real_reload_file (LrgScripting  *self,
                                const gchar   *path,
                                GError       **error)
{
    GValue state = G_VALUE_INIT;
    gboolean have_state = FALSE;
    gboolean ok;

    /* save: on_before_reload() -> state (failures are not fatal) */
    if (lrg_scripting_has_function (self, LRG_SCRIPT_HOOK_BEFORE_RELOAD))
    {
        g_autoptr(GError) save_error = NULL;

        if (lrg_scripting_call_function (self, LRG_SCRIPT_HOOK_BEFORE_RELOAD,
                                         &state, 0, NULL, &save_error))
            have_state = G_IS_VALUE (&state);
        else
            lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "%s: %s failed: %s", path,
                       LRG_SCRIPT_HOOK_BEFORE_RELOAD, save_error->message);
    }

    ok = lrg_scripting_load_file (self, path, error);

    /* restore: on_reload(state) */
    if (ok && lrg_scripting_has_function (self, LRG_SCRIPT_HOOK_RELOAD))
    {
        ok = lrg_scripting_call_function (self, LRG_SCRIPT_HOOK_RELOAD, NULL,
                                          have_state ? 1 : 0,
                                          have_state ? &state : NULL, error);
    }

    if (have_state)
        g_value_unset (&state);
    return ok;
}

static void
lrg_scripting_class_init (LrgScriptingClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = lrg_scripting_constructed;
    object_class->finalize = lrg_scripting_finalize;

    /*
     * Abstract class - the virtual methods are implemented by subclasses;
     * only reload_file has a generic default.
     */
    klass->load_file = NULL;
    klass->load_string = NULL;
    klass->call_function = NULL;
    klass->register_function = NULL;
    klass->get_global = NULL;
    klass->set_global = NULL;
    klass->reset = NULL;
    klass->reload_file = lrg_scripting_real_reload_file;
}

static void
lrg_scripting_init (LrgScripting *self)
{
    LrgScriptingPrivate *priv = lrg_scripting_get_instance_private (self);

    g_mutex_init (&priv->lock);
    priv->loaded_files = g_ptr_array_new_with_free_func (g_free);
    priv->search_paths = g_ptr_array_new_with_free_func (g_free);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
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
    gboolean           ok;

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

    lrg_scripting_priv_push_current (self);
    ok = klass->load_file (self, path, error);
    lrg_scripting_priv_pop_current (self);

    /* remember the file even when it failed: a fixed file reloads it */
    if (g_file_test (path, G_FILE_TEST_EXISTS))
        lrg_scripting_priv_note_loaded (self, path);
    return ok;
}

/*
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
    gboolean           ok;

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

    lrg_scripting_priv_push_current (self);
    ok = klass->load_string (self, name, code, error);
    lrg_scripting_priv_pop_current (self);
    return ok;
}

/*
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
    gboolean           ok;

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

    lrg_scripting_priv_push_current (self);
    ok = klass->call_function (self, func_name, return_value, n_args, args, error);
    lrg_scripting_priv_pop_current (self);
    return ok;
}

/*
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

/*
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

/*
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

/*
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

    /* a reset context has executed nothing */
    {
        LrgScriptingPrivate *priv = lrg_scripting_get_instance_private (self);

        g_mutex_lock (&priv->lock);
        g_ptr_array_set_size (priv->loaded_files, 0);
        g_mutex_unlock (&priv->lock);
    }
}

/*
 * lrg_scripting_add_search_path:
 * @self: an #LrgScripting
 * @path: (type filename): a directory
 *
 * Add a directory the backend searches when a script imports or includes
 * another file.
 */
void
lrg_scripting_add_search_path (LrgScripting *self,
                               const gchar  *path)
{
    LrgScriptingClass *klass;

    g_return_if_fail (LRG_IS_SCRIPTING (self));
    g_return_if_fail (path != NULL);

    klass = LRG_SCRIPTING_GET_CLASS (self);

    /* Backends without imports simply have nothing to extend */
    if (klass->add_search_path != NULL)
    {
        klass->add_search_path (self, path);
    }

    /* remember it for the reloader (watched like loaded files) */
    {
        LrgScriptingPrivate *priv = lrg_scripting_get_instance_private (self);
        g_autofree gchar *canon = canonical_path (path);
        guint i;
        gboolean known = FALSE;

        const gchar *stored = NULL;

        g_mutex_lock (&priv->lock);
        for (i = 0; i < priv->search_paths->len; i++)
            known = known || g_strcmp0 (g_ptr_array_index (priv->search_paths, i), canon) == 0;
        if (!known)
        {
            stored = canon;
            g_ptr_array_add (priv->search_paths, g_steal_pointer (&canon));
        }
        g_mutex_unlock (&priv->lock);

        if (stored != NULL)
            fire_path_hooks (self, stored);
    }
}

/*
 * lrg_scripting_has_function:
 * @self: an #LrgScripting
 * @func_name: a function name
 *
 * Check whether the loaded scripts define a callable @func_name.
 *
 * Returns: %TRUE when @func_name can be called
 */
gboolean
lrg_scripting_has_function (LrgScripting *self,
                            const gchar  *func_name)
{
    LrgScriptingClass *klass;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);
    g_return_val_if_fail (func_name != NULL, FALSE);

    klass = LRG_SCRIPTING_GET_CLASS (self);

    if (klass->has_function == NULL)
    {
        return FALSE;
    }

    return klass->has_function (self, func_name);
}

/*
 * Pull "File \"PATH\", line N" (Python) or "PATH:N:" out of a backend
 * message when the backend did not record a line itself.
 */
static void
capture_error_location (LrgScripting *self,
                        const gchar  *path,
                        const GError *error)
{
    gchar *existing = NULL;
    gint line = 0;
    const gchar *msg;
    const gchar *marker;

    if (error == NULL || error->message == NULL)
        return;

    if (lrg_scripting_get_error_location (self, &existing, &line) && line > 0)
    {
        g_free (existing);
        return;
    }
    g_free (existing);

    msg = error->message;
    marker = strstr (msg, "File \"");
    if (marker != NULL)
    {
        const gchar *start = marker + 6;
        const gchar *end = strchr (start, '"');
        const gchar *lp;

        if (end != NULL)
        {
            g_autofree gchar *file = g_strndup (start, (gsize) (end - start));

            lp = strstr (end, "line ");
            if (lp != NULL && g_ascii_isdigit (lp[5]))
            {
                lrg_scripting_priv_set_error_location (self, file, atoi (lp + 5));
                return;
            }
        }
    }

    /* PATH:N: — the first colon that introduces a positive line number. */
    {
        const gchar *cursor = msg;

        while ((cursor = strchr (cursor, ':')) != NULL)
        {
            const gchar *digit = cursor + 1;
            const gchar *start;
            gint n = 0;

            if (!g_ascii_isdigit (*digit))
            {
                cursor++;
                continue;
            }
            while (g_ascii_isdigit (*digit))
            {
                n = n * 10 + (*digit - '0');
                digit++;
            }
            if (*digit != ':' || n <= 0)
            {
                cursor++;
                continue;
            }
            start = cursor;
            while (start > msg &&
                   start[-1] != ' ' && start[-1] != '\'' && start[-1] != '"')
                start--;
            {
                g_autofree gchar *file = g_strndup (start, (gsize) (cursor - start));

                lrg_scripting_priv_set_error_location (self, file, n);
                return;
            }
        }
    }

    lrg_scripting_priv_set_error_location (self, path, 0);
}

/*
 * lrg_scripting_reload_file:
 * @self: an #LrgScripting
 * @path: (type filename): a script file
 * @error: (nullable): return location for error
 *
 * Hot-reloads @path in this live context (see the header docs).
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_scripting_reload_file (LrgScripting  *self,
                           const gchar   *path,
                           GError       **error)
{
    LrgScriptingClass *klass;
    gboolean           ok;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    klass = LRG_SCRIPTING_GET_CLASS (self);
    lrg_scripting_priv_set_error_location (self, NULL, 0);

    lrg_scripting_priv_push_current (self);
    ok = klass->reload_file (self, path, error);
    lrg_scripting_priv_pop_current (self);

    if (!ok)
        capture_error_location (self, path, error != NULL ? *error : NULL);
    return ok;
}

/* copy a guarded string array into a NULL-terminated strv */
static gchar **
dup_strv (LrgScripting *self,
          GPtrArray    *array)
{
    LrgScriptingPrivate *priv = lrg_scripting_get_instance_private (self);
    GStrvBuilder *builder = g_strv_builder_new ();
    gchar **result;
    guint i;

    g_mutex_lock (&priv->lock);
    for (i = 0; i < array->len; i++)
        g_strv_builder_add (builder, g_ptr_array_index (array, i));
    g_mutex_unlock (&priv->lock);

    result = g_strv_builder_end (builder);
    g_strv_builder_unref (builder);
    return result;
}

/*
 * lrg_scripting_get_loaded_files:
 * @self: an #LrgScripting
 *
 * Returns: (transfer full): the canonical files this context loaded
 */
gchar **
lrg_scripting_get_loaded_files (LrgScripting *self)
{
    LrgScriptingPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), NULL);
    priv = lrg_scripting_get_instance_private (self);
    return dup_strv (self, priv->loaded_files);
}

/*
 * lrg_scripting_has_loaded_file:
 * @self: an #LrgScripting
 * @path: a path
 *
 * Returns: whether this context loaded @path
 */
gboolean
lrg_scripting_has_loaded_file (LrgScripting *self,
                               const gchar  *path)
{
    LrgScriptingPrivate *priv;
    g_autofree gchar *canon = NULL;
    gboolean found = FALSE;
    guint i;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    priv = lrg_scripting_get_instance_private (self);
    canon = canonical_path (path);
    g_mutex_lock (&priv->lock);
    for (i = 0; i < priv->loaded_files->len && !found; i++)
        found = g_strcmp0 (g_ptr_array_index (priv->loaded_files, i), canon) == 0;
    g_mutex_unlock (&priv->lock);
    return found;
}

/*
 * lrg_scripting_get_search_paths:
 * @self: an #LrgScripting
 *
 * Returns: (transfer full): the canonical search paths
 */
gchar **
lrg_scripting_get_search_paths (LrgScripting *self)
{
    LrgScriptingPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), NULL);
    priv = lrg_scripting_get_instance_private (self);
    return dup_strv (self, priv->search_paths);
}

/*
 * lrg_scripting_get_error_location:
 * @self: an #LrgScripting
 * @file: (out) (optional): the failing file
 * @line: (out) (optional): the failing line
 *
 * Returns: %TRUE when a location is known
 */
gboolean
lrg_scripting_get_error_location (LrgScripting  *self,
                                  gchar        **file,
                                  gint          *line)
{
    LrgScriptingPrivate *priv;
    gboolean known;

    g_return_val_if_fail (LRG_IS_SCRIPTING (self), FALSE);

    priv = lrg_scripting_get_instance_private (self);
    g_mutex_lock (&priv->lock);
    known = priv->error_file != NULL;
    if (file != NULL)
        *file = g_strdup (priv->error_file);
    if (line != NULL)
        *line = priv->error_line;
    g_mutex_unlock (&priv->lock);
    return known;
}

/*
 * lrg_scripting_list_instances:
 *
 * Returns: (transfer full): every live context (each ref'd)
 */
GPtrArray *
lrg_scripting_list_instances (void)
{
    GPtrArray *result = g_ptr_array_new_with_free_func (g_object_unref);
    guint i;

    g_mutex_lock (&instances_lock);
    for (i = 0; instances != NULL && i < instances->len; i++)
    {
        GObject *object = g_ptr_array_index (instances, i);

        /* skip contexts already being finalized (ref count 0) */
        if (g_atomic_int_get ((gint *) &object->ref_count) > 0)
            g_ptr_array_add (result, g_object_ref (object));
    }
    g_mutex_unlock (&instances_lock);
    return result;
}
