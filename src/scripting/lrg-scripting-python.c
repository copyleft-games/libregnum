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
#define LRG_SCRIPTING_PRIVATE

#include "lrg-scripting-python.h"
#include "lrg-script-debugger.h"
#include "lrg-script-reloader.h"
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
    LrgScriptDebugger *debugger;    /* not ref'd; the attached debugger */
    gint          c_depth;          /* host call nesting */
    gint          in_hook;          /* monitoring-callback nesting */
    gboolean      exception_reported; /* this exception already stopped */
};

static void python_debuggable_init (LrgScriptDebuggableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgScriptingPython, lrg_scripting_python, LRG_TYPE_SCRIPTING,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_SCRIPT_DEBUGGABLE,
                                                python_debuggable_init))

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

    /*
     * Each context owns a private globals dict instead of __main__.__dict__:
     * two contexts (for example two plugins) must never see or clear each
     * other's names.
     */
    self->main_module = NULL;
    self->main_dict = lrg_python_new_globals ();
    if (self->main_dict == NULL)
    {
        lrg_error (LRG_LOG_DOMAIN_SCRIPTING, "Failed to create Python globals");
        return FALSE;
    }

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
    if (reg->scripting == NULL)
    {
        PyErr_Format (PyExc_RuntimeError, "'%s' is no longer registered in this context", reg->name);
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
        /* A failing callee may still have set the return value */
        if (G_IS_VALUE (&return_value))
            g_value_unset (&return_value);
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
 * RegisteredCFunction lifetime: the context's table holds one reference and
 * every Python callable made for it another, because scripts may keep a
 * callable (an alias, a stored callback) after the context re-registers
 * the name, resets or dies.
 */
static RegisteredCFunction *
registered_func_ref (RegisteredCFunction *reg)
{
    g_atomic_int_inc (&reg->ref_count);
    return reg;
}

static void
registered_func_unref (gpointer data)
{
    RegisteredCFunction *reg = (RegisteredCFunction *)data;

    if (!g_atomic_int_dec_and_test (&reg->ref_count))
        return;
    g_free (reg->name);
    g_free (reg);
}

/* The context forgets a registration: callables still alive now fail cleanly. */
static void
registered_func_release (gpointer data)
{
    RegisteredCFunction *reg = (RegisteredCFunction *)data;

    reg->scripting = NULL;
    registered_func_unref (reg);
}

/* ==========================================================================
 * Hot reload and debugger
 * ========================================================================== */

#define LRG_PY_CHILD_CAP (500)
#define LRG_PY_TOOL_ID   (0)

static void
py_enter (LrgScriptingPython *self)
{
    self->c_depth++;
    lrg_scripting_priv_push_current (LRG_SCRIPTING (self));
}

static void
py_leave (LrgScriptingPython *self)
{
    lrg_scripting_priv_pop_current (LRG_SCRIPTING (self));
    self->c_depth--;
    if (self->c_depth > 0)
        return;
    self->c_depth = 0;
    self->exception_reported = FALSE;
    if (self->debugger != NULL)
        lrg_script_debugger_note_host_return (self->debugger);
}

/* Copy the pending exception's file:line, then put the error back. */
static void
py_set_location_from_error (LrgScripting *scripting,
                            const gchar  *fallback)
{
    PyObject *typ = NULL;
    PyObject *val = NULL;
    PyObject *tb = NULL;
    PyObject *lineno = NULL;
    PyObject *filename = NULL;
    PyObject *tb_next = NULL;
    PyObject *frame = NULL;
    PyObject *code = NULL;
    gchar *file_copy = NULL;
    const gchar *file = fallback;
    gint line = 0;

    PyErr_Fetch (&typ, &val, &tb);
    PyErr_NormalizeException (&typ, &val, &tb);
    if (val != NULL)
    {
        lineno = PyObject_GetAttrString (val, "lineno");
        filename = PyObject_GetAttrString (val, "filename");
        PyErr_Clear ();
    }
    if ((lineno == NULL || !PyLong_Check (lineno) || filename == NULL ||
         !PyUnicode_Check (filename)) &&
        tb != NULL)
    {
        Py_XDECREF (lineno);
        Py_XDECREF (filename);
        lineno = PyObject_GetAttrString (tb, "tb_lineno");
        frame = PyObject_GetAttrString (tb, "tb_frame");
        PyErr_Clear ();
        if (frame != NULL)
        {
            code = PyObject_GetAttrString (frame, "f_code");
            PyErr_Clear ();
        }
        if (code != NULL)
        {
            filename = PyObject_GetAttrString (code, "co_filename");
            PyErr_Clear ();
        }
    }
    if (lineno != NULL && PyLong_Check (lineno))
        line = (gint) PyLong_AsLong (lineno);
    if (filename != NULL && filename != Py_None && PyUnicode_Check (filename))
    {
        const char *utf8 = PyUnicode_AsUTF8 (filename);

        if (utf8 != NULL)
        {
            file_copy = g_strdup (utf8);
            file = file_copy;
        }
    }
    lrg_scripting_priv_set_error_location (scripting,
                                           file != NULL ? file : "",
                                           line);
    g_free (file_copy);
    Py_XDECREF (tb_next);
    Py_XDECREF (code);
    Py_XDECREF (frame);
    Py_XDECREF (filename);
    Py_XDECREF (lineno);
    PyErr_Restore (typ, val, tb);
}

static void
py_note_host_exception (LrgScriptingPython *self,
                        const gchar        *fallback)
{
    PyObject *typ = NULL;
    PyObject *val = NULL;
    PyObject *tb = NULL;
    PyObject *text = NULL;
    gchar *message = NULL;
    LrgScriptExceptionMode mode;

    if (self->exception_reported || self->debugger == NULL)
        return;
    mode = lrg_script_debugger_get_exception_mode (self->debugger);
    if (mode == LRG_SCRIPT_EXCEPTION_MODE_NEVER)
        return;

    py_set_location_from_error (LRG_SCRIPTING (self), fallback);
    PyErr_Fetch (&typ, &val, &tb);
    PyErr_NormalizeException (&typ, &val, &tb);
    if (val != NULL)
        text = PyObject_Str (val);
    if (text != NULL && PyUnicode_Check (text))
    {
        const char *utf8 = PyUnicode_AsUTF8 (text);

        message = g_strdup (utf8 != NULL ? utf8 : "");
    }
    else
        PyErr_Clear ();
    Py_XDECREF (text);

    self->exception_reported = TRUE;
    {
        const gchar *shown = message != NULL ? message : "";

        Py_BEGIN_ALLOW_THREADS
        lrg_script_debugger_report_exception (self->debugger,
                                              LRG_SCRIPT_DEBUGGABLE (self),
                                              fallback,
                                              0,
                                              shown,
                                              TRUE);
        Py_END_ALLOW_THREADS
    }
    g_free (message);
    PyErr_Restore (typ, val, tb);
}

/* Shallow: in-place mutation of a value object is not rolled back. */
static void
py_restore_dict (PyObject *live,
                 PyObject *snapshot)
{
    PyObject *doomed;
    PyObject *key;
    PyObject *value;
    Py_ssize_t pos;
    Py_ssize_t i;
    Py_ssize_t n;

    doomed = PyList_New (0);
    if (doomed == NULL)
    {
        PyErr_Clear ();
        return;
    }
    pos = 0;
    while (PyDict_Next (live, &pos, &key, &value))
    {
        int contains = PyDict_Contains (snapshot, key);

        if (contains < 0)
        {
            PyErr_Clear ();
            continue;
        }
        if (contains == 0)
        {
            Py_INCREF (key);
            PyList_Append (doomed, key);
            Py_DECREF (key);
        }
    }
    n = PyList_GET_SIZE (doomed);
    for (i = 0; i < n; i++)
    {
        if (PyDict_DelItem (live, PyList_GET_ITEM (doomed, i)) < 0)
            PyErr_Clear ();
    }
    Py_DECREF (doomed);
    if (PyDict_Update (live, snapshot) < 0)
        PyErr_Clear ();
}

#if PY_VERSION_HEX >= 0x030C0000

typedef enum
{
    PY_H_DICT,
    PY_H_SEQ,
    PY_H_OBJECT
} PyHandleKind;

typedef struct
{
    PyHandleKind  kind;
    PyObject     *obj;
    GObject      *object;
} PyHandle;

static PyObject *py_monitoring = NULL;
static PyObject *py_line_fn = NULL;
static PyObject *py_raise_fn = NULL;
static PyObject *py_unwind_fn = NULL;
static int py_ev_line = 0;
static int py_ev_raise = 0;
static int py_ev_unwind = 0;
static LrgScriptDebugger *py_tool_owner = NULL;
static gint py_tool_users = 0;
static gboolean py_events_dirty = FALSE;

static void py_apply_events (LrgScriptingPython *self);

static void
py_handle_free (gpointer data)
{
    PyHandle *handle = (PyHandle *) data;
    PyGILState_STATE state;

    state = PyGILState_Ensure ();
    Py_XDECREF (handle->obj);
    PyGILState_Release (state);
    if (handle->object != NULL)
        g_object_unref (handle->object);
    g_free (handle);
}

static guint
py_box (LrgScriptDebugger *debugger,
        PyObject          *obj,
        PyHandleKind       kind)
{
    PyHandle *handle;

    if (debugger == NULL || obj == NULL)
        return 0;
    handle = g_new0 (PyHandle, 1);
    handle->kind = kind;
    handle->obj = obj;
    Py_INCREF (obj);
    return lrg_script_debugger_alloc_reference (debugger, handle, py_handle_free);
}

static guint
py_box_gobject (LrgScriptDebugger *debugger,
                PyObject          *obj)
{
    GObject *object;
    PyHandle *handle;

    object = lrg_python_unwrap_gobject (obj);
    if (debugger == NULL || object == NULL)
        return 0;
    handle = g_new0 (PyHandle, 1);
    handle->kind = PY_H_OBJECT;
    handle->object = g_object_ref (object);
    return lrg_script_debugger_alloc_reference (debugger, handle, py_handle_free);
}

static LrgScriptVariable *
py_variable_from (LrgScriptDebugger *debugger,
                  const gchar       *name,
                  PyObject          *value)
{
    PyObject *repr;
    gchar *text;
    gchar *type_name;
    guint ref = 0;
    int truthy;
    LrgScriptVariable *var;

    repr = PyObject_Repr (value);
    if (repr != NULL && PyUnicode_Check (repr))
    {
        const char *utf8 = PyUnicode_AsUTF8 (repr);

        text = g_strdup (utf8 != NULL ? utf8 : "?");
    }
    else
    {
        PyErr_Clear ();
        text = g_strdup ("?");
    }
    Py_XDECREF (repr);
    if (text != NULL && strlen (text) > 240)
        text[240] = '\0';
    type_name = g_strdup (Py_TYPE (value)->tp_name);
    if (PyDict_Check (value))
        ref = py_box (debugger, value, PY_H_DICT);
    else if (PyList_Check (value) || PyTuple_Check (value))
        ref = py_box (debugger, value, PY_H_SEQ);
    else if (lrg_python_is_gobject (value))
        ref = py_box_gobject (debugger, value);
    var = lrg_script_variable_new (name != NULL ? name : "?",
                                   text != NULL ? text : "?",
                                   type_name,
                                   ref);
    truthy = PyObject_IsTrue (value);
    if (truthy < 0)
    {
        PyErr_Clear ();
        truthy = 0;
    }
    lrg_script_variable_set_truthy (var, truthy ? TRUE : FALSE);
    g_free (text);
    g_free (type_name);
    return var;
}

static PyFrameObject *
py_frame_at (int level)
{
    PyFrameObject *frame;
    int i;

    frame = PyThreadState_GetFrame (PyThreadState_Get ());
    for (i = 0; i < level && frame != NULL; i++)
    {
        PyFrameObject *back = PyFrame_GetBack (frame);

        Py_DECREF (frame);
        frame = back;
    }
    return frame;
}

static guint
py_count_frames (void)
{
    PyFrameObject *frame;
    guint depth = 0;

    frame = PyThreadState_GetFrame (PyThreadState_Get ());
    while (frame != NULL)
    {
        PyFrameObject *back = PyFrame_GetBack (frame);

        Py_DECREF (frame);
        frame = back;
        depth++;
    }
    if (depth == 0)
        depth = 1;
    return depth;
}

static LrgScriptingPython *
py_current (void)
{
    LrgScripting *current = lrg_scripting_priv_get_current ();

    if (current == NULL || !LRG_IS_SCRIPTING_PYTHON (current))
        return NULL;
    return LRG_SCRIPTING_PYTHON (current);
}

static void
py_finish_hook (LrgScriptingPython *self)
{
    self->in_hook--;
    if (self->in_hook == 0 && py_events_dirty)
    {
        py_events_dirty = FALSE;
        py_apply_events (self);
    }
}

static PyObject *
py_line_cb (PyObject *unused,
            PyObject *args)
{
    PyObject *code = NULL;
    int line = 0;
    LrgScriptingPython *self;
    PyObject *filename;
    gchar *file_copy;
    guint depth;

    (void) unused;
    if (!PyArg_ParseTuple (args, "Oi", &code, &line))
    {
        PyErr_Clear ();
        Py_RETURN_NONE;
    }
    self = py_current ();
    if (self == NULL || self->debugger == NULL || self->in_hook > 0)
        Py_RETURN_NONE;
    self->exception_reported = FALSE;
    if (!lrg_script_debugger_line_may_break (self->debugger, line))
        Py_RETURN_NONE;

    filename = PyObject_GetAttrString (code, "co_filename");
    file_copy = g_strdup ("");
    if (filename != NULL && PyUnicode_Check (filename))
    {
        const char *utf8 = PyUnicode_AsUTF8 (filename);

        g_free (file_copy);
        file_copy = g_strdup (utf8 != NULL ? utf8 : "");
    }
    Py_XDECREF (filename);
    PyErr_Clear ();

    depth = py_count_frames ();
    self->in_hook++;
    Py_BEGIN_ALLOW_THREADS
    lrg_script_debugger_report_line (self->debugger,
                                     LRG_SCRIPT_DEBUGGABLE (self),
                                     file_copy,
                                     line,
                                     depth);
    Py_END_ALLOW_THREADS
    py_finish_hook (self);
    g_free (file_copy);
    Py_RETURN_NONE;
}

static void
py_report_monitored (LrgScriptingPython *self,
                     PyObject           *code,
                     PyObject           *exc,
                     gboolean            uncaught)
{
    PyObject *filename;
    PyObject *text;
    gchar *file_copy;
    gchar *message;
    int line = 0;

    if (self->exception_reported)
        return;
    filename = code != NULL ? PyObject_GetAttrString (code, "co_filename") : NULL;
    file_copy = g_strdup ("");
    if (filename != NULL && PyUnicode_Check (filename))
    {
        const char *utf8 = PyUnicode_AsUTF8 (filename);

        g_free (file_copy);
        file_copy = g_strdup (utf8 != NULL ? utf8 : "");
    }
    Py_XDECREF (filename);
    text = exc != NULL ? PyObject_Str (exc) : NULL;
    message = g_strdup ("");
    if (text != NULL && PyUnicode_Check (text))
    {
        const char *utf8 = PyUnicode_AsUTF8 (text);

        g_free (message);
        message = g_strdup (utf8 != NULL ? utf8 : "");
    }
    Py_XDECREF (text);
    PyErr_Clear ();
    if (code != NULL)
    {
        PyObject *first = PyObject_GetAttrString (code, "co_firstlineno");

        if (first != NULL && PyLong_Check (first))
            line = (gint) PyLong_AsLong (first);
        Py_XDECREF (first);
        PyErr_Clear ();
    }

    self->exception_reported = TRUE;
    self->in_hook++;
    Py_BEGIN_ALLOW_THREADS
    lrg_script_debugger_report_exception (self->debugger,
                                          LRG_SCRIPT_DEBUGGABLE (self),
                                          file_copy,
                                          line,
                                          message,
                                          uncaught);
    Py_END_ALLOW_THREADS
    py_finish_hook (self);
    g_free (file_copy);
    g_free (message);
}

static PyObject *
py_raise_cb (PyObject *unused,
             PyObject *args)
{
    PyObject *code = NULL;
    PyObject *exc = NULL;
    int offset = 0;
    LrgScriptingPython *self;

    (void) unused;
    if (!PyArg_ParseTuple (args, "OiO", &code, &offset, &exc))
    {
        PyErr_Clear ();
        Py_RETURN_NONE;
    }
    self = py_current ();
    if (self == NULL || self->debugger == NULL || self->in_hook > 0)
        Py_RETURN_NONE;
    py_report_monitored (self, code, exc, FALSE);
    Py_RETURN_NONE;
}

static PyObject *
py_unwind_cb (PyObject *unused,
              PyObject *args)
{
    PyObject *code = NULL;
    PyObject *exc = NULL;
    int offset = 0;
    LrgScriptingPython *self;
    PyFrameObject *frame;
    PyFrameObject *back;
    PyObject *fcode;
    gboolean top;

    (void) unused;
    if (!PyArg_ParseTuple (args, "OiO", &code, &offset, &exc))
    {
        PyErr_Clear ();
        Py_RETURN_NONE;
    }
    self = py_current ();
    if (self == NULL || self->debugger == NULL || self->in_hook > 0)
        Py_RETURN_NONE;

    frame = PyThreadState_GetFrame (PyThreadState_Get ());
    back = NULL;
    top = FALSE;
    while (frame != NULL)
    {
        fcode = (PyObject *) PyFrame_GetCode (frame);
        if (fcode == code)
        {
            back = PyFrame_GetBack (frame);
            top = back == NULL;
            Py_XDECREF (back);
            Py_DECREF (fcode);
            Py_DECREF (frame);
            break;
        }
        Py_DECREF (fcode);
        back = PyFrame_GetBack (frame);
        Py_DECREF (frame);
        frame = back;
    }
    if (top)
        py_report_monitored (self, code, exc, TRUE);
    Py_RETURN_NONE;
}

static PyMethodDef py_line_def = { "lrg_py_line", py_line_cb, METH_VARARGS, NULL };
static PyMethodDef py_raise_def = { "lrg_py_raise", py_raise_cb, METH_VARARGS, NULL };
static PyMethodDef py_unwind_def = { "lrg_py_unwind", py_unwind_cb, METH_VARARGS, NULL };

static gboolean
py_call_ok (PyObject    *result,
            const gchar *what,
            GError     **error)
{
    gchar *msg;

    if (result != NULL)
    {
        Py_DECREF (result);
        return TRUE;
    }
    msg = lrg_python_get_error_message ();
    g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                 LRG_SCRIPT_DEBUGGER_ERROR_FAILED,
                 "sys.monitoring.%s failed: %s",
                 what, msg != NULL ? msg : "(unknown)");
    g_free (msg);
    return FALSE;
}

