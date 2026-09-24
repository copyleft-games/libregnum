/* lrg-script-host.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Introspectable bridge between C hosts and GI-bound script runtimes.
 *
 * A process-wide table maps bridge ids to per-context slots.  Each slot
 * holds the arguments stashed for the next call, the last result handed
 * back, and the published functions and objects.  All access is guarded by
 * one mutex; callers are expected to be on the thread that owns the script
 * runtime, but the table itself stays consistent regardless.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-script-host.h"
#include "../lrg-enums.h"
#include "../lrg-log.h"

/* A published host function */
typedef struct
{
    LrgScriptingCFunction  func;
    gpointer               user_data;
} HostFunction;

/* Everything the bridge knows about one context */
typedef struct
{
    LrgScripting *scripting;  /* not referenced: the context owns the slot */
    GVariant     *args;       /* stashed `av` for the next call */
    GVariant     *result;     /* last value handed back */
    GHashTable   *functions;  /* name -> HostFunction */
    GHashTable   *objects;    /* name -> GObject (referenced) */
} HostSlot;

static GMutex      host_lock;
static GHashTable *host_slots;  /* guint id -> HostSlot */
static guint       host_next_id = 1;

/* Interpreter runs in progress (main thread) and the mask before the first */
static guint          script_depth;
static GLogLevelFlags script_outer_fatal_mask;

/* Frees a slot with everything it holds */
static void
host_slot_free (gpointer data)
{
    HostSlot *slot = data;

    g_clear_pointer (&slot->args, g_variant_unref);
    g_clear_pointer (&slot->result, g_variant_unref);
    g_clear_pointer (&slot->functions, g_hash_table_unref);
    g_clear_pointer (&slot->objects, g_hash_table_unref);
    g_free (slot);
}

GLogLevelFlags
lrg_script_host_enter_script (void)
{
    GLogLevelFlags previous;

    previous = g_log_set_always_fatal (G_LOG_FATAL_MASK & ~(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING));
    if (script_depth++ == 0)
    {
        script_outer_fatal_mask = previous;
    }
    return previous;
}

void
lrg_script_host_leave_script (GLogLevelFlags previous)
{
    g_return_if_fail (script_depth > 0);
    script_depth--;
    g_log_set_always_fatal (previous);
}

/* Looks up a slot; the caller holds host_lock */
static HostSlot *
host_slot_lookup (guint id)
{
    if (host_slots == NULL)
    {
        return NULL;
    }

    return g_hash_table_lookup (host_slots, GUINT_TO_POINTER (id));
}

/* ==========================================================================
 * Host side
 * ========================================================================== */

/*
 * lrg_script_host_register: (skip)
 * @scripting: the context the id stands for (not referenced)
 *
 * Allocates a bridge id for @scripting.
 *
 * Returns: a non-zero id
 */
guint
lrg_script_host_register (LrgScripting *scripting)
{
    HostSlot *slot;
    guint     id;

    g_return_val_if_fail (LRG_IS_SCRIPTING (scripting), 0);

    slot = g_new0 (HostSlot, 1);
    slot->scripting = scripting;
    slot->functions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    slot->objects = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    g_mutex_lock (&host_lock);
    if (host_slots == NULL)
    {
        host_slots = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                            NULL, host_slot_free);
    }
    /* Ids are never reused within a process; 0 stays invalid */
    id = host_next_id++;
    if (host_next_id == 0)
    {
        host_next_id = 1;
    }
    g_hash_table_insert (host_slots, GUINT_TO_POINTER (id), slot);
    g_mutex_unlock (&host_lock);

    return id;
}

/*
 * lrg_script_host_unregister: (skip)
 * @id: a bridge id
 *
 * Forgets @id with its stashed arguments, result, functions and objects.
 */
void
lrg_script_host_unregister (guint id)
{
    g_mutex_lock (&host_lock);
    if (host_slots != NULL)
    {
        g_hash_table_remove (host_slots, GUINT_TO_POINTER (id));
    }
    g_mutex_unlock (&host_lock);
}

/*
 * lrg_script_host_add_function: (skip)
 * @id: a bridge id
 * @name: the script-visible name
 * @func: the host function
 * @user_data: data for @func
 *
 * Publishes a host function for lrg_script_host_call().
 */
