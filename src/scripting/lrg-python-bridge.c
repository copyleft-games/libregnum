/* lrg-python-bridge.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GObject <-> Python type conversion bridge.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-python-bridge.h"
#include "lrg-scripting-python-private.h"
#include "lrg-scriptable.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Forward Declarations
 * ========================================================================== */

static PyObject * gobject_wrapper_getattro (PyObject *self, PyObject *name);
static int        gobject_wrapper_setattro (PyObject *self, PyObject *name, PyObject *value);
static void       gobject_wrapper_dealloc (PyObject *self);
static PyObject * gobject_wrapper_repr (PyObject *self);
static PyObject * gobject_wrapper_connect (PyObject *self, PyObject *args);

/* BoundMethod for LrgScriptable methods */
static void       bound_method_dealloc (PyObject *self);
static PyObject * bound_method_call (PyObject *self, PyObject *args, PyObject *kwargs);
static PyObject * bound_method_repr (PyObject *self);

/* ==========================================================================
 * BoundMethod Type (for LrgScriptable methods)
 * ========================================================================== */

typedef struct {
    PyObject_HEAD
    GObject               *gobject;        /* The wrapped GObject (borrowed ref) */
    const LrgScriptMethod *method;         /* The script method descriptor */
    PyObject              *wrapper_ref;    /* Reference to wrapper to prevent GC */
} BoundMethodObject;

static PyTypeObject BoundMethodType = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "libregnum.BoundMethod",
    .tp_doc = "Bound method for LrgScriptable objects",
    .tp_basicsize = sizeof (BoundMethodObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = bound_method_dealloc,
    .tp_call = bound_method_call,
    .tp_repr = bound_method_repr,
};

/* ==========================================================================
 * GObject Wrapper Type Definition
 * ========================================================================== */

static PyMethodDef gobject_wrapper_methods[] = {
    {"connect", gobject_wrapper_connect, METH_VARARGS,
     "Connect a callback to a GObject signal"},
    {NULL, NULL, 0, NULL}
};

PyTypeObject LrgGObjectWrapperType = {
    PyVarObject_HEAD_INIT (NULL, 0)
    .tp_name = "libregnum.GObject",
    .tp_doc = "GObject wrapper for libregnum",
    .tp_basicsize = sizeof (GObjectWrapper),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = gobject_wrapper_dealloc,
    .tp_repr = gobject_wrapper_repr,
    .tp_getattro = gobject_wrapper_getattro,
    .tp_setattro = gobject_wrapper_setattro,
    .tp_methods = gobject_wrapper_methods,
};

/* ==========================================================================
 * GValue -> Python Conversion
 * ========================================================================== */

/**
 * lrg_python_from_gvalue:
 * @value: the GValue to convert
 *
 * Converts a GValue to a Python object.
 *
 * Returns: (transfer full): a new Python object, or NULL on error
 */
