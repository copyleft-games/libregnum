/* lrg-python-api.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built-in Python API for libregnum.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-python-api.h"
#include "lrg-python-bridge.h"
#include "lrg-scripting-python-private.h"
#include "../lrg-log.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"

/* ==========================================================================
 * Log API Implementation
 * ========================================================================== */

static PyObject *
log_debug (PyObject *self, PyObject *args)
{
    const char *msg;

    (void)self;

    if (!PyArg_ParseTuple (args, "s", &msg))
    {
        return NULL;
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "[Python] %s", msg);

    Py_RETURN_NONE;
}

static PyObject *
log_info (PyObject *self, PyObject *args)
{
    const char *msg;

    (void)self;

    if (!PyArg_ParseTuple (args, "s", &msg))
    {
        return NULL;
    }

    lrg_info (LRG_LOG_DOMAIN_SCRIPTING, "[Python] %s", msg);

    Py_RETURN_NONE;
}

static PyObject *
log_warning (PyObject *self, PyObject *args)
{
    const char *msg;

    (void)self;

    if (!PyArg_ParseTuple (args, "s", &msg))
    {
        return NULL;
    }

    lrg_warning (LRG_LOG_DOMAIN_SCRIPTING, "[Python] %s", msg);

    Py_RETURN_NONE;
}

static PyObject *
log_error (PyObject *self, PyObject *args)
{
    const char *msg;

    (void)self;

    if (!PyArg_ParseTuple (args, "s", &msg))
    {
        return NULL;
    }

    lrg_error (LRG_LOG_DOMAIN_SCRIPTING, "[Python] %s", msg);

    Py_RETURN_NONE;
}

