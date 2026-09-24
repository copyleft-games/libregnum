/* lrg-quest-chain.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Ordered storyline of quests.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "quest/lrg-quest-def.h"
#include "quest/lrg-quest-log.h"

G_BEGIN_DECLS

#define LRG_TYPE_QUEST_CHAIN (lrg_quest_chain_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgQuestChain, lrg_quest_chain, LRG, QUEST_CHAIN, GObject)

/**
 * lrg_quest_chain_new:
 * @id: unique chain identifier
 * @name: (nullable): display name
 *
 * Creates an empty quest chain. A chain is an ordered list of quest IDs
 * that a character completes one after another, such as a zone
 * storyline or a class quest line. It stores IDs only and never holds
 * references to definitions or logs.
 *
 * Returns: (transfer full): a new #LrgQuestChain
 */
LRG_AVAILABLE_IN_ALL
LrgQuestChain *lrg_quest_chain_new                (const gchar   *id,
                                                   const gchar   *name);

/**
 * lrg_quest_chain_get_id:
 * @self: an #LrgQuestChain
 *
 * Returns: (transfer none): the chain identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar   *lrg_quest_chain_get_id             (LrgQuestChain *self);

/**
 * lrg_quest_chain_get_name:
 * @self: an #LrgQuestChain
 *
 * Returns: (transfer none) (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar   *lrg_quest_chain_get_name           (LrgQuestChain *self);

/**
 * lrg_quest_chain_set_name:
 * @self: an #LrgQuestChain
 * @name: (nullable): display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void           lrg_quest_chain_set_name           (LrgQuestChain *self,
                                                   const gchar   *name);

/**
 * lrg_quest_chain_get_storyline:
 * @self: an #LrgQuestChain
 *
 * Returns: (transfer none) (nullable): the storyline this chain belongs to
 */
LRG_AVAILABLE_IN_ALL
const gchar   *lrg_quest_chain_get_storyline      (LrgQuestChain *self);

/**
 * lrg_quest_chain_set_storyline:
 * @self: an #LrgQuestChain
 * @storyline: (nullable): storyline name, for grouping chains in a UI
 *
 * Sets the storyline.
 */
LRG_AVAILABLE_IN_ALL
void           lrg_quest_chain_set_storyline      (LrgQuestChain *self,
                                                   const gchar   *storyline);

/**
 * lrg_quest_chain_get_description:
 * @self: an #LrgQuestChain
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar   *lrg_quest_chain_get_description    (LrgQuestChain *self);

/**
 * lrg_quest_chain_set_description:
 * @self: an #LrgQuestChain
 * @description: (nullable): description
 *
 * Sets the description.
 */
LRG_AVAILABLE_IN_ALL
void           lrg_quest_chain_set_description    (LrgQuestChain *self,
                                                   const gchar   *description);

/**
 * lrg_quest_chain_add_quest:
 * @self: an #LrgQuestChain
 * @quest_id: quest ID to append
 *
 * Appends a quest to the end of the chain.
 *
 * Returns: %TRUE if added, %FALSE if @quest_id is already in the chain
 */
LRG_AVAILABLE_IN_ALL
gboolean       lrg_quest_chain_add_quest          (LrgQuestChain *self,
                                                   const gchar   *quest_id);

/**
 * lrg_quest_chain_get_quest_ids:
 * @self: an #LrgQuestChain
 *
 * Returns: (transfer none) (element-type utf8): quest IDs in chain order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray     *lrg_quest_chain_get_quest_ids      (LrgQuestChain *self);

/**
 * lrg_quest_chain_get_length:
 * @self: an #LrgQuestChain
 *
 * Returns: the number of quests in the chain
 */
LRG_AVAILABLE_IN_ALL
guint          lrg_quest_chain_get_length         (LrgQuestChain *self);

/**
 * lrg_quest_chain_index_of:
 * @self: an #LrgQuestChain
 * @quest_id: quest ID
 *
 * Returns: the position of @quest_id in the chain, or -1
 */
LRG_AVAILABLE_IN_ALL
gint           lrg_quest_chain_index_of           (LrgQuestChain *self,
                                                   const gchar   *quest_id);

/**
 * lrg_quest_chain_get_next:
 * @self: an #LrgQuestChain
 * @log: the character's quest log
 *
 * Gets the first quest of the chain that @log has not completed. The
 * quest may already be active in @log.
 *
 * Returns: (transfer none) (nullable): the next quest ID, or %NULL when
 *   every quest is completed
 */
LRG_AVAILABLE_IN_ALL
const gchar   *lrg_quest_chain_get_next           (LrgQuestChain *self,
                                                   LrgQuestLog   *log);

/**
 * lrg_quest_chain_get_progress:
 * @self: an #LrgQuestChain
 * @log: the character's quest log
 *
 * Returns: the number of chain quests completed in @log
 */
LRG_AVAILABLE_IN_ALL
guint          lrg_quest_chain_get_progress       (LrgQuestChain *self,
                                                   LrgQuestLog   *log);

/**
 * lrg_quest_chain_is_complete:
 * @self: an #LrgQuestChain
 * @log: the character's quest log
 *
 * Returns: %TRUE if every chain quest is completed in @log (an empty
 *   chain is complete)
 */
LRG_AVAILABLE_IN_ALL
gboolean       lrg_quest_chain_is_complete        (LrgQuestChain *self,
                                                   LrgQuestLog   *log);

/**
 * lrg_quest_chain_link_prerequisites:
 * @self: an #LrgQuestChain
 * @defs: (element-type utf8 LrgQuestDef): quest definitions by ID
 * @error: (nullable): return location for a #GError
 *
 * Authoring helper: for every quest after the first, adds its predecessor
 * in the chain as a prerequisite of its definition (skipping ones that
 * are already present). Every chain quest is looked up first; if any is
 * missing from @defs, %LRG_PROGRESSION_ERROR_NOT_FOUND is reported and no
 * definition is modified.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean       lrg_quest_chain_link_prerequisites (LrgQuestChain *self,
                                                   GHashTable    *defs,
                                                   GError       **error);

G_END_DECLS