static gboolean
py_monitoring_init (GError **error)
{
    PyObject *sysmod;
    PyObject *events;
    PyObject *attr;

    if (py_monitoring != NULL)
        return TRUE;
    sysmod = PyImport_ImportModule ("sys");
    if (sysmod == NULL)
        return py_call_ok (NULL, "import sys", error);
    py_monitoring = PyObject_GetAttrString (sysmod, "monitoring");
    Py_DECREF (sysmod);
    if (py_monitoring == NULL)
        return py_call_ok (NULL, "sys.monitoring", error);
    events = PyObject_GetAttrString (py_monitoring, "events");
    if (events == NULL)
        return py_call_ok (NULL, "events", error);
    attr = PyObject_GetAttrString (events, "LINE");
    py_ev_line = (attr != NULL && PyLong_Check (attr)) ? (int) PyLong_AsLong (attr) : 0;
    Py_XDECREF (attr);
    attr = PyObject_GetAttrString (events, "RAISE");
    py_ev_raise = (attr != NULL && PyLong_Check (attr)) ? (int) PyLong_AsLong (attr) : 0;
    Py_XDECREF (attr);
    attr = PyObject_GetAttrString (events, "PY_UNWIND");
    py_ev_unwind = (attr != NULL && PyLong_Check (attr)) ? (int) PyLong_AsLong (attr) : 0;
    Py_XDECREF (attr);
    Py_DECREF (events);
    PyErr_Clear ();
    if (py_ev_line == 0)
    {
        Py_CLEAR (py_monitoring);
        g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                     LRG_SCRIPT_DEBUGGER_ERROR_FAILED,
                     "sys.monitoring.events.LINE is missing");
        return FALSE;
    }
    return TRUE;
}

