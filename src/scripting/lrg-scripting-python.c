/* lrg-scripting-python.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Python scripting backend implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-scripting-python.h"
#include "lrg-scripting-python-private.h"
#include "lrg-python-bridge.h"
#include "lrg-python-api.h"
#include "../lrg-log.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"

struct _LrgScriptingPython
{
    LrgScripting  parent_instance;

    PyObject     *main_module;      /* __main__ module */
    PyObject     *main_dict;        /* __main__.__dict__ (globals) */
    PyObject     *libregnum_module; /* Built-in 'libregnum' module */
    LrgRegistry  *registry;         /* Type registry (weak ref) */
    LrgEngine    *engine;           /* Engine (weak ref) */
    GPtrArray    *update_hooks;     /* Array of function names (gchar *) */
    GPtrArray    *search_paths;     /* Array of search paths (gchar *) */
    GHashTable   *registered_funcs; /* name -> RegisteredCFunction mapping */
    gboolean      initialized;      /* Whether Python is initialized */
};

G_DEFINE_TYPE (LrgScriptingPython, lrg_scripting_python, LRG_TYPE_SCRIPTING)

/* Global flag to track if Python has been initialized */
static gboolean g_python_initialized = FALSE;

/* ==========================================================================
 * Private Functions
 * ========================================================================== */

/*
 * Initialize Python if not already done.
 */
static gboolean
ensure_python_initialized (LrgScriptingPython *self)
{
    if (self->initialized)
    {
        return TRUE;
    }

    if (!g_python_initialized)
    {
        Py_Initialize ();
        g_python_initialized = TRUE;
    }

    /* Register GObject wrapper type */
    if (!lrg_python_register_gobject_type ())
    {
        lrg_error (LRG_LOG_DOMAIN_SCRIPTING, "Failed to register GObject type");
        return FALSE;
    }

    /* Register BoundMethod type for LrgScriptable methods */
    if (!lrg_python_register_bound_method_type ())
    {
        lrg_error (LRG_LOG_DOMAIN_SCRIPTING, "Failed to register BoundMethod type");
        return FALSE;
    }

    /* Get __main__ module */
    self->main_module = PyImport_AddModule ("__main__");
    if (self->main_module == NULL)
    {
        lrg_error (LRG_LOG_DOMAIN_SCRIPTING, "Failed to get __main__ module");
        return FALSE;
    }
    Py_INCREF (self->main_module);

    /* Get __main__.__dict__ */
    self->main_dict = PyModule_GetDict (self->main_module);
    if (self->main_dict == NULL)
    {
        lrg_error (LRG_LOG_DOMAIN_SCRIPTING, "Failed to get __main__.__dict__");
        return FALSE;
    }
    Py_INCREF (self->main_dict);

    /* Register built-in API */
    lrg_python_api_register_all (self);

    self->initialized = TRUE;

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Python interpreter initialized");

    return TRUE;
}

/*
 * Update sys.path with custom search paths.
 */
static void
update_search_paths (LrgScriptingPython *self)
{
    PyObject *sys_path;
    guint     i;

    if (!self->initialized)
    {
        return;
    }

    sys_path = PySys_GetObject ("path");
    if (sys_path == NULL || !PyList_Check (sys_path))
    {
        return;
    }

    /* Add custom paths at the beginning */
    for (i = 0; i < self->search_paths->len; i++)
    {
        const gchar *path = g_ptr_array_index (self->search_paths, i);
        PyObject    *path_str = PyUnicode_FromString (path);

        if (path_str != NULL)
        {
            /* Only add if not already in path */
            if (!PySequence_Contains (sys_path, path_str))
            {
                PyList_Insert (sys_path, 0, path_str);
            }
            Py_DECREF (path_str);
        }
    }
}

/*
 * C function wrapper for Python.
 */
