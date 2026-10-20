/* lrg-command.c - Named, typed commands for agents, scripts and tools
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"
#include "lrg-command.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * SECTION:lrg-command
 * @title: LrgCommand
 * @short_description: A named command with typed parameters
 *
 * See lrg-command.h for the overview and docs/modules/control/index.org
 * for the protocol the commands are served with.
 */

/* ------------------------------------------------------------------ */
/* enums and errors                                                     */

G_DEFINE_QUARK (lrg-command-error-quark, lrg_command_error)

GType
lrg_command_param_type_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_COMMAND_PARAM_STRING, "LRG_COMMAND_PARAM_STRING", "string" },
            { LRG_COMMAND_PARAM_INT, "LRG_COMMAND_PARAM_INT", "int" },
            { LRG_COMMAND_PARAM_NUMBER, "LRG_COMMAND_PARAM_NUMBER", "number" },
            { LRG_COMMAND_PARAM_BOOLEAN, "LRG_COMMAND_PARAM_BOOLEAN", "boolean" },
            { LRG_COMMAND_PARAM_OBJECT, "LRG_COMMAND_PARAM_OBJECT", "object" },
            { LRG_COMMAND_PARAM_ARRAY, "LRG_COMMAND_PARAM_ARRAY", "array" },
            { LRG_COMMAND_PARAM_ANY, "LRG_COMMAND_PARAM_ANY", "any" },
            { 0, NULL, NULL }
        };
        GType id;

        id = g_enum_register_static ("LrgCommandParamType", values);
        g_once_init_leave (&type_id, id);
    }
    return (GType)type_id;
}

GType
lrg_command_param_flags_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GFlagsValue values[] = {
            { LRG_COMMAND_PARAM_FLAG_NONE, "LRG_COMMAND_PARAM_FLAG_NONE", "none" },
            { LRG_COMMAND_PARAM_FLAG_REQUIRED, "LRG_COMMAND_PARAM_FLAG_REQUIRED", "required" },
            { LRG_COMMAND_PARAM_FLAG_REST, "LRG_COMMAND_PARAM_FLAG_REST", "rest" },
            { 0, NULL, NULL }
        };
        GType id;

        id = g_flags_register_static ("LrgCommandParamFlags", values);
        g_once_init_leave (&type_id, id);
    }
    return (GType)type_id;
}

GType
lrg_command_flags_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GFlagsValue values[] = {
            { LRG_COMMAND_FLAG_NONE, "LRG_COMMAND_FLAG_NONE", "none" },
            { LRG_COMMAND_FLAG_READ_ONLY, "LRG_COMMAND_FLAG_READ_ONLY", "read-only" },
            { LRG_COMMAND_FLAG_ASYNC, "LRG_COMMAND_FLAG_ASYNC", "async" },
            { LRG_COMMAND_FLAG_HIDDEN, "LRG_COMMAND_FLAG_HIDDEN", "hidden" },
            { LRG_COMMAND_FLAG_DESTRUCTIVE, "LRG_COMMAND_FLAG_DESTRUCTIVE", "destructive" },
            { 0, NULL, NULL }
        };
        GType id;

        id = g_flags_register_static ("LrgCommandFlags", values);
        g_once_init_leave (&type_id, id);
    }
    return (GType)type_id;
}

GType
lrg_command_error_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_COMMAND_ERROR_FAILED, "LRG_COMMAND_ERROR_FAILED", "failed" },
            { LRG_COMMAND_ERROR_UNKNOWN_COMMAND, "LRG_COMMAND_ERROR_UNKNOWN_COMMAND", "unknown-command" },
            { LRG_COMMAND_ERROR_INVALID_PARAMS, "LRG_COMMAND_ERROR_INVALID_PARAMS", "invalid-params" },
            { LRG_COMMAND_ERROR_NOT_FOUND, "LRG_COMMAND_ERROR_NOT_FOUND", "not-found" },
            { LRG_COMMAND_ERROR_REFUSED, "LRG_COMMAND_ERROR_REFUSED", "refused" },
            { LRG_COMMAND_ERROR_UNSUPPORTED, "LRG_COMMAND_ERROR_UNSUPPORTED", "unsupported" },
            { LRG_COMMAND_ERROR_CANCELLED, "LRG_COMMAND_ERROR_CANCELLED", "cancelled" },
            { LRG_COMMAND_ERROR_UNAUTHORIZED, "LRG_COMMAND_ERROR_UNAUTHORIZED", "unauthorized" },
            { 0, NULL, NULL }
        };
        GType id;

        id = g_enum_register_static ("LrgCommandError", values);
        g_once_init_leave (&type_id, id);
    }
    return (GType)type_id;
}

/* JSON-RPC codes: the standard ones, then the server-defined -32000.. */
gint
lrg_command_error_to_jsonrpc_code (const GError *error)
{
    g_return_val_if_fail (error != NULL, -32000);

    if (error->domain != LRG_COMMAND_ERROR)
        return -32000;
    switch ((LrgCommandError)error->code)
    {
    case LRG_COMMAND_ERROR_UNKNOWN_COMMAND:
        return -32601;
    case LRG_COMMAND_ERROR_INVALID_PARAMS:
        return -32602;
    case LRG_COMMAND_ERROR_NOT_FOUND:
        return -32001;
    case LRG_COMMAND_ERROR_REFUSED:
        return -32002;
    case LRG_COMMAND_ERROR_UNSUPPORTED:
        return -32003;
    case LRG_COMMAND_ERROR_CANCELLED:
        return -32004;
    case LRG_COMMAND_ERROR_UNAUTHORIZED:
        return -32005;
    case LRG_COMMAND_ERROR_FAILED:
    default:
        return -32000;
    }
}

LrgCommandError
lrg_command_error_from_jsonrpc_code (gint code)
{
    switch (code)
    {
    case -32601:
        return LRG_COMMAND_ERROR_UNKNOWN_COMMAND;
    case -32700:
    case -32600:
    case -32602:
        return LRG_COMMAND_ERROR_INVALID_PARAMS;
    case -32001:
        return LRG_COMMAND_ERROR_NOT_FOUND;
    case -32002:
        return LRG_COMMAND_ERROR_REFUSED;
    case -32003:
        return LRG_COMMAND_ERROR_UNSUPPORTED;
    case -32004:
        return LRG_COMMAND_ERROR_CANCELLED;
    case -32005:
        return LRG_COMMAND_ERROR_UNAUTHORIZED;
    default:
        return LRG_COMMAND_ERROR_FAILED;
    }
}

