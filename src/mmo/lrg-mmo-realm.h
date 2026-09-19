/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_REALM (lrg_mmo_realm_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoRealm, lrg_mmo_realm, LRG, MMO_REALM, GObject)

/**
 * lrg_mmo_realm_new:
 * @capacity: maximum simultaneous sessions
 * @timeout_us: idle timeout in monotonic microseconds
 *
 * Creates a single-thread-owned realm. Authentication is performed by the host.
 *
 * Returns: (transfer full): a realm
 */
LRG_AVAILABLE_IN_ALL
LrgMmoRealm *
lrg_mmo_realm_new (guint capacity,
                   gint64 timeout_us);

/**
 * lrg_mmo_realm_add_zone:
 * @self: the instance
 * @zone: unique zone ID
 * @capacity: maximum occupants
 * @error: (nullable): return location for an error
 *
 * Registers a zone; duplicate IDs and zero capacity are rejected.
 *
 * Returns: whether the zone was added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_realm_add_zone (LrgMmoRealm *self,
                        const gchar *zone,
                        guint capacity,
                        GError **error);

/**
 * lrg_mmo_realm_login:
 * @self: the instance
 * @peer_id: nonzero transport peer ID
 * @account: verified account ID, supplied by the trusted host
 * @now_us: nonnegative monotonic time
 * @error: (nullable): return location for an error
 *
 * Admits one session per account and peer. Never pass an unverified client identity.
 *
 * Returns: whether admitted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_realm_login (LrgMmoRealm *self,
                     guint32 peer_id,
                     const gchar *account,
                     gint64 now_us,
                     GError **error);

/**
 * lrg_mmo_realm_logout:
 * @self: the instance
 * @peer_id: transport peer ID
 *
 * Removes a session, releasing zone occupancy. Emits session-ended after removal.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_realm_logout (LrgMmoRealm *self,
                      guint32 peer_id);

/**
 * lrg_mmo_realm_enter_zone:
 * @self: the instance
 * @peer_id: session peer ID
 * @zone: destination zone ID
 * @error: (nullable): return location for an error
 *
 * Moves a session atomically after checking destination capacity.
 *
 * Returns: whether moved
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_realm_enter_zone (LrgMmoRealm *self,
                          guint32 peer_id,
                          const gchar *zone,
                          GError **error);

/**
 * lrg_mmo_realm_get_zone:
 * @self: the instance
 * @peer_id: session peer ID
 *
 * Looks up the current zone. The result lasts until the next realm mutation.
 *
 * Returns: (transfer none) (nullable): zone ID, or NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_mmo_realm_get_zone (LrgMmoRealm *self,
                        guint32 peer_id);

/**
 * lrg_mmo_realm_get_account:
 * @self: the instance
 * @peer_id: session peer ID
 *
 * Looks up the verified identity. The result lasts until the next realm mutation.
 *
 * Returns: (transfer none) (nullable): account ID, or NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_mmo_realm_get_account (LrgMmoRealm *self,
                           guint32 peer_id);

/**
 * lrg_mmo_realm_accept_command:
 * @self: the instance
 * @peer_id: transport peer ID
 * @sequence: strictly increasing nonzero command sequence
 * @now_us: nondecreasing monotonic time
 * @error: (nullable): return location for an error
 *
 * Checks idle expiry, replay and a token bucket (burst 100, refill 50/second).
 * Successful admission refreshes the idle deadline. Gameplay authorization remains the host's responsibility.
 *
 * Returns: whether the command may be processed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_realm_accept_command (LrgMmoRealm *self,
                              guint32 peer_id,
                              guint64 sequence,
                              gint64 now_us,
                              GError **error);

/**
 * lrg_mmo_realm_expire:
 * @self: the instance
 * @now_us: monotonic time
 *
 * Removes idle sessions and emits session-ended for each.
 *
 * Returns: number removed
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_realm_expire (LrgMmoRealm *self,
                      gint64 now_us);

/**
 * lrg_mmo_realm_advance:
 * @self: the instance
 * @elapsed: finite elapsed seconds, at least zero
 *
 * Runs fixed 50 ms ticks, emitting tick with tick number and step seconds.
 * At most eight ticks run per call; excess whole ticks are dropped to bound catch-up.
 * Recursive calls during tick emission are ignored.
 *
 * Returns: number of ticks run
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_realm_advance (LrgMmoRealm *self,
                       gdouble elapsed);

/**
 * lrg_mmo_realm_get_session_count:
 * @self: the instance
 *
 * Gets the number of admitted sessions.
 *
 * Returns: session count
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_realm_get_session_count (LrgMmoRealm *self);

G_END_DECLS