static PyObject *
c_function_wrapper (PyObject *capsule, PyObject *args)
{
    RegisteredCFunction *reg;
    GValue              *gargs = NULL;
    GValue               return_value = G_VALUE_INIT;
    g_autoptr(GError)    error = NULL;
    Py_ssize_t           n_args;
    Py_ssize_t           i;
    gboolean             success;
    PyObject            *result = NULL;

    /* Get the registered function data from capsule */
    reg = (RegisteredCFunction *)PyCapsule_GetPointer (capsule, "RegisteredCFunction");
    if (reg == NULL || reg->func == NULL)
    {
        PyErr_SetString (PyExc_RuntimeError, "Invalid C function registration");
        return NULL;
    }

    /* Get arguments */
    n_args = PyTuple_Size (args);

    if (n_args > 0)
    {
        gargs = g_new0 (GValue, n_args);

        for (i = 0; i < n_args; i++)
        {
            PyObject *arg = PyTuple_GetItem (args, i);
            if (!lrg_python_to_gvalue (arg, &gargs[i]))
            {
                Py_ssize_t j;
                for (j = 0; j < i; j++)
                {
                    g_value_unset (&gargs[j]);
                }
                g_free (gargs);
                PyErr_Format (PyExc_TypeError, "Cannot convert argument %zd", i + 1);
                return NULL;
            }
        }
    }

    /* Call the C function */
    success = reg->func (LRG_SCRIPTING (reg->scripting),
                         (guint)n_args,
                         gargs,
                         &return_value,
                         reg->user_data,
                         &error);

    /* Clean up arguments */
    for (i = 0; i < n_args; i++)
    {
        g_value_unset (&gargs[i]);
    }
    g_free (gargs);

    if (!success)
    {
        const gchar *msg = error ? error->message : "Unknown error";
        PyErr_SetString (PyExc_RuntimeError, msg);
        return NULL;
    }

    /* Convert return value */
    if (G_IS_VALUE (&return_value))
    {
        result = lrg_python_from_gvalue (&return_value);
        g_value_unset (&return_value);
        return result;
    }

    Py_RETURN_NONE;
}

/*
 * Free a RegisteredCFunction.
 */
static void
registered_func_free (gpointer data)
{
    RegisteredCFunction *reg = (RegisteredCFunction *)data;
    g_free (reg->name);
    g_free (reg);
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_scripting_python_load_file (LrgScripting  *scripting,
                                const gchar   *path,
                                GError       **error)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (scripting);
    FILE               *fp;
    PyObject           *result;

    g_return_val_if_fail (path != NULL, FALSE);

    if (!ensure_python_initialized (self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Python interpreter not initialized");
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loading Python script: %s", path);

    fp = fopen (path, "r");
    if (fp == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_LOAD,
                     "Failed to open file: %s",
                     path);
        return FALSE;
    }

    result = PyRun_FileEx (fp, path, Py_file_input,
                           self->main_dict, self->main_dict, 1);

    if (result == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     PyErr_ExceptionMatches (PyExc_SyntaxError)
                         ? LRG_SCRIPTING_ERROR_SYNTAX
                         : LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error in '%s': %s",
                     path, msg ? msg : "(unknown error)");
        return FALSE;
    }

    Py_DECREF (result);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loaded Python script: %s", path);

    return TRUE;
}

static gboolean
lrg_scripting_python_load_string (LrgScripting  *scripting,
                                  const gchar   *name,
                                  const gchar   *code,
                                  GError       **error)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (scripting);
    PyObject           *compiled;
    PyObject           *result;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    if (!ensure_python_initialized (self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Python interpreter not initialized");
        return FALSE;
    }

    /* Compile the code */
    compiled = Py_CompileString (code, name, Py_file_input);
    if (compiled == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_SYNTAX,
                     "Syntax error in '%s': %s",
                     name, msg ? msg : "(unknown error)");
        return FALSE;
    }

    /* Execute the compiled code */
    result = PyEval_EvalCode (compiled, self->main_dict, self->main_dict);
    Py_DECREF (compiled);

    if (result == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error executing '%s': %s",
                     name, msg ? msg : "(unknown error)");
        return FALSE;
    }

    Py_DECREF (result);

    return TRUE;
}

static gboolean
lrg_scripting_python_call_function (LrgScripting  *scripting,
                                    const gchar   *func_name,
                                    GValue        *return_value,
                                    guint          n_args,
                                    const GValue  *args,
                                    GError       **error)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (scripting);
    PyObject           *func;
    PyObject           *py_args;
    PyObject           *result;
    guint               i;

    g_return_val_if_fail (func_name != NULL, FALSE);

    if (!ensure_python_initialized (self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Python interpreter not initialized");
        return FALSE;
    }

    /* Get the function */
    func = PyDict_GetItemString (self->main_dict, func_name);
    if (func == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Function '%s' not found",
                     func_name);
        return FALSE;
    }

    if (!PyCallable_Check (func))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_TYPE,
                     "'%s' is not callable",
                     func_name);
        return FALSE;
    }

    /* Build arguments tuple */
    py_args = PyTuple_New ((Py_ssize_t)n_args);
    if (py_args == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Failed to create arguments tuple");
        return FALSE;
    }

    for (i = 0; i < n_args; i++)
    {
        PyObject *arg = lrg_python_from_gvalue (&args[i]);
        if (arg == NULL)
        {
            arg = Py_None;
            Py_INCREF (arg);
        }
        PyTuple_SET_ITEM (py_args, (Py_ssize_t)i, arg);
    }

    /* Call the function */
    result = PyObject_CallObject (func, py_args);
    Py_DECREF (py_args);

    if (result == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error calling '%s': %s",
                     func_name, msg ? msg : "(unknown error)");
        return FALSE;
    }

    /* Get return value if requested */
    if (return_value != NULL && result != Py_None)
    {
        if (!lrg_python_to_gvalue (result, return_value))
        {
            g_set_error (error,
                         LRG_SCRIPTING_ERROR,
                         LRG_SCRIPTING_ERROR_TYPE,
                         "Cannot convert return value from '%s'",
                         func_name);
            Py_DECREF (result);
            return FALSE;
        }
    }

    Py_DECREF (result);

    return TRUE;
}

