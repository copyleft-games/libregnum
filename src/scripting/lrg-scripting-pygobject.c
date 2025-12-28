/* lrg-scripting-pygobject.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * PyGObject-based Python scripting backend implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

/* Python.h must be included before any standard headers */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "lrg-scripting-pygobject.h"
#include "lrg-scripting-gi-private.h"
#include "lrg-python-bridge.h"
#include "../lrg-log.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"

struct _LrgScriptingPyGObject
{
    LrgScriptingGI  parent_instance;

    PyObject       *main_module;      /* __main__ module */
    PyObject       *main_dict;        /* __main__.__dict__ (globals) */
    PyObject       *gi_module;        /* The 'gi' module */
    PyObject       *gi_repository;    /* gi.repository module */
};

G_DEFINE_TYPE (LrgScriptingPyGObject, lrg_scripting_pygobject, LRG_TYPE_SCRIPTING_GI)

/* Global flag to track if Python has been initialized */
static gboolean g_python_initialized = FALSE;

/* ==========================================================================
 * C Function Wrapper for Python
 *
 * This wrapper allows registered C functions to be called from Python.
 * ========================================================================== */

/**
 * pygobject_c_function_wrapper:
 *
 * Python function that wraps a registered C function.
 * Uses a PyCapsule to store the RegisteredCFunctionGI pointer.
 */
static PyObject *
pygobject_c_function_wrapper (PyObject *capsule,
                              PyObject *args)
{
    RegisteredCFunctionGI *reg;
    GValue                *gargs = NULL;
    GValue                 return_value = G_VALUE_INIT;
    g_autoptr(GError)      error = NULL;
    Py_ssize_t             n_args;
    Py_ssize_t             i;
    gboolean               success;
    PyObject              *result = NULL;

    /* Get the registered function data from capsule */
    reg = (RegisteredCFunctionGI *)PyCapsule_GetPointer (capsule,
                                                          "RegisteredCFunctionGI");
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
    if (G_VALUE_TYPE (&return_value) != G_TYPE_INVALID)
    {
        result = lrg_python_from_gvalue (&return_value);
        g_value_unset (&return_value);
    }

    if (result == NULL)
    {
        Py_RETURN_NONE;
    }

    return result;
}

/* ==========================================================================
 * LrgScriptingGI Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_scripting_pygobject_init_interpreter (LrgScriptingGI  *gi_self,
                                          GError         **error)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (gi_self);

    if (lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        return TRUE;
    }

    if (!g_python_initialized)
    {
        Py_Initialize ();
        g_python_initialized = TRUE;
    }

    /* Get __main__ module */
    self->main_module = PyImport_AddModule ("__main__");
    if (self->main_module == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Failed to get __main__ module");
        return FALSE;
    }
    Py_INCREF (self->main_module);

    /* Get __main__.__dict__ */
    self->main_dict = PyModule_GetDict (self->main_module);
    if (self->main_dict == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Failed to get __main__.__dict__");
        return FALSE;
    }
    Py_INCREF (self->main_dict);

    /* Import the gi module */
    self->gi_module = PyImport_ImportModule ("gi");
    if (self->gi_module == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Failed to import gi module: %s",
                     msg ? msg : "(unknown error)");
        return FALSE;
    }

    /* Import gi.repository */
    self->gi_repository = PyImport_ImportModule ("gi.repository");
    if (self->gi_repository == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Failed to import gi.repository: %s",
                     msg ? msg : "(unknown error)");
        return FALSE;
    }

    lrg_scripting_gi_set_interpreter_initialized (gi_self, TRUE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "PyGObject interpreter initialized");

    return TRUE;
}

static void
lrg_scripting_pygobject_finalize_interpreter (LrgScriptingGI *gi_self)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (gi_self);

    if (self->gi_repository != NULL)
    {
        Py_DECREF (self->gi_repository);
        self->gi_repository = NULL;
    }

    if (self->gi_module != NULL)
    {
        Py_DECREF (self->gi_module);
        self->gi_module = NULL;
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

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "PyGObject interpreter finalized");
}

