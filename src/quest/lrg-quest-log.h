/* lrg-quest-log.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Quest log for managing active and completed quests.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "quest/lrg-quest-def.h"
#include "quest/lrg-quest-instance.h"

G_BEGIN_DECLS

#define LRG_TYPE_QUEST_LOG (lrg_quest_log_get_type ())

#pragma GCC visibility push(default)

G_DECLARE_FINAL_TYPE (LrgQuestLog, lrg_quest_log, LRG, QUEST_LOG, GObject)

/**
 * lrg_quest_log_new:
 *
 * Creates a new quest log.
 *
 * Returns: (transfer full): A new #LrgQuestLog
 */
LrgQuestLog      *lrg_quest_log_new                 (void);

/**
 * lrg_quest_log_start_quest:
 * @self: an #LrgQuestLog
 * @quest_def: quest definition to start
 *
 * Starts a new quest from a definition.
 *
 * Returns: (transfer none) (nullable): The quest instance, or %NULL on failure
 */
LrgQuestInstance *lrg_quest_log_start_quest         (LrgQuestLog *self,
                                                     LrgQuestDef *quest_def);

/**
 * lrg_quest_log_get_quest:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID
 *
 * Gets a quest instance by its definition ID.
 *
 * Returns: (transfer none) (nullable): The quest instance, or %NULL
 */
LrgQuestInstance *lrg_quest_log_get_quest           (LrgQuestLog *self,
                                                     const gchar *quest_id);

/**
 * lrg_quest_log_get_active_quests:
 * @self: an #LrgQuestLog
 *
 * Gets all active quests.
 *
 * Returns: (transfer container) (element-type LrgQuestInstance): List of active quests
 */
GList            *lrg_quest_log_get_active_quests   (LrgQuestLog *self);

/**
 * lrg_quest_log_get_completed_quests:
 * @self: an #LrgQuestLog
 *
 * Gets all completed quests.
 *
 * Returns: (transfer container) (element-type LrgQuestInstance): List of completed quests
 */
GList            *lrg_quest_log_get_completed_quests (LrgQuestLog *self);

/**
 * lrg_quest_log_is_quest_completed:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID
 *
 * Checks if a quest has been completed.
 *
 * Returns: %TRUE if completed
 */
gboolean          lrg_quest_log_is_quest_completed  (LrgQuestLog *self,
                                                     const gchar *quest_id);

/**
 * lrg_quest_log_is_quest_active:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID
 *
 * Checks if a quest is currently active.
 *
 * Returns: %TRUE if active
 */
gboolean          lrg_quest_log_is_quest_active     (LrgQuestLog *self,
                                                     const gchar *quest_id);

/**
 * lrg_quest_log_get_tracked_quest:
 * @self: an #LrgQuestLog
 *
 * Gets the currently tracked quest for HUD display.
 *
 * Returns: (transfer none) (nullable): The tracked quest
 */
LrgQuestInstance *lrg_quest_log_get_tracked_quest   (LrgQuestLog *self);

/**
 * lrg_quest_log_set_tracked_quest:
 * @self: an #LrgQuestLog
 * @quest: (nullable): quest to track
 *
 * Sets the currently tracked quest.
 */
void              lrg_quest_log_set_tracked_quest   (LrgQuestLog      *self,
                                                     LrgQuestInstance *quest);

/**
 * lrg_quest_log_track_quest:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID to track
 *
 * Tracks a quest by its ID.
 *
 * Returns: %TRUE if quest was found and tracked
 */
gboolean          lrg_quest_log_track_quest         (LrgQuestLog *self,
                                                     const gchar *quest_id);

/**
 * lrg_quest_log_abandon_quest:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID
 *
 * Abandons an active quest.
 *
 * Returns: %TRUE if quest was abandoned
 */
gboolean          lrg_quest_log_abandon_quest       (LrgQuestLog *self,
                                                     const gchar *quest_id);

/**
 * lrg_quest_log_get_active_count:
 * @self: an #LrgQuestLog
 *
 * Gets the number of active quests.
 *
 * Returns: Active quest count
 */
guint             lrg_quest_log_get_active_count    (LrgQuestLog *self);

/**
 * lrg_quest_log_get_completed_count:
 * @self: an #LrgQuestLog
 *
 * Gets the number of completed quests.
 *
 * Returns: Completed quest count
 */
guint             lrg_quest_log_get_completed_count (LrgQuestLog *self);

#pragma GCC visibility pop

G_END_DECLS