/* ------------------------------------------------------------------ */
/* LrgCommandParam                                                      */

struct _LrgCommandParam
{
    gatomicrefcount       ref_count;
    gchar                *name;
    LrgCommandParamType   type;
    LrgCommandParamFlags  flags;
    gchar                *description;
    JsonNode             *default_value;  /* owned, nullable */
    gchar               **choices;        /* owned, nullable */
    gboolean              has_range;
    gdouble               minimum;
    gdouble               maximum;
};

G_DEFINE_BOXED_TYPE (LrgCommandParam, lrg_command_param,
                     lrg_command_param_ref, lrg_command_param_unref)

/* names: lower case letters, digits, '_' and '-' (JSON and shells agree) */
static gboolean
name_is_valid (const gchar *name,
               gboolean     allow_dot)
{
    const gchar *p;

    if (name == NULL || name[0] == '\0' || !g_ascii_islower (name[0]))
        return FALSE;
    for (p = name; *p != '\0'; p++)
    {
        if (g_ascii_islower (*p) || g_ascii_isdigit (*p) || *p == '_' || *p == '-')
            continue;
        if (allow_dot && *p == '.')
            continue;
        return FALSE;
    }
    return TRUE;
}

LrgCommandParam *
lrg_command_param_new (const gchar          *name,
                       LrgCommandParamType   type,
                       LrgCommandParamFlags  flags,
                       const gchar          *description)
{
    LrgCommandParam *self;

    g_return_val_if_fail (name_is_valid (name, FALSE), NULL);
    g_return_val_if_fail (type <= LRG_COMMAND_PARAM_ANY, NULL);

    self = g_new0 (LrgCommandParam, 1);
    g_atomic_ref_count_init (&self->ref_count);
    self->name = g_strdup (name);
    self->type = type;
    self->flags = flags;
    self->description = g_strdup (description);
    return self;
}

LrgCommandParam *
lrg_command_param_ref (LrgCommandParam *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    g_atomic_ref_count_inc (&self->ref_count);
    return self;
}

void
lrg_command_param_unref (LrgCommandParam *self)
{
    g_return_if_fail (self != NULL);

    if (!g_atomic_ref_count_dec (&self->ref_count))
        return;
    g_free (self->name);
    g_free (self->description);
    g_clear_pointer (&self->default_value, json_node_unref);
    g_strfreev (self->choices);
    g_free (self);
}

const gchar *
lrg_command_param_get_name (LrgCommandParam *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->name;
}

LrgCommandParamType
lrg_command_param_get_param_type (LrgCommandParam *self)
{
    g_return_val_if_fail (self != NULL, LRG_COMMAND_PARAM_ANY);

    return self->type;
}

LrgCommandParamFlags
lrg_command_param_get_flags (LrgCommandParam *self)
{
    g_return_val_if_fail (self != NULL, LRG_COMMAND_PARAM_FLAG_NONE);

    return self->flags;
}

gboolean
lrg_command_param_is_required (LrgCommandParam *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return (self->flags & LRG_COMMAND_PARAM_FLAG_REQUIRED) != 0;
}

const gchar *
lrg_command_param_get_description (LrgCommandParam *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->description;
}

void
lrg_command_param_set_default (LrgCommandParam *self,
                               JsonNode        *value)
{
    g_return_if_fail (self != NULL);

    g_clear_pointer (&self->default_value, json_node_unref);
    if (value != NULL)
        self->default_value = json_node_copy (value);
}

JsonNode *
lrg_command_param_get_default (LrgCommandParam *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->default_value;
}

void
lrg_command_param_set_choices (LrgCommandParam    *self,
                               const gchar *const *choices)
{
    g_return_if_fail (self != NULL);

    g_strfreev (self->choices);
    self->choices = g_strdupv ((gchar **)choices);
}

const gchar *const *
lrg_command_param_get_choices (LrgCommandParam *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return (const gchar *const *)self->choices;
}

void
lrg_command_param_set_range (LrgCommandParam *self,
                             gdouble          minimum,
                             gdouble          maximum)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (minimum <= maximum);

    self->has_range = TRUE;
    self->minimum = minimum;
    self->maximum = maximum;
}

gboolean
lrg_command_param_get_range (LrgCommandParam *self,
                             gdouble         *minimum,
                             gdouble         *maximum)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (minimum != NULL)
        *minimum = self->minimum;
    if (maximum != NULL)
        *maximum = self->maximum;
    return self->has_range;
}

/* the JSON schema type name of a parameter type (NULL: any) */
static const gchar *
schema_type_name (LrgCommandParamType type)
{
    switch (type)
    {
    case LRG_COMMAND_PARAM_STRING:
        return "string";
    case LRG_COMMAND_PARAM_INT:
        return "integer";
    case LRG_COMMAND_PARAM_NUMBER:
        return "number";
    case LRG_COMMAND_PARAM_BOOLEAN:
        return "boolean";
    case LRG_COMMAND_PARAM_OBJECT:
        return "object";
    case LRG_COMMAND_PARAM_ARRAY:
        return "array";
    case LRG_COMMAND_PARAM_ANY:
    default:
        return NULL;
    }
}

