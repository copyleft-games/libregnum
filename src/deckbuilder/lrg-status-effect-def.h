/* lrg-status-effect-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgStatusEffectDef - Status effect definition.
 *
 * Status effects are buffs or debuffs applied to combatants during combat.
 * Each status has a stack count and optional duration. Common examples
 * include Strength, Vulnerable, Poison, and Artifact.
 *
 * Status effects can modify combat calculations (damage, block), trigger
 * effects at specific times (turn start/end), or provide passive abilities.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_STATUS_EFFECT_DEF (lrg_status_effect_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgStatusEffectDef, lrg_status_effect_def, LRG, STATUS_EFFECT_DEF, GObject)

/**
 * LrgStatusEffectType:
 * @LRG_STATUS_EFFECT_TYPE_BUFF: Beneficial effect
 * @LRG_STATUS_EFFECT_TYPE_DEBUFF: Detrimental effect
 * @LRG_STATUS_EFFECT_TYPE_NEUTRAL: Neither beneficial nor detrimental
 *
 * The category of status effect.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_STATUS_EFFECT_TYPE_BUFF,
    LRG_STATUS_EFFECT_TYPE_DEBUFF,
    LRG_STATUS_EFFECT_TYPE_NEUTRAL
} LrgStatusEffectType;

/**
 * LrgStatusStackBehavior:
 * @LRG_STATUS_STACK_INTENSITY: Stacks add together (e.g., Strength +2, +3 = +5)
 * @LRG_STATUS_STACK_DURATION: Stacks refresh duration (e.g., Vulnerable 2 turns)
 * @LRG_STATUS_STACK_COUNTER: Special counter behavior (e.g., Artifact blocks N debuffs)
 *
 * How stacks are interpreted for this status.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_STATUS_STACK_INTENSITY,
    LRG_STATUS_STACK_DURATION,
    LRG_STATUS_STACK_COUNTER
} LrgStatusStackBehavior;

/**
 * LrgStatusEffectDefClass:
 * @parent_class: parent class
 * @on_apply: called when status is first applied
 * @on_remove: called when status is removed
 * @on_stack_change: called when stack count changes
 * @on_turn_start: called at the start of the owner's turn
 * @on_turn_end: called at the end of the owner's turn
 * @on_damage_dealt: called when owner deals damage
 * @on_damage_received: called when owner receives damage
 * @on_block_gained: called when owner gains block
 * @modify_damage_dealt: modify outgoing damage
 * @modify_damage_received: modify incoming damage
 * @modify_block_gained: modify block amount
 * @can_apply_debuff: check if a debuff can be applied (for Artifact)
 * @get_tooltip: get dynamic tooltip text
 *
 * Class structure for status effect definitions.
 *
 * Since: 1.0
 */
struct _LrgStatusEffectDefClass
{
    GObjectClass parent_class;

    /**
     * LrgStatusEffectDefClass::on_apply:
     * @self: the status effect definition
     * @owner: the combatant receiving the status
     * @stacks: initial stack count
     * @context: (nullable): combat context
     *
     * Called when the status is first applied to a combatant.
     *
     * Since: 1.0
     */
    void (*on_apply) (LrgStatusEffectDef *self,
                      gpointer            owner,
                      gint                stacks,
                      gpointer            context);

    /**
     * LrgStatusEffectDefClass::on_remove:
     * @self: the status effect definition
     * @owner: the combatant losing the status
     * @context: (nullable): combat context
     *
     * Called when the status is removed from a combatant.
     *
     * Since: 1.0
     */
    void (*on_remove) (LrgStatusEffectDef *self,
                       gpointer            owner,
                       gpointer            context);

    /**
     * LrgStatusEffectDefClass::on_stack_change:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @old_stacks: previous stack count
     * @new_stacks: new stack count
     * @context: (nullable): combat context
     *
     * Called when the stack count changes.
     *
     * Since: 1.0
     */
    void (*on_stack_change) (LrgStatusEffectDef *self,
                             gpointer            owner,
                             gint                old_stacks,
                             gint                new_stacks,
                             gpointer            context);

    /**
     * LrgStatusEffectDefClass::on_turn_start:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @stacks: current stack count
     * @context: (nullable): combat context
     *
     * Called at the start of the owner's turn.
     *
     * Since: 1.0
     */
    void (*on_turn_start) (LrgStatusEffectDef *self,
                           gpointer            owner,
                           gint                stacks,
                           gpointer            context);

