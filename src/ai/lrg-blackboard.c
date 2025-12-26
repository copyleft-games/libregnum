/* lrg-blackboard.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Blackboard implementation for behavior tree data sharing.
 */

#include "lrg-blackboard.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

/*
 * BlackboardEntry:
 *
 * Internal structure for storing blackboard values.
 */
typedef enum
{
    ENTRY_TYPE_INT,
    ENTRY_TYPE_FLOAT,
    ENTRY_TYPE_BOOL,
    ENTRY_TYPE_STRING,
    ENTRY_TYPE_OBJECT,
    ENTRY_TYPE_POINTER
} EntryType;

typedef struct
{
    EntryType type;
    union
    {
        gint           int_val;
        gfloat         float_val;
        gboolean       bool_val;
        gchar         *string_val;
        GObject       *object_val;
        gpointer       pointer_val;
    } value;
    GDestroyNotify destroy;
} BlackboardEntry;

struct _LrgBlackboard
{
    GObject      parent_instance;

    GHashTable  *entries;  /* gchar* -> BlackboardEntry* */
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBlackboard, lrg_blackboard, G_TYPE_OBJECT)
#pragma GCC visibility pop

/*
 * entry_free:
 *
 * Frees a blackboard entry and its resources.
 */
static void
entry_free (BlackboardEntry *entry)
{
    if (entry == NULL)
        return;

    switch (entry->type)
    {
    case ENTRY_TYPE_STRING:
        g_free (entry->value.string_val);
        break;

    case ENTRY_TYPE_OBJECT:
        g_clear_object (&entry->value.object_val);
        break;

    case ENTRY_TYPE_POINTER:
        if (entry->destroy && entry->value.pointer_val)
            entry->destroy (entry->value.pointer_val);
        break;

    default:
        break;
    }

    g_free (entry);
}

static void
lrg_blackboard_finalize (GObject *object)
{
    LrgBlackboard *self = LRG_BLACKBOARD (object);

    g_clear_pointer (&self->entries, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_blackboard_parent_class)->finalize (object);
}

static void
lrg_blackboard_class_init (LrgBlackboardClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_blackboard_finalize;
}

static void
lrg_blackboard_init (LrgBlackboard *self)
{
    self->entries = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           (GDestroyNotify) entry_free);
}

/**
 * lrg_blackboard_new:
 *
 * Creates a new blackboard for storing behavior tree data.
 *
 * Returns: (transfer full): A new #LrgBlackboard
 */
LrgBlackboard *
lrg_blackboard_new (void)
{
    return g_object_new (LRG_TYPE_BLACKBOARD, NULL);
}

/**
 * lrg_blackboard_set_int:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: The integer value
 *
 * Sets an integer value in the blackboard.
 */
void
lrg_blackboard_set_int (LrgBlackboard *self,
                        const gchar   *key,
                        gint           value)
{
    BlackboardEntry *entry;

    g_return_if_fail (LRG_IS_BLACKBOARD (self));
    g_return_if_fail (key != NULL);

    entry = g_new0 (BlackboardEntry, 1);
    entry->type = ENTRY_TYPE_INT;
    entry->value.int_val = value;

    g_hash_table_insert (self->entries, g_strdup (key), entry);
}

/**
 * lrg_blackboard_get_int:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @default_value: Default if key not found
 *
 * Gets an integer value from the blackboard.
 *
 * Returns: The integer value, or @default_value if not found
 */
gint
lrg_blackboard_get_int (LrgBlackboard *self,
                        const gchar   *key,
                        gint           default_value)
{
    BlackboardEntry *entry;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), default_value);
    g_return_val_if_fail (key != NULL, default_value);

    entry = g_hash_table_lookup (self->entries, key);
    if (entry == NULL || entry->type != ENTRY_TYPE_INT)
        return default_value;

    return entry->value.int_val;
}

/**
 * lrg_blackboard_set_float:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: The float value
 *
 * Sets a float value in the blackboard.
 */
void
lrg_blackboard_set_float (LrgBlackboard *self,
                          const gchar   *key,
                          gfloat         value)
{
    BlackboardEntry *entry;

    g_return_if_fail (LRG_IS_BLACKBOARD (self));
    g_return_if_fail (key != NULL);

    entry = g_new0 (BlackboardEntry, 1);
    entry->type = ENTRY_TYPE_FLOAT;
    entry->value.float_val = value;

    g_hash_table_insert (self->entries, g_strdup (key), entry);
}

/**
 * lrg_blackboard_get_float:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @default_value: Default if key not found
 *
 * Gets a float value from the blackboard.
 *
 * Returns: The float value, or @default_value if not found
 */
gfloat
lrg_blackboard_get_float (LrgBlackboard *self,
                          const gchar   *key,
                          gfloat         default_value)
{
    BlackboardEntry *entry;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), default_value);
    g_return_val_if_fail (key != NULL, default_value);

    entry = g_hash_table_lookup (self->entries, key);
    if (entry == NULL || entry->type != ENTRY_TYPE_FLOAT)
        return default_value;

    return entry->value.float_val;
}

/**
 * lrg_blackboard_set_bool:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: The boolean value
 *
 * Sets a boolean value in the blackboard.
 */
void
lrg_blackboard_set_bool (LrgBlackboard *self,
                         const gchar   *key,
                         gboolean       value)
{
    BlackboardEntry *entry;

    g_return_if_fail (LRG_IS_BLACKBOARD (self));
    g_return_if_fail (key != NULL);

    entry = g_new0 (BlackboardEntry, 1);
    entry->type = ENTRY_TYPE_BOOL;
    entry->value.bool_val = value;

    g_hash_table_insert (self->entries, g_strdup (key), entry);
}