JsonNode *
lrg_command_param_to_json_schema (LrgCommandParam *self)
{
    g_autoptr(JsonBuilder) builder = NULL;
    const gchar *type_name;
    guint i;

    g_return_val_if_fail (self != NULL, NULL);

    builder = json_builder_new ();
    json_builder_begin_object (builder);
    type_name = schema_type_name (self->type);
    if (type_name != NULL)
    {
        json_builder_set_member_name (builder, "type");
        json_builder_add_string_value (builder, type_name);
    }
    if (self->description != NULL)
    {
        json_builder_set_member_name (builder, "description");
        json_builder_add_string_value (builder, self->description);
    }
    if (self->default_value != NULL)
    {
        json_builder_set_member_name (builder, "default");
        json_builder_add_value (builder, json_node_copy (self->default_value));
    }
    if (self->choices != NULL)
    {
        json_builder_set_member_name (builder, "enum");
        json_builder_begin_array (builder);
        for (i = 0; self->choices[i] != NULL; i++)
            json_builder_add_string_value (builder, self->choices[i]);
        json_builder_end_array (builder);
    }
    if (self->has_range)
    {
        json_builder_set_member_name (builder, "minimum");
        json_builder_add_double_value (builder, self->minimum);
        json_builder_set_member_name (builder, "maximum");
        json_builder_add_double_value (builder, self->maximum);
    }
    if (self->type == LRG_COMMAND_PARAM_ARRAY && (self->flags & LRG_COMMAND_PARAM_FLAG_REST))
    {
        json_builder_set_member_name (builder, "items");
        json_builder_begin_object (builder);
        json_builder_set_member_name (builder, "type");
        json_builder_add_string_value (builder, "string");
        json_builder_end_object (builder);
    }
    json_builder_end_object (builder);
    return json_builder_get_root (builder);
}

/* ------------------------------------------------------------------ */
/* LrgCommandCall                                                       */

struct _LrgCommandCall
{
    GObject      parent_instance;

    LrgCommand  *command;    /* owned, nullable */
    JsonObject  *params;     /* owned */
    gchar       *origin;
    gboolean     done;
    gboolean     cancelled;
    JsonNode    *result;     /* owned, nullable */
    GError      *error;      /* owned, nullable */
};

G_DEFINE_TYPE (LrgCommandCall, lrg_command_call, G_TYPE_OBJECT)

enum
{
    CALL_SIGNAL_COMPLETED,
    CALL_SIGNAL_CANCELLED,
    CALL_N_SIGNALS
};

static guint call_signals[CALL_N_SIGNALS];

static void
lrg_command_call_finalize (GObject *object)
{
    LrgCommandCall *self = LRG_COMMAND_CALL (object);

    g_clear_object (&self->command);
    g_clear_pointer (&self->params, json_object_unref);
    g_free (self->origin);
    g_clear_pointer (&self->result, json_node_unref);
    g_clear_error (&self->error);

    G_OBJECT_CLASS (lrg_command_call_parent_class)->finalize (object);
}

static void
lrg_command_call_class_init (LrgCommandCallClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_command_call_finalize;

    /**
     * LrgCommandCall::completed:
     * @self: the call
     *
     * The call ended with a result or an error (once).
     */
    call_signals[CALL_SIGNAL_COMPLETED] =
        g_signal_new ("completed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 0);

    /**
     * LrgCommandCall::cancelled:
     * @self: the call
     *
     * Someone cancelled the pending call; stop what it started.
     */
    call_signals[CALL_SIGNAL_CANCELLED] =
        g_signal_new ("cancelled", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_command_call_init (LrgCommandCall *self)
{
}

LrgCommandCall *
lrg_command_call_new (LrgCommand  *command,
                      JsonObject  *params,
                      const gchar *origin)
{
    LrgCommandCall *self;

    g_return_val_if_fail (command == NULL || LRG_IS_COMMAND (command), NULL);

    self = g_object_new (LRG_TYPE_COMMAND_CALL, NULL);
    if (command != NULL)
        self->command = g_object_ref (command);
    self->params = params != NULL ? json_object_ref (params) : json_object_new ();
    self->origin = g_strdup (origin);
    return self;
}

LrgCommand *
lrg_command_call_get_command (LrgCommandCall *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), NULL);

    return self->command;
}

const gchar *
lrg_command_call_get_name (LrgCommandCall *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), NULL);

    return self->command != NULL ? lrg_command_get_name (self->command) : NULL;
}

const gchar *
lrg_command_call_get_origin (LrgCommandCall *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), NULL);

    return self->origin;
}

JsonObject *
lrg_command_call_get_params (LrgCommandCall *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), NULL);

    return self->params;
}

JsonNode *
lrg_command_call_get_node (LrgCommandCall *self,
                           const gchar    *name)
{
    JsonNode *node;

    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    if (!json_object_has_member (self->params, name))
        return NULL;
    node = json_object_get_member (self->params, name);
    if (node == NULL || JSON_NODE_HOLDS_NULL (node))
        return NULL;
    return node;
}

gboolean
lrg_command_call_has_param (LrgCommandCall *self,
                            const gchar    *name)
{
    return lrg_command_call_get_node (self, name) != NULL;
}

const gchar *
lrg_command_call_get_string (LrgCommandCall *self,
                             const gchar    *name)
{
    JsonNode *node;

    node = lrg_command_call_get_node (self, name);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) ||
        json_node_get_value_type (node) != G_TYPE_STRING)
        return NULL;
    return json_node_get_string (node);
}

gint64
lrg_command_call_get_int (LrgCommandCall *self,
                          const gchar    *name,
                          gint64          fallback)
{
    JsonNode *node;
    GType type;

    node = lrg_command_call_get_node (self, name);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
        return fallback;
    type = json_node_get_value_type (node);
    if (type == G_TYPE_INT64)
        return json_node_get_int (node);
    if (type == G_TYPE_DOUBLE)
        return (gint64)json_node_get_double (node);
    if (type == G_TYPE_BOOLEAN)
        return json_node_get_boolean (node) ? 1 : 0;
    return fallback;
}

gdouble
lrg_command_call_get_double (LrgCommandCall *self,
                             const gchar    *name,
                             gdouble         fallback)
{
    JsonNode *node;
    GType type;

    node = lrg_command_call_get_node (self, name);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
        return fallback;
    type = json_node_get_value_type (node);
    if (type == G_TYPE_DOUBLE)
        return json_node_get_double (node);
    if (type == G_TYPE_INT64)
        return (gdouble)json_node_get_int (node);
    return fallback;
}

gboolean
lrg_command_call_get_boolean (LrgCommandCall *self,
                              const gchar    *name,
                              gboolean        fallback)
{
    JsonNode *node;

    node = lrg_command_call_get_node (self, name);
    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) ||
        json_node_get_value_type (node) != G_TYPE_BOOLEAN)
        return fallback;
    return json_node_get_boolean (node);
}