static gboolean
py_tool_acquire (LrgScriptDebugger *debugger,
                 GError           **error)
{
    PyObject *result;

    if (py_tool_owner != NULL && py_tool_owner != debugger)
    {
        g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                     LRG_SCRIPT_DEBUGGER_ERROR_ALREADY_ATTACHED,
                     "another debugger owns Python's sys.monitoring tool id");
        return FALSE;
    }
    if (py_tool_owner == debugger)
    {
        py_tool_users++;
        return TRUE;
    }
    if (!py_monitoring_init (error))
        return FALSE;
    result = PyObject_CallMethod (py_monitoring, "use_tool_id", "is",
                                  LRG_PY_TOOL_ID, "libregnum");
    if (!py_call_ok (result, "use_tool_id", error))
        return FALSE;
    if (py_line_fn == NULL)
        py_line_fn = PyCFunction_New (&py_line_def, NULL);
    if (py_raise_fn == NULL)
        py_raise_fn = PyCFunction_New (&py_raise_def, NULL);
    if (py_unwind_fn == NULL)
        py_unwind_fn = PyCFunction_New (&py_unwind_def, NULL);
    result = PyObject_CallMethod (py_monitoring, "register_callback", "iiO",
                                  LRG_PY_TOOL_ID, py_ev_line, py_line_fn);
    if (!py_call_ok (result, "register_callback LINE", error))
        return FALSE;
    result = PyObject_CallMethod (py_monitoring, "register_callback", "iiO",
                                  LRG_PY_TOOL_ID, py_ev_raise, py_raise_fn);
    if (!py_call_ok (result, "register_callback RAISE", error))
        return FALSE;
    result = PyObject_CallMethod (py_monitoring, "register_callback", "iiO",
                                  LRG_PY_TOOL_ID, py_ev_unwind, py_unwind_fn);
    if (!py_call_ok (result, "register_callback PY_UNWIND", error))
        return FALSE;
    py_tool_owner = debugger;
    py_tool_users = 1;
    return TRUE;
}

