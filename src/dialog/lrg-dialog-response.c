/* lrg-dialog-response.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "dialog/lrg-dialog-response.h"

/**
 * LrgDialogResponse:
 *
 * Internal structure for dialog responses.
 */
struct _LrgDialogResponse
{
    gchar     *id;
    gchar     *text;
    gchar     *next_node_id;
    GPtrArray *conditions;
    GPtrArray *effects;
};

G_DEFINE_BOXED_TYPE (LrgDialogResponse,
                     lrg_dialog_response,
                     lrg_dialog_response_copy,
                     lrg_dialog_response_free)

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
LrgDialogResponse *
lrg_dialog_response_new (const gchar *id,
                         const gchar *text,
                         const gchar *next_node_id)
{
    LrgDialogResponse *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (text != NULL, NULL);

    self = g_new0 (LrgDialogResponse, 1);
    self->id = g_strdup (id);
    self->text = g_strdup (text);
    self->next_node_id = g_strdup (next_node_id);
    self->conditions = g_ptr_array_new_with_free_func (g_free);
    self->effects = g_ptr_array_new_with_free_func (g_free);

    return self;
}

/**
 * lrg_dialog_response_copy:
 * @self: an #LrgDialogResponse
 *
 * Creates a deep copy of the response.
 *
 * Returns: (transfer full): A copy of @self
 */
LrgDialogResponse *
lrg_dialog_response_copy (const LrgDialogResponse *self)
{
    LrgDialogResponse *copy;
    guint              i;

    g_return_val_if_fail (self != NULL, NULL);

    copy = lrg_dialog_response_new (self->id, self->text, self->next_node_id);

    /* Copy conditions */
    for (i = 0; i < self->conditions->len; i++)
    {
        const gchar *condition = g_ptr_array_index (self->conditions, i);
        g_ptr_array_add (copy->conditions, g_strdup (condition));
    }

    /* Copy effects */
    for (i = 0; i < self->effects->len; i++)
    {
        const gchar *effect = g_ptr_array_index (self->effects, i);
        g_ptr_array_add (copy->effects, g_strdup (effect));
    }

    return copy;
}

/**
 * lrg_dialog_response_free:
 * @self: an #LrgDialogResponse
 *
 * Frees the response and all associated data.
 */
void
lrg_dialog_response_free (LrgDialogResponse *self)
{
    if (self == NULL)
        return;

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->text, g_free);
    g_clear_pointer (&self->next_node_id, g_free);
    g_clear_pointer (&self->conditions, g_ptr_array_unref);
    g_clear_pointer (&self->effects, g_ptr_array_unref);
    g_free (self);
}

/**
 * lrg_dialog_response_get_id:
 * @self: an #LrgDialogResponse
 *
 * Gets the response identifier.
 *
 * Returns: (transfer none): The response ID
 */
const gchar *
lrg_dialog_response_get_id (const LrgDialogResponse *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

/**
 * lrg_dialog_response_get_text:
 * @self: an #LrgDialogResponse
 *
 * Gets the display text.
 *
 * Returns: (transfer none): The response text
 */
const gchar *
lrg_dialog_response_get_text (const LrgDialogResponse *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->text;
}

/**
 * lrg_dialog_response_set_text:
 * @self: an #LrgDialogResponse
 * @text: new display text
 *
 * Sets the display text.
 */
void
lrg_dialog_response_set_text (LrgDialogResponse *self,
                              const gchar       *text)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (text != NULL);

    g_free (self->text);
    self->text = g_strdup (text);
}

/**
 * lrg_dialog_response_get_next_node_id:
 * @self: an #LrgDialogResponse
 *
 * Gets the next node ID to transition to.
 *
 * Returns: (transfer none) (nullable): The next node ID, or %NULL
 */
const gchar *
lrg_dialog_response_get_next_node_id (const LrgDialogResponse *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->next_node_id;
}

/**
 * lrg_dialog_response_set_next_node_id:
 * @self: an #LrgDialogResponse
 * @next_node_id: (nullable): next node ID
 *
 * Sets the next node ID.
 */
void
lrg_dialog_response_set_next_node_id (LrgDialogResponse *self,
                                      const gchar       *next_node_id)
{
    g_return_if_fail (self != NULL);

    g_free (self->next_node_id);
    self->next_node_id = g_strdup (next_node_id);
}

/**
 * lrg_dialog_response_add_condition:
 * @self: an #LrgDialogResponse
 * @condition: condition expression string
 *
 * Adds a condition that must be true for this response to appear.
 */
void
lrg_dialog_response_add_condition (LrgDialogResponse *self,
                                   const gchar       *condition)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (condition != NULL);

    g_ptr_array_add (self->conditions, g_strdup (condition));
}

/**
 * lrg_dialog_response_get_conditions:
 * @self: an #LrgDialogResponse
 *
 * Gets all conditions for this response.
 *
 * Returns: (transfer none) (element-type utf8): Array of condition strings
 */
GPtrArray *
lrg_dialog_response_get_conditions (const LrgDialogResponse *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->conditions;
}

/**
 * lrg_dialog_response_add_effect:
 * @self: an #LrgDialogResponse
 * @effect: effect expression string
 *
 * Adds an effect that triggers when this response is selected.
 */
void
lrg_dialog_response_add_effect (LrgDialogResponse *self,
                                const gchar       *effect)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (effect != NULL);

    g_ptr_array_add (self->effects, g_strdup (effect));
}

/**
 * lrg_dialog_response_get_effects:
 * @self: an #LrgDialogResponse
 *
 * Gets all effects for this response.
 *
 * Returns: (transfer none) (element-type utf8): Array of effect strings
 */
GPtrArray *
lrg_dialog_response_get_effects (const LrgDialogResponse *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->effects;
}