/* the completion: once; the signal runs after the state is set */
static void
call_complete (LrgCommandCall *self,
               JsonNode       *result,
               GError         *error)
{
    if (self->done)
    {
        if (result != NULL)
            json_node_unref (result);
        if (error != NULL)
            g_error_free (error);
        return;
    }
    self->done = TRUE;
    if (error != NULL)
    {
        self->error = error;
        if (result != NULL)
            json_node_unref (result);
    }
    else
    {
        self->result = result != NULL ? result : json_node_new (JSON_NODE_NULL);
    }
    g_object_ref (self);
    g_signal_emit (self, call_signals[CALL_SIGNAL_COMPLETED], 0);
    g_object_unref (self);
}

void
lrg_command_call_return_value (LrgCommandCall *self,
                               JsonNode       *result)
{
    g_return_if_fail (LRG_IS_COMMAND_CALL (self));

    call_complete (self, result, NULL);
}

void
lrg_command_call_return_object (LrgCommandCall *self,
                                JsonObject     *result)
{
    JsonNode *node;

    g_return_if_fail (LRG_IS_COMMAND_CALL (self));
    g_return_if_fail (result != NULL);

    node = json_node_new (JSON_NODE_OBJECT);
    json_node_take_object (node, result);
    call_complete (self, node, NULL);
}

void
lrg_command_call_return_error (LrgCommandCall *self,
                               GError         *error)
{
    g_return_if_fail (LRG_IS_COMMAND_CALL (self));
    g_return_if_fail (error != NULL);

    call_complete (self, NULL, error);
}

void
lrg_command_call_return_new_error (LrgCommandCall  *self,
                                   LrgCommandError  code,
                                   const gchar     *format,
                                   ...)
{
    va_list args;
    GError *error;

    g_return_if_fail (LRG_IS_COMMAND_CALL (self));
    g_return_if_fail (format != NULL);

    va_start (args, format);
    error = g_error_new_valist (LRG_COMMAND_ERROR, (gint)code, format, args);
    va_end (args);
    call_complete (self, NULL, error);
}

gboolean
lrg_command_call_is_done (LrgCommandCall *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), FALSE);

    return self->done;
}

JsonNode *
lrg_command_call_get_result (LrgCommandCall *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), NULL);

    return self->result;
}

const GError *
lrg_command_call_get_error (LrgCommandCall *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), NULL);

    return self->error;
}

JsonNode *
lrg_command_call_propagate (LrgCommandCall  *self,
                            GError         **error)
{
    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!self->done)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_PENDING,
                     "%s has not completed", lrg_command_call_get_name (self) != NULL ?
                     lrg_command_call_get_name (self) : "the call");
        return NULL;
    }
    if (self->error != NULL)
    {
        if (error != NULL)
            *error = g_error_copy (self->error);
        return NULL;
    }
    return json_node_copy (self->result);
}

void
lrg_command_call_cancel (LrgCommandCall *self)
{
    g_return_if_fail (LRG_IS_COMMAND_CALL (self));

    if (self->done || self->cancelled)
        return;
    self->cancelled = TRUE;
    g_object_ref (self);
    g_signal_emit (self, call_signals[CALL_SIGNAL_CANCELLED], 0);
    if (!self->done)
        lrg_command_call_return_new_error (self, LRG_COMMAND_ERROR_CANCELLED, "%s was cancelled",
                                           lrg_command_call_get_name (self) != NULL ?
                                           lrg_command_call_get_name (self) : "the call");
    g_object_unref (self);
}

gboolean
lrg_command_call_is_cancelled (LrgCommandCall *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_CALL (self), FALSE);

    return self->cancelled;
}

/* ------------------------------------------------------------------ */
/* LrgCommand                                                           */

typedef struct
{
    gchar             *name;
    gchar             *group;
    gchar             *summary;
    gchar             *description;
    gchar             *example;
    LrgCommandFlags    flags;
    GPtrArray         *params;     /* LrgCommandParam, owned */
    LrgCommandHandler  handler;
    gpointer           user_data;
    GDestroyNotify     destroy;
} LrgCommandPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCommand, lrg_command, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_GROUP,
    PROP_SUMMARY,
    PROP_DESCRIPTION,
    PROP_FLAGS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_command_finalize (GObject *object)
{
    LrgCommand *self = LRG_COMMAND (object);
    LrgCommandPrivate *priv = lrg_command_get_instance_private (self);

    if (priv->destroy != NULL)
        priv->destroy (priv->user_data);
    g_free (priv->name);
    g_free (priv->group);
    g_free (priv->summary);
    g_free (priv->description);
    g_free (priv->example);
    g_ptr_array_unref (priv->params);

    G_OBJECT_CLASS (lrg_command_parent_class)->finalize (object);
}

