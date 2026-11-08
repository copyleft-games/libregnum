/* lrg-blackboard.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Blackboard implementation for behavior tree data sharing.
 */

#include "lrg-blackboard.h"
#include <gio/gio.h>

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
    guint64      serial;   /* bumped by every change (inspectors poll it) */
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

/*
 * store_entry:
 *
 * Stores @entry under a copy of @key (replacing what was there) and
 * bumps the change serial.
 */
static void
store_entry (LrgBlackboard   *self,
             const gchar     *key,
             BlackboardEntry *entry)
{
    g_hash_table_insert (self->entries, g_strdup (key), entry);
    self->serial++;
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

/*
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

/*
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

    store_entry (self, key, entry);
}

/*
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

/*
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

    store_entry (self, key, entry);
}

/*
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

/*
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

    store_entry (self, key, entry);
}

/*
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

/*
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

    store_entry (self, key, entry);
}

/*
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

/*
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

    store_entry (self, key, entry);
}

/*
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

/*
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

    store_entry (self, key, entry);
}

/*
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

/*
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

/*
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

    if (!g_hash_table_remove (self->entries, key))
        return FALSE;
    self->serial++;
    return TRUE;
}

/*
 * lrg_blackboard_clear:
 * @self: an #LrgBlackboard
 *
 * Removes all entries from the blackboard.
 */
void
lrg_blackboard_clear (LrgBlackboard *self)
{
    g_return_if_fail (LRG_IS_BLACKBOARD (self));

    if (g_hash_table_size (self->entries) > 0)
        self->serial++;
    g_hash_table_remove_all (self->entries);
}

/*
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

guint
lrg_blackboard_get_size (LrgBlackboard *self)
{
    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), 0);
    return g_hash_table_size (self->entries);
}

static gint
compare_key_names (gconstpointer a, gconstpointer b)
{
    return g_strcmp0 (*(gchar * const *) a, *(gchar * const *) b);
}

gchar **
lrg_blackboard_dup_keys (LrgBlackboard *self)
{
    GHashTableIter iter;
    gpointer key;
    GPtrArray *keys;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), NULL);
    keys = g_ptr_array_new ();
    g_hash_table_iter_init (&iter, self->entries);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (keys, g_strdup (key));
    g_ptr_array_sort (keys, compare_key_names);
    g_ptr_array_add (keys, NULL);
    return (gchar **) g_ptr_array_free (keys, FALSE);
}

gchar *
lrg_blackboard_dup_string (LrgBlackboard *self,
                           const gchar   *key)
{
    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);
    return g_strdup (lrg_blackboard_get_string (self, key));
}

/* ==========================================================================
 * Typed access, comparisons and change tracking (0.9.0)
 * ========================================================================== */

/* the spellings of LrgBlackboardOp, in enum order */
static const gchar * const op_names[] = {
    "eq", "ne", "lt", "le", "gt", "ge", "set", "unset"
};

GType
lrg_blackboard_op_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_BLACKBOARD_OP_EQ, "LRG_BLACKBOARD_OP_EQ", "eq" },
            { LRG_BLACKBOARD_OP_NE, "LRG_BLACKBOARD_OP_NE", "ne" },
            { LRG_BLACKBOARD_OP_LT, "LRG_BLACKBOARD_OP_LT", "lt" },
            { LRG_BLACKBOARD_OP_LE, "LRG_BLACKBOARD_OP_LE", "le" },
            { LRG_BLACKBOARD_OP_GT, "LRG_BLACKBOARD_OP_GT", "gt" },
            { LRG_BLACKBOARD_OP_GE, "LRG_BLACKBOARD_OP_GE", "ge" },
            { LRG_BLACKBOARD_OP_SET, "LRG_BLACKBOARD_OP_SET", "set" },
            { LRG_BLACKBOARD_OP_UNSET, "LRG_BLACKBOARD_OP_UNSET", "unset" },
            { 0, NULL, NULL }
        };
        GType type = g_enum_register_static ("LrgBlackboardOp", values);

        g_once_init_leave (&type_id, type);
    }
    return (GType) type_id;
}