void
lrg_script_host_add_function (guint                  id,
                              const gchar           *name,
                              LrgScriptingCFunction  func,
                              gpointer               user_data)
{
    HostSlot     *slot;
    HostFunction *host;

    g_return_if_fail (name != NULL);
    g_return_if_fail (func != NULL);

    g_mutex_lock (&host_lock);
    slot = host_slot_lookup (id);
    if (slot != NULL)
    {
        host = g_new0 (HostFunction, 1);
        host->func = func;
        host->user_data = user_data;
        g_hash_table_replace (slot->functions, g_strdup (name), host);
    }
    g_mutex_unlock (&host_lock);
}

/*
 * lrg_script_host_add_object: (skip)
 * @id: a bridge id
 * @name: the script-visible name
 * @object: the object; a reference is kept until unregister
 *
 * Publishes an object for lrg_script_host_get_object().
 */
void
lrg_script_host_add_object (guint        id,
                            const gchar *name,
                            GObject     *object)
{
    HostSlot *slot;

    g_return_if_fail (name != NULL);
    g_return_if_fail (G_IS_OBJECT (object));

    g_mutex_lock (&host_lock);
    slot = host_slot_lookup (id);
    if (slot != NULL)
    {
        g_hash_table_replace (slot->objects, g_strdup (name), g_object_ref (object));
    }
    g_mutex_unlock (&host_lock);
}

/*
 * lrg_script_host_stash_args: (skip)
 * @id: a bridge id
 * @args: (transfer none): an `av` array for the script to take
 *
 * Stores arguments for the next lrg_script_host_take_args().
 */
void
lrg_script_host_stash_args (guint     id,
                            GVariant *args)
{
    HostSlot *slot;

    g_return_if_fail (args != NULL);
    g_return_if_fail (g_variant_is_of_type (args, G_VARIANT_TYPE ("av")));

    g_mutex_lock (&host_lock);
    slot = host_slot_lookup (id);
    if (slot != NULL)
    {
        g_clear_pointer (&slot->args, g_variant_unref);
        slot->args = g_variant_ref_sink (args);
    }
    g_mutex_unlock (&host_lock);
}

/*
 * lrg_script_host_steal_result: (skip)
 * @id: a bridge id
 *
 * Takes the value the script last handed back.
 *
 * Returns: (transfer full) (nullable): the result, or %NULL if none was set
 */
GVariant *
lrg_script_host_steal_result (guint id)
{
    HostSlot *slot;
    GVariant *result;

    result = NULL;
    g_mutex_lock (&host_lock);
    slot = host_slot_lookup (id);
    if (slot != NULL)
    {
        result = g_steal_pointer (&slot->result);
    }
    g_mutex_unlock (&host_lock);

    return result;
}

/* ==========================================================================
 * Script side (introspectable)
 * ========================================================================== */

/*
 * lrg_script_host_take_args:
 * @id: a bridge id
 *
 * Script side: takes the arguments the host stashed for this call.
 *
 * Returns: (transfer full) (nullable): an `av` array, or %NULL when none
 */
GVariant *
lrg_script_host_take_args (guint id)
{
    HostSlot *slot;
    GVariant *args;

    args = NULL;
    g_mutex_lock (&host_lock);
    slot = host_slot_lookup (id);
    if (slot != NULL)
    {
        args = g_steal_pointer (&slot->args);
    }
    g_mutex_unlock (&host_lock);

    return args;
}

/*
 * lrg_script_host_set_result:
 * @id: a bridge id
 * @result: (nullable): the value to hand back to the host
 *
 * Script side: hands a value back to the host.
 */
void
lrg_script_host_set_result (guint     id,
                            GVariant *result)
{
    HostSlot *slot;

    g_mutex_lock (&host_lock);
    slot = host_slot_lookup (id);
    if (slot != NULL)
    {
        g_clear_pointer (&slot->result, g_variant_unref);
        if (result != NULL)
        {
            slot->result = g_variant_ref_sink (result);
        }
    }
    g_mutex_unlock (&host_lock);
}

/*
 * lrg_script_host_call:
 * @id: a bridge id
 * @name: a function published with lrg_script_host_add_function()
 * @args: an `av` array of arguments
 * @error: return location for an error
 *
 * Script side: calls a host function.
 *
 * Returns: (transfer full) (nullable): the result, or %NULL for "no value"
 */
