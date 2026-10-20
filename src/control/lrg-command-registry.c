/* lrg-command-registry.c - The commands a game offers, and its events
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"
#include "lrg-command-registry.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

/**
 * SECTION:lrg-command-registry
 * @title: LrgCommandRegistry
 * @short_description: A game's commands by name, and its event stream
 *
 * See lrg-command-registry.h.
 */

struct _LrgCommandRegistry
{
    GObject      parent_instance;

    GHashTable  *commands;   /* name -> LrgCommand (owned) */
    GHashTable  *topics;     /* topic -> description (owned strings) */
    guint64      event_seq;
};

G_DEFINE_TYPE (LrgCommandRegistry, lrg_command_registry, G_TYPE_OBJECT)

enum
{
    SIGNAL_COMMAND_ADDED,
    SIGNAL_COMMAND_REMOVED,
    SIGNAL_EVENT,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_command_registry_finalize (GObject *object)
{
    LrgCommandRegistry *self = LRG_COMMAND_REGISTRY (object);

    g_hash_table_unref (self->commands);
    g_hash_table_unref (self->topics);

    G_OBJECT_CLASS (lrg_command_registry_parent_class)->finalize (object);
}

static void
lrg_command_registry_class_init (LrgCommandRegistryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_command_registry_finalize;

    /**
     * LrgCommandRegistry::command-added:
     * @self: the registry
     * @command: the new command
     */
    signals[SIGNAL_COMMAND_ADDED] =
        g_signal_new ("command-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_COMMAND);

    /**
     * LrgCommandRegistry::command-removed:
     * @self: the registry
     * @command: the removed command
     */
    signals[SIGNAL_COMMAND_REMOVED] =
        g_signal_new ("command-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_COMMAND);

    /**
     * LrgCommandRegistry::event:
     * @self: the registry
     * @topic: the event's topic
     * @data: (nullable): its payload
     * @seq: its sequence number
     *
     * An event was published (lrg_command_registry_publish()).
     */
    signals[SIGNAL_EVENT] =
        g_signal_new ("event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 3,
                      G_TYPE_STRING, JSON_TYPE_NODE, G_TYPE_UINT64);
}

static void
lrg_command_registry_init (LrgCommandRegistry *self)
{
    self->commands = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
    self->topics = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

LrgCommandRegistry *
lrg_command_registry_new (void)
{
    return g_object_new (LRG_TYPE_COMMAND_REGISTRY, NULL);
}

gboolean
lrg_command_registry_register (LrgCommandRegistry  *self,
                               LrgCommand          *command,
                               GError             **error)
{
    const gchar *name;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), FALSE);
    g_return_val_if_fail (LRG_IS_COMMAND (command), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    name = lrg_command_get_name (command);
    if (name == NULL || name[0] == '\0')
    {
        g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS,
                     "a command needs a name");
        return FALSE;
    }
    if (g_hash_table_contains (self->commands, name))
    {
        g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS,
                     "a command named %s is registered", name);
        return FALSE;
    }
    if (g_str_has_prefix (name, "rpc.") || g_str_has_prefix (name, "events.") ||
        strcmp (name, "auth") == 0)
    {
        g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS,
                     "%s is reserved for the control protocol", name);
        return FALSE;
    }
    /* the key is the command's own name string, alive while it is held */
    g_hash_table_insert (self->commands, (gpointer)name, g_object_ref (command));
    g_signal_emit (self, signals[SIGNAL_COMMAND_ADDED], 0, command);
    return TRUE;
}

LrgCommand *
lrg_command_registry_add (LrgCommandRegistry *self,
                          const gchar        *name,
                          const gchar        *group,
                          const gchar        *summary,
                          LrgCommandHandler   handler,
                          gpointer            user_data,
                          GDestroyNotify      destroy)
{
    g_autoptr(LrgCommand) command = NULL;
    g_autoptr(GError) error = NULL;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), NULL);

    command = lrg_command_new (name, group, summary, handler, user_data, destroy);
    g_return_val_if_fail (command != NULL, NULL);
    if (!lrg_command_registry_register (self, command, &error))
    {
        g_critical ("%s: %s", G_STRFUNC, error->message);
        return NULL;
    }
    return command; /* the registry holds a reference */
}

