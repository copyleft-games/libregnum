/* lrg-player-profile.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPlayerProfile - Persistent player progress data.
 *
 * Tracks meta-progression across runs:
 * - Character unlocks and progress
 * - Card/relic/joker unlocks
 * - Ascension levels per character
 * - Statistics and achievements
 * - Challenge mode completions
 *
 * Implements LrgSaveable for persistence.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../lrg-types.h"
#include "../save/lrg-saveable.h"

G_BEGIN_DECLS

#define LRG_TYPE_PLAYER_PROFILE (lrg_player_profile_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPlayerProfile, lrg_player_profile, LRG, PLAYER_PROFILE, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_player_profile_new:
 * @profile_name: (nullable): profile name
 *
 * Creates a new player profile.
 *
 * Returns: (transfer full): a new #LrgPlayerProfile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlayerProfile * lrg_player_profile_new (const gchar *profile_name);

/**
 * lrg_player_profile_get_default:
 *
 * Gets the default player profile singleton.
 *
 * Returns: (transfer none): the default #LrgPlayerProfile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlayerProfile * lrg_player_profile_get_default (void);

/* ==========================================================================
 * Profile Info
 * ========================================================================== */

/**
 * lrg_player_profile_get_name:
 * @self: a #LrgPlayerProfile
 *
 * Gets the profile name.
 *
 * Returns: (transfer none) (nullable): profile name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_player_profile_get_name (LrgPlayerProfile *self);

/**
 * lrg_player_profile_set_name:
 * @self: a #LrgPlayerProfile
 * @name: (nullable): new profile name
 *
 * Sets the profile name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_set_name (LrgPlayerProfile *self,
                                   const gchar      *name);

/**
 * lrg_player_profile_get_total_playtime:
 * @self: a #LrgPlayerProfile
 *
 * Gets total playtime in seconds.
 *
 * Returns: total playtime
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_player_profile_get_total_playtime (LrgPlayerProfile *self);

/**
 * lrg_player_profile_add_playtime:
 * @self: a #LrgPlayerProfile
 * @seconds: seconds to add
 *
 * Adds to total playtime.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_add_playtime (LrgPlayerProfile *self,
                                       gint64            seconds);

/* ==========================================================================
 * Unlock Tracking
 * ========================================================================== */

/**
 * lrg_player_profile_is_unlocked:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 * @id: ID of the item
 *
 * Checks if an item is unlocked.
 *
 * Returns: %TRUE if unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_player_profile_is_unlocked (LrgPlayerProfile *self,
                                          LrgUnlockType     unlock_type,
                                          const gchar      *id);

/**
 * lrg_player_profile_unlock:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 * @id: ID of the item to unlock
 *
 * Unlocks an item.
 *
 * Returns: %TRUE if newly unlocked, %FALSE if already unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_player_profile_unlock (LrgPlayerProfile *self,
                                     LrgUnlockType     unlock_type,
                                     const gchar      *id);

/**
 * lrg_player_profile_get_unlock_status:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 * @id: ID of the item
 *
 * Gets the unlock status of an item.
 *
 * Returns: the #LrgUnlockStatus
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgUnlockStatus lrg_player_profile_get_unlock_status (LrgPlayerProfile *self,
                                                       LrgUnlockType     unlock_type,
                                                       const gchar      *id);

/**
 * lrg_player_profile_mark_seen:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 * @id: ID of the item
 *
 * Marks a newly unlocked item as seen (removes "NEW" badge).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_mark_seen (LrgPlayerProfile *self,
                                    LrgUnlockType     unlock_type,
                                    const gchar      *id);

/**
 * lrg_player_profile_get_unlocked_ids:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 *
 * Gets all unlocked IDs of a type.
 *
 * Returns: (transfer full) (element-type utf8): array of unlocked IDs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_player_profile_get_unlocked_ids (LrgPlayerProfile *self,
                                                  LrgUnlockType     unlock_type);

/* ==========================================================================
 * Character Progress
 * ========================================================================== */

