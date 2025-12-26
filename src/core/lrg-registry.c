/* lrg-registry.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Type registry implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

#include "lrg-registry.h"
#include "../lrg-log.h"

#include <stdarg.h>

struct _LrgRegistry
{
    GObject     parent_instance;

    /* Hash table mapping string names to GTypes */
    GHashTable *name_to_type;

    /* Reverse lookup: GType to name (for lookup_name) */
    GHashTable *type_to_name;
};

G_DEFINE_TYPE (LrgRegistry, lrg_registry, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_registry_finalize (GObject *object)
{
    LrgRegistry *self = LRG_REGISTRY (object);

    g_clear_pointer (&self->name_to_type, g_hash_table_unref);
    g_clear_pointer (&self->type_to_name, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_registry_parent_class)->finalize (object);
}

static void
lrg_registry_class_init (LrgRegistryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_registry_finalize;
}

static void
lrg_registry_init (LrgRegistry *self)
{
    /* Create hash table with string keys (copied) and GType values */
    self->name_to_type = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                NULL);

    /*
     * Reverse lookup table. Keys are GTypes cast to pointers,
     * values are the string names (borrowed from name_to_type).
     */
    self->type_to_name = g_hash_table_new (g_direct_hash,
                                           g_direct_equal);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_registry_new:
 *
 * Creates a new type registry.
 *
 * Returns: (transfer full): A new #LrgRegistry
 */
LrgRegistry *
lrg_registry_new (void)
{
    return g_object_new (LRG_TYPE_REGISTRY, NULL);
}

/* ==========================================================================
 * Public API - Type Registration
 * ========================================================================== */

/**
 * lrg_registry_register:
 * @self: an #LrgRegistry
 * @name: the string name to register (e.g., "player", "enemy")
 * @type: the #GType to associate with the name
 *
 * Registers a GType with a string name for data-driven instantiation.
 */
void
lrg_registry_register (LrgRegistry *self,
                       const gchar *name,
                       GType        type)
{
    gchar *name_copy;
    GType  old_type;

    g_return_if_fail (LRG_IS_REGISTRY (self));
    g_return_if_fail (name != NULL && name[0] != '\0');
    g_return_if_fail (type != G_TYPE_INVALID);

    /* Check if name is already registered */
    old_type = GPOINTER_TO_SIZE (g_hash_table_lookup (self->name_to_type, name));
    if (old_type != G_TYPE_INVALID)
    {
        /* Remove old reverse mapping */
        g_hash_table_remove (self->type_to_name, GSIZE_TO_POINTER (old_type));

        lrg_debug (LRG_LOG_DOMAIN_CORE,
                   "Overwriting registry entry '%s': %s -> %s",
                   name,
                   g_type_name (old_type),
                   g_type_name (type));
    }

    /* Insert name -> type mapping (g_strdup for key) */
    name_copy = g_strdup (name);
    g_hash_table_insert (self->name_to_type,
                         name_copy,
                         GSIZE_TO_POINTER (type));

    /* Insert reverse mapping (type -> name, name borrowed from name_to_type) */
    g_hash_table_insert (self->type_to_name,
                         GSIZE_TO_POINTER (type),
                         name_copy);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Registered type '%s' as '%s'",
               g_type_name (type),
               name);
}

/**
 * lrg_registry_unregister:
 * @self: an #LrgRegistry
 * @name: the string name to unregister
 *
 * Removes a type registration from the registry.
 *
 * Returns: %TRUE if the name was registered and removed
 */
