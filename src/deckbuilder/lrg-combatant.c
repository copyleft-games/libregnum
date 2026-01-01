/* lrg-combatant.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-combatant.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-combatant
 * @title: LrgCombatant
 * @short_description: Interface for combat participants
 *
 * #LrgCombatant is an interface that defines the contract for
 * entities that can participate in deckbuilder combat. Both
 * players and enemies implement this interface, allowing the
 * combat system to treat them uniformly.
 *
 * Implementors must provide methods for:
 * - Health management (current, max, healing, damage)
 * - Block management (gaining, spending on damage)
 * - Status effect tracking (apply, remove, query)
 *
 * Since: 1.0
 */

G_DEFINE_INTERFACE (LrgCombatant, lrg_combatant, G_TYPE_OBJECT)

static void
lrg_combatant_default_init (LrgCombatantInterface *iface)
{
    /* Default implementations are NULL - implementors must provide */
}

/**
 * lrg_combatant_get_id:
 * @self: an #LrgCombatant
 *
 * Gets the unique identifier for this combatant.
 *
 * Returns: (transfer none): the combatant's ID
 *
 * Since: 1.0
 */
const gchar *
lrg_combatant_get_id (LrgCombatant *self)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), NULL);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->get_id != NULL, NULL);

    return iface->get_id (self);
}

/**
 * lrg_combatant_get_name:
 * @self: an #LrgCombatant
 *
 * Gets the display name for this combatant.
 *
 * Returns: (transfer none): the combatant's name
 *
 * Since: 1.0
 */
const gchar *
lrg_combatant_get_name (LrgCombatant *self)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), NULL);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->get_name != NULL, NULL);

    return iface->get_name (self);
}

/**
 * lrg_combatant_get_max_health:
 * @self: an #LrgCombatant
 *
 * Gets the maximum health of this combatant.
 *
 * Returns: the maximum health
 *
 * Since: 1.0
 */
gint
lrg_combatant_get_max_health (LrgCombatant *self)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), 0);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->get_max_health != NULL, 0);

    return iface->get_max_health (self);
}

/**
 * lrg_combatant_get_current_health:
 * @self: an #LrgCombatant
 *
 * Gets the current health of this combatant.
 *
 * Returns: the current health
 *
 * Since: 1.0
 */
gint
lrg_combatant_get_current_health (LrgCombatant *self)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), 0);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->get_current_health != NULL, 0);

    return iface->get_current_health (self);
}

/**
 * lrg_combatant_set_current_health:
 * @self: an #LrgCombatant
 * @health: the new health value
 *
 * Sets the current health of this combatant.
 * The value will be clamped to [0, max_health].
 *
 * Since: 1.0
 */
void
lrg_combatant_set_current_health (LrgCombatant *self,
                                  gint          health)
{
    LrgCombatantInterface *iface;

    g_return_if_fail (LRG_IS_COMBATANT (self));

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_if_fail (iface->set_current_health != NULL);

    iface->set_current_health (self, health);
}

/**
 * lrg_combatant_get_block:
 * @self: an #LrgCombatant
 *
 * Gets the current block of this combatant.
 *
 * Returns: the current block amount
 *
 * Since: 1.0
 */
gint
lrg_combatant_get_block (LrgCombatant *self)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), 0);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->get_block != NULL, 0);

    return iface->get_block (self);
}

/**
 * lrg_combatant_set_block:
 * @self: an #LrgCombatant
 * @block: the new block value
 *
 * Sets the current block of this combatant directly.
 * Use add_block() for normal block gains.
 *
 * Since: 1.0
 */
void
lrg_combatant_set_block (LrgCombatant *self,
                         gint          block)
{
    LrgCombatantInterface *iface;

    g_return_if_fail (LRG_IS_COMBATANT (self));

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_if_fail (iface->set_block != NULL);

    iface->set_block (self, MAX (0, block));
}

gint
lrg_combatant_add_block (LrgCombatant *self,
                         gint          amount)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), 0);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->add_block != NULL, 0);

    return iface->add_block (self, amount);
}

/**
 * lrg_combatant_clear_block:
 * @self: an #LrgCombatant
 *
 * Removes all block from this combatant.
 *
 * Since: 1.0
 */
void
lrg_combatant_clear_block (LrgCombatant *self)
{
    LrgCombatantInterface *iface;

    g_return_if_fail (LRG_IS_COMBATANT (self));

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_if_fail (iface->clear_block != NULL);

    iface->clear_block (self);
}

