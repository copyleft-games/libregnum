/* lrg-registry.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Type registry for data-driven object instantiation.
 *
 * The registry maps string type names to GTypes, allowing objects
 * to be created from YAML files without hardcoding type references.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_REGISTRY (lrg_registry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRegistry, lrg_registry, LRG, REGISTRY, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_registry_new:
 *
 * Creates a new type registry.
 *
 * Returns: (transfer full): A new #LrgRegistry
 */
LRG_AVAILABLE_IN_ALL
LrgRegistry * lrg_registry_new (void);

/* ==========================================================================
 * Type Registration
 * ========================================================================== */

/**
 * lrg_registry_register:
 * @self: an #LrgRegistry
 * @name: the string name to register (e.g., "player", "enemy")
 * @type: the #GType to associate with the name
 *
 * Registers a GType with a string name for data-driven instantiation.
 *
 * The name should be a simple identifier that will appear in YAML
 * data files. For example, registering MY_TYPE_PLAYER as "player"
 * allows YAML files to specify `type: player` to create instances.
 *
 * If the name is already registered, it will be overwritten with
 * the new type. This allows mods to override base game types.
 */
LRG_AVAILABLE_IN_ALL
void lrg_registry_register (LrgRegistry *self,
                            const gchar *name,
                            GType        type);

/**
 * lrg_registry_unregister:
 * @self: an #LrgRegistry
 * @name: the string name to unregister
 *
 * Removes a type registration from the registry.
 *
 * Returns: %TRUE if the name was registered and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_registry_unregister (LrgRegistry *self,
                                  const gchar *name);

/**
 * lrg_registry_is_registered:
 * @self: an #LrgRegistry
 * @name: the string name to check
 *
 * Checks if a name is registered in the registry.
 *
 * Returns: %TRUE if the name is registered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_registry_is_registered (LrgRegistry *self,
                                     const gchar *name);

/* ==========================================================================
 * Type Lookup
 * ========================================================================== */

/**
 * lrg_registry_lookup:
 * @self: an #LrgRegistry
 * @name: the string name to look up
 *
 * Looks up a GType by its registered string name.
 *
 * Returns: The registered #GType, or %G_TYPE_INVALID if not found
 */
LRG_AVAILABLE_IN_ALL
GType lrg_registry_lookup (LrgRegistry *self,
                           const gchar *name);

/**
 * lrg_registry_lookup_name:
 * @self: an #LrgRegistry
 * @type: the #GType to look up
 *
 * Looks up the registered name for a GType.
 *
 * If multiple names are registered for the same type, returns
 * the first one found (order is not guaranteed).
 *
 * Returns: (nullable) (transfer none): The registered name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_registry_lookup_name (LrgRegistry *self,
                                        GType        type);

/* ==========================================================================
 * Object Creation
 * ========================================================================== */

/**
 * lrg_registry_create:
 * @self: an #LrgRegistry
 * @name: the registered type name
 * @first_property_name: (nullable): name of first property to set
 * @...: value of first property, then additional name/value pairs, NULL-terminated
 *
 * Creates a new object of the type registered under @name.
 *
 * This is a convenience wrapper around g_object_new() that looks
 * up the type from the registry first.
 *
 * Returns: (transfer full) (nullable): A new #GObject instance, or %NULL
 *          if the name is not registered
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_registry_create (LrgRegistry *self,
                               const gchar *name,
                               const gchar *first_property_name,
                               ...);

/**
 * lrg_registry_create_with_properties:
 * @self: an #LrgRegistry
 * @name: the registered type name
 * @n_properties: number of properties
 * @names: (array length=n_properties): property names
 * @values: (array length=n_properties): property values
 *
 * Creates a new object of the type registered under @name with
 * the specified properties.
 *
 * Returns: (transfer full) (nullable): A new #GObject instance, or %NULL
 *          if the name is not registered
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_registry_create_with_properties (LrgRegistry  *self,
                                               const gchar  *name,
                                               guint         n_properties,
                                               const gchar **names,
                                               const GValue *values);

/* ==========================================================================
 * Enumeration
 * ========================================================================== */

/**
 * lrg_registry_get_names:
 * @self: an #LrgRegistry
 *
 * Gets all registered type names.
 *
 * Returns: (transfer container) (element-type utf8): A list of registered
 *          names. Free with g_list_free() (but not the strings themselves).
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_registry_get_names (LrgRegistry *self);

/**
 * lrg_registry_get_count:
 * @self: an #LrgRegistry
 *
 * Gets the number of registered types.
 *
 * Returns: The number of registered types
 */
LRG_AVAILABLE_IN_ALL
guint lrg_registry_get_count (LrgRegistry *self);

/**
 * LrgRegistryForeachFunc:
 * @name: the registered name
 * @type: the registered #GType
 * @user_data: user-provided data
 *
 * Callback function for lrg_registry_foreach().
 */
typedef void (*LrgRegistryForeachFunc) (const gchar *name,
                                        GType        type,
                                        gpointer     user_data);

/**
 * lrg_registry_foreach:
 * @self: an #LrgRegistry
 * @func: (scope call): the function to call for each entry
 * @user_data: (closure): user data to pass to @func
 *
 * Calls @func for each registered type in the registry.
 */
LRG_AVAILABLE_IN_ALL
void lrg_registry_foreach (LrgRegistry            *self,
                           LrgRegistryForeachFunc  func,
                           gpointer                user_data);

/* ==========================================================================
 * Bulk Operations
 * ========================================================================== */

/**
 * lrg_registry_register_builtin:
 * @self: an #LrgRegistry
 *
 * Registers all built-in Libregnum types.
 *
 * This is called automatically during engine startup but can
 * also be called manually for testing purposes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_registry_register_builtin (LrgRegistry *self);

/**
 * lrg_registry_clear:
 * @self: an #LrgRegistry
 *
 * Removes all type registrations from the registry.
 */
LRG_AVAILABLE_IN_ALL
void lrg_registry_clear (LrgRegistry *self);

G_END_DECLS