static void
py_tool_release (void)
{
    PyObject *result;

    if (py_tool_users > 0)
        py_tool_users--;
    if (py_tool_users > 0 || py_tool_owner == NULL || py_monitoring == NULL)
        return;
    result = PyObject_CallMethod (py_monitoring, "set_events", "ii",
                                  LRG_PY_TOOL_ID, 0);
    Py_XDECREF (result);
    PyErr_Clear ();
    result = PyObject_CallMethod (py_monitoring, "free_tool_id", "i",
                                  LRG_PY_TOOL_ID);
    Py_XDECREF (result);
    PyErr_Clear ();
    py_tool_owner = NULL;
}

static void
py_apply_events (LrgScriptingPython *self)
{
    PyGILState_STATE state;
    PyObject *result;
    int mask = 0;
    LrgScriptExceptionMode mode;

    if (py_monitoring == NULL || py_tool_owner == NULL)
        return;
    if (self->in_hook > 0)
    {
        py_events_dirty = TRUE;
        return;
    }
    if (self->debugger != NULL)
    {
        if (lrg_script_debugger_wants_line_events (self->debugger))
            mask |= py_ev_line;
        mode = lrg_script_debugger_get_exception_mode (self->debugger);
        if (mode == LRG_SCRIPT_EXCEPTION_MODE_ALL)
            mask |= py_ev_raise;
        else if (mode == LRG_SCRIPT_EXCEPTION_MODE_UNCAUGHT)
            mask |= py_ev_unwind;
    }
    state = PyGILState_Ensure ();
    result = PyObject_CallMethod (py_monitoring, "set_events", "ii",
                                  LRG_PY_TOOL_ID, mask);
    Py_XDECREF (result);
    PyErr_Clear ();
    PyGILState_Release (state);
}

