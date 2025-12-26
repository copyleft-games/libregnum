/* lrg-dialog-node.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "dialog/lrg-dialog-node.h"

/**
 * LrgDialogNodePrivate:
 *
 * Private data for dialog nodes.
 */
typedef struct
{
    gchar     *id;
    gchar     *speaker;
    gchar     *text;
    gchar     *next_node_id;
    GPtrArray *responses;
    GPtrArray *conditions;
    GPtrArray *effects;
} LrgDialogNodePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgDialogNode, lrg_dialog_node, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_SPEAKER,
    PROP_TEXT,
    PROP_NEXT_NODE_ID,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Forward declarations */
static const gchar *lrg_dialog_node_real_get_display_text    (LrgDialogNode *self);
static gboolean     lrg_dialog_node_real_evaluate_conditions (LrgDialogNode *self,
                                                              GHashTable    *context);
static void         lrg_dialog_node_real_apply_effects       (LrgDialogNode *self,
                                                              GHashTable    *context);

static void
lrg_dialog_node_finalize (GObject *object)
{
    LrgDialogNode        *self = LRG_DIALOG_NODE (object);
    LrgDialogNodePrivate *priv = lrg_dialog_node_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->speaker, g_free);
    g_clear_pointer (&priv->text, g_free);
    g_clear_pointer (&priv->next_node_id, g_free);
    g_clear_pointer (&priv->responses, g_ptr_array_unref);
    g_clear_pointer (&priv->conditions, g_ptr_array_unref);
    g_clear_pointer (&priv->effects, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_dialog_node_parent_class)->finalize (object);
}

