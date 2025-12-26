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

#include "quest/lrg-quest-objective.h"

G_BEGIN_DECLS

#define LRG_TYPE_QUEST_DEF (lrg_quest_def_get_type ())

#pragma GCC visibility push(default)

G_DECLARE_DERIVABLE_TYPE (LrgQuestDef, lrg_quest_def, LRG, QUEST_DEF, GObject)

/**
 * LrgQuestDefClass:
 * @parent_class: Parent class
 * @check_prerequisites: Virtual method to check if prerequisites are met
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
 * @player: (nullable): player context
 *
 * Checks if all prerequisites are met.
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

#pragma GCC visibility pop

G_END_DECLS