const gchar *
lrg_blackboard_op_to_string (LrgBlackboardOp op)
{
    if ((guint) op >= G_N_ELEMENTS (op_names))
        return "eq";
    return op_names[op];
}

gboolean
lrg_blackboard_op_from_string (const gchar     *text,
                               LrgBlackboardOp *out_op)
{
    /* symbol spellings, in enum order */
    static const gchar * const symbols[] = { "==", "!=", "<", "<=", ">", ">=" };
    guint i;

    g_return_val_if_fail (out_op != NULL, FALSE);

    if (text == NULL)
        return FALSE;
    for (i = 0; i < G_N_ELEMENTS (op_names); i++)
    {
        if (g_strcmp0 (text, op_names[i]) == 0 ||
            (i < G_N_ELEMENTS (symbols) && g_strcmp0 (text, symbols[i]) == 0))
        {
            *out_op = (LrgBlackboardOp) i;
            return TRUE;
        }
    }
    return FALSE;
}

GType
lrg_blackboard_get_value_type (LrgBlackboard *self,
                               const gchar   *key)
{
    BlackboardEntry *entry;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), G_TYPE_INVALID);
    g_return_val_if_fail (key != NULL, G_TYPE_INVALID);

    entry = g_hash_table_lookup (self->entries, key);
    if (entry == NULL)
        return G_TYPE_INVALID;
    switch (entry->type)
    {
    case ENTRY_TYPE_INT:     return G_TYPE_INT;
    case ENTRY_TYPE_FLOAT:   return G_TYPE_FLOAT;
    case ENTRY_TYPE_BOOL:    return G_TYPE_BOOLEAN;
    case ENTRY_TYPE_STRING:  return G_TYPE_STRING;
    case ENTRY_TYPE_OBJECT:  return G_TYPE_OBJECT;
    case ENTRY_TYPE_POINTER: return G_TYPE_POINTER;
    default:                 return G_TYPE_INVALID;
    }
}

gchar *
lrg_blackboard_dup_value_text (LrgBlackboard *self,
                               const gchar   *key)
{
    BlackboardEntry *entry;
    gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    entry = g_hash_table_lookup (self->entries, key);
    if (entry == NULL)
        return NULL;
    switch (entry->type)
    {
    case ENTRY_TYPE_INT:
        return g_strdup_printf ("%d", entry->value.int_val);
    case ENTRY_TYPE_FLOAT:
        return g_strdup (g_ascii_formatd (buf, sizeof buf, "%.6g", (gdouble) entry->value.float_val));
    case ENTRY_TYPE_BOOL:
        return g_strdup (entry->value.bool_val ? "true" : "false");
    case ENTRY_TYPE_STRING:
        return g_strdup (entry->value.string_val != NULL ? entry->value.string_val : "");
    case ENTRY_TYPE_OBJECT:
        if (entry->value.object_val == NULL)
            return g_strdup ("null");
        return g_strdup_printf ("%s@%p", G_OBJECT_TYPE_NAME (entry->value.object_val),
                                (gpointer) entry->value.object_val);
    case ENTRY_TYPE_POINTER:
        return g_strdup_printf ("%p", entry->value.pointer_val);
    default:
        return NULL;
    }
}

GVariant *
lrg_blackboard_dup_variant (LrgBlackboard *self,
                            const gchar   *key)
{
    BlackboardEntry *entry;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    entry = g_hash_table_lookup (self->entries, key);
    if (entry == NULL)
        return NULL;
    switch (entry->type)
    {
    case ENTRY_TYPE_INT:
        return g_variant_ref_sink (g_variant_new_int32 (entry->value.int_val));
    case ENTRY_TYPE_FLOAT:
        return g_variant_ref_sink (g_variant_new_double ((gdouble) entry->value.float_val));
    case ENTRY_TYPE_BOOL:
        return g_variant_ref_sink (g_variant_new_boolean (entry->value.bool_val));
    case ENTRY_TYPE_STRING:
        return g_variant_ref_sink (g_variant_new_string (entry->value.string_val != NULL ?
                                                         entry->value.string_val : ""));
    default:
        return NULL;
    }
}

