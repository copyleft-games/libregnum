/* lrg-dialog-runner.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "dialog/lrg-dialog-runner.h"
#include "lrg-enums.h"

/**
 * LrgDialogRunner:
 *
 * Manages the flow of a dialog conversation.
 */
struct _LrgDialogRunner
{
    GObject        parent_instance;

    LrgDialogTree *tree;
    LrgDialogNode *current_node;
    GHashTable    *context;      /* string -> string */
    gboolean       active;
};

G_DEFINE_TYPE (LrgDialogRunner, lrg_dialog_runner, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_TREE,
    PROP_ACTIVE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_NODE_ENTERED,
    SIGNAL_RESPONSE_SELECTED,
    SIGNAL_DIALOG_ENDED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_dialog_runner_dispose (GObject *object)
{
    LrgDialogRunner *self = LRG_DIALOG_RUNNER (object);

    g_clear_object (&self->tree);
    self->current_node = NULL;

    G_OBJECT_CLASS (lrg_dialog_runner_parent_class)->dispose (object);
}

static void
lrg_dialog_runner_finalize (GObject *object)
{
    LrgDialogRunner *self = LRG_DIALOG_RUNNER (object);

    g_clear_pointer (&self->context, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_dialog_runner_parent_class)->finalize (object);
}

static void
lrg_dialog_runner_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgDialogRunner *self = LRG_DIALOG_RUNNER (object);

    switch (prop_id)
    {
    case PROP_TREE:
        g_value_set_object (value, self->tree);
        break;
    case PROP_ACTIVE:
        g_value_set_boolean (value, self->active);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_dialog_runner_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgDialogRunner *self = LRG_DIALOG_RUNNER (object);

    switch (prop_id)
    {
    case PROP_TREE:
        lrg_dialog_runner_set_tree (self, g_value_get_object (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_dialog_runner_class_init (LrgDialogRunnerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_dialog_runner_dispose;
    object_class->finalize = lrg_dialog_runner_finalize;
    object_class->get_property = lrg_dialog_runner_get_property;
    object_class->set_property = lrg_dialog_runner_set_property;

    /**
     * LrgDialogRunner:tree:
     *
     * The dialog tree being run.
     */
    properties[PROP_TREE] =
        g_param_spec_object ("tree",
                             "Tree",
                             "Dialog tree",
                             LRG_TYPE_DIALOG_TREE,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDialogRunner:active:
     *
     * Whether a dialog is currently active.
     */
    properties[PROP_ACTIVE] =
        g_param_spec_boolean ("active",
                              "Active",
                              "Whether dialog is active",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgDialogRunner::node-entered:
     * @self: the runner
     * @node: the node that was entered
     *
     * Emitted when a dialog node is entered.
     */
    signals[SIGNAL_NODE_ENTERED] =
        g_signal_new ("node-entered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_DIALOG_NODE);

    /**
     * LrgDialogRunner::response-selected:
     * @self: the runner
     * @response: the response that was selected
     *
     * Emitted when a response is selected.
     */
    signals[SIGNAL_RESPONSE_SELECTED] =
        g_signal_new ("response-selected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_DIALOG_RESPONSE);

    /**
     * LrgDialogRunner::dialog-ended:
     * @self: the runner
     *
     * Emitted when the dialog ends.
     */
    signals[SIGNAL_DIALOG_ENDED] =
        g_signal_new ("dialog-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_dialog_runner_init (LrgDialogRunner *self)
{
    self->context = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_free);
    self->active = FALSE;
}

/**
 * lrg_dialog_runner_new:
 *
 * Creates a new dialog runner.
 *
 * Returns: (transfer full): A new #LrgDialogRunner
 */
LrgDialogRunner *
lrg_dialog_runner_new (void)
{
    return g_object_new (LRG_TYPE_DIALOG_RUNNER, NULL);
}

/**
 * lrg_dialog_runner_get_tree:
 * @self: an #LrgDialogRunner
 *
 * Gets the current dialog tree.
 *
 * Returns: (transfer none) (nullable): The current tree
 */
LrgDialogTree *
lrg_dialog_runner_get_tree (LrgDialogRunner *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), NULL);
    return self->tree;
}

/**
 * lrg_dialog_runner_set_tree:
 * @self: an #LrgDialogRunner
 * @tree: (nullable): dialog tree to set
 *
 * Sets the dialog tree for this runner.
 */
void
lrg_dialog_runner_set_tree (LrgDialogRunner *self,
                            LrgDialogTree   *tree)
{
    g_return_if_fail (LRG_IS_DIALOG_RUNNER (self));

    if (self->active)
        lrg_dialog_runner_stop (self);

    g_set_object (&self->tree, tree);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TREE]);
}

/**
 * lrg_dialog_runner_get_current_node:
 * @self: an #LrgDialogRunner
 *
 * Gets the current dialog node.
 *
 * Returns: (transfer none) (nullable): The current node
 */
LrgDialogNode *
lrg_dialog_runner_get_current_node (LrgDialogRunner *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), NULL);
    return self->current_node;
}

/*
 * Enter a node: apply effects and emit signal.
 */
static void
enter_node (LrgDialogRunner *self,
            LrgDialogNode   *node)
{
    self->current_node = node;

    if (node != NULL)
    {
        /* Apply node effects */
        lrg_dialog_node_apply_effects (node, self->context);

        g_signal_emit (self, signals[SIGNAL_NODE_ENTERED], 0, node);

        /* Check if terminal */
        if (lrg_dialog_node_is_terminal (node))
        {
            self->active = FALSE;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
            g_signal_emit (self, signals[SIGNAL_DIALOG_ENDED], 0);
        }
    }
}

/**
 * lrg_dialog_runner_start:
 * @self: an #LrgDialogRunner
 * @error: (nullable): return location for error
 *
 * Starts the dialog from the tree's start node.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_dialog_runner_start (LrgDialogRunner  *self,
                         GError          **error)
{
    LrgDialogNode *start;

    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), FALSE);

    if (self->tree == NULL)
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_NO_TREE,
                     "No dialog tree set");
        return FALSE;
    }

    start = lrg_dialog_tree_get_start_node (self->tree);
    if (start == NULL)
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_INVALID_NODE,
                     "Dialog tree has no start node");
        return FALSE;
    }

    self->active = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);

    enter_node (self, start);
    return TRUE;
}

/**
 * lrg_dialog_runner_start_at:
 * @self: an #LrgDialogRunner
 * @node_id: node ID to start at
 * @error: (nullable): return location for error
 *
 * Starts the dialog at a specific node.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_dialog_runner_start_at (LrgDialogRunner  *self,
                            const gchar      *node_id,
                            GError          **error)
{
    LrgDialogNode *node;

    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), FALSE);
    g_return_val_if_fail (node_id != NULL, FALSE);

    if (self->tree == NULL)
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_NO_TREE,
                     "No dialog tree set");
        return FALSE;
    }

    node = lrg_dialog_tree_get_node (self->tree, node_id);
    if (node == NULL)
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_INVALID_NODE,
                     "Node '%s' not found in tree",
                     node_id);
        return FALSE;
    }

    self->active = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);

    enter_node (self, node);
    return TRUE;
}

/**
 * lrg_dialog_runner_advance:
 * @self: an #LrgDialogRunner
 * @error: (nullable): return location for error
 *
 * Advances to the next node if auto-advance is set.
 *
 * Returns: %TRUE if advanced, %FALSE if at a choice or terminal node
 */
gboolean
lrg_dialog_runner_advance (LrgDialogRunner  *self,
                           GError          **error)
{
    const gchar   *next_id;
    LrgDialogNode *next;

    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), FALSE);

    if (!self->active || self->current_node == NULL)
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_FAILED,
                     "No active dialog");
        return FALSE;
    }

    /* Check if at choice (has responses) */
    if (lrg_dialog_node_get_response_count (self->current_node) > 0)
        return FALSE;

    /* Get next node ID */
    next_id = lrg_dialog_node_get_next_node_id (self->current_node);
    if (next_id == NULL)
    {
        /* Terminal node */
        return FALSE;
    }

    next = lrg_dialog_tree_get_node (self->tree, next_id);
    if (next == NULL)
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_INVALID_NODE,
                     "Next node '%s' not found",
                     next_id);
        return FALSE;
    }

    enter_node (self, next);
    return TRUE;
}