gboolean
lrg_command_registry_unregister (LrgCommandRegistry *self,
                                 const gchar        *name)
{
    g_autoptr(LrgCommand) command = NULL;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    command = g_hash_table_lookup (self->commands, name);
    if (command == NULL)
        return FALSE;
    g_object_ref (command);
    g_hash_table_remove (self->commands, name);
    g_signal_emit (self, signals[SIGNAL_COMMAND_REMOVED], 0, command);
    return TRUE;
}

LrgCommand *
lrg_command_registry_lookup (LrgCommandRegistry *self,
                             const gchar        *name)
{
    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_hash_table_lookup (self->commands, name);
}

/* group, then name; commands without a group last */
static gint
compare_commands (gconstpointer a,
                  gconstpointer b)
{
    LrgCommand *ca = *(LrgCommand **)a;
    LrgCommand *cb = *(LrgCommand **)b;
    const gchar *ga = lrg_command_get_group (ca);
    const gchar *gb = lrg_command_get_group (cb);
    gint order;

    if (ga == NULL && gb != NULL)
        return 1;
    if (ga != NULL && gb == NULL)
        return -1;
    order = g_strcmp0 (ga, gb);
    if (order != 0)
        return order;
    return g_strcmp0 (lrg_command_get_name (ca), lrg_command_get_name (cb));
}

GPtrArray *
lrg_command_registry_list (LrgCommandRegistry *self,
                           gboolean            include_hidden)
{
    GPtrArray *list;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), NULL);

    list = g_ptr_array_new ();
    g_hash_table_iter_init (&iter, self->commands);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        if (!include_hidden && (lrg_command_get_flags (value) & LRG_COMMAND_FLAG_HIDDEN))
            continue;
        g_ptr_array_add (list, value);
    }
    g_ptr_array_sort (list, compare_commands);
    return list;
}

guint
lrg_command_registry_get_n_commands (LrgCommandRegistry *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), 0);

    return g_hash_table_size (self->commands);
}

JsonNode *
lrg_command_registry_describe (LrgCommandRegistry *self)
{
    g_autoptr(GPtrArray) list = NULL;
    JsonArray *array;
    JsonNode *node;
    guint i;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), NULL);

    list = lrg_command_registry_list (self, FALSE);
    array = json_array_new ();
    for (i = 0; i < list->len; i++)
        json_array_add_element (array, lrg_command_describe (g_ptr_array_index (list, i)));
    node = json_node_new (JSON_NODE_ARRAY);
    json_node_take_array (node, array);
    return node;
}

/* a call that failed before its command ran */
static LrgCommandCall *
failed_call (LrgCommand  *command,
             const gchar *origin,
             GError      *error)
{
    LrgCommandCall *call;

    call = lrg_command_call_new (command, NULL, origin);
    lrg_command_call_return_error (call, error);
    return call;
}

LrgCommandCall *
lrg_command_registry_invoke (LrgCommandRegistry *self,
                             const gchar        *name,
                             JsonObject         *params,
                             const gchar        *origin)
{
    g_autoptr(JsonObject) checked = NULL;
    GError *error = NULL;
    LrgCommandCall *call;
    LrgCommand *command;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    command = g_hash_table_lookup (self->commands, name);
    if (command == NULL)
        return failed_call (NULL, origin,
                            g_error_new (LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNKNOWN_COMMAND,
                                         "unknown command '%s'", name));
    checked = lrg_command_check_params (command, params, &error);
    if (checked == NULL)
        return failed_call (command, origin, error);

    /* hold the command while it runs: a handler may unregister it */
    g_object_ref (command);
    call = lrg_command_call_new (command, checked, origin);
    lrg_command_invoke (command, call);
    if (!lrg_command_call_is_done (call) &&
        (lrg_command_get_flags (command) & LRG_COMMAND_FLAG_ASYNC) == 0)
        lrg_command_call_return_value (call, NULL);
    g_object_unref (command);
    return call;
}

