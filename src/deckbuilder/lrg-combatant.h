/* lrg-combatant.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_COMBATANT_H
#define LRG_COMBATANT_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_COMBATANT (lrg_combatant_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgCombatant, lrg_combatant, LRG, COMBATANT, GObject)

/* Forward declarations */
typedef struct _LrgStatusEffectInstance LrgStatusEffectInstance;

/**
 * LrgCombatantInterface:
 * @parent_iface: parent interface
 * @get_id: get unique combatant identifier
 * @get_name: get display name
 * @get_max_health: get maximum health
 * @get_current_health: get current health
 * @set_current_health: set current health
 * @get_block: get current block
 * @set_block: set current block
 * @add_block: add block (applies dexterity, frail)
 * @clear_block: clear all block
 * @take_damage: take damage (returns actual damage taken)
 * @heal: heal health (returns actual healing)
 * @is_alive: check if combatant is alive
 * @get_status_stacks: get stacks of a status effect
 * @has_status: check if combatant has a status
 * @apply_status: apply a status effect
 * @remove_status: remove a status effect
 * @remove_status_stacks: remove stacks from a status
 * @clear_statuses: clear all statuses
 * @get_statuses: get list of all status instances
 *
 * Interface for entities that participate in combat.
 *
 * Since: 1.0
 */
struct _LrgCombatantInterface
{
    GTypeInterface parent_iface;

    /* Identity */
    const gchar * (* get_id)              (LrgCombatant *self);
    const gchar * (* get_name)            (LrgCombatant *self);

    /* Health */
    gint          (* get_max_health)      (LrgCombatant *self);
    gint          (* get_current_health)  (LrgCombatant *self);
    void          (* set_current_health)  (LrgCombatant *self,
                                           gint          health);

    /* Block */
    gint          (* get_block)           (LrgCombatant *self);
    void          (* set_block)           (LrgCombatant *self,
                                           gint          block);
    gint          (* add_block)           (LrgCombatant *self,
                                           gint          amount);
    void          (* clear_block)         (LrgCombatant *self);

    /* Combat actions */
    gint          (* take_damage)         (LrgCombatant   *self,
                                           gint            amount,
                                           LrgEffectFlags  flags);
    gint          (* heal)                (LrgCombatant *self,
                                           gint          amount);
    gboolean      (* is_alive)            (LrgCombatant *self);

    /* Status effects */
    gint          (* get_status_stacks)   (LrgCombatant *self,
                                           const gchar  *status_id);
    gboolean      (* has_status)          (LrgCombatant *self,
                                           const gchar  *status_id);
    gboolean      (* apply_status)        (LrgCombatant *self,
                                           const gchar  *status_id,
                                           gint          stacks);
    gboolean      (* remove_status)       (LrgCombatant *self,
                                           const gchar  *status_id);
    void          (* remove_status_stacks)(LrgCombatant *self,
                                           const gchar  *status_id,
                                           gint          stacks);
    void          (* clear_statuses)      (LrgCombatant *self);
    GList *       (* get_statuses)        (LrgCombatant *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* Identity methods */

LRG_AVAILABLE_IN_ALL
const gchar *
lrg_combatant_get_id (LrgCombatant *self);

LRG_AVAILABLE_IN_ALL
const gchar *
lrg_combatant_get_name (LrgCombatant *self);

/* Health methods */

LRG_AVAILABLE_IN_ALL
gint
lrg_combatant_get_max_health (LrgCombatant *self);

LRG_AVAILABLE_IN_ALL
gint
lrg_combatant_get_current_health (LrgCombatant *self);

LRG_AVAILABLE_IN_ALL
void
lrg_combatant_set_current_health (LrgCombatant *self,
                                  gint          health);

/* Block methods */

LRG_AVAILABLE_IN_ALL
gint
lrg_combatant_get_block (LrgCombatant *self);

LRG_AVAILABLE_IN_ALL
void
lrg_combatant_set_block (LrgCombatant *self,
                         gint          block);

/**
 * lrg_combatant_add_block:
 * @self: an #LrgCombatant
 * @amount: base amount of block to add
 *
 * Adds block to the combatant. The actual block gained may
 * be modified by dexterity and frail status effects.
 *
 * Returns: the actual amount of block gained
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combatant_add_block (LrgCombatant *self,
                         gint          amount);

LRG_AVAILABLE_IN_ALL
void
lrg_combatant_clear_block (LrgCombatant *self);

/* Combat action methods */

/**
 * lrg_combatant_take_damage:
 * @self: an #LrgCombatant
 * @amount: the amount of damage to take
 * @flags: effect flags (unblockable, HP loss, etc.)
 *
 * Deals damage to the combatant. Block is applied first
 * unless flags indicate otherwise.
 *
 * Returns: the actual damage taken (after block)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combatant_take_damage (LrgCombatant   *self,
                           gint            amount,
                           LrgEffectFlags  flags);

/**
 * lrg_combatant_heal:
 * @self: an #LrgCombatant
 * @amount: the amount to heal
 *
 * Heals the combatant. Healing cannot exceed max health.
 *
 * Returns: the actual amount healed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combatant_heal (LrgCombatant *self,
                    gint          amount);

LRG_AVAILABLE_IN_ALL
gboolean
lrg_combatant_is_alive (LrgCombatant *self);

/* Status effect methods */

LRG_AVAILABLE_IN_ALL
gint
lrg_combatant_get_status_stacks (LrgCombatant *self,
                                 const gchar  *status_id);

LRG_AVAILABLE_IN_ALL
gboolean
lrg_combatant_has_status (LrgCombatant *self,
                          const gchar  *status_id);

/**
 * lrg_combatant_apply_status:
 * @self: an #LrgCombatant
 * @status_id: the status effect identifier
 * @stacks: number of stacks to apply
 *
 * Applies a status effect to the combatant. If the status
 * already exists, stacks are added. May be blocked by
 * artifact status.
 *
 * Returns: %TRUE if the status was applied, %FALSE if blocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_combatant_apply_status (LrgCombatant *self,
                            const gchar  *status_id,
                            gint          stacks);

LRG_AVAILABLE_IN_ALL
gboolean
lrg_combatant_remove_status (LrgCombatant *self,
                             const gchar  *status_id);

LRG_AVAILABLE_IN_ALL
void
lrg_combatant_remove_status_stacks (LrgCombatant *self,
                                    const gchar  *status_id,
                                    gint          stacks);

LRG_AVAILABLE_IN_ALL
void
lrg_combatant_clear_statuses (LrgCombatant *self);

/**
 * lrg_combatant_get_statuses:
 * @self: an #LrgCombatant
 *
 * Gets all status effect instances on this combatant.
 *
 * Returns: (transfer none) (element-type LrgStatusEffectInstance):
 *   list of status instances
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_combatant_get_statuses (LrgCombatant *self);

G_END_DECLS

#endif /* LRG_COMBATANT_H */
