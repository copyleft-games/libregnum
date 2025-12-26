/* lrg-input-action.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Named input action with multiple bindings.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-input-binding.h"

G_BEGIN_DECLS

#define LRG_TYPE_INPUT_ACTION (lrg_input_action_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgInputAction, lrg_input_action, LRG, INPUT_ACTION, GObject)

/**
 * LrgInputActionClass:
 * @parent_class: The parent class
 *
 * The class structure for #LrgInputAction.
 */
struct _LrgInputActionClass
{
    GObjectClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_input_action_new:
 * @name: the action name (e.g., "jump", "attack")
 *
 * Creates a new input action with the given name.
 *
 * Returns: (transfer full): A new #LrgInputAction
 */
LRG_AVAILABLE_IN_ALL
LrgInputAction * lrg_input_action_new (const gchar *name);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_input_action_get_name:
 * @self: an #LrgInputAction
 *
 * Gets the action name.
 *
 * Returns: (transfer none): The action name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_input_action_get_name (LrgInputAction *self);

/* ==========================================================================
 * Binding Management
 * ========================================================================== */

/**
 * lrg_input_action_add_binding:
 * @self: an #LrgInputAction
 * @binding: the binding to add
 *
 * Adds an input binding to this action.
 *
 * The binding is copied, so the caller retains ownership of the original.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_action_add_binding (LrgInputAction        *self,
                                   const LrgInputBinding *binding);

/**
 * lrg_input_action_remove_binding:
 * @self: an #LrgInputAction
 * @index: the index of the binding to remove
 *
 * Removes a binding from this action by index.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_action_remove_binding (LrgInputAction *self,
                                      guint           index);

/**
 * lrg_input_action_clear_bindings:
 * @self: an #LrgInputAction
 *
 * Removes all bindings from this action.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_action_clear_bindings (LrgInputAction *self);

/**
 * lrg_input_action_get_binding_count:
 * @self: an #LrgInputAction
 *
 * Gets the number of bindings in this action.
 *
 * Returns: The binding count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_input_action_get_binding_count (LrgInputAction *self);

/**
 * lrg_input_action_get_binding:
 * @self: an #LrgInputAction
 * @index: the binding index
 *
 * Gets a binding by index.
 *
 * Returns: (transfer none) (nullable): The binding, or %NULL if index is out of range
 */
LRG_AVAILABLE_IN_ALL
const LrgInputBinding * lrg_input_action_get_binding (LrgInputAction *self,
                                                      guint           index);

/* ==========================================================================
 * State Query
 * ========================================================================== */

/**
 * lrg_input_action_is_pressed:
 * @self: an #LrgInputAction
 *
 * Checks if any binding was just pressed this frame.
 *
 * Returns: %TRUE if any binding was just pressed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_action_is_pressed (LrgInputAction *self);

/**
 * lrg_input_action_is_down:
 * @self: an #LrgInputAction
 *
 * Checks if any binding is currently held down.
 *
 * Returns: %TRUE if any binding is held down
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_action_is_down (LrgInputAction *self);

/**
 * lrg_input_action_is_released:
 * @self: an #LrgInputAction
 *
 * Checks if any binding was just released this frame.
 *
 * Returns: %TRUE if any binding was just released
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_action_is_released (LrgInputAction *self);

/**
 * lrg_input_action_get_value:
 * @self: an #LrgInputAction
 *
 * Gets the maximum axis value from all bindings.
 *
 * For digital bindings, returns 1.0 if any is down, 0.0 otherwise.
 * For axis bindings, returns the maximum absolute value.
 *
 * Returns: The axis value (0.0 to 1.0 typically)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_input_action_get_value (LrgInputAction *self);

G_END_DECLS
