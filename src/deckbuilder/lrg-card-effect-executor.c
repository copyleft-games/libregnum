/* lrg-card-effect-executor.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardEffectExecutor - Interface implementation.
 */

#include "lrg-card-effect-executor.h"

G_DEFINE_INTERFACE (LrgCardEffectExecutor, lrg_card_effect_executor, G_TYPE_OBJECT)

static gboolean
lrg_card_effect_executor_default_validate (LrgCardEffectExecutor  *self,
                                           LrgCardEffect          *effect,
                                           GError                **error)
{
    /* Default validation always passes */
    return TRUE;
}

static gchar *
lrg_card_effect_executor_default_get_description (LrgCardEffectExecutor *self,
                                                  LrgCardEffect         *effect)
{
    /* Default description uses the effect type */
    return g_strdup_printf ("%s effect",
                            lrg_card_effect_get_effect_type (effect));
}

static void
lrg_card_effect_executor_default_init (LrgCardEffectExecutorInterface *iface)
{
    /* Set default implementations for optional methods */
    iface->validate = lrg_card_effect_executor_default_validate;
    iface->get_description = lrg_card_effect_executor_default_get_description;
}

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
const gchar *
lrg_card_effect_executor_get_effect_type (LrgCardEffectExecutor *self)
{
    LrgCardEffectExecutorInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_EFFECT_EXECUTOR (self), NULL);

    iface = LRG_CARD_EFFECT_EXECUTOR_GET_IFACE (self);
    g_return_val_if_fail (iface->get_effect_type != NULL, NULL);

    return iface->get_effect_type (self);
}

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
gboolean
lrg_card_effect_executor_execute (LrgCardEffectExecutor  *self,
                                  LrgCardEffect          *effect,
                                  gpointer                context,
                                  gpointer                source,
                                  gpointer                target,
                                  GError                **error)
{
    LrgCardEffectExecutorInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_EFFECT_EXECUTOR (self), FALSE);
    g_return_val_if_fail (effect != NULL, FALSE);

    iface = LRG_CARD_EFFECT_EXECUTOR_GET_IFACE (self);
    g_return_val_if_fail (iface->execute != NULL, FALSE);

    return iface->execute (self, effect, context, source, target, error);
}

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
gboolean
lrg_card_effect_executor_validate (LrgCardEffectExecutor  *self,
                                   LrgCardEffect          *effect,
                                   GError                **error)
{
    LrgCardEffectExecutorInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_EFFECT_EXECUTOR (self), FALSE);
    g_return_val_if_fail (effect != NULL, FALSE);

    iface = LRG_CARD_EFFECT_EXECUTOR_GET_IFACE (self);
    g_return_val_if_fail (iface->validate != NULL, FALSE);

    return iface->validate (self, effect, error);
}

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
gchar *
lrg_card_effect_executor_get_description (LrgCardEffectExecutor *self,
                                          LrgCardEffect         *effect)
{
    LrgCardEffectExecutorInterface *iface;

    g_return_val_if_fail (LRG_IS_CARD_EFFECT_EXECUTOR (self), NULL);
    g_return_val_if_fail (effect != NULL, NULL);

    iface = LRG_CARD_EFFECT_EXECUTOR_GET_IFACE (self);
    g_return_val_if_fail (iface->get_description != NULL, NULL);

    return iface->get_description (self, effect);
}
