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

G_END_DECLS