/*
 * variant_as_double:
 *
 * Reads any numeric (or boolean) variant as a double. Returns FALSE for
 * strings and other types.
 */
static gboolean
variant_as_double (GVariant *v,
                   gdouble  *out)
{
    const GVariantType *t;

    if (v == NULL)
        return FALSE;
    t = g_variant_get_type (v);
    if (g_variant_type_equal (t, G_VARIANT_TYPE_BOOLEAN))
        *out = g_variant_get_boolean (v) ? 1.0 : 0.0;
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_BYTE))
        *out = (gdouble) g_variant_get_byte (v);
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_INT16))
        *out = (gdouble) g_variant_get_int16 (v);
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_UINT16))
        *out = (gdouble) g_variant_get_uint16 (v);
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_INT32))
        *out = (gdouble) g_variant_get_int32 (v);
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_UINT32))
        *out = (gdouble) g_variant_get_uint32 (v);
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_INT64))
        *out = (gdouble) g_variant_get_int64 (v);
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_UINT64))
        *out = (gdouble) g_variant_get_uint64 (v);
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_DOUBLE))
        *out = g_variant_get_double (v);
    else
        return FALSE;
    return TRUE;
}

gboolean
lrg_blackboard_set_variant (LrgBlackboard *self,
                            const gchar   *key,
                            GVariant      *value)
{
    const GVariantType *t;
    gdouble d;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    /* a floating reference is consumed */
    if (g_variant_is_floating (value))
    {
        gboolean ok;

        g_variant_ref_sink (value);
        ok = lrg_blackboard_set_variant (self, key, value);
        g_variant_unref (value);
        return ok;
    }
    t = g_variant_get_type (value);
    if (g_variant_type_equal (t, G_VARIANT_TYPE_BOOLEAN))
        lrg_blackboard_set_bool (self, key, g_variant_get_boolean (value));
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_STRING))
        lrg_blackboard_set_string (self, key, g_variant_get_string (value, NULL));
    else if (g_variant_type_equal (t, G_VARIANT_TYPE_DOUBLE))
        lrg_blackboard_set_float (self, key, (gfloat) g_variant_get_double (value));
    else if (variant_as_double (value, &d))
        lrg_blackboard_set_int (self, key, (gint) CLAMP (d, (gdouble) G_MININT, (gdouble) G_MAXINT));
    else
        return FALSE;
    return TRUE;
}

/*
 * entry_truthy:
 *
 * The SET/UNSET notion of a value being "there".
 */
static gboolean
entry_truthy (const BlackboardEntry *entry)
{
    switch (entry->type)
    {
    case ENTRY_TYPE_INT:     return entry->value.int_val != 0;
    case ENTRY_TYPE_FLOAT:   return entry->value.float_val != 0.0f;
    case ENTRY_TYPE_BOOL:    return entry->value.bool_val;
    case ENTRY_TYPE_STRING:  return entry->value.string_val != NULL && *entry->value.string_val != '\0';
    case ENTRY_TYPE_OBJECT:  return entry->value.object_val != NULL;
    case ENTRY_TYPE_POINTER: return entry->value.pointer_val != NULL;
    default:                 return FALSE;
    }
}

