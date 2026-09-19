/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-mmo-store.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_AUTH (lrg_mmo_auth_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoAuth, lrg_mmo_auth, LRG, MMO_AUTH, GObject)

/**
 * lrg_mmo_auth_new:
 * @store: transactional store, retained by the service
 *
 * Creates a persistent credential service. Password work and storage are blocking;
 * call on a worker thread. The caller must enforce ingress rate limits.
 *
 * Returns: (transfer full): service
 */
LRG_AVAILABLE_IN_ALL
LrgMmoAuth *
lrg_mmo_auth_new (LrgMmoStore *store);

/**
 * lrg_mmo_auth_register:
 * @self: the service; confine to its owning thread
 * @account: ASCII account ID, 1 to 64 letters, digits, underscores or hyphens
 * @password: UTF-8 password, 12 to 1024 bytes
 * @error: (nullable): return location for error
 *
 * Creates a credential using a random salt and PBKDF2-HMAC-SHA256 (600000 iterations).
 *
 * Returns: whether created
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_auth_register (LrgMmoAuth *self,
                       const gchar *account,
                       const gchar *password,
                       GError **error);

/**
 * lrg_mmo_auth_login:
 * @self: the service; confine to its owning thread
 * @account: ASCII account ID, 1 to 64 letters, digits, underscores or hyphens
 * @password: UTF-8 password, 12 to 1024 bytes
 * @now: trusted server Unix time in seconds
 * @error: (nullable): return location for error
 *
 * Verifies credentials and issues a cryptographically random one-hour token.
 *
 * Returns: (transfer full) (nullable): token, or NULL
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_mmo_auth_login (LrgMmoAuth *self,
                    const gchar *account,
                    const gchar *password,
                    gint64 now,
                    GError **error);

/**
 * lrg_mmo_auth_verify:
 * @self: the service; confine to its owning thread
 * @token: opaque bearer token; never log it
 * @now: trusted server Unix time in seconds
 * @error: (nullable): return location for error
 *
 * Checks token expiry, account generation and ban status. Tokens are stored only as hashes.
 *
 * Returns: (transfer full) (nullable): authenticated account, or NULL
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_mmo_auth_verify (LrgMmoAuth *self,
                     const gchar *token,
                     gint64 now,
                     GError **error);

/**
 * lrg_mmo_auth_rotate:
 * @self: the service; confine to its owning thread
 * @token: opaque bearer token; never log it
 * @now: trusted server Unix time in seconds
 * @error: (nullable): return location for error
 *
 * Atomically revokes the old bearer token and issues a new one-hour token.
 *
 * Returns: (transfer full) (nullable): replacement token
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_mmo_auth_rotate (LrgMmoAuth *self,
                     const gchar *token,
                     gint64 now,
                     GError **error);

/**
 * lrg_mmo_auth_revoke:
 * @self: the service; confine to its owning thread
 * @token: opaque bearer token; never log it
 * @error: (nullable): return location for error
 *
 * Revokes a token. Repeated revocation succeeds.
 *
 * Returns: whether revoked
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_auth_revoke (LrgMmoAuth *self,
                     const gchar *token,
                     GError **error);

/**
 * lrg_mmo_auth_set_banned:
 * @self: the service; confine to its owning thread
 * @account: ASCII account ID, 1 to 64 letters, digits, underscores or hyphens
 * @banned: new ban state
 * @error: (nullable): return location for error
 *
 * Trusted administration operation. Changes account generation, invalidating existing tokens.
 *
 * Returns: whether updated
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_auth_set_banned (LrgMmoAuth *self,
                         const gchar *account,
                         gboolean banned,
                         GError **error);

/**
 * lrg_mmo_auth_begin_recovery:
 * @self: the service; confine to its owning thread
 * @account: ASCII account ID, 1 to 64 letters, digits, underscores or hyphens
 * @now: trusted server Unix time in seconds
 * @error: (nullable): return location for error
 *
 * Trusted administration operation after out-of-band identity verification.
 * Issues a one-use fifteen-minute reset capability; deliver it over a trusted channel.
 *
 * Returns: (transfer full) (nullable): recovery capability
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_mmo_auth_begin_recovery (LrgMmoAuth *self,
                             const gchar *account,
                             gint64 now,
                             GError **error);

/**
 * lrg_mmo_auth_recover:
 * @self: the service; confine to its owning thread
 * @token: opaque bearer token; never log it
 * @password: UTF-8 password, 12 to 1024 bytes
 * @now: trusted server Unix time in seconds
 * @error: (nullable): return location for error
 *
 * Consumes a recovery capability and atomically changes credentials, invalidating old sessions.
 *
 * Returns: whether recovered
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_auth_recover (LrgMmoAuth *self,
                      const gchar *token,
                      const gchar *password,
                      gint64 now,
                      GError **error);

/**
 * lrg_mmo_auth_generate_totp_secret:
 * @error: (nullable): return location for error
 *
 * Generates a secret for private authenticator provisioning. Never log or publish.
 * Returns: (transfer full) (nullable): 20 random bytes
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_mmo_auth_generate_totp_secret (GError **error);
/**
 * lrg_mmo_auth_set_totp:
 * @self: auth service
 * @account: account ID
 * @secret: (nullable): private 20 to 64 byte secret; NULL disables TOTP
 * @confirmation: six-digit TOTP enrollment confirmation as an integer
 * @now: trusted Unix seconds
 * @error: (nullable): return location for error
 *
 * Trusted administration after reauthentication: enables/replaces/disables TOTP
 * and invalidates existing tokens atomically. Uses HMAC-SHA1, six digits and
 * 30-second steps with one-step skew. Confirmation consumes its time step.
 * Database backups contain this secret and must be protected accordingly.
 * Returns: whether updated
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_auth_set_totp (LrgMmoAuth *self, const gchar *account, GBytes *secret,
                                guint confirmation, gint64 now, GError **error);
/**
 * lrg_mmo_auth_login_totp:
 * @self: auth service
 * @account: account ID
 * @password: account password
 * @code: six-digit TOTP as an integer
 * @now: trusted Unix seconds
 * @error: (nullable): return location for error
 *
 * Checks password and configured TOTP, consuming a counter with CAS before
 * issuing a token. Replayed codes reject across connections. Apply ingress rate
 * limits. A failure after consumption can require waiting for the next code.
 * Password-only login rejects accounts with TOTP enabled.
 * Returns: (transfer full) (nullable): bearer token
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_mmo_auth_login_totp (LrgMmoAuth *self, const gchar *account, const gchar *password,
                               guint code, gint64 now, GError **error);
/**
 * lrg_mmo_auth_begin_address:
 * @self: auth service
 * @account: account authenticated by recent password/MFA reauthentication
 * @address: ASCII mailbox, up to 254 bytes
 * @now: trusted Unix seconds
 * @error: (nullable): error return
 *
 * Trusted delivery operation. Send the returned one-use proof only to @address;
 * never return it to the enrollment requester. Confirmation invalidates existing
 * tokens and recovery capabilities. Gate requests before calling this method.
 * Returns: (transfer full) (nullable): private fifteen-minute address proof
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_mmo_auth_begin_address (LrgMmoAuth *self, const gchar *account,
                                   const gchar *address, gint64 now, GError **error);
/**
 * lrg_mmo_auth_confirm_address:
 * @self: auth service
 * @proof: secret received through the proposed mailbox
 * @now: trusted Unix seconds
 * @error: (nullable): error return
 * Returns: whether the verified address was bound atomically
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_auth_confirm_address (LrgMmoAuth *self, const gchar *proof, gint64 now, GError **error);
/**
 * lrg_mmo_auth_prepare_recovery:
 * @self: trusted delivery service
 * @account: requested account
 * @now: trusted Unix seconds
 * @error: (nullable): error return
 *
 * Issues a recovery capability bound to the current verified mailbox and account
 * generation. Atomically checks both revisions and enforces a sixty-second
 * account cooldown. The host must send through its configured provider and give
 * requesters a generic response regardless of account existence or delivery.
 * Delivery failure consumes the cooldown; retry later. No plaintext token is
 * persisted. This return value must never reach the unauthenticated requester.
 * Returns: (transfer full) (nullable): private (ss) mailbox and reset capability
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_auth_prepare_recovery (LrgMmoAuth *self, const gchar *account, gint64 now, GError **error);
/**
 * lrg_mmo_auth_moderate:
 * @self: trusted administration service
 * @operator_id: authenticated administrator identifier for the audit
 * @account: target account
 * @banned: desired ban state
 * @reason: nonempty UTF-8 evidence/reason, up to 2048 bytes
 * @operation: stable unique moderation operation ID
 * @error: (nullable): error return
 *
 * Atomically changes the ban/generation and writes a durable moderation event
 * and retry receipt. This is privileged just like set_banned(); operator_id is
 * audit attribution, not authorization. Restrict invocation to trusted operators.
 * Returns: whether applied or recognized as an exact retry
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_auth_moderate (LrgMmoAuth *self, const gchar *operator_id, const gchar *account,
                                gboolean banned, const gchar *reason, const gchar *operation, GError **error);
G_END_DECLS