GVariant *
lrg_script_host_call (guint         id,
                      const gchar  *name,
                      GVariant     *args,
                      GError      **error)
{
    HostSlot      *slot;
    HostFunction   host;
    LrgScripting  *scripting;
    GValue        *values;
    GValue         result = G_VALUE_INIT;
    GVariant      *out;
    gsize          n_args;
    gsize          i;
    gboolean       ok;

    g_return_val_if_fail (name != NULL, NULL);

    /* Copy what the call needs, so the lock is not held across the callback */
    g_mutex_lock (&host_lock);
    slot = host_slot_lookup (id);
    if (slot == NULL || g_hash_table_lookup (slot->functions, name) == NULL)
    {
        g_mutex_unlock (&host_lock);
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Host function '%s' is not registered", name);
        return NULL;
    }
    host = *(HostFunction *)g_hash_table_lookup (slot->functions, name);
    scripting = slot->scripting;
    g_mutex_unlock (&host_lock);

    /* Unpack the argument array into GValues */
    n_args = (args != NULL && g_variant_is_of_type (args, G_VARIANT_TYPE ("av")))
             ? g_variant_n_children (args) : 0;
    values = g_new0 (GValue, n_args > 0 ? n_args : 1);
    for (i = 0; i < n_args; i++)
    {
        g_autoptr(GVariant) child = g_variant_get_child_value (args, i);

        if (!lrg_script_host_variant_to_value (child, &values[i]))
        {
            gsize j;

            for (j = 0; j < i; j++)
            {
                g_value_unset (&values[j]);
            }
            g_free (values);
            g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
                         "Argument %" G_GSIZE_FORMAT " to '%s' has an unsupported type",
                         i + 1, name);
            return NULL;
        }
    }

    /* Host code runs under the caller's fatal mask, not the script's */
    if (script_depth > 0)
    {
        GLogLevelFlags during = g_log_set_always_fatal (script_outer_fatal_mask);

        ok = host.func (scripting, (guint)n_args, values, &result, host.user_data, error);
        g_log_set_always_fatal (during);
    }
    else
    {
        ok = host.func (scripting, (guint)n_args, values, &result, host.user_data, error);
    }

    for (i = 0; i < n_args; i++)
    {
        g_value_unset (&values[i]);
    }
    g_free (values);

    if (!ok)
    {
        if (G_IS_VALUE (&result))
        {
            g_value_unset (&result);
        }
        if (error != NULL && *error == NULL)
        {
            g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                         "Host function '%s' failed", name);
        }
        return NULL;
    }

    if (!G_IS_VALUE (&result))
    {
        return NULL;
    }

    out = lrg_script_host_value_to_variant (&result);
    g_value_unset (&result);
    return out;
}

/*
 * lrg_script_host_get_object:
 * @id: a bridge id
 * @name: an object published with lrg_script_host_add_object()
 *
 * Script side: fetches a published object.
 *
 * Returns: (transfer none) (nullable): the object
 */
GObject *
lrg_script_host_get_object (guint        id,
                            const gchar *name)
{
    HostSlot *slot;
    GObject  *object;

    g_return_val_if_fail (name != NULL, NULL);

    object = NULL;
    g_mutex_lock (&host_lock);
    slot = host_slot_lookup (id);
    if (slot != NULL)
    {
        object = g_hash_table_lookup (slot->objects, name);
    }
    g_mutex_unlock (&host_lock);

    return object;
}

/* ==========================================================================
 * Conversions
 * ========================================================================== */

/*
 * lrg_script_host_value_to_variant: (skip)
 * @value: (nullable): a #GValue
 *
 * Converts a scalar #GValue for the bridge.
 *
 * Returns: (transfer full): a new variant
 */
GVariant *
lrg_script_host_value_to_variant (const GValue *value)
{
    GVariant *out;

    out = NULL;
    if (value != NULL && G_IS_VALUE (value))
    {
        switch (G_TYPE_FUNDAMENTAL (G_VALUE_TYPE (value)))
        {
        case G_TYPE_BOOLEAN:
            out = g_variant_new_boolean (g_value_get_boolean (value));
            break;
        case G_TYPE_CHAR:
            out = g_variant_new_int64 (g_value_get_schar (value));
            break;
        case G_TYPE_UCHAR:
            out = g_variant_new_int64 (g_value_get_uchar (value));
            break;
        case G_TYPE_INT:
            out = g_variant_new_int64 (g_value_get_int (value));
            break;
        case G_TYPE_UINT:
            out = g_variant_new_int64 (g_value_get_uint (value));
            break;
        case G_TYPE_LONG:
            out = g_variant_new_int64 (g_value_get_long (value));
            break;
        case G_TYPE_ULONG:
            out = g_variant_new_uint64 (g_value_get_ulong (value));
            break;
        case G_TYPE_INT64:
            out = g_variant_new_int64 (g_value_get_int64 (value));
            break;
        case G_TYPE_UINT64:
            out = g_variant_new_uint64 (g_value_get_uint64 (value));
            break;
        case G_TYPE_ENUM:
            out = g_variant_new_int64 (g_value_get_enum (value));
            break;
        case G_TYPE_FLAGS:
            out = g_variant_new_int64 (g_value_get_flags (value));
            break;
        case G_TYPE_FLOAT:
            out = g_variant_new_double (g_value_get_float (value));
            break;
        case G_TYPE_DOUBLE:
            out = g_variant_new_double (g_value_get_double (value));
            break;
        case G_TYPE_STRING:
            if (g_value_get_string (value) != NULL)
            {
                out = g_variant_new_string (g_value_get_string (value));
            }
            break;
        default:
            break;
        }
    }

    /* Nothing, NULL strings and unsupported types all read as "no value" */
    if (out == NULL)
    {
        out = g_variant_new_maybe (G_VARIANT_TYPE_STRING, NULL);
    }

    return g_variant_ref_sink (out);
}

