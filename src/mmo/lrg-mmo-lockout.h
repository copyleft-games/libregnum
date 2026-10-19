/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../core/lrg-reset-schedule.h"

G_BEGIN_DECLS

#define LRG_TYPE_MMO_LOCKOUT (lrg_mmo_lockout_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoLockout, lrg_mmo_lockout, LRG, MMO_LOCKOUT, GObject)

/**
 * LRG_MMO_LOCKOUT_VARIANT_TYPE:
 *
 * The GVariant type of a persisted lockout book: an array of
 * (account, key, period index).
 */
#define LRG_MMO_LOCKOUT_VARIANT_TYPE "a(ssx)"

/**
 * lrg_mmo_lockout_new:
 * @schedule: (nullable): the reset schedule whose boundaries end a lockout
 *   (referenced); %NULL for a weekly schedule at 00:00 UTC on Tuesday
 * @period: daily or weekly cadence of @schedule
 *
 * A book of per-account, per-key lockouts for content that may be done
 * once per reset period (a weekly world boss's loot, a daily dungeon).
 * A lock made at time T lasts until the first boundary of @schedule after
 * T, whatever the lock's length in seconds.  Every query takes the time
 * as Unix seconds from the caller (an MMO server passes its trusted
 * clock), so the book never reads a clock and tests are exact.  Thread
 * confined; serialise access.
 *
 * Returns: (transfer full): a new #LrgMmoLockout
 */
LRG_AVAILABLE_IN_ALL
LrgMmoLockout *lrg_mmo_lockout_new (LrgResetSchedule *schedule,
                                    LrgResetPeriod    period);

/**
 * lrg_mmo_lockout_get_schedule:
 * @self: an #LrgMmoLockout
 *
 * Returns: (transfer none): the book's reset schedule
 */
LRG_AVAILABLE_IN_ALL
LrgResetSchedule *lrg_mmo_lockout_get_schedule (LrgMmoLockout *self);

/**
 * lrg_mmo_lockout_get_period:
 * @self: an #LrgMmoLockout
 *
 * Returns: the cadence the book was made with
 */
LRG_AVAILABLE_IN_ALL
LrgResetPeriod lrg_mmo_lockout_get_period (LrgMmoLockout *self);

/**
 * lrg_mmo_lockout_is_locked:
 * @self: an #LrgMmoLockout
 * @account: an account id
 * @key: what is locked (a boss, a dungeon), 1 to 128 bytes
 * @now: Unix seconds
 *
 * Returns: whether @account is locked out of @key at @now: it was locked
 *   in the reset period @now is in
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_lockout_is_locked (LrgMmoLockout *self,
                                    const gchar   *account,
                                    const gchar   *key,
                                    gint64         now);

/**
 * lrg_mmo_lockout_lock:
 * @self: an #LrgMmoLockout
 * @account: an account id, 1 to 128 bytes
 * @key: what is locked, 1 to 128 bytes
 * @now: Unix seconds
 *
 * Locks @account out of @key until the next reset boundary after @now.
 * A lock made in an earlier period is replaced by the new one.
 *
 * Returns: %TRUE when the lock is new; %FALSE when @account already was
 *   locked at @now (nothing changes) or an argument is invalid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_lockout_lock (LrgMmoLockout *self,
                               const gchar   *account,
                               const gchar   *key,
                               gint64         now);

/**
 * lrg_mmo_lockout_unlock:
 * @self: an #LrgMmoLockout
 * @account: an account id
 * @key: what was locked
 *
 * Lifts a lock (an administrator's mercy, a refunded run).
 *
 * Returns: whether a lock was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_lockout_unlock (LrgMmoLockout *self,
                                 const gchar   *account,
                                 const gchar   *key);

/**
 * lrg_mmo_lockout_get_reset:
 * @self: an #LrgMmoLockout
 * @account: an account id
 * @key: what is locked
 * @now: Unix seconds
 *
 * Returns: the Unix second the lock ends at, 0 when @account is not
 *   locked out of @key at @now
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_mmo_lockout_get_reset (LrgMmoLockout *self,
                                  const gchar   *account,
                                  const gchar   *key,
                                  gint64         now);

/**
 * lrg_mmo_lockout_list:
 * @self: an #LrgMmoLockout
 * @account: an account id
 * @now: Unix seconds
 *
 * Returns: (transfer full): an `a(sx)` of the keys @account is locked
 *   out of at @now with the time each lock ends, sorted by key
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_lockout_list (LrgMmoLockout *self,
                                const gchar   *account,
                                gint64         now);

/**
 * lrg_mmo_lockout_purge:
 * @self: an #LrgMmoLockout
 * @now: Unix seconds
 *
 * Drops the locks of earlier periods (they no longer lock anything).
 *
 * Returns: how many locks were dropped
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mmo_lockout_purge (LrgMmoLockout *self,
                             gint64         now);

/**
 * lrg_mmo_lockout_get_count:
 * @self: an #LrgMmoLockout
 *
 * Returns: how many locks the book holds (expired ones included until
 *   they are purged)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mmo_lockout_get_count (LrgMmoLockout *self);

/**
 * lrg_mmo_lockout_clear:
 * @self: an #LrgMmoLockout
 *
 * Forgets every lock.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mmo_lockout_clear (LrgMmoLockout *self);

/**
 * lrg_mmo_lockout_to_variant:
 * @self: an #LrgMmoLockout
 *
 * Returns: (transfer full): the book as `a(ssx)` (account, key, period
 *   index), sorted so equal books give equal bytes
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_lockout_to_variant (LrgMmoLockout *self);

/**
 * lrg_mmo_lockout_restore:
 * @self: an #LrgMmoLockout
 * @state: (nullable): a value from lrg_mmo_lockout_to_variant()
 * @error: (nullable): error return
 *
 * Replaces the book with @state.  A wrong type, an empty or over-long
 * name or a duplicated (account, key) is refused as a whole and the book
 * stays as it was.
 *
 * Returns: whether the book was replaced
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_lockout_restore (LrgMmoLockout *self,
                                  GVariant      *state,
                                  GError       **error);

G_END_DECLS
