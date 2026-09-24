/* lrg-quest-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Quest definition containing stages and rewards.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "lrg-version.h"
#include "lrg-enums.h"
#include "quest/lrg-quest-objective.h"

G_BEGIN_DECLS

/**
 * LRG_QUEST_MAX_STAGE_OBJECTIVES:
 *
 * Maximum number of parallel objectives in a single quest stage. This is
 * also the bound enforced when restoring a #LrgQuestLog snapshot.
 */
#define LRG_QUEST_MAX_STAGE_OBJECTIVES (16)

#define LRG_TYPE_QUEST_DEF (lrg_quest_def_get_type ())

#pragma GCC visibility push(default)

G_DECLARE_DERIVABLE_TYPE (LrgQuestDef, lrg_quest_def, LRG, QUEST_DEF, GObject)

/**
 * LrgQuestDefClass:
 * @parent_class: Parent class
 * @check_prerequisites: Virtual method to check if prerequisites are met.
 *   The default implementation treats a #LrgQuestLog @player specially:
 *   it passes iff every prerequisite is completed in that log and no
 *   exclusive quest is active or completed there. For any other @player
 *   (including %NULL) it passes only when there are no prerequisites.
 * @grant_rewards: Virtual method to grant quest rewards
 *
 * Class structure for #LrgQuestDef.
 */
struct _LrgQuestDefClass
{
    GObjectClass parent_class;

    gboolean (*check_prerequisites) (LrgQuestDef *self,
                                     gpointer     player);
    void     (*grant_rewards)       (LrgQuestDef *self,
                                     gpointer     player);

    gpointer _reserved[8];
};

/**
 * lrg_quest_def_new:
 * @id: unique identifier for the quest
 *
 * Creates a new quest definition.
 *
 * Returns: (transfer full): A new #LrgQuestDef
 */
LrgQuestDef       *lrg_quest_def_new                  (const gchar   *id);

/**
 * lrg_quest_def_get_id:
 * @self: an #LrgQuestDef
 *
 * Gets the quest ID.
 *
 * Returns: (transfer none): The quest ID
 */
const gchar       *lrg_quest_def_get_id               (LrgQuestDef *self);

/**
 * lrg_quest_def_get_name:
 * @self: an #LrgQuestDef
 *
 * Gets the quest name.
 *
 * Returns: (transfer none) (nullable): The quest name
 */
const gchar       *lrg_quest_def_get_name             (LrgQuestDef *self);

/**
 * lrg_quest_def_set_name:
 * @self: an #LrgQuestDef
 * @name: (nullable): quest name
 *
 * Sets the quest name.
 */
void               lrg_quest_def_set_name             (LrgQuestDef *self,
                                                       const gchar *name);

/**
 * lrg_quest_def_get_description:
 * @self: an #LrgQuestDef
 *
 * Gets the quest description.
 *
 * Returns: (transfer none) (nullable): The description
 */
const gchar       *lrg_quest_def_get_description      (LrgQuestDef *self);

/**
 * lrg_quest_def_set_description:
 * @self: an #LrgQuestDef
 * @description: (nullable): quest description
 *
 * Sets the quest description.
 */
void               lrg_quest_def_set_description      (LrgQuestDef *self,
                                                       const gchar *description);

/**
 * lrg_quest_def_get_giver_npc:
 * @self: an #LrgQuestDef
 *
 * Gets the quest giver NPC ID.
 *
 * Returns: (transfer none) (nullable): The giver NPC ID
 */
const gchar       *lrg_quest_def_get_giver_npc        (LrgQuestDef *self);

/**
 * lrg_quest_def_set_giver_npc:
 * @self: an #LrgQuestDef
 * @npc_id: (nullable): NPC ID
 *
 * Sets the quest giver NPC.
 */
void               lrg_quest_def_set_giver_npc        (LrgQuestDef *self,
                                                       const gchar *npc_id);

/**
 * lrg_quest_def_add_stage:
 * @self: an #LrgQuestDef
 * @objective: (transfer full): objective to add as a stage
 *
 * Adds a stage (objective) to the quest.
 */
void               lrg_quest_def_add_stage            (LrgQuestDef       *self,
                                                       LrgQuestObjective *objective);

/**
 * lrg_quest_def_get_stages:
 * @self: an #LrgQuestDef
 *
 * Gets all quest stages.
 *
 * Returns: (transfer none) (element-type LrgQuestObjective): Array of stages
 */
GPtrArray         *lrg_quest_def_get_stages           (LrgQuestDef *self);

/**
 * lrg_quest_def_get_stage_count:
 * @self: an #LrgQuestDef
 *
 * Gets the number of stages.
 *
 * Returns: Stage count
 */
guint              lrg_quest_def_get_stage_count      (LrgQuestDef *self);

