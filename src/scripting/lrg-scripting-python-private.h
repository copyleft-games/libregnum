/* lrg-scripting-python-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private types and functions for Python scripting backend.
 */

#pragma once

#include <glib-object.h>

/* Python.h must be included before any standard headers */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "lrg-scripting-python.h"

G_BEGIN_DECLS

/*
 * RegisteredCFunction:
 *
 * Internal structure to track C functions registered with Python.
 * This is stored via PyCapsule so we can access it from the Python wrapper.
 */
typedef struct
{
    LrgScriptingPython    *scripting;   /* Weak reference to scripting context */
    LrgScriptingCFunction  func;        /* The C function to call */
    gpointer               user_data;   /* User data to pass to the function */
    gchar                 *name;        /* Function name for error messages */
} RegisteredCFunction;

/*
 * GObjectWrapper:
 *
 * Python object wrapper for GObjects.
 * This allows GObjects to be passed to and from Python scripts.
 */
typedef struct
{
    PyObject_HEAD
    GObject *gobject;   /* The wrapped GObject (ref counted) */
} GObjectWrapper;

/*
 * lrg_scripting_python_get_main_dict:
 * @self: an #LrgScriptingPython
 *
 * Gets the main module's __dict__ (globals dictionary).
 *
 * Returns: (transfer none): the main dict PyObject
 */
PyObject * lrg_scripting_python_get_main_dict (LrgScriptingPython *self);

/*
 * lrg_scripting_python_get_module:
 * @self: an #LrgScriptingPython
 *
 * Gets the libregnum module.
 *
 * Returns: (transfer none): the libregnum module PyObject
 */
PyObject * lrg_scripting_python_get_module (LrgScriptingPython *self);

G_END_DECLS