/**
 * lrg_dialog_runner_select_response:
 * @self: an #LrgDialogRunner
 * @index: response index
 * @error: (nullable): return location for error
 *
 * Selects a response by index and advances to its target node.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_dialog_runner_select_response (LrgDialogRunner  *self,
                                   guint             index,
                                   GError          **error)
{
    LrgDialogResponse *response;
    const gchar       *next_id;
    LrgDialogNode     *next;

    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), FALSE);

    if (!self->active || self->current_node == NULL)
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_FAILED,
                     "No active dialog");
        return FALSE;
    }

    response = lrg_dialog_node_get_response (self->current_node, index);
    if (response == NULL)
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_INVALID_NODE,
                     "Response index %u out of bounds",
                     index);
        return FALSE;
    }

    /* Emit response selected signal */
    g_signal_emit (self, signals[SIGNAL_RESPONSE_SELECTED], 0, response);

    /* Get next node */
    next_id = lrg_dialog_response_get_next_node_id (response);
    if (next_id == NULL)
    {
        /* Response leads to end of dialog */
        self->current_node = NULL;
        self->active = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
        g_signal_emit (self, signals[SIGNAL_DIALOG_ENDED], 0);
        return TRUE;
    }

    next = lrg_dialog_tree_get_node (self->tree, next_id);
    if (next == NULL)
    {
        g_set_error (error,
                     LRG_DIALOG_ERROR,
                     LRG_DIALOG_ERROR_INVALID_NODE,
                     "Response target node '%s' not found",
                     next_id);
        return FALSE;
    }

    enter_node (self, next);
    return TRUE;
}