/*
 * 3.13+ PyFrame_GetLocals() is a FrameLocalsProxy, not a dict.  Dict keys
 * stay their Unicode text; other keys fall back to repr().
 */
static gchar *
py_key_name (PyObject *key)
{
    gchar *name = NULL;

    if (key != NULL && PyUnicode_Check (key))
    {
        const char *utf8 = PyUnicode_AsUTF8 (key);

        name = g_strdup (utf8 != NULL ? utf8 : "?");
    }
    else if (key != NULL)
    {
        PyObject *repr = PyObject_Repr (key);

        if (repr != NULL && PyUnicode_Check (repr))
        {
            const char *utf8 = PyUnicode_AsUTF8 (repr);

            name = g_strdup (utf8 != NULL ? utf8 : "?");
        }
        Py_XDECREF (repr);
        PyErr_Clear ();
    }
    if (name == NULL)
        name = g_strdup ("?");
    return name;
}

static GPtrArray *
py_dict_children (LrgScriptDebugger *debugger,
                  PyObject          *dict)
{
    GPtrArray *vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    PyObject *key;
    PyObject *value;
    Py_ssize_t pos = 0;

    if (dict == NULL)
        return vars;

    /* Mappings that are not real dicts (frame-local proxies). */
    if (!PyDict_Check (dict))
    {
        PyObject *items = PyMapping_Items (dict);
        Py_ssize_t n;
        Py_ssize_t i;

        if (items == NULL)
        {
            PyErr_Clear ();
            return vars;
        }
        n = PySequence_Size (items);
        if (n < 0)
        {
            PyErr_Clear ();
            Py_DECREF (items);
            return vars;
        }
        for (i = 0; i < n && vars->len < LRG_PY_CHILD_CAP; i++)
        {
            PyObject *pair = PySequence_GetItem (items, i);
            PyObject *item_key;
            PyObject *item_value;
            gchar *name;

            if (pair == NULL)
            {
                PyErr_Clear ();
                break;
            }
            item_key = PySequence_GetItem (pair, 0);
            item_value = PySequence_GetItem (pair, 1);
            name = py_key_name (item_key);
            if (item_value != NULL)
                g_ptr_array_add (vars, py_variable_from (debugger, name, item_value));
            g_free (name);
            Py_XDECREF (item_key);
            Py_XDECREF (item_value);
            Py_DECREF (pair);
        }
        Py_DECREF (items);
        return vars;
    }

    while (vars->len < LRG_PY_CHILD_CAP && PyDict_Next (dict, &pos, &key, &value))
    {
        gchar *name = NULL;

        name = py_key_name (key);
        g_ptr_array_add (vars, py_variable_from (debugger, name, value));
        g_free (name);
    }
    return vars;
}

static GPtrArray *
py_seq_children (LrgScriptDebugger *debugger,
                 PyObject          *seq)
{
    GPtrArray *vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    Py_ssize_t n;
    Py_ssize_t i;

    n = PySequence_Size (seq);
    if (n < 0)
    {
        PyErr_Clear ();
        return vars;
    }
    for (i = 0; i < n && vars->len < LRG_PY_CHILD_CAP; i++)
    {
        PyObject *item = PySequence_GetItem (seq, i);
        gchar *name;

        if (item == NULL)
        {
            PyErr_Clear ();
            continue;
        }
        name = g_strdup_printf ("[%d]", (int) i);
        g_ptr_array_add (vars, py_variable_from (debugger, name, item));
        g_free (name);
        Py_DECREF (item);
    }
    return vars;
}

static GPtrArray *
py_object_children (GObject *object)
{
    GPtrArray *vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    guint n_props = 0;
    GParamSpec **props;
    guint i;

    props = g_object_class_list_properties (G_OBJECT_GET_CLASS (object), &n_props);
    for (i = 0; i < n_props && vars->len < LRG_PY_CHILD_CAP; i++)
    {
        GValue value = G_VALUE_INIT;
        gchar *text;
        LrgScriptVariable *var;

        if ((props[i]->flags & G_PARAM_READABLE) == 0)
            continue;
        g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (props[i]));
        g_object_get_property (object, props[i]->name, &value);
        text = g_strdup_value_contents (&value);
        var = lrg_script_variable_new (props[i]->name,
                                       text != NULL ? text : "",
                                       g_type_name (G_PARAM_SPEC_VALUE_TYPE (props[i])),
                                       0);
        g_ptr_array_add (vars, var);
        g_free (text);
        g_value_unset (&value);
    }
    g_free (props);
    return vars;
}