static gboolean
lrg_scripting_pygobject_expose_typelib (LrgScriptingGI  *gi_self,
                                        const gchar     *namespace_,
                                        const gchar     *version,
                                        GError         **error)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (gi_self);
    PyObject              *require_version;
    PyObject              *args;
    PyObject              *result;
    PyObject              *module;

    g_return_val_if_fail (namespace_ != NULL, FALSE);
    g_return_val_if_fail (version != NULL, FALSE);

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Interpreter not initialized");
        return FALSE;
    }

    /* Call gi.require_version(namespace, version) */
    require_version = PyObject_GetAttrString (self->gi_module, "require_version");
    if (require_version == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Failed to get gi.require_version: %s",
                     msg ? msg : "(unknown error)");
        return FALSE;
    }

    args = Py_BuildValue ("(ss)", namespace_, version);
    if (args == NULL)
    {
        Py_DECREF (require_version);
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Failed to build arguments for require_version");
        return FALSE;
    }

    result = PyObject_CallObject (require_version, args);
    Py_DECREF (args);
    Py_DECREF (require_version);

    if (result == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_TYPELIB_NOT_FOUND,
                     "gi.require_version('%s', '%s') failed: %s",
                     namespace_, version, msg ? msg : "(unknown error)");
        return FALSE;
    }
    Py_DECREF (result);

    /*
     * Import the module from gi.repository using PyImport_ImportModule.
     * gi.repository uses lazy loading, so we can't use GetAttrString.
     * We need to import "gi.repository.Namespace" as a full module path.
     */
    {
        g_autofree gchar *full_module_name = NULL;

        full_module_name = g_strdup_printf ("gi.repository.%s", namespace_);
        module = PyImport_ImportModule (full_module_name);
    }

    if (module == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Failed to import gi.repository.%s: %s",
                     namespace_, msg ? msg : "(unknown error)");
        return FALSE;
    }

    /* Make available as global */
    PyDict_SetItemString (self->main_dict, namespace_, module);
    Py_DECREF (module);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
               "Exposed typelib %s-%s to PyGObject",
               namespace_, version);

    return TRUE;
}

static gboolean
lrg_scripting_pygobject_expose_gobject (LrgScriptingGI  *gi_self,
                                        const gchar     *name,
                                        GObject         *object,
                                        GError         **error)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (gi_self);
    PyObject              *pygobj;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (G_IS_OBJECT (object), FALSE);

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Interpreter not initialized");
        return FALSE;
    }

    /*
     * Use PyGObject's pygobject_new() via Python.
     * We import the internal module and call its function to wrap the GObject.
     * This is done via: from gi._gi import _PyGObject_API
     * But we use a simpler approach: create a wrapper via gi.types
     */

    /* Use the simpler approach: wrap using PyCapsule and gi internals */
    /* For now, use our bridge's wrap function which creates a compatible object */
    pygobj = lrg_python_wrap_gobject (object);
    if (pygobj == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Failed to wrap GObject as Python object");
        return FALSE;
    }

    /* Set as global */
    PyDict_SetItemString (self->main_dict, name, pygobj);
    Py_DECREF (pygobj);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
               "Exposed GObject as '%s' to PyGObject",
               name);

    return TRUE;
}

static gboolean
lrg_scripting_pygobject_call_update_hook (LrgScriptingGI  *gi_self,
                                          const gchar     *func_name,
                                          gfloat           delta,
                                          GError         **error)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (gi_self);
    PyObject              *func;
    PyObject              *args;
    PyObject              *result;

    g_return_val_if_fail (func_name != NULL, FALSE);

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_GI_FAILED,
                     "Interpreter not initialized");
        return FALSE;
    }

    /* Get the function */
    func = PyDict_GetItemString (self->main_dict, func_name);
    if (func == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Update hook '%s' not found",
                     func_name);
        return FALSE;
    }

    if (!PyCallable_Check (func))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_TYPE,
                     "Update hook '%s' is not callable",
                     func_name);
        return FALSE;
    }

    /* Build arguments */
    args = Py_BuildValue ("(f)", (double)delta);
    if (args == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Failed to build arguments for update hook '%s'",
                     func_name);
        return FALSE;
    }

    /* Call the function */
    result = PyObject_CallObject (func, args);
    Py_DECREF (args);

    if (result == NULL)
    {
        g_autofree gchar *msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_RUNTIME,
                     "Update hook '%s' error: %s",
                     func_name, msg ? msg : "(unknown error)");
        return FALSE;
    }

    Py_DECREF (result);

    return TRUE;
}

