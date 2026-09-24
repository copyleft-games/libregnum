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

#include "lrg-version.h"
#include "lrg-enums.h"
#include "core/lrg-reset-schedule.h"
#include "quest/lrg-quest-def.h"
#include "quest/lrg-quest-instance.h"

G_BEGIN_DECLS

/**
 * LRG_QUEST_LOG_MAX_ACTIVE:
 *
 * Maximum number of active quests in a #LrgQuestLog snapshot. The checked
 * start path refuses to exceed it with %LRG_PROGRESSION_ERROR_LIMIT.
 */
#define LRG_QUEST_LOG_MAX_ACTIVE (256)

/**
 * LRG_QUEST_LOG_MAX_COMPLETED:
 *
 * Maximum number of completion records in a #LrgQuestLog snapshot. The
 * checked start path refuses a quest that would need a new record beyond
 * it with %LRG_PROGRESSION_ERROR_LIMIT.
 */
#define LRG_QUEST_LOG_MAX_COMPLETED (8192)

/**
 * LRG_QUEST_LOG_VARIANT_TYPE:
 *
 * GVariant type string of lrg_quest_log_to_variant():
 * (active: array of (quest ID, current stage, per-objective progress of
 * the current stage), completed: array of (quest ID, completion count,
 * last completion Unix seconds)).
 */
#define LRG_QUEST_LOG_VARIANT_TYPE "(a(suau)a(sux))"

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
 * Starts a new quest from a definition. This legacy entry point performs
 * no level, prerequisite, exclusivity or repeat checks and refuses any
 * quest that is active or has a completion record. When the instance
 * completes its last stage it moves to the completed set automatically.
 * Use lrg_quest_log_start_quest_checked() for server-authoritative play.
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
 * Gets the instances of completed quests. Completion records created by
 * lrg_quest_log_mark_completed() have no instance and are not listed;
 * use lrg_quest_log_get_completed_ids() for every completed quest.
 *
 * Returns: (transfer container) (element-type LrgQuestInstance): List of completed quests
 */
GList            *lrg_quest_log_get_completed_quests (LrgQuestLog *self);

/**
 * lrg_quest_log_is_quest_completed:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID
 *
 * Checks if a quest has a completion record (it was completed at least
 * once). A repeatable quest that was restarted is both active and
 * completed.
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
 * Gets the number of quests with a completion record.
 *
 * Returns: Completed quest count
 */
guint             lrg_quest_log_get_completed_count (LrgQuestLog *self);

/**
 * lrg_quest_log_can_start:
 * @self: an #LrgQuestLog
 * @quest_def: quest definition
 * @level: the character's level
 * @schedule: (nullable): reset schedule; may be %NULL only when the quest
 *   is not repeatable
 * @now: trusted server Unix seconds
 * @error: (nullable): return location for a #GError
 *
 * Full availability check. Rules are evaluated in this order and the
 * first failure is reported in the %LRG_PROGRESSION_ERROR domain:
 *
 * 1. the quest is already active: %LRG_PROGRESSION_ERROR_DUPLICATE
 * 2. @level is below #LrgQuestDef:min-level: %LRG_PROGRESSION_ERROR_REQUIREMENT
 * 3. a prerequisite is not completed: %LRG_PROGRESSION_ERROR_REQUIREMENT
 * 4. an exclusive quest is active or completed: %LRG_PROGRESSION_ERROR_REQUIREMENT
 * 5. completed and not repeatable: %LRG_PROGRESSION_ERROR_DUPLICATE
 * 6. completed, repeatable and @schedule is %NULL: %LRG_PROGRESSION_ERROR_INVALID;
 *    completed in the current (or a later) reset period: %LRG_PROGRESSION_ERROR_NOT_READY
 * 7. %LRG_QUEST_LOG_MAX_ACTIVE active quests, or a new completion record
 *    would exceed %LRG_QUEST_LOG_MAX_COMPLETED: %LRG_PROGRESSION_ERROR_LIMIT
 *
 * The #LrgQuestDefClass.check_prerequisites virtual method is not called.
 *
 * Returns: %TRUE if the quest may be started
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_quest_log_can_start           (LrgQuestLog      *self,
                                                     LrgQuestDef      *quest_def,
                                                     guint             level,
                                                     LrgResetSchedule *schedule,
                                                     gint64            now,
                                                     GError          **error);

/**
 * lrg_quest_log_start_quest_checked:
 * @self: an #LrgQuestLog
 * @quest_def: quest definition
 * @level: the character's level
 * @schedule: (nullable): reset schedule, see lrg_quest_log_can_start()
 * @now: trusted server Unix seconds
 * @error: (nullable): return location for a #GError
 *
 * Runs lrg_quest_log_can_start() and starts the quest. Quests started
 * this way stay active in state %LRG_QUEST_STATE_COMPLETE once their last
 * stage finishes (#LrgQuestLog::quest-ready is emitted) until
 * lrg_quest_log_turn_in() is called. A restarted repeatable quest keeps
 * its completion record.
 *
 * Returns: (transfer none) (nullable): the new active instance, or %NULL
 *   with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgQuestInstance *lrg_quest_log_start_quest_checked (LrgQuestLog      *self,
                                                     LrgQuestDef      *quest_def,
                                                     guint             level,
                                                     LrgResetSchedule *schedule,
                                                     gint64            now,
                                                     GError          **error);

/**
 * lrg_quest_log_turn_in:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID
 * @now: trusted server Unix seconds of the turn-in
 * @error: (nullable): return location for a #GError
 *
 * Turns in an active quest whose instance is in state
 * %LRG_QUEST_STATE_COMPLETE: the instance moves to the completed set, the
 * completion count is incremented (saturating) and @now is recorded as
 * the last completion. Rewards are not granted; call
 * lrg_quest_def_grant_rewards() after success.
 *
 * Errors: %LRG_PROGRESSION_ERROR_NOT_FOUND if the quest is not active,
 * %LRG_PROGRESSION_ERROR_REQUIREMENT if it is not complete. The log is
 * unchanged on failure.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_quest_log_turn_in             (LrgQuestLog *self,
                                                     const gchar *quest_id,
                                                     gint64       now,
                                                     GError     **error);

/**
 * lrg_quest_log_get_completion_count:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID
 *
 * Returns: how many times the quest was completed, 0 if never
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_quest_log_get_completion_count (LrgQuestLog *self,
                                                      const gchar *quest_id);

/**
 * lrg_quest_log_get_last_completed:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID
 *
 * Returns: Unix seconds of the last completion, 0 if never completed or unknown
 */