static GPtrArray *
py_debug_frames (LrgScriptDebuggable *debuggable)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (debuggable);
    GPtrArray *frames = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    PyGILState_STATE state;
    PyFrameObject *frame;
    int level = 0;

    if (!self->initialized)
        return frames;
    state = PyGILState_Ensure ();
    frame = PyThreadState_GetFrame (PyThreadState_Get ());
    while (frame != NULL)
    {
        PyCodeObject *code = PyFrame_GetCode (frame);
        PyObject *filename = PyObject_GetAttrString ((PyObject *) code, "co_filename");
        PyObject *name = PyObject_GetAttrString ((PyObject *) code, "co_name");
        const char *file_utf = "";
        const char *name_utf = NULL;
        int line = PyFrame_GetLineNumber (frame);
        PyFrameObject *back;
        LrgScriptStackFrame *stack_frame;

        if (filename != NULL && PyUnicode_Check (filename))
        {
            const char *utf8 = PyUnicode_AsUTF8 (filename);

            if (utf8 != NULL)
                file_utf = utf8;
        }
        if (name != NULL && PyUnicode_Check (name))
            name_utf = PyUnicode_AsUTF8 (name);
        stack_frame = lrg_script_stack_frame_new (level, name_utf, file_utf, line);
        g_ptr_array_add (frames, stack_frame);
        Py_XDECREF (name);
        Py_XDECREF (filename);
        Py_DECREF (code);
        back = PyFrame_GetBack (frame);
        Py_DECREF (frame);
        frame = back;
        level++;
        PyErr_Clear ();
    }
    PyGILState_Release (state);
    return frames;
}

static GPtrArray *
py_debug_scopes (LrgScriptDebuggable *debuggable,
                 LrgScriptStackFrame *stack_frame)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (debuggable);
    GPtrArray *scopes = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    int level = stack_frame != NULL ? lrg_script_stack_frame_get_level (stack_frame) : 0;
    PyGILState_STATE state;
    PyFrameObject *frame;
    PyObject *locals;
    PyObject *globals;

    if (!self->initialized || self->debugger == NULL)
        return scopes;
    state = PyGILState_Ensure ();
    frame = py_frame_at (level);
    if (frame == NULL)
    {
        PyGILState_Release (state);
        return scopes;
    }
    locals = PyFrame_GetLocals (frame);
    globals = PyFrame_GetGlobals (frame);
    if (locals != NULL)
    {
        g_ptr_array_add (scopes, lrg_script_scope_new (
            "Locals",
            py_box (self->debugger, locals, PY_H_DICT),
            FALSE));
        Py_DECREF (locals);
    }
    if (globals != NULL)
    {
        g_ptr_array_add (scopes, lrg_script_scope_new (
            "Globals",
            py_box (self->debugger, globals, PY_H_DICT),
            TRUE));
        Py_DECREF (globals);
    }
    Py_DECREF (frame);
    PyErr_Clear ();
    PyGILState_Release (state);
    return scopes;
}

static GPtrArray *
py_debug_variables (LrgScriptDebuggable *debuggable,
                    gpointer             handle_data)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (debuggable);
    PyHandle *handle = (PyHandle *) handle_data;
    PyGILState_STATE state;
    GPtrArray *vars;

    if (handle == NULL)
        return g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    if (handle->kind == PY_H_OBJECT && handle->object != NULL)
        return py_object_children (handle->object);
    state = PyGILState_Ensure ();
    if (handle->kind == PY_H_DICT && handle->obj != NULL)
        vars = py_dict_children (self->debugger, handle->obj);
    else if (handle->kind == PY_H_SEQ && handle->obj != NULL)
        vars = py_seq_children (self->debugger, handle->obj);
    else
        vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    PyGILState_Release (state);
    return vars;
}

static LrgScriptVariable *
py_debug_eval (LrgScriptDebuggable  *debuggable,
               LrgScriptStackFrame  *stack_frame,
               const gchar          *expression,
               GError              **error)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (debuggable);
    int level = stack_frame != NULL ? lrg_script_stack_frame_get_level (stack_frame) : 0;
    PyGILState_STATE state;
    PyFrameObject *frame = NULL;
    PyObject *locals = NULL;
    PyObject *globals = NULL;
    PyObject *compiled = NULL;
    PyObject *result = NULL;
    PyObject *copied = NULL;
    LrgScriptVariable *var = NULL;
    gchar *msg;

    state = PyGILState_Ensure ();
    frame = py_frame_at (level);
    if (frame != NULL)
    {
        locals = PyFrame_GetLocals (frame);
        globals = PyFrame_GetGlobals (frame);
    }
    if (globals == NULL && self->main_dict != NULL)
    {
        globals = self->main_dict;
        Py_INCREF (globals);
    }
    if (locals == NULL && globals != NULL)
    {
        locals = globals;
        Py_INCREF (locals);
    }
    /* EvalCode wants a real dict.  A frame-local proxy is copied for the read. */
    if (locals != NULL && !PyDict_Check (locals))
    {
        copied = PyObject_CallFunctionObjArgs ((PyObject *) &PyDict_Type, locals, NULL);
        if (copied != NULL)
        {
            Py_DECREF (locals);
            locals = copied;
            copied = NULL;
        }
        else
            PyErr_Clear ();
    }
    compiled = Py_CompileString (expression, "<eval>", Py_eval_input);
    if (compiled == NULL)
    {
        PyErr_Clear ();
        compiled = Py_CompileString (expression, "<eval>", Py_file_input);
    }
    if (compiled == NULL)
    {
        msg = lrg_python_get_error_message ();
        g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                     LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE,
                     "%s", msg != NULL ? msg : "syntax error");
        g_free (msg);
        goto done;
    }
    result = PyEval_EvalCode (compiled, globals, locals);
    if (result == NULL)
    {
        msg = lrg_python_get_error_message ();
        g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                     LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE,
                     "%s", msg != NULL ? msg : "evaluation failed");
        g_free (msg);
        goto done;
    }
    var = py_variable_from (self->debugger, "result", result);

done:
    Py_XDECREF (result);
    Py_XDECREF (compiled);
    Py_XDECREF (locals);
    Py_XDECREF (globals);
    Py_XDECREF (frame);
    PyGILState_Release (state);
    return var;
}

