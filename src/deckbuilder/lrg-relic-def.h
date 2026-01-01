/* lrg-relic-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRelicDef - Base class for relic definitions.
 *
 * Relics are passive items that provide effects throughout a run.
 * They can trigger on various game events (combat start, turn start,
 * card played, damage dealt, etc.) and may have counters for
 * tracking activation conditions.
 *
 * This is a derivable type - subclass it to create custom relics
 * with specialized behavior.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LrgRelicTrigger:
 * @LRG_RELIC_TRIGGER_NONE: No automatic trigger
 * @LRG_RELIC_TRIGGER_COMBAT_START: Triggers at combat start
 * @LRG_RELIC_TRIGGER_COMBAT_END: Triggers at combat end
 * @LRG_RELIC_TRIGGER_TURN_START: Triggers at turn start
 * @LRG_RELIC_TRIGGER_TURN_END: Triggers at turn end
 * @LRG_RELIC_TRIGGER_ON_CARD_PLAYED: Triggers when a card is played
 * @LRG_RELIC_TRIGGER_ON_ATTACK: Triggers on attack cards
 * @LRG_RELIC_TRIGGER_ON_SKILL: Triggers on skill cards
 * @LRG_RELIC_TRIGGER_ON_POWER: Triggers on power cards
 * @LRG_RELIC_TRIGGER_ON_DAMAGE_DEALT: Triggers when dealing damage
 * @LRG_RELIC_TRIGGER_ON_DAMAGE_RECEIVED: Triggers when receiving damage
 * @LRG_RELIC_TRIGGER_ON_BLOCK_GAINED: Triggers when gaining block
 * @LRG_RELIC_TRIGGER_ON_HEAL: Triggers when healing
 * @LRG_RELIC_TRIGGER_ON_GOLD_GAINED: Triggers when gaining gold
 * @LRG_RELIC_TRIGGER_ON_CARD_DRAW: Triggers when drawing cards
 * @LRG_RELIC_TRIGGER_ON_CARD_EXHAUST: Triggers when exhausting cards
 * @LRG_RELIC_TRIGGER_ON_CARD_DISCARD: Triggers when discarding cards
 * @LRG_RELIC_TRIGGER_ON_SHUFFLE: Triggers when deck is shuffled
 * @LRG_RELIC_TRIGGER_ON_ENEMY_DEATH: Triggers when an enemy dies
 * @LRG_RELIC_TRIGGER_ON_REST: Triggers at rest sites
 * @LRG_RELIC_TRIGGER_ON_CHEST_OPEN: Triggers when opening chests
 * @LRG_RELIC_TRIGGER_ON_POTION_USE: Triggers when using potions
 *
 * Relic trigger events (flags).
 *
 * Since: 1.0
 */
typedef enum /*< flags >*/
{
    LRG_RELIC_TRIGGER_NONE              = 0,
    LRG_RELIC_TRIGGER_COMBAT_START      = 1 << 0,
    LRG_RELIC_TRIGGER_COMBAT_END        = 1 << 1,
    LRG_RELIC_TRIGGER_TURN_START        = 1 << 2,
    LRG_RELIC_TRIGGER_TURN_END          = 1 << 3,
    LRG_RELIC_TRIGGER_ON_CARD_PLAYED    = 1 << 4,
    LRG_RELIC_TRIGGER_ON_ATTACK         = 1 << 5,
    LRG_RELIC_TRIGGER_ON_SKILL          = 1 << 6,
    LRG_RELIC_TRIGGER_ON_POWER          = 1 << 7,
    LRG_RELIC_TRIGGER_ON_DAMAGE_DEALT   = 1 << 8,
    LRG_RELIC_TRIGGER_ON_DAMAGE_RECEIVED = 1 << 9,
    LRG_RELIC_TRIGGER_ON_BLOCK_GAINED   = 1 << 10,
    LRG_RELIC_TRIGGER_ON_HEAL           = 1 << 11,
    LRG_RELIC_TRIGGER_ON_GOLD_GAINED    = 1 << 12,
    LRG_RELIC_TRIGGER_ON_CARD_DRAW      = 1 << 13,
    LRG_RELIC_TRIGGER_ON_CARD_EXHAUST   = 1 << 14,
    LRG_RELIC_TRIGGER_ON_CARD_DISCARD   = 1 << 15,
    LRG_RELIC_TRIGGER_ON_SHUFFLE        = 1 << 16,
    LRG_RELIC_TRIGGER_ON_ENEMY_DEATH    = 1 << 17,
    LRG_RELIC_TRIGGER_ON_REST           = 1 << 18,
    LRG_RELIC_TRIGGER_ON_CHEST_OPEN     = 1 << 19,
    LRG_RELIC_TRIGGER_ON_POTION_USE     = 1 << 20
} LrgRelicTrigger;