static void
lrg_dialog_node_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgDialogNode        *self = LRG_DIALOG_NODE (object);
    LrgDialogNodePrivate *priv = lrg_dialog_node_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;
    case PROP_SPEAKER:
        g_value_set_string (value, priv->speaker);
        break;
    case PROP_TEXT:
        g_value_set_string (value, priv->text);
        break;
    case PROP_NEXT_NODE_ID:
        g_value_set_string (value, priv->next_node_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_dialog_node_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgDialogNode        *self = LRG_DIALOG_NODE (object);
    LrgDialogNodePrivate *priv = lrg_dialog_node_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_SPEAKER:
        g_free (priv->speaker);
        priv->speaker = g_value_dup_string (value);
        break;
    case PROP_TEXT:
        g_free (priv->text);
        priv->text = g_value_dup_string (value);
        break;
    case PROP_NEXT_NODE_ID:
        g_free (priv->next_node_id);
        priv->next_node_id = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_dialog_node_class_init (LrgDialogNodeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_dialog_node_finalize;
    object_class->get_property = lrg_dialog_node_get_property;
    object_class->set_property = lrg_dialog_node_set_property;

    /* Virtual methods */
    klass->get_display_text = lrg_dialog_node_real_get_display_text;
    klass->evaluate_conditions = lrg_dialog_node_real_evaluate_conditions;
    klass->apply_effects = lrg_dialog_node_real_apply_effects;

    /**
     * LrgDialogNode:id:
     *
     * Unique identifier for this node within the dialog tree.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Node identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDialogNode:speaker:
     *
     * Name of the character speaking this line.
     */
    properties[PROP_SPEAKER] =
        g_param_spec_string ("speaker",
                             "Speaker",
                             "Character speaking",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDialogNode:text:
     *
     * The dialog text content.
     */
    properties[PROP_TEXT] =
        g_param_spec_string ("text",
                             "Text",
                             "Dialog text",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDialogNode:next-node-id:
     *
     * ID of the next node for auto-advance.
     */
    properties[PROP_NEXT_NODE_ID] =
        g_param_spec_string ("next-node-id",
                             "Next Node ID",
                             "Next node for auto-advance",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
response_free_wrapper (gpointer data)
{
    lrg_dialog_response_free ((LrgDialogResponse *)data);
}

static void
lrg_dialog_node_init (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv = lrg_dialog_node_get_instance_private (self);

    priv->responses = g_ptr_array_new_with_free_func (response_free_wrapper);
    priv->conditions = g_ptr_array_new_with_free_func (g_free);
    priv->effects = g_ptr_array_new_with_free_func (g_free);
}

/*
 * Default implementation of get_display_text.
 * Returns the raw text. Subclasses can override for localization.
 */
static const gchar *
lrg_dialog_node_real_get_display_text (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv = lrg_dialog_node_get_instance_private (self);
    return priv->text;
}

/*
 * Default implementation of evaluate_conditions.
 * Always returns TRUE if no conditions, otherwise assumes true.
 * Subclasses should override for actual condition evaluation.
 */
static gboolean
lrg_dialog_node_real_evaluate_conditions (LrgDialogNode *self,
                                          GHashTable    *context)
{
    LrgDialogNodePrivate *priv = lrg_dialog_node_get_instance_private (self);

    /* No conditions means always pass */
    if (priv->conditions->len == 0)
        return TRUE;

    /*
     * Default behavior: if context is NULL, fail any conditions.
     * Real evaluation would parse condition strings.
     */
    return (context != NULL);
}

/*
 * Default implementation of apply_effects.
 * Does nothing. Subclasses should override for actual effect application.
 */
static void
lrg_dialog_node_real_apply_effects (LrgDialogNode *self,
                                    GHashTable    *context)
{
    /* Default: no-op, subclasses implement effect logic */
    (void)self;
    (void)context;
}

/**
 * lrg_dialog_node_new:
 * @id: unique identifier for the node
 *
 * Creates a new dialog node.
 *
 * Returns: (transfer full): A new #LrgDialogNode
 */
LrgDialogNode *
lrg_dialog_node_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_DIALOG_NODE,
                         "id", id,
                         NULL);
}

/**
 * lrg_dialog_node_get_id:
 * @self: an #LrgDialogNode
 *
 * Gets the node identifier.
 *
 * Returns: (transfer none): The node ID
 */
const gchar *
lrg_dialog_node_get_id (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    return priv->id;
}

/**
 * lrg_dialog_node_get_speaker:
 * @self: an #LrgDialogNode
 *
 * Gets the speaker for this node.
 *
 * Returns: (transfer none) (nullable): The speaker name
 */
const gchar *
lrg_dialog_node_get_speaker (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    return priv->speaker;
}

/**
 * lrg_dialog_node_set_speaker:
 * @self: an #LrgDialogNode
 * @speaker: (nullable): speaker name
 *
 * Sets the speaker for this node.
 */
void
lrg_dialog_node_set_speaker (LrgDialogNode *self,
                             const gchar   *speaker)
{
    g_return_if_fail (LRG_IS_DIALOG_NODE (self));
    g_object_set (self, "speaker", speaker, NULL);
}

/**
 * lrg_dialog_node_get_text:
 * @self: an #LrgDialogNode
 *
 * Gets the raw text content.
 *
 * Returns: (transfer none) (nullable): The node text
 */
const gchar *
lrg_dialog_node_get_text (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    return priv->text;
}

/**
 * lrg_dialog_node_set_text:
 * @self: an #LrgDialogNode
 * @text: (nullable): text content
 *
 * Sets the text content.
 */
void
lrg_dialog_node_set_text (LrgDialogNode *self,
                          const gchar   *text)
{
    g_return_if_fail (LRG_IS_DIALOG_NODE (self));
    g_object_set (self, "text", text, NULL);
}

/**
 * lrg_dialog_node_get_display_text:
 * @self: an #LrgDialogNode
 *
 * Gets the display text, potentially localized.
 *
 * Returns: (transfer none) (nullable): The display text
 */
const gchar *
lrg_dialog_node_get_display_text (LrgDialogNode *self)
{
    LrgDialogNodeClass *klass;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), NULL);

    klass = LRG_DIALOG_NODE_GET_CLASS (self);
    if (klass->get_display_text)
        return klass->get_display_text (self);

    return NULL;
}

/**
 * lrg_dialog_node_get_next_node_id:
 * @self: an #LrgDialogNode
 *
 * Gets the default next node ID for auto-advance.
 *
 * Returns: (transfer none) (nullable): The next node ID
 */
const gchar *
lrg_dialog_node_get_next_node_id (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    return priv->next_node_id;
}

/**
 * lrg_dialog_node_set_next_node_id:
 * @self: an #LrgDialogNode
 * @next_node_id: (nullable): next node ID
 *
 * Sets the default next node ID.
 */
void
lrg_dialog_node_set_next_node_id (LrgDialogNode *self,
                                  const gchar   *next_node_id)
{
    g_return_if_fail (LRG_IS_DIALOG_NODE (self));
    g_object_set (self, "next-node-id", next_node_id, NULL);
}

/**
 * lrg_dialog_node_add_response:
 * @self: an #LrgDialogNode
 * @response: (transfer full): response to add
 *
 * Adds a response option to this node.
 */
void
lrg_dialog_node_add_response (LrgDialogNode     *self,
                              LrgDialogResponse *response)
{
    LrgDialogNodePrivate *priv;

    g_return_if_fail (LRG_IS_DIALOG_NODE (self));
    g_return_if_fail (response != NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    g_ptr_array_add (priv->responses, response);
}

/**
 * lrg_dialog_node_get_responses:
 * @self: an #LrgDialogNode
 *
 * Gets all responses for this node.
 *
 * Returns: (transfer none) (element-type LrgDialogResponse): Array of responses
 */
GPtrArray *
lrg_dialog_node_get_responses (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    return priv->responses;
}

/**
 * lrg_dialog_node_get_response_count:
 * @self: an #LrgDialogNode
 *
 * Gets the number of responses.
 *
 * Returns: Response count
 */
guint
lrg_dialog_node_get_response_count (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), 0);

    priv = lrg_dialog_node_get_instance_private (self);
    return priv->responses->len;
}