    /**
     * LrgStatusEffectDefClass::on_turn_end:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @stacks: current stack count
     * @context: (nullable): combat context
     *
     * Called at the end of the owner's turn. Duration-based statuses
     * typically decrement here.
     *
     * Since: 1.0
     */
    void (*on_turn_end) (LrgStatusEffectDef *self,
                         gpointer            owner,
                         gint                stacks,
                         gpointer            context);

    /**
     * LrgStatusEffectDefClass::on_damage_dealt:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @target: the damage target
     * @damage: damage amount dealt
     * @stacks: current stack count
     * @context: (nullable): combat context
     *
     * Called after the owner deals damage.
     *
     * Since: 1.0
     */
    void (*on_damage_dealt) (LrgStatusEffectDef *self,
                             gpointer            owner,
                             gpointer            target,
                             gint                damage,
                             gint                stacks,
                             gpointer            context);

    /**
     * LrgStatusEffectDefClass::on_damage_received:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @attacker: (nullable): the damage source
     * @damage: damage amount received
     * @stacks: current stack count
     * @context: (nullable): combat context
     *
     * Called after the owner receives damage.
     *
     * Since: 1.0
     */
    void (*on_damage_received) (LrgStatusEffectDef *self,
                                gpointer            owner,
                                gpointer            attacker,
                                gint                damage,
                                gint                stacks,
                                gpointer            context);

    /**
     * LrgStatusEffectDefClass::on_block_gained:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @block: block amount gained
     * @stacks: current stack count
     * @context: (nullable): combat context
     *
     * Called after the owner gains block.
     *
     * Since: 1.0
     */
    void (*on_block_gained) (LrgStatusEffectDef *self,
                             gpointer            owner,
                             gint                block,
                             gint                stacks,
                             gpointer            context);

    /**
     * LrgStatusEffectDefClass::modify_damage_dealt:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @base_damage: base damage amount
     * @stacks: current stack count
     * @context: (nullable): combat context
     *
     * Modifies outgoing damage. Called during damage calculation.
     *
     * Returns: the modified damage amount
     *
     * Since: 1.0
     */
    gint (*modify_damage_dealt) (LrgStatusEffectDef *self,
                                 gpointer            owner,
                                 gint                base_damage,
                                 gint                stacks,
                                 gpointer            context);

    /**
     * LrgStatusEffectDefClass::modify_damage_received:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @base_damage: base damage amount
     * @stacks: current stack count
     * @context: (nullable): combat context
     *
     * Modifies incoming damage. Called during damage calculation.
     *
     * Returns: the modified damage amount
     *
     * Since: 1.0
     */
    gint (*modify_damage_received) (LrgStatusEffectDef *self,
                                    gpointer            owner,
                                    gint                base_damage,
                                    gint                stacks,
                                    gpointer            context);

    /**
     * LrgStatusEffectDefClass::modify_block_gained:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @base_block: base block amount
     * @stacks: current stack count
     * @context: (nullable): combat context
     *
     * Modifies block gained. Called during block calculation.
     *
     * Returns: the modified block amount
     *
     * Since: 1.0
     */
    gint (*modify_block_gained) (LrgStatusEffectDef *self,
                                 gpointer            owner,
                                 gint                base_block,
                                 gint                stacks,
                                 gpointer            context);

    /**
     * LrgStatusEffectDefClass::can_apply_debuff:
     * @self: the status effect definition
     * @owner: the combatant with the status
     * @debuff: the debuff being applied
     * @stacks: current stack count of this status
     * @context: (nullable): combat context
     *
     * Checks if a debuff can be applied. Used by Artifact to block debuffs.
     *
     * Returns: %TRUE if the debuff can be applied
     *
     * Since: 1.0
     */
    gboolean (*can_apply_debuff) (LrgStatusEffectDef *self,
                                  gpointer            owner,
                                  LrgStatusEffectDef *debuff,
                                  gint                stacks,
                                  gpointer            context);

