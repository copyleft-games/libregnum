/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include "lrg-mmo-store.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_SEASON (lrg_mmo_season_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoSeason, lrg_mmo_season, LRG, MMO_SEASON, GObject)
/**
 * lrg_mmo_season_new:
 * @store: durable storage
 *
 * Thread-confined trusted match-result service. Never expose result submission
 * directly to players. Seasons use three points per win and one per draw.
 * Returns: (transfer full): service
 */
LRG_AVAILABLE_IN_ALL
LrgMmoSeason *lrg_mmo_season_new (LrgMmoStore *store);
/**
 * lrg_mmo_season_create:
 * @self: service
 * @season: unique season identifier
 * @start: inclusive Unix seconds
 * @end: exclusive Unix seconds, greater than start
 * @capacity: maximum entrants, 2 to 1000
 * @error: (nullable): error return
 * Returns: whether created
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_season_create (LrgMmoSeason *self, const gchar *season, gint64 start,
                                 gint64 end, guint capacity, GError **error);
/**
 * lrg_mmo_season_record:
 * @self: trusted service
 * @season: season identifier
 * @first: first authenticated participant
 * @second: other participant
 * @result: 0 for draw, 1 for first win, 2 for second win
 * @match: globally unique authoritative match ID, stable on retries
 * @now: trusted server Unix seconds
 * @error: (nullable): error return
 *
 * Atomically applies both results and a semantic retry receipt. An exact retry
 * succeeds even after closure. Reusing a match ID for different results rejects.
 * Entrants are admitted lazily up to capacity. Races return CAS conflicts for retry.
 * Returns: whether applied or recognized
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_season_record (LrgMmoSeason *self, const gchar *season, const gchar *first,
                                 const gchar *second, guint result, const gchar *match,
                                 gint64 now, GError **error);
/**
 * lrg_mmo_season_standings:
 * @self: service
 * @season: season identifier
 * @error: (nullable): error return
 *
 * Returns points descending, then wins descending, then account ID ascending.
 * Ended seasons cannot accept new results; retained standings are immutable.
 * Returns: (transfer full) (nullable): a(suuu) account, wins, losses, draws
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_season_standings (LrgMmoSeason *self, const gchar *season, GError **error);
/**
 * LRG_MMO_SEASON_MAX_TEAM:
 *
 * The most members one side of a team result may have.
 */
#define LRG_MMO_SEASON_MAX_TEAM (64)

/**
 * lrg_mmo_season_create_rated:
 * @self: service
 * @season: unique season identifier
 * @start: inclusive Unix seconds
 * @end: exclusive Unix seconds, greater than start
 * @capacity: maximum entrants, 2 to 1000
 * @base_rating: the rating of a new entrant, 1 to 100000
 * @k_factor: the Elo K factor (the most one result moves a rating), 1 to 1000
 * @error: (nullable): error return
 *
 * Creates a season like lrg_mmo_season_create() that also keeps an Elo
 * rating per entrant (record "season-rating/<season>", created in the same
 * transaction). Every result recorded afterwards moves the ratings: each side's
 * expected score is 1 / (1 + 10^((other - own) / 400)) of the sides' mean
 * ratings, and every member of a side moves by round(K * (score - expected)),
 * never below zero. Ratings start at @base_rating in every season, so a new
 * season resets them.
 * Returns: whether created
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_season_create_rated (LrgMmoSeason *self, const gchar *season, gint64 start,
                                      gint64 end, guint capacity, guint base_rating,
                                      guint k_factor, GError **error);
/**
 * lrg_mmo_season_record_teams:
 * @self: trusted service
 * @season: season identifier
 * @first: (array zero-terminated=1): the authenticated members of the first side
 * @second: (array zero-terminated=1): the members of the other side
 * @result: 0 for draw, 1 for first win, 2 for second win
 * @match: globally unique authoritative match ID, stable on retries
 * @now: trusted server Unix seconds
 * @error: (nullable): error return
 *
 * Applies one team result atomically: every member's wins, losses or draws,
 * the ratings of a rated season (sides by their mean rating), and the semantic
 * retry receipt. Sides hold 1 to %LRG_MMO_SEASON_MAX_TEAM distinct accounts and
 * share none. Retries, closure and capacity behave as lrg_mmo_season_record().
 * Returns: whether applied or recognized
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_season_record_teams (LrgMmoSeason *self, const gchar *season,
                                      const gchar * const *first, const gchar * const *second,
                                      guint result, const gchar *match, gint64 now,
                                      GError **error);
/**
 * lrg_mmo_season_get_rating:
 * @self: service
 * @season: a rated season
 * @account: an account
 * @out_rating: (out): its rating (the base rating before its first result)
 * @out_games: (out) (optional): the results it took part in
 * @error: (nullable): error return
 * Returns: %FALSE when the season is unknown or not rated
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_season_get_rating (LrgMmoSeason *self, const gchar *season, const gchar *account,
                                    guint *out_rating, guint *out_games, GError **error);
/**
 * lrg_mmo_season_ratings:
 * @self: service
 * @season: a rated season
 * @error: (nullable): error return
 *
 * Returns rating descending, then games descending, then account ascending.
 * Returns: (transfer full) (nullable): a(suu) account, rating, games
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_season_ratings (LrgMmoSeason *self, const gchar *season, GError **error);
/**
 * lrg_mmo_season_get_window:
 * @self: service
 * @season: season identifier
 * @out_start: (out) (optional): inclusive Unix seconds
 * @out_end: (out) (optional): exclusive Unix seconds
 * @out_capacity: (out) (optional): maximum entrants
 * @out_rated: (out) (optional): whether the season keeps ratings
 * @error: (nullable): error return
 *
 * Reads a season's interval, so a host can tell an ended season and start
 * the next one.
 * Returns: %FALSE when the season is unknown (%G_IO_ERROR_NOT_FOUND)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_season_get_window (LrgMmoSeason *self, const gchar *season, gint64 *out_start,
                                    gint64 *out_end, guint *out_capacity, gboolean *out_rated,
                                    GError **error);
G_END_DECLS
