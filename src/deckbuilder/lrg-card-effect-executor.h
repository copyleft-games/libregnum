/* lrg-card-effect-executor.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardEffectExecutor - Interface for effect execution.
 *
 * Effect executors handle the actual logic of applying effects.
 * Each executor is registered with the effect registry and matched
 * by the effect's type string (e.g., "damage", "block", "draw").
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-card-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_CARD_EFFECT_EXECUTOR (lrg_card_effect_executor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgCardEffectExecutor, lrg_card_effect_executor,
                     LRG, CARD_EFFECT_EXECUTOR, GObject)

/**
 * LrgCardEffectExecutorInterface:
 * @parent_iface: parent interface
 * @get_effect_type: returns the effect type this executor handles
 * @execute: executes the effect against a target
 * @validate: validates effect parameters before execution
 * @get_description: generates a human-readable description
 *
 * Interface structure for #LrgCardEffectExecutor.
 *
 * Implementors must provide at minimum `get_effect_type` and `execute`.
 * The validate and get_description methods have default implementations.
 */
struct _LrgCardEffectExecutorInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgCardEffectExecutorInterface::get_effect_type:
     * @self: a #LrgCardEffectExecutor
     *
     * Gets the effect type string this executor handles.
     * This should match the effect_type field of effects
     * that this executor can process.
     *
     * Returns: (transfer none): the effect type string (e.g., "damage")
     */
    const gchar * (*get_effect_type) (LrgCardEffectExecutor *self);

    /**
     * LrgCardEffectExecutorInterface::execute:
     * @self: a #LrgCardEffectExecutor
     * @effect: the effect to execute
     * @context: the combat context (opaque pointer)
     * @source: (nullable): the source combatant
     * @target: (nullable): the target combatant
     * @error: (optional): return location for a #GError
     *
     * Executes the effect. The context provides access to combat
     * state, and source/target are the combatants involved.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*execute) (LrgCardEffectExecutor  *self,
                         LrgCardEffect          *effect,
                         gpointer                context,
                         gpointer                source,
                         gpointer                target,
                         GError                **error);

    /**
     * LrgCardEffectExecutorInterface::validate:
     * @self: a #LrgCardEffectExecutor
     * @effect: the effect to validate
     * @error: (optional): return location for a #GError
     *
     * Validates that the effect has all required parameters
     * and they are within acceptable ranges.
     *
     * Returns: %TRUE if valid, %FALSE with @error set if invalid
     */
    gboolean (*validate) (LrgCardEffectExecutor  *self,
                          LrgCardEffect          *effect,
                          GError                **error);

    /**
     * LrgCardEffectExecutorInterface::get_description:
     * @self: a #LrgCardEffectExecutor
     * @effect: the effect to describe
     *
     * Generates a human-readable description of the effect
     * for display in card tooltips.
     *
     * Returns: (transfer full): the description string
     */
    gchar * (*get_description) (LrgCardEffectExecutor *self,
                                LrgCardEffect         *effect);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_card_effect_executor_get_effect_type:
 * @self: a #LrgCardEffectExecutor
 *
 * Gets the effect type string this executor handles.
 *
 * Returns: (transfer none): the effect type string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_effect_executor_get_effect_type (LrgCardEffectExecutor *self);

/**
 * lrg_card_effect_executor_execute:
 * @self: a #LrgCardEffectExecutor
 * @effect: the effect to execute
 * @context: the combat context
 * @source: (nullable): the source combatant
 * @target: (nullable): the target combatant
 * @error: (optional): return location for a #GError
 *
 * Executes the effect against the target.
 *
 * Returns: %TRUE on success, %FALSE with @error set on failure
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_effect_executor_execute (LrgCardEffectExecutor  *self,
                                           LrgCardEffect          *effect,
                                           gpointer                context,
                                           gpointer                source,
                                           gpointer                target,
                                           GError                **error);

/**
 * lrg_card_effect_executor_validate:
 * @self: a #LrgCardEffectExecutor
 * @effect: the effect to validate
 * @error: (optional): return location for a #GError
 *
 * Validates that the effect has all required parameters.
 *
 * Returns: %TRUE if valid, %FALSE with @error set if invalid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_effect_executor_validate (LrgCardEffectExecutor  *self,
                                            LrgCardEffect          *effect,
                                            GError                **error);

/**
 * lrg_card_effect_executor_get_description:
 * @self: a #LrgCardEffectExecutor
 * @effect: the effect to describe
 *
 * Generates a human-readable description of the effect.
 *
 * Returns: (transfer full): the description string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_card_effect_executor_get_description (LrgCardEffectExecutor *self,
                                                  LrgCardEffect         *effect);

G_END_DECLS
