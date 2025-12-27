/* lrg-scriptable.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects with custom script exposure.
 *
 * Objects implementing this interface can:
 * - Expose custom callable methods to scripts
 * - Control which properties are accessible from scripts
 * - Receive lifecycle callbacks when exposed to script contexts
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTABLE (lrg_scriptable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgScriptable, lrg_scriptable, LRG, SCRIPTABLE, GObject)

/**
 * LrgScriptMethodFunc:
 * @self: the scriptable object
 * @n_args: number of arguments passed from script
 * @args: (array length=n_args): array of argument values
 * @return_value: (out): location to store return value (uninitialized)
 * @error: (nullable): return location for error
 *
 * Callback signature for script-callable methods.
 *
 * The @return_value should be initialized with g_value_init() if the
 * method returns a value. Leave uninitialized for void methods.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
typedef gboolean (*LrgScriptMethodFunc) (LrgScriptable  *self,
                                          guint           n_args,
                                          const GValue   *args,
                                          GValue         *return_value,
                                          GError        **error);

/**
 * LrgScriptMethod:
 * @name: method name as exposed to scripts
 * @func: the C function to invoke
 * @doc: (nullable): optional documentation string
 * @n_params: expected number of parameters (-1 for variadic)
 *
 * Descriptor for a script-callable method.
 *
 * Use the LRG_SCRIPT_METHOD() macro to define these conveniently.
 */
struct _LrgScriptMethod
{
    const gchar         *name;
    LrgScriptMethodFunc  func;
    const gchar         *doc;
    gint                 n_params;
};

/**
 * LRG_SCRIPT_METHOD:
 * @_name: method name (string literal)
 * @_func: the function to call
 * @_doc: documentation string (string literal or NULL)
 * @_n_params: expected parameter count (-1 for variadic)
 *
 * Convenience macro to define a script method descriptor.
 *
 * |[<!-- language="C" -->
 * static const LrgScriptMethod my_methods[] = {
 *     LRG_SCRIPT_METHOD ("attack", my_attack, "Attack a target", 1),
 *     LRG_SCRIPT_METHOD ("heal", my_heal, "Heal self", 1),
 *     LRG_SCRIPT_METHOD_END
 * };
 * ]|
 */
#define LRG_SCRIPT_METHOD(_name, _func, _doc, _n_params) \
    { (_name), (_func), (_doc), (_n_params) }

/**
 * LRG_SCRIPT_METHOD_END:
 *
 * Sentinel value to mark end of method array.
 */
#define LRG_SCRIPT_METHOD_END { NULL, NULL, NULL, 0 }

/**
 * LrgScriptableInterface:
 * @parent_iface: parent interface
 * @get_script_methods: returns array of available script methods
 * @get_property_access: returns access flags for a property
 * @on_script_attach: called when object is exposed to a script context
 * @on_script_detach: called when object is removed from a script context
 *
 * Interface structure for #LrgScriptable.
 *
 * All methods are optional and have sensible default implementations:
 * - get_script_methods: returns NULL (no custom methods)
 * - get_property_access: returns flags based on GParamSpec
 * - on_script_attach: no-op
 * - on_script_detach: no-op
 */