gboolean
lrg_registry_unregister (LrgRegistry *self,
                         const gchar *name)
{
    GType type;

    g_return_val_if_fail (LRG_IS_REGISTRY (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    /* Look up type first for reverse mapping removal */
    type = GPOINTER_TO_SIZE (g_hash_table_lookup (self->name_to_type, name));
    if (type == G_TYPE_INVALID)
    {
        return FALSE;
    }

    /* Remove reverse mapping */
    g_hash_table_remove (self->type_to_name, GSIZE_TO_POINTER (type));

    /* Remove name -> type mapping */
    g_hash_table_remove (self->name_to_type, name);

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Unregistered type '%s'",
               name);

    return TRUE;
}

/**
 * lrg_registry_is_registered:
 * @self: an #LrgRegistry
 * @name: the string name to check
 *
 * Checks if a name is registered in the registry.
 *
 * Returns: %TRUE if the name is registered
 */
gboolean
lrg_registry_is_registered (LrgRegistry *self,
                            const gchar *name)
{
    g_return_val_if_fail (LRG_IS_REGISTRY (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_contains (self->name_to_type, name);
}

/* ==========================================================================
 * Public API - Type Lookup
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
GType
lrg_registry_lookup (LrgRegistry *self,
                     const gchar *name)
{
    g_return_val_if_fail (LRG_IS_REGISTRY (self), G_TYPE_INVALID);
    g_return_val_if_fail (name != NULL, G_TYPE_INVALID);

    return GPOINTER_TO_SIZE (g_hash_table_lookup (self->name_to_type, name));
}

/**
 * lrg_registry_lookup_name:
 * @self: an #LrgRegistry
 * @type: the #GType to look up
 *
 * Looks up the registered name for a GType.
 *
 * Returns: (nullable) (transfer none): The registered name, or %NULL
 */
const gchar *
lrg_registry_lookup_name (LrgRegistry *self,
                          GType        type)
{
    g_return_val_if_fail (LRG_IS_REGISTRY (self), NULL);
    g_return_val_if_fail (type != G_TYPE_INVALID, NULL);

    return g_hash_table_lookup (self->type_to_name, GSIZE_TO_POINTER (type));
}

/* ==========================================================================
 * Public API - Object Creation
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
 * Returns: (transfer full) (nullable): A new #GObject instance, or %NULL
 */
GObject *
lrg_registry_create (LrgRegistry *self,
                     const gchar *name,
                     const gchar *first_property_name,
                     ...)
{
    GType    type;
    GObject *object;
    va_list  args;

    g_return_val_if_fail (LRG_IS_REGISTRY (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    type = lrg_registry_lookup (self, name);
    if (type == G_TYPE_INVALID)
    {
        lrg_warning (LRG_LOG_DOMAIN_CORE,
                     "Cannot create object: type '%s' not registered",
                     name);
        return NULL;
    }

    /* Use g_object_new_valist for varargs construction */
    va_start (args, first_property_name);
    object = g_object_new_valist (type, first_property_name, args);
    va_end (args);

    return object;
}

/**
 * lrg_registry_create_with_properties:
 * @self: an #LrgRegistry
 * @name: the registered type name
 * @n_properties: number of properties
 * @names: (array length=n_properties): property names
 * @values: (array length=n_properties): property values
 *
 * Creates a new object with the specified properties.
 *
 * Returns: (transfer full) (nullable): A new #GObject instance, or %NULL
 */
GObject *
lrg_registry_create_with_properties (LrgRegistry  *self,
                                     const gchar  *name,
                                     guint         n_properties,
                                     const gchar **names,
                                     const GValue *values)
{
    GType type;

    g_return_val_if_fail (LRG_IS_REGISTRY (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    type = lrg_registry_lookup (self, name);
    if (type == G_TYPE_INVALID)
    {
        lrg_warning (LRG_LOG_DOMAIN_CORE,
                     "Cannot create object: type '%s' not registered",
                     name);
        return NULL;
    }

    return g_object_new_with_properties (type, n_properties, names, values);
}

/* ==========================================================================
 * Public API - Enumeration
 * ========================================================================== */

/**
 * lrg_registry_get_names:
 * @self: an #LrgRegistry
 *
 * Gets all registered type names.
 *
 * Returns: (transfer container) (element-type utf8): A list of registered names
 */
GList *
lrg_registry_get_names (LrgRegistry *self)
{
    g_return_val_if_fail (LRG_IS_REGISTRY (self), NULL);

    return g_hash_table_get_keys (self->name_to_type);
}

/**
 * lrg_registry_get_count:
 * @self: an #LrgRegistry
 *
 * Gets the number of registered types.
 *
 * Returns: The number of registered types
 */
guint
lrg_registry_get_count (LrgRegistry *self)
{
    g_return_val_if_fail (LRG_IS_REGISTRY (self), 0);

    return g_hash_table_size (self->name_to_type);
}

/**
 * lrg_registry_foreach:
 * @self: an #LrgRegistry
 * @func: (scope call): the function to call for each entry
 * @user_data: (closure): user data to pass to @func
 *
 * Calls @func for each registered type in the registry.
 */
void
lrg_registry_foreach (LrgRegistry            *self,
                      LrgRegistryForeachFunc  func,
                      gpointer                user_data)
{
    GHashTableIter iter;
    gpointer       key;
    gpointer       value;

    g_return_if_fail (LRG_IS_REGISTRY (self));
    g_return_if_fail (func != NULL);

    g_hash_table_iter_init (&iter, self->name_to_type);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        func ((const gchar *)key,
              GPOINTER_TO_SIZE (value),
              user_data);
    }
}

/* ==========================================================================
 * Public API - Bulk Operations
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
void
lrg_registry_register_builtin (LrgRegistry *self)
{
    g_return_if_fail (LRG_IS_REGISTRY (self));

    /*
     * TODO: Register built-in types as they are implemented.
     *
     * Examples:
     * lrg_registry_register (self, "game-object", LRG_TYPE_GAME_OBJECT);
     * lrg_registry_register (self, "transform", LRG_TYPE_TRANSFORM_COMPONENT);
     * lrg_registry_register (self, "sprite", LRG_TYPE_SPRITE_COMPONENT);
     * etc.
     */

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Built-in types registered (count: %u)",
               lrg_registry_get_count (self));
}

/**
 * lrg_registry_clear:
 * @self: an #LrgRegistry
 *
 * Removes all type registrations from the registry.
 */
void
lrg_registry_clear (LrgRegistry *self)
{
    g_return_if_fail (LRG_IS_REGISTRY (self));

    g_hash_table_remove_all (self->name_to_type);
    g_hash_table_remove_all (self->type_to_name);

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Registry cleared");
}
