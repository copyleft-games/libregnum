/* lrg-item-stack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INVENTORY

#include "config.h"
#include "lrg-item-stack.h"
#include "../lrg-log.h"

/* Instance data value type */
typedef struct
{
    enum { DATA_INT, DATA_FLOAT, DATA_STRING } type;
    union {
        gint    int_val;
        gfloat  float_val;
        gchar  *string_val;
    } value;
} InstanceData;

static void
instance_data_free (gpointer data)
{
    InstanceData *idata = (InstanceData *)data;
    if (idata->type == DATA_STRING)
        g_free (idata->value.string_val);
    g_free (idata);
}

static InstanceData *
instance_data_copy (InstanceData *src)
{
    InstanceData *copy;

    copy = g_new0 (InstanceData, 1);
    copy->type = src->type;

    switch (src->type)
    {
    case DATA_INT:
        copy->value.int_val = src->value.int_val;
        break;
    case DATA_FLOAT:
        copy->value.float_val = src->value.float_val;
        break;
    case DATA_STRING:
        copy->value.string_val = g_strdup (src->value.string_val);
        break;
    }

    return copy;
}

/* Item stack structure */
struct _LrgItemStack
{
    volatile gint ref_count;
    LrgItemDef   *def;
    guint         quantity;
    GHashTable   *instance_data;  /* gchar* -> InstanceData* */
};

G_DEFINE_BOXED_TYPE (LrgItemStack, lrg_item_stack,
                     lrg_item_stack_ref, lrg_item_stack_unref)

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgItemStack *
lrg_item_stack_new (LrgItemDef *def,
                    guint       quantity)
{
    LrgItemStack *self;
    guint max_stack;

    g_return_val_if_fail (LRG_IS_ITEM_DEF (def), NULL);

    self = g_new0 (LrgItemStack, 1);
    self->ref_count = 1;
    self->def = g_object_ref (def);
    self->instance_data = g_hash_table_new_full (g_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  instance_data_free);

    /* Clamp quantity to max_stack */
    max_stack = lrg_item_def_get_max_stack (def);
    self->quantity = MIN (quantity, max_stack);

    return self;
}

LrgItemStack *
lrg_item_stack_ref (LrgItemStack *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (self->ref_count > 0, NULL);

    g_atomic_int_inc (&self->ref_count);

    return self;
}

void
lrg_item_stack_unref (LrgItemStack *self)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (self->ref_count > 0);

    if (g_atomic_int_dec_and_test (&self->ref_count))
    {
        g_clear_object (&self->def);
        g_clear_pointer (&self->instance_data, g_hash_table_unref);
        g_free (self);
    }
}

/* Helper for copying hash table entries */
static void
copy_instance_data_entry (gpointer key,
                          gpointer value,
                          gpointer user_data)
{
    GHashTable *dest = (GHashTable *)user_data;
    InstanceData *src_data = (InstanceData *)value;
    g_hash_table_insert (dest,
                         g_strdup ((const gchar *)key),
                         instance_data_copy (src_data));
}

LrgItemStack *
lrg_item_stack_copy (const LrgItemStack *self)
{
    LrgItemStack *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgItemStack, 1);
    copy->ref_count = 1;
    copy->def = g_object_ref (self->def);
    copy->quantity = self->quantity;
    copy->instance_data = g_hash_table_new_full (g_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  instance_data_free);

    /* Copy instance data */
    g_hash_table_foreach (self->instance_data, copy_instance_data_entry, copy->instance_data);

    return copy;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

LrgItemDef *
lrg_item_stack_get_def (const LrgItemStack *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->def;
}

guint
lrg_item_stack_get_quantity (const LrgItemStack *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->quantity;
}

void
lrg_item_stack_set_quantity (LrgItemStack *self,
                             guint         quantity)
{
    guint max_stack;

    g_return_if_fail (self != NULL);

    max_stack = lrg_item_def_get_max_stack (self->def);
    self->quantity = MIN (quantity, max_stack);
}

guint
lrg_item_stack_get_max_quantity (const LrgItemStack *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return lrg_item_def_get_max_stack (self->def);
}

gboolean
lrg_item_stack_is_full (const LrgItemStack *self)
{
    g_return_val_if_fail (self != NULL, TRUE);

    return self->quantity >= lrg_item_def_get_max_stack (self->def);
}

gboolean
lrg_item_stack_is_empty (const LrgItemStack *self)
{
    g_return_val_if_fail (self != NULL, TRUE);

    return self->quantity == 0;
}

guint
lrg_item_stack_get_space_remaining (const LrgItemStack *self)
{
    guint max_stack;

    g_return_val_if_fail (self != NULL, 0);

    max_stack = lrg_item_def_get_max_stack (self->def);
    if (self->quantity >= max_stack)
        return 0;

    return max_stack - self->quantity;
}

/* ==========================================================================
 * Quantity Operations
 * ========================================================================== */

guint
lrg_item_stack_add (LrgItemStack *self,
                    guint         amount)
{
    guint space;
    guint actual;

    g_return_val_if_fail (self != NULL, 0);

    if (amount == 0)
        return 0;

    space = lrg_item_stack_get_space_remaining (self);
    actual = MIN (amount, space);
    self->quantity += actual;

    return actual;
}