LRG_AVAILABLE_IN_ALL
gint64            lrg_quest_log_get_last_completed   (LrgQuestLog *self,
                                                      const gchar *quest_id);

/**
 * lrg_quest_log_get_completed_ids:
 * @self: an #LrgQuestLog
 *
 * Gets every quest ID with a completion record.
 *
 * Returns: (transfer container) (element-type utf8): IDs sorted with
 *   g_strcmp0(); the strings are owned by the log
 */
LRG_AVAILABLE_IN_ALL
GPtrArray        *lrg_quest_log_get_completed_ids   (LrgQuestLog *self);

/**
 * lrg_quest_log_count_completed_in_period:
 * @self: an #LrgQuestLog
 * @category: (nullable): category filter, %NULL for any
 * @schedule: reset schedule
 * @period: which reset cadence to count within
 * @now: trusted server Unix seconds
 * @defs: (element-type utf8 LrgQuestDef): quest definitions by ID
 *
 * Counts repeatable quests (per @defs) matching @category whose last
 * completion falls in the current @period of @schedule. Records whose
 * last completion is unknown (0) or whose definition is missing are not
 * counted. Used for daily and weekly caps.
 *
 * Returns: the number of matching quests
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_quest_log_count_completed_in_period (LrgQuestLog      *self,
                                                           const gchar      *category,
                                                           LrgResetSchedule *schedule,
                                                           LrgResetPeriod    period,
                                                           gint64            now,
                                                           GHashTable       *defs);

/**
 * lrg_quest_log_mark_completed:
 * @self: an #LrgQuestLog
 * @quest_id: quest definition ID; non-empty UTF-8 of at most 128 bytes
 * @count: completion count, at least 1
 * @last: Unix seconds of the last completion, 0 if unknown
 *
 * Replaces the completion record of a quest directly, for migrations and
 * administrative grants. No instance is created and no signal is emitted
 * other than the #LrgQuestLog:completed-count notification.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_quest_log_mark_completed      (LrgQuestLog *self,
                                                     const gchar *quest_id,
                                                     guint        count,
                                                     gint64       last);

/**
 * lrg_quest_log_to_variant:
 * @self: an #LrgQuestLog
 *
 * Serializes the log as %LRG_QUEST_LOG_VARIANT_TYPE. Both arrays are
 * sorted by quest ID. Each active entry stores the current stage (the
 * stage count when every stage is done and the quest awaits turn-in)
 * and the progress of every objective of that stage, clamped to its
 * target. The tracked quest and instance states are not persisted.
 *
 * Returns: (transfer full): a new non-floating #GVariant
 */
LRG_AVAILABLE_IN_ALL
GVariant         *lrg_quest_log_to_variant          (LrgQuestLog *self);

/**
 * lrg_quest_log_new_from_variant:
 * @variant: a snapshot of type %LRG_QUEST_LOG_VARIANT_TYPE
 * @defs: (element-type utf8 LrgQuestDef): quest definitions by ID
 * @error: (nullable): return location for a #GError
 *
 * Restores a log. The whole snapshot is rejected, without side effects,
 * when any entry is invalid. %LRG_PROGRESSION_ERROR_NOT_FOUND is reported
 * for a quest ID missing from @defs; %LRG_PROGRESSION_ERROR_INVALID for
 * a wrong type string, a non-normal-form variant, more than
 * %LRG_QUEST_LOG_MAX_ACTIVE active or %LRG_QUEST_LOG_MAX_COMPLETED
 * completed entries, an empty, overlong (over 128 bytes) or invalid UTF-8
 * ID, a duplicate ID within either array, a stage beyond the stage count,
 * more than %LRG_QUEST_MAX_STAGE_OBJECTIVES progress values, more values
 * than the stage has objectives, a progress value above its target, or a
 * completion count of 0.
 *
 * Restored active quests are started in the checked (turn-in) mode; an
 * active entry whose stage equals the stage count is restored in state
 * %LRG_QUEST_STATE_COMPLETE, ready for lrg_quest_log_turn_in(). Missing
 * trailing progress values are treated as 0. Completed quests get a
 * completed instance so lrg_quest_log_get_quest() keeps working.
 *
 * Returns: (transfer full) (nullable): the restored log, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgQuestLog      *lrg_quest_log_new_from_variant    (GVariant    *variant,
                                                     GHashTable  *defs,
                                                     GError     **error);

#pragma GCC visibility pop

G_END_DECLS