gboolean
lrg_command_registry_parse_line (LrgCommandRegistry  *self,
                                 const gchar         *line,
                                 gchar              **name,
                                 JsonObject         **params,
                                 GError             **error)
{
    g_auto(GStrv) argv = NULL;
    g_autoptr(GError) shell_error = NULL;
    LrgCommand *command;
    JsonObject *parsed;
    gint argc;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), FALSE);
    g_return_val_if_fail (line != NULL, FALSE);
    g_return_val_if_fail (name != NULL && params != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    *name = NULL;
    *params = NULL;
    if (!g_shell_parse_argv (line, &argc, &argv, &shell_error))
    {
        /* an empty line is G_SHELL_ERROR_EMPTY_STRING */
        g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS,
                     "cannot read the command: %s", shell_error->message);
        return FALSE;
    }
    command = g_hash_table_lookup (self->commands, argv[0]);
    if (command == NULL)
    {
        g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNKNOWN_COMMAND,
                     "unknown command '%s'", argv[0]);
        return FALSE;
    }
    parsed = lrg_command_parse_arguments (command, (const gchar *const *)(argv + 1), error);
    if (parsed == NULL)
        return FALSE;
    *name = g_strdup (argv[0]);
    *params = parsed;
    return TRUE;
}

LrgCommandCall *
lrg_command_registry_invoke_line (LrgCommandRegistry *self,
                                  const gchar        *line,
                                  const gchar        *origin)
{
    g_autofree gchar *name = NULL;
    g_autoptr(JsonObject) params = NULL;
    GError *error = NULL;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), NULL);
    g_return_val_if_fail (line != NULL, NULL);

    if (!lrg_command_registry_parse_line (self, line, &name, &params, &error))
        return failed_call (NULL, origin, error);
    return lrg_command_registry_invoke (self, name, params, origin);
}

JsonNode *
lrg_command_registry_call_sync (LrgCommandRegistry  *self,
                                const gchar         *name,
                                JsonObject          *params,
                                GError             **error)
{
    g_autoptr(LrgCommandCall) call = NULL;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    call = lrg_command_registry_invoke (self, name, params, "sync");
    if (!lrg_command_call_is_done (call))
    {
        /* nobody will wait for it: stop it, report it */
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_PENDING,
                     "%s did not complete at once", name);
        lrg_command_call_cancel (call);
        return NULL;
    }
    return lrg_command_call_propagate (call, error);
}

void
lrg_command_registry_add_topic (LrgCommandRegistry *self,
                                const gchar        *topic,
                                const gchar        *description)
{
    g_return_if_fail (LRG_IS_COMMAND_REGISTRY (self));
    g_return_if_fail (topic != NULL && topic[0] != '\0');

    g_hash_table_replace (self->topics, g_strdup (topic), g_strdup (description));
}

/* sorts an array of strings in place */
static int
compare_strings (const void *a,
                 const void *b)
{
    return strcmp (*(const gchar *const *)a, *(const gchar *const *)b);
}

gchar **
lrg_command_registry_list_topics (LrgCommandRegistry *self)
{
    gchar **out;
    GHashTableIter iter;
    gpointer key;
    guint n;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), NULL);

    out = g_new0 (gchar *, g_hash_table_size (self->topics) + 1);
    n = 0;
    g_hash_table_iter_init (&iter, self->topics);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        out[n++] = g_strdup (key);
    qsort (out, n, sizeof (gchar *), compare_strings);
    return out;
}

const gchar *
lrg_command_registry_get_topic_description (LrgCommandRegistry *self,
                                            const gchar        *topic)
{
    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), NULL);
    g_return_val_if_fail (topic != NULL, NULL);

    return g_hash_table_lookup (self->topics, topic);
}

guint64
lrg_command_registry_publish (LrgCommandRegistry *self,
                              const gchar        *topic,
                              JsonNode           *data)
{
    guint64 seq;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), 0);
    g_return_val_if_fail (topic != NULL && topic[0] != '\0', 0);

    seq = ++self->event_seq;
    g_signal_emit (self, signals[SIGNAL_EVENT], 0, topic, data, seq);
    return seq;
}

guint64
lrg_command_registry_get_event_seq (LrgCommandRegistry *self)
{
    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (self), 0);

    return self->event_seq;
}

gboolean
lrg_command_topic_matches (const gchar *pattern,
                           const gchar *topic)
{
    g_return_val_if_fail (pattern != NULL, FALSE);
    g_return_val_if_fail (topic != NULL, FALSE);

    if (strcmp (pattern, "*") == 0)
        return TRUE;
    if (strchr (pattern, '*') == NULL && strchr (pattern, '?') == NULL)
        return strcmp (pattern, topic) == 0;
    return g_pattern_match_simple (pattern, topic);
}