static gboolean
py_debug_attach (LrgScriptDebuggable  *debuggable,
                 LrgScriptDebugger    *debugger,
                 GError              **error)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (debuggable);
    PyGILState_STATE state;
    gboolean ok;

    if (self->debugger != NULL && self->debugger != debugger)
    {
        g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                     LRG_SCRIPT_DEBUGGER_ERROR_ALREADY_ATTACHED,
                     "Python context is already attached to a debugger");
        return FALSE;
    }
    state = PyGILState_Ensure ();
    ok = py_tool_acquire (debugger, error);
    PyGILState_Release (state);
    if (!ok)
        return FALSE;
    self->debugger = debugger;
    py_apply_events (self);
    return TRUE;
}

static void
py_debug_detach (LrgScriptDebuggable *debuggable,
                 LrgScriptDebugger   *debugger)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (debuggable);
    PyGILState_STATE state;

    if (self->debugger != NULL && self->debugger != debugger)
        return;
    self->debugger = NULL;
    state = PyGILState_Ensure ();
    py_tool_release ();
    PyGILState_Release (state);
}

static void
py_debug_update (LrgScriptDebuggable *debuggable)
{
    py_apply_events (LRG_SCRIPTING_PYTHON (debuggable));
}

#else

static gboolean
py_debug_attach (LrgScriptDebuggable  *debuggable,
                 LrgScriptDebugger    *debugger,
                 GError              **error)
{
    (void) debuggable;
    (void) debugger;
    g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                 LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED,
                 "Python %d.%d has no sys.monitoring; breakpoints need 3.12 or newer",
                 PY_MAJOR_VERSION, PY_MINOR_VERSION);
    return FALSE;
}

static void
py_debug_detach (LrgScriptDebuggable *debuggable,
                 LrgScriptDebugger   *debugger)
{
    (void) debuggable;
    (void) debugger;
}

static void
py_debug_update (LrgScriptDebuggable *debuggable)
{
    (void) debuggable;
}

static GPtrArray *
py_debug_frames (LrgScriptDebuggable *debuggable)
{
    (void) debuggable;
    return g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static GPtrArray *
py_debug_scopes (LrgScriptDebuggable *debuggable,
                 LrgScriptStackFrame *stack_frame)
{
    (void) debuggable;
    (void) stack_frame;
    return g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static GPtrArray *
py_debug_variables (LrgScriptDebuggable *debuggable,
                    gpointer             handle_data)
{
    (void) debuggable;
    (void) handle_data;
    return g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static LrgScriptVariable *
py_debug_eval (LrgScriptDebuggable  *debuggable,
               LrgScriptStackFrame  *stack_frame,
               const gchar          *expression,
               GError              **error)
{
    (void) debuggable;
    (void) stack_frame;
    (void) expression;
    g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                 LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED,
                 "Python %d.%d has no sys.monitoring; evaluation needs 3.12 or newer",
                 PY_MAJOR_VERSION, PY_MINOR_VERSION);
    return NULL;
}

#endif

static void
python_debuggable_init (LrgScriptDebuggableInterface *iface)
{
    iface->debug_attach = py_debug_attach;
    iface->debug_detach = py_debug_detach;
    iface->debug_update = py_debug_update;
    iface->debug_get_frames = py_debug_frames;
    iface->debug_get_scopes = py_debug_scopes;
    iface->debug_get_variables = py_debug_variables;
    iface->debug_evaluate = py_debug_eval;
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

    py_enter (self);
    result = PyRun_FileEx (fp, path, Py_file_input,
                           self->main_dict, self->main_dict, 1);

    if (result == NULL)
    {
        gboolean syntax;
        g_autofree gchar *msg = NULL;

        py_set_location_from_error (scripting, path);
        syntax = PyErr_ExceptionMatches (PyExc_SyntaxError);
        if (!syntax)
            py_note_host_exception (self, path);
        msg = lrg_python_get_error_message ();
        py_leave (self);
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     syntax ? LRG_SCRIPTING_ERROR_SYNTAX
                            : LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error in '%s': %s",
                     path, msg ? msg : "(unknown error)");
        return FALSE;
    }

    Py_DECREF (result);
    py_leave (self);

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
        g_autofree gchar *msg = NULL;

        py_set_location_from_error (scripting, name);
        msg = lrg_python_get_error_message ();
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_SYNTAX,
                     "Syntax error in '%s': %s",
                     name, msg ? msg : "(unknown error)");
        return FALSE;
    }

    /* Execute the compiled code */
    py_enter (self);
    result = PyEval_EvalCode (compiled, self->main_dict, self->main_dict);
    Py_DECREF (compiled);

    if (result == NULL)
    {
        g_autofree gchar *msg = NULL;

        py_set_location_from_error (scripting, name);
        py_note_host_exception (self, name);
        msg = lrg_python_get_error_message ();
        py_leave (self);
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error executing '%s': %s",
                     name, msg ? msg : "(unknown error)");
        return FALSE;
    }

    Py_DECREF (result);
    py_leave (self);

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
    py_enter (self);
    result = PyObject_CallObject (func, py_args);
    Py_DECREF (py_args);

    if (result == NULL)
    {
        g_autofree gchar *msg = NULL;

        py_set_location_from_error (scripting, func_name);
        py_note_host_exception (self, func_name);
        msg = lrg_python_get_error_message ();
        py_leave (self);
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
            py_leave (self);
            return FALSE;
        }
    }

    Py_DECREF (result);
    py_leave (self);

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
    PyObject            *py_func;

    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (func != NULL, FALSE);

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
    reg->ref_count = 1;
    reg->scripting = self;
    reg->func = func;
    reg->user_data = user_data;
    reg->name = g_strdup (name);

    /* Store in hash table for cleanup */
    g_hash_table_insert (self->registered_funcs, g_strdup (name), reg);

    /* Create a callable with its own method definition */
    py_func = lrg_python_new_c_function (name,
                                         (PyCFunction)c_function_wrapper,
                                         registered_func_ref (reg),
                                         "RegisteredCFunction",
                                         registered_func_unref);
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

/* Generic search-path vfunc: forwards to the sys.path helper */
static void
lrg_scripting_python_vfunc_add_search_path (LrgScripting *scripting,
                                            const gchar  *path)
{
    lrg_scripting_python_add_search_path (LRG_SCRIPTING_PYTHON (scripting), path);
}