gboolean
lrg_blackboard_test (LrgBlackboard   *self,
                     const gchar     *key,
                     LrgBlackboardOp  op,
                     GVariant        *operand)
{
    BlackboardEntry *entry;
    gint cmp;
    gboolean comparable = FALSE;
    gdouble a = 0.0;
    gdouble b = 0.0;

    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    /* a floating operand is consumed */
    if (operand != NULL && g_variant_is_floating (operand))
    {
        gboolean holds;

        g_variant_ref_sink (operand);
        holds = lrg_blackboard_test (self, key, op, operand);
        g_variant_unref (operand);
        return holds;
    }
    entry = g_hash_table_lookup (self->entries, key);

    /* presence tests first: they need no operand */
    if (op == LRG_BLACKBOARD_OP_SET)
        return entry != NULL && entry_truthy (entry);
    if (op == LRG_BLACKBOARD_OP_UNSET)
        return entry == NULL || !entry_truthy (entry);

    /* a three-way comparison where the types allow one */
    cmp = 0;
    if (entry != NULL && operand != NULL)
    {
        switch (entry->type)
        {
        case ENTRY_TYPE_INT:
        case ENTRY_TYPE_FLOAT:
        case ENTRY_TYPE_BOOL:
            a = entry->type == ENTRY_TYPE_INT ? (gdouble) entry->value.int_val :
                entry->type == ENTRY_TYPE_FLOAT ? (gdouble) entry->value.float_val :
                (entry->value.bool_val ? 1.0 : 0.0);
            if (variant_as_double (operand, &b))
            {
                /* floats are stored single precision: compare there */
                if (entry->type == ENTRY_TYPE_FLOAT)
                    b = (gdouble) (gfloat) b;
                cmp = a < b ? -1 : (a > b ? 1 : 0);
                comparable = TRUE;
            }
            break;

        case ENTRY_TYPE_STRING:
            if (g_variant_is_of_type (operand, G_VARIANT_TYPE_STRING))
            {
                cmp = g_strcmp0 (entry->value.string_val != NULL ? entry->value.string_val : "",
                                 g_variant_get_string (operand, NULL));
                cmp = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
                comparable = TRUE;
            }
            break;

        default:
            break;
        }
    }

    if (!comparable)
        return op == LRG_BLACKBOARD_OP_NE;

    switch (op)
    {
    case LRG_BLACKBOARD_OP_EQ: return cmp == 0;
    case LRG_BLACKBOARD_OP_NE: return cmp != 0;
    case LRG_BLACKBOARD_OP_LT: return cmp < 0;
    case LRG_BLACKBOARD_OP_LE: return cmp <= 0;
    case LRG_BLACKBOARD_OP_GT: return cmp > 0;
    case LRG_BLACKBOARD_OP_GE: return cmp >= 0;
    case LRG_BLACKBOARD_OP_SET:
    case LRG_BLACKBOARD_OP_UNSET:
    default:
        return FALSE;
    }
}

guint64
lrg_blackboard_get_serial (LrgBlackboard *self)
{
    g_return_val_if_fail (LRG_IS_BLACKBOARD (self), 0);
    return self->serial;
}

/* ==========================================================================
 * LrgBlackboardSchema
 * ========================================================================== */

typedef struct
{
    gchar    *name;
    GType     type;
    GVariant *def;    /* of the key's variant type, or NULL */
} SchemaKey;

struct _LrgBlackboardSchema
{
    GObject    parent_instance;

