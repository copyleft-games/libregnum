/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-mmo-store.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_SOCIAL (lrg_mmo_social_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoSocial, lrg_mmo_social, LRG, MMO_SOCIAL, GObject)

/**
 * lrg_mmo_social_new:
 * @store: transactional store, retained by the service
 *
 * Creates durable guild and private messaging services. Actor identities must come
 * from authentication. Calls perform blocking storage operations.
 *
 * Returns: (transfer full): service
 */
LRG_AVAILABLE_IN_ALL
LrgMmoSocial *
lrg_mmo_social_new (LrgMmoStore *store);

/**
 * lrg_mmo_social_create_guild:
 * @self: the service; confine to its owning thread
 * @actor: authenticated founder
 * @guild: unique guild ID
 * @error: (nullable): return location for error
 *
 * Creates a guild with the founder as its leader. Membership is limited to 128.
 *
 * Returns: whether created
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_social_create_guild (LrgMmoSocial *self,
                             const gchar *actor,
                             const gchar *guild,
                             GError **error);

/**
 * lrg_mmo_social_invite:
 * @self: the service; confine to its owning thread
 * @actor: authenticated leader or officer
 * @guild: guild ID
 * @target: invitee account ID
 * @error: (nullable): return location for error
 *
 * Creates a one-hour invitation. At most 128 pending invitations are retained.
 *
 * Returns: whether invited
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_social_invite (LrgMmoSocial *self,
                       const gchar *actor,
                       const gchar *guild,
                       const gchar *target,
                       GError **error);

/**
 * lrg_mmo_social_join:
 * @self: the service; confine to its owning thread
 * @actor: authenticated invitee
 * @guild: guild ID
 * @error: (nullable): return location for error
 *
 * Consumes a live invitation and adds a member atomically.
 *
 * Returns: whether joined
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_social_join (LrgMmoSocial *self,
                     const gchar *actor,
                     const gchar *guild,
                     GError **error);

/**
 * lrg_mmo_social_set_role:
 * @self: the service; confine to its owning thread
 * @actor: authenticated leader
 * @guild: guild ID
 * @target: existing member, other than actor
 * @role: 0 removes, 1 member, 2 officer, 3 transfers leadership
 * @error: (nullable): return location for error
 *
 * Changes a role with leader authorization. Leadership transfer demotes the old leader to officer.
 *
 * Returns: whether changed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_social_set_role (LrgMmoSocial *self,
                         const gchar *actor,
                         const gchar *guild,
                         const gchar *target,
                         guint role,
                         GError **error);

/**
 * lrg_mmo_social_leave:
 * @self: the service; confine to its owning thread
 * @actor: authenticated member
 * @guild: guild ID
 * @error: (nullable): return location for error
 *
 * Removes a member. A leader with other members must transfer leadership first.
 *
 * Returns: whether left
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_social_leave (LrgMmoSocial *self,
                      const gchar *actor,
                      const gchar *guild,
                      GError **error);

/**
 * lrg_mmo_social_get_guild:
 * @self: the service; confine to its owning thread
 * @guild: guild ID
 * @error: (nullable): return location for error
 *
 * Reads leader, member roles and invitation deadlines. This is a trusted service query;
 * do not expose invitation/account data to unauthorized clients.
 *
 * Returns: (transfer full) (nullable): (sa{su}a{sx}) record
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_social_get_guild (LrgMmoSocial *self,
                          const gchar *guild,
                          GError **error);

/**
 * lrg_mmo_social_block:
 * @self: the service; confine to its owning thread
 * @actor: authenticated account
 * @target: account to block or unblock
 * @blocked: new state
 * @error: (nullable): return location for error
 *
 * Updates a durable private-message block.
 *
 * Returns: whether updated
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_social_block (LrgMmoSocial *self,
                      const gchar *actor,
                      const gchar *target,
                      gboolean blocked,
                      GError **error);

/**
 * lrg_mmo_social_send_message:
 * @self: the service; confine to its owning thread
 * @actor: authenticated sender
 * @recipient: destination account
 * @text: nonempty UTF-8 text, at most 2048 bytes
 * @operation: durable idempotency key
 * @error: (nullable): return location for error
 *
 * Stores an offline-capable private message after checking blocks in both directions.
 * The recipient inbox retains the latest 200 messages; ingress rate limits remain required.
 *
 * Returns: whether delivered or an exact retry
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_social_send_message (LrgMmoSocial *self,
                             const gchar *actor,
                             const gchar *recipient,
                             const gchar *text,
                             const gchar *operation,
                             GError **error);

/**
 * lrg_mmo_social_read_inbox:
 * @self: the service; confine to its owning thread
 * @actor: authenticated inbox owner
 * @error: (nullable): return location for error
 *
 * Reads ordered (operation ID, sender, text) messages for the authenticated account.
 *
 * Returns: (transfer full) (nullable): a(sss) inbox
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_social_read_inbox (LrgMmoSocial *self,
                           const gchar *actor,
                           GError **error);

G_END_DECLS