static gboolean
lrg_scripting_python_register_function (LrgScripting           *scripting,
                                        const gchar            *name,
                                        LrgScriptingCFunction   func,
                                        gpointer                user_data,
                                        GError                **error)
{
    LrgScriptingPython  *self = LRG_SCRIPTING_PYTHON (scripting);
    RegisteredCFunction *reg;
    PyObject            *capsule;
    PyObject            *py_func;
    static PyMethodDef   method_def = {"", NULL, METH_VARARGS, NULL};

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (func != NULL, FALSE);

    (void)error;

    if (!ensure_python_initialized (self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Python interpreter not initialized");
        return FALSE;
    }

    /* Create registration data */
    reg = g_new0 (RegisteredCFunction, 1);
    reg->scripting = self;
    reg->func = func;
    reg->user_data = user_data;
    reg->name = g_strdup (name);

    /* Store in hash table for cleanup */
    g_hash_table_insert (self->registered_funcs, g_strdup (name), reg);

    /* Create a PyCapsule to hold the registration data */
    capsule = PyCapsule_New (reg, "RegisteredCFunction", NULL);
    if (capsule == NULL)
    {
        return FALSE;
    }

    /* Create a Python function */
    method_def.ml_name = name;
    method_def.ml_meth = (PyCFunction)c_function_wrapper;

    py_func = PyCFunction_New (&method_def, capsule);
    Py_DECREF (capsule);

    if (py_func == NULL)
    {
        return FALSE;
    }

    /* Set as global */
    PyDict_SetItemString (self->main_dict, name, py_func);
    Py_DECREF (py_func);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Registered C function: %s", name);

    return TRUE;
}

static gboolean
lrg_scripting_python_get_global (LrgScripting  *scripting,
                                 const gchar   *name,
                                 GValue        *value,
                                 GError       **error)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (scripting);
    PyObject           *obj;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    if (!ensure_python_initialized (self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Python interpreter not initialized");
        return FALSE;
    }

    obj = PyDict_GetItemString (self->main_dict, name);
    if (obj == NULL || obj == Py_None)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Global '%s' not found",
                     name);
        return FALSE;
    }

    if (!lrg_python_to_gvalue (obj, value))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_TYPE,
                     "Cannot convert global '%s' to GValue",
                     name);
        return FALSE;
    }

    return TRUE;
}

static gboolean
lrg_scripting_python_set_global (LrgScripting  *scripting,
                                 const gchar   *name,
                                 const GValue  *value,
                                 GError       **error)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (scripting);
    PyObject           *obj;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    (void)error;

    if (!ensure_python_initialized (self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Python interpreter not initialized");
        return FALSE;
    }

    obj = lrg_python_from_gvalue (value);
    if (obj == NULL)
    {
        obj = Py_None;
        Py_INCREF (obj);
    }

    PyDict_SetItemString (self->main_dict, name, obj);
    Py_DECREF (obj);

    return TRUE;
}