    GPtrArray *keys;  /* SchemaKey, declaration order */
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBlackboardSchema, lrg_blackboard_schema, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
schema_key_free (gpointer data)
{
    SchemaKey *key = data;

    g_free (key->name);
    g_clear_pointer (&key->def, g_variant_unref);
    g_free (key);
}

static void
lrg_blackboard_schema_finalize (GObject *object)
{
    LrgBlackboardSchema *self = LRG_BLACKBOARD_SCHEMA (object);

    g_clear_pointer (&self->keys, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_blackboard_schema_parent_class)->finalize (object);
}

static void
lrg_blackboard_schema_class_init (LrgBlackboardSchemaClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_blackboard_schema_finalize;
}

static void
lrg_blackboard_schema_init (LrgBlackboardSchema *self)
{
    self->keys = g_ptr_array_new_with_free_func (schema_key_free);
}

LrgBlackboardSchema *
lrg_blackboard_schema_new (void)
{
    return g_object_new (LRG_TYPE_BLACKBOARD_SCHEMA, NULL);
}

static SchemaKey *
schema_find (LrgBlackboardSchema *self,
             const gchar         *name,
             guint               *out_index)
{
    guint i;

    for (i = 0; i < self->keys->len; i++)
    {
        SchemaKey *key = g_ptr_array_index (self->keys, i);

        if (g_strcmp0 (key->name, name) == 0)
        {
            if (out_index != NULL)
                *out_index = i;
            return key;
        }
    }
    return NULL;
}

/*
 * convert_default:
 *
 * Converts @value to the variant type of a key of @type: `i` for ints,
 * `d` for floats, `b` for booleans, `s` for strings. Returns a new
 * (non-floating) reference, or NULL when the value does not convert.
 */
static GVariant *
convert_default (GType     type,
                 GVariant *value)
{
    gdouble d;

    if (type == G_TYPE_STRING)
    {
        if (!g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
            return NULL;
        return g_variant_ref_sink (g_variant_new_string (g_variant_get_string (value, NULL)));
    }
    if (!variant_as_double (value, &d))
        return NULL;
    if (type == G_TYPE_INT)
    {
        if (d != (gdouble) (gint64) d)
            return NULL;    /* 1.5 is not an int default */
        return g_variant_ref_sink (g_variant_new_int32 ((gint32) CLAMP (d, (gdouble) G_MININT32, (gdouble) G_MAXINT32)));
    }
    if (type == G_TYPE_FLOAT)
        return g_variant_ref_sink (g_variant_new_double (d));
    if (type == G_TYPE_BOOLEAN)
    {
        if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            return NULL;
        return g_variant_ref_sink (g_variant_new_boolean (g_variant_get_boolean (value)));
    }
    return NULL;
}

gboolean
lrg_blackboard_schema_add_key (LrgBlackboardSchema *self,
                               const gchar         *name,
                               GType                type,
                               GVariant            *default_value)
{
    SchemaKey *key;
    GVariant *def = NULL;

    g_return_val_if_fail (LRG_IS_BLACKBOARD_SCHEMA (self), FALSE);
    g_return_val_if_fail (name != NULL && *name != '\0', FALSE);

    /* a floating default is consumed */
    if (default_value != NULL && g_variant_is_floating (default_value))
    {
        gboolean ok;

        g_variant_ref_sink (default_value);
        ok = lrg_blackboard_schema_add_key (self, name, type, default_value);
        g_variant_unref (default_value);
        return ok;
    }
    if (lrg_blackboard_schema_type_to_string (type) == NULL)
        return FALSE;
    if (default_value != NULL && type != G_TYPE_OBJECT)
    {
        def = convert_default (type, default_value);
        if (def == NULL)
            return FALSE;
    }

    key = schema_find (self, name, NULL);
    if (key == NULL)
    {
        key = g_new0 (SchemaKey, 1);
        key->name = g_strdup (name);
        g_ptr_array_add (self->keys, key);
    }
    key->type = type;
    g_clear_pointer (&key->def, g_variant_unref);
    key->def = def;
    return TRUE;
}

gboolean
lrg_blackboard_schema_remove_key (LrgBlackboardSchema *self,
                                  const gchar         *name)
{
    guint index;

    g_return_val_if_fail (LRG_IS_BLACKBOARD_SCHEMA (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    if (schema_find (self, name, &index) == NULL)
        return FALSE;
    g_ptr_array_remove_index (self->keys, index);
    return TRUE;
}

guint
lrg_blackboard_schema_get_key_count (LrgBlackboardSchema *self)
{
    g_return_val_if_fail (LRG_IS_BLACKBOARD_SCHEMA (self), 0);
    return self->keys->len;
}

const gchar *
lrg_blackboard_schema_get_key_name (LrgBlackboardSchema *self,
                                    guint                index)
{
    g_return_val_if_fail (LRG_IS_BLACKBOARD_SCHEMA (self), NULL);

    if (index >= self->keys->len)
        return NULL;
    return ((SchemaKey *) g_ptr_array_index (self->keys, index))->name;
}

gboolean
lrg_blackboard_schema_has_key (LrgBlackboardSchema *self,
                               const gchar         *name)
{
    g_return_val_if_fail (LRG_IS_BLACKBOARD_SCHEMA (self), FALSE);
    return name != NULL && schema_find (self, name, NULL) != NULL;
}

GType
lrg_blackboard_schema_get_key_type (LrgBlackboardSchema *self,
                                    const gchar         *name)
{
    SchemaKey *key;

    g_return_val_if_fail (LRG_IS_BLACKBOARD_SCHEMA (self), G_TYPE_INVALID);

    key = name != NULL ? schema_find (self, name, NULL) : NULL;
    return key != NULL ? key->type : G_TYPE_INVALID;
}

GVariant *
lrg_blackboard_schema_get_key_default (LrgBlackboardSchema *self,
                                       const gchar         *name)
{
    SchemaKey *key;

    g_return_val_if_fail (LRG_IS_BLACKBOARD_SCHEMA (self), NULL);

    key = name != NULL ? schema_find (self, name, NULL) : NULL;
    return key != NULL ? key->def : NULL;
}

void
lrg_blackboard_schema_apply_defaults (LrgBlackboardSchema *self,
                                      LrgBlackboard       *blackboard,
                                      gboolean             overwrite)
{
    guint i;

    g_return_if_fail (LRG_IS_BLACKBOARD_SCHEMA (self));
    g_return_if_fail (LRG_IS_BLACKBOARD (blackboard));

    for (i = 0; i < self->keys->len; i++)
    {
        SchemaKey *key = g_ptr_array_index (self->keys, i);

        if (key->type == G_TYPE_OBJECT)
            continue;
        if (!overwrite && lrg_blackboard_has_key (blackboard, key->name))
            continue;
        if (key->type == G_TYPE_INT)
            lrg_blackboard_set_int (blackboard, key->name, key->def != NULL ? g_variant_get_int32 (key->def) : 0);
        else if (key->type == G_TYPE_FLOAT)
            lrg_blackboard_set_float (blackboard, key->name,
                                      key->def != NULL ? (gfloat) g_variant_get_double (key->def) : 0.0f);
        else if (key->type == G_TYPE_BOOLEAN)
            lrg_blackboard_set_bool (blackboard, key->name, key->def != NULL ? g_variant_get_boolean (key->def) : FALSE);
        else if (key->type == G_TYPE_STRING)
            lrg_blackboard_set_string (blackboard, key->name,
                                       key->def != NULL ? g_variant_get_string (key->def, NULL) : "");
    }
}

gboolean
lrg_blackboard_schema_validate (LrgBlackboardSchema  *self,
                                LrgBlackboard        *blackboard,
                                GError              **error)
{
    guint i;

    g_return_val_if_fail (LRG_IS_BLACKBOARD_SCHEMA (self), FALSE);
    g_return_val_if_fail (LRG_IS_BLACKBOARD (blackboard), FALSE);

    for (i = 0; i < self->keys->len; i++)
    {
        SchemaKey *key = g_ptr_array_index (self->keys, i);
        GType have = lrg_blackboard_get_value_type (blackboard, key->name);

        if (have == G_TYPE_INVALID || have == key->type)
            continue;
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "blackboard key '%s' holds a %s, the schema declares %s", key->name,
                     have == G_TYPE_POINTER ? "pointer" : lrg_blackboard_schema_type_to_string (have),
                     lrg_blackboard_schema_type_to_string (key->type));
        return FALSE;
    }
    return TRUE;
}

const gchar *
lrg_blackboard_schema_type_to_string (GType type)
{
    if (type == G_TYPE_INT)
        return "int";
    if (type == G_TYPE_FLOAT)
        return "float";
    if (type == G_TYPE_BOOLEAN)
        return "bool";
    if (type == G_TYPE_STRING)
        return "string";
    if (type == G_TYPE_OBJECT)
        return "object";
    return NULL;
}

GType
lrg_blackboard_schema_type_from_string (const gchar *text)
{
    if (g_strcmp0 (text, "int") == 0)
        return G_TYPE_INT;
    if (g_strcmp0 (text, "float") == 0)
        return G_TYPE_FLOAT;
    if (g_strcmp0 (text, "bool") == 0)
        return G_TYPE_BOOLEAN;
    if (g_strcmp0 (text, "string") == 0)
        return G_TYPE_STRING;
    if (g_strcmp0 (text, "object") == 0)
        return G_TYPE_OBJECT;
    return G_TYPE_INVALID;
}
