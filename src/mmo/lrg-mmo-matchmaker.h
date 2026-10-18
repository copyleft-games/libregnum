/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"

G_BEGIN_DECLS
#define LRG_TYPE_MMO_MATCHMAKER (lrg_mmo_matchmaker_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoMatchmaker, lrg_mmo_matchmaker, LRG, MMO_MATCHMAKER, GObject)

/**
 * lrg_mmo_matchmaker_new:
 * @capacity: maximum queued accounts
 *
 * Creates an ephemeral FIFO matchmaking queue, owned by one thread.
 *
 * Returns: (transfer full): queue
 */
LRG_AVAILABLE_IN_ALL
LrgMmoMatchmaker *
lrg_mmo_matchmaker_new (guint capacity);

/**
 * lrg_mmo_matchmaker_enqueue:
 * @self: the service; confine to its owning thread
 * @account: authenticated unique account ID
 * @mode: game mode ID
 * @rating: skill rating
 * @now_us: nonnegative monotonic server microseconds
 * @error: (nullable): return location for error
 *
 * Queues one account for up to sixty seconds. Duplicates and capacity overflow fail.
 *
 * Returns: whether queued
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_matchmaker_enqueue (LrgMmoMatchmaker *self,
                            const gchar *account,
                            const gchar *mode,
                            guint rating,
                            gint64 now_us,
                            GError **error);

/**
 * lrg_mmo_matchmaker_cancel:
 * @self: the service; confine to its owning thread
 * @account: account ID
 *
 * Removes the entire ticket containing this account, preserving party integrity.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_matchmaker_cancel (LrgMmoMatchmaker *self,
                           const gchar *account);

/**
 * lrg_mmo_matchmaker_take:
 * @self: the service; confine to its owning thread
 * @mode: game mode ID
 * @size: requested group size, 2 to 128
 * @spread: maximum difference between lowest and highest rating
 * @now_us: monotonic server microseconds
 * @error: (nullable): return location for error
 *
 * Expires old tickets and greedily packs whole FIFO parties within the rating
 * bound across all participants. Parties are never split.
 * This bounded-policy queue does not optimize global match quality. Incomplete groups remain queued; no ticket is removed twice.
 *
 * Returns: (transfer full) (element-type utf8) (nullable): account IDs, empty if no full group
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_mmo_matchmaker_take (LrgMmoMatchmaker *self,
                         const gchar *mode,
                         guint size,
                         guint spread,
                         gint64 now_us,
                         GError **error);

/**
 * lrg_mmo_matchmaker_enqueue_party:
 * @self: matchmaking queue
 * @members: a(su) account and trusted rating tuples, 1 to 128 unique accounts
 * @mode: game mode ID
 * @now_us: trusted nonnegative monotonic microseconds
 * @error: (nullable): error return
 *
 * Atomically enqueues a whole party. The host verifies membership and participant
 * consent before calling; tuple account IDs are not authentication. Capacity
 * counts accounts, including solo tickets. Duplicate/queued members reject the
 * whole party. Expiry or cancellation of any member removes the whole ticket.
 * Returns: whether queued
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_matchmaker_enqueue_party (LrgMmoMatchmaker *self, GVariant *members,
                                           const gchar *mode, gint64 now_us, GError **error);
/**
 * lrg_mmo_matchmaker_set_ticket_lifetime:
 * @self: the service; confine to its owning thread
 * @lifetime_us: how long a ticket waits before it expires, in microseconds;
 *   at least one second (shorter values are raised to one second)
 *
 * Changes how long new and already queued tickets wait (sixty seconds by
 * default). Battleground-style queues that fill with substitutes after a wait
 * need tickets that outlive that wait.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_matchmaker_set_ticket_lifetime (LrgMmoMatchmaker *self,
                                        gint64            lifetime_us);

/**
 * lrg_mmo_matchmaker_get_ticket_lifetime:
 * @self: the service
 *
 * Returns: the ticket lifetime in microseconds
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_matchmaker_get_ticket_lifetime (LrgMmoMatchmaker *self);

/**
 * lrg_mmo_matchmaker_is_queued:
 * @self: the service
 * @account: account ID
 *
 * Whether @account is in a ticket, as of the last call that took a time
 * (expiry only happens on calls given the current time).
 *
 * Returns: %TRUE when queued
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_matchmaker_is_queued (LrgMmoMatchmaker *self,
                              const gchar      *account);

/**
 * lrg_mmo_matchmaker_get_queued:
 * @self: the service
 * @mode: (nullable): a game mode ID, %NULL for every mode
 *
 * Returns: the accounts queued for @mode
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_matchmaker_get_queued (LrgMmoMatchmaker *self,
                               const gchar      *mode);

/**
 * lrg_mmo_matchmaker_get_wait:
 * @self: the service
 * @account: account ID
 * @now_us: monotonic server microseconds
 *
 * Returns: how long @account's ticket has waited in microseconds, or -1 when
 *   it is not queued
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_matchmaker_get_wait (LrgMmoMatchmaker *self,
                             const gchar      *account,
                             gint64            now_us);

/**
 * lrg_mmo_matchmaker_get_oldest_wait:
 * @self: the service
 * @mode: a game mode ID
 * @now_us: monotonic server microseconds
 *
 * The wait of the oldest ticket of @mode: a host may fill a long-waiting
 * queue with substitutes (bots) of that ticket's rating.
 *
 * Returns: microseconds, or -1 when nothing waits for @mode
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_matchmaker_get_oldest_wait (LrgMmoMatchmaker *self,
                                    const gchar      *mode,
                                    gint64            now_us);

/**
 * lrg_mmo_matchmaker_get_oldest_rating:
 * @self: the service
 * @mode: a game mode ID
 * @out_rating: (out): the mean rating of the oldest ticket of @mode
 * @out_size: (out) (optional): its member count
 *
 * Returns: %TRUE when a ticket of @mode waits
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_matchmaker_get_oldest_rating (LrgMmoMatchmaker *self,
                                      const gchar      *mode,
                                      guint            *out_rating,
                                      guint            *out_size);

/**
 * lrg_mmo_matchmaker_take_teams:
 * @self: the service; confine to its owning thread
 * @mode: game mode ID
 * @team_size: members per team, 1 to 64
 * @teams: number of teams, 2 to 16
 * @spread: maximum difference between the lowest and highest rating of all
 *   participants
 * @now_us: monotonic server microseconds
 * @error: (nullable): return location for error
 *
 * Expires old tickets, then assembles one match of @teams teams of exactly
 * @team_size members from whole FIFO tickets within the rating bound, never
 * splitting a party across teams. Among the packings it tries, larger parties
 * are placed first and each ticket goes to the open team with the lowest rating
 * total, so solo players balance the teams. The search is bounded; like
 * lrg_mmo_matchmaker_take() it does not optimize global match quality.
 * The chosen tickets leave the queue; incomplete matches stay queued.
 *
 * Returns: (transfer full) (element-type GPtrArray) (nullable): one array of
 *   account IDs (utf8) per team, empty when no full match exists, %NULL on a
 *   bad request
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_mmo_matchmaker_take_teams (LrgMmoMatchmaker *self,
                               const gchar      *mode,
                               guint             team_size,
                               guint             teams,
                               guint             spread,
                               gint64            now_us,
                               GError          **error);
G_END_DECLS