/**
 * lrg_dialog_node_get_response:
 * @self: an #LrgDialogNode
 * @index: response index
 *
 * Gets a response by index.
 *
 * Returns: (transfer none) (nullable): The response, or %NULL if out of bounds
 */
LrgDialogResponse *
lrg_dialog_node_get_response (LrgDialogNode *self,
                              guint          index)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), NULL);

    priv = lrg_dialog_node_get_instance_private (self);

    if (index >= priv->responses->len)
        return NULL;

    return g_ptr_array_index (priv->responses, index);
}

/**
 * lrg_dialog_node_add_condition:
 * @self: an #LrgDialogNode
 * @condition: condition expression
 *
 * Adds a condition that must be met to show this node.
 */
void
lrg_dialog_node_add_condition (LrgDialogNode *self,
                               const gchar   *condition)
{
    LrgDialogNodePrivate *priv;

    g_return_if_fail (LRG_IS_DIALOG_NODE (self));
    g_return_if_fail (condition != NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    g_ptr_array_add (priv->conditions, g_strdup (condition));
}

/**
 * lrg_dialog_node_get_conditions:
 * @self: an #LrgDialogNode
 *
 * Gets all conditions.
 *
 * Returns: (transfer none) (element-type utf8): Array of conditions
 */
GPtrArray *
lrg_dialog_node_get_conditions (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    return priv->conditions;
}

/**
 * lrg_dialog_node_evaluate_conditions:
 * @self: an #LrgDialogNode
 * @context: (nullable): variable context for evaluation
 *
 * Evaluates whether all conditions are met.
 *
 * Returns: %TRUE if all conditions pass
 */
gboolean
lrg_dialog_node_evaluate_conditions (LrgDialogNode *self,
                                     GHashTable    *context)
{
    LrgDialogNodeClass *klass;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), FALSE);

    klass = LRG_DIALOG_NODE_GET_CLASS (self);
    if (klass->evaluate_conditions)
        return klass->evaluate_conditions (self, context);

    return TRUE;
}

/**
 * lrg_dialog_node_add_effect:
 * @self: an #LrgDialogNode
 * @effect: effect expression
 *
 * Adds an effect to trigger when entering this node.
 */
void
lrg_dialog_node_add_effect (LrgDialogNode *self,
                            const gchar   *effect)
{
    LrgDialogNodePrivate *priv;

    g_return_if_fail (LRG_IS_DIALOG_NODE (self));
    g_return_if_fail (effect != NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    g_ptr_array_add (priv->effects, g_strdup (effect));
}

/**
 * lrg_dialog_node_get_effects:
 * @self: an #LrgDialogNode
 *
 * Gets all effects.
 *
 * Returns: (transfer none) (element-type utf8): Array of effects
 */
GPtrArray *
lrg_dialog_node_get_effects (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), NULL);

    priv = lrg_dialog_node_get_instance_private (self);
    return priv->effects;
}

/**
 * lrg_dialog_node_apply_effects:
 * @self: an #LrgDialogNode
 * @context: (nullable): variable context for effects
 *
 * Applies all effects to the context.
 */
void
lrg_dialog_node_apply_effects (LrgDialogNode *self,
                               GHashTable    *context)
{
    LrgDialogNodeClass *klass;

    g_return_if_fail (LRG_IS_DIALOG_NODE (self));

    klass = LRG_DIALOG_NODE_GET_CLASS (self);
    if (klass->apply_effects)
        klass->apply_effects (self, context);
}

/**
 * lrg_dialog_node_is_terminal:
 * @self: an #LrgDialogNode
 *
 * Checks if this node ends the dialog.
 *
 * Returns: %TRUE if this is a terminal node
 */
gboolean
lrg_dialog_node_is_terminal (LrgDialogNode *self)
{
    LrgDialogNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_DIALOG_NODE (self), TRUE);

    priv = lrg_dialog_node_get_instance_private (self);

    /* Terminal if no next node and no responses */
    return (priv->next_node_id == NULL && priv->responses->len == 0);
}