static void
lrg_scripting_pygobject_update_search_paths (LrgScriptingGI *gi_self)
{
    LrgScriptingGIPrivate   *priv = lrg_scripting_gi_get_private (gi_self);
    PyObject                *sys_path;
    guint                    i;

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        return;
    }

    sys_path = PySys_GetObject ("path");
    if (sys_path == NULL || !PyList_Check (sys_path))
    {
        return;
    }

    /* Add custom paths at the beginning */
    for (i = 0; i < priv->search_paths->len; i++)
    {
        const gchar *path = g_ptr_array_index (priv->search_paths, i);
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

static const gchar *
lrg_scripting_pygobject_get_interpreter_name (LrgScriptingGI *gi_self)
{
    return "PyGObject";
}

/* ==========================================================================
 * LrgScripting Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_scripting_pygobject_load_file (LrgScripting  *scripting,
                                   const gchar   *path,
                                   GError       **error)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (scripting);
    LrgScriptingGI        *gi_self = LRG_SCRIPTING_GI (scripting);
    FILE                  *fp;
    PyObject              *result;

    g_return_val_if_fail (path != NULL, FALSE);

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        g_autoptr(GError) init_error = NULL;
        if (!lrg_scripting_pygobject_init_interpreter (gi_self, &init_error))
        {
            g_propagate_error (error, g_steal_pointer (&init_error));
            return FALSE;
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loading PyGObject script: %s", path);

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

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loaded PyGObject script: %s", path);

    return TRUE;
}

static gboolean
lrg_scripting_pygobject_load_string (LrgScripting  *scripting,
                                     const gchar   *name,
                                     const gchar   *code,
                                     GError       **error)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (scripting);
    LrgScriptingGI        *gi_self = LRG_SCRIPTING_GI (scripting);
    PyObject              *compiled;
    PyObject              *result;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        g_autoptr(GError) init_error = NULL;
        if (!lrg_scripting_pygobject_init_interpreter (gi_self, &init_error))
        {
            g_propagate_error (error, g_steal_pointer (&init_error));
            return FALSE;
        }
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
lrg_scripting_pygobject_call_function (LrgScripting  *scripting,
                                       const gchar   *func_name,
                                       GValue        *return_value,
                                       guint          n_args,
                                       const GValue  *args,
                                       GError       **error)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (scripting);
    LrgScriptingGI        *gi_self = LRG_SCRIPTING_GI (scripting);
    PyObject              *func;
    PyObject              *py_args;
    PyObject              *result;
    guint                  i;

    g_return_val_if_fail (func_name != NULL, FALSE);

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Interpreter not initialized");
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
lrg_scripting_pygobject_register_function (LrgScripting           *scripting,
                                           const gchar            *name,
                                           LrgScriptingCFunction   func,
                                           gpointer                user_data,
                                           GError                **error)
{
    LrgScriptingPyGObject   *self = LRG_SCRIPTING_PYGOBJECT (scripting);
    LrgScriptingGI          *gi_self = LRG_SCRIPTING_GI (scripting);
    RegisteredCFunctionGI   *reg;
    PyObject                *capsule;
    PyObject                *py_func;
    static PyMethodDef       method_def = {"", NULL, METH_VARARGS, NULL};

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (func != NULL, FALSE);

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        g_autoptr(GError) init_error = NULL;
        if (!lrg_scripting_pygobject_init_interpreter (gi_self, &init_error))
        {
            g_propagate_error (error, g_steal_pointer (&init_error));
            return FALSE;
        }
    }

    /* Store in base class tracking */
    reg = lrg_scripting_gi_add_registered_function (gi_self, name, func, user_data);
    if (reg == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Failed to register function '%s'",
                     name);
        return FALSE;
    }

    /* Create a PyCapsule to hold the registration data */
    capsule = PyCapsule_New (reg, "RegisteredCFunctionGI", NULL);
    if (capsule == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Failed to create capsule for '%s'",
                     name);
        return FALSE;
    }

    /* Create a Python function */
    method_def.ml_name = name;
    method_def.ml_meth = (PyCFunction)pygobject_c_function_wrapper;

    py_func = PyCFunction_New (&method_def, capsule);
    Py_DECREF (capsule);

    if (py_func == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Failed to create Python function for '%s'",
                     name);
        return FALSE;
    }

    /* Set as global */
    PyDict_SetItemString (self->main_dict, name, py_func);
    Py_DECREF (py_func);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Registered C function: %s", name);

    return TRUE;
}