#define LRG_TYPE_RELIC_DEF (lrg_relic_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgRelicDef, lrg_relic_def, LRG, RELIC_DEF, GObject)

/**
 * LrgRelicDefClass:
 * @parent_class: The parent class
 * @on_obtain: Called when the relic is obtained
 * @on_remove: Called when the relic is removed
 * @on_combat_start: Called at combat start
 * @on_combat_end: Called at combat end
 * @on_turn_start: Called at turn start
 * @on_turn_end: Called at turn end
 * @on_card_played: Called when a card is played
 * @on_damage_dealt: Called when damage is dealt
 * @on_damage_received: Called when damage is received
 * @on_heal: Called when healing occurs
 * @on_counter_reached: Called when counter reaches threshold
 * @modify_damage_dealt: Modify outgoing damage
 * @modify_damage_received: Modify incoming damage
 * @modify_block_gained: Modify block gained
 * @modify_heal: Modify healing amount
 * @modify_gold_gained: Modify gold gained
 * @get_tooltip: Get tooltip text
 *
 * The virtual function table for #LrgRelicDef.
 *
 * Since: 1.0
 */
struct _LrgRelicDefClass
{
    GObjectClass parent_class;

    /* Lifecycle */
    void     (* on_obtain)           (LrgRelicDef *self,
                                      gpointer     context);
    void     (* on_remove)           (LrgRelicDef *self,
                                      gpointer     context);

    /* Combat events */
    void     (* on_combat_start)     (LrgRelicDef *self,
                                      gpointer     context);
    void     (* on_combat_end)       (LrgRelicDef *self,
                                      gpointer     context,
                                      gboolean     victory);
    void     (* on_turn_start)       (LrgRelicDef *self,
                                      gpointer     context,
                                      gint         turn);
    void     (* on_turn_end)         (LrgRelicDef *self,
                                      gpointer     context,
                                      gint         turn);

    /* Card events */
    void     (* on_card_played)      (LrgRelicDef *self,
                                      gpointer     context,
                                      gpointer     card);
    void     (* on_card_draw)        (LrgRelicDef *self,
                                      gpointer     context,
                                      gpointer     card);
    void     (* on_card_exhaust)     (LrgRelicDef *self,
                                      gpointer     context,
                                      gpointer     card);
    void     (* on_card_discard)     (LrgRelicDef *self,
                                      gpointer     context,
                                      gpointer     card);

    /* Damage/healing events */
    void     (* on_damage_dealt)     (LrgRelicDef *self,
                                      gpointer     context,
                                      gpointer     target,
                                      gint         amount);
    void     (* on_damage_received)  (LrgRelicDef *self,
                                      gpointer     context,
                                      gpointer     source,
                                      gint         amount);
    void     (* on_heal)             (LrgRelicDef *self,
                                      gpointer     context,
                                      gint         amount);

    /* Counter events */
    void     (* on_counter_reached)  (LrgRelicDef *self,
                                      gpointer     context);

    /* Modifiers */
    gint     (* modify_damage_dealt) (LrgRelicDef *self,
                                      gpointer     context,
                                      gint         base_damage,
                                      gpointer     target);
    gint     (* modify_damage_received) (LrgRelicDef *self,
                                         gpointer     context,
                                         gint         base_damage,
                                         gpointer     source);
    gint     (* modify_block_gained) (LrgRelicDef *self,
                                      gpointer     context,
                                      gint         base_block);
    gint     (* modify_heal)         (LrgRelicDef *self,
                                      gpointer     context,
                                      gint         base_heal);
    gint     (* modify_gold_gained)  (LrgRelicDef *self,
                                      gpointer     context,
                                      gint         base_gold);