/**
 * lrg_blackboard_get_bool:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @default_value: Default if key not found
 *
 * Gets a boolean value from the blackboard.
 *
 * Returns: The boolean value, or @default_value if not found
 */
gboolean
lrg_blackboard_get_bool (LrgBlackboard *self,
                         const gchar   *key,
                         gboolean       default_value)
{
    BlackboardEntry *entry;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), default_value);
    g_return_val_if_fail (key != NULL, default_value);

    entry = g_hash_table_lookup (self->entries, key);
    if (entry == NULL || entry->type != ENTRY_TYPE_BOOL)
        return default_value;

    return entry->value.bool_val;
}

/**
 * lrg_blackboard_set_string:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: (nullable): The string value
 *
 * Sets a string value in the blackboard.
 */
void
lrg_blackboard_set_string (LrgBlackboard *self,
                           const gchar   *key,
                           const gchar   *value)
{
    BlackboardEntry *entry;

    g_return_if_fail (LRG_IS_BLACKBOARD (self));
    g_return_if_fail (key != NULL);

    entry = g_new0 (BlackboardEntry, 1);
    entry->type = ENTRY_TYPE_STRING;
    entry->value.string_val = g_strdup (value);

    g_hash_table_insert (self->entries, g_strdup (key), entry);
}

/**
 * lrg_blackboard_get_string:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Gets a string value from the blackboard.
 *
 * Returns: (transfer none) (nullable): The string value, or %NULL if not found
 */
const gchar *
lrg_blackboard_get_string (LrgBlackboard *self,
                           const gchar   *key)
{
    BlackboardEntry *entry;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    entry = g_hash_table_lookup (self->entries, key);
    if (entry == NULL || entry->type != ENTRY_TYPE_STRING)
        return NULL;

    return entry->value.string_val;
}

/**
 * lrg_blackboard_set_object:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @object: (nullable) (transfer none): The GObject to store
 *
 * Sets a GObject reference in the blackboard.
 */
void
lrg_blackboard_set_object (LrgBlackboard *self,
                           const gchar   *key,
                           GObject       *object)
{
    BlackboardEntry *entry;

    g_return_if_fail (LRG_IS_BLACKBOARD (self));
    g_return_if_fail (key != NULL);
    g_return_if_fail (object == NULL || G_IS_OBJECT (object));

    entry = g_new0 (BlackboardEntry, 1);
    entry->type = ENTRY_TYPE_OBJECT;
    entry->value.object_val = object ? g_object_ref (object) : NULL;

    g_hash_table_insert (self->entries, g_strdup (key), entry);
}

/**
 * lrg_blackboard_get_object:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Gets a GObject from the blackboard.
 *
 * Returns: (transfer none) (nullable): The GObject, or %NULL if not found
 */
GObject *
lrg_blackboard_get_object (LrgBlackboard *self,
                           const gchar   *key)
{
    BlackboardEntry *entry;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    entry = g_hash_table_lookup (self->entries, key);
    if (entry == NULL || entry->type != ENTRY_TYPE_OBJECT)
        return NULL;

    return entry->value.object_val;
}

/**
 * lrg_blackboard_set_pointer:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @pointer: (nullable): The pointer to store
 * @destroy: (nullable): Destroy function for the pointer
 *
 * Sets an arbitrary pointer in the blackboard.
 */
void
lrg_blackboard_set_pointer (LrgBlackboard  *self,
                            const gchar    *key,
                            gpointer        pointer,
                            GDestroyNotify  destroy)
{
    BlackboardEntry *entry;

    g_return_if_fail (LRG_IS_BLACKBOARD (self));
    g_return_if_fail (key != NULL);

    entry = g_new0 (BlackboardEntry, 1);
    entry->type = ENTRY_TYPE_POINTER;
    entry->value.pointer_val = pointer;
    entry->destroy = destroy;

    g_hash_table_insert (self->entries, g_strdup (key), entry);
}

/**
 * lrg_blackboard_get_pointer:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Gets a pointer from the blackboard.
 *
 * Returns: (nullable): The pointer, or %NULL if not found
 */
gpointer
lrg_blackboard_get_pointer (LrgBlackboard *self,
                            const gchar   *key)
{
    BlackboardEntry *entry;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    entry = g_hash_table_lookup (self->entries, key);
    if (entry == NULL || entry->type != ENTRY_TYPE_POINTER)
        return NULL;

    return entry->value.pointer_val;
}

/**
 * lrg_blackboard_has_key:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Checks if a key exists in the blackboard.
 *
 * Returns: %TRUE if key exists
 */
gboolean
lrg_blackboard_has_key (LrgBlackboard *self,
                        const gchar   *key)
{
    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_contains (self->entries, key);
}

/**
 * lrg_blackboard_remove:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Removes a key from the blackboard.
 *
 * Returns: %TRUE if key was removed
 */
gboolean
lrg_blackboard_remove (LrgBlackboard *self,
                       const gchar   *key)
{
    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_remove (self->entries, key);
}

/**
 * lrg_blackboard_clear:
 * @self: an #LrgBlackboard
 *
 * Removes all entries from the blackboard.
 */
void
lrg_blackboard_clear (LrgBlackboard *self)
{
    g_return_if_fail (LRG_IS_BLACKBOARD (self));

    g_hash_table_remove_all (self->entries);
}

/**
 * lrg_blackboard_get_keys:
 * @self: an #LrgBlackboard
 *
 * Gets all keys in the blackboard.
 *
 * Returns: (transfer container) (element-type utf8): List of key names
 */
GList *
lrg_blackboard_get_keys (LrgBlackboard *self)
{
    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), NULL);

    return g_hash_table_get_keys (self->entries);
}
