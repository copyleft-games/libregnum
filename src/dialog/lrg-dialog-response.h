/* lrg-dialog-response.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dialog response structure for branching conversations.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define LRG_TYPE_DIALOG_RESPONSE (lrg_dialog_response_get_type ())

/**
 * LrgDialogResponse:
 *
 * A response option in a dialog node.
 *
 * Dialog responses represent player choices in a conversation.
 * Each response has display text and points to a next node.
 * Responses can have conditions that must be met to appear
 * and effects that trigger when selected.
 */
typedef struct _LrgDialogResponse LrgDialogResponse;

#pragma GCC visibility push(default)

GType              lrg_dialog_response_get_type       (void) G_GNUC_CONST;

/**
 * lrg_dialog_response_new:
 * @id: unique identifier for the response
 * @text: display text shown to player
 * @next_node_id: (nullable): ID of the node to transition to
 *
 * Creates a new dialog response.
 *
 * Returns: (transfer full): A new #LrgDialogResponse
 */
LrgDialogResponse *lrg_dialog_response_new            (const gchar       *id,
                                                       const gchar       *text,
                                                       const gchar       *next_node_id);

/**
 * lrg_dialog_response_copy:
 * @self: an #LrgDialogResponse
 *
 * Creates a deep copy of the response.
 *
 * Returns: (transfer full): A copy of @self
 */
LrgDialogResponse *lrg_dialog_response_copy           (const LrgDialogResponse *self);

/**
 * lrg_dialog_response_free:
 * @self: an #LrgDialogResponse
 *
 * Frees the response and all associated data.
 */
void               lrg_dialog_response_free           (LrgDialogResponse *self);

/**
 * lrg_dialog_response_get_id:
 * @self: an #LrgDialogResponse
 *
 * Gets the response identifier.
 *
 * Returns: (transfer none): The response ID
 */
const gchar       *lrg_dialog_response_get_id         (const LrgDialogResponse *self);

/**
 * lrg_dialog_response_get_text:
 * @self: an #LrgDialogResponse
 *
 * Gets the display text.
 *
 * Returns: (transfer none): The response text
 */
const gchar       *lrg_dialog_response_get_text       (const LrgDialogResponse *self);

/**
 * lrg_dialog_response_set_text:
 * @self: an #LrgDialogResponse
 * @text: new display text
 *
 * Sets the display text.
 */
void               lrg_dialog_response_set_text       (LrgDialogResponse *self,
                                                       const gchar       *text);

/**
 * lrg_dialog_response_get_next_node_id:
 * @self: an #LrgDialogResponse
 *
 * Gets the next node ID to transition to.
 *
 * Returns: (transfer none) (nullable): The next node ID, or %NULL
 */
const gchar       *lrg_dialog_response_get_next_node_id (const LrgDialogResponse *self);

/**
 * lrg_dialog_response_set_next_node_id:
 * @self: an #LrgDialogResponse
 * @next_node_id: (nullable): next node ID
 *
 * Sets the next node ID.
 */
void               lrg_dialog_response_set_next_node_id (LrgDialogResponse *self,
                                                         const gchar       *next_node_id);

/**
 * lrg_dialog_response_add_condition:
 * @self: an #LrgDialogResponse
 * @condition: condition expression string
 *
 * Adds a condition that must be true for this response to appear.
 */
void               lrg_dialog_response_add_condition  (LrgDialogResponse *self,
                                                       const gchar       *condition);

/**
 * lrg_dialog_response_get_conditions:
 * @self: an #LrgDialogResponse
 *
 * Gets all conditions for this response.
 *
 * Returns: (transfer none) (element-type utf8): Array of condition strings
 */
GPtrArray         *lrg_dialog_response_get_conditions (const LrgDialogResponse *self);

/**
 * lrg_dialog_response_add_effect:
 * @self: an #LrgDialogResponse
 * @effect: effect expression string
 *
 * Adds an effect that triggers when this response is selected.
 */
void               lrg_dialog_response_add_effect     (LrgDialogResponse *self,
                                                       const gchar       *effect);

/**
 * lrg_dialog_response_get_effects:
 * @self: an #LrgDialogResponse
 *
 * Gets all effects for this response.
 *
 * Returns: (transfer none) (element-type utf8): Array of effect strings
 */
GPtrArray         *lrg_dialog_response_get_effects    (const LrgDialogResponse *self);

#pragma GCC visibility pop

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgDialogResponse, lrg_dialog_response_free)

G_END_DECLS