    /**
     * LrgStatusEffectDefClass::get_tooltip:
     * @self: the status effect definition
     * @stacks: current stack count
     *
     * Gets dynamic tooltip text based on current stacks.
     *
     * Returns: (transfer full): the tooltip text
     *
     * Since: 1.0
     */
    gchar * (*get_tooltip) (LrgStatusEffectDef *self,
                            gint                stacks);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_status_effect_def_new:
 * @id: unique status identifier
 * @name: display name
 * @effect_type: buff, debuff, or neutral
 *
 * Creates a new status effect definition.
 *
 * Returns: (transfer full): a new #LrgStatusEffectDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusEffectDef * lrg_status_effect_def_new (const gchar          *id,
                                                 const gchar          *name,
                                                 LrgStatusEffectType   effect_type);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_status_effect_def_get_id:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): the status ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_status_effect_def_get_id (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_get_name:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the display name.
 *
 * Returns: (transfer none): the status name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_status_effect_def_get_name (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_get_description:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_status_effect_def_get_description (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_set_description:
 * @self: a #LrgStatusEffectDef
 * @description: (nullable): the description
 *
 * Sets the description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_set_description (LrgStatusEffectDef *self,
                                             const gchar        *description);

/**
 * lrg_status_effect_def_get_icon:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the icon identifier.
 *
 * Returns: (transfer none) (nullable): the icon ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_status_effect_def_get_icon (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_set_icon:
 * @self: a #LrgStatusEffectDef
 * @icon: (nullable): the icon identifier
 *
 * Sets the icon identifier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_set_icon (LrgStatusEffectDef *self,
                                      const gchar        *icon);

/**
 * lrg_status_effect_def_get_effect_type:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the effect type (buff, debuff, neutral).
 *
 * Returns: the effect type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusEffectType lrg_status_effect_def_get_effect_type (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_get_stack_behavior:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the stack behavior.
 *
 * Returns: the stack behavior
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusStackBehavior lrg_status_effect_def_get_stack_behavior (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_set_stack_behavior:
 * @self: a #LrgStatusEffectDef
 * @behavior: the stack behavior
 *
 * Sets the stack behavior.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_set_stack_behavior (LrgStatusEffectDef    *self,
                                                LrgStatusStackBehavior behavior);

/**
 * lrg_status_effect_def_get_max_stacks:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the maximum stack count (0 = unlimited).
 *
 * Returns: the max stacks
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_status_effect_def_get_max_stacks (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_set_max_stacks:
 * @self: a #LrgStatusEffectDef
 * @max_stacks: maximum stacks (0 = unlimited)
 *
 * Sets the maximum stack count.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_set_max_stacks (LrgStatusEffectDef *self,
                                            gint                max_stacks);

/**
 * lrg_status_effect_def_is_permanent:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status is permanent (survives combat end).
 *
 * Returns: %TRUE if permanent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_def_is_permanent (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_set_permanent:
 * @self: a #LrgStatusEffectDef
 * @permanent: whether the status is permanent
 *
 * Sets whether the status is permanent.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_set_permanent (LrgStatusEffectDef *self,
                                           gboolean            permanent);

/**
 * lrg_status_effect_def_clears_at_turn_end:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status clears at end of turn.
 *
 * Returns: %TRUE if clears at turn end
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_def_clears_at_turn_end (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_set_clears_at_turn_end:
 * @self: a #LrgStatusEffectDef
 * @clears: whether to clear at turn end
 *
 * Sets whether the status clears at end of turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_set_clears_at_turn_end (LrgStatusEffectDef *self,
                                                    gboolean            clears);

/**
 * lrg_status_effect_def_decrements_at_turn_end:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status decrements stacks at end of turn.
 *
 * Returns: %TRUE if decrements at turn end
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_def_decrements_at_turn_end (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_set_decrements_at_turn_end:
 * @self: a #LrgStatusEffectDef
 * @decrements: whether to decrement at turn end
 *
 * Sets whether the status decrements stacks at end of turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_set_decrements_at_turn_end (LrgStatusEffectDef *self,
                                                        gboolean            decrements);

/**
 * lrg_status_effect_def_is_buff:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status is a buff.
 *
 * Returns: %TRUE if buff
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_def_is_buff (LrgStatusEffectDef *self);

/**
 * lrg_status_effect_def_is_debuff:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status is a debuff.
 *
 * Returns: %TRUE if debuff
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_def_is_debuff (LrgStatusEffectDef *self);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_status_effect_def_on_apply:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant receiving the status
 * @stacks: initial stack count
 * @context: (nullable): combat context
 *
 * Called when the status is first applied.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_on_apply (LrgStatusEffectDef *self,
                                      gpointer            owner,
                                      gint                stacks,
                                      gpointer            context);

/**
 * lrg_status_effect_def_on_remove:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant losing the status
 * @context: (nullable): combat context
 *
 * Called when the status is removed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_on_remove (LrgStatusEffectDef *self,
                                       gpointer            owner,
                                       gpointer            context);

/**
 * lrg_status_effect_def_on_stack_change:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @old_stacks: previous stack count
 * @new_stacks: new stack count
 * @context: (nullable): combat context
 *
 * Called when the stack count changes.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_on_stack_change (LrgStatusEffectDef *self,
                                             gpointer            owner,
                                             gint                old_stacks,
                                             gint                new_stacks,
                                             gpointer            context);

/**
 * lrg_status_effect_def_on_turn_start:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called at the start of the owner's turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_on_turn_start (LrgStatusEffectDef *self,
                                           gpointer            owner,
                                           gint                stacks,
                                           gpointer            context);

/**
 * lrg_status_effect_def_on_turn_end:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called at the end of the owner's turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_on_turn_end (LrgStatusEffectDef *self,
                                         gpointer            owner,
                                         gint                stacks,
                                         gpointer            context);

/**
 * lrg_status_effect_def_on_damage_dealt:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @target: the damage target
 * @damage: damage amount dealt
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called after the owner deals damage.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_on_damage_dealt (LrgStatusEffectDef *self,
                                             gpointer            owner,
                                             gpointer            target,
                                             gint                damage,
                                             gint                stacks,
                                             gpointer            context);

/**
 * lrg_status_effect_def_on_damage_received:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @attacker: (nullable): the damage source
 * @damage: damage amount received
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called after the owner receives damage.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_on_damage_received (LrgStatusEffectDef *self,
                                                gpointer            owner,
                                                gpointer            attacker,
                                                gint                damage,
                                                gint                stacks,
                                                gpointer            context);

/**
 * lrg_status_effect_def_on_block_gained:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @block: block amount gained
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called after the owner gains block.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_def_on_block_gained (LrgStatusEffectDef *self,
                                             gpointer            owner,
                                             gint                block,
                                             gint                stacks,
                                             gpointer            context);

/**
 * lrg_status_effect_def_modify_damage_dealt:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @base_damage: base damage amount
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Modifies outgoing damage.
 *
 * Returns: the modified damage amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_status_effect_def_modify_damage_dealt (LrgStatusEffectDef *self,
                                                 gpointer            owner,
                                                 gint                base_damage,
                                                 gint                stacks,
                                                 gpointer            context);

/**
 * lrg_status_effect_def_modify_damage_received:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @base_damage: base damage amount
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Modifies incoming damage.
 *
 * Returns: the modified damage amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_status_effect_def_modify_damage_received (LrgStatusEffectDef *self,
                                                    gpointer            owner,
                                                    gint                base_damage,
                                                    gint                stacks,
                                                    gpointer            context);

/**
 * lrg_status_effect_def_modify_block_gained:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @base_block: base block amount
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Modifies block gained.
 *
 * Returns: the modified block amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_status_effect_def_modify_block_gained (LrgStatusEffectDef *self,
                                                 gpointer            owner,
                                                 gint                base_block,
                                                 gint                stacks,
                                                 gpointer            context);

/**
 * lrg_status_effect_def_can_apply_debuff:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @debuff: the debuff being applied
 * @stacks: current stack count of this status
 * @context: (nullable): combat context
 *
 * Checks if a debuff can be applied. Used by Artifact.
 *
 * Returns: %TRUE if the debuff can be applied
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_def_can_apply_debuff (LrgStatusEffectDef *self,
                                                  gpointer            owner,
                                                  LrgStatusEffectDef *debuff,
                                                  gint                stacks,
                                                  gpointer            context);

/**
 * lrg_status_effect_def_get_tooltip:
 * @self: a #LrgStatusEffectDef
 * @stacks: current stack count
 *
 * Gets dynamic tooltip text based on current stacks.
 *
 * Returns: (transfer full): the tooltip text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_status_effect_def_get_tooltip (LrgStatusEffectDef *self,
                                            gint                stacks);

G_END_DECLS