static void
lrg_command_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgCommand *self = LRG_COMMAND (object);
    LrgCommandPrivate *priv = lrg_command_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_GROUP:
        g_value_set_string (value, priv->group);
        break;
    case PROP_SUMMARY:
        g_value_set_string (value, priv->summary);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, priv->description);
        break;
    case PROP_FLAGS:
        g_value_set_flags (value, priv->flags);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_command_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgCommand *self = LRG_COMMAND (object);
    LrgCommandPrivate *priv = lrg_command_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_GROUP:
        g_free (priv->group);
        priv->group = g_value_dup_string (value);
        break;
    case PROP_SUMMARY:
        g_free (priv->summary);
        priv->summary = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        lrg_command_set_description (self, g_value_get_string (value));
        break;
    case PROP_FLAGS:
        lrg_command_set_flags (self, (LrgCommandFlags)g_value_get_flags (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* the default invoke: the handler given to lrg_command_new() */
static void
lrg_command_real_invoke (LrgCommand     *self,
                         LrgCommandCall *call)
{
    LrgCommandPrivate *priv = lrg_command_get_instance_private (self);

    if (priv->handler == NULL)
    {
        lrg_command_call_return_new_error (call, LRG_COMMAND_ERROR_UNSUPPORTED,
                                           "%s has no handler", priv->name);
        return;
    }
    priv->handler (self, call, priv->user_data);
}

static void
lrg_command_class_init (LrgCommandClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_command_finalize;
    object_class->get_property = lrg_command_get_property;
    object_class->set_property = lrg_command_set_property;
    klass->invoke = lrg_command_real_invoke;

    /**
     * LrgCommand:name:
     *
     * The command's name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "The command's name", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCommand:group:
     *
     * The command's group for listings.
     */
    properties[PROP_GROUP] =
        g_param_spec_string ("group", "Group", "The command's group", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCommand:summary:
     *
     * One line of help.
     */
    properties[PROP_SUMMARY] =
        g_param_spec_string ("summary", "Summary", "One line of help", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCommand:description:
     *
     * Longer help.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", "Description", "Longer help", NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgCommand:flags:
     *
     * The #LrgCommandFlags.
     */
    properties[PROP_FLAGS] =
        g_param_spec_flags ("flags", "Flags", "The command's flags", LRG_TYPE_COMMAND_FLAGS,
                            LRG_COMMAND_FLAG_NONE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_command_init (LrgCommand *self)
{
    LrgCommandPrivate *priv = lrg_command_get_instance_private (self);

    priv->params = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_command_param_unref);
}

LrgCommand *
lrg_command_new (const gchar       *name,
                 const gchar       *group,
                 const gchar       *summary,
                 LrgCommandHandler  handler,
                 gpointer           user_data,
                 GDestroyNotify     destroy)
{
    LrgCommand *self;
    LrgCommandPrivate *priv;

    g_return_val_if_fail (name_is_valid (name, TRUE), NULL);

    self = g_object_new (LRG_TYPE_COMMAND, "name", name, "group", group, "summary", summary, NULL);
    priv = lrg_command_get_instance_private (self);
    priv->handler = handler;
    priv->user_data = user_data;
    priv->destroy = destroy;
    return self;
}

const gchar *
lrg_command_get_name (LrgCommand *self)
{
    LrgCommandPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);
    priv = lrg_command_get_instance_private (self);
    return priv->name;
}

const gchar *
lrg_command_get_group (LrgCommand *self)
{
    LrgCommandPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);
    priv = lrg_command_get_instance_private (self);
    return priv->group;
}

const gchar *
lrg_command_get_summary (LrgCommand *self)
{
    LrgCommandPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);
    priv = lrg_command_get_instance_private (self);
    return priv->summary;
}

void
lrg_command_set_description (LrgCommand  *self,
                             const gchar *description)
{
    LrgCommandPrivate *priv;

    g_return_if_fail (LRG_IS_COMMAND (self));
    priv = lrg_command_get_instance_private (self);
    if (g_strcmp0 (priv->description, description) == 0)
        return;
    g_free (priv->description);
    priv->description = g_strdup (description);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
}

const gchar *
lrg_command_get_description (LrgCommand *self)
{
    LrgCommandPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);
    priv = lrg_command_get_instance_private (self);
    return priv->description;
}

void
lrg_command_set_flags (LrgCommand      *self,
                       LrgCommandFlags  flags)
{
    LrgCommandPrivate *priv;

    g_return_if_fail (LRG_IS_COMMAND (self));
    priv = lrg_command_get_instance_private (self);
    if (priv->flags == flags)
        return;
    priv->flags = flags;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLAGS]);
}

LrgCommandFlags
lrg_command_get_flags (LrgCommand *self)
{
    LrgCommandPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMMAND (self), LRG_COMMAND_FLAG_NONE);
    priv = lrg_command_get_instance_private (self);
    return priv->flags;
}

void
lrg_command_set_example (LrgCommand  *self,
                         const gchar *example)
{
    LrgCommandPrivate *priv;

    g_return_if_fail (LRG_IS_COMMAND (self));
    priv = lrg_command_get_instance_private (self);
    g_free (priv->example);
    priv->example = g_strdup (example);
}

const gchar *
lrg_command_get_example (LrgCommand *self)
{
    LrgCommandPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);
    priv = lrg_command_get_instance_private (self);
    return priv->example;
}

gboolean
lrg_command_add_param (LrgCommand      *self,
                       LrgCommandParam *param)
{
    LrgCommandPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMMAND (self), FALSE);
    g_return_val_if_fail (param != NULL, FALSE);

    priv = lrg_command_get_instance_private (self);
    if (lrg_command_find_param (self, param->name) != NULL)
        return FALSE;
    g_ptr_array_add (priv->params, lrg_command_param_ref (param));
    return TRUE;
}

LrgCommandParam *
lrg_command_add_param_full (LrgCommand           *self,
                            const gchar          *name,
                            LrgCommandParamType   type,
                            LrgCommandParamFlags  flags,
                            const gchar          *description)
{
    g_autoptr(LrgCommandParam) param = NULL;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);

    param = lrg_command_param_new (name, type, flags, description);
    g_return_val_if_fail (param != NULL, NULL);
    if (!lrg_command_add_param (self, param))
    {
        g_critical ("%s: command %s already has a parameter %s", G_STRFUNC,
                    lrg_command_get_name (self), name);
        return NULL;
    }
    return param; /* the command holds a reference */
}

GPtrArray *
lrg_command_get_params (LrgCommand *self)
{
    LrgCommandPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);
    priv = lrg_command_get_instance_private (self);
    return priv->params;
}

LrgCommandParam *
lrg_command_find_param (LrgCommand  *self,
                        const gchar *name)
{
    LrgCommandPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_command_get_instance_private (self);
    for (i = 0; i < priv->params->len; i++)
    {
        LrgCommandParam *param = g_ptr_array_index (priv->params, i);

        if (strcmp (param->name, name) == 0)
            return param;
    }
    return NULL;
}