static PyMethodDef log_methods[] = {
    {"debug",   log_debug,   METH_VARARGS, "Log a debug message"},
    {"info",    log_info,    METH_VARARGS, "Log an info message"},
    {"warning", log_warning, METH_VARARGS, "Log a warning message"},
    {"error",   log_error,   METH_VARARGS, "Log an error message"},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject LogType = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "libregnum.Log",
    .tp_doc = "Libregnum logging interface",
    .tp_basicsize = sizeof (PyObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = log_methods,
    .tp_new = PyType_GenericNew,
};

/* ==========================================================================
 * Registry API Implementation
 * ========================================================================== */

typedef struct {
    PyObject_HEAD
    LrgScriptingPython *scripting;
} RegistryObject;

static PyObject *
registry_create (RegistryObject *self, PyObject *args, PyObject *kwargs)
{
    const char   *type_name;
    LrgRegistry  *registry;
    GType         gtype;
    GObject      *object;
    PyObject     *result;

    if (!PyArg_ParseTuple (args, "s", &type_name))
    {
        return NULL;
    }

    registry = lrg_scripting_python_get_registry (self->scripting);
    if (registry == NULL)
    {
        PyErr_SetString (PyExc_RuntimeError, "No registry available");
        return NULL;
    }

    gtype = lrg_registry_lookup (registry, type_name);
    if (gtype == G_TYPE_INVALID)
    {
        PyErr_Format (PyExc_KeyError, "Type '%s' not registered", type_name);
        return NULL;
    }

    /* Create the object */
    object = g_object_new (gtype, NULL);
    if (object == NULL)
    {
        PyErr_Format (PyExc_RuntimeError, "Failed to create object of type '%s'", type_name);
        return NULL;
    }

    /* Apply kwargs as properties */
    if (kwargs != NULL && PyDict_Size (kwargs) > 0)
    {
        PyObject    *key, *value;
        Py_ssize_t   pos = 0;
        GObjectClass *klass = G_OBJECT_GET_CLASS (object);

        while (PyDict_Next (kwargs, &pos, &key, &value))
        {
            const char *prop_name = PyUnicode_AsUTF8 (key);
            GParamSpec *pspec;
            g_autofree gchar *dash_name = NULL;

            if (prop_name == NULL)
            {
                continue;
            }

            /* Try with underscores converted to dashes */
            {
                gchar *p;
                dash_name = g_strdup (prop_name);
                for (p = dash_name; *p; p++)
                {
                    if (*p == '_')
                        *p = '-';
                }
            }

            pspec = g_object_class_find_property (klass, dash_name);
            if (pspec == NULL)
            {
                pspec = g_object_class_find_property (klass, prop_name);
            }

            if (pspec != NULL && (pspec->flags & G_PARAM_WRITABLE))
            {
                GValue gval = G_VALUE_INIT;
                if (lrg_python_to_gvalue_with_type (value, pspec->value_type, &gval))
                {
                    g_object_set_property (object, pspec->name, &gval);
                    g_value_unset (&gval);
                }
            }
        }
    }

    result = lrg_python_wrap_gobject (object);
    g_object_unref (object);

    return result;
}

static PyObject *
registry_is_registered (RegistryObject *self, PyObject *args)
{
    const char  *type_name;
    LrgRegistry *registry;

    if (!PyArg_ParseTuple (args, "s", &type_name))
    {
        return NULL;
    }

    registry = lrg_scripting_python_get_registry (self->scripting);
    if (registry == NULL)
    {
        Py_RETURN_FALSE;
    }

    if (lrg_registry_is_registered (registry, type_name))
    {
        Py_RETURN_TRUE;
    }

    Py_RETURN_FALSE;
}

static PyObject *
registry_get_types (RegistryObject *self, PyObject *args)
{
    LrgRegistry  *registry;
    GList        *types;
    GList        *iter;
    PyObject     *result;
    Py_ssize_t    i;

    (void)args;

    registry = lrg_scripting_python_get_registry (self->scripting);
    if (registry == NULL)
    {
        return PyList_New (0);
    }

    types = lrg_registry_get_names (registry);
    result = PyList_New ((Py_ssize_t)g_list_length (types));

    for (iter = types, i = 0; iter != NULL; iter = iter->next, i++)
    {
        const gchar *name = (const gchar *)iter->data;
        PyList_SET_ITEM (result, i, PyUnicode_FromString (name));
    }

    g_list_free (types);

    return result;
}

static PyMethodDef registry_methods[] = {
    {"create", (PyCFunction)(void(*)(void))registry_create, METH_VARARGS | METH_KEYWORDS,
     "Create a new object of the specified type"},
    {"is_registered", (PyCFunction)registry_is_registered, METH_VARARGS,
     "Check if a type is registered"},
    {"get_types", (PyCFunction)registry_get_types, METH_NOARGS,
     "Get list of all registered type names"},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject RegistryType = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "libregnum.Registry",
    .tp_doc = "Libregnum type registry",
    .tp_basicsize = sizeof (RegistryObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = registry_methods,
    .tp_new = PyType_GenericNew,
};

/* ==========================================================================
 * Engine API Implementation
 * ========================================================================== */

typedef struct {
    PyObject_HEAD
    LrgScriptingPython *scripting;
} EngineObject;

static PyObject *
engine_getattro (PyObject *self, PyObject *name)
{
    EngineObject *engine_obj = (EngineObject *)self;
    LrgEngine    *engine;
    const char   *attr_name;
    PyObject     *result;

    /* Check for methods first */
    result = PyObject_GenericGetAttr (self, name);
    if (result != NULL || !PyErr_ExceptionMatches (PyExc_AttributeError))
    {
        return result;
    }
    PyErr_Clear ();

    engine = lrg_scripting_python_get_engine (engine_obj->scripting);
    if (engine == NULL)
    {
        PyErr_SetString (PyExc_RuntimeError, "Engine not available");
        return NULL;
    }

    attr_name = PyUnicode_AsUTF8 (name);
    if (attr_name == NULL)
    {
        return NULL;
    }

    /* Engine properties */
    if (g_strcmp0 (attr_name, "registry") == 0)
    {
        LrgRegistry *registry = lrg_engine_get_registry (engine);
        return lrg_python_wrap_gobject (G_OBJECT (registry));
    }

    if (g_strcmp0 (attr_name, "data_loader") == 0)
    {
        LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
        return lrg_python_wrap_gobject (G_OBJECT (loader));
    }

    if (g_strcmp0 (attr_name, "asset_manager") == 0)
    {
        LrgAssetManager *assets = lrg_engine_get_asset_manager (engine);
        return lrg_python_wrap_gobject (G_OBJECT (assets));
    }

    if (g_strcmp0 (attr_name, "state") == 0)
    {
        LrgEngineState state = lrg_engine_get_state (engine);
        return PyLong_FromLong ((long)state);
    }

    if (g_strcmp0 (attr_name, "is_running") == 0)
    {
        if (lrg_engine_is_running (engine))
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    PyErr_Format (PyExc_AttributeError,
                  "'Engine' object has no attribute '%s'",
                  attr_name);
    return NULL;
}

static PyTypeObject EngineType = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "libregnum.Engine",
    .tp_doc = "Libregnum engine singleton",
    .tp_basicsize = sizeof (EngineObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getattro = engine_getattro,
    .tp_new = PyType_GenericNew,
};

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_python_api_register_all:
 * @scripting: the scripting context
 *
 * Registers all built-in API globals in Python.
 */
void
lrg_python_api_register_all (LrgScriptingPython *scripting)
{
    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (scripting));

    lrg_python_api_register_log (scripting);
    lrg_python_api_register_registry (scripting);
    lrg_python_api_register_engine (scripting);
}

/**
 * lrg_python_api_register_log:
 * @scripting: the scripting context
 *
 * Registers the Log global.
 */
void
lrg_python_api_register_log (LrgScriptingPython *scripting)
{
    PyObject *main_dict;
    PyObject *log_instance;

    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (scripting));

    main_dict = lrg_scripting_python_get_main_dict (scripting);
    if (main_dict == NULL)
    {
        return;
    }

    if (PyType_Ready (&LogType) < 0)
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING, "Failed to initialize Log type");
        return;
    }

    log_instance = PyObject_CallObject ((PyObject *)&LogType, NULL);
    if (log_instance != NULL)
    {
        PyDict_SetItemString (main_dict, "Log", log_instance);
        Py_DECREF (log_instance);
    }
}