/* A function exists when this context's globals hold a callable */
static gboolean
lrg_scripting_python_has_function (LrgScripting *scripting,
                                   const gchar  *func_name)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (scripting);
    PyObject           *func;

    if (!ensure_python_initialized (self))
    {
        return FALSE;
    }

    func = PyDict_GetItemString (self->main_dict, func_name);
    return func != NULL && PyCallable_Check (func);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static gboolean
lrg_scripting_python_reload_file (LrgScripting  *scripting,
                                  const gchar   *path,
                                  GError       **error)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (scripting);
    g_autofree gchar *source = NULL;
    PyObject *compiled = NULL;
    PyObject *snapshot = NULL;
    PyObject *state = NULL;
    PyObject *hook;
    PyObject *result;
    PyObject *args;
    gchar *msg;

    g_return_val_if_fail (path != NULL, FALSE);

    if (!ensure_python_initialized (self))
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "Python interpreter not initialized");
        return FALSE;
    }
    if (!g_file_get_contents (path, &source, NULL, error))
        return FALSE;

    /* Compile before anything runs, so a syntax error cannot mutate. */
    compiled = Py_CompileString (source, path, Py_file_input);
    if (compiled == NULL)
    {
        py_set_location_from_error (scripting, path);
        msg = lrg_python_get_error_message ();
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_SYNTAX,
                     "Syntax error in '%s': %s",
                     path, msg != NULL ? msg : "(unknown error)");
        g_free (msg);
        return FALSE;
    }

    hook = PyDict_GetItemString (self->main_dict, LRG_SCRIPT_HOOK_BEFORE_RELOAD);
    if (hook != NULL && PyCallable_Check (hook))
    {
        py_enter (self);
        state = PyObject_CallObject (hook, NULL);
        if (state == NULL)
        {
            msg = lrg_python_get_error_message ();
            lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "%s: %s failed: %s",
                       path, LRG_SCRIPT_HOOK_BEFORE_RELOAD,
                       msg != NULL ? msg : "(unknown)");
            g_free (msg);
        }
        py_leave (self);
    }

    snapshot = PyDict_Copy (self->main_dict);
    if (snapshot == NULL)
    {
        msg = lrg_python_get_error_message ();
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
                     "Failed to snapshot globals: %s",
                     msg != NULL ? msg : "(unknown)");
        g_free (msg);
        Py_DECREF (compiled);
        Py_XDECREF (state);
        return FALSE;
    }

    py_enter (self);
    result = PyEval_EvalCode (compiled, self->main_dict, self->main_dict);
    Py_DECREF (compiled);
    compiled = NULL;
    if (result == NULL)
    {
        py_set_location_from_error (scripting, path);
        py_note_host_exception (self, path);
        msg = lrg_python_get_error_message ();
        py_restore_dict (self->main_dict, snapshot);
        py_leave (self);
        Py_DECREF (snapshot);
        Py_XDECREF (state);
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error executing '%s': %s",
                     path, msg != NULL ? msg : "(unknown error)");
        g_free (msg);
        return FALSE;
    }
    Py_DECREF (result);

    hook = PyDict_GetItemString (self->main_dict, LRG_SCRIPT_HOOK_RELOAD);
    if (hook != NULL && PyCallable_Check (hook))
    {
        args = PyTuple_New (1);
        if (state != NULL)
            PyTuple_SET_ITEM (args, 0, state);
        else
        {
            Py_INCREF (Py_None);
            PyTuple_SET_ITEM (args, 0, Py_None);
        }
        state = NULL;
        result = PyObject_CallObject (hook, args);
        Py_DECREF (args);
        if (result == NULL)
        {
            py_set_location_from_error (scripting, path);
            py_note_host_exception (self, LRG_SCRIPT_HOOK_RELOAD);
            msg = lrg_python_get_error_message ();
            py_restore_dict (self->main_dict, snapshot);
            py_leave (self);
            Py_DECREF (snapshot);
            g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                         "Error calling '%s': %s",
                         LRG_SCRIPT_HOOK_RELOAD,
                         msg != NULL ? msg : "(unknown error)");
            g_free (msg);
            return FALSE;
        }
        Py_DECREF (result);
    }

    py_leave (self);
    Py_DECREF (snapshot);
    Py_XDECREF (state);
    return TRUE;
}

static void
lrg_scripting_python_finalize (GObject *object)
{
    LrgScriptingPython *self = LRG_SCRIPTING_PYTHON (object);

    if (self->debugger != NULL)
    {
        self->debugger = NULL;
#if PY_VERSION_HEX >= 0x030C0000
        py_tool_release ();
#endif
    }

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
    scripting_class->reload_file = lrg_scripting_python_reload_file;
    scripting_class->load_string = lrg_scripting_python_load_string;
    scripting_class->call_function = lrg_scripting_python_call_function;
    scripting_class->register_function = lrg_scripting_python_register_function;
    scripting_class->get_global = lrg_scripting_python_get_global;
    scripting_class->set_global = lrg_scripting_python_set_global;
    scripting_class->reset = lrg_scripting_python_reset;
    scripting_class->add_search_path = lrg_scripting_python_vfunc_add_search_path;
    scripting_class->has_function = lrg_scripting_python_has_function;
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
                                                     g_free, registered_func_release);
    self->initialized = FALSE;

    /* Initialize Python */
    ensure_python_initialized (self);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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
        py_enter (self);
        result = PyObject_CallObject (func, args);
        Py_DECREF (args);

        if (result == NULL)
        {
            g_autofree gchar *msg = NULL;

            py_note_host_exception (self, func_name);
            msg = lrg_python_get_error_message ();
            py_leave (self);
            lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                         "Update hook '%s' error: %s",
                         func_name, msg ? msg : "(unknown)");
        }
        else
        {
            Py_DECREF (result);
            py_leave (self);
        }
    }
}

/*
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

/*
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