/**
 * lrg_quest_def_get_stage:
 * @self: an #LrgQuestDef
 * @index: stage index
 *
 * Gets a stage by index.
 *
 * Returns: (transfer none) (nullable): The stage objective
 */
LrgQuestObjective *lrg_quest_def_get_stage            (LrgQuestDef *self,
                                                       guint        index);

/**
 * lrg_quest_def_add_prerequisite:
 * @self: an #LrgQuestDef
 * @quest_id: prerequisite quest ID
 *
 * Adds a prerequisite quest that must be completed first.
 */
void               lrg_quest_def_add_prerequisite     (LrgQuestDef *self,
                                                       const gchar *quest_id);

/**
 * lrg_quest_def_get_prerequisites:
 * @self: an #LrgQuestDef
 *
 * Gets all prerequisite quest IDs.
 *
 * Returns: (transfer none) (element-type utf8): Array of quest IDs
 */
GPtrArray         *lrg_quest_def_get_prerequisites    (LrgQuestDef *self);

/**
 * lrg_quest_def_set_reward_gold:
 * @self: an #LrgQuestDef
 * @gold: gold reward amount
 *
 * Sets the gold reward.
 */
void               lrg_quest_def_set_reward_gold      (LrgQuestDef *self,
                                                       gint         gold);

/**
 * lrg_quest_def_get_reward_gold:
 * @self: an #LrgQuestDef
 *
 * Gets the gold reward.
 *
 * Returns: Gold amount
 */
gint               lrg_quest_def_get_reward_gold      (LrgQuestDef *self);

/**
 * lrg_quest_def_set_reward_xp:
 * @self: an #LrgQuestDef
 * @xp: experience reward amount
 *
 * Sets the experience reward.
 */
void               lrg_quest_def_set_reward_xp        (LrgQuestDef *self,
                                                       gint         xp);

/**
 * lrg_quest_def_get_reward_xp:
 * @self: an #LrgQuestDef
 *
 * Gets the experience reward.
 *
 * Returns: XP amount
 */
gint               lrg_quest_def_get_reward_xp        (LrgQuestDef *self);

/**
 * lrg_quest_def_add_reward_item:
 * @self: an #LrgQuestDef
 * @item_id: item ID to reward
 * @count: number of items
 *
 * Adds an item reward.
 */
void               lrg_quest_def_add_reward_item      (LrgQuestDef *self,
                                                       const gchar *item_id,
                                                       guint        count);

/**
 * lrg_quest_def_get_reward_items:
 * @self: an #LrgQuestDef
 *
 * Gets all item rewards as a hash table (item_id -> count).
 *
 * Returns: (transfer none): Hash table of item rewards
 */
GHashTable        *lrg_quest_def_get_reward_items     (LrgQuestDef *self);

/**
 * lrg_quest_def_check_prerequisites:
 * @self: an #LrgQuestDef
 * @player: (nullable): player context; %NULL, a #GObject or another
 *   #GTypeInstance (the default implementation type-checks it)
 *
 * Checks if all prerequisites are met.
 *
 * The default implementation returns %TRUE when @player is a
 * #LrgQuestLog in which every prerequisite quest is completed and no
 * exclusive quest (see lrg_quest_def_add_exclusive()) is active or
 * completed. For every other @player value it keeps the historical
 * behaviour and returns %TRUE only when the quest has no prerequisites.
 *
 * Returns: %TRUE if prerequisites are satisfied
 */
gboolean           lrg_quest_def_check_prerequisites  (LrgQuestDef *self,
                                                       gpointer     player);

/**
 * lrg_quest_def_grant_rewards:
 * @self: an #LrgQuestDef
 * @player: (nullable): player to grant rewards to
 *
 * Grants all quest rewards.
 */
void               lrg_quest_def_grant_rewards        (LrgQuestDef *self,
                                                       gpointer     player);

/**
 * lrg_quest_def_get_min_level:
 * @self: an #LrgQuestDef
 *
 * Gets the minimum character level needed to start the quest.
 *
 * Returns: the minimum level, 0 for none
 */
LRG_AVAILABLE_IN_ALL
guint              lrg_quest_def_get_min_level        (LrgQuestDef *self);

/**
 * lrg_quest_def_set_min_level:
 * @self: an #LrgQuestDef
 * @min_level: minimum level, 0 for none
 *
 * Sets the minimum character level needed to start the quest.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_quest_def_set_min_level        (LrgQuestDef *self,
                                                       guint        min_level);

/**
 * lrg_quest_def_get_repeat:
 * @self: an #LrgQuestDef
 *
 * Gets how often the quest may be completed again.
 *
 * Returns: the repeat cadence
 */
LRG_AVAILABLE_IN_ALL
LrgQuestRepeat     lrg_quest_def_get_repeat           (LrgQuestDef *self);

