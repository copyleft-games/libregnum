/* lrg-python-bridge.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GObject <-> Python type conversion bridge.
 *
 * This file provides utilities for converting between GLib/GObject types
 * and Python values. It handles pushing C values to Python and
 * extracting C values from Python objects.
 */

#pragma once

#include <glib-object.h>

/* Python.h must be included before any standard headers */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

G_BEGIN_DECLS

/* ==========================================================================
 * GValue <-> Python Conversion
 * ========================================================================== */

/**
 * lrg_python_from_gvalue:
 * @value: the GValue to convert
 *
 * Converts a GValue to a Python object.
 *
 * Type mappings:
 * - G_TYPE_NONE -> None
 * - G_TYPE_BOOLEAN -> bool
 * - G_TYPE_INT, G_TYPE_UINT, G_TYPE_INT64, G_TYPE_UINT64 -> int
 * - G_TYPE_FLOAT, G_TYPE_DOUBLE -> float
 * - G_TYPE_STRING -> str
 * - G_TYPE_OBJECT -> GObjectWrapper
 *
 * Returns: (transfer full): a new Python object, or NULL on error
 */
PyObject * lrg_python_from_gvalue (const GValue *value);

/**
 * lrg_python_to_gvalue:
 * @obj: the Python object to convert
 * @value: (out): GValue to initialize and fill
 *
 * Converts a Python object to a GValue.
 * The GValue is initialized with the appropriate type.
 *
 * Type mappings:
 * - None -> G_TYPE_NONE
 * - bool -> G_TYPE_BOOLEAN
 * - int -> G_TYPE_INT64
 * - float -> G_TYPE_DOUBLE
 * - str -> G_TYPE_STRING
 * - GObjectWrapper -> G_TYPE_OBJECT
 *
 * Returns: %TRUE on success, %FALSE on unsupported type
 */
gboolean lrg_python_to_gvalue (PyObject *obj,
                               GValue   *value);

/**
 * lrg_python_to_gvalue_with_type:
 * @obj: the Python object to convert
 * @type: expected GType
 * @value: (out): GValue to initialize and fill
 *
 * Converts a Python object to a GValue of the specified type,
 * performing type coercion if possible.
 *
 * Returns: %TRUE on success, %FALSE on type mismatch or unsupported type
 */
gboolean lrg_python_to_gvalue_with_type (PyObject *obj,
                                         GType     type,
                                         GValue   *value);

/* ==========================================================================
 * GObject Handling
 * ========================================================================== */

/**
 * LrgGObjectWrapperType:
 *
 * The Python type object for GObject wrappers.
 * Must be initialized with lrg_python_register_gobject_type().
 */
extern PyTypeObject LrgGObjectWrapperType;

/**
 * lrg_python_wrap_gobject:
 * @object: (nullable): the GObject to wrap
 *
 * Wraps a GObject as a Python object with property access.
 *
 * The object is referenced (g_object_ref) when wrapped and will be
 * unreferenced when the Python wrapper is garbage collected.
 *
 * If @object is %NULL, returns Py_None.
 *
 * Returns: (transfer full): a new Python wrapper object
 */
PyObject * lrg_python_wrap_gobject (GObject *object);

/**
 * lrg_python_unwrap_gobject:
 * @obj: a Python object
 *
 * Extracts the GObject from a Python wrapper.
 *
 * Returns: (transfer none) (nullable): the GObject, or %NULL if not a wrapper
 */
GObject * lrg_python_unwrap_gobject (PyObject *obj);

/**
 * lrg_python_is_gobject:
 * @obj: a Python object
 *
 * Checks if the Python object is a GObject wrapper.
 *
 * Returns: %TRUE if the object is a GObject wrapper
 */
gboolean lrg_python_is_gobject (PyObject *obj);

/* ==========================================================================
 * Type Registration
 * ========================================================================== */

/**
 * lrg_python_register_gobject_type:
 *
 * Registers the GObject wrapper type with Python.
 * This must be called once before any GObjects are wrapped.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean lrg_python_register_gobject_type (void);

/**
 * lrg_python_register_bound_method_type:
 *
 * Registers the BoundMethod type with Python.
 * This must be called once before LrgScriptable methods are used.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean lrg_python_register_bound_method_type (void);

/* ==========================================================================
 * Error Handling
 * ========================================================================== */

/**
 * lrg_python_check_error:
 * @error: (out) (optional): return location for error
 *
 * Checks if a Python exception occurred and converts it to a GError.
 * Clears the Python error state if an error is found.
 *
 * Returns: %TRUE if an error occurred, %FALSE otherwise
 */
gboolean lrg_python_check_error (GError **error);

/**
 * lrg_python_get_error_message:
 *
 * Gets the current Python exception as a string.
 * Clears the Python error state.
 *
 * Returns: (transfer full) (nullable): the error message, or %NULL if no error
 */
gchar * lrg_python_get_error_message (void);

G_END_DECLS
