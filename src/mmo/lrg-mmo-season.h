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
G_END_DECLS