/**
 * lrg_quest_def_set_repeat:
 * @self: an #LrgQuestDef
 * @repeat: repeat cadence
 *
 * Sets how often the quest may be completed again.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_quest_def_set_repeat           (LrgQuestDef    *self,
                                                       LrgQuestRepeat  repeat);

/**
 * lrg_quest_def_get_category:
 * @self: an #LrgQuestDef
 *
 * Gets the game-defined category, for example "story", "daily" or "class".
 *
 * Returns: (transfer none) (nullable): the category
 */
LRG_AVAILABLE_IN_ALL
const gchar       *lrg_quest_def_get_category         (LrgQuestDef *self);

/**
 * lrg_quest_def_set_category:
 * @self: an #LrgQuestDef
 * @category: (nullable): category name
 *
 * Sets the game-defined category.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_quest_def_set_category         (LrgQuestDef *self,
                                                       const gchar *category);

/**
 * lrg_quest_def_get_chain_id:
 * @self: an #LrgQuestDef
 *
 * Gets the identifier of the #LrgQuestChain this quest belongs to.
 *
 * Returns: (transfer none) (nullable): the chain ID
 */
LRG_AVAILABLE_IN_ALL
const gchar       *lrg_quest_def_get_chain_id         (LrgQuestDef *self);

/**
 * lrg_quest_def_set_chain_id:
 * @self: an #LrgQuestDef
 * @chain_id: (nullable): chain ID
 *
 * Sets the identifier of the chain this quest belongs to.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_quest_def_set_chain_id         (LrgQuestDef *self,
                                                       const gchar *chain_id);

/**
 * lrg_quest_def_get_zone:
 * @self: an #LrgQuestDef
 *
 * Gets the zone the quest is offered in.
 *
 * Returns: (transfer none) (nullable): the zone ID
 */
LRG_AVAILABLE_IN_ALL
const gchar       *lrg_quest_def_get_zone             (LrgQuestDef *self);

/**
 * lrg_quest_def_set_zone:
 * @self: an #LrgQuestDef
 * @zone: (nullable): zone ID
 *
 * Sets the zone the quest is offered in.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_quest_def_set_zone             (LrgQuestDef *self,
                                                       const gchar *zone);

/**
 * lrg_quest_def_add_exclusive:
 * @self: an #LrgQuestDef
 * @quest_id: ID of a quest that excludes this one
 *
 * Declares a mutually exclusive quest: this quest cannot be started while
 * @quest_id is active or once it has been completed. Adding the same ID
 * twice has no effect.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_quest_def_add_exclusive        (LrgQuestDef *self,
                                                       const gchar *quest_id);

/**
 * lrg_quest_def_get_exclusives:
 * @self: an #LrgQuestDef
 *
 * Gets the exclusive quest IDs in insertion order.
 *
 * Returns: (transfer none) (element-type utf8): exclusive quest IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray         *lrg_quest_def_get_exclusives       (LrgQuestDef *self);

/**
 * lrg_quest_def_add_stage_objective:
 * @self: an #LrgQuestDef
 * @stage: index of an existing stage
 * @objective: (transfer full): extra objective for the stage
 *
 * Adds a parallel objective to an existing stage. The objective passed to
 * lrg_quest_def_add_stage() is objective 0 of that stage; objectives added
 * here follow it in order. A stage completes only once every one of its
 * objectives is complete.
 *
 * The objective is always consumed. It is freed and %FALSE is returned
 * when @stage does not exist, when the stage already holds
 * %LRG_QUEST_MAX_STAGE_OBJECTIVES objectives, or when an objective with
 * the same ID already exists in the stage.
 *
 * Returns: %TRUE if the objective was added
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_quest_def_add_stage_objective  (LrgQuestDef       *self,
                                                       guint              stage,
                                                       LrgQuestObjective *objective);

/**
 * lrg_quest_def_get_stage_objectives:
 * @self: an #LrgQuestDef
 * @stage: stage index
 *
 * Gets every objective of a stage, objective 0 first.
 *
 * Returns: (transfer none) (nullable) (element-type LrgQuestObjective): the
 *   stage objectives, or %NULL if @stage is out of range
 */
LRG_AVAILABLE_IN_ALL
GPtrArray         *lrg_quest_def_get_stage_objectives (LrgQuestDef *self,
                                                       guint        stage);

/**
 * lrg_quest_def_get_stage_objective_count:
 * @self: an #LrgQuestDef
 * @stage: stage index
 *
 * Gets the number of objectives in a stage.
 *
 * Returns: the objective count, 0 if @stage is out of range
 */
LRG_AVAILABLE_IN_ALL
guint              lrg_quest_def_get_stage_objective_count (LrgQuestDef *self,
                                                            guint        stage);

#pragma GCC visibility pop

G_END_DECLS