/**
 * lrg_python_api_register_registry:
 * @scripting: the scripting context
 *
 * Registers the Registry global.
 */
void
lrg_python_api_register_registry (LrgScriptingPython *scripting)
{
    PyObject       *main_dict;
    RegistryObject *registry_instance;

    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (scripting));

    main_dict = lrg_scripting_python_get_main_dict (scripting);
    if (main_dict == NULL)
    {
        return;
    }

    if (PyType_Ready (&RegistryType) < 0)
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING, "Failed to initialize Registry type");
        return;
    }

    registry_instance = PyObject_New (RegistryObject, &RegistryType);
    if (registry_instance != NULL)
    {
        registry_instance->scripting = scripting;
        PyDict_SetItemString (main_dict, "Registry", (PyObject *)registry_instance);
        Py_DECREF (registry_instance);
    }
}

/**
 * lrg_python_api_register_engine:
 * @scripting: the scripting context
 *
 * Registers the Engine global.
 */
void
lrg_python_api_register_engine (LrgScriptingPython *scripting)
{
    PyObject     *main_dict;
    EngineObject *engine_instance;

    g_return_if_fail (LRG_IS_SCRIPTING_PYTHON (scripting));

    main_dict = lrg_scripting_python_get_main_dict (scripting);
    if (main_dict == NULL)
    {
        return;
    }

    if (PyType_Ready (&EngineType) < 0)
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING, "Failed to initialize Engine type");
        return;
    }

    engine_instance = PyObject_New (EngineObject, &EngineType);
    if (engine_instance != NULL)
    {
        engine_instance->scripting = scripting;
        PyDict_SetItemString (main_dict, "Engine", (PyObject *)engine_instance);
        Py_DECREF (engine_instance);
    }
}

/**
 * lrg_python_api_update_engine:
 * @scripting: the scripting context
 * @engine: (nullable): the new engine to expose
 *
 * Updates the Engine global to reference a different engine.
 */
void
lrg_python_api_update_engine (LrgScriptingPython *scripting,
                              LrgEngine          *engine)
{
    /* The engine object gets its engine from scripting->engine
     * which is already updated, so nothing to do here */
    (void)scripting;
    (void)engine;
}

/**
 * lrg_python_api_update_registry:
 * @scripting: the scripting context
 * @registry: (nullable): the new registry to expose
 *
 * Updates the Registry global to reference a different registry.
 */
void
lrg_python_api_update_registry (LrgScriptingPython *scripting,
                                LrgRegistry        *registry)
{
    /* The registry object gets its registry from scripting->registry
     * which is already updated, so nothing to do here */
    (void)scripting;
    (void)registry;
}