/**
 * lrg_player_profile_get_character_wins:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Gets number of wins with a character.
 *
 * Returns: win count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_player_profile_get_character_wins (LrgPlayerProfile *self,
                                             const gchar      *character_id);

/**
 * lrg_player_profile_add_character_win:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Records a win with a character.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_add_character_win (LrgPlayerProfile *self,
                                            const gchar      *character_id);

/**
 * lrg_player_profile_get_character_runs:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Gets total number of runs with a character.
 *
 * Returns: run count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_player_profile_get_character_runs (LrgPlayerProfile *self,
                                             const gchar      *character_id);

/**
 * lrg_player_profile_add_character_run:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Records a run attempt with a character.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_add_character_run (LrgPlayerProfile *self,
                                            const gchar      *character_id);

/* ==========================================================================
 * Ascension Progress
 * ========================================================================== */

/**
 * lrg_player_profile_get_max_ascension:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Gets the maximum unlocked ascension level for a character.
 *
 * Returns: max ascension level (0 = none)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_player_profile_get_max_ascension (LrgPlayerProfile *self,
                                            const gchar      *character_id);

/**
 * lrg_player_profile_set_max_ascension:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 * @level: ascension level
 *
 * Sets the maximum unlocked ascension level.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_set_max_ascension (LrgPlayerProfile *self,
                                            const gchar      *character_id,
                                            gint              level);

/**
 * lrg_player_profile_unlock_next_ascension:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Unlocks the next ascension level for a character.
 *
 * Returns: the newly unlocked level, or -1 if at max
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_player_profile_unlock_next_ascension (LrgPlayerProfile *self,
                                                const gchar      *character_id);

/* ==========================================================================
 * Statistics
 * ========================================================================== */

/**
 * lrg_player_profile_get_stat:
 * @self: a #LrgPlayerProfile
 * @stat_name: statistic name
 *
 * Gets a statistic value.
 *
 * Returns: the statistic value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_player_profile_get_stat (LrgPlayerProfile *self,
                                     const gchar      *stat_name);

/**
 * lrg_player_profile_set_stat:
 * @self: a #LrgPlayerProfile
 * @stat_name: statistic name
 * @value: new value
 *
 * Sets a statistic value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_set_stat (LrgPlayerProfile *self,
                                   const gchar      *stat_name,
                                   gint64            value);

/**
 * lrg_player_profile_increment_stat:
 * @self: a #LrgPlayerProfile
 * @stat_name: statistic name
 * @amount: amount to add
 *
 * Increments a statistic.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_increment_stat (LrgPlayerProfile *self,
                                         const gchar      *stat_name,
                                         gint64            amount);

/**
 * lrg_player_profile_get_total_wins:
 * @self: a #LrgPlayerProfile
 *
 * Gets total number of wins across all characters.
 *
 * Returns: total wins
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_player_profile_get_total_wins (LrgPlayerProfile *self);

/**
 * lrg_player_profile_get_total_runs:
 * @self: a #LrgPlayerProfile
 *
 * Gets total number of runs across all characters.
 *
 * Returns: total runs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_player_profile_get_total_runs (LrgPlayerProfile *self);

/* ==========================================================================
 * High Scores
 * ========================================================================== */

/**
 * lrg_player_profile_get_high_score:
 * @self: a #LrgPlayerProfile
 * @character_id: (nullable): character ID, or NULL for global
 *
 * Gets the high score for a character or globally.
 *
 * Returns: high score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_player_profile_get_high_score (LrgPlayerProfile *self,
                                           const gchar      *character_id);

/**
 * lrg_player_profile_submit_score:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 * @score: the score
 *
 * Submits a score, updating high score if applicable.
 *
 * Returns: %TRUE if this was a new high score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_player_profile_submit_score (LrgPlayerProfile *self,
                                           const gchar      *character_id,
                                           gint64            score);

/* ==========================================================================
 * Persistence
 * ========================================================================== */

/**
 * lrg_player_profile_reset:
 * @self: a #LrgPlayerProfile
 *
 * Resets all progress (dangerous!).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_reset (LrgPlayerProfile *self);

/**
 * lrg_player_profile_is_dirty:
 * @self: a #LrgPlayerProfile
 *
 * Checks if the profile has unsaved changes.
 *
 * Returns: %TRUE if there are unsaved changes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_player_profile_is_dirty (LrgPlayerProfile *self);

/**
 * lrg_player_profile_mark_clean:
 * @self: a #LrgPlayerProfile
 *
 * Marks the profile as saved (no unsaved changes).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_player_profile_mark_clean (LrgPlayerProfile *self);

G_END_DECLS