    /* Tooltip */
    gchar *  (* get_tooltip)         (LrgRelicDef *self,
                                      gpointer     context);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_relic_def_new:
 * @id: unique identifier
 * @name: display name
 *
 * Creates a new relic definition.
 *
 * Returns: (transfer full): a new #LrgRelicDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRelicDef * lrg_relic_def_new (const gchar *id,
                                  const gchar *name);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_relic_def_get_id:
 * @self: a #LrgRelicDef
 *
 * Gets the relic's unique identifier.
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_relic_def_get_id (LrgRelicDef *self);

/**
 * lrg_relic_def_get_name:
 * @self: a #LrgRelicDef
 *
 * Gets the relic's display name.
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_relic_def_get_name (LrgRelicDef *self);

/**
 * lrg_relic_def_set_name:
 * @self: a #LrgRelicDef
 * @name: the new name
 *
 * Sets the relic's display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_set_name (LrgRelicDef *self,
                              const gchar *name);

/**
 * lrg_relic_def_get_description:
 * @self: a #LrgRelicDef
 *
 * Gets the relic's description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_relic_def_get_description (LrgRelicDef *self);

/**
 * lrg_relic_def_set_description:
 * @self: a #LrgRelicDef
 * @description: (nullable): the description
 *
 * Sets the relic's description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_set_description (LrgRelicDef *self,
                                     const gchar *description);

/**
 * lrg_relic_def_get_flavor_text:
 * @self: a #LrgRelicDef
 *
 * Gets the relic's flavor text.
 *
 * Returns: (transfer none) (nullable): the flavor text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_relic_def_get_flavor_text (LrgRelicDef *self);

/**
 * lrg_relic_def_set_flavor_text:
 * @self: a #LrgRelicDef
 * @flavor_text: (nullable): the flavor text
 *
 * Sets the relic's flavor text.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_set_flavor_text (LrgRelicDef *self,
                                     const gchar *flavor_text);

/**
 * lrg_relic_def_get_icon:
 * @self: a #LrgRelicDef
 *
 * Gets the relic's icon path.
 *
 * Returns: (transfer none) (nullable): the icon path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_relic_def_get_icon (LrgRelicDef *self);

/**
 * lrg_relic_def_set_icon:
 * @self: a #LrgRelicDef
 * @icon: (nullable): the icon path
 *
 * Sets the relic's icon path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_set_icon (LrgRelicDef *self,
                              const gchar *icon);

/**
 * lrg_relic_def_get_rarity:
 * @self: a #LrgRelicDef
 *
 * Gets the relic's rarity.
 *
 * Returns: the rarity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRelicRarity lrg_relic_def_get_rarity (LrgRelicDef *self);

/**
 * lrg_relic_def_set_rarity:
 * @self: a #LrgRelicDef
 * @rarity: the rarity
 *
 * Sets the relic's rarity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_set_rarity (LrgRelicDef   *self,
                                LrgRelicRarity rarity);

/**
 * lrg_relic_def_get_triggers:
 * @self: a #LrgRelicDef
 *
 * Gets the relic's trigger flags.
 *
 * Returns: the trigger flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRelicTrigger lrg_relic_def_get_triggers (LrgRelicDef *self);

/**
 * lrg_relic_def_set_triggers:
 * @self: a #LrgRelicDef
 * @triggers: the trigger flags
 *
 * Sets the relic's trigger flags.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_set_triggers (LrgRelicDef    *self,
                                  LrgRelicTrigger triggers);

/**
 * lrg_relic_def_has_trigger:
 * @self: a #LrgRelicDef
 * @trigger: the trigger to check
 *
 * Checks if the relic has a specific trigger.
 *
 * Returns: %TRUE if the trigger is set
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_relic_def_has_trigger (LrgRelicDef    *self,
                                     LrgRelicTrigger trigger);

/**
 * lrg_relic_def_get_counter_max:
 * @self: a #LrgRelicDef
 *
 * Gets the maximum counter value (0 = no counter).
 *
 * Returns: the max counter
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_relic_def_get_counter_max (LrgRelicDef *self);

/**
 * lrg_relic_def_set_counter_max:
 * @self: a #LrgRelicDef
 * @counter_max: the max counter (0 = no counter)
 *
 * Sets the maximum counter value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_set_counter_max (LrgRelicDef *self,
                                     gint         counter_max);

/**
 * lrg_relic_def_get_unique:
 * @self: a #LrgRelicDef
 *
 * Gets whether the relic is unique (only one per run).
 *
 * Returns: %TRUE if unique
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_relic_def_get_unique (LrgRelicDef *self);

/**
 * lrg_relic_def_set_unique:
 * @self: a #LrgRelicDef
 * @unique: whether unique
 *
 * Sets whether the relic is unique.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_set_unique (LrgRelicDef *self,
                                gboolean     unique);

/**
 * lrg_relic_def_get_price:
 * @self: a #LrgRelicDef
 *
 * Gets the relic's base shop price.
 *
 * Returns: the price
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_relic_def_get_price (LrgRelicDef *self);

/**
 * lrg_relic_def_set_price:
 * @self: a #LrgRelicDef
 * @price: the base price
 *
 * Sets the relic's base shop price.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_set_price (LrgRelicDef *self,
                               gint         price);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_relic_def_on_obtain:
 * @self: a #LrgRelicDef
 * @context: (nullable): game context
 *
 * Called when the relic is obtained.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_on_obtain (LrgRelicDef *self,
                               gpointer     context);

/**
 * lrg_relic_def_on_remove:
 * @self: a #LrgRelicDef
 * @context: (nullable): game context
 *
 * Called when the relic is removed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_on_remove (LrgRelicDef *self,
                               gpointer     context);

/**
 * lrg_relic_def_on_combat_start:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 *
 * Called at combat start.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_on_combat_start (LrgRelicDef *self,
                                     gpointer     context);

/**
 * lrg_relic_def_on_combat_end:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 * @victory: whether the combat was won
 *
 * Called at combat end.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_on_combat_end (LrgRelicDef *self,
                                   gpointer     context,
                                   gboolean     victory);

/**
 * lrg_relic_def_on_turn_start:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 * @turn: current turn number
 *
 * Called at turn start.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_on_turn_start (LrgRelicDef *self,
                                   gpointer     context,
                                   gint         turn);

/**
 * lrg_relic_def_on_turn_end:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 * @turn: current turn number
 *
 * Called at turn end.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_on_turn_end (LrgRelicDef *self,
                                 gpointer     context,
                                 gint         turn);

/**
 * lrg_relic_def_on_card_played:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 * @card: (nullable): the card that was played
 *
 * Called when a card is played.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_def_on_card_played (LrgRelicDef *self,
                                    gpointer     context,
                                    gpointer     card);

/**
 * lrg_relic_def_modify_damage_dealt:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 * @base_damage: base damage amount
 * @target: (nullable): damage target
 *
 * Modifies outgoing damage.
 *
 * Returns: modified damage
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_relic_def_modify_damage_dealt (LrgRelicDef *self,
                                         gpointer     context,
                                         gint         base_damage,
                                         gpointer     target);

/**
 * lrg_relic_def_modify_damage_received:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 * @base_damage: base damage amount
 * @source: (nullable): damage source
 *
 * Modifies incoming damage.
 *
 * Returns: modified damage
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_relic_def_modify_damage_received (LrgRelicDef *self,
                                            gpointer     context,
                                            gint         base_damage,
                                            gpointer     source);

/**
 * lrg_relic_def_modify_block_gained:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 * @base_block: base block amount
 *
 * Modifies block gained.
 *
 * Returns: modified block
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_relic_def_modify_block_gained (LrgRelicDef *self,
                                         gpointer     context,
                                         gint         base_block);

/**
 * lrg_relic_def_modify_heal:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 * @base_heal: base heal amount
 *
 * Modifies healing.
 *
 * Returns: modified heal
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_relic_def_modify_heal (LrgRelicDef *self,
                                 gpointer     context,
                                 gint         base_heal);

/**
 * lrg_relic_def_modify_gold_gained:
 * @self: a #LrgRelicDef
 * @context: (nullable): combat context
 * @base_gold: base gold amount
 *
 * Modifies gold gained.
 *
 * Returns: modified gold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_relic_def_modify_gold_gained (LrgRelicDef *self,
                                        gpointer     context,
                                        gint         base_gold);

/**
 * lrg_relic_def_get_tooltip:
 * @self: a #LrgRelicDef
 * @context: (nullable): game context
 *
 * Gets the relic's tooltip text.
 *
 * Returns: (transfer full): tooltip text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_relic_def_get_tooltip (LrgRelicDef *self,
                                    gpointer     context);

G_END_DECLS