struct _LrgScriptableInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgScriptableInterface::get_script_methods:
     * @self: a #LrgScriptable
     * @n_methods: (out): return location for number of methods
     *
     * Returns an array of script method descriptors.
     *
     * The returned array is owned by the object and must remain valid
     * for the lifetime of the object. Return %NULL if no custom methods.
     *
     * Returns: (transfer none) (array length=n_methods) (nullable):
     *     array of #LrgScriptMethod, or %NULL
     */
    const LrgScriptMethod * (*get_script_methods) (LrgScriptable *self,
                                                    guint         *n_methods);

    /**
     * LrgScriptableInterface::get_property_access:
     * @self: a #LrgScriptable
     * @property_name: the property name to query
     *
     * Returns the script access flags for a property.
     *
     * The default implementation allows all readable/writable properties
     * to be accessed with their normal flags. Override to restrict or
     * modify access.
     *
     * Return %LRG_SCRIPT_ACCESS_NONE to completely hide a property.
     *
     * Returns: access flags for the property
     */
    LrgScriptAccessFlags (*get_property_access) (LrgScriptable *self,
                                                  const gchar   *property_name);

    /**
     * LrgScriptableInterface::on_script_attach:
     * @self: a #LrgScriptable
     * @scripting: the #LrgScripting context
     *
     * Called when the object is first exposed to a script context.
     *
     * This allows objects to perform initialization like registering
     * additional callbacks or setting up script-side state.
     */
    void (*on_script_attach) (LrgScriptable *self,
                              LrgScripting  *scripting);

    /**
     * LrgScriptableInterface::on_script_detach:
     * @self: a #LrgScriptable
     * @scripting: the #LrgScripting context
     *
     * Called when the object is removed from a script context.
     *
     * This allows objects to clean up any script-side state.
     */
    void (*on_script_detach) (LrgScriptable *self,
                              LrgScripting  *scripting);
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_scriptable_get_script_methods:
 * @self: a #LrgScriptable
 * @n_methods: (out) (optional): return location for number of methods
 *
 * Gets the script methods exposed by this object.
 *
 * Returns: (transfer none) (array length=n_methods) (nullable):
 *     array of #LrgScriptMethod, or %NULL if no custom methods
 */
LRG_AVAILABLE_IN_ALL
const LrgScriptMethod * lrg_scriptable_get_script_methods (LrgScriptable *self,
                                                            guint         *n_methods);

/**
 * lrg_scriptable_get_property_access:
 * @self: a #LrgScriptable
 * @property_name: the property name to query
 *
 * Gets the script access flags for a property.
 *
 * Returns: access flags for the property
 */
LRG_AVAILABLE_IN_ALL
LrgScriptAccessFlags lrg_scriptable_get_property_access (LrgScriptable *self,
                                                          const gchar   *property_name);

/**
 * lrg_scriptable_on_script_attach:
 * @self: a #LrgScriptable
 * @scripting: the #LrgScripting context
 *
 * Notifies the object it has been exposed to a script context.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scriptable_on_script_attach (LrgScriptable *self,
                                       LrgScripting  *scripting);

/**
 * lrg_scriptable_on_script_detach:
 * @self: a #LrgScriptable
 * @scripting: the #LrgScripting context
 *
 * Notifies the object it has been removed from a script context.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scriptable_on_script_detach (LrgScriptable *self,
                                       LrgScripting  *scripting);

/* ==========================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * lrg_scriptable_find_method:
 * @self: a #LrgScriptable
 * @method_name: the name of the method to find
 *
 * Finds a script method by name.
 *
 * Returns: (transfer none) (nullable): the method descriptor, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const LrgScriptMethod * lrg_scriptable_find_method (LrgScriptable *self,
                                                     const gchar   *method_name);

/**
 * lrg_scriptable_invoke_method:
 * @self: a #LrgScriptable
 * @method_name: the name of the method to invoke
 * @n_args: number of arguments
 * @args: (array length=n_args) (nullable): array of arguments
 * @return_value: (out) (optional): location to store return value
 * @error: (nullable): return location for error
 *
 * Invokes a script method by name.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scriptable_invoke_method (LrgScriptable  *self,
                                        const gchar    *method_name,
                                        guint           n_args,
                                        const GValue   *args,
                                        GValue         *return_value,
                                        GError        **error);

/**
 * lrg_scriptable_default_get_property_access:
 * @self: a #LrgScriptable
 * @property_name: the property name to query
 *
 * Default implementation for get_property_access().
 *
 * Returns access flags based on the property's GParamSpec:
 * - LRG_SCRIPT_ACCESS_READ if G_PARAM_READABLE
 * - LRG_SCRIPT_ACCESS_WRITE if G_PARAM_WRITABLE
 *
 * Implementations can call this as a fallback.
 *
 * Returns: access flags for the property
 */
LRG_AVAILABLE_IN_ALL
LrgScriptAccessFlags lrg_scriptable_default_get_property_access (LrgScriptable *self,
                                                                  const gchar   *property_name);

G_END_DECLS