JsonNode *
lrg_command_get_input_schema (LrgCommand *self)
{
    g_autoptr(JsonBuilder) builder = NULL;
    LrgCommandPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);

    priv = lrg_command_get_instance_private (self);
    builder = json_builder_new ();
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "type");
    json_builder_add_string_value (builder, "object");
    json_builder_set_member_name (builder, "properties");
    json_builder_begin_object (builder);
    for (i = 0; i < priv->params->len; i++)
    {
        LrgCommandParam *param = g_ptr_array_index (priv->params, i);

        json_builder_set_member_name (builder, param->name);
        json_builder_add_value (builder, lrg_command_param_to_json_schema (param));
    }
    json_builder_end_object (builder);
    json_builder_set_member_name (builder, "required");
    json_builder_begin_array (builder);
    for (i = 0; i < priv->params->len; i++)
    {
        LrgCommandParam *param = g_ptr_array_index (priv->params, i);

        if (lrg_command_param_is_required (param))
            json_builder_add_string_value (builder, param->name);
    }
    json_builder_end_array (builder);
    json_builder_set_member_name (builder, "additionalProperties");
    json_builder_add_boolean_value (builder, FALSE);
    json_builder_end_object (builder);
    return json_builder_get_root (builder);
}

JsonNode *
lrg_command_describe (LrgCommand *self)
{
    g_autoptr(JsonBuilder) builder = NULL;
    LrgCommandPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);

    priv = lrg_command_get_instance_private (self);
    builder = json_builder_new ();
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "name");
    json_builder_add_string_value (builder, priv->name);
    json_builder_set_member_name (builder, "group");
    json_builder_add_string_value (builder, priv->group != NULL ? priv->group : "");
    json_builder_set_member_name (builder, "summary");
    json_builder_add_string_value (builder, priv->summary != NULL ? priv->summary : "");
    if (priv->description != NULL)
    {
        json_builder_set_member_name (builder, "description");
        json_builder_add_string_value (builder, priv->description);
    }
    if (priv->example != NULL)
    {
        json_builder_set_member_name (builder, "example");
        json_builder_add_string_value (builder, priv->example);
    }
    json_builder_set_member_name (builder, "read_only");
    json_builder_add_boolean_value (builder, (priv->flags & LRG_COMMAND_FLAG_READ_ONLY) != 0);
    json_builder_set_member_name (builder, "async");
    json_builder_add_boolean_value (builder, (priv->flags & LRG_COMMAND_FLAG_ASYNC) != 0);
    json_builder_set_member_name (builder, "destructive");
    json_builder_add_boolean_value (builder, (priv->flags & LRG_COMMAND_FLAG_DESTRUCTIVE) != 0);
    json_builder_set_member_name (builder, "params");
    json_builder_begin_array (builder);
    for (i = 0; i < priv->params->len; i++)
    {
        LrgCommandParam *param = g_ptr_array_index (priv->params, i);
        g_autoptr(JsonNode) schema = NULL;
        JsonObject *object;
        GEnumClass *enum_class;
        GEnumValue *value;

        schema = lrg_command_param_to_json_schema (param);
        object = json_node_get_object (schema);
        json_object_set_string_member (object, "name", param->name);
        enum_class = g_type_class_ref (LRG_TYPE_COMMAND_PARAM_TYPE);
        value = g_enum_get_value (enum_class, (gint)param->type);
        json_object_set_string_member (object, "type", value != NULL ? value->value_nick : "any");
        g_type_class_unref (enum_class);
        json_object_set_boolean_member (object, "required", lrg_command_param_is_required (param));
        json_object_set_boolean_member (object, "rest",
                                        (param->flags & LRG_COMMAND_PARAM_FLAG_REST) != 0);
        if (json_object_has_member (object, "items"))
            json_object_remove_member (object, "items");
        json_builder_add_value (builder, json_node_ref (schema));
    }
    json_builder_end_array (builder);
    json_builder_set_member_name (builder, "schema");
    json_builder_add_value (builder, lrg_command_get_input_schema (self));
    json_builder_end_object (builder);
    return json_builder_get_root (builder);
}

/* ------------------------------------------------------------------ */
/* checking and converting values                                      */

static gboolean
param_fail (GError      **error,
            LrgCommand   *self,
            const gchar  *format,
            ...) G_GNUC_PRINTF (3, 4);

/* an INVALID_PARAMS error prefixed with the command's name */
static gboolean
param_fail (GError      **error,
            LrgCommand   *self,
            const gchar  *format,
            ...)
{
    g_autofree gchar *message = NULL;
    va_list args;

    va_start (args, format);
    message = g_strdup_vprintf (format, args);
    va_end (args);
    g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS, "%s: %s",
                 lrg_command_get_name (self), message);
    return FALSE;
}

/* the accepted parameter names, for "unknown parameter" messages */
static gchar *
accepted_names (LrgCommand *self)
{
    LrgCommandPrivate *priv = lrg_command_get_instance_private (self);
    GString *text;
    guint i;

    if (priv->params->len == 0)
        return g_strdup ("none");
    text = g_string_new (NULL);
    for (i = 0; i < priv->params->len; i++)
    {
        LrgCommandParam *param = g_ptr_array_index (priv->params, i);

        if (i > 0)
            g_string_append (text, ", ");
        g_string_append (text, param->name);
    }
    return g_string_free (text, FALSE);
}

/* 2^63. Every gint64 is strictly below this double; -2^63 is
 * G_MININT64 and is exact in a double. Comparing a full id to a
 * double rounds it, so the range checks stay in integer space. */
#define INT64_DOUBLE_LIMIT (9223372036854775808.0)

/* TRUE when @i is strictly less than @bound. @i is never rounded. */
static gboolean
int_below_double (gint64  i,
                  gdouble bound)
{
    gdouble floored;
    gint64 limit;

    if (isnan (bound) || bound >= INT64_DOUBLE_LIMIT)
        return TRUE;
    /* @bound <= -2^63: no gint64 is smaller */
    if (bound <= -INT64_DOUBLE_LIMIT)
        return FALSE;
    floored = floor (bound);
    limit = (gint64)floored;
    if (floored == bound)
        return i < limit;
    return i <= limit;
}