static void
lrg_scripting_python_reset (LrgScripting *scripting)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (scripting);

    /* Clear update hooks */
    g_ptr_array_set_size (self->update_hooks, 0);

    /* Clear registered functions */
    g_hash_table_remove_all (self->registered_funcs);

    if (self->initialized && self->main_dict != NULL)
    {
        /* Clear globals but keep builtins */
        PyObject *builtins = PyDict_GetItemString (self->main_dict, "__builtins__");
        PyObject *name = PyDict_GetItemString (self->main_dict, "__name__");

        if (builtins != NULL)
            Py_INCREF (builtins);
        if (name != NULL)
            Py_INCREF (name);

        PyDict_Clear (self->main_dict);

        if (builtins != NULL)
        {
            PyDict_SetItemString (self->main_dict, "__builtins__", builtins);
            Py_DECREF (builtins);
        }
        if (name != NULL)
        {
            PyDict_SetItemString (self->main_dict, "__name__", name);
            Py_DECREF (name);
        }

        /* Re-register API */
        lrg_python_api_register_all (self);
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Python script context reset");
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scripting_python_finalize (GObject *object)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (object);

    g_clear_pointer (&self->update_hooks, g_ptr_array_unref);
    g_clear_pointer (&self->search_paths, g_ptr_array_unref);
    g_clear_pointer (&self->registered_funcs, g_hash_table_unref);

    if (self->libregnum_module != NULL)
    {
        Py_DECREF (self->libregnum_module);
        self->libregnum_module = NULL;
    }

    if (self->main_dict != NULL)
    {
        Py_DECREF (self->main_dict);
        self->main_dict = NULL;
    }

    if (self->main_module != NULL)
    {
        Py_DECREF (self->main_module);
        self->main_module = NULL;
    }

    /* Note: We don't call Py_Finalize() as it can cause issues
     * and Python should stay initialized for the process lifetime */

    G_OBJECT_CLASS (lrg_scripting_python_parent_class)->finalize (object);
}

static void
lrg_scripting_python_class_init (LrgScriptingPythonClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    LrgScriptingClass *scripting_class = LRG_SCRIPTING_CLASS (klass);

    object_class->finalize = lrg_scripting_python_finalize;

    /* Override virtual methods */
    scripting_class->load_file = lrg_scripting_python_load_file;
    scripting_class->load_string = lrg_scripting_python_load_string;
    scripting_class->call_function = lrg_scripting_python_call_function;
    scripting_class->register_function = lrg_scripting_python_register_function;
    scripting_class->get_global = lrg_scripting_python_get_global;
    scripting_class->set_global = lrg_scripting_python_set_global;
    scripting_class->reset = lrg_scripting_python_reset;
}

static void
lrg_scripting_python_init (LrgScriptingPython *self)
{
    self->main_module = NULL;
    self->main_dict = NULL;
    self->libregnum_module = NULL;
    self->registry = NULL;
    self->engine = NULL;
    self->update_hooks = g_ptr_array_new_with_free_func (g_free);
    self->search_paths = g_ptr_array_new_with_free_func (g_free);
    self->registered_funcs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                     g_free, registered_func_free);
    self->initialized = FALSE;

    /* Initialize Python */
    ensure_python_initialized (self);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_scripting_python_new:
 *
 * Creates a new Python scripting context.
 *
 * Returns: (transfer full): a new #LrgScriptingPython
 */
LrgScriptingPython *
lrg_scripting_python_new (void)
{
    return g_object_new (LRG_TYPE_SCRIPTING_PYTHON, NULL);
}

/**
 * lrg_scripting_python_set_registry:
 * @self: an #LrgScriptingPython
 * @registry: (nullable): the #LrgRegistry for type lookups
 *
 * Sets the registry used to expose types to Python.
 */
void
lrg_scripting_python_set_registry (LrgScriptingPython *self,
                                   LrgRegistry        *registry)
{
    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (self));
    g_return_if_fail (registry == NULL || LRG_IS_REGISTRY (registry));

    self->registry = registry;

    if (self->initialized)
    {
        lrg_python_api_update_registry (self, registry);
    }
}

/**
 * lrg_scripting_python_get_registry:
 * @self: an #LrgScriptingPython
 *
 * Gets the registry used for type lookups.
 *
 * Returns: (transfer none) (nullable): the #LrgRegistry
 */
LrgRegistry *
lrg_scripting_python_get_registry (LrgScriptingPython *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_PYTHON (self), NULL);

    return self->registry;
}

/**
 * lrg_scripting_python_add_search_path:
 * @self: an #LrgScriptingPython
 * @path: (type filename): directory path to add
 *
 * Adds a directory to the Python import search path.
 */
void
lrg_scripting_python_add_search_path (LrgScriptingPython *self,
                                      const gchar        *path)
{
    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (self));
    g_return_if_fail (path != NULL);

    g_ptr_array_add (self->search_paths, g_strdup (path));
    update_search_paths (self);
}

/**
 * lrg_scripting_python_clear_search_paths:
 * @self: an #LrgScriptingPython
 *
 * Clears all custom search paths.
 */
void
lrg_scripting_python_clear_search_paths (LrgScriptingPython *self)
{
    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (self));

    g_ptr_array_set_size (self->search_paths, 0);
}

/**
 * lrg_scripting_python_register_update_hook:
 * @self: an #LrgScriptingPython
 * @func_name: name of the Python function to call on update
 *
 * Registers a Python function to be called each frame.
 */
