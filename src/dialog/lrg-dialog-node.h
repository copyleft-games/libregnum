/* lrg-dialog-node.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dialog node representing a single point in a conversation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "dialog/lrg-dialog-response.h"

G_BEGIN_DECLS

#define LRG_TYPE_DIALOG_NODE (lrg_dialog_node_get_type ())

#pragma GCC visibility push(default)

G_DECLARE_DERIVABLE_TYPE (LrgDialogNode, lrg_dialog_node, LRG, DIALOG_NODE, GObject)

/**
 * LrgDialogNodeClass:
 * @parent_class: Parent class
 * @get_display_text: Virtual method to get the display text (for localization)
 * @evaluate_conditions: Virtual method to check if node conditions are met
 * @apply_effects: Virtual method to apply node effects
 *
 * Class structure for #LrgDialogNode.
 */
struct _LrgDialogNodeClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    const gchar *(*get_display_text)    (LrgDialogNode *self);
    gboolean     (*evaluate_conditions) (LrgDialogNode *self,
                                         GHashTable    *context);
    void         (*apply_effects)       (LrgDialogNode *self,
                                         GHashTable    *context);

    /* Padding for ABI stability */
    gpointer _reserved[8];
};

/**
 * lrg_dialog_node_new:
 * @id: unique identifier for the node
 *
 * Creates a new dialog node.
 *
 * Returns: (transfer full): A new #LrgDialogNode
 */
LrgDialogNode     *lrg_dialog_node_new                  (const gchar   *id);

/**
 * lrg_dialog_node_get_id:
 * @self: an #LrgDialogNode
 *
 * Gets the node identifier.
 *
 * Returns: (transfer none): The node ID
 */
const gchar       *lrg_dialog_node_get_id               (LrgDialogNode *self);

/**
 * lrg_dialog_node_get_speaker:
 * @self: an #LrgDialogNode
 *
 * Gets the speaker for this node.
 *
 * Returns: (transfer none) (nullable): The speaker name
 */
const gchar       *lrg_dialog_node_get_speaker          (LrgDialogNode *self);

/**
 * lrg_dialog_node_set_speaker:
 * @self: an #LrgDialogNode
 * @speaker: (nullable): speaker name
 *
 * Sets the speaker for this node.
 */
void               lrg_dialog_node_set_speaker          (LrgDialogNode *self,
                                                         const gchar   *speaker);

/**
 * lrg_dialog_node_get_text:
 * @self: an #LrgDialogNode
 *
 * Gets the raw text content.
 *
 * Returns: (transfer none) (nullable): The node text
 */
const gchar       *lrg_dialog_node_get_text             (LrgDialogNode *self);

/**
 * lrg_dialog_node_set_text:
 * @self: an #LrgDialogNode
 * @text: (nullable): text content
 *
 * Sets the text content.
 */
void               lrg_dialog_node_set_text             (LrgDialogNode *self,
                                                         const gchar   *text);

/**
 * lrg_dialog_node_get_display_text:
 * @self: an #LrgDialogNode
 *
 * Gets the display text, potentially localized.
 *
 * Subclasses can override this to provide localization.
 *
 * Returns: (transfer none) (nullable): The display text
 */
const gchar       *lrg_dialog_node_get_display_text     (LrgDialogNode *self);

/**
 * lrg_dialog_node_get_next_node_id:
 * @self: an #LrgDialogNode
 *
 * Gets the default next node ID for auto-advance.
 *
 * Returns: (transfer none) (nullable): The next node ID
 */
const gchar       *lrg_dialog_node_get_next_node_id     (LrgDialogNode *self);

/**
 * lrg_dialog_node_set_next_node_id:
 * @self: an #LrgDialogNode
 * @next_node_id: (nullable): next node ID
 *
 * Sets the default next node ID.
 */
void               lrg_dialog_node_set_next_node_id     (LrgDialogNode *self,
                                                         const gchar   *next_node_id);

/**
 * lrg_dialog_node_add_response:
 * @self: an #LrgDialogNode
 * @response: (transfer full): response to add
 *
 * Adds a response option to this node.
 */
void               lrg_dialog_node_add_response         (LrgDialogNode     *self,
                                                         LrgDialogResponse *response);

/**
 * lrg_dialog_node_get_responses:
 * @self: an #LrgDialogNode
 *
 * Gets all responses for this node.
 *
 * Returns: (transfer none) (element-type LrgDialogResponse): Array of responses
 */
GPtrArray         *lrg_dialog_node_get_responses        (LrgDialogNode *self);

/**
 * lrg_dialog_node_get_response_count:
 * @self: an #LrgDialogNode
 *
 * Gets the number of responses.
 *
 * Returns: Response count
 */
guint              lrg_dialog_node_get_response_count   (LrgDialogNode *self);

/**
 * lrg_dialog_node_get_response:
 * @self: an #LrgDialogNode
 * @index: response index
 *
 * Gets a response by index.
 *
 * Returns: (transfer none) (nullable): The response, or %NULL if out of bounds
 */
LrgDialogResponse *lrg_dialog_node_get_response         (LrgDialogNode *self,
                                                         guint          index);

/**
 * lrg_dialog_node_add_condition:
 * @self: an #LrgDialogNode
 * @condition: condition expression
 *
 * Adds a condition that must be met to show this node.
 */
void               lrg_dialog_node_add_condition        (LrgDialogNode *self,
                                                         const gchar   *condition);

/**
 * lrg_dialog_node_get_conditions:
 * @self: an #LrgDialogNode
 *
 * Gets all conditions.
 *
 * Returns: (transfer none) (element-type utf8): Array of conditions
 */
GPtrArray         *lrg_dialog_node_get_conditions       (LrgDialogNode *self);

/**
 * lrg_dialog_node_evaluate_conditions:
 * @self: an #LrgDialogNode
 * @context: (nullable): variable context for evaluation
 *
 * Evaluates whether all conditions are met.
 *
 * Returns: %TRUE if all conditions pass
 */
gboolean           lrg_dialog_node_evaluate_conditions  (LrgDialogNode *self,
                                                         GHashTable    *context);

/**
 * lrg_dialog_node_add_effect:
 * @self: an #LrgDialogNode
 * @effect: effect expression
 *
 * Adds an effect to trigger when entering this node.
 */
void               lrg_dialog_node_add_effect           (LrgDialogNode *self,
                                                         const gchar   *effect);

/**
 * lrg_dialog_node_get_effects:
 * @self: an #LrgDialogNode
 *
 * Gets all effects.
 *
 * Returns: (transfer none) (element-type utf8): Array of effects
 */
GPtrArray         *lrg_dialog_node_get_effects          (LrgDialogNode *self);

/**
 * lrg_dialog_node_apply_effects:
 * @self: an #LrgDialogNode
 * @context: (nullable): variable context for effects
 *
 * Applies all effects to the context.
 */
void               lrg_dialog_node_apply_effects        (LrgDialogNode *self,
                                                         GHashTable    *context);

/**
 * lrg_dialog_node_is_terminal:
 * @self: an #LrgDialogNode
 *
 * Checks if this node ends the dialog.
 *
 * A node is terminal if it has no next_node_id and no responses.
 *
 * Returns: %TRUE if this is a terminal node
 */
gboolean           lrg_dialog_node_is_terminal          (LrgDialogNode *self);

#pragma GCC visibility pop

G_END_DECLS