gint
lrg_combatant_take_damage (LrgCombatant   *self,
                           gint            amount,
                           LrgEffectFlags  flags)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), 0);

    if (amount <= 0)
        return 0;

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->take_damage != NULL, 0);

    return iface->take_damage (self, amount, flags);
}

gint
lrg_combatant_heal (LrgCombatant *self,
                    gint          amount)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), 0);

    if (amount <= 0)
        return 0;

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->heal != NULL, 0);

    return iface->heal (self, amount);
}

/**
 * lrg_combatant_is_alive:
 * @self: an #LrgCombatant
 *
 * Checks if this combatant is still alive.
 *
 * Returns: %TRUE if current health > 0
 *
 * Since: 1.0
 */
gboolean
lrg_combatant_is_alive (LrgCombatant *self)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), FALSE);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->is_alive != NULL, FALSE);

    return iface->is_alive (self);
}

/**
 * lrg_combatant_get_status_stacks:
 * @self: an #LrgCombatant
 * @status_id: the status effect identifier
 *
 * Gets the number of stacks of a status effect.
 *
 * Returns: the number of stacks, or 0 if not present
 *
 * Since: 1.0
 */
gint
lrg_combatant_get_status_stacks (LrgCombatant *self,
                                 const gchar  *status_id)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), 0);
    g_return_val_if_fail (status_id != NULL, 0);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->get_status_stacks != NULL, 0);

    return iface->get_status_stacks (self, status_id);
}

/**
 * lrg_combatant_has_status:
 * @self: an #LrgCombatant
 * @status_id: the status effect identifier
 *
 * Checks if this combatant has a status effect.
 *
 * Returns: %TRUE if the status is present with > 0 stacks
 *
 * Since: 1.0
 */
gboolean
lrg_combatant_has_status (LrgCombatant *self,
                          const gchar  *status_id)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), FALSE);
    g_return_val_if_fail (status_id != NULL, FALSE);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->has_status != NULL, FALSE);

    return iface->has_status (self, status_id);
}

gboolean
lrg_combatant_apply_status (LrgCombatant *self,
                            const gchar  *status_id,
                            gint          stacks)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), FALSE);
    g_return_val_if_fail (status_id != NULL, FALSE);

    if (stacks <= 0)
        return FALSE;

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->apply_status != NULL, FALSE);

    return iface->apply_status (self, status_id, stacks);
}

/**
 * lrg_combatant_remove_status:
 * @self: an #LrgCombatant
 * @status_id: the status effect identifier
 *
 * Completely removes a status effect.
 *
 * Returns: %TRUE if the status was present and removed
 *
 * Since: 1.0
 */
gboolean
lrg_combatant_remove_status (LrgCombatant *self,
                             const gchar  *status_id)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), FALSE);
    g_return_val_if_fail (status_id != NULL, FALSE);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->remove_status != NULL, FALSE);

    return iface->remove_status (self, status_id);
}

/**
 * lrg_combatant_remove_status_stacks:
 * @self: an #LrgCombatant
 * @status_id: the status effect identifier
 * @stacks: number of stacks to remove
 *
 * Removes stacks from a status effect. If stacks reaches 0,
 * the status is removed entirely.
 *
 * Since: 1.0
 */
void
lrg_combatant_remove_status_stacks (LrgCombatant *self,
                                    const gchar  *status_id,
                                    gint          stacks)
{
    LrgCombatantInterface *iface;

    g_return_if_fail (LRG_IS_COMBATANT (self));
    g_return_if_fail (status_id != NULL);

    if (stacks <= 0)
        return;

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_if_fail (iface->remove_status_stacks != NULL);

    iface->remove_status_stacks (self, status_id, stacks);
}

/**
 * lrg_combatant_clear_statuses:
 * @self: an #LrgCombatant
 *
 * Removes all status effects from this combatant.
 *
 * Since: 1.0
 */
void
lrg_combatant_clear_statuses (LrgCombatant *self)
{
    LrgCombatantInterface *iface;

    g_return_if_fail (LRG_IS_COMBATANT (self));

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_if_fail (iface->clear_statuses != NULL);

    iface->clear_statuses (self);
}

GList *
lrg_combatant_get_statuses (LrgCombatant *self)
{
    LrgCombatantInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBATANT (self), NULL);

    iface = LRG_COMBATANT_GET_IFACE (self);
    g_return_val_if_fail (iface->get_statuses != NULL, NULL);

    return iface->get_statuses (self);
}