/* TRUE when @i is strictly greater than @bound. @i is never rounded. */
static gboolean
int_above_double (gint64  i,
                  gdouble bound)
{
    gdouble ceiled;
    gint64 limit;

    if (isnan (bound))
        return TRUE;
    if (bound >= INT64_DOUBLE_LIMIT)
        return FALSE;
    if (bound < -INT64_DOUBLE_LIMIT)
        return TRUE;
    ceiled = ceil (bound);
    /* ceil(-2^63) is -2^63; the cast of that magnitude is G_MININT64 */
    if (ceiled <= -INT64_DOUBLE_LIMIT)
        return i != G_MININT64;
    if (ceiled >= INT64_DOUBLE_LIMIT)
        return FALSE;
    limit = (gint64)ceiled;
    if (ceiled == bound)
        return i > limit;
    return i >= limit;
}

/* A finite double that is a whole number and exactly one gint64. */
static gboolean
double_to_exact_int (gdouble  number,
                     gint64  *out)
{
    gint64 value;

    if (!isfinite (number) || number != floor (number))
        return FALSE;
    if (number < -INT64_DOUBLE_LIMIT || number >= INT64_DOUBLE_LIMIT)
        return FALSE;
    value = (gint64)number;
    if ((gdouble)value != number)
        return FALSE;
    *out = value;
    return TRUE;
}

/* checks one given value; returns the (possibly converted) copy */
static JsonNode *
check_value (LrgCommand      *self,
             LrgCommandParam *param,
             JsonNode        *node,
             GError         **error)
{
    GType value_type;
    gdouble number;

    value_type = JSON_NODE_HOLDS_VALUE (node) ? json_node_get_value_type (node) : G_TYPE_INVALID;
    switch (param->type)
    {
    case LRG_COMMAND_PARAM_STRING:
        if (value_type != G_TYPE_STRING)
        {
            param_fail (error, self, "%s must be a string", param->name);
            return NULL;
        }
        if (param->choices != NULL &&
            !g_strv_contains ((const gchar *const *)param->choices, json_node_get_string (node)))
        {
            g_autofree gchar *list = g_strjoinv (", ", param->choices);

            param_fail (error, self, "%s must be one of: %s", param->name, list);
            return NULL;
        }
        return json_node_copy (node);

    case LRG_COMMAND_PARAM_INT:
    {
        /* Kept as gint64. A double only stands in when it is an exact
         * integer (3.0); ids past 2^53 never go through a double. */
        gint64 ival;

        if (value_type == G_TYPE_INT64)
            ival = json_node_get_int (node);
        else if (value_type == G_TYPE_DOUBLE)
        {
            if (!double_to_exact_int (json_node_get_double (node), &ival))
            {
                param_fail (error, self, "%s must be an integer", param->name);
                return NULL;
            }
        }
        else
        {
            param_fail (error, self, "%s must be an integer", param->name);
            return NULL;
        }
        if (param->has_range &&
            (int_below_double (ival, param->minimum) || int_above_double (ival, param->maximum)))
        {
            param_fail (error, self, "%s must be between %g and %g", param->name,
                        param->minimum, param->maximum);
            return NULL;
        }
        {
            JsonNode *out = json_node_new (JSON_NODE_VALUE);

            json_node_set_int (out, ival);
            return out;
        }
    }

    case LRG_COMMAND_PARAM_NUMBER:
        if (value_type == G_TYPE_INT64)
            number = (gdouble)json_node_get_int (node);
        else if (value_type == G_TYPE_DOUBLE)
            number = json_node_get_double (node);
        else
        {
            param_fail (error, self, "%s must be a number", param->name);
            return NULL;
        }
        if (!isfinite (number))
        {
            param_fail (error, self, "%s must be finite", param->name);
            return NULL;
        }
        if (param->has_range && (number < param->minimum || number > param->maximum))
        {
            param_fail (error, self, "%s must be between %g and %g", param->name,
                        param->minimum, param->maximum);
            return NULL;
        }
        {
            JsonNode *out = json_node_new (JSON_NODE_VALUE);

            json_node_set_double (out, number);
            return out;
        }

    case LRG_COMMAND_PARAM_BOOLEAN:
        if (value_type != G_TYPE_BOOLEAN)
        {
            param_fail (error, self, "%s must be true or false", param->name);
            return NULL;
        }
        return json_node_copy (node);

    case LRG_COMMAND_PARAM_OBJECT:
        if (!JSON_NODE_HOLDS_OBJECT (node))
        {
            param_fail (error, self, "%s must be an object", param->name);
            return NULL;
        }
        return json_node_copy (node);

    case LRG_COMMAND_PARAM_ARRAY:
        if (!JSON_NODE_HOLDS_ARRAY (node))
        {
            param_fail (error, self, "%s must be an array", param->name);
            return NULL;
        }
        return json_node_copy (node);

    case LRG_COMMAND_PARAM_ANY:
    default:
        return json_node_copy (node);
    }
}

JsonObject *
lrg_command_check_params (LrgCommand  *self,
                          JsonObject  *params,
                          GError     **error)
{
    g_autoptr(JsonObject) out = NULL;
    LrgCommandPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    priv = lrg_command_get_instance_private (self);
    out = json_object_new ();

    /* --- unknown names first: they are the usual typo --- */
    if (params != NULL)
    {
        g_autoptr(GList) members = json_object_get_members (params);
        GList *l;

        for (l = members; l != NULL; l = l->next)
        {
            const gchar *name = l->data;

            if (lrg_command_find_param (self, name) == NULL)
            {
                g_autofree gchar *names = accepted_names (self);

                param_fail (error, self, "unknown parameter %s (accepted: %s)", name, names);
                return NULL;
            }
        }
    }

    /* --- every declared parameter: given, default or missing --- */
    for (i = 0; i < priv->params->len; i++)
    {
        LrgCommandParam *param = g_ptr_array_index (priv->params, i);
        JsonNode *given = NULL;

        if (params != NULL && json_object_has_member (params, param->name))
        {
            given = json_object_get_member (params, param->name);
            if (given != NULL && JSON_NODE_HOLDS_NULL (given))
                given = NULL;
        }
        if (given != NULL)
        {
            JsonNode *checked = check_value (self, param, given, error);

            if (checked == NULL)
                return NULL;
            json_object_set_member (out, param->name, checked);
        }
        else if (param->default_value != NULL)
        {
            json_object_set_member (out, param->name, json_node_copy (param->default_value));
        }
        else if (lrg_command_param_is_required (param))
        {
            param_fail (error, self, "missing required parameter %s", param->name);
            return NULL;
        }
    }
    return g_steal_pointer (&out);
}