static gboolean
lrg_scripting_pygobject_get_global (LrgScripting  *scripting,
                                    const gchar   *name,
                                    GValue        *value,
                                    GError       **error)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (scripting);
    LrgScriptingGI        *gi_self = LRG_SCRIPTING_GI (scripting);
    PyObject              *obj;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "Interpreter not initialized");
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
lrg_scripting_pygobject_set_global (LrgScripting  *scripting,
                                    const gchar   *name,
                                    const GValue  *value,
                                    GError       **error)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (scripting);
    LrgScriptingGI        *gi_self = LRG_SCRIPTING_GI (scripting);
    PyObject              *obj;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    (void)error;

    if (!lrg_scripting_gi_is_interpreter_initialized (gi_self))
    {
        g_autoptr(GError) init_error = NULL;
        if (!lrg_scripting_pygobject_init_interpreter (gi_self, &init_error))
        {
            g_propagate_error (error, g_steal_pointer (&init_error));
            return FALSE;
        }
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
lrg_scripting_pygobject_reset (LrgScripting *scripting)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (scripting);
    LrgScriptingGI        *gi_self = LRG_SCRIPTING_GI (scripting);

    /* Clear base class data */
    lrg_scripting_gi_clear_update_hooks (gi_self);
    lrg_scripting_gi_clear_registered_functions (gi_self);

    if (lrg_scripting_gi_is_interpreter_initialized (gi_self) && self->main_dict != NULL)
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
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "PyGObject script context reset");
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scripting_pygobject_finalize (GObject *object)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (object);
    LrgScriptingGIClass   *gi_class;

    /* Call parent's finalize_interpreter */
    gi_class = LRG_SCRIPTING_GI_GET_CLASS (self);
    if (gi_class->finalize_interpreter != NULL)
    {
        gi_class->finalize_interpreter (LRG_SCRIPTING_GI (self));
    }

    G_OBJECT_CLASS (lrg_scripting_pygobject_parent_class)->finalize (object);
}

static void
lrg_scripting_pygobject_constructed (GObject *object)
{
    LrgScriptingPyGObject *self = LRG_SCRIPTING_PYGOBJECT (object);
    g_autoptr(GError)      error = NULL;

    G_OBJECT_CLASS (lrg_scripting_pygobject_parent_class)->constructed (object);

    /* Initialize the interpreter */
    if (!lrg_scripting_pygobject_init_interpreter (LRG_SCRIPTING_GI (self), &error))
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                     "Failed to initialize PyGObject interpreter: %s",
                     error->message);
    }
}

static void
lrg_scripting_pygobject_class_init (LrgScriptingPyGObjectClass *klass)
{
    GObjectClass          *object_class = G_OBJECT_CLASS (klass);
    LrgScriptingClass     *scripting_class = LRG_SCRIPTING_CLASS (klass);
    LrgScriptingGIClass   *gi_class = LRG_SCRIPTING_GI_CLASS (klass);

    object_class->finalize = lrg_scripting_pygobject_finalize;
    object_class->constructed = lrg_scripting_pygobject_constructed;

    /* Override LrgScripting virtual methods */
    scripting_class->load_file = lrg_scripting_pygobject_load_file;
    scripting_class->load_string = lrg_scripting_pygobject_load_string;
    scripting_class->call_function = lrg_scripting_pygobject_call_function;
    scripting_class->register_function = lrg_scripting_pygobject_register_function;
    scripting_class->get_global = lrg_scripting_pygobject_get_global;
    scripting_class->set_global = lrg_scripting_pygobject_set_global;
    scripting_class->reset = lrg_scripting_pygobject_reset;

    /* Override LrgScriptingGI virtual methods */
    gi_class->init_interpreter = lrg_scripting_pygobject_init_interpreter;
    gi_class->finalize_interpreter = lrg_scripting_pygobject_finalize_interpreter;
    gi_class->expose_typelib = lrg_scripting_pygobject_expose_typelib;
    gi_class->expose_gobject = lrg_scripting_pygobject_expose_gobject;
    gi_class->call_update_hook = lrg_scripting_pygobject_call_update_hook;
    gi_class->update_search_paths = lrg_scripting_pygobject_update_search_paths;
    gi_class->get_interpreter_name = lrg_scripting_pygobject_get_interpreter_name;
}

static void
lrg_scripting_pygobject_init (LrgScriptingPyGObject *self)
{
    self->main_module = NULL;
    self->main_dict = NULL;
    self->gi_module = NULL;
    self->gi_repository = NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_scripting_pygobject_new:
 *
 * Creates a new PyGObject-based Python scripting context.
 *
 * Returns: (transfer full): a new #LrgScriptingPyGObject
 */
LrgScriptingPyGObject *
lrg_scripting_pygobject_new (void)
{
    return g_object_new (LRG_TYPE_SCRIPTING_PYGOBJECT, NULL);
}