/**
 * lrg_dialog_runner_is_active:
 * @self: an #LrgDialogRunner
 *
 * Checks if a dialog is currently active.
 *
 * Returns: %TRUE if active
 */
gboolean
lrg_dialog_runner_is_active (LrgDialogRunner *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), FALSE);
    return self->active;
}

/**
 * lrg_dialog_runner_is_at_choice:
 * @self: an #LrgDialogRunner
 *
 * Checks if at a node that requires a response selection.
 *
 * Returns: %TRUE if at a choice node
 */
gboolean
lrg_dialog_runner_is_at_choice (LrgDialogRunner *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), FALSE);

    if (!self->active || self->current_node == NULL)
        return FALSE;

    return (lrg_dialog_node_get_response_count (self->current_node) > 0);
}

/**
 * lrg_dialog_runner_stop:
 * @self: an #LrgDialogRunner
 *
 * Stops the current dialog.
 */
void
lrg_dialog_runner_stop (LrgDialogRunner *self)
{
    g_return_if_fail (LRG_IS_DIALOG_RUNNER (self));

    if (self->active)
    {
        self->current_node = NULL;
        self->active = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
        g_signal_emit (self, signals[SIGNAL_DIALOG_ENDED], 0);
    }
}

/**
 * lrg_dialog_runner_get_available_responses:
 * @self: an #LrgDialogRunner
 *
 * Gets available responses for the current node.
 *
 * Returns: (transfer container) (element-type LrgDialogResponse): Available responses
 */
GPtrArray *
lrg_dialog_runner_get_available_responses (LrgDialogRunner *self)
{
    GPtrArray *available;
    GPtrArray *all;
    guint      i;

    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), NULL);

    available = g_ptr_array_new ();

    if (!self->active || self->current_node == NULL)
        return available;

    all = lrg_dialog_node_get_responses (self->current_node);
    for (i = 0; i < all->len; i++)
    {
        LrgDialogResponse *resp = g_ptr_array_index (all, i);
        GPtrArray         *conditions;

        /*
         * Check conditions. For now, if there are no conditions,
         * or if we have a context, assume conditions pass.
         * Real implementation would parse condition expressions.
         */
        conditions = lrg_dialog_response_get_conditions (resp);
        if (conditions->len == 0 || g_hash_table_size (self->context) > 0)
        {
            g_ptr_array_add (available, resp);
        }
    }

    return available;
}

/**
 * lrg_dialog_runner_get_context:
 * @self: an #LrgDialogRunner
 *
 * Gets the variable context.
 *
 * Returns: (transfer none): The context hash table
 */
GHashTable *
lrg_dialog_runner_get_context (LrgDialogRunner *self)
{
    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), NULL);
    return self->context;
}

/**
 * lrg_dialog_runner_set_variable:
 * @self: an #LrgDialogRunner
 * @key: variable name
 * @value: variable value
 *
 * Sets a variable in the context.
 */
void
lrg_dialog_runner_set_variable (LrgDialogRunner *self,
                                const gchar     *key,
                                const gchar     *value)
{
    g_return_if_fail (LRG_IS_DIALOG_RUNNER (self));
    g_return_if_fail (key != NULL);

    g_hash_table_replace (self->context, g_strdup (key), g_strdup (value));
}

/**
 * lrg_dialog_runner_get_variable:
 * @self: an #LrgDialogRunner
 * @key: variable name
 *
 * Gets a variable from the context.
 *
 * Returns: (transfer none) (nullable): Variable value, or %NULL
 */
const gchar *
lrg_dialog_runner_get_variable (LrgDialogRunner *self,
                                const gchar     *key)
{
    g_return_val_if_fail (LRG_IS_DIALOG_RUNNER (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return g_hash_table_lookup (self->context, key);
}