/* one text word as a value of the parameter's type (NULL: refused) */
static JsonNode *
word_to_node (LrgCommand       *self,
              LrgCommandParam  *param,
              const gchar      *word,
              GError          **error)
{
    JsonNode *node;
    gchar *end;

    switch (param->type)
    {
    case LRG_COMMAND_PARAM_STRING:
        node = json_node_new (JSON_NODE_VALUE);
        json_node_set_string (node, word);
        return node;

    case LRG_COMMAND_PARAM_INT:
    {
        gint64 value;

        errno = 0;
        value = g_ascii_strtoll (word, &end, 10);
        if (errno != 0 || end == word || *end != '\0')
        {
            gdouble d;

            /* "3.0" is a whole number; a magnitude a double cannot hold
             * exactly is refused instead of rounded. */
            errno = 0;
            d = g_ascii_strtod (word, &end);
            if (errno != 0 || end == word || *end != '\0' || !double_to_exact_int (d, &value))
            {
                param_fail (error, self, "%s must be an integer, not '%s'", param->name, word);
                return NULL;
            }
        }
        node = json_node_new (JSON_NODE_VALUE);
        json_node_set_int (node, value);
        return node;
    }

    case LRG_COMMAND_PARAM_NUMBER:
    {
        gdouble value;

        errno = 0;
        value = g_ascii_strtod (word, &end);
        if (errno != 0 || end == word || *end != '\0')
        {
            param_fail (error, self, "%s must be a number, not '%s'", param->name, word);
            return NULL;
        }
        node = json_node_new (JSON_NODE_VALUE);
        json_node_set_double (node, value);
        return node;
    }

    case LRG_COMMAND_PARAM_BOOLEAN:
    {
        gboolean value;

        if (g_ascii_strcasecmp (word, "true") == 0 || g_ascii_strcasecmp (word, "yes") == 0 ||
            g_ascii_strcasecmp (word, "on") == 0 || strcmp (word, "1") == 0)
            value = TRUE;
        else if (g_ascii_strcasecmp (word, "false") == 0 || g_ascii_strcasecmp (word, "no") == 0 ||
                 g_ascii_strcasecmp (word, "off") == 0 || strcmp (word, "0") == 0)
            value = FALSE;
        else
        {
            param_fail (error, self, "%s must be true or false, not '%s'", param->name, word);
            return NULL;
        }
        node = json_node_new (JSON_NODE_VALUE);
        json_node_set_boolean (node, value);
        return node;
    }

    case LRG_COMMAND_PARAM_OBJECT:
    case LRG_COMMAND_PARAM_ARRAY:
    case LRG_COMMAND_PARAM_ANY:
    default:
    {
        g_autoptr(JsonParser) parser = json_parser_new ();

        if (json_parser_load_from_data (parser, word, -1, NULL) &&
            json_parser_get_root (parser) != NULL)
            return json_node_copy (json_parser_get_root (parser));
        if (param->type != LRG_COMMAND_PARAM_ANY)
        {
            param_fail (error, self, "%s must be JSON", param->name);
            return NULL;
        }
        node = json_node_new (JSON_NODE_VALUE);
        json_node_set_string (node, word);
        return node;
    }
    }
}

JsonObject *
lrg_command_parse_arguments (LrgCommand         *self,
                             const gchar *const *argv,
                             GError            **error)
{
    g_autoptr(JsonObject) out = NULL;
    LrgCommandPrivate *priv;
    guint next;
    guint i;

    g_return_val_if_fail (LRG_IS_COMMAND (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    priv = lrg_command_get_instance_private (self);
    out = json_object_new ();
    next = 0;
    for (i = 0; argv != NULL && argv[i] != NULL; i++)
    {
        const gchar *word = argv[i];
        const gchar *eq = strchr (word, '=');
        LrgCommandParam *param = NULL;
        JsonNode *node;

        /* --- name=value for a known name --- */
        if (eq != NULL && eq != word)
        {
            g_autofree gchar *name = g_strndup (word, (gsize)(eq - word));

            param = lrg_command_find_param (self, name);
            if (param != NULL)
            {
                node = word_to_node (self, param, eq + 1, error);
                if (node == NULL)
                    return NULL;
                json_object_set_member (out, param->name, node);
                continue;
            }
        }

        /* --- positional: the next parameter not given by name --- */
        while (next < priv->params->len &&
               json_object_has_member (out, ((LrgCommandParam *)g_ptr_array_index (priv->params, next))->name))
            next++;
        if (next >= priv->params->len)
        {
            param_fail (error, self, "too many arguments ('%s')", word);
            return NULL;
        }
        param = g_ptr_array_index (priv->params, next);
        if ((param->flags & LRG_COMMAND_PARAM_FLAG_REST) != 0)
        {
            /* the rest of the words */
            if (param->type == LRG_COMMAND_PARAM_ARRAY)
            {
                JsonArray *array = json_array_new ();

                for (; argv[i] != NULL; i++)
                    json_array_add_string_element (array, argv[i]);
                node = json_node_new (JSON_NODE_ARRAY);
                json_node_take_array (node, array);
            }
            else
            {
                g_autofree gchar *joined = g_strjoinv (" ", (gchar **)(argv + i));

                node = word_to_node (self, param, joined, error);
                if (node == NULL)
                    return NULL;
            }
            json_object_set_member (out, param->name, node);
            break;
        }
        node = word_to_node (self, param, word, error);
        if (node == NULL)
            return NULL;
        json_object_set_member (out, param->name, node);
        next++;
    }
    return g_steal_pointer (&out);
}

void
lrg_command_invoke (LrgCommand     *self,
                    LrgCommandCall *call)
{
    LrgCommandClass *klass;

    g_return_if_fail (LRG_IS_COMMAND (self));
    g_return_if_fail (LRG_IS_COMMAND_CALL (call));

    klass = LRG_COMMAND_GET_CLASS (self);
    g_return_if_fail (klass->invoke != NULL);
    klass->invoke (self, call);
}
