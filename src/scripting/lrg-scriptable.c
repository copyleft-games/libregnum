/* lrg-scriptable.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the LrgScriptable interface.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-scriptable.h"
#include "lrg-scripting.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-scriptable
 * @title: LrgScriptable
 * @short_description: Interface for objects with custom script exposure
 *
 * #LrgScriptable is an interface that can be implemented by any
 * GObject that wants to expose custom methods to scripts, control
 * property access, or receive lifecycle hooks.
 *
 * ## Default Behavior
 *
 * Without implementing #LrgScriptable, objects exposed to scripts:
 * - Have all readable properties accessible via get
 * - Have all writable properties accessible via set
 * - Cannot expose custom methods
 *
 * ## Implementing LrgScriptable
 *
 * |[<!-- language="C" -->
 * static gboolean
 * my_object_attack (LrgScriptable  *self,
 *                   guint           n_args,
 *                   const GValue   *args,
 *                   GValue         *return_value,
 *                   GError        **error)
 * {
 *     // Implementation...
 *     g_value_init (return_value, G_TYPE_INT);
 *     g_value_set_int (return_value, damage);
 *     return TRUE;
 * }
 *
 * static const LrgScriptMethod my_object_methods[] = {
 *     LRG_SCRIPT_METHOD ("attack", my_object_attack, "Attack a target", 1),
 *     LRG_SCRIPT_METHOD_END
 * };
 *
 * static const LrgScriptMethod *
 * my_object_get_script_methods (LrgScriptable *scriptable,
 *                               guint         *n_methods)
 * {
 *     *n_methods = G_N_ELEMENTS (my_object_methods) - 1;
 *     return my_object_methods;
 * }
 *
 * static LrgScriptAccessFlags
 * my_object_get_property_access (LrgScriptable *scriptable,
 *                                const gchar   *property_name)
 * {
 *     // Hide internal-state from scripts
 *     if (g_strcmp0 (property_name, "internal-state") == 0)
 *         return LRG_SCRIPT_ACCESS_NONE;
 *
 *     // Make health read-only
 *     if (g_strcmp0 (property_name, "health") == 0)
 *         return LRG_SCRIPT_ACCESS_READ;
 *
 *     // Use default for other properties
 *     return lrg_scriptable_default_get_property_access (scriptable, property_name);
 * }
 *
 * static void
 * my_object_scriptable_init (LrgScriptableInterface *iface)
 * {
 *     iface->get_script_methods = my_object_get_script_methods;
 *     iface->get_property_access = my_object_get_property_access;
 * }
 *
 * G_DEFINE_TYPE_WITH_CODE (MyObject, my_object, G_TYPE_OBJECT,
 *     G_IMPLEMENT_INTERFACE (LRG_TYPE_SCRIPTABLE, my_object_scriptable_init))
 * ]|
 */

G_DEFINE_INTERFACE (LrgScriptable, lrg_scriptable, G_TYPE_OBJECT)

static void
lrg_scriptable_default_init (LrgScriptableInterface *iface)
{
    /* All methods are optional - set defaults to NULL */
    iface->get_script_methods = NULL;
    iface->get_property_access = NULL;  /* Will use default helper when NULL */
    iface->on_script_attach = NULL;
    iface->on_script_detach = NULL;
}

/* ==========================================================================
 * Public API
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
const LrgScriptMethod *
lrg_scriptable_get_script_methods (LrgScriptable *self,
                                    guint         *n_methods)
{
    LrgScriptableInterface *iface;

    g_return_val_if_fail (LRG_IS_SCRIPTABLE (self), NULL);

    if (n_methods != NULL)
    {
        *n_methods = 0;
    }

    iface = LRG_SCRIPTABLE_GET_IFACE (self);

    if (iface->get_script_methods == NULL)
    {
        return NULL;
    }

    return iface->get_script_methods (self, n_methods);
}

/**
 * lrg_scriptable_get_property_access:
 * @self: a #LrgScriptable
 * @property_name: the property name to query
 *
 * Gets the script access flags for a property.
 *
 * Returns: access flags for the property
 */
LrgScriptAccessFlags
lrg_scriptable_get_property_access (LrgScriptable *self,
                                     const gchar   *property_name)
{
    LrgScriptableInterface *iface;

    g_return_val_if_fail (LRG_IS_SCRIPTABLE (self), LRG_SCRIPT_ACCESS_NONE);
    g_return_val_if_fail (property_name != NULL, LRG_SCRIPT_ACCESS_NONE);

    iface = LRG_SCRIPTABLE_GET_IFACE (self);

    /* Use default implementation if not overridden */
    if (iface->get_property_access == NULL)
    {
        return lrg_scriptable_default_get_property_access (self, property_name);
    }

    return iface->get_property_access (self, property_name);
}