guint
lrg_item_stack_remove (LrgItemStack *self,
                       guint         amount)
{
    guint actual;

    g_return_val_if_fail (self != NULL, 0);

    if (amount == 0)
        return 0;

    actual = MIN (amount, self->quantity);
    self->quantity -= actual;

    return actual;
}

LrgItemStack *
lrg_item_stack_split (LrgItemStack *self,
                      guint         amount)
{
    LrgItemStack *split;

    g_return_val_if_fail (self != NULL, NULL);

    if (amount == 0 || amount > self->quantity)
        return NULL;

    split = lrg_item_stack_copy (self);
    split->quantity = amount;
    self->quantity -= amount;

    return split;
}

gboolean
lrg_item_stack_can_merge (const LrgItemStack *self,
                          const LrgItemStack *other)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (other != NULL, FALSE);

    /* Can't merge if either is full */
    if (lrg_item_stack_is_full (self))
        return FALSE;

    /* Check if the items can stack */
    return lrg_item_def_can_stack_with (self->def, other->def);
}

guint
lrg_item_stack_merge (LrgItemStack *self,
                      LrgItemStack *other)
{
    guint amount;

    g_return_val_if_fail (self != NULL, 0);
    g_return_val_if_fail (other != NULL, 0);

    if (!lrg_item_stack_can_merge (self, other))
        return 0;

    amount = lrg_item_stack_add (self, other->quantity);
    other->quantity -= amount;

    return amount;
}

/* ==========================================================================
 * Instance Data
 * ========================================================================== */

gint
lrg_item_stack_get_data_int (const LrgItemStack *self,
                             const gchar        *key,
                             gint                default_value)
{
    InstanceData *data;

    g_return_val_if_fail (self != NULL, default_value);
    g_return_val_if_fail (key != NULL, default_value);

    data = g_hash_table_lookup (self->instance_data, key);
    if (data != NULL && data->type == DATA_INT)
        return data->value.int_val;

    return default_value;
}

void
lrg_item_stack_set_data_int (LrgItemStack *self,
                             const gchar  *key,
                             gint          value)
{
    InstanceData *data;

    g_return_if_fail (self != NULL);
    g_return_if_fail (key != NULL);

    data = g_new0 (InstanceData, 1);
    data->type = DATA_INT;
    data->value.int_val = value;

    g_hash_table_insert (self->instance_data, g_strdup (key), data);
}

gfloat
lrg_item_stack_get_data_float (const LrgItemStack *self,
                               const gchar        *key,
                               gfloat              default_value)
{
    InstanceData *data;

    g_return_val_if_fail (self != NULL, default_value);
    g_return_val_if_fail (key != NULL, default_value);

    data = g_hash_table_lookup (self->instance_data, key);
    if (data != NULL && data->type == DATA_FLOAT)
        return data->value.float_val;

    return default_value;
}

void
lrg_item_stack_set_data_float (LrgItemStack *self,
                               const gchar  *key,
                               gfloat        value)
{
    InstanceData *data;

    g_return_if_fail (self != NULL);
    g_return_if_fail (key != NULL);

    data = g_new0 (InstanceData, 1);
    data->type = DATA_FLOAT;
    data->value.float_val = value;

    g_hash_table_insert (self->instance_data, g_strdup (key), data);
}

const gchar *
lrg_item_stack_get_data_string (const LrgItemStack *self,
                                const gchar        *key)
{
    InstanceData *data;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (key != NULL, NULL);

    data = g_hash_table_lookup (self->instance_data, key);
    if (data != NULL && data->type == DATA_STRING)
        return data->value.string_val;

    return NULL;
}

void
lrg_item_stack_set_data_string (LrgItemStack *self,
                                const gchar  *key,
                                const gchar  *value)
{
    InstanceData *data;

    g_return_if_fail (self != NULL);
    g_return_if_fail (key != NULL);

    data = g_new0 (InstanceData, 1);
    data->type = DATA_STRING;
    data->value.string_val = g_strdup (value);

    g_hash_table_insert (self->instance_data, g_strdup (key), data);
}

gboolean
lrg_item_stack_has_data (const LrgItemStack *self,
                         const gchar        *key)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_contains (self->instance_data, key);
}

gboolean
lrg_item_stack_remove_data (LrgItemStack *self,
                            const gchar  *key)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_remove (self->instance_data, key);
}

void
lrg_item_stack_clear_data (LrgItemStack *self)
{
    g_return_if_fail (self != NULL);

    g_hash_table_remove_all (self->instance_data);
}

/* ==========================================================================
 * Usage
 * ========================================================================== */

guint
lrg_item_stack_use (LrgItemStack *self,
                    GObject      *owner,
                    guint         quantity)
{
    guint used;
    guint i;

    g_return_val_if_fail (self != NULL, 0);

    quantity = MIN (quantity, self->quantity);
    if (quantity == 0)
        return 0;

    used = 0;
    for (i = 0; i < quantity; i++)
    {
        if (lrg_item_def_use (self->def, owner, 1))
        {
            used++;
        }
    }

    /* Remove consumed items */
    self->quantity -= used;

    return used;
}
