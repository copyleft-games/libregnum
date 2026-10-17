/* lrg-quest-tracker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgQuestTracker - the quests a player follows on the HUD, in the
 * order they were tracked, with a limit (the "tracked quests" of a
 * quest log: the HUD tracker lists them, the world guidance points at
 * the first).  Quests are named by string ids the game chooses, so it
 * works for #LrgQuestLog's quests and for any other quest system.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "lrg-version.h"
#include "lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_QUEST_TRACKER_DEFAULT_MAX:
 *
 * The limit of a tracker made with a limit of 0.
 */
#define LRG_QUEST_TRACKER_DEFAULT_MAX (5)

/**
 * LRG_QUEST_TRACKER_MAX_LIMIT:
 *
 * The highest limit a tracker accepts.
 */
#define LRG_QUEST_TRACKER_MAX_LIMIT (64)

#define LRG_TYPE_QUEST_TRACKER (lrg_quest_tracker_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgQuestTracker, lrg_quest_tracker, LRG, QUEST_TRACKER, GObject)
#pragma GCC visibility pop

/**
 * LrgQuestTracker:
 *
 * An ordered set of tracked quest ids, at most #LrgQuestTracker:max-tracked.
 *
 * Signals: #LrgQuestTracker::changed after every change of the set or
 * its order.
 */

/**
 * LrgQuestTracker::changed:
 * @self: the tracker
 * @quest_id: (nullable): the quest tracked or untracked, %NULL when the
 *   whole set changed (cleared, restored, reordered, the limit lowered)
 * @tracked: %TRUE when @quest_id is now tracked
 */

/**
 * lrg_quest_tracker_new:
 * @max_tracked: the limit, 0 for %LRG_QUEST_TRACKER_DEFAULT_MAX
 *   (clamped to 1 .. %LRG_QUEST_TRACKER_MAX_LIMIT)
 *
 * Returns: (transfer full): an empty tracker
 */
LRG_AVAILABLE_IN_ALL
LrgQuestTracker *
lrg_quest_tracker_new (guint max_tracked);

/**
 * lrg_quest_tracker_get_max_tracked:
 * @self: an #LrgQuestTracker
 *
 * Returns: the limit
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_quest_tracker_get_max_tracked (LrgQuestTracker *self);

/**
 * lrg_quest_tracker_set_max_tracked:
 * @self: an #LrgQuestTracker
 * @max_tracked: the new limit (clamped to 1 .. %LRG_QUEST_TRACKER_MAX_LIMIT)
 *
 * A lower limit untracks the most recently tracked quests beyond it.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_quest_tracker_set_max_tracked (LrgQuestTracker *self,
                                   guint            max_tracked);

/**
 * lrg_quest_tracker_track:
 * @self: an #LrgQuestTracker
 * @quest_id: a quest id
 *
 * Tracks @quest_id after the others.
 *
 * Returns: %TRUE when it was not tracked and there was room; %FALSE for
 *   an empty id, a tracked one or a full tracker
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_quest_tracker_track (LrgQuestTracker *self,
                         const gchar     *quest_id);

/**
 * lrg_quest_tracker_untrack:
 * @self: an #LrgQuestTracker
 * @quest_id: a quest id
 *
 * Returns: %TRUE when @quest_id was tracked
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_quest_tracker_untrack (LrgQuestTracker *self,
                           const gchar     *quest_id);

/**
 * lrg_quest_tracker_set_tracked:
 * @self: an #LrgQuestTracker
 * @quest_id: a quest id
 * @tracked: whether to track it
 *
 * Returns: %TRUE when the set changed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_quest_tracker_set_tracked (LrgQuestTracker *self,
                               const gchar     *quest_id,
                               gboolean         tracked);

/**
 * lrg_quest_tracker_is_tracked:
 * @self: an #LrgQuestTracker
 * @quest_id: (nullable): a quest id
 *
 * Returns: %TRUE when @quest_id is tracked
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_quest_tracker_is_tracked (LrgQuestTracker *self,
                              const gchar     *quest_id);

/**
 * lrg_quest_tracker_get_index:
 * @self: an #LrgQuestTracker
 * @quest_id: (nullable): a quest id
 *
 * Returns: the position of @quest_id in the tracking order, -1 when it
 *   is not tracked
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_quest_tracker_get_index (LrgQuestTracker *self,
                             const gchar     *quest_id);

/**
 * lrg_quest_tracker_move:
 * @self: an #LrgQuestTracker
 * @quest_id: a tracked quest id
 * @index: its new position (clamped to the end)
 *
 * Moves a tracked quest (the first is the one the world guidance
 * follows).
 *
 * Returns: %TRUE when @quest_id is tracked
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_quest_tracker_move (LrgQuestTracker *self,
                        const gchar     *quest_id,
                        guint            index);

/**
 * lrg_quest_tracker_get_count:
 * @self: an #LrgQuestTracker
 *
 * Returns: how many quests are tracked
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_quest_tracker_get_count (LrgQuestTracker *self);

/**
 * lrg_quest_tracker_get_ids:
 * @self: an #LrgQuestTracker
 *
 * Returns: (transfer full) (array zero-terminated=1): the tracked ids in
 *   tracking order
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_quest_tracker_get_ids (LrgQuestTracker *self);

/**
 * lrg_quest_tracker_clear:
 * @self: an #LrgQuestTracker
 *
 * Untracks everything.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_quest_tracker_clear (LrgQuestTracker *self);

/**
 * lrg_quest_tracker_to_variant:
 * @self: an #LrgQuestTracker
 *
 * Returns: (transfer floating): the ids in order, type "as"
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_quest_tracker_to_variant (LrgQuestTracker *self);

/**
 * lrg_quest_tracker_from_variant:
 * @self: an #LrgQuestTracker
 * @variant: what lrg_quest_tracker_to_variant() wrote
 * @error: return location for a #GError
 *
 * Replaces the set.  Empty and repeated ids are skipped; ids beyond the
 * limit are dropped.  A variant of another type is
 * %LRG_PROGRESSION_ERROR_INVALID and leaves the set unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_quest_tracker_from_variant (LrgQuestTracker  *self,
                                GVariant         *variant,
                                GError          **error);

G_END_DECLS