/**
 * lrg_scriptable_on_script_attach:
 * @self: a #LrgScriptable
 * @scripting: the #LrgScripting context
 *
 * Notifies the object it has been exposed to a script context.
 */
void
lrg_scriptable_on_script_attach (LrgScriptable *self,
                                  LrgScripting  *scripting)
{
    LrgScriptableInterface *iface;

    g_return_if_fail (LRG_IS_SCRIPTABLE (self));
    g_return_if_fail (LRG_IS_SCRIPTING (scripting));

    iface = LRG_SCRIPTABLE_GET_IFACE (self);

    if (iface->on_script_attach != NULL)
    {
        iface->on_script_attach (self, scripting);
    }
}

/**
 * lrg_scriptable_on_script_detach:
 * @self: a #LrgScriptable
 * @scripting: the #LrgScripting context
 *
 * Notifies the object it has been removed from a script context.
 */
void
lrg_scriptable_on_script_detach (LrgScriptable *self,
                                  LrgScripting  *scripting)
{
    LrgScriptableInterface *iface;

    g_return_if_fail (LRG_IS_SCRIPTABLE (self));
    g_return_if_fail (LRG_IS_SCRIPTING (scripting));

    iface = LRG_SCRIPTABLE_GET_IFACE (self);

    if (iface->on_script_detach != NULL)
    {
        iface->on_script_detach (self, scripting);
    }
}

/**
 * lrg_scriptable_find_method:
 * @self: a #LrgScriptable
 * @method_name: the name of the method to find
 *
 * Finds a script method by name.
 *
 * Returns: (transfer none) (nullable): the method descriptor, or %NULL
 */
const LrgScriptMethod *
lrg_scriptable_find_method (LrgScriptable *self,
                             const gchar   *method_name)
{
    const LrgScriptMethod *methods;
    guint                  n_methods;
    guint                  i;

    g_return_val_if_fail (LRG_IS_SCRIPTABLE (self), NULL);
    g_return_val_if_fail (method_name != NULL, NULL);

    methods = lrg_scriptable_get_script_methods (self, &n_methods);

    if (methods == NULL || n_methods == 0)
    {
        return NULL;
    }

    for (i = 0; i < n_methods; i++)
    {
        if (g_strcmp0 (methods[i].name, method_name) == 0)
        {
            return &methods[i];
        }
    }

    return NULL;
}

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
gboolean
lrg_scriptable_invoke_method (LrgScriptable  *self,
                               const gchar    *method_name,
                               guint           n_args,
                               const GValue   *args,
                               GValue         *return_value,
                               GError        **error)
{
    const LrgScriptMethod *method;

    g_return_val_if_fail (LRG_IS_SCRIPTABLE (self), FALSE);
    g_return_val_if_fail (method_name != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    method = lrg_scriptable_find_method (self, method_name);

    if (method == NULL)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Method '%s' not found on %s",
                     method_name, G_OBJECT_TYPE_NAME (self));
        return FALSE;
    }

    /* Validate argument count if specified (>= 0 means exact count) */
    if (method->n_params >= 0 && (gint)n_args != method->n_params)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_TYPE,
                     "Method '%s' expects %d arguments, got %u",
                     method_name, method->n_params, n_args);
        return FALSE;
    }

    return method->func (self, n_args, args, return_value, error);
}

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
LrgScriptAccessFlags
lrg_scriptable_default_get_property_access (LrgScriptable *self,
                                             const gchar   *property_name)
{
    GObjectClass         *klass;
    GParamSpec           *pspec;
    LrgScriptAccessFlags  flags;

    g_return_val_if_fail (G_IS_OBJECT (self), LRG_SCRIPT_ACCESS_NONE);
    g_return_val_if_fail (property_name != NULL, LRG_SCRIPT_ACCESS_NONE);

    klass = G_OBJECT_GET_CLASS (self);
    pspec = g_object_class_find_property (klass, property_name);

    if (pspec == NULL)
    {
        return LRG_SCRIPT_ACCESS_NONE;
    }

    flags = LRG_SCRIPT_ACCESS_NONE;

    if (pspec->flags & G_PARAM_READABLE)
    {
        flags |= LRG_SCRIPT_ACCESS_READ;
    }

    if (pspec->flags & G_PARAM_WRITABLE)
    {
        flags |= LRG_SCRIPT_ACCESS_WRITE;
    }

    return flags;
}
