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

/* --- role queues (group finders) --- */

/**
 * LRG_MMO_MATCHMAKER_MAX_ROLES:
 *
 * The most roles a role queue knows (role @i is the bit `1 << i` of a
 * member's role mask).
 */
#define LRG_MMO_MATCHMAKER_MAX_ROLES (8)

/**
 * lrg_mmo_matchmaker_set_timeout:
 * @self: matchmaking queue
 * @timeout_us: how long a ticket stays queued, in microseconds (at least one second)
 *
 * Sets how long tickets wait before they expire (sixty seconds by default).
 * Tickets already queued are measured against the new value.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_matchmaker_set_timeout (LrgMmoMatchmaker *self,
                                gint64            timeout_us);

/**
 * lrg_mmo_matchmaker_get_timeout:
 * @self: matchmaking queue
 *
 * Returns: how long a ticket stays queued, in microseconds
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_matchmaker_get_timeout (LrgMmoMatchmaker *self);

/**
 * lrg_mmo_matchmaker_enqueue_roles:
 * @self: matchmaking queue
 * @members: a(suu) account, trusted rating and role mask tuples, 1 to 128
 *   unique accounts; a mask of 0 takes any role
 * @mode: game mode ID
 * @since_us: when the ticket was first queued (at most @now_us): a ticket
 *   put back after a declined match or a restart keeps its place
 * @now_us: trusted nonnegative monotonic microseconds
 * @error: (nullable): error return
 *
 * Atomically enqueues a whole party for a role queue (see
 * lrg_mmo_matchmaker_take_roles()).  The ticket is placed among the others
 * by @since_us, oldest first.  The rules of lrg_mmo_matchmaker_enqueue_party()
 * apply: duplicate or already queued accounts and capacity overflow fail
 * and change nothing; a ticket whose age reaches the timeout expires.
 *
 * Returns: whether queued
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_matchmaker_enqueue_roles (LrgMmoMatchmaker *self,
                                  GVariant         *members,
                                  const gchar      *mode,
                                  gint64            since_us,
                                  gint64            now_us,
                                  GError          **error);

/**
 * lrg_mmo_matchmaker_set_roles:
 * @self: matchmaking queue
 * @account: a queued account
 * @roles: the new role mask (0: any role)
 * @error: (nullable): error return
 *
 * Changes the roles a queued account offers (a role swap) without moving
 * its ticket in the queue.
 *
 * Returns: whether changed; %G_IO_ERROR_NOT_FOUND when @account is not queued
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_matchmaker_set_roles (LrgMmoMatchmaker *self,
                              const gchar      *account,
                              guint             roles,
                              GError          **error);

/**
 * lrg_mmo_matchmaker_take_roles:
 * @self: matchmaking queue
 * @mode: game mode ID
 * @slots: (array length=n_slots): how many members each role needs
 * @n_slots: number of roles, 1 to %LRG_MMO_MATCHMAKER_MAX_ROLES
 * @spread: maximum difference between the lowest and highest rating
 * @min_size: the fewest members a partial group may have (1 or more)
 * @partial_after_us: a group may start short once its oldest ticket has
 *   waited this long (%G_MAXINT64: never)
 * @now_us: monotonic server microseconds
 * @error: (nullable): error return
 *
 * Expires old tickets, then fills the role slots from whole FIFO tickets:
 * starting with the oldest ticket of @mode, later tickets join while the
 * ratings stay within @spread and every member still gets a slot of a role
 * its mask offers (a maximum bipartite matching, so a flexible member never
 * blocks a strict one).  The first group that fills every slot is taken.
 * When none fills and the oldest ticket of @mode has waited
 * @partial_after_us or longer, its group is taken short if it has at least
 * @min_size members, so the host can fill the empty slots itself (with bots,
 * for instance).  Parties are never split; taken tickets leave the queue.
 *
 * Returns: (transfer full): a(su) of account and role index for the group
 *   taken, an empty array when no group could be formed, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_matchmaker_take_roles (LrgMmoMatchmaker *self,
                               const gchar      *mode,
                               const guint      *slots,
                               guint             n_slots,
                               guint             spread,
                               guint             min_size,
                               gint64            partial_after_us,
                               gint64            now_us,
                               GError          **error);

/**
 * lrg_mmo_matchmaker_is_queued:
 * @self: matchmaking queue
 * @account: account ID
 *
 * Returns: whether @account is in a queued ticket
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_matchmaker_is_queued (LrgMmoMatchmaker *self,
                              const gchar      *account);

/**
 * lrg_mmo_matchmaker_get_ticket:
 * @self: matchmaking queue
 * @account: account ID
 * @mode: (out) (optional) (transfer none): the ticket's mode
 * @since_us: (out) (optional): when the ticket was queued
 * @roles: (out) (optional): the role mask of @account
 *
 * Returns: (transfer full) (element-type utf8) (nullable): the accounts of
 *   the ticket holding @account, in their order, or %NULL when not queued
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_mmo_matchmaker_get_ticket (LrgMmoMatchmaker  *self,
                               const gchar       *account,
                               const gchar      **mode,
                               gint64            *since_us,
                               guint             *roles);

/**
 * lrg_mmo_matchmaker_get_queued:
 * @self: matchmaking queue
 * @mode: (nullable): a game mode ID, or %NULL for every mode
 *
 * Returns: the number of queued accounts (of @mode)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_matchmaker_get_queued (LrgMmoMatchmaker *self,
                               const gchar      *mode);

/**
 * lrg_mmo_matchmaker_list_modes:
 * @self: matchmaking queue
 *
 * Returns: (transfer full) (element-type utf8): the modes that have queued
 *   tickets, in the order of their oldest ticket
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_mmo_matchmaker_list_modes (LrgMmoMatchmaker *self);

/**
 * lrg_mmo_matchmaker_expire:
 * @self: matchmaking queue
 * @now_us: monotonic server microseconds (not earlier than the last call)
 *
 * Drops the tickets that waited as long as the timeout.
 *
 * Returns: (transfer full) (element-type utf8): the accounts whose tickets
 *   expired, for the host to tell them
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_mmo_matchmaker_expire (LrgMmoMatchmaker *self,
                           gint64            now_us);

/**
 * lrg_mmo_matchmaker_save:
 * @self: matchmaking queue
 * @now_us: monotonic server microseconds
 *
 * The queue as a value that survives a restart: a(sxa(suu)) of mode, the
 * ticket's age in microseconds and its members (account, rating, roles),
 * oldest first.  Clocks do not survive a restart, ages do.
 *
 * Returns: (transfer full): the queue
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_matchmaker_save (LrgMmoMatchmaker *self,
                         gint64            now_us);

/**
 * lrg_mmo_matchmaker_load:
 * @self: an empty matchmaking queue
 * @saved: the value of lrg_mmo_matchmaker_save()
 * @now_us: monotonic server microseconds of this process
 * @error: (nullable): error return
 *
 * Queues every saved ticket again with its age.  Everything is checked
 * first: a malformed value, a duplicate account, a queue that is not empty
 * or too small fails and changes nothing.  Tickets that would be expired
 * are dropped.
 *
 * Returns: whether loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_matchmaker_load (LrgMmoMatchmaker *self,
                         GVariant         *saved,
                         gint64            now_us,
                         GError          **error);
/**
 * lrg_mmo_matchmaker_roles_fit:
 * @masks: (array length=n_masks): role masks of a party (0: any role)
 * @n_masks: members of the party
 * @slots: (array length=n_slots): how many members each role needs
 * @n_slots: number of roles, 1 to %LRG_MMO_MATCHMAKER_MAX_ROLES
 *
 * Whether a party could be seated in the slots of a role queue (the check
 * lrg_mmo_matchmaker_take_roles() applies), so a host can refuse a party
 * that would never be matched before queueing it.
 *
 * Returns: whether every member gets a seat of a role it offers
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_matchmaker_roles_fit (const guint *masks,
                              guint        n_masks,
                              const guint *slots,
                              guint        n_slots);
/**
 * lrg_mmo_matchmaker_set_ticket_lifetime:
 * @self: the service; confine to its owning thread
 * @lifetime_us: how long a ticket waits before it expires, in microseconds;
 *   at least one second (shorter values are raised to one second)
 *
 * The same lifetime as lrg_mmo_matchmaker_set_timeout(). Changes how long
 * new and already queued tickets wait (sixty seconds by
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