void
lrg_scripting_python_register_update_hook (LrgScriptingPython *self,
                                           const gchar        *func_name)
{
    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (self));
    g_return_if_fail (func_name != NULL);

    g_ptr_array_add (self->update_hooks, g_strdup (func_name));

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Registered update hook: %s", func_name);
}

/**
 * lrg_scripting_python_unregister_update_hook:
 * @self: an #LrgScriptingPython
 * @func_name: name of the Python function to unregister
 *
 * Unregisters a previously registered update hook.
 *
 * Returns: %TRUE if the hook was found and removed
 */
gboolean
lrg_scripting_python_unregister_update_hook (LrgScriptingPython *self,
                                             const gchar        *func_name)
{
    guint i;

    g_return_val_if_fail (LRG_IS_SCRIPTING_PYTHON (self), FALSE);
    g_return_val_if_fail (func_name != NULL, FALSE);

    for (i = 0; i < self->update_hooks->len; i++)
    {
        const gchar *name = g_ptr_array_index (self->update_hooks, i);
        if (g_strcmp0 (name, func_name) == 0)
        {
            g_ptr_array_remove_index (self->update_hooks, i);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_scripting_python_clear_update_hooks:
 * @self: an #LrgScriptingPython
 *
 * Clears all registered update hooks.
 */
void
lrg_scripting_python_clear_update_hooks (LrgScriptingPython *self)
{
    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (self));

    g_ptr_array_set_size (self->update_hooks, 0);
}

/**
 * lrg_scripting_python_update:
 * @self: an #LrgScriptingPython
 * @delta: time since last frame in seconds
 *
 * Calls all registered update hooks.
 */
void
lrg_scripting_python_update (LrgScriptingPython *self,
                             gfloat              delta)
{
    guint i;

    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (self));

    if (!self->initialized)
    {
        return;
    }

    for (i = 0; i < self->update_hooks->len; i++)
    {
        const gchar *func_name = g_ptr_array_index (self->update_hooks, i);
        PyObject    *func;
        PyObject    *args;
        PyObject    *result;

        /* Get the function */
        func = PyDict_GetItemString (self->main_dict, func_name);
        if (func == NULL || !PyCallable_Check (func))
        {
            lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                         "Update hook '%s' is not callable",
                         func_name);
            continue;
        }

        /* Build arguments */
        args = Py_BuildValue ("(f)", (double)delta);
        if (args == NULL)
        {
            lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                         "Failed to build arguments for update hook '%s'",
                         func_name);
            continue;
        }

        /* Call the function */
        result = PyObject_CallObject (func, args);
        Py_DECREF (args);

        if (result == NULL)
        {
            g_autofree gchar *msg = lrg_python_get_error_message ();
            lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                         "Update hook '%s' error: %s",
                         func_name, msg ? msg : "(unknown)");
        }
        else
        {
            Py_DECREF (result);
        }
    }
}

/**
 * lrg_scripting_python_set_engine:
 * @self: an #LrgScriptingPython
 * @engine: (nullable): the #LrgEngine to expose to scripts
 *
 * Sets the engine instance to expose to Python.
 */
void
lrg_scripting_python_set_engine (LrgScriptingPython *self,
                                 LrgEngine          *engine)
{
    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (self));
    g_return_if_fail (engine == NULL || LRG_IS_ENGINE (engine));

    self->engine = engine;

    if (self->initialized)
    {
        lrg_python_api_update_engine (self, engine);
    }
}

/**
 * lrg_scripting_python_get_engine:
 * @self: an #LrgScriptingPython
 *
 * Gets the engine instance exposed to Python.
 *
 * Returns: (transfer none) (nullable): the #LrgEngine
 */
LrgEngine *
lrg_scripting_python_get_engine (LrgScriptingPython *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_PYTHON (self), NULL);

    return self->engine;
}

/* ==========================================================================
 * Internal API (for lrg-python-api.c)
 * ========================================================================== */

/**
 * lrg_scripting_python_get_main_dict:
 * @self: an #LrgScriptingPython
 *
 * Gets the main module's __dict__ (globals dictionary).
 *
 * Returns: (transfer none): the main dict PyObject
 */
PyObject *
lrg_scripting_python_get_main_dict (LrgScriptingPython *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_PYTHON (self), NULL);

    return self->main_dict;
}

/**
 * lrg_scripting_python_get_module:
 * @self: an #LrgScriptingPython
 *
 * Gets the libregnum module.
 *
 * Returns: (transfer none): the libregnum module PyObject
 */
PyObject *
lrg_scripting_python_get_module (LrgScriptingPython *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_PYTHON (self), NULL);

    return self->libregnum_module;
}