PyObject *
lrg_python_from_gvalue (const GValue *value)
{
    GType type;

    if (value == NULL || !G_IS_VALUE (value))
    {
        Py_RETURN_NONE;
    }

    type = G_VALUE_TYPE (value);

    /* Handle None/Invalid */
    if (type == G_TYPE_NONE || type == G_TYPE_INVALID)
    {
        Py_RETURN_NONE;
    }

    /* Boolean */
    if (type == G_TYPE_BOOLEAN)
    {
        if (g_value_get_boolean (value))
        {
            Py_RETURN_TRUE;
        }
        else
        {
            Py_RETURN_FALSE;
        }
    }

    /* Signed integers */
    if (type == G_TYPE_CHAR)
    {
        return PyLong_FromLong ((long)g_value_get_schar (value));
    }
    if (type == G_TYPE_INT)
    {
        return PyLong_FromLong ((long)g_value_get_int (value));
    }
    if (type == G_TYPE_LONG)
    {
        return PyLong_FromLong (g_value_get_long (value));
    }
    if (type == G_TYPE_INT64)
    {
        return PyLong_FromLongLong ((long long)g_value_get_int64 (value));
    }

    /* Unsigned integers */
    if (type == G_TYPE_UCHAR)
    {
        return PyLong_FromUnsignedLong ((unsigned long)g_value_get_uchar (value));
    }
    if (type == G_TYPE_UINT)
    {
        return PyLong_FromUnsignedLong ((unsigned long)g_value_get_uint (value));
    }
    if (type == G_TYPE_ULONG)
    {
        return PyLong_FromUnsignedLong (g_value_get_ulong (value));
    }
    if (type == G_TYPE_UINT64)
    {
        return PyLong_FromUnsignedLongLong ((unsigned long long)g_value_get_uint64 (value));
    }

    /* Floating point */
    if (type == G_TYPE_FLOAT)
    {
        return PyFloat_FromDouble ((double)g_value_get_float (value));
    }
    if (type == G_TYPE_DOUBLE)
    {
        return PyFloat_FromDouble (g_value_get_double (value));
    }

    /* String */
    if (type == G_TYPE_STRING)
    {
        const gchar *str = g_value_get_string (value);
        if (str == NULL)
        {
            Py_RETURN_NONE;
        }
        return PyUnicode_FromString (str);
    }

    /* Enum */
    if (G_TYPE_IS_ENUM (type))
    {
        return PyLong_FromLong ((long)g_value_get_enum (value));
    }

    /* Flags */
    if (G_TYPE_IS_FLAGS (type))
    {
        return PyLong_FromUnsignedLong ((unsigned long)g_value_get_flags (value));
    }

    /* GObject */
    if (g_type_is_a (type, G_TYPE_OBJECT))
    {
        GObject *obj = g_value_get_object (value);
        return lrg_python_wrap_gobject (obj);
    }

    /* Unsupported type */
    lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                 "Cannot convert GValue of type '%s' to Python",
                 g_type_name (type));
    Py_RETURN_NONE;
}

/* ==========================================================================
 * Python -> GValue Conversion
 * ========================================================================== */

/**
 * lrg_python_to_gvalue:
 * @obj: the Python object to convert
 * @value: (out): GValue to initialize and fill
 *
 * Converts a Python object to a GValue.
 *
 * Returns: %TRUE on success, %FALSE on unsupported type
 */
gboolean
lrg_python_to_gvalue (PyObject *obj,
                      GValue   *value)
{
    g_return_val_if_fail (value != NULL, FALSE);

    /* None */
    if (obj == Py_None)
    {
        g_value_init (value, G_TYPE_NONE);
        return TRUE;
    }

    /* Boolean (must check before int since bool is subclass of int) */
    if (PyBool_Check (obj))
    {
        g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, obj == Py_True);
        return TRUE;
    }

    /* Integer */
    if (PyLong_Check (obj))
    {
        int overflow = 0;
        gint64 val;
        guint64 uval;

        val = PyLong_AsLongLongAndOverflow (obj, &overflow);

        if (overflow == 0 && !PyErr_Occurred ())
        {
            g_value_init (value, G_TYPE_INT64);
            g_value_set_int64 (value, val);
            return TRUE;
        }
        PyErr_Clear ();

        /* Try unsigned */
        uval = PyLong_AsUnsignedLongLong (obj);
        if (!PyErr_Occurred ())
        {
            g_value_init (value, G_TYPE_UINT64);
            g_value_set_uint64 (value, uval);
            return TRUE;
        }
        PyErr_Clear ();
        return FALSE;
    }

    /* Float */
    if (PyFloat_Check (obj))
    {
        g_value_init (value, G_TYPE_DOUBLE);
        g_value_set_double (value, PyFloat_AsDouble (obj));
        return TRUE;
    }

    /* String */
    if (PyUnicode_Check (obj))
    {
        const char *str = PyUnicode_AsUTF8 (obj);
        if (str == NULL)
        {
            PyErr_Clear ();
            return FALSE;
        }
        g_value_init (value, G_TYPE_STRING);
        g_value_set_string (value, str);
        return TRUE;
    }

    /* GObject wrapper */
    if (lrg_python_is_gobject (obj))
    {
        GObject *gobj = lrg_python_unwrap_gobject (obj);
        g_value_init (value, G_TYPE_OBJECT);
        g_value_set_object (value, gobj);
        return TRUE;
    }

    /* Unsupported */
    return FALSE;
}

