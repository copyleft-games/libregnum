/* lrg-status-effect-instance.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgStatusEffectInstance - Runtime status effect instance implementation.
 */

#include "lrg-status-effect-instance.h"
#include "../lrg-log.h"

struct _LrgStatusEffectInstance
{
    LrgStatusEffectDef *def;
    gint                stacks;
};

G_DEFINE_BOXED_TYPE (LrgStatusEffectInstance,
                     lrg_status_effect_instance,
                     lrg_status_effect_instance_copy,
                     lrg_status_effect_instance_free)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_status_effect_instance_new:
 * @def: (transfer none): the status effect definition
 * @stacks: initial stack count
 *
 * Creates a new status effect instance.
 *
 * Returns: (transfer full): a new #LrgStatusEffectInstance
 *
 * Since: 1.0
 */
LrgStatusEffectInstance *
lrg_status_effect_instance_new (LrgStatusEffectDef *def,
                                 gint                stacks)
{
    LrgStatusEffectInstance *self;
    gint max_stacks;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (def), NULL);
    g_return_val_if_fail (stacks > 0, NULL);

    self = g_slice_new0 (LrgStatusEffectInstance);
    self->def = g_object_ref (def);

    /* Clamp to max stacks if set */
    max_stacks = lrg_status_effect_def_get_max_stacks (def);
    if (max_stacks > 0 && stacks > max_stacks)
        self->stacks = max_stacks;
    else
        self->stacks = stacks;

    return self;
}

/**
 * lrg_status_effect_instance_copy:
 * @self: a #LrgStatusEffectInstance
 *
 * Creates a copy of a status effect instance.
 *
 * Returns: (transfer full): a copy of @self
 *
 * Since: 1.0
 */
LrgStatusEffectInstance *
lrg_status_effect_instance_copy (LrgStatusEffectInstance *self)
{
    LrgStatusEffectInstance *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new0 (LrgStatusEffectInstance);
    copy->def = g_object_ref (self->def);
    copy->stacks = self->stacks;

    return copy;
}

/**
 * lrg_status_effect_instance_free:
 * @self: (nullable): a #LrgStatusEffectInstance
 *
 * Frees a status effect instance.
 *
 * Since: 1.0
 */
void
lrg_status_effect_instance_free (LrgStatusEffectInstance *self)
{
    if (self == NULL)
        return;

    g_clear_object (&self->def);
    g_slice_free (LrgStatusEffectInstance, self);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_status_effect_instance_get_def:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the status effect definition.
 *
 * Returns: (transfer none): the definition
 *
 * Since: 1.0
 */
LrgStatusEffectDef *
lrg_status_effect_instance_get_def (LrgStatusEffectInstance *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->def;
}

/**
 * lrg_status_effect_instance_get_id:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the status effect ID (from definition).
 *
 * Returns: (transfer none): the status ID
 *
 * Since: 1.0
 */
const gchar *
lrg_status_effect_instance_get_id (LrgStatusEffectInstance *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return lrg_status_effect_def_get_id (self->def);
}

/**
 * lrg_status_effect_instance_get_stacks:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the current stack count.
 *
 * Returns: the stack count
 *
 * Since: 1.0
 */
gint
lrg_status_effect_instance_get_stacks (LrgStatusEffectInstance *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->stacks;
}

/**
 * lrg_status_effect_instance_set_stacks:
 * @self: a #LrgStatusEffectInstance
 * @stacks: the new stack count
 *
 * Sets the stack count. Respects max stacks from definition.
 *
 * Since: 1.0
 */
void
lrg_status_effect_instance_set_stacks (LrgStatusEffectInstance *self,
                                        gint                     stacks)
{
    gint max_stacks;

    g_return_if_fail (self != NULL);

    max_stacks = lrg_status_effect_def_get_max_stacks (self->def);

    if (max_stacks > 0 && stacks > max_stacks)
        self->stacks = max_stacks;
    else
        self->stacks = stacks;
}

/**
 * lrg_status_effect_instance_add_stacks:
 * @self: a #LrgStatusEffectInstance
 * @amount: stacks to add (can be negative)
 *
 * Adds stacks to the current count. Respects max stacks.
 *
 * Returns: the new stack count
 *
 * Since: 1.0
 */
gint
lrg_status_effect_instance_add_stacks (LrgStatusEffectInstance *self,
                                        gint                     amount)
{
    g_return_val_if_fail (self != NULL, 0);

    lrg_status_effect_instance_set_stacks (self, self->stacks + amount);
    return self->stacks;
}

/**
 * lrg_status_effect_instance_remove_stacks:
 * @self: a #LrgStatusEffectInstance
 * @amount: stacks to remove
 *
 * Removes stacks from the current count.
 *
 * Returns: the new stack count
 *
 * Since: 1.0
 */
gint
lrg_status_effect_instance_remove_stacks (LrgStatusEffectInstance *self,
                                           gint                     amount)
{
    g_return_val_if_fail (self != NULL, 0);
    g_return_val_if_fail (amount >= 0, self->stacks);

    self->stacks -= amount;
    if (self->stacks < 0)
        self->stacks = 0;

    return self->stacks;
}

/**
 * lrg_status_effect_instance_is_expired:
 * @self: a #LrgStatusEffectInstance
 *
 * Checks if the status has expired (stacks <= 0).
 *
 * Returns: %TRUE if expired
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_instance_is_expired (LrgStatusEffectInstance *self)
{
    g_return_val_if_fail (self != NULL, TRUE);
    return self->stacks <= 0;
}

/* ==========================================================================
 * Convenience Accessors (from definition)
 * ========================================================================== */

/**
 * lrg_status_effect_instance_get_name:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the display name (from definition).
 *
 * Returns: (transfer none): the status name
 *
 * Since: 1.0
 */
const gchar *
lrg_status_effect_instance_get_name (LrgStatusEffectInstance *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return lrg_status_effect_def_get_name (self->def);
}

/**
 * lrg_status_effect_instance_get_effect_type:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the effect type (from definition).
 *
 * Returns: the effect type
 *
 * Since: 1.0
 */
LrgStatusEffectType
lrg_status_effect_instance_get_effect_type (LrgStatusEffectInstance *self)
{
    g_return_val_if_fail (self != NULL, LRG_STATUS_EFFECT_TYPE_BUFF);
    return lrg_status_effect_def_get_effect_type (self->def);
}

/**
 * lrg_status_effect_instance_is_buff:
 * @self: a #LrgStatusEffectInstance
 *
 * Checks if this is a buff.
 *
 * Returns: %TRUE if buff
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_instance_is_buff (LrgStatusEffectInstance *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return lrg_status_effect_def_is_buff (self->def);
}

/**
 * lrg_status_effect_instance_is_debuff:
 * @self: a #LrgStatusEffectInstance
 *
 * Checks if this is a debuff.
 *
 * Returns: %TRUE if debuff
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_instance_is_debuff (LrgStatusEffectInstance *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return lrg_status_effect_def_is_debuff (self->def);
}

/**
 * lrg_status_effect_instance_get_tooltip:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the tooltip text for current state.
 *
 * Returns: (transfer full): the tooltip text
 *
 * Since: 1.0
 */
gchar *
lrg_status_effect_instance_get_tooltip (LrgStatusEffectInstance *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return lrg_status_effect_def_get_tooltip (self->def, self->stacks);
}