/*
 * lrg_script_host_variant_to_value: (skip)
 * @variant: a bridge variant (a `v` box is unwrapped first)
 * @value: (out caller-allocates): an uninitialized #GValue
 *
 * Converts a bridge value to a #GValue.
 *
 * Returns: %TRUE on success, %FALSE for unsupported types
 */
gboolean
lrg_script_host_variant_to_value (GVariant *variant,
                                  GValue   *value)
{
    g_autoptr(GVariant) inner = NULL;

    g_return_val_if_fail (variant != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    /* Unbox `v` and `m*` layers */
    inner = g_variant_ref (variant);
    while (g_variant_is_of_type (inner, G_VARIANT_TYPE_VARIANT) ||
           g_variant_is_of_type (inner, G_VARIANT_TYPE_MAYBE))
    {
        GVariant *next;

        if (g_variant_is_of_type (inner, G_VARIANT_TYPE_VARIANT))
        {
            next = g_variant_get_variant (inner);
        }
        else
        {
            next = g_variant_get_maybe (inner);
            if (next == NULL)
            {
                g_value_init (value, G_TYPE_POINTER);
                g_value_set_pointer (value, NULL);
                return TRUE;
            }
        }
        g_variant_unref (inner);
        inner = next;
    }

    switch (g_variant_classify (inner))
    {
    case G_VARIANT_CLASS_BOOLEAN:
        g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, g_variant_get_boolean (inner));
        return TRUE;
    case G_VARIANT_CLASS_BYTE:
        g_value_init (value, G_TYPE_INT64);
        g_value_set_int64 (value, g_variant_get_byte (inner));
        return TRUE;
    case G_VARIANT_CLASS_INT16:
        g_value_init (value, G_TYPE_INT64);
        g_value_set_int64 (value, g_variant_get_int16 (inner));
        return TRUE;
    case G_VARIANT_CLASS_UINT16:
        g_value_init (value, G_TYPE_INT64);
        g_value_set_int64 (value, g_variant_get_uint16 (inner));
        return TRUE;
    case G_VARIANT_CLASS_INT32:
        g_value_init (value, G_TYPE_INT64);
        g_value_set_int64 (value, g_variant_get_int32 (inner));
        return TRUE;
    case G_VARIANT_CLASS_UINT32:
        g_value_init (value, G_TYPE_INT64);
        g_value_set_int64 (value, g_variant_get_uint32 (inner));
        return TRUE;
    case G_VARIANT_CLASS_INT64:
        g_value_init (value, G_TYPE_INT64);
        g_value_set_int64 (value, g_variant_get_int64 (inner));
        return TRUE;
    case G_VARIANT_CLASS_UINT64:
        g_value_init (value, G_TYPE_UINT64);
        g_value_set_uint64 (value, g_variant_get_uint64 (inner));
        return TRUE;
    case G_VARIANT_CLASS_DOUBLE:
        g_value_init (value, G_TYPE_DOUBLE);
        g_value_set_double (value, g_variant_get_double (inner));
        return TRUE;
    case G_VARIANT_CLASS_STRING:
    case G_VARIANT_CLASS_OBJECT_PATH:
    case G_VARIANT_CLASS_SIGNATURE:
        g_value_init (value, G_TYPE_STRING);
        g_value_set_string (value, g_variant_get_string (inner, NULL));
        return TRUE;
    default:
        return FALSE;
    }
}