/**
 * lrg_python_to_gvalue_with_type:
 * @obj: the Python object to convert
 * @type: expected GType
 * @value: (out): GValue to initialize and fill
 *
 * Converts a Python object to a GValue of the specified type.
 *
 * Returns: %TRUE on success, %FALSE on type mismatch or unsupported type
 */
gboolean
lrg_python_to_gvalue_with_type (PyObject *obj,
                                GType     type,
                                GValue   *value)
{
    g_return_val_if_fail (value != NULL, FALSE);

    /* Handle None for nullable types */
    if (obj == Py_None)
    {
        if (type == G_TYPE_NONE)
        {
            g_value_init (value, G_TYPE_NONE);
            return TRUE;
        }
        if (g_type_is_a (type, G_TYPE_OBJECT))
        {
            g_value_init (value, type);
            g_value_set_object (value, NULL);
            return TRUE;
        }
        if (type == G_TYPE_STRING)
        {
            g_value_init (value, G_TYPE_STRING);
            g_value_set_string (value, NULL);
            return TRUE;
        }
        return FALSE;
    }

    /* Boolean */
    if (type == G_TYPE_BOOLEAN)
    {
        int result = PyObject_IsTrue (obj);
        if (result == -1)
        {
            PyErr_Clear ();
            return FALSE;
        }
        g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, result != 0);
        return TRUE;
    }

    /* Signed integers */
    if (type == G_TYPE_CHAR || type == G_TYPE_INT ||
        type == G_TYPE_LONG || type == G_TYPE_INT64)
    {
        gint64 val;

        if (!PyLong_Check (obj) && !PyFloat_Check (obj))
        {
            return FALSE;
        }

        if (PyFloat_Check (obj))
        {
            val = (gint64)PyFloat_AsDouble (obj);
        }
        else
        {
            val = PyLong_AsLongLong (obj);
        }

        if (PyErr_Occurred ())
        {
            PyErr_Clear ();
            return FALSE;
        }

        g_value_init (value, type);
        if (type == G_TYPE_CHAR)
            g_value_set_schar (value, (gint8)val);
        else if (type == G_TYPE_INT)
            g_value_set_int (value, (gint)val);
        else if (type == G_TYPE_LONG)
            g_value_set_long (value, (glong)val);
        else
            g_value_set_int64 (value, val);
        return TRUE;
    }

    /* Unsigned integers */
    if (type == G_TYPE_UCHAR || type == G_TYPE_UINT ||
        type == G_TYPE_ULONG || type == G_TYPE_UINT64)
    {
        guint64 val;

        if (!PyLong_Check (obj) && !PyFloat_Check (obj))
        {
            return FALSE;
        }

        if (PyFloat_Check (obj))
        {
            val = (guint64)PyFloat_AsDouble (obj);
        }
        else
        {
            val = PyLong_AsUnsignedLongLong (obj);
        }

        if (PyErr_Occurred ())
        {
            PyErr_Clear ();
            return FALSE;
        }

        g_value_init (value, type);
        if (type == G_TYPE_UCHAR)
            g_value_set_uchar (value, (guint8)val);
        else if (type == G_TYPE_UINT)
            g_value_set_uint (value, (guint)val);
        else if (type == G_TYPE_ULONG)
            g_value_set_ulong (value, (gulong)val);
        else
            g_value_set_uint64 (value, val);
        return TRUE;
    }

    /* Floating point */
    if (type == G_TYPE_FLOAT || type == G_TYPE_DOUBLE)
    {
        gdouble val;
        if (PyFloat_Check (obj))
        {
            val = PyFloat_AsDouble (obj);
        }
        else if (PyLong_Check (obj))
        {
            val = (gdouble)PyLong_AsLongLong (obj);
        }
        else
        {
            return FALSE;
        }

        if (PyErr_Occurred ())
        {
            PyErr_Clear ();
            return FALSE;
        }

        g_value_init (value, type);
        if (type == G_TYPE_FLOAT)
            g_value_set_float (value, (gfloat)val);
        else
            g_value_set_double (value, val);
        return TRUE;
    }

    /* String */
    if (type == G_TYPE_STRING)
    {
        const char *str;

        if (!PyUnicode_Check (obj))
        {
            return FALSE;
        }
        str = PyUnicode_AsUTF8 (obj);
        if (str == NULL)
        {
            PyErr_Clear ();
            return FALSE;
        }
        g_value_init (value, G_TYPE_STRING);
        g_value_set_string (value, str);
        return TRUE;
    }

    /* Enum */
    if (G_TYPE_IS_ENUM (type))
    {
        if (!PyLong_Check (obj))
        {
            return FALSE;
        }
        g_value_init (value, type);
        g_value_set_enum (value, (gint)PyLong_AsLong (obj));
        return TRUE;
    }

    /* Flags */
    if (G_TYPE_IS_FLAGS (type))
    {
        if (!PyLong_Check (obj))
        {
            return FALSE;
        }
        g_value_init (value, type);
        g_value_set_flags (value, (guint)PyLong_AsUnsignedLong (obj));
        return TRUE;
    }

    /* GObject */
    if (g_type_is_a (type, G_TYPE_OBJECT))
    {
        GObject *gobj;

        if (!lrg_python_is_gobject (obj))
        {
            return FALSE;
        }
        gobj = lrg_python_unwrap_gobject (obj);
        if (gobj != NULL && !g_type_is_a (G_OBJECT_TYPE (gobj), type))
        {
            return FALSE;
        }
        g_value_init (value, type);
        g_value_set_object (value, gobj);
        return TRUE;
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Wrapper Implementation
 * ========================================================================== */

/**
 * lrg_python_wrap_gobject:
 * @object: (nullable): the GObject to wrap
 *
 * Wraps a GObject as a Python object with property access.
 *
 * Returns: (transfer full): a new Python wrapper object
 */
PyObject *
lrg_python_wrap_gobject (GObject *object)
{
    GObjectWrapper *wrapper;

    if (object == NULL)
    {
        Py_RETURN_NONE;
    }

    wrapper = PyObject_New (GObjectWrapper, &LrgGObjectWrapperType);
    if (wrapper == NULL)
    {
        return NULL;
    }

    wrapper->gobject = g_object_ref (object);

    return (PyObject *)wrapper;
}

/**
 * lrg_python_unwrap_gobject:
 * @obj: a Python object
 *
 * Extracts the GObject from a Python wrapper.
 *
 * Returns: (transfer none) (nullable): the GObject, or %NULL if not a wrapper
 */
GObject *
lrg_python_unwrap_gobject (PyObject *obj)
{
    if (!lrg_python_is_gobject (obj))
    {
        return NULL;
    }

    return ((GObjectWrapper *)obj)->gobject;
}

/**
 * lrg_python_is_gobject:
 * @obj: a Python object
 *
 * Checks if the Python object is a GObject wrapper.
 *
 * Returns: %TRUE if the object is a GObject wrapper
 */
gboolean
lrg_python_is_gobject (PyObject *obj)
{
    return obj != NULL && PyObject_TypeCheck (obj, &LrgGObjectWrapperType);
}

/**
 * lrg_python_register_gobject_type:
 *
 * Registers the GObject wrapper type with Python.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_python_register_gobject_type (void)
{
    if (PyType_Ready (&LrgGObjectWrapperType) < 0)
    {
        return FALSE;
    }
    return TRUE;
}

/* ==========================================================================
 * GObject Wrapper Methods
 * ========================================================================== */

static void
gobject_wrapper_dealloc (PyObject *self)
{
    GObjectWrapper *wrapper = (GObjectWrapper *)self;

    if (wrapper->gobject != NULL)
    {
        g_object_unref (wrapper->gobject);
        wrapper->gobject = NULL;
    }

    Py_TYPE (self)->tp_free (self);
}

static PyObject *
gobject_wrapper_repr (PyObject *self)
{
    GObjectWrapper *wrapper = (GObjectWrapper *)self;

    if (wrapper->gobject == NULL)
    {
        return PyUnicode_FromString ("<GObject (disposed)>");
    }

    return PyUnicode_FromFormat ("<%s at %p>",
                                 G_OBJECT_TYPE_NAME (wrapper->gobject),
                                 (void *)wrapper->gobject);
}

/*
 * Gets an attribute from a GObject wrapper.
 * Supports LrgScriptable methods and property access control.
 */
static PyObject *
gobject_wrapper_getattro (PyObject *self, PyObject *name)
{
    GObjectWrapper        *wrapper = (GObjectWrapper *)self;
    GObjectClass          *klass;
    GParamSpec            *pspec;
    const char            *attr_name;
    g_autofree gchar      *dash_name = NULL;
    GValue                 value = G_VALUE_INIT;
    PyObject              *result;

    /* Check for internal attributes first (like 'connect') */
    result = PyObject_GenericGetAttr (self, name);
    if (result != NULL || !PyErr_ExceptionMatches (PyExc_AttributeError))
    {
        return result;
    }
    PyErr_Clear ();

    if (wrapper->gobject == NULL)
    {
        PyErr_SetString (PyExc_RuntimeError, "GObject has been disposed");
        return NULL;
    }

    attr_name = PyUnicode_AsUTF8 (name);
    if (attr_name == NULL)
    {
        return NULL;
    }

    /*
     * Check for LrgScriptable custom methods.
     * These take priority over properties with the same name.
     */
    if (LRG_IS_SCRIPTABLE (wrapper->gobject))
    {
        const LrgScriptMethod *method;

        method = lrg_scriptable_find_method (LRG_SCRIPTABLE (wrapper->gobject),
                                              attr_name);
        if (method != NULL)
        {
            BoundMethodObject *bound;

            /* Create a bound method object */
            bound = PyObject_New (BoundMethodObject, &BoundMethodType);
            if (bound == NULL)
            {
                return NULL;
            }

            bound->gobject = wrapper->gobject;
            bound->method = method;
            bound->wrapper_ref = self;
            Py_INCREF (self);

            return (PyObject *)bound;
        }
    }

    /* Look for property */
    klass = G_OBJECT_GET_CLASS (wrapper->gobject);
    pspec = g_object_class_find_property (klass, attr_name);

    if (pspec == NULL)
    {
        /* Try converting underscores to dashes */
        gchar *p;

        dash_name = g_strdup (attr_name);
        for (p = dash_name; *p; p++)
        {
            if (*p == '_')
                *p = '-';
        }
        pspec = g_object_class_find_property (klass, dash_name);
    }

    if (pspec != NULL)
    {
        /*
         * Check access control if object implements LrgScriptable.
         * Default behavior allows reading if G_PARAM_READABLE is set.
         */
        if (LRG_IS_SCRIPTABLE (wrapper->gobject))
        {
            LrgScriptAccessFlags access_flags;

            access_flags = lrg_scriptable_get_property_access (
                LRG_SCRIPTABLE (wrapper->gobject), pspec->name);

            if (!(access_flags & LRG_SCRIPT_ACCESS_READ))
            {
                PyErr_Format (PyExc_AttributeError,
                              "Property '%s' is not script-readable",
                              attr_name);
                return NULL;
            }
        }

        g_value_init (&value, pspec->value_type);
        g_object_get_property (wrapper->gobject, pspec->name, &value);
        result = lrg_python_from_gvalue (&value);
        g_value_unset (&value);
        return result;
    }

    PyErr_Format (PyExc_AttributeError,
                  "'%s' object has no attribute '%s'",
                  G_OBJECT_TYPE_NAME (wrapper->gobject),
                  attr_name);
    return NULL;
}

/*
 * Sets an attribute on a GObject wrapper.
 * Supports LrgScriptable property access control.
 */
static int
gobject_wrapper_setattro (PyObject *self, PyObject *name, PyObject *value)
{
    GObjectWrapper   *wrapper = (GObjectWrapper *)self;
    GObjectClass     *klass;
    GParamSpec       *pspec;
    const char       *attr_name;
    g_autofree gchar *dash_name = NULL;
    GValue            gval = G_VALUE_INIT;

    if (wrapper->gobject == NULL)
    {
        PyErr_SetString (PyExc_RuntimeError, "GObject has been disposed");
        return -1;
    }

    attr_name = PyUnicode_AsUTF8 (name);
    if (attr_name == NULL)
    {
        return -1;
    }

    /* Look for property */
    klass = G_OBJECT_GET_CLASS (wrapper->gobject);
    pspec = g_object_class_find_property (klass, attr_name);

    if (pspec == NULL)
    {
        /* Try converting underscores to dashes */
        gchar *p;

        dash_name = g_strdup (attr_name);
        for (p = dash_name; *p; p++)
        {
            if (*p == '_')
                *p = '-';
        }
        pspec = g_object_class_find_property (klass, dash_name);
    }

    if (pspec == NULL)
    {
        PyErr_Format (PyExc_AttributeError,
                      "'%s' object has no attribute '%s'",
                      G_OBJECT_TYPE_NAME (wrapper->gobject),
                      attr_name);
        return -1;
    }

    /*
     * Check access control if object implements LrgScriptable.
     * This takes precedence over G_PARAM_WRITABLE check.
     */
    if (LRG_IS_SCRIPTABLE (wrapper->gobject))
    {
        LrgScriptAccessFlags access_flags;

        access_flags = lrg_scriptable_get_property_access (
            LRG_SCRIPTABLE (wrapper->gobject), pspec->name);

        if (!(access_flags & LRG_SCRIPT_ACCESS_WRITE))
        {
            PyErr_Format (PyExc_AttributeError,
                          "Property '%s' is not script-writable",
                          attr_name);
            return -1;
        }
    }
    else if (!(pspec->flags & G_PARAM_WRITABLE))
    {
        PyErr_Format (PyExc_AttributeError,
                      "Property '%s' is not writable",
                      attr_name);
        return -1;
    }

    if (!lrg_python_to_gvalue_with_type (value, pspec->value_type, &gval))
    {
        PyErr_Format (PyExc_TypeError,
                      "Cannot convert value to type '%s' for property '%s'",
                      g_type_name (pspec->value_type),
                      attr_name);
        return -1;
    }

    g_object_set_property (wrapper->gobject, pspec->name, &gval);
    g_value_unset (&gval);

    return 0;
}

static PyObject *
gobject_wrapper_connect (PyObject *self, PyObject *args)
{
    /* Signal connection - simplified version, just store reference */
    /* Full implementation would need signal callback handling */
    (void)self;
    (void)args;

    PyErr_SetString (PyExc_NotImplementedError,
                     "Signal connection from Python is not yet implemented");
    return NULL;
}

/* ==========================================================================
 * Error Handling
 * ========================================================================== */

/**
 * lrg_python_check_error:
 * @error: (out) (optional): return location for error
 *
 * Checks if a Python exception occurred and converts it to a GError.
 *
 * Returns: %TRUE if an error occurred, %FALSE otherwise
 */
gboolean
lrg_python_check_error (GError **error)
{
    PyObject *type, *value, *traceback;
    g_autofree gchar *msg = NULL;

    if (!PyErr_Occurred ())
    {
        return FALSE;
    }

    PyErr_Fetch (&type, &value, &traceback);
    PyErr_NormalizeException (&type, &value, &traceback);

    if (value != NULL)
    {
        PyObject *str = PyObject_Str (value);
        if (str != NULL)
        {
            msg = g_strdup (PyUnicode_AsUTF8 (str));
            Py_DECREF (str);
        }
    }

    if (msg == NULL)
    {
        msg = g_strdup ("Unknown Python error");
    }

    g_set_error (error,
                 LRG_SCRIPTING_ERROR,
                 LRG_SCRIPTING_ERROR_RUNTIME,
                 "%s", msg);

    Py_XDECREF (type);
    Py_XDECREF (value);
    Py_XDECREF (traceback);

    return TRUE;
}

/**
 * lrg_python_get_error_message:
 *
 * Gets the current Python exception as a string.
 *
 * Returns: (transfer full) (nullable): the error message, or %NULL if no error
 */
gchar *
lrg_python_get_error_message (void)
{
    PyObject *type, *value, *traceback;
    gchar    *msg = NULL;

    if (!PyErr_Occurred ())
    {
        return NULL;
    }

    PyErr_Fetch (&type, &value, &traceback);
    PyErr_NormalizeException (&type, &value, &traceback);

    if (value != NULL)
    {
        PyObject *str = PyObject_Str (value);
        if (str != NULL)
        {
            msg = g_strdup (PyUnicode_AsUTF8 (str));
            Py_DECREF (str);
        }
    }

    if (msg == NULL)
    {
        msg = g_strdup ("Unknown Python error");
    }

    Py_XDECREF (type);
    Py_XDECREF (value);
    Py_XDECREF (traceback);

    return msg;
}

/* ==========================================================================
 * BoundMethod Implementation (for LrgScriptable methods)
 * ========================================================================== */

static void
bound_method_dealloc (PyObject *self)
{
    BoundMethodObject *bound = (BoundMethodObject *)self;

    Py_XDECREF (bound->wrapper_ref);
    bound->gobject = NULL;
    bound->method = NULL;

    Py_TYPE (self)->tp_free (self);
}

static PyObject *
bound_method_repr (PyObject *self)
{
    BoundMethodObject *bound = (BoundMethodObject *)self;

    if (bound->gobject == NULL || bound->method == NULL)
    {
        return PyUnicode_FromString ("<BoundMethod (invalid)>");
    }

    return PyUnicode_FromFormat ("<BoundMethod %s.%s>",
                                 G_OBJECT_TYPE_NAME (bound->gobject),
                                 bound->method->name);
}

/*
 * Invokes an LrgScriptable method from Python.
 */
static PyObject *
bound_method_call (PyObject *self, PyObject *args, PyObject *kwargs)
{
    BoundMethodObject     *bound = (BoundMethodObject *)self;
    const LrgScriptMethod *method;
    GObject               *gobject;
    GValue                *gargs;
    GValue                 return_value = G_VALUE_INIT;
    g_autoptr(GError)      error = NULL;
    Py_ssize_t             n_args;
    Py_ssize_t             i;
    gboolean               success;
    PyObject              *result;

    (void)kwargs;  /* Unused for now */

    if (bound->gobject == NULL || bound->method == NULL)
    {
        PyErr_SetString (PyExc_RuntimeError, "BoundMethod is invalid");
        return NULL;
    }

    gobject = bound->gobject;
    method = bound->method;

    /* Get argument count */
    n_args = PyTuple_Size (args);

    /* Check argument count if method specifies exact count */
    if (method->n_params >= 0 && n_args != method->n_params)
    {
        PyErr_Format (PyExc_TypeError,
                      "%s() takes %d arguments (%zd given)",
                      method->name, method->n_params, n_args);
        return NULL;
    }

    /* Convert Python arguments to GValues */
    if (n_args > 0)
    {
        gargs = g_new0 (GValue, n_args);

        for (i = 0; i < n_args; i++)
        {
            PyObject *arg = PyTuple_GetItem (args, i);

            if (!lrg_python_to_gvalue (arg, &gargs[i]))
            {
                /* Clean up already converted values */
                Py_ssize_t j;

                for (j = 0; j < i; j++)
                {
                    g_value_unset (&gargs[j]);
                }
                g_free (gargs);

                PyErr_Format (PyExc_TypeError,
                              "Cannot convert argument %zd for method '%s'",
                              i + 1, method->name);
                return NULL;
            }
        }
    }
    else
    {
        gargs = NULL;
    }

    /* Invoke the method */
    success = method->func (LRG_SCRIPTABLE (gobject),
                            (guint)n_args,
                            gargs,
                            &return_value,
                            &error);

    /* Clean up arguments */
    if (gargs != NULL)
    {
        for (i = 0; i < n_args; i++)
        {
            g_value_unset (&gargs[i]);
        }
        g_free (gargs);
    }

    if (!success)
    {
        const gchar *msg;

        msg = (error != NULL) ? error->message : "Unknown error";
        PyErr_Format (PyExc_RuntimeError,
                      "Method '%s' failed: %s",
                      method->name, msg);
        return NULL;
    }

    /* Return the result */
    if (G_IS_VALUE (&return_value))
    {
        result = lrg_python_from_gvalue (&return_value);
        g_value_unset (&return_value);
        return result;
    }

    Py_RETURN_NONE;
}

/**
 * lrg_python_register_bound_method_type:
 *
 * Registers the BoundMethod type with Python.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_python_register_bound_method_type (void)
{
    if (PyType_Ready (&BoundMethodType) < 0)
    {
        return FALSE;
    }
    return TRUE;
}
